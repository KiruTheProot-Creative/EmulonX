// Emulon X — Settings > Skin designer: build a custom skin and export it as a .emxd file.
// Every change shows live (the design is used as the current skin while you work on it).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "SkinDesign.h"
#include "../Settings.h"

namespace emx
{
class SkinDesigner final : public juce::Component,
                           private juce::Timer
{
public:
    explicit SkinDesigner (Settings&);
    ~SkinDesigner() override;

    /** The working copy lives here, so a design survives closing the plugin. */
    static constexpr const char* workFileName = ".designer.emxd";

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PartTile;
    class ColourTile;

    void changed();                      // something in the design changed: show it, save soon
    void timerCallback() override;       // saves the working copy and uses it as the skin
    void refreshControls();
    void chooseImage (int part);
    void chooseFont();
    void loadFolder();
    void saveTemplate();
    void exportSkin();
    void setStatus (const juce::String&);

    Settings& settings;
    SkinDesign design;
    LedLabel nameField { 10.0f }, authorField { 10.0f };
    juce::TextButton hueButton, knobButton, shadowButton, cornersButton, fontButton { "CHOOSE FONT..." }, fontClear { "X" };
    juce::TextButton startOver { "START OVER" }, loadFolderButton { "LOAD FOLDER..." }, templateButton { "SAVE TEMPLATE..." },
                     exportButton { "EXPORT .EMXD..." };
    juce::OwnedArray<PartTile> tiles;
    juce::OwnedArray<ColourTile> colourTiles;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status, fontName;
    juce::Rectangle<int> rowName, rowFont, colourRow, statusRow, fontRect;
};
} // namespace emx
