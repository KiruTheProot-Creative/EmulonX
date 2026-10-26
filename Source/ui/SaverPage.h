// Emulon X — V3 SAVER tab: the preset (name, zones, load / save / new), its info (type, who made
// it, tags) and the bank list (collect presets, export them as one .emxb).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "PresetPanel.h"
#include "BankPanel.h"

namespace emx
{
class EmulonXProcessor;

class SaverPage final : public juce::Component,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit SaverPage (EmulonXProcessor&);
    ~SaverPage() override;

    PresetPanel& getPresetPanel() noexcept { return presetPanel; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;   // typing settles: the info goes to the preset
    void refresh();
    void commit();

    EmulonXProcessor& proc;
    PresetPanel presetPanel;
    BankPanel bankPanel;
    juce::TextEditor type, author, tags;
    juce::TextButton typeMenu { juce::String::fromUTF8 ("\xe2\x96\xbe") };
    juce::Rectangle<float> infoArea, infoInset;
    juce::Rectangle<int> labels[3];
};
} // namespace emx
