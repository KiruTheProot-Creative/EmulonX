// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SampleEdit.h"
#include <cmath>

namespace emx::edit
{
const char* warpShapeName (WarpShape s)
{
    static const char* n[] { "BEND", "SQUEEZE", "FOLD", "MIRROR", "STEPS", "DRIVE" };
    return n[juce::jlimit (0, numWarpShapes - 1, (int) s)];
}

const char* warpShapeHint (WarpShape s)
{
    static const char* h[] { "pushes the wave up on one side: fuller, warmer, uneven",
                             "pinches the wave towards its peaks: thinner, brighter",
                             "folds the peaks back inside: metallic overtones",
                             "flips the bottom half up: an octave higher, hollow",
                             "rounds the wave into steps: crunchy, lo-fi",
                             "rounds off the peaks: louder, saturated" };
    return h[juce::jlimit (0, numWarpShapes - 1, (int) s)];
}

float warpValue (WarpShape s, float v, float a) noexcept
{
    v = juce::jlimit (-1.0f, 1.0f, v);
    a = juce::jlimit (0.0f, 1.0f, a);
    switch (s)
    {
        case WarpShape::bend:    return juce::jlimit (-1.0f, 1.0f, v + a * 0.8f * (1.0f - v * v));
        case WarpShape::squeeze: return std::copysign (std::pow (std::abs (v), 1.0f + 4.0f * a), v);
        case WarpShape::fold:
        {
            const float x = v * (1.0f + 6.0f * a) * 0.25f + 0.25f;
            return 4.0f * std::abs (x - std::round (x)) - 1.0f;
        }
        case WarpShape::mirror:  return (1.0f - a) * v + a * (std::abs (v) * 2.0f - 0.6f);
        case WarpShape::steps:
        {
            const float levels = std::max (2.0f, std::round (2.0f + (1.0f - a) * (1.0f - a) * 40.0f));
            return std::round (v * levels) / levels;
        }
        case WarpShape::drive:
        {
            const float g = 1.0f + 8.0f * a;
            return (1.0f - a) * v + a * std::tanh (v * g) / std::tanh (g);
        }
    }
    return v;
}

float warpReference (const SampleData& s, int from, int to) noexcept
{
    const int n = s.length();
    to = to < 0 ? n : juce::jlimit (0, n, to);
    from = juce::jlimit (0, to, from);
    float peak = 0.0f;
    for (int c = 0; c < s.audio.getNumChannels(); ++c)
        peak = std::max (peak, s.audio.getMagnitude (c, from, to - from));
    return peak > 1.0e-6f ? peak : 1.0f;
}

float warpedSample (const Warp& w, float x, float ref) noexcept
{
    const float shaped = warpValue (w.shape, x / ref, w.amount) * ref;
    return w.mix * shaped + (1.0f - w.mix) * x;
}

std::shared_ptr<SampleData> applyWarp (const SampleData& src, const Warp& w)
{
    auto out = std::make_shared<SampleData> (src);
    out->source = src.source.startsWith ("edited|") ? src.source : "edited|" + src.source;
    const int n = src.length();
    const int to = w.to < 0 ? n : juce::jlimit (0, n, w.to);
    const int from = juce::jlimit (0, to, w.from);
    const float ref = warpReference (src, from, to);
    // short fades at the edges of a part, so changing only the loop doesn't click where it starts and ends
    const int edge = juce::jmin (64, (to - from) / 4);
    for (int c = 0; c < out->audio.getNumChannels(); ++c)
    {
        auto* d = out->audio.getWritePointer (c);
        for (int i = from; i < to; ++i)
        {
            float k = 1.0f;
            if (from > 0 && i - from < edge) k = (float) (i - from) / (float) edge;
            if (to < n && to - 1 - i < edge) k = std::min (k, (float) (to - 1 - i) / (float) edge);
            d[i] = d[i] + (warpedSample (w, d[i], ref) - d[i]) * k;
        }
    }
    return out;
}

std::vector<int> findHits (const SampleData& s, float sensitivity, int maxHits)
{
    const int n = s.length();
    std::vector<int> hits { 0 };
    if (n < 256)
        return hits;
    // Loudness in short steps (about 5 ms), then a hit wherever it jumps up clearly from the steps before.
    const int hop = juce::jmax (64, (int) (s.sampleRate * 0.005));
    std::vector<float> env;
    for (int i = 0; i + hop <= n; i += hop)
    {
        float e = 0.0f;
        for (int c = 0; c < s.audio.getNumChannels(); ++c)
        {
            const float* d = s.audio.getReadPointer (c, i);
            for (int k = 0; k < hop; ++k)
                e += d[k] * d[k];
        }
        env.push_back (std::sqrt (e / (float) (hop * s.audio.getNumChannels())));
    }
    float peak = 1.0e-9f;
    for (auto e : env) peak = std::max (peak, e);
    const float ratio = 1.4f + (1.0f - juce::jlimit (0.0f, 1.0f, sensitivity)) * 4.0f;   // how big a jump counts
    const float floor = peak * (0.25f - 0.23f * sensitivity);                             // ignore very quiet bits
    const int minGap = juce::jmax (1, (int) (s.sampleRate * 0.06) / hop);                 // hits at least 60 ms apart
    int last = -minGap;
    for (size_t i = 2; i < env.size(); ++i)
    {
        const float before = std::max (1.0e-6f, std::max (env[i - 1], env[i - 2]));
        if (env[i] > floor && env[i] > before * ratio && (int) i - last >= minGap)
        {
            // start a little before the jump, at the quietest point, so the slice keeps the attack
            int at = (int) (i - 1) * hop;
            const float* d = s.audio.getReadPointer (0);
            int best = at;
            for (int k = juce::jmax (0, at - hop); k < juce::jmin (n, at + hop); ++k)
                if (std::abs (d[k]) < std::abs (d[best])) best = k;
            if (best > hop)
                hits.push_back (best);
            last = (int) i;
            if ((int) hits.size() >= maxHits)
                break;
        }
    }
    return hits;
}

std::vector<int> splitEvenly (int length, int count)
{
    count = juce::jlimit (1, 128, count);
    std::vector<int> cuts;
    for (int i = 0; i < count; ++i)
        cuts.push_back ((int) ((juce::int64) length * i / count));
    return cuts;
}
} // namespace emx::edit
