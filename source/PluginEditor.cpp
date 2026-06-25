#include "PluginEditor.h"

using LnF = mf::ForgeLookAndFeel;
namespace pid = mf::pid;

namespace
{
const juce::Colour kSpectrum { 0xff4a90c2 };

juce::String fmtLufs (float v)
{
    return v <= -99.0f ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e")) : juce::String (v, 1);
}
}

// ===========================================================================
//  LabeledKnob
// ===========================================================================
mf::LabeledKnob::LabeledKnob (juce::AudioProcessorValueTreeState& state,
                              const juce::String& paramID, const juce::String& name)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
    addAndMakeVisible (slider);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (11.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, LnF::textDim);
    addAndMakeVisible (label);

    attachment = std::make_unique<SliderAttachment> (state, paramID, slider);
}

void mf::LabeledKnob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (15));
    slider.setBounds (r);
}

// ===========================================================================
//  SectionPanel
// ===========================================================================
mf::LabeledKnob& mf::SectionPanel::addKnob (juce::AudioProcessorValueTreeState& state,
                                            const juce::String& paramID, const juce::String& name)
{
    auto* k = new LabeledKnob (state, paramID, name);
    knobs.add (k);
    addAndMakeVisible (k);
    return *k;
}

static void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour (LnF::panel);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (LnF::panelBorder);
    g.drawRoundedRectangle (r, 7.0f, 1.4f);

    auto titleArea = r.removeFromTop (23.0f);
    g.setColour (LnF::accent);
    g.setFont (juce::Font (13.5f, juce::Font::bold));
    g.drawText (title, titleArea.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
    g.setColour (LnF::accent.withAlpha (0.30f));
    g.fillRect (titleArea.getX() + 10.0f, titleArea.getBottom() - 1.0f, r.getWidth() - 20.0f, 1.0f);
}

void mf::SectionPanel::paint (juce::Graphics& g)
{
    paintPanel (g, getLocalBounds().toFloat().reduced (2.0f), titleText);
}

void mf::SectionPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (18);
    const int n = knobs.size();
    if (n == 0) return;
    const int w = r.getWidth() / n;
    for (int i = 0; i < n; ++i)
        knobs[i]->setBounds ((i == n - 1 ? r : r.removeFromLeft (w)).reduced (3));
}

// ===========================================================================
//  MultibandPanel
// ===========================================================================
mf::MultibandPanel::MultibandPanel (juce::AudioProcessorValueTreeState& s)
{
    const auto add = [&] (juce::OwnedArray<LabeledKnob>& arr, const char* id, const juce::String& name)
    {
        auto* k = new LabeledKnob (s, id, name);
        arr.add (k);
        addAndMakeVisible (k);
    };

    add (globals, pid::mbXLow,   "X LO/MID");
    add (globals, pid::mbXHigh,  "X MID/HI");
    add (globals, pid::compAttack,  "ATTACK");
    add (globals, pid::compRelease, "RELEASE");
    add (globals, pid::compKnee,    "KNEE");

    add (bandKnobs, pid::mbLowThresh, "THRESH");
    add (bandKnobs, pid::mbLowRatio,  "RATIO");
    add (bandKnobs, pid::mbLowMakeup, "GAIN");
    add (bandKnobs, pid::mbMidThresh, "THRESH");
    add (bandKnobs, pid::mbMidRatio,  "RATIO");
    add (bandKnobs, pid::mbMidMakeup, "GAIN");
    add (bandKnobs, pid::mbHiThresh,  "THRESH");
    add (bandKnobs, pid::mbHiRatio,   "RATIO");
    add (bandKnobs, pid::mbHiMakeup,  "GAIN");
}

void mf::MultibandPanel::setReductions (float lo, float mid, float hi)
{
    grLo = lo; grMid = mid; grHi = hi;
    repaint();
}

void mf::MultibandPanel::paint (juce::Graphics& g)
{
    paintPanel (g, getLocalBounds().toFloat().reduced (2.0f), "MULTIBAND COMPRESSOR");

    const std::array<juce::String, 3> names { "LOW", "MID", "HIGH" };
    const float grValues[3] = { grLo, grMid, grHi };

    for (int i = 0; i < 3; ++i)
    {
        g.setColour (LnF::text);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (names[(size_t) i], labelBounds[(size_t) i], juce::Justification::centred);

        auto bar = grBarBounds[(size_t) i].toFloat();
        g.setColour (LnF::track.darker (0.2f));
        g.fillRoundedRectangle (bar, 2.0f);
        const float n = juce::jlimit (0.0f, 1.0f, grValues[i] / 18.0f);
        g.setColour (LnF::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * n), 2.0f);
    }
}

void mf::MultibandPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (18);

    auto globalsRow = r.removeFromTop (90);
    const int gw = globalsRow.getWidth() / globals.size();
    for (int i = 0; i < globals.size(); ++i)
        globals[i]->setBounds ((i == globals.size() - 1 ? globalsRow : globalsRow.removeFromLeft (gw)).reduced (3));

    r.removeFromTop (4);

    const int bw = r.getWidth() / 3;
    for (int b = 0; b < 3; ++b)
    {
        auto col = (b == 2 ? r : r.removeFromLeft (bw));
        auto inner = col.reduced (4);
        labelBounds[(size_t) b] = inner.removeFromTop (18);
        grBarBounds[(size_t) b] = inner.removeFromBottom (12).reduced (2, 2);
        inner.removeFromBottom (3);

        const int kw = inner.getWidth() / 3;
        for (int k = 0; k < 3; ++k)
            bandKnobs[b * 3 + k]->setBounds ((k == 2 ? inner : inner.removeFromLeft (kw)).reduced (3));
    }
}

// ===========================================================================
//  LimiterPanel
// ===========================================================================
mf::LimiterPanel::LimiterPanel (juce::AudioProcessorValueTreeState& s)
{
    const auto add = [&] (const char* id, const juce::String& name)
    {
        auto* k = new LabeledKnob (s, id, name);
        knobs.add (k);
        addAndMakeVisible (k);
    };
    add (pid::limCeiling, "CEILING");
    add (pid::limRelease, "RELEASE");

    addAndMakeVisible (truePeakButton);
    tpAttachment = std::make_unique<ButtonAttachment> (s, pid::limTruePeak, truePeakButton);
}

void mf::LimiterPanel::paint (juce::Graphics& g)
{
    paintPanel (g, getLocalBounds().toFloat().reduced (2.0f), "LIMITER");
}

void mf::LimiterPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (18);
    auto toggleRow = r.removeFromBottom (24);
    truePeakButton.setBounds (toggleRow.reduced (4, 2));

    const int w = r.getWidth() / knobs.size();
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds ((i == knobs.size() - 1 ? r : r.removeFromLeft (w)).reduced (3));
}

// ===========================================================================
//  LevelMeter
// ===========================================================================
void mf::LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (LnF::track.darker (0.4f));
    g.fillRoundedRectangle (r, 3.0f);

    const float norm = juce::jlimit (0.0f, 1.0f, (levelDb - minDb) / (maxDb - minDb));
    if (norm > 0.0f)
    {
        auto fill = r.withTop (r.getBottom() - r.getHeight() * norm);
        juce::ColourGradient grad (juce::Colour (0xff43c46b), 0.0f, r.getBottom(),
                                   juce::Colour (0xffe1402f), 0.0f, r.getY(), false);
        grad.addColour (0.72, juce::Colour (0xffe9bd39));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 3.0f);
    }
    g.setColour (LnF::panelBorder);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
}

// ===========================================================================
//  MeterPanel
// ===========================================================================
mf::MeterPanel::MeterPanel()
{
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this] { if (onResetIntegrated) onResetIntegrated(); };
}

void mf::MeterPanel::update (float outLDb, float outRDb,
                             float lufsM, float lufsS, float lufsI,
                             float lo, float mid, float hi, float lim)
{
    meterL.setLevel (outLDb);
    meterR.setLevel (outRDb);
    momentary = lufsM; shortTerm = lufsS; integrated = lufsI;
    grLow = lo; grMidB = mid; grHigh = hi; grLim = lim;
    repaint();
}

void mf::MeterPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (22);

    auto bottom = r.removeFromBottom (26);
    resetButton.setBounds (bottom.removeFromLeft (84).reduced (0, 2));

    auto metersArea = r.removeFromLeft (62);
    const int half = metersArea.getWidth() / 2;
    meterL.setBounds (metersArea.removeFromLeft (half).reduced (3, 2));
    meterR.setBounds (metersArea.reduced (3, 2));

    r.removeFromLeft (12);
    textBounds = r;
}

void mf::MeterPanel::paint (juce::Graphics& g)
{
    paintPanel (g, getLocalBounds().toFloat().reduced (2.0f), "METERING");

    auto t = textBounds;

    g.setColour (LnF::text);
    g.setFont (juce::Font (11.5f, juce::Font::bold));
    g.drawText ("LOUDNESS (LUFS)", t.removeFromTop (18), juce::Justification::centredLeft);

    const auto drawValue = [&] (const juce::String& name, const juce::String& value, juce::Colour c)
    {
        auto row = t.removeFromTop (21);
        g.setColour (LnF::textDim);
        g.setFont (12.0f);
        g.drawText (name, row.removeFromLeft (60), juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (juce::Font (14.0f, juce::Font::bold));
        g.drawText (value, row, juce::Justification::centredRight);
    };
    drawValue ("Moment.", fmtLufs (momentary),  LnF::text);
    drawValue ("Short",   fmtLufs (shortTerm),  LnF::text);
    drawValue ("Integr.", fmtLufs (integrated), LnF::accentGlow);

    t.removeFromTop (8);
    g.setColour (LnF::text);
    g.setFont (juce::Font (11.5f, juce::Font::bold));
    g.drawText ("GAIN REDUCTION", t.removeFromTop (18), juce::Justification::centredLeft);

    const auto drawGr = [&] (const juce::String& name, float gr)
    {
        auto row = t.removeFromTop (20);
        g.setColour (LnF::textDim);
        g.setFont (11.0f);
        g.drawText (name, row.removeFromLeft (42), juce::Justification::centredLeft);

        auto bar = row.toFloat().reduced (2.0f, 5.0f);
        g.setColour (LnF::track.darker (0.2f));
        g.fillRoundedRectangle (bar, 2.0f);
        const float n = juce::jlimit (0.0f, 1.0f, gr / 18.0f);
        g.setColour (LnF::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * n), 2.0f);
        g.setColour (LnF::text);
        g.setFont (10.5f);
        g.drawText (juce::String (gr, 1), row.toNearestInt(), juce::Justification::centredRight);
    };
    drawGr ("MB Lo", grLow);
    drawGr ("MB Md", grMidB);
    drawGr ("MB Hi", grHigh);
    drawGr ("Limit", grLim);
}

// ===========================================================================
//  EQDisplay
// ===========================================================================
mf::EQDisplay::EQDisplay (MasterForgeAudioProcessor& proc, juce::AudioProcessorValueTreeState& state)
    : processor (proc), apvts (state)
{
    bands = { {
        { pid::eqLowFreq,  pid::eqLowGain,  nullptr,    20.0f,   500.0f },
        { pid::eqLmFreq,   pid::eqLmGain,   pid::eqLmQ,  80.0f,  2000.0f },
        { pid::eqHmFreq,   pid::eqHmGain,   pid::eqHmQ, 800.0f, 12000.0f },
        { pid::eqHighFreq, pid::eqHighGain, nullptr,  2000.0f, 20000.0f },
    } };
}

float mf::EQDisplay::getVal (const char* id) const
{
    return apvts.getRawParameterValue (id)->load();
}

void mf::EQDisplay::setVal (const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float mf::EQDisplay::freqToX (float freq) const
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float prop = std::log (freq / fMinHz) / std::log (fMaxHz / fMinHz);
    return b.getX() + prop * b.getWidth();
}

float mf::EQDisplay::xToFreq (float x) const
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float prop = (x - b.getX()) / b.getWidth();
    return fMinHz * std::pow (fMaxHz / fMinHz, prop);
}

float mf::EQDisplay::gainToY (float gainDb) const
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    return b.getCentreY() - (gainDb / maxGainDb) * (b.getHeight() * 0.46f);
}

float mf::EQDisplay::yToGain (float y) const
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    return (b.getCentreY() - y) / (b.getHeight() * 0.46f) * maxGainDb;
}

juce::Point<float> mf::EQDisplay::handlePos (const Band& band) const
{
    return { freqToX (getVal (band.freqId)), gainToY (getVal (band.gainId)) };
}

int mf::EQDisplay::findHandle (juce::Point<float> p) const
{
    for (int i = 0; i < (int) bands.size(); ++i)
        if (handlePos (bands[(size_t) i]).getDistanceFrom (p) < 14.0f)
            return i;
    return -1;
}

void mf::EQDisplay::refresh()
{
    if (processor.getAnalyzer().pullMagnitudes (magnitudes))
    {
        if (smoothedDb.size() != magnitudes.size())
            smoothedDb.assign (magnitudes.size(), specMinDb);

        for (size_t i = 0; i < magnitudes.size(); ++i)
        {
            const float db = juce::Decibels::gainToDecibels (magnitudes[i], specMinDb);
            if (db > smoothedDb[i]) smoothedDb[i] = db;
            else                    smoothedDb[i] += (db - smoothedDb[i]) * 0.3f;
        }
    }
    repaint();
}

void mf::EQDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (LnF::background.darker (0.3f));
    g.fillRoundedRectangle (b, 6.0f);

    double sr = processor.getCurrentSampleRate();
    if (sr <= 0.0) sr = 48000.0;

    // --- grid ---
    g.setFont (10.0f);
    const float gridFreqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    g.setColour (LnF::panelBorder.withAlpha (0.5f));
    for (float f : gridFreqs)
        g.drawVerticalLine ((int) freqToX (f), b.getY(), b.getBottom());

    g.setColour (LnF::textDim);
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String (f, 0),
                    (int) freqToX (f) - 18, (int) b.getBottom() - 14, 36, 12, juce::Justification::centred);

    for (float dB : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = gainToY (dB);
        g.setColour (std::abs (dB) < 0.5f ? LnF::panelBorder : LnF::panelBorder.withAlpha (0.4f));
        g.drawHorizontalLine ((int) y, b.getX(), b.getRight());
    }

    // --- spectrum ---
    if (! smoothedDb.empty())
    {
        juce::Path spec;
        spec.startNewSubPath (b.getX(), b.getBottom());
        const int bins = (int) smoothedDb.size();
        for (float x = b.getX(); x <= b.getRight(); x += 1.0f)
        {
            const float freq = xToFreq (x);
            int bin = (int) std::round (freq * (float) (2 * bins) / (float) sr);
            bin = juce::jlimit (0, bins - 1, bin);
            const float prop = juce::jlimit (0.0f, 1.0f, (smoothedDb[(size_t) bin] - specMinDb) / (specMaxDb - specMinDb));
            spec.lineTo (x, b.getBottom() - prop * b.getHeight());
        }
        spec.lineTo (b.getRight(), b.getBottom());
        spec.closeSubPath();
        g.setColour (kSpectrum.withAlpha (0.35f));
        g.fillPath (spec);
    }

    // --- EQ response curve ---
    using Coefs = juce::dsp::IIR::Coefficients<float>;
    const auto lin = [&] (const char* id) { return juce::Decibels::decibelsToGain (getVal (id)); };
    auto low  = Coefs::makeLowShelf  (sr, getVal (pid::eqLowFreq),  0.707f, lin (pid::eqLowGain));
    auto lm   = Coefs::makePeakFilter (sr, getVal (pid::eqLmFreq),  getVal (pid::eqLmQ), lin (pid::eqLmGain));
    auto hm   = Coefs::makePeakFilter (sr, getVal (pid::eqHmFreq),  getVal (pid::eqHmQ), lin (pid::eqHmGain));
    auto high = Coefs::makeHighShelf (sr, getVal (pid::eqHighFreq), 0.707f, lin (pid::eqHighGain));

    juce::Path curve;
    bool firstPoint = true;
    for (float x = b.getX(); x <= b.getRight(); x += 1.0f)
    {
        const double f = xToFreq (x);
        const double mag = low->getMagnitudeForFrequency (f, sr)
                         * lm->getMagnitudeForFrequency (f, sr)
                         * hm->getMagnitudeForFrequency (f, sr)
                         * high->getMagnitudeForFrequency (f, sr);
        const float y = gainToY (juce::Decibels::gainToDecibels ((float) mag));
        if (firstPoint) { curve.startNewSubPath (x, y); firstPoint = false; }
        else            curve.lineTo (x, y);
    }
    g.setColour (LnF::accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // --- band handles ---
    for (int i = 0; i < (int) bands.size(); ++i)
    {
        const auto p = handlePos (bands[(size_t) i]);
        const float rad = (i == draggingBand) ? 8.0f : 6.0f;
        g.setColour (LnF::accentGlow);
        g.fillEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (LnF::background);
        g.drawEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f, 1.5f);
    }
}

void mf::EQDisplay::mouseDown (const juce::MouseEvent& e)
{
    draggingBand = findHandle (e.position);
    repaint();
}

void mf::EQDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingBand < 0)
        return;

    const auto& band = bands[(size_t) draggingBand];
    setVal (band.freqId, juce::jlimit (band.fMin, band.fMax, xToFreq (e.position.x)));
    setVal (band.gainId, juce::jlimit (-maxGainDb, maxGainDb, yToGain (e.position.y)));
}

void mf::EQDisplay::mouseUp (const juce::MouseEvent&)
{
    draggingBand = -1;
    repaint();
}

void mf::EQDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int handle = findHandle (e.position);
    if (handle < 0)
        return;

    const auto& band = bands[(size_t) handle];
    if (band.qId == nullptr)
        return;

    const float q = juce::jlimit (0.2f, 8.0f, getVal (band.qId) * (wheel.deltaY > 0.0f ? 1.12f : 0.89f));
    setVal (band.qId, q);
}

// ===========================================================================
//  PresetBar
// ===========================================================================
mf::PresetBar::PresetBar (PresetManager& pm) : presetManager (pm)
{
    caption.setFont (juce::Font (12.0f, juce::Font::bold));
    caption.setColour (juce::Label::textColourId, LnF::textDim);
    caption.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (caption);

    presetBox.setTextWhenNothingSelected ("-- select preset --");
    presetBox.onChange = [this] { selectionChanged(); };
    addAndMakeVisible (presetBox);

    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    abButton.onClick   = [this]
    {
        presetManager.toggleAB();
        abButton.setButtonText (presetManager.isSlotA() ? "A" : "B");
    };
    copyButton.onClick = [this] { presetManager.copyToOtherSlot(); };
    saveButton.onClick = [this] { showSaveDialog(); };

    abButton.setButtonText ("A");
    for (auto* b : { &prevButton, &nextButton, &abButton, &copyButton, &saveButton })
        addAndMakeVisible (b);

    refresh();
}

void mf::PresetBar::refresh()
{
    presetBox.clear (juce::dontSendNotification);
    orderedIds.clear();

    const auto factory = presetManager.getFactoryPresetNames();
    for (int i = 0; i < factory.size(); ++i)
    {
        presetBox.addItem (factory[i], i + 1);
        orderedIds.push_back (i + 1);
    }

    userNames = presetManager.getUserPresetNames();
    if (! userNames.isEmpty())
    {
        presetBox.addSeparator();
        for (int j = 0; j < userNames.size(); ++j)
        {
            presetBox.addItem (userNames[j], 1000 + j);
            orderedIds.push_back (1000 + j);
        }
    }
}

void mf::PresetBar::selectionChanged()
{
    const int id = presetBox.getSelectedId();
    if (id <= 0)
        return;

    if (id < 1000)
        presetManager.loadFactoryPreset (id - 1);
    else if (const int j = id - 1000; j >= 0 && j < userNames.size())
        presetManager.loadUserPreset (userNames[j]);
}

void mf::PresetBar::step (int delta)
{
    if (orderedIds.empty())
        return;

    const int current = presetBox.getSelectedId();
    int idx = 0;
    for (int i = 0; i < (int) orderedIds.size(); ++i)
        if (orderedIds[(size_t) i] == current) { idx = i; break; }

    idx = juce::jlimit (0, (int) orderedIds.size() - 1, idx + delta);
    presetBox.setSelectedId (orderedIds[(size_t) idx]);  // triggers onChange
}

void mf::PresetBar::showSaveDialog()
{
    auto* aw = new juce::AlertWindow ("Save Preset", "Enter a preset name:",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", presetManager.getCurrentPresetName());
    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result == 1)
        {
            const auto name = aw->getTextEditorContents ("name");
            if (presetManager.saveUserPreset (name))
            {
                refresh();
                for (int j = 0; j < userNames.size(); ++j)
                    if (userNames[j] == name.trim())
                        presetBox.setSelectedId (1000 + j, juce::dontSendNotification);
            }
        }
    }), true);
}

void mf::PresetBar::paint (juce::Graphics&) {}

void mf::PresetBar::resized()
{
    auto r = getLocalBounds().reduced (2, 4);
    caption.setBounds (r.removeFromLeft (58));
    r.removeFromLeft (4);

    prevButton.setBounds (r.removeFromLeft (28));
    r.removeFromLeft (3);
    nextButton.setBounds (r.removeFromLeft (28));
    r.removeFromLeft (6);

    saveButton.setBounds (r.removeFromRight (60));
    r.removeFromRight (4);
    copyButton.setBounds (r.removeFromRight (60));
    r.removeFromRight (4);
    abButton.setBounds   (r.removeFromRight (40));
    r.removeFromRight (8);

    presetBox.setBounds (r);
}

// ===========================================================================
//  Editor
// ===========================================================================
MasterForgeAudioProcessorEditor::MasterForgeAudioProcessorEditor (MasterForgeAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p),
      presetBar (p.getPresetManager()),
      eqDisplay (p, p.getAPVTS()),
      multiband (p.getAPVTS()),
      limiterPanel (p.getAPVTS())
{
    setLookAndFeel (&lookAndFeel);
    auto& state = processorRef.getAPVTS();

    titleLabel.setText ("MASTER FORGE", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (24.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, LnF::accent);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<mf::ButtonAttachment> (state, pid::bypass, bypassButton);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (eqDisplay);

    eqSection.addKnob (state, pid::eqLowFreq,  "LOW Hz");
    eqSection.addKnob (state, pid::eqLowGain,  "LOW dB");
    eqSection.addKnob (state, pid::eqLmFreq,   "LMID Hz");
    eqSection.addKnob (state, pid::eqLmGain,   "LMID dB");
    eqSection.addKnob (state, pid::eqLmQ,      "LMID Q");
    eqSection.addKnob (state, pid::eqHmFreq,   "HMID Hz");
    eqSection.addKnob (state, pid::eqHmGain,   "HMID dB");
    eqSection.addKnob (state, pid::eqHmQ,      "HMID Q");
    eqSection.addKnob (state, pid::eqHighFreq, "HIGH Hz");
    eqSection.addKnob (state, pid::eqHighGain, "HIGH dB");
    addAndMakeVisible (eqSection);

    addAndMakeVisible (multiband);

    gainSection.addKnob (state, pid::inputGain,  "INPUT");
    gainSection.addKnob (state, pid::outputGain, "OUTPUT");
    addAndMakeVisible (gainSection);

    characterSection.addKnob (state, pid::satDrive, "DRIVE");
    characterSection.addKnob (state, pid::satMix,   "SAT MIX");
    characterSection.addKnob (state, pid::width,    "WIDTH");
    addAndMakeVisible (characterSection);

    addAndMakeVisible (limiterPanel);

    meterPanel.onResetIntegrated = [this] { processorRef.resetIntegratedLUFS(); };
    addAndMakeVisible (meterPanel);

    setSize (1180, 840);
    startTimerHz (30);
}

MasterForgeAudioProcessorEditor::~MasterForgeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MasterForgeAudioProcessorEditor::timerCallback()
{
    meterPanel.update (processorRef.getOutputPeakLDb(),
                       processorRef.getOutputPeakRDb(),
                       processorRef.getMomentaryLUFS(),
                       processorRef.getShortTermLUFS(),
                       processorRef.getIntegratedLUFS(),
                       processorRef.getCompReductionLowDb(),
                       processorRef.getCompReductionMidDb(),
                       processorRef.getCompReductionHiDb(),
                       processorRef.getLimReductionDb());

    multiband.setReductions (processorRef.getCompReductionLowDb(),
                             processorRef.getCompReductionMidDb(),
                             processorRef.getCompReductionHiDb());

    eqDisplay.refresh();
}

void MasterForgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (LnF::background);

    auto header = getLocalBounds().removeFromTop (52);
    g.setGradientFill (juce::ColourGradient (LnF::panel, 0.0f, 0.0f,
                                             LnF::background, 0.0f, 52.0f, false));
    g.fillRect (header);
    g.setColour (LnF::accent);
    g.fillRect (0, 50, getWidth(), 2);

    g.setColour (LnF::textDim);
    g.setFont (11.0f);
    g.drawText ("MASTERING CONSOLE", 232, 19, 240, 16, juce::Justification::centredLeft);
}

void MasterForgeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (52);
    titleLabel.setBounds (header.removeFromLeft (224).withTrimmedLeft (16));
    bypassButton.setBounds (header.removeFromRight (120).reduced (14, 14));

    presetBar.setBounds (area.removeFromTop (40).reduced (10, 2));

    area.reduce (10, 8);

    auto meterArea = area.removeFromRight (256);
    meterPanel.setBounds (meterArea);
    area.removeFromRight (10);

    eqDisplay.setBounds (area.removeFromTop (196));
    area.removeFromTop (8);
    eqSection.setBounds (area.removeFromTop (118));
    area.removeFromTop (8);
    multiband.setBounds (area.removeFromTop (212));
    area.removeFromTop (8);

    auto bottom = area;
    gainSection.setBounds (bottom.removeFromLeft ((int) (bottom.getWidth() * 0.26f)));
    bottom.removeFromLeft (8);
    limiterPanel.setBounds (bottom.removeFromRight ((int) (bottom.getWidth() * 0.40f)));
    bottom.removeFromRight (8);
    characterSection.setBounds (bottom);
}
