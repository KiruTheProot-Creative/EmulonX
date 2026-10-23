// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SampleLoader.h"

namespace emx::loader
{
using namespace juce;

static AudioFormatManager& formats()
{
    static AudioFormatManager fm;
    static const bool registered = [] { fm.registerBasicFormats(); return true; }();
    ignoreUnused (registered);
    return fm;
}

static constexpr int maxSeconds = 600;   // refuse absurdly long files (10 minutes)

String supportedWildcards() { return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.emxp;*.emxb;*.zip"; }

bool isAudioFile (const File& f)  { return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"); }
bool isPresetFile (const File& f) { return f.hasFileExtension ("emxp") || f.getFileName().endsWithIgnoreCase (".emxp.zip"); }
bool isBankFile (const File& f)   { return f.hasFileExtension ("emxb") || f.getFileName().endsWithIgnoreCase (".emxb.zip"); }
bool isSupported (const File& f)  { return isAudioFile (f) || isPresetFile (f) || isBankFile (f) || f.hasFileExtension ("zip"); }

String safeFileName (const String& name)
{
    auto s = name.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ").trim().replaceCharacter (' ', '_');
    return s.isNotEmpty() ? s.substring (0, 60) : String ("untitled");
}

//== Decoding ==================================================================
std::shared_ptr<SampleData> decode (std::unique_ptr<InputStream> stream, const String& name,
                                    const String& source, String& error)
{
    if (stream == nullptr)
    {
        error = name + ": could not be opened";
        return {};
    }
    std::unique_ptr<AudioFormatReader> reader (formats().createReaderFor (std::move (stream)));
    if (reader == nullptr)
    {
        error = name + ": not a supported audio file";
        return {};
    }
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > (int64) (reader->sampleRate * maxSeconds))
    {
        error = name + ": empty or longer than 10 minutes";
        return {};
    }

    auto s = std::make_shared<SampleData>();
    s->name = name;
    s->source = source;
    s->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    const int channels = (int) jlimit<unsigned> (1, 2, reader->numChannels);
    s->audio.setSize (channels, (int) reader->lengthInSamples);
    reader->read (&s->audio, 0, (int) reader->lengthInSamples, 0, true, channels > 1);

    // WAV "smpl" chunk: root note and first loop (JUCE stores the loop end inclusively).
    const auto& meta = reader->metadataValues;
    if (meta.containsKey ("MidiUnityNote") && meta.containsKey ("NumSampleLoops"))
    {
        const int r = meta["MidiUnityNote"].getIntValue();
        if (r >= 0 && r < 128)
            s->rootKey = r;
    }
    if (meta["NumSampleLoops"].getIntValue() > 0)
    {
        const int ls = meta["Loop0Start"].getIntValue(), le = meta["Loop0End"].getIntValue() + 1;
        if (ls >= 0 && le > ls && le <= s->length())
        {
            s->loopStart = ls;
            s->loopEnd = le;
        }
    }
    return s;
}

LoadResult loadAudioFile (const File& file)
{
    LoadResult r;
    String err;
    if (auto s = decode (file.createInputStream(), file.getFileNameWithoutExtension(), "file|" + file.getFullPathName(), err))
        r.zones.push_back ({ s, ZoneParams::defaultsFor (*s), {} });
    else
        r.errors.add (err);
    return r;
}

//== Preset JSON ===============================================================
static LoopMode loopModeFrom (const String& s)
{
    return s == "loop" ? LoopMode::loop : s == "sustain" ? LoopMode::untilRelease : LoopMode::off;
}
static String loopModeName (LoopMode m)
{
    return m == LoopMode::loop ? "loop" : m == LoopMode::untilRelease ? "sustain" : "off";
}

ZoneParams paramsFromJson (const var& j, const SampleData& s, bool v1)
{
    const int n = s.length();
    ZoneParams p = ZoneParams::defaultsFor (s);
    auto num = [&j] (const char* key, double fallback) { const auto v = j.getProperty (key, var()); return v.isVoid() ? fallback : (double) v; };

    p.root  = (int) num ("rootKey", 60);
    p.loKey = (int) num ("loKey", 0);
    p.hiKey = (int) num ("hiKey", 127);
    p.loVel = (int) num ("loVel", 1);
    p.hiVel = (int) num ("hiVel", 127);

    if (v1)
    {
        p.loopMode  = (bool) j.getProperty ("loop", false) ? LoopMode::loop : LoopMode::off;
        p.loopStart = (int) std::lround (num ("loopStart", 0) * n);
        p.loopEnd   = (int) std::lround (num ("loopEnd", 1) * n);
        p.keepSpeed = false;
    }
    else
    {
        const auto env = j.getProperty ("envelope", var());
        auto envNum = [&env] (const char* key, double fallback) { const auto v = env.getProperty (key, var()); return v.isVoid() ? fallback : (double) v; };
        p.loopMode  = loopModeFrom (j.getProperty ("loopMode", "off").toString());
        p.loopStart = (int) num ("loopStart", p.loopStart);
        p.loopEnd   = (int) num ("loopEnd", p.loopEnd);
        p.crossfade = (int) num ("loopCrossfade", 0);
        p.sampleStart = (int) num ("sampleStart", 0);
        p.sampleEnd   = (int) num ("sampleEnd", n);
        p.randomStart = (bool) j.getProperty ("randomStart", false);
        p.randomEnd   = (int) num ("randomEnd", 0);
        p.attack    = (float) envNum ("attack", p.attack);
        p.decay     = (float) envNum ("decay", p.decay);
        p.sustain   = (float) envNum ("sustain", p.sustain);
        p.release   = (float) envNum ("release", p.release);
        p.gainDb    = (float) num ("gainDb", 0);
        p.pan       = (float) num ("pan", 0);
        p.tuneCents = (float) num ("tuneCents", 0);
        p.keepSpeed = (bool) j.getProperty ("keepSpeed", true);
        p.grainMs   = (float) num ("grainMs", 60);
    }
    p.clampTo (n);
    return p;
}

static var paramsToJson (const String& sampleFile, const ZoneParams& p)
{
    auto* z = new DynamicObject();
    z->setProperty ("sample", sampleFile);
    z->setProperty ("rootKey", p.root);
    z->setProperty ("loKey", p.loKey);
    z->setProperty ("hiKey", p.hiKey);
    z->setProperty ("loVel", p.loVel);
    z->setProperty ("hiVel", p.hiVel);
    z->setProperty ("loopMode", loopModeName (p.loopMode));
    z->setProperty ("loopStart", p.loopStart);
    z->setProperty ("loopEnd", p.loopEnd);
    z->setProperty ("loopCrossfade", p.crossfade);
    z->setProperty ("sampleStart", p.sampleStart);
    z->setProperty ("sampleEnd", p.sampleEnd);
    z->setProperty ("randomStart", p.randomStart);
    z->setProperty ("randomEnd", p.randomEnd);
    auto* env = new DynamicObject();
    env->setProperty ("attack", p.attack);
    env->setProperty ("decay", p.decay);
    env->setProperty ("sustain", p.sustain);
    env->setProperty ("release", p.release);
    z->setProperty ("envelope", var (env));
    z->setProperty ("gainDb", p.gainDb);
    z->setProperty ("pan", p.pan);
    z->setProperty ("tuneCents", p.tuneCents);
    z->setProperty ("keepSpeed", p.keepSpeed);
    z->setProperty ("grainMs", p.grainMs);
    return var (z);
}

/** Strips the "01_" prefix that saved sample files carry. */
static String displayNameForEntry (const String& entry)
{
    auto name = entry.fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false);
    if (name.length() > 3 && name[2] == '_' && CharacterFunctions::isDigit (name[0]) && CharacterFunctions::isDigit (name[1]))
        name = name.substring (3);
    return name;
}

/** Reads a preset from an open zip. sourcePrefix + "|" + entry is stored with each sample. */
static LoadResult readPreset (ZipFile& zip, const String& sourcePrefix, const String& fallbackName)
{
    LoadResult r;
    r.isPreset = true;
    const auto* entry = zip.getEntry ("preset.json");
    if (entry == nullptr)
    {
        r.errors.add (fallbackName + ": not an Emulon X preset (no preset.json)");
        return r;
    }
    std::unique_ptr<InputStream> js (zip.createStreamForEntry (*entry));
    const auto json = JSON::parse (js != nullptr ? js->readEntireStreamAsString() : String());
    if (json.getProperty ("format", "").toString() != "emulon-x-preset")
    {
        r.errors.add (fallbackName + ": not an Emulon X preset");
        return r;
    }

    const bool v1 = (int) json.getProperty ("version", 1) < 2;
    r.presetName = json.getProperty ("name", fallbackName).toString();
    if (r.presetName.isEmpty())
        r.presetName = fallbackName;
    const auto interp = json.getProperty ("interpolation", "sinc").toString();
    r.interp = interp == "linear" ? 0 : interp == "cubic" ? 1 : 2;
    r.fx = json.getProperty ("fx", var());
    r.arp = json.getProperty ("arp", var());
    r.voice = json.getProperty ("voice", var());

    if (auto* macros = json.getProperty ("macros", var()).getArray())
        for (auto& m : *macros)
            r.macros.emplace_back (m.getProperty ("name", "").toString(), (float) (double) m.getProperty ("value", 0.0));

    std::map<String, std::shared_ptr<const SampleData>> samples;   // decode each sample once
    if (auto* arr = json.getProperty ("samples", var()).getArray())
        for (auto& s : *arr)
        {
            const auto path = s.getProperty ("file", "").toString();
            const auto* e = zip.getEntry (path);
            if (e == nullptr)
            {
                r.errors.add (path + ": missing inside the preset");
                continue;
            }
            String err;
            auto data = decode (std::unique_ptr<InputStream> (zip.createStreamForEntry (*e)),
                                s.getProperty ("name", displayNameForEntry (path)).toString(),
                                sourcePrefix + "|" + path, err);
            if (data != nullptr)
                samples[path] = std::move (data);
            else
                r.errors.add (err);
        }

    if (auto* arr = json.getProperty ("zones", var()).getArray())
        for (auto& z : *arr)
        {
            const auto it = samples.find (z.getProperty ("sample", "").toString());
            if (it != samples.end())
                r.zones.push_back ({ it->second, paramsFromJson (z, *it->second, v1), z.getProperty ("fx", var()) });
        }
    return r;
}

LoadResult loadPreset (const File& file)
{
    ZipFile zip (file);
    auto name = file.getFileNameWithoutExtension();
    if (name.endsWithIgnoreCase (".emxp"))
        name = name.dropLastCharacters (5);
    return readPreset (zip, "emxp|" + file.getFullPathName(), name);
}

LoadResult loadPresetFromMemory (const MemoryBlock& data, const String& sourcePrefix, const String& fallbackName)
{
    ZipFile zip (std::make_unique<MemoryInputStream> (data, false));
    return readPreset (zip, sourcePrefix, fallbackName);
}

LoadResult load (const File& file)
{
    if (isAudioFile (file))
        return loadAudioFile (file);
    return loadPreset (file);
}

//== Banks =====================================================================
BankInfo readBankInfo (const File& file)
{
    BankInfo b;
    ZipFile zip (file);
    const auto* entry = zip.getEntry ("bank.json");
    if (entry == nullptr)
    {
        b.error = file.getFileName() + ": not an Emulon X bank";
        return b;
    }
    std::unique_ptr<InputStream> js (zip.createStreamForEntry (*entry));
    const auto json = JSON::parse (js != nullptr ? js->readEntireStreamAsString() : String());
    if (json.getProperty ("format", "").toString() != "emulon-x-bank")
    {
        b.error = file.getFileName() + ": not an Emulon X bank";
        return b;
    }
    b.name = json.getProperty ("name", file.getFileNameWithoutExtension()).toString();
    if (auto* arr = json.getProperty ("presets", var()).getArray())
        for (auto& p : *arr)
        {
            const auto path = p.getProperty ("file", "").toString();
            if (zip.getEntry (path) != nullptr)
            {
                b.presetNames.add (p.getProperty ("name", displayNameForEntry (path)).toString());
                b.presetEntries.add (path);
            }
        }
    return b;
}

MemoryBlock readBankEntry (const File& bank, const String& entryPath)
{
    MemoryBlock mb;
    ZipFile zip (bank);
    if (const auto* e = zip.getEntry (entryPath))
        if (std::unique_ptr<InputStream> in (zip.createStreamForEntry (*e)); in != nullptr)
            in->readIntoMemoryBlock (mb);
    return mb;
}

//== Re-loading single samples (for saved projects) ===========================
static std::shared_ptr<const SampleData> sampleFromZip (ZipFile& zip, const String& entry, const String& source, String& error)
{
    if (const auto* e = zip.getEntry (entry))
        return decode (std::unique_ptr<InputStream> (zip.createStreamForEntry (*e)), displayNameForEntry (entry), source, error);
    error = entry + " is missing";
    return {};
}

std::shared_ptr<const SampleData> loadSource (const String& source, String& error)
{
    const auto parts = StringArray::fromTokens (source, "|", "");
    if (parts.size() >= 2 && parts[0] == "file")
    {
        const File f (parts[1]);
        if (! f.existsAsFile())
        {
            error = "Missing file: " + f.getFullPathName();
            return {};
        }
        return decode (f.createInputStream(), f.getFileNameWithoutExtension(), source, error);
    }
    if (parts.size() >= 3 && parts[0] == "emxp")
    {
        const File f (parts[1]);
        if (! f.existsAsFile())
        {
            error = "Missing preset: " + f.getFullPathName();
            return {};
        }
        ZipFile zip (f);
        return sampleFromZip (zip, parts[2], source, error);
    }
    if (parts.size() >= 4 && parts[0] == "emxb")
    {
        const File f (parts[1]);
        if (! f.existsAsFile())
        {
            error = "Missing bank: " + f.getFullPathName();
            return {};
        }
        const auto presetBytes = readBankEntry (f, parts[2]);
        ZipFile inner (std::make_unique<MemoryInputStream> (presetBytes, false));
        return sampleFromZip (inner, parts[3], source, error);
    }
    error = "Unknown sample source";
    return {};
}

//== Saving ====================================================================
static MemoryBlock wavBytes (const SampleData& s, String& error)
{
    MemoryBlock mb;
    {
        std::unique_ptr<OutputStream> out = std::make_unique<MemoryOutputStream> (mb, false);
        auto writer = WavAudioFormat().createWriterFor (out, AudioFormatWriterOptions{}
                                                                .withSampleRate (s.sampleRate)
                                                                .withNumChannels (s.audio.getNumChannels())
                                                                .withBitsPerSample (24));
        if (writer == nullptr)
        {
            error = s.name + ": could not be encoded";
            return {};
        }
        writer->writeFromAudioSampleBuffer (s.audio, 0, s.length());
    }
    return mb;
}

MemoryBlock presetToBytes (const PresetToSave& p, String& error)
{
    ZipFile::Builder zip;
    std::map<const SampleData*, String> files;
    Array<var> samples, zones;

    for (auto& z : p.zones)
    {
        if (z.sample == nullptr)
            continue;
        auto it = files.find (z.sample.get());
        if (it == files.end())
        {
            const auto file = "samples/" + String (files.size() + 1).paddedLeft ('0', 2) + "_" + safeFileName (z.sample->name) + ".wav";
            auto wav = wavBytes (*z.sample, error);
            if (wav.isEmpty())
                return {};
            // Light compression: WAV shrinks a little and saving stays fast.
            zip.addEntry (std::make_unique<MemoryInputStream> (std::move (wav)), 1, file, Time::getCurrentTime());
            auto* s = new DynamicObject();
            s->setProperty ("file", file);
            s->setProperty ("name", z.sample->name);
            samples.add (var (s));
            it = files.emplace (z.sample.get(), file).first;
        }
        auto zj = paramsToJson (it->second, z.params);
        if (auto* chain = z.fx.getArray(); chain != nullptr && ! chain->isEmpty())
            if (auto* obj = zj.getDynamicObject())
                obj->setProperty ("fx", z.fx);
        zones.add (zj);
    }

    Array<var> macros;
    for (auto& [name, value] : p.macros)
    {
        auto* m = new DynamicObject();
        m->setProperty ("name", name);
        m->setProperty ("value", value);
        macros.add (var (m));
    }

    auto* root = new DynamicObject();
    root->setProperty ("format", "emulon-x-preset");
    root->setProperty ("version", 4);
    root->setProperty ("name", p.name);
    root->setProperty ("interpolation", p.interp == Interp::linear ? "linear" : p.interp == Interp::cubic ? "cubic" : "sinc");
    root->setProperty ("macros", macros);
    if (! p.fx.isVoid())
        root->setProperty ("fx", p.fx);
    if (! p.arp.isVoid())
        root->setProperty ("arp", p.arp);
    if (! p.voice.isVoid())
        root->setProperty ("voice", p.voice);
    root->setProperty ("samples", samples);
    root->setProperty ("zones", zones);
    root->setProperty ("createdWith", "Emulon X " EMX_VERSION_STRING);

    const auto json = JSON::toString (var (root));
    zip.addEntry (std::make_unique<MemoryInputStream> (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 9,
                  "preset.json", Time::getCurrentTime());

    MemoryBlock out;
    {
        MemoryOutputStream mos (out, false);
        if (! zip.writeToStream (mos, nullptr))
        {
            error = "Could not write the preset";
            return {};
        }
    }
    return out;
}

static bool writeFileSafely (const File& target, const MemoryBlock& data, String& error)
{
    // Write next to the target first, then swap, so a failed save never destroys the old file.
    TemporaryFile tmp (target);
    if (! tmp.getFile().replaceWithData (data.getData(), data.getSize()) || ! tmp.overwriteTargetFileWithTemporary())
    {
        error = "Could not write " + target.getFullPathName();
        return false;
    }
    return true;
}

bool savePreset (const File& file, const PresetToSave& p, String& error)
{
    const auto data = presetToBytes (p, error);
    return ! data.isEmpty() && writeFileSafely (file, data, error);
}

bool saveBank (const File& file, const String& bankName,
               const std::vector<std::pair<String, MemoryBlock>>& presets, String& error)
{
    ZipFile::Builder zip;
    Array<var> list;
    int index = 0;
    for (auto& [name, bytes] : presets)
    {
        const auto entry = "presets/" + String (++index).paddedLeft ('0', 2) + "_" + safeFileName (name) + ".emxp";
        zip.addEntry (std::make_unique<MemoryInputStream> (bytes, true), 0, entry, Time::getCurrentTime());
        auto* p = new DynamicObject();
        p->setProperty ("name", name);
        p->setProperty ("file", entry);
        list.add (var (p));
    }
    auto* root = new DynamicObject();
    root->setProperty ("format", "emulon-x-bank");
    root->setProperty ("version", 1);
    root->setProperty ("name", bankName);
    root->setProperty ("presets", list);
    const auto json = JSON::toString (var (root));
    zip.addEntry (std::make_unique<MemoryInputStream> (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 9,
                  "bank.json", Time::getCurrentTime());

    MemoryBlock out;
    {
        MemoryOutputStream mos (out, false);
        if (! zip.writeToStream (mos, nullptr))
        {
            error = "Could not write the bank";
            return false;
        }
    }
    return writeFileSafely (file, out, error);
}
} // namespace emx::loader
