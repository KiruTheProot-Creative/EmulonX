// Emulon X — V3: the keyboard along the bottom of the window (every tab but the Editor), with a
// pitch wheel (springs back) and a mod wheel on its left. Plays through the processor's keyboard
// state, so it sounds exactly like a MIDI keyboard would.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <bitset>

namespace emx
{
class EmulonXProcessor;

class PlayKeys final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    explicit PlayKeys (EmulonXProcessor&);

    /** Called ~30x per second: shows held notes and the wheels as they move (also from MIDI). */
    void tick();
    /** Optional colour for a key (the Machine tab marks its pads); transparent = normal. */
    std::function<juce::Colour (int note)> keyTint;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct KeyRect { float x, w; bool black; };
    KeyRect keyRect (int note) const;
    int noteAt (juce::Point<float>) const;
    int velocityAt (float y, int note) const;
    void setWheel (int which, float y);

    EmulonXProcessor& proc;
    juce::Rectangle<float> wheelArea[2], octDown, octUp, keysArea;
    int firstNote = 36, lastNote = 96, playingNote = -1, dragWheel = -1;
    float keyW = 10.0f;
    std::bitset<128> held;
    float shownBend = 0.0f, shownMod = 0.0f;
};
} // namespace emx
