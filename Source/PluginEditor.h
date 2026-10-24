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
    float appliedHue = 0.0f;
    void updateTooltips();

    /** Everything visible lives in here, at base size; the editor scales it. */
    class Root final : public juce::Component
    {
    public:
        explicit Root (EmulonXEditor&);
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void showPage (int index);

        EmulonXEditor& owner;
        juce::TextButton tabMain { "MAIN" }, tabEditor { "EDITOR" }, tabFx { "FX" }, tabArp { "ARP" };
        LedDisplay voicesLed, cpuLed;
        MasterBar masterBar;
        GearButton gear;
        Explorer explorer;
        MainPage mainPage;
        EditorPage editorPage;
        FxPage fxPage;
        ArpPage arpPage;
        std::unique_ptr<SettingsPanel> settingsPanel;
        juce::Rectangle<float> titleArea;
    };

    EmulonXProcessor& proc;
    juce::SharedResourcePointer<Settings> settings;
    EmxLookAndFeel lnf;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<Root> root;
    float uiScale = 1.0f;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmulonXEditor)
};
} // namespace emx
