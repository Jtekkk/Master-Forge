#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace pid = mf::pid;

MasterForgeAudioProcessor::MasterForgeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (pid::bypass));
    jassert (bypassParam != nullptr);
}

bool MasterForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono()
        && mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Match input and output layouts.
    return layouts.getMainInputChannelSet() == mainOut;
}

void MasterForgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize  = (juce::uint32) juce::jmax (1, samplesPerBlock);
    spec.numChannels       = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());

    inputGain.prepare (spec);
    inputGain.setRampDurationSeconds (0.02);
    outputGain.prepare (spec);
    outputGain.setRampDurationSeconds (0.02);

    eq.prepare (spec);
    compressor.prepare (spec);
    saturation.prepare (spec);
    stereoWidth.prepare (spec);
    limiter.prepare (spec);
    meter.prepare (sampleRate, (int) spec.numChannels);

    updateParameters();

    setLatencySamples (limiter.getLatencySamples());
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

    compressor.setParameters (val (pid::compThresh), val (pid::compRatio),
                              val (pid::compAttack), val (pid::compRelease),
                              val (pid::compKnee),   val (pid::compMakeup));

    saturation.setParameters (val (pid::satDrive), val (pid::satMix));
    stereoWidth.setWidth (val (pid::width));
    limiter.setParameters (val (pid::limCeiling), val (pid::limRelease));
}

void MasterForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateParameters();

    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();

    if (bypassParam->get())
    {
        compReductionDb.store (0.0f);
        limReductionDb.store (0.0f);
    }
    else
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);

        inputGain.process (context);
        eq.process (context);

        const float compGr = compressor.process (buffer);

        saturation.process (buffer);
        stereoWidth.process (buffer);

        outputGain.process (context);

        const float limGr = limiter.process (buffer);

        compReductionDb.store (compGr);
        limReductionDb.store (limGr);
    }

    // Metering on the output signal (works in bypass too, showing the dry level).
    meter.process (buffer);

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

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterForgeAudioProcessor();
}
