// Emulon X — the ARP tab: arpeggiator settings and its 16-step pattern.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "../engine/Arpeggiator.h"

namespace emx
{
class EmulonXProcessor;
class StepGrid;

class ArpPage final : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit ArpPage (EmulonXProcessor&);
    ~ArpPage() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void edit (std::function<void (ArpSettings&)>);

    EmulonXProcessor& proc;
    juce::TextButton onButton { "ARP OFF" }, latchButton { "LATCH" };
    juce::TextButton velPattern { "PATTERN" }, velPlayed { "AS PLAYED" };
    juce::ComboBox modeBox, denBox;
    juce::TextButton dots[3] { juce::TextButton ("STR"), juce::TextButton ("DOT"), juce::TextButton ("TRI") };
    ValueControl beats { "Beats", ValueControl::Style::knob }, octaves { "Octaves", ValueControl::Style::knob },
                 gate { "Gate", ValueControl::Style::knob }, swing { "Swing", ValueControl::Style::knob },
                 length { "Steps", ValueControl::Style::knob };
    LedDisplay rateLed, heldLed;
    std::unique_ptr<StepGrid> grid;
    juce::TextButton allOn { "ALL ON" }, accents { "ACCENTS" }, randomVel { "RANDOM" }, flat { "FLAT" }, clearOct { "OCTAVES 0" };

    juce::Rectangle<float> topArea, patternArea, bottomArea, gridArea;
    juce::Rectangle<float> modeLabel, velLabel, rateLabel;
};
} // namespace emx
