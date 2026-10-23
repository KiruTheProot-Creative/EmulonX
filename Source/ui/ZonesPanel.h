// Emulon X — the zone list on the Main tab (until the full Editor tab arrives).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace emx
{
class EmulonXProcessor;

class ZonesPanel final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::ListBoxModel
{
public:
    explicit ZonesPanel (EmulonXProcessor&);
    ~ZonesPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void openLoadDialog();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void deleteKeyPressed (int lastRowSelected) override;

    EmulonXProcessor& proc;
    juce::ListBox list { "zones", this };
    juce::TextButton loadButton { "LOAD..." }, mapButton { "AUTO MAP" }, removeButton { "REMOVE" }, clearButton { "CLEAR" };
    juce::TextButton interpButtons[3] { juce::TextButton ("LINEAR"), juce::TextButton ("CUBIC"), juce::TextButton ("SINC HQ") };
    juce::TextButton keepSpeedButton { "KEEP SPEED" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> listArea, statusArea;
};
} // namespace emx
