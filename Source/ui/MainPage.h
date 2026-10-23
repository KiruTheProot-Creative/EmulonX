// Emulon X — the Main tab: preset, bank, master volume + meter, 20 macros.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "ZonesPanel.h"

namespace emx
{
class EmulonXProcessor;

class MainPage final : public juce::Component
{
public:
    explicit MainPage (EmulonXProcessor&);

    /** Called ~30x per second by the editor to update meters. */
    void tick();
    ZonesPanel& getZonesPanel() noexcept { return zonesPanel; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    EmulonXProcessor& proc;
    ZonesPanel zonesPanel;
    EmxKnob masterKnob;
    LevelMeter meter;
    LedDisplay peakLed;
    juce::OwnedArray<EmxKnob> macros;
    juce::Rectangle<float> masterArea, macroArea, macroInset;
};
} // namespace emx
