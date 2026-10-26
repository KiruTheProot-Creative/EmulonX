// Emulon X — V3 Arranger tab: up to 16 presets at once (multimode). Each part listens to its own
// MIDI channel, so tracks in the DAW on different channels play different parts.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"

namespace emx
{
class EmulonXProcessor;
class ValueBox;

class ArrangerPage final : public juce::Component,
                           private juce::ChangeListener
{
public:
    /** Always the plugin itself (part 1), whichever part the rest of the window edits. */
    explicit ArrangerPage (EmulonXProcessor& main);
    ~ArrangerPage() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    EmulonXProcessor& proc;
    juce::TextButton multiButton { "MULTI" }, singleButton { "SINGLE" }, allNotesOff { "ALL NOTES OFF" };
    juce::OwnedArray<Row> rows;
    juce::Rectangle<float> panel, table, columns, hint;
    int shownVersion = -1;
};
} // namespace emx
