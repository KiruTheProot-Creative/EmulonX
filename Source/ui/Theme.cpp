// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Theme.h"
#include "EmxAssets.h"

namespace emx::theme
{
static juce::Typeface::Ptr typefaceFor (int weight)
{
    static auto w500 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Orbitron500_ttf, (size_t) EmxAssets::Orbitron500_ttfSize);
    static auto w700 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Orbitron700_ttf, (size_t) EmxAssets::Orbitron700_ttfSize);
    static auto w900 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Orbitron900_ttf, (size_t) EmxAssets::Orbitron900_ttfSize);
    return weight >= 900 ? w900 : weight >= 700 ? w700 : w500;
}

juce::Font font (float height, int weight, float kerning)
{
    juce::Font f { juce::FontOptions (typefaceFor (weight)).withHeight (height) };
    return std::abs (kerning) > 1.0e-6f ? f.withExtraKerningFactor (kerning) : f;
}

juce::Image image (Img which)
{
    switch (which)
    {
        case Img::background: return juce::ImageCache::getFromMemory (EmxAssets::background_jpg, EmxAssets::background_jpgSize);
        case Img::frame:      return juce::ImageCache::getFromMemory (EmxAssets::frame_png, EmxAssets::frame_pngSize);
        case Img::knob:       return juce::ImageCache::getFromMemory (EmxAssets::knob_png, EmxAssets::knob_pngSize);
        case Img::slider:     return juce::ImageCache::getFromMemory (EmxAssets::slider_png, EmxAssets::slider_pngSize);
    }
    return {};
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    juce::Path p;
    p.addRoundedRectangle (r, radius);
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 10, { 0, 3 }).drawForPath (g, p);

    juce::ColourGradient grad (juce::Colour (0xff26232a), 0.0f, r.getY(), juce::Colour (0xff0d0c10), 0.0f, r.getBottom(), false);
    grad.addColour (0.4, juce::Colour (0xff141217));
    g.setGradientFill (grad);
    g.fillPath (p);

    g.setColour (highlight);
    g.fillRect (r.getX() + radius, r.getY() + 1.0f, r.getWidth() - 2.0f * radius, 1.0f);
    g.setColour (juce::Colours::black);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void drawInset (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff1c1a1f), r.getY(), juce::Colour (0xff111014), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::black.withAlpha (0.6f), r.getY(), juce::Colours::transparentBlack, r.getY() + 6.0f));
    g.fillRoundedRectangle (r, radius);
}

void drawLed (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float textHeight,
              juce::Justification just, juce::Colour textColour)
{
    g.setColour (edge);
    g.fillRoundedRectangle (r.translated (0, 1), 6.0f);
    g.setColour (ledBg);
    g.fillRoundedRectangle (r, 6.0f);

    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, 6.0f);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
            g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::black.withAlpha (0.8f), r.getY(), juce::Colours::transparentBlack, r.getY() + 5.0f));
        g.fillRect (r);
    }
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    if (text.isNotEmpty())
        drawGlowText (g, text, r.reduced (6.0f, 0.0f), font (textHeight, 500, 0.06f), textColour,
                      textColour.withAlpha (0.55f), 6, just);
}

void drawGlowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                   juce::Colour colour, juce::Colour glow, int glowRadius, juce::Justification just)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (f, text, r.getX(), r.getY(), r.getWidth(), r.getHeight(), just, 1, 0.7f);
    juce::Path p;
    ga.createPath (p);
    if (glowRadius > 0 && ! glow.isTransparent())
        juce::DropShadow (glow, glowRadius, {}).drawForPath (g, p);
    g.setColour (colour);
    g.fillPath (p);
}

juce::Rectangle<float> drawHeading (juce::Graphics& g, juce::Rectangle<float> inner,
                                    const juce::String& title, const juce::String& subtitle)
{
    auto row = inner.removeFromTop (16.0f);
    const auto f = font (10.0f, 700, 0.14f);
    g.setColour (textCol);
    g.setFont (f);
    const auto t = title.toUpperCase();
    g.drawText (t, row, juce::Justification::centredLeft, false);

    if (subtitle.isNotEmpty())
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, t, 0, 0);
        const auto w = ga.getBoundingBox (0, -1, true).getWidth();
        g.setColour (textMuted);
        g.setFont (font (8.0f, 500, 0.1f));
        g.drawText (subtitle.toUpperCase(), row.withTrimmedLeft (w + 8.0f), juce::Justification::centredLeft, true);
    }
    inner.removeFromTop (6.0f);
    return inner;
}

void drawNineSlice (juce::Graphics& g, const juce::Image& img, juce::Rectangle<int> d, int s, int b)
{
    const int W = img.getWidth(), H = img.getHeight();
    const int dw = d.getWidth(), dh = d.getHeight(), x = d.getX(), y = d.getY();
    auto blit = [&] (int dx, int dy, int dW, int dH, int sx, int sy, int sW, int sH)
    {
        if (dW > 0 && dH > 0)
            g.drawImage (img, dx, dy, dW, dH, sx, sy, sW, sH);
    };
    blit (x, y, b, b, 0, 0, s, s);
    blit (x + b, y, dw - 2 * b, b, s, 0, W - 2 * s, s);
    blit (x + dw - b, y, b, b, W - s, 0, s, s);
    blit (x, y + b, b, dh - 2 * b, 0, s, s, H - 2 * s);
    blit (x + dw - b, y + b, b, dh - 2 * b, W - s, s, s, H - 2 * s);
    blit (x, y + dh - b, b, b, 0, H - s, s, s);
    blit (x + b, y + dh - b, dw - 2 * b, b, s, H - s, W - 2 * s, s);
    blit (x + dw - b, y + dh - b, b, b, W - s, H - s, s, s);
}
} // namespace emx::theme
