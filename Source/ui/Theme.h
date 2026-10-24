// Emulon X — colours, fonts, artwork and the shared drawing helpers that give
// the UI its look (metal panels, red LED displays, glowing text).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace emx::theme
{
//== Colours ======================================================================
// These follow the hue setting (Settings > Customization), so they are variables:
// read them when painting, don't copy them into members.
inline juce::Colour red        { 0xffff2a2a };
inline juce::Colour redDim     { 0xffe83c3c };
inline juce::Colour redDark    { 0xff7a2828 };   // fills and lines only - too dark for text
inline juce::Colour textCol    { 0xffe8e4e4 };
inline juce::Colour textMuted  { 0xffb8a6a6 };   // secondary text: details, hints, empty-list notes
inline juce::Colour listText   { 0xffff4a4a };   // names in lists (presets, files, banks)
inline juce::Colour ledBg      { 0xff070203 };
inline juce::Colour edge       { 0xff3a363d };
inline juce::Colour highlight  { 0xff4a444f };

//== Skins =========================================================================
// EMULON CLASSIC is the red metal design; EMULON XP is a light or dark homage to early-2000s
// desktops; EMURUM and EMUTAL are flat, modern synth styles. Every helper below draws in the
// current skin. The colour shift works on Classic, EMURUM and EMUTAL.
enum class Skin { classic, xpLight, xpDark, emurum, emutal };
void setSkin (Skin);   // message thread only
Skin getSkin() noexcept;
inline bool isXp() noexcept   { const auto s = getSkin(); return s == Skin::xpLight || s == Skin::xpDark; }
inline bool isFlat() noexcept { const auto s = getSkin(); return s == Skin::emurum || s == Skin::emutal; }
inline bool skinAllowsColourShift (Skin s) noexcept { return s == Skin::classic || s == Skin::emurum || s == Skin::emutal; }

/** The flat skins' surfaces (valid while isFlat()). */
struct FlatStyle
{
    juce::Colour bg, topBar, panel, panelEdge, inset, display, displayEdge, text, muted, accent, list,
                 button, buttonHover, buttonEdge, knobBody, knobTrack;
    float radius;
    float neutralHue, neutralChroma, neutralLift;   // how the red design's greys are translated
};
const FlatStyle& flat() noexcept;
/** The colour the colour shift starts from: Classic red, or a flat skin's accent. */
inline juce::Colour baseAccent() noexcept { return isFlat() ? flat().accent : juce::Colour (0xffff2a2a); }

inline juce::Colour textStrong { 0xffffffff };   // titles and labels on panels (white in Classic)
inline juce::Colour ledText    { 0xffff2a2a };   // text in LED displays / text fields
inline juce::Colour windowBg   { 0xff000000 };   // behind everything

/** Black or white text, whichever reads better on this fill colour. */
inline juce::Colour textOn (juce::Colour fill) { return fill.getPerceivedBrightness() > 0.62f ? juce::Colour (0xff101010) : juce::Colours::white; }

/** Window background (artwork in Classic, flat with a title band in XP) and the frame on top. */
void drawBackground (juce::Graphics&, juce::Rectangle<int> bounds, float headerBottom);
void drawFrame (juce::Graphics&, juce::Rectangle<int> bounds);

/** Rotates the colour scheme by this many degrees (0 = the original red). Message thread only. */
void setHueShift (float degrees);
float getHueShift() noexcept;
/** A colour from the original red design, translated to the current skin and hue (alpha is kept). */
juce::Colour hue (juce::uint32 argbInRedDesign);
/** Same rotation for any colour; lightness is kept so contrast stays the same. */
juce::Colour rotateHue (juce::Colour, float degrees);

//== Fonts: Saira (SIL Open Font License), weights 500 / 700 / 900 ==============
juce::Font font (float height, int weight = 700, float kerning = 0.0f);

//== Artwork (your PNGs, embedded at 2x so they stay sharp up to 200%) ==========
enum class Img { background, frame, knob, slider, logo };

/** The Emulon X logo, fitted into the area (keeps its shape), with a soft glow. */
void drawLogo (juce::Graphics&, juce::Rectangle<float> area, juce::RectanglePlacement = juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
juce::Image image (Img which);

//== Drawing helpers =============================================================
/** Dark metal panel with a lighter top edge and drop shadow. */
void drawPanel (juce::Graphics&, juce::Rectangle<float> r, float radius = 9.0f);

/** The recessed area that holds knobs. */
void drawInset (juce::Graphics&, juce::Rectangle<float> r, float radius = 8.0f);

/** Red LED readout with scanlines. */
void drawLed (juce::Graphics&, juce::Rectangle<float> r, const juce::String& text,
              float textHeight = 10.0f, juce::Justification just = juce::Justification::centred,
              juce::Colour textColour = red);

/** Text with a soft coloured glow behind it. */
void drawGlowText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                   juce::Colour colour, juce::Colour glow, int glowRadius,
                   juce::Justification just = juce::Justification::centredLeft);

/** Panel heading: white title plus optional grey subtitle. Returns the area below it. */
juce::Rectangle<float> drawHeading (juce::Graphics&, juce::Rectangle<float> panelInner,
                                    const juce::String& title, const juce::String& subtitle = {});

/** Draws an image as a 9-slice (corners kept, edges stretched) — used for the frame. */
void drawNineSlice (juce::Graphics&, const juce::Image&, juce::Rectangle<int> dest, int sourceBorder, int destBorder);
} // namespace emx::theme
