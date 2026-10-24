// Emulon X — drawing a custom skin (.emxd): fills, corner shapes, overlay textures, knobs, faders.
// Used by the plugin and by Emulon X SD, so the designer's preview looks exactly like the plugin.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "SkinFile.h"

namespace emx::skin
{
/** A rounded rectangle whose corners can be cut straight (shape 0), round (0.5) or squircle (1). */
juce::Path shapePath (juce::Rectangle<float>, float radius, float shape);

void fillPath (juce::Graphics&, const juce::Path&, juce::Rectangle<float> bounds, const Fill&);
/** `base` is the surface's colour underneath: like the "overlay" blend, a dark surface brightens less
    and a light one darkens less. */
void drawOverlay (juce::Graphics&, const juce::Path& clip, juce::Rectangle<float> bounds, const Overlay&, juce::Colour base);
/** A surface: shadow, fill, overlay texture, outline. */
void drawBox (juce::Graphics&, juce::Rectangle<float>, const Box&);

void drawBackground (juce::Graphics&, const Design&, juce::Rectangle<int> bounds, float headerBottom);
void drawFrame (juce::Graphics&, const Design&, juce::Rectangle<int> bounds);
void drawPanel (juce::Graphics&, const Design&, juce::Rectangle<float>);
void drawInset (juce::Graphics&, const Design&, juce::Rectangle<float>);
/** A value display with its text (glowing if the skin says so). */
void drawDisplay (juce::Graphics&, const Design&, juce::Rectangle<float>, const juce::String& text,
                  const juce::Font&, juce::Justification, juce::Colour textColour);

void drawButton (juce::Graphics&, const Design&, juce::Rectangle<float>, bool on, bool tab,
                 bool highlighted, bool down, bool enabled);
juce::Colour buttonTextColour (const Design&, bool on, bool tab, bool enabled);

void drawKnob (juce::Graphics&, const Design&, juce::Rectangle<float>, float startAngle, float endAngle, float pos);
/** `pos` is the handle's centre (y), like JUCE's slider position. */
void drawFader (juce::Graphics&, const Design&, juce::Rectangle<float> track, float pos);
} // namespace emx::skin
