#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/*
    Central place for every parameter ID and for building the parameter layout.
    Keeping the IDs here (instead of scattering string literals) means the
    processor and editor can never drift out of sync.
*/
namespace mf::pid
{
    // --- global -------------------------------------------------------------
    inline constexpr auto bypass      = "bypass";
    inline constexpr auto inputGain   = "inputGain";
    inline constexpr auto outputGain  = "outputGain";

    // --- global quality -----------------------------------------------------
    inline constexpr auto hqMode      = "hqMode";      // 16x oversampling when on
    inline constexpr auto eqLinear    = "eqLinear";    // linear-phase EQ when on

    // --- 4-band mastering EQ -----------------------------------------------
    inline constexpr auto eqMode      = "eqMode";      // 0=Stereo, 1=Mid, 2=Side
    inline constexpr auto eqLowFreq   = "eqLowFreq";   // low shelf
    inline constexpr auto eqLowGain   = "eqLowGain";
    inline constexpr auto eqLmFreq    = "eqLmFreq";    // low-mid bell
    inline constexpr auto eqLmGain    = "eqLmGain";
    inline constexpr auto eqLmQ       = "eqLmQ";
    inline constexpr auto eqHmFreq    = "eqHmFreq";    // high-mid bell
    inline constexpr auto eqHmGain    = "eqHmGain";
    inline constexpr auto eqHmQ       = "eqHmQ";
    inline constexpr auto eqHighFreq  = "eqHighFreq";  // high shelf
    inline constexpr auto eqHighGain  = "eqHighGain";

    // --- multiband compressor ----------------------------------------------
    inline constexpr auto mbXLow      = "mbXLow";      // low/mid crossover
    inline constexpr auto mbXHigh     = "mbXHigh";     // mid/high crossover
    inline constexpr auto compAttack  = "compAttack";  // shared
    inline constexpr auto compRelease = "compRelease"; // shared
    inline constexpr auto compKnee    = "compKnee";    // shared
    inline constexpr auto mbLowThresh = "mbLowThresh";
    inline constexpr auto mbLowRatio  = "mbLowRatio";
    inline constexpr auto mbLowMakeup = "mbLowMakeup";
    inline constexpr auto mbMidThresh = "mbMidThresh";
    inline constexpr auto mbMidRatio  = "mbMidRatio";
    inline constexpr auto mbMidMakeup = "mbMidMakeup";
    inline constexpr auto mbHiThresh  = "mbHiThresh";
    inline constexpr auto mbHiRatio   = "mbHiRatio";
    inline constexpr auto mbHiMakeup  = "mbHiMakeup";

    // --- saturation / harmonics --------------------------------------------
    inline constexpr auto thd         = "thd";         // THD amount (harmonics)

    // --- stereo width -------------------------------------------------------
    inline constexpr auto width       = "width";

    // --- brickwall limiter --------------------------------------------------
    inline constexpr auto limCeiling  = "limCeiling";
    inline constexpr auto limRelease  = "limRelease";
    inline constexpr auto limTruePeak = "limTruePeak"; // oversampled ISP limiting
}

namespace mf
{
    // A skewed (logarithmic-ish) range, handy for frequency controls.
    inline juce::NormalisableRange<float> freqRange (float min, float max, float centre)
    {
        juce::NormalisableRange<float> r (min, max);
        r.setSkewForCentre (centre);
        return r;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using FloatParam = juce::AudioParameterFloat;
        using BoolParam  = juce::AudioParameterBool;
        using Attr       = juce::AudioParameterFloatAttributes;
        namespace p = mf::pid;

        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        const auto db   = [] (const juce::String& s) { return Attr().withLabel (s); };
        const auto vid  = [] (const char* id) { return juce::ParameterID { id, 1 }; };
        const auto gainRange = [] { return juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f); };
        const auto threshRange = [] { return juce::NormalisableRange<float> (-48.0f, 0.0f, 0.1f); };
        const auto ratioRange  = [] { return juce::NormalisableRange<float> (1.0f, 20.0f, 0.1f, 0.5f); };
        const auto makeupRange = [] { return juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f); };

        // ---- global ----
        params.push_back (std::make_unique<BoolParam>  (vid (p::bypass), "Bypass", false));
        params.push_back (std::make_unique<BoolParam>  (vid (p::hqMode), "HQ", false));
        params.push_back (std::make_unique<FloatParam> (vid (p::inputGain),  "Input",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db ("dB")));
        params.push_back (std::make_unique<FloatParam> (vid (p::outputGain), "Output",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db ("dB")));

        // ---- EQ ----
        params.push_back (std::make_unique<juce::AudioParameterChoice> (vid (p::eqMode), "EQ Mode",
            juce::StringArray { "Stereo", "Mid", "Side" }, 0));
        params.push_back (std::make_unique<BoolParam> (vid (p::eqLinear), "Linear Phase", false));
        params.push_back (std::make_unique<FloatParam> (vid (p::eqLowFreq), "Low Freq",
            freqRange (20.0f, 500.0f, 120.0f), 100.0f, db ("Hz")));
        params.push_back (std::make_unique<FloatParam> (vid (p::eqLowGain), "Low Gain", gainRange(), 0.0f, db ("dB")));

        params.push_back (std::make_unique<FloatParam> (vid (p::eqLmFreq), "L-Mid Freq",
            freqRange (80.0f, 2000.0f, 500.0f), 400.0f, db ("Hz")));
        params.push_back (std::make_unique<FloatParam> (vid (p::eqLmGain), "L-Mid Gain", gainRange(), 0.0f, db ("dB")));
        params.push_back (std::make_unique<FloatParam> (vid (p::eqLmQ), "L-Mid Q",
            juce::NormalisableRange<float> (0.2f, 8.0f, 0.01f, 0.4f), 0.7f));

        params.push_back (std::make_unique<FloatParam> (vid (p::eqHmFreq), "H-Mid Freq",
            freqRange (800.0f, 12000.0f, 3000.0f), 3000.0f, db ("Hz")));
        params.push_back (std::make_unique<FloatParam> (vid (p::eqHmGain), "H-Mid Gain", gainRange(), 0.0f, db ("dB")));
        params.push_back (std::make_unique<FloatParam> (vid (p::eqHmQ), "H-Mid Q",
            juce::NormalisableRange<float> (0.2f, 8.0f, 0.01f, 0.4f), 0.7f));

        params.push_back (std::make_unique<FloatParam> (vid (p::eqHighFreq), "High Freq",
            freqRange (2000.0f, 20000.0f, 8000.0f), 10000.0f, db ("Hz")));
        params.push_back (std::make_unique<FloatParam> (vid (p::eqHighGain), "High Gain", gainRange(), 0.0f, db ("dB")));

        // ---- multiband compressor ----
        params.push_back (std::make_unique<FloatParam> (vid (p::mbXLow), "X Low/Mid",
            freqRange (40.0f, 1000.0f, 250.0f), 200.0f, db ("Hz")));
        params.push_back (std::make_unique<FloatParam> (vid (p::mbXHigh), "X Mid/High",
            freqRange (1000.0f, 12000.0f, 3000.0f), 2500.0f, db ("Hz")));
        params.push_back (std::make_unique<FloatParam> (vid (p::compAttack), "Attack",
            juce::NormalisableRange<float> (0.1f, 100.0f, 0.1f, 0.4f), 10.0f, db ("ms")));
        params.push_back (std::make_unique<FloatParam> (vid (p::compRelease), "Release",
            juce::NormalisableRange<float> (10.0f, 1000.0f, 1.0f, 0.4f), 150.0f, db ("ms")));
        params.push_back (std::make_unique<FloatParam> (vid (p::compKnee), "Knee", makeupRange(), 6.0f, db ("dB")));

        params.push_back (std::make_unique<FloatParam> (vid (p::mbLowThresh), "Low Thr",  threshRange(), -18.0f, db ("dB")));
        params.push_back (std::make_unique<FloatParam> (vid (p::mbLowRatio),  "Low Ratio", ratioRange(), 1.0f, Attr().withLabel (":1")));
        params.push_back (std::make_unique<FloatParam> (vid (p::mbLowMakeup), "Low Gain",  makeupRange(), 0.0f, db ("dB")));

        params.push_back (std::make_unique<FloatParam> (vid (p::mbMidThresh), "Mid Thr",  threshRange(), -18.0f, db ("dB")));
        params.push_back (std::make_unique<FloatParam> (vid (p::mbMidRatio),  "Mid Ratio", ratioRange(), 1.0f, Attr().withLabel (":1")));
        params.push_back (std::make_unique<FloatParam> (vid (p::mbMidMakeup), "Mid Gain",  makeupRange(), 0.0f, db ("dB")));

        params.push_back (std::make_unique<FloatParam> (vid (p::mbHiThresh), "High Thr",  threshRange(), -18.0f, db ("dB")));
        params.push_back (std::make_unique<FloatParam> (vid (p::mbHiRatio),  "High Ratio", ratioRange(), 1.0f, Attr().withLabel (":1")));
        params.push_back (std::make_unique<FloatParam> (vid (p::mbHiMakeup), "High Gain",  makeupRange(), 0.0f, db ("dB")));

        // ---- saturation / harmonics ----
        params.push_back (std::make_unique<FloatParam> (vid (p::thd), "THD",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, db ("%")));

        // ---- width ----
        params.push_back (std::make_unique<FloatParam> (vid (p::width), "Width",
            juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f, db ("%")));

        // ---- limiter ----
        params.push_back (std::make_unique<FloatParam> (vid (p::limCeiling), "Ceiling",
            juce::NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -0.3f, db ("dB")));
        params.push_back (std::make_unique<FloatParam> (vid (p::limRelease), "Lim Release",
            juce::NormalisableRange<float> (1.0f, 500.0f, 1.0f, 0.4f), 100.0f, db ("ms")));
        params.push_back (std::make_unique<BoolParam>  (vid (p::limTruePeak), "True Peak", true));

        return { params.begin(), params.end() };
    }
}
