// Emulon X — the plugin window.
// The whole UI is laid out once at 1280 x 800 and scaled as one piece
// (75 / 100 / 125 / 150 / 200 %), so everything stays in proportion.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Settings.h"
#include "ui/EmxLookAndFeel.h"
#include "ui/MainPage.h"
#include "ui/Explorer.h"
#include "ui/SettingsPanel.h"
#include "ui/EditorPage.h"
#include "ui/FxPage.h"
#include "ui/ArpPage.h"
#include "ui/MasterBar.h"
#include "ui/PlayKeys.h"
#include "ui/MachinePage.h"
#include "ui/ArrangerPage.h"
#include "ui/Theme.h"

namespace emx
{
class EmulonXEditor final : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    static constexpr int baseWidth = 1280, baseHeight = 800;

    explicit EmulonXEditor (EmulonXProcessor&);
    ~EmulonXEditor() override;

    void resized() override;
    void setUiScale (float scale);
    void showPage (int index) { root->showPage (index); }
    void showSettings (bool show, int page = 0);
    Explorer& getExplorer() noexcept { return root->explorer; }
    FxPage& getFxPage() noexcept { return root->fxPage; }
    EditorPage& getEditorPage() noexcept { return root->editorPage; }

    //== Drop audio files / presets anywhere on the window =====================
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragging = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;
    void paintOverChildren (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;   // Ctrl+Z / Ctrl+Shift+Z

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // settings
    bool applyHue();   // true if the colours changed
    void rebuildForPart();   // the edited Arranger part changed: the whole UI now shows that part
    int appliedGeneration = -1;
    void updateTooltips();

    /** Everything visible lives in here, at base size; the editor scales it. */
    class Root final : public juce::Component
    {
    public:
        Root (EmulonXEditor&, EmulonXProcessor& part);
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void showPage (int index);
        void showSaveMenu();
        void showPartMenu();

        // V3 tabs: MAIN, EDITOR, PROCESSOR, ARPEGGIATOR, MACHINE, ARRANGER
        static constexpr int numPages = 6;
        EmulonXEditor& owner;
        EmulonXProcessor& proc;   // the part this window edits (part 1 = the plugin itself)
        juce::TextButton tabs[numPages] { juce::TextButton ("MAIN"), juce::TextButton ("EDITOR"), juce::TextButton ("PROCESSOR"),
                                          juce::TextButton ("ARPEGGIATOR"), juce::TextButton ("MACHINE"), juce::TextButton ("ARRANGER") };
        juce::TextButton saveButton { "SAVE" }, partButton;
        LedDisplay statusLed, voicesLed, cpuLed;
        MasterBar masterBar;
        GearButton gear;
        Explorer explorer;
        MainPage mainPage;
        EditorPage editorPage;
        FxPage fxPage;
        ArpPage arpPage;
        MachinePage machinePage;
        ArrangerPage arrangerPage;
        PlayKeys keys;
        std::unique_ptr<SettingsPanel> settingsPanel;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::Rectangle<float> titleArea;
        int page = 0;
        juce::Component* pages[numPages] {};
    };

    EmulonXProcessor& proc;
    juce::SharedResourcePointer<Settings> settings;
    EmxLookAndFeel lnf;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<Root> root, retiredRoot;
    float uiScale = 1.0f;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmulonXEditor)
};
} // namespace emx
