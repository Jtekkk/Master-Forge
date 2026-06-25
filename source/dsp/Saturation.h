#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace mf
{
/**
    Smooth tanh saturator with a dry/wet mix.

    y = tanh(drive * x) / drive keeps unity gain for quiet signals (it tends to
    x as x -> 0) while progressively rounding peaks as drive increases, adding
    odd-harmonic warmth without a large level change.
*/
class Saturation
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        driveSmoothed.reset (spec.sampleRate, 0.02);
        mixSmoothed.reset   (spec.sampleRate, 0.02);
    }

    void reset() {}

    void setParameters (float driveDb, float mixPercent)
    {
        driveSmoothed.setTargetValue (juce::jmax (1.0f, juce::Decibels::decibelsToGain (driveDb)));
        mixSmoothed.setTargetValue   (juce::jlimit (0.0f, 1.0f, mixPercent * 0.01f));
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh      = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        auto* const* data    = buffer.getArrayOfWritePointers();

        for (int i = 0; i < numSamples; ++i)
        {
            const float drive = driveSmoothed.getNextValue();
            const float mix   = mixSmoothed.getNextValue();
            const float invDrive = 1.0f / drive;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x   = data[ch][i];
                const float wet = std::tanh (drive * x) * invDrive;
                data[ch][i] = x + mix * (wet - x);
            }
        }
    }

private:
    juce::SmoothedValue<float> driveSmoothed { 1.0f };
    juce::SmoothedValue<float> mixSmoothed   { 0.0f };
};
} // namespace mf
