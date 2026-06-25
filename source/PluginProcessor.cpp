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

    juce::dsp::ProcessSpec spec;
    spec.sampleRate      = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    spec.numChannels      = (juce::uint32) numCh;

    inputGain.prepare (spec);
    inputGain.setRampDurationSeconds (0.02);
    outputGain.prepare (spec);
    outputGain.setRampDurationSeconds (0.02);

    eq.prepare (spec);
    multiband.prepare (spec);
    saturation.prepare (spec);
    stereoWidth.prepare (spec);
    limiter.prepare (spec);
    meter.prepare (sampleRate, numCh);
    analyzer.prepare (sampleRate);

    // True-peak / oversampled limiter path (4x).
    // Linear-phase FIR oversampling for clean inter-sample-peak (true-peak) control.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) spec.maximumBlockSize);
    oversampler->reset();
    osFactor = (int) oversampler->getOversamplingFactor();

    juce::dsp::ProcessSpec osSpec;
    osSpec.sampleRate      = sampleRate * osFactor;
    osSpec.maximumBlockSize = spec.maximumBlockSize * (juce::uint32) osFactor;
    osSpec.numChannels      = (juce::uint32) numCh;
    limiterOS.prepare (osSpec);

    updateParameters();

    lastTruePeak = truePeakParam->get();
    setLatencySamples (computeLatencySamples (lastTruePeak));
}

int MasterForgeAudioProcessor::computeLatencySamples (bool truePeak)
{
    if (truePeak && oversampler != nullptr)
        return (int) std::round (oversampler->getLatencyInSamples())
             + limiterOS.getLatencySamples() / juce::jmax (1, osFactor);

    return limiter.getLatencySamples();
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
                      val (pid::eqHighFreq), val (pid::eqHighGain));

    multiband.setParameters (val (pid::mbXLow), val (pid::mbXHigh),
                             val (pid::compAttack), val (pid::compRelease), val (pid::compKnee),
                             val (pid::mbLowThresh), val (pid::mbLowRatio), val (pid::mbLowMakeup),
                             val (pid::mbMidThresh), val (pid::mbMidRatio), val (pid::mbMidMakeup),
                             val (pid::mbHiThresh),  val (pid::mbHiRatio),  val (pid::mbHiMakeup));

    saturation.setParameters (val (pid::satDrive), val (pid::satMix));
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
        eq.process (context);

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
