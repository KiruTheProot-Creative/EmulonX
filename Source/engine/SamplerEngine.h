// Emulon X — the sampler engine (runs on the audio thread).
// A direct port of the browser prototype's engine: same zones, loops, crossfades,
// keep-speed pitching and envelopes, so presets sound the same in both.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <bitset>
#include "Zone.h"
#include "Interpolator.h"
#include "EmuVoice.h"

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
    /** Mono: one note at a time. Legato: overlapping notes slide without restarting.
        Glide: seconds for the pitch to slide to the new note (0 = off). */
    void setVoiceMode (bool monoMode, bool legatoMode, float glideSeconds) noexcept
    {
        mono = monoMode; legato = legatoMode; glideTime = juce::jlimit (0.0f, 5.0f, glideSeconds);
    }

    //== MIDI ==================================================================
    void handleMidi (const juce::MidiMessage&) noexcept;
    void noteOn (int note, int velocity) noexcept;
    void noteOff (int note) noexcept;
    void setSustain (bool down) noexcept;
    void setPitchBend (float minusOneToOne) noexcept { bend = minusOneToOne; }
    /** For E-mu voices: MIDI A..P (Emulon X's macros, 0..1) and the tempo. */
    void setControllers (const float* sixteen) noexcept { for (int i = 0; i < 16; ++i) emuIn.midi[(size_t) i] = juce::jlimit (0.0f, 1.0f, sixteen[i]); }
    void setTempo (double bpm) noexcept { emuIn.bpm = bpm; }
    void allNotesOff() noexcept;   // quick fade
    void reset() noexcept;         // silence immediately

    /** Adds the voices into L/R (they must be cleared by the caller). */
    void render (float* left, float* right, int numSamples) noexcept;

    /** A zone with its own FX channel: its voices go to this buffer instead of the main mix. */
    struct Bus { int zoneId; float* left; float* right; };
    void render (float* left, float* right, int numSamples, const Bus* buses, int numBuses) noexcept;

    /** Modulated pitch of zones (semitones added on top of their SEMI knob), set every block. */
    struct PitchMod { int zoneId; float semitones; };
    void setPitchMods (const PitchMod* mods, int count) noexcept
    {
        numPitchMods = juce::jmin (count, (int) pitchMods.size());
        for (int i = 0; i < numPitchMods; ++i)
            pitchMods[(size_t) i] = mods[i];
    }

    /** For the FX envelopes: goes up with every note-on; gate = any key (or the pedal) held. */
    juce::uint32 getNoteOnCount() const noexcept { return noteOnCount; }
    bool isGateOpen() const noexcept { return keysDown.any() || sustainDown; }

    int getActiveVoiceCount() const noexcept;

    /** Fades out one voice (released notes first). Used by the voice limit and the CPU guard. */
    bool stealOne() noexcept;
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
        double pos = 0.0, read0 = 0.0, read1 = 0.0;   // keep-speed: read positions of the two grains
        float glide = 0.0f, glideStep = 0.0f;         // semitones still to slide, and per sample
        float level = 0.0f;
        int stage = 0;                        // 0 attack, 1 decay/sustain, 3 release
        bool released = false, stolen = false, keepSpeed = true, inLoop = false;
        juce::uint64 age = 0;
        emu::VoiceState emu;                  // used when the zone has an E-mu design
        float emuGain = 0.0f;                 // gain at the end of the last control step (ramped from here)
    };

    /** rawForEmu: no envelope, gain or pan (the E-mu layer adds them), pitch includes its modulation. */
    void renderVoice (Voice&, const ZoneParams&, float* L, float* R, int n, bool rawForEmu = false) noexcept;
    void renderEmuVoice (Voice&, const ZoneParams&, float* L, float* R, int n) noexcept;
    Voice* freeSlot() noexcept;
    void startVoicesFor (int note, int velocity, float glideFrom = 0.0f, const std::bitset<1024>* skipZones = nullptr) noexcept;
    void monoNoteOn (int note, int velocity, int previous) noexcept;
    float glideStepFor (float semitones) const noexcept;

    std::array<Voice, maxVoiceSlots> voices;
    std::shared_ptr<const ZoneSet> zoneSet;
    double sampleRate = 44100.0;
    juce::uint64 ageCounter = 0;
    int maxVoices = 64;
    bool stealQuietest = false;
    float globalTuneCents = 0.0f, bendRange = 2.0f, bend = 0.0f, modWheel = 0.0f;
    bool sustainDown = false;
    juce::uint32 noteOnCount = 0;
    bool mono = false, legato = true;
    float glideTime = 0.0f;
    int noteStack[128] {}, stackSize = 0, monoNote = -1, lastNote = -1, monoVelocity = 100;
    std::bitset<128> keysDown;
    juce::Random random;
    std::array<PitchMod, 64> pitchMods {};
    int numPitchMods = 0;
    emu::Inputs emuIn;
    std::vector<float> combMemory;            // flanger delay lines for the E-mu voices
};
} // namespace emx
