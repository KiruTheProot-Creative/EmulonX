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

String supportedWildcards() { return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.emxp;*.zip"; }

bool isSupported (const File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;emxp;zip");
}

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
    if (meta.containsKey ("MidiUnityNote"))
    {
        const int r = meta["MidiUnityNote"].getIntValue();
        if (r >= 0 && r < 128 && meta.containsKey ("NumSampleLoops"))   // only trust it inside a real smpl chunk
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
        r.zones.push_back ({ s, ZoneParams::defaultsFor (*s) });
    else
        r.errors.add (err);
    return r;
}

//== Presets (.emxp = zip with preset.json + samples/*.wav) ====================
static LoopMode loopModeFrom (const String& s)
{
    return s == "loop" ? LoopMode::loop : s == "sustain" ? LoopMode::untilRelease : LoopMode::off;
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

LoadResult loadPreset (const File& file)
{
    LoadResult r;
    ZipFile zip (file);
    const auto* entry = zip.getEntry ("preset.json");
    if (entry == nullptr)
    {
        r.errors.add (file.getFileName() + ": not an Emulon X preset (no preset.json)");
        return r;
    }

    std::unique_ptr<InputStream> js (zip.createStreamForEntry (*entry));
    const auto json = JSON::parse (js != nullptr ? js->readEntireStreamAsString() : String());
    if (json.getProperty ("format", "").toString() != "emulon-x-preset")
    {
        r.errors.add (file.getFileName() + ": not an Emulon X preset");
        return r;
    }

    const bool v1 = (int) json.getProperty ("version", 1) < 2;
    r.presetName = json.getProperty ("name", file.getFileNameWithoutExtension()).toString();
    const auto interp = json.getProperty ("interpolation", "sinc").toString();
    r.interp = interp == "linear" ? 0 : interp == "cubic" ? 1 : 2;

    // Decode each sample once, even if several zones use it.
    std::map<String, std::shared_ptr<const SampleData>> samples;
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
                                s.getProperty ("name", path).toString(),
                                "emxp|" + file.getFullPathName() + "|" + path, err);
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
                r.zones.push_back ({ it->second, paramsFromJson (z, *it->second, v1) });
        }
    return r;
}

LoadResult load (const File& file)
{
    if (file.hasFileExtension ("emxp;zip"))
        return loadPreset (file);
    return loadAudioFile (file);
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
        if (const auto* e = zip.getEntry (parts[2]))
        {
            auto name = parts[2].fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false);
            if (name.length() > 3 && name[2] == '_' && CharacterFunctions::isDigit (name[0]))
                name = name.substring (3);   // "01_Piano_C4" -> "Piano_C4"
            return decode (std::unique_ptr<InputStream> (zip.createStreamForEntry (*e)), name, source, error);
        }
        error = parts[2] + " missing in " + f.getFileName();
        return {};
    }
    error = "Unknown sample source";
    return {};
}
} // namespace emx::loader
