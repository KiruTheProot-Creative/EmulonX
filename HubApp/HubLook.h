// Emulon X Hub — the look: Classic red / black metal, as in the plugin. All sizes are in the Hub's own
// 1280 x 720 pixels (the window scales everything).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/Theme.h"

namespace emx::hub::look
{
using juce::Colour;
inline const Colour red { 0xffff2a2a }, redHot { 0xffff5a4a }, redDark { 0xff7a1414 };
inline const Colour text { 0xffece6e6 }, muted { 0xffb9a7a7 }, list { 0xffff5050 }, body { 0xffddd3d3 };
inline const Colour ledBg { 0xff070203 }, ok { 0xff4ed38a }, warm { 0xffffc9a0 };
inline constexpr float em = 13.0f;   // the prototype's text size

/** Saira at a CSS-like size in px (the size of the letters, not the line). */
inline juce::Font font (float px, int weight = 500, float kerning = 0.0f)
{
    return emx::theme::font (px * 1.25f, weight, kerning);
}

inline void metal (juce::Graphics& g, juce::Rectangle<float> r, float radius = 9.0f)
{
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 10, { 0, 3 }).drawForPath (g, [&] { juce::Path p; p.addRoundedRectangle (r, radius); return p; }());
    juce::ColourGradient cg (Colour (38, 35, 42).withAlpha (0.94f), 0, r.getY(), Colour (10, 9, 12).withAlpha (0.97f), 0, r.getBottom(), false);
    cg.addColour (0.45, Colour (16, 14, 18).withAlpha (0.95f));
    g.setGradientFill (cg);
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

inline void led (juce::Graphics& g, juce::Rectangle<float> r, float radius = 6.0f, bool scanlines = true)
{
    g.setColour (ledBg);
    g.fillRoundedRectangle (r, radius);
    if (scanlines)
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, radius);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
            g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        // inner shadow
        juce::ColourGradient top (juce::Colours::black.withAlpha (0.85f), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 7.0f, false);
        g.setGradientFill (top);
        g.fillRect (r.withHeight (7.0f));
    }
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

enum class Style { grey, hot, quiet, dark };

inline void buttonFace (juce::Graphics& g, juce::Rectangle<float> r, Style s, bool hover, bool down, float radius = 5.0f, bool glow = true)
{
    Colour a, b, line;
    switch (s)
    {
        case Style::hot:   a = Colour (0xffd42c2c); b = Colour (0xff600909); line = Colour (0xffff9090); break;
        case Style::dark:  a = Colour (0xff141015); b = Colour (0xff141015); line = juce::Colours::white.withAlpha (0.12f); break;
        case Style::grey:
        case Style::quiet: a = Colour (0xff4a464f); b = Colour (0xff1a181d); line = Colour (0xff77717c); break;
    }
    if (s == Style::hot && glow)
    {
        juce::Path p;
        p.addRoundedRectangle (r, radius);
        juce::DropShadow (red.withAlpha (0.45f), 12, {}).drawForPath (g, p);
    }
    if (hover) { a = a.brighter (0.12f); b = b.brighter (0.12f); }
    if (down)  { std::swap (a, b); }
    g.setGradientFill (juce::ColourGradient::vertical (a, r.getY(), b, r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setColour (line);
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius * 0.6f, r.getRight() - radius * 0.6f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

inline void dot (juce::Graphics& g, juce::Point<float> c, Colour col, float d = 8.0f)
{
    if (col != Colour (0xff555555))
    {
        g.setColour (col.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (d * 2.2f, d * 2.2f).withCentre (c));
    }
    g.setColour (col);
    g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c));
}

/** **bold** in a line of text, as an AttributedString. */
inline juce::AttributedString rich (const juce::String& t, const juce::Font& f, const juce::Font& bold, Colour c, Colour boldColour)
{
    juce::AttributedString s;
    auto rest = t;
    bool isBold = false;
    while (rest.isNotEmpty())
    {
        const int i = rest.indexOf ("**");
        const auto part = i < 0 ? rest : rest.substring (0, i);
        if (part.isNotEmpty())
            s.append (part, isBold ? bold : f, isBold ? boldColour : c);
        if (i < 0)
            break;
        rest = rest.substring (i + 2);
        isBold = ! isBold;
    }
    return s;
}
} // namespace emx::hub::look
