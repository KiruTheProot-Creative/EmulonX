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

/** Rotates the colour scheme by this many degrees (0 = the original red). Message thread only. */
void setHueShift (float degrees);
float getHueShift() noexcept;
/** A colour from the original red design, turned to the current hue (alpha is kept). */
juce::Colour hue (juce::uint32 argbInRedDesign);
/** Same rotation for any colour; lightness is kept so contrast stays the same. */
juce::Colour rotateHue (juce::Colour, float degrees);

//== Fonts: Orbitron (SIL Open Font License), weights 500 / 700 / 900 ===========
juce::Font font (float height, int weight = 700, float kerning = 0.0f);

//== Artwork (your PNGs, embedded at 2x so they stay sharp up to 200%) ==========
enum class Img { background, frame, knob, slider };
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
