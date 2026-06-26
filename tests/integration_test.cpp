// End-to-end smoke test: drive the whole MasterForgeAudioProcessor (multiband
// compressor, true-peak limiter, analyzer, presets) and construct its editor,
// all without a plugin host. Run with -DMASTERFORGE_BUILD_TESTS=ON.

#include "PluginProcessor.h"

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
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // message manager + singletons

    const double sr = 48000.0;
    const int    block = 512;

    MasterForgeAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    std::cout << "Master Forge integration test (" << proc.getName() << ")\n";

    auto& apvts = proc.getAPVTS();
    const auto setParam = [&] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    float lastPeak = 0.0f;
    bool  lastFinite = true;
    long long phase = 0;
    const auto runAudio = [&] (int blocks)
    {
        lastPeak = 0.0f;
        lastFinite = true;
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf (2, block);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, (float) (1.3 * std::sin (2.0 * kPi * 220.0 * (double) (phase + i) / sr)));
            phase += block;

            proc.processBlock (buf, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                for (int i = 0; i < block; ++i)
                    if (! std::isfinite (buf.getSample (ch, i))) lastFinite = false;
                lastPeak = juce::jmax (lastPeak, buf.getMagnitude (ch, 0, block));
            }
        }
    };

    // --- true-peak ON (default) ---
    std::cout << "[true-peak ON]\n";
    setParam (mf::pid::limCeiling, -1.0f);
    runAudio (200);
    const float ceil = juce::Decibels::decibelsToGain (-1.0f);
    check (lastFinite, "output is finite");
    check (lastPeak <= ceil + 1.0e-3f,
           "sample peak " + juce::String (juce::Decibels::gainToDecibels (lastPeak), 2).toStdString()
               + " dB under ceiling -1 dB");
    check (proc.getLatencySamples() > 0, "reports oversampling latency (" + std::to_string (proc.getLatencySamples()) + ")");

    // --- true-peak OFF ---
    std::cout << "[true-peak OFF]\n";
    setParam (mf::pid::limTruePeak, 0.0f);
    runAudio (200);
    check (lastFinite, "output is finite");
    check (lastPeak <= ceil + 1.0e-3f, "sample peak stays under ceiling without oversampling");

    // --- every factory preset loads and runs ---
    std::cout << "[factory presets]\n";
    auto& pm = proc.getPresetManager();
    const auto names = pm.getFactoryPresetNames();
    bool allPresetsOk = true;
    for (int i = 0; i < names.size(); ++i)
    {
        pm.loadFactoryPreset (i);
        runAudio (12);
        if (! lastFinite) allPresetsOk = false;
    }
    check (allPresetsOk, "all " + std::to_string (names.size()) + " factory presets produce finite output");

    // --- A/B compare ---
    std::cout << "[A/B + state]\n";
    pm.loadFactoryPreset (1);
    pm.copyToOtherSlot();
    pm.toggleAB();
    runAudio (12);
    check (lastFinite, "A/B toggle keeps output finite");

    // --- state save / restore round-trip ---
    setParam (mf::pid::eqHighGain, 4.2f);
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    setParam (mf::pid::eqHighGain, -3.0f);
    proc.setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (apvts.getRawParameterValue (mf::pid::eqHighGain)->load() - 4.2f) < 0.05f,
           "state round-trips a parameter value");

    // --- block-size independence (regression): the output must not depend on
    //     the host buffer size. A scratch-buffer length bug here once made the
    //     multiband run over stale tail samples at any size != the prepared max,
    //     which sounded like a "buffer issue" at typical DAW buffer sizes. ---
    std::cout << "[block-size independence]\n";
    {
        const int total = 8000;
        const auto renderAt = [&] (int bs)
        {
            MasterForgeAudioProcessor q;
            q.setPlayConfigDetails (2, 2, sr, bs);
            q.prepareToPlay (sr, bs);
            std::vector<float> out; out.reserve ((size_t) total);
            juce::MidiBuffer m;
            long long nn = 0; int pos = 0;
            while (pos < total)
            {
                const int b = juce::jmin (bs, total - pos);
                juce::AudioBuffer<float> buf (2, b);
                for (int i = 0; i < b; ++i)
                {
                    const float s = (float) (0.5 * std::sin (2.0 * kPi * 110.0 * (double) (nn + i) / sr));
                    buf.setSample (0, i, s); buf.setSample (1, i, s);
                }
                nn += b; q.processBlock (buf, m);
                for (int i = 0; i < b; ++i) out.push_back (buf.getSample (0, i));
                pos += b;
            }
            return out;
        };
        auto a = renderAt (512), b64 = renderAt (64), b100 = renderAt (100);
        float d = 0.0f;
        for (int i = 2000; i < total; ++i)
            d = juce::jmax (d, std::abs (a[(size_t) i] - b64[(size_t) i]), std::abs (a[(size_t) i] - b100[(size_t) i]));
        check (d < 1.0e-4f, "output identical at 512 / 64 / 100-sample buffers (max diff "
                              + juce::String (d, 6).toStdString() + ")");
    }

    // --- double-precision processing: the native AudioBuffer<double> path must
    //     run, stay finite/under-ceiling, and match the float path sample for
    //     sample (the chain is the same 64-bit engine; the float path only
    //     truncates at the boundary). ---
    std::cout << "[double precision]\n";
    {
        check (proc.supportsDoublePrecisionProcessing(),
               "processor advertises double-precision support");

        const int total = 6000;
        const auto renderDouble = [&] (int bs)
        {
            MasterForgeAudioProcessor q;
            q.setPlayConfigDetails (2, 2, sr, bs);
            q.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
            q.prepareToPlay (sr, bs);
            std::vector<float> out; out.reserve ((size_t) total);
            juce::MidiBuffer m;
            long long nn = 0; int pos = 0;
            bool finite = true; float peak = 0.0f;
            while (pos < total)
            {
                const int b = juce::jmin (bs, total - pos);
                juce::AudioBuffer<double> buf (2, b);
                for (int i = 0; i < b; ++i)
                {
                    const double s = 1.3 * std::sin (2.0 * kPi * 220.0 * (double) (nn + i) / sr);
                    buf.setSample (0, i, s); buf.setSample (1, i, s);
                }
                nn += b; q.processBlock (buf, m);
                for (int i = 0; i < b; ++i)
                {
                    const double v = buf.getSample (0, i);
                    if (! std::isfinite (v)) finite = false;
                    peak = juce::jmax (peak, (float) std::abs (v));
                    out.push_back ((float) v);
                }
                pos += b;
            }
            return std::make_tuple (out, finite, peak);
        };

        const auto floatRef = [&] (int bs)
        {
            MasterForgeAudioProcessor q;
            q.setPlayConfigDetails (2, 2, sr, bs);
            q.prepareToPlay (sr, bs);
            std::vector<float> out; out.reserve ((size_t) total);
            juce::MidiBuffer m;
            long long nn = 0; int pos = 0;
            while (pos < total)
            {
                const int b = juce::jmin (bs, total - pos);
                juce::AudioBuffer<float> buf (2, b);
                for (int i = 0; i < b; ++i)
                {
                    const float s = (float) (1.3 * std::sin (2.0 * kPi * 220.0 * (double) (nn + i) / sr));
                    buf.setSample (0, i, s); buf.setSample (1, i, s);
                }
                nn += b; q.processBlock (buf, m);
                for (int i = 0; i < b; ++i) out.push_back (buf.getSample (0, i));
                pos += b;
            }
            return out;
        };

        auto [dOut, dFinite, dPeak] = renderDouble (512);
        auto fOut = floatRef (512);
        check (dFinite, "double-precision output is finite");
        // Fresh processors here use the default ceiling (-0.3 dB), not the -1 dB
        // set on `proc` above.
        const float defCeil = juce::Decibels::decibelsToGain (-0.3f);
        check (dPeak <= defCeil + 1.0e-3f, "double-precision output stays under ceiling");

        float maxDiff = 0.0f;
        for (size_t i = 500; i < dOut.size(); ++i)
            maxDiff = juce::jmax (maxDiff, std::abs (dOut[i] - fOut[i]));
        check (maxDiff < 1.0e-5f,
               "double and float paths agree (max diff " + juce::String (maxDiff, 7).toStdString() + ")");
    }

    // --- editor constructs/destructs (headless: no native peer) ---
    std::cout << "[editor]\n";
    if (auto* editor = proc.createEditor())
    {
        check (editor->getWidth() > 0 && editor->getHeight() > 0,
               "editor builds with a non-zero size (" + std::to_string (editor->getWidth())
                   + "x" + std::to_string (editor->getHeight()) + ")");
        delete editor;
    }
    else
    {
        check (false, "editor created");
    }

    proc.releaseResources();

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : std::to_string (failures) + " CHECK(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
