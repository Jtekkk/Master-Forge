#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <complex>
#include <cmath>

namespace mf
{
/**
    Four-band mastering EQ: low shelf, two parametric bells and a high shelf.

    Two modes:
      - Minimum phase: cascaded second-order IIR biquads (zero latency, the
        classic analog-style behaviour).
      - Linear phase: a single FIR whose kernel is derived from the combined
        magnitude response of the four bands, so there's no phase smearing
        (at the cost of latency = (taps-1)/2). The kernel is only rebuilt when a
        band actually changes.

    Mid/Side targeting (Stereo/Mid/Side) wraps either path.
*/
class ParametricEQ
{
public:
    enum Mode { stereo = 0, mid = 1, side = 2 };

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        iir.prepare (spec);

        // FIR starts as a unit impulse (pass-through) of the right length.
        kernel.assign ((size_t) firTaps, 0.0f);
        kernel[(size_t) (firTaps / 2)] = 1.0f;
        fir.state = new juce::dsp::FIR::Coefficients<float> (kernel.data(), (size_t) firTaps);
        fir.prepare (spec);

        freqBuf.assign ((size_t) fftSize, {});
        timeBuf.assign ((size_t) fftSize, {});
        cachedValid = false;
    }

    void reset() { iir.reset(); fir.reset(); }

    int getLatencySamples() const noexcept { return linearPhase ? (firTaps - 1) / 2 : 0; }

    void setParameters (float lowFreq,  float lowGainDb,
                        float lmFreq,   float lmGainDb,  float lmQ,
                        float hmFreq,   float hmGainDb,  float hmQ,
                        float highFreq, float highGainDb, bool useLinearPhase)
    {
        linearPhase = useLinearPhase;

        using Coefs = juce::dsp::IIR::Coefficients<float>;
        const auto g = [] (float dB) { return juce::Decibels::decibelsToGain (dB); };

        auto lowC  = Coefs::makeLowShelf  (sampleRate, lowFreq,  0.707f, g (lowGainDb));
        auto lmC   = Coefs::makePeakFilter (sampleRate, lmFreq,  lmQ,    g (lmGainDb));
        auto hmC   = Coefs::makePeakFilter (sampleRate, hmFreq,  hmQ,    g (hmGainDb));
        auto highC = Coefs::makeHighShelf (sampleRate, highFreq, 0.707f, g (highGainDb));

        *iir.get<0>().state = *lowC;
        *iir.get<1>().state = *lmC;
        *iir.get<2>().state = *hmC;
        *iir.get<3>().state = *highC;

        if (linearPhase)
        {
            const std::array<float, 10> v { lowFreq, lowGainDb, lmFreq, lmGainDb, lmQ,
                                            hmFreq, hmGainDb, hmQ, highFreq, highGainDb };
            if (! cachedValid || v != cached)
            {
                rebuildKernel (lowC, lmC, hmC, highC);
                cached = v;
                cachedValid = true;
            }
        }
    }

    void process (juce::AudioBuffer<float>& buffer, int msMode)
    {
        const bool ms = (buffer.getNumChannels() >= 2 && msMode != stereo);

        if (ms) encodeMS (buffer);

        if (ms)
        {
            juce::dsp::AudioBlock<float> block (buffer);
            auto target = block.getSingleChannelBlock (msMode == mid ? 0 : 1);
            juce::dsp::ProcessContextReplacing<float> ctx (target);
            if (linearPhase) fir.process (ctx); else iir.process (ctx);
        }
        else
        {
            juce::dsp::AudioBlock<float> block (buffer);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            if (linearPhase) fir.process (ctx); else iir.process (ctx);
        }

        if (ms) decodeMS (buffer);
    }

private:
    using IIRBand = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                   juce::dsp::IIR::Coefficients<float>>;

    void encodeMS (juce::AudioBuffer<float>& buffer)
    {
        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getWritePointer (1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float s = 0.5f * (L[i] - R[i]);
            L[i] = m; R[i] = s;
        }
    }

    void decodeMS (juce::AudioBuffer<float>& buffer)
    {
        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getWritePointer (1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float m = L[i];
            const float s = R[i];
            L[i] = m + s; R[i] = m - s;
        }
    }

    void rebuildKernel (const juce::dsp::IIR::Coefficients<float>::Ptr& low,
                        const juce::dsp::IIR::Coefficients<float>::Ptr& lm,
                        const juce::dsp::IIR::Coefficients<float>::Ptr& hm,
                        const juce::dsp::IIR::Coefficients<float>::Ptr& high)
    {
        // Zero-phase target spectrum = combined magnitude response.
        for (int k = 0; k <= fftSize / 2; ++k)
        {
            const double f = (double) k * sampleRate / fftSize;
            double mag = 1.0;
            if (k > 0 && f < sampleRate * 0.5)
                mag = low->getMagnitudeForFrequency (f, sampleRate)
                    * lm->getMagnitudeForFrequency (f, sampleRate)
                    * hm->getMagnitudeForFrequency (f, sampleRate)
                    * high->getMagnitudeForFrequency (f, sampleRate);
            freqBuf[(size_t) k] = { (float) mag, 0.0f };
            if (k > 0 && k < fftSize / 2)
                freqBuf[(size_t) (fftSize - k)] = { (float) mag, 0.0f };
        }

        fft.perform (freqBuf.data(), timeBuf.data(), true); // inverse -> symmetric impulse

        // Centre, window (Hann) and copy out a linear-phase FIR.
        const int half = firTaps / 2;
        for (int i = 0; i < firTaps; ++i)
        {
            const int src = ((i - half) % fftSize + fftSize) % fftSize;
            const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (firTaps - 1));
            kernel[(size_t) i] = timeBuf[(size_t) src].real() * w;
        }

        *fir.state = juce::dsp::FIR::Coefficients<float> (kernel.data(), (size_t) firTaps);
    }

    static constexpr int firTaps = 1025;          // latency 512
    static constexpr int fftOrder = 12;           // 4096-point design FFT
    static constexpr int fftSize = 1 << fftOrder;

    double sampleRate = 44100.0;
    bool   linearPhase = false;

    juce::dsp::ProcessorChain<IIRBand, IIRBand, IIRBand, IIRBand> iir;
    juce::dsp::ProcessorDuplicator<juce::dsp::FIR::Filter<float>,
                                   juce::dsp::FIR::Coefficients<float>> fir;

    juce::dsp::FFT fft { fftOrder };
    std::vector<juce::dsp::Complex<float>> freqBuf, timeBuf;
    std::vector<float> kernel;

    std::array<float, 10> cached {};
    bool cachedValid = false;
};
} // namespace mf
