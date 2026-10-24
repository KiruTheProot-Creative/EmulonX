// Emulon X — the Bank panel on the Main tab: collect presets, export them as one .emxb.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"

namespace emx
{
class EmulonXProcessor;

class BankPanel final : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit BankPanel (EmulonXProcessor&);
    ~BankPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    class Grid;
    void select (int index);

    EmulonXProcessor& proc;
    LedLabel nameLabel { 10.0f };
    std::unique_ptr<Grid> grid;
    juce::Viewport viewport;
    int selected = -1;
    juce::TextButton addButton { "ADD CURRENT" }, loadButton { "LOAD" }, removeButton { "REMOVE" },
                     openButton { "OPEN..." }, exportButton { "EXPORT..." }, clearButton { "CLEAR" },
                     sortButton { "SORT A-Z" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> listArea;
};
} // namespace emx
