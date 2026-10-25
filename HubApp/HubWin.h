// Emulon X Hub — small Windows helpers kept apart from JUCE (they need <windows.h>).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace emx::hub::win
{
/** Names of the programs that have any of these files open right now (for example a DAW that loaded
    the plugin), asked from Windows' Restart Manager - the same check installers use. Empty elsewhere. */
std::vector<std::wstring> programsUsing (const std::vector<std::wstring>& files);
} // namespace emx::hub::win
