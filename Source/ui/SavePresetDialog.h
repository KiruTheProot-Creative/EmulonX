// Emulon X — V3 Save Preset window: name, type (free text), who made it, tags, notes, where it goes.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace emx
{
class EmulonXProcessor;

class SavePresetDialog final : public juce::Component
{
public:
    explicit SavePresetDialog (EmulonXProcessor&);
    ~SavePresetDialog() override;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // outside the panel = cancel
    bool keyPressed (const juce::KeyPress&) override;
    void focusFirst();

private:
    void save();
    void update();   // file name preview, SAVE / REPLACE
    juce::File targetFile() const;
    void showTypeMenu();

    EmulonXProcessor& proc;
    juce::TextEditor name, type, author, tags, notes;
    juce::TextButton typeMenu { juce::String::fromUTF8 ("\xe2\x96\xbe") }, folderButton { "CHANGE..." }, cancelButton { "CANCEL" }, saveButton { "SAVE" };
    juce::TextButton addToBank { "ALSO ADD TO THE BANK LIST" };
    juce::File folder;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> panel, labels[6], folderArea, fileLine, infoLine;
};
} // namespace emx
