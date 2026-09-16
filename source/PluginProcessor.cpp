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
    meter.prepare (sampleRate, numCh);
    analyzer.prepare (sampleRate);

    // Both quality paths, ready to switch between without allocating later.
    // The limiter oversamples its *detector* only - the audio path stays at the
    // base rate, so true-peak mode costs no extra filtering of the signal.
    saturationStd.prepare (spec, 2);   // 4x
    saturationHq .prepare (spec, 4);   // 16x
    limiterStd   .prepare (spec, 2);
    limiterHq    .prepare (spec, 4);

    // Boundary scratch for the float <-> double conversion and the float meters.
    const int scratchCh = juce::jmax (numCh, getTotalNumInputChannels());
    const int maxBlock  = juce::jmax (1, samplesPerBlock);
    doubleScratch.setSize (scratchCh, maxBlock);
    meterScratch.setSize  (scratchCh, maxBlock);
    dryScratch.setSize    (scratchCh, maxBlock);

    bypassMix.reset (sampleRate, 0.02);
    bypassMix.setCurrentAndTargetValue (bypassParam->get() ? (Real) 1 : (Real) 0);

    // The dry-bypass delay has to cover the longest latency the chain can
    // report, whichever quality path and EQ mode are selected.
    const int maxLatency = juce::jmax (saturationStd.getLatencySamples(),
                                       saturationHq.getLatencySamples())
                         + mf::ParametricEQT<Real>::getMaxLatencySamples()
                         + juce::jmax (limiterStd.getLatencySamples(),
                                       limiterHq.getLatencySamples());
    dryDelay.setSize (numCh, maxLatency + maxBlock + 4);
    dryDelay.clear();
    dryWritePos = 0;

    lastLinear = apvts.getRawParameterValue (pid::eqLinear)->load() > 0.5f;

    reportedLatency = -1;
    selectQuality (apvts.getRawParameterValue (pid::hqMode)->load() > 0.5f);
    updateParameters();
}

void MasterForgeAudioProcessor::selectQuality (bool hq)
{
    auto* nextSat = hq ? &saturationHq : &saturationStd;
    auto* nextLim = hq ? &limiterHq    : &limiterStd;

    if (nextSat != saturation || nextLim != limiter)
    {
        // The path we are leaving has been sitting idle, so start the new one
        // from a clean state rather than from stale history.
        nextSat->reset();
        nextLim->reset();
        saturation = nextSat;
        limiter    = nextLim;
    }

    lastHq = hq;
    refreshLatency();
}

int MasterForgeAudioProcessor::computeLatencySamples() const
{
    return saturation->getLatencySamples()
         + eq.getLatencySamples()
         + limiter->getLatencySamples();
}

void MasterForgeAudioProcessor::refreshLatency()
{
    const int latency = computeLatencySamples();
    if (latency == reportedLatency)
        return;

    reportedLatency = latency;
    setLatencySamples (latency);
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

    // Both quality paths are kept in sync so a switch is seamless.
    const float thdAmount = val (pid::thd);
    saturationStd.setThd (thdAmount);
    saturationHq .setThd (thdAmount);

    stereoWidth.setWidth (val (pid::width));

    const float ceiling = val (pid::limCeiling);
    const float release = val (pid::limRelease);
    const bool  tp      = truePeakParam->get();
    limiterStd.setParameters (ceiling, release);
    limiterHq .setParameters (ceiling, release);
    limiterStd.setTruePeak (tp);
    limiterHq .setTruePeak (tp);
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

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    const bool hq = apvts.getRawParameterValue (pid::hqMode)->load() > 0.5f;
    if (hq != lastHq)
        selectQuality (hq);

    const bool linear = apvts.getRawParameterValue (pid::eqLinear)->load() > 0.5f;
    if (linear != lastLinear)
    {
        lastLinear = linear;
        refreshLatency();
    }

    // --- dry path: the input, delayed by exactly the latency we report -------
    const int delaySize = dryDelay.getNumSamples();
    const int dryTaps   = juce::jlimit (0, juce::jmax (0, delaySize - 1), reportedLatency);
    const int dryCh     = juce::jmin (numCh, dryDelay.getNumChannels());

    juce::AudioBuffer<Real> dry (dryScratch.getArrayOfWritePointers(), numCh, n);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* src = buffer.getReadPointer (ch);
        auto* out       = dry.getWritePointer (ch);

        if (ch < dryCh && delaySize > 0)
        {
            auto* line = dryDelay.getWritePointer (ch);
            int w = dryWritePos;
            for (int i = 0; i < n; ++i)
            {
                int r = w - dryTaps;
                if (r < 0) r += delaySize;
                line[w] = src[i];
                out[i]  = line[r];
                if (++w >= delaySize) w = 0;
            }
        }
        else
        {
            for (int i = 0; i < n; ++i)
                out[i] = src[i];
        }
    }
    if (delaySize > 0)
        dryWritePos = (dryWritePos + n) % delaySize;

    // --- wet path: always processed, so bypass is a clean A/B ---------------
    juce::dsp::AudioBlock<Real> block (buffer);
    juce::dsp::ProcessContextReplacing<Real> context (block);

    inputGain.process (context);
    eq.process (buffer, (int) apvts.getRawParameterValue (pid::eqMode)->load());

    multiband.process (buffer);
    saturation->process (buffer);
    stereoWidth.process (buffer);
    outputGain.process (context);

    const float limGr = limiter->process (buffer);

    // --- bypass crossfade ---------------------------------------------------
    bypassMix.setTargetValue (bypassParam->get() ? (Real) 1 : (Real) 0);

    const bool fullyBypassed = ! bypassMix.isSmoothing() && bypassMix.getCurrentValue() > (Real) 0.5;

    if (bypassMix.isSmoothing() || bypassMix.getCurrentValue() > (Real) 0)
    {
        auto* const* wet    = buffer.getArrayOfWritePointers();
        auto* const* dryPtr = dry.getArrayOfReadPointers();

        for (int i = 0; i < n; ++i)
        {
            // Raised cosine on the mix: no slope discontinuity at either end.
            const Real m = bypassMix.getNextValue();
            const Real s = (Real) 0.5 - (Real) 0.5 * std::cos (juce::MathConstants<Real>::pi * m);

            for (int ch = 0; ch < numCh; ++ch)
                wet[ch][i] += s * (dryPtr[ch][i] - wet[ch][i]);
        }
    }
    else
    {
        bypassMix.skip (n);
    }

    if (fullyBypassed)
    {
        compLowDb.store (0.0f);
        compMidDb.store (0.0f);
        compHiDb.store  (0.0f);
        limReductionDb.store (0.0f);
    }
    else
    {
        compLowDb.store (multiband.getReductionLow());
        compMidDb.store (multiband.getReductionMid());
        compHiDb.store  (multiband.getReductionHigh());
        limReductionDb.store (limGr);
    }
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
