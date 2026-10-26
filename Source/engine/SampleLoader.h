// Emulon X — loading and saving: audio files, .emxp presets and .emxb banks.
// Everything here may run on a background thread.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Zone.h"

namespace emx
{
struct LoadedZone
{
    std::shared_ptr<const SampleData> sample;
    ZoneParams params;
    juce::var fx;   // this zone's own FX chain (prototype JSON), may be void
};

using MacroList = std::vector<std::pair<juce::String, float>>;   // name, value 0..100

struct LoadResult
{
    std::vector<LoadedZone> zones;
    bool isPreset = false;       // true: replaces the current preset; false: zones are added
    int interp = -1;             // from a preset, -1 = leave unchanged
    juce::String presetName;
    MacroList macros;            // from a preset (may be empty)
    juce::var fx;                // FX section (chains, modulation, envelopes)
    juce::var arp, voice;        // arpeggiator and mono / poly settings (may be void)
    juce::var splice;            // splicer mode: first key, cuts (may be void)
    juce::var machine;           // V3 Machine: pads and step patterns (may be void)
    juce::var info;              // V3: type, author, description, tags (may be void)
    juce::StringArray errors;
};

/** Everything needed to write a preset file. */
/** What the Save Preset window fills in: the preset's type (free text), who made it, notes, tags. */
struct PresetInfo
{
    juce::String type, author, description, tags;
    juce::String created;   // ISO date of the first save (kept on later saves)
    bool isEmpty() const noexcept { return type.isEmpty() && author.isEmpty() && description.isEmpty() && tags.isEmpty() && created.isEmpty(); }
    juce::var toJson() const
    {
        auto* o = new juce::DynamicObject();
        if (type.isNotEmpty())        o->setProperty ("type", type);
        if (author.isNotEmpty())      o->setProperty ("author", author);
        if (description.isNotEmpty()) o->setProperty ("description", description);
        if (tags.isNotEmpty())        o->setProperty ("tags", tags);
        if (created.isNotEmpty())     o->setProperty ("created", created);
        return juce::var (o);
    }
    static PresetInfo fromJson (const juce::var& j)
    {
        PresetInfo i;
        if (! j.isObject())
            return i;
        i.type = j.getProperty ("type", "").toString().trim().substring (0, 32);
        i.author = j.getProperty ("author", "").toString().trim().substring (0, 48);
        i.description = j.getProperty ("description", "").toString().substring (0, 1000);
        i.tags = j.getProperty ("tags", "").toString().trim().substring (0, 200);
        i.created = j.getProperty ("created", "").toString().trim().substring (0, 32);
        return i;
    }
};

struct PresetToSave
{
    juce::String name;
    Interp interp = Interp::sinc;
    std::vector<LoadedZone> zones;
    MacroList macros;
    juce::var fx, arp, voice, splice, machine;
    PresetInfo info;
};

struct BankInfo
{
    juce::String name;
    juce::StringArray presetNames, presetEntries;   // entry = path of the .emxp inside the bank
    juce::String error;
};

namespace loader
{
    //== What can be opened =====================================================
    juce::String supportedWildcards();
    bool isSupported (const juce::File&);
    bool isAudioFile (const juce::File&);
    bool isPresetFile (const juce::File&);   // .emxp (also a renamed .emxp.zip)
    bool isBankFile (const juce::File&);     // .emxb (also .emxb.zip)

    //== Loading ================================================================
    LoadResult loadAudioFile (const juce::File&);
    LoadResult loadPreset (const juce::File&);
    LoadResult loadPresetFromMemory (const juce::MemoryBlock&, const juce::String& sourcePrefix, const juce::String& fallbackName);
    LoadResult load (const juce::File&);   // audio or preset, by extension / content

    BankInfo readBankInfo (const juce::File&);
    juce::MemoryBlock readBankEntry (const juce::File& bank, const juce::String& entry);

    /** Re-loads one sample from a saved source string:
        "file|<path>", "emxp|<preset>|<entry>" or "emxb|<bank>|<preset entry>|<sample entry>". */
    std::shared_ptr<const SampleData> loadSource (const juce::String& source, juce::String& error);

    std::shared_ptr<SampleData> decode (std::unique_ptr<juce::InputStream>, const juce::String& name,
                                        const juce::String& source, juce::String& error);
    ZoneParams paramsFromJson (const juce::var& z, const SampleData&, bool v1);

    //== Saving =================================================================
    /** A preset with its samples packed inside as 24-bit WAV. Same format as the prototype. */
    juce::MemoryBlock presetToBytes (const PresetToSave&, juce::String& error);
    bool savePreset (const juce::File&, const PresetToSave&, juce::String& error);

    /** A bank: several presets (as .emxp data) in one file. */
    bool saveBank (const juce::File&, const juce::String& bankName,
                   const std::vector<std::pair<juce::String, juce::MemoryBlock>>& presets, juce::String& error);

    /** Turns a name into something safe for a file name. */
    juce::String safeFileName (const juce::String&);
}
} // namespace emx
