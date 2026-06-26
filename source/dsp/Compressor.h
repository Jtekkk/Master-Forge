#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace mf
{
/**
    Stereo-linked, soft-knee compressor with an RMS (mean-square) detector.

    Detection is RMS rather than instantaneous peak: the squared input is
    smoothed with the attack/release time constants before the level is taken.
    This is what keeps the gain from following the waveform on low frequencies
    (a peak detector there modulates the signal within each cycle and adds
    audible harmonic distortion). The static curve is the classic soft-knee
    formulation from Giannoulis, Massberg & Reiss (JAES 2012).
*/
class Compressor
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        rmsCoeff = timeToCoeff (rmsMs);
        reset();
    }

    void reset() { msEnv = 0.0f; grEnv = 0.0f; }

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
            // Linked detector: loudest squared sample across channels.
            float peakSq = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peakSq = juce::jmax (peakSq, data[ch][i] * data[ch][i]);

            // Stage 1: symmetric RMS pre-average. Smoothing the power here (rather
            // than branching) keeps the level steady within a waveform cycle, so
            // the gain doesn't modulate the signal (the source of bass distortion).
            msEnv = rmsCoeff * msEnv + (1.0f - rmsCoeff) * peakSq;

            const float levelDb = 10.0f * std::log10 (juce::jmax (msEnv, 1.0e-10f));
            const float targetReductionDb = levelDb - computeCurve (levelDb); // >= 0

            // Stage 2: attack/release envelope on the gain reduction.
            const float coeff = (targetReductionDb > grEnv) ? attackCoeff : releaseCoeff;
            grEnv = coeff * grEnv + (1.0f - coeff) * targetReductionDb;

            const float gain = juce::Decibels::decibelsToGain (-grEnv) * makeupGain;
            for (int ch = 0; ch < numCh; ++ch)
                data[ch][i] *= gain;

            maxReductionDb = juce::jmax (maxReductionDb, grEnv);
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
    float  msEnv = 0.0f, grEnv = 0.0f;

    static constexpr float rmsMs = 30.0f;  // RMS detector averaging window
    float rmsCoeff = 0.0f;
};
} // namespace mf
