#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "PluginProcessor.h"
#include "gui/ForgeLookAndFeel.h"

namespace mf
{
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

/** A rotary slider with a caption, wired to an APVTS parameter. */
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (juce::AudioProcessorValueTreeState& state,
                 const juce::String& paramID, const juce::String& name);
    void resized() override;

    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<SliderAttachment> attachment;
};

/** A titled, bordered panel that lays its knobs out in a single row. */
class SectionPanel : public juce::Component
{
public:
    explicit SectionPanel (const juce::String& title) : titleText (title) {}
    LabeledKnob& addKnob (juce::AudioProcessorValueTreeState& state,
                          const juce::String& paramID, const juce::String& name);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String titleText;
    juce::OwnedArray<LabeledKnob> knobs;
};

/** Multiband compressor panel: shared globals on top, three bands below,
    each with a live gain-reduction bar. */
class MultibandPanel : public juce::Component
{
public:
    explicit MultibandPanel (juce::AudioProcessorValueTreeState& state);
    void setReductions (float lo, float mid, float hi);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::OwnedArray<LabeledKnob> globals;   // xLow, xHigh, attack, release, knee
    juce::OwnedArray<LabeledKnob> bandKnobs; // 3 bands x (thr, ratio, gain)
    float grLo = 0.0f, grMid = 0.0f, grHi = 0.0f;
    std::array<juce::Rectangle<int>, 3> labelBounds {};
    std::array<juce::Rectangle<int>, 3> grBarBounds {};
};

/** Limiter panel: ceiling + release knobs and a True-Peak toggle. */
class LimiterPanel : public juce::Component
{
public:
    explicit LimiterPanel (juce::AudioProcessorValueTreeState& state);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::OwnedArray<LabeledKnob> knobs;
    juce::ToggleButton truePeakButton { "TRUE PEAK" };
    std::unique_ptr<ButtonAttachment> tpAttachment;
};

/** A vertical peak-level meter (dBFS). */
class LevelMeter : public juce::Component
{
public:
    void setLevel (float dB) { levelDb = dB; repaint(); }
    void paint (juce::Graphics&) override;

private:
    float levelDb = -100.0f;
    static constexpr float minDb = -60.0f, maxDb = 0.0f;
};

/** Output meters, LUFS readouts and gain-reduction bars. */
class MeterPanel : public juce::Component
{
public:
    MeterPanel();
    void update (float outLDb, float outRDb,
                 float lufsM, float lufsS, float lufsI,
                 float grLo, float grMid, float grHi, float grLim);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onResetIntegrated;

private:
    LevelMeter meterL, meterR;
    juce::TextButton resetButton { "RESET" };
    juce::Rectangle<int> textBounds;
    float momentary = -100.0f, shortTerm = -100.0f, integrated = -100.0f;
    float grLow = 0.0f, grMidB = 0.0f, grHigh = 0.0f, grLim = 0.0f;
};

/** Spectrum analyzer + live 4-band EQ response curve with draggable handles. */
class EQDisplay : public juce::Component
{
public:
    EQDisplay (MasterForgeAudioProcessor& proc, juce::AudioProcessorValueTreeState& state);

    void refresh();                       // pull analyzer data + repaint (timer-driven)
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Band
    {
        const char* freqId;
        const char* gainId;
        const char* qId;     // nullptr for shelves
        float fMin, fMax;
    };

    float getVal (const char* id) const;
    void  setVal (const char* id, float value);

    float freqToX (float freq) const;
    float xToFreq (float x) const;
    float gainToY (float gainDb) const;
    float yToGain (float y) const;
    int   findHandle (juce::Point<float>) const;
    juce::Point<float> handlePos (const Band&) const;

    MasterForgeAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;

    std::array<Band, 4> bands;
    std::vector<float> magnitudes;   // raw FFT magnitudes pulled this tick
    std::vector<float> smoothedDb;   // displayed spectrum (dB), peak-decay smoothed
    int draggingBand = -1;

    static constexpr float fMinHz = 20.0f, fMaxHz = 20000.0f;
    static constexpr float maxGainDb = 15.0f;
    static constexpr float specMinDb = -84.0f, specMaxDb = 6.0f;
};

/** Preset selector with prev/next, A/B compare and save. */
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (PresetManager& pm);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();   // repopulate the combo box from disk + factory list

private:
    void selectionChanged();
    void step (int delta);
    void showSaveDialog();

    PresetManager& presetManager;

    juce::Label    caption { {}, "PRESET" };
    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::TextButton abButton   { "A/B" }, copyButton { "COPY" }, saveButton { "SAVE" };

    std::vector<int> orderedIds;   // selectable item ids in display order
    juce::StringArray userNames;
};
} // namespace mf

class MasterForgeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit MasterForgeAudioProcessorEditor (MasterForgeAudioProcessor&);
    ~MasterForgeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    MasterForgeAudioProcessor& processorRef;
    mf::ForgeLookAndFeel lookAndFeel;

    juce::Label        titleLabel;
    juce::ToggleButton bypassButton { "BYPASS" };
    std::unique_ptr<mf::ButtonAttachment> bypassAttachment;

    mf::PresetBar      presetBar;
    mf::EQDisplay      eqDisplay;
    mf::SectionPanel   eqSection        { "EQUALISER" };
    mf::MultibandPanel multiband;
    mf::SectionPanel   gainSection      { "GAIN" };
    mf::SectionPanel   characterSection { "CHARACTER" };
    mf::LimiterPanel   limiterPanel;
    mf::MeterPanel     meterPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterForgeAudioProcessorEditor)
};
