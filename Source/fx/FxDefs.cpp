// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxDefs.h"

namespace emx::fx
{
using juce::String;

float ParamSpec::fromNorm (float n) const noexcept
{
    n = juce::jlimit (0.0f, 1.0f, n);
    float v = log ? min * std::pow (max / min, n) : min + (max - min) * n;
    return integer ? std::round (v) : v;
}

float ParamSpec::toNorm (float v) const noexcept
{
    const float n = log ? std::log (juce::jmax (1.0e-9f, v / min)) / std::log (max / min) : (v - min) / (max - min);
    return juce::jlimit (0.0f, 1.0f, n);
}

static String timeText (double s)
{
    if (s < 0.01) return String (s * 1000.0, 1) + " MS";
    if (s < 1.0)  return String (juce::roundToInt (s * 1000.0)) + " MS";
    return String (s, 2) + " S";
}

String ParamSpec::format (float v) const
{
    switch (fmt)
    {
        case Fmt::db:    return (v > 0 ? "+" : "") + String (v, 1) + " DB";
        case Fmt::vol:   return v <= -59.9f ? String ("-INF") : (v > 0 ? "+" : "") + String (v, 1) + " DB";
        case Fmt::hz:    return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " K" : String (juce::roundToInt (v)) + " HZ";
        case Fmt::hz2:   return String (v, 2) + " HZ";
        case Fmt::pct:   return String (juce::roundToInt (v * 100.0f)) + "%";
        case Fmt::bi:    { const int n = juce::roundToInt (v * 100.0f); return (n > 0 ? "+" : "") + String (n) + "%"; }
        case Fmt::pan:   { const int n = juce::roundToInt (v * 100.0f); return n == 0 ? String ("C") : n < 0 ? "L" + String (-n) : "R" + String (n); }
        case Fmt::x:     return String (v, 2) + "X";
        case Fmt::bits:  return String (juce::roundToInt (v)) + " BIT";
        case Fmt::div:   return "/" + String (v, 1);
        case Fmt::ms:    return timeText (v / 1000.0);
        case Fmt::sec:   return timeText (v);
        case Fmt::ratio: return String (v, 1) + ":1";
        case Fmt::q:     return "Q " + String (v, 2);
        case Fmt::vow:
        {
            static const char* vowels = "AEIOU";
            const float p = v * 4.0f;
            const int i = juce::jmin (3, (int) p);
            const float f = p - (float) i;
            if (f < 0.1f) return String::charToString (vowels[i]);
            if (f > 0.9f) return String::charToString (vowels[i + 1]);
            return String::charToString (vowels[i]) + ">" + String::charToString (vowels[i + 1]);
        }
        case Fmt::mor:   return v < 0.17f ? "LP" : v < 0.33f ? "LP/BP" : v < 0.67f ? "BP" : v < 0.83f ? "BP/HP" : "HP";
        case Fmt::plain: break;
    }
    return String (v, 2);
}

namespace
{
ParamSpec P (const char* id, const char* l, float mn, float mx, float d, Fmt f, bool lg = false, bool in = false)
{
    ParamSpec s;
    s.id = id; s.label = l; s.min = mn; s.max = mx; s.def = d; s.fmt = f; s.log = lg; s.integer = in;
    return s;
}
ParamSpec M (const char* l, float mn, float mx, float d, Fmt f, bool lg = false, bool in = false) { return P ("", l, mn, mx, d, f, lg, in); }
ParamSpec bias (const char* l = "BIAS") { return M (l, -1, 1, 0, Fmt::bi); }
Slot fixed (ParamSpec s) { Slot sl; sl.id = s.id; sl.spec = s; return sl; }
Slot ab (const char* id, int index) { Slot sl; sl.id = id; sl.ab = index; return sl; }

std::vector<TypeDef> buildDefs()
{
    std::vector<TypeDef> d (numTypes);

    d[(int) Type::dist] = { Type::dist, "dist", "Distortion",
        { "Soft Clip", "Hard Clip", "Tube", "Fuzz", "Foldback", "Bitcrush", "Rectify", "Sine Shaper", "Tape", "Diode" }, 0,
        { fixed (P ("drive", "DRIVE", 0, 48, 12, Fmt::db)), ab ("a", 0), ab ("b", 1),
          fixed (P ("tone", "TONE", 500, 20000, 20000, Fmt::hz, true)), fixed (P ("out", "OUTPUT", -24, 12, -6, Fmt::db)),
          fixed (P ("mix", "MIX", 0, 1, 1, Fmt::pct)) },
        { { bias(), M ("CURVE", 0.5f, 6, 2, Fmt::x) },
          { M ("CEILING", 0.05f, 1, 0.7f, Fmt::pct), bias() },
          { M ("WARMTH", 0, 1, 0.5f, Fmt::pct), bias() },
          { M ("GATE", 0, 0.2f, 0, Fmt::pct), M ("OCTAVE", 0, 1, 0, Fmt::pct) },
          { M ("FOLDS", 1, 8, 2, Fmt::x), bias ("SYMMETRY") },
          { M ("BITS", 1, 16, 8, Fmt::bits, false, true), M ("DOWNSAMPLE", 1, 64, 4, Fmt::div, true) },
          { M ("FULL WAVE", 0, 1, 1, Fmt::pct), bias() },
          { M ("HARMONICS", 1, 12, 3, Fmt::x), M ("BLEND", 0, 1, 1, Fmt::pct) },
          { M ("AGE", 0, 1, 0.3f, Fmt::pct), M ("COMPRESS", 0, 1, 0.4f, Fmt::pct) },
          { M ("ASYMMETRY", 1, 6, 2.5f, Fmt::x), M ("KNEE", 0, 1, 0.6f, Fmt::pct) } } };

    d[(int) Type::delay] = { Type::delay, "delay", "Delay", { "Stereo", "Ping-Pong", "Tape", "Diffuse" }, 0,
        { fixed (P ("time", "TIME", 1, 4000, 375, Fmt::ms, true)), fixed (P ("fb", "FEEDBACK", 0, 1, 0.4f, Fmt::pct)),
          ab ("a", 0), ab ("b", 1), fixed (P ("lo", "LOW CUT", 20, 2000, 80, Fmt::hz, true)),
          fixed (P ("hi", "HIGH CUT", 500, 20000, 9000, Fmt::hz, true)), fixed (P ("mix", "MIX", 0, 1, 0.3f, Fmt::pct)) },
        { { M ("SPREAD", 0, 1, 0.3f, Fmt::pct), M ("DUCK", 0, 1, 0, Fmt::pct) },
          { M ("WIDTH", 0, 1, 1, Fmt::pct), M ("DUCK", 0, 1, 0, Fmt::pct) },
          { M ("WOW", 0, 1, 0.3f, Fmt::pct), M ("SATURATE", 0, 1, 0.4f, Fmt::pct) },
          { M ("DIFFUSION", 0, 1, 0.6f, Fmt::pct), M ("SIZE", 0, 1, 0.5f, Fmt::pct) } } };

    d[(int) Type::filter] = { Type::filter, "filter", "Filter", { "Low Pass", "High Pass", "Mid (Band)", "Notch" }, 0,
        { fixed (P ("cut", "CUTOFF", 20, 20000, 2000, Fmt::hz, true)), fixed (P ("res", "RESONANCE", 0, 1, 0.2f, Fmt::pct)),
          fixed (P ("drive", "DRIVE", 0, 24, 0, Fmt::db)), fixed (P ("mix", "MIX", 0, 1, 1, Fmt::pct)) }, {} };

    d[(int) Type::morph] = { Type::morph, "morph", "Morph Filter", { "Vowel", "Sweep" }, 0,
        { ab ("a", 0), ab ("b", 1), fixed (P ("res", "RESONANCE", 0, 1, 0.6f, Fmt::pct)),
          fixed (P ("drive", "DRIVE", 0, 18, 0, Fmt::db)), fixed (P ("mix", "MIX", 0, 1, 1, Fmt::pct)) },
        { { M ("MORPH", 0, 1, 0, Fmt::vow), M ("SHIFT", 0.5f, 2, 1, Fmt::x, true) },
          { M ("MORPH", 0, 1, 0, Fmt::mor), M ("CUTOFF", 40, 16000, 1200, Fmt::hz, true) } } };

    d[(int) Type::eq] = { Type::eq, "eq", "EQ", { "3-Band" }, 0,
        { fixed (P ("lg", "LOW", -18, 18, 0, Fmt::db)), fixed (P ("lf", "LOW FREQ", 30, 600, 120, Fmt::hz, true)),
          fixed (P ("mg", "MID", -18, 18, 0, Fmt::db)), fixed (P ("mf", "MID FREQ", 200, 8000, 1000, Fmt::hz, true)),
          fixed (P ("mq", "MID Q", 0.3f, 8, 1, Fmt::q, true)), fixed (P ("hg", "HIGH", -18, 18, 0, Fmt::db)),
          fixed (P ("hf", "HIGH FREQ", 1500, 16000, 6000, Fmt::hz, true)) }, {} };

    d[(int) Type::comp] = { Type::comp, "comp", "Compressor", { "Compressor", "Limiter", "OTT" }, 0,
        { ab ("a", 0), ab ("b", 1), ab ("c", 2), ab ("d", 3), fixed (P ("out", "OUTPUT", -24, 24, 0, Fmt::db)),
          fixed (P ("mix", "MIX", 0, 1, 1, Fmt::pct)), ab ("e", 4) },
        { { M ("THRESHOLD", -60, 0, -18, Fmt::db), M ("RATIO", 1, 20, 4, Fmt::ratio, true),
            M ("ATTACK", 0.1f, 100, 10, Fmt::ms, true), M ("RELEASE", 10, 1000, 150, Fmt::ms, true) },
          { M ("CEILING", -24, 0, -1, Fmt::db), M ("RELEASE", 5, 500, 60, Fmt::ms, true), M ("INPUT", 0, 24, 0, Fmt::db) },
          { M ("TIME", 0, 1, 0.5f, Fmt::pct), M ("UPWARD", 0, 1, 1, Fmt::pct), M ("DOWNWARD", 0, 1, 1, Fmt::pct),
            M ("INPUT", -12, 24, 0, Fmt::db), M ("INTENSITY", 0, 2, 1, Fmt::pct) } } };

    d[(int) Type::chorus] = { Type::chorus, "chorus", "Chorus", { "Chorus", "Flanger", "Ensemble" }, 0,
        { fixed (P ("rate", "RATE", 0.05f, 10, 0.8f, Fmt::hz2, true)), fixed (P ("depth", "DEPTH", 0, 1, 0.5f, Fmt::pct)),
          ab ("a", 0), ab ("b", 1), fixed (P ("mix", "MIX", 0, 1, 0.5f, Fmt::pct)) },
        { { M ("DELAY", 5, 40, 15, Fmt::ms), M ("WIDTH", 0, 1, 1, Fmt::pct) },
          { M ("DELAY", 0.2f, 10, 2, Fmt::ms, true), M ("FEEDBACK", -0.95f, 0.95f, 0.6f, Fmt::bi) },
          { M ("DELAY", 5, 30, 12, Fmt::ms), M ("SPREAD", 0, 1, 0.8f, Fmt::pct) } } };

    d[(int) Type::phaser] = { Type::phaser, "phaser", "Phaser", { "4 Stage", "8 Stage", "12 Stage" }, 1,
        { fixed (P ("rate", "RATE", 0.02f, 8, 0.4f, Fmt::hz2, true)), fixed (P ("depth", "DEPTH", 0, 1, 0.7f, Fmt::pct)),
          fixed (P ("center", "CENTER", 100, 5000, 800, Fmt::hz, true)), fixed (P ("fb", "FEEDBACK", -0.95f, 0.95f, 0.5f, Fmt::bi)),
          fixed (P ("stereo", "STEREO", 0, 1, 0.5f, Fmt::pct)), fixed (P ("mix", "MIX", 0, 1, 0.5f, Fmt::pct)) }, {} };

    d[(int) Type::reverb] = { Type::reverb, "reverb", "Reverb", { "Room", "Hall", "Plate", "Ambience", "Cathedral", "Spring", "Shimmer" }, 1,
        { fixed (P ("size", "SIZE", 0, 1, 0.5f, Fmt::pct)), fixed (P ("decay", "DECAY", 0.1f, 20, 2.5f, Fmt::sec, true)),
          fixed (P ("pre", "PRE-DELAY", 0, 250, 10, Fmt::ms)), fixed (P ("damp", "DAMPING", 1000, 20000, 7000, Fmt::hz, true)),
          fixed (P ("lo", "LOW CUT", 20, 1000, 120, Fmt::hz, true)), fixed (P ("width", "WIDTH", 0, 1, 1, Fmt::pct)),
          fixed (P ("mix", "MIX", 0, 1, 0.25f, Fmt::pct)) }, {} };

    d[(int) Type::spectral] = { Type::spectral, "spectral", "Spectral", { "Spectral" }, 0,
        { fixed (P ("reso", "RESONANCE", 0, 1, 0.4f, Fmt::pct)), fixed (P ("smear", "SMEAR", 0, 1, 0.3f, Fmt::pct)),
          fixed (P ("tilt", "TILT", -1, 1, 0, Fmt::bi)), fixed (P ("mix", "MIX", 0, 1, 0.5f, Fmt::pct)) }, {} };

    d[(int) Type::util] = { Type::util, "util", "Utility", { "Stereo" }, 0,
        { fixed (P ("gain", "GAIN", -36, 12, 0, Fmt::db)), fixed (P ("pan", "PAN", -1, 1, 0, Fmt::pan)),
          fixed (P ("width", "WIDTH", 0, 2, 1, Fmt::pct)) }, {} };

    return d;
}

const std::vector<TypeDef>& defs()
{
    static const auto table = buildDefs();
    return table;
}
} // namespace

const TypeDef& def (Type t) { return defs()[(size_t) juce::jlimit (0, numTypes - 1, (int) t)]; }

const TypeDef* defByKey (const String& key)
{
    for (auto& d : defs())
        if (key == d.key)
            return &d;
    return nullptr;
}

const std::array<Type, numTypes>& addOrder()
{
    static const std::array<Type, numTypes> order { Type::filter, Type::morph, Type::eq, Type::dist, Type::comp,
                                                    Type::chorus, Type::phaser, Type::delay, Type::reverb, Type::spectral, Type::util };
    return order;
}

const ParamSpec* specFor (Type t, int mode, int slot)
{
    const auto& d = def (t);
    if (slot < 0 || slot >= (int) d.slots.size())
        return nullptr;
    const auto& s = d.slots[(size_t) slot];
    if (s.ab < 0)
        return &s.spec;
    if (mode < 0 || mode >= (int) d.modeSpecs.size() || s.ab >= (int) d.modeSpecs[(size_t) mode].size())
        return nullptr;
    return &d.modeSpecs[(size_t) mode][(size_t) s.ab];
}

int slotIndex (Type t, const String& paramId)
{
    const auto& d = def (t);
    for (size_t i = 0; i < d.slots.size(); ++i)
        if (paramId == d.slots[i].id)
            return (int) i;
    return -1;
}

const ParamSpec& masterVolumeSpec()
{
    static const ParamSpec s = P ("vol", "VOLUME", -60, 6, 0, Fmt::vol);
    return s;
}

float curve (float u, float c) noexcept
{
    if (std::abs (c) < 1.0e-3f)
        return u;
    const float k = c * 8.0f;
    return (std::exp (k * u) - 1.0f) / (std::exp (k) - 1.0f);
}

double syncSeconds (int num, int den, int dot, double bpm) noexcept
{
    static const double mult[] { 1.0, 1.5, 2.0 / 3.0 };
    return (double) num / (double) juce::jmax (1, den) * 240.0 / juce::jmax (1.0, bpm) * mult[juce::jlimit (0, 2, dot)];
}
} // namespace emx::fx
