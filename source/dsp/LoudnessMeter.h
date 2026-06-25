#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include <cmath>

namespace mf
{
/**
    ITU-R BS.1770 loudness meter: momentary (400 ms), short-term (3 s) and
    gated integrated LUFS.

    The signal is K-weighted (a high-shelf "head" filter plus a ~38 Hz
    high-pass), the channel-summed mean square is tracked over sliding windows,
    and the integrated value uses the two-stage gating from BS.1770. Integrated
    blocks are accumulated into a fixed loudness histogram so the measurement is
    bounded in memory and free of audio-thread allocations. Results are
    published through atomics for the GUI thread to read.
*/
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        fs       = sampleRate;
        channels = juce::jmax (1, numChannels);

        stage1.assign ((size_t) channels, makeHighShelf (fs, 1681.974450955533, 3.999843853973347, 0.7071752369554196));
        stage2.assign ((size_t) channels, makeHighPass  (fs, 38.13547087602444, 0.5003270373238773));

        momWindow = juce::jmax (1, (int) std::round (0.4 * fs));
        shortWindow = juce::jmax (1, (int) std::round (3.0 * fs));
        hopSamples  = juce::jmax (1, (int) std::round (0.1 * fs));

        momRing.assign   ((size_t) momWindow, 0.0f);
        shortRing.assign ((size_t) shortWindow, 0.0f);

        histCount.assign  ((size_t) numBins, 0);
        histEnergy.assign ((size_t) numBins, 0.0);

        reset();
    }

    void reset()
    {
        for (auto& b : stage1) b.reset();
        for (auto& b : stage2) b.reset();

        std::fill (momRing.begin(),   momRing.end(),   0.0f);
        std::fill (shortRing.begin(), shortRing.end(), 0.0f);
        momIndex = shortIndex = 0;
        momSum = shortSum = 0.0;
        momFill = 0;
        hopCounter = 0;

        resetIntegrated();

        momentaryLufs.store (silence);
        shortTermLufs.store (silence);
    }

    void resetIntegrated()
    {
        std::fill (histCount.begin(),  histCount.end(),  0ll);
        std::fill (histEnergy.begin(), histEnergy.end(), 0.0);
        gatedEnergySum = 0.0;
        gatedCount     = 0;
        integratedLufs.store (silence);
    }

    void process (const juce::AudioBuffer<float>& buffer)
    {
        const int numCh      = juce::jmin (channels, buffer.getNumChannels());
        const int numSamples = buffer.getNumSamples();
        if (numCh <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            double s = 0.0;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float w = stage2[(size_t) ch].process (stage1[(size_t) ch].process (buffer.getSample (ch, i)));
                s += (double) w * w;                 // channel weight = 1.0 (mono/stereo)
            }

            momSum   += s - momRing[(size_t) momIndex];
            momRing[(size_t) momIndex] = (float) s;
            if (++momIndex >= momWindow) momIndex = 0;

            shortSum += s - shortRing[(size_t) shortIndex];
            shortRing[(size_t) shortIndex] = (float) s;
            if (++shortIndex >= shortWindow) shortIndex = 0;

            if (momFill < momWindow)
                ++momFill;

            // Every 100 ms, fold the current 400 ms block into the integrated histogram.
            if (++hopCounter >= hopSamples)
            {
                hopCounter = 0;
                if (momFill >= momWindow)
                    addIntegratedBlock (momSum / (double) momWindow);
            }
        }

        momentaryLufs.store (loudnessFromMeanSquare (momSum   / (double) momWindow));
        shortTermLufs.store (loudnessFromMeanSquare (shortSum / (double) shortWindow));
    }

    float getMomentaryLUFS()  const noexcept { return momentaryLufs.load(); }
    float getShortTermLUFS()  const noexcept { return shortTermLufs.load(); }
    float getIntegratedLUFS() const noexcept { return integratedLufs.load(); }

private:
    // ---- biquad -----------------------------------------------------------
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        float process (float in)
        {
            const double x = (double) in;
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() { z1 = z2 = 0.0; }
    };

    static Biquad makeHighPass (double fs, double f0, double Q)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
        const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        Biquad bq;
        bq.b0 = ((1.0 + c) * 0.5) / a0;
        bq.b1 = (-(1.0 + c))      / a0;
        bq.b2 = ((1.0 + c) * 0.5) / a0;
        bq.a1 = (-2.0 * c)        / a0;
        bq.a2 = (1.0 - alpha)     / a0;
        return bq;
    }

    static Biquad makeHighShelf (double fs, double f0, double gainDb, double Q)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
        const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * Q);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) - (A - 1.0) * c + sa;
        Biquad bq;
        bq.b0 =        A * ((A + 1.0) + (A - 1.0) * c + sa) / a0;
        bq.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c)      / a0;
        bq.b2 =        A * ((A + 1.0) + (A - 1.0) * c - sa) / a0;
        bq.a1 =  2.0 *     ((A - 1.0) - (A + 1.0) * c)      / a0;
        bq.a2 =           ((A + 1.0) - (A - 1.0) * c - sa)  / a0;
        return bq;
    }

    static float loudnessFromMeanSquare (double meanSquare)
    {
        if (meanSquare <= 1.0e-12)
            return silence;
        return (float) (-0.691 + 10.0 * std::log10 (meanSquare));
    }

    void addIntegratedBlock (double meanSquare)
    {
        const float loudness = loudnessFromMeanSquare (meanSquare);
        if (loudness < absoluteGate)        // BS.1770 absolute gate (-70 LUFS)
            return;

        int bin = (int) ((loudness - absoluteGate) / binWidth);
        bin = juce::jlimit (0, numBins - 1, bin);
        histCount[(size_t) bin]  += 1;
        histEnergy[(size_t) bin] += meanSquare;

        gatedEnergySum += meanSquare;
        gatedCount     += 1;

        // Relative gate: 10 LU below the mean loudness of the absolute-gated blocks.
        const double meanAbs = gatedEnergySum / (double) gatedCount;
        const double relGate = -0.691 + 10.0 * std::log10 (meanAbs) - 10.0;

        double energy = 0.0;
        long long count = 0;
        for (int b = 0; b < numBins; ++b)
        {
            const double centre = absoluteGate + (b + 0.5) * binWidth;
            if (centre >= relGate)
            {
                energy += histEnergy[(size_t) b];
                count  += histCount[(size_t) b];
            }
        }

        integratedLufs.store (count > 0 ? loudnessFromMeanSquare (energy / (double) count)
                                        : silence);
    }

    static constexpr float silence      = -100.0f;
    static constexpr double absoluteGate = -70.0;
    static constexpr double binWidth     = 0.1;
    static constexpr int    numBins      = 751;   // -70.0 .. +5.1 LUFS

    double fs = 44100.0;
    int    channels = 2;

    std::vector<Biquad> stage1, stage2;

    std::vector<float> momRing, shortRing;
    int   momWindow = 1, shortWindow = 1, hopSamples = 1;
    int   momIndex = 0, shortIndex = 0, momFill = 0, hopCounter = 0;
    double momSum = 0.0, shortSum = 0.0;

    std::vector<long long> histCount;
    std::vector<double>    histEnergy;
    double    gatedEnergySum = 0.0;
    long long gatedCount     = 0;

    std::atomic<float> momentaryLufs  { silence };
    std::atomic<float> shortTermLufs  { silence };
    std::atomic<float> integratedLufs { silence };
};
} // namespace mf
