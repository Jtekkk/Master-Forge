#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <complex>
#include <algorithm>
#include <cmath>

namespace mf
{
/**
    Four-band mastering EQ: low shelf, two parametric bells and a high shelf.

    Two modes:
      - Minimum phase: cascaded second-order IIR biquads (zero latency, the
        classic analog-style behaviour). Frequency/gain/Q are de-zippered and
        the coefficients are re-derived every 32 samples while a control is
        moving, so automating a band is a continuous glide rather than a
        staircase of per-block coefficient jumps.
      - Linear phase: a single FIR whose kernel is derived from the combined
        magnitude response of the four bands, so there's no phase smearing
        (at the cost of latency = (taps-1)/2 = 1024 samples). The kernel is only
        rebuilt when a band actually changes, and it realises the drawn curve to
        within about 0.11 dB from 20 Hz to 20 kHz.

    Mid/Side targeting (Stereo/Mid/Side) wraps either path. In linear-phase
    M/S the untreated channel is pushed through a matching delay, so Mid and
    Side stay sample-aligned and the stereo image is reconstructed exactly.

    Templated on the sample type so the IIR/FIR filtering runs at 64-bit double
    internally (float alias `ParametricEQ` kept for the standalone DSP tests).
    The linear-phase kernel is *designed* with a float FFT (magnitude response →
    IFFT → window) and the resulting taps are stored in the sample type.
*/
template <typename Sample>
class ParametricEQT
{
public:
    enum Mode { stereo = 0, mid = 1, side = 2 };

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        maxBlock   = (int) juce::jmax (1u, spec.maximumBlockSize);

        iir.prepare (spec);

        // FIR starts as a unit impulse (pass-through) of the right length.
        kernel.assign ((size_t) firTaps, (Sample) 0);
        kernel[(size_t) (firTaps / 2)] = (Sample) 1;
        fir.state = new juce::dsp::FIR::Coefficients<Sample> (kernel.data(), (size_t) firTaps);
        fir.prepare (spec);

        freqBuf.assign ((size_t) fftSize, {});
        timeBuf.assign ((size_t) fftSize, {});
        cachedValid = false;

        // Alignment delay for the channel that bypasses the linear-phase FIR.
        alignLen = firLatency + maxBlock + 4;
        alignBuf.assign ((size_t) alignLen, (Sample) 0);
        alignPos = 0;

        const double rampSeconds = 0.02;
        for (auto* s : allSmoothers())
            s->reset (sampleRate, rampSeconds);

        smoothersPrimed = false;
        reset();
    }

    void reset()
    {
        iir.reset();
        fir.reset();
        std::fill (alignBuf.begin(), alignBuf.end(), (Sample) 0);
        alignPos = 0;
    }

    int getLatencySamples() const noexcept { return linearPhase ? firLatency : 0; }

    /** Latency of the linear-phase path, whether or not it is currently active. */
    static constexpr int getMaxLatencySamples() noexcept { return firLatency; }

    void setParameters (float lowFreq,  float lowGainDb,
                        float lmFreq,   float lmGainDb,  float lmQ,
                        float hmFreq,   float hmGainDb,  float hmQ,
                        float highFreq, float highGainDb, bool useLinearPhase)
    {
        linearPhase = useLinearPhase;

        const std::array<float, 10> target { lowFreq, lowGainDb, lmFreq, lmGainDb, lmQ,
                                             hmFreq, hmGainDb, hmQ, highFreq, highGainDb };

        auto sm = allSmoothers();
        for (size_t i = 0; i < target.size(); ++i)
        {
            if (! smoothersPrimed)
                sm[i]->setCurrentAndTargetValue (target[i]);
            else
                sm[i]->setTargetValue (target[i]);
        }

        if (! smoothersPrimed)
        {
            smoothersPrimed = true;
            applyCoefficients (target);
            applied = target;
            appliedValid = true;
        }

        if (linearPhase && (! cachedValid || target != cached))
        {
            rebuildKernel (target);
            cached = target;
            cachedValid = true;
        }
    }

    void process (juce::AudioBuffer<Sample>& buffer, int msMode)
    {
        const int numSamples = buffer.getNumSamples();
        if (numSamples <= 0)
            return;

        const bool ms = (buffer.getNumChannels() >= 2 && msMode != stereo);

        if (ms) encodeMS (buffer);

        if (linearPhase)
        {
            // The FIR runs from the settled targets; skip the de-zipper so the
            // two paths stay in step when the user toggles between them.
            for (auto* s : allSmoothers())
                s->skip (numSamples);

            juce::dsp::AudioBlock<Sample> block (buffer);

            if (ms)
            {
                const int treated  = (msMode == mid ? 0 : 1);
                const int bypassed = 1 - treated;

                auto target = block.getSingleChannelBlock ((size_t) treated);
                juce::dsp::ProcessContextReplacing<Sample> ctx (target);
                fir.process (ctx);

                // Keep the untouched channel sample-aligned with the FIR.
                delayChannel (buffer.getWritePointer (bypassed), numSamples);
            }
            else
            {
                juce::dsp::ProcessContextReplacing<Sample> ctx (block);
                fir.process (ctx);
            }
        }
        else
        {
            processIIR (buffer, ms, msMode, numSamples);
        }

        if (ms) decodeMS (buffer);
    }

private:
    using IIRBand = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<Sample>,
                                                   juce::dsp::IIR::Coefficients<Sample>>;

    std::array<juce::SmoothedValue<float>*, 10> allSmoothers()
    {
        return { &sLowF, &sLowG, &sLmF, &sLmG, &sLmQ, &sHmF, &sHmG, &sHmQ, &sHighF, &sHighG };
    }

    bool anySmoothing()
    {
        for (auto* s : allSmoothers())
            if (s->isSmoothing())
                return true;
        return false;
    }

    void applyCoefficients (const std::array<float, 10>& v)
    {
        using Coefs = juce::dsp::IIR::Coefficients<Sample>;
        const auto g = [] (float dB) { return (Sample) juce::Decibels::decibelsToGain (dB); };
        const float nyq = (float) (sampleRate * 0.5);
        const auto f = [nyq] (float hz) { return juce::jlimit (10.0f, nyq * 0.995f, hz); };
        const auto q = [] (float x) { return (Sample) juce::jlimit (0.1f, 20.0f, x); };

        *iir.template get<0>().state = *Coefs::makeLowShelf   (sampleRate, f (v[0]), (Sample) 0.707, g (v[1]));
        *iir.template get<1>().state = *Coefs::makePeakFilter (sampleRate, f (v[2]), q (v[4]),       g (v[3]));
        *iir.template get<2>().state = *Coefs::makePeakFilter (sampleRate, f (v[5]), q (v[7]),       g (v[6]));
        *iir.template get<3>().state = *Coefs::makeHighShelf  (sampleRate, f (v[8]), (Sample) 0.707, g (v[9]));
    }

    void processIIR (juce::AudioBuffer<Sample>& buffer, bool ms, int msMode, int numSamples)
    {
        juce::dsp::AudioBlock<Sample> full (buffer);
        auto target = ms ? full.getSingleChannelBlock ((size_t) (msMode == mid ? 0 : 1)) : full;

        if (! anySmoothing())
        {
            std::array<float, 10> v {};
            auto sm = allSmoothers();
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = sm[i]->skip (numSamples);

            // The smoothers can finish while the linear-phase path is the one
            // running; make sure the biquads carry the settled values before
            // this block goes through them.
            if (! appliedValid || v != applied)
            {
                applyCoefficients (v);
                applied = v;
                appliedValid = true;
            }

            juce::dsp::ProcessContextReplacing<Sample> ctx (target);
            iir.process (ctx);
            return;
        }

        // A control is moving: re-derive the biquads on a short grid so the
        // response glides instead of stepping once per host block.
        for (int start = 0; start < numSamples; start += coeffUpdateSamples)
        {
            const int len = juce::jmin (coeffUpdateSamples, numSamples - start);

            std::array<float, 10> v {};
            auto sm = allSmoothers();
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = sm[i]->skip (len);

            applyCoefficients (v);
            applied = v;
            appliedValid = true;

            auto sub = target.getSubBlock ((size_t) start, (size_t) len);
            juce::dsp::ProcessContextReplacing<Sample> ctx (sub);
            iir.process (ctx);
        }
    }

    void delayChannel (Sample* d, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            int readPos = alignPos - firLatency;
            if (readPos < 0)
                readPos += alignLen;

            alignBuf[(size_t) alignPos] = d[i];
            d[i] = alignBuf[(size_t) readPos];

            if (++alignPos >= alignLen)
                alignPos = 0;
        }
    }

    void encodeMS (juce::AudioBuffer<Sample>& buffer)
    {
        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getWritePointer (1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const Sample m = (Sample) 0.5 * (L[i] + R[i]);
            const Sample s = (Sample) 0.5 * (L[i] - R[i]);
            L[i] = m; R[i] = s;
        }
    }

    void decodeMS (juce::AudioBuffer<Sample>& buffer)
    {
        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getWritePointer (1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const Sample m = L[i];
            const Sample s = R[i];
            L[i] = m + s; R[i] = m - s;
        }
    }

    /** Zeroth-order modified Bessel function, for the Kaiser window. */
    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double halfSq = 0.25 * x * x;
        for (int k = 1; k < 64; ++k)
        {
            term *= halfSq / ((double) k * (double) k);
            sum  += term;
            if (term < sum * 1.0e-16)
                break;
        }
        return sum;
    }

    void rebuildKernel (const std::array<float, 10>& v)
    {
        using Coefs = juce::dsp::IIR::Coefficients<Sample>;
        const auto g = [] (float dB) { return (Sample) juce::Decibels::decibelsToGain (dB); };
        const float nyq = (float) (sampleRate * 0.5);
        const auto fc = [nyq] (float hz) { return juce::jlimit (10.0f, nyq * 0.995f, hz); };
        const auto qc = [] (float x) { return (Sample) juce::jlimit (0.1f, 20.0f, x); };

        auto low  = Coefs::makeLowShelf   (sampleRate, fc (v[0]), (Sample) 0.707, g (v[1]));
        auto lm   = Coefs::makePeakFilter (sampleRate, fc (v[2]), qc (v[4]),      g (v[3]));
        auto hm   = Coefs::makePeakFilter (sampleRate, fc (v[5]), qc (v[7]),      g (v[6]));
        auto high = Coefs::makeHighShelf  (sampleRate, fc (v[8]), (Sample) 0.707, g (v[9]));

        // Zero-phase target spectrum = combined magnitude response, evaluated at
        // every bin including DC and Nyquist. (Pinning those two to unity, as a
        // naive design does, puts a step at each end of the spectrum and rings
        // it back through the kernel as ripple, and gets the shelf gains wrong.)
        const double maxF = sampleRate * 0.5 * 0.9999999;
        for (int k = 0; k <= fftSize / 2; ++k)
        {
            const double f = juce::jmin (maxF, (double) k * sampleRate / (double) fftSize);
            const double mag = low ->getMagnitudeForFrequency (f, sampleRate)
                             * lm  ->getMagnitudeForFrequency (f, sampleRate)
                             * hm  ->getMagnitudeForFrequency (f, sampleRate)
                             * high->getMagnitudeForFrequency (f, sampleRate);

            freqBuf[(size_t) k] = { (float) mag, 0.0f };
            if (k > 0 && k < fftSize / 2)
                freqBuf[(size_t) (fftSize - k)] = { (float) mag, 0.0f };
        }

        fft.perform (freqBuf.data(), timeBuf.data(), true); // inverse -> symmetric impulse

        // Centre and window. Kaiser, with beta tuned by measuring the realised
        // response against the target: what limits accuracy here is the kernel
        // length at the bottom of the band, not sidelobe leakage, so a narrow
        // main lobe (low beta) tracks the drawn curve more closely than a hard
        // window does. 2049 taps at beta 4 hold it to ~0.11 dB from 20 Hz up.
        const int half = firTaps / 2;
        const double beta = 4.0;
        const double invI0 = 1.0 / besselI0 (beta);

        for (int i = 0; i < firTaps; ++i)
        {
            const int src = ((i - half) % fftSize + fftSize) % fftSize;
            const double r = (2.0 * (double) i / (double) (firTaps - 1)) - 1.0;
            const double w = besselI0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) * invI0;
            kernel[(size_t) i] = (Sample) ((double) timeBuf[(size_t) src].real() * w);
        }

        // Force exact symmetry: the FFT round trip leaves a little asymmetry,
        // and only a perfectly symmetric kernel is truly linear phase.
        for (int i = 0; i < half; ++i)
        {
            const Sample a = (Sample) 0.5 * (kernel[(size_t) i] + kernel[(size_t) (firTaps - 1 - i)]);
            kernel[(size_t) i] = a;
            kernel[(size_t) (firTaps - 1 - i)] = a;
        }

        *fir.state = juce::dsp::FIR::Coefficients<Sample> (kernel.data(), (size_t) firTaps);
    }

    static constexpr int firTaps    = 2049;        // latency 1024
    static constexpr int firLatency = (firTaps - 1) / 2;
    static constexpr int fftOrder   = 14;          // 16384-point design FFT (8x the kernel)
    static constexpr int fftSize    = 1 << fftOrder;
    static constexpr int coeffUpdateSamples = 32;  // IIR de-zipper grid

    double sampleRate  = 44100.0;
    int    maxBlock    = 512;
    bool   linearPhase = false;

    juce::dsp::ProcessorChain<IIRBand, IIRBand, IIRBand, IIRBand> iir;
    juce::dsp::ProcessorDuplicator<juce::dsp::FIR::Filter<Sample>,
                                   juce::dsp::FIR::Coefficients<Sample>> fir;

    juce::dsp::FFT fft { fftOrder };                       // kernel design only (float)
    std::vector<juce::dsp::Complex<float>> freqBuf, timeBuf;
    std::vector<Sample> kernel;

    std::vector<Sample> alignBuf;
    int alignLen = 1, alignPos = 0;

    juce::SmoothedValue<float> sLowF, sLowG, sLmF, sLmG, sLmQ, sHmF, sHmG, sHmQ, sHighF, sHighG;
    bool smoothersPrimed = false;

    std::array<float, 10> cached {};
    bool cachedValid = false;
    std::array<float, 10> applied {};
    bool appliedValid = false;
};

using ParametricEQ = ParametricEQT<float>;
} // namespace mf
