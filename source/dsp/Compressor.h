#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace mf
{
/**
    Stereo-linked, soft-knee compressor.

    Detection is peak based and linked across channels (the loudest channel
    drives the gain) so the stereo image stays put. The static curve follows
    the classic soft-knee formulation from Giannoulis, Massberg & Reiss,
    "Digital Dynamic Range Compressor Design" (JAES 2012).
*/
class Compressor
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        reset();
    }

    void reset() { envelopeDb = 0.0f; }

    void setParameters (float thresholdDb, float ratio_, float attackMs,
                        float releaseMs, float kneeDb, float makeupDb)
    {
        threshold = thresholdDb;
        ratio     = juce::jmax (1.0f, ratio_);
        knee      = juce::jmax (0.0f, kneeDb);
        makeupGain = juce::Decibels::decibelsToGain (makeupDb);

        attackCoeff  = timeToCoeff (attackMs);
        releaseCoeff = timeToCoeff (releaseMs);
    }

    /** Processes in place. Returns the peak gain reduction (dB, >= 0) for metering. */
    float process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh      = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        auto* const* data    = buffer.getArrayOfWritePointers();

        float maxReductionDb = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // Linked detector: loudest sample across all channels.
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, std::abs (data[ch][i]));

            const float levelDb     = juce::Decibels::gainToDecibels (peak, -100.0f);
            const float targetGainDb = levelDb - computeCurve (levelDb); // >= 0 reduction

            // Branching attack/release on the gain-reduction envelope.
            const float coeff = (targetGainDb > envelopeDb) ? attackCoeff : releaseCoeff;
            envelopeDb = coeff * envelopeDb + (1.0f - coeff) * targetGainDb;

            const float gain = juce::Decibels::decibelsToGain (-envelopeDb) * makeupGain;
            for (int ch = 0; ch < numCh; ++ch)
                data[ch][i] *= gain;

            maxReductionDb = juce::jmax (maxReductionDb, envelopeDb);
        }

        return maxReductionDb;
    }

private:
    float timeToCoeff (float timeMs) const
    {
        const float t = juce::jmax (0.01f, timeMs) * 0.001f; // seconds
        return std::exp (-1.0f / (t * static_cast<float> (sampleRate)));
    }

    /** Output level (dB) for a given input level (dB) on the static curve. */
    float computeCurve (float xDb) const
    {
        const float over = xDb - threshold;

        if (2.0f * over < -knee)                 // fully below the knee
            return xDb;

        if (knee > 0.0f && 2.0f * std::abs (over) <= knee) // inside the knee
        {
            const float t = over + 0.5f * knee;
            return xDb + (1.0f / ratio - 1.0f) * (t * t) / (2.0f * knee);
        }

        return threshold + over / ratio;          // above the knee
    }

    double sampleRate = 44100.0;
    float  threshold = -18.0f, ratio = 2.0f, knee = 6.0f, makeupGain = 1.0f;
    float  attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float  envelopeDb = 0.0f;
};
} // namespace mf
