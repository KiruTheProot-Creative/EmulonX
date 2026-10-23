// Emulon X — the effect processors (audio thread). Ported from the browser prototype.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "FxDefs.h"
#include <memory>

namespace emx::fx
{
/** Switches and choices of an effect that are not knobs. */
struct Settings
{
    int mode = 0;
    int slope = 0;            // filter: 0 = 12 dB, 1 = 24 dB
    bool sync = true;         // delay: time follows the tempo
    int num = 1, den = 4, dot = 0;
};

/** The running state of one effect (delay lines, filter memories...).
    Created on the message thread; only the audio thread calls process(). */
class Processor
{
public:
    virtual ~Processor() = default;
    /** Allocates buffers. Not real-time safe: call before audio runs or while it is stopped. */
    virtual void prepare (double sampleRate) = 0;
    /** v holds the knob values in real units (slot order of the effect's definition). */
    virtual void process (const float* v, const Settings&, float* L, float* R, int n, double sampleRate, double bpm) noexcept = 0;
    float gainReductionDb = 0.0f;   // compressor: lowest gain of the last block (for the GR meter)
};

std::unique_ptr<Processor> createProcessor (Type, double sampleRate);
} // namespace emx::fx
