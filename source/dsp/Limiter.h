#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

namespace mf
{
/**
    True lookahead brickwall limiter.

    The detector keeps a sliding-window maximum of the upcoming peaks (a
    monotonic deque stored in a fixed-capacity ring, so there are no audio
    thread allocations). Because we see each peak `lookahead` samples early,
    the gain can ramp down smoothly before the peak reaches the (delayed)
    output. A final hard clamp at the ceiling catches any sub-sample overshoot
    left by the smoothing, guaranteeing the output never exceeds the ceiling.

    Templated on the sample type so the delay line and gain stage run at 64-bit
    double internally (float alias `Limiter` kept for the standalone DSP tests).
*/
template <typename Sample>
class LimiterT
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate       = spec.sampleRate;
        lookaheadSamples = juce::jmax (1, (int) std::round (lookaheadMs * 0.001 * sampleRate));

        const int numCh    = (int) spec.numChannels;
        const int maxBlock = (int) spec.maximumBlockSize;
        delayBuffer.setSize (numCh, lookaheadSamples + maxBlock + 4);

        // Attack settles well within the lookahead window (~5 time constants).
        attackCoeff = std::exp (-(Sample) 1 / (Sample) juce::jmax (1.0f, lookaheadSamples * 0.2f));

        dqCapacity = lookaheadSamples + 1;
        dq.assign ((size_t) dqCapacity, {});

        setRelease (releaseMs);
        reset();
    }

    void reset()
    {
        delayBuffer.clear();
        writePos      = 0;
        gainState     = (Sample) 1;
        sampleCounter = 0;
        dqHead = dqTail = 0;
    }

    void setParameters (float ceilingDb, float releaseMs_)
    {
        ceiling = (Sample) juce::Decibels::decibelsToGain (ceilingDb);
        setRelease (releaseMs_);
    }

    int getLatencySamples() const noexcept { return lookaheadSamples; }

    /** Processes in place. Returns peak gain reduction (dB, >= 0) for metering. */
    float process (juce::AudioBuffer<Sample>& buffer)
    {
        const int numCh      = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        const int delaySize  = delayBuffer.getNumSamples();
        auto* const* data    = buffer.getArrayOfWritePointers();

        float maxReductionDb = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            Sample peak = (Sample) 0;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, std::abs (data[ch][i]));

            pushMax (peak);
            const Sample windowPeak = dqFront().value;
            const Sample target = (windowPeak > ceiling) ? (ceiling / windowPeak) : (Sample) 1;

            // Fast (lookahead-matched) attack down, smooth release up.
            const Sample coeff = (target < gainState) ? attackCoeff : releaseCoeff;
            gainState = coeff * gainState + ((Sample) 1 - coeff) * target;

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = delayBuffer.getWritePointer (ch);
                d[writePos] = data[ch][i];

                int readPos = writePos - lookaheadSamples;
                if (readPos < 0)
                    readPos += delaySize;

                const Sample out = d[readPos] * gainState;
                data[ch][i] = juce::jlimit (-ceiling, ceiling, out); // brickwall safety
            }

            if (++writePos >= delaySize)
                writePos = 0;

            maxReductionDb = juce::jmax (maxReductionDb,
                                         -(float) juce::Decibels::gainToDecibels (gainState, (Sample) -60));
        }

        return maxReductionDb;
    }

private:
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
        // Drop entries that have fallen outside the lookahead window first, so
        // the deque never holds more than `lookaheadSamples` entries.
        const long long windowStart = sampleCounter - (lookaheadSamples - 1);
        while (! dqEmpty() && dqFront().index < windowStart)
            dqPopFront();

        // Maintain the monotonic (non-increasing) invariant.
        while (! dqEmpty() && dqBack().value <= v)
            dqPopBack();

        dqPushBack ({ sampleCounter, v });
        ++sampleCounter;
    }

    double sampleRate     = 44100.0;
    float  lookaheadMs    = 5.0f;
    int    lookaheadSamples = 1;

    Sample ceiling      = (Sample) 1;
    float  releaseMs    = 100.0f;
    Sample attackCoeff  = (Sample) 0;
    Sample releaseCoeff = (Sample) 0;
    Sample gainState    = (Sample) 1;

    juce::AudioBuffer<Sample> delayBuffer;
    int writePos = 0;

    std::vector<MaxEntry> dq;
    int dqCapacity = 1, dqHead = 0, dqTail = 0;
    long long sampleCounter = 0;
};

using Limiter = LimiterT<float>;
} // namespace mf
