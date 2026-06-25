#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/ForgeLookAndFeel.h"

namespace mf
{
/** A rotary slider with a caption, wired to an APVTS parameter. */
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (juce::AudioProcessorValueTreeState& state,
                 const juce::String& paramID, const juce::String& name);

    void resized() override;

    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A titled, bordered panel that lays its knobs out in a row. */
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
                 float grCompDb, float grLimDb);

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onResetIntegrated;

private:
    LevelMeter meterL, meterR;
    juce::TextButton resetButton { "RESET" };

    juce::Rectangle<int> textBounds;
    float momentary = -100.0f, shortTerm = -100.0f, integrated = -100.0f;
    float grComp = 0.0f, grLim = 0.0f;
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

    juce::Label       titleLabel;
    juce::ToggleButton bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    mf::SectionPanel eqSection        { "EQUALISER" };
    mf::SectionPanel compSection      { "COMPRESSOR" };
    mf::SectionPanel gainSection      { "GAIN" };
    mf::SectionPanel characterSection { "CHARACTER" };
    mf::SectionPanel limiterSection   { "LIMITER" };

    mf::MeterPanel meterPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterForgeAudioProcessorEditor)
};
