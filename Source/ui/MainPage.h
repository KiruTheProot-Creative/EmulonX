// Emulon X — the Main tab: preset, bank, 16 macros (master volume lives in the top bar).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "PresetPanel.h"
#include "BankPanel.h"
#include "../Settings.h"

namespace emx
{
class EmulonXProcessor;

class MainPage final : public juce::Component,
                       private juce::ChangeListener
{
public:
    explicit MainPage (EmulonXProcessor&);
    ~MainPage() override;

    /** Called ~30x per second by the editor to update meters. */
    void tick();
    PresetPanel& getPresetPanel() noexcept { return presetPanel; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    EmulonXProcessor& proc;
    juce::SharedResourcePointer<Settings> settings;
    PresetPanel presetPanel;
    BankPanel bankPanel;
    juce::OwnedArray<EmxKnob> macros;
    juce::Rectangle<float> macroArea, macroInset;
};
} // namespace emx
