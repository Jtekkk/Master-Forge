#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>

#include "Parameters.h"
#include "PresetManager.h"
#include "dsp/ParametricEQ.h"
#include "dsp/MultibandCompressor.h"
#include "dsp/Saturation.h"
#include "dsp/StereoWidth.h"
#include "dsp/Limiter.h"
#include "dsp/LoudnessMeter.h"
#include "dsp/SpectrumAnalyzer.h"

/**
    Master Forge — a mastering processor.

    Signal flow:
        input gain -> 4-band EQ -> 3-band multiband compressor -> saturation ->
        stereo width -> output gain -> brickwall limiter (optionally true-peak
        / oversampled)

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

    const juce::String getName() const override { return "Master Forge"; }
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

    // ---- accessors for the editor -----------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    mf::SpectrumAnalyzer& getAnalyzer() noexcept { return analyzer; }
    mf::PresetManager&    getPresetManager() noexcept { return presetManager; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    float getMomentaryLUFS()  const noexcept { return meter.getMomentaryLUFS(); }
    float getShortTermLUFS()  const noexcept { return meter.getShortTermLUFS(); }
    float getIntegratedLUFS() const noexcept { return meter.getIntegratedLUFS(); }
    void  resetIntegratedLUFS() noexcept     { meter.resetIntegrated(); }

    float getCompReductionLowDb() const noexcept { return compLowDb.load(); }
    float getCompReductionMidDb() const noexcept { return compMidDb.load(); }
    float getCompReductionHiDb()  const noexcept { return compHiDb.load(); }
    float getLimReductionDb()     const noexcept { return limReductionDb.load(); }
    float getOutputPeakLDb()      const noexcept { return outPeakLDb.load(); }
    float getOutputPeakRDb()      const noexcept { return outPeakRDb.load(); }
    float getCorrelation()        const noexcept { return correlation.load(); }

private:
    void updateParameters();
    int  computeLatencySamples (bool truePeak);

    juce::AudioProcessorValueTreeState apvts {
        *this, nullptr, "PARAMS", mf::createParameterLayout() };

    mf::PresetManager presetManager { apvts };

    juce::AudioParameterBool* bypassParam   = nullptr;
    juce::AudioParameterBool* truePeakParam = nullptr;

    mf::ParametricEQ         eq;
    mf::MultibandCompressor  multiband;
    mf::Saturation           saturation;
    mf::StereoWidth          stereoWidth;
    mf::Limiter              limiter;     // base-rate path
    mf::Limiter              limiterOS;   // oversampled (true-peak) path
    mf::LoudnessMeter        meter;
    mf::SpectrumAnalyzer     analyzer;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int  osFactor = 1;
    bool lastTruePeak = false;

    juce::dsp::Gain<float> inputGain, outputGain;

    double currentSampleRate = 44100.0;

    std::atomic<float> compLowDb { 0.0f };
    std::atomic<float> compMidDb { 0.0f };
    std::atomic<float> compHiDb  { 0.0f };
    std::atomic<float> limReductionDb { 0.0f };
    std::atomic<float> outPeakLDb { -100.0f };
    std::atomic<float> outPeakRDb { -100.0f };
    std::atomic<float> correlation { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterForgeAudioProcessor)
};
