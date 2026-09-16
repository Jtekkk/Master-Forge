// Lightweight runtime checks for the Master Forge DSP modules.
// Builds with -DMASTERFORGE_BUILD_TESTS=ON and runs without a host.

#include <juce_dsp/juce_dsp.h>

#include "dsp/Limiter.h"
#include "dsp/Compressor.h"
#include "dsp/MultibandCompressor.h"
#include "dsp/Saturation.h"
#include "dsp/StereoWidth.h"
#include "dsp/ParametricEQ.h"
#include "dsp/LoudnessMeter.h"
#include "dsp/SpectrumAnalyzer.h"

#include <iostream>
#include <string>
#include <cmath>
#include <vector>

namespace
{
int failures = 0;
constexpr double kPi = juce::MathConstants<double>::pi;

void check (bool cond, const std::string& msg)
{
    std::cout << (cond ? "  ok   : " : "  FAIL : ") << msg << '\n';
    if (! cond) ++failures;
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

void fillSine (juce::AudioBuffer<float>& b, double freq, double sr, double amp, long long startSample)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, (float) (amp * std::sin (2.0 * kPi * freq * (double) (startSample + i) / sr)));
}
}

int main()
{
    const double sr = 48000.0;
    const int    block = 512;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };

    std::cout << "Master Forge DSP checks @ " << sr << " Hz\n";

    // ---- Limiter: output must never exceed the ceiling -------------------
    std::cout << "[Limiter]\n";
    {
        mf::Limiter lim;
        lim.prepare (spec);
        const float ceilDb = -1.0f;
        lim.setParameters (ceilDb, 100.0f);
        const float ceilLin = juce::Decibels::decibelsToGain (ceilDb);

        float maxOut = 0.0f;
        bool finite = true;
        long long n = 0;
        for (int blk = 0; blk < 300; ++blk)
        {
            juce::AudioBuffer<float> buf (2, block);
            fillSine (buf, 220.0, sr, 1.6, n);   // way over 0 dBFS
            n += block;
            lim.process (buf);
            finite = finite && allFinite (buf);
            for (int ch = 0; ch < 2; ++ch)
                maxOut = juce::jmax (maxOut, buf.getMagnitude (ch, 0, block));
        }
        check (finite, "output is finite for hot input");
        check (maxOut <= ceilLin + 1.0e-4f,
               "peak " + juce::String (juce::Decibels::gainToDecibels (maxOut), 3).toStdString()
                   + " dB stays under ceiling " + std::to_string (ceilDb) + " dB");
    }

    // ---- Compressor: loud input above threshold gets reduced -------------
    std::cout << "[Compressor]\n";
    {
        mf::Compressor comp;
        comp.prepare (spec);
        comp.setParameters (-20.0f, 4.0f, 5.0f, 80.0f, 6.0f, 0.0f);

        float maxGr = 0.0f;
        bool finite = true;
        long long n = 0;
        for (int blk = 0; blk < 200; ++blk)
        {
            juce::AudioBuffer<float> buf (2, block);
            fillSine (buf, 100.0, sr, 0.5, n);   // ~-6 dBFS, above -20 dB threshold
            n += block;
            maxGr = juce::jmax (maxGr, comp.process (buf));
            finite = finite && allFinite (buf);
        }
        check (finite, "output is finite");
        check (maxGr > 1.0f, "applies gain reduction (" + juce::String (maxGr, 2).toStdString() + " dB) on loud input");

        // Quiet input below threshold should pass essentially untouched.
        comp.reset();
        float grQuiet = 0.0f;
        long long m = 0;
        for (int blk = 0; blk < 50; ++blk)
        {
            juce::AudioBuffer<float> buf (2, block);
            fillSine (buf, 100.0, sr, 0.02, m);  // ~-34 dBFS
            m += block;
            grQuiet = juce::jmax (grQuiet, comp.process (buf));
        }
        check (grQuiet < 0.5f, "near-zero reduction (" + juce::String (grQuiet, 3).toStdString() + " dB) on quiet input");
    }

    // ---- Saturation: tames peaks, stays finite ---------------------------
    std::cout << "[Saturation]\n";
    {
        mf::Saturation sat;
        sat.prepare (spec, 2);
        sat.setThd (80.0f);

        juce::AudioBuffer<float> buf (2, block);
        fillSine (buf, 440.0, sr, 0.9, 0);
        const float inPeak = buf.getMagnitude (0, 0, block);
        for (int i = 0; i < 20; ++i) sat.process (buf), fillSine (buf, 440.0, sr, 0.9, 0);
        sat.process (buf);
        check (allFinite (buf), "output is finite");
        check (buf.getMagnitude (0, 0, block) < inPeak, "rounds peaks below the input level");
    }

    // ---- Stereo width: 0% collapses to mono ------------------------------
    std::cout << "[StereoWidth]\n";
    {
        mf::StereoWidth w;
        w.prepare (spec);
        w.setWidth (0.0f);
        juce::AudioBuffer<float> buf (2, block);
        for (int blk = 0; blk < 30; ++blk)   // let the smoother settle to 0%
        {
            for (int i = 0; i < block; ++i) { buf.setSample (0, i, 0.4f); buf.setSample (1, i, -0.4f); }
            w.process (buf);
        }
        float maxDiff = 0.0f;
        for (int i = 0; i < block; ++i)
            maxDiff = juce::jmax (maxDiff, std::abs (buf.getSample (0, i) - buf.getSample (1, i)));
        check (allFinite (buf), "output is finite");
        check (maxDiff < 1.0e-3f, "L and R collapse to mono at 0% width");
    }

    // ---- Parametric EQ: changes the signal, stays finite -----------------
    std::cout << "[ParametricEQ]\n";
    {
        mf::ParametricEQ eq;
        eq.prepare (spec);
        eq.setParameters (100.0f, 6.0f, 500.0f, -4.0f, 1.0f, 3000.0f, 5.0f, 0.8f, 10000.0f, 4.0f, false);
        juce::AudioBuffer<float> buf (2, block);
        fillSine (buf, 100.0, sr, 0.3, 0);
        const float dryPeak = buf.getMagnitude (0, 0, block);
        for (int i = 0; i < 10; ++i) eq.process (buf, mf::ParametricEQ::stereo);
        check (allFinite (buf), "output is finite");
        check (std::abs (buf.getMagnitude (0, 0, block) - dryPeak) > 1.0e-4f,
               "low-shelf boost changes 100 Hz level");

        // Side-mode EQ must leave a mono (centre) signal untouched (side == 0).
        eq.reset();
        juce::AudioBuffer<float> monoBuf (2, block);
        fillSine (monoBuf, 100.0, sr, 0.3, 0);   // identical L and R => pure mid
        for (int i = 0; i < 10; ++i) eq.process (monoBuf, mf::ParametricEQ::side);
        float msDiff = 0.0f;
        for (int i = 0; i < block; ++i)
        {
            const float ref = (float) (0.3 * std::sin (2.0 * kPi * 100.0 * (double) i / sr));
            msDiff = juce::jmax (msDiff, std::abs (monoBuf.getSample (0, i) - ref));
        }
        check (msDiff < 1.0e-3f, "Side-mode EQ leaves a centred signal unchanged");
    }

    // ---- Loudness meter: plausible LUFS for a known sine -----------------
    std::cout << "[LoudnessMeter]\n";
    {
        mf::LoudnessMeter meter;
        meter.prepare (sr, 2);
        long long n = 0;
        for (int blk = 0; blk < 200; ++blk)   // ~2.1 s, fills the momentary window
        {
            juce::AudioBuffer<float> buf (2, block);
            fillSine (buf, 1000.0, sr, 0.5, n);  // -6 dBFS, 1 kHz
            n += block;
            meter.process (buf);
        }
        const float m = meter.getMomentaryLUFS();
        const float in = meter.getIntegratedLUFS();
        check (std::isfinite (m) && m > -30.0f && m < 0.0f,
               "momentary LUFS is plausible (" + juce::String (m, 2).toStdString() + ")");
        check (std::isfinite (in) && in > -30.0f && in < 0.0f,
               "integrated LUFS is plausible (" + juce::String (in, 2).toStdString() + ")");
    }

    // ---- Multiband compressor: reconstruction + band isolation ----------
    std::cout << "[MultibandCompressor]\n";
    {
        mf::MultibandCompressor mb;
        mb.prepare (spec);
        // No compression anywhere (ratio 1) -> bands should sum back flat.
        mb.setParameters (200.0f, 2500.0f, 10.0f, 150.0f, 6.0f,
                          0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        float outPeak = 0.0f; bool finite = true;
        long long n = 0;
        for (int blk = 0; blk < 60; ++blk)
        {
            juce::AudioBuffer<float> buf (2, block);
            fillSine (buf, 1000.0, sr, 0.5, n);
            n += block;
            mb.process (buf);
            finite = finite && allFinite (buf);
            if (blk > 5) outPeak = juce::jmax (outPeak, buf.getMagnitude (0, 0, block));
        }
        check (finite, "output is finite");
        check (std::abs (outPeak - 0.5f) < 0.06f,
               "bands reconstruct flat (1 kHz peak " + juce::String (outPeak, 3).toStdString() + " ~= 0.5)");

        // Loud low-frequency tone should only compress the low band.
        mb.reset();
        mb.setParameters (200.0f, 2500.0f, 5.0f, 120.0f, 2.0f,
                          -30.0f, 6.0f, 0.0f, -30.0f, 6.0f, 0.0f, -30.0f, 6.0f, 0.0f);
        long long m = 0;
        for (int blk = 0; blk < 120; ++blk)
        {
            juce::AudioBuffer<float> buf (2, block);
            fillSine (buf, 50.0, sr, 0.5, m);
            m += block;
            mb.process (buf);
        }
        check (mb.getReductionLow() > 1.0f,
               "low band compresses 50 Hz tone (" + juce::String (mb.getReductionLow(), 2).toStdString() + " dB)");
        check (mb.getReductionMid() < 0.5f && mb.getReductionHigh() < 0.5f,
               "mid/high bands stay idle on a low tone");
    }

    // ---- Spectrum analyzer: peak bin matches the input tone --------------
    std::cout << "[SpectrumAnalyzer]\n";
    {
        mf::SpectrumAnalyzer an;
        an.prepare (sr);
        long long n = 0;
        for (int blk = 0; blk < 12; ++blk)  // > fftSize samples
        {
            juce::AudioBuffer<float> buf (2, block);
            fillSine (buf, 1000.0, sr, 0.5, n);
            n += block;
            an.pushBuffer (buf);
        }
        std::vector<float> mags;
        const bool got = an.pullMagnitudes (mags);
        check (got && ! mags.empty(), "produces FFT magnitudes");
        if (got)
        {
            int peakBin = 0;
            for (int i = 1; i < (int) mags.size(); ++i)
                if (mags[(size_t) i] > mags[(size_t) peakBin]) peakBin = i;
            const double peakFreq = peakBin * sr / mf::SpectrumAnalyzer::fftSize;
            check (std::abs (peakFreq - 1000.0) < 50.0,
                   "peak bin at " + juce::String (peakFreq, 1).toStdString() + " Hz tracks the 1 kHz tone");
        }
    }

    // ---- True-peak limiter chain (oversample -> limit -> downsample) -----
    // ---- True peak: the detector oversamples, the audio path does not -----
    std::cout << "[TruePeak limiter]\n";
    {
        using OS = juce::dsp::Oversampling<float>;

        mf::Limiter lim;
        lim.prepare (spec, 2);              // 4x detector
        const float ceilDb = -1.0f;
        lim.setParameters (ceilDb, 100.0f);
        lim.setTruePeak (true);
        const float ceilLin = juce::Decibels::decibelsToGain (ceilDb);

        // Toggling true peak must not move the latency the host compensates.
        const int latTrue = lim.getLatencySamples();
        lim.setTruePeak (false);
        const int latSample = lim.getLatencySamples();
        lim.setTruePeak (true);
        check (latTrue == latSample,
               "latency is identical with true peak on and off (" + std::to_string (latTrue) + ")");

        OS osMeas (2, 4, OS::filterHalfBandFIREquiripple, true, true);  // 16x reference
        osMeas.initProcessing ((size_t) block);
        osMeas.reset();

        float maxTruePeak = 0.0f;
        bool finite = true;
        long long n = 0;
        for (int blk = 0; blk < 300; ++blk)
        {
            juce::AudioBuffer<float> buf (2, block);
            fillSine (buf, 11000.0, sr, 1.4, n);   // hot, inter-sample-peak prone
            n += block;

            lim.process (buf);
            finite = finite && allFinite (buf);

            if (blk > 30)   // after the chains settle
            {
                juce::dsp::AudioBlock<float> mb (buf);
                auto upm = osMeas.processSamplesUp (mb);
                for (int c = 0; c < (int) upm.getNumChannels(); ++c)
                    for (int i = 0; i < (int) upm.getNumSamples(); ++i)
                        maxTruePeak = juce::jmax (maxTruePeak, std::abs (upm.getChannelPointer ((size_t) c)[i]));
                osMeas.processSamplesDown (mb);
            }
        }
        check (finite, "output is finite");
        // 0.15 dB of headroom for the 4x detector's own estimation error.
        check (maxTruePeak <= ceilLin * 1.0175f,
               "measured true peak " + juce::String (juce::Decibels::gainToDecibels (maxTruePeak), 2).toStdString()
                   + " dB holds the ceiling " + std::to_string (ceilDb) + " dB");
    }

    // ---- Limiter gain smoothing ------------------------------------------
    // The output has to be the input times a smooth gain envelope: if the
    // attack overshoots and the safety clamp has to catch it, the waveform is
    // flat-topped and the gain recovered from out/in shows a sharp kink there.
    std::cout << "[Limiter smoothing]\n";
    {
        mf::LimiterT<double> lim;
        lim.prepare (spec);
        const float ceilDb  = -1.0f;
        const double ceilLin = juce::Decibels::decibelsToGain (ceilDb);
        lim.setParameters (ceilDb, 100.0f);
        const int lat = lim.getLatencySamples();

        // Transient-heavy material: this is what makes a one-pole attack
        // overshoot into the clamp.
        std::vector<double> dryHist;
        double maxOut = 0.0, maxGainStep = 0.0, prevGain = 1.0;
        bool havePrev = false;
        long long n = 0;
        for (int blk = 0; blk < 400; ++blk)
        {
            juce::AudioBuffer<double> buf (2, block);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const double t = (double) (n + i) / sr;
                    const double env = (std::fmod (t, 0.05) < 0.004) ? 3.0 : 0.25;
                    buf.setSample (ch, i, env * std::sin (2.0 * kPi * 320.0 * t));
                }
            for (int i = 0; i < block; ++i)
                dryHist.push_back (buf.getSample (0, i));
            const int base = (int) dryHist.size() - block;
            n += block;

            lim.process (buf);

            for (int i = 0; i < block; ++i)
            {
                const double a = std::abs (buf.getSample (0, i));
                if (blk >= 4)
                    maxOut = juce::jmax (maxOut, a);

                const int src = base + i - lat;
                if (blk < 4 || src < 0) continue;

                const double dry = dryHist[(size_t) src];
                if (std::abs (dry) < 0.05) { havePrev = false; continue; }

                const double gain = buf.getSample (0, i) / dry;
                if (havePrev)
                    maxGainStep = juce::jmax (maxGainStep, std::abs (gain - prevGain));
                prevGain = gain;
                havePrev = true;
            }
        }
        check (maxOut <= ceilLin + 1.0e-9,
               "transient peaks stay at or under the ceiling ("
                   + juce::String (juce::Decibels::gainToDecibels (maxOut), 4).toStdString() + " dB)");
        // The ramp spans the whole look-ahead window, so per-sample steps are tiny.
        check (maxGainStep < 0.02,
               "gain envelope is continuous - no clamping kink (max step "
                   + juce::String (maxGainStep, 5).toStdString() + ")");
    }

    // ---- Linear-phase EQ: alignment and realised magnitude ---------------
    std::cout << "[Linear-phase EQ]\n";
    {
        // A flat linear-phase EQ in Mid mode must give back the input delayed by
        // the FIR latency on *both* channels. If the untreated channel skipped
        // the delay, M and S would be 512 samples apart and the decoded stereo
        // image would comb out.
        mf::ParametricEQ lp;
        lp.prepare (spec);
        lp.setParameters (100.0f, 0.0f, 500.0f, 0.0f, 1.0f, 3000.0f, 0.0f, 0.8f, 10000.0f, 0.0f, true);
        const int lat = lp.getLatencySamples();

        std::vector<float> histL, histR;
        float worstErr = 0.0f;
        long long n = 0;
        for (int blk = 0; blk < 12; ++blk)
        {
            juce::AudioBuffer<float> buf (2, block);
            for (int i = 0; i < block; ++i)
            {
                const double t = (double) (n + i) / sr;
                buf.setSample (0, i, (float) (0.4 * std::sin (2.0 * kPi * 700.0 * t)));
                buf.setSample (1, i, (float) (0.25 * std::sin (2.0 * kPi * 1900.0 * t + 1.1)));
            }
            for (int i = 0; i < block; ++i) { histL.push_back (buf.getSample (0, i)); histR.push_back (buf.getSample (1, i)); }
            n += block;

            lp.process (buf, mf::ParametricEQ::mid);

            const int base = (int) histL.size() - block;
            for (int i = 0; i < block; ++i)
            {
                const int src = base + i - lat;
                if (src < 2 * block) continue;      // let the FIR fill
                worstErr = juce::jmax (worstErr, std::abs (buf.getSample (0, i) - histL[(size_t) src]));
                worstErr = juce::jmax (worstErr, std::abs (buf.getSample (1, i) - histR[(size_t) src]));
            }
        }
        check (worstErr < 1.0e-4f,
               "flat linear-phase M/S is a pure delay on both channels (max error "
                   + juce::String (worstErr, 7).toStdString() + ")");

        // Realised response vs. the target curve across the band, measured from
        // the FIR's own impulse response. This is what the window design and the
        // DC/Nyquist handling in the kernel actually buy.
        {
            mf::ParametricEQ probeEq;
            juce::dsp::ProcessSpec impSpec { sr, 8192, 2 };
            probeEq.prepare (impSpec);
            probeEq.setParameters (100.0f, 6.0f, 500.0f, -5.0f, 2.0f,
                                   3000.0f, 9.0f, 4.0f, 10000.0f, -6.0f, true);

            constexpr int irOrder = 13, irSize = 1 << irOrder;
            juce::AudioBuffer<float> imp (2, irSize);
            imp.clear();
            imp.setSample (0, 0, 1.0f);
            imp.setSample (1, 0, 1.0f);
            probeEq.process (imp, mf::ParametricEQ::stereo);

            juce::dsp::FFT irFft (irOrder);
            std::vector<juce::dsp::Complex<float>> in ((size_t) irSize), out ((size_t) irSize);
            for (int i = 0; i < irSize; ++i)
                in[(size_t) i] = { imp.getSample (0, i), 0.0f };
            irFft.perform (in.data(), out.data(), false);

            using C = juce::dsp::IIR::Coefficients<float>;
            const auto gg = [] (float dB) { return juce::Decibels::decibelsToGain (dB); };
            auto rLow  = C::makeLowShelf   (sr, 100.0f,   0.707f, gg (6.0f));
            auto rLm   = C::makePeakFilter (sr, 500.0f,   2.0f,   gg (-5.0f));
            auto rHm   = C::makePeakFilter (sr, 3000.0f,  4.0f,   gg (9.0f));
            auto rHigh = C::makeHighShelf  (sr, 10000.0f, 0.707f, gg (-6.0f));

            double worstDb = 0.0, worstHz = 0.0;
            for (int k = 1; k < irSize / 2; ++k)
            {
                const double f = (double) k * sr / (double) irSize;
                if (f < 20.0 || f > 20000.0) continue;

                const double target = rLow ->getMagnitudeForFrequency (f, sr)
                                    * rLm  ->getMagnitudeForFrequency (f, sr)
                                    * rHm  ->getMagnitudeForFrequency (f, sr)
                                    * rHigh->getMagnitudeForFrequency (f, sr);
                const double errDb = std::abs (juce::Decibels::gainToDecibels (std::abs (out[(size_t) k]))
                                             - juce::Decibels::gainToDecibels (target));
                if (errDb > worstDb) { worstDb = errDb; worstHz = f; }
            }
            check (worstDb < 0.15,
                   "linear-phase FIR tracks the target curve within "
                       + juce::String (worstDb, 3).toStdString() + " dB (worst at "
                       + juce::String (worstHz, 0).toStdString() + " Hz)");
        }

    }

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : std::to_string (failures) + " CHECK(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
