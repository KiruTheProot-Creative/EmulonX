// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PreviewView.h"
#include "ui/SkinDraw.h"
#include "ui/Theme.h"
#include "EmxAssets.h"

namespace emx::sd
{
using namespace juce;

const std::vector<Part>& allParts()
{
    static const std::vector<Part> parts {
        { "window", "Background", "WINDOW" }, { "frame", "Window frame", "WINDOW" }, { "topbar", "Top bar & logo", "WINDOW" },
        { "panel", "Panels", "SURFACES" }, { "heading", "Panel titles", "SURFACES" }, { "inset", "Knob areas", "SURFACES" },
        { "display", "Displays", "READOUTS" }, { "list", "List text", "READOUTS" },
        { "button", "Buttons", "CONTROLS" }, { "buttonOn", "Buttons (on)", "CONTROLS" }, { "tab", "Tabs", "CONTROLS" },
        { "knob", "Knobs", "CONTROLS" }, { "fader", "Faders", "CONTROLS" }, { "label", "Knob captions", "CONTROLS" },
        { "type", "Font", "TYPE" } };
    return parts;
}

String partTitle (const String& key)
{
    for (auto& p : allParts())
        if (key == p.key)
            return p.title;
    return key;
}

//== Fonts =========================================================================
namespace
{
struct Fonts
{
    Typeface::Ptr saira[3], fredoka[2];
    std::map<size_t, std::pair<Typeface::Ptr, float>> files;
    std::map<String, float> installed;
    float fredokaScale = 0.0f;

    static Fonts& get() { static Fonts f; return f; }

    Typeface::Ptr sairaFor (int weight)
    {
        const int i = weight >= 900 ? 2 : weight >= 700 ? 1 : 0;
        if (saira[i] == nullptr)
        {
            const char* data[] { EmxAssets::Saira500_ttf, EmxAssets::Saira700_ttf, EmxAssets::Saira900_ttf };
            const int sizes[] { EmxAssets::Saira500_ttfSize, EmxAssets::Saira700_ttfSize, EmxAssets::Saira900_ttfSize };
            saira[i] = Typeface::createSystemTypefaceFor (data[i], (size_t) sizes[i]);
        }
        return saira[i];
    }

    static float cap (const Font& f)
    {
        GlyphArrangement ga;
        ga.addLineOfText (f, "H", 0, 0);
        Path p;
        ga.createPath (p);
        return p.getBounds().getHeight();
    }

    /** Scale that gives a font the same capital height as Saira in Emulon X. */
    float matchScale (const Font& at100)
    {
        const float target = cap (Font (FontOptions (sairaFor (700)).withHeight (100.0f * 1.3137f)));
        const float mine = cap (at100);
        return mine > 1.0f ? jlimit (0.5f, 2.5f, target / mine) : 1.0f;
    }
};
} // namespace

void releaseFonts()
{
    auto& f = Fonts::get();
    for (auto& t : f.saira) t = nullptr;
    for (auto& t : f.fredoka) t = nullptr;
    f.files.clear();
}

Font skinFont (const skin::Design& d, float height, int weight, float kerning)
{
    auto& fonts = Fonts::get();
    auto kern = [kerning] (Font f, float amount) { return std::abs (kerning) > 1.0e-6f ? f.withExtraKerningFactor (kerning * amount) : f; };
    if (d.fontData.getSize() > 0)
    {
        const auto h = std::hash<std::string_view>{} (std::string_view (static_cast<const char*> (d.fontData.getData()), d.fontData.getSize()));
        auto it = fonts.files.find (h);
        if (it == fonts.files.end())
        {
            auto tf = Typeface::createSystemTypefaceFor (d.fontData.getData(), d.fontData.getSize());
            const float s = tf != nullptr ? fonts.matchScale (Font (FontOptions (tf).withHeight (100.0f))) : 1.0f;
            it = fonts.files.emplace (h, std::make_pair (tf, s)).first;
        }
        if (it->second.first != nullptr)
            return kern (Font (FontOptions (it->second.first).withHeight (height * it->second.second)), 0.5f);
    }
    const auto name = d.fontName.trim();
    if (name.equalsIgnoreCase ("Fredoka"))
    {
        if (fonts.fredoka[0] == nullptr)
        {
            fonts.fredoka[0] = Typeface::createSystemTypefaceFor (EmxAssets::Fredoka500_ttf, (size_t) EmxAssets::Fredoka500_ttfSize);
            fonts.fredoka[1] = Typeface::createSystemTypefaceFor (EmxAssets::Fredoka600_ttf, (size_t) EmxAssets::Fredoka600_ttfSize);
            fonts.fredokaScale = fonts.matchScale (Font (FontOptions (fonts.fredoka[1]).withHeight (100.0f)));
        }
        return kern (Font (FontOptions (fonts.fredoka[weight >= 700 ? 1 : 0]).withHeight (height * fonts.fredokaScale)), 0.4f);
    }
    if (name.equalsIgnoreCase ("Tahoma"))
    {
        static const bool hasTahoma = Font::findAllTypefaceNames().contains ("Tahoma");
        return Font (FontOptions (hasTahoma ? String ("Tahoma") : Font::getDefaultSansSerifFontName(), height * 1.02f, weight >= 700 ? Font::bold : Font::plain));
    }
    if (name.isNotEmpty() && ! name.equalsIgnoreCase ("Saira"))
    {
        auto it = fonts.installed.find (name);
        if (it == fonts.installed.end())
        {
            const bool exists = Font::findAllTypefaceNames().contains (name, true);
            it = fonts.installed.emplace (name, exists ? fonts.matchScale (Font (FontOptions (name, 100.0f, Font::bold))) : 0.0f).first;
        }
        if (it->second > 0.0f)
            return kern (Font (FontOptions (name, height * it->second, weight >= 700 ? Font::bold : Font::plain)), 0.5f);
    }
    return kern (Font (FontOptions (fonts.sairaFor (weight)).withHeight (height * 1.3137f)), 1.0f);
}

//== Preview =======================================================================
PreviewView::PreviewView (SkinDocument& d) : doc (d)
{
    doc.addChangeListener (this);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

PreviewView::~PreviewView() { doc.removeChangeListener (this); }

AffineTransform PreviewView::toScreen() const
{
    const auto area = getLocalBounds().toFloat().reduced (12.0f);
    const float s = jmin (area.getWidth() / (float) logicalWidth, area.getHeight() / (float) logicalHeight);
    const float x = area.getCentreX() - logicalWidth * s * 0.5f, y = area.getCentreY() - logicalHeight * s * 0.5f;
    return AffineTransform::scale (s).translated (x, y);
}

void PreviewView::paint (Graphics& g)
{
    // the stage: black, slightly sunken
    const auto t = toScreen();
    const auto stage = Rectangle<float> (0, 0, (float) logicalWidth, (float) logicalHeight).transformedBy (t).expanded (8.0f);
    g.setColour (Colours::black);
    g.fillRoundedRectangle (stage, 6.0f);
    const auto design = doc.design();
    {
        Graphics::ScopedSaveState state (g);
        g.addTransform (t);
        g.reduceClipRegion (Rectangle<int> (0, 0, logicalWidth, logicalHeight));
        drawPreview (g, *design);
        if (hovered.isNotEmpty() && hovered != selected)
            outline (g, hovered, false);
        outline (g, selected == "type" ? String ("heading") : selected, true);
    }
}

void PreviewView::outline (Graphics& g, const String& key, bool solid)
{
    for (auto& h : hits)
    {
        if (h.key != key || (key == "window" && solid))
            continue;
        const auto r = h.r.expanded (key == "frame" || key == "window" || key == "topbar" ? -2.0f : 2.0f);
        if (solid)
        {
            g.setColour (Colour (0xffff2a2a).withAlpha (0.55f));
            g.drawRoundedRectangle (r.expanded (2.0f), 5.0f, 4.0f);
            g.setColour (Colours::white);
            g.drawRoundedRectangle (r, 4.0f, 1.6f);
        }
        else
        {
            Path p;
            p.addRoundedRectangle (r, 4.0f);
            Path dashed;
            const float dashes[] { 4.0f, 3.0f };
            PathStrokeType (1.4f).createDashedStroke (dashed, p, dashes, 2);
            g.setColour (Colours::white.withAlpha (0.75f));
            g.fillPath (dashed);
        }
    }
}

String PreviewView::partAt (Point<float> screenPos) const
{
    const auto p = screenPos.transformedBy (toScreen().inverted());
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (it->r.contains (p))
            return it->key;
    return {};
}

void PreviewView::mouseMove (const MouseEvent& e)
{
    const auto k = partAt (e.position);
    if (k != hovered)
    {
        hovered = k;
        setTooltip (k.isEmpty() ? String() : partTitle (k));
        repaint();
    }
}

void PreviewView::mouseExit (const MouseEvent&)
{
    hovered = {};
    repaint();
}

void PreviewView::mouseUp (const MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;
    const auto k = partAt (e.position);
    if (k.isNotEmpty() && onSelect)
        onSelect (k);
}

void PreviewView::drawPreview (Graphics& g, const skin::Design& d)
{
    hits.clear();
    auto hit = [this] (Rectangle<float> r, const char* key) { hits.push_back ({ r, key }); };
    auto up = [&d] (const String& s) { return d.upperCase ? s.toUpperCase() : s; };
    const auto all = Rectangle<float> (0, 0, (float) logicalWidth, (float) logicalHeight);
    const float topBottom = 50.0f;   // the top bar ends at 58

    skin::drawBackground (g, d, all.toNearestInt(), topBottom);
    hit (all, "window");
    hit (all.withHeight (topBottom + 8.0f), "topbar");

    // logo
    {
        const auto area = Rectangle<float> (22, 13, 136, 32);
        Image logo = d.logo.image;
        if (! logo.isValid())
        {
            const int glowKey = roundToInt (d.logoGlow * 100.0f);
            if (! logoCache.img.isValid() || logoCache.letters != d.logoColour || logoCache.accent != d.accent)
                logoCache = { d.logoColour, d.accent, glowKey,
                              theme::tintedLogo (ImageCache::getFromMemory (EmxAssets::logo_png, EmxAssets::logo_pngSize), d.logoColour, d.accent) };
            logo = logoCache.img;
        }
        if (logo.isValid())
        {
            const auto dest = RectanglePlacement (RectanglePlacement::xLeft | RectanglePlacement::yMid).appliedTo (logo.getBounds().toFloat(), area);
            if (d.logoGlow > 0.001f)
            {
                Image shape (Image::ARGB, roundToInt (dest.getWidth() * 2.0f) + 40, roundToInt (dest.getHeight() * 2.0f) + 40, true);
                {
                    Graphics sg (shape);
                    sg.setImageResamplingQuality (Graphics::highResamplingQuality);
                    sg.drawImage (logo, Rectangle<float> (20.0f, 20.0f, dest.getWidth() * 2.0f, dest.getHeight() * 2.0f));
                }
                Image glow (Image::ARGB, shape.getWidth(), shape.getHeight(), true);
                {
                    Graphics gg (glow);
                    DropShadow (d.accent.withAlpha (0.55f * d.logoGlow), jmax (1, roundToInt (18.0f * d.logoGlow)), {}).drawForImage (gg, shape);
                }
                g.drawImage (glow, Rectangle<float> (dest.getX() - 10.0f, dest.getY() - 10.0f, dest.getWidth() + 20.0f, dest.getHeight() + 20.0f));
            }
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (logo, dest);
            hit (dest, "topbar");
        }
    }

    auto buttonText = [&] (Rectangle<float> r, const String& text, bool on, bool tab)
    {
        g.setColour (skin::buttonTextColour (d, on, tab, true));
        g.setFont (skinFont (d, 9.5f, 700, 0.08f));
        g.drawFittedText (up (text), r.reduced (4.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.7f);
    };
    auto button = [&] (Rectangle<float> r, const String& text, bool on)
    {
        skin::drawButton (g, d, r, on, false, hoverState && ! on, false, true);
        buttonText (r, text, on, false);
        hit (r, on ? "buttonOn" : "button");
    };
    auto display = [&] (Rectangle<float> r, const String& text, Justification just = Justification::centred)
    {
        skin::drawDisplay (g, d, r, text, skinFont (d, 9.0f, 500, 0.04f), just, d.displayText);
        hit (r, "display");
    };
    auto heading = [&] (Rectangle<float> panel, const String& title, const String& sub)
    {
        const auto f = skinFont (d, 10.0f, 700, 0.14f);
        const auto t = up (title);
        auto row = Rectangle<float> (panel.getX() + 12.0f, panel.getY() + 8.0f, panel.getWidth() - 24.0f, 16.0f);
        g.setColour (d.heading);
        g.setFont (f);
        g.drawText (t, row, Justification::centredLeft, false);
        GlyphArrangement ga;
        ga.addLineOfText (f, t, 0, 0);
        const float w = ga.getBoundingBox (0, -1, true).getWidth();
        hit (row.withWidth (w + 4.0f), "heading");
        if (sub.isNotEmpty())
        {
            g.setColour (d.muted);
            g.setFont (skinFont (d, 8.0f, 500, 0.1f));
            g.drawText (up (sub), row.withTrimmedLeft (w + 8.0f), Justification::centredLeft, true);
        }
    };
    auto caption = [&] (Rectangle<float> r, const String& text)
    {
        g.setColour (d.label);
        g.setFont (skinFont (d, 8.0f, 700, 0.1f));
        g.drawFittedText (up (text), r.toNearestInt(), Justification::centred, 1, 0.7f);
        hit (r, "label");
    };

    // tabs
    {
        const char* names[] { "Main", "Editor", "FX", "Arp" };
        for (int i = 0; i < 4; ++i)
        {
            const auto r = Rectangle<float> (172.0f + (float) i * 80.0f, 16, 72, 26);
            skin::drawButton (g, d, r, i == 0, true, hoverState && i == 1, false, true);
            buttonText (r, names[i], i == 0, true);
            hit (r, "tab");
        }
    }

    // volume with its meter, voices, settings
    {
        const auto vol = Rectangle<float> (510, 17, 214, 24);
        skin::drawDisplay (g, d, vol, {}, skinFont (d, 9.0f), Justification::centred, d.displayText);
        const auto inner = vol.reduced (6.0f, 6.0f);
        for (int i = 0; i < 2; ++i)
        {
            const auto bar = Rectangle<float> (inner.getX(), inner.getY() + (float) i * 7.0f, inner.getWidth() * (i == 0 ? 0.72f : 0.66f), 5.0f);
            g.setGradientFill (ColourGradient::horizontal (d.faderFill.darker (0.8f), bar.getX(), d.accent, bar.getRight()));
            g.fillRoundedRectangle (bar, 2.0f);
        }
        g.setColour (d.thumbTop);
        g.fillRoundedRectangle (Rectangle<float> (vol.getX() + vol.getWidth() * 0.64f, vol.getY() - 3.0f, 5.0f, vol.getHeight() + 6.0f), 2.0f);
        hit (vol, "fader");
    }
    display ({ 732, 17, 86, 24 }, "VOICES 3");
    {
        const auto r = Rectangle<float> (826, 16, 34, 26);
        skin::drawButton (g, d, r, false, false, hoverState, false, true);
        const auto c = r.getCentre();
        Path cog;   // a small gear
        for (int i = 0; i < 8; ++i)
        {
            Path tooth;
            tooth.addRectangle (-1.6f, -7.2f, 3.2f, 3.6f);
            tooth.applyTransform (AffineTransform::rotation (MathConstants<float>::twoPi * (float) i / 8.0f));
            cog.addPath (tooth);
        }
        cog.addEllipse (-4.8f, -4.8f, 9.6f, 9.6f);
        cog.applyTransform (AffineTransform::translation (c.x, c.y));
        g.setColour (skin::buttonTextColour (d, false, false, true));
        g.fillPath (cog);
        g.setColour (d.button.fill.average());
        g.fillEllipse (c.x - 2.2f, c.y - 2.2f, 4.4f, 4.4f);
        hit (r, "button");
    }

    // explorer
    {
        const auto p = Rectangle<float> (16, 72, 196, 450);
        skin::drawPanel (g, d, p);
        hit (p, "panel");
        heading (p, "Explorer", {});
        button ({ p.getX() + 10, p.getY() + 34, 84, 24 }, "Samples", false);
        button ({ p.getX() + 102, p.getY() + 34, 84, 24 }, "Banks", true);
        const auto l = Rectangle<float> (p.getX() + 10, p.getY() + 68, 176, 372);
        skin::drawDisplay (g, d, l, {}, skinFont (d, 9.0f), Justification::centred, d.displayText);
        float y = l.getY() + 10.0f;
        auto cat = [&] (const String& s)
        {
            g.setColour (d.heading);
            g.setFont (skinFont (d, 8.0f, 700, 0.1f));
            g.drawText (up (s), Rectangle<float> (l.getX() + 10, y, l.getWidth() - 20, 12), Justification::centredLeft, false);
            g.setColour (d.accent);
            g.fillRect (l.getX() + 10, y + 13.0f, l.getWidth() - 20, 1.0f);
            y += 20.0f;
        };
        auto item = [&] (const String& s)
        {
            g.setColour (d.list);
            g.setFont (skinFont (d, 9.0f, 500, 0.03f));
            g.drawText (s, Rectangle<float> (l.getX() + 10, y, l.getWidth() - 20, 13), Justification::centredLeft, true);
            y += 18.0f;
        };
        cat ("Base banks");
        for (auto s : { "EMX - A001 - BASS 001", "EMX - B001 - KEYS 001", "EMX - B001 - KEYS 002" }) item (s);
        y += 4.0f;
        cat ("User");
        for (auto s : { "Glass Pad", "Dusty Choir", "Tape Strings" }) item (s);
        hit (l, "list");
    }

    // preset
    {
        const auto p = Rectangle<float> (224, 72, 640, 178);
        skin::drawPanel (g, d, p);
        hit (p, "panel");
        heading (p, "Preset", "3 zones - 4.6 MB");
        display ({ p.getX() + 150, p.getY() + 7, 200, 20 }, "Glass Pad");
        const auto l = Rectangle<float> (p.getX() + 12, p.getY() + 36, 520, 100);
        skin::drawDisplay (g, d, l, {}, skinFont (d, 9.0f), Justification::centred, d.displayText);
        const char* rows[][2] { { "Glass Pad_C3", "C-1 - F#3   ROOT C3   LOOP" }, { "Glass Pad_C4", "G3 - F#4   ROOT C4   LOOP" }, { "Glass Pad_C5", "G4 - G9   ROOT C5" } };
        for (int i = 0; i < 3; ++i)
        {
            const auto row = Rectangle<float> (l.getX() + 10, l.getY() + 9 + (float) i * 18, l.getWidth() - 20, 13);
            g.setColour (d.list);
            g.setFont (skinFont (d, 9.0f, 500, 0.03f));
            g.drawText (rows[i][0], row, Justification::centredLeft, true);
            g.setColour (d.muted);
            g.setFont (skinFont (d, 7.5f, 500, 0.06f));
            g.drawText (rows[i][1], row, Justification::centredRight, true);
        }
        hit (l, "list");
        button ({ p.getX() + 544, p.getY() + 36, 84, 24 }, "Load...", false);
        button ({ p.getX() + 544, p.getY() + 66, 84, 24 }, "Save", false);
        button ({ p.getX() + 544, p.getY() + 96, 84, 24 }, "Save as...", false);
        button ({ p.getX() + 12, p.getY() + 146, 84, 24 }, "Auto map", false);
        button ({ p.getX() + 104, p.getY() + 146, 74, 24 }, "Linear", false);
        button ({ p.getX() + 186, p.getY() + 146, 84, 24 }, "Sinc HQ", true);
        button ({ p.getX() + 278, p.getY() + 146, 96, 24 }, "Keep speed", false);
    }

    // macros
    {
        const auto p = Rectangle<float> (224, 262, 418, 260);
        skin::drawPanel (g, d, p);
        hit (p, "panel");
        heading (p, "Macro controller", "automatable in your DAW");
        const auto in = Rectangle<float> (p.getX() + 12, p.getY() + 34, 394, 214);
        skin::drawInset (g, d, in);
        hit (in, "inset");
        const float vals[] { 0.15f, 0.4f, 0.62f, 1.0f, 0.3f, 0.78f };
        const char* names[] { "Cutoff", "Space", "Drive", "Width", "Shimmer", "Glide" };
        for (int i = 0; i < 6; ++i)
        {
            const float kx = p.getX() + 30 + (float) (i % 3) * 126, ky = p.getY() + 46 + (float) (i / 3) * 100;
            const auto k = Rectangle<float> (kx + 26, ky, 44, 44);
            skin::drawKnob (g, d, k, -2.356f, 2.356f, vals[i]);
            hit (k, "knob");
            display ({ kx + 14, ky + 50, 68, 17 }, String (roundToInt (vals[i] * 100.0f)) + "%");
            caption ({ kx, ky + 72, 96, 12 }, names[i]);
        }
    }

    // envelope
    {
        const auto p = Rectangle<float> (654, 262, 210, 260);
        skin::drawPanel (g, d, p);
        hit (p, "panel");
        heading (p, "Envelope", {});
        const auto in = Rectangle<float> (p.getX() + 12, p.getY() + 34, 186, 214);
        skin::drawInset (g, d, in);
        hit (in, "inset");
        const float vals[] { 0.12f, 0.6f, 1.0f, 0.45f };
        const char* names[] { "Att", "Dec", "Sus", "Rel" };
        const char* readouts[] { "2 ms", "300", "100%", "250" };
        for (int i = 0; i < 4; ++i)
        {
            const float fx = p.getX() + 22 + (float) i * 44;
            const auto track = Rectangle<float> (fx, p.getY() + 46, 28, 120);
            skin::drawFader (g, d, track, track.getY() + 8.0f + (track.getHeight() - 16.0f) * (1.0f - vals[i]));
            hit (track, "fader");
            display ({ fx - 6, p.getY() + 174, 40, 17 }, readouts[i]);
            caption ({ fx - 6, p.getY() + 198, 40, 12 }, names[i]);
        }
    }

    skin::drawFrame (g, d, all.toNearestInt());
    const float fw = jmax (5.0f, d.frameWidth + 2.0f);
    hit (all.withWidth (fw), "frame");
    hit (all.withTrimmedLeft (all.getWidth() - fw), "frame");
    hit (all.withHeight (fw), "frame");
    hit (all.withTrimmedTop (all.getHeight() - fw), "frame");
}
} // namespace emx::sd
