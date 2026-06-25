#include "PluginEditor.h"

using LnF = mf::ForgeLookAndFeel;

// ===========================================================================
//  LabeledKnob
// ===========================================================================
mf::LabeledKnob::LabeledKnob (juce::AudioProcessorValueTreeState& state,
                              const juce::String& paramID, const juce::String& name)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible (slider);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, LnF::textDim);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);
}

void mf::LabeledKnob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
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

void mf::SectionPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (LnF::panel);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (LnF::panelBorder);
    g.drawRoundedRectangle (r, 7.0f, 1.4f);

    auto titleArea = r.removeFromTop (24.0f);
    g.setColour (LnF::accent);
    g.setFont (juce::Font (14.0f, juce::Font::bold));
    g.drawText (titleText, titleArea.reduced (10.0f, 0.0f), juce::Justification::centredLeft);

    g.setColour (LnF::accent.withAlpha (0.35f));
    g.fillRect (titleArea.getX() + 10.0f, titleArea.getBottom() - 1.0f, r.getWidth() - 20.0f, 1.0f);
}

void mf::SectionPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (20);

    const int n = knobs.size();
    if (n == 0)
        return;

    const int w = r.getWidth() / n;
    for (int i = 0; i < n; ++i)
        knobs[i]->setBounds ((i == n - 1 ? r : r.removeFromLeft (w)).reduced (3));
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
                             float grCompDb, float grLimDb)
{
    meterL.setLevel (outLDb);
    meterR.setLevel (outRDb);
    momentary = lufsM; shortTerm = lufsS; integrated = lufsI;
    grComp = grCompDb; grLim = grLimDb;
    repaint();
}

void mf::MeterPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (24);

    auto bottom = r.removeFromBottom (28);
    resetButton.setBounds (bottom.removeFromLeft (84).reduced (0, 2));

    auto metersArea = r.removeFromLeft (66);
    const int half = metersArea.getWidth() / 2;
    meterL.setBounds (metersArea.removeFromLeft (half).reduced (3, 2));
    meterR.setBounds (metersArea.reduced (3, 2));

    r.removeFromLeft (12);
    textBounds = r;
}

void mf::MeterPanel::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (LnF::panel);
    g.fillRoundedRectangle (full, 7.0f);
    g.setColour (LnF::panelBorder);
    g.drawRoundedRectangle (full, 7.0f, 1.4f);

    auto titleArea = full.removeFromTop (24.0f);
    g.setColour (LnF::accent);
    g.setFont (juce::Font (14.0f, juce::Font::bold));
    g.drawText ("METERING", titleArea.reduced (10.0f, 0.0f), juce::Justification::centredLeft);

    const auto fmt = [] (float v) { return v <= -99.0f ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e"))
                                                       : juce::String (v, 1); };

    auto t = textBounds;

    g.setColour (LnF::text);
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText ("LOUDNESS (LUFS)", t.removeFromTop (20), juce::Justification::centredLeft);

    const auto drawValue = [&] (const juce::String& name, const juce::String& value, juce::Colour c)
    {
        auto row = t.removeFromTop (22);
        g.setColour (LnF::textDim);
        g.setFont (12.0f);
        g.drawText (name, row.removeFromLeft (64), juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (juce::Font (15.0f, juce::Font::bold));
        g.drawText (value, row, juce::Justification::centredRight);
    };

    drawValue ("Moment.", fmt (momentary),  LnF::text);
    drawValue ("Short",   fmt (shortTerm),  LnF::text);
    drawValue ("Integr.", fmt (integrated), LnF::accentGlow);

    t.removeFromTop (10);
    g.setColour (LnF::text);
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText ("GAIN REDUCTION", t.removeFromTop (20), juce::Justification::centredLeft);

    const auto drawGr = [&] (const juce::String& name, float gr)
    {
        auto row = t.removeFromTop (24);
        g.setColour (LnF::textDim);
        g.setFont (11.0f);
        g.drawText (name, row.removeFromLeft (44), juce::Justification::centredLeft);

        auto bar = row.toFloat().reduced (2.0f, 6.0f);
        g.setColour (LnF::track.darker (0.2f));
        g.fillRoundedRectangle (bar, 2.0f);

        const float n = juce::jlimit (0.0f, 1.0f, gr / 18.0f);
        g.setColour (LnF::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * n), 2.0f);

        g.setColour (LnF::text);
        g.setFont (11.0f);
        g.drawText (juce::String (gr, 1) + " dB", row.toNearestInt(), juce::Justification::centredRight);
    };

    drawGr ("Comp",  grComp);
    drawGr ("Limit", grLim);
}

// ===========================================================================
//  Editor
// ===========================================================================
MasterForgeAudioProcessorEditor::MasterForgeAudioProcessorEditor (MasterForgeAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& state = processorRef.getAPVTS();

    titleLabel.setText ("MASTER FORGE", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (26.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, LnF::accent);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, mf::pid::bypass, bypassButton);

    eqSection.addKnob (state, mf::pid::eqLowFreq,  "LOW Hz");
    eqSection.addKnob (state, mf::pid::eqLowGain,  "LOW dB");
    eqSection.addKnob (state, mf::pid::eqLmFreq,   "LMID Hz");
    eqSection.addKnob (state, mf::pid::eqLmGain,   "LMID dB");
    eqSection.addKnob (state, mf::pid::eqLmQ,      "LMID Q");
    eqSection.addKnob (state, mf::pid::eqHmFreq,   "HMID Hz");
    eqSection.addKnob (state, mf::pid::eqHmGain,   "HMID dB");
    eqSection.addKnob (state, mf::pid::eqHmQ,      "HMID Q");
    eqSection.addKnob (state, mf::pid::eqHighFreq, "HIGH Hz");
    eqSection.addKnob (state, mf::pid::eqHighGain, "HIGH dB");
    addAndMakeVisible (eqSection);

    compSection.addKnob (state, mf::pid::compThresh,  "THRESH");
    compSection.addKnob (state, mf::pid::compRatio,   "RATIO");
    compSection.addKnob (state, mf::pid::compAttack,  "ATTACK");
    compSection.addKnob (state, mf::pid::compRelease, "RELEASE");
    compSection.addKnob (state, mf::pid::compKnee,    "KNEE");
    compSection.addKnob (state, mf::pid::compMakeup,  "MAKEUP");
    addAndMakeVisible (compSection);

    gainSection.addKnob (state, mf::pid::inputGain,  "INPUT");
    gainSection.addKnob (state, mf::pid::outputGain, "OUTPUT");
    addAndMakeVisible (gainSection);

    characterSection.addKnob (state, mf::pid::satDrive, "DRIVE");
    characterSection.addKnob (state, mf::pid::satMix,   "SAT MIX");
    characterSection.addKnob (state, mf::pid::width,    "WIDTH");
    addAndMakeVisible (characterSection);

    limiterSection.addKnob (state, mf::pid::limCeiling, "CEILING");
    limiterSection.addKnob (state, mf::pid::limRelease, "RELEASE");
    addAndMakeVisible (limiterSection);

    meterPanel.onResetIntegrated = [this] { processorRef.resetIntegratedLUFS(); };
    addAndMakeVisible (meterPanel);

    setSize (1040, 660);   // fixed size (editors are non-resizable by default)
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
                       processorRef.getCompReductionDb(),
                       processorRef.getLimReductionDb());
}

void MasterForgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (LnF::background);

    auto header = getLocalBounds().removeFromTop (58);
    g.setGradientFill (juce::ColourGradient (LnF::panel, 0.0f, 0.0f,
                                             LnF::background, 0.0f, 58.0f, false));
    g.fillRect (header);

    g.setColour (LnF::accent);
    g.fillRect (0, 56, getWidth(), 2);

    g.setColour (LnF::textDim);
    g.setFont (12.0f);
    g.drawText ("MASTERING CONSOLE  \xe2\x80\xa2  EQ / DYNAMICS / SATURATION / WIDTH / LIMIT",
                250, 22, 520, 16, juce::Justification::centredLeft);
}

void MasterForgeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (58);
    titleLabel.setBounds (header.removeFromLeft (236).withTrimmedLeft (16));
    bypassButton.setBounds (header.removeFromRight (130).reduced (16, 16));

    area.reduce (10, 10);

    auto meterArea = area.removeFromRight (250);
    meterPanel.setBounds (meterArea);
    area.removeFromRight (10);

    eqSection.setBounds (area.removeFromTop (190));
    area.removeFromTop (8);
    compSection.setBounds (area.removeFromTop (165));
    area.removeFromTop (8);

    auto bottom = area;
    gainSection.setBounds (bottom.removeFromLeft ((int) (bottom.getWidth() * 0.24f)));
    bottom.removeFromLeft (8);
    limiterSection.setBounds (bottom.removeFromRight ((int) (bottom.getWidth() * 0.34f)));
    bottom.removeFromRight (8);
    characterSection.setBounds (bottom);
}
