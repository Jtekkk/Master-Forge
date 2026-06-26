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

    lastTruePeak = truePeakParam->get();
    lastLinear   = apvts.getRawParameterValue (pid::eqLinear)->load() > 0.5f;
    prepareQuality (apvts.getRawParameterValue (pid::hqMode)->load() > 0.5f);
}

void MasterForgeAudioProcessor::prepareQuality (bool hq)
{
    const int stages = hq ? 4 : 2; // 16x (HQ) or 4x

    saturation.prepare (spec, stages);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) spec.numChannels, (size_t) stages,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
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
    }
    else
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);

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
            juce::dsp::AudioBlock<float> baseBlock (buffer);
            auto osBlock = oversampler->processSamplesUp (baseBlock);

            const int osCh = (int) osBlock.getNumChannels();
            const int osN  = (int) osBlock.getNumSamples();
            float* ptrs[8] = {};
            for (int c = 0; c < osCh && c < 8; ++c)
                ptrs[c] = osBlock.getChannelPointer ((size_t) c);

            juce::AudioBuffer<float> osBuffer (ptrs, osCh, osN);
            limGr = limiterOS.process (osBuffer);

            oversampler->processSamplesDown (baseBlock);
        }
        else
        {
            limGr = limiter.process (buffer);
        }
        limReductionDb.store (limGr);
    }

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
