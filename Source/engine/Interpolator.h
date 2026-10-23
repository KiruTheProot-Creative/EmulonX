// Emulon X — reading a sample between its stored points.
// Linear and cubic (Hermite) are cheap; "Sinc HQ" is a 16-point windowed sinc whose
// cut-off follows the pitch, so samples played higher don't alias.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>
#include <cmath>
#include "Zone.h"

namespace emx
{
class SincTables
{
public:
    static constexpr int taps = 16, half = 8, phases = 512, cutoffSteps = 32;

    /** Built once (about 1 MB), shared by every instance. */
    static const SincTables& get();

    /** Table for a playback step (source samples per output sample). */
    const float* forStep (double step) const noexcept
    {
        const auto fc = step > 1.0 ? 1.0 / step : 1.0;
        const int q = juce::jlimit (1, cutoffSteps, (int) std::floor (fc * cutoffSteps));
        return data.data() + (size_t) (q - 1) * (size_t) ((phases + 1) * taps);
    }

private:
    SincTables();
    std::vector<float> data;
};

/** Value of `ch` (length n) at fractional position x; outside the sample is silence. */
inline float interpolate (const float* ch, double x, int n, Interp mode, const float* sincTable) noexcept
{
    const auto i = (int) std::floor (x);
    const auto f = (float) (x - i);
    auto at = [ch, n] (int k) noexcept { return k >= 0 && k < n ? ch[k] : 0.0f; };

    if (mode == Interp::linear)
    {
        const auto a = at (i), b = at (i + 1);
        return a + (b - a) * f;
    }
    if (mode == Interp::cubic)
    {
        const auto y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const auto c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    const float* t = sincTable + (size_t) std::lround (f * SincTables::phases) * SincTables::taps;
    const int s0 = i - SincTables::half + 1;
    float acc = 0.0f;
    if (s0 >= 0 && s0 + SincTables::taps <= n)
    {
        for (int j = 0; j < SincTables::taps; ++j)
            acc += t[j] * ch[s0 + j];
    }
    else
    {
        for (int j = 0; j < SincTables::taps; ++j)
            acc += t[j] * at (s0 + j);
    }
    return acc;
}
} // namespace emx
