// Emulon X — the ARP tab: arpeggiator settings, 8 patterns and an arrangement.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "../engine/Arpeggiator.h"

namespace emx
{
class EmulonXProcessor;
class StepGrid;
class ArrangeBar;

class ArpPage final : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit ArpPage (EmulonXProcessor&);
    ~ArpPage() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

    int getEditPattern() const noexcept { return editPattern; }
    void setEditPattern (int p);
    void edit (std::function<void (ArpSettings&)>);
    EmulonXProcessor& getProcessor() noexcept { return proc; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    EmulonXProcessor& proc;
    int editPattern = 0, shownPlayingPattern = -2;
    std::optional<ArpPattern> clipboard;

    juce::TextButton onButton { "ARP OFF" }, latchButton { "HOLD" };
    juce::TextButton velPattern { "PATTERN" }, velPlayed { "AS PLAYED" };
    juce::ComboBox modeBox, denBox;
    juce::TextButton dots[3] { juce::TextButton ("STR"), juce::TextButton ("DOT"), juce::TextButton ("TRI") };
    ValueControl beats { "Beats", ValueControl::Style::knob }, octaves { "Octaves", ValueControl::Style::knob },
                 gate { "Gate", ValueControl::Style::knob }, swing { "Swing", ValueControl::Style::knob },
                 length { "Steps", ValueControl::Style::knob };
    LedDisplay rateLed, heldLed;
    juce::OwnedArray<juce::TextButton> patternTabs;
    juce::TextButton copyButton { "COPY" }, pasteButton { "PASTE" };
    std::unique_ptr<ArrangeBar> arrange;
    std::unique_ptr<StepGrid> grid;
    juce::TextButton allOn { "ALL ON" }, accents { "ACCENTS" }, randomVel { "RANDOM" }, flat { "FLAT" }, clearNotes { "NOTES 0" };

    juce::Rectangle<float> topArea, patternArea, bottomArea;
    juce::Rectangle<float> modeLabel, velLabel, rateLabel, arrangeLabel, patternLabel;
};
} // namespace emx
