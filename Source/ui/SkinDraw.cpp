// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SkinDraw.h"
#include <unordered_map>

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
            g.setOpacity (1.0f);
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

//== Effects: blur and bloom ===============================================
namespace
{
/** A picture as floats (premultiplied ARGB, 4 per pixel), for the effects. */
struct Pixels
{
    int w = 0, h = 0;
    std::vector<float> v;

    static Pixels from (const Image& img)
    {
        Pixels p;
        p.w = img.getWidth();
        p.h = img.getHeight();
        p.v.resize ((size_t) (p.w * p.h * 4));
        const Image::BitmapData d (img, Image::BitmapData::readOnly);
        for (int y = 0; y < p.h; ++y)
            for (int x = 0; x < p.w; ++x)
            {
                const auto* px = reinterpret_cast<const PixelARGB*> (d.getPixelPointer (x, y));
                auto* o = &p.v[(size_t) ((y * p.w + x) * 4)];
                o[0] = px->getAlpha(); o[1] = px->getRed(); o[2] = px->getGreen(); o[3] = px->getBlue();
            }
        return p;
    }

    void into (Image& img) const
    {
        Image::BitmapData d (img, Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto* o = &v[(size_t) ((y * w + x) * 4)];
                const auto a = (uint8) jlimit (0, 255, roundToInt (o[0]));
                auto clampTo = [a] (float c) { return (uint8) jlimit (0, (int) a, roundToInt (c)); };   // premultiplied: never above alpha
                reinterpret_cast<PixelARGB*> (d.getPixelPointer (x, y))->setARGB (a, clampTo (o[1]), clampTo (o[2]), clampTo (o[3]));
            }
    }

    /** Three box blurs in a row look like a Gaussian blur. */
    void blur (float radius)
    {
        const int r = jmax (1, roundToInt (radius * 0.6f));
        std::vector<float> line;
        auto pass = [&] (bool horizontal)
        {
            const int len = horizontal ? w : h, lines = horizontal ? h : w;
            line.resize ((size_t) len * 4);
            for (int l = 0; l < lines; ++l)
            {
                auto at = [&] (int i) -> float* { return &v[(size_t) (((horizontal ? l * w + i : i * w + l)) * 4)]; };
                for (int i = 0; i < len; ++i)
                    for (int c = 0; c < 4; ++c)
                        line[(size_t) (i * 4 + c)] = at (i)[c];
                float sum[4] {};
                for (int i = -r; i <= r; ++i)
                    for (int c = 0; c < 4; ++c)
                        sum[c] += (i >= 0 && i < len) ? line[(size_t) (i * 4 + c)] : 0.0f;
                const float norm = 1.0f / (float) (2 * r + 1);
                for (int i = 0; i < len; ++i)
                {
                    auto* o = at (i);
                    for (int c = 0; c < 4; ++c)
                        o[c] = sum[c] * norm;
                    const int out = i - r, in = i + r + 1;
                    for (int c = 0; c < 4; ++c)
                        sum[c] += (in < len ? line[(size_t) (in * 4 + c)] : 0.0f) - (out >= 0 ? line[(size_t) (out * 4 + c)] : 0.0f);
                }
            }
        };
        for (int i = 0; i < 3; ++i)
        {
            pass (true);
            pass (false);
        }
    }

    /** Keeps only what's brighter than the threshold (for bloom). */
    Pixels brightPart (float threshold) const
    {
        Pixels p = *this;
        for (size_t i = 0; i < p.v.size(); i += 4)
        {
            const float a = p.v[i];
            if (a < 1.0f)
            {
                p.v[i] = p.v[i + 1] = p.v[i + 2] = p.v[i + 3] = 0.0f;
                continue;
            }
            const float lum = (0.2126f * p.v[i + 1] + 0.7152f * p.v[i + 2] + 0.0722f * p.v[i + 3]) / a;
            const float k = jlimit (0.0f, 1.0f, (lum - threshold) / jmax (0.05f, 1.0f - threshold));
            for (int c = 0; c < 4; ++c)
                p.v[i + (size_t) c] *= k;
        }
        return p;
    }
};

struct EffectCache
{
    std::unordered_map<uint64, Image> images;
    int64 pixels = 0;
    CriticalSection lock;
    static EffectCache& get() { static EffectCache c; return c; }
};

uint64 mix (uint64 h, uint64 v) { return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 0xff51afd7ed558ccdull; }
} // namespace

uint64 effectKey (const Design& d, int part, Rectangle<float> area, uint64 extra)
{
    uint64 h = mix ((uint64) d.uid, (uint64) part);
    h = mix (h, (uint64) roundToInt (area.getWidth() * 4.0f));
    h = mix (h, (uint64) roundToInt (area.getHeight() * 4.0f));
    return mix (h, extra);
}

void drawWithEffects (Graphics& g, Rectangle<float> area, const Effects& fx, uint64 key,
                      const std::function<void (Graphics&)>& content, float margin)
{
    if (! fx.isActive())
    {
        content (g);
        return;
    }
    const float scale = jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const float m = margin + fx.blur * 2.0f + (fx.bloom > 0.001f ? fx.bloomSize * 2.0f : 0.0f) + 2.0f;
    const auto outer = area.expanded (m);
    const int w = roundToInt (outer.getWidth() * scale), h = roundToInt (outer.getHeight() * scale);
    if (w <= 0 || h <= 0 || (int64) w * h > 6'000'000)
    {
        content (g);
        return;
    }
    // what's drawn depends on the effects, the scale and where the content sits between pixels
    uint64 k = mix (key, (uint64) roundToInt (scale * 100.0f));
    for (float f : { fx.blur, fx.bloom, fx.bloomSize, fx.bloomThreshold, margin })
        k = mix (k, (uint64) roundToInt (f * 1000.0f));
    auto& cache = EffectCache::get();
    Image img;
    {
        const ScopedLock sl (cache.lock);
        if (auto it = cache.images.find (k); it != cache.images.end())
            img = it->second;
    }
    if (! img.isValid())
    {
        img = Image (Image::ARGB, w, h, true);
        {
            Graphics ig (img);
            ig.addTransform (AffineTransform::translation (-outer.getX(), -outer.getY()).scaled (scale));
            content (ig);
        }
        auto px = Pixels::from (img);
        if (fx.blur > 0.05f)
            px.blur (fx.blur * scale);
        if (fx.bloom > 0.001f)
        {
            auto glow = px.brightPart (fx.bloomThreshold);
            glow.blur (fx.bloomSize * scale);
            // screen-like: brightens what's there and glows around it
            for (size_t i = 0; i < px.v.size(); i += 4)
            {
                const float s = fx.bloom * 1.6f;
                for (int c = 0; c < 4; ++c)
                {
                    const float a = px.v[i + (size_t) c], b = glow.v[i + (size_t) c] * s;
                    px.v[i + (size_t) c] = a + b - a * b / 255.0f;
                }
            }
        }
        px.into (img);
        const ScopedLock sl (cache.lock);
        if (cache.pixels > 48'000'000 || cache.images.size() > 600)
        {
            cache.images.clear();
            cache.pixels = 0;
        }
        cache.images[k] = img;
        cache.pixels += (int64) w * h;
    }
    Graphics::ScopedSaveState state (g);
    g.setOpacity (1.0f);   // drawImage uses the alpha of the last colour set
    g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
    g.drawImage (img, outer);
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

enum PartId { pWindow = 1, pTopBar, pFrame, pPanel, pInset, pDisplay, pButton, pKnob, pFader, pScreen };

void drawBackground (Graphics& g, const Design& d, Rectangle<int> bounds, float headerBottom)
{
    const auto r = bounds.toFloat();
    drawWithEffects (g, r, d.windowFx, effectKey (d, pWindow, r), [&] (Graphics& gg)
    {
        Path all;
        all.addRectangle (r);
        fillPath (gg, all, r, d.window);
        if (d.pattern == Design::Pattern::grid && d.patternOpacity > 0.001f)
        {
            gg.setColour (d.patternColour.withMultipliedAlpha (d.patternOpacity));
            for (float x = r.getX(); x < r.getRight(); x += 22.0f)
                gg.fillRect (x, r.getY(), 1.0f, r.getHeight());
            for (float y = r.getY(); y < r.getBottom(); y += 22.0f)
                gg.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        }
        else if (d.pattern == Design::Pattern::noise && d.patternOpacity > 0.001f)
        {
            gg.setTiledImageFill (noiseTile(), 0, 0, jmin (1.0f, d.patternOpacity * 3.0f));
            gg.fillRect (r);
        }
        drawOverlay (gg, all, r, d.windowOverlay, d.window.average());
    });

    const auto bar = r.withHeight (headerBottom + 8.0f - r.getY());
    drawWithEffects (g, bar, d.topBarFx, effectKey (d, pTopBar, bar), [&] (Graphics& gg)
    {
        Path top;
        top.addRectangle (bar);
        fillPath (gg, top, bar, d.topBar);
        drawOverlay (gg, top, bar, d.topBarOverlay, d.topBar.average());
        if (d.topLine > 0.01f)
        {
            gg.setColour (d.topLineColour);
            gg.fillRect (bar.getX(), bar.getBottom() - d.topLine, bar.getWidth(), d.topLine);
        }
    });
}

void drawFrame (Graphics& g, const Design& d, Rectangle<int> bounds)
{
    if (d.frameWidth < 0.01f)
        return;
    const auto r = bounds.toFloat();
    if (! d.frameFx.isActive())
    {
        g.setColour (d.frameColour);
        g.drawRect (r, d.frameWidth);
        return;
    }
    // the four edges separately, so the big see-through middle isn't processed
    const float fw = d.frameWidth;
    const Rectangle<float> edges[] { r.withHeight (fw), r.withTrimmedTop (r.getHeight() - fw), r.withWidth (fw), r.withTrimmedLeft (r.getWidth() - fw) };
    for (int i = 0; i < 4; ++i)
        drawWithEffects (g, edges[i], d.frameFx, effectKey (d, pFrame, edges[i], (uint64) i), [&] (Graphics& gg)
        {
            gg.setColour (d.frameColour);
            gg.fillRect (edges[i]);
        });
}

static float boxMargin (const Box& b) { return b.shadowOpacity > 0.001f ? b.shadowBlur + std::abs (b.shadowY) + 2.0f : 0.0f; }

void drawPanel (Graphics& g, const Design& d, Rectangle<float> r)
{
    drawWithEffects (g, r, d.panel.fx, effectKey (d, pPanel, r), [&] (Graphics& gg)
    {
        drawBox (gg, r, d.panel);
        // a faint light edge along the top
        const float in = jmax (d.panel.radius, d.panel.outline) + 1.0f;
        if (r.getWidth() > 2.0f * in)
        {
            gg.setColour (Colours::white.withAlpha (0.08f));
            gg.fillRect (r.getX() + in, r.getY() + d.panel.outline + 0.5f, r.getWidth() - 2.0f * in, 1.0f);
        }
    }, boxMargin (d.panel));
}

void drawInset (Graphics& g, const Design& d, Rectangle<float> r)
{
    drawWithEffects (g, r, d.inset.fx, effectKey (d, pInset, r), [&] (Graphics& gg)
    {
        drawBox (gg, r, d.inset);
        if (d.insetDepth > 0.001f)
        {
            Graphics::ScopedSaveState state (gg);
            gg.reduceClipRegion (shapePath (r, d.inset.radius, d.inset.shape));
            gg.setGradientFill (ColourGradient::vertical (Colours::black.withAlpha (d.insetDepth * 0.85f), r.getY(), Colours::transparentBlack, r.getY() + 8.0f));
            gg.fillRect (r);
        }
    }, boxMargin (d.inset));
}

/** The pattern on a screen: scan lines, a grid, an LCD dot matrix or vertical stripes. */
static void drawScreenPattern (Graphics& g, Design::ScreenPattern pattern, float s, Colour colour, Rectangle<float> r)
{
    const auto c = [colour] (float alpha) { return colour.withMultipliedAlpha (alpha); };
    switch (pattern)
    {
        case Design::ScreenPattern::none: return;
        case Design::ScreenPattern::scanlines:
            g.setColour (c (0.35f * s));
            for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
                g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
            return;
        case Design::ScreenPattern::grid:
            g.setColour (c (0.3f * s));
            for (float y = r.getY(); y < r.getBottom(); y += 4.0f)
                g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
            for (float x = r.getX(); x < r.getRight(); x += 4.0f)
                g.fillRect (x, r.getY(), 1.0f, r.getHeight());
            return;
        case Design::ScreenPattern::dots:
            g.setColour (c (0.42f * s));
            for (float y = r.getY() + 1.0f; y < r.getBottom(); y += 2.0f)
                g.fillRect (r.getX(), y, r.getWidth(), 0.7f);
            for (float x = r.getX() + 1.0f; x < r.getRight(); x += 2.0f)
                g.fillRect (x, r.getY(), 0.7f, r.getHeight());
            return;
        case Design::ScreenPattern::stripes:
            g.setColour (c (0.32f * s));
            for (float x = r.getX(); x < r.getRight(); x += 3.0f)
                g.fillRect (x, r.getY(), 1.0f, r.getHeight());
            return;
    }
}

/** A glossy reflection: light on the upper half, a soft diagonal sheen and a bright top edge. */
static void drawGlass (Graphics& g, Rectangle<float> r, const Path& clip, const Box& b)
{
    Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (clip);
    g.setGradientFill (ColourGradient::vertical (Colours::white.withAlpha (0.2f), r.getY(), Colours::white.withAlpha (0.03f), r.getCentreY()));
    g.fillRect (r.withHeight (r.getHeight() * 0.5f));
    Path sheen;
    sheen.startNewSubPath (r.getX() + r.getWidth() * 0.55f, r.getY());
    sheen.lineTo (r.getX() + r.getWidth() * 0.75f, r.getY());
    sheen.lineTo (r.getX() + r.getWidth() * 0.45f, r.getBottom());
    sheen.lineTo (r.getX() + r.getWidth() * 0.25f, r.getBottom());
    sheen.closeSubPath();
    g.setColour (Colours::white.withAlpha (0.06f));
    g.fillPath (sheen);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillRect (r.getX() + b.radius, r.getY() + b.outline + 0.5f, jmax (0.0f, r.getWidth() - 2.0f * b.radius), 0.8f);
}

void drawScreen (Graphics& g, const Design& d, Rectangle<float> r)
{
    drawWithEffects (g, r, d.screen.fx, effectKey (d, pScreen, r), [&] (Graphics& gg)
    {
        drawBox (gg, r, d.screen);
        const auto clip = shapePath (r, d.screen.radius, d.screen.shape);
        {
            Graphics::ScopedSaveState state (gg);
            gg.reduceClipRegion (clip);
            drawScreenPattern (gg, d.graphPattern, d.graphPatternStrength, d.graphPatternColour, r);
            if (d.screensLikeDisplays && d.displayGlow > 0.001f)   // like a glowing display, it sits a little deeper
            {
                gg.setGradientFill (ColourGradient::vertical (Colours::black.withAlpha (0.7f), r.getY(), Colours::transparentBlack, r.getY() + 5.0f));
                gg.fillRect (r);
            }
        }
        if (d.graphGlass)
            drawGlass (gg, r, clip, d.screen);
    }, boxMargin (d.screen));
}

void drawDisplay (Graphics& g, const Design& d, Rectangle<float> r, const String& text, const Font& font,
                  Justification just, Colour textColour)
{
    uint64 extra = (uint64) text.hashCode64();
    extra = mix (extra, (uint64) textColour.getARGB());
    extra = mix (extra, (uint64) roundToInt (font.getHeight() * 100.0f));
    extra = mix (extra, (uint64) font.getTypefaceName().hashCode64());
    extra = mix (extra, (uint64) just.getFlags());
    drawWithEffects (g, r, d.display.fx, effectKey (d, pDisplay, r, extra), [&] (Graphics& gg)
    {
        drawBox (gg, r, d.display);
        const auto clip = shapePath (r, d.display.radius, d.display.shape);
        {
            Graphics::ScopedSaveState state (gg);
            gg.reduceClipRegion (clip);
            if (d.scanlines)
            {
                gg.setColour (Colours::black.withAlpha (0.35f));
                for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
                    gg.fillRect (r.getX(), y, r.getWidth(), 1.0f);
            }
            if (d.displayGlow > 0.001f)   // a glowing screen sits a little deeper
            {
                gg.setGradientFill (ColourGradient::vertical (Colours::black.withAlpha (0.7f), r.getY(), Colours::transparentBlack, r.getY() + 5.0f));
                gg.fillRect (r);
            }
        }
        if (text.isNotEmpty())
        {
            const auto area = r.reduced (5.0f + d.display.outline, 0.0f);
            if (d.displayGlow > 0.001f)
            {
                GlyphArrangement ga;
                ga.addFittedText (font, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(), just, 1, 0.7f);
                Path p;
                ga.createPath (p);
                DropShadow (textColour.withAlpha (0.55f * d.displayGlow + 0.1f), roundToInt (6.0f * d.displayGlow + 1.0f), {}).drawForPath (gg, p);
                gg.setColour (textColour);
                gg.fillPath (p);
            }
            else
            {
                gg.setColour (textColour);
                gg.setFont (font);
                gg.drawFittedText (text, area.toNearestInt(), just, 1, 0.7f);
            }
        }
    }, boxMargin (d.display) + (d.displayGlow > 0.001f ? 8.0f : 0.0f));
}

void drawButton (Graphics& g, const Design& d, Rectangle<float> r, bool on, bool tab, bool highlighted, bool down, bool enabled)
{
    const auto& b = tab ? (on ? d.tabOn : d.tab) : (on ? d.buttonOn : d.button);
    const float glow = (tab && ! d.tabsLikeButtons) ? 0.0f : d.buttonOnGlow;
    const uint64 state = (on ? 1u : 0u) | (tab ? 2u : 0u) | (highlighted ? 4u : 0u) | (down ? 8u : 0u) | (enabled ? 16u : 0u);
    drawWithEffects (g, r, b.fx, effectKey (d, pButton, r, state), [&] (Graphics& gg)
    {
        auto rr = down ? r.translated (0.0f, 1.0f) : r;
        const auto path = shapePath (rr, b.radius, b.shape);
        if (on && enabled && glow > 0.001f)
            DropShadow (b.fill.average().withAlpha (0.9f * glow), roundToInt (12.0f * glow), {}).drawForPath (gg, path);
        drawBox (gg, rr, b);
        if (! on && d.bevel > 0.001f)
        {
            // shine on the upper half
            Graphics::ScopedSaveState ss (gg);
            gg.reduceClipRegion (path);
            gg.setGradientFill (ColourGradient::vertical (Colours::white.withAlpha (0.25f * d.bevel + (highlighted && enabled ? 0.08f : 0.0f)), rr.getY(),
                                                          Colours::white.withAlpha (0.0f), rr.getY() + rr.getHeight() * 0.45f));
            gg.fillRect (rr);
        }
        if (highlighted && enabled && ! on && ! down)
        {
            gg.setColour (d.accent.withAlpha (0.5f));
            gg.strokePath (shapePath (rr.reduced (b.outline + 1.0f), jmax (0.0f, b.radius - b.outline - 1.0f), b.shape), PathStrokeType (1.5f));
        }
        if (! enabled)
        {
            gg.setColour ((d.isLight() ? Colours::white : Colours::black).withAlpha (0.4f));
            gg.fillPath (path);
        }
    }, boxMargin (b) + 12.0f * glow + 1.0f);
}

Colour buttonTextColour (const Design& d, bool on, bool tab, bool enabled)
{
    auto c = tab && ! d.tabsLikeButtons ? d.tabText : on ? d.buttonOnText : d.buttonText;
    return enabled ? c : c.withMultipliedAlpha (0.45f);
}

static void drawKnobRaw (Graphics& g, const Design& d, Rectangle<float> r, float startAngle, float endAngle, float pos)
{
    const float size = jmin (r.getWidth(), r.getHeight()), rad = size * 0.5f, scale = size / 44.0f;
    const auto c = r.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    Path circle;

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

    if (d.knobStyle == Design::KnobStyle::picture && (d.knobLower.isValid() || d.knobUpper.isValid()))
    {
        // two picture layers, each turning with the value or standing still
        const auto area = Rectangle<float> (d.ring > 0.01f ? capR * 2.0f + 4.0f * scale : size, d.ring > 0.01f ? capR * 2.0f + 4.0f * scale : size).withCentre (c);
        auto layer = [&] (const Picture& p, bool turns)
        {
            if (! p.isValid())
                return;
            Graphics::ScopedSaveState state (g);
            if (turns)
                g.addTransform (AffineTransform::rotation (angle, c.x, c.y));
            g.setOpacity (1.0f);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (p.image, area, RectanglePlacement::centred);
        };
        layer (d.knobLower, d.lowerTurns);
        layer (d.knobUpper, d.upperTurns);
        circle.addEllipse (area);
        drawOverlay (g, circle, area, d.knobOverlay, Colours::grey);
        return;
    }

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

void drawKnob (Graphics& g, const Design& d, Rectangle<float> r, float startAngle, float endAngle, float pos)
{
    if (! d.knobFx.isActive())
    {
        drawKnobRaw (g, d, r, startAngle, endAngle, pos);
        return;
    }
    // positions are rounded to 1/256 of a turn, so a drag reuses what's been drawn
    const float q = std::round (jlimit (0.0f, 1.0f, pos) * 256.0f) / 256.0f;
    uint64 extra = mix ((uint64) roundToInt (q * 256.0f), (uint64) roundToInt (startAngle * 1000.0f + 10000.0f));
    extra = mix (extra, (uint64) roundToInt (endAngle * 1000.0f + 10000.0f));
    drawWithEffects (g, r, d.knobFx, effectKey (d, pKnob, r, extra),
                     [&] (Graphics& gg) { drawKnobRaw (gg, d, r, startAngle, endAngle, q); }, jmin (r.getWidth(), r.getHeight()) * 0.15f + 2.0f);
}

void drawFader (Graphics& g, const Design& d, Rectangle<float> track, float pos)
{
    auto raw = [&d, track] (Graphics& gg, float p)
    {
        const float cx = track.getCentreX();
        const auto groove = Rectangle<float> (cx - 3.0f, track.getY() + 6.0f, 6.0f, jmax (2.0f, track.getHeight() - 12.0f));
        gg.setColour (d.faderTrack);
        gg.fillRoundedRectangle (groove, 3.0f);
        gg.setColour (Colours::black.withAlpha (0.6f));
        gg.drawRoundedRectangle (groove, 3.0f, 1.0f);
        const float top = jlimit (groove.getY(), groove.getBottom(), p);
        if (groove.getBottom() - top > 0.5f)
        {
            gg.setColour (d.faderFill);
            gg.fillRoundedRectangle (Rectangle<float> (cx - 2.0f, top, 4.0f, groove.getBottom() - top), 2.0f);
        }
        const auto thumb = Rectangle<float> (cx - 13.0f, p - 7.0f, 26.0f, 14.0f);
        Path path;
        path.addRoundedRectangle (thumb, 3.0f);
        if (d.thumbPicture.isValid())
        {
            Graphics::ScopedSaveState state (gg);
            gg.reduceClipRegion (path);
            gg.setOpacity (1.0f);
            gg.drawImage (d.thumbPicture.image, thumb, RectanglePlacement::stretchToFit);
        }
        else
        {
            gg.setGradientFill (ColourGradient::vertical (d.thumbTop, thumb.getY(), d.thumbBottom, thumb.getBottom()));
            gg.fillPath (path);
        }
        drawOverlay (gg, path, thumb, d.faderOverlay, d.thumbPicture.isValid() ? Colours::grey : d.thumbTop.interpolatedWith (d.thumbBottom, 0.5f));
        gg.setColour (Colours::black.withAlpha (0.85f));
        gg.strokePath (path, PathStrokeType (1.0f));
    };
    if (! d.faderFx.isActive())
    {
        raw (g, pos);
        return;
    }
    const float rel = std::round (pos - track.getY());
    drawWithEffects (g, track.expanded (8.0f, 0.0f), d.faderFx, effectKey (d, pFader, track, (uint64) roundToInt (rel + 1000.0f)),
                     [&] (Graphics& gg) { raw (gg, track.getY() + rel); }, 8.0f);
}
} // namespace emx::skin
