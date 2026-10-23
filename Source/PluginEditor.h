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

namespace emx
{
class EmulonXEditor final : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    static constexpr int baseWidth = 1280, baseHeight = 800;

    explicit EmulonXEditor (EmulonXProcessor&);
    ~EmulonXEditor() override;

    void resized() override;
    void setUiScale (float scale);
    void showPage (int index) { root->showPage (index); }

    //== Drop audio files / presets anywhere on the window =====================
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragging = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;
    void paintOverChildren (juce::Graphics&) override;

private:
    void timerCallback() override;
    void showSettingsMenu();

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
        juce::TextButton tabMain { "MAIN" }, tabEditor { "EDITOR" }, tabFx { "FX" };
        LedDisplay voicesLed, cpuLed;
        GearButton gear;
        ComingSoonPanel explorer;
        MainPage mainPage;
        ComingSoonPanel editorPage, fxPage;
        juce::Rectangle<float> titleArea;
    };

    EmulonXProcessor& proc;
    juce::SharedResourcePointer<Settings> settings;
    EmxLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Root> root;
    float uiScale = 1.0f;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmulonXEditor)
};
} // namespace emx
