// Offline audio analysis: runs test signals through MasterForgeAudioProcessor
// and measures THD (distortion) and frequency-response flatness, so we can find
// what is colouring/distorting the sound. Build with -DMASTERFORGE_BUILD_TESTS=ON.

#include "PluginProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <functional>

namespace
{
constexpr double kPi = juce::MathConstants<double>::pi;
constexpr double SR  = 48000.0;
constexpr int    BLK = 512;

void setP (MasterForgeAudioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.getAPVTS().getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

// Run `total` samples of a generated signal through the processor; return
// channel-0 output with the first `skip` samples dropped (latency + settling).
std::vector<float> run (MasterForgeAudioProcessor& p, int total, int skip,
                        const std::function<float (long long)>& gen)
{
    std::vector<float> out;
    out.reserve ((size_t) total);
    juce::MidiBuffer midi;
    long long n = 0;
    while ((int) out.size() < total)
    {
        juce::AudioBuffer<float> buf (2, BLK);
        for (int i = 0; i < BLK; ++i)
        {
            const float s = gen (n + i);
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        n += BLK;
        p.processBlock (buf, midi);
        for (int i = 0; i < BLK; ++i)
            out.push_back (buf.getSample (0, i));
    }
    if (skip > 0 && skip < (int) out.size())
        out.erase (out.begin(), out.begin() + skip);
    return out;
}

double thdPercent (const std::vector<float>& x, double f0, int order = 15)
{
    const int N = 1 << order;
    if ((int) x.size() < N) return -1.0;

    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> data (2 * (size_t) N, 0.0f);
    for (int i = 0; i < N; ++i) data[(size_t) i] = x[x.size() - (size_t) N + (size_t) i];
    win.multiplyWithWindowingTable (data.data(), (size_t) N);
    fft.performFrequencyOnlyForwardTransform (data.data());

    const auto energy = [&] (double freq)
    {
        const int bin = (int) std::round (freq * N / SR);
        double e = 0.0;
        for (int b = bin - 2; b <= bin + 2; ++b)
            if (b > 0 && b < N / 2) e += (double) data[(size_t) b] * data[(size_t) b];
        return e;
    };

    const double fund = energy (f0);
    double harm = 0.0;
    for (int h = 2; h * f0 < SR * 0.5; ++h) harm += energy (h * f0);
    return fund > 0.0 ? 100.0 * std::sqrt (harm / fund) : -1.0;
}

// RMS-based gain (dB) of the processor at a single frequency.
double gainDbAt (MasterForgeAudioProcessor& p, double freq, double amp)
{
    auto y = run (p, 1 << 15, 8192, [=] (long long n)
        { return (float) (amp * std::sin (2.0 * kPi * freq * (double) n / SR)); });
    double sum = 0.0;
    for (float v : y) sum += (double) v * v;
    const double rmsOut = std::sqrt (sum / (double) y.size());
    const double rmsIn  = amp / std::sqrt (2.0);
    return juce::Decibels::gainToDecibels (rmsOut / rmsIn);
}

const double F0 = std::round (1000.0 * (1 << 15) / SR) * SR / (1 << 15); // bin-aligned ~1 kHz

// Render a fixed input through the processor, chunked into the given block
// sizes (cycled). Fresh processor each time. Returns channel-0 output.
std::vector<float> render (const std::vector<float>& input, int prepareBlock,
                           const std::vector<int>& blockSizes, bool truePeak, bool compress)
{
    MasterForgeAudioProcessor p;
    p.setPlayConfigDetails (2, 2, SR, prepareBlock);
    p.prepareToPlay (SR, prepareBlock);
    setP (p, mf::pid::limTruePeak, truePeak ? 1.0f : 0.0f);
    if (compress)   // engage some compression so the dynamic path is exercised
    {
        setP (p, mf::pid::mbLowRatio, 4.0f); setP (p, mf::pid::mbMidRatio, 4.0f); setP (p, mf::pid::mbHiRatio, 4.0f);
        setP (p, mf::pid::mbLowThresh, -30.0f); setP (p, mf::pid::mbMidThresh, -30.0f); setP (p, mf::pid::mbHiThresh, -30.0f);
    }

    std::vector<float> out;
    out.reserve (input.size());
    juce::MidiBuffer midi;
    int pos = 0, bi = 0;
    while (pos < (int) input.size())
    {
        const int bs = juce::jmin (blockSizes[(size_t) (bi++ % blockSizes.size())], (int) input.size() - pos);
        juce::AudioBuffer<float> buf (2, bs);
        for (int i = 0; i < bs; ++i) { buf.setSample (0, i, input[(size_t) (pos + i)]); buf.setSample (1, i, input[(size_t) (pos + i)]); }
        p.processBlock (buf, midi);
        for (int i = 0; i < bs; ++i) out.push_back (buf.getSample (0, i));
        pos += bs;
    }
    return out;
}

void blockSizeTest (const juce::String& label, bool truePeak, bool compress)
{
    const int total = 1 << 16;
    std::vector<float> input ((size_t) total);
    for (int n = 0; n < total; ++n)
        input[(size_t) n] = (float) (0.5 * std::sin (2.0 * kPi * 110.0 * n / SR));

    auto ref = render (input, 512,  { 512 },                truePeak, compress);
    auto b64 = render (input, 512,  { 64 },                 truePeak, compress);
    auto var = render (input, 2048, { 64, 256, 37, 512, 129 }, truePeak, compress);

    const int skip = 4096;
    const auto maxDiff = [&] (const std::vector<float>& a, const std::vector<float>& b)
    {
        double m = 0.0;
        for (int i = skip; i < (int) std::min (a.size(), b.size()); ++i)
            m = std::max (m, (double) std::abs (a[(size_t) i] - b[(size_t) i]));
        return m;
    };

    const double d64 = maxDiff (ref, b64);
    const double dvar = maxDiff (ref, var);
    std::cout << "  " << label
              << ":  512 vs 64 = " << juce::String (juce::Decibels::gainToDecibels ((float) d64 + 1e-12f), 1) << " dB,"
              << "  512 vs variable = " << juce::String (juce::Decibels::gainToDecibels ((float) dvar + 1e-12f), 1) << " dB"
              << (juce::jmax (d64, dvar) > 1.0e-4 ? "   <-- INCONSISTENT" : "   ok") << "\n";
}

// Energy of all non-fundamental in-band content, in dB below the fundamental.
// For a high enough tone every harmonic folds, so this is the aliasing floor.
double aliasingDb (const juce::String& name, double freq, double ampDb,
                   const std::function<void (MasterForgeAudioProcessor&)>& setup)
{
    MasterForgeAudioProcessor p;
    p.setPlayConfigDetails (2, 2, SR, BLK);
    p.prepareToPlay (SR, BLK);
    setup (p);

    const int order = 16, N = 1 << order;
    const double f0 = std::round (freq * N / SR) * SR / N;   // bin-aligned
    const double amp = juce::Decibels::decibelsToGain (ampDb);
    auto y = run (p, 1 << 17, 8192 + p.getLatencySamples(),
                  [=] (long long n) { return (float) (amp * std::sin (2.0 * kPi * f0 * (double) n / SR)); });

    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> data (2 * (size_t) N, 0.0f);
    for (int i = 0; i < N; ++i) data[(size_t) i] = y[y.size() - (size_t) N + (size_t) i];
    win.multiplyWithWindowingTable (data.data(), (size_t) N);
    fft.performFrequencyOnlyForwardTransform (data.data());

    const int fundBin = (int) std::round (f0 * N / SR);
    double fund = 0.0, other = 0.0;
    for (int b = 2; b < N / 2; ++b)
    {
        const double e = (double) data[(size_t) b] * data[(size_t) b];
        if (std::abs (b - fundBin) <= 4) fund += e;
        else                             other += e;
    }
    const double dB = fund > 0.0 ? 10.0 * std::log10 (other / fund) : -200.0;
    std::cout << "  " << name << " (" << (int) f0 << " Hz): aliasing = " << juce::String (dB, 1) << " dB\n";
    return dB;
}

void thdScenario (const juce::String& name, double freq, double ampDb,
                  const std::function<void (MasterForgeAudioProcessor&)>& setup)
{
    MasterForgeAudioProcessor p;
    p.setPlayConfigDetails (2, 2, SR, BLK);
    p.prepareToPlay (SR, BLK);
    setup (p);
    const double amp = juce::Decibels::decibelsToGain (ampDb);
    const double f = std::round (freq * (1 << 15) / SR) * SR / (1 << 15); // bin-aligned
    auto y = run (p, 1 << 16, 8192 + p.getLatencySamples(),
                  [=] (long long n) { return (float) (amp * std::sin (2.0 * kPi * f * (double) n / SR)); });
    std::cout << "  " << name << "  (" << ampDb << " dBFS @ " << juce::String (f, 0) << " Hz):  THD = "
              << juce::String (thdPercent (y, f), 3) << " %\n";
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::cout << "Master Forge audio analysis @ " << SR << " Hz\n\n";

    std::cout << "[THD — distortion of a steady sine]\n";
    thdScenario ("bypass             ", 1000.0, -12.0f, [] (auto& p) { setP (p, mf::pid::bypass, 1.0f); });
    thdScenario ("default 1k         ", 1000.0, -12.0f, [] (auto&) {});
    thdScenario ("default 1k (hot)   ", 1000.0,  -1.0f, [] (auto&) {});
    std::cout << "  -- low frequency (peak-detector compressor stress) --\n";
    thdScenario ("bypass 60Hz        ",   60.0, -12.0f, [] (auto& p) { setP (p, mf::pid::bypass, 1.0f); });
    thdScenario ("default 60Hz       ",   60.0, -12.0f, [] (auto&) {});
    thdScenario ("default 60Hz (R=4) ",   60.0, -12.0f, [] (auto& p) { setP (p, mf::pid::mbLowRatio, 4.0f); });
    thdScenario ("default 90Hz       ",   90.0, -10.0f, [] (auto&) {});

    std::cout << "\n[Aliasing — nonlinear stages, high tone (lower dB = cleaner)]\n";
    aliasingDb ("clean (sat off)  ", 14000.0, -6.0f, [] (auto&) {});
    aliasingDb ("saturation 12/100", 14000.0, -6.0f, [] (auto& p) { setP (p, mf::pid::satDrive, 12.0f); setP (p, mf::pid::satMix, 100.0f); });
    aliasingDb ("hot into limiter ", 14000.0,  3.0f, [] (auto& p) { setP (p, mf::pid::limCeiling, -6.0f); });

    std::cout << "\n[Block-size consistency — output must not depend on buffering]\n";
    blockSizeTest ("default,         TP on ", true,  false);
    blockSizeTest ("default,         TP off", false, false);
    blockSizeTest ("compressing,     TP on ", true,  true);
    blockSizeTest ("compressing,     TP off", false, true);

    std::cout << "\n[Divergence probe — default, TP off, where do 512 and 64 differ?]\n";
    {
        const int total = 1 << 14;
        std::vector<float> input ((size_t) total);
        for (int n = 0; n < total; ++n) input[(size_t) n] = (float) (0.5 * std::sin (2.0 * kPi * 110.0 * n / SR));
        auto a = render (input, 512, { 512 }, false, false);
        auto b = render (input, 512, { 64 },  false, false);
        int shown = 0;
        for (int i = 6000; i < total && shown < 8; ++i)   // steady state
        {
            if (std::abs (a[(size_t) i] - b[(size_t) i]) > 1.0e-3f)
            {
                std::cout << "  i=" << i << " (i%64=" << (i % 64) << ", i%512=" << (i % 512) << ")  "
                          << "512=" << juce::String (a[(size_t) i], 4) << "  64=" << juce::String (b[(size_t) i], 4) << "\n";
                ++shown;
            }
        }
        if (shown == 0) std::cout << "  (no divergence > 1e-3)\n";
    }

    std::cout << "\n[Module bisect — which stage is block-size dependent?]\n";
    {
        juce::dsp::ProcessSpec spec { SR, 512, 2 };
        const int total = 8192;
        std::vector<float> in ((size_t) total);
        for (int n = 0; n < total; ++n) in[(size_t) n] = (float) (0.5 * std::sin (2.0 * kPi * 110.0 * n / SR));

        auto renderMod = [&] (int bs, std::function<void (juce::AudioBuffer<float>&)> proc)
        {
            std::vector<float> out; out.reserve ((size_t) total);
            int pos = 0;
            while (pos < total)
            {
                const int b = juce::jmin (bs, total - pos);
                juce::AudioBuffer<float> buf (2, b);
                for (int i = 0; i < b; ++i) { buf.setSample (0, i, in[(size_t) (pos + i)]); buf.setSample (1, i, in[(size_t) (pos + i)]); }
                proc (buf);
                for (int i = 0; i < b; ++i) out.push_back (buf.getSample (0, i));
                pos += b;
            }
            return out;
        };
        auto cmp = [&] (const char* name, std::function<std::function<void (juce::AudioBuffer<float>&)>()> make)
        {
            auto a = renderMod (512, make());
            auto b = renderMod (64,  make());
            double m = 0.0;
            for (int i = 1500; i < total; ++i) m = std::max (m, (double) std::abs (a[(size_t) i] - b[(size_t) i]));
            std::cout << "  " << name << ": " << juce::String (juce::Decibels::gainToDecibels ((float) m + 1e-12f), 1)
                      << " dB " << (m > 1.0e-4 ? "  <-- INCONSISTENT" : "  ok") << "\n";
        };
        using PF = std::function<void (juce::AudioBuffer<float>&)>;
        cmp ("dsp::Gain   ", [&] { auto g = std::make_shared<juce::dsp::Gain<float>>(); g->prepare (spec); g->setRampDurationSeconds (0.02);
            return PF ([g] (juce::AudioBuffer<float>& buf) { g->setGainDecibels (0.0f); juce::dsp::AudioBlock<float> b (buf); g->process (juce::dsp::ProcessContextReplacing<float> (b)); }); });
        cmp ("ParametricEQ", [&] { auto e = std::make_shared<mf::ParametricEQ>(); e->prepare (spec);
            return PF ([e] (juce::AudioBuffer<float>& buf) { e->setParameters (100,2,400,0,0.7f,3000,0,0.7f,10000,0); e->process (buf, 0); }); });
        cmp ("Multiband   ", [&] { auto m = std::make_shared<mf::MultibandCompressor>(); m->prepare (spec);
            return PF ([m] (juce::AudioBuffer<float>& buf) { m->setParameters (200,2500,10,150,6, -18,1,0, -18,1,0, -18,1,0); m->process (buf); }); });
        cmp ("Saturation  ", [&] { auto s = std::make_shared<mf::Saturation>(); s->prepare (spec);
            return PF ([s] (juce::AudioBuffer<float>& buf) { s->setParameters (6,0); s->process (buf); }); });
        cmp ("StereoWidth ", [&] { auto w = std::make_shared<mf::StereoWidth>(); w->prepare (spec);
            return PF ([w] (juce::AudioBuffer<float>& buf) { w->setWidth (100); w->process (buf); }); });
        cmp ("Limiter     ", [&] { auto l = std::make_shared<mf::Limiter>(); l->prepare (spec);
            return PF ([l] (juce::AudioBuffer<float>& buf) { l->setParameters (-0.3f, 100); l->process (buf); }); });

        struct Chain { juce::dsp::Gain<float> ig, og; mf::ParametricEQ eq; mf::MultibandCompressor mb;
                       mf::Saturation sat; mf::StereoWidth w; mf::Limiter lim; };
        const auto makeChain = [&] { auto c = std::make_shared<Chain>();
            c->ig.prepare (spec); c->ig.setRampDurationSeconds (0.02);
            c->og.prepare (spec); c->og.setRampDurationSeconds (0.02);
            c->eq.prepare (spec); c->mb.prepare (spec); c->sat.prepare (spec); c->w.prepare (spec); c->lim.prepare (spec);
            return c; };
        const auto setChain = [] (Chain& c) {
            c.ig.setGainDecibels (0.0f); c.og.setGainDecibels (0.0f);
            c.eq.setParameters (100,0,400,0,0.7f,3000,0,0.7f,10000,0);
            c.mb.setParameters (200,2500,10,150,6, -18,1,0, -18,1,0, -18,1,0);
            c.sat.setParameters (6,0); c.w.setWidth (100); c.lim.setParameters (-0.3f, 100); };

        cmp ("FULL chain  ", [&] { auto c = makeChain();
            return PF ([c, setChain] (juce::AudioBuffer<float>& buf) { setChain (*c);
                juce::dsp::AudioBlock<float> block (buf);
                juce::dsp::ProcessContextReplacing<float> context (block);
                c->ig.process (context); c->eq.process (buf, 0); c->mb.process (buf);
                c->sat.process (buf); c->w.process (buf); c->og.process (context); c->lim.process (buf); }); });

        cmp ("chain noLIM ", [&] { auto c = makeChain();
            return PF ([c, setChain] (juce::AudioBuffer<float>& buf) { setChain (*c);
                juce::dsp::AudioBlock<float> block (buf);
                juce::dsp::ProcessContextReplacing<float> context (block);
                c->ig.process (context); c->eq.process (buf, 0); c->mb.process (buf);
                c->sat.process (buf); c->w.process (buf); c->og.process (context); }); });

        cmp ("chain noGAIN", [&] { auto c = makeChain();
            return PF ([c, setChain] (juce::AudioBuffer<float>& buf) { setChain (*c);
                c->eq.process (buf, 0); c->mb.process (buf);
                c->sat.process (buf); c->w.process (buf); c->lim.process (buf); }); });
    }

    std::cout << "\n[Frequency response — default settings, should be flat]\n";
    {
        MasterForgeAudioProcessor p;
        p.setPlayConfigDetails (2, 2, SR, BLK);
        p.prepareToPlay (SR, BLK);
        // Disable compression so we measure the pure crossover/EQ magnitude response.
        setP (p, mf::pid::mbLowRatio, 1.0f); setP (p, mf::pid::mbMidRatio, 1.0f); setP (p, mf::pid::mbHiRatio, 1.0f);
        double minG = 1e9, maxG = -1e9;
        for (double f : { 30.0, 60.0, 120.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
        {
            const double g = gainDbAt (p, f, juce::Decibels::decibelsToGain (-18.0));
            minG = std::min (minG, g); maxG = std::max (maxG, g);
            std::cout << "  " << juce::String (f, 0).paddedLeft (' ', 6) << " Hz : "
                      << juce::String (g, 2) << " dB\n";
        }
        std::cout << "  --> ripple = " << juce::String (maxG - minG, 2) << " dB\n";
    }
    return 0;
}
