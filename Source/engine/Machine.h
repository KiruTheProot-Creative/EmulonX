// Emulon X — V3 Machine: drum pads (each plays a note of the preset, with its own level, pan, tune,
// decay and choke group) and a 16-step sequencer per pad with 8 variations (audio thread runs it).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <optional>

namespace emx
{
struct PadSettings
{
    int note = 36;
    float level = 1.0f;     // 0..1 (multiplies the velocity's loudness)
    float pan = 0.0f;       // -1..1
    float tune = 0.0f;      // semitones, -24..24
    float decay = 1.0f;     // 1 = the sound as it is; lower = it fades out sooner
    int choke = 0;          // 0 = none, 1..8: a pad in the same group cuts this one off
    juce::String name;
};

struct MachineSettings
{
    static constexpr int maxPads = 64, numSteps = 16, numVariations = 8;
    int grid = 4;                                            // 4 x 4 or 8 x 8 pads
    std::array<PadSettings, maxPads> pads;
    // velocity of every step of every pad in every variation (0 = off)
    std::array<std::array<std::array<juce::uint8, numSteps>, maxPads>, numVariations> steps {};
    int variation = 0;
    int length = 16;                                         // steps per pattern
    int den = 16;                                            // step length: 1/16
    float swing = 0.0f;                                      // 0..1
    bool playing = false;                                    // the sequencer runs (with the DAW when it plays)

    MachineSettings();
    int padCount() const noexcept { return grid * grid; }
    /** The pad that plays this note (-1 = none). */
    int padForNote (int note) const noexcept;
    juce::var toJson() const;
    static MachineSettings fromJson (const juce::var&);
};

class MachineSequencer
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void setSettings (const MachineSettings& s) noexcept { settings = s; }
    void reset() noexcept { lastStep = -1; numSounding = 0; internalPpq = 0.0; }

    /** Adds the steps that fall in this block to `out` (note-ons and their note-offs). */
    void process (juce::MidiBuffer& out, int numSamples, double bpm, std::optional<double> hostPpq) noexcept;

    std::atomic<int> currentStep { -1 };   // for the UI

private:
    MachineSettings settings;
    double sr = 44100.0, internalPpq = 0.0;
    juce::int64 lastStep = -1;
    struct Sounding { int note; juce::int64 offIn; };
    std::array<Sounding, 128> sounding {};
    int numSounding = 0;
    bool wasPlaying = false;
};
} // namespace emx
