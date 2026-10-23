// Emulon X — engine tests. Build with -DEMX_BUILD_TESTS=ON and run EmulonXTests.
// Exits with code 1 if anything fails (so the GitHub build turns red).
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/SamplerEngine.h"
#include "engine/SampleLoader.h"
#include "fx/FxEngine.h"

using namespace emx;
using namespace juce;

static int failures = 0, passes = 0;
static void check (bool ok, const String& what, const String& detail = {})
{
    std::printf ("%s  %s%s\n", ok ? "PASS" : "FAIL", what.toRawUTF8(), detail.isNotEmpty() ? (" (" + detail + ")").toRawUTF8() : "");
    (ok ? passes : failures)++;
}
static bool near (double v, double target, double tol) { return std::abs (v - target) <= tol; }

static std::shared_ptr<SampleData> sine (double sr, double seconds, double freq, float amp = 0.8f)
{
    auto s = std::make_shared<SampleData>();
    s->name = "sine";
    s->sampleRate = sr;
    s->audio.setSize (1, (int) (sr * seconds));
    for (int i = 0; i < s->length(); ++i)
        s->audio.setSample (0, i, amp * (float) std::sin (MathConstants<double>::twoPi * freq * i / sr));
    return s;
}

struct Render
{
    std::vector<float> left;
    double aliveSeconds = 0;
    bool finite = true;
};

/** Plays `note` through a one-zone engine. Optional: release after `releaseAt` seconds. */
static Render play (std::shared_ptr<SampleData> s, ZoneParams p, int note, double seconds,
                    Interp interp = Interp::sinc, double releaseAt = -1, double outRate = 48000.0,
                    std::function<void (SamplerEngine&, int block)> perBlock = {})
{
    auto set = std::make_shared<ZoneSet>();
    set->interp = interp;
    set->zones.push_back ({ 1, s, p });
    SamplerEngine e;
    e.prepare (outRate);
    e.setZoneSet (set);
    e.noteOn (note, 127);

    Render r;
    const int N = 128, blocks = (int) (seconds * outRate / N);
    float L[N], R[N];
    for (int b = 0; b < blocks; ++b)
    {
        if (releaseAt >= 0 && b == (int) (releaseAt * outRate / N))
            e.noteOff (note);
        if (perBlock)
            perBlock (e, b);
        std::fill (L, L + N, 0.0f);
        std::fill (R, R + N, 0.0f);
        e.render (L, R, N);
        for (int i = 0; i < N; ++i)
        {
            r.finite = r.finite && std::isfinite (L[i]) && std::isfinite (R[i]);
            r.left.push_back (L[i]);
        }
        if (e.getActiveVoiceCount() > 0)
            r.aliveSeconds = (b + 1) * N / outRate;
    }
    return r;
}

static double frequency (const std::vector<float>& x, double sr, double from, double to)
{
    int crossings = 0;
    for (auto i = (size_t) (from * sr) + 1; i < (size_t) (to * sr) && i < x.size(); ++i)
        if (x[i - 1] < 0 && x[i] >= 0)
            ++crossings;
    return crossings / (to - from);
}

/** Strongest frequency between fmin and fmax (Goertzel scan, 0.5 Hz steps). Robust for granular audio. */
static double peakFrequency (const std::vector<float>& x, double sr, double from, double to, double fmin, double fmax)
{
    const auto a = (size_t) (from * sr), b = jmin ((size_t) (to * sr), x.size());
    double best = 0, bestPower = -1;
    for (double f = fmin; f <= fmax; f += 0.5)
    {
        const double w = MathConstants<double>::twoPi * f / sr, c = 2 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (auto i = a; i < b; ++i)
        {
            const double s0 = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        if (power > bestPower) { bestPower = power; best = f; }
    }
    return best;
}

static double rms (const std::vector<float>& x, double sr, double from, double to)
{
    double s = 0;
    size_t c = 0;
    for (auto i = (size_t) (from * sr); i < (size_t) (to * sr) && i < x.size(); ++i, ++c)
        s += x[i] * x[i];
    return c ? std::sqrt (s / c) : 0.0;
}

static ZoneParams basic (const SampleData& s, int root)
{
    auto p = ZoneParams::defaultsFor (s);
    p.root = root;
    p.keepSpeed = false;
    p.attack = 0.001f;
    p.release = 0.05f;
    return p;
}

/** EmulonXTests --bench : CPU use of 32 voices for each interpolation / keep-speed combination. */
static int bench()
{
    const double SR = 48000.0;
    auto s = sine (SR, 4.0, 220.0, 0.3f);
    for (int ks = 0; ks < 2; ++ks)
        for (int m = 0; m < 3; ++m)
        {
            auto p = basic (*s, 57);
            p.keepSpeed = ks == 1;
            p.loopMode = LoopMode::loop;
            p.loopStart = 20000;
            p.loopEnd = 180000;
            p.crossfade = 4000;
            auto set = std::make_shared<ZoneSet>();
            set->interp = (Interp) m;
            set->zones.push_back ({ 1, s, p });
            SamplerEngine e;
            e.prepare (SR);
            e.setZoneSet (set);
            for (int i = 0; i < 32; ++i)
                e.noteOn (40 + i, 100);
            float L[256], R[256];
            const auto t0 = Time::getMillisecondCounterHiRes();
            for (int b = 0; b < (int) (2.0 * SR / 256); ++b)
            {
                std::fill (L, L + 256, 0.f);
                std::fill (R, R + 256, 0.f);
                e.render (L, R, 256);
            }
            std::printf ("keepSpeed=%d %-6s : %5.1f%% of one core for 32 voices\n", ks, m == 0 ? "linear" : m == 1 ? "cubic" : "sinc",
                         (Time::getMillisecondCounterHiRes() - t0) / 20.0);
        }
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 1 && String (argv[1]) == "--bench")
        return bench();
    const double SR = 48000.0;
    auto a440 = sine (SR, 1.0, 440.0);   // 1 second of A4

    //== Pitch and length ===========================================================
    for (auto [mode, label] : { std::pair { Interp::linear, "linear" }, { Interp::cubic, "cubic" }, { Interp::sinc, "sinc" } })
    {
        auto p = basic (*a440, 69);
        auto r = play (a440, p, 81, 1.6, mode);
        const auto f = frequency (r.left, SR, 0.1, 0.4);
        check (near (f, 880, 6) && near (r.aliveSeconds, 0.5, 0.06) && r.finite,
               String ("classic +12 semitones, ") + label + ": octave up, half length",
               String (f, 0) + " Hz, " + String (r.aliveSeconds, 2) + " s");

        p.keepSpeed = true;
        r = play (a440, p, 81, 1.6, mode);
        const auto f2 = peakFrequency (r.left, SR, 0.1, 0.6, 700, 1050);
        check (near (f2, 880, 2) && near (r.aliveSeconds, 1.0, 0.08) && r.finite,
               String ("keep speed +12, ") + label + ": octave up, same length",
               String (f2, 0) + " Hz, " + String (r.aliveSeconds, 2) + " s");
    }

    //== Loops ======================================================================
    {
        auto p = basic (*a440, 69);
        p.loopMode = LoopMode::loop;
        p.loopStart = 12000;
        p.loopEnd = 48000;
        p.crossfade = 2000;
        auto r = play (a440, p, 69, 3.2);
        check (r.aliveSeconds > 3.1 && near (frequency (r.left, SR, 2.0, 3.0), 440, 4) && r.finite,
               "loop with crossfade keeps playing while held", String (r.aliveSeconds, 2) + " s");

        p.loopMode = LoopMode::untilRelease;
        r = play (a440, p, 69, 2.0, Interp::sinc, 1.0);
        check (r.aliveSeconds > 1.0 && r.aliveSeconds < 1.25, "loop until release plays out after key-up", String (r.aliveSeconds, 2) + " s");

        // A loop whose length isn't a whole number of cycles clicks without a crossfade.
        auto worstJump = [&] (int xf)
        {
            auto q = basic (*a440, 69);
            q.loopMode = LoopMode::loop;
            q.loopStart = 10000;
            q.loopEnd = 10000 + 5555;   // 50.9 cycles -> phase jump at the seam
            q.crossfade = xf;
            auto rr = play (a440, q, 69, 1.5);
            float worst = 0;
            for (size_t i = (size_t) (0.5 * SR); i < rr.left.size(); ++i)
                worst = jmax (worst, std::abs (rr.left[i] - rr.left[i - 1]));
            return worst;
        };
        const auto hard = worstJump (0), smooth = worstJump (2000);
        check (smooth < hard * 0.7f, "crossfade smooths the loop seam",
               "max step " + String (hard, 3) + " -> " + String (smooth, 3));

        auto q = basic (*a440, 69);
        q.keepSpeed = true;
        q.loopMode = LoopMode::loop;
        q.loopStart = 12000;
        q.loopEnd = 48000;
        q.crossfade = 1000;
        r = play (a440, q, 57, 2.4, Interp::sinc, 1.5);
        const auto f3 = peakFrequency (r.left, SR, 0.3, 1.2, 150, 300);
        check (near (f3, 220, 2) && r.aliveSeconds > 1.5, "keep speed -12 with loop", String (f3, 1) + " Hz (spectral peak)");
    }

    //== Anti-aliasing: a 15 kHz tone played an octave up should be removed by sinc ==
    {
        auto high = sine (44100.0, 0.5, 15000.0);
        auto p = basic (*high, 60);
        p.keepSpeed = false;
        const auto lin = rms (play (high, p, 72, 0.3, Interp::linear, -1, 44100.0).left, 44100.0, 0.02, 0.2);
        const auto snc = rms (play (high, p, 72, 0.3, Interp::sinc, -1, 44100.0).left, 44100.0, 0.02, 0.2);
        check (snc < lin * 0.1, "sinc HQ suppresses aliasing when pitching up",
               "alias level linear " + String (lin, 4) + " vs sinc " + String (snc, 4));
    }

    //== Sample rate conversion, bend, tuning ======================================
    {
        auto low = sine (22050.0, 1.0, 440.0);
        auto r = play (low, basic (*low, 69), 69, 0.5);
        check (near (frequency (r.left, SR, 0.1, 0.4), 440, 4), "22.05 kHz sample plays at the right pitch at 48 kHz");

        r = play (a440, basic (*a440, 69), 69, 0.5, Interp::sinc, -1, SR, [] (SamplerEngine& e, int b) { if (b == 0) e.setPitchBend (1.0f); });
        check (near (frequency (r.left, SR, 0.1, 0.4), 493.9, 5), "pitch bend +2 semitones", String (frequency (r.left, SR, 0.1, 0.4), 0) + " Hz");

        r = play (a440, basic (*a440, 69), 69, 0.5, Interp::sinc, -1, SR,
                  [] (SamplerEngine& e, int b) { if (b == 0) e.setGlobalTune ((float) (1200.0 * std::log2 (432.0 / 440.0))); });
        check (near (frequency (r.left, SR, 0.1, 0.4), 432, 4), "master tune A4 = 432 Hz", String (frequency (r.left, SR, 0.1, 0.4), 0) + " Hz");
    }

    //== Sustain pedal, voice limit, velocity ranges ===============================
    {
        auto set = std::make_shared<ZoneSet>();
        auto p = basic (*a440, 69);
        p.loopMode = LoopMode::loop;
        p.loopStart = 1000;
        p.loopEnd = 40000;
        p.release = 0.2f;
        set->zones.push_back ({ 1, a440, p });
        SamplerEngine e;
        e.prepare (SR);
        e.setZoneSet (set);
        float L[256], R[256];
        auto run = [&] (double secs) { for (int b = 0; b < (int) (secs * SR / 256); ++b) { std::fill (L, L + 256, 0.f); std::fill (R, R + 256, 0.f); e.render (L, R, 256); } };

        e.setSustain (true);
        e.noteOn (69, 100);
        run (0.1);
        e.noteOff (69);
        run (1.0);
        const int held = e.getActiveVoiceCount();
        e.setSustain (false);
        run (1.0);
        check (held == 1 && e.getActiveVoiceCount() == 0, "sustain pedal holds notes until released");

        e.setMaxVoices (4);
        for (int k = 0; k < 10; ++k)
            e.noteOn (50 + k, 100);
        run (0.05);
        check (e.getActiveVoiceCount() == 4, "voice limit 4 with 10 notes", String (e.getActiveVoiceCount()) + " sounding");

        auto set2 = std::make_shared<ZoneSet>();
        auto soft = p;
        soft.loVel = 1;
        soft.hiVel = 64;
        set2->zones.push_back ({ 2, a440, soft });
        SamplerEngine e2;
        e2.prepare (SR);
        e2.setZoneSet (set2);
        e2.noteOn (69, 100);
        const int loud = e2.getActiveVoiceCount();
        e2.noteOn (70, 40);
        check (loud == 0 && e2.getActiveVoiceCount() == 1, "velocity ranges choose the right layer");
    }

    //== Root notes from file names ================================================
    {
        const std::pair<const char*, int> cases[] { { "Piano_C4", 60 }, { "Pad A#3", 58 }, { "Bass-Eb1", 27 }, { "Kick", -1 },
                                                    { "Strings C-1", 0 }, { "Bell_C5_v2", 72 }, { "Take 12", -1 } };
        bool ok = true;
        String bad;
        for (auto& [name, want] : cases)
            if (noteFromName (name) != want) { ok = false; bad << name << "=" << noteFromName (name) << " "; }
        check (ok, "note names in file names", bad);
    }

    //== WAV loop markers + root note (smpl chunk) =================================
    const auto tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("emulonx-tests");
    tmp.deleteRecursively();
    tmp.createDirectory();
    auto writeWav = [] (const File& f, const SampleData& s, std::unordered_map<String, String> meta)
    {
        f.deleteFile();
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> out (f.createOutputStream().release());
        auto writer = wav.createWriterFor (out, AudioFormatWriterOptions{}.withSampleRate (s.sampleRate)
                                                    .withNumChannels (1).withBitsPerSample (24).withMetadataValues (meta));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (s.audio, 0, s.length());
    };
    {
        std::unordered_map<String, String> meta { { "MidiUnityNote", "57" }, { "NumSampleLoops", "1" }, { "Loop0Type", "0" },
                                                  { "Loop0Start", "4800" }, { "Loop0End", "43199" } };
        const auto f = tmp.getChildFile ("Looped.wav");
        writeWav (f, *a440, meta);
        auto r = loader::loadAudioFile (f);
        const bool ok = r.zones.size() == 1 && r.zones[0].params.root == 57 && r.zones[0].params.loopMode == LoopMode::loop
                        && r.zones[0].params.loopStart == 4800 && r.zones[0].params.loopEnd == 43200
                        && near (r.zones[0].sample->sampleRate, SR, 0.5);
        check (ok, "WAV root note and loop markers are read",
               r.zones.empty() ? r.errors.joinIntoString (";")
                               : "root " + String (r.zones[0].params.root) + ", loop " + String (r.zones[0].params.loopStart) + "-" + String (r.zones[0].params.loopEnd));
    }

    //== .emxp preset from the prototype ===========================================
    {
        const auto wavFile = tmp.getChildFile ("01_Pad_A3.wav");
        writeWav (wavFile, *a440, {});
        const String json = R"({"format":"emulon-x-preset","version":4,"name":"Warm Pad","interpolation":"cubic",
            "samples":[{"file":"samples/01_Pad_A3.wav","name":"Pad_A3"}],
            "zones":[{"sample":"samples/01_Pad_A3.wav","rootKey":57,"loKey":40,"hiKey":70,"loVel":1,"hiVel":127,
                      "loopMode":"sustain","loopStart":1000,"loopEnd":30000,"loopCrossfade":500,
                      "envelope":{"attack":0.05,"decay":0.4,"sustain":0.7,"release":1.2},
                      "gainDb":-3,"pan":20,"tuneCents":5,"keepSpeed":false,"grainMs":80},
                     {"sample":"samples/01_Pad_A3.wav","rootKey":69,"loKey":71,"hiKey":127}]})";
        const auto preset = tmp.getChildFile ("Warm Pad.emxp");
        preset.deleteFile();
        {
            ZipFile::Builder zb;
            zb.addEntry (std::make_unique<MemoryInputStream> (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 0, "preset.json", Time::getCurrentTime());
            zb.addFile (wavFile, 0, "samples/01_Pad_A3.wav");
            FileOutputStream out (preset);
            zb.writeToStream (out, nullptr);
        }
        auto r = loader::loadPreset (preset);
        bool ok = r.zones.size() == 2 && r.interp == 1 && r.presetName == "Warm Pad";
        if (ok)
        {
            const auto& p = r.zones[0].params;
            ok = p.root == 57 && p.loKey == 40 && p.hiKey == 70 && p.loopMode == LoopMode::untilRelease && p.loopStart == 1000
                 && p.loopEnd == 30000 && p.crossfade == 500 && near (p.release, 1.2, 1e-4) && near (p.gainDb, -3, 1e-4)
                 && near (p.pan, 20, 1e-4) && ! p.keepSpeed && r.zones[0].sample == r.zones[1].sample;
        }
        check (ok, "prototype .emxp preset imports with all zone settings", r.errors.joinIntoString ("; "));

        String err;
        auto again = loader::loadSource ("emxp|" + preset.getFullPathName() + "|samples/01_Pad_A3.wav", err);
        check (again != nullptr && again->name == "Pad_A3" && again->length() == a440->length(),
               "a sample inside a preset reloads from a saved project", err);

        auto missing = loader::loadSource ("file|" + tmp.getChildFile ("nope.wav").getFullPathName(), err);
        check (missing == nullptr && err.startsWith ("Missing"), "missing files are reported, not crashed on");

        //== Save a preset, load it back: everything must survive ==================
        PresetToSave save;
        save.name = "Round Trip";
        save.interp = Interp::sinc;
        save.zones = r.zones;
        save.macros = { { "CUTOFF", 42.0f }, { "SPACE", 7.5f } };
        save.fx = JSON::parse (R"({"preset":[{"type":"delay","on":true}]})");
        const auto saved = tmp.getChildFile ("Round Trip.emxp");
        String saveErr;
        const bool wrote = loader::savePreset (saved, save, saveErr);
        auto back = loader::loadPreset (saved);
        bool same = wrote && back.isPreset && back.zones.size() == 2 && back.presetName == "Round Trip"
                    && back.interp == (int) Interp::sinc && back.macros.size() >= 2
                    && back.macros[0].first == "CUTOFF" && near (back.macros[0].second, 42.0, 1e-3)
                    && back.fx["preset"].size() == 1;
        float worstDiff = 1.0f;
        if (same)
        {
            const auto& a = r.zones[0];
            const auto& b = back.zones[0];
            same = b.params.root == a.params.root && b.params.loKey == a.params.loKey && b.params.loopMode == a.params.loopMode
                   && b.params.loopStart == a.params.loopStart && b.params.loopEnd == a.params.loopEnd
                   && b.params.crossfade == a.params.crossfade && near (b.params.release, a.params.release, 1e-4)
                   && near (b.params.pan, a.params.pan, 1e-4) && b.params.keepSpeed == a.params.keepSpeed
                   && b.sample->length() == a.sample->length() && near (b.sample->sampleRate, a.sample->sampleRate, 0.1)
                   && back.zones[0].sample == back.zones[1].sample;   // shared samples are stored once
            worstDiff = 0.0f;
            for (int i = 0; i < a.sample->length(); ++i)
                worstDiff = jmax (worstDiff, std::abs (a.sample->audio.getSample (0, i) - b.sample->audio.getSample (0, i)));
        }
        check (same && worstDiff < 1.0e-5f, "preset save + load keeps zones, macros, FX and audio (24-bit)",
               saveErr + back.errors.joinIntoString ("; ") + " diff " + String (worstDiff, 7));

        //== Bank: two presets in one .emxb ========================================
        MemoryBlock p1 = loader::presetToBytes (save, saveErr);
        save.name = "Second";
        MemoryBlock p2 = loader::presetToBytes (save, saveErr);
        const auto bankFile = tmp.getChildFile ("My Bank.emxb");
        const bool bankOk = loader::saveBank (bankFile, "My Bank", { { "Round Trip", p1 }, { "Second", p2 } }, saveErr);
        auto info = loader::readBankInfo (bankFile);
        bool bankGood = bankOk && info.error.isEmpty() && info.name == "My Bank" && info.presetNames.size() == 2
                        && info.presetNames[1] == "Second";
        if (bankGood)
        {
            auto bytes = loader::readBankEntry (bankFile, info.presetEntries[1]);
            auto second = loader::loadPresetFromMemory (bytes, "emxb|" + bankFile.getFullPathName() + "|" + info.presetEntries[1], "?");
            bankGood = second.isPreset && second.presetName == "Second" && second.zones.size() == 2;
            if (bankGood)
            {
                String e2;
                auto fromSource = loader::loadSource (second.zones[0].sample->source, e2);
                bankGood = fromSource != nullptr && fromSource->length() == a440->length();
                if (! bankGood) saveErr = "source " + second.zones[0].sample->source + ": " + e2;
            }
        }
        check (bankGood, "bank save + open + load preset + reload sample from bank", saveErr + info.error);

        auto viaLoad = loader::load (saved);
        check (viaLoad.isPreset && viaLoad.zones.size() == 2 && loader::isBankFile (bankFile) && ! loader::isPresetFile (bankFile),
               "file types are recognised (.emxp / .emxb)");
    }
    tmp.deleteRecursively();

    //== FX ========================================================================
    {
        using namespace emx::fx;
        constexpr int blk = 64;
        // Runs a module over a signal (stereo, in 64-sample blocks) and returns the left channel.
        auto runFx = [&] (Module& m, const std::vector<float>& in, double bpm = 120.0)
        {
            std::vector<float> l (in), r (in);
            float v[maxSlots] {};
            for (int i = 0; i < maxSlots; ++i)
                v[i] = m.value (i);
            for (size_t p = 0; p < l.size(); p += blk)
            {
                const int n = (int) std::min<size_t> (blk, l.size() - p);
                m.processor->process (v, m.settings, l.data() + p, r.data() + p, n, SR, bpm);
            }
            return l;
        };
        auto tone = [&] (double hz, double secs, float amp = 0.5f)
        {
            std::vector<float> x ((size_t) (secs * SR));
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = amp * (float) std::sin (MathConstants<double>::twoPi * hz * (double) i / SR);
            return x;
        };
        auto level = [] (const std::vector<float>& x, size_t from, size_t to)
        {
            double s2 = 0;
            for (size_t i = from; i < std::min (to, x.size()); ++i) s2 += (double) x[i] * x[i];
            return std::sqrt (s2 / (double) std::max<size_t> (1, std::min (to, x.size()) - from));
        };

        // Every effect in every mode, with default knobs: finite output, no runaway.
        String bad;
        int runs = 0;
        auto noise = tone (220.0, 0.5);
        for (auto t : addOrder())
            for (int mode = 0; mode < (int) def (t).modes.size(); ++mode)
            {
                auto m = makeModule (t, 1, SR);
                setMode (m, mode);
                if (t == Type::delay) m.settings.sync = mode % 2 == 0;
                auto out = runFx (m, noise);
                bool ok = true;
                for (auto x : out)
                    if (! std::isfinite (x) || std::abs (x) > 8.0f) { ok = false; break; }
                ++runs;
                if (! ok) bad << def (t).name << "/" << def (t).modes[(size_t) mode] << " ";
            }
        check (bad.isEmpty(), "all " + String (runs) + " effect modes run cleanly", bad);

        // Filter: low pass at 500 Hz keeps 200 Hz and removes 5 kHz; high pass the opposite.
        {
            auto lp = makeModule (Type::filter, 1, SR);
            lp.norm[0] = def (Type::filter).slots[0].spec.toNorm (500.0f);
            lp.norm[1] = 0.0f;
            const auto lo = level (runFx (lp, tone (200, 0.5)), 4800, 24000), hi = level (runFx (lp, tone (5000, 0.5)), 4800, 24000);
            auto hp = lp;
            hp.processor = createProcessor (Type::filter, SR);
            hp.settings.mode = 1;
            hp.settings.slope = 1;
            const auto hlo = level (runFx (hp, tone (100, 0.5)), 4800, 24000), hhi = level (runFx (hp, tone (5000, 0.5)), 4800, 24000);
            check (lo > 0.3 && hi < 0.03 && hlo < 0.02 && hhi > 0.3, "filter low pass / high pass",
                   "LP " + String (lo, 3) + " / " + String (hi, 4) + ", HP 24dB " + String (hlo, 4) + " / " + String (hhi, 3));
        }

        // Delay synced to 1/4 at 120 BPM: the echo of a click arrives after 0.5 s.
        {
            auto d = makeModule (Type::delay, 1, SR);
            d.settings.sync = true; d.settings.num = 1; d.settings.den = 4;
            d.norm[6] = 1.0f;   // mix 100%: only the echo
            d.norm[1] = 0.0f;   // no repeats
            std::vector<float> click ((size_t) SR, 0.0f);
            click[100] = 1.0f;
            auto out = runFx (d, click, 120.0);
            size_t peakAt = 0;
            for (size_t i = 1; i < out.size(); ++i)
                if (std::abs (out[i]) > std::abs (out[peakAt])) peakAt = i;
            check (near ((double) (peakAt - 100) / SR, 0.5, 0.004), "delay 1/4 at 120 BPM echoes after 0.5 s",
                   String ((double) (peakAt - 100) / SR, 4) + " s");
        }

        // Reverb: sound keeps ringing after the input stops.
        {
            auto rv = makeModule (Type::reverb, 1, SR);
            auto in = tone (440, 0.3);
            in.resize ((size_t) (SR * 1.5), 0.0f);
            auto out = runFx (rv, in);
            const auto tail = level (out, (size_t) (SR * 0.5), (size_t) (SR * 0.9));
            check (tail > 0.003, "reverb tail rings after the note", "tail rms " + String (tail, 4));
        }

        // Compressor: a loud tone comes out quieter and reports gain reduction.
        {
            auto c = makeModule (Type::comp, 1, SR);
            const auto in = tone (440, 0.5, 0.9f);
            const auto out = runFx (c, in);
            check (level (out, 12000, 24000) < level (in, 12000, 24000) * 0.7 && c.processor->gainReductionDb < -3.0f,
                   "compressor reduces a loud signal", "GR " + String (c.processor->gainReductionDb, 1) + " dB");
        }

        // Modulation: a macro and an envelope move a filter's cutoff.
        {
            Setup setup;
            auto m = makeModule (Type::filter, 7, SR);
            m.norm[0] = 0.2f;
            setup.master.push_back (m);
            setup.envelopes = { makeEnvelope (false, "Saw Up") };
            setup.envelopes[0].sync = false;
            setup.envelopes[0].hz = 1.0f;
            setup.routes.push_back ({ { SrcKind::macro, 0 }, { 7, 0 }, 0.5f });
            setup.routes.push_back ({ { SrcKind::env, 0 }, { 7, 0 }, 0.25f });
            auto snap = Snapshot::build (setup, {});
            Engine eng;
            eng.setSnapshot (snap.get());
            float macros[20] {};
            macros[0] = 1.0f;
            float l[blk] {}, r[blk] {};
            eng.beginBlock (blk, SR, 120, 1, true, macros, 20, 0.0f);   // note-on starts the envelope
            eng.processChain (snap->master, l, r, blk);
            const float atStart = eng.modOut[0].value[0].load();
            for (int b = 0; b < (int) (SR * 0.5) / blk; ++b)
            {
                eng.beginBlock (blk, SR, 120, 1, true, macros, 20, 0.0f);
                eng.processChain (snap->master, l, r, blk);
            }
            const float half = eng.modOut[0].value[0].load();
            check (near (atStart, 0.7, 0.01) && near (half, 0.2f + 0.5f + 0.25f * 0.5f, 0.02),
                   "macro and envelope modulate a knob", String (atStart, 3) + " then " + String (half, 3));
        }

        // JSON round trip (prototype format).
        {
            std::map<String, int> idMap;
            int nextId = 50;
            Chain chain { makeModule (Type::delay, 3, SR), makeModule (Type::dist, 4, SR) };
            chain[0].settings.sync = false; chain[0].norm[0] = 0.33f;
            setMode (chain[1], 5);
            chain[1].norm[1] = 0.8f;
            auto back = chainFromJson (JSON::parse (JSON::toString (chainToJson (chain))), idMap, nextId, SR);
            auto env = makeEnvelope (true, "Pluck");
            env.a = 0.25f; env.cr = -0.5f;
            const auto env2 = envelopeFromJson (JSON::parse (JSON::toString (envelopeToJson (env))));
            const bool ok = back.size() == 2 && back[0].type == Type::delay && ! back[0].settings.sync
                            && near (back[0].norm[0], 0.33, 1e-4) && back[1].settings.mode == 5 && near (back[1].norm[1], 0.8, 1e-4)
                            && idMap["f3"] == 50 && idMap["f4"] == 51 && env2.ahdsr && near (env2.a, 0.25, 1e-5) && near (env2.cr, -0.5, 1e-5);
            check (ok, "FX chains and envelopes save and load (prototype JSON)");
        }

        // CPU: a chain with all ten effects must be cheap.
        {
            Chain all;
            for (auto t : addOrder())
                all.push_back (makeModule (t, (int) t + 1, SR));
            Setup setup;
            setup.master = all;
            auto snap = Snapshot::build (setup, {});
            Engine eng;
            eng.setSnapshot (snap.get());
            float macros[20] {};
            std::vector<float> l (blk), r (blk);
            const int blocks = (int) (SR * 2.0) / blk;
            const auto t0 = Time::getMillisecondCounterHiRes();
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blk; ++i) l[(size_t) i] = r[(size_t) i] = 0.3f * (float) std::sin (0.05 * (b * blk + i));
                eng.beginBlock (blk, SR, 120, 0, false, macros, 20, 0.0f);
                eng.processChain (snap->master, l.data(), r.data(), blk);
            }
            const auto load = (Time::getMillisecondCounterHiRes() - t0) / 2000.0;
            check (load < 0.25, "all 10 effects in one chain run in real time", String (load * 100.0, 1) + "% of one CPU core");
        }
    }

    //== CPU: 32 keep-speed voices with sinc HQ must run comfortably in real time ==
    {
        auto pad = sine (SR, 4.0, 220.0, 0.3f);
        auto p = basic (*pad, 57);
        p.keepSpeed = true;
        p.loopMode = LoopMode::loop;
        p.loopStart = 20000;
        p.loopEnd = 180000;
        p.crossfade = 4000;
        auto set = std::make_shared<ZoneSet>();
        set->zones.push_back ({ 1, pad, p });
        SamplerEngine e;
        e.prepare (SR);
        e.setZoneSet (set);
        for (int i = 0; i < 32; ++i)
            e.noteOn (40 + i, 100);
        float L[256], R[256];
        const double seconds = 2.0;
        const auto t0 = Time::getMillisecondCounterHiRes();
        for (int b = 0; b < (int) (seconds * SR / 256); ++b)
        {
            std::fill (L, L + 256, 0.f);
            std::fill (R, R + 256, 0.f);
            e.render (L, R, 256);
        }
        const auto load = (Time::getMillisecondCounterHiRes() - t0) / (seconds * 1000.0);
        check (load < 0.35 && e.getActiveVoiceCount() == 32, "32 keep-speed sinc voices run in real time",
               String (load * 100.0, 1) + "% of one CPU core");
    }

    std::printf ("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
