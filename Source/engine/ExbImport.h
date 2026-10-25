// Emulon X — EXPERIMENTAL: reading Emulator X banks (.exb + SamplePool\*.ebl) you already own.
// Worked out from the files themselves (no official documentation exists). Step 1: samples, key and
// velocity ranges, root keys and loops. Envelopes, filters and modulation are not read yet.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "SampleLoader.h"

namespace emx::exb
{
/** An Emulator X bank: the .exb file, or the "Name.exb" folder it usually sits in (with its SamplePool). */
bool isExb (const juce::File&);
/** The .exb file itself for a bank folder or file. */
juce::File bankFile (const juce::File&);

/** The bank's presets; each entry is the preset's number in the bank ("0", "1", ...). */
BankInfo readInfo (const juce::File&);

/** One preset as Emulon X zones (replaces the current preset). */
LoadResult loadPreset (const juce::File& bank, int presetNumber);

/** One sample file (.ebl): 16-bit mono or stereo, with its loop. Used again when a project re-opens. */
std::shared_ptr<SampleData> loadSample (const juce::File& ebl, juce::String& error, bool* loops = nullptr);
} // namespace emx::exb
