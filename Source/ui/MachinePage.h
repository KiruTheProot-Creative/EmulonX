// Emulon X — V3 Machine tab: drum pads (4 x 4 or 8 x 8), the selected pad's settings and its
// 16-step pattern (8 variations), like a drum machine on top of the preset.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "../engine/Machine.h"
#include <bitset>

namespace emx
{
class EmulonXProcessor;
class ValueBox;

class MachinePage final : public juce::Component,
                          private juce::ChangeListener
{
public:
    explicit MachinePage (EmulonXProcessor&);
    ~MachinePage() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    int getSelectedPad() const noexcept { return selected; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void edit (std::function<void (MachineSettings&)>);
    void editPad (std::function<void (PadSettings&)>);
    juce::Rectangle<float> padRect (int index) const;
    juce::Rectangle<float> stepRect (int step) const;
    int padAt (juce::Point<float>) const;
    int stepAt (juce::Point<float>) const;

    EmulonXProcessor& proc;
    int selected = 0, playingPad = -1, dragStep = -1, lastStep = -1;
    bool learning = false, stepPaintOn = true;
    std::bitset<128> held, flash;

    juce::TextButton grid4 { "4 X 4" }, grid8 { "8 X 8" }, playButton { "PLAY" }, learnButton { "LEARN" }, clearButton { "CLEAR" }, copyButton { "COPY TO NEXT" };
    juce::TextButton variations[MachineSettings::numVariations];
    juce::ComboBox choke, stepLength;
    LedLabel nameLabel { 10.0f };
    std::unique_ptr<ValueBox> noteBox, lengthBox, swingBox;
    ValueControl level { "Level", ValueControl::Style::knob }, pan { "Pan", ValueControl::Style::knob },
                 tune { "Tune", ValueControl::Style::knob }, decay { "Decay", ValueControl::Style::knob };

    juce::Rectangle<float> padsArea, padGrid, padPanel, padInset, seqArea, stepsArea;
    juce::Rectangle<float> labels[6];
};
} // namespace emx
