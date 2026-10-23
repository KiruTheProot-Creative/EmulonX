// Emulon X — the Preset panel on the Main tab: name, zones, load / save / new.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"

namespace emx
{
class EmulonXProcessor;

class PresetPanel final : public juce::Component,
                          private juce::ChangeListener,
                          private juce::ListBoxModel
{
public:
    explicit PresetPanel (EmulonXProcessor&);
    ~PresetPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void openLoadDialog();
    std::function<void()> onOpenZone;   // double-click: edit the zone in the Editor tab
    void save();
    void saveAs();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void deleteKeyPressed (int lastRowSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

    EmulonXProcessor& proc;
    LedLabel nameLabel { 10.0f };
    LedDisplay status;
    juce::ListBox list { "zones", this };
    juce::TextButton loadButton { "LOAD..." }, saveButton { "SAVE" }, saveAsButton { "SAVE AS..." }, newButton { "NEW" };
    juce::TextButton mapButton { "AUTO MAP" }, removeButton { "REMOVE" }, clearButton { "CLEAR" };
    juce::TextButton interpButtons[3] { juce::TextButton ("LINEAR"), juce::TextButton ("CUBIC"), juce::TextButton ("SINC HQ") };
    juce::TextButton keepSpeedButton { "KEEP SPEED" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> listArea;
};
} // namespace emx
