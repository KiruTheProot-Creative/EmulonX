// Emulon X — the Bank panel on the Main tab: collect presets, export them as one .emxb.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"

namespace emx
{
class EmulonXProcessor;

class BankPanel final : public juce::Component,
                        private juce::ChangeListener,
                        private juce::ListBoxModel
{
public:
    explicit BankPanel (EmulonXProcessor&);
    ~BankPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int lastRowSelected) override;
    void returnKeyPressed (int lastRowSelected) override;

    EmulonXProcessor& proc;
    LedLabel nameLabel { 10.0f };
    juce::ListBox list { "bank", this };
    juce::TextButton addButton { "ADD CURRENT" }, loadButton { "LOAD" }, removeButton { "REMOVE" },
                     openButton { "OPEN..." }, exportButton { "EXPORT..." }, clearButton { "CLEAR" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> listArea;
};
} // namespace emx
