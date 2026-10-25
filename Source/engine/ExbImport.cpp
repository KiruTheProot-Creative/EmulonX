// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ExbImport.h"
#include <map>

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
    const uint8* flags = h + 44;   // [1] 1 = no loop, [2] 1 = left, 2 = right, 3 = stereo
    const bool stereo = flags[2] == 3;
    if (flags[0] != 1)
    {
        error = ebl.getFileName() + ": sample format not supported yet";
        return {};
    }
    // the end is where the data stops (some files have one more sample, some don't: take what is there)
    auto frames = [&] (uint32 a, uint32 b)
    {
        if (b < a || base + a >= n) return 0;
        return (int) jmin ((size_t) (b - a) / 2 + 1, (n - base - a) / 2);
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
    const auto* voices = list (chunks, "E5VL");
    if (voices == nullptr)
    {
        r.errors.add (preset->name + ": no voices found");
        return r;
    }
    std::map<String, std::pair<std::shared_ptr<const SampleData>, bool>> cache;
    for (auto& v : voices->kids)
    {
        if (v.id != "E5V1")
            continue;
        int vLoKey = 0, vHiKey = 127, vLoVel = 1, vHiVel = 127;
        windows (list (v.kids, "TWL"), mb, vLoKey, vHiKey, vLoVel, vHiVel);
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
            const int root = jlimit (0, 127, (int) zh[10]);
            int loKey = vLoKey, hiKey = vHiKey, loVel = vLoVel, hiVel = vHiVel;
            if (i + 1 < zl->kids.size() && zl->kids[i + 1].id == "LIST" && zl->kids[i + 1].listType == "TWL")
                windows (&zl->kids[i + 1], mb, loKey, hiKey, loVel, hiVel);
            if (loKey > hiKey || loVel > hiVel)
                continue;   // no key / velocity left for this zone

            const auto ebl = eblFor (sampleNumber);
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
    if (r.zones.empty() && r.errors.isEmpty())
        r.errors.add (preset->name + ": no playable zones");
    return r;
}
} // namespace emx::exb
