// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SkinDraw.h"

namespace emx::skin
{
using namespace juce;

Path shapePath (Rectangle<float> r, float radius, float shape)
{
    Path p;
    const float rad = jlimit (0.0f, jmin (r.getWidth(), r.getHeight()) * 0.5f, radius);
    if (rad < 0.25f)
    {
        p.addRectangle (r);
        return p;
    }
    if (std::abs (shape - 0.5f) < 0.01f)
    {
        p.addRoundedRectangle (r, rad);
        return p;
    }
    // each corner is a quarter of a superellipse |x|^n + |y|^n = 1: n = 1 cuts straight,
    // 2 is a circle, 4 a squircle (like CSS corner-shape: superellipse())
    const float n = std::pow (2.0f, jlimit (0.0f, 1.0f, shape) * 2.0f), e = 2.0f / n;
    constexpr float pi = MathConstants<float>::pi;
    const struct { float cx, cy, a0; } corners[] {
        { r.getX() + rad, r.getY() + rad, pi },              { r.getRight() - rad, r.getY() + rad, 1.5f * pi },
        { r.getRight() - rad, r.getBottom() - rad, 0.0f },   { r.getX() + rad, r.getBottom() - rad, 0.5f * pi } };
    constexpr int steps = 12;
    bool first = true;
    for (auto& c : corners)
        for (int i = 0; i <= steps; ++i)
        {
            const float a = c.a0 + 0.5f * pi * (float) i / (float) steps;
            const float ca = std::cos (a), sa = std::sin (a);
            const float x = c.cx + rad * (ca < 0 ? -1.0f : 1.0f) * std::pow (std::abs (ca), e);
            const float y = c.cy + rad * (sa < 0 ? -1.0f : 1.0f) * std::pow (std::abs (sa), e);
            if (first) p.startNewSubPath (x, y); else p.lineTo (x, y);
            first = false;
        }
    p.closeSubPath();
    return p;
}

void fillPath (Graphics& g, const Path& path, Rectangle<float> r, const Fill& f)
{
    switch (f.type)
    {
        case Fill::Type::solid:
            g.setColour (f.a);
            g.fillPath (path);
            return;
        case Fill::Type::gradient:
        {
            // CSS-style angle: 0 points up, 90 right, 180 down; the line spans the box's corners
            const float th = degreesToRadians (f.angle);
            const Point<float> dir (std::sin (th), -std::cos (th));
            const float len = std::abs (r.getWidth() * dir.x) + std::abs (r.getHeight() * dir.y);
            const auto c = r.getCentre();
            g.setGradientFill (ColourGradient (f.a, c - dir * (len * 0.5f), f.b, c + dir * (len * 0.5f), false));
            g.fillPath (path);
            return;
        }
        case Fill::Type::image:
        {
            g.setColour (f.a);   // behind see-through parts
            g.fillPath (path);
            if (! f.picture.isValid())
                return;
            Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (path);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            if (f.fit == Fill::Fit::tile)
            {
                g.setTiledImageFill (f.picture.image, (int) r.getX(), (int) r.getY(), 1.0f);
                g.fillPath (path);
            }
            else
                g.drawImage (f.picture.image, r, f.fit == Fill::Fit::cover ? RectanglePlacement (RectanglePlacement::centred | RectanglePlacement::fillDestination)
                                                                            : RectanglePlacement (RectanglePlacement::stretchToFit));
            return;
        }
    }
}

void drawOverlay (Graphics& g, const Path& clip, Rectangle<float> r, const Overlay& ov, Colour base)
{
    if (! ov.isActive())
        return;
    // "Overlay" with texture t on surface b: b < 0.5 gives 2bt, otherwise 1 - 2(1-b)(1-t). Drawing the
    // texture's light parts in white and its dark parts in black matches that exactly when the white is
    // scaled by b / (1-b) on dark surfaces and the black by (1-b) / b on light ones.
    const float b = jlimit (0.02f, 0.98f, 0.2126f * base.getFloatRed() + 0.7152f * base.getFloatGreen() + 0.0722f * base.getFloatBlue());
    const float k = ov.strength * (ov.softLight ? 0.5f : 1.0f);
    const float up = b < 0.5f ? b / (1.0f - b) : 1.0f, down = b > 0.5f ? (1.0f - b) / b : 1.0f;
    Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (clip);
    g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
    g.setOpacity (jlimit (0.0f, 1.0f, k * up));
    g.drawImage (ov.lighter, r, RectanglePlacement::stretchToFit);
    g.setOpacity (jlimit (0.0f, 1.0f, k * down));
    g.drawImage (ov.darker, r, RectanglePlacement::stretchToFit);
}

void drawBox (Graphics& g, Rectangle<float> r, const Box& b)
{
    const auto path = shapePath (r, b.radius, b.shape);
    if (b.shadowOpacity > 0.001f)
    {
        if (b.shadowBlur < 0.5f)   // a hard, cartoon-style shadow
        {
            g.setColour (Colours::black.withAlpha (b.shadowOpacity));
            g.fillPath (path, AffineTransform::translation (0.0f, b.shadowY));
        }
        else
            DropShadow (Colours::black.withAlpha (b.shadowOpacity), roundToInt (b.shadowBlur), { 0, roundToInt (b.shadowY) }).drawForPath (g, path);
    }
    fillPath (g, path, r, b.fill);
    drawOverlay (g, path, r, b.overlay, b.fill.average());
    if (b.outline > 0.01f)
    {
        g.setColour (b.outlineColour);
        g.strokePath (shapePath (r.reduced (b.outline * 0.5f), jmax (0.0f, b.radius - b.outline * 0.5f), b.shape), PathStrokeType (b.outline));
    }
}

static const Image& noiseTile()
{
    static const Image img = []
    {
        Image im (Image::ARGB, 128, 128, true);
        Random rnd (0x7a1e);
        Image::BitmapData d (im, Image::BitmapData::writeOnly);
        for (int y = 0; y < im.getHeight(); ++y)
            for (int x = 0; x < im.getWidth(); ++x)
            {
                const float v = rnd.nextFloat();
                if (v > 0.82f)
                    d.setPixelColour (x, y, Colours::white.withAlpha ((v - 0.82f) * 3.0f));
                else if (v < 0.18f)
                    d.setPixelColour (x, y, Colours::black.withAlpha ((0.18f - v) * 3.0f));
            }
        return im;
    }();
    return img;
}

void drawBackground (Graphics& g, const Design& d, Rectangle<int> bounds, float headerBottom)
{
    const auto r = bounds.toFloat();
    Path all;
    all.addRectangle (r);
    fillPath (g, all, r, d.window);
    if (d.pattern == Design::Pattern::grid && d.patternOpacity > 0.001f)
    {
        g.setColour (d.patternColour.withMultipliedAlpha (d.patternOpacity));
        for (float x = r.getX(); x < r.getRight(); x += 22.0f)
            g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        for (float y = r.getY(); y < r.getBottom(); y += 22.0f)
            g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    }
    else if (d.pattern == Design::Pattern::noise && d.patternOpacity > 0.001f)
    {
        g.setTiledImageFill (noiseTile(), 0, 0, jmin (1.0f, d.patternOpacity * 3.0f));
        g.fillRect (r);
    }
    drawOverlay (g, all, r, d.windowOverlay, d.window.average());

    const auto bar = r.withHeight (headerBottom + 8.0f - r.getY());
    Path top;
    top.addRectangle (bar);
    fillPath (g, top, bar, d.topBar);
    drawOverlay (g, top, bar, d.topBarOverlay, d.topBar.average());
    if (d.topLine > 0.01f)
    {
        g.setColour (d.topLineColour);
        g.fillRect (bar.getX(), bar.getBottom() - d.topLine, bar.getWidth(), d.topLine);
    }
}

void drawFrame (Graphics& g, const Design& d, Rectangle<int> bounds)
{
    if (d.frameWidth < 0.01f)
        return;
    g.setColour (d.frameColour);
    g.drawRect (bounds.toFloat(), d.frameWidth);
}

void drawPanel (Graphics& g, const Design& d, Rectangle<float> r)
{
    drawBox (g, r, d.panel);
    // a faint light edge along the top
    const float in = jmax (d.panel.radius, d.panel.outline) + 1.0f;
    if (r.getWidth() > 2.0f * in)
    {
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillRect (r.getX() + in, r.getY() + d.panel.outline + 0.5f, r.getWidth() - 2.0f * in, 1.0f);
    }
}

void drawInset (Graphics& g, const Design& d, Rectangle<float> r)
{
    drawBox (g, r, d.inset);
    if (d.insetDepth > 0.001f)
    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shapePath (r, d.inset.radius, d.inset.shape));
        g.setGradientFill (ColourGradient::vertical (Colours::black.withAlpha (d.insetDepth * 0.85f), r.getY(), Colours::transparentBlack, r.getY() + 8.0f));
        g.fillRect (r);
    }
}

void drawDisplay (Graphics& g, const Design& d, Rectangle<float> r, const String& text, const Font& font,
                  Justification just, Colour textColour)
{
    drawBox (g, r, d.display);
    if (d.scanlines || d.displayGlow > 0.001f)
    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shapePath (r, d.display.radius, d.display.shape));
        if (d.scanlines)
        {
            g.setColour (Colours::black.withAlpha (0.35f));
            for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
                g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        }
        if (d.displayGlow > 0.001f)   // a glowing screen sits a little deeper
        {
            g.setGradientFill (ColourGradient::vertical (Colours::black.withAlpha (0.7f), r.getY(), Colours::transparentBlack, r.getY() + 5.0f));
            g.fillRect (r);
        }
    }
    if (text.isEmpty())
        return;
    const auto area = r.reduced (5.0f + d.display.outline, 0.0f);
    if (d.displayGlow > 0.001f)
    {
        GlyphArrangement ga;
        ga.addFittedText (font, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(), just, 1, 0.7f);
        Path p;
        ga.createPath (p);
        DropShadow (textColour.withAlpha (0.55f * d.displayGlow + 0.1f), roundToInt (6.0f * d.displayGlow + 1.0f), {}).drawForPath (g, p);
        g.setColour (textColour);
        g.fillPath (p);
        return;
    }
    g.setColour (textColour);
    g.setFont (font);
    g.drawFittedText (text, area.toNearestInt(), just, 1, 0.7f);
}

void drawButton (Graphics& g, const Design& d, Rectangle<float> r, bool on, bool tab, bool highlighted, bool down, bool enabled)
{
    const auto& b = tab ? (on ? d.tabOn : d.tab) : (on ? d.buttonOn : d.button);
    if (down)
        r = r.translated (0.0f, 1.0f);
    const auto path = shapePath (r, b.radius, b.shape);
    const float glow = (tab && ! d.tabsLikeButtons) ? 0.0f : d.buttonOnGlow;
    if (on && enabled && glow > 0.001f)
        DropShadow (b.fill.average().withAlpha (0.9f * glow), roundToInt (12.0f * glow), {}).drawForPath (g, path);
    drawBox (g, r, b);
    if (! on && d.bevel > 0.001f)
    {
        // shine on the upper half
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (path);
        g.setGradientFill (ColourGradient::vertical (Colours::white.withAlpha (0.25f * d.bevel + (highlighted && enabled ? 0.08f : 0.0f)), r.getY(),
                                                     Colours::white.withAlpha (0.0f), r.getY() + r.getHeight() * 0.45f));
        g.fillRect (r);
    }
    if (highlighted && enabled && ! on && ! down)
    {
        g.setColour (d.accent.withAlpha (0.5f));
        g.strokePath (shapePath (r.reduced (b.outline + 1.0f), jmax (0.0f, b.radius - b.outline - 1.0f), b.shape), PathStrokeType (1.5f));
    }
    if (! enabled)
    {
        g.setColour ((d.isLight() ? Colours::white : Colours::black).withAlpha (0.4f));
        g.fillPath (path);
    }
}

Colour buttonTextColour (const Design& d, bool on, bool tab, bool enabled)
{
    auto c = tab && ! d.tabsLikeButtons ? d.tabText : on ? d.buttonOnText : d.buttonText;
    return enabled ? c : c.withMultipliedAlpha (0.45f);
}

void drawKnob (Graphics& g, const Design& d, Rectangle<float> r, float startAngle, float endAngle, float pos)
{
    const float size = jmin (r.getWidth(), r.getHeight()), rad = size * 0.5f, scale = size / 44.0f;
    const auto c = r.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    Path circle;

    if (d.knobStyle == Design::KnobStyle::picture && d.knobPicture.isValid())
    {
        {
            Graphics::ScopedSaveState state (g);
            g.addTransform (AffineTransform::rotation (angle, c.x, c.y));
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (d.knobPicture.image, r, RectanglePlacement::centred);
        }
        circle.addEllipse (r.withSizeKeepingCentre (size, size));
        drawOverlay (g, circle, r.withSizeKeepingCentre (size, size), d.knobOverlay, Colours::grey);
        return;
    }
    float capR = rad - 2.0f * scale;
    if (d.ring > 0.01f)
    {
        const float w = d.ring * scale, ar = rad - w * 0.5f - scale;
        Path track, value;
        track.addCentredArc (c.x, c.y, ar, ar, 0.0f, startAngle, endAngle, true);
        g.setColour (d.ringTrack);
        g.strokePath (track, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
        if (pos > 0.002f)
        {
            value.addCentredArc (c.x, c.y, ar, ar, 0.0f, startAngle, angle, true);
            g.setColour (d.ringValue);
            g.strokePath (value, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
        }
        capR = rad - w - 5.0f * scale;
    }
    capR = jmax (2.0f, capR);
    const auto cap = Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (cap.translated (0.0f, 1.5f * scale));
    const Point<float> hot (c.x - 0.3f * capR, c.y - 0.4f * capR);
    g.setGradientFill (ColourGradient (d.capLight, hot, d.capDark, hot + Point<float> (1.6f * capR, 0.0f), true));
    g.fillEllipse (cap);
    circle.addEllipse (cap);
    drawOverlay (g, circle, cap, d.knobOverlay, d.capLight.interpolatedWith (d.capDark, 0.5f));
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);

    Path pointer;
    pointer.addLineSegment (Line<float> (c.x, c.y - capR * 0.12f, c.x, c.y - capR * 0.82f), jmax (2.0f, size * 0.06f));
    pointer.applyTransform (AffineTransform::rotation (angle, c.x, c.y));
    if (d.pointerGlow > 0.001f)
        DropShadow (d.pointer.withAlpha (0.9f * d.pointerGlow), roundToInt (size * 0.12f * d.pointerGlow) + 1, {}).drawForPath (g, pointer);
    g.setColour (d.pointer);
    g.fillPath (pointer);
}

void drawFader (Graphics& g, const Design& d, Rectangle<float> track, float pos)
{
    const float cx = track.getCentreX();
    const auto groove = Rectangle<float> (cx - 3.0f, track.getY() + 6.0f, 6.0f, jmax (2.0f, track.getHeight() - 12.0f));
    g.setColour (d.faderTrack);
    g.fillRoundedRectangle (groove, 3.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (groove, 3.0f, 1.0f);
    const float top = jlimit (groove.getY(), groove.getBottom(), pos);
    if (groove.getBottom() - top > 0.5f)
    {
        g.setColour (d.faderFill);
        g.fillRoundedRectangle (Rectangle<float> (cx - 2.0f, top, 4.0f, groove.getBottom() - top), 2.0f);
    }
    const auto thumb = Rectangle<float> (cx - 13.0f, pos - 7.0f, 26.0f, 14.0f);
    Path p;
    p.addRoundedRectangle (thumb, 3.0f);
    if (d.thumbPicture.isValid())
    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (p);
        g.drawImage (d.thumbPicture.image, thumb, RectanglePlacement::stretchToFit);
    }
    else
    {
        g.setGradientFill (ColourGradient::vertical (d.thumbTop, thumb.getY(), d.thumbBottom, thumb.getBottom()));
        g.fillPath (p);
    }
    drawOverlay (g, p, thumb, d.faderOverlay, d.thumbPicture.isValid() ? Colours::grey : d.thumbTop.interpolatedWith (d.thumbBottom, 0.5f));
    g.setColour (Colours::black.withAlpha (0.85f));
    g.strokePath (p, PathStrokeType (1.0f));
}
} // namespace emx::skin
