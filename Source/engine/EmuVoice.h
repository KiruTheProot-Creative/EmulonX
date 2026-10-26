// Emulon X — an E-mu style voice: what a zone can carry on top of the usual settings so every note
// gets its own three 6-stage envelopes, filter, two LFOs, lags and a mod matrix ("cords").
// Made for presets from Emulator X banks; zones without it play exactly as before.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace emx::emu
{
//== The design (read-only, shared by all notes of a zone) =============================
struct Envelope
{
    std::array<float, 6> rate {};    // 0..127: attack 1, attack 2, decay 1, decay 2, release 1, release 2
    std::array<float, 6> level {};   // -100..100 %
};

struct Lfo
{
    int shape = 0;                   // see lfoShapeNames()
    float hz = 1.0f;
    bool sync = false;               // tempo sync (then `division` is used)
    int division = 0;
    float delay = 0.0f;              // seconds before it fades in
    float variation = 0.0f;          // % random rate change per note
    bool keySync = false;            // restarts with every note
};

struct Cord
{
    int source = 0, dest = 0;
    float amount = 0.0f;             // -100..100 %
};

struct Design
{
    std::array<Envelope, 3> env;     // amp, filter, aux
    int filterType = 127;            // E-mu's filter numbers (127 = off)
    float cutoff = 1.0f, resonance = 0.0f;   // 0..1
    std::array<Lfo, 2> lfo;
    std::array<float, 2> lagRate { 0.0f, 0.0f };
    std::vector<Cord> cords;         // up to 36
    float chorusAmount = 0.0f, chorusWidth = 100.0f;   // %
    int bendUp = 2, bendDown = 2;    // semitones
    int transpose = 0, coarse = 0;   // semitones (the zone's SEMI knob gets their sum)
    float fine = 0.0f;               // cents (the zone's TUNE knob)
    float ampEnvRange = 60.0f;       // dB the amp envelope spans (measured)

    juce::var toJson() const;
    static std::shared_ptr<const Design> fromJson (const juce::var&);
};

const juce::StringArray& filterNames();     // index = E-mu filter number, "" if unknown
juce::String sourceName (int);
juce::String destName (int);
/** E-mu envelope rate (0..127) -> seconds for a full 0..100 % move (measured from Emulator X). */
double rateSeconds (double rate) noexcept;
/** The other way: seconds -> rate (0..127). */
double rateFromSeconds (double seconds) noexcept;

//== For editing (the Editor's EMU engine panels) =====================================
/** A plain design for a zone that starts using the EMU engine: its amp envelope from the zone's ADSR,
    no filter, gentle LFOs, empty cords. */
std::shared_ptr<Design> makeDesign (float attack, float decay, float sustain, float release);
/** Writes an ADSR into the amp envelope (attack 1 / decay 1 / release 1; attack 2, decay 2 and release 2 off). */
void setAmpFromAdsr (Design&, float attack, float decay, float sustain, float release);
/** The amp envelope seen as an ADSR (what the VOICE panel's faders show). */
void adsrFromAmp (const Design&, float& attack, float& decay, float& sustain, float& release);
const juce::StringArray& lfoShapeNames();          // index = Lfo::shape
juce::String divisionName (int division);           // tempo-synced LFO rate, e.g. "1/8 D"
constexpr int numDivisions = 26;
/** The lists the Cords panel offers, in a sensible order (ids as in Emulator X). */
const std::vector<int>& sourceIds();
const std::vector<int>& destIds();
constexpr int maxCords = 36;

//== What every note keeps while it plays ===============================================
struct EnvState
{
    int stage = 0;                   // 0..3 while held, 4..5 releasing, 6 done
    float level = 0.0f;              // -1..1
    float from = 0.0f, pos = 0.0f, len = 0.0f;   // current stage: start level, samples done, length
    bool skip = false;                             // this stage is left out (decay at rate 0)
};

struct VoiceState
{
    std::array<EnvState, 3> env;
    std::array<double, 2> lfoPhase {};
    std::array<float, 2> lfoRateScale { 1.0f, 1.0f }, lfoHeld {}, lfoAge {};
    std::array<float, 2> lag {};
    std::array<float, 2> random {};  // per-note random values (kRandom 1, 2)
    float noise = 0.0f, pink = 0.0f;
    bool released = false;
    int ampStage = -1;               // amp envelope stage the dB fade below belongs to
    float ampDb = -200.0f, ampFromDb = -200.0f;   // the amp envelope in dB: now, and where this stage began

    // results of the last control update
    float ampGain = 1.0f;            // linear, includes the amp envelope and volume cords
    float pitchSemis = 0.0f, pan = 0.0f;
    float cutoff = 1.0f, resonance = 0.0f;

    // filter coefficients, worked out every control step (3 state-variable sets + an allpass / delay value)
    float coef[3][5] {};
    float aux = 0.0f;

    // filter memory (two channels, up to 6 poles / allpass stages)
    float z[2][12] {};
    float fb[2] {};
    float* comb = nullptr;           // 2 x combSize floats owned by the engine (flanger delay)
    int combSize = 0, combPos = 0;
};

/** Live values from outside the voice. */
struct Inputs
{
    float modWheel = 0.0f, pitchWheel = 0.0f, pressure = 0.0f, pedal = 0.0f;
    bool footswitch1 = false, footswitch2 = false;
    std::array<float, 16> midi {};   // MIDI A..P, 0..1 (Emulon X's macros)
    double bpm = 120.0;
};

void startNote (VoiceState&, const Design&, juce::Random&);
void releaseNote (VoiceState&);
/** Runs envelopes, LFOs and cords for `numSamples`; returns false when the amp envelope has finished. */
bool control (VoiceState&, const Design&, const Inputs&, int note, int velocity, double sampleRate, int numSamples) noexcept;
/** Filters one stereo sample with the voice's current cutoff and resonance. */
void filter (VoiceState&, const Design&, double sampleRate, float& left, float& right) noexcept;
} // namespace emx::emu
