// Emulon X — renders the plugin window to PNG files and runs an audio self-test
// through the full processor, without a DAW.
// Developer tool: build with -DEMX_BUILD_PREVIEW=ON, then run
//     EmulonXPreview <output-folder>
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/SkinFile.h"

using namespace juce;
static int arrangerFailures = 0;

/** A grayscale "brushed metal" texture for the test skin (made here, so no picture file is needed). */
static MemoryBlock brushedTexturePng()
{
    Image im (Image::RGB, 256, 256, false);
    Random rnd (0x5eed);
    for (int y = 0; y < im.getHeight(); ++y)
    {
        const float row = rnd.nextFloat() * 2.0f - 1.0f;
        float streak = 0.0f;
        for (int x = 0; x < im.getWidth(); ++x)
        {
            streak = streak * 0.97f + (rnd.nextFloat() * 2.0f - 1.0f) * 0.03f;
            const float band = 0.12f * std::sin ((float) y / 256.0f * MathConstants<float>::twoPi * 1.5f);
            const auto v = (uint8) jlimit (0, 255, roundToInt (128.0f + 255.0f * (row * 0.08f + streak * 0.9f + band)));
            im.setPixelAt (x, y, Colour (v, v, v));
        }
    }
    MemoryOutputStream out;
    PNGImageFormat().writeImageToStream (im, out);
    return out.getMemoryBlock();
}

/** The test skin: Classic-like colours, brushed panels, squircle buttons, ring knobs, a light display. */
static const char* testSkinJson = R"JSON({
  "format": "emxd", "version": 2, "name": "Preview Test", "author": "Emulon X", "hueShift": true,
  "tokens": { "accent": "#ff7a1a", "text": "#f0ece6", "muted": "#b6aa9c", "surface": "#24201c", "background": "#140c06", "display": "#0a0603" },
  "font": "Saira", "upper": true,
  "window": { "fill": { "type": "gradient", "a": "#3a1d06", "b": "#050304", "angle": 160 }, "texture": "grid", "textureColor": "@accent", "textureOpacity": 0.1 },
  "frame": { "width": 3, "color": "#000000" },
  "topbar": { "fill": { "type": "gradient", "a": "#2a2018", "b": "#0d0906" }, "line": 2, "lineColor": "@accent", "logoColor": "#ffe2c4", "logoGlow": 0.7,
              "overlay": { "tex": "metal-test", "strength": 0.5, "blend": "overlay" } },
  "panel": { "fill": { "type": "gradient", "a": "#4a443e", "b": "#1d1915" }, "radius": 12, "shape": 1, "outline": 1, "outlineColor": "#000000",
             "shadowY": 3, "shadowBlur": 10, "shadowOpacity": 0.7, "overlay": { "tex": "metal-test", "strength": 0.8, "blend": "overlay" } },
  "heading": { "color": "@text" },
  "inset": { "fill": { "type": "solid", "a": "#15110d" }, "radius": 10, "shape": 1, "outline": 0, "outlineColor": "#000000", "innerShadow": 0.7 },
  "display": { "fill": { "type": "solid", "a": "@display" }, "radius": 4, "shape": 0, "outline": 1, "outlineColor": "#000000", "textColor": "@accent", "glow": 0.6, "scanlines": true },
  "list": { "color": "#ffa25a" },
  "button": { "fill": { "type": "gradient", "a": "#6a625a", "b": "#24201c" }, "radius": 9, "shape": 1, "outline": 1, "outlineColor": "#000000",
              "textColor": "#eeeeee", "shadowY": 0, "shadowBlur": 0, "shadowOpacity": 0, "bevel": 0.5 },
  "buttonOn": { "fill": { "type": "gradient", "a": "#ff8a2a", "b": "#7a2e04" }, "radius": 9, "shape": 1, "outline": 1, "outlineColor": "#000000", "textColor": "#ffffff", "glow": 0.5 },
  "tab": { "same": false, "fill": { "type": "solid", "a": "#2c2621" }, "onFill": { "type": "gradient", "a": "#ff8a2a", "b": "#7a2e04" },
           "textColor": "#f5efe8", "radius": 4, "shape": 0, "outline": 1, "outlineColor": "#000000" },
  "knob": { "style": "ring", "capA": "#8a8278", "capB": "#1a1612", "ring": 3, "ringTrack": "#3a332c", "ringColor": "@accent", "pointer": "#ffffff", "pointerGlow": 0.4,
            "overlay": { "tex": "metal-test", "strength": 0.6, "blend": "overlay" } },
  "fader": { "track": "#3a332c", "fill": "@accent", "thumbA": "#d8d0c8", "thumbB": "#6a625a" },
  "label": { "color": "@text" }
})JSON";

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

    // EMX_EXB_DIR: load an Emulator X preset through the plugin (like a double-click in the explorer) and play it
    if (const auto exbDir = SystemStats::getEnvironmentVariable ("EMX_EXB_DIR", {}); exbDir.isNotEmpty())
    {
        proc.prepareToPlay (48000.0, 512);
        for (auto [bank, entry] : { std::pair { "Orbit-3.exb", "332" }, std::pair { "Vintage Keys.exb", "0" } })
        {
            proc.loadPresetFromBankFile (File (exbDir).getChildFile (bank), entry, "test");
            for (int i = 0; i < 200 && proc.isBusy(); ++i)
                MessageManager::getInstance()->runDispatchLoopUntil (20);
            MessageManager::getInstance()->runDispatchLoopUntil (100);
            AudioBuffer<float> b (2, 512);
            MidiBuffer m;
            double sum = 0;
            for (int i = 0; i < 100; ++i)
            {
                m.clear();
                if (i == 0) m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
                b.clear();
                proc.processBlock (b, m);
                sum += b.getRMSLevel (0, 0, 512);
            }
            std::printf ("exb %s: preset '%s', %d zones, rms %.5f, voices %d, message '%s'\n", bank, proc.getPresetName().toRawUTF8(),
                         (int) proc.getZones().size(), sum / 100.0, proc.getActiveVoices(), proc.getLastMessage().toRawUTF8());
            for (auto& z : proc.getZones())
                std::printf ("   zone %s keys %d-%d vel %d-%d len %d gain %.1f\n", z.name.toRawUTF8(), z.params.loKey, z.params.hiKey, z.params.loVel, z.params.hiVel,
                             z.sample ? z.sample->length() : -1, z.params.gainDb);
        }
        return 0;
    }

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

    // Typing values into value displays, and copying an envelope between zones.
    {
        using emx::ValueControl;
        bool allOk = true;
        String report;
        auto check = [&] (ValueControl& c, const char* typed, double expect, double tol)
        {
            double v = -999.0;
            const bool parsed = emx::valueFromTypedText (c.slider, c.format, typed, v);
            const bool ok = parsed && std::abs (v - expect) <= tol;
            allOk = allOk && ok;
            if (! ok) report << " [" << typed << " -> " << String (v, 4) << ", wanted " << String (expect, 4) << "]";
        };
        auto timeText = [] (double t) { return t < 1.0 ? String (roundToInt (t * 1000.0)) + " MS" : String (t, 2) + " S"; };
        NormalisableRange<double> timeRange { 0.001, 10.0,
            [] (double, double, double n) { return 0.001 * std::pow (1.0e4, n); },
            [] (double, double, double v) { return std::log (jmax (0.001, v) / 0.001) / std::log (1.0e4); } };
        ValueControl attack ("Attack", ValueControl::Style::fader, 84);
        attack.setRange (timeRange, 0.25); attack.format = timeText; attack.setValue (0.25);
        check (attack, "300ms", 0.3, 0.002);
        check (attack, "1.5 s", 1.5, 0.01);
        check (attack, "300", 0.3, 0.002);          // bare number: shown in MS, so 300 ms
        check (attack, "2,5s", 2.5, 0.02);           // comma as decimal point
        ValueControl sustain ("Sustain", ValueControl::Style::fader, 84);
        sustain.setRange (NormalisableRange<double> (0.0, 1.0, 0.001), 1.0);
        sustain.format = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
        check (sustain, "50%", 0.5, 0.006);
        check (sustain, "75", 0.75, 0.006);
        emx::setMiddleCOctave (4);   // the checks below use C4 = middle C
        ValueControl root ("Root", ValueControl::Style::knob);
        root.setRange (NormalisableRange<double> (0, 127, 1), 60);
        root.format = [] (double v) { return emx::noteName (roundToInt (v)); };
        check (root, "C4", 60, 0.0);
        check (root, "F#3", 54, 0.0);
        check (root, "a#2", 46, 0.0);
        emx::setMiddleCOctave (3);   // Ableton's names (the default): C3 = MIDI 60
        check (root, "C3", 60, 0.0);
        ValueControl pan ("Pan", ValueControl::Style::knob);
        pan.setRange (NormalisableRange<double> (-100, 100, 1), 0);
        pan.format = [] (double v) { const int c = roundToInt (v); return c == 0 ? String ("C") : (c < 0 ? "L" + String (-c) : "R" + String (c)); };
        check (pan, "L20", -20, 0.0);
        check (pan, "R35", 35, 0.0);
        check (pan, "C", 0, 0.0);
        ValueControl gain ("Gain", ValueControl::Style::knob);
        gain.setRange (NormalisableRange<double> (-24.0, 12.0, 0.5), 0.0);
        gain.format = [] (double v) { return (v > 0 ? "+" : "") + String (v, 1) + " DB"; };
        check (gain, "-6 dB", -6.0, 0.0);
        check (gain, "+3.5", 3.5, 0.0);
        ValueControl cutoff ("Cutoff", ValueControl::Style::knob);   // like the FX filter: 0..1 slider, Hz display
        cutoff.setRange (NormalisableRange<double> (0.0, 1.0), 0.5);
        cutoff.format = [] (double n) { const double hz = 20.0 * std::pow (1000.0, n); return hz < 1000.0 ? String (roundToInt (hz)) + " HZ" : String (hz / 1000.0, 2) + " K"; };
        cutoff.setValue (0.8);
        check (cutoff, "2.5k", std::log (2500.0 / 20.0) / std::log (1000.0), 0.002);
        check (cutoff, "440 Hz", std::log (440.0 / 20.0) / std::log (1000.0), 0.002);
        double dummy;
        const bool rejects = ! emx::valueFromTypedText (attack.slider, attack.format, "hello", dummy);
        allOk = allOk && rejects;

        // envelope copy
        bool copyOk = false, allZonesOk = false, undoOk = false;
        if (proc.getZones().size() >= 2)
        {
            const auto& zs = proc.getZones();
            const int a = zs[0].id, b = zs[1].id;
            auto q = zs[0].params;
            q.attack = 0.123f; q.decay = 0.456f; q.sustain = 0.5f; q.release = 1.25f;
            proc.setZoneParams (a, q);
            const auto before = proc.getZones()[1].params;
            proc.copyEnvelope (a, b);
            const auto& c = proc.getZones()[1].params;
            copyOk = std::abs (c.attack - 0.123f) < 1e-6f && std::abs (c.decay - 0.456f) < 1e-6f && std::abs (c.sustain - 0.5f) < 1e-6f
                  && std::abs (c.release - 1.25f) < 1e-6f && c.loopStart == before.loopStart && c.root == before.root;
            proc.undo();
            undoOk = std::abs (proc.getZones()[1].params.attack - before.attack) < 1e-6f;
            proc.copyEnvelope (a, -1);
            allZonesOk = true;
            for (auto& z : proc.getZones())
                allZonesOk = allZonesOk && std::abs (z.params.release - 1.25f) < 1e-6f;
            proc.undo();
            proc.undo();
        }
        std::printf ("typing values: %s%s; envelope copy %s, to all zones %s, undo %s\n", allOk ? "ok" : "FAIL", report.toRawUTF8(),
                     copyOk ? "ok" : "FAIL", allZonesOk ? "ok" : "FAIL", undoOk ? "ok" : "FAIL");
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
    {
        // V2 editor: scrolled to the keymap, the WARP tab, then the splicer
        auto& ed = editor.getEditorPage();
        editor.showPage (1);
        ed.scrollTo (400);
        save ("preview-editor-scrolled");
        // V3: the EMU engine panels on a zone (filter, 6-stage envelopes, LFOs, cords)
        if (auto* sz = proc.getSelectedZone())
        {
            auto q = sz->params;
            auto d = emx::emu::makeDesign (q.attack, q.decay, q.sustain, q.release);
            d->filterType = 64; d->cutoff = 0.45f; d->resonance = 0.6f; d->chorusAmount = 12.0f;
            d->env[0].rate = { 4.0f, 0.0f, 30.0f, 43.0f, 23.5f, 0.0f };
            d->env[0].level = { 100.0f, 100.0f, 80.0f, 60.0f, 0.0f, 0.0f };
            d->cords = { { 17, 56, -33.0f }, { 96, 47, 15.0f }, { 20, 56, 32.0f }, { 80, 56, 40.0f }, { 105, 65, 87.0f }, { 10, 73, 19.0f } };
            d->lfo[1].sync = true; d->lfo[1].division = 25; d->lfo[1].keySync = true;
            q.design = d;
            proc.setZoneParams (sz->id, q);
            ed.scrollTo (520);
            save ("preview-editor-emu");
            ed.scrollTo (900);
            save ("preview-editor-emu2");
        }
        ed.scrollTo (0);
        ed.showTab (emx::EditorPage::Tab::warp);
        save ("preview-editor-warp");
        if (auto* sz = proc.getSelectedZone())
        {
            emx::edit::Warp w;
            w.shape = emx::edit::WarpShape::fold;
            proc.warpZone (sz->id, w);
        }
        save ("preview-editor-warped");
        proc.undo();
        ed.showTab (emx::EditorPage::Tab::loop);
        proc.setSplicer (true);
        save ("preview-editor-splicer");
        ed.setPanelShown (0, false);
        save ("preview-editor-splicer-folded");
        ed.setPanelShown (0, true);
        proc.setSplicer (false);
        // V3: keymap in OVERLAP and STACK mode (sample and voice folded so it is in view)
        ed.setPanelShown (0, false);
        ed.setPanelShown (1, false);
        ed.scrollTo (100000);
        save ("preview-keymap-overlap");
        emx::KeymapView::stacked = true;
        ed.setPanelShown (2, true);
        ed.scrollTo (100000);
        save ("preview-keymap-stack");
        emx::KeymapView::stacked = false;
        ed.setPanelShown (0, true);
        ed.setPanelShown (1, true);
        ed.scrollTo (0);
        editor.showPage (0);
        // V3: the Save Preset window
        {
            emx::PresetInfo info;
            info.type = "Pad"; info.author = "Kiru"; info.tags = "glass, slow, wide";
            info.description = "Mod wheel opens the filter. Macro 2 adds space.";
            proc.setPresetInfo (info);
            editor.showSaveDialog();
            save ("preview-save-dialog");
            editor.closeSaveDialog();
            save ("preview-main-type");
            proc.setPresetInfo ({});
        }
    }
    editor.showPage (3);
    save ("preview-arp");
    // V3: Machine (4 x 4, then 8 x 8)
    editor.showPage (4);
    save ("preview-machine");
    {
        auto m = proc.getMachineSettings();
        m.grid = 8;
        proc.setMachineSettings (m);
        save ("preview-machine-8x8");
        m.grid = 4;
        proc.setMachineSettings (m);
    }
    // V3: Processor with the new effects, and an AUX bus
    proc.addFx (0, emx::fx::Type::spectral);
    proc.addFx (emx::EmulonXProcessor::auxChainId (0), emx::fx::Type::reverb);
    proc.setAuxSend (0, 0.25f);
    editor.showPage (2);
    save ("preview-processor-spectral");
    editor.getFxPage().setChannel (emx::EmulonXProcessor::auxChainId (0));
    save ("preview-processor-aux");
    editor.getFxPage().setChannel (0);
    {
        // V3: Arranger with a few parts, then part 3 being edited
        proc.setArrangerOn (true);
        for (int k : { 1, 2, 4, 9 })
            proc.copyPartTo (0, k);
        auto pi = proc.getPartInfo (1); pi.keyHi = 59; proc.setPartInfo (1, pi);
        pi = proc.getPartInfo (2); pi.keyLo = 60; pi.output = 2; proc.setPartInfo (2, pi);
        pi = proc.getPartInfo (4); pi.mute = true; proc.setPartInfo (4, pi);
        proc.getPart (9)->setPartVolume (-8.5f);
        proc.getPart (9)->setPartPan (-0.4f);
        for (int i = 0; i < 60; ++i)
            MessageManager::getInstance()->runDispatchLoopUntil (30);   // the parts load their samples in the background

        // Audio checks: each DAW channel plays its own part, key splits, mute, outputs, saving
        float pk[16] {};
        float o = 0.0f;
        auto report = [&] (bool ok, const String& what)
        {
            std::printf ("%s  arranger: %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
            if (! ok)
            {
                arrangerFailures++;
                std::printf ("      out %.6f  parts %.4f %.4f %.4f %.4f %.4f\n", o, pk[0], pk[1], pk[2], pk[3], pk[4]);
            }
        };
        proc.prepareToPlay (48000.0, 512);
        auto play = [&] (int ch, int note) -> float
        {
            for (int k = 0; k < emx::EmulonXProcessor::numParts; ++k)
                proc.takePartPeak (k);
            AudioBuffer<float> buf (2, 512);
            float peakOut = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                MidiBuffer m;
                if (b == 0) m.addEvent (MidiMessage::noteOn (ch, note, (uint8) 110), 0);
                if (b == 30) m.addEvent (MidiMessage::noteOff (ch, note), 0);
                proc.processBlock (buf, m);
                peakOut = jmax (peakOut, buf.getMagnitude (0, 512));
            }
            for (int b = 0; b < 1200; ++b) { MidiBuffer m; proc.processBlock (buf, m); }   // let it (and its reverb) ring out
            return peakOut;
        };
        auto peaks = [&] { for (int k = 0; k < 16; ++k) pk[k] = proc.takePartPeak (k); };
        o = play (2, 48); peaks();
        report (o > 0.01f && pk[1] > 0.01f && pk[0] < 1.0e-6f && pk[2] < 1.0e-6f, "channel 2 plays part 2 only (" + String (o, 3) + ")");
        o = play (3, 48); peaks();
        report (o < 1.0e-6f, "part 3's split (C3 and up) ignores a low note");
        o = play (3, 72); peaks();
        report (o > 0.01f && pk[2] > 0.01f, "part 3 plays a high note (its OUT 3 is off in the host, so it uses MAIN)");
        o = play (5, 60); peaks();
        report (o < 1.0e-6f && pk[4] > 0.01f, "a muted part plays but is not heard");
        o = play (1, 60); peaks();
        report (o > 0.01f && pk[0] > 0.01f && pk[1] < 1.0e-6f, "channel 1 plays part 1 only");
        o = play (11, 60);
        report (o < 1.0e-6f, "a channel without a part is silent");
        {
            MemoryBlock st;
            proc.getStateInformation (st);
            emx::EmulonXProcessor copy;
            copy.setStateInformation (st.getData(), (int) st.getSize());
            for (int i = 0; i < 40; ++i)
                MessageManager::getInstance()->runDispatchLoopUntil (30);
            report (copy.isArrangerOn() && copy.isPartUsed (1) && copy.isPartUsed (2) && copy.isPartUsed (4) && copy.isPartUsed (9) && ! copy.isPartUsed (3),
                    "the project keeps which parts are used");
            report (copy.getPartInfo (2).keyLo == 60 && copy.getPartInfo (2).output == 2 && copy.getPartInfo (4).mute
                    && std::abs (copy.getPart (9)->getPartVolume() + 8.5f) < 0.01f, "the project keeps each part's settings");
            report (copy.getPart (2) != nullptr && copy.getPart (2)->getZones().size() == proc.getZones().size(), "a part's preset comes back with the project");
        }
        editor.showPage (5);
        save ("preview-arranger");
        proc.setEditedPart (2);
        save ("preview-arranger-part3");
        editor.showPage (0);
        save ("preview-main-part3");
        proc.setEditedPart (0);
        for (int k = 1; k < emx::EmulonXProcessor::numParts; ++k)
            proc.clearPart (k);
        proc.setArrangerOn (false);
        editor.showPage (5);
        save ("preview-arranger-single");
    }
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
    {
        // A fuller bank, shown as a grid with different column settings
        SharedResourcePointer<emx::Settings> st;
        for (int i = 0; i < 9; ++i)
            proc.addCurrentPresetToBank();
        for (int i = 0; i < 40 && proc.getBank().size() < 11; ++i)
            MessageManager::getInstance()->runDispatchLoopUntil (100);   // adding runs in the background
        for (int cols : { 3, 1, 5 })
        {
            st->setBankColumns (cols);
            save ("preview-main-bank-grid-" + String (cols));
        }
        st->setBankColumns (3);
        std::printf ("bank entries now: %d\n", (int) proc.getBank().size());
    }
    const char* settingsPages[] { "folders", "performance", "midi", "interface", "customization", "about" };
    for (int p = 0; p < 6; ++p)
    {
        editor.showSettings (true, p);
        save (String ("preview-settings-") + settingsPages[p]);
    }
    {
        // Hue: the whole interface in other colours (and how long recolouring takes)
        SharedResourcePointer<emx::Settings> st;
        editor.showSettings (true, 4);
        st->setHueShift (-122.0f);
        save ("preview-settings-customization-blue");
        editor.showSettings (false);
        const char* pageNames[] { "main", "editor", "fx", "arp" };
        for (auto [deg, name] : { std::pair<float, const char*> { -122.0f, "blue" }, { 117.0f, "green" }, { -89.0f, "purple" } })
        {
            st->setHueShift (deg);
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            const auto t0 = Time::getMillisecondCounterHiRes();
            (void) editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
            std::printf ("hue %s: first repaint incl. recolouring %.0f ms\n", name, Time::getMillisecondCounterHiRes() - t0);
            for (int pg = 0; pg < 4; ++pg)
            {
                editor.showPage (pg);
                save (String ("preview-hue-") + name + "-" + pageNames[pg]);
            }
        }
        st->setHueShift (0.0f);
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        editor.showPage (0);

        // Skins: EMULON XP light and dark on every page
        for (auto [skin, sname] : { std::pair<int, const char*> { 1, "xp-light" }, { 2, "xp-dark" }, { 3, "emurum" }, { 4, "emutal" }, { 5, "emublox" }, { 6, "emul8r" } })
        {
            st->setSkin (skin);
            MessageManager::getInstance()->runDispatchLoopUntil (80);
            for (int pg = 0; pg < 4; ++pg)
            {
                editor.showPage (pg);
                save (String ("preview-") + sname + "-" + pageNames[pg]);
            }
            editor.showPage (0);
            editor.showSettings (true, 4);
            save (String ("preview-") + sname + "-settings");
            editor.showSettings (false);
        }
        st->setHueShift (-60.0f);   // the colour shift works on EMUTAL too
        MessageManager::getInstance()->runDispatchLoopUntil (80);
        save ("preview-emutal-shifted");
        st->setHueShift (0.0f);
        st->setSkin (0);
        MessageManager::getInstance()->runDispatchLoopUntil (80);
    }
    {
        // Custom skins (.emxd): the test skin, plus any skin files in $EMX_PREVIEW_SKINS
        SharedResourcePointer<emx::Settings> st;
        const auto skinDir = demoDir.getChildFile ("skins");
        skinDir.createDirectory();
        const auto testSkin = skinDir.getChildFile ("Preview Test.emxd");
        String err;
        const bool saved = emx::skin::save (testSkin, JSON::parse (testSkinJson), { { "textures/metal-test.png", brushedTexturePng() } }, err);
        const auto skinFoldersBefore = st->getFolders (emx::Settings::Folder::skins);
        st->addFolder (emx::Settings::Folder::skins, skinDir);   // so the skin shows in the list
        Array<File> skinFiles { testSkin };
        if (const auto extra = SystemStats::getEnvironmentVariable ("EMX_PREVIEW_SKINS", {}); extra.isNotEmpty())
            skinFiles.addArray (emx::skin::findSkins (StringArray (extra)));
        const char* pageNames[] { "main", "editor", "fx", "arp" };
        for (auto& f : skinFiles)
        {
            st->useCustomSkin (f);
            MessageManager::getInstance()->runDispatchLoopUntil (80);
            const bool ok = emx::theme::getSkin() == emx::theme::Skin::custom && emx::theme::customSkin() != nullptr;
            std::printf ("skin file %s: %s%s%s\n", f.getFileName().toRawUTF8(), ok && (saved || f != testSkin) ? "ok" : "FAIL",
                         emx::theme::customSkinMessage().isNotEmpty() ? " - " : "", emx::theme::customSkinMessage().toRawUTF8());
            const auto tag = File::createLegalFileName (f.getFileNameWithoutExtension().toLowerCase().replaceCharacter (' ', '-'));
            for (int pg = 0; pg < 4; ++pg)
            {
                editor.showPage (pg);
                save ("preview-skinfile-" + tag + "-" + pageNames[pg]);
            }
            editor.showPage (0);
            editor.showSettings (true, 4);
            save ("preview-skinfile-" + tag + "-settings");
            editor.showSettings (false);
        }
        // the colour shift turns a skin file that allows it
        st->useCustomSkin (testSkin);
        st->setHueShift (-150.0f);
        MessageManager::getInstance()->runDispatchLoopUntil (80);
        save ("preview-skinfile-shifted");
        st->setHueShift (0.0f);
        // a broken file falls back to Classic with a note
        const auto broken = skinDir.getChildFile ("Broken.emxd");
        broken.replaceWithText ("not a skin");
        st->useCustomSkin (broken);
        MessageManager::getInstance()->runDispatchLoopUntil (80);
        std::printf ("broken skin file: %s (%s)\n", emx::theme::getSkin() == emx::theme::Skin::classic ? "falls back to Classic ok" : "FAIL",
                     emx::theme::customSkinMessage().toRawUTF8());
        editor.showSettings (true, 4);
        save ("preview-skinfile-broken-settings");
        editor.showSettings (false);
        editor.showSettings (true, 0);
        save ("preview-settings-folders");
        editor.showSettings (false);
        st->setFolders (emx::Settings::Folder::skins, skinFoldersBefore);
        st->setSkin (0);
        MessageManager::getInstance()->runDispatchLoopUntil (80);
    }
    for (auto sc : { 0.75f, 1.5f })
    {
        editor.setUiScale (sc);
        save ("preview-main-" + String (roundToInt (sc * 100.0f)));
    }
    editor.setUiScale (1.0f);
    demoDir.deleteRecursively();
    return arrangerFailures > 0 ? 1 : 0;
}
