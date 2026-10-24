// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Theme.h"
#include "EmxAssets.h"
#include "SkinDesign.h"
#include <thread>
#include <unordered_map>
#include <map>

namespace emx::theme
{
static juce::Typeface::Ptr typefaceFor (int weight)
{
    static auto w500 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Saira500_ttf, (size_t) EmxAssets::Saira500_ttfSize);
    static auto w700 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Saira700_ttf, (size_t) EmxAssets::Saira700_ttfSize);
    static auto w900 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Saira900_ttf, (size_t) EmxAssets::Saira900_ttfSize);
    return weight >= 900 ? w900 : weight >= 700 ? w700 : w500;
}

namespace
{
/** The loaded custom design (decoded once). */
struct CustomSkin
{
    juce::Image img[11];                 // by Img
    std::map<juce::String, juce::Colour> colours;
    bool hue = true, knobRotates = false, shadows = true, keepCorners = false;
    juce::Typeface::Ptr font;
    float fontScale = 1.0f;
    float accentOffset = 0.0f;           // hue turn from the classic red to the skin's accent
    int generation = 0;
};
CustomSkin customSkin;
float capHeightOf (juce::Typeface::Ptr t)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (juce::Font (juce::FontOptions (t).withHeight (100.0f)), "H", 0, 0);
    juce::Path p;
    ga.createPath (p);
    return p.getBounds().getHeight();
}
} // namespace

juce::Font font (float height, int weight, float kerning)
{
    if (getSkin() == Skin::custom && customSkin.font != nullptr)
    {
        juce::Font f { juce::FontOptions (customSkin.font).withHeight (height * customSkin.fontScale) };
        return std::abs (kerning) > 1.0e-6f ? f.withExtraKerningFactor (kerning) : f;
    }
    if (isXp())
    {
        // Tahoma, the classic desktop font, from the system (not shipped with the plugin);
        // the system's standard sans-serif steps in where it is missing.
        static const bool hasTahoma = juce::Font::findAllTypefaceNames().contains ("Tahoma");
        const juce::String name = hasTahoma ? juce::String ("Tahoma") : juce::Font::getDefaultSansSerifFontName();
        return juce::Font (juce::FontOptions (name, height * 1.02f, weight >= 700 ? juce::Font::bold : juce::Font::plain));
    }
    // Saira's capitals are smaller for the same font height than the old Orbitron's; this keeps
    // every size in the interface the same as it was designed (equal capital height).
    constexpr float capMatch = 1.3137f;
    juce::Font f { juce::FontOptions (typefaceFor (weight)).withHeight (height * capMatch) };
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
Skin currentSkin = Skin::classic;
std::unordered_map<juce::uint32, juce::uint32> hueCache;

/** Translates a colour of the red design into the XP skins: red becomes XP blue; in the light
    skin dark backgrounds become light and light text becomes dark. Orange/yellow/green keep their hue. */
juce::Colour xpMap (juce::Colour c, bool light)
{
    auto lab = linearToLab (toLinear (c.getFloatRed()), toLinear (c.getFloatGreen()), toLinear (c.getFloatBlue()));
    const float chroma = std::hypot (lab.a, lab.b);
    float hueDeg = juce::radiansToDegrees (std::atan2 (lab.b, lab.a));
    float L = lab.L, C = chroma;
    if (chroma < 0.035f)
    {
        // greys: backgrounds and text
        if (light)
        {
            const bool background = L < 0.5f;
            L = background ? 0.985f - L * 0.42f : 0.05f + (1.0f - L) * 0.9f;
            if (background) { hueDeg = 95.0f; C = 0.016f; }   // warm beige, like old desktop windows
            else C = 0.0f;
        }
        else
        {
            L = 0.10f + L * 0.92f;
            C = 0.0f;
        }
    }
    else
    {
        const bool keepHue = hueDeg > 45.0f && hueDeg < 150.0f;   // warnings, modulation rings
        if (! keepHue)
            hueDeg = 262.0f + (hueDeg - 29.0f) * 0.3f;            // reds -> XP blue (purples stay near it)
        C = juce::jmin (0.2f, C * 0.85f);
        if (light)
            L = L < 0.45f ? 0.97f - L * 0.55f : 0.30f + (L - 0.45f) * 0.55f;
    }
    float r, g, b;
    const float rad = juce::degreesToRadians (hueDeg);
    labToLinear ({ L, C * std::cos (rad), C * std::sin (rad) }, r, g, b);
    return juce::Colour::fromFloatRGBA (toSrgb (juce::jlimit (0.0f, 1.0f, r)), toSrgb (juce::jlimit (0.0f, 1.0f, g)),
                                        toSrgb (juce::jlimit (0.0f, 1.0f, b)), c.getFloatAlpha());
}

void applyPalette();

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
    if (currentSkin == Skin::classic && std::abs (currentShift) < 0.01f)
        return juce::Colour (argb);
    if (currentSkin == Skin::custom && std::abs (customSkin.accentOffset + currentShift) < 0.01f)
        return juce::Colour (argb);
    if (auto it = hueCache.find (argb); it != hueCache.end())
        return juce::Colour (it->second);
    const auto c = currentSkin == Skin::classic ? rotateHue (juce::Colour (argb), currentShift)
                 : currentSkin == Skin::custom  ? rotateHue (juce::Colour (argb), customSkin.accentOffset + currentShift)
                                                 : xpMap (juce::Colour (argb), currentSkin == Skin::xpLight);
    hueCache[argb] = c.getARGB();
    return c;
}

Skin getSkin() noexcept { return currentSkin; }

void setSkin (Skin skin)
{
    if (skin == currentSkin)
        return;
    currentSkin = skin;
    hueCache.clear();
    applyPalette();
}

namespace
{
void applyPalette()
{
    switch (currentSkin)
    {
        case Skin::classic:
            for (auto& b : baseColours)
                *b.target = hue (b.argb);
            textStrong = juce::Colours::white;
            ledText = red;
            windowBg = juce::Colours::black;
            break;
        case Skin::custom:
        {
            for (auto& b : baseColours)
                *b.target = hue (b.argb);
            textStrong = juce::Colours::white;
            ledText = red;
            windowBg = juce::Colours::black;
            auto own = [] (const char* key, juce::Colour& target)
            {
                if (auto it = customSkin.colours.find (key); it != customSkin.colours.end())
                    target = rotateHue (it->second, currentShift);
            };
            own ("accent", red);
            own ("text", textStrong);
            own ("text", textCol);
            own ("muted", textMuted);
            own ("display-text", ledText);
            own ("display-bg", ledBg);
            own ("list", listText);
            if (customSkin.colours.count ("accent") != 0)
            {
                redDim = red.brighter (0.15f);
                if (customSkin.colours.count ("display-text") == 0)
                    ledText = red;
            }
            break;
        }
        case Skin::xpLight:
            red = juce::Colour (0xff2f6bd9);  redDim = juce::Colour (0xff3d7ee6);  redDark = juce::Colour (0xffa9c1ec);
            textCol = juce::Colour (0xff101010);  textMuted = juce::Colour (0xff57564f);  listText = juce::Colour (0xff0b3a9e);
            ledBg = juce::Colours::white;  edge = juce::Colour (0xff7f9db9);  highlight = juce::Colours::white;
            textStrong = juce::Colour (0xff101010);  ledText = juce::Colour (0xff101010);  windowBg = juce::Colour (0xffece9d8);
            break;
        case Skin::xpDark:
            red = juce::Colour (0xff4d8dff);  redDim = juce::Colour (0xff5f99ff);  redDark = juce::Colour (0xff27426f);
            textCol = juce::Colour (0xffeaeaea);  textMuted = juce::Colour (0xffa9a9a9);  listText = juce::Colour (0xff8fbaff);
            ledBg = juce::Colour (0xff191919);  edge = juce::Colour (0xff56677f);  highlight = juce::Colour (0xff4a4a4a);
            textStrong = juce::Colour (0xffeeeeee);  ledText = juce::Colour (0xffe6e6e6);  windowBg = juce::Colour (0xff1e1e1e);
            break;
    }
}
} // namespace

float getHueShift() noexcept { return currentShift; }

void setHueShift (float degrees)
{
    degrees = juce::jlimit (-180.0f, 180.0f, degrees);
    if (std::abs (degrees - currentShift) < 0.01f)
        return;
    currentShift = degrees;
    hueCache.clear();
    applyPalette();
}

bool hasImage (Img which) noexcept
{
    return currentSkin == Skin::custom && customSkin.img[(int) which].isValid();
}

juce::Image image (Img which)
{
    const int idx = (int) which;
    juce::Image src;
    bool own = false;
    if (currentSkin == Skin::custom && customSkin.img[idx].isValid())
    {
        src = customSkin.img[idx];
        own = true;
    }
    else
    {
        switch (which)
        {
            case Img::background: src = juce::ImageCache::getFromMemory (EmxAssets::background_jpg, EmxAssets::background_jpgSize); break;
            case Img::frame:      src = juce::ImageCache::getFromMemory (EmxAssets::frame_png, EmxAssets::frame_pngSize); break;
            case Img::knob:       src = juce::ImageCache::getFromMemory (EmxAssets::knob_png, EmxAssets::knob_pngSize); break;
            case Img::slider:     src = juce::ImageCache::getFromMemory (EmxAssets::slider_png, EmxAssets::slider_pngSize); break;
            case Img::logo:       src = juce::ImageCache::getFromMemory (EmxAssets::logo_png, EmxAssets::logo_pngSize); break;
            case Img::panel: case Img::inset: case Img::display: case Img::button: case Img::buttonOn: case Img::faderCap:
                break;   // drawn, not pictures (unless a skin brings one)
        }
    }
    if (isXp() || ! src.isValid())
        return src;
    // Classic: the colour shift. Custom: the skin's own pictures follow the colour shift (if allowed),
    // the Classic pictures it doesn't replace also turn to its accent colour.
    const float deg = currentSkin == Skin::classic ? currentShift
                    : own ? currentShift : customSkin.accentOffset + currentShift;
    if (std::abs (deg) < 0.01f)
        return src;

    struct Tinted { float deg = 0.0f; bool own = false; int gen = -1; juce::Image img; };
    static Tinted tinted[11];
    auto& t = tinted[idx];
    if (! t.img.isValid() || std::abs (t.deg - deg) > 0.01f || t.own != own || t.gen != customSkin.generation)
        t = { deg, own, customSkin.generation, recolour (src, deg) };
    return t.img;
}

void drawSliced (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> r, int)
{
    if (! img.isValid() || r.isEmpty())
        return;
    const int srcMin = juce::jmax (1, juce::jmin (img.getWidth(), img.getHeight()));
    const int s = juce::jmax (1, juce::roundToInt ((float) srcMin * 0.25f));
    // shrink corners as much as the picture's short side has to shrink (at most to half: 2x artwork)
    const float scale = juce::jmin (0.5f, juce::jmin (r.getWidth(), r.getHeight()) / (float) srcMin);
    const int d = juce::jmax (1, juce::jmin (juce::roundToInt ((float) s * scale), (int) juce::jmin (r.getWidth(), r.getHeight()) / 2));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (currentSkin == Skin::custom && ! customSkin.keepCorners)
    {
        // the whole picture fills the area
        drawImageWithShadow (g, img, juce::RectanglePlacement (juce::RectanglePlacement::stretchToFit)
                                         .getTransformToFit (img.getBounds().toFloat(), r));
        return;
    }
    const auto dest = r.toNearestInt();
    if (currentSkin == Skin::custom && customSkin.shadows)
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        drawNineSlice (g, img, dest.translated (0, 3), s, d, true);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        drawNineSlice (g, img, dest.translated (0, 1), s, d, true);
    }
    drawNineSlice (g, img, dest, s, d);
}

void drawImageWithShadow (juce::Graphics& g, const juce::Image& img, juce::AffineTransform t)
{
    if (! img.isValid())
        return;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (currentSkin == Skin::custom && customSkin.shadows)
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.drawImageTransformed (img, t.translated (0.0f, 3.0f), true);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.drawImageTransformed (img, t.translated (0.0f, 1.2f), true);
    }
    g.drawImageTransformed (img, t);
}

bool customShadows() noexcept { return currentSkin == Skin::custom && customSkin.shadows; }

juce::MemoryBlock defaultArtworkFile (Img which, juce::String& ext)
{
    ext = "png";
    switch (which)
    {
        case Img::background: ext = "jpg"; return { EmxAssets::background_jpg, (size_t) EmxAssets::background_jpgSize };
        case Img::frame:      return { EmxAssets::frame_png, (size_t) EmxAssets::frame_pngSize };
        case Img::knob:       return { EmxAssets::knob_png, (size_t) EmxAssets::knob_pngSize };
        case Img::slider:     return { EmxAssets::slider_png, (size_t) EmxAssets::slider_pngSize };
        case Img::logo:       return { EmxAssets::logo_png, (size_t) EmxAssets::logo_pngSize };
        case Img::panel: case Img::inset: case Img::display: case Img::button: case Img::buttonOn: case Img::faderCap:
            break;
    }
    return {};
}

juce::Image defaultPartImage (Img which)
{
    // the Classic drawing, at 2x, in the original red (no skin, no shift)
    auto make = [] (int w, int h, const std::function<void (juce::Graphics&, juce::Rectangle<float>)>& paint)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);
        paint (g, juce::Rectangle<float> ((float) w, (float) h).reduced (1.0f));
        return img;
    };
    switch (which)
    {
        case Img::panel:
            return make (96, 96, [] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                juce::ColourGradient grad (juce::Colour (0xff26232a), 0.0f, r.getY(), juce::Colour (0xff0d0c10), 0.0f, r.getBottom(), false);
                grad.addColour (0.4, juce::Colour (0xff141217));
                g.setGradientFill (grad);
                g.fillRoundedRectangle (r, 18.0f);
                g.setColour (juce::Colour (0xff4a444f));
                g.fillRect (r.getX() + 18.0f, r.getY() + 2.0f, r.getWidth() - 36.0f, 2.0f);
                g.setColour (juce::Colours::black);
                g.drawRoundedRectangle (r, 18.0f, 2.0f);
            });
        case Img::inset:
            return make (80, 80, [] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff1c1a1f), r.getY(), juce::Colour (0xff111014), r.getBottom()));
                g.fillRoundedRectangle (r, 16.0f);
                g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::black.withAlpha (0.6f), r.getY(), juce::Colours::transparentBlack, r.getY() + 12.0f));
                g.fillRoundedRectangle (r, 16.0f);
            });
        case Img::display:
            return make (48, 32, [] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                g.setColour (juce::Colour (0xff070203));
                g.fillRoundedRectangle (r, 10.0f);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                for (float y = r.getY() + 2.0f; y < r.getBottom() - 2.0f; y += 6.0f)
                    g.fillRect (r.getX() + 3.0f, y, r.getWidth() - 6.0f, 2.0f);
                g.setColour (juce::Colours::black);
                g.drawRoundedRectangle (r, 10.0f, 2.0f);
            });
        case Img::button:
        case Img::buttonOn:
        {
            const bool on = which == Img::buttonOn;
            return make (64, 36, [on] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (on ? 0xffc62828 : 0xff55515a), r.getY(),
                                                                   juce::Colour (on ? 0xff5a0808 : 0xff1d1b20), r.getBottom()));
                g.fillRoundedRectangle (r, 10.0f);
                g.setColour (juce::Colour (on ? 0xffff7a7a : 0xff7a747f));
                g.fillRect (r.getX() + 8.0f, r.getY() + 2.0f, r.getWidth() - 16.0f, 2.0f);
                g.setColour (juce::Colours::black);
                g.drawRoundedRectangle (r, 10.0f, 2.0f);
            });
        }
        case Img::faderCap:
            return make (64, 32, [] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff8a8590), r.getY(), juce::Colour (0xff202025), r.getBottom()));
                g.fillRoundedRectangle (r, 8.0f);
                g.setColour (juce::Colours::black);
                g.drawRoundedRectangle (r, 8.0f, 2.0f);
                g.setColour (juce::Colour (0xffff2a2a));
                g.fillRect (r.withSizeKeepingCentre (r.getWidth() - 12.0f, 4.0f));
            });
        case Img::background: case Img::frame: case Img::knob: case Img::slider: case Img::logo:
            break;
    }
    juce::String ext;
    const auto mb = defaultArtworkFile (which, ext);
    return juce::ImageFileFormat::loadFrom (mb.getData(), mb.getSize());
}

bool customHueShift() noexcept { return customSkin.hue; }

juce::Colour baseAccent() noexcept
{
    if (currentSkin == Skin::custom)
        if (auto it = customSkin.colours.find ("accent"); it != customSkin.colours.end())
            return it->second;
    return juce::Colour (0xffff2a2a);
}
bool customKnobRotates() noexcept { return customSkin.knobRotates; }

void setCustomSkin (const SkinDesign& d)
{
    static const Img order[SkinDesign::numParts] { Img::background, Img::frame, Img::knob, Img::slider, Img::panel,
                                                   Img::inset, Img::display, Img::button, Img::buttonOn, Img::logo, Img::faderCap };
    CustomSkin c;
    for (int p = 0; p < SkinDesign::numParts; ++p)
        if (! d.images[p].isEmpty())
            c.img[(int) order[p]] = juce::ImageFileFormat::loadFrom (d.images[p].data.getData(), d.images[p].data.getSize());
    c.colours = d.colours;
    c.hue = d.hueShift;
    c.knobRotates = d.knobRotates;
    c.shadows = d.shadows;
    c.keepCorners = d.keepCorners;
    if (! d.font.isEmpty())
    {
        c.font = juce::Typeface::createSystemTypefaceFor (d.font.data.getData(), d.font.data.getSize());
        if (c.font != nullptr)
        {
            // same capital height as Saira at its usual sizes, so every layout still fits
            static const float target = capHeightOf (typefaceFor (700)) * 1.3137f;
            const float h = capHeightOf (c.font);
            c.fontScale = h > 1.0f ? juce::jlimit (0.5f, 2.5f, target / h) : 1.0f;
        }
    }
    if (auto it = d.colours.find ("accent"); it != d.colours.end() && it->second.getSaturation() > 0.08f)
    {
        auto labHue = [] (juce::Colour col)
        {
            const auto l = linearToLab (toLinear (col.getFloatRed()), toLinear (col.getFloatGreen()), toLinear (col.getFloatBlue()));
            return juce::radiansToDegrees (std::atan2 (l.b, l.a));
        };
        float off = labHue (it->second) - labHue (juce::Colour (0xffff2a2a));
        while (off > 180.0f) off -= 360.0f;
        while (off < -180.0f) off += 360.0f;
        c.accentOffset = off;
    }
    c.generation = customSkin.generation + 1;
    customSkin = std::move (c);

    // Let go of the skin's font and pictures when JUCE shuts down (before the font system does).
    struct Releaser final : juce::DeletedAtShutdown
    {
        ~Releaser() override { customSkin = CustomSkin(); }
    };
    static auto* releaser = new Releaser();
    juce::ignoreUnused (releaser);
    hueCache.clear();
    applyPalette();
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> area, juce::RectanglePlacement placement)
{
    const auto img = image (Img::logo);
    if (! img.isValid())
        return;
    const auto dest = placement.appliedTo (img.getBounds().toFloat(), area);

    // logo + soft glow, rendered once per size, screen scale and hue (the title is repainted often)
    const float px = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float pad = dest.getHeight() * 0.35f;
    const int w = juce::jmax (1, juce::roundToInt ((dest.getWidth() + 2.0f * pad) * px));
    const int h = juce::jmax (1, juce::roundToInt ((dest.getHeight() + 2.0f * pad) * px));
    struct Cache { int w = 0, h = 0; float shift = 1000.0f; Skin skin = Skin::classic; int gen = -1; juce::Image img; };
    static Cache cache;
    if (cache.w != w || cache.h != h || std::abs (cache.shift - currentShift) > 0.01f || cache.skin != currentSkin || cache.gen != customSkin.generation)
    {
        juce::Image out (juce::Image::ARGB, w, h, true);
        {
            juce::Graphics cg (out);
            cg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            const auto inner = juce::Rectangle<float> (pad * px, pad * px, dest.getWidth() * px, dest.getHeight() * px);
            juce::Image shape (juce::Image::ARGB, w, h, true);
            {
                juce::Graphics sg (shape);
                sg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                sg.drawImage (img, inner);
            }
            if (! isXp())
                juce::DropShadow (red.withAlpha (0.45f), juce::roundToInt (dest.getHeight() * 0.3f * px), {}).drawForImage (cg, shape);
            else   // a crisp drop shadow, like desktop icon labels
                juce::DropShadow (juce::Colours::black.withAlpha (0.55f), juce::roundToInt (2.0f * px), { 1, 1 }).drawForImage (cg, shape);
            cg.setOpacity (1.0f);   // the shadow leaves its own colour (45 %) set
            cg.drawImageAt (shape, 0, 0);
        }
        cache = { w, h, currentShift, currentSkin, customSkin.generation, out };
    }
    juce::Graphics::ScopedSaveState state (g);
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (cache.img, dest.expanded (pad));
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    if (isXp())
    {
        // a flat window pane with a thin rounded border
        const bool light = currentSkin == Skin::xpLight;
        g.setColour (light ? juce::Colour (0xfff6f4ec) : juce::Colour (0xff2a2a2a));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (light ? juce::Colour (0xffc9c6b4) : juce::Colour (0xff474747));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        return;
    }
    if (hasImage (Img::panel))
    {
        drawSliced (g, image (Img::panel), r, 24);
        return;
    }
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
    if (isXp())
    {
        const bool light = currentSkin == Skin::xpLight;
        g.setColour (light ? juce::Colour (0xffece9d8) : juce::Colour (0xff222222));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (light ? juce::Colour (0xffd6d3c2) : juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        return;
    }
    if (hasImage (Img::inset))
    {
        drawSliced (g, image (Img::inset), r, 20);
        return;
    }
    g.setGradientFill (juce::ColourGradient::vertical (hue (0xff1c1a1f), r.getY(), hue (0xff111014), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::black.withAlpha (0.6f), r.getY(), juce::Colours::transparentBlack, r.getY() + 6.0f));
    g.fillRoundedRectangle (r, radius);
}

void drawLed (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float textHeight,
              juce::Justification just, juce::Colour textColour)
{
    if (isXp())
    {
        // a plain text field: white (or dark) box, blue-grey border, no glow
        g.setColour (ledBg);
        g.fillRect (r);
        g.setColour (edge);
        g.drawRect (r, 1.0f);
        if (text.isNotEmpty())
        {
            g.setColour (textColour == red ? ledText : textColour);
            g.setFont (font (textHeight, 500));
            g.drawFittedText (text, r.reduced (5.0f, 0.0f).toNearestInt(), just, 1, 0.7f);
        }
        return;
    }
    if (hasImage (Img::display))
    {
        drawSliced (g, image (Img::display), r, 12);
        if (text.isNotEmpty())
            drawGlowText (g, text, r.reduced (6.0f, 0.0f), font (textHeight, 500, 0.06f), textColour == red ? ledText : textColour,
                          (textColour == red ? ledText : textColour).withAlpha (0.55f), 6, just);
        return;
    }
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
    {
        const auto tc = textColour == red ? ledText : textColour;
        drawGlowText (g, text, r.reduced (6.0f, 0.0f), font (textHeight, 500, 0.06f), tc, tc.withAlpha (0.55f), 6, just);
    }
}

void drawGlowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                   juce::Colour colour, juce::Colour glow, int glowRadius, juce::Justification just)
{
    if (isXp())
    {
        // no glow; white text means "strong text" (black on the light skin)
        g.setColour (colour == juce::Colours::white ? textStrong : colour);
        g.setFont (f);
        g.drawFittedText (text, r.toNearestInt(), just, 1, 0.7f);
        return;
    }
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
    if (isXp())
    {
        // bold blue title in normal case, a fading rule underneath (like a task pane header)
        const bool light = currentSkin == Skin::xpLight;
        const auto titleCol = light ? juce::Colour (0xff0b47c4) : juce::Colour (0xff8db6ff);
        const auto f = font (11.0f, 700);
        g.setColour (titleCol);
        g.setFont (f);
        g.drawText (title, row, juce::Justification::centredLeft, false);
        if (subtitle.isNotEmpty())
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (f, title, 0, 0);
            const auto w = ga.getBoundingBox (0, -1, true).getWidth();
            g.setColour (textMuted);
            g.setFont (font (9.5f, 500));
            g.drawText (subtitle, row.withTrimmedLeft (w + 10.0f), juce::Justification::centredLeft, true);
        }
        g.setGradientFill (juce::ColourGradient::horizontal (titleCol.withAlpha (0.55f), row.getX(), titleCol.withAlpha (0.0f), row.getRight()));
        g.fillRect (row.getX(), row.getBottom() + 2.0f, row.getWidth(), 1.0f);
        inner.removeFromTop (6.0f);
        return inner;
    }
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

void drawBackground (juce::Graphics& g, juce::Rectangle<int> bounds, float headerBottom)
{
    if (! isXp())
    {
        g.fillAll (juce::Colours::black);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (image (Img::background), bounds.toFloat(),
                     juce::RectanglePlacement (juce::RectanglePlacement::xMid | juce::RectanglePlacement::yMid
                                               | juce::RectanglePlacement::fillDestination));
        return;
    }
    const bool light = currentSkin == Skin::xpLight;
    g.setColour (windowBg);
    g.fillRect (bounds);
    // the title band across the top, glossy blue
    const auto band = bounds.toFloat().withHeight (headerBottom + 8.0f);
    juce::ColourGradient grad (light ? juce::Colour (0xff0997ff) : juce::Colour (0xff3b5a8c), 0.0f, band.getY(),
                               light ? juce::Colour (0xff003dd7) : juce::Colour (0xff0d1830), 0.0f, band.getBottom(), false);
    grad.addColour (0.08, light ? juce::Colour (0xff0053ee) : juce::Colour (0xff22385e));
    grad.addColour (0.45, light ? juce::Colour (0xff0050ee) : juce::Colour (0xff1a2c4d));
    grad.addColour (0.88, light ? juce::Colour (0xff0066ff) : juce::Colour (0xff20365c));
    g.setGradientFill (grad);
    g.fillRect (band);
    g.setColour (juce::Colours::black.withAlpha (light ? 0.25f : 0.5f));
    g.fillRect (band.getX(), band.getBottom(), band.getWidth(), 1.0f);
}

void drawFrame (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (! isXp())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto frameImg = image (Img::frame);
        if (frameImg.isValid())   // the frame art is 1920 wide; its corner size scales with it
            drawNineSlice (g, frameImg, bounds, juce::jmax (8, juce::roundToInt (120.0f * (float) frameImg.getWidth() / 1920.0f)), 30);
        return;
    }
    // a window border: blue (light) or deep navy (dark), lighter on the inside edge
    const bool light = currentSkin == Skin::xpLight;
    auto r = bounds.toFloat();
    g.setColour (light ? juce::Colour (0xff0831d9) : juce::Colour (0xff0b1426));
    g.drawRect (r, 4.0f);
    g.setColour (light ? juce::Colour (0xff166aee) : juce::Colour (0xff22385e));
    g.drawRect (r.reduced (4.0f), 1.0f);
}

void drawNineSlice (juce::Graphics& g, const juce::Image& img, juce::Rectangle<int> d, int s, int b, bool fill)
{
    const int W = img.getWidth(), H = img.getHeight();
    const int dw = d.getWidth(), dh = d.getHeight(), x = d.getX(), y = d.getY();
    auto blit = [&] (int dx, int dy, int dW, int dH, int sx, int sy, int sW, int sH)
    {
        if (dW > 0 && dH > 0)
            g.drawImage (img, dx, dy, dW, dH, sx, sy, sW, sH, fill);
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
