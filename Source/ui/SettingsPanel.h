// Emulon X — the settings window (opens over the plugin from the gear button).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "../Settings.h"

namespace emx
{
class EmulonXProcessor;

class SettingsPanel final : public juce::Component,
                            private juce::ChangeListener
{
public:
    explicit SettingsPanel (EmulonXProcessor&);
    ~SettingsPanel() override;

    std::function<void()> onClose;

    void showPage (int index);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Row
    {
        juce::String title, help;
        juce::ComboBox box;
        std::function<int()> read;            // current item index
        std::function<void (int)> write;      // item index chosen
        int page = 0;
    };

    class FolderList;
    class HueBar;
    class Swatch;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    Row& addRow (int page, const juce::String& title, const juce::String& help, const juce::StringArray& items,
                 std::function<int()> read, std::function<void (int)> write);
    void refresh();

    EmulonXProcessor& proc;
    juce::SharedResourcePointer<Settings> settings;
    static constexpr int numPages = 6;
    enum Page { folders, performance, audioMidi, interface, customization, about };
    juce::TextButton navButtons[numPages] { juce::TextButton ("FOLDERS"), juce::TextButton ("PERFORMANCE"), juce::TextButton ("AUDIO & MIDI"),
                                            juce::TextButton ("INTERFACE"), juce::TextButton ("CUSTOMIZATION"), juce::TextButton ("ABOUT") };
    juce::TextButton doneButton { "DONE" }, panicButton { "ALL NOTES OFF" }, resetButton { "RESET SETTINGS" };
    juce::HyperlinkButton siteLink { "github.com/KiruTheProot-Creative/EmulonX",
                                     juce::URL ("https://github.com/KiruTheProot-Creative/EmulonX") };
    juce::OwnedArray<Row> rows;
    juce::OwnedArray<FolderList> folderLists;
    std::unique_ptr<HueBar> hueBar;
    juce::OwnedArray<Swatch> swatches;
    juce::TextButton hueResetButton { "BACK TO RED" };
    juce::TextButton skinButtons[3] { juce::TextButton ("EMULON CLASSIC"), juce::TextButton ("EMULON XP - LIGHT"), juce::TextButton ("EMULON XP - DARK") };
    int page = 0;
    juce::Rectangle<int> panel, content;
};
} // namespace emx
