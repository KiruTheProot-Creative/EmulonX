// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Theme.h"
#include "EmxAssets.h"
#include <thread>
#include <unordered_map>

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

//== Hue ==========================================================================
// The rotation happens in OKLab, a colour space where equal numbers look equally bright:
// turning red into blue keeps the same lightness, so text stays as readable as in red.
namespace
{
struct Lab { float L, a, b; };

float toLinear (float c) { return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f); }
float toSrgb (float c)   { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow (c, 1.0f / 2.4f) - 0.055f; }

Lab linearToLab (float r, float g, float b)
{
    const float l = std::cbrt (0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt (0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt (0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
             1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
             0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
}

void labToLinear (const Lab& c, float& r, float& g, float& b)
{
    const float l = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    const float m = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    const float s = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
    const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
    r =  4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
    g = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
    b = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;
}

/** Rotates r,g,b (0..1, sRGB) in place; pulls the colour inside the screen's range by
    lowering its saturation rather than clipping, so hue and lightness stay put. */
void rotateRgb (float& r, float& g, float& b, float cosA, float sinA)
{
    auto lab = linearToLab (toLinear (r), toLinear (g), toLinear (b));
    const float a = lab.a * cosA - lab.b * sinA, bb = lab.a * sinA + lab.b * cosA;
    auto inside = [] (float x, float y, float z) { return x >= -1.0e-4f && x <= 1.0001f && y >= -1.0e-4f && y <= 1.0001f && z >= -1.0e-4f && z <= 1.0001f; };
    float lr, lg, lb;
    labToLinear ({ lab.L, a, bb }, lr, lg, lb);
    if (! inside (lr, lg, lb))
    {
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 14; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            labToLinear ({ lab.L, a * mid, bb * mid }, lr, lg, lb);
            (inside (lr, lg, lb) ? lo : hi) = mid;
        }
        labToLinear ({ lab.L, a * lo, bb * lo }, lr, lg, lb);
    }
    r = toSrgb (juce::jlimit (0.0f, 1.0f, lr));
    g = toSrgb (juce::jlimit (0.0f, 1.0f, lg));
    b = toSrgb (juce::jlimit (0.0f, 1.0f, lb));
}

float currentShift = 0.0f;
std::unordered_map<juce::uint32, juce::uint32> hueCache;

struct BaseColours
{
    juce::Colour* target;
    juce::uint32 argb;
};
const BaseColours baseColours[] {
    { &red, 0xffff2a2a }, { &redDim, 0xffe83c3c }, { &redDark, 0xff7a2828 }, { &textCol, 0xffe8e4e4 },
    { &textMuted, 0xffb8a6a6 }, { &listText, 0xffff4a4a }, { &ledBg, 0xff070203 }, { &edge, 0xff3a363d },
    { &highlight, 0xff4a444f } };

/** A recoloured copy of an image: a 33x33x33 colour table, looked up with blending (fast enough to
    follow the hue slider live). */
juce::Image recolour (const juce::Image& src, float degrees)
{
    constexpr int N = 33;
    static std::vector<float> lut ((size_t) (N * N * N * 3));
    static float lutDegrees = 1000.0f;   // the table is shared by the four images of one hue
    const int threads = juce::jlimit (1, 8, (int) std::thread::hardware_concurrency());
    auto parallel = [threads] (int count, const std::function<void (int, int)>& job)
    {
        std::vector<std::thread> pool;
        for (int t = 1; t < threads; ++t)
            pool.emplace_back (job, count * t / threads, count * (t + 1) / threads);
        job (0, count / threads);
        for (auto& th : pool)
            th.join();
    };
    if (std::abs (lutDegrees - degrees) > 0.001f)
    {
        lutDegrees = degrees;
        const float ca = std::cos (juce::degreesToRadians (degrees)), sa = std::sin (juce::degreesToRadians (degrees));
        parallel (N, [&] (int r0, int r1)
        {
            for (int ri = r0; ri < r1; ++ri)
                for (int gi = 0; gi < N; ++gi)
                    for (int bi = 0; bi < N; ++bi)
                    {
                        float r = (float) ri / (N - 1), g = (float) gi / (N - 1), b = (float) bi / (N - 1);
                        rotateRgb (r, g, b, ca, sa);
                        auto* o = &lut[(size_t) (((ri * N + gi) * N + bi) * 3)];
                        o[0] = r * 255.0f; o[1] = g * 255.0f; o[2] = b * 255.0f;
                    }
        });
    }

    auto out = src.convertedToFormat (juce::Image::ARGB);
    out.duplicateIfShared();
    juce::Image::BitmapData d (out, juce::Image::BitmapData::readWrite);
    constexpr float step = (N - 1) / 255.0f;
    auto rowsFrom = [&d] (int y0, int y1)
    {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < d.width; ++x)
            {
                auto* px = reinterpret_cast<juce::PixelARGB*> (d.getPixelPointer (x, y));
                const auto alpha = px->getAlpha();
                if (alpha == 0)
                    continue;
                if (alpha < 255) px->unpremultiply();
                const float fr = px->getRed() * step, fg = px->getGreen() * step, fb = px->getBlue() * step;
                const int r0 = juce::jmin (N - 2, (int) fr), g0 = juce::jmin (N - 2, (int) fg), b0 = juce::jmin (N - 2, (int) fb);
                const float tr = fr - r0, tg = fg - g0, tb = fb - b0;
                float res[3] {};
                for (int corner = 0; corner < 8; ++corner)
                {
                    const int dr = corner >> 2, dg = (corner >> 1) & 1, db = corner & 1;
                    const float wgt = (dr ? tr : 1 - tr) * (dg ? tg : 1 - tg) * (db ? tb : 1 - tb);
                    const auto* l = &lut[(size_t) ((((r0 + dr) * N + g0 + dg) * N + b0 + db) * 3)];
                    res[0] += wgt * l[0]; res[1] += wgt * l[1]; res[2] += wgt * l[2];
                }
                px->setARGB (alpha, (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (res[0])),
                             (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (res[1])),
                             (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (res[2])));
                if (alpha < 255) px->premultiply();
            }
    };
    // split the rows over the CPU cores so the colour follows the hue bar smoothly
    parallel (d.height, rowsFrom);
    return out;
}
} // namespace

juce::Colour rotateHue (juce::Colour c, float degrees)
{
    if (std::abs (degrees) < 0.01f || c.getSaturation() < 0.02f)
        return c;
    float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
    rotateRgb (r, g, b, std::cos (juce::degreesToRadians (degrees)), std::sin (juce::degreesToRadians (degrees)));
    return juce::Colour::fromFloatRGBA (r, g, b, c.getFloatAlpha());
}

juce::Colour hue (juce::uint32 argb)
{
    if (std::abs (currentShift) < 0.01f)
        return juce::Colour (argb);
    if (auto it = hueCache.find (argb); it != hueCache.end())
        return juce::Colour (it->second);
    const auto c = rotateHue (juce::Colour (argb), currentShift);
    hueCache[argb] = c.getARGB();
    return c;
}

float getHueShift() noexcept { return currentShift; }

void setHueShift (float degrees)
{
    degrees = juce::jlimit (-180.0f, 180.0f, degrees);
    if (std::abs (degrees - currentShift) < 0.01f)
        return;
    currentShift = degrees;
    hueCache.clear();
    for (auto& b : baseColours)
        *b.target = hue (b.argb);
}

juce::Image image (Img which)
{
    juce::Image src;
    switch (which)
    {
        case Img::background: src = juce::ImageCache::getFromMemory (EmxAssets::background_jpg, EmxAssets::background_jpgSize); break;
        case Img::frame:      src = juce::ImageCache::getFromMemory (EmxAssets::frame_png, EmxAssets::frame_pngSize); break;
        case Img::knob:       src = juce::ImageCache::getFromMemory (EmxAssets::knob_png, EmxAssets::knob_pngSize); break;
        case Img::slider:     src = juce::ImageCache::getFromMemory (EmxAssets::slider_png, EmxAssets::slider_pngSize); break;
    }
    if (std::abs (currentShift) < 0.01f || ! src.isValid())
        return src;

    struct Tinted { float shift = 0.0f; juce::Image img; };
    static Tinted tinted[4];
    auto& t = tinted[(int) which];
    if (! t.img.isValid() || std::abs (t.shift - currentShift) > 0.01f)
        t = { currentShift, recolour (src, currentShift) };
    return t.img;
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    juce::Path p;
    p.addRoundedRectangle (r, radius);
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 10, { 0, 3 }).drawForPath (g, p);

    juce::ColourGradient grad (hue (0xff26232a), 0.0f, r.getY(), hue (0xff0d0c10), 0.0f, r.getBottom(), false);
    grad.addColour (0.4, hue (0xff141217));
    g.setGradientFill (grad);
    g.fillPath (p);

    g.setColour (highlight);
    g.fillRect (r.getX() + radius, r.getY() + 1.0f, r.getWidth() - 2.0f * radius, 1.0f);
    g.setColour (juce::Colours::black);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void drawInset (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setGradientFill (juce::ColourGradient::vertical (hue (0xff1c1a1f), r.getY(), hue (0xff111014), r.getBottom()));
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
