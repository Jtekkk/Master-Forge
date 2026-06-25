#include "ForgeLookAndFeel.h"

namespace mf
{
const juce::Colour ForgeLookAndFeel::background   { 0xff100d0b };
const juce::Colour ForgeLookAndFeel::backgroundHi { 0xff1d1813 };
const juce::Colour ForgeLookAndFeel::panel        { 0xff221b16 };
const juce::Colour ForgeLookAndFeel::panelHi      { 0xff2d241d };
const juce::Colour ForgeLookAndFeel::panelLo      { 0xff181310 };
const juce::Colour ForgeLookAndFeel::panelBorder  { 0xff44382f };
const juce::Colour ForgeLookAndFeel::accent       { 0xffff6a1a };
const juce::Colour ForgeLookAndFeel::accentGlow   { 0xffffa247 };
const juce::Colour ForgeLookAndFeel::accentHot    { 0xffffd190 };
const juce::Colour ForgeLookAndFeel::text         { 0xfff2ebe1 };
const juce::Colour ForgeLookAndFeel::textDim      { 0xff8c8076 };
const juce::Colour ForgeLookAndFeel::track        { 0xff2b251f };

const juce::Colour ForgeLookAndFeel::bandLow      { 0xff5b8def };
const juce::Colour ForgeLookAndFeel::bandLowMid   { 0xff3fbfa8 };
const juce::Colour ForgeLookAndFeel::bandHighMid  { 0xffe7b53a };
const juce::Colour ForgeLookAndFeel::bandHigh     { 0xffff6a3d };

ForgeLookAndFeel::ForgeLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);

    setColour (juce::Slider::textBoxTextColourId,       text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x40000000));
    setColour (juce::Slider::textBoxOutlineColourId,    panelBorder);
    setColour (juce::Slider::rotarySliderFillColourId,  accent);

    setColour (juce::Label::textColourId, text);

    setColour (juce::ToggleButton::textColourId,         text);
    setColour (juce::ToggleButton::tickColourId,         accent);
    setColour (juce::ToggleButton::tickDisabledColourId, track);

    setColour (juce::TextButton::buttonColourId,   panel);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId,  text);
    setColour (juce::TextButton::textColourOnId,   background);

    setColour (juce::ComboBox::backgroundColourId, panelLo);
    setColour (juce::ComboBox::textColourId,       text);
    setColour (juce::ComboBox::outlineColourId,    panelBorder);
    setColour (juce::ComboBox::arrowColourId,      accent);
    setColour (juce::PopupMenu::backgroundColourId,         panelLo);
    setColour (juce::PopupMenu::textColourId,               text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId,    background);
}

void ForgeLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                                  const juce::String& title, float corner)
{
    // soft drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), corner);

    g.setGradientFill (juce::ColourGradient (panelHi, bounds.getCentreX(), bounds.getY(),
                                             panelLo, bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, corner);

    // inner top highlight
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (bounds.getX() + corner, bounds.getY() + 1.0f,
                bounds.getRight() - corner, bounds.getY() + 1.0f, 1.0f);

    g.setColour (panelBorder);
    g.drawRoundedRectangle (bounds, corner, 1.2f);

    if (title.isNotEmpty())
    {
        auto titleArea = bounds.reduced (12.0f, 0.0f).removeFromTop (24.0f).withTrimmedTop (4.0f);

        // accent tab
        g.setColour (accent);
        g.fillRoundedRectangle (titleArea.getX() - 4.0f, titleArea.getY() + 2.0f, 3.0f, 13.0f, 1.5f);

        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText (title.toUpperCase(), titleArea.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
    }
}

void ForgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = startAngle + sliderPos * (endAngle - startAngle);

    const float lineW     = juce::jmax (2.5f, radius * 0.13f);
    const float arcRadius  = radius - lineW * 0.5f;

    // tick marks around the dial
    g.setColour (panelBorder.brighter (0.1f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        const float r1 = radius + 1.0f, r2 = radius + (i % 5 == 0 ? 4.0f : 2.5f);
        const juce::Point<float> p1 (centre.x + std::cos (a - juce::MathConstants<float>::halfPi) * r1,
                                     centre.y + std::sin (a - juce::MathConstants<float>::halfPi) * r1);
        const juce::Point<float> p2 (centre.x + std::cos (a - juce::MathConstants<float>::halfPi) * r2,
                                     centre.y + std::sin (a - juce::MathConstants<float>::halfPi) * r2);
        g.drawLine ({ p1, p2 }, 1.0f);
    }

    // background track
    juce::Path back;
    back.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (track);
    g.strokePath (back, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc with ember glow
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (accentGlow.withAlpha (0.30f));
    g.strokePath (value, juce::PathStrokeType (lineW * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body — metallic radial gradient with rim
    const float knobR = arcRadius - lineW * 1.35f;
    auto knob = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.40f));
    g.fillEllipse (knob.translated (0.0f, 1.5f));

    juce::ColourGradient body (panel.brighter (0.28f), centre.x - knobR * 0.4f, centre.y - knobR * 0.6f,
                               panelLo.darker (0.2f),   centre.x, centre.y + knobR, true);
    g.setGradientFill (body);
    g.fillEllipse (knob);
    g.setColour (panelBorder.brighter (0.15f));
    g.drawEllipse (knob, 1.2f);

    // pointer with glow
    juce::Path pointer;
    const float pointerLen = knobR * 0.92f;
    const float pointerW   = juce::jmax (2.2f, lineW * 0.6f);
    pointer.addRoundedRectangle (-pointerW * 0.5f, -pointerLen, pointerW, pointerLen * 0.62f, pointerW * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (accentGlow.withAlpha (0.5f));
    g.strokePath (pointer, juce::PathStrokeType (2.0f));
    g.setColour (accentHot);
    g.fillPath (pointer);
}

void ForgeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                         bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);
    const float corner = bounds.getHeight() * 0.5f;
    const bool on = b.getToggleState();

    if (on)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillRoundedRectangle (bounds.expanded (1.5f), corner + 1.5f);
        g.setGradientFill (juce::ColourGradient (accentGlow, bounds.getCentreX(), bounds.getY(),
                                                 accent, bounds.getCentreX(), bounds.getBottom(), false));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (panelHi, bounds.getCentreX(), bounds.getY(),
                                                 panelLo, bounds.getCentreX(), bounds.getBottom(), false));
    }
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (on ? accentHot : panelBorder);
    g.drawRoundedRectangle (bounds, corner, 1.2f);

    // LED dot
    auto led = bounds.removeFromLeft (bounds.getHeight()).reduced (bounds.getHeight() * 0.32f);
    g.setColour (on ? accentHot : track.brighter (0.1f));
    g.fillEllipse (led);
    if (on)
    {
        g.setColour (accentHot.withAlpha (0.5f));
        g.fillEllipse (led.expanded (2.0f));
    }

    g.setColour (on ? background : (highlighted ? text : textDim));
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText (b.getButtonText(), bounds.withTrimmedLeft (2.0f), juce::Justification::centred);
    juce::ignoreUnused (down);
}

void ForgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                             bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 4.0f;
    const bool on = b.getToggleState();

    juce::Colour base = bg;
    if (highlighted) base = base.brighter (0.12f);
    if (down)        base = base.darker (0.12f);

    if (on)
    {
        g.setColour (accent.withAlpha (0.22f));
        g.fillRoundedRectangle (bounds.expanded (1.0f), corner + 1.0f);
    }

    g.setGradientFill (juce::ColourGradient (base.brighter (0.10f), bounds.getCentreX(), bounds.getY(),
                                             base.darker (0.14f),   bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (on ? accentHot : panelBorder.brighter (highlighted ? 0.25f : 0.0f));
    g.drawRoundedRectangle (bounds, corner, 1.1f);
}

juce::Font ForgeLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (12.0f, juce::Font::bold);
}

void ForgeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                     int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (panelLo.brighter (0.06f), 0.0f, 0.0f,
                                             panelLo.darker (0.12f), 0.0f, (float) height, false));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (box.isMouseOver() ? accent.withAlpha (0.7f) : panelBorder);
    g.drawRoundedRectangle (bounds, 4.0f, 1.1f);

    auto arrow = juce::Rectangle<float> ((float) (width - 22), 0.0f, 18.0f, (float) height).reduced (4.0f);
    juce::Path p;
    p.addTriangle (arrow.getCentreX() - 5.0f, arrow.getCentreY() - 3.0f,
                   arrow.getCentreX() + 5.0f, arrow.getCentreY() - 3.0f,
                   arrow.getCentreX(),        arrow.getCentreY() + 4.0f);
    g.setColour (accent);
    g.fillPath (p);
}

juce::Font ForgeLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (13.0f, juce::Font::bold);
}
} // namespace mf
