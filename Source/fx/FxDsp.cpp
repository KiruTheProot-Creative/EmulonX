// SPDX-License-Identifier: AGPL-3.0-or-later
// Line-by-line port of the prototype's effects (engine.js), so presets sound the same.
#include "FxDsp.h"
#include <cmath>
#include <vector>

namespace emx::fx
{
namespace
{
constexpr double pi = juce::MathConstants<double>::pi;
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr double sqrt2 = juce::MathConstants<double>::sqrt2;

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double onePole (double hz, double sr) { return 1.0 - std::exp (-twoPi * juce::jmin (hz, sr * 0.45) / sr); }

/** State-variable (TPT) filter coefficients. */
struct Svf { double a1 = 0, a2 = 0, a3 = 0; };
inline Svf svc (double f, double k, double sr)
{
    const double g = std::tan (pi * juce::jmin (f, sr * 0.45) / sr), a1 = 1.0 / (1.0 + g * (g + k));
    return { a1, g * a1, g * g * a1 };
}

/** Reads a circular buffer `d` samples behind the write position, with linear interpolation. */
inline float readBack (const std::vector<float>& b, int w, double d)
{
    const int len = (int) b.size();
    double p = (double) w - d;
    while (p < 0) p += len;
    const int i0 = juce::jmin (len - 1, (int) p);
    const double f = p - i0;
    const int i1 = i0 + 1 >= len ? 0 : i0 + 1;
    return (float) (b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f);
}

//== Reverb: seven types, each built differently ==================================
class Reverb final : public Processor
{
public:
    enum Mode { room, hall, plate, ambience, cathedral, spring, shimmer, numModes };

    void prepare (double sr) override
    {
        mx = (int) std::ceil (0.34 * sr) + 256;
        for (auto& l : lines) { l.b.assign ((size_t) mx, 0.0f); l.lp = 0; }
        pre.assign ((size_t) std::ceil (sr * 0.3) + 4, 0.0f);
        er.assign ((size_t) std::ceil (sr * 0.32) + 4, 0.0f);
        for (int k = 0; k < maxDiffusers; ++k)
        {
            ap[k].b.assign ((size_t) std::ceil (0.03 * sr) + 4, 0.0f);
            ap[k].i = 0;
        }
        for (auto& s : springs)
        {
            s.line.assign ((size_t) std::ceil (0.12 * sr) + 8, 0.0f);
            s.w = 0;
            for (auto& d : s.disp) d = {};
            s.lp = s.hp = s.inLp = s.inHp = 0;
        }
        for (int k = 0; k < numShifters; ++k)
            shifters[k].reset (k * 0.37);
        w = pwi = ewi = 0; ph = hl = hr = inLp = 0;
        std::fill (std::begin (y), std::end (y), 0.0);
    }

    void process (const float* v, const Settings& s, float* L, float* R, int N, double sr, double) noexcept override
    {
        if (mx == 0) return;
        const int mode = juce::jlimit (0, numModes - 1, s.mode);
        if (mode == spring)
            processSpring (v, L, R, N, sr);
        else
            processRoom (mode, v, L, R, N, sr);
    }

private:
    struct ModeDef
    {
        double scale, modMs, modHz, diffGain;
        int diffStages;
        double erLevel, lateLevel, dampMul, decayMul, decayCap, outGain;
        int erSet;
    };
    static const ModeDef& modeDef (int m)
    {
        static const ModeDef defs[numModes] {
            //  scale  mod   rate  diffG stg  er    late  damp  decay cap   out  erSet
            {   0.42, 0.25, 0.80, 0.62, 3,   0.85, 0.75, 0.75, 1.0,  20.0, 1.30, 0 },   // room
            {   1.15, 0.90, 0.30, 0.70, 4,   0.35, 1.00, 0.90, 1.0,  20.0, 1.45, 1 },   // hall
            {   0.62, 0.55, 1.30, 0.78, 6,   0.00, 1.00, 2.20, 1.0,  20.0, 1.20, -1 },  // plate
            {   0.30, 0.10, 0.50, 0.50, 2,   1.10, 0.22, 0.60, 0.25, 0.8,  1.20, 2 },   // ambience
            {   2.10, 1.60, 0.17, 0.72, 4,   0.22, 1.15, 0.55, 1.7,  30.0, 1.55, 3 },   // cathedral
            {   1.00, 0.00, 0.00, 0.00, 0,   0.00, 1.00, 1.00, 1.0,  20.0, 1.00, -1 },  // spring (own path)
            {   1.20, 1.10, 0.25, 0.72, 4,   0.25, 1.00, 1.10, 1.3,  30.0, 1.60, 1 },   // shimmer
        };
        return defs[juce::jlimit (0, numModes - 1, m)];
    }

    struct Tap { float ms, gain, pan; };
    static const std::vector<Tap>& erTaps (int set)
    {
        static const std::vector<Tap> sets[4] {
            { { 3.1f, .9f, -.6f }, { 5.3f, .8f, .7f }, { 7.9f, .75f, -.3f }, { 11.2f, .7f, .5f }, { 13.7f, .6f, -.8f },
              { 17.3f, .55f, .2f }, { 21.1f, .5f, -.4f }, { 24.8f, .45f, .8f }, { 29.3f, .4f, -.1f }, { 34.6f, .35f, .4f } },
            { { 12.4f, .7f, -.7f }, { 19.8f, .6f, .6f }, { 27.1f, .55f, -.2f }, { 36.5f, .5f, .8f }, { 47.2f, .42f, -.5f },
              { 58.9f, .36f, .3f }, { 71.4f, .3f, -.8f }, { 86.3f, .25f, .5f } },
            { { 1.2f, .9f, .3f }, { 2.9f, .85f, -.5f }, { 4.4f, .8f, .7f }, { 6.1f, .78f, -.2f }, { 8.3f, .72f, .5f },
              { 10.2f, .7f, -.8f }, { 13.0f, .66f, .1f }, { 15.7f, .6f, -.4f }, { 19.4f, .55f, .8f }, { 23.1f, .5f, -.6f },
              { 28.0f, .45f, .4f }, { 33.6f, .4f, -.1f }, { 40.2f, .34f, .6f }, { 47.9f, .28f, -.7f } },
            { { 21.0f, .6f, -.5f }, { 33.0f, .55f, .6f }, { 49.0f, .5f, -.8f }, { 66.0f, .45f, .3f }, { 84.0f, .4f, -.2f },
              { 103.0f, .35f, .7f }, { 125.0f, .3f, -.6f }, { 142.0f, .25f, .4f } } };
        return sets[juce::jlimit (0, 3, set)];
    }

    static float readCirc (const std::vector<float>& b, int writePos, double delay)
    {
        const int len = (int) b.size();
        delay = juce::jlimit (1.0, (double) len - 2.0, delay);
        double p = (double) writePos - delay;
        if (p < 0) p += len;
        const int i0 = juce::jmin (len - 1, (int) p);
        const double f = p - i0;
        const int i1 = i0 + 1 >= len ? 0 : i0 + 1;
        return (float) (b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f);
    }

    //== Room, Hall, Plate, Ambience, Cathedral, Shimmer: early reflections + 8-line FDN
    void processRoom (int mode, const float* v, float* L, float* R, int N, double sr) noexcept
    {
        const auto& M = modeDef (mode);
        const double size = v[0], damp = v[3], lo = v[4], width = v[5], mix = v[6];
        const double decay = juce::jmin (M.decayCap, v[1] * M.decayMul);
        const double sc = M.scale * (0.4 + 1.6 * size);
        double lens[8], gs[8], md[8];
        const double modDepth = M.modMs * 0.001 * sr;
        for (int j = 0; j < 8; ++j)
        {
            lens[j] = juce::jmin ((double) mx - modDepth - 8.0, lineMs[j] * sc * sr / 1000.0);
            gs[j] = std::pow (10.0, -3.0 * lens[j] / (decay * sr));
        }
        const double dc = onePole (damp * M.dampMul, sr), lc = 1.0 - std::exp (-twoPi * lo / sr);
        const double inC = mode == plate ? 1.0 : onePole (juce::jmin (sr * 0.45, damp * 1.3), sr);
        if (mode == shimmer)
        {
            shimInC = onePole (juce::jmin (sr * 0.2, juce::jmin (4500.0, damp)), sr);
            shimOutC = onePole (juce::jmin (sr * 0.4, juce::jmin (8000.0, damp * 1.5)), sr);
            shimHpC = 1.0 - std::exp (-twoPi * 160.0 / sr);
        }
        const int plen = (int) pre.size();
        const int pd = juce::jlimit (1, plen - 1, (int) std::round (v[2] * 0.001 * sr));
        ph += N / sr;
        for (int j = 0; j < 8; ++j)
            md[j] = lens[j] + modDepth * std::sin (twoPi * ph * M.modHz * (0.7 + 0.11 * j) + j * 1.7);

        // Diffuser lengths (scaled with the room size)
        int apn[maxDiffusers];
        for (int k = 0; k < maxDiffusers; ++k)
            apn[k] = juce::jlimit (1, (int) ap[k].b.size() - 2, (int) std::round (apMs[k] * std::sqrt (sc) * sr / 1000.0));

        // Early reflection taps
        const auto& taps = M.erSet >= 0 ? erTaps (M.erSet) : erTaps (0);
        const double erScale = 0.5 + size;
        const int elen = (int) er.size();

        for (int i = 0; i < N; ++i)
        {
            const double xl = L[i], xr = R[i];
            pre[(size_t) pwi] = (float) ((xl + xr) * 0.5);
            int rp = pwi - pd;
            if (rp < 0) rp += plen;
            const double dry = pre[(size_t) rp];
            if (++pwi >= plen) pwi = 0;

            // Early reflections (from the pre-delayed input)
            double el = 0, erR = 0;
            er[(size_t) ewi] = (float) dry;
            if (M.erSet >= 0 && M.erLevel > 0)
                for (auto& t : taps)
                {
                    const double val = readCirc (er, ewi, t.ms * erScale * 0.001 * sr) * t.gain;
                    el += val * std::sqrt ((1.0 - t.pan) * 0.5);
                    erR += val * std::sqrt ((1.0 + t.pan) * 0.5);
                }
            if (++ewi >= elen) ewi = 0;

            // Input: tone, then diffusion
            inLp += (dry - inLp) * inC;
            double d = inLp;
            for (int k = 0; k < M.diffStages && k < maxDiffusers; ++k)
            {
                auto& A = ap[k];
                if (A.i >= apn[k]) A.i = 0;
                const double bo = A.b[(size_t) A.i], yv = bo - M.diffGain * d;
                A.b[(size_t) A.i] = (float) (d + M.diffGain * yv);
                ++A.i;
                d = yv;
            }

            // Late tail: 8 modulated delay lines with a Householder mix
            double sum = 0;
            for (int j = 0; j < 8; ++j)
            {
                auto& Ln = lines[j];
                double yy = readCirc (Ln.b, w, md[j]);
                Ln.lp += (yy - Ln.lp) * dc;
                yy = Ln.lp * gs[j];
                y[j] = yy;
                sum += yy;
            }
            sum *= 0.25;
            double fbv[8];
            for (int j = 0; j < 8; ++j)
                fbv[j] = y[j] - sum;   // Householder mix (energy preserving)
            if (mode == shimmer)
            {
                // Swap part of four lines for their octave-up copies instead of adding the copies on
                // top: the feedback can then never come back louder than it went in, so the loop
                // stays stable at any decay. The shifters' inputs and outputs are band-limited, so
                // octaves stacked on octaves fade out instead of piling up into a whistle.
                for (int k = 0; k < numShifters; ++k)
                    fbv[k] += shimmerAmount * (shifters[k].tap (fbv[k], shimInC, shimOutC, shimHpC) - fbv[k]);
            }
            for (int j = 0; j < 8; ++j)
                lines[j].b[(size_t) w] = (float) (fbv[j] + ((j & 1) ? -d : d) * 0.5);
            if (++w >= mx) w = 0;

            double ol = (y[0] + y[2] + y[4] + y[6]) * M.outGain * M.lateLevel + el * M.erLevel * 0.6;
            double orr = (y[1] + y[3] + y[5] + y[7]) * M.outGain * M.lateLevel + erR * M.erLevel * 0.6;
            const double m = (ol + orr) * 0.5, sd = (ol - orr) * 0.5 * width;
            ol = m + sd; orr = m - sd;
            hl += (ol - hl) * lc; ol -= hl;
            hr += (orr - hr) * lc; orr -= hr;
            L[i] = (float) (xl * (1 - mix) + ol * mix);
            R[i] = (float) (xr * (1 - mix) + orr * mix);
        }
    }

    //== Spring: a delay line with a chain of dispersive all-passes ("boing")
    void processSpring (const float* v, float* L, float* R, int N, double sr) noexcept
    {
        const double size = v[0], decay = v[1], damp = v[3], lo = v[4], width = v[5], mix = v[6];
        static constexpr double baseMs[2] { 31.0, 37.3 };   // two springs of different length
        const double lc = 1.0 - std::exp (-twoPi * juce::jmax (lo, 120.0) / sr);
        const double hc = onePole (juce::jmin (damp, 5000.0), sr);
        double D[2], fb[2];
        for (int c = 0; c < 2; ++c)
        {
            const int len = (int) springs[c].line.size();
            D[c] = juce::jlimit (8.0, (double) len - 4.0, baseMs[c] * (0.6 + 1.4 * size) * 0.001 * sr);
            fb[c] = std::pow (10.0, -3.0 * D[c] / (juce::jmax (0.1, decay) * sr));
        }
        for (int i = 0; i < N; ++i)
        {
            const double x = (L[i] + R[i]) * 0.5;
            double out[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& sp = springs[c];
                // band-limit the input like a spring tank (no deep lows, no air)
                sp.inHp += (x - sp.inHp) * lc;
                double in = x - sp.inHp;
                sp.inLp += (in - sp.inLp) * hc;
                in = sp.inLp;
                double t = readCirc (sp.line, sp.w, D[c]);
                // dispersion: high frequencies arrive at a different time -> the chirp
                for (auto& ap1 : sp.disp)
                {
                    const double yv = -dispersion * t + ap1.x1 + dispersion * ap1.y1;
                    ap1.x1 = t;
                    ap1.y1 = yv;
                    t = yv;
                }
                sp.lp += (t - sp.lp) * hc;
                sp.line[(size_t) sp.w] = (float) (in + sp.lp * fb[c]);
                if (++sp.w >= (int) sp.line.size()) sp.w = 0;
                sp.hp += (sp.lp - sp.hp) * lc;
                out[c] = (sp.lp - sp.hp) * 1.6;
            }
            const double m = (out[0] + out[1]) * 0.5, sd = (out[0] - out[1]) * 0.5 * width;
            L[i] = (float) (L[i] * (1 - mix) + (m + sd) * mix);
            R[i] = (float) (R[i] * (1 - mix) + (m - sd) * mix);
        }
    }

    static constexpr double lineMs[8] { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
    static constexpr int maxDiffusers = 6;
    static constexpr double apMs[maxDiffusers] { 4.7, 3.6, 12.7, 9.3, 2.1, 7.4 };
    static constexpr double dispersion = 0.62;
    static constexpr double shimmerAmount = 0.6;
    static constexpr int numShifters = 4;

    /** Octave-up pitch shifter (two crossfading read heads) for the Shimmer tail. */
    struct Shifter
    {
        std::vector<float> buf;
        int pw = 0;
        double phase = 0, i1 = 0, i2 = 0, o1 = 0, o2 = 0, hp = 0;

        void reset (double startPhase)
        {
            buf.assign (4096, 0.0f);
            pw = 0; phase = startPhase; i1 = i2 = o1 = o2 = hp = 0;
        }
        double tap (double x, double inC, double outC, double hpC) noexcept
        {
            // keep the top end out of the shifter (it would come back an octave higher)
            i1 += (x - i1) * inC;
            i2 += (i1 - i2) * inC;
            const int W = (int) buf.size();
            buf[(size_t) pw] = (float) i2;
            phase -= 1.0 / (W - 8);
            if (phase < 0) phase += 1.0;
            double out = 0;
            for (int h = 0; h < 2; ++h)
            {
                double p = phase + 0.5 * h;
                if (p >= 1.0) p -= 1.0;
                const double win = std::sin (pi * p);
                out += win * win * readCirc (buf, pw, 2.0 + p * (W - 8));
            }
            if (++pw >= W) pw = 0;
            // soften the shifted top end and drop the rumble the crossfading heads can leave behind
            o1 += (out - o1) * outC;
            o2 += (o1 - o2) * outC;
            hp += (o2 - hp) * hpC;
            const double r = o2 - hp;
            return std::isfinite (r) ? r : 0.0;
        }
    };
    Shifter shifters[numShifters];
    struct Line { std::vector<float> b; double lp = 0; };
    struct AllPass { std::vector<float> b; int i = 0; };
    struct Disp { double x1 = 0, y1 = 0; };
    struct Spring { std::vector<float> line; int w = 0; std::array<Disp, 18> disp {}; double lp = 0, hp = 0, inLp = 0, inHp = 0; };
    Line lines[8];
    AllPass ap[maxDiffusers];
    Spring springs[2];
    std::vector<float> pre, er;
    int mx = 0, w = 0, pwi = 0, ewi = 0;
    double ph = 0, hl = 0, hr = 0, y[8] {}, inLp = 0, shimInC = 0.5, shimOutC = 0.5, shimHpC = 0.02;
};

//== Compressor / Limiter / OTT ==================================================
class Compressor final : public Processor
{
public:
    void prepare (double) override { e = 0; g = 1; be[0] = be[1] = be[2] = 0; for (auto& c : sv) for (auto& f : c) f[0] = f[1] = 0; }

    void process (const float* v, const Settings& s, float* L, float* R, int N, double sr, double) noexcept override
    {
        const int mode = juce::jlimit (0, 2, s.mode);
        const double mix = v[5], og = dbToGain (v[4]);
        double gr = 0;
        if (mode == 0)
        {
            const double thr = v[0], sl = 1.0 / v[1] - 1.0, ac = std::exp (-1.0 / (v[2] * 0.001 * sr)), rc = std::exp (-1.0 / (v[3] * 0.001 * sr));
            for (int i = 0; i < N; ++i)
            {
                const double xl = L[i], xr = R[i], lv = juce::jmax (std::abs (xl), std::abs (xr));
                e = lv > e ? ac * e + (1 - ac) * lv : rc * e + (1 - rc) * lv;
                const double o = 20.0 * std::log10 (e + 1e-9) - thr;
                const double r = o <= -3 ? 0.0 : o >= 3 ? o * sl : sl * (o + 3) * (o + 3) / 12.0;
                gr = juce::jmin (gr, r);
                const double k = dbToGain (r) * og;
                L[i] = (float) (xl * (1 - mix) + xl * k * mix);
                R[i] = (float) (xr * (1 - mix) + xr * k * mix);
            }
        }
        else if (mode == 1)
        {
            const double c = dbToGain (v[0]), rc = std::exp (-1.0 / (v[1] * 0.001 * sr)), ig = dbToGain (v[2]);
            for (int i = 0; i < N; ++i)
            {
                const double dl = L[i], dr = R[i], xl = dl * ig, xr = dr * ig, pk = juce::jmax (std::abs (xl), std::abs (xr));
                const double tg = pk > c ? c / pk : 1.0;
                g = tg < g ? tg : g + (tg - g) * (1 - rc);
                gr = juce::jmin (gr, 20.0 * std::log10 (g + 1e-9));
                const double ol = juce::jlimit (-c, c, xl * g) * og, orr = juce::jlimit (-c, c, xr * g) * og;
                L[i] = (float) (dl * (1 - mix) + ol * mix);
                R[i] = (float) (dr * (1 - mix) + orr * mix);
            }
        }
        else
        {
            // OTT: three bands, each pushed down above and pulled up below its target.
            // INTENSITY (0..200 %) scales both the upward and the downward push
            const double inten = juce::jlimit (0.0, 2.0, (double) v[6]);
            const double t = v[0], up = v[1] * inten, dn = v[2] * inten, ig = dbToGain (v[3]), K = sqrt2;
            const Svf c1 = svc (88.3, K, sr), c2 = svc (2500.0, K, sr);
            const double ac = std::exp (-1.0 / ((1 + 20 * t) * 0.001 * sr)), rc = std::exp (-1.0 / ((20 + 400 * t) * 0.001 * sr));
            static constexpr double TD[3] { -17, -16, -18 }, TU[3] { -33, -34, -33 };
            auto lp = [] (double* S, double x, const Svf& c)
            {
                const double v3 = x - S[1], v1 = c.a1 * S[0] + c.a2 * v3, v2 = S[1] + c.a2 * S[0] + c.a3 * v3;
                S[0] = 2 * v1 - S[0]; S[1] = 2 * v2 - S[1];
                return v2;
            };
            auto hp = [K] (double* S, double x, const Svf& c)
            {
                const double v3 = x - S[1], v1 = c.a1 * S[0] + c.a2 * v3, v2 = S[1] + c.a2 * S[0] + c.a3 * v3;
                S[0] = 2 * v1 - S[0]; S[1] = 2 * v2 - S[1];
                return x - K * v1 - v2;
            };
            for (int i = 0; i < N; ++i)
            {
                double B[2][3];
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& S = sv[ch];
                    const double x = (ch ? R : L)[i] * ig;
                    const double lo = lp (S[1], lp (S[0], x, c1), c1), rest = x - lo, hi = hp (S[3], hp (S[2], rest, c2), c2);
                    B[ch][0] = lo; B[ch][1] = rest - hi; B[ch][2] = hi;
                }
                double wl = 0, wr = 0;
                for (int k = 0; k < 3; ++k)
                {
                    const double lv = juce::jmax (std::abs (B[0][k]), std::abs (B[1][k]));
                    be[k] = lv > be[k] ? ac * be[k] + (1 - ac) * lv : rc * be[k] + (1 - rc) * lv;
                    const double db = 20.0 * std::log10 (be[k] + 1e-9);
                    double r = 0;
                    if (db > TD[k]) r -= (db - TD[k]) * (1.0 - 1.0 / 66.0) * dn;
                    if (db < TU[k])
                    {
                        const double amt = juce::jlimit (0.0, 1.0, (db + 75) / 15);
                        r += juce::jmin (18.0, (TU[k] - db) * 0.76) * up * amt;
                    }
                    gr = juce::jmin (gr, r);
                    const double k2 = dbToGain (r);
                    wl += B[0][k] * k2;
                    wr += B[1][k] * k2;
                }
                L[i] = (float) (L[i] * (1 - mix) + wl * og * 2.0 * mix);
                R[i] = (float) (R[i] * (1 - mix) + wr * og * 2.0 * mix);
            }
        }
        gainReductionDb = (float) gr;
    }

private:
    double e = 0, g = 1, be[3] {}, sv[2][4][2] {};
};

//== Spectral: resonance (peaks pushed up), smear (partials held on) and tilt, in the frequency domain
class Spectral final : public Processor
{
public:
    static constexpr int N = 1024, H = 256, bins = N / 2 + 1;

    void prepare (double) override
    {
        win.resize (N);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (twoPi * i / N));
        for (auto& c : ch)
        {
            c.in.assign (N, 0.0f);
            c.out.assign (N, 0.0f);
            c.dry.assign (N, 0.0f);
            c.held.assign (bins, 0.0);
        }
        re.assign (N, 0.0);
        im.assign (N, 0.0);
        mag.assign (bins, 0.0);
        w = 0;
        hop = 0;
    }

    void process (const float* v, const Settings&, float* L, float* R, int n, double sr, double) noexcept override
    {
        if (win.empty())
            return;
        reso = juce::jlimit (0.0, 1.0, (double) v[0]);
        smear = juce::jlimit (0.0, 1.0, (double) v[1]);
        tilt = juce::jlimit (-1.0, 1.0, (double) v[2]);
        const double mix = juce::jlimit (0.0, 1.0, (double) v[3]);
        // smear: how long a partial keeps sounding after it stops (50 ms .. 6 s)
        const double holdSec = 0.05 * std::pow (120.0, smear);
        holdCoef = smear < 0.01 ? 0.0 : std::pow (0.001, (double) H / (sr * holdSec));
        for (int i = 0; i < n; ++i)
        {
            float* io[2] { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[c];
                const float x = *io[c];
                const float dryOut = s.dry[(size_t) w];   // the dry sound, delayed like the wet one
                s.dry[(size_t) w] = x;
                s.in[(size_t) w] = x;
                const float wet = s.out[(size_t) w];
                s.out[(size_t) w] = 0.0f;
                *io[c] = (float) (dryOut * (1.0 - mix) + wet * mix);
            }
            w = (w + 1) % N;
            if (++hop == H)
            {
                hop = 0;
                frame (ch[0]);
                frame (ch[1]);
            }
        }
    }

private:
    struct Ch { std::vector<float> in, out, dry; std::vector<double> held; };

    static void fft (std::vector<double>& xr, std::vector<double>& xi, bool inverse) noexcept
    {
        const int n = (int) xr.size();
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap (xr[(size_t) i], xr[(size_t) j]); std::swap (xi[(size_t) i], xi[(size_t) j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const double ang = twoPi / len * (inverse ? 1.0 : -1.0);
            const double wr = std::cos (ang), wi = std::sin (ang);
            for (int i = 0; i < n; i += len)
            {
                double cr = 1.0, ci = 0.0;
                for (int k = 0; k < len / 2; ++k)
                {
                    const size_t a = (size_t) (i + k), b = (size_t) (i + k + len / 2);
                    const double tr = xr[b] * cr - xi[b] * ci, ti = xr[b] * ci + xi[b] * cr;
                    xr[b] = xr[a] - tr; xi[b] = xi[a] - ti;
                    xr[a] += tr; xi[a] += ti;
                    const double nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = nr;
                }
            }
        }
    }

    void frame (Ch& c) noexcept
    {
        for (int k = 0; k < N; ++k)
        {
            re[(size_t) k] = c.in[(size_t) ((w + k) % N)] * win[(size_t) k];   // oldest first
            im[(size_t) k] = 0.0;
        }
        fft (re, im, false);
        double eIn = 0.0;
        for (int b = 0; b < bins; ++b)
        {
            mag[(size_t) b] = std::sqrt (re[(size_t) b] * re[(size_t) b] + im[(size_t) b] * im[(size_t) b]);
            eIn += mag[(size_t) b] * mag[(size_t) b];
        }
        // resonance: each bin against its neighbourhood (+-6 bins), peaks pushed up
        double eOut = 0.0;
        std::array<double, bins> gain {};
        double run = 0.0;
        for (int b = 0; b < juce::jmin (bins, 7); ++b) run += mag[(size_t) b];
        int lo = 0, hi = juce::jmin (bins, 7);
        for (int b = 0; b < bins; ++b)
        {
            while (hi < juce::jmin (bins, b + 7)) run += mag[(size_t) hi++];
            while (lo < b - 6) run -= mag[(size_t) lo++];
            const double avg = run / juce::jmax (1, hi - lo) + 1e-9;
            double g = reso > 0.001 ? juce::jmin (8.0, std::pow ((mag[(size_t) b] + 1e-9) / avg, reso * 2.0)) : 1.0;
            if (std::abs (tilt) > 0.001)
                g *= juce::jlimit (0.1, 10.0, std::pow ((b + 1) / 48.0, tilt * 0.6));
            gain[(size_t) b] = g;
            eOut += (mag[(size_t) b] * g) * (mag[(size_t) b] * g);
        }
        const double norm = eOut > 1e-18 ? std::sqrt (eIn / eOut) : 1.0;
        for (int b = 0; b < bins; ++b)
        {
            double m = mag[(size_t) b] * gain[(size_t) b] * norm;
            if (holdCoef > 0.0)
            {
                c.held[(size_t) b] = juce::jmax (m, c.held[(size_t) b] * holdCoef);   // smear: partials fade slowly
                m = c.held[(size_t) b];
            }
            const double k = mag[(size_t) b] > 1e-12 ? m / mag[(size_t) b] : 0.0;
            if (mag[(size_t) b] <= 1e-12 && m > 0.0)
            {
                // a held partial whose input went silent: give it a phase that keeps turning
                const double ph = twoPi * b * (double) H / N * (double) (++turn % 4096);
                re[(size_t) b] = m * std::cos (ph);
                im[(size_t) b] = m * std::sin (ph);
            }
            else
            {
                re[(size_t) b] *= k;
                im[(size_t) b] *= k;
            }
            if (b > 0 && b < N / 2)
            {
                re[(size_t) (N - b)] = re[(size_t) b];
                im[(size_t) (N - b)] = -im[(size_t) b];
            }
        }
        fft (re, im, true);
        const double scale = 1.0 / (N * 1.5);   // inverse FFT size, and Hann x Hann at 75 % overlap
        for (int k = 0; k < N; ++k)
            c.out[(size_t) ((w + k) % N)] += (float) (re[(size_t) k] * win[(size_t) k] * scale);
    }

    std::vector<float> win;
    Ch ch[2];
    std::vector<double> re, im, mag;
    int w = 0, hop = 0;
    juce::uint32 turn = 0;
    double reso = 0.4, smear = 0.3, tilt = 0.0, holdCoef = 0.0;
};

//== 3-band EQ ======================================================================
class Equaliser final : public Processor
{
public:
    void prepare (double) override { for (auto& c : z) for (auto& b : c) for (auto& x : b) x = 0; }

    void process (const float* v, const Settings&, float* L, float* R, int N, double sr, double) noexcept override
    {
        const double bands[3][4] { { 0, v[1], v[0], 0.7 }, { 1, v[3], v[2], v[4] }, { 2, v[6], v[5], 0.7 } };
        for (int k = 0; k < 3; ++k)
        {
            if (std::abs (bands[k][2]) <= 0.05)
                continue;
            double co[5];
            biquad ((int) bands[k][0], bands[k][1], bands[k][2], bands[k][3], sr, co);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* X = ch ? R : L;
                double* zz = z[ch][k];
                for (int i = 0; i < N; ++i)
                {
                    const double x = X[i], y = co[0] * x + co[1] * zz[0] + co[2] * zz[1] - co[3] * zz[2] - co[4] * zz[3];
                    zz[1] = zz[0]; zz[0] = x; zz[3] = zz[2]; zz[2] = y;
                    X[i] = (float) y;
                }
            }
        }
    }

private:
    static void biquad (int type, double f, double gdb, double q, double sr, double* out)
    {
        f = juce::jmin (f, sr * 0.45);
        const double A = std::pow (10.0, gdb / 40.0), w = twoPi * f / sr, cs = std::cos (w), sn = std::sin (w);
        double b0, b1, b2, a0, a1, a2;
        if (type == 1)
        {
            const double al = sn / (2 * q);
            b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A;
        }
        else
        {
            const double al = sn / 2 * sqrt2, sq = 2 * std::sqrt (A) * al;
            if (type == 0)
            {
                b0 = A * ((A + 1) - (A - 1) * cs + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - sq);
                a0 = (A + 1) + (A - 1) * cs + sq; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - sq;
            }
            else
            {
                b0 = A * ((A + 1) + (A - 1) * cs + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - sq);
                a0 = (A + 1) - (A - 1) * cs + sq; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - sq;
            }
        }
        out[0] = b0 / a0; out[1] = b1 / a0; out[2] = b2 / a0; out[3] = a1 / a0; out[4] = a2 / a0;
    }
    double z[2][3][4] {};
};

//== Chorus / Flanger / Ensemble =================================================
class Chorus final : public Processor
{
public:
    void prepare (double sr) override
    {
        bl.assign ((size_t) std::ceil (sr * 0.1) + 4, 0.0f);
        br.assign (bl.size(), 0.0f);
        w = 0; ph = 0;
    }

    void process (const float* v, const Settings& s, float* L, float* R, int N, double sr, double) noexcept override
    {
        if (bl.empty()) return;
        const int mode = juce::jlimit (0, 2, s.mode), len = (int) bl.size();
        const double rate = v[0], dep = v[1], a = v[2], b = v[3], mix = v[4];
        const double base = juce::jmin ((double) len * 0.6, a * 0.001 * sr);
        for (int i = 0; i < N; ++i)
        {
            ph += rate / sr;
            if (ph > 1) ph -= 1;
            const double P = twoPi * ph, xl = L[i], xr = R[i];
            double wl, wr;
            if (mode == 0)
            {
                const double m = base * 0.45 * dep;
                wl = readBack (bl, w, base + m * std::sin (P));
                wr = readBack (br, w, base + m * std::sin (P + pi * b));
                bl[(size_t) w] = (float) xl; br[(size_t) w] = (float) xr;
            }
            else if (mode == 1)
            {
                const double dl = juce::jmax (2.0, base * (1 - dep * (0.5 + 0.5 * std::sin (P))));
                const double dr = juce::jmax (2.0, base * (1 - dep * (0.5 + 0.5 * std::sin (P + pi / 2))));
                wl = readBack (bl, w, dl); wr = readBack (br, w, dr);
                bl[(size_t) w] = (float) (xl + wl * b); br[(size_t) w] = (float) (xr + wr * b);
            }
            else
            {
                const double m = base * 0.3 * dep;
                const double t0 = readBack (bl, w, base + m * std::sin (P)), t1 = readBack (bl, w, base + m * std::sin (P + 2.094)),
                             t2 = readBack (bl, w, base + m * std::sin (P + 4.189)), hb = 0.5 * b;
                wl = (t0 * (0.5 + hb) + t1 * 0.5 + t2 * (0.5 - hb)) / 1.5;
                wr = (t2 * (0.5 + hb) + t1 * 0.5 + t0 * (0.5 - hb)) / 1.5;
                bl[(size_t) w] = (float) ((xl + xr) * 0.5);
            }
            if (++w >= len) w = 0;
            L[i] = (float) (xl * (1 - mix) + wl * mix);
            R[i] = (float) (xr * (1 - mix) + wr * mix);
        }
    }

private:
    std::vector<float> bl, br;
    int w = 0;
    double ph = 0;
};

//== Phaser ========================================================================
class Phaser final : public Processor
{
public:
    void prepare (double) override { ph = 0; a[0] = a[1] = fb[0] = fb[1] = 0; for (auto& c : z) for (auto& x : c) x = 0; }

    void process (const float* v, const Settings& s, float* L, float* R, int N, double sr, double) noexcept override
    {
        static constexpr int stages[3] { 4, 8, 12 };
        const int n = stages[juce::jlimit (0, 2, s.mode)];
        const double rate = v[0], dep = v[1], center = v[2], feedback = v[3], sp = v[4] * pi, mix = v[5];
        for (int i = 0; i < N; ++i)
        {
            ph += rate / sr;
            if (ph > 1) ph -= 1;
            if ((i & 7) == 0)
                for (int c = 0; c < 2; ++c)
                {
                    const double f = juce::jmin (sr * 0.45, center * std::pow (2.0, dep * 3 * std::sin (twoPi * ph + c * sp)));
                    const double t = std::tan (pi * f / sr);
                    a[c] = (t - 1) / (t + 1);
                }
            for (int c = 0; c < 2; ++c)
            {
                float* X = c ? R : L;
                const double x = X[i];
                double y = x + fb[c] * feedback;
                for (int k = 0; k < n; ++k)
                {
                    const double o = a[c] * y + z[c][k];
                    z[c][k] = y - a[c] * o;
                    y = o;
                }
                fb[c] = y;
                X[i] = (float) (x * (1 - mix) + y * mix);
            }
        }
    }

private:
    double ph = 0, a[2] {}, fb[2] {}, z[2][12] {};
};

//== Morph filter (vowels / LP-BP-HP sweep) ======================================
class Morph final : public Processor
{
public:
    void prepare (double) override { for (auto& x : st) x[0] = x[1] = 0; }

    void process (const float* v, const Settings& s, float* L, float* R, int N, double sr, double) noexcept override
    {
        static constexpr double VOW[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
        const double a = v[0], b = v[1], res = v[2], drive = v[3], mix = v[4];
        const double dg = dbToGain (drive), comp = 1.0 / std::sqrt (dg);
        const bool drv = drive > 0.05;
        if (s.mode == 0)
        {
            const double p = a * 4;
            const int i0 = juce::jmin (3, (int) p);
            const double f = p - i0, k = 1.0 / (2 + 18 * res);
            static constexpr double G[3] { 1, 0.7, 0.45 };
            Svf co[3];
            for (int j = 0; j < 3; ++j)
                co[j] = svc (std::exp (std::log (VOW[i0][j]) * (1 - f) + std::log (VOW[i0 + 1][j]) * f) * b, k, sr);
            for (int c = 0; c < 2; ++c)
            {
                float* X = c ? R : L;
                for (int i = 0; i < N; ++i)
                {
                    const double x0 = X[i], x = drv ? std::tanh (x0 * dg) * comp : x0;
                    double y = 0;
                    for (int j = 0; j < 3; ++j)
                    {
                        auto& S = st[c * 3 + j];
                        const auto& q = co[j];
                        const double v3 = x - S[1], v1 = q.a1 * S[0] + q.a2 * v3, v2 = S[1] + q.a2 * S[0] + q.a3 * v3;
                        S[0] = 2 * v1 - S[0]; S[1] = 2 * v2 - S[1];
                        y += G[j] * v1 * k;
                    }
                    X[i] = (float) (x0 + (y * 2.5 - x0) * mix);
                }
            }
        }
        else
        {
            const double k = 2 - 1.9 * res, m = a;
            const Svf q = svc (b, k, sr);
            for (int c = 0; c < 2; ++c)
            {
                float* X = c ? R : L;
                auto& S = st[c];
                for (int i = 0; i < N; ++i)
                {
                    const double x0 = X[i], x = drv ? std::tanh (x0 * dg) * comp : x0;
                    const double v3 = x - S[1], v1 = q.a1 * S[0] + q.a2 * v3, v2 = S[1] + q.a2 * S[0] + q.a3 * v3;
                    S[0] = 2 * v1 - S[0]; S[1] = 2 * v2 - S[1];
                    const double bp = v1 * k, hp = x - k * v1 - v2;
                    const double y = m < 0.5 ? v2 * (1 - 2 * m) + bp * 2 * m : bp * (2 - 2 * m) + hp * (2 * m - 1);
                    X[i] = (float) (x0 + (y - x0) * mix);
                }
            }
        }
    }

private:
    double st[6][2] {};
};

//== Utility ========================================================================
class Utility final : public Processor
{
public:
    void prepare (double) override {}
    void process (const float* v, const Settings&, float* L, float* R, int N, double, double) noexcept override
    {
        const double g = dbToGain (v[0]), p = (v[1] + 1) * pi / 4, gl = std::cos (p) * sqrt2 * g, gr = std::sin (p) * sqrt2 * g, w = v[2];
        for (int i = 0; i < N; ++i)
        {
            const double m = (L[i] + R[i]) * 0.5, sd = (L[i] - R[i]) * 0.5 * w;
            L[i] = (float) ((m + sd) * gl);
            R[i] = (float) ((m - sd) * gr);
        }
    }
};

//== Distortion (10 types) =========================================================
class Distortion final : public Processor
{
public:
    void prepare (double) override { for (auto& s : st) s = {}; }

    void process (const float* v, const Settings& set, float* L, float* R, int N, double sr, double) noexcept override
    {
        const int mode = juce::jlimit (0, 9, set.mode);
        const double g = dbToGain (v[0]), a = v[1], b = v[2], out = dbToGain (v[4]), mix = v[5];
        const double tc = v[3] >= 19500 ? 1.0 : 1.0 - std::exp (-twoPi * v[3] / sr);
        const double ageC = onePole (20000.0 * std::pow (0.075, a), sr);
        for (int c = 0; c < 2; ++c)
        {
            float* X = c ? R : L;
            auto& S = st[c];
            for (int i = 0; i < N; ++i)
            {
                const double x = X[i];
                double z = x * g, y;
                switch (mode)
                {
                    case 0:
                    {
                        auto f = [b] (double q) { return q / std::pow (1 + std::pow (std::abs (q), b), 1 / b); };
                        y = f (z + a) - f (a);
                        break;
                    }
                    case 1:
                    {
                        auto cl = [a] (double q) { return juce::jlimit (-a, a, q) / a; };
                        y = cl (z + b) - cl (b);
                        break;
                    }
                    case 2:
                    {
                        const double q = z + b;
                        y = std::tanh (q + a * 0.5 * q * q) - std::tanh (b + a * 0.5 * b * b);
                        break;
                    }
                    case 3:
                    {
                        const double ax = std::abs (x);
                        if (a > 0 && ax < a) z *= (ax / a) * (ax / a);
                        y = (z > 0 ? 1.0 : z < 0 ? -1.0 : 0.0) * (1 - std::exp (-std::abs (z)));
                        y = (1 - b) * y + b * std::abs (y) * 1.5;
                        break;
                    }
                    case 4:
                    {
                        double t = (z * a + b + 1) * 0.25;
                        t -= std::floor (t);
                        y = t < 0.5 ? 4 * t - 1 : 3 - 4 * t;
                        break;
                    }
                    case 5:
                    {
                        if (--S.hc <= 0) { S.hc += b; S.h = z; }
                        const double q = std::pow (2.0, a - 1);
                        y = std::round (juce::jlimit (-1.0, 1.0, S.h) * q) / q;
                        break;
                    }
                    case 6:
                    {
                        const double q = z + b;
                        y = std::tanh (juce::jmax (q, 0.0) * (1 - a) + std::abs (q) * a);
                        break;
                    }
                    case 7:
                        y = (1 - b) * std::tanh (z) + b * std::sin (z * a * pi / 2);
                        break;
                    case 8:
                    {
                        const double az = std::abs (z);
                        S.env += (az - S.env) * (az > S.env ? 0.01 : 0.0005);
                        z /= 1 + b * 3 * S.env;
                        y = 1.5 * z / (1 + std::abs (z));
                        S.lt += (y - S.lt) * ageC;
                        y = S.lt;
                        break;
                    }
                    default:
                    {
                        const double soft = z >= 0 ? 1 - std::exp (-z) : -(1 - std::exp (z * a)) / a;
                        const double hard = z >= 0 ? juce::jmin (z, 1.0) : juce::jmax (z * a, -1.0) / a;
                        y = hard + (soft - hard) * b;
                    }
                }
                const double yo = y - S.x1 + 0.995 * S.y1;   // DC blocker
                S.x1 = y; S.y1 = yo; y = yo;
                if (tc < 1) { S.lp += (y - S.lp) * tc; y = S.lp; }
                X[i] = (float) (x + (y * out - x) * mix);
            }
        }
    }

private:
    struct Ch { double x1 = 0, y1 = 0, lp = 0, hc = 0, h = 0, env = 0, lt = 0; };
    Ch st[2];
};

//== Delay (Stereo / Ping-Pong / Tape / Diffuse) ===================================
class Delay final : public Processor
{
public:
    void prepare (double sr) override
    {
        const auto len = (size_t) std::ceil (sr * 10.0) + 8;
        bl.assign (len, 0.0f);
        br.assign (len, 0.0f);
        const auto apLen = (size_t) std::ceil (2.0 * 400.0 * sr / 48000.0) + 4;
        for (auto& c : ap) for (auto& a : c) { a.b.assign (apLen, 0.0f); a.i = 0; }
        w = 0; D = -1; hl = hr = ll = lr = ho = lo = lfo = env = 0;
    }

    void process (const float* v, const Settings& s, float* L, float* R, int N, double sr, double bpm) noexcept override
    {
        if (bl.empty()) return;
        const int len = (int) bl.size(), mode = juce::jlimit (0, 3, s.mode);
        const double secs = s.sync ? syncSeconds (s.num, s.den, s.dot, bpm) : v[0] / 1000.0;
        const double Dt = juce::jmin ((double) len - 8, juce::jmax (1.0, secs * sr));
        if (D < 0) D = Dt;
        const double fb = v[1] * (mode == 2 ? 1.02 : 0.98), a = v[2], b = v[3], mix = v[6];
        const double hc = onePole (v[5], sr), lc = 1.0 - std::exp (-twoPi * v[4] / sr);
        const double gd = a * 0.7, sz = 0.5 + 1.5 * b;
        static constexpr int DIFF[4] { 142, 107, 379, 277 };
        int apn[4];
        for (int k = 0; k < 4; ++k)
            apn[k] = juce::jlimit (1, (int) ap[0][k].b.size() - 1, (int) std::round (DIFF[k] * sz * sr / 48000.0));

        auto diffuse = [&] (double x, int c)
        {
            for (int k = 0; k < 4; ++k)
            {
                auto& A = ap[c][k];
                if (A.i >= apn[k]) A.i = 0;
                const double bo = A.b[(size_t) A.i], y = bo - gd * x;
                A.b[(size_t) A.i] = (float) (x + gd * y);
                A.i++;
                x = y;
            }
            return x;
        };

        for (int i = 0; i < N; ++i)
        {
            D += (Dt - D) * 0.0008;
            double d = D;
            const double xl = L[i], xr = R[i];
            if (mode == 2)
            {
                lfo += 1.0 / sr;
                d *= 1 + a * (0.004 * std::sin (twoPi * 0.6 * lfo) + 0.0015 * std::sin (twoPi * 7.3 * lfo));
            }
            // Both sides repeat exactly on the beat. Stereo's SPREAD only shifts what you HEAR on the right by up
            // to 12 ms (a fixed offset, outside the feedback), so the repeats never drift off the grid.
            const double dd = juce::jmin ((double) len - 8, d);
            double wl = readBack (bl, w, dd), wr = readBack (br, w, dd);
            if (mode == 3) { wl = diffuse (wl, 0); wr = diffuse (wr, 1); }
            hl += (wl - hl) * hc; hr += (wr - hr) * hc;
            double fl = hl, fr = hr;
            ll += (fl - ll) * lc; lr += (fr - lr) * lc;
            fl -= ll; fr -= lr;
            double heardR = fr;
            if (mode == 0 && a > 0)
            {
                const double wo = readBack (br, w, juce::jmin ((double) len - 8, dd + a * 0.012 * sr));
                ho += (wo - ho) * hc;
                lo += (ho - lo) * lc;
                heardR = ho - lo;
            }
            if (mode == 2)
            {
                const double sg = 1 + b * 4;
                fl = std::tanh (fl * sg) / sg;
                fr = std::tanh (fr * sg) / sg;
            }
            double il, ir;
            if (mode == 1) { il = (xl + xr) * 0.5 + fr * fb; ir = fl * fb; }
            else           { il = xl + fl * fb; ir = xr + fr * fb; }
            bl[(size_t) w] = (float) il;
            br[(size_t) w] = (float) ir;
            if (++w >= len) w = 0;
            double ol = fl, orr = heardR;
            if (mode == 1)
            {
                const double m = (ol + orr) * 0.5;
                ol = m + (ol - m) * a;
                orr = m + (orr - m) * a;
            }
            if (mode < 2 && b > 0)
            {
                const double e = std::abs (xl) + std::abs (xr);
                env += (e - env) * (e > env ? 0.01 : 0.0003);
                const double k = 1 - b * juce::jmin (1.0, env * 3);
                ol *= k; orr *= k;
            }
            L[i] = (float) (xl * (1 - mix) + ol * mix);
            R[i] = (float) (xr * (1 - mix) + orr * mix);
        }
    }

private:
    struct AllPass { std::vector<float> b; int i = 0; };
    std::vector<float> bl, br;
    AllPass ap[2][4];
    int w = 0;
    double D = -1, hl = 0, hr = 0, ll = 0, lr = 0, ho = 0, lo = 0, lfo = 0, env = 0;
};

//== Filter (12 / 24 dB, LP HP BP Notch) ============================================
class Filter final : public Processor
{
public:
    void prepare (double) override { c = -1; for (auto& x : s) x[0] = x[1] = 0; }

    void process (const float* v, const Settings& set, float* L, float* R, int N, double sr, double) noexcept override
    {
        const int mode = juce::jlimit (0, 3, set.mode), stages = set.slope ? 2 : 1;
        const double k = 2 - 1.95 * v[1], dg = dbToGain (v[2]), comp = 1.0 / std::sqrt (dg), mix = v[3];
        const bool drv = v[2] > 0.05;
        if (c < 0) c = v[0];
        const double c0 = c, c1 = v[0];
        c = c1;
        for (int i = 0; i < N; ++i)
        {
            const double fc = juce::jmin (sr * 0.45, c0 * std::pow (c1 / c0, (double) i / N));
            const double g = std::tan (pi * fc / sr), a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* X = ch ? R : L;
                const double x0 = X[i];
                double x = drv ? std::tanh (x0 * dg) * comp : x0;
                for (int q = 0; q < stages; ++q)
                {
                    auto& S = s[ch * 2 + q];
                    const double v3 = x - S[1], v1 = a1 * S[0] + a2 * v3, v2 = S[1] + a2 * S[0] + a3 * v3;
                    S[0] = 2 * v1 - S[0]; S[1] = 2 * v2 - S[1];
                    x = mode == 0 ? v2 : mode == 1 ? x - k * v1 - v2 : mode == 2 ? v1 * k : x - k * v1;
                }
                X[i] = (float) (x0 + (x - x0) * mix);
            }
        }
    }

private:
    double c = -1, s[4][2] {};
};
} // namespace

std::unique_ptr<Processor> createProcessor (Type t, double sr)
{
    std::unique_ptr<Processor> p;
    switch (t)
    {
        case Type::reverb: p = std::make_unique<Reverb>(); break;
        case Type::comp:   p = std::make_unique<Compressor>(); break;
        case Type::eq:     p = std::make_unique<Equaliser>(); break;
        case Type::chorus: p = std::make_unique<Chorus>(); break;
        case Type::phaser: p = std::make_unique<Phaser>(); break;
        case Type::morph:  p = std::make_unique<Morph>(); break;
        case Type::util:   p = std::make_unique<Utility>(); break;
        case Type::dist:   p = std::make_unique<Distortion>(); break;
        case Type::delay:  p = std::make_unique<Delay>(); break;
        case Type::filter: p = std::make_unique<Filter>(); break;
        case Type::spectral: p = std::make_unique<Spectral>(); break;
        case Type::count:  break;
    }
    if (p != nullptr)
        p->prepare (sr);
    return p;
}
} // namespace emx::fx
