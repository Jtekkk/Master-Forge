#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <cmath>

namespace mf
{
/**
    Harmonic "THD" stage: a smooth tanh nonlinearity blended with the dry signal,
    driven by a single THD amount (0..100%). It is oversampled (4x normally, 16x
    in HQ mode) with linear-phase FIR filters so the added harmonics don't alias,
    and the dry/wet blend is done in the oversampled domain so the two stay
    phase-aligned.
*/
class Saturation
{
public:
    /** @param stages number of 2x oversampling stages (2 = 4x, 4 = 16x). */
    void prepare (const juce::dsp::ProcessSpec& spec, int stages)
    {
        currentStages = juce::jmax (1, stages);
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            juce::jmax (1u, spec.numChannels), (size_t) currentStages,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
        oversampler->initProcessing (spec.maximumBlockSize);
        oversampler->reset();
        osFactor = (int) oversampler->getOversamplingFactor();

        driveSmoothed.reset (spec.sampleRate * osFactor, 0.02);
        mixSmoothed.reset   (spec.sampleRate * osFactor, 0.02);
    }

    void reset() { if (oversampler != nullptr) oversampler->reset(); }

    int getStages() const noexcept { return currentStages; }

    int getLatencySamples() const noexcept
    {
        return oversampler != nullptr ? (int) std::round (oversampler->getLatencyInSamples()) : 0;
    }

    /** @param thdPercent 0..100 — harmonic amount (0 = clean). */
    void setThd (float thdPercent)
    {
        const float t = juce::jlimit (0.0f, 1.0f, thdPercent * 0.01f);
        driveSmoothed.setTargetValue (1.0f + t * driveScale);
        mixSmoothed.setTargetValue   (t);
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
    static constexpr float driveScale = 9.0f; // THD 100% -> drive 10x

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int currentStages = 2;
    int osFactor = 1;

    juce::SmoothedValue<float> driveSmoothed { 1.0f };
    juce::SmoothedValue<float> mixSmoothed   { 0.0f };
};
} // namespace mf
