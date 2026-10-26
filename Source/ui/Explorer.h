// Emulon X — the Explorer sidebar: browse sample folders, banks and presets.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "../Settings.h"

namespace emx
{
class EmulonXProcessor;

/** Small metal button with a folder + icon. */
class FolderAddButton final : public juce::Button
{
public:
    FolderAddButton();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class Explorer final : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit Explorer (EmulonXProcessor&);
    ~Explorer() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showTab (int index);   // 0 samples, 1 banks
    void rescan();
    void addFolder();

    //== Used by the tree items ================================================
    EmulonXProcessor& proc;
    void setHint (const juce::String& h) { hint = h; repaint (hintArea.toNearestInt().expanded (2)); }
    void removeRootFolder (Settings::Folder, const juce::String& path);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;   // search: runs a moment after typing stops
    void buildSearchResults (juce::TreeViewItem& root, const juce::String& query);
    void chooseFolderFor (Settings::Folder);

    juce::SharedResourcePointer<Settings> settings;
    FolderAddButton addButton;
    juce::TextButton tabSamples { "SAMPLES" }, tabBanks { "BANKS" };
    juce::TreeView tree;
    juce::TextEditor search;
    std::unique_ptr<juce::TreeViewItem> rootItem;
    std::unique_ptr<juce::XmlElement> openness[2];
    std::unique_ptr<juce::FileChooser> chooser;
    int tab = 0, treeTab = 0, lastFilesWritten = 0;
    juce::String hint;
    juce::Rectangle<float> treeArea, hintArea;
};
} // namespace emx
