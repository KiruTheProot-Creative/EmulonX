// Emulon X — engine tests. Build with -DEMX_BUILD_TESTS=ON and run EmulonXTests.
// Exits with code 1 if anything fails (so the GitHub build turns red).
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <juce_audio_formats/juce_audio_formats.h>
#include <set>
#include "engine/SamplerEngine.h"
#include "engine/SampleLoader.h"
#include "fx/FxEngine.h"
#include "engine/Arpeggiator.h"
#include "engine/SampleEdit.h"
#include "engine/ExbImport.h"
#include "engine/EmuVoice.h"
#include "NaturalSort.h"
#include "ui/SkinFile.h"
#include "ui/SkinDraw.h"

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

/** EmulonXTests --reverb-demo file.wav : the same pluck through each reverb type (2.5 s each). */
static int reverbDemo (const File& file)
{
    using namespace emx::fx;
    const double SR = 48000.0;
    const int seg = (int) (SR * 2.5);
    const int numModes = (int) def (Type::reverb).modes.size();
    AudioBuffer<float> outBuf (2, seg * numModes);
    outBuf.clear();
    for (int m = 0; m < numModes; ++m)
    {
        auto rv = makeModule (Type::reverb, 1, SR);
        setMode (rv, m);
        rv.norm[6] = def (Type::reverb).slots[6].spec.toNorm (0.4f);   // 40% mix
        std::vector<float> l ((size_t) seg, 0.0f), r ((size_t) seg, 0.0f);
        // A short plucked minor chord (decaying saw-ish tones).
        const double freqs[] { 220.0, 261.63, 329.63, 440.0 };
        for (int i = 0; i < (int) (SR * 0.5); ++i)
        {
            double v = 0;
            for (double f : freqs)
                v += std::sin (MathConstants<double>::twoPi * f * i / SR) + 0.3 * std::sin (MathConstants<double>::twoPi * 2.0 * f * i / SR);
            const float e = (float) (0.12 * std::exp (-i / (SR * 0.12)));
            l[(size_t) i] = r[(size_t) i] = (float) v * e;
        }
        float v8[maxSlots] {};
        for (int i = 0; i < maxSlots; ++i)
            v8[i] = rv.value (i);
        for (int p0 = 0; p0 < seg; p0 += 64)
            rv.processor->process (v8, rv.settings, l.data() + p0, r.data() + p0, std::min (64, seg - p0), SR, 120.0);
        for (int i = 0; i < seg; ++i)
        {
            outBuf.setSample (0, m * seg + i, l[(size_t) i]);
            outBuf.setSample (1, m * seg + i, r[(size_t) i]);
        }
        std::printf ("%d: %s\n", m + 1, def (Type::reverb).modes[(size_t) m]);
    }
    file.deleteFile();
    std::unique_ptr<OutputStream> os (file.createOutputStream().release());
    if (auto w = WavAudioFormat().createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (SR).withNumChannels (2).withBitsPerSample (16)))
        w->writeFromAudioSampleBuffer (outBuf, 0, outBuf.getNumSamples());
    return 0;
}


//== Skin files (.emxd) ==============================================================
static MemoryBlock pngOf (Colour c, int w = 4, int h = 4)
{
    Image im (Image::ARGB, w, h, true);
    im.clear (im.getBounds(), c);
    MemoryOutputStream out;
    PNGImageFormat().writeImageToStream (im, out);
    return out.getMemoryBlock();
}

static void skinFileTests()
{
    using namespace emx::skin;
    const auto noFiles = [] (const String&) { return MemoryBlock(); };
    Colour c;
    check (parseColour ("#ff2a2a", c) && c == Colour (0xffff2a2a), "skin: #rrggbb colours");
    check (parseColour ("abc", c) && c == Colour (0xffaabbcc), "skin: short #rgb colours");
    check (! parseColour ("red", c) && ! parseColour ("#12345", c), "skin: rejects things that aren't colours");

    {
        Design d;
        String err;
        StringArray warn;
        const bool ok = fromJson (JSON::parse (R"({"format":"emxd","version":2,"name":"Links","tokens":{"accent":"#00ff00","display":"#102030"},
            "display":{"textColor":"@accent","fill":{"type":"solid","a":"@display"}},"knob":{"pointer":"@nope"},"panel":{"radius":500,"shape":7}})"),
                                  noFiles, d, err, &warn);
        check (ok && d.displayText == Colour (0xff00ff00) && d.display.fill.a == Colour (0xff102030),
               "skin: colours linked to the palette follow it");
        check (ok && d.pointer == d.accent, "skin: a link to an unknown palette colour falls back");
        check (ok && d.panel.radius <= 64.0f && d.panel.shape <= 1.0f, "skin: out-of-range numbers are kept in range");
    }
    {
        Design d;
        String err;
        check (! fromJson (JSON::parse ("{\"name\":\"x\"}"), noFiles, d, err) && err.isNotEmpty(), "skin: rejects a file that isn't a skin", err);
        check (! fromJson (JSON::parse ("{\"format\":\"emxd\",\"version\":1}"), noFiles, d, err), "skin: rejects the old 1.1.2 skin format", err);
    }
    {
        // a picture inside the description (data: URL), as Skin Studio exports it
        const auto png = pngOf (Colours::orange, 6, 3);
        const auto json = "{\"format\":\"emxd\",\"version\":2,\"button\":{\"fill\":{\"type\":\"image\",\"a\":\"#000000\",\"img\":\"data:image/png;base64,"
                          + Base64::toBase64 (png.getData(), png.getSize()) + "\",\"fit\":\"cover\"}}}";
        Design d;
        String err;
        const bool ok = fromJson (JSON::parse (json), noFiles, d, err);
        check (ok && d.button.fill.type == Fill::Type::image && d.button.fill.picture.image.getWidth() == 6 && d.button.fill.fit == Fill::Fit::cover,
               "skin: pictures inside the description (data: URLs)");
    }
    {
        // save -> load: pictures and textures travel inside the .emxd file
        const auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("emx-skin-tests");
        dir.deleteRecursively();
        dir.createDirectory();
        Image grey (Image::RGB, 8, 8, false);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                grey.setPixelAt (x, y, x < 4 ? Colour (0xff404040) : Colour (0xffc0c0c0));
        MemoryOutputStream tex;
        PNGImageFormat().writeImageToStream (grey, tex);
        const auto json = JSON::parse (R"({"format":"emxd","version":2,"name":"Round Trip","author":"Tester",
            "panel":{"fill":{"type":"image","a":"#111111","img":"images/panel.png"},"overlay":{"tex":"metal-x","strength":0.7}},
            "button":{"overlay":{"tex":"metal-missing","strength":0.5}},
            "knob":{"style":"picture","picture":"images/knob.png"}})");
        String err;
        const auto file = dir.getChildFile ("b 10.emxd");
        const bool saved = save (file, json, { { "images/panel.png", pngOf (Colours::blue) }, { "images/knob.png", pngOf (Colours::white, 16, 16) },
                                               { "textures/metal-x.png", tex.getMemoryBlock() } }, err);
        Design d;
        StringArray warn;
        const bool loaded = saved && load (file, d, err, &warn);
        check (loaded && d.name == "Round Trip" && d.panel.fill.picture.isValid() && d.knobStyle == Design::KnobStyle::picture,
               "skin: save and load an .emxd file with pictures", err);
        check (loaded && d.panel.overlay.isActive() && std::abs (d.panel.overlay.strength - 0.7f) < 0.001f, "skin: textures inside the file work as overlays");
        check (loaded && ! d.button.overlay.isActive() && warn.joinIntoString (" ").contains ("metal-missing"),
               "skin: a missing texture is reported but doesn't stop the skin", warn.joinIntoString ("; "));
        String name, author;
        check (readInfo (file, name, author) && name == "Round Trip" && author == "Tester", "skin: quick look at name and author");
        file.copyFileTo (dir.getChildFile ("b 2.emxd"));
        dir.getChildFile ("notes.txt").replaceWithText ("x");
        const auto found = findSkins ({ dir.getFullPathName() });
        check (found.size() == 2 && found[0].getFileName() == "b 2.emxd", "skin: finds skin files, sorted like Explorer");
        const auto broken = dir.getChildFile ("broken.emxd");
        broken.replaceWithText ("garbage");
        check (! load (broken, d, err) && err.isNotEmpty(), "skin: a broken file is refused with a reason", err);
        // the light / dark halves of a texture: mid-grey changes nothing
        Image mid (Image::RGB, 4, 4, false);
        mid.clear (mid.getBounds(), Colour (0xff808080));
        Image lighter, darker;
        makeBlendImages (mid, lighter, darker);
        check (lighter.getPixelAt (1, 1).getAlpha() < 4 && darker.getPixelAt (1, 1).getAlpha() < 4, "skin: mid-grey in a texture leaves the surface as it is");
        dir.deleteRecursively();
    }
    {
        // effects: bloom makes bright things glow, it must never hide them; blur softens
        Design d;
        d.uid = 4242;
        auto render = [&] (const Effects& fx, int salt)
        {
            Image out (Image::ARGB, 200, 100, true);
            Graphics g (out);
            g.fillAll (Colour (0xff202020));
            g.setColour (Colours::white.withAlpha (0.08f));   // something see-through drawn just before
            g.fillRect (0, 0, 2, 2);
            const Rectangle<float> r (40, 30, 120, 40);
            drawWithEffects (g, r, fx, effectKey (d, 99, r, (uint64) salt), [&] (Graphics& gg)
            {
                gg.setColour (Colours::white);
                gg.fillRect (r.reduced (30, 12));     // 70..130 x 42..58
            });
            return out;
        };
        auto lum = [] (Colour c) { return c.getPerceivedBrightness(); };
        Effects bloom;
        bloom.bloom = 0.9f;
        bloom.bloomSize = 8.0f;
        auto b = render (bloom, 1);
        std::printf ("  bloom: centre %s, edge glow %s, far %s\n", b.getPixelAt (100, 50).toDisplayString (false).toRawUTF8(),
                     b.getPixelAt (64, 50).toDisplayString (false).toRawUTF8(), b.getPixelAt (10, 10).toDisplayString (false).toRawUTF8());
        check (lum (b.getPixelAt (100, 50)) > 0.95f, "skin: bloom keeps bright things bright");
        check (lum (b.getPixelAt (64, 50)) > lum (Colour (0xff202020)) + 0.05f, "skin: bloom glows around bright things");
        check (std::abs (lum (b.getPixelAt (10, 10)) - lum (Colour (0xff202020))) < 0.02f, "skin: bloom leaves the rest alone");
        auto b2 = render (bloom, 1);   // from the cache
        check (b2.getPixelAt (100, 50) == b.getPixelAt (100, 50) && b2.getPixelAt (64, 50) == b.getPixelAt (64, 50), "skin: effects drawn from the cache look the same");
        Effects blur;
        blur.blur = 4.0f;
        auto u = render (blur, 2);
        check (lum (u.getPixelAt (100, 50)) > 0.8f && lum (u.getPixelAt (68, 50)) > lum (Colour (0xff202020)) + 0.05f
                   && lum (u.getPixelAt (68, 50)) < 0.9f, "skin: blur softens the edges");
    }
    {
        // corner shapes: cut, round and squircle corners leave the very corner out; no rounding keeps it
        const Rectangle<float> r (0, 0, 100, 60);
        const Point<float> corner (2.0f, 2.0f), nearRound (5.0f, 5.0f);
        const auto cut = shapePath (r, 20, 0.0f), round = shapePath (r, 20, 0.5f), squircle = shapePath (r, 20, 1.0f), square = shapePath (r, 0, 0.5f);
        check (! cut.contains (corner) && ! round.contains (corner) && ! squircle.contains (corner) && square.contains (corner),
               "skin: corner shapes cut the corner");
        check (! cut.contains (nearRound) && squircle.contains (nearRound) && cut.getBounds() == r && squircle.getBounds() == r,
               "skin: cut corners take more away than squircles, and stay inside the box");
    }
}

/** --exb-scan <folder>: loads every preset of every Emulator X bank in the folder and plays a note in each zone. */
static int exbScan (const File& dir)
{
    std::map<String, std::vector<String>> problems;
    std::map<String, int> reasons;
    std::map<String, String> reasonExample;
    int presets = 0, fine = 0, partly = 0;
    for (const auto& entry : RangedDirectoryIterator (dir, false, "*.exb", File::findDirectories | File::findFiles))
    {
        const auto bank = entry.getFile();
        if (! exb::isExb (bank))
            continue;
        const auto info = exb::readInfo (bank);
        if (info.error.isNotEmpty())
            problems["bank can't be read: " + info.error].push_back (bank.getFileName());
        for (int i = 0; i < info.presetEntries.size(); ++i)
        {
            ++presets;
            const auto key = bank.getFileNameWithoutExtension() + " | " + info.presetEntries[i] + " " + info.presetNames[i];
            auto r = exb::loadPreset (bank, info.presetEntries[i].getIntValue());
            if (r.zones.empty())
            {
                auto why = r.errors.isEmpty() ? String ("no zones") : r.errors[0];
                // group similar reasons: drop file paths and names
                if (why.contains (": ")) why = why.fromFirstOccurrenceOf (": ", false, false);
                if (why.startsWith ("Missing sample")) why = "missing sample file";
                problems["no zones: " + why].push_back (key + (r.errors.isEmpty() ? String() : "  [" + r.errors.joinIntoString ("; ").substring (0, 140) + "]"));
                continue;
            }
            // play the middle key of every zone (velocity inside the zone) and listen
            int silent = 0;
            for (auto& z : r.zones)
            {
                auto set = std::make_shared<ZoneSet>();
                set->zones.push_back ({ 1, z.sample, z.params });
                SamplerEngine e;
                e.prepare (48000.0);
                e.setZoneSet (set);
                float knobs[16] {};
                for (size_t k = 0; k < r.macros.size() && k < 16; ++k)
                    knobs[k] = r.macros[k].second / 100.0f;
                e.setControllers (knobs);
                e.noteOn ((z.params.loKey + z.params.hiKey) / 2, jlimit (1, 127, (z.params.loVel + z.params.hiVel) / 2));
                float L[256], R[256];
                double peak = 0;
                for (int b = 0; b < 100; ++b)
                {
                    std::fill (L, L + 256, 0.0f); std::fill (R, R + 256, 0.0f);
                    e.render (L, R, 256);
                    for (int k = 0; k < 256; ++k) peak = jmax (peak, (double) std::abs (L[k]), (double) std::abs (R[k]));
                }
                if (peak > 8.0 || ! std::isfinite (peak))
                    problems["unstable or far too loud"].push_back (key + " | " + z.sample->name + " peak " + String (peak, 1));
                if ((peak < 1.0e-4 || ! std::isfinite (peak)) && z.params.design != nullptr)
                {
                    // why: follow the E-mu voice for the same time
                    emu::Inputs in;
                    for (size_t k = 0; k < r.macros.size() && k < 16; ++k) in.midi[k] = r.macros[k].second / 100.0f;
                    emu::VoiceState st;
                    Random rnd (1);
                    emu::startNote (st, *z.params.design, rnd);
                    float maxEnv = 0, maxGain = 0, minCut = 1, maxCut = 0;
                    for (int step = 0; step < 800; ++step)
                    {
                        emu::control (st, *z.params.design, in, (z.params.loKey + z.params.hiKey) / 2, jlimit (1, 127, (z.params.loVel + z.params.hiVel) / 2), 48000.0, 32);
                        maxEnv = jmax (maxEnv, st.env[0].level); maxGain = jmax (maxGain, st.ampGain);
                        minCut = jmin (minCut, st.cutoff); maxCut = jmax (maxCut, st.cutoff);
                    }
                    const int ft = z.params.design->filterType;
                    String why = maxEnv < 0.01f ? "amp envelope stays near 0"
                               : maxGain < 0.01f ? "volume cords turn it down"
                               : z.params.attack > 0.4f ? "slow attack (fades in later)"
                               : (ft <= 2 && maxCut < 0.12f) ? "low pass nearly closed"
                               : ((ft == 8 || ft == 9) && minCut > 0.9f) ? "high pass nearly closed"
                               : z.params.loVel > 1 && z.params.hiVel < 40 ? "very soft velocity layer"
                               : "other (filter " + String (ft) + ")";
                    reasons[why]++;
                    if (reasonExample.find (why) == reasonExample.end())
                        reasonExample[why] = key + " | " + z.sample->name;
                }
                if (peak < 1.0e-4 || ! std::isfinite (peak))
                {
                    ++silent;
                    if (SystemStats::getEnvironmentVariable ("EMX_EXB_VERBOSE", {}).isNotEmpty())
                    {
                        const auto mag = z.sample->audio.getMagnitude (0, z.sample->length());
                        std::printf ("   silent: %s | %s root %d keys %d-%d vel %d-%d len %d peak %.4f start %d end %d loop %d %d-%d A %.3f gain %.1f rate %.0f\n",
                                     key.toRawUTF8(), z.sample->name.toRawUTF8(), z.params.root, z.params.loKey, z.params.hiKey, z.params.loVel, z.params.hiVel, z.sample->length(),
                                     mag, z.params.sampleStart, z.params.sampleEnd, (int) z.params.loopMode, z.params.loopStart, z.params.loopEnd, z.params.attack, z.params.gainDb,
                                     z.sample->sampleRate);
                    }
                }
            }
            if (silent == (int) r.zones.size())
                problems["loads but every zone is silent"].push_back (key);
            else if (silent > 0 || ! r.errors.isEmpty())
            {
                ++partly;
                problems[silent > 0 ? String ("some zones silent") : "some zones skipped: " + r.errors[0].fromFirstOccurrenceOf (": ", false, false)]
                    .push_back (key + " (" + String (silent) + " of " + String ((int) r.zones.size()) + " silent)");
            }
            else
                ++fine;
        }
    }
    std::printf ("%d presets: %d fine, %d partly\n", presets, fine, partly);
    std::printf ("silent zones by reason:\n");
    for (auto& [why, count] : reasons)
        std::printf ("   %5d  %s   (e.g. %s)\n", count, why.toRawUTF8(), reasonExample[why].toRawUTF8());
    for (auto& [why, list] : problems)
    {
        std::printf ("\n== %s: %d\n", why.toRawUTF8(), (int) list.size());
        for (size_t i = 0; i < list.size() && i < 15; ++i)
            std::printf ("   %s\n", list[i].toRawUTF8());
    }
    return 0;
}

/** --exb-debug <bank> <preset number> <zone index>: prints what one note's E-mu voice does over time. */
static int exbDebug (const File& bank, int preset, int zoneIndex)
{
    auto r = exb::loadPreset (bank, preset);
    if (zoneIndex >= (int) r.zones.size()) { std::printf ("only %d zones\n", (int) r.zones.size()); return 1; }
    const auto& z = r.zones[(size_t) zoneIndex];
    const auto& d = *z.params.design;
    std::printf ("%s: zone %d '%s' keys %d-%d vel %d-%d gain %.1f filter %d cut %.3f res %.3f\n", r.presetName.toRawUTF8(), zoneIndex,
                 z.sample->name.toRawUTF8(), z.params.loKey, z.params.hiKey, z.params.loVel, z.params.hiVel, z.params.gainDb, d.filterType, d.cutoff, d.resonance);
    for (int e = 0; e < 3; ++e)
    {
        std::printf ("  env %d:", e);
        for (int i = 0; i < 6; ++i) std::printf (" %.1f/%.1f", d.env[(size_t) e].rate[(size_t) i], d.env[(size_t) e].level[(size_t) i]);
        std::printf ("\n");
    }
    for (auto& c : d.cords) std::printf ("  cord %s -> %s %.1f\n", emu::sourceName (c.source).toRawUTF8(), emu::destName (c.dest).toRawUTF8(), c.amount);
    emu::Inputs in;
    for (size_t k = 0; k < r.macros.size() && k < 16; ++k) in.midi[k] = r.macros[k].second / 100.0f;
    emu::VoiceState st;
    Random rnd (1);
    emu::startNote (st, d, rnd);
    const int note = (z.params.loKey + z.params.hiKey) / 2, vel = (z.params.loVel + z.params.hiVel) / 2;
    for (int step = 0; step < 48000 / 32 * 2; ++step)
    {
        if (step == 48000 / 32) emu::releaseNote (st);
        emu::control (st, d, in, note, vel, 48000.0, 32);
        if (step % 150 == 0)
            std::printf ("  %.2fs amp env st%d %.3f  flt env %.3f  gain %.4f  cutoff %.3f res %.3f pitch %+.2f\n", step * 32 / 48000.0, st.env[0].stage, st.env[0].level,
                         st.env[1].level, st.ampGain, st.cutoff, st.resonance, st.pitchSemis);
    }
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 4 && String (argv[1]) == "--exb-debug")
        return exbDebug (File (String (argv[2])), String (argv[3]).getIntValue(), String (argv[4]).getIntValue());
    if (argc > 2 && String (argv[1]) == "--exb-scan")
        return exbScan (File (String (argv[2])));
    if (argc > 1 && String (argv[1]) == "--bench")
        return bench();
    if (argc > 2 && String (argv[1]) == "--reverb-demo")
        return reverbDemo (File (String (argv[2])));
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
        setMiddleCOctave (4);   // scientific pitch: C4 = middle C
        for (auto& [name, want] : cases)
            if (noteFromName (name) != want) { ok = false; bad << name << "=" << noteFromName (name) << " "; }
        check (ok, "note names in file names (C4 = middle C)", bad);

        // Ableton's naming (the default): C3 = middle C = MIDI 60
        setMiddleCOctave (3);
        const std::pair<const char*, int> ableton[] { { "Piano_C3", 60 }, { "Pad A#2", 58 }, { "Bell_C5", 84 }, { "Sub C-2", 0 } };
        ok = true;
        bad = {};
        for (auto& [name, want] : ableton)
            if (noteFromName (name) != want) { ok = false; bad << name << "=" << noteFromName (name) << " "; }
        ok = ok && noteName (60) == "C3" && noteName (69) == "A3" && noteName (0) == "C-2";
        check (ok, "note names like Ableton: C3 = middle C (MIDI 60)", bad + noteName (60) + " " + noteName (0));
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

    //== Sample start / end ===========================================================
    {
        auto p = basic (*a440, 69);
        p.loopMode = LoopMode::off;
        p.sampleStart = a440->length() / 2;
        auto r = play (a440, p, 69, 1.4);
        p.sampleStart = 0;
        p.sampleEnd = a440->length() / 4;
        auto r2 = play (a440, p, 69, 1.4);
        check (near (r.aliveSeconds, 0.5, 0.02) && near (r2.aliveSeconds, 0.25, 0.02), "sample start and end markers",
               String (r.aliveSeconds, 3) + " s from the middle, " + String (r2.aliveSeconds, 3) + " s to a quarter");
    }

    //== Random start =================================================================
    {
        auto p = basic (*a440, 69);
        p.loopMode = LoopMode::off;
        p.sampleStart = 4800;
        p.randomStart = true;
        p.randomEnd = 24000;
        auto set = std::make_shared<ZoneSet>();
        set->zones.push_back ({ 1, a440, p });
        SamplerEngine e;
        e.prepare (SR);
        e.setZoneSet (set);
        std::set<int> starts;
        bool inside = true;
        for (int i = 0; i < 12; ++i)
        {
            e.reset();
            e.noteOn (69, 100);
            SamplerEngine::Playhead ph[4];
            if (e.getPlayheads (ph, 4) == 1)
            {
                starts.insert ((int) ph[0].position);
                inside = inside && ph[0].position >= 4800 && ph[0].position <= 24000;
            }
        }
        check (starts.size() >= 8 && inside, "random start picks a different spot inside the area each note",
               String ((int) starts.size()) + " different starts");
    }

    //== SEMI knob and its modulation ==================================================
    {
        auto p = basic (*a440, 69);
        p.semitones = 12;
        auto up = play (a440, p, 69, 0.5);
        p.semitones = 0;
        auto modded = play (a440, p, 69, 0.5, Interp::sinc, -1, 48000.0, [] (SamplerEngine& e, int)
        {
            SamplerEngine::PitchMod m { 1, 7.0f };   // zone 1 pushed up a fifth by modulation
            e.setPitchMods (&m, 1);
        });
        const auto fu = frequency (up.left, SR, 0.1, 0.4), fm = frequency (modded.left, SR, 0.1, 0.4);
        check (near (fu, 880, 5) && near (fm, 659.26, 5), "SEMI +12 plays an octave up; modulation moves the pitch",
               String (fu, 0) + " Hz / " + String (fm, 0) + " Hz");

        using namespace emx::fx;
        Setup setup;
        setup.routes.push_back ({ { SrcKind::macro, 0 }, { Target::zonePitch, 1 }, 0.25f });
        auto snap = Snapshot::build (setup, {}, { { 1, 0 }, { 2, 5 } });
        Engine eng;
        eng.setSnapshot (snap.get());
        float macros[20] {};
        macros[0] = 1.0f;
        eng.beginBlock (64, SR, 120, 0, false, macros, 20, 0.0f);
        std::pair<int, float> out[8];
        const int count = eng.modulateZonePitch (out, 8);
        check (count == 1 && out[0].first == 1 && near (out[0].second, 12.0, 0.01),
               "a macro can move a zone's SEMI knob (+25% = +12 semitones)", String (count) + " / " + String (count > 0 ? out[0].second : 0.0f, 2));
    }

    //== Arpeggiator ================================================================
    {
        struct Ev { juce::int64 t; int note; bool on; };
        // Runs the arp for `secs`; `inEvents` are (sample, note, on). hostStart >= 0: host playing from that beat.
        auto runArp = [&] (ArpSettings a, std::vector<std::tuple<juce::int64, int, bool>> inEvents, double secs, double hostStart = -1.0)
        {
            Arpeggiator arp;
            arp.prepare (SR);
            arp.setSettings (a);
            std::vector<Ev> outEv;
            const int N = 512;
            MidiBuffer in, out;
            for (juce::int64 b = 0; b * N < (juce::int64) (secs * SR); ++b)
            {
                in.clear();
                for (auto& [t, note, on] : inEvents)
                    if (t >= b * N && t < (b + 1) * N)
                        in.addEvent (on ? MidiMessage::noteOn (1, note, (uint8) 100) : MidiMessage::noteOff (1, note), (int) (t - b * N));
                std::optional<double> ppq;
                if (hostStart >= 0) ppq = hostStart + (double) (b * N) / SR * 2.0;   // 120 BPM = 2 beats per second
                arp.process (in, out, N, 120.0, ppq);
                for (const auto meta : out)
                {
                    const auto m = meta.getMessage();
                    if (m.isNoteOn() || m.isNoteOff())
                        outEv.push_back ({ b * N + meta.samplePosition, m.getNoteNumber(), m.isNoteOn() });
                }
            }
            return outEv;
        };
        auto ons = [] (const std::vector<Ev>& ev) { std::vector<Ev> r; for (auto& e : ev) if (e.on) r.push_back (e); return r; };
        const std::vector<std::tuple<juce::int64, int, bool>> chord { { 100, 60, true }, { 100, 64, true }, { 100, 67, true } };

        ArpSettings a;
        a.enabled = true; a.mode = ArpSettings::up; a.den = 16; a.gate = 0.5f;
        auto up = ons (runArp (a, chord, 0.8));
        bool ok = up.size() >= 6 && up[0].note == 60 && up[1].note == 64 && up[2].note == 67 && up[3].note == 60
                  && up[0].t == 100 && up[1].t == 100 + 6000 && up[2].t == 100 + 12000;
        check (ok, "arp up: C E G C, one 1/16 step (6000 samples at 120 BPM) apart",
               up.size() >= 3 ? String (up[0].note) + " " + String (up[1].note) + " " + String (up[2].note) + " @ " + String (up[1].t) : String ("no notes"));

        a.mode = ArpSettings::down; a.octaves = 2;
        auto dn = ons (runArp (a, chord, 0.9));
        check (dn.size() >= 6 && dn[0].note == 79 && dn[1].note == 76 && dn[2].note == 72 && dn[3].note == 67 && dn[5].note == 60,
               "arp down over 2 octaves", dn.size() >= 4 ? String (dn[0].note) + " " + String (dn[1].note) + " " + String (dn[2].note) + " " + String (dn[3].note) : String ("no notes"));

        a.mode = ArpSettings::up; a.octaves = 1;
        auto all = runArp (a, chord, 0.3);
        juce::int64 offAt = -1;
        for (auto& e : all) if (! e.on && e.note == 60) { offAt = e.t; break; }
        check (offAt == 100 + 3000 - 1, "arp gate 50% ends the note half way through the step", String (offAt));

        auto host = ons (runArp (a, chord, 0.5, 0.1));
        check (! host.empty() && host[0].t == 3600, "arp follows the DAW's beat grid", host.empty() ? String ("none") : String (host[0].t));

        a.patterns[0].steps[1].on = false;
        auto rest = ons (runArp (a, chord, 0.5));
        check (rest.size() >= 2 && rest[1].t == 100 + 12000, "a switched-off step is a rest");
        a.patterns[0].steps[1].on = true;

        // Per-step transpose in semitones, and an arrangement A A B (B is 2 steps, shifted +7).
        {
            ArpSettings t;
            t.enabled = true; t.mode = ArpSettings::up; t.den = 16;
            t.patterns[0].length = 2;
            t.patterns[0].steps[1].transpose = 5;
            t.patterns[1].length = 2;
            t.patterns[1].steps[0].transpose = 7;
            t.patterns[1].steps[1].transpose = -12;
            t.arrangementLength = 3;
            t.arrangement = { 0, 0, 1 };
            auto seq = ons (runArp (t, { { 0, 60, true } }, 1.0));
            const int expect[] { 60, 65, 60, 65, 67, 48, 60, 65 };
            bool okSeq = seq.size() >= 8;
            String got;
            for (size_t i = 0; i < std::min<size_t> (8, seq.size()); ++i)
            {
                got << seq[i].note << " ";
                okSeq = okSeq && seq[i].note == expect[i];
            }
            check (okSeq, "arp arrangement A A B with semitone steps", got);
        }
        // Old presets: octave cells become +/-12 semitones, single pattern.
        {
            auto old = JSON::parse (R"({"enabled":true,"length":4,"steps":[{"on":true,"velocity":1,"octave":1},{"on":false,"velocity":0.5,"octave":-1}]})");
            const auto conv = ArpSettings::fromJson (old);
            check (conv.patterns[0].length == 4 && conv.patterns[0].steps[0].transpose == 12 && ! conv.patterns[0].steps[1].on
                   && conv.patterns[0].steps[1].transpose == -12 && conv.arrangementLength == 1,
                   "older arp settings load (octaves become semitones)");
            const auto again = ArpSettings::fromJson (JSON::parse (JSON::toString (conv.toJson())));
            check (again == conv, "arp patterns and arrangement save and load");
        }

        // No skipped steps, even when steps land exactly on block boundaries or the host's position jitters.
        {
            ArpSettings b;
            b.enabled = true; b.mode = ArpSettings::up; b.den = 16;   // 6000 samples per step at 120 BPM
            String bad;
            for (int blockSize : { 500, 512, 480, 1000, 64, 333 })
                for (int hostMode = 0; hostMode < 3; ++hostMode)
                {
                    Arpeggiator arp;
                    arp.prepare (SR);
                    arp.setSettings (b);
                    MidiBuffer in, out;
                    int count = 0;
                    double ppq = 0.0;
                    const int total = (int) (SR * 4.0);
                    for (int pos = 0; pos < total; pos += blockSize)
                    {
                        in.clear();
                        if (pos == 0)
                            in.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
                        std::optional<double> songPos;
                        if (hostMode == 1) songPos = ppq;                                  // exact song position
                        if (hostMode == 2) songPos = ppq + ((pos / blockSize) % 3 - 1) * 1.0e-7;   // tiny jitter, like real hosts
                        arp.process (in, out, blockSize, 120.0, songPos);
                        for (const auto meta : out)
                            if (meta.getMessage().isNoteOn())
                                ++count;
                        ppq += blockSize / SR * 2.0;
                    }
                    if (count < 32)
                        bad << blockSize << "/" << (hostMode == 0 ? "free" : hostMode == 1 ? "host" : "jitter") << ":" << count << " ";
                }
            check (bad.isEmpty(), "arp never skips a step (32 steps in 4 s, any block size)", bad);
        }

        // When the DAW loops back, the arp follows at once (first step of the loop plays right at the jump).
        {
            ArpSettings b;
            b.enabled = true; b.mode = ArpSettings::up; b.den = 16;
            Arpeggiator arp;
            arp.prepare (SR);
            arp.setSettings (b);
            MidiBuffer in, out;
            const int bs = 512;
            double ppq = 0.0;
            juce::int64 firstAfterJump = -1, jumpAt = -1;
            for (int blk = 0; blk < 300; ++blk)
            {
                in.clear();
                if (blk == 0) in.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
                if (blk == 150) { ppq = 0.0; jumpAt = (juce::int64) blk * bs; }   // loop back to bar 1
                arp.process (in, out, bs, 120.0, ppq);
                for (const auto meta : out)
                    if (meta.getMessage().isNoteOn() && jumpAt >= 0 && firstAfterJump < 0)
                        firstAfterJump = (juce::int64) blk * bs + meta.samplePosition;
                ppq += bs / SR * 2.0;
            }
            check (firstAfterJump == jumpAt, "arp follows the DAW when it loops back", String (firstAfterJump - jumpAt) + " samples late");
        }

        auto released = runArp (a, { { 100, 60, true }, { 100, 64, true }, { 20000, 60, false }, { 20000, 64, false } }, 1.0);
        juce::int64 lastOn = 0;
        int balance = 0;
        for (auto& e : released) { if (e.on) { lastOn = e.t; ++balance; } else --balance; }
        a.latch = true;
        auto latched = ons (runArp (a, { { 100, 60, true }, { 20000, 60, false } }, 1.0));
        check (lastOn < 20000 && balance == 0 && latched.size() >= 7, "letting go stops the arp; latch keeps it going",
               "last note " + String (lastOn) + ", latched notes " + String ((int) latched.size()));
    }

    //== Mono, legato, glide =======================================================
    {
        auto tone = sine (SR, 2.0, 440.0, 0.5f);
        auto p = basic (*tone, 69);
        p.loopMode = LoopMode::loop; p.loopStart = 0; p.loopEnd = tone->length();
        // events: (time, note, on?)
        auto run = [&] (bool legatoMode, float glide, float attack, std::vector<std::tuple<double, int, bool>> events, double secs, int* sounding = nullptr)
        {
            auto q = p;
            q.attack = attack;
            auto set = std::make_shared<ZoneSet>();
            set->interp = Interp::sinc;
            set->zones.push_back ({ 1, tone, q });
            SamplerEngine e;
            e.prepare (SR);
            e.setZoneSet (set);
            e.setVoiceMode (true, legatoMode, glide);
            std::vector<float> out;
            const int N = 64;
            float L[N], R[N];
            for (int b = 0; b < (int) (secs * SR / N); ++b)
            {
                for (auto& [t, note, on] : events)
                    if (b == (int) (t * SR / N))
                        on ? e.noteOn (note, 127) : e.noteOff (note);
                std::fill (L, L + N, 0.0f);
                std::fill (R, R + N, 0.0f);
                e.render (L, R, N);
                out.insert (out.end(), L, L + N);
            }
            if (sounding != nullptr)
                *sounding = e.getActiveVoiceCount();
            return out;
        };

        int voicesLeft = 0;
        auto mono = run (false, 0.0f, 0.001f, { { 0.0, 69, true }, { 0.3, 76, true } }, 0.9, &voicesLeft);
        const auto fm = frequency (mono, SR, 0.5, 0.85);
        check (voicesLeft == 1 && near (fm, 659.26, 5), "mono: a new note replaces the old one",
               String (voicesLeft) + " voice, " + String (fm, 1) + " Hz");

        auto leg = run (true, 0.0f, 1.0f, { { 0.0, 69, true }, { 0.5, 76, true } }, 0.8);
        auto ret = run (false, 0.0f, 1.0f, { { 0.0, 69, true }, { 0.5, 76, true } }, 0.8);
        const auto lv = rms (leg, SR, 0.6, 0.7), rv = rms (ret, SR, 0.6, 0.7);
        check (lv > rv * 2.5 && near (frequency (leg, SR, 0.55, 0.75), 659.26, 6), "legato: the note slides without restarting",
               "level " + String (lv, 3) + " vs restarted " + String (rv, 3));

        auto gl = run (true, 0.2f, 0.001f, { { 0.0, 69, true }, { 0.5, 81, true } }, 1.0);
        const auto mid = frequency (gl, SR, 0.56, 0.62), end = frequency (gl, SR, 0.8, 0.98);
        check (mid > 480 && mid < 840 && near (end, 880, 6), "glide slides the pitch to the new note",
               String (mid, 0) + " Hz on the way, " + String (end, 1) + " Hz after");

        auto back = run (true, 0.0f, 0.001f, { { 0.0, 69, true }, { 0.3, 76, true }, { 0.6, 76, false } }, 1.0);
        check (near (frequency (back, SR, 0.75, 0.98), 440, 4), "mono: letting go returns to the key still held",
               String (frequency (back, SR, 0.75, 0.98), 1) + " Hz");
    }

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

        // Stereo delay with spread, synced to 1/8 at 120 BPM: every repeat on both sides lands on the beat grid
        // (the right side may sit a fixed few ms later, but never drifts further with each repeat).
        {
            auto d = makeModule (Type::delay, 1, SR);
            d.settings.mode = 0;
            d.settings.sync = true; d.settings.num = 1; d.settings.den = 8;
            d.norm[6] = 1.0f;                                                       // only the echoes
            d.norm[1] = def (Type::delay).slots[1].spec.toNorm (0.6f);             // repeats
            d.norm[2] = 1.0f;                                                       // full spread
            d.norm[4] = 0.0f; d.norm[5] = 1.0f;                                     // filters wide open
            const int n = (int) (SR * 1.6);
            std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
            l[100] = r[100] = 1.0f;
            float v[maxSlots] {};
            for (int i = 0; i < maxSlots; ++i) v[i] = d.value (i);
            for (int p = 0; p < n; p += blk)
                d.processor->process (v, d.settings, l.data() + p, r.data() + p, std::min (blk, n - p), SR, 120.0);
            const double step = 0.25 * SR;   // an eighth at 120 BPM
            auto peakNear = [&] (const std::vector<float>& x, double centre)
            {
                int best = (int) centre;
                for (int i = (int) centre - 1000; i <= (int) centre + 1000; ++i)
                    if (i > 0 && i < n && std::abs (x[(size_t) i]) > std::abs (x[(size_t) best])) best = i;
                return (double) (best - 100) / SR;
            };
            double worstL = 0, firstR = peakNear (r, 100 + step) - 0.25, worstR = 0;
            for (int k = 1; k <= 5; ++k)
            {
                worstL = std::max (worstL, std::abs (peakNear (l, 100 + k * step) - 0.25 * k));
                worstR = std::max (worstR, std::abs (peakNear (r, 100 + k * step) - 0.25 * k - firstR));
            }
            check (worstL < 0.0005 && worstR < 0.0005 && firstR >= 0 && firstR < 0.0125, "stereo delay repeats stay on the beat",
                   "left off by up to " + String (worstL * 1000, 2) + " ms, right offset " + String (firstR * 1000, 2) + " ms, drift " + String (worstR * 1000, 2) + " ms");
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

        // Reverb types must sound clearly different: compare their impulse responses.
        {
            const int numModes = (int) def (Type::reverb).modes.size();
            std::vector<std::vector<double>> env ((size_t) numModes);   // energy envelope in dB, 20 ms bins, 0..2 s
            std::vector<double> early ((size_t) numModes), tail ((size_t) numModes), bright ((size_t) numModes);
            String table;
            for (int m = 0; m < numModes; ++m)
            {
                auto rv = makeModule (Type::reverb, 1, SR);
                setMode (rv, m);
                rv.norm[6] = 1.0f;   // 100% wet
                std::vector<float> imp ((size_t) (SR * 2.0), 0.0f);
                imp[0] = 1.0f;
                auto out = runFx (rv, imp);
                const size_t bin = (size_t) (SR * 0.02);
                for (size_t b0 = 0; b0 + bin <= out.size(); b0 += bin)
                    env[(size_t) m].push_back (20.0 * std::log10 (level (out, b0, b0 + bin) + 1e-9));
                early[(size_t) m] = 20.0 * std::log10 (level (out, 0, (size_t) (SR * 0.08)) + 1e-9);
                tail[(size_t) m] = 20.0 * std::log10 (level (out, (size_t) (SR * 0.6), (size_t) (SR * 1.6)) + 1e-9);
                double diff = 0, total = 0;   // brightness: energy of the difference signal vs the signal
                for (size_t i = (size_t) (SR * 0.1); i < (size_t) (SR * 0.8); ++i)
                {
                    diff += (double) (out[i] - out[i - 1]) * (out[i] - out[i - 1]);
                    total += (double) out[i] * out[i];
                }
                bright[(size_t) m] = 10.0 * std::log10 (diff / (total + 1e-12) + 1e-12);
                table << def (Type::reverb).modes[(size_t) m] << " " << roundToInt (early[(size_t) m]) << "/" << roundToInt (tail[(size_t) m])
                      << "/" << roundToInt (bright[(size_t) m]) << "  ";
            }
            double minDist = 1e9;
            String closest;
            for (int a2 = 0; a2 < numModes; ++a2)
                for (int b2 = a2 + 1; b2 < numModes; ++b2)
                {
                    double d = 0;
                    const size_t nb = std::min (env[(size_t) a2].size(), env[(size_t) b2].size());
                    for (size_t k = 0; k < nb; ++k)
                        d += std::abs (env[(size_t) a2][k] - env[(size_t) b2][k]);
                    d = d / (double) nb + std::abs (bright[(size_t) a2] - bright[(size_t) b2]) * 2.0;
                    if (d < minDist)
                    {
                        minDist = d;
                        closest = String (def (Type::reverb).modes[(size_t) a2]) + " vs " + def (Type::reverb).modes[(size_t) b2];
                    }
                }
            std::printf ("      reverb early/tail dB/brightness: %s\n", table.toRawUTF8());
            check (minDist > 3.0 && tail[3] < tail[1] - 15.0 && tail[4] > tail[1],
                   "the 7 reverb types sound clearly different", "closest pair " + closest + ": " + String (minDist, 1));
        }

        // Shimmer with a long held note: the octave-up feedback must not pile up into a harsh,
        // ever-brighter whistle. Compare the top-end share and the level early vs. late.
        {
            String report;
            bool ok = true;
            for (float decayNorm : { 0.4f, 0.75f, 1.0f })
            {
                auto rv = makeModule (Type::reverb, 1, SR);
                setMode (rv, 6);   // Shimmer
                rv.norm[1] = decayNorm;
                rv.norm[6] = 1.0f;   // wet only
                auto in = tone (220, 15.0, 0.3f);   // a note held for 15 seconds
                auto out = runFx (rv, in);
                auto hfShare = [&] (double from, double to)
                {
                    // energy of the 2nd difference (strongly weights high frequencies) vs. the signal
                    double hf = 0, all = 0;
                    for (size_t i = (size_t) (from * SR) + 2; i < (size_t) (to * SR); ++i)
                    {
                        const double d2 = (double) out[i] - 2.0 * out[i - 1] + out[i - 2];
                        hf += d2 * d2;
                        all += (double) out[i] * out[i];
                    }
                    return 10.0 * std::log10 (hf / (all + 1e-12) + 1e-12);
                };
                const double hfEarly = hfShare (1.0, 3.0), hfLate = hfShare (12.0, 15.0);
                const double lvEarly = level (out, (size_t) (SR * 1.0), (size_t) (SR * 3.0));
                const double lvLate = level (out, (size_t) (SR * 12.0), (size_t) (SR * 15.0));
                const bool good = hfLate < hfEarly + 3.0 && lvLate < lvEarly * 2.0 && lvLate < 1.0;
                ok = ok && good;
                report << "decay " << String (rv.value (1), 1) << "s: top " << String (hfEarly, 1) << ">" << String (hfLate, 1)
                       << " dB, level " << String (lvEarly, 3) << ">" << String (lvLate, 3) << (good ? "" : " BAD") << "  ";
            }
            check (ok, "shimmer stays smooth with a long held note", report);
        }

        // Preset names sort like the Windows file explorer: no case, numbers by value.
        {
            const StringArray want { "EMX - A001 - BASS 001", "EMX - A001 - BASS 002", "EMX - A001 - BASS 010", "EMX - B001 - KEYS 001",
                                     "emx - b002 - keys 001", "Pad", "Pad 2", "Pad 10", "pad 11", "Strings" };
            StringArray got;
            for (int i : { 7, 3, 9, 0, 5, 8, 2, 4, 1, 6 })
                got.add (want[i]);
            std::sort (got.begin(), got.end(), emx::naturalLess);
            check (got == want, "preset names sort like the Windows file explorer", got.joinIntoString (" | "));
        }

        // Every reverb type stays stable at maximum size and decay (Shimmer feeds itself).
        {
            String unstable;
            for (int m = 0; m < (int) def (Type::reverb).modes.size(); ++m)
            {
                auto rv = makeModule (Type::reverb, 1, SR);
                setMode (rv, m);
                rv.norm[0] = 1.0f;   // size
                rv.norm[1] = 1.0f;   // decay 20 s
                rv.norm[6] = 1.0f;
                auto in = tone (330, 0.5, 0.5f);
                in.resize ((size_t) (SR * 8.0), 0.0f);
                auto out = runFx (rv, in);
                const auto peakEarly = level (out, 0, (size_t) (SR * 1.0));
                const auto peakLate = level (out, (size_t) (SR * 7.0), (size_t) (SR * 8.0));
                bool finite = true;
                for (auto x : out) finite = finite && std::isfinite (x);
                if (! finite || peakLate > peakEarly * 1.2)
                    unstable << def (Type::reverb).modes[(size_t) m] << " (" << String (peakLate / (peakEarly + 1e-9), 2) << "x) ";
            }
            check (unstable.isEmpty(), "reverb stays stable at maximum size and decay", unstable);
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

    skinFileTests();

    //== V2: loop directions =========================================================
    {
        // A ramp (value = position / length) played at its own pitch, so the output shows where the voice reads.
        auto ramp = std::make_shared<SampleData>();
        ramp->sampleRate = SR;
        ramp->audio.setSize (1, 4800);
        for (int i = 0; i < 4800; ++i)
            ramp->audio.setSample (0, i, (float) i / 4800.0f);
        auto pos = [&] (LoopDir dir, int at)
        {
            ZoneParams p = ZoneParams::defaultsFor (*ramp);
            p.root = 60; p.keepSpeed = false; p.loopMode = LoopMode::loop; p.loopDir = dir;
            p.loopStart = 1000; p.loopEnd = 2000; p.crossfade = 0; p.attack = 0.001f; p.sustain = 1.0f;
            const auto r = play (ramp, p, 60, 0.1, Interp::linear);
            return r.left[(size_t) at] / 0.5f * 4800.0f;   // headroom 0.5 at full velocity, centre pan
        };
        const bool fwd = near (pos (LoopDir::forward, 2300), 1300, 3) && near (pos (LoopDir::forward, 3300), 1300, 3);
        const bool ping = near (pos (LoopDir::pingPong, 2300), 1699, 4) && near (pos (LoopDir::pingPong, 3300), 1300, 4);
        const bool back = near (pos (LoopDir::backward, 2300), 1699, 4) && pos (LoopDir::backward, 2600) < pos (LoopDir::backward, 2500);
        check (fwd, "loop forward repeats start to end", String (pos (LoopDir::forward, 2300), 0) + " / " + String (pos (LoopDir::forward, 3300), 0));
        check (ping, "loop ping-pong goes back and forth", String (pos (LoopDir::pingPong, 2300), 0) + " / " + String (pos (LoopDir::pingPong, 3300), 0));
        check (back, "loop backward plays end to start", String (pos (LoopDir::backward, 2300), 0));
        // released in "until release": carries on from where it really is, then stops at the end
        ZoneParams p = ZoneParams::defaultsFor (*ramp);
        p.root = 60; p.keepSpeed = false; p.loopMode = LoopMode::untilRelease; p.loopDir = LoopDir::pingPong;
        p.loopStart = 1000; p.loopEnd = 2000; p.attack = 0.001f; p.release = 0.001f; p.sustain = 1.0f;
        const auto r = play (ramp, p, 60, 0.2, Interp::linear, 2600.0 / SR);
        check (r.finite && r.aliveSeconds < 0.15, "a released ping-pong loop finishes cleanly", String (r.aliveSeconds, 3) + " s");
    }

    //== V2: waveform warp =============================================================
    {
        bool bounded = true;
        for (int sh = 0; sh < edit::numWarpShapes; ++sh)
            for (float a : { 0.0f, 0.5f, 1.0f })
                for (float v = -1.0f; v <= 1.0f; v += 0.05f)
                {
                    const float y = edit::warpValue ((edit::WarpShape) sh, v, a);
                    bounded = bounded && std::isfinite (y) && std::abs (y) <= 1.6f;
                }
        check (bounded, "every warp shape stays in range");
        check (near (edit::warpValue (edit::WarpShape::drive, 0.3f, 0.0f), 0.3f, 0.06) && near (edit::warpValue (edit::WarpShape::bend, 0.3f, 0.0f), 0.3f, 1e-6),
               "a warp at 0% leaves the wave nearly as it is");

        auto quiet = sine (SR, 0.5, 220.0, 0.2f);
        quiet->source = "file|x.wav";
        edit::Warp w;
        w.shape = edit::WarpShape::fold; w.amount = 0.8f;
        auto folded = edit::applyWarp (*quiet, w);
        double diff = 0;
        for (int i = 0; i < quiet->length(); ++i)
            diff += std::abs (folded->audio.getSample (0, i) - quiet->audio.getSample (0, i));
        check (folded->length() == quiet->length() && diff / quiet->length() > 0.02 && folded->source == "edited|file|x.wav",
               "fold changes a quiet sample (as much as a loud one) and marks it as edited", String (diff / quiet->length(), 4));
        check (folded->audio.getMagnitude (0, 0, folded->length()) <= 0.2f * 1.01f, "a warp keeps the sample's loudest level");

        edit::Warp part = w;
        part.from = 6000; part.to = 12000;
        auto onlyPart = edit::applyWarp (*quiet, part);
        bool outsideSame = true, insideChanged = false;
        for (int i = 0; i < quiet->length(); ++i)
        {
            const bool same = onlyPart->audio.getSample (0, i) == quiet->audio.getSample (0, i);
            if (i < 6000 || i >= 12000) outsideSame = outsideSame && same;
            else if (i > 6100 && i < 11900 && ! same) insideChanged = true;
        }
        check (outsideSame && insideChanged, "a warp of the loop changes only the loop");
        edit::Warp mix0 = w;
        mix0.mix = 0.0f;
        auto dry = edit::applyWarp (*quiet, mix0);
        check (dry->audio.getSample (0, 1234) == quiet->audio.getSample (0, 1234), "mix 0% keeps the original");
    }

    //== V2: splicer hits ==============================================================
    {
        auto loop = std::make_shared<SampleData>();
        loop->sampleRate = SR;
        loop->audio.setSize (1, (int) SR);
        loop->audio.clear();
        Random rnd (7);
        const double at[] { 0.0, 0.125, 0.25, 0.5, 0.625, 0.75 };
        for (double t : at)
            for (int i = 0; i < 2400; ++i)
                loop->audio.setSample (0, (int) (t * SR) + i, (rnd.nextFloat() * 2.0f - 1.0f) * 0.8f * std::exp (-i / 400.0f));
        const auto hits = edit::findHits (*loop, 0.6f);
        bool placed = hits.size() == 6;
        for (size_t i = 0; placed && i < hits.size(); ++i)
            placed = std::abs (hits[i] - (int) (at[i] * SR)) < 400;
        String where;
        for (auto h : hits) where << String (h / SR, 3) << " ";
        check (placed, "the splicer finds each drum hit", where.trim());
        const auto even = edit::splitEvenly (48000, 8);
        check (even.size() == 8 && even[0] == 0 && even[1] == 6000 && even[7] == 42000, "split evenly into 8");
    }

    //== Emulator X banks (experimental; only when EMX_EXB_DIR points at a folder of them) ==
    if (const auto dir = SystemStats::getEnvironmentVariable ("EMX_EXB_DIR", {}); dir.isNotEmpty())
    {
        const File sp = File (dir).getChildFile ("SP-1200.exb"), vk = File (dir).getChildFile ("Vintage Keys.exb");
        const auto info = exb::readInfo (sp);
        check (exb::isExb (sp) && info.presetNames.size() == 346 && info.presetNames[8] == "SP Kick 1",
               "exb: bank lists its presets", String (info.presetNames.size()) + ", " + info.presetNames[8]);
        auto kick = exb::loadPreset (sp, info.presetEntries[8].getIntValue());
        check (kick.zones.size() == 1 && kick.zones[0].sample != nullptr && kick.zones[0].sample->name == "SP Kick 1"
                   && kick.zones[0].params.loopMode == LoopMode::off && kick.zones[0].sample->sampleRate == 48000.0,
               "exb: drum preset = one one-shot zone with its sample",
               String ((int) kick.zones.size()) + " zones " + kick.errors.joinIntoString ("; "));
        auto keys = exb::loadPreset (vk, 0);
        int looped = 0, minKey = 127, maxKey = 0;
        for (auto& z : keys.zones)
        {
            looped += z.params.loopMode == LoopMode::loop;
            minKey = jmin (minKey, z.params.loKey);
            maxKey = jmax (maxKey, z.params.hiKey);
        }
        std::printf ("  vintage keys preset 0: %s, %d zones, keys %d..%d, %d looped, errors: %s\n", keys.presetName.toRawUTF8(),
                     (int) keys.zones.size(), minKey, maxKey, looped, keys.errors.joinIntoString ("; ").toRawUTF8());
        for (auto& z : keys.zones)
            std::printf ("    %-14s keys %3d-%3d root %3d vel %3d-%3d  %6.0f Hz  %d ch  loop %d %d-%d\n", z.sample->name.toRawUTF8(), z.params.loKey, z.params.hiKey,
                         z.params.root, z.params.loVel, z.params.hiVel, z.sample->sampleRate, z.sample->audio.getNumChannels(), (int) z.params.loopMode,
                         z.params.loopStart, z.params.loopEnd);
        check (keys.presetName == "apo:PlusDXep" && keys.zones.size() >= 20 && minKey == 21 && maxKey == 108 && looped > 0,
               "exb: keyboard preset = zones across the keys, with loops");
        // Planet Earth "wnd:East Winds", voice 1, compared with Emulator X's screens (slow times, tuning, LFOs)
        {
            const File pe = File (dir).getChildFile ("Planet Earth.exb");
            if (exb::isExb (pe))
            {
                auto ew = exb::loadPreset (pe, 156);
                const auto* z = ew.zones.empty() ? nullptr : &ew.zones.front();
                const auto* d = z != nullptr ? z->params.design.get() : nullptr;
                check (d != nullptr && std::abs (z->params.decay - 2.871f) < 0.05f && std::abs (z->params.release - 0.661f) < 0.02f
                           && std::abs (z->params.sustain - 0.961f) < 0.01f && z->params.semitones == -1 && std::abs (z->params.tuneCents - 16.0f) < 0.01f
                           && std::abs (z->params.gainDb + 10.0f) < 0.01f && d->lfo[0].shape == 0 && d->lfo[1].shape == 1
                           && std::abs (d->lfo[0].hz - 3.0f) < 0.01f && std::abs (d->lfo[1].hz - 0.39f) < 0.01f
                           && std::abs (emu::rateSeconds (d->env[1].rate[4]) - 118.71) < 0.5 && std::abs (emu::rateSeconds (d->env[2].rate[2]) - 0.175) < 0.01,
                       "exb: East Winds' envelopes, tuning, volume and LFOs match Emulator X", ew.errors.joinIntoString ("; "));
            }
        }

        // EMX_EXB_RENDER=<folder>: play a little phrase with imported presets, to listen to them
        auto renderPhrase = [] (const LoadResult& preset, const String& name, bool slowChord)
        {
            const auto outDir = SystemStats::getEnvironmentVariable ("EMX_EXB_RENDER", {});
            if (outDir.isEmpty() || preset.zones.empty())
                return;
            auto set = std::make_shared<ZoneSet>();
            int id = 1;
            for (auto& z : preset.zones)
                set->zones.push_back ({ id++, z.sample, z.params });
            SamplerEngine e;
            e.prepare (48000.0);
            e.setZoneSet (set);
            float knobs[16] {};
            for (size_t k = 0; k < preset.macros.size() && k < 16; ++k)
                knobs[k] = preset.macros[k].second / 100.0f;   // MIDI A..P as the plugin passes its macros
            e.setControllers (knobs);
            AudioBuffer<float> buf (2, 48000 * (slowChord ? 12 : 9));
            buf.clear();
            std::vector<std::array<int, 3>> notes;   // note, on, off (in 50 ms steps)
            if (slowChord)
                notes = { { 48, 0, 120 }, { 55, 0, 120 }, { 63, 0, 120 }, { 70, 0, 120 }, { 60, 130, 200 }, { 67, 130, 200 }, { 75, 130, 200 } };
            else
                notes = { { 48, 0, 60 }, { 55, 0, 60 }, { 60, 0, 60 }, { 64, 0, 60 }, { 36, 66, 90 }, { 72, 66, 90 }, { 84, 96, 120 },
                          { 96, 126, 150 }, { 60, 156, 210 }, { 67, 156, 210 }, { 76, 156, 210 } };
            const int N = 120;   // 2400 samples (50 ms) = 20 blocks
            for (int b = 0; (b + 1) * N <= buf.getNumSamples(); ++b)
            {
                if (b % 20 == 0)
                    for (auto& n : notes)
                    {
                        if (b / 20 == n[1]) e.noteOn (n[0], 100);
                        if (b / 20 == n[2]) e.noteOff (n[0]);
                    }
                e.render (buf.getWritePointer (0, b * N), buf.getWritePointer (1, b * N), N);
            }
            buf.applyGain (0.8f / jmax (0.05f, buf.getMagnitude (0, buf.getNumSamples())));
            const File out = File (outDir).getChildFile (name + ".wav");
            out.deleteFile();
            if (auto w = std::unique_ptr<AudioFormatWriter> (WavAudioFormat().createWriterFor (new FileOutputStream (out), 48000.0, 2, 16, {}, 0)))
                w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
            std::printf ("  rendered %s\n", out.getFullPathName().toRawUTF8());
        };
        renderPhrase (keys, "PlusDXep", false);

        // Orbit-3 "amb:Galactic End", compared with Emulator X's own screens
        {
            const File orbit = File (dir).getChildFile ("Orbit-3.exb");
            const auto oi = exb::readInfo (orbit);
            const int idx = oi.presetNames.indexOf ("amb:Galactic End");
            auto ge = exb::loadPreset (orbit, idx >= 0 ? oi.presetEntries[idx].getIntValue() : -1);
            for (auto& z : ge.zones)
                std::printf ("    %-18s keys %3d-%3d root %3d  vol %+5.1f dB pan %+4.0f  A %.3f D %.3f S %.3f R %.3f  fx %s\n", z.sample->name.toRawUTF8(),
                             z.params.loKey, z.params.hiKey, z.params.root, z.params.gainDb, z.params.pan, z.params.attack, z.params.decay,
                             z.params.sustain, z.params.release, JSON::toString (z.fx, true).toRawUTF8());
            const auto* first = ge.zones.empty() ? nullptr : &ge.zones.front().params;
            check (ge.zones.size() == 7 && first != nullptr && std::abs (first->gainDb + 18.0f) < 0.01f && std::abs (first->attack - 0.072f) < 0.01f
                       && std::abs (first->decay - 0.425f) < 0.03f && std::abs (first->sustain - 0.953f) < 0.01f && std::abs (first->release - 0.995f) < 0.02f
                       && std::abs (ge.zones.back().params.gainDb + 12.0f) < 0.01f && ge.zones.back().params.pan < -70.0f,
                   "exb: Galactic End's volume, pan and amp envelope match Emulator X", ge.errors.joinIntoString ("; "));
            renderPhrase (ge, "GalacticEnd", true);
            // saved as an Emulon X preset and loaded again: the E-mu voice comes back
            {
                PresetToSave ps;
                ps.name = ge.presetName;
                ps.zones = ge.zones;
                ps.macros = ge.macros;
                String err;
                const auto bytes = loader::presetToBytes (ps, err);
                auto back = loader::loadPresetFromMemory (bytes, "test", "test");
                const auto* d0 = ge.zones.empty() ? nullptr : ge.zones[0].params.design.get();
                const auto* d1 = back.zones.empty() ? nullptr : back.zones[0].params.design.get();
                check (d0 != nullptr && d1 != nullptr && d1->cords.size() == d0->cords.size() && d1->filterType == 64
                           && std::abs (d1->env[1].rate[0] - d0->env[1].rate[0]) < 0.01f && back.macros.size() == 16
                           && back.macros[7].first == "RATE",
                       "exb: an imported preset saved as .emxp keeps its E-mu voice and knob names", err);
            }
        }


    }

    std::printf ("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
