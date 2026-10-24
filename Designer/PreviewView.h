// Emulon X SD — the live preview: a small Emulon X drawn with the skin exactly as the plugin draws it.
// Hover to find a part, click to edit it.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "SkinDocument.h"

namespace emx::sd
{
/** The parts a skin is made of (keys as in skin.json). */
struct Part { const char* key; const char* title; const char* group; };
const std::vector<Part>& allParts();
juce::String partTitle (const juce::String& key);

/** Fonts as the skin names them (Saira, Fredoka, Tahoma, an installed font or a font file). */
juce::Font skinFont (const skin::Design&, float height, int weight = 700, float kerning = 0.0f);
void releaseFonts();   // before the app shuts down

class PreviewView final : public juce::Component, public juce::SettableTooltipClient, private juce::ChangeListener
{
public:
    static constexpr int logicalWidth = 880, logicalHeight = 540;

    explicit PreviewView (SkinDocument&);
    ~PreviewView() override;

    std::function<void (const juce::String& partKey)> onSelect;
    void setSelected (const juce::String& key) { selected = key; repaint(); }
    void setHoverState (bool b) { hoverState = b; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Draws the preview at its own size (880 x 540) into g. */
    void drawPreview (juce::Graphics&, const skin::Design&);

private:
    struct Hit { juce::Rectangle<float> r; juce::String key; };
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    juce::AffineTransform toScreen() const;
    juce::String partAt (juce::Point<float> screenPos) const;
    void outline (juce::Graphics&, const juce::String& key, bool solid);

    SkinDocument& doc;
    std::vector<Hit> hits;
    juce::String selected { "panel" }, hovered;
    bool hoverState = false;
    struct LogoCache { juce::Colour letters, accent; int glowKey = -1; juce::Image img; } logoCache;
};
} // namespace emx::sd
