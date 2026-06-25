#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

#include "Parameters.h"
#include "dsp/ParametricEQ.h"
#include "dsp/Compressor.h"
#include "dsp/Saturation.h"
#include "dsp/StereoWidth.h"
#include "dsp/Limiter.h"
#include "dsp/LoudnessMeter.h"

/**
    Master Forge — a mastering processor.

    Signal flow:
        input gain -> 4-band EQ -> compressor -> saturation ->
        stereo width -> output gain -> brickwall limiter

    The output gain sits before the limiter so it acts as the drive into the
    limiter, while the limiter ceiling stays the true final peak.
*/
class MasterForgeAudioProcessor : public juce::AudioProcessor
{
public:
    MasterForgeAudioProcessor();
    ~MasterForgeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    // ---- accessors for the editor's meters --------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    float getMomentaryLUFS()  const noexcept { return meter.getMomentaryLUFS(); }
    float getShortTermLUFS()  const noexcept { return meter.getShortTermLUFS(); }
    float getIntegratedLUFS() const noexcept { return meter.getIntegratedLUFS(); }
    void  resetIntegratedLUFS() noexcept     { meter.resetIntegrated(); }

    float getCompReductionDb() const noexcept { return compReductionDb.load(); }
    float getLimReductionDb()  const noexcept { return limReductionDb.load(); }
    float getOutputPeakLDb()   const noexcept { return outPeakLDb.load(); }
    float getOutputPeakRDb()   const noexcept { return outPeakRDb.load(); }

private:
    void updateParameters();

    juce::AudioProcessorValueTreeState apvts {
        *this, nullptr, "PARAMS", mf::createParameterLayout() };

    // cached parameter pointers (read on the audio thread)
    juce::AudioParameterBool* bypassParam = nullptr;

    mf::ParametricEQ  eq;
    mf::Compressor    compressor;
    mf::Saturation    saturation;
    mf::StereoWidth   stereoWidth;
    mf::Limiter       limiter;
    mf::LoudnessMeter meter;

    juce::dsp::Gain<float> inputGain, outputGain;

    // meter readouts published to the editor
    std::atomic<float> compReductionDb { 0.0f };
    std::atomic<float> limReductionDb  { 0.0f };
    std::atomic<float> outPeakLDb { -100.0f };
    std::atomic<float> outPeakRDb { -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterForgeAudioProcessor)
};
