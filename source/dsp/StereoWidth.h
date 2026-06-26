#pragma once

#include <juce_dsp/juce_dsp.h>

namespace mf
{
/**
    Mid/Side stereo width control.

    width = 1.0 (100%) is unity. Below that the image narrows toward mono
    (0% = fully mono); above that the side signal is boosted to widen it.
    A no-op on mono signals.

    Templated on the sample type so the processor can run it at 64-bit double
    internally (float alias `StereoWidth` kept for the standalone DSP tests).
*/
template <typename Sample>
class StereoWidthT
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        widthSmoothed.reset (spec.sampleRate, 0.02);
    }

    void reset() {}

    /** @param widthPercent 0..200 */
    void setWidth (float widthPercent)
    {
        widthSmoothed.setTargetValue ((Sample) (widthPercent * 0.01f));
    }

    void process (juce::AudioBuffer<Sample>& buffer)
    {
        if (buffer.getNumChannels() < 2)
        {
            widthSmoothed.skip (buffer.getNumSamples());
            return;
        }

        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        const int numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            const Sample w    = widthSmoothed.getNextValue();
            const Sample mid  = (Sample) 0.5 * (left[i] + right[i]);
            const Sample side = (Sample) 0.5 * (left[i] - right[i]) * w;

            left[i]  = mid + side;
            right[i] = mid - side;
        }
    }

private:
    juce::SmoothedValue<Sample> widthSmoothed { (Sample) 1 };
};

using StereoWidth = StereoWidthT<float>;
} // namespace mf
