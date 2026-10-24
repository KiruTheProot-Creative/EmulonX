// Emulon X SD — the main window: toolbar, parts and palette, live preview, inspector, readability bar.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "SkinDocument.h"
#include "PreviewView.h"
#include "Inspector.h"

namespace emx::sd
{
/** Left column: the parts of a skin, the palette, colour shift. */
class PartsPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit PartsPanel (SkinDocument&);
    ~PartsPanel() override;
    std::function<void (const juce::String&)> onSelect;
    void setSelected (const juce::String& k) { selected = k; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::Colour swatchFor (const juce::String& key) const;
    struct Item { juce::String key, title, group; juce::Rectangle<int> r; };
    std::vector<Item> layoutItems() const;

    SkinDocument& doc;
    juce::String selected { "panel" }, hovered;
    juce::OwnedArray<ColourSwatch> palette;
    juce::TextButton hueOn { "ON" }, hueOff { "OFF" };
    int paletteTop = 0;
};

class MainComponent final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    SkinDocument doc;
    std::function<void (const juce::String&)> onTitleChanged;

    void select (const juce::String& partKey);
    bool openFile (const juce::File&);
    void setStatus (const juce::String&);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshToolbar();
    void save (bool askForFile);
    void saveToMySkins();
    void writeTo (const juce::File&);

    juce::ComboBox startFrom;
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" }, openButton { "OPEN..." }, saveButton { "SAVE" },
                     saveAsButton { "SAVE AS..." }, mySkinsButton { "SAVE TO MY SKINS" }, newButton { "NEW" };
    juce::TextEditor nameEditor, authorEditor;
    PartsPanel parts;
    PreviewView preview;
    juce::TextButton normalButton { "NORMAL" }, hoverButton { "HOVER" };
    Inspector inspector;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> toolbarArea, leftArea, centreArea, rightArea, footerArea;
    juce::String status;
    double statusTime = 0.0;
    int draftRevision = -1, lastChangeRevision = -1;
    double lastChangeTime = 0.0;
};
} // namespace emx::sd
