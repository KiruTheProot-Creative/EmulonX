// Emulon X — the sampler engine (runs on the audio thread).
// A direct port of the browser prototype's engine: same zones, loops, crossfades,
// keep-speed pitching and envelopes, so presets sound the same in both.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <bitset>
#include "Zone.h"
#include "Interpolator.h"

namespace emx
{
class SamplerEngine
{
public:
    static constexpr int maxVoiceSlots = 288;   // 256 playable + room for fading stolen voices

    SamplerEngine();

    void prepare (double sampleRate);

    /** The zones to play. Call from the audio thread with a snapshot that something else
        keeps alive (the processor's release pool), so nothing is freed here. */
    void setZoneSet (std::shared_ptr<const ZoneSet> set) noexcept { zoneSet = std::move (set); }

    //== Performance settings ==================================================
    void setMaxVoices (int n) noexcept      { maxVoices = juce::jlimit (1, 256, n); }
    void setStealQuietest (bool q) noexcept { stealQuietest = q; }
    void setGlobalTune (float cents) noexcept { globalTuneCents = cents; }
    void setBendRange (float semis) noexcept  { bendRange = semis; }

    //== MIDI ==================================================================
    void handleMidi (const juce::MidiMessage&) noexcept;
    void noteOn (int note, int velocity) noexcept;
    void noteOff (int note) noexcept;
    void setSustain (bool down) noexcept;
    void setPitchBend (float minusOneToOne) noexcept { bend = minusOneToOne; }
    void allNotesOff() noexcept;   // quick fade
    void reset() noexcept;         // silence immediately

    /** Adds the voices into L/R (they must be cleared by the caller). */
    void render (float* left, float* right, int numSamples) noexcept;

    int getActiveVoiceCount() const noexcept;
    float getModWheel() const noexcept { return modWheel; }

    /** Current playback position (in source frames) of the newest voice of each zone. For the UI. */
    struct Playhead { int zoneId; double position; };
    int getPlayheads (Playhead* out, int maxCount) const noexcept;

private:
    struct Voice
    {
        bool active = false;
        int note = 0, velocity = 0, zoneId = 0;
        std::shared_ptr<const SampleData> sample;
        ZoneParams params;                    // last known zone settings (used if the zone is removed)
        double k = 1.0;                       // source rate / output rate
        int grain = 2048, grainCounter = 0;
        double pos = 0.0, anchor0 = 0.0, anchor1 = 0.0;
        float level = 0.0f;
        int stage = 0;                        // 0 attack, 1 decay/sustain, 3 release
        bool released = false, stolen = false, keepSpeed = true;
        juce::uint64 age = 0;
    };

    void renderVoice (Voice&, const ZoneParams&, float* L, float* R, int n) noexcept;
    Voice* freeSlot() noexcept;
    bool stealOne() noexcept;
    void startVoicesFor (int note, int velocity) noexcept;

    std::array<Voice, maxVoiceSlots> voices;
    std::shared_ptr<const ZoneSet> zoneSet;
    double sampleRate = 44100.0;
    juce::uint64 ageCounter = 0;
    int maxVoices = 64;
    bool stealQuietest = false;
    float globalTuneCents = 0.0f, bendRange = 2.0f, bend = 0.0f, modWheel = 0.0f;
    bool sustainDown = false;
    std::bitset<128> keysDown;
};
} // namespace emx
