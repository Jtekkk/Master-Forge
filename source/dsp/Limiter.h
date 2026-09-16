#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>
#include <cmath>

namespace mf
{
/**
    Transparent lookahead brickwall limiter with optional true-peak detection.

    Gain computation
    ----------------
    Two stages, arranged so the output provably never exceeds the ceiling
    *without* ever having to clip:

      1. A sliding-window maximum over the next `window` samples (a monotonic
         deque in a fixed-capacity ring — no audio-thread allocation) turns the
         upcoming peaks into a required-gain signal r[j] = ceiling / peak. By
         construction r[j] is already the minimum required gain anywhere in
         [j, j+window-1].
      2. That signal is smoothed by two cascaded moving averages whose combined
         support is exactly `window` samples. Because every tap of the smoothing
         kernel is non-negative, sums to one, and looks at an r[] whose own
         window covers the output sample, the smoothed gain is guaranteed to be
         <= the gain that sample actually needs. The ramp therefore reaches its
         target exactly when the peak arrives — no overshoot, and none of the
         hard clipping (and its broadband distortion) that a one-pole attack
         leaves behind. The cascade also makes the gain curve C1-continuous, so
         there are no slope discontinuities to modulate the signal.

    Release is an upward-only one-pole applied before the smoother. It can only
    ever lower the gain relative to r[], so the no-overshoot guarantee survives.

    True peak
    ---------
    When a detector oversampling factor is requested, the *detector* runs
    oversampled while the audio itself stays at the base rate: the input is
    upsampled into a scratch buffer purely to find the inter-sample peaks, and
    the resulting gain is applied to the untouched base-rate signal. Nothing in
    the audio path is resampled, so true-peak mode adds no extra filtering,
    no reconstruction ripple and no down-sampling overshoot — it is bit-for-bit
    the same signal path as sample-peak mode, only better informed.

    The detector's own (linear-phase, integer) latency is carried by the audio
    delay line in both modes, so toggling true peak never changes the reported
    latency and never forces the host to re-compensate mid-playback.

    Templated on the sample type so the delay line and gain stage run at 64-bit
    double internally (float alias `Limiter` kept for the standalone DSP tests).
*/
template <typename Sample>
class LimiterT
{
public:
    /** @param detectorStages number of 2x stages for true-peak detection
                              (0 = none, 2 = 4x, 4 = 16x). */
    void prepare (const juce::dsp::ProcessSpec& spec, int detectorStages = 0)
    {
        sampleRate  = spec.sampleRate;
        numChannels = (int) juce::jmax (1u, spec.numChannels);
        maxBlock    = (int) juce::jmax (1u, spec.maximumBlockSize);

        buildDetector (juce::jmax (0, detectorStages));

        // Forward look-ahead window, and the smoothing kernel that fills it.
        window = juce::jmax (4, (int) std::lround (lookaheadMs * 0.001 * sampleRate));
        boxA   = juce::jmax (1, (window + 1) / 2);
        boxB   = juce::jmax (1, window + 1 - boxA);   // boxA + boxB - 1 == window

        gainDelay  = window - 1;
        totalDelay = detectorDelay + gainDelay;

        delayBuffer.setSize (numChannels, totalDelay + maxBlock + 4);

        dqCapacity = window + 1;
        dq.assign ((size_t) dqCapacity, {});

        ringA.assign ((size_t) boxA, 1.0);
        ringB.assign ((size_t) boxB, 1.0);

        // Read-then-write on a ring of length N is a delay of exactly N, so the
        // sample-peak path lines up with the detector's latency. N == 0 means no
        // delay at all - going through a 1-slot ring would lag the detector by a
        // sample and let the very peak it is aiming at slip past.
        peakDelayLen = juce::jmax (1, detectorDelay);
        peakDelay.assign ((size_t) peakDelayLen, (Sample) 0);

        blockPeaks.assign ((size_t) maxBlock, (Sample) 0);

        setRelease (releaseMs);
        reset();
    }

    void reset()
    {
        delayBuffer.clear();
        writePos      = 0;
        sampleCounter = 0;
        dqHead = dqTail = 0;

        std::fill (ringA.begin(), ringA.end(), 1.0);
        std::fill (ringB.begin(), ringB.end(), 1.0);
        sumA = (double) boxA;
        sumB = (double) boxB;
        posA = posB = 0;

        std::fill (peakDelay.begin(), peakDelay.end(), (Sample) 0);
        peakPos = 0;

        releaseState = (Sample) 1;

        if (detector != nullptr)
            detector->reset();
    }

    void setParameters (float ceilingDb, float releaseMs_)
    {
        ceiling = (Sample) juce::Decibels::decibelsToGain (ceilingDb);
        setRelease (releaseMs_);
    }

    /** Switches the detector between inter-sample (true) peak and sample peak.
        Latency is identical either way, so this is safe to automate. */
    void setTruePeak (bool shouldUseTruePeak) noexcept
    {
        truePeak = shouldUseTruePeak && detector != nullptr;
    }

    int getLatencySamples() const noexcept { return totalDelay; }

    /** Processes in place. Returns peak gain reduction (dB, >= 0) for metering. */
    float process (juce::AudioBuffer<Sample>& buffer)
    {
        // The detector and the scratch buffers are sized for the block size we
        // were prepared with; a host that hands us more goes through in chunks
        // rather than off the end of them.
        const int total = buffer.getNumSamples();
        if (total > maxBlock)
        {
            float gr = 0.0f;
            for (int start = 0; start < total; start += maxBlock)
            {
                const int len = juce::jmin (maxBlock, total - start);
                juce::AudioBuffer<Sample> sub (buffer.getArrayOfWritePointers(),
                                               buffer.getNumChannels(), start, len);
                gr = juce::jmax (gr, process (sub));
            }
            return gr;
        }

        const int numCh     = juce::jmin (buffer.getNumChannels(), delayBuffer.getNumChannels());
        const int numSamples = buffer.getNumSamples();
        const int delaySize = delayBuffer.getNumSamples();

        if (numCh <= 0 || numSamples <= 0 || delaySize <= 0)
            return 0.0f;

        auto* const* data = buffer.getArrayOfWritePointers();

        gatherPeaks (buffer, numCh, numSamples);

        float maxReductionDb = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            pushMax (blockPeaks[(size_t) i]);

            const Sample windowPeak = dqFront().value;
            const Sample required   = (windowPeak > ceiling) ? (ceiling / windowPeak) : (Sample) 1;

            // Upward-only release: instant downward, smoothed upward. Never
            // rises above `required`, so the no-overshoot proof still holds.
            releaseState = (required < releaseState)
                             ? required
                             : (Sample) (releaseCoeff * releaseState
                                         + ((Sample) 1 - releaseCoeff) * required);

            const Sample gain = (Sample) smoothGain ((double) releaseState);

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = delayBuffer.getWritePointer (ch);
                d[writePos] = data[ch][i];

                int readPos = writePos - totalDelay;
                if (readPos < 0)
                    readPos += delaySize;

                // The clamp is a pure safety net: by construction `gain` is
                // already low enough that it never engages.
                data[ch][i] = juce::jlimit (-ceiling, ceiling, d[readPos] * gain);
            }

            if (++writePos >= delaySize)
                writePos = 0;

            maxReductionDb = juce::jmax (maxReductionDb,
                                         -(float) juce::Decibels::gainToDecibels (gain, (Sample) -60));
        }

        return maxReductionDb;
    }

private:
    // --- detector ----------------------------------------------------------
    void buildDetector (int stages)
    {
        if (stages > 0)
        {
            detector = std::make_unique<juce::dsp::Oversampling<Sample>> (
                (size_t) numChannels, (size_t) stages,
                juce::dsp::Oversampling<Sample>::filterHalfBandFIREquiripple,
                true,   // maximum quality half-band FIRs
                true);  // integer latency, so the alignment below is exact
            detector->initProcessing ((size_t) maxBlock);
            detector->reset();
            detectorFactor = (int) detector->getOversamplingFactor();
            detectorDelay  = (int) std::lround (detector->getLatencyInSamples());
            detectorScratch.setSize (numChannels, maxBlock);
        }
        else
        {
            detector.reset();
            detectorFactor = 1;
            detectorDelay  = 0;
            truePeak       = false;
        }
    }

    /** Fills blockPeaks[] with the detection peak belonging to each *delayed*
        input sample, so both modes line up with the same audio delay. */
    void gatherPeaks (const juce::AudioBuffer<Sample>& buffer, int numCh, int numSamples)
    {
        if (truePeak && detector != nullptr)
        {
            const int detCh = juce::jmin (numCh, detectorScratch.getNumChannels());
            juce::AudioBuffer<Sample> copy (detectorScratch.getArrayOfWritePointers(),
                                            detCh, numSamples);
            for (int ch = 0; ch < detCh; ++ch)
                copy.copyFrom (ch, 0, buffer, ch, 0, numSamples);

            juce::dsp::AudioBlock<Sample> copyBlock (copy);
            auto up = detector->processSamplesUp (copyBlock);

            const int f    = juce::jmax (1, detectorFactor);
            const int osCh = juce::jmin (detCh, (int) up.getNumChannels());
            const int osN  = (int) up.getNumSamples();

            for (int i = 0; i < numSamples; ++i)
            {
                Sample p = (Sample) 0;
                const int base = i * f;
                for (int ch = 0; ch < osCh; ++ch)
                {
                    const auto* d = up.getChannelPointer ((size_t) ch);
                    for (int k = 0; k < f && base + k < osN; ++k)
                        p = juce::jmax (p, std::abs (d[base + k]));
                }
                blockPeaks[(size_t) i] = p;
            }
        }
        else
        {
            // Sample peak, pushed through a matching delay so the reported
            // latency is the same whether or not true peak is engaged.
            for (int i = 0; i < numSamples; ++i)
            {
                Sample p = (Sample) 0;
                for (int ch = 0; ch < numCh; ++ch)
                    p = juce::jmax (p, std::abs (buffer.getReadPointer (ch)[i]));

                if (detectorDelay <= 0)
                {
                    blockPeaks[(size_t) i] = p;
                }
                else
                {
                    blockPeaks[(size_t) i] = peakDelay[(size_t) peakPos];
                    peakDelay[(size_t) peakPos] = p;
                    if (++peakPos >= peakDelayLen)
                        peakPos = 0;
                }
            }
        }
    }

    // --- gain smoothing ----------------------------------------------------
    /** Two cascaded moving averages; combined support == `window` samples. */
    double smoothGain (double x)
    {
        sumA += x - ringA[(size_t) posA];
        ringA[(size_t) posA] = x;
        if (++posA >= boxA) posA = 0;
        const double a = sumA / (double) boxA;

        sumB += a - ringB[(size_t) posB];
        ringB[(size_t) posB] = a;
        if (++posB >= boxB) posB = 0;
        return juce::jlimit (0.0, 1.0, sumB / (double) boxB);
    }

    void setRelease (float ms)
    {
        releaseMs = ms;
        const Sample t = (Sample) (juce::jmax (1.0f, ms) * 0.001f);
        releaseCoeff = std::exp (-(Sample) 1 / (t * (Sample) sampleRate));
    }

    // --- fixed-capacity monotonic deque of (sampleIndex, value) -------------
    struct MaxEntry { long long index = 0; Sample value = (Sample) 0; };

    MaxEntry& dqFront()        { return dq[(size_t) dqHead]; }
    bool      dqEmpty() const  { return dqHead == dqTail; }
    void      dqPopFront()     { if (++dqHead >= dqCapacity) dqHead = 0; }

    MaxEntry& dqBack()
    {
        int b = dqTail - 1;
        if (b < 0) b += dqCapacity;
        return dq[(size_t) b];
    }
    void dqPopBack() { if (--dqTail < 0) dqTail += dqCapacity; }
    void dqPushBack (MaxEntry e)
    {
        dq[(size_t) dqTail] = e;
        if (++dqTail >= dqCapacity) dqTail = 0;
    }

    void pushMax (Sample v)
    {
        const long long windowStart = sampleCounter - (window - 1);
        while (! dqEmpty() && dqFront().index < windowStart)
            dqPopFront();

        while (! dqEmpty() && dqBack().value <= v)
            dqPopBack();

        dqPushBack ({ sampleCounter, v });
        ++sampleCounter;
    }

    double sampleRate  = 44100.0;
    int    numChannels = 2;
    int    maxBlock    = 512;

    float lookaheadMs = 5.0f;
    int   window      = 1;     // forward look-ahead window (samples)
    int   boxA = 1, boxB = 1;  // cascaded moving-average lengths
    int   gainDelay  = 0;
    int   totalDelay = 0;

    Sample ceiling      = (Sample) 1;
    float  releaseMs    = 100.0f;
    Sample releaseCoeff = (Sample) 0;
    Sample releaseState = (Sample) 1;

    juce::AudioBuffer<Sample> delayBuffer;
    int writePos = 0;

    std::vector<MaxEntry> dq;
    int dqCapacity = 1, dqHead = 0, dqTail = 0;
    long long sampleCounter = 0;

    std::vector<double> ringA, ringB;
    double sumA = 0.0, sumB = 0.0;
    int posA = 0, posB = 0;

    std::unique_ptr<juce::dsp::Oversampling<Sample>> detector;
    juce::AudioBuffer<Sample> detectorScratch;
    int  detectorFactor = 1;
    int  detectorDelay  = 0;
    bool truePeak       = false;

    std::vector<Sample> peakDelay;
    int peakDelayLen = 1, peakPos = 0;

    std::vector<Sample> blockPeaks;
};

using Limiter = LimiterT<float>;
} // namespace mf
