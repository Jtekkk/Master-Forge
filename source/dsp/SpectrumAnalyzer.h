#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>

namespace mf
{
/**
    FFT spectrum analyzer with a lock-free hand-off.

    The audio thread fills a fixed buffer one block of mono-summed samples at a
    time; when it is full it is copied into the FFT scratch area and a flag is
    raised. The GUI thread runs the windowed FFT in pullMagnitudes() and clears
    the flag. The audio thread never refills while the flag is set, so the two
    threads never touch the scratch area at once.
*/
class SpectrumAnalyzer
{
public:
    static constexpr int fftOrder = 11;          // 2048-point FFT
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int numBins  = fftSize / 2;

    void prepare (double sampleRate)
    {
        currentSampleRate = sampleRate;
        fifoIndex = 0;
        std::fill (std::begin (fifo), std::end (fifo), 0.0f);
        nextBlockReady.store (false);
    }

    /** Audio thread: push a whole block (channels are averaged to mono). */
    void pushBuffer (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numCh = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        if (numCh <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                mono += buffer.getSample (ch, i);
            pushSample (mono / (float) numCh);
        }
    }

    /** GUI thread: if a new block is ready, run the FFT and fill @p magnitudes
        (linear, length numBins). Returns true when new data was produced. */
    bool pullMagnitudes (std::vector<float>& magnitudes)
    {
        if (! nextBlockReady.load())
            return false;

        window.multiplyWithWindowingTable (fftData, (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData);

        magnitudes.resize ((size_t) numBins);
        const float norm = 2.0f / (float) fftSize;
        for (int i = 0; i < numBins; ++i)
            magnitudes[(size_t) i] = fftData[i] * norm;

        nextBlockReady.store (false);
        return true;
    }

    double getSampleRate() const noexcept { return currentSampleRate; }

private:
    void pushSample (float sample) noexcept
    {
        if (fifoIndex == fftSize)
        {
            if (! nextBlockReady.load())
            {
                juce::FloatVectorOperations::clear (fftData, 2 * fftSize);
                juce::FloatVectorOperations::copy (fftData, fifo, fftSize);
                nextBlockReady.store (true);
            }
            fifoIndex = 0;
        }
        fifo[fifoIndex++] = sample;
    }

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize,
        juce::dsp::WindowingFunction<float>::hann };

    float fifo[fftSize] {};
    float fftData[2 * fftSize] {};
    int   fifoIndex = 0;
    std::atomic<bool> nextBlockReady { false };

    double currentSampleRate = 44100.0;
};
} // namespace mf
