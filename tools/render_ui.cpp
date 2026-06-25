// Offline screenshot tool: builds the processor + editor, feeds a little audio
// so the analyzer has something to draw, renders the editor to a PNG (no
// display / native window needed) and writes it out.
//
//   MasterForgeRender [output.png] [factoryPresetIndex]

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <cmath>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String outPath = argc > 1 ? juce::String (argv[1]) : "masterforge_ui.png";
    const int presetIndex      = argc > 2 ? juce::String (argv[2]).getIntValue() : 1;

    const double sr = 48000.0;
    const int    block = 512;

    MasterForgeAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    proc.getPresetManager().loadFactoryPreset (presetIndex);

    // Feed a multi-tone signal so the spectrum analyzer has content to display.
    juce::MidiBuffer midi;
    long long n = 0;
    for (int b = 0; b < 48; ++b)
    {
        juce::AudioBuffer<float> buf (2, block);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const double t = (double) (n + i) / sr;
                float s = 0.0f;
                for (double f : { 55.0, 110.0, 220.0, 440.0, 900.0, 1800.0, 4000.0, 9000.0 })
                    s += (float) (0.10 * std::sin (2.0 * juce::MathConstants<double>::pi * f * t));
                buf.setSample (ch, i, s);
            }
        n += block;
        proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    if (auto* mf = dynamic_cast<MasterForgeAudioProcessorEditor*> (editor.get()))
        for (int i = 0; i < 12; ++i) mf->refreshUi();   // populate spectrum smoothing

    const int w = editor->getWidth();
    const int h = editor->getHeight();
    juce::Image image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, false);
    }

    auto file = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    if (stream.openedOk())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (image, stream);
        std::cout << "Wrote " << file.getFullPathName() << "  (" << w << "x" << h << ")\n";
        return 0;
    }

    std::cerr << "Failed to open " << file.getFullPathName() << "\n";
    return 1;
}
