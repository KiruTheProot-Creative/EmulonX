// Emulon X — samples, zones and the immutable zone set the audio thread plays.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>

namespace emx
{
/** One decoded sample, kept at its original sample rate. Never modified after loading. */
struct SampleData
{
    juce::String name;               // shown in the UI
    juce::String source;             // where it came from: "file|<path>" or "emxp|<path>|<entry>"
    double sampleRate = 44100.0;
    juce::AudioBuffer<float> audio;  // 1 or 2 channels

    // Optional hints found in the file (WAV "smpl" chunk); -1 = none.
    int rootKey = -1, loopStart = -1, loopEnd = -1;

    int length() const noexcept { return audio.getNumSamples(); }
};

enum class LoopMode { off = 0, loop = 1, untilRelease = 2 };
/** Which way the loop plays: start to end again and again, back and forth, or end to start again and again. */
enum class LoopDir { forward = 0, pingPong = 1, backward = 2 };
enum class Interp { linear = 0, cubic = 1, sinc = 2 };

/** Everything that can be set per zone. Same meaning and units as the browser prototype. */
struct ZoneParams
{
    int root = 60, loKey = 0, hiKey = 127, loVel = 1, hiVel = 127;
    LoopMode loopMode = LoopMode::off;
    LoopDir loopDir = LoopDir::forward;
    int loopStart = 0, loopEnd = 0, crossfade = 0;   // in sample frames
    int sampleStart = 0, sampleEnd = 0;              // where playback starts / stops (0 end = whole sample)
    bool randomStart = false;                        // each note starts somewhere between sampleStart and randomEnd
    int randomEnd = 0;
    float attack = 0.002f, decay = 0.3f, sustain = 1.0f, release = 0.25f;   // seconds / level
    float gainDb = 0.0f, pan = 0.0f;                 // pan: -100 (left) .. +100 (right)
    float tuneCents = 0.0f;
    int semitones = 0;                               // coarse pitch, -24 .. +24 (can be modulated)
    bool keepSpeed = true;                           // pitch without changing length
    float grainMs = 60.0f;

    /** Sensible starting values for a freshly loaded sample (uses file hints when present). */
    static ZoneParams defaultsFor (const SampleData&);
    /** Keeps loop points inside the sample and in the right order. */
    void clampTo (int sampleLength) noexcept;
};

struct Zone
{
    int id = 0;
    std::shared_ptr<const SampleData> sample;
    ZoneParams params;
};

/** A complete, read-only description of what to play. The audio thread only ever
    sees these snapshots; edits build a new one and swap it in. */
struct ZoneSet
{
    std::vector<Zone> zones;
    Interp interp = Interp::sinc;

    const Zone* find (int id) const noexcept
    {
        for (auto& z : zones)
            if (z.id == id)
                return &z;
        return nullptr;
    }
};

/** Which octave number middle C (MIDI note 60) gets in note names: 3 like Ableton / Kontakt
    (the default), or 4 (scientific pitch). Used by noteName and noteFromName. */
void setMiddleCOctave (int octave) noexcept;
int getMiddleCOctave() noexcept;

/** Reads a note from a file name like "Piano_C3", "Pad A#3" or "Bass-Eb1". -1 if none. */
int noteFromName (const juce::String& name);
juce::String noteName (int midiNote);
} // namespace emx
