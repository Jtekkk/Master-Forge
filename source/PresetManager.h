#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mf
{
/**
    Preset handling for Master Forge:
      - built-in factory presets,
      - user presets saved to / loaded from disk,
      - an A/B compare pair of state snapshots.
*/
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    // ---- factory ----------------------------------------------------------
    juce::StringArray getFactoryPresetNames() const;
    void loadFactoryPreset (int index);

    // ---- user -------------------------------------------------------------
    juce::StringArray getUserPresetNames() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    juce::File getUserPresetDirectory() const;

    juce::String getCurrentPresetName() const { return currentName; }

    // ---- A/B --------------------------------------------------------------
    void toggleAB();                 // switch the live state between slot A and B
    void copyToOtherSlot();          // copy the current state into the other slot
    bool isSlotA() const noexcept { return currentIsA; }

private:
    void resetToDefaults();
    void applyValues (const std::vector<std::pair<juce::String, float>>& values);

    struct FactoryPreset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };
    static const std::vector<FactoryPreset>& factoryPresets();

    juce::AudioProcessorValueTreeState& apvts;
    juce::ValueTree slotA, slotB;
    bool currentIsA = true;
    juce::String currentName { "Default" };
};
} // namespace mf
