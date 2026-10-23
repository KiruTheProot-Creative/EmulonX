// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Interpolator.h"

namespace emx
{
const SincTables& SincTables::get()
{
    static const SincTables tables;
    return tables;
}

SincTables::SincTables()
{
    const size_t perTable = (size_t) ((phases + 1) * taps);
    data.resize (perTable * cutoffSteps);
    const double pi = juce::MathConstants<double>::pi;

    for (int q = 1; q <= cutoffSteps; ++q)
    {
        const double fc = (double) q / cutoffSteps;
        float* table = data.data() + (size_t) (q - 1) * perTable;
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = (double) p / phases;
            double sum = 0.0;
            double h[taps];
            for (int j = 0; j < taps; ++j)
            {
                const double d = (j - half + 1) - frac;
                const double x = pi * fc * d;
                const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (x) / x;
                const double w = (d + half) / (2.0 * half);   // Blackman window across +-8 samples
                const double win = (w <= 0.0 || w >= 1.0) ? 0.0
                                 : 0.42 - 0.5 * std::cos (2.0 * pi * w) + 0.08 * std::cos (4.0 * pi * w);
                h[j] = sinc * win;
                sum += h[j];
            }
            for (int j = 0; j < taps; ++j)
                table[p * taps + j] = (float) (h[j] / sum);   // unity gain at DC
        }
    }
}
} // namespace emx
