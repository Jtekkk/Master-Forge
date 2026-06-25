#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mf
{
/** Dark, "molten metal" look for Master Forge: charcoal panels, ember-orange accents. */
class ForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ForgeLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    // --- shared palette -----------------------------------------------------
    static const juce::Colour background;
    static const juce::Colour backgroundHi;
    static const juce::Colour panel;
    static const juce::Colour panelHi;
    static const juce::Colour panelLo;
    static const juce::Colour panelBorder;
    static const juce::Colour accent;
    static const juce::Colour accentGlow;
    static const juce::Colour accentHot;
    static const juce::Colour text;
    static const juce::Colour textDim;
    static const juce::Colour track;

    // EQ band colours (shared by the analyzer handles and the multiband panel)
    static const juce::Colour bandLow;
    static const juce::Colour bandLowMid;
    static const juce::Colour bandHighMid;
    static const juce::Colour bandHigh;

    /** Paints a panel background: vertical gradient, top highlight, soft shadow. */
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds,
                           const juce::String& title, float cornerSize = 8.0f);
};
} // namespace mf
