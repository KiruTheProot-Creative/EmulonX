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
    for (int i = 0; i < 100 && (proc.isBusy() || proc.getZones().size() < 3); ++i)
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

    auto waitIdle = [&proc]
    {
        for (int i = 0; i < 200; ++i)
        {
            MessageManager::getInstance()->runDispatchLoopUntil (30);
            if (! proc.isBusy())
                break;
        }
        MessageManager::getInstance()->runDispatchLoopUntil (30);
    };

    // Save a preset, build a bank, export it.
    const auto presetDir = demoDir.getChildFile ("Presets"), bankDir = demoDir.getChildFile ("Banks");
    presetDir.createDirectory();
    bankDir.createDirectory();
    proc.setPresetName ("Glass Pad");
    proc.savePresetAs (presetDir.getChildFile ("Glass Pad.emxp"));
    waitIdle();
    proc.addCurrentPresetToBank();
    waitIdle();
    proc.setPresetName ("Glass Pad Bright");
    proc.addCurrentPresetToBank();
    waitIdle();
    proc.setBankName ("Demo Bank");
    proc.exportBank (bankDir.getChildFile ("Demo Bank.emxb"));
    waitIdle();
    std::printf ("preset file: %lld bytes, bank file: %lld bytes, bank entries %d, message '%s'\n",
                 presetDir.getChildFile ("Glass Pad.emxp").getSize(), bankDir.getChildFile ("Demo Bank.emxb").getSize(),
                 (int) proc.getBank().size(), proc.getLastMessage().toRawUTF8());

    // Project state with the samples inside: delete the WAVs, restore, everything must still play.
    {
        MemoryBlock state;
        for (int i = 0; i < 20; ++i) MessageManager::getInstance()->runDispatchLoopUntil (60);   // let the embed cache build
        proc.getStateInformation (state);
        for (auto n : { "Glass Pad_C3", "Glass Pad_C4", "Glass Pad_C5" })
            demoDir.getChildFile (String (n) + ".wav").deleteFile();
        proc.newPreset();
        proc.setStateInformation (state.getData(), (int) state.getSize());
        waitIdle();
        int loaded = 0;
        for (auto& z : proc.getZones())
            if (z.sample != nullptr)
                ++loaded;
        std::printf ("state round trip (embedded, %lld KB): %d zones restored, %d with audio, preset '%s'\n",
                     (long long) state.getSize() / 1024, (int) proc.getZones().size(), loaded, proc.getPresetName().toRawUTF8());
    }

    // Open the bank file and load its second preset.
    proc.clearBank();
    proc.openBank (bankDir.getChildFile ("Demo Bank.emxb"));
    waitIdle();
    proc.loadBankEntry (1);
    waitIdle();
    std::printf ("bank reopened: '%s' with %d presets; loaded '%s' (%d zones)\n", proc.getBankName().toRawUTF8(),
                 (int) proc.getBank().size(), proc.getPresetName().toRawUTF8(), (int) proc.getZones().size());

    // Zone editing + undo / redo.
    {
        const int id = proc.getZones()[0].id;
        const int ls0 = proc.getZones()[0].params.loopStart;
        auto q = proc.getZones()[0].params;
        q.loopStart = 1000;
        proc.setZoneParams (id, q);
        q.loopStart = 1200;                     // same gesture: must merge into one undo step
        proc.setZoneParams (id, q);
        const bool edited = proc.getZones()[0].params.loopStart == 1200;
        proc.undo();
        const bool undone = proc.getZones()[0].params.loopStart == ls0;
        proc.redo();
        const bool redone = proc.getZones()[0].params.loopStart == 1200;
        const auto count = proc.getZones().size();
        proc.duplicateZone (id);
        const bool duplicated = proc.getZones().size() == count + 1 && proc.getSelectedIndex() == 1;
        proc.undo();
        const bool dupUndone = proc.getZones().size() == count;
        std::printf ("editing: edit %s, undo %s, redo %s, duplicate %s, undo duplicate %s\n",
                     edited ? "ok" : "FAIL", undone ? "ok" : "FAIL", redone ? "ok" : "FAIL",
                     duplicated ? "ok" : "FAIL", dupUndone ? "ok" : "FAIL");
        proc.undo();   // back to the original loop
    }

    // FX: chains on the preset and on a zone, modulation, audio through it, preset + project round trips.
    {
        using namespace emx;
        proc.addFx (0, fx::Type::filter);
        proc.addFx (0, fx::Type::delay);
        proc.addFx (0, fx::Type::reverb);
        const int zoneId = proc.getZones()[0].id;
        proc.addFx (zoneId, fx::Type::dist);
        const int filterId = proc.getFxSetup().master[0].id, reverbId = proc.getFxSetup().master[2].id;
        proc.setFxParam (filterId, 1, 0.55f);
        proc.toggleRoute ({ fx::SrcKind::env, 0 }, { filterId, 0 });
        proc.toggleRoute ({ fx::SrcKind::macro, 0 }, { reverbId, 6 });
        proc.toggleRoute ({ fx::SrcKind::macro, 1 }, { -1, 0 });
        proc.setRouteAmount (2, -0.3f);
        proc.setMacroName (0, "SPACE");
        proc.setMacroName (1, "DUCK");

        proc.prepareToPlay (48000.0, 512);
        AudioBuffer<float> b (2, 512);
        double sum = 0;
        bool finite = true;
        for (int i = 0; i < 80; ++i)
        {
            MidiBuffer m;
            if (i == 0) m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
            if (i == 40) m.addEvent (MidiMessage::noteOff (1, 60), 0);
            proc.processBlock (b, m);
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 512; ++k)
                    finite = finite && std::isfinite (b.getSample (c, k));
            sum += b.getRMSLevel (0, 0, 512);
        }
        std::printf ("fx audio: rms %.4f, finite %s, modulated cutoff %.3f\n", sum / 80.0, finite ? "yes" : "NO",
                     proc.getFxEngine().modOut[1].value[0].load());

        const auto fxPreset = demoDir.getChildFile ("Presets").getChildFile ("FX Test.emxp");
        proc.setPresetName ("FX Test");
        proc.savePresetAs (fxPreset);
        waitIdle();
        proc.newPreset();
        proc.loadFiles ({ fxPreset });
        waitIdle();
        int zoneFx = 0;
        for (auto& z : proc.getZones()) zoneFx += (int) z.fx.size();
        std::printf ("fx preset round trip: %d preset effects, %d zone effects, %d routes, %d envelopes\n",
                     (int) proc.getFxSetup().master.size(), zoneFx, (int) proc.getFxSetup().routes.size(),
                     (int) proc.getFxSetup().envelopes.size());

        MemoryBlock state;
        proc.getStateInformation (state);
        proc.newPreset();
        proc.setStateInformation (state.getData(), (int) state.getSize());
        waitIdle();
        zoneFx = 0;
        for (auto& z : proc.getZones()) zoneFx += (int) z.fx.size();
        std::printf ("fx project round trip: %d preset effects, %d zone effects, %d routes\n",
                     (int) proc.getFxSetup().master.size(), zoneFx, (int) proc.getFxSetup().routes.size());
    }

    // Keep speed must come back exactly as saved (all on, all off, mixed).
    {
        const auto ksFile = demoDir.getChildFile ("Presets").getChildFile ("KS Test.emxp");
        String report;
        for (int variant = 0; variant < 3; ++variant)
        {
            if (variant < 2) proc.setKeepSpeedForAll (variant == 0);
            else { auto q = proc.getZones()[0].params; q.keepSpeed = ! q.keepSpeed; proc.setZoneParams (proc.getZones()[0].id, q); }
            String before;
            for (auto& z : proc.getZones()) before << (z.params.keepSpeed ? "1" : "0");
            proc.savePresetAs (ksFile);
            waitIdle();
            proc.newPreset();
            proc.loadFiles ({ ksFile });
            waitIdle();
            String after;
            for (auto& z : proc.getZones()) after << (z.params.keepSpeed ? "1" : "0");
            MemoryBlock st;
            proc.getStateInformation (st);
            proc.newPreset();
            proc.setStateInformation (st.getData(), (int) st.getSize());
            waitIdle();
            String project;
            for (auto& z : proc.getZones()) project << (z.params.keepSpeed ? "1" : "0");
            report << before << ">" << after << ">" << project << "  ";
        }
        std::printf ("keep speed save/load (zones before > preset > project): %s\n", report.toRawUTF8());
    }

    // SEMI knob modulated by an envelope: saved in presets and projects.
    {
        auto q = proc.getZones()[1].params;
        q.semitones = -5;
        proc.setZoneParams (proc.getZones()[1].id, q);
        proc.toggleRoute ({ emx::fx::SrcKind::env, 0 }, { emx::fx::Target::zonePitch, proc.getZones()[1].id });
        const auto file = demoDir.getChildFile ("Presets").getChildFile ("Semi Test.emxp");
        proc.savePresetAs (file);
        waitIdle();
        proc.newPreset();
        proc.loadFiles ({ file });
        waitIdle();
        auto countSemiRoutes = [&proc]
        {
            int n = 0;
            for (auto& r : proc.getFxSetup().routes)
                if (r.target.isZonePitch() && proc.getZones().size() > 1 && r.target.slot == proc.getZones()[1].id) ++n;
            return n;
        };
        const int afterPreset = countSemiRoutes();
        const int semiPreset = proc.getZones().size() > 1 ? proc.getZones()[1].params.semitones : 99;
        MemoryBlock st;
        proc.getStateInformation (st);
        proc.newPreset();
        proc.setStateInformation (st.getData(), (int) st.getSize());
        waitIdle();
        std::printf ("semi: preset %d st / %d route, project %d st / %d route, target '%s'\n", semiPreset, afterPreset,
                     proc.getZones().size() > 1 ? proc.getZones()[1].params.semitones : 99, countSemiRoutes(),
                     proc.getZones().size() > 1 ? proc.describeTarget ({ emx::fx::Target::zonePitch, proc.getZones()[1].id }).toRawUTF8() : "");
    }

    // Arpeggiator through the whole processor, and mono mode.
    {
        auto a = proc.getArpSettings();
        a.enabled = true; a.mode = emx::ArpSettings::upDown; a.octaves = 2; a.swing = 0.2f;
        auto& pa = a.patterns[0];
        pa.length = 12;
        pa.steps[3].on = false; pa.steps[6].transpose = 7; pa.steps[9].velocity = 0.35f; pa.steps[10].transpose = -5;
        a.patterns[1].length = 8;
        a.patterns[1].steps[2].transpose = 12;
        a.arrangementLength = 4;
        a.arrangement = { 0, 0, 1, 0 };
        proc.setArpSettings (a);
        AudioBuffer<float> b (2, 512);
        int maxVoices = 0;
        double sum = 0;
        for (int i = 0; i < 60; ++i)
        {
            MidiBuffer m;
            if (i == 0) { m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0); m.addEvent (MidiMessage::noteOn (1, 64, (uint8) 100), 0); m.addEvent (MidiMessage::noteOn (1, 67, (uint8) 100), 0); }
            proc.processBlock (b, m);
            maxVoices = jmax (maxVoices, proc.getActiveVoices());
            sum += b.getRMSLevel (0, 0, 512);
        }
        std::printf ("arp audio: rms %.4f, step %d, voices up to %d\n", sum / 60.0, proc.getArp().currentStep.load(), maxVoices);
        auto v = proc.getVoiceSettings();
        v.mono = true; v.glide = 0.12f;
        proc.setVoiceSettings (v);
    }

    // Show the demo folders in the explorer.
    auto& settings = proc.getSettings();
    settings.setFolders (emx::Settings::Folder::samples, { File::getSpecialLocation (File::userHomeDirectory).getFullPathName() });
    settings.setFolders (emx::Settings::Folder::other, { bankDir.getFullPathName() });
    settings.setFolders (emx::Settings::Folder::user, { presetDir.getFullPathName() });
    settings.setFolders (emx::Settings::Folder::base, {});

    const float demo[] { 0.0f, 0.25f, 0.6f, 1.0f, 0.4f };
    for (int i = 0; i < emx::numMacros; ++i)
        proc.apvts.getParameter (emx::macroParamId (i))->setValueNotifyingHost (demo[i % 5]);
    proc.setMacroName (0, "CUTOFF");
    proc.setMacroName (1, "SPACE");

    std::unique_ptr<AudioProcessorEditor> base (proc.createEditor());
    auto& editor = dynamic_cast<emx::EmulonXEditor&> (*base);

    auto save = [&] (const String& name)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (120);   // let async layout (trees, lists) settle
        const auto img = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile (name + ".png");
        file.deleteFile();
        FileOutputStream out (file);
        PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };

    proc.selectZone (proc.getZones().size() > 1 ? proc.getZones()[1].id : 0);
    if (auto* sz = proc.getSelectedZone(); sz != nullptr && sz->sample != nullptr)
    {
        auto q = sz->params;
        q.sampleStart = 6000;
        q.sampleEnd = sz->sample->length() - 4000;
        q.crossfade = 3000;
        q.randomStart = true;
        q.randomEnd = 20000;
        proc.setZoneParams (sz->id, q);
    }
    proc.keyboardState.noteOn (1, 60, 0.8f);   // shows a held key on the editor keyboard
    {
        AudioBuffer<float> b (2, 512);
        MidiBuffer m;
        for (int i = 0; i < 30; ++i) { m.clear(); proc.processBlock (b, m); }   // let the note start (playhead)
    }
    const char* pages[] { "main", "editor", "fx" };
    for (int p = 0; p < 3; ++p)
    {
        editor.showPage (p);
        save (String ("preview-") + pages[p] + "-100");
    }
    editor.showPage (3);
    save ("preview-arp");
    editor.showPage (2);
    save ("preview-fx-master");
    {
        // Change envelope 1 the way its buttons do, without switching envelope tabs.
        auto e = proc.getFxSetup().envelopes[0];
        e.loop = false; e.dot = 1; e.den = 8; e.trigger = emx::fx::Envelope::free;
        proc.setEnvelope (0, e);
        save ("preview-fx-envchange");
    }
    editor.getFxPage().setChannel (proc.getZones()[0].id);
    editor.getFxPage().selectEnvelope (1);
    save ("preview-fx-zone");
    editor.getFxPage().setChannel (0);
    editor.getFxPage().selectEnvelope (0);
    proc.keyboardState.allNotesOff (0);
    editor.showPage (0);
    editor.getExplorer().showTab (0);
    save ("preview-main-samples");
    editor.getExplorer().showTab (1);
    // Open every folder and bank in the explorer, to show presets inside banks.
    if (auto* tv = [&]() -> TreeView* { for (auto* c : editor.getExplorer().getChildren()) if (auto* t = dynamic_cast<TreeView*> (c)) return t; return nullptr; }())
    {
        std::function<void (TreeViewItem*)> openAll = [&] (TreeViewItem* it)
        {
            it->setOpen (true);
            for (int i = 0; i < it->getNumSubItems(); ++i)
                openAll (it->getSubItem (i));
        };
        openAll (tv->getRootItem());
    }
    save ("preview-main-banks-open");
    const char* settingsPages[] { "folders", "performance", "midi", "interface", "about" };
    for (int p = 0; p < 5; ++p)
    {
        editor.showSettings (true, p);
        save (String ("preview-settings-") + settingsPages[p]);
    }
    editor.showSettings (false);
    for (auto sc : { 0.75f, 1.5f })
    {
        editor.setUiScale (sc);
        save ("preview-main-" + String (roundToInt (sc * 100.0f)));
    }
    editor.setUiScale (1.0f);
    demoDir.deleteRecursively();
    return 0;
}
