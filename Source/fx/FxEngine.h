// Emulon X — runs the FX chains, envelopes and modulation on the audio thread.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "FxModel.h"
#include <atomic>

namespace emx::fx
{
/** A read-only copy of the FX setup for the audio thread (built on the message thread). */
struct Snapshot
{
    struct RouteRT { int slot; SrcKind kind; int index; float amount; };
    struct ModuleRT
    {
        int id = 0;
        Type type = Type::filter;
        bool on = true;
        Settings settings;
        std::array<float, maxSlots> norm {};
        std::array<const ParamSpec*, maxSlots> spec {};
        Processor* processor = nullptr;
        std::vector<RouteRT> routes;
    };
    struct ChainRT { int zoneId = -1; std::vector<ModuleRT> modules; };

    std::vector<ChainRT> zoneChains;   // only zones that have effects
    ChainRT master;
    ChainRT aux[2];
    float auxSend[2] {};
    std::vector<Envelope> envelopes;
    std::vector<RouteRT> volumeRoutes;  // modulation of the master volume
    struct ZonePitchRT { int zoneId; float baseNorm; std::vector<RouteRT> routes; };
    std::vector<ZonePitchRT> zonePitch; // modulated SEMI knobs (zones with connections only)
    float bpm = 120.0f;
    std::vector<std::shared_ptr<Processor>> keepAlive;

    /** zoneChains: (zone id, chain) pairs. */
    static std::shared_ptr<Snapshot> build (const Setup&, const std::vector<std::pair<int, const Chain*>>& zoneChains,
                                            const std::vector<std::pair<int, int>>& zoneSemitones = {});
};

class Engine
{
public:
    static constexpr int maxModOut = 64;

    void setSnapshot (const Snapshot* s) noexcept { snap = s; }
    const Snapshot* getSnapshot() const noexcept { return snap; }

    /** Call once per sub-block before processing chains. hostBeat: the DAW's position in quarter notes
        at the start of this sub-block (negative = not playing; synced envelopes then keep their own beat clock). */
    void beginBlock (int numSamples, double sampleRate, double bpm, juce::uint32 noteOnCount, bool gateOpen,
                     const float* macros01, int numMacros, float modWheel, double hostBeat = -1.0) noexcept;
    void processChain (const Snapshot::ChainRT&, float* L, float* R, int n) noexcept;
    /** Modulated SEMI knobs: fills (zone id, semitones). Returns how many. */
    int modulateZonePitch (std::pair<int, float>* out, int maxCount) noexcept;
    /** Master volume 0..1 (linear dB knob) after modulation. */
    float modulateVolume (float norm01) noexcept;
    void reset() noexcept;

    //== Read by the UI (any thread) ==============================================
    struct ModOut
    {
        std::atomic<int> id { 0 };
        std::atomic<float> value[maxSlots] {};
        std::atomic<float> gainReduction { 0.0f };
    };
    ModOut modOut[maxModOut];
    std::atomic<int> modOutCount { 0 };
    std::atomic<float> volumeOut { -1.0f };   // -1 = not modulated
    static constexpr int maxPitchOut = 64;
    std::atomic<int> pitchOutZone[maxPitchOut] {}, pitchOutCount { 0 };
    std::atomic<float> pitchOutNorm[maxPitchOut] {};
    std::atomic<float> envValue[maxEnvelopes] {}, envPhase[maxEnvelopes] {};
    std::atomic<int> envStage[maxEnvelopes] {};   // 0 idle, 1 gate on, 2 releasing
    std::atomic<float> bpmUsed { 120.0f };

private:
    float sourceValue (SrcKind, int index) const noexcept;
    float modulated (float base, const std::vector<Snapshot::RouteRT>& routes, int slot) const noexcept;

    const Snapshot* snap = nullptr;
    double sr = 44100.0, bpmNow = 120.0;
    juce::int64 T = 0;
    double beat = 0.0;   // quarter notes: the DAW's position while it plays, else counted here
    juce::uint32 lastNoteOns = 0;
    bool lastGate = false;
    const float* macros = nullptr;
    int macroCount = 0;
    float wheel = 0.0f;
    int outIndex = 0;

    struct EnvState
    {
        bool gate = false, trig = false; juce::int64 t0 = 0, tr0 = 0; float l0 = 0, lr = 0, last = 0;
        double b0 = 0.0;   // beat of the last note-on
        juce::int64 cycle = -1; float rPrev = 0.5f, rNext = 0.5f; juce::uint32 seed = 12345u;   // random kind
    };
    EnvState env[maxEnvelopes];
    float envNow[maxEnvelopes] {};
};
} // namespace emx::fx
