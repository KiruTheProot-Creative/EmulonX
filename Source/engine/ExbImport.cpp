// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ExbImport.h"
#include "EmuVoice.h"
#include <map>
#include <cmath>
#include <iterator>

namespace emx::exb
{
using namespace juce;

namespace
{
// The files are IFF-style: "FORM" + size + "E5B0", then chunks of 4-letter id + big-endian size + data.
// Chunk sizes are not padded to even lengths. Sample headers inside .ebl files are little-endian.
uint32 be32 (const uint8* p) noexcept { return ((uint32) p[0] << 24) | ((uint32) p[1] << 16) | ((uint32) p[2] << 8) | p[3]; }
uint16 be16 (const uint8* p) noexcept { return (uint16) ((p[0] << 8) | p[1]); }
uint32 le32 (const uint8* p) noexcept { return ((uint32) p[3] << 24) | ((uint32) p[2] << 16) | ((uint32) p[1] << 8) | p[0]; }

struct Chunk
{
    String id;
    size_t data = 0, size = 0;   // where the chunk's data starts, and its length
    String listType;             // for LIST chunks
    std::vector<Chunk> kids;
};

std::vector<Chunk> parse (const MemoryBlock& mb, size_t off, size_t end, int depth = 0)
{
    std::vector<Chunk> out;
    const auto* d = static_cast<const uint8*> (mb.getData());
    end = jmin (end, mb.getSize());
    while (off + 8 <= end)
    {
        Chunk c;
        c.id = String::fromUTF8 (reinterpret_cast<const char*> (d + off), 4);
        c.size = be32 (d + off + 4);
        c.data = off + 8;
        if (c.data + c.size > end)
            break;
        if (depth < 8 && c.id == "LIST" && c.size >= 4)
        {
            c.listType = String::fromUTF8 (reinterpret_cast<const char*> (d + c.data), 4).trim();
            c.kids = parse (mb, c.data + 4, c.data + c.size, depth + 1);
        }
        else if (depth < 8 && c.id == "E5V1")
            c.kids = parse (mb, c.data, c.data + c.size, depth + 1);
        off = c.data + c.size;
        out.push_back (std::move (c));
    }
    return out;
}

const Chunk* list (const std::vector<Chunk>& cs, const char* type)
{
    for (auto& c : cs)
        if (c.id == "LIST" && c.listType == type)
            return &c;
    return nullptr;
}

String utf16Name (const uint8* p, int bytes)
{
    String s;
    for (int i = 0; i + 1 < bytes; i += 2)
    {
        const auto ch = (juce_wchar) (p[i] | (p[i + 1] << 8));
        if (ch == 0) break;
        s += String::charToString (ch);
    }
    return s.trim();
}

struct TocEntry { String id, name; uint32 size = 0, offset = 0; int index = 0; };

std::vector<TocEntry> readToc (const MemoryBlock& mb, String& error)
{
    std::vector<TocEntry> toc;
    const auto* d = static_cast<const uint8*> (mb.getData());
    if (mb.getSize() < 20 || memcmp (d, "FORM", 4) != 0 || memcmp (d + 8, "E5B0", 4) != 0 || memcmp (d + 12, "TOC", 3) != 0)
    {
        error = "not an Emulator X bank";
        return toc;
    }
    const size_t size = be32 (d + 16);
    for (size_t i = 20; i + 78 <= jmin ((size_t) mb.getSize(), 20 + size); i += 78)
    {
        TocEntry e;
        e.id = String::fromUTF8 (reinterpret_cast<const char*> (d + i), 4);
        e.size = be32 (d + i + 4);
        e.offset = be32 (d + i + 8);
        e.index = be16 (d + i + 12);
        e.name = utf16Name (d + i + 14, 64);
        toc.push_back (e);
    }
    return toc;
}

/** A key or velocity window: low .. high (the two bytes between are fades, not used yet). */
bool window (const Chunk& etw, const MemoryBlock& mb, int& lo, int& hi)
{
    if (etw.id != "ETW " || etw.size < 8)
        return false;
    const auto* p = static_cast<const uint8*> (mb.getData()) + etw.data;
    lo = jlimit (0, 127, (int) p[4]);
    hi = jlimit (lo, 127, (int) p[7]);
    return true;
}

void windows (const Chunk* twl, const MemoryBlock& mb, int& loKey, int& hiKey, int& loVel, int& hiVel)
{
    if (twl == nullptr)
        return;
    int n = 0;
    for (auto& w : twl->kids)
    {
        int lo, hi;
        if (! window (w, mb, lo, hi))
            continue;
        if (n == 0)      { loKey = jmax (loKey, lo); hiKey = jmin (hiKey, hi); }
        else if (n == 1) { loVel = jmax (loVel, jmax (1, lo)); hiVel = jmin (hiVel, hi); }
        ++n;
    }
}
} // namespace

//== Voice settings: amp, envelope, filter ==========================================
// Worked out by comparing the files with what Emulator X shows (Orbit-3 "amb:Galactic End").
namespace
{
/** E-mu envelope rate (0..127, stored as rate / 127) -> seconds for a full 0..100 % move.
    Measured points from Emulator X, joined smoothly (log scale) in between. */
double rateSeconds (double rate) { return emu::rateSeconds (rate); }

struct Env { float time[6] {}, level[6] {}; bool valid = false; };

Env readEnv (const Chunk& ev, const MemoryBlock& mb)
{
    // E5Ev: 4-byte version, 5 bytes (mode...), then 6 stages of: 1 byte, time (rate / 127, float), level (%, float)
    Env e;
    if (ev.id != "E5Ev" || ev.size < 4 + 5 + 6 * 9)
        return e;
    const auto* p = static_cast<const uint8*> (mb.getData()) + ev.data + 4 + 5;
    for (int i = 0; i < 6; ++i)
    {
        const auto* q = p + i * 9 + 1;
        uint32 t = be32 (q), l = be32 (q + 4);
        float tf, lf;
        memcpy (&tf, &t, 4); memcpy (&lf, &l, 4);
        e.time[i] = std::isfinite (tf) ? jlimit (0.0f, 1.0f, tf) : 0.0f;
        e.level[i] = std::isfinite (lf) ? jlimit (-100.0f, 100.0f, lf) : 0.0f;
    }
    e.valid = true;
    return e;
}

/** Stages 0..5 = attack 1, attack 2, decay 1, decay 2, release 1, release 2. Attack and decay times
    depend on how far the level moves; release times are as shown in Emulator X. */
void ampEnvelope (const Env& e, ZoneParams& p)
{
    auto seg = [&] (int i, float from) { return (float) (rateSeconds (e.time[i] * 127.0) * std::abs (e.level[i] - from) / 100.0); };
    const float l1 = e.level[0], l2 = e.level[1], l3 = e.level[2], l4 = e.level[3];
    p.attack = jmax (0.001f, seg (0, 0.0f) + seg (1, l1));
    const float peak = jmax (1.0f, jmax (l1, l2));
    p.decay = jmax (0.001f, seg (2, l2) + seg (3, l3));
    p.sustain = jlimit (0.0f, 1.0f, l4 / peak);
    p.release = jmax (0.005f, (float) (rateSeconds (e.time[4] * 127.0) + (e.level[4] > 0.5f ? rateSeconds (e.time[5] * 127.0) : 0.0)));
}

float beFloat (const uint8* p) { const uint32 u = be32 (p); float f; memcpy (&f, &u, 4); return std::isfinite (f) ? f : 0.0f; }

/** The voice's filter as an Emulon X filter effect (only the classic types so far). */
var filterFx (const Chunk& fl, const MemoryBlock& mb)
{
    if (fl.id != "E5Fl" || fl.size < 13)
        return {};
    const auto* p = static_cast<const uint8*> (mb.getData()) + fl.data + 4;
    const int type = p[0];
    const float cut = jlimit (0.0f, 1.0f, beFloat (p + 1)), res = jlimit (0.0f, 1.0f, beFloat (p + 5));
    // E-mu's filter numbers: 0-2 low pass (4, 2, 6 pole), 8-9 high pass, 16-18 band pass, 127 off
    int mode = -1, slope = 1;
    if (type <= 2)                  { mode = 0; slope = type == 1 ? 0 : 1; }
    else if (type == 8 || type == 9) { mode = 1; slope = type == 8 ? 0 : 1; }
    else if (type >= 16 && type <= 18) { mode = 2; slope = type == 16 ? 0 : 1; }
    if (mode < 0 || (mode == 0 && cut >= 0.999f && res < 0.01f))
        return {};   // off, wide open, or a type Emulon X doesn't have yet
    const double hz = 57.0 * std::pow (20000.0 / 57.0, (double) cut);   // E-mu's range, approximate until checked
    auto* settings = new DynamicObject();
    settings->setProperty ("mode", mode);
    settings->setProperty ("slope", slope);
    auto* params = new DynamicObject();
    params->setProperty ("cut", jlimit (20.0, 20000.0, hz));
    params->setProperty ("res", res * 0.9f);
    params->setProperty ("drive", 0.0f);
    params->setProperty ("mix", 1.0f);
    auto* m = new DynamicObject();
    m->setProperty ("type", "filter");
    m->setProperty ("on", true);
    m->setProperty ("settings", var (settings));
    m->setProperty ("params", var (params));
    Array<var> chain;
    chain.add (var (m));
    return chain;
}
} // namespace

/** Everything about a voice's sound for Emulon X's E-mu voice layer. */
std::shared_ptr<const emu::Design> readDesign (const Chunk& v, const MemoryBlock& mb)
{
    const auto* d = static_cast<const uint8*> (mb.getData());
    auto des = std::make_shared<emu::Design>();
    for (auto& c : v.kids)
    {
        if (c.id == "LIST" && c.listType == "EvL")
        {
            int e = 0;
            for (auto& ev : c.kids)
                if (ev.id == "E5Ev" && e < 3)
                {
                    const auto env = readEnv (ev, mb);
                    for (int i = 0; i < 6; ++i)
                    {
                        des->env[(size_t) e].rate[(size_t) i] = env.time[i] * 127.0f;   // stored as rate / 127
                        des->env[(size_t) e].level[(size_t) i] = env.level[i];
                    }
                    ++e;
                }
        }
        else if (c.id == "E5Fl" && c.size >= 13)
        {
            des->filterType = d[c.data + 4];
            des->cutoff = jlimit (0.0f, 1.0f, beFloat (d + c.data + 5));
            des->resonance = jlimit (0.0f, 1.0f, beFloat (d + c.data + 9));
        }
        else if (c.id == "LIST" && c.listType == "LFOL")
        {
            int i = 0;
            for (auto& l : c.kids)
                if (l.id == "E5LF" && l.size >= 20 && i < 2)
                {
                    // 4-byte version, shape, 3 more bytes, rate (Hz, float), tempo division, ?, variation (%, float), ?, key sync
                    const auto* q = d + l.data;
                    auto& f = des->lfo[(size_t) i++];
                    // shape byte (Emulator X: 0 triangle, 1 sine, then saw, square, random...) -> Emulon X's LFO codes
                    static const int shapes[] { 1, 0, 3, 2, 4 };
                    f.shape = q[13] < 5 ? shapes[q[13]] : 0;
                    f.hz = jlimit (0.0f, 50.0f, beFloat (q + 8));
                    f.division = q[12];
                    f.sync = q[18] == 0;          // 0 = BPM on
                    f.variation = jlimit (0.0f, 100.0f, beFloat (q + 14));
                    f.keySync = q[19] != 0;
                }
        }
        else if (c.id == "E5MP" && c.size >= 12)
            des->lagRate = { beFloat (d + c.data + 4), beFloat (d + c.data + 8) };
        else if (c.id == "LIST" && c.listType == "CrdL")
        {
            for (auto& cd : c.kids)
                if (cd.id == "E5Cd" && cd.size >= 10 && des->cords.size() < 36)
                    des->cords.push_back ({ d[cd.data + 4], d[cd.data + 5], jlimit (-100.0f, 100.0f, beFloat (d + cd.data + 6)) });
        }
        else if (c.id == "E5Oc" && c.size >= 35)
        {
            des->transpose = (int8) d[c.data + 14];
            des->coarse = (int8) d[c.data + 15];
            des->fine = jlimit (-100.0f, 100.0f, beFloat (d + c.data + 16));
            des->chorusWidth = jlimit (0.0f, 100.0f, beFloat (d + c.data + 27));
            des->chorusAmount = jlimit (0.0f, 100.0f, beFloat (d + c.data + 31));
        }
    }
    return des;
}

//== Effects ==========================================================================
namespace
{
var fxModule (const char* type, int mode, std::initializer_list<std::pair<const char*, double>> params)
{
    auto* st = new DynamicObject();
    st->setProperty ("mode", mode);
    auto* pr = new DynamicObject();
    for (auto& p : params)
        pr->setProperty (p.first, p.second);
    auto* m = new DynamicObject();
    m->setProperty ("type", type);
    m->setProperty ("on", true);
    m->setProperty ("settings", var (st));
    m->setProperty ("params", var (pr));
    return var (m);
}

/** FX B (E-mu's chorus / flanger / delay / distortion list): type, feedback, LFO rate, delay. */
void addFxB (Array<var>& chain, int t, int feedback, int lfoRate, int delay)
{
    const double rate = 0.05 * std::pow (2.0, lfoRate / 127.0 * 7.0);   // ~0.05 .. 6 Hz
    const double fb = feedback / 127.0;
    if ((t >= 1 && t <= 5) || t == 15 || t == 16 || t == 17)
        chain.add (fxModule ("chorus", t == 17 ? 2 : 0, { { "rate", rate }, { "depth", t == 15 || t == 16 ? 0.7 : 0.5 }, { "a", 12.0 + t }, { "b", 1.0 }, { "mix", 0.45 } }));
    else if (t == 6 || t == 7)
        chain.add (fxModule ("delay", 0, { { "time", t == 6 ? 35.0 : 110.0 }, { "fb", 0.0 }, { "a", 0.6 }, { "b", 0.0 }, { "lo", 80.0 }, { "hi", 9000.0 }, { "mix", 0.35 } }));
    else if (t >= 8 && t <= 14)
        chain.add (fxModule ("chorus", 1, { { "rate", rate }, { "depth", 0.6 }, { "a", 1.0 + (t - 8) * 0.6 }, { "b", 0.2 + fb * 0.7 }, { "mix", 0.5 } }));
    else if (t >= 18 && t <= 26)
        chain.add (fxModule ("delay", t == 21 || t == 23 || t == 24 ? 1 : 0,
                             { { "time", jlimit (20.0, 2000.0, delay > 0 ? delay * 5.0 : 300.0) }, { "fb", fb * 0.85 }, { "a", 0.6 }, { "b", 0.0 },
                               { "lo", 80.0 }, { "hi", 8000.0 }, { "mix", 0.3 } }));
    else if (t == 27)
        chain.add (fxModule ("chorus", 0, { { "rate", rate }, { "depth", 0.4 }, { "a", 8.0 }, { "b", 1.0 }, { "mix", 1.0 } }));
    else if (t >= 28 && t <= 32)
    {
        chain.add (fxModule ("dist", 2, { { "drive", 18.0 }, { "a", 0.5 }, { "b", 0.0 }, { "tone", 9000.0 }, { "out", -8.0 }, { "mix", 1.0 } }));
        if (t >= 30)
            chain.add (fxModule ("chorus", t == 30 ? 1 : 0, { { "rate", rate }, { "depth", 0.5 }, { "a", t == 30 ? 2.0 : 15.0 }, { "b", t == 30 ? 0.5 : 1.0 }, { "mix", 0.5 } }));
    }
}

/** FX A (E-mu's reverb / delay list): type, decay (0..90), high damping (0..127). */
void addFxA (Array<var>& chain, int t, int decay, int damping)
{
    const double damp = 20000.0 * std::pow (2.0, -damping / 127.0 * 4.0);
    const double d = decay / 90.0;
    auto reverb = [&] (int mode, double size, double seconds)
    {
        chain.add (fxModule ("reverb", mode, { { "size", size }, { "decay", jlimit (0.2, 15.0, seconds) }, { "pre", 12.0 }, { "damp", jlimit (1000.0, 20000.0, damp) },
                                               { "lo", 100.0 }, { "width", 1.0 }, { "mix", 0.25 } }));
    };
    if (t == 1 || t == 2 || t == 3 || (t >= 13 && t <= 16))   reverb (0, 0.3 + 0.05 * (t % 4), 0.3 + d * 2.0);       // rooms
    else if (t == 6 || t == 17 || t == 22)                      reverb (2, 0.5, 0.6 + d * 3.5);                      // plates
    else if (t == 23 || t == 24)                                reverb (3, 0.6, 0.6 + d * 3.0);                      // court, gym
    else if (t == 25 || t == 35)                                reverb (4, 0.95, 2.0 + d * 10.0);                    // cavern, canyon
    else if (t >= 28 && t <= 30)                                reverb (0, 0.4, 0.3 + d * 0.6);                      // gated
    else if (t >= 7 && t <= 12)
        chain.add (fxModule ("delay", t % 2 == 0 ? 1 : 0, { { "time", 350.0 }, { "fb", d * 0.75 }, { "a", 0.6 }, { "b", 0.0 }, { "lo", 80.0 },
                                                            { "hi", jlimit (1000.0, 20000.0, damp) }, { "mix", 0.25 } }));
    else if (t >= 36 && t <= 44)
    {
        chain.add (fxModule ("delay", t % 2 == 1 ? 0 : 1, { { "time", 300.0 }, { "fb", 0.35 }, { "a", 0.6 }, { "b", 0.0 }, { "lo", 80.0 }, { "hi", 7000.0 }, { "mix", 0.2 } }));
        reverb (1, 0.7, 1.0 + d * 4.0);
    }
    else if (t > 0)                                             reverb (1, 0.75, 1.0 + d * 6.0);                     // halls and concerts
}
} // namespace

//== Files ===========================================================================
File bankFile (const File& f)
{
    if (f.isDirectory())
        return f.getChildFile (f.getFileName());   // "SP-1200.exb\SP-1200.exb"
    return f;
}

bool isExb (const File& f)
{
    return f.hasFileExtension ("exb") && bankFile (f).existsAsFile();
}

BankInfo readInfo (const File& f)
{
    BankInfo b;
    const auto file = bankFile (f);
    b.name = file.getFileNameWithoutExtension();
    MemoryBlock mb;
    if (! file.loadFileAsData (mb))
    {
        b.error = file.getFileName() + ": can't be read";
        return b;
    }
    String error;
    for (auto& e : readToc (mb, error))
        if (e.id == "E5P1")
        {
            b.presetNames.add (e.name);
            b.presetEntries.add (String (e.index));
        }
    if (error.isNotEmpty())
        b.error = file.getFileName() + ": " + error;
    return b;
}

//== Samples =========================================================================
std::shared_ptr<SampleData> loadSample (const File& ebl, String& error, bool* loops)
{
    MemoryBlock mb;
    if (! ebl.loadFileAsData (mb))
    {
        error = "Missing sample: " + ebl.getFullPathName();
        return {};
    }
    const auto* d = static_cast<const uint8*> (mb.getData());
    const size_t n = mb.getSize();
    if (n < 24 || memcmp (d, "FORM", 4) != 0 || memcmp (d + 12, "TOC", 3) != 0)
    {
        error = ebl.getFileName() + ": not an Emulator X sample";
        return {};
    }
    const size_t chunk = 20 + be32 (d + 16);
    if (chunk + 8 + 6 + 64 + 48 > n || memcmp (d + chunk, "E5S1", 4) != 0)
    {
        error = ebl.getFileName() + ": no sample inside";
        return {};
    }
    // E5S1: 2-byte number, 4 more bytes, then the name (64 bytes, UTF-16) and the header.
    // All positions below are byte offsets counted from where the name starts.
    const size_t base = chunk + 8 + 6;
    const auto* h = d + base + 64;
    const uint32 startL = le32 (h + 4), startR = le32 (h + 8), endL = le32 (h + 12), endR = le32 (h + 16);
    const uint32 loopL = le32 (h + 20), loopEndL = le32 (h + 28);
    const uint32 rate = le32 (h + 36);
    const uint8* flags = h + 44;   // [0] 0 or 1 (both plain 16-bit), [1] 1 = no loop, [2] 1 = left, 2 = right, 3 = stereo
    const bool stereo = flags[2] == 3;
    if (flags[0] > 1)
    {
        error = ebl.getFileName() + ": sample format not supported yet";
        return {};
    }
    // the end is where the data stops
    auto frames = [&] (uint32 a, uint32 b)
    {
        if (b < a || base + a >= n) return 0;
        return (int) jmin ((size_t) (b - a) / 2, (n - base - a) / 2);   // the end points at the last sample: leave it out (it is often not audio)
    };
    const int len = stereo ? jmin (frames (startL, endL), frames (startR, endR)) : frames (startL, endL);
    if (len <= 0)
    {
        error = ebl.getFileName() + ": sample data is cut off";
        return {};
    }

    auto s = std::make_shared<SampleData>();
    s->name = utf16Name (d + base, 64);
    if (s->name.isEmpty()) s->name = ebl.getFileNameWithoutExtension();
    s->source = "ebl|" + ebl.getFullPathName();
    s->sampleRate = rate > 1000 ? (double) rate : 44100.0;
    s->audio.setSize (stereo ? 2 : 1, len);
    for (int c = 0; c < s->audio.getNumChannels(); ++c)
    {
        const auto* src = d + base + (c == 0 ? startL : startR);
        auto* dst = s->audio.getWritePointer (c);
        for (int i = 0; i < len; ++i)
            dst[i] = (float) (int16) (src[2 * i] | (src[2 * i + 1] << 8)) / 32768.0f;
    }
    const bool hasLoop = flags[1] == 0 && loopEndL > loopL && loopL >= startL;
    if (hasLoop)
    {
        s->loopStart = (int) ((loopL - startL) / 2);
        s->loopEnd = jmin (len, (int) ((loopEndL - startL) / 2 + 1));
    }
    if (loops != nullptr)
        *loops = hasLoop;
    return s;
}

//== Presets =========================================================================
LoadResult loadPreset (const File& bankIn, int presetNumber)
{
    LoadResult r;
    r.isPreset = true;
    const auto file = bankFile (bankIn);
    MemoryBlock mb;
    if (! file.loadFileAsData (mb))
    {
        r.errors.add (file.getFileName() + ": can't be read");
        return r;
    }
    String error;
    const auto toc = readToc (mb, error);
    if (error.isNotEmpty())
    {
        r.errors.add (file.getFileName() + ": " + error);
        return r;
    }
    const auto* d = static_cast<const uint8*> (mb.getData());

    // sample number -> number of its .ebl file in SamplePool
    std::map<int, int> sampleFiles;
    const TocEntry* preset = nullptr;
    for (auto& e : toc)
    {
        if (e.id == "E5SL" && e.offset + 14 <= mb.getSize())
            sampleFiles[e.index] = (int) be32 (d + e.offset + 10);
        if (e.id == "E5P1" && e.index == presetNumber)
            preset = &e;
    }
    if (preset == nullptr || preset->offset + 10 + preset->size > mb.getSize() + 2)
    {
        r.errors.add (file.getFileName() + ": preset " + String (presetNumber) + " not found");
        return r;
    }
    r.presetName = preset->name;
    r.interp = (int) Interp::sinc;

    const auto pool = file.getParentDirectory().getChildFile ("SamplePool");
    const auto stem = file.getFileNameWithoutExtension();
    auto eblFor = [&] (int sampleNumber)
    {
        const int num = sampleFiles.count (sampleNumber) ? sampleFiles[sampleNumber] : sampleNumber;
        auto f = pool.getChildFile (stem + "SL" + String (num).paddedLeft ('0', 3) + ".ebl");
        if (! f.existsAsFile())
            for (const auto& e : RangedDirectoryIterator (pool, false, "*SL*" + String (num) + ".ebl"))
                if (e.getFile().getFileNameWithoutExtension().fromLastOccurrenceOf ("SL", false, false).getIntValue() == num)
                    return e.getFile();
        return f;
    };

    // E5P1: 2-byte number, then the preset's chunks (the chunk's own size is 2 more than the table says)
    const size_t presetEnd = preset->offset + 8 + jmax ((size_t) preset->size, (size_t) be32 (d + preset->offset + 4));
    const auto chunks = parse (mb, preset->offset + 10, presetEnd);
    // MIDI A..P: their names and starting values become Emulon X's 16 macros
    for (auto& c : chunks)
        if (c.id == "E5IC" && c.size >= 20)
        {
            const Chunk* names = nullptr;
            for (auto& n : chunks)
                if (n.id == "E5CL") names = &n;
            static const char* letters = "ABCDEFGHIJKLMNOP";
            for (int i = 0; i < 16; ++i)
            {
                String name = names != nullptr && names->size >= (size_t) (4 + 32 * (size_t) (i + 1)) ? utf16Name (d + names->data + 4 + 32 * i, 32) : String();
                if (name.isEmpty()) name = String ("MIDI ") + letters[i];
                const int value = d[c.data + 4 + (size_t) i];   // 255 = not set
                r.macros.push_back ({ name.toUpperCase(), value > 127 ? 0.0f : (float) value * 100.0f / 127.0f });
            }
        }
    const auto* voices = list (chunks, "E5VL");
    if (voices == nullptr)
    {
        r.errors.add (preset->name + ": no voices found");
        return r;
    }
    std::map<String, std::pair<std::shared_ptr<const SampleData>, bool>> cache;
    int missing = 0;
    for (auto& v : voices->kids)
    {
        if (v.id != "E5V1")
            continue;
        int vLoKey = 0, vHiKey = 127, vLoVel = 1, vHiVel = 127;
        windows (list (v.kids, "TWL"), mb, vLoKey, vHiKey, vLoVel, vHiVel);
        // the voice's sound: volume (dB) and pan, amp envelope, filter
        float gainDb = 0.0f, pan = 0.0f;
        Env amp;
        var fx;
        const auto design = readDesign (v, mb);
        for (auto& c : v.kids)
        {
            if (c.id == "E5Am" && c.size >= 9)
            {
                gainDb = jlimit (-48.0f, 24.0f, beFloat (d + c.data + 4));
                pan = jlimit (-100.0f, 100.0f, (float) (int8) d[c.data + 8] * 100.0f / 64.0f);   // E-mu pan -64..+63
            }
            if (c.id == "E5Fl")
                fx = filterFx (c, mb);
            if (c.id == "LIST" && c.listType == "EvL")
                for (auto& ev : c.kids)
                    if (ev.id == "E5Ev") { amp = readEnv (ev, mb); break; }   // the first one is the amp envelope
        }
        const auto* zl = list (v.kids, "E5ZL");
        if (zl == nullptr)
            continue;
        // zones: a Zhdr, then that zone's own key / velocity windows
        for (size_t i = 0; i < zl->kids.size(); ++i)
        {
            const auto& z = zl->kids[i];
            if (z.id != "Zhdr" || z.size < 12)
                continue;
            const auto* zh = d + z.data;
            const int sampleNumber = be16 (zh + 4);
            if (sampleNumber == 0)
                continue;   // a zone without a sample (Emulator X keeps these as placeholders)
            const int root = jlimit (0, 127, (int) zh[10]);
            int loKey = vLoKey, hiKey = vHiKey, loVel = vLoVel, hiVel = vHiVel;
            if (i + 1 < zl->kids.size() && zl->kids[i + 1].id == "LIST" && zl->kids[i + 1].listType == "TWL")
                windows (&zl->kids[i + 1], mb, loKey, hiKey, loVel, hiVel);
            if (loKey > hiKey || loVel > hiVel)
                continue;   // no key / velocity left for this zone

            const auto ebl = eblFor (sampleNumber);
            if (! ebl.existsAsFile())
            {
                ++missing;   // some sound sets refer to samples they don't include
                continue;
            }
            auto& entry = cache[ebl.getFullPathName()];
            if (entry.first == nullptr)
            {
                String e;
                bool loops = false;
                entry.first = loadSample (ebl, e, &loops);
                entry.second = loops;
                if (entry.first == nullptr)
                {
                    r.errors.addIfNotAlreadyThere (e);
                    continue;
                }
            }
            LoadedZone lz;
            lz.sample = entry.first;
            auto& p = lz.params;
            p = ZoneParams::defaultsFor (*lz.sample);
            p.root = root;
            p.loKey = loKey; p.hiKey = hiKey;
            p.loVel = loVel; p.hiVel = hiVel;
            p.keepSpeed = false;   // classic sampler: pitch changes the speed
            p.attack = 0.001f; p.decay = 0.3f; p.sustain = 1.0f; p.release = 0.3f;
            if (amp.valid)
                ampEnvelope (amp, p);
            p.gainDb = gainDb;
            p.pan = pan;
            p.design = design;   // envelopes, filter, LFOs and cords run per note in Emulon X's E-mu voice layer
            p.semitones = jlimit (-24, 24, design->transpose + design->coarse);
            p.tuneCents = design->fine;
            lz.fx = var();       // (so no separate filter effect)
            (void) fx;
            if (entry.second)
            {
                p.loopMode = LoopMode::loop;
                p.loopStart = lz.sample->loopStart;
                p.loopEnd = lz.sample->loopEnd;
            }
            else
                p.loopMode = LoopMode::off;
            p.clampTo (lz.sample->length());
            r.zones.push_back (std::move (lz));
        }
    }
    // the preset's effects: the voices' chorus, then FX B (chorus / flanger / delay), then FX A (reverb / delay)
    {
        Array<var> chain;
        float chorus = 0.0f;
        for (auto& z : r.zones)
            if (z.params.design != nullptr)
                chorus = jmax (chorus, z.params.design->chorusAmount);
        if (chorus > 0.5f)
            chain.add (fxModule ("chorus", 2, { { "rate", 0.6 }, { "depth", jlimit (0.1, 0.9, chorus / 25.0) }, { "a", 12.0 }, { "b", 0.9 },
                                                { "mix", jlimit (0.1, 0.5, chorus / 30.0) } }));
        if (const auto* ael = list (chunks, "AEL"))
        {
            std::vector<const Chunk*> slots;
            for (auto& c : ael->kids)
                if (c.id == "E5E1" && c.size >= 9)
                    slots.push_back (&c);
            if (slots.size() >= 2)
            {
                const auto* b = d + slots[1]->data;
                addFxB (chain, b[4], b[5], b[6], b[7]);
            }
            if (! slots.empty())
            {
                const auto* a = d + slots[0]->data;
                addFxA (chain, a[4], a[5], a[6]);
            }
        }
        if (! chain.isEmpty())
        {
            auto* fxo = new DynamicObject();
            fxo->setProperty ("chain", chain);
            r.fx = var (fxo);
        }
    }
    if (r.zones.empty() && r.errors.isEmpty())
        r.errors.add (preset->name + ": no playable zones" + (missing > 0 ? " (its samples aren't in this sound set)" : ""));
    return r;
}
} // namespace emx::exb
