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
    std::cout << "[TruePeak limiter chain]\n";
    {
        using OS = juce::dsp::Oversampling<float>;
        OS os (2, 2, OS::filterHalfBandFIREquiripple, true, true);
        os.initProcessing ((size_t) block);
        os.reset();
        const int osFactor = (int) os.getOversamplingFactor();

        mf::Limiter limOS;
        juce::dsp::ProcessSpec osSpec { sr * osFactor, (juce::uint32) (block * osFactor), 2 };
        limOS.prepare (osSpec);
        const float ceilDb = -1.0f;
        limOS.setParameters (ceilDb, 100.0f);
        const float ceilLin = juce::Decibels::decibelsToGain (ceilDb);

        OS osMeas (2, 2, OS::filterHalfBandFIREquiripple, true, true);  // estimates output true peak
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

            juce::dsp::AudioBlock<float> base (buf);
            auto up = os.processSamplesUp (base);
            const int oc = (int) up.getNumChannels();
            const int on = (int) up.getNumSamples();
            float* ptrs[8] = {};
            for (int c = 0; c < oc && c < 8; ++c) ptrs[c] = up.getChannelPointer ((size_t) c);
            juce::AudioBuffer<float> osBuf (ptrs, oc, on);
            limOS.process (osBuf);
            os.processSamplesDown (base);
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
        check (maxTruePeak <= ceilLin * 1.30f,
               "estimated true peak " + juce::String (juce::Decibels::gainToDecibels (maxTruePeak), 2).toStdString()
                   + " dB controlled near ceiling " + std::to_string (ceilDb) + " dB");
    }

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : std::to_string (failures) + " CHECK(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
