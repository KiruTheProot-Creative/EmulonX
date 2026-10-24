// Emulon X — sorting names like the Windows file explorer does ("natural" order):
// letters ignore upper / lower case, and numbers inside names count by their value,
// so "Pad 2" comes before "Pad 10" and "KEYS 001" before "KEYS 002".
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>

namespace emx
{
/** < 0 if a comes first, > 0 if b comes first, 0 if they are the same. */
inline int naturalCompare (const juce::String& a, const juce::String& b)
{
    auto p = a.getCharPointer(), q = b.getCharPointer();
    for (;;)
    {
        const auto ca = *p, cb = *q;
        if (ca == 0 || cb == 0)
            break;
        if (juce::CharacterFunctions::isDigit (ca) && juce::CharacterFunctions::isDigit (cb))
        {
            // compare the whole number by its value: skip leading zeros, then longer = bigger
            while (*p == '0') ++p;
            while (*q == '0') ++q;
            auto ea = p, eb = q;
            int la = 0, lb = 0;
            while (juce::CharacterFunctions::isDigit (*ea)) { ++ea; ++la; }
            while (juce::CharacterFunctions::isDigit (*eb)) { ++eb; ++lb; }
            if (la != lb)
                return la < lb ? -1 : 1;
            for (auto x = p, y = q; x != ea; ++x, ++y)
                if (*x != *y)
                    return *x < *y ? -1 : 1;
            p = ea;
            q = eb;
            continue;
        }
        const auto la = juce::CharacterFunctions::toLowerCase (ca), lb = juce::CharacterFunctions::toLowerCase (cb);
        if (la != lb)
            return la < lb ? -1 : 1;
        ++p;
        ++q;
    }
    if (*p != 0 || *q != 0)
        return *p == 0 ? -1 : 1;              // the shorter name first ("Pad" before "Pad 2")
    return a.compare (b);                     // same apart from case / leading zeros: keep a fixed order
}

/** For std::sort and friends. */
inline bool naturalLess (const juce::String& a, const juce::String& b) { return naturalCompare (a, b) < 0; }
} // namespace emx
