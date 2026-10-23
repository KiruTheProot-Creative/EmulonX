// Emulon X — the FX tab: effect chains (whole preset + per zone), envelopes, modulation.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "../fx/FxModel.h"

namespace emx
{
class EmulonXProcessor;

//== Modulation helpers shared with the Main tab ===================================
namespace modui
{
    /** Right-click menu: connect envelopes, macros or the mod wheel to a knob. */
    void showMenu (EmulonXProcessor&, fx::Target, juce::Component* near);
    /** If a macro is waiting to be linked ("learn"), link it to this knob and return true. */
    bool tryLearn (EmulonXProcessor&, fx::Target);
    /** Rings to draw around a knob. */
    ModRings rings (EmulonXProcessor&, fx::Target, float base);
    /** Right-click menu of a macro knob: its connections and "learn". */
    void showMacroMenu (EmulonXProcessor&, int macroIndex, juce::Component* near);
}

class FxCard;
class EnvelopeGraph;
class DestinationList;

class FxPage final : public juce::Component,
                     private juce::ChangeListener
{
public:
    explicit FxPage (EmulonXProcessor&);
    ~FxPage() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    int getChannel() const noexcept { return channel; }
    void setChannel (int chainId);
    int getSelectedEnvelope() const noexcept { return envIndex; }
    void selectEnvelope (int index);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();          // structure changed: new cards, envelope options, destinations
    void syncValues();       // only values changed
    void rebuildChannelBox();
    void rebuildEnvelopeOptions();
    void syncEnvelopeOptions();
    void layoutEnvelopeOptions();

    EmulonXProcessor& proc;
    int channel = 0, envIndex = 0, builtStructure = -1, builtZoneCount = -1;

    // Chain side
    juce::ComboBox channelBox, addBox;
    LedLabel bpmBox { 9.0f };
    LedDisplay bpmLed;
    juce::Viewport chainView;
    juce::Component chainContent;
    juce::OwnedArray<FxCard> cards;

    // Envelope side
    juce::OwnedArray<juce::TextButton> envTabs;
    juce::TextButton envAdd { "+" }, envRemove { "X" }, typeShape { "SHAPE" }, typeAhdsr { "AHDSR" };
    juce::ComboBox shapeBox, gridBox;
    std::unique_ptr<EnvelopeGraph> graph;
    juce::OwnedArray<juce::Component> envOptions;
    std::vector<std::function<void (const fx::Envelope&)>> envSyncers;
    std::unique_ptr<DestinationList> destinations;
    juce::Viewport destView;

    juce::Rectangle<float> chainArea, envArea, chainListArea, graphArea, optionsArea, destArea;
    juce::Rectangle<float> channelLabel, hintArea;
};
} // namespace emx
