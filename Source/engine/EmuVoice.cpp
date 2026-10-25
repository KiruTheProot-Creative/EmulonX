// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EmuVoice.h"
#include <cmath>
#include <iterator>

namespace emx::emu
{
using namespace juce;
static void prepareFilter (VoiceState&, const Design&, double sr) noexcept;

//== Names ===============================================================================
const StringArray& filterNames()
{
    static const StringArray names = []
    {
        StringArray n;
        for (int i = 0; i < 256; ++i) n.add ({});
        auto set = [&n] (int i, const char* s) { n.set (i, s); };
        set (0, "4-Pole Lowpass"); set (1, "2-Pole Lowpass"); set (2, "6-Pole Lowpass");
        set (8, "2nd Order Highpass"); set (9, "4th Order Highpass");
        set (16, "2nd Order Bandpass"); set (17, "4th Order Bandpass"); set (18, "Contrary Bandpass");
        set (32, "Swept EQ 1-Octave"); set (33, "Swept EQ 2>1-Octave"); set (34, "Swept EQ 3>1-Octave");
        set (64, "Phaser 1"); set (65, "Phaser 2"); set (66, "Bat-Phaser"); set (72, "Flanger Lite");
        set (80, "Vocal Ah-Ay-Ee"); set (81, "Vocal Oo-Ah");
        set (127, "Off");
        for (int i = 128; i < 256; ++i) if (n[i].isEmpty()) n.set (i, "Morph " + String (i));
        return n;
    }();
    return names;
}

String sourceName (int s)
{
    switch (s)
    {
        case 0: return "Off";          case 8: return "Key +";       case 9: return "Key ~";
        case 10: return "Velocity +";  case 11: return "Velocity ~"; case 12: return "Velocity <";
        case 13: return "Release Vel"; case 14: return "Gate";       case 16: return "Pitch Wheel";
        case 17: return "Mod Wheel";   case 18: return "Pressure";   case 19: return "Pedal";
        case 20: return "MIDI A";      case 21: return "MIDI B";     case 22: return "Footswitch 1";
        case 23: return "Footswitch 2"; case 32: return "MIDI C";    case 33: return "MIDI D";
        case 34: return "MIDI E";      case 35: return "MIDI F";     case 36: return "MIDI G";
        case 37: return "MIDI H";      case 40: return "MIDI I";     case 41: return "MIDI J";
        case 42: return "MIDI K";      case 43: return "MIDI L";     case 44: return "MIDI M";
        case 45: return "MIDI N";      case 46: return "MIDI O";     case 47: return "MIDI P";
        case 72: return "Amp Env +";   case 73: return "Amp Env ~";  case 74: return "Amp Env <";
        case 80: return "Filter Env +"; case 81: return "Filter Env ~"; case 82: return "Filter Env <";
        case 88: return "Aux Env +";   case 89: return "Aux Env ~";  case 90: return "Aux Env <";
        case 96: return "LFO 1 ~";     case 97: return "LFO 1 +";    case 98: return "White Noise";
        case 99: return "Pink Noise";  case 100: return "kRandom 1"; case 101: return "kRandom 2";
        case 104: return "LFO 2 ~";    case 105: return "LFO 2 +";   case 144: return "DC";
        default: return "Source " + String (s);
    }
}

String destName (int d)
{
    // Emulator X's destination list (confirmed against Orbit-3 "amb:Galactic End")
    if (d >= 168 && d < 204) return "Cord " + String (d - 167).paddedLeft ('0', 2) + " Amt";
    switch (d)
    {
        case 0: return "Off";                 case 8: return "Key Sustain";
        case 47: return "Fine Pitch";         case 48: return "Pitch";            case 49: return "Glide Rate";
        case 50: return "Chorus Amt";         case 51: return "Chorus Initial ITD"; case 52: return "Sample Start";
        case 53: return "Sample Loop";        case 54: return "Sample Retrigger (+)"; case 55: return "Sample Retrigger (-)";
        case 56: return "Filter Frequency";   case 57: return "Filter Resonance"; case 58: return "Realtime Resonance";
        case 64: return "Amp Volume";         case 65: return "Amp Pan";          case 66: return "Amp Crossfade";
        case 67: return "Send Main";          case 68: return "Send Aux 1";       case 69: return "Send Aux 2";
        case 70: return "Send Aux 3";         case 71: return "Wet/Dry Mix";
        case 72: return "Amp Env Rates";      case 73: return "Amp Env Attack";   case 74: return "Amp Env Decay";
        case 75: return "Amp Env Release";    case 76: return "Amp Env Sustain";
        case 80: return "Filter Env Rates";   case 81: return "Filter Env Attack"; case 82: return "Filter Env Decay";
        case 83: return "Filter Env Release"; case 84: return "Filter Env Sustain"; case 85: return "Filter Env Trigger";
        case 88: return "Aux Env Rates";      case 89: return "Aux Env Attack";   case 90: return "Aux Env Decay";
        case 91: return "Aux Env Release";    case 92: return "Aux Env Sustain";  case 93: return "Aux Env Trigger";
        case 96: return "LFO 1 Frequency";    case 97: return "LFO 1 Trigger";
        case 104: return "LFO 2 Frequency";   case 105: return "LFO 2 Trigger";
        default: return "Destination " + String (d);
    }
}

double rateSeconds (double rate) noexcept
{
    // measured in Emulator X (Orbit-3 "amb:Galactic End", Planet Earth "wnd:East Winds"), joined on a log scale;
    // E-mu's longest time is 163.6 s
    static const double pts[][2] { { 0.0, 0.0 }, { 4.0, 0.072 }, { 9.5, 0.175 }, { 10.0, 0.192 }, { 20.0, 0.661 }, { 23.5, 0.995 },
                                   { 28.5, 1.763 }, { 32.0, 2.622 }, { 38.0, 5.138 }, { 43.0, 8.997 }, { 50.0, 20.12 }, { 51.79, 24.9 },
                                   { 59.5, 73.6 }, { 61.23, 100.3 }, { 62.0, 118.71 }, { 63.6, 163.6 } };
    rate = jlimit (0.0, 127.0, rate);
    if (rate <= 0.0) return 0.0;
    if (rate <= 4.0) return 0.072 * rate / 4.0;   // (the log curve starts at the first measured point)
    for (size_t i = 2; i < std::size (pts); ++i)
        if (rate <= pts[i][0])
        {
            const double t = (rate - pts[i - 1][0]) / (pts[i][0] - pts[i - 1][0]);
            return std::exp (std::log (pts[i - 1][1]) + t * (std::log (pts[i][1]) - std::log (pts[i - 1][1])));
        }
    return 163.6;
}

//== Saving ==============================================================================
var Design::toJson() const
{
    auto* o = new DynamicObject();
    Array<var> envs;
    for (auto& e : env)
    {
        Array<var> r, l;
        for (int i = 0; i < 6; ++i) { r.add (e.rate[(size_t) i]); l.add (e.level[(size_t) i]); }
        auto* eo = new DynamicObject();
        eo->setProperty ("rate", r);
        eo->setProperty ("level", l);
        envs.add (var (eo));
    }
    o->setProperty ("env", envs);
    o->setProperty ("filter", filterType);
    o->setProperty ("cutoff", cutoff);
    o->setProperty ("res", resonance);
    Array<var> lfos;
    for (auto& f : lfo)
    {
        auto* lo = new DynamicObject();
        lo->setProperty ("shape", f.shape); lo->setProperty ("hz", f.hz); lo->setProperty ("sync", f.sync);
        lo->setProperty ("div", f.division); lo->setProperty ("delay", f.delay); lo->setProperty ("vari", f.variation);
        lo->setProperty ("keySync", f.keySync);
        lfos.add (var (lo));
    }
    o->setProperty ("lfo", lfos);
    o->setProperty ("lag", Array<var> { lagRate[0], lagRate[1] });
    Array<var> cs;
    for (auto& c : cords)
        cs.add (Array<var> { c.source, c.dest, c.amount });
    o->setProperty ("cords", cs);
    o->setProperty ("chorus", Array<var> { chorusAmount, chorusWidth });
    o->setProperty ("bend", Array<var> { bendUp, bendDown });
    o->setProperty ("ampRange", ampEnvRange);
    return var (o);
}

std::shared_ptr<const Design> Design::fromJson (const var& j)
{
    if (! j.isObject())
        return {};
    auto d = std::make_shared<Design>();
    auto num = [] (const var& v, float def) { return v.isVoid() ? def : (float) (double) v; };
    if (auto* envs = j["env"].getArray())
        for (int e = 0; e < 3 && e < envs->size(); ++e)
            for (int i = 0; i < 6; ++i)
            {
                d->env[(size_t) e].rate[(size_t) i] = jlimit (0.0f, 127.0f, num ((*envs)[e]["rate"][i], 0.0f));
                d->env[(size_t) e].level[(size_t) i] = jlimit (-100.0f, 100.0f, num ((*envs)[e]["level"][i], 0.0f));
            }
    d->filterType = jlimit (0, 255, (int) num (j["filter"], 127.0f));
    d->cutoff = jlimit (0.0f, 1.0f, num (j["cutoff"], 1.0f));
    d->resonance = jlimit (0.0f, 1.0f, num (j["res"], 0.0f));
    if (auto* lfos = j["lfo"].getArray())
        for (int i = 0; i < 2 && i < lfos->size(); ++i)
        {
            const auto& l = (*lfos)[i];
            auto& f = d->lfo[(size_t) i];
            f.shape = (int) num (l["shape"], 0.0f); f.hz = jlimit (0.0f, 100.0f, num (l["hz"], 1.0f));
            f.sync = (bool) l["sync"]; f.division = (int) num (l["div"], 0.0f); f.delay = num (l["delay"], 0.0f);
            f.variation = num (l["vari"], 0.0f); f.keySync = (bool) l["keySync"];
        }
    d->lagRate = { num (j["lag"][0], 0.0f), num (j["lag"][1], 0.0f) };
    if (auto* cs = j["cords"].getArray())
        for (auto& c : *cs)
            if (d->cords.size() < 36)
                d->cords.push_back ({ (int) num (c[0], 0.0f), (int) num (c[1], 0.0f), jlimit (-100.0f, 100.0f, num (c[2], 0.0f)) });
    d->chorusAmount = num (j["chorus"][0], 0.0f);
    d->chorusWidth = num (j["chorus"][1], 100.0f);
    d->bendUp = (int) num (j["bend"][0], 2.0f);
    d->bendDown = (int) num (j["bend"][1], 2.0f);
    d->ampEnvRange = num (j["ampRange"], 96.0f);
    return d;
}

//== Envelopes ===========================================================================
namespace
{
/** Sets up a stage. Attack and decay times depend on how far the level moves; release times don't. */
void beginStage (EnvState& e, const Envelope& d, int stage, float timeScale, double sr)
{
    e.stage = stage;
    if (stage >= 6)
        return;
    e.from = e.level;
    e.pos = 0.0f;
    if (stage == 3 && d.rate[(size_t) stage] <= 0.0f)
    {
        // decay 2 at rate 0 means "no second decay": the level reached by decay 1 is held (E-mu's sustain);
        // every other stage at rate 0 is instant
        e.len = 0.0f;
        e.skip = true;
        return;
    }
    e.skip = false;
    const float target = d.level[(size_t) stage] / 100.0f;
    const double full = rateSeconds (d.rate[(size_t) stage]) * timeScale;
    const double t = stage >= 4 ? (std::abs (target - e.from) > 1.0e-4f ? full : 0.0) : full * std::abs (target - e.from);
    e.len = (float) (t * sr);
}

void runEnv (EnvState& e, const Envelope& d, float timeScale, double sr, int n, float sustainMod)
{
    while (e.stage < 6 && n > 0)
    {
        float target = e.skip ? e.level : d.level[(size_t) e.stage] / 100.0f;
        if (e.stage == 3)
            target = jlimit (-1.0f, 1.0f, (e.skip ? e.level : target) + sustainMod * (e.skip ? 0.0f : 1.0f));
        if (e.len <= 0.5f || e.pos + (float) n >= e.len)
        {
            n -= jmax (0, (int) (e.len - e.pos));
            e.level = target;
            if (e.stage == 3)
                return;                                 // decay 2 holds its level until the key is let go
            beginStage (e, d, e.stage + 1, timeScale, sr);
            continue;
        }
        e.pos += (float) n;
        e.level = e.from + (target - e.from) * (e.pos / e.len);
        return;
    }
}

float lfoWave (int shape, double ph, float held) noexcept
{
    const double p = ph - std::floor (ph);
    switch (shape)
    {
        case 1:  return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);    // triangle
        case 2:  return p < 0.5 ? 1.0f : -1.0f;                                // square
        case 3:  return (float) (2.0 * p - 1.0);                               // saw
        case 4:  return held;                                                  // random (sample and hold)
        default: return (float) std::sin (2.0 * MathConstants<double>::pi * p); // sine
    }
}
} // namespace

void startNote (VoiceState& s, const Design& d, Random& rnd)
{
    auto* comb = s.comb;
    const int combSize = s.combSize;
    s = VoiceState {};
    s.comb = comb;
    s.combSize = combSize;
    if (comb != nullptr)
        std::fill (comb, comb + 2 * combSize, 0.0f);
    for (int i = 0; i < 3; ++i)
    {
        s.env[(size_t) i].level = 0.0f;
        beginStage (s.env[(size_t) i], d.env[(size_t) i], 0, 1.0f, 48000.0);   // real length set on the first control call
        s.env[(size_t) i].len = -1.0f;
    }
    for (int i = 0; i < 2; ++i)
    {
        const auto& l = d.lfo[(size_t) i];
        s.lfoPhase[(size_t) i] = l.keySync ? 0.0 : rnd.nextDouble();
        s.lfoRateScale[(size_t) i] = 1.0f + (rnd.nextFloat() * 2.0f - 1.0f) * l.variation / 100.0f;
        s.lfoHeld[(size_t) i] = rnd.nextFloat() * 2.0f - 1.0f;
    }
    s.random = { rnd.nextFloat(), rnd.nextFloat() };
}

void releaseNote (VoiceState& s)
{
    s.released = true;
    for (auto& e : s.env)
        if (e.stage < 4)
        {
            e.stage = 4;
            e.len = -1.0f;   // set up on the next control call
        }
}

bool control (VoiceState& s, const Design& d, const Inputs& in, int note, int velocity, double sr, int n) noexcept
{
    const float vel = (float) velocity / 127.0f;
    auto source = [&] (int id) -> float
    {
        switch (id)
        {
            case 8:  return (float) note / 127.0f;
            case 9:  return ((float) note - 64.0f) / 64.0f;
            case 10: return vel;
            case 11: return vel * 2.0f - 1.0f;
            case 12: return -std::pow (1.0f - vel, 1.5f);           // velocity curve for loudness
            case 13: return vel;
            case 14: return s.released ? 0.0f : 1.0f;
            case 16: return in.pitchWheel;
            case 17: return in.modWheel;
            case 18: return in.pressure;
            case 19: return in.pedal;
            case 22: return in.footswitch1 ? 1.0f : 0.0f;
            case 23: return in.footswitch2 ? 1.0f : 0.0f;
            case 20: return in.midi[0];  case 21: return in.midi[1];
            case 32: return in.midi[2];  case 33: return in.midi[3];  case 34: return in.midi[4];  case 35: return in.midi[5];
            case 36: return in.midi[6];  case 37: return in.midi[7];  case 40: return in.midi[8];  case 41: return in.midi[9];
            case 42: return in.midi[10]; case 43: return in.midi[11]; case 44: return in.midi[12]; case 45: return in.midi[13];
            case 46: return in.midi[14]; case 47: return in.midi[15];
            case 72: return jmax (0.0f, s.env[0].level);  case 73: return s.env[0].level * 2.0f - 1.0f; case 74: return -jmax (0.0f, s.env[0].level);
            case 80: return jmax (0.0f, s.env[1].level);  case 81: return s.env[1].level;               case 82: return -jmax (0.0f, s.env[1].level);
            case 88: return jmax (0.0f, s.env[2].level);  case 89: return s.env[2].level;               case 90: return -jmax (0.0f, s.env[2].level);
            case 96: case 97: case 104: case 105:
            {
                const int i = id >= 104 ? 1 : 0;
                const auto& l = d.lfo[(size_t) i];
                const float fade = l.delay <= 0.0f ? 1.0f : jlimit (0.0f, 1.0f, (s.lfoAge[(size_t) i] - l.delay) / 0.1f);
                const float w = lfoWave (l.shape, s.lfoPhase[(size_t) i], s.lfoHeld[(size_t) i]) * fade;
                return (id & 1) ? (w + 1.0f) * 0.5f : w;
            }
            case 98: return s.noise;
            case 99: return s.pink;
            case 100: return s.random[0];
            case 101: return s.random[1];
            case 144: return 1.0f;
            default: return 0.0f;
        }
    };

    // cords that change other cords' amounts go first
    float amount[36];
    for (size_t i = 0; i < d.cords.size() && i < 36; ++i)
        amount[i] = d.cords[i].amount;
    for (size_t i = 0; i < d.cords.size() && i < 36; ++i)
    {
        const auto& c = d.cords[i];
        if (c.dest >= 168 && c.dest < 204 && (size_t) (c.dest - 168) < d.cords.size())
            amount[c.dest - 168] += source (c.source) * c.amount;
    }
    float dest[168] {};
    for (size_t i = 0; i < d.cords.size() && i < 36; ++i)
    {
        const auto& c = d.cords[i];
        if (c.dest > 0 && c.dest < 168)
            dest[c.dest] += source (c.source) * jlimit (-100.0f, 100.0f, amount[i]) / 100.0f;
    }

    // envelopes (positive rate changes make them faster)
    auto scale = [&] (int rates, int stageDest) { return (float) std::exp2 (-4.0f * (dest[rates] + dest[stageDest])); };
    for (int e = 0; e < 3; ++e)
    {
        auto& st = s.env[(size_t) e];
        const int base = e == 0 ? 72 : e == 1 ? 80 : 88;
        const int phaseDest = st.stage < 2 ? base + 1 : st.stage < 4 ? base + 2 : base + 3;
        const float ts = scale (base, phaseDest);
        if (st.len < 0.0f)
            beginStage (st, d.env[(size_t) e], st.stage, ts, sr);
        runEnv (st, d.env[(size_t) e], ts, sr, n, dest[base + 4]);
    }

    // LFOs, noise, lags
    for (int i = 0; i < 2; ++i)
    {
        const auto& l = d.lfo[(size_t) i];
        const float rateMod = dest[i == 0 ? 96 : 104];
        double baseHz = l.hz;
        if (l.sync && l.division == 25)
            baseHz = in.bpm / 60.0 / 32.0;   // "8/1": eight bars (other tempo divisions use the stored rate until checked)
        const double hz = jlimit (0.0, 50.0, baseHz * s.lfoRateScale[(size_t) i] * std::exp2 (3.0 * rateMod));
        const double before = s.lfoPhase[(size_t) i];
        s.lfoPhase[(size_t) i] += hz * n / sr;
        if ((juce::int64) std::floor (s.lfoPhase[(size_t) i]) != (juce::int64) std::floor (before))
        {
            const double r = std::sin (s.lfoPhase[(size_t) i] * 12.9898 + (double) i) * 43758.5453;   // a new random step
            s.lfoHeld[(size_t) i] = (float) (r - std::floor (r)) * 2.0f - 1.0f;
        }
        s.lfoAge[(size_t) i] += (float) (n / sr);
    }
    s.noise = (float) (std::sin ((double) s.lfoPhase[0] * 78.233 + s.lfoAge[0] * 1.0e4) * 2.0 - 1.0);
    s.pink += (s.noise - s.pink) * 0.05f;

    // results
    const float ampEnv = jmax (0.0f, s.env[0].level);
    const float volDb = jlimit (-96.0f, 10.0f, 48.0f * dest[64]);   // E-mu's amp volume range tops out at +10 dB
    s.ampGain = ampEnv * Decibels::decibelsToGain (volDb, -100.0f);
    s.pitchSemis = dest[47] + (12.0f / 0.3721f) * dest[48];   // fine pitch: 100 % = 1 semitone; pitch: 37.21 % = 1 octave
    s.pan = 100.0f * dest[65];
    // filter frequency cords: 100 % moves half of the 57 Hz - 20 kHz range (so a full Tone knob plus a full
    // filter envelope doesn't shut a high pass or overshoot); to be checked by ear against Emulator X
    s.cutoff = jlimit (0.0f, 1.0f, d.cutoff + 0.5f * dest[56]);
    s.resonance = jlimit (0.0f, 1.0f, d.resonance + dest[57]);
    prepareFilter (s, d, sr);
    return s.env[0].stage < 6;
}

//== Filters =============================================================================
namespace
{
struct Svf { float g, k, a1, a2, a3; };

Svf makeSvf (double hz, double q, double sr) noexcept
{
    Svf f;
    f.g = (float) std::tan (MathConstants<double>::pi * jlimit (10.0, sr * 0.45, hz) / sr);
    f.k = (float) (1.0 / jmax (0.3, q));
    f.a1 = 1.0f / (1.0f + f.g * (f.g + f.k));
    f.a2 = f.g * f.a1;
    f.a3 = f.g * f.a2;
    return f;
}

/** One state-variable stage (Cytomic): returns low, band and high pass. */
inline void svf (const Svf& f, float* z, float x, float& lp, float& bp, float& hp) noexcept
{
    const float v3 = x - z[1];
    const float v1 = f.a1 * z[0] + f.a2 * v3;
    const float v2 = z[1] + f.a2 * z[0] + f.a3 * v3;
    z[0] = 2.0f * v1 - z[0];
    z[1] = 2.0f * v2 - z[1];
    lp = v2; bp = v1; hp = x - f.k * v1 - v2;
}

double cutoffHz (float c) noexcept { return 57.0 * std::pow (20000.0 / 57.0, (double) c); }   // E-mu's range

/** Vowel formants (Hz) for the vocal filters and the morph approximations. */
const float vowels[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };   // ah eh ee oh oo
} // namespace

namespace
{
void storeSvf (float* c, double hz, double q, double sr) noexcept
{
    const auto f = makeSvf (hz, q, sr);
    c[0] = f.g; c[1] = f.k; c[2] = f.a1; c[3] = f.a2; c[4] = f.a3;
}
inline Svf loadSvf (const float* c) noexcept { return { c[0], c[1], c[2], c[3], c[4] }; }
} // namespace

/** Works out the filter's coefficients for the current cutoff and resonance (once per control step). */
static void prepareFilter (VoiceState& s, const Design& d, double sr) noexcept
{
    const int t = d.filterType;
    const float res = s.resonance;
    const double hz = cutoffHz (s.cutoff);
    if (t <= 2 || t == 96 || t == 97 || t == 104 || t == 8 || t == 9)
    {
        storeSvf (s.coef[0], hz, 0.5 + res * res * 9.5, sr);   // the resonant stage
        storeSvf (s.coef[1], hz, 0.54, sr);               // the others
    }
    else if (t >= 16 && t <= 18)
        storeSvf (s.coef[0], hz, 0.7 + res * res * 8.0, sr);
    else if (t >= 32 && t <= 34)
    {
        storeSvf (s.coef[0], hz, t == 32 ? 1.4 : t == 33 ? 1.0 : 0.7, sr);
        s.aux = Decibels::decibelsToGain (res * 24.0f) - 1.0f;
    }
    else if (t >= 64 && t <= 66)
    {
        const double w = std::tan (MathConstants<double>::pi * jlimit (20.0, sr * 0.45, hz) / sr);
        s.aux = (float) ((w - 1.0) / (w + 1.0));
    }
    else if (t == 72)
        s.aux = (float) jlimit (2.0, (double) s.combSize - 2.0, sr / jmax (20.0, hz));
    else if (t == 80 || t == 81)
    {
        // vocal: morph through the vowels with the frequency (ah > ay > ee, or oo > ah)
        const int path80[3] { 0, 1, 2 }, path81[2] { 4, 0 };
        const int* path = t == 80 ? path80 : path81;
        const int steps = t == 80 ? 2 : 1;
        const float pos = s.cutoff * (float) steps;
        const int a = jlimit (0, steps - 1, (int) pos);
        const float u = jlimit (0.0f, 1.0f, pos - (float) a);
        for (int p = 0; p < 3; ++p)
            storeSvf (s.coef[p], vowels[path[a]][p] + (vowels[path[a + 1]][p] - vowels[path[a]][p]) * u, 4.0 + res * 16.0, sr);
    }
    else if (t != 127)
    {
        // E-mu's Z-plane morph filters: approximated as three resonant peaks that move with the frequency.
        // Each type gets its own character from its number (not exact, but it keeps their colour and movement).
        const uint32 seed = (uint32) t * 2654435761u;
        for (int p = 0; p < 3; ++p)
        {
            const float spread = (float) ((seed >> (p * 8)) & 255) / 255.0f;
            const float base = 200.0f * std::pow (2.0f, (float) p * 1.6f + spread * 1.5f);
            storeSvf (s.coef[p], jlimit (60.0f, (float) sr * 0.42f, base * std::pow (2.0f, (s.cutoff - 0.5f) * 5.0f)), 2.0 + res * 18.0, sr);
        }
    }
}

static void filterRaw (VoiceState& s, const Design& d, float& L, float& R) noexcept;

void filter (VoiceState& s, const Design& d, double sr, float& L, float& R) noexcept
{
    (void) sr;
    if (d.filterType == 127)
        return;
    // like E-mu's filters, more resonance makes the rest of the sound quieter (so the peak doesn't explode);
    // phasers and the flanger only cut notches, so they keep their level
    const bool notches = d.filterType >= 64 && d.filterType <= 72;
    const float comp = notches ? 1.0f : 1.0f / (1.0f + s.resonance * s.resonance * 2.0f);
    L *= comp;
    R *= comp;
    filterRaw (s, d, L, R);
    // safety: soft-limit anything far above full scale (a filter pushed hard by its cords)
    auto limit = [] (float x) { return std::abs (x) < 2.0f ? x : (x > 0.0f ? 1.0f : -1.0f) * (2.0f + std::tanh (std::abs (x) - 2.0f)); };
    L = limit (L);
    R = limit (R);
}

static void filterRaw (VoiceState& s, const Design& d, float& L, float& R) noexcept
{
    const int t = d.filterType;
    const float res = s.resonance;
    float* io[2] { &L, &R };

    if (t <= 2 || t == 96 || t == 97 || t == 104 || t == 8 || t == 9)
    {
        const bool high = t == 8 || t == 9;
        const int stages = t == 0 ? 2 : t == 2 ? 3 : t == 9 ? 2 : 1;
        const auto f = loadSvf (s.coef[0]), soft = loadSvf (s.coef[1]);
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = *io[ch];
            for (int st = 0; st < stages; ++st)
            {
                float lp, bp, hp;
                svf (st == stages - 1 ? f : soft, s.z[ch] + st * 2, x, lp, bp, hp);
                x = high ? hp : lp;
            }
            *io[ch] = x;
        }
    }
    else if (t >= 16 && t <= 18)
    {
        const auto f = loadSvf (s.coef[0]);
        const int stages = t == 17 ? 2 : 1;
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = *io[ch];
            for (int st = 0; st < stages; ++st)
            {
                float lp, bp, hp;
                svf (f, s.z[ch] + st * 2, x, lp, bp, hp);
                x = t == 18 ? lp + hp : bp * f.k;
            }
            *io[ch] = x;
        }
    }
    else if (t >= 32 && t <= 34)
    {
        const auto f = loadSvf (s.coef[0]);
        for (int ch = 0; ch < 2; ++ch)
        {
            float lp, bp, hp;
            svf (f, s.z[ch], *io[ch], lp, bp, hp);
            *io[ch] += bp * f.k * s.aux;
        }
    }
    else if (t >= 64 && t <= 66)
    {
        // phaser: first-order allpass stages around the frequency added to the dry sound. That cuts notches
        // (two for Phaser 1) without taking the highs or lows away; resonance makes the notches deeper and
        // adds a little feedback for the peaks between them. Scaled so the average level stays the same.
        const int stages = t == 64 ? 4 : t == 65 ? 6 : 8;
        const float a = s.aux;
        const float depth = 0.55f + 0.45f * res, fbAmt = 0.35f * res;
        const float norm = 1.0f / std::sqrt (1.0f + depth * depth);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = *io[ch];
            float x = dry + s.fb[ch] * fbAmt;
            for (int st = 0; st < stages; ++st)
            {
                const float y = a * x + s.z[ch][st];
                s.z[ch][st] = x - a * y;
                x = y;
            }
            s.fb[ch] = x;
            *io[ch] = (dry + depth * x) * norm * (1.0f - 0.5f * fbAmt);
        }
    }
    else if (t == 72)
    {
        // flanger: a short comb filter tuned by the frequency
        if (s.comb == nullptr || s.combSize < 8)
            return;
        const int size = s.combSize;
        const float delay = s.aux;
        for (int ch = 0; ch < 2; ++ch)
        {
            float* comb = s.comb + ch * size;
            const float rp = (float) s.combPos - delay;
            const int i0 = ((int) std::floor (rp) % size + size) % size;
            const float fr = rp - std::floor (rp);
            const float delayed = comb[i0] * (1.0f - fr) + comb[(i0 + 1) % size] * fr;
            comb[s.combPos] = *io[ch] + delayed * res * 0.9f;
            *io[ch] = (*io[ch] + delayed) * 0.5f;
        }
        s.combPos = (s.combPos + 1) % size;
    }
    else
    {
        // three resonant peaks (vocal and morph types)
        const float gains[3] { 1.0f, t == 80 || t == 81 ? 0.6f : 0.75f, t == 80 || t == 81 ? 0.35f : 0.5f };
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = *io[ch];
            float y = 0.0f;
            for (int p = 0; p < 3; ++p)
            {
                float lp, bp, hp;
                const auto f = loadSvf (s.coef[p]);
                svf (f, s.z[ch] + p * 2, x, lp, bp, hp);
                y += bp * f.k * gains[p];
            }
            *io[ch] = y * 1.6f;
        }
    }
}
} // namespace emx::emu
