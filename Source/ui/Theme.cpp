// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Theme.h"
#include "EmxAssets.h"
#include <thread>
#include <unordered_map>

namespace emx::theme
{
static juce::Typeface::Ptr typefaceFor (int weight)
{
    static auto w500 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Saira500_ttf, (size_t) EmxAssets::Saira500_ttfSize);
    static auto w700 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Saira700_ttf, (size_t) EmxAssets::Saira700_ttfSize);
    static auto w900 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Saira900_ttf, (size_t) EmxAssets::Saira900_ttfSize);
    return weight >= 900 ? w900 : weight >= 700 ? w700 : w500;
}

juce::Font font (float height, int weight, float kerning)
{
    if (getSkin() == Skin::emublox)
    {
        // Fredoka (SIL OFL): round and chunky, sized to the same capital height as Saira
        static auto f500 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Fredoka500_ttf, (size_t) EmxAssets::Fredoka500_ttfSize);
        static auto f600 = juce::Typeface::createSystemTypefaceFor (EmxAssets::Fredoka600_ttf, (size_t) EmxAssets::Fredoka600_ttfSize);
        static const float scale = []
        {
            auto cap = [] (juce::Typeface::Ptr t)
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText (juce::Font (juce::FontOptions (t).withHeight (100.0f)), "H", 0, 0);
                juce::Path p;
                ga.createPath (p);
                return p.getBounds().getHeight();
            };
            const float c = cap (f600);
            return c > 1.0f ? cap (typefaceFor (700)) * 1.3137f / c : 1.0f;
        }();
        juce::Font f { juce::FontOptions (weight >= 700 ? f600 : f500).withHeight (height * scale) };
        return std::abs (kerning) > 1.0e-6f ? f.withExtraKerningFactor (kerning * 0.4f) : f;
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

const FlatStyle emurumStyle {
    juce::Colour (0xff13161b), juce::Colour (0xff0d0f13), juce::Colour (0xff20252d), juce::Colour (0xff2f3640),
    juce::Colour (0xff191d23), juce::Colour (0xff0f1216), juce::Colour (0xff2c333d), juce::Colour (0xffe6ebf0),
    juce::Colour (0xff8a95a4), juce::Colour (0xff33b5ff), juce::Colour (0xff9ad8ff), juce::Colour (0xff29303a),
    juce::Colour (0xff323a46), juce::Colour (0xff0e1115), juce::Colour (0xff3b4350), juce::Colour (0xff353d48),
    3.0f, 250.0f, 0.014f, 0.15f };
const FlatStyle emutalStyle {
    juce::Colour (0xff1d1f23), juce::Colour (0xff16181b), juce::Colour (0xff2a2d33), juce::Colour (0xff383c43),
    juce::Colour (0xff23262b), juce::Colour (0xff1a1c20), juce::Colour (0xff3a3e46), juce::Colour (0xffe8e8ea),
    juce::Colour (0xff9ba1aa), juce::Colour (0xffaa88ff), juce::Colour (0xffc9b8ff), juce::Colour (0xff363a41),
    juce::Colour (0xff42464e), juce::Colour (0xff24272c), juce::Colour (0xff2f3238), juce::Colour (0xff484c54),
    6.0f, 255.0f, 0.008f, 0.17f };

const FlatStyle emubloxStyle {
    juce::Colour (0xff7cc8ff), juce::Colour (0xffffffff), juce::Colour (0xffffffff), juce::Colour (0xffcdd5dc),
    juce::Colour (0xffeef2f6), juce::Colour (0xffffffff), juce::Colour (0xff232527), juce::Colour (0xff1b1d1f),
    juce::Colour (0xff59626c), juce::Colour (0xffe8261c), juce::Colour (0xff0b6fc4), juce::Colour (0xffffffff),
    juce::Colour (0xffe6f4ff), juce::Colour (0xff232527), juce::Colour (0xffffffff), juce::Colour (0xffd3dbe2),
    9.0f, 240.0f, 0.01f, 0.0f,
    true, 2.5f, 4.0f, juce::Colour (0xff232527), juce::Colour (0xff00b06f), juce::Colour (0xff7d8994) };

const FlatStyle emulatorStyle {
    juce::Colour (0xff4a545d), juce::Colour (0xff353e46), juce::Colour (0xffb4bcc3), juce::Colour (0xff59636c),
    juce::Colour (0xffa3abb3), juce::Colour (0xfff2f4f5), juce::Colour (0xff6d7780), juce::Colour (0xff15191c),
    juce::Colour (0xff3e474f), juce::Colour (0xff3fbcae), juce::Colour (0xff0b5f58), juce::Colour (0xffd3d9de),
    juce::Colour (0xffe2e7ea), juce::Colour (0xff4f5961), juce::Colour (0xff2aa598), juce::Colour (0xff6d7780),
    7.0f, 215.0f, 0.02f, 0.28f,
    true, 0.0f, 0.0f, {}, {}, {}, 0.1f };   // light translation: white lists with dark text, like the original

const FlatStyle& styleFor (Skin s)
{
    return s == Skin::emurum ? emurumStyle : s == Skin::emublox ? emubloxStyle : s == Skin::emulator ? emulatorStyle : emutalStyle;
}

/** Fine horizontal streaks, like brushed aluminium (made once, tiled over panels). */
const juce::Image& brushedMetal()
{
    static const juce::Image img = []
    {
        juce::Image im (juce::Image::ARGB, 512, 256, true);
        juce::Random rnd (0x5eed);
        juce::Image::BitmapData d (im, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < im.getHeight(); ++y)
        {
            const float rowBase = rnd.nextFloat() * 2.0f - 1.0f;
            float streak = 0.0f;
            for (int x = 0; x < im.getWidth(); ++x)
            {
                streak = streak * 0.97f + (rnd.nextFloat() * 2.0f - 1.0f) * 0.03f;   // long, soft streaks
                const float v = juce::jlimit (-1.0f, 1.0f, rowBase * 0.6f + streak * 6.0f);
                const auto a = (juce::uint8) juce::jlimit (0, 255, (int) (std::abs (v) * 70.0f));
                d.setPixelColour (x, y, (v > 0 ? juce::Colours::white : juce::Colours::black).withAlpha (a));
            }
        }
        return im;
    }();
    return img;
}

float labHueOf (juce::Colour c)
{
    const auto l = linearToLab (toLinear (c.getFloatRed()), toLinear (c.getFloatGreen()), toLinear (c.getFloatBlue()));
    return juce::radiansToDegrees (std::atan2 (l.b, l.a));
}

/** How far the flat skin's accent (plus the colour shift) is from the classic red. */
float flatOffset()
{
    return labHueOf (styleFor (currentSkin).accent) - labHueOf (juce::Colour (0xffff2a2a)) + currentShift;
}

/** Translates a colour of the red design into a flat skin: reds become the accent colour,
    greys become the skin's cool greys; orange / yellow / green keep their hue (plus the shift). */
juce::Colour flatMap (juce::Colour c)
{
    const auto& st = styleFor (currentSkin);
    auto lab = linearToLab (toLinear (c.getFloatRed()), toLinear (c.getFloatGreen()), toLinear (c.getFloatBlue()));
    const float chroma = std::hypot (lab.a, lab.b);
    float h = juce::radiansToDegrees (std::atan2 (lab.b, lab.a)), L = lab.L, C = chroma;
    if (chroma < 0.035f)
    {
        if (st.light)   // dark backgrounds become light, light text becomes dark
            L = L < 0.5f ? 0.985f - L * 0.32f : 0.10f + (1.0f - L) * 0.85f;
        else
            L = st.neutralLift + L * (1.0f - st.neutralLift) * 0.97f;
        h = st.neutralHue;
        C = st.neutralChroma;
    }
    else
    {
        const bool keep = h > 45.0f && h < 150.0f;
        h += keep ? currentShift : flatOffset();
        C = juce::jmin (0.22f, C);
        if (st.accentLift > 0.0f && L >= 0.45f)
            L = juce::jmin (0.9f, L + st.accentLift);
        if (st.light && L < 0.45f)   // dark red fills become light tints
            L = 0.94f - L * 0.5f;
        else if (st.light && L > 0.75f)   // pale tints were text on dark fills: make them dark
            L = 0.28f + (1.0f - L) * 0.5f;
    }
    float rr, gg, bb;
    const float rad = juce::degreesToRadians (h);
    labToLinear ({ L, C * std::cos (rad), C * std::sin (rad) }, rr, gg, bb);
    return juce::Colour::fromFloatRGBA (toSrgb (juce::jlimit (0.0f, 1.0f, rr)), toSrgb (juce::jlimit (0.0f, 1.0f, gg)),
                                        toSrgb (juce::jlimit (0.0f, 1.0f, bb)), c.getFloatAlpha());
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
    if (auto it = hueCache.find (argb); it != hueCache.end())
        return juce::Colour (it->second);
    const auto c = currentSkin == Skin::classic ? rotateHue (juce::Colour (argb), currentShift)
                 : isFlat()                     ? flatMap (juce::Colour (argb))
                                                 : xpMap (juce::Colour (argb), currentSkin == Skin::xpLight);
    hueCache[argb] = c.getARGB();
    return c;
}

Skin getSkin() noexcept { return currentSkin; }

const FlatStyle& flat() noexcept { return styleFor (currentSkin); }

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
        case Skin::emurum:
        case Skin::emutal:
        case Skin::emublox:
        case Skin::emulator:
        {
            const auto& st = flat();
            red = rotateHue (st.accent, currentShift);
            redDim = red.brighter (0.12f);
            redDark = red.interpolatedWith (st.panel, 0.65f);
            textCol = st.text;  textMuted = st.muted;  listText = rotateHue (st.list, currentShift);
            ledBg = st.display;  edge = st.displayEdge;  highlight = st.panelEdge;
            textStrong = st.text;  ledText = st.text;  windowBg = st.light ? st.panel : st.bg;
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

juce::Image image (Img which)
{
    juce::Image src;
    switch (which)
    {
        case Img::background: src = juce::ImageCache::getFromMemory (EmxAssets::background_jpg, EmxAssets::background_jpgSize); break;
        case Img::frame:      src = juce::ImageCache::getFromMemory (EmxAssets::frame_png, EmxAssets::frame_pngSize); break;
        case Img::knob:       src = juce::ImageCache::getFromMemory (EmxAssets::knob_png, EmxAssets::knob_pngSize); break;
        case Img::slider:     src = juce::ImageCache::getFromMemory (EmxAssets::slider_png, EmxAssets::slider_pngSize); break;
        case Img::logo:
            src = currentSkin == Skin::emublox ? juce::ImageCache::getFromMemory (EmxAssets::logoblox_png, EmxAssets::logoblox_pngSize)
                                               : juce::ImageCache::getFromMemory (EmxAssets::logo_png, EmxAssets::logo_pngSize);
            break;
    }
    if (isXp() || ! src.isValid())
        return src;
    const float deg = isFlat() && currentSkin != Skin::emublox ? flatOffset() : currentShift;   // EMUBLOX's logo is already red
    if (std::abs (deg) < 0.01f)
        return src;

    struct Tinted { float shift = 0.0f; juce::Image img; };
    static Tinted tinted[5], tintedBlox;   // the EMUBLOX logo is a different picture
    auto& t = (currentSkin == Skin::emublox && which == Img::logo) ? tintedBlox : tinted[(int) which];
    if (! t.img.isValid() || std::abs (t.shift - deg) > 0.01f)
        t = { deg, recolour (src, deg) };
    return t.img;
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
    struct Cache { int w = 0, h = 0; float shift = 1000.0f; Skin skin = Skin::classic; juce::Image img; };
    static Cache cache;
    if (cache.w != w || cache.h != h || std::abs (cache.shift - currentShift) > 0.01f || cache.skin != currentSkin)
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
            if (currentSkin == Skin::classic)
                juce::DropShadow (red.withAlpha (0.45f), juce::roundToInt (dest.getHeight() * 0.3f * px), {}).drawForImage (cg, shape);
            else   // a crisp drop shadow, like desktop icon labels
                juce::DropShadow (juce::Colours::black.withAlpha (0.55f), juce::roundToInt (2.0f * px), { 1, 1 }).drawForImage (cg, shape);
            cg.setOpacity (1.0f);   // the shadow leaves its own colour (45 %) set
            cg.drawImageAt (shape, 0, 0);
        }
        cache = { w, h, currentShift, currentSkin, out };
    }
    juce::Graphics::ScopedSaveState state (g);
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (cache.img, dest.expanded (pad));
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    if (isMetal())
    {
        // brushed aluminium with a light top edge and a dark outline
        juce::Path p;
        p.addRoundedRectangle (r, 8.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 6, { 0, 2 }).drawForPath (g, p);
        juce::ColourGradient grad (juce::Colour (0xffc9d0d6), 0.0f, r.getY(), juce::Colour (0xff98a1a9), 0.0f, r.getBottom(), false);
        grad.addColour (0.5, juce::Colour (0xffb3bbc2));
        g.setGradientFill (grad);
        g.fillPath (p);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (p);
            g.setTiledImageFill (brushedMetal(), 0, 0, 0.35f);
            g.fillPath (p);
        }
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawLine (r.getX() + 8.0f, r.getY() + 1.5f, r.getRight() - 8.0f, r.getY() + 1.5f, 1.0f);
        g.setColour (juce::Colour (0xff4f5961));
        g.strokePath (p, juce::PathStrokeType (1.2f));
        return;
    }
    if (isFlat())
    {
        const auto& st = flat();
        if (st.outline > 0.0f)
        {
            // a toy brick: cartoon shadow, white face, thick outline
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRoundedRectangle (r.translated (0.0f, st.shadow), st.radius);
            g.setColour (st.panel);
            g.fillRoundedRectangle (r, st.radius);
            g.setColour (st.outlineColour);
            g.drawRoundedRectangle (r.reduced (st.outline * 0.5f), st.radius, st.outline);
            return;
        }
        g.setColour (st.panel);
        g.fillRoundedRectangle (r, st.radius);
        g.setColour (st.panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), st.radius, 1.0f);
        return;
    }
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
    if (isMetal())
    {
        // a slightly darker recess in the metal
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff8f98a0), r.getY(), juce::Colour (0xffaab2b9), r.getBottom()));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colour (0xff5f6971));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawLine (r.getX() + 5.0f, r.getBottom() - 0.5f, r.getRight() - 5.0f, r.getBottom() - 0.5f, 1.0f);
        return;
    }
    if (isFlat())
    {
        g.setColour (flat().inset);
        g.fillRoundedRectangle (r, juce::jmax (2.0f, flat().radius - 1.0f));
        if (flat().light)
        {
            g.setColour (flat().panelEdge);
            g.drawRoundedRectangle (r.reduced (1.0f), juce::jmax (2.0f, flat().radius - 1.0f), 2.0f);
        }
        return;
    }
    if (isXp())
    {
        const bool light = currentSkin == Skin::xpLight;
        g.setColour (light ? juce::Colour (0xffece9d8) : juce::Colour (0xff222222));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (light ? juce::Colour (0xffd6d3c2) : juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
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
    if (isMetal())
    {
        // a light value field with a thin dark border, black text
        g.setColour (juce::Colour (0xfff2f4f5));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (juce::Colour (0xff59636c));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRect (r.getX() + 1.0f, r.getY() + 1.0f, r.getWidth() - 2.0f, 1.0f);
        if (text.isNotEmpty())
        {
            g.setColour (textColour == red ? ledText : textColour);
            g.setFont (font (textHeight, 500, 0.02f));
            g.drawFittedText (text, r.reduced (5.0f, 0.0f).toNearestInt(), just, 1, 0.7f);
        }
        return;
    }
    if (isFlat())
    {
        // a flat value field: dark, thin edge, plain text
        const auto& st = flat();
        const float rad = juce::jmin (st.radius, st.light ? 6.0f : 4.0f);
        const float edgeW = st.outline > 0.0f ? 2.0f : 1.0f;
        g.setColour (st.display);
        g.fillRoundedRectangle (r, rad);
        g.setColour (st.displayEdge);
        g.drawRoundedRectangle (r.reduced (edgeW * 0.5f), rad, edgeW);
        if (text.isNotEmpty())
        {
            g.setColour (textColour == red ? ledText : textColour);
            g.setFont (font (textHeight, 500, 0.03f));
            g.drawFittedText (text, r.reduced (5.0f, 0.0f).toNearestInt(), just, 1, 0.7f);
        }
        return;
    }
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
    if (isFlat())
    {
        g.setColour (colour == juce::Colours::white ? textStrong : colour);
        g.setFont (f);
        g.drawFittedText (text, r.toNearestInt(), just, 1, 0.7f);
        juce::ignoreUnused (glow, glowRadius);
        return;
    }
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
    if (isFlat())
    {
        // small caps title; EMURUM marks it with a little accent light, EMUBLOX makes it big and bold
        const bool emurum = currentSkin == Skin::emurum;
        if (isMetal())
        {
            // dark engraved label, like on hardware
            const auto f = font (9.5f, 700, 0.08f);
            const auto up = title.toUpperCase();
            g.setFont (f);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawText (up, row.translated (0.0f, 1.0f), juce::Justification::centredLeft, false);
            g.setColour (juce::Colour (0xff1b2126));
            g.drawText (up, row, juce::Justification::centredLeft, false);
            if (subtitle.isNotEmpty())
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText (f, up, 0, 0);
                const auto w = ga.getBoundingBox (0, -1, true).getWidth();
                g.setColour (textMuted);
                g.setFont (font (8.0f, 500, 0.05f));
                g.drawText (subtitle.toUpperCase(), row.withTrimmedLeft (w + 8.0f), juce::Justification::centredLeft, true);
            }
            inner.removeFromTop (6.0f);
            return inner;
        }
        if (currentSkin == Skin::emublox)
        {
            const auto f = font (11.5f, 700, 0.04f);
            g.setColour (textStrong);
            g.setFont (f);
            const auto up = title.toUpperCase();
            g.drawText (up, row, juce::Justification::centredLeft, false);
            if (subtitle.isNotEmpty())
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText (f, up, 0, 0);
                const auto w = ga.getBoundingBox (0, -1, true).getWidth();
                g.setColour (textMuted);
                g.setFont (font (8.5f, 500, 0.02f));
                g.drawText (subtitle, row.withTrimmedLeft (w + 8.0f), juce::Justification::centredLeft, true);
            }
            inner.removeFromTop (6.0f);
            return inner;
        }
        auto t = row;
        if (emurum)
        {
            g.setColour (red);
            g.fillRoundedRectangle (t.removeFromLeft (6.0f).withSizeKeepingCentre (5.0f, 5.0f), 1.0f);
            t.removeFromLeft (6.0f);
        }
        const auto f = font (9.5f, 700, 0.1f);
        g.setColour (emurum ? textStrong : textMuted);
        g.setFont (f);
        const auto up = title.toUpperCase();
        g.drawText (up, t, juce::Justification::centredLeft, false);
        if (subtitle.isNotEmpty())
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (f, up, 0, 0);
            const auto w = ga.getBoundingBox (0, -1, true).getWidth();
            g.setColour (textMuted.withAlpha (0.8f));
            g.setFont (font (8.0f, 500, 0.06f));
            g.drawText (subtitle.toUpperCase(), t.withTrimmedLeft (w + 8.0f), juce::Justification::centredLeft, true);
        }
        inner.removeFromTop (6.0f);
        return inner;
    }
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
    if (isMetal())
    {
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff56616a), (float) bounds.getY(), juce::Colour (0xff3d464e), (float) bounds.getBottom()));
        g.fillRect (bounds);
        const auto bar = bounds.toFloat().withHeight (headerBottom + 8.0f);
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff4a555f), bar.getY(), juce::Colour (0xff2b333a), bar.getBottom()));
        g.fillRect (bar);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (bar.getX(), bar.getBottom(), bar.getWidth(), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (bar.getX(), bar.getBottom() + 1.0f, bar.getWidth(), 1.0f);
        return;
    }
    if (isFlat())
    {
        const auto& st = flat();
        if (st.light)
        {
            // a bright sky, a white bar with a thick line under it
            g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff5fb8ff), (float) bounds.getY(), juce::Colour (0xffc9ecff), (float) bounds.getBottom()));
            g.fillRect (bounds);
            const auto bar = bounds.toFloat().withHeight (headerBottom + 8.0f);
            g.setColour (st.topBar);
            g.fillRect (bar);
            g.setColour (st.outlineColour);
            g.fillRect (bar.getX(), bar.getBottom() - st.outline, bar.getWidth(), st.outline);
            return;
        }
        g.setColour (st.bg);
        g.fillRect (bounds);
        const auto bar = bounds.toFloat().withHeight (headerBottom + 8.0f);
        g.setColour (st.topBar);
        g.fillRect (bar);
        g.setColour (st.panelEdge);
        g.fillRect (bar.getX(), bar.getBottom(), bar.getWidth(), 1.0f);
        return;
    }
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
    if (isMetal())
    {
        g.setColour (juce::Colour (0xff20262b));
        g.drawRect (bounds.toFloat(), 2.0f);
        return;
    }
    if (isFlat())
    {
        g.setColour (flat().outline > 0.0f ? flat().outlineColour : flat().panelEdge);
        g.drawRect (bounds.toFloat(), juce::jmax (1.0f, flat().outline + 0.5f));
        return;
    }
    if (! isXp())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        drawNineSlice (g, image (Img::frame), bounds, 120, 30);
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

void drawScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (! isMetal())
    {
        drawLed (g, r, {});
        return;
    }
    // a teal LCD with a fine grid and a dark bezel
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff3c968d), r.getY(), juce::Colour (0xff2a7870), r.getBottom()));
    g.fillRect (r);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (float x = r.getX() + 12.0f; x < r.getRight(); x += 12.0f)
            g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        for (float y = r.getY() + 12.0f; y < r.getBottom(); y += 12.0f)
            g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::black.withAlpha (0.35f), r.getY(), juce::Colours::transparentBlack, r.getY() + 6.0f));
        g.fillRect (r);
    }
    g.setColour (juce::Colour (0xff1b2a2b));
    g.drawRect (r, 1.5f);
}

juce::Colour headerText() noexcept
{
    return isXp() || isMetal() ? juce::Colours::white.withAlpha (0.9f) : textMuted;
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
