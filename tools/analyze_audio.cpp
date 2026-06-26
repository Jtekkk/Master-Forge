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
