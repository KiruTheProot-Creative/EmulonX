// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Zone.h"

namespace emx
{
ZoneParams ZoneParams::defaultsFor (const SampleData& s)
{
    ZoneParams p;
    const int n = s.length();
    p.root = s.rootKey >= 0 ? s.rootKey : (noteFromName (s.name) >= 0 ? noteFromName (s.name) : 60);
    const bool hasLoop = s.loopStart >= 0 && s.loopEnd > s.loopStart && s.loopEnd <= n;
    p.loopMode  = hasLoop ? LoopMode::loop : LoopMode::off;
    p.loopStart = hasLoop ? s.loopStart : n / 2;
    p.loopEnd   = hasLoop ? s.loopEnd : n;
    p.clampTo (n);
    return p;
}

void ZoneParams::clampTo (int n) noexcept
{
    n = juce::jmax (1, n);
    loopEnd   = juce::jlimit (1, n, loopEnd);
    loopStart = juce::jlimit (0, loopEnd - 1, loopStart);
    crossfade = juce::jmax (0, crossfade);
    loKey = juce::jlimit (0, 127, loKey);
    hiKey = juce::jlimit (loKey, 127, hiKey);
    loVel = juce::jlimit (1, 127, loVel);
    hiVel = juce::jlimit (loVel, 127, hiVel);
    root  = juce::jlimit (0, 127, root);
}

int noteFromName (const juce::String& name)
{
    // Last match of: (start or non-letter) [A-G] [#|b]? -? digit (not followed by a digit)
    static const juce::String letters ("CDEFGAB");
    static const int semis[] { 0, 2, 4, 5, 7, 9, 11 };
    const auto s = name.toStdString();
    int found = -1;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = (char) std::toupper ((unsigned char) s[i]);
        const int li = letters.indexOfChar (c);
        if (li < 0 || (i > 0 && (std::isalpha ((unsigned char) s[i - 1]) || s[i - 1] == '#')))
            continue;
        size_t j = i + 1;
        int acc = 0;
        if (j < s.size() && (s[j] == '#' || s[j] == 'b')) { acc = s[j] == '#' ? 1 : -1; ++j; }
        bool neg = false;
        if (j < s.size() && s[j] == '-') { neg = true; ++j; }
        if (j >= s.size() || ! std::isdigit ((unsigned char) s[j]))
            continue;
        if (j + 1 < s.size() && std::isdigit ((unsigned char) s[j + 1]))
            continue;
        const int octave = (s[j] - '0') * (neg ? -1 : 1);
        const int note = semis[li] + acc + 12 * (octave + 1);
        if (note >= 0 && note < 128)
            found = note;
    }
    return found;
}

juce::String noteName (int n)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}
} // namespace emx
