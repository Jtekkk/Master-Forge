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
        stereo width -> output gain -> brickwall limiter (optionally true-peak)

    The output gain sits before the limiter so it acts as the drive into the
    limiter, while the limiter ceiling stays the true final peak.

    The whole chain keeps running while the plugin is bypassed: bypass is a
    delay-matched crossfade to the dry signal rather than an early return, so
    an A/B comparison is click-free, level-matched and time-aligned, and the
    filters/compressors are already warm when the wet path comes back.
*/
class MasterForgeAudioProcessor : public juce::AudioProcessor
{
public:
    MasterForgeAudioProcessor();
    ~MasterForgeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // The whole chain runs internally at 64-bit double. A float host is up/down
    // converted at the boundary; a double-precision host is processed natively.
    bool supportsDoublePrecisionProcessing() const override { return true; }
    void processBlock (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

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
    // Internal working precision. The chain processes at 64-bit double regardless
    // of the host's precision (the "internal double precision" quality feature).
    using Real = double;

    void updateParameters();
    void selectQuality (bool hq);             // switch between the prepared quality paths
    int  computeLatencySamples() const;
    void refreshLatency();
    void processChain (juce::AudioBuffer<Real>& buffer); // the double-precision DSP chain
    void publishMeters (const juce::AudioBuffer<float>& output); // metering / analysis taps

    juce::AudioProcessorValueTreeState apvts {
        *this, nullptr, "PARAMS", mf::createParameterLayout() };

    mf::PresetManager presetManager { apvts };

    juce::AudioParameterBool* bypassParam   = nullptr;
    juce::AudioParameterBool* truePeakParam = nullptr;

    mf::ParametricEQT<Real>          eq;
    mf::MultibandCompressorT<Real>   multiband;
    mf::StereoWidthT<Real>           stereoWidth;
    mf::LoudnessMeter                meter;       // metering stays single precision
    mf::SpectrumAnalyzer             analyzer;

    // Both quality paths are built up front in prepareToPlay and simply
    // selected by pointer, so toggling HQ takes effect on the very next block
    // and never allocates on the audio thread.
    mf::SaturationT<Real> saturationStd, saturationHq;   // 4x / 16x
    mf::LimiterT<Real>    limiterStd,    limiterHq;      // 4x / 16x true-peak detector
    mf::SaturationT<Real>* saturation = &saturationStd;
    mf::LimiterT<Real>*    limiter    = &limiterStd;

    juce::dsp::ProcessSpec spec {};
    bool lastHq = false;
    bool lastLinear = false;
    int  reportedLatency = 0;

    juce::dsp::Gain<Real> inputGain, outputGain;

    // Boundary scratch: up-convert a float host block to double and back, and a
    // float copy of the output to feed the (single-precision) meters/analyzer.
    juce::AudioBuffer<Real>  doubleScratch;
    juce::AudioBuffer<float> meterScratch;

    // Latency-compensated bypass: the dry signal is delayed by exactly the
    // latency the chain reports and crossfaded against the wet path, so
    // toggling bypass neither shifts the audio in time nor clicks.
    juce::AudioBuffer<Real> dryDelay;
    juce::AudioBuffer<Real> dryScratch;
    int  dryWritePos = 0;
    juce::SmoothedValue<Real> bypassMix { (Real) 0 };

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
