// Lightweight runtime checks for the Master Forge DSP modules.
// Builds with -DMASTERFORGE_BUILD_TESTS=ON and runs without a host.

#include <juce_dsp/juce_dsp.h>

#include "dsp/Limiter.h"
#include "dsp/Compressor.h"
#include "dsp/Saturation.h"
#include "dsp/StereoWidth.h"
#include "dsp/ParametricEQ.h"
#include "dsp/LoudnessMeter.h"

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
        sat.prepare (spec);
        sat.setParameters (18.0f, 100.0f);

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
        eq.setParameters (100.0f, 6.0f, 500.0f, -4.0f, 1.0f, 3000.0f, 5.0f, 0.8f, 10000.0f, 4.0f);
        juce::AudioBuffer<float> buf (2, block);
        fillSine (buf, 100.0, sr, 0.3, 0);
        juce::AudioBuffer<float> dry (buf);
        juce::dsp::AudioBlock<float> blk (buf);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        for (int i = 0; i < 10; ++i) eq.process (ctx);
        check (allFinite (buf), "output is finite");
        check (std::abs (buf.getMagnitude (0, 0, block) - dry.getMagnitude (0, 0, block)) > 1.0e-4f,
               "low-shelf boost changes 100 Hz level");
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

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : std::to_string (failures) + " CHECK(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
