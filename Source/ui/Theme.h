// Emulon X — colours, fonts, artwork and the shared drawing helpers that give
// the UI its look (metal panels, red LED displays, glowing text).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace emx::skin { struct Design; }

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
// desktops; EMURUM and EMUTAL are flat, modern synth styles; custom is a skin file (.emxd) made
// in Emulon X SD. Every helper below draws in the current skin.
enum class Skin { classic, xpLight, xpDark, emurum, emutal, emublox, emulator, custom };
void setSkin (Skin);   // message thread only
Skin getSkin() noexcept;
/** The skin file used by Skin::custom (call before setSkin (Skin::custom)). Message thread only. */
void setCustomSkin (std::shared_ptr<const skin::Design>);
/** The custom skin as it is drawn (turned by the colour shift), or nullptr. */
const skin::Design* customSkin() noexcept;
/** Changes every time a different custom skin is set. */
int customSkinGeneration() noexcept;
bool customSkinAllowsColourShift() noexcept;
bool customSkinIsLight() noexcept;
/** A short note about the last skin file that was loaded (why it couldn't be used, or what's missing). */
void setCustomSkinMessage (const juce::String&);
juce::String customSkinMessage();

inline bool isCustom() noexcept { return getSkin() == Skin::custom; }
inline bool isXp() noexcept   { const auto s = getSkin(); return s == Skin::xpLight || s == Skin::xpDark; }
/** Flat skins share one way of translating the red design's colours (custom skins too). */
inline bool isFlat() noexcept { const auto s = getSkin(); return s == Skin::emurum || s == Skin::emutal || s == Skin::emublox || s == Skin::emulator || s == Skin::custom; }
/** EMUL8R: brushed metal, teal LCD screens (an homage to the early-2000s hardware-style samplers). */
inline bool isMetal() noexcept { return getSkin() == Skin::emulator; }
inline bool skinAllowsColourShift (Skin s) noexcept
{
    return s == Skin::classic || s == Skin::emurum || s == Skin::emutal || s == Skin::emublox || (s == Skin::custom && customSkinAllowsColourShift());
}
/** Light backgrounds with dark text (XP light, EMUBLOX, EMUL8R, light custom skins). */
inline bool isLightSkin() noexcept { const auto s = getSkin(); return s == Skin::xpLight || s == Skin::emublox || s == Skin::emulator || (s == Skin::custom && customSkinIsLight()); }

/** The flat skins' surfaces (valid while isFlat()). */
struct FlatStyle
{
    juce::Colour bg, topBar, panel, panelEdge, inset, display, displayEdge, text, muted, accent, list,
                 button, buttonHover, buttonEdge, knobBody, knobTrack;
    float radius;
    float neutralHue, neutralChroma, neutralLift;   // how the red design's greys are translated
    // chunky toy style (EMUBLOX)
    bool light = false;                 // light surfaces, dark text
    float outline = 0.0f;               // thick outlines around panels and controls
    float shadow = 0.0f;                // offset cartoon shadow below panels
    juce::Colour outlineColour {}, onFill {}, lip {};
    float accentLift = 0.0f;            // brighter accents (for EMUL8R's teal screens)
};

/** A graph area (waveform, envelope, step bars): an LCD screen in EMUL8R, a display elsewhere. */
void drawScreen (juce::Graphics&, juce::Rectangle<float>);
/** Readable text colour on the top bar. */
juce::Colour headerText() noexcept;
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
