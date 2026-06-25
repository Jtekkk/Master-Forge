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

    // --- shared palette -----------------------------------------------------
    static const juce::Colour background;
    static const juce::Colour panel;
    static const juce::Colour panelBorder;
    static const juce::Colour accent;
    static const juce::Colour accentGlow;
    static const juce::Colour text;
    static const juce::Colour textDim;
    static const juce::Colour track;
};
} // namespace mf
