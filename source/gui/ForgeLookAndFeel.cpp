#include "ForgeLookAndFeel.h"

namespace mf
{
const juce::Colour ForgeLookAndFeel::background  { 0xff14110f };
const juce::Colour ForgeLookAndFeel::panel       { 0xff211b16 };
const juce::Colour ForgeLookAndFeel::panelBorder { 0xff3c322a };
const juce::Colour ForgeLookAndFeel::accent      { 0xffff6a1a };
const juce::Colour ForgeLookAndFeel::accentGlow  { 0xffffa247 };
const juce::Colour ForgeLookAndFeel::text        { 0xffece3d8 };
const juce::Colour ForgeLookAndFeel::textDim     { 0xff8c8076 };
const juce::Colour ForgeLookAndFeel::track       { 0xff37302a };

ForgeLookAndFeel::ForgeLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);

    setColour (juce::Slider::textBoxTextColourId,       text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId,  accent);

    setColour (juce::Label::textColourId, text);

    setColour (juce::ToggleButton::textColourId,         text);
    setColour (juce::ToggleButton::tickColourId,         accent);
    setColour (juce::ToggleButton::tickDisabledColourId, track);

    setColour (juce::TextButton::buttonColourId,   panel);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId,  text);
    setColour (juce::TextButton::textColourOnId,   background);
}

void ForgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = startAngle + sliderPos * (endAngle - startAngle);

    const float lineW     = juce::jmax (2.5f, radius * 0.12f);
    const float arcRadius = radius - lineW * 0.5f;

    // Background track.
    juce::Path back;
    back.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (track);
    g.strokePath (back, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc with a soft ember glow.
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (accentGlow.withAlpha (0.25f));
    g.strokePath (value, juce::PathStrokeType (lineW * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body.
    const float knobR = arcRadius - lineW * 1.3f;
    auto knob = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (panel.brighter (0.18f), centre.x, centre.y - knobR,
                                             panel.darker (0.45f),  centre.x, centre.y + knobR, false));
    g.fillEllipse (knob);
    g.setColour (panelBorder);
    g.drawEllipse (knob, 1.2f);

    // Pointer.
    juce::Path pointer;
    const float pointerLen = knobR * 0.9f;
    const float pointerW   = juce::jmax (2.0f, lineW * 0.55f);
    pointer.addRoundedRectangle (-pointerW * 0.5f, -pointerLen, pointerW, pointerLen * 0.62f, pointerW * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (accentGlow);
    g.fillPath (pointer);
}
} // namespace mf
