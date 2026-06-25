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
*/
class Limiter
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
        attackCoeff = std::exp (-1.0f / juce::jmax (1.0f, lookaheadSamples * 0.2f));

        dqCapacity = lookaheadSamples + 1;
        dq.assign ((size_t) dqCapacity, {});

        setRelease (releaseMs);
        reset();
    }

    void reset()
    {
        delayBuffer.clear();
        writePos      = 0;
        gainState     = 1.0f;
        sampleCounter = 0;
        dqHead = dqTail = 0;
    }

    void setParameters (float ceilingDb, float releaseMs_)
    {
        ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        setRelease (releaseMs_);
    }

    int getLatencySamples() const noexcept { return lookaheadSamples; }

    /** Processes in place. Returns peak gain reduction (dB, >= 0) for metering. */
    float process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh      = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        const int delaySize  = delayBuffer.getNumSamples();
        auto* const* data    = buffer.getArrayOfWritePointers();

        float maxReductionDb = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, std::abs (data[ch][i]));

            pushMax (peak);
            const float windowPeak = dqFront().value;
            const float target = (windowPeak > ceiling) ? (ceiling / windowPeak) : 1.0f;

            // Fast (lookahead-matched) attack down, smooth release up.
            const float coeff = (target < gainState) ? attackCoeff : releaseCoeff;
            gainState = coeff * gainState + (1.0f - coeff) * target;

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = delayBuffer.getWritePointer (ch);
                d[writePos] = data[ch][i];

                int readPos = writePos - lookaheadSamples;
                if (readPos < 0)
                    readPos += delaySize;

                const float out = d[readPos] * gainState;
                data[ch][i] = juce::jlimit (-ceiling, ceiling, out); // brickwall safety
            }

            if (++writePos >= delaySize)
                writePos = 0;

            maxReductionDb = juce::jmax (maxReductionDb,
                                         -juce::Decibels::gainToDecibels (gainState, -60.0f));
        }

        return maxReductionDb;
    }

private:
    void setRelease (float ms)
    {
        releaseMs = ms;
        const float t = juce::jmax (1.0f, ms) * 0.001f;
        releaseCoeff = std::exp (-1.0f / (t * static_cast<float> (sampleRate)));
    }

    // --- fixed-capacity monotonic deque of (sampleIndex, value) -------------
    struct MaxEntry { long long index = 0; float value = 0.0f; };

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

    void pushMax (float v)
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

    float  ceiling      = 1.0f;
    float  releaseMs    = 100.0f;
    float  attackCoeff  = 0.0f;
    float  releaseCoeff = 0.0f;
    float  gainState    = 1.0f;

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;

    std::vector<MaxEntry> dq;
    int dqCapacity = 1, dqHead = 0, dqTail = 0;
    long long sampleCounter = 0;
};
} // namespace mf
