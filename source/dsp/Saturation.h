#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <cmath>

namespace mf
{
/**
    Smooth tanh saturator with a dry/wet mix, oversampled to suppress aliasing.

    y = tanh(drive * x) / drive keeps unity gain for quiet signals while
    progressively rounding peaks as drive increases. A memoryless nonlinearity
    like this generates harmonics above Nyquist that fold back as inharmonic
    aliasing, so the wet path is processed at 4x (linear-phase FIR oversampling)
    and the dry/wet blend is done in the oversampled domain — that way the dry
    and wet share the oversampler's latency and stay phase-aligned.
*/
class Saturation
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        const auto numCh = juce::jmax (1u, spec.numChannels);
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            numCh, osStages, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
        oversampler->initProcessing (spec.maximumBlockSize);
        oversampler->reset();
        osFactor = (int) oversampler->getOversamplingFactor();

        driveSmoothed.reset (spec.sampleRate * osFactor, 0.02);
        mixSmoothed.reset   (spec.sampleRate * osFactor, 0.02);
    }

    void reset() { if (oversampler != nullptr) oversampler->reset(); }

    int getLatencySamples() const noexcept
    {
        return oversampler != nullptr ? (int) std::round (oversampler->getLatencyInSamples()) : 0;
    }

    void setParameters (float driveDb, float mixPercent)
    {
        driveSmoothed.setTargetValue (juce::jmax (1.0f, juce::Decibels::decibelsToGain (driveDb)));
        mixSmoothed.setTargetValue   (juce::jlimit (0.0f, 1.0f, mixPercent * 0.01f));
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (oversampler == nullptr)
            return;

        juce::dsp::AudioBlock<float> block (buffer);
        auto os = oversampler->processSamplesUp (block);

        const int osCh = (int) os.getNumChannels();
        const int osN  = (int) os.getNumSamples();

        for (int i = 0; i < osN; ++i)
        {
            const float drive    = driveSmoothed.getNextValue();
            const float mix      = mixSmoothed.getNextValue();
            const float invDrive = 1.0f / drive;

            for (int ch = 0; ch < osCh; ++ch)
            {
                auto* d = os.getChannelPointer ((size_t) ch);
                const float x   = d[i];
                const float wet = std::tanh (drive * x) * invDrive;
                d[i] = x + mix * (wet - x);
            }
        }

        oversampler->processSamplesDown (block);
    }

private:
    static constexpr size_t osStages = 2; // 4x

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int osFactor = 1;

    juce::SmoothedValue<float> driveSmoothed { 1.0f };
    juce::SmoothedValue<float> mixSmoothed   { 0.0f };
};
} // namespace mf
