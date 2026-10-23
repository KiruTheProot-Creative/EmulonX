// Emulon X — loading audio files and .emxp presets (runs on a background thread).
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
};

struct LoadResult
{
    std::vector<LoadedZone> zones;
    int interp = -1;             // from a preset, -1 = leave unchanged
    juce::String presetName;
    juce::StringArray errors;
};

namespace loader
{
    /** File types that can be dropped onto the plugin. */
    juce::String supportedWildcards();
    bool isSupported (const juce::File&);

    /** One audio file -> one zone (root and loop taken from the file when present). */
    LoadResult loadAudioFile (const juce::File&);

    /** A preset made in Emulon X (or the browser prototype): zones with all their settings. */
    LoadResult loadPreset (const juce::File&);

    /** Picks loadAudioFile or loadPreset by extension. */
    LoadResult load (const juce::File&);

    /** Re-loads one sample from a saved source string ("file|..." or "emxp|...|entry"). */
    std::shared_ptr<const SampleData> loadSource (const juce::String& source, juce::String& error);

    /** Decodes audio from a stream. `name` is used for display and root-note detection. */
    std::shared_ptr<SampleData> decode (std::unique_ptr<juce::InputStream>, const juce::String& name,
                                        const juce::String& source, juce::String& error);

    /** Reads a zone's settings from a preset's JSON. `v1` = very first prototype format. */
    ZoneParams paramsFromJson (const juce::var& z, const SampleData&, bool v1);
}
} // namespace emx
