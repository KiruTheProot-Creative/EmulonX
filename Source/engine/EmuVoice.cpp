// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EmuVoice.h"
#include <cmath>
#include <iterator>

namespace emx::emu
{
using namespace juce;

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
    if (d >= 168 && d < 204) return "Cord " + String (d - 167).paddedLeft ('0', 2) + " Amt";
    switch (d)
    {
        case 0: return "Off";          case 8: return "Key Sustain"; case 47: return "Fine Pitch";
        case 48: return "Pitch";       case 56: return "Filter Frequency"; case 57: return "Filter Resonance";
        case 64: return "Amp Volume";  case 65: return "Amp Pan";    case 66: return "Amp Crossfade";
        case 72: return "Amp Env Rates"; case 73: return "Amp Env Attack"; case 74: return "Amp Env Decay";
        case 75: return "Amp Env Release"; case 80: return "Filter Env Rates"; case 81: return "Filter Env Attack";
        case 82: return "Filter Env Decay"; case 83: return "Filter Env Release"; case 88: return "Aux Env Rates";
        case 89: return "Aux Env Attack"; case 90: return "Aux Env Decay"; case 91: return "Aux Env Release";
        case 104: return "LFO 1 Rate";
        default: return "Destination " + String (d);
    }
}

double rateSeconds (double rate) noexcept
{
    // measured in Emulator X (Orbit-3 "amb:Galactic End"), joined on a log scale
    static const double pts[][2] { { 0.0, 0.0 }, { 4.0, 0.072 }, { 23.5, 0.995 }, { 28.5, 1.763 }, { 32.0, 2.622 },
                                   { 38.0, 5.138 }, { 43.0, 8.997 }, { 50.0, 20.12 } };
    rate = jlimit (0.0, 127.0, rate);
    if (rate <= 0.0) return 0.0;
    if (rate < 4.0) return 0.072 * rate / 4.0;
    for (size_t i = 1; i < std::size (pts); ++i)
        if (rate <= pts[i][0])
        {
            const double t = (rate - pts[i - 1][0]) / (pts[i][0] - pts[i - 1][0]);
            return std::exp (std::log (pts[i - 1][1]) + t * (std::log (pts[i][1]) - std::log (pts[i - 1][1])));
        }
    return jmin (163.0, 20.12 * std::exp ((rate - 50.0) * std::log (20.12 / 8.997) / 7.0));
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
    const float target = d.level[(size_t) stage] / 100.0f;
    const double full = rateSeconds (d.rate[(size_t) stage]) * timeScale;
    const double t = stage >= 4 ? (std::abs (target - e.from) > 1.0e-4f ? full : 0.0) : full * std::abs (target - e.from);
    e.len = (float) (t * sr);
}

void runEnv (EnvState& e, const Envelope& d, float timeScale, double sr, int n)
{
    while (e.stage < 6 && n > 0)
    {
        const float target = d.level[(size_t) e.stage] / 100.0f;
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
        runEnv (st, d.env[(size_t) e], ts, sr, n);
    }

    // LFOs, noise, lags
    for (int i = 0; i < 2; ++i)
    {
        const auto& l = d.lfo[(size_t) i];
        const float rateMod = i == 0 ? dest[104] : 0.0f;
        const double hz = jlimit (0.0, 50.0, (double) l.hz * s.lfoRateScale[(size_t) i] * std::exp2 (3.0 * rateMod));
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
    const float volDb = 48.0f * dest[64];
    s.ampGain = ampEnv * Decibels::decibelsToGain (volDb, -100.0f);
    s.pitchSemis = dest[47] + 12.0f * dest[48];
    s.pan = 100.0f * dest[65];
    s.cutoff = jlimit (0.0f, 1.0f, d.cutoff + dest[56]);
    s.resonance = jlimit (0.0f, 1.0f, d.resonance + dest[57]);
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

void filter (VoiceState& s, const Design& d, double sr, float& L, float& R) noexcept
{
    const int t = d.filterType;
    if (t == 127)
        return;
    const float res = s.resonance;
    const double hz = cutoffHz (s.cutoff);
    float* io[2] { &L, &R };

    auto lowOrHigh = [&] (int stages, bool high)
    {
        const auto f = makeSvf (hz, 0.5 + res * 12.0, sr);
        const auto soft = makeSvf (hz, 0.54, sr);
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
    };
    auto band = [&] (int stages, bool notch)
    {
        const auto f = makeSvf (hz, 0.7 + res * 10.0, sr);
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = *io[ch];
            for (int st = 0; st < stages; ++st)
            {
                float lp, bp, hp;
                svf (f, s.z[ch] + st * 2, x, lp, bp, hp);
                x = notch ? lp + hp : bp * f.k;
            }
            *io[ch] = x;
        }
    };
    auto peaks = [&] (const float* freqs, const float* gains, float q)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = *io[ch];
            float y = 0.0f;
            for (int p = 0; p < 3; ++p)
            {
                float lp, bp, hp;
                const auto f = makeSvf (freqs[p], q, sr);
                svf (f, s.z[ch] + p * 2, x, lp, bp, hp);
                y += bp * f.k * gains[p];
            }
            *io[ch] = y * 1.6f;
        }
    };

    if (t <= 2 || t == 96 || t == 97 || t == 104)
        lowOrHigh (t == 0 ? 2 : t == 2 ? 3 : 1, false);
    else if (t == 8 || t == 9)
        lowOrHigh (t == 8 ? 1 : 2, true);
    else if (t == 16 || t == 17)
        band (t == 16 ? 1 : 2, false);
    else if (t == 18)
        band (1, true);
    else if (t >= 32 && t <= 34)
    {
        // swept EQ: a bell that boosts (resonance) at the frequency
        const float gainDb = res * 24.0f;
        const auto f = makeSvf (hz, t == 32 ? 1.4 : t == 33 ? 1.0 : 0.7, sr);
        const float gain = Decibels::decibelsToGain (gainDb) - 1.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            float lp, bp, hp;
            svf (f, s.z[ch], *io[ch], lp, bp, hp);
            *io[ch] += bp * f.k * gain;
        }
    }
    else if (t >= 64 && t <= 66)
    {
        // phaser: first-order allpass stages around the frequency, mixed with the dry sound (notches)
        const int stages = t == 64 ? 4 : t == 65 ? 6 : 8;
        const float a = (float) ((std::tan (MathConstants<double>::pi * jlimit (20.0, sr * 0.45, hz) / sr) - 1.0)
                                 / (std::tan (MathConstants<double>::pi * jlimit (20.0, sr * 0.45, hz) / sr) + 1.0));
        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = *io[ch];
            float x = dry + s.fb[ch] * res * 0.85f;
            for (int st = 0; st < stages; ++st)
            {
                const float y = a * x + s.z[ch][st];
                s.z[ch][st] = x - a * y;
                x = y;
            }
            s.fb[ch] = x;
            *io[ch] = (dry + x) * 0.5f;
        }
    }
    else if (t == 72)
    {
        // flanger: a short comb filter tuned by the frequency
        if (s.comb == nullptr || s.combSize < 8)
            return;
        const int size = s.combSize;
        const float delay = (float) jlimit (2.0, (double) size - 2.0, sr / jmax (20.0, hz));
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
    else if (t == 80 || t == 81)
    {
        // vocal: morph through the vowels with the frequency (ah > ay > ee, or oo > ah)
        const int path80[3] { 0, 1, 2 }, path81[2] { 4, 0 };
        const int* path = t == 80 ? path80 : path81;
        const int steps = t == 80 ? 2 : 1;
        const float pos = s.cutoff * (float) steps;
        const int a = jlimit (0, steps - 1, (int) pos);
        const float u = jlimit (0.0f, 1.0f, pos - (float) a);
        float freqs[3];
        const float gains[3] { 1.0f, 0.6f, 0.35f };
        for (int p = 0; p < 3; ++p)
            freqs[p] = vowels[path[a]][p] + (vowels[path[a + 1]][p] - vowels[path[a]][p]) * u;
        peaks (freqs, gains, 4.0f + res * 16.0f);
    }
    else
    {
        // E-mu's Z-plane morph filters: approximated as three resonant peaks that move with the frequency.
        // Each type gets its own character from its number (not exact, but it keeps their colour and movement).
        const uint32 seed = (uint32) t * 2654435761u;
        float freqs[3], gains[3];
        for (int p = 0; p < 3; ++p)
        {
            const float spread = (float) ((seed >> (p * 8)) & 255) / 255.0f;
            const float base = 200.0f * std::pow (2.0f, (float) p * 1.6f + spread * 1.5f);
            freqs[p] = jlimit (60.0f, (float) sr * 0.42f, base * std::pow (2.0f, (s.cutoff - 0.5f) * 5.0f));
            gains[p] = 1.0f - (float) p * 0.25f;
        }
        peaks (freqs, gains, 2.0f + res * 18.0f);
    }
}
} // namespace emx::emu
