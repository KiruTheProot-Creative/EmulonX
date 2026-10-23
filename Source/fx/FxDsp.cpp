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

//== Reverb: 8-line feedback delay network with diffusion ========================
class Reverb final : public Processor
{
public:
    void prepare (double sr) override
    {
        mx = (int) std::ceil (73.1 * 1.35 * 2.0 * sr / 1000.0) + 64;
        for (auto& l : lines) { l.b.assign ((size_t) mx, 0.0f); l.lp = 0; }
        pre.assign ((size_t) std::ceil (sr * 0.3) + 4, 0.0f);
        for (int k = 0; k < 4; ++k)
        {
            ap[k].b.assign ((size_t) std::ceil (apMs[k] * sr / 1000.0) + 2, 0.0f);
            ap[k].n = juce::jmax (1, (int) std::round (apMs[k] * sr / 1000.0));
            ap[k].i = 0;
        }
        w = pw = 0; ph = hl = hr = 0;
        std::fill (std::begin (y), std::end (y), 0.0);
    }

    void process (const float* v, const Settings& s, float* L, float* R, int N, double sr, double) noexcept override
    {
        if (mx == 0) return;
        const int mode = juce::jlimit (0, 3, s.mode);
        const double size = v[0], decay = v[1], preMs = v[2], damp = v[3], lo = v[4], width = v[5], mix = v[6];
        const double sc = scale[mode] * (0.4 + 1.6 * size);
        double lens[8], gs[8], md[8];
        for (int j = 0; j < 8; ++j)
        {
            lens[j] = juce::jmin ((double) mx - 24.0, lineMs[j] * sc * sr / 1000.0);
            gs[j] = std::pow (10.0, -3.0 * lens[j] / (decay * sr));
        }
        const double dc = onePole (damp, sr), lc = 1.0 - std::exp (-twoPi * lo / sr);
        const int plen = (int) pre.size();
        const int pd = juce::jlimit (1, plen - 1, (int) std::round (preMs * 0.001 * sr));
        const double gd = diffusion[mode];
        ph += N / sr;
        for (int j = 0; j < 8; ++j)
            md[j] = lens[j] + (mode == 2 ? 2.0 : 5.0) * std::sin (twoPi * ph * (0.5 + j * 0.07) + j);

        for (int i = 0; i < N; ++i)
        {
            const double xl = L[i], xr = R[i];
            pre[(size_t) pw] = (float) ((xl + xr) * 0.5);
            int rp = pw - pd;
            if (rp < 0) rp += plen;
            double d = pre[(size_t) rp];
            if (++pw >= plen) pw = 0;
            for (int k = 0; k < 4; ++k)
            {
                auto& A = ap[k];
                const double bo = A.b[(size_t) A.i], yv = bo - gd * d;
                A.b[(size_t) A.i] = (float) (d + gd * yv);
                if (++A.i >= A.n) A.i = 0;
                d = yv;
            }
            double sum = 0;
            for (int j = 0; j < 8; ++j)
            {
                auto& Ln = lines[j];
                double p = w - md[j];
                if (p < 0) p += mx;
                const int i0 = juce::jmin (mx - 1, (int) p);
                const double f = p - i0;
                const int i1 = i0 + 1 >= mx ? 0 : i0 + 1;
                double yy = Ln.b[(size_t) i0] + (Ln.b[(size_t) i1] - Ln.b[(size_t) i0]) * f;
                Ln.lp += (yy - Ln.lp) * dc;
                yy = Ln.lp * gs[j];
                y[j] = yy;
                sum += yy;
            }
            sum *= 0.25;
            for (int j = 0; j < 8; ++j)
                lines[j].b[(size_t) w] = (float) (y[j] - sum + ((j & 1) ? -d : d) * 0.5);
            if (++w >= mx) w = 0;

            const double og = outGain[mode];
            double ol = (y[0] + y[2] + y[4] + y[6]) * og, orr = (y[1] + y[3] + y[5] + y[7]) * og;
            const double m = (ol + orr) * 0.5, sd = (ol - orr) * 0.5 * width;
            ol = m + sd; orr = m - sd;
            hl += (ol - hl) * lc; ol -= hl;
            hr += (orr - hr) * lc; orr -= hr;
            L[i] = (float) (xl * (1 - mix) + ol * mix);
            R[i] = (float) (xr * (1 - mix) + orr * mix);
        }
    }

private:
    static constexpr double lineMs[8] { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
    static constexpr double apMs[4] { 4.7, 3.6, 12.7, 9.3 };
    static constexpr double scale[4] { 0.55, 1.35, 0.9, 0.3 };
    static constexpr double diffusion[4] { 0.5, 0.62, 0.75, 0.45 };
    static constexpr double outGain[4] { 1.1, 1.6, 1.3, 0.9 };
    struct Line { std::vector<float> b; double lp = 0; };
    struct AllPass { std::vector<float> b; int n = 1, i = 0; };
    Line lines[8];
    AllPass ap[4];
    std::vector<float> pre;
    int mx = 0, w = 0, pw = 0;
    double ph = 0, hl = 0, hr = 0, y[8] {};
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
            const double t = v[0], up = v[1], dn = v[2], ig = dbToGain (v[3]), K = sqrt2;
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
        w = 0; D = -1; hl = hr = ll = lr = lfo = env = 0;
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
            const double dr = mode == 0 ? juce::jmin ((double) len - 8, d * (1 + a * 0.5)) : d;
            double wl = readBack (bl, w, juce::jmin ((double) len - 8, d)), wr = readBack (br, w, juce::jmin ((double) len - 8, dr));
            if (mode == 3) { wl = diffuse (wl, 0); wr = diffuse (wr, 1); }
            hl += (wl - hl) * hc; hr += (wr - hr) * hc;
            double fl = hl, fr = hr;
            ll += (fl - ll) * lc; lr += (fr - lr) * lc;
            fl -= ll; fr -= lr;
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
            double ol = fl, orr = fr;
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
    double D = -1, hl = 0, hr = 0, ll = 0, lr = 0, lfo = 0, env = 0;
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
        case Type::count:  break;
    }
    if (p != nullptr)
        p->prepare (sr);
    return p;
}
} // namespace emx::fx
