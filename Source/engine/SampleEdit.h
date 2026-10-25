// Emulon X — editing the sample itself (V2 "SuperSampler"): reshaping the waveform (warp) and finding
// hits for the splicer. Runs on the message thread; makes new samples, never changes one in place.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Zone.h"

namespace emx::edit
{
enum class WarpShape { bend = 0, squeeze, fold, mirror, steps, drive };
inline constexpr int numWarpShapes = 6;
const char* warpShapeName (WarpShape);      // BEND, SQUEEZE, ...
const char* warpShapeHint (WarpShape);      // what it does to the sound, in a few words

/** One value of a wave (-1..1) reshaped; amount 0..1 (0 leaves it as it is, except MIRROR / STEPS at their gentlest). */
float warpValue (WarpShape, float v, float amount) noexcept;

struct Warp
{
    WarpShape shape = WarpShape::bend;
    float amount = 0.45f, mix = 1.0f;   // 0..1
    int from = 0, to = -1;              // the frames to change (to -1 = the end)
};

/** The level the warp works at: the loudest point of the part it changes (so quiet samples bend as much as loud ones). */
float warpReference (const SampleData&, int from, int to) noexcept;
/** One reshaped value as APPLY would make it (for drawing a preview). */
float warpedSample (const Warp&, float sample, float reference) noexcept;

/** A copy of the sample with the warp baked in. Keeps the name; the source becomes "edited|<old source>". */
std::shared_ptr<SampleData> applyWarp (const SampleData&, const Warp&);

/** Where the hits (drum strikes, note starts) are, as frames, always starting with 0.
    sensitivity 0..1: higher finds quieter hits. At most maxHits. */
std::vector<int> findHits (const SampleData&, float sensitivity, int maxHits = 64);

/** count equal parts: 0, n/count, 2n/count ... */
std::vector<int> splitEvenly (int length, int count);
} // namespace emx::edit
