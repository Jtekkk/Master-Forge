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

    Templated on the sample type so the processor can run the whole detector and
    gain stage at 64-bit double (float alias `Compressor` kept for the tests).
*/
template <typename Sample>
class CompressorT
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        rmsCoeff = timeToCoeff (rmsMs);
        reset();
    }

    void reset() { msEnv = (Sample) 0; grEnv = (Sample) 0; }

    void setParameters (float thresholdDb, float ratio_, float attackMs,
                        float releaseMs, float kneeDb, float makeupDb)
    {
        threshold = thresholdDb;
        ratio     = juce::jmax (1.0f, ratio_);
        knee      = juce::jmax (0.0f, kneeDb);
        makeupGain = (Sample) juce::Decibels::decibelsToGain (makeupDb);

        attackCoeff  = timeToCoeff (attackMs);
        releaseCoeff = timeToCoeff (releaseMs);
    }

    /** Processes in place. Returns the peak gain reduction (dB, >= 0) for metering. */
    float process (juce::AudioBuffer<Sample>& buffer)
    {
        const int numCh      = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        auto* const* data    = buffer.getArrayOfWritePointers();

        float maxReductionDb = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // Linked detector: loudest squared sample across channels.
            Sample peakSq = (Sample) 0;
            for (int ch = 0; ch < numCh; ++ch)
                peakSq = juce::jmax (peakSq, data[ch][i] * data[ch][i]);

            // Stage 1: symmetric RMS pre-average. Smoothing the power here (rather
            // than branching) keeps the level steady within a waveform cycle, so
            // the gain doesn't modulate the signal (the source of bass distortion).
            msEnv = rmsCoeff * msEnv + ((Sample) 1 - rmsCoeff) * peakSq;

            const Sample levelDb = (Sample) 10 * std::log10 (juce::jmax (msEnv, (Sample) 1.0e-10));
            const Sample targetReductionDb = levelDb - computeCurve (levelDb); // >= 0

            // Stage 2: attack/release envelope on the gain reduction.
            const Sample coeff = (targetReductionDb > grEnv) ? attackCoeff : releaseCoeff;
            grEnv = coeff * grEnv + ((Sample) 1 - coeff) * targetReductionDb;

            const Sample gain = juce::Decibels::decibelsToGain (-grEnv) * makeupGain;
            for (int ch = 0; ch < numCh; ++ch)
                data[ch][i] *= gain;

            maxReductionDb = juce::jmax (maxReductionDb, (float) grEnv);
        }

        return maxReductionDb;
    }

private:
    Sample timeToCoeff (float timeMs) const
    {
        const Sample t = (Sample) (juce::jmax (0.01f, timeMs) * 0.001f); // seconds
        return std::exp (-(Sample) 1 / (t * (Sample) sampleRate));
    }

    /** Output level (dB) for a given input level (dB) on the static curve. */
    Sample computeCurve (Sample xDb) const
    {
        const Sample over = xDb - (Sample) threshold;

        if ((Sample) 2 * over < -(Sample) knee)                 // fully below the knee
            return xDb;

        if (knee > 0.0f && (Sample) 2 * std::abs (over) <= (Sample) knee) // inside the knee
        {
            const Sample t = over + (Sample) 0.5 * (Sample) knee;
            return xDb + ((Sample) 1 / (Sample) ratio - (Sample) 1) * (t * t) / ((Sample) 2 * (Sample) knee);
        }

        return (Sample) threshold + over / (Sample) ratio;       // above the knee
    }

    double sampleRate = 44100.0;
    float  threshold = -18.0f, ratio = 2.0f, knee = 6.0f;
    Sample makeupGain = (Sample) 1;
    Sample attackCoeff = (Sample) 0, releaseCoeff = (Sample) 0;
    Sample msEnv = (Sample) 0, grEnv = (Sample) 0;

    static constexpr float rmsMs = 30.0f;  // RMS detector averaging window
    Sample rmsCoeff = (Sample) 0;
};

using Compressor = CompressorT<float>;
} // namespace mf
