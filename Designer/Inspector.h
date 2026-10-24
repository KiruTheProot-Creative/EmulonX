// Emulon X SD — the controls that edit the skin: colours (with palette links), sliders, fills,
// pictures, overlay textures, fonts. The Inspector shows the ones for the selected part.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "SkinDocument.h"

namespace emx::sd
{
/** One line in the inspector. */
class Row : public juce::Component
{
public:
    virtual void refresh() {}
    virtual int heightFor (int width) const { juce::ignoreUnused (width); return 28; }
};

/** A colour well: shows a colour, opens a colour picker when clicked. */
class ColourSwatch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<juce::Colour()> read;
    std::function<void (juce::Colour)> write;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
};

class Inspector final : public juce::Component, private juce::ChangeListener
{
public:
    explicit Inspector (SkinDocument&);
    ~Inspector() override;

    void show (const juce::String& partKey);
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Rows for a part (used by the inspector; public so the palette can reuse them). */
    /** page 0 = the part's look, 1 = its effects. */
    static std::vector<std::unique_ptr<Row>> rowsFor (const juce::String& partKey, int page, SkinDocument&, std::function<void()> rebuild);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();
    void layoutRows();

    SkinDocument& doc;
    juce::String part { "panel" };
    int page = 0;
    juce::TextButton styleTab { "STYLE" }, fxTab { "FX" };
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    int seenStructure = -1;
};
} // namespace emx::sd
