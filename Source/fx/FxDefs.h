// Emulon X — what every effect is: its modes, knobs, ranges and display format.
// Same table as the browser prototype, so presets made there load with identical settings.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace emx::fx
{
enum class Type { filter, morph, eq, dist, comp, chorus, phaser, delay, reverb, util, count };
inline constexpr int numTypes = (int) Type::count;
inline constexpr int maxSlots = 8;   // knobs per effect

enum class Fmt { db, vol, hz, hz2, pct, bi, pan, x, bits, div, ms, sec, ratio, q, vow, mor, plain };

/** One knob: range, curve and how its value is shown. */
struct ParamSpec
{
    const char* id = "";
    const char* label = "";
    float min = 0, max = 1, def = 0;
    bool log = false, integer = false;
    Fmt fmt = Fmt::plain;

    float fromNorm (float n) const noexcept;   // 0..1 -> value
    float toNorm (float v) const noexcept;     // value -> 0..1
    juce::String format (float value) const;
};

/** A knob slot of an effect. Slots marked `ab` change meaning with the effect's mode. */
struct Slot
{
    const char* id = "";
    int ab = -1;              // -1: fixed knob (spec below); otherwise index into the mode's list
    ParamSpec spec;
};

struct TypeDef
{
    Type type;
    const char* key;          // name in preset files ("dist", "delay", ...)
    const char* name;         // shown in the UI
    std::vector<const char*> modes;
    int defaultMode = 0;
    std::vector<Slot> slots;
    std::vector<std::vector<ParamSpec>> modeSpecs;   // per mode, for the `ab` slots
};

const TypeDef& def (Type);
const TypeDef* defByKey (const juce::String& key);
/** Order of the "+ ADD EFFECT" list. */
const std::array<Type, numTypes>& addOrder();

/** The spec a slot has in a given mode, or nullptr if that mode doesn't use the slot. */
const ParamSpec* specFor (Type, int mode, int slot);
int slotIndex (Type, const juce::String& paramId);

/** Master volume as a modulation target (dB, linear knob like the prototype). */
const ParamSpec& masterVolumeSpec();

//== Envelope helpers shared by the engine and the UI ============================
float curve (float u, float c) noexcept;   // c in -1..1 bends the segment
double syncSeconds (int num, int den, int dot, double bpm) noexcept;   // dot: 0 straight, 1 dotted, 2 triplet
} // namespace emx::fx
