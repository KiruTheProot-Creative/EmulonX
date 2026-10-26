// Emulon X — the FX setup as the editor sees it: chains, envelopes and modulation routes.
// Also reads and writes the prototype's preset JSON for all of it.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "FxDsp.h"
#include <juce_core/juce_core.h>
#include <map>
#include <array>

namespace emx::fx
{
/** One effect in a chain. Knob values are stored normalised (0..1). */
struct Module
{
    int id = 0;
    Type type = Type::filter;
    bool on = true;
    Settings settings;
    std::array<float, maxSlots> norm {};
    std::shared_ptr<Processor> processor;   // the running DSP (shared with the audio thread)

    float value (int slot) const;           // real units, or 0 if the slot is unused in this mode
};
using Chain = std::vector<Module>;

Module makeModule (Type, int id, double sampleRate);
/** Changes an effect's mode and resets the knobs whose meaning changes. */
void setMode (Module&, int mode);

//== Envelopes (Vital-style shapes or AHDSR) =====================================
struct EnvPoint { float x = 0, y = 0, c = 0; };

struct Envelope
{
    bool ahdsr = false;
    std::vector<EnvPoint> points;
    enum Trigger { retrig = 0, free = 1 };
    bool loop = true, sync = true;
    int trigger = retrig;   // restart with every note, or keep running freely
    int num = 1, den = 4, dot = 0;
    float hz = 2.0f;
    int grid = 8;
    float a = 0.01f, h = 0.0f, d = 0.3f, s = 0.6f, r = 0.4f, ca = 0.0f, cd = 0.4f, cr = 0.4f;
    // V3: two more kinds, timed like a shape (sync / rate): a step sequence (ARP) and a random source
    enum Kind { classic = 0, steps = 1, random = 2 };
    int kind = classic;      // classic = shape or AHDSR (see `ahdsr`)
    std::array<float, 16> stepValues { 1.0f, 0.25f, 0.6f, 0.25f, 0.9f, 0.4f, 0.7f, 0.2f, 1.0f, 0.3f, 0.6f, 0.15f, 0.85f, 0.45f, 0.7f, 0.3f };
    int numSteps = 16;
    enum RandomType { sampleHold = 0, smooth = 1, drift = 2, brownian = 3 };
    int randomType = sampleHold;

    float shapeAt (float x) const noexcept;
};

inline constexpr int maxEnvelopes = 16;
inline constexpr int maxMacros = 16;   // matches a full Ableton rack (2 controller pages of 8)
const std::vector<std::pair<juce::String, std::vector<EnvPoint>>>& shapeLibrary();
Envelope makeEnvelope (bool ahdsr, const juce::String& shape, int num = 1, int den = 4);
std::vector<Envelope> defaultEnvelopes();

//== Modulation =====================================================================
enum class SrcKind { env, macro, wheel };

struct Source
{
    SrcKind kind = SrcKind::env;
    int index = 0;
    juce::String key() const;                   // "e0", "m3", "w" (prototype format)
    static bool parse (const juce::String&, Source&);
    bool operator== (const Source& o) const noexcept { return kind == o.kind && index == o.index; }
};

struct Target
{
    static constexpr int masterVolume = -1, zonePitch = -2;
    int module = -1;   // effect id, or masterVolume, or zonePitch (then slot = zone id)
    int slot = 0;
    bool isMaster() const noexcept    { return module == masterVolume; }
    bool isZonePitch() const noexcept { return module == zonePitch; }
    juce::String key (Type) const;              // "f12:cut" or "master:vol"
    bool operator== (const Target& o) const noexcept { return module == o.module && slot == o.slot; }
};

struct Route
{
    Source source;
    Target target;
    float amount = 0.5f;   // -1..1 of the knob's range
};

//== The whole setup ================================================================
struct Setup
{
    Chain master;
    /** V3: two shared AUX buses (like Emulator X's AUX FX). Everything is sent to them before the
        whole-preset chain, their effects run fully wet and come back after it. */
    Chain aux[2];
    float auxSend[2] { 0.0f, 0.0f };   // 0..1
    std::vector<Route> routes;
    std::vector<Envelope> envelopes = defaultEnvelopes();
    float bpm = 120.0f;
};

//== JSON (same format as the browser prototype) ===================================
juce::var chainToJson (const Chain&);
/** idMap collects old module ids ("f12") -> new ids, so routes can be re-pointed. */
Chain chainFromJson (const juce::var&, std::map<juce::String, int>& idMap, int& nextId, double sampleRate);
juce::var envelopeToJson (const Envelope&);
Envelope envelopeFromJson (const juce::var&);
juce::var routeToJson (const Route&, Type targetType);
} // namespace emx::fx
