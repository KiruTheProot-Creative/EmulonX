// Emulon X — the FX setup as the editor sees it: chains, envelopes and modulation routes.
// Also reads and writes the prototype's preset JSON for all of it.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "FxDsp.h"
#include <juce_core/juce_core.h>
#include <map>

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
    bool loop = true, retrigger = true, sync = true;
    int num = 1, den = 4, dot = 0;
    float hz = 2.0f;
    int grid = 8;
    float a = 0.01f, h = 0.0f, d = 0.3f, s = 0.6f, r = 0.4f, ca = 0.0f, cd = 0.4f, cr = 0.4f;

    float shapeAt (float x) const noexcept;
};

inline constexpr int maxEnvelopes = 16;
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
    int module = -1;   // -1 = master volume
    int slot = 0;
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
