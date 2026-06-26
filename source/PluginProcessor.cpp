#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace pid = mf::pid;

MasterForgeAudioProcessor::MasterForgeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    bypassParam   = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (pid::bypass));
    truePeakParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (pid::limTruePeak));
    jassert (bypassParam != nullptr && truePeakParam != nullptr);
}

bool MasterForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono()
        && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == mainOut;
}

void MasterForgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());

    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    spec.numChannels      = (juce::uint32) numCh;

    inputGain.prepare (spec);
    inputGain.setRampDurationSeconds (0.02);
    outputGain.prepare (spec);
    outputGain.setRampDurationSeconds (0.02);

    eq.prepare (spec);
    multiband.prepare (spec);
    stereoWidth.prepare (spec);
    limiter.prepare (spec);
    meter.prepare (sampleRate, numCh);
    analyzer.prepare (sampleRate);

    // Boundary scratch for the float <-> double conversion and the float meters.
    const int scratchCh = juce::jmax (numCh, getTotalNumInputChannels());
    const int maxBlock  = juce::jmax (1, samplesPerBlock);
    doubleScratch.setSize (scratchCh, maxBlock);
    meterScratch.setSize  (scratchCh, maxBlock);

    lastTruePeak = truePeakParam->get();
    lastLinear   = apvts.getRawParameterValue (pid::eqLinear)->load() > 0.5f;
    prepareQuality (apvts.getRawParameterValue (pid::hqMode)->load() > 0.5f);
}

void MasterForgeAudioProcessor::prepareQuality (bool hq)
{
    const int stages = hq ? 4 : 2; // 16x (HQ) or 4x

    saturation.prepare (spec, stages);

    oversampler = std::make_unique<juce::dsp::Oversampling<Real>> (
        (size_t) spec.numChannels, (size_t) stages,
        juce::dsp::Oversampling<Real>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) spec.maximumBlockSize);
    oversampler->reset();
    osFactor = (int) oversampler->getOversamplingFactor();

    juce::dsp::ProcessSpec osSpec;
    osSpec.sampleRate       = spec.sampleRate * osFactor;
    osSpec.maximumBlockSize = spec.maximumBlockSize * (juce::uint32) osFactor;
    osSpec.numChannels      = spec.numChannels;
    limiterOS.prepare (osSpec);

    lastHq = hq;
    updateParameters();
    setLatencySamples (computeLatencySamples (truePeakParam->get()));
}

int MasterForgeAudioProcessor::computeLatencySamples (bool truePeak)
{
    int lat = saturation.getLatencySamples() + eq.getLatencySamples();

    if (truePeak && oversampler != nullptr)
        lat += (int) std::round (oversampler->getLatencyInSamples())
             + limiterOS.getLatencySamples() / juce::jmax (1, osFactor);
    else
        lat += limiter.getLatencySamples();

    return lat;
}

void MasterForgeAudioProcessor::updateParameters()
{
    const auto val = [this] (const char* id)
    {
        return apvts.getRawParameterValue (id)->load();
    };

    inputGain.setGainDecibels  (val (pid::inputGain));
    outputGain.setGainDecibels (val (pid::outputGain));

    eq.setParameters (val (pid::eqLowFreq),  val (pid::eqLowGain),
                      val (pid::eqLmFreq),   val (pid::eqLmGain),  val (pid::eqLmQ),
                      val (pid::eqHmFreq),   val (pid::eqHmGain),  val (pid::eqHmQ),
                      val (pid::eqHighFreq), val (pid::eqHighGain),
                      val (pid::eqLinear) > 0.5f);

    multiband.setParameters (val (pid::mbXLow), val (pid::mbXHigh),
                             val (pid::compAttack), val (pid::compRelease), val (pid::compKnee),
                             val (pid::mbLowThresh), val (pid::mbLowRatio), val (pid::mbLowMakeup),
                             val (pid::mbMidThresh), val (pid::mbMidRatio), val (pid::mbMidMakeup),
                             val (pid::mbHiThresh),  val (pid::mbHiRatio),  val (pid::mbHiMakeup));

    saturation.setThd (val (pid::thd));
    stereoWidth.setWidth (val (pid::width));

    const float ceiling = val (pid::limCeiling);
    const float release = val (pid::limRelease);
    limiter.setParameters (ceiling, release);
    limiterOS.setParameters (ceiling, release);
}

void MasterForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    // Up-convert the float host block into the double scratch, run the chain at
    // 64-bit, then write the result back down to the host buffer.
    juce::AudioBuffer<Real> work (doubleScratch.getArrayOfWritePointers(), numCh, n);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* src = buffer.getReadPointer (ch);
        auto* dst       = work.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            dst[i] = (Real) src[i];
    }

    processChain (work);

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* src = work.getReadPointer (ch);
        auto* dst       = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            dst[i] = (float) src[i];
    }

    publishMeters (buffer);
}

void MasterForgeAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer,
                                              juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // A double-precision host feeds us double directly — no boundary conversion.
    processChain (buffer);

    // The meters / analyzer are single precision: hand them a float copy.
    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();
    juce::AudioBuffer<float> fb (meterScratch.getArrayOfWritePointers(), numCh, n);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* src = buffer.getReadPointer (ch);
        auto* dst       = fb.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            dst[i] = (float) src[i];
    }

    publishMeters (fb);
}

void MasterForgeAudioProcessor::processChain (juce::AudioBuffer<Real>& buffer)
{
    updateParameters();

    // Quality-mode changes: re-prepare the oversampled stages when HQ toggles,
    // and refresh reported latency when HQ / linear-phase changes.
    const bool hq = apvts.getRawParameterValue (pid::hqMode)->load() > 0.5f;
    if (hq != lastHq)
        prepareQuality (hq);

    const bool linear = apvts.getRawParameterValue (pid::eqLinear)->load() > 0.5f;
    if (linear != lastLinear)
    {
        lastLinear = linear;
        setLatencySamples (computeLatencySamples (truePeakParam->get()));
    }

    if (bypassParam->get())
    {
        compLowDb.store (0.0f);
        compMidDb.store (0.0f);
        compHiDb.store  (0.0f);
        limReductionDb.store (0.0f);
        return;                               // signal passes through untouched
    }

    juce::dsp::AudioBlock<Real> block (buffer);
    juce::dsp::ProcessContextReplacing<Real> context (block);

    inputGain.process (context);
    eq.process (buffer, (int) apvts.getRawParameterValue (pid::eqMode)->load());

    multiband.process (buffer);
    compLowDb.store (multiband.getReductionLow());
    compMidDb.store (multiband.getReductionMid());
    compHiDb.store  (multiband.getReductionHigh());

    saturation.process (buffer);
    stereoWidth.process (buffer);
    outputGain.process (context);

    // --- limiter (with optional true-peak oversampling) ---
    const bool truePeak = truePeakParam->get();
    if (truePeak != lastTruePeak)
    {
        setLatencySamples (computeLatencySamples (truePeak));
        if (oversampler != nullptr)
            oversampler->reset();
        lastTruePeak = truePeak;
    }

    float limGr = 0.0f;
    if (truePeak && oversampler != nullptr)
    {
        juce::dsp::AudioBlock<Real> baseBlock (buffer);
        auto osBlock = oversampler->processSamplesUp (baseBlock);

        const int osCh = (int) osBlock.getNumChannels();
        const int osN  = (int) osBlock.getNumSamples();
        Real* ptrs[8] = {};
        for (int c = 0; c < osCh && c < 8; ++c)
            ptrs[c] = osBlock.getChannelPointer ((size_t) c);

        juce::AudioBuffer<Real> osBuffer (ptrs, osCh, osN);
        limGr = limiterOS.process (osBuffer);

        oversampler->processSamplesDown (baseBlock);
    }
    else
    {
        limGr = limiter.process (buffer);
    }
    limReductionDb.store (limGr);
}

void MasterForgeAudioProcessor::publishMeters (const juce::AudioBuffer<float>& buffer)
{
    // Metering / analysis on the output (works in bypass too, showing dry level).
    meter.process (buffer);
    analyzer.pushBuffer (buffer);

    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();
    const float peakL = numCh > 0 ? buffer.getMagnitude (0, 0, numSamples) : 0.0f;
    const float peakR = numCh > 1 ? buffer.getMagnitude (1, 0, numSamples) : peakL;
    outPeakLDb.store (juce::Decibels::gainToDecibels (peakL, -100.0f));
    outPeakRDb.store (juce::Decibels::gainToDecibels (peakR, -100.0f));

    // Stereo correlation (mono-compatibility): +1 mono, 0 wide, -1 out of phase.
    if (numCh > 1 && numSamples > 0)
    {
        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);
        double sLR = 0.0, sLL = 0.0, sRR = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            sLR += (double) l[i] * r[i];
            sLL += (double) l[i] * l[i];
            sRR += (double) r[i] * r[i];
        }
        const double denom = std::sqrt (sLL * sRR);
        const float c = denom > 1.0e-9 ? (float) (sLR / denom) : 1.0f;
        correlation.store (0.85f * correlation.load() + 0.15f * c);
    }
    else
    {
        correlation.store (1.0f);
    }
}

juce::AudioProcessorEditor* MasterForgeAudioProcessor::createEditor()
{
    return new MasterForgeAudioProcessorEditor (*this);
}

void MasterForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MasterForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterForgeAudioProcessor();
}
