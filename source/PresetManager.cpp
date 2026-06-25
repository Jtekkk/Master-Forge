#include "PresetManager.h"
#include "Parameters.h"

namespace mf
{
namespace pid = mf::pid;

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    slotA = apvts.copyState();
    slotB = apvts.copyState();
}

const std::vector<PresetManager::FactoryPreset>& PresetManager::factoryPresets()
{
    // Each preset only lists the parameters it cares about; everything else is
    // reset to its default first (see loadFactoryPreset).
    static const std::vector<FactoryPreset> presets =
    {
        { "Default", {} },

        { "Transparent Master", {
            { pid::mbLowThresh, -16.0f }, { pid::mbLowRatio, 1.8f },
            { pid::mbMidThresh, -16.0f }, { pid::mbMidRatio, 1.8f },
            { pid::mbHiThresh,  -16.0f }, { pid::mbHiRatio,  1.8f },
            { pid::compAttack, 20.0f },   { pid::compRelease, 200.0f },
            { pid::limCeiling, -1.0f },   { pid::outputGain, 2.0f } } },

        { "Loud & Proud", {
            { pid::inputGain, 3.0f },
            { pid::eqLowGain, 1.5f }, { pid::eqLowFreq, 80.0f },
            { pid::eqHighGain, 2.0f }, { pid::eqHighFreq, 9000.0f },
            { pid::mbLowThresh, -22.0f }, { pid::mbLowRatio, 3.0f }, { pid::mbLowMakeup, 2.0f },
            { pid::mbMidThresh, -24.0f }, { pid::mbMidRatio, 3.5f }, { pid::mbMidMakeup, 3.0f },
            { pid::mbHiThresh,  -22.0f }, { pid::mbHiRatio,  3.0f }, { pid::mbHiMakeup,  2.0f },
            { pid::compAttack, 5.0f }, { pid::compRelease, 120.0f },
            { pid::satDrive, 8.0f }, { pid::satMix, 25.0f },
            { pid::outputGain, 6.0f }, { pid::limCeiling, -0.3f } } },

        { "Gentle Glue", {
            { pid::mbLowThresh, -14.0f }, { pid::mbLowRatio, 1.5f },
            { pid::mbMidThresh, -14.0f }, { pid::mbMidRatio, 1.5f },
            { pid::mbHiThresh,  -14.0f }, { pid::mbHiRatio,  1.5f },
            { pid::compAttack, 30.0f }, { pid::compRelease, 250.0f }, { pid::compKnee, 12.0f },
            { pid::limCeiling, -1.0f } } },

        { "Warm Tape", {
            { pid::eqLowGain, 2.0f }, { pid::eqLowFreq, 90.0f },
            { pid::eqHighGain, -1.5f }, { pid::eqHighFreq, 12000.0f },
            { pid::mbLowThresh, -20.0f }, { pid::mbLowRatio, 2.5f },
            { pid::mbMidThresh, -18.0f }, { pid::mbMidRatio, 2.0f },
            { pid::mbHiThresh,  -16.0f }, { pid::mbHiRatio,  1.8f },
            { pid::satDrive, 12.0f }, { pid::satMix, 45.0f },
            { pid::width, 110.0f }, { pid::outputGain, 4.0f }, { pid::limCeiling, -0.5f } } },

        { "Bright & Wide", {
            { pid::eqHmGain, 2.5f }, { pid::eqHmFreq, 4000.0f }, { pid::eqHmQ, 0.8f },
            { pid::eqHighGain, 3.5f }, { pid::eqHighFreq, 10000.0f },
            { pid::mbHiThresh, -22.0f }, { pid::mbHiRatio, 2.5f }, { pid::mbHiMakeup, 1.5f },
            { pid::width, 135.0f }, { pid::outputGain, 4.0f }, { pid::limCeiling, -0.3f } } },

        { "Streaming -14 LUFS", {
            { pid::inputGain, 2.0f },
            { pid::mbLowThresh, -20.0f }, { pid::mbLowRatio, 2.5f }, { pid::mbLowMakeup, 1.5f },
            { pid::mbMidThresh, -20.0f }, { pid::mbMidRatio, 2.5f }, { pid::mbMidMakeup, 1.5f },
            { pid::mbHiThresh,  -20.0f }, { pid::mbHiRatio,  2.5f }, { pid::mbHiMakeup,  1.5f },
            { pid::compAttack, 10.0f }, { pid::compRelease, 150.0f },
            { pid::satMix, 15.0f }, { pid::outputGain, 5.0f },
            { pid::limCeiling, -1.0f }, { pid::limTruePeak, 1.0f } } },
    };
    return presets;
}

juce::StringArray PresetManager::getFactoryPresetNames() const
{
    juce::StringArray names;
    for (const auto& p : factoryPresets())
        names.add (p.name);
    return names;
}

void PresetManager::resetToDefaults()
{
    for (auto* param : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            rp->setValueNotifyingHost (rp->getDefaultValue());
}

void PresetManager::applyValues (const std::vector<std::pair<juce::String, float>>& values)
{
    for (const auto& [id, value] : values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void PresetManager::loadFactoryPreset (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    resetToDefaults();
    applyValues (presets[(size_t) index].values);
    currentName = presets[(size_t) index].name;
}

juce::File PresetManager::getUserPresetDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Forge Audio")
                   .getChildFile ("Master Forge")
                   .getChildFile ("Presets");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.mfpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool PresetManager::saveUserPreset (const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return false;

    auto file = getUserPresetDirectory().getChildFile (trimmed + ".mfpreset");
    if (auto xml = apvts.copyState().createXml())
    {
        currentName = trimmed;
        return xml->writeTo (file);
    }
    return false;
}

bool PresetManager::loadUserPreset (const juce::String& name)
{
    auto file = getUserPresetDirectory().getChildFile (name + ".mfpreset");
    if (! file.existsAsFile())
        return false;

    if (auto xml = juce::XmlDocument::parse (file))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentName = name;
            return true;
        }
    }
    return false;
}

bool PresetManager::deleteUserPreset (const juce::String& name)
{
    return getUserPresetDirectory().getChildFile (name + ".mfpreset").deleteFile();
}

void PresetManager::toggleAB()
{
    if (currentIsA)
    {
        slotA = apvts.copyState();
        apvts.replaceState (slotB.createCopy());
    }
    else
    {
        slotB = apvts.copyState();
        apvts.replaceState (slotA.createCopy());
    }
    currentIsA = ! currentIsA;
}

void PresetManager::copyToOtherSlot()
{
    if (currentIsA)
        slotB = apvts.copyState();
    else
        slotA = apvts.copyState();
}
} // namespace mf
