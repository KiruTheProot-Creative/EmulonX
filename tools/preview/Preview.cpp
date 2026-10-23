// Emulon X — renders the plugin window to PNG files without a DAW.
// Developer tool: build with -DEMX_BUILD_PREVIEW=ON, then run
//     EmulonXPreview <output-folder>
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    emx::EmulonXProcessor proc;

    // Audio self-test: one note through the processor, at 0 dB and at -60 (silent).
    {
        proc.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        auto runNote = [&]
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
            double sum = 0;
            for (int b = 0; b < 20; ++b)
            {
                proc.processBlock (buf, midi);
                midi.clear();
                sum += buf.getRMSLevel (0, 0, 512);
            }
            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            for (int b = 0; b < 100; ++b) { proc.processBlock (buf, off); off.clear(); }
            return sum / 20.0;
        };
        const auto loud = runNote();
        proc.apvts.getParameter (emx::masterParamId)->setValueNotifyingHost (0.0f);
        for (int b = 0; b < 20; ++b) { juce::MidiBuffer none; proc.processBlock (buf, none); }
        const auto silent = runNote();
        proc.apvts.getParameter (emx::masterParamId)->setValueNotifyingHost (proc.apvts.getParameter (emx::masterParamId)->getDefaultValue());
        std::printf ("audio test: note at 0 dB rms=%.4f, master at -inf rms=%.6f, voices after release=%d\n", loud, silent, proc.getActiveVoices());
    }
    // Give a few macros visible values so the knobs aren't all at zero.
    const float demo[] { 0.0f, 0.25f, 0.6f, 1.0f, 0.4f };
    for (int i = 0; i < emx::numMacros; ++i)
        proc.apvts.getParameter (emx::macroParamId (i))->setValueNotifyingHost (demo[i % 5]);
    proc.setMacroName (0, "CUTOFF");
    proc.setMacroName (1, "SPACE");

    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto& editor = dynamic_cast<emx::EmulonXEditor&> (*base);

    auto save = [&] (const juce::String& name)
    {
        const auto img = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };

    const char* pages[] { "main", "editor", "fx" };
    for (int p = 0; p < 3; ++p)
    {
        editor.showPage (p);
        save (juce::String ("preview-") + pages[p] + "-100");
    }

    editor.showPage (0);
    for (auto s : { 0.75f, 1.5f })
    {
        editor.setUiScale (s);
        save ("preview-main-" + juce::String (juce::roundToInt (s * 100.0f)));
    }
    return 0;
}
