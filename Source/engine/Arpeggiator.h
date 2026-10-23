// Emulon X — arpeggiator: turns held notes into a rhythmic pattern (audio thread).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <bitset>
#include <optional>

namespace emx
{
struct ArpStep
{
    bool on = true;
    float velocity = 0.8f;   // 0..1
    int transpose = 0;       // semitones, -24 .. +24
};

struct ArpPattern
{
    int length = 8;          // 1..16 steps
    std::array<ArpStep, 16> steps {};
};

struct ArpSettings
{
    enum Mode { up, down, upDown, downUp, played, random, chord, numModes };
    static const char* modeName (int);

    bool enabled = false;
    int mode = up;
    int num = 1, den = 16, dot = 0;   // step length, like the synced delay
    int octaves = 1;                  // 1..4
    float gate = 0.75f;               // 0.05..1
    float swing = 0.0f;               // 0..1 (delays every second step by up to half a step)
    bool latch = false;
    bool playedVelocity = false;      // use the velocity you played instead of the pattern's
    static constexpr int numPatterns = 8, maxSlots = 16;
    std::array<ArpPattern, numPatterns> patterns {};   // A..H
    std::array<int, maxSlots> arrangement {};          // which pattern each slot plays
    int arrangementLength = 1;                         // 1..16 slots, played in a loop

    ArpSettings();
    double stepBeats() const noexcept;   // length of one step in quarter-note beats
    int totalSteps() const noexcept;     // one pass through the arrangement
    /** Which slot / pattern / step a running step counter is at. */
    void locate (juce::int64 stepIndex, int& slot, int& pattern, int& step) const noexcept;
    juce::var toJson() const;
    static ArpSettings fromJson (const juce::var&);
    bool operator== (const ArpSettings&) const noexcept;
};

class Arpeggiator
{
public:
    Arpeggiator();
    void prepare (double sampleRate);
    void setSettings (const ArpSettings& s) noexcept;   // audio thread
    void reset() noexcept;

    /** in: incoming MIDI (already channel-filtered); out: what the sampler plays.
        hostPpq: the song position in beats when the host is playing, so steps land on its grid. */
    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples, double bpm,
                  std::optional<double> hostPpq) noexcept;

    /** True while the arp needs process() (on, or still has notes to switch off). */
    bool isActive() const noexcept { return settings.enabled || numSounding > 0; }

    // For the UI (any thread)
    std::atomic<int> currentStep { -1 }, currentPattern { -1 }, currentSlot { -1 };
    std::atomic<juce::uint64> heldMask[2] {};

private:
    void noteInput (int note, int velocity, bool on, int pos, juce::MidiBuffer& out) noexcept;
    void trigger (juce::int64 stepIndex, int pos, int numSamples, double samplesPerStep, juce::MidiBuffer& out) noexcept;
    void stopAll (int pos, juce::MidiBuffer& out) noexcept;
    void publishHeld() noexcept;

    ArpSettings settings;
    double sampleRate = 44100.0;
    double internalPpq = 0.0;
    bool wasEnabled = false, startNow = false;

    struct Held { int note, velocity; };
    std::array<Held, 32> held {};
    int numHeld = 0;
    std::bitset<128> physical;

    struct Sounding { int note; juce::int64 offIn; };   // samples until note-off
    std::array<Sounding, 64> sounding {};
    int numSounding = 0;

    int seqIndex = 0;
    juce::int64 lastStep = -1;
    juce::Random rng;
};
} // namespace emx
