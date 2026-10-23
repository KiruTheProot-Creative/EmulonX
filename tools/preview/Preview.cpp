// Emulon X — renders the plugin window to PNG files and runs an audio self-test
// through the full processor, without a DAW.
// Developer tool: build with -DEMX_BUILD_PREVIEW=ON, then run
//     EmulonXPreview <output-folder>
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

/** Writes a looped 24-bit test WAV whose name carries its root note. */
static File writeDemoSample (const File& dir, const String& name, double freq)
{
    const double sr = 44100.0;
    AudioBuffer<float> buf (2, (int) (sr * 2.0));
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const auto v = (float) (0.4 * std::sin (MathConstants<double>::twoPi * freq * t)
                              + 0.15 * std::sin (MathConstants<double>::twoPi * freq * 2.003 * t));
        buf.setSample (0, i, v);
        buf.setSample (1, i, v * 0.9f);
    }
    auto f = dir.getChildFile (name + ".wav");
    f.deleteFile();
    std::unique_ptr<OutputStream> out (f.createOutputStream().release());
    std::unordered_map<String, String> meta { { "NumSampleLoops", "1" }, { "Loop0Start", "22050" }, { "Loop0End", "88199" },
                                              { "MidiUnityNote", String (emx::noteFromName (name)) } };
    if (auto w = WavAudioFormat().createWriterFor (out, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2)
                                                          .withBitsPerSample (24).withMetadataValues (meta)))
        w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    return f;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juceInit;
    const auto outDir = argc > 1 ? File (String (argv[1])) : File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    emx::EmulonXProcessor proc;

    // Load three demo samples the same way a drag & drop does, then let the loader finish.
    const auto demoDir = File::getSpecialLocation (File::tempDirectory).getChildFile ("emulonx-preview");
    demoDir.createDirectory();
    proc.loadFiles ({ writeDemoSample (demoDir, "Glass Pad_C3", 130.81), writeDemoSample (demoDir, "Glass Pad_C4", 261.63),
                      writeDemoSample (demoDir, "Glass Pad_C5", 523.25) });
    for (int i = 0; i < 100 && (proc.isLoading() || proc.getZones().size() < 3); ++i)
        MessageManager::getInstance()->runDispatchLoopUntil (50);

    String zonesInfo;
    for (auto& z : proc.getZones())
        zonesInfo << z.name << " [" << emx::noteName (z.params.loKey) << "-" << emx::noteName (z.params.hiKey)
                  << " root " << emx::noteName (z.params.root) << (z.params.loopMode == emx::LoopMode::loop ? " loop" : "") << "]  ";
    std::printf ("zones: %s\n", zonesInfo.toRawUTF8());

    // Audio self-test through the whole processor (MIDI -> engine -> master volume).
    {
        proc.prepareToPlay (48000.0, 512);
        AudioBuffer<float> buf (2, 512);
        auto runNote = [&] (int note)
        {
            MidiBuffer midi;
            midi.addEvent (MidiMessage::noteOn (1, note, (uint8) 100), 100);
            double sum = 0;
            for (int b = 0; b < 40; ++b)
            {
                proc.processBlock (buf, midi);
                midi.clear();
                sum += buf.getRMSLevel (0, 0, 512);
            }
            MidiBuffer off;
            off.addEvent (MidiMessage::noteOff (1, note), 0);
            for (int b = 0; b < 100; ++b) { proc.processBlock (buf, off); off.clear(); }
            return sum / 40.0;
        };
        const auto c4 = runNote (60), g6 = runNote (91);
        proc.apvts.getParameter (emx::masterParamId)->setValueNotifyingHost (0.0f);
        for (int b = 0; b < 20; ++b) { MidiBuffer none; proc.processBlock (buf, none); }
        const auto silent = runNote (60);
        proc.apvts.getParameter (emx::masterParamId)->setValueNotifyingHost (proc.apvts.getParameter (emx::masterParamId)->getDefaultValue());
        std::printf ("audio test: C4 rms=%.4f, G6 rms=%.4f, master at -inf rms=%.6f, voices after release=%d\n",
                     c4, g6, silent, proc.getActiveVoices());
    }

    // State round trip: save, clear, restore -> the same zones come back.
    {
        MemoryBlock state;
        proc.getStateInformation (state);
        proc.clearZones();
        proc.setStateInformation (state.getData(), (int) state.getSize());
        for (int i = 0; i < 100 && (proc.isLoading() || proc.getZones().empty()); ++i)
            MessageManager::getInstance()->runDispatchLoopUntil (50);
        int loaded = 0;
        for (auto& z : proc.getZones())
            if (z.sample != nullptr)
                ++loaded;
        std::printf ("state round trip: %d zones restored, %d with audio\n", (int) proc.getZones().size(), loaded);
    }

    const float demo[] { 0.0f, 0.25f, 0.6f, 1.0f, 0.4f };
    for (int i = 0; i < emx::numMacros; ++i)
        proc.apvts.getParameter (emx::macroParamId (i))->setValueNotifyingHost (demo[i % 5]);
    proc.setMacroName (0, "CUTOFF");
    proc.setMacroName (1, "SPACE");

    std::unique_ptr<AudioProcessorEditor> base (proc.createEditor());
    auto& editor = dynamic_cast<emx::EmulonXEditor&> (*base);

    auto save = [&] (const String& name)
    {
        const auto img = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile (name + ".png");
        file.deleteFile();
        FileOutputStream out (file);
        PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };

    const char* pages[] { "main", "editor", "fx" };
    for (int p = 0; p < 3; ++p)
    {
        editor.showPage (p);
        save (String ("preview-") + pages[p] + "-100");
    }
    editor.showPage (0);
    for (auto s : { 0.75f, 1.5f })
    {
        editor.setUiScale (s);
        save ("preview-main-" + String (roundToInt (s * 100.0f)));
    }
    demoDir.deleteRecursively();
    return 0;
}
