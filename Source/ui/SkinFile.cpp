// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SkinFile.h"
#include "../NaturalSort.h"
#include <string_view>
#include <atomic>

namespace emx::skin
{
using namespace juce;

namespace
{
constexpr int64 maxEntryBytes = 32 * 1024 * 1024;     // one picture or font
constexpr int64 maxTotalBytes = 128 * 1024 * 1024;    // the whole skin, unpacked
constexpr int maxPictureSize = 2048;                  // bigger pictures are scaled down (memory)

/** Decoded pictures and prepared textures, kept for a while so a skin that is edited live
    (in Emulon X SD) doesn't decode its pictures again on every change. */
struct DecodeCache
{
    CriticalSection lock;
    std::map<size_t, Image> pictures;
    std::map<size_t, std::pair<Image, Image>> blends;
    static DecodeCache& get() { static DecodeCache c; return c; }
    static size_t hashOf (const MemoryBlock& m) { return std::hash<std::string_view>{} (std::string_view (static_cast<const char*> (m.getData()), m.getSize())); }
    template <typename Map> static void trim (Map& m) { if (m.size() > 48) m.clear(); }
};

var prop (const var& o, const char* key) { return o.getProperty (Identifier (key), var()); }

float num (const var& o, const char* key, float fallback, float lo, float hi)
{
    const auto v = prop (o, key);
    if (v.isDouble() || v.isInt() || v.isInt64() || v.isBool())
    {
        const auto d = (double) v;
        if (std::isfinite (d))
            return jlimit (lo, hi, (float) d);
    }
    return fallback;
}

bool flag (const var& o, const char* key, bool fallback)
{
    const auto v = prop (o, key);
    return v.isVoid() ? fallback : (bool) v;
}

String str (const var& o, const char* key, const String& fallback = {})
{
    const auto v = prop (o, key);
    return v.isString() ? v.toString() : fallback;
}

/** Everything the reader needs while going through one file. */
struct Reader
{
    const std::function<MemoryBlock (const String&)>& fileData;
    std::map<String, Colour> tokens;
    StringArray& warnings;

    Colour colour (const var& o, const char* key, Colour fallback) const
    {
        const auto s = str (o, key).trim();
        if (s.startsWithChar ('@'))
        {
            if (auto it = tokens.find (s.substring (1)); it != tokens.end())
                return it->second;
            return fallback;
        }
        Colour c;
        return parseColour (s, c) ? c : fallback;
    }

    /** Decodes a picture given as a file inside the skin or as a data: URL. */
    Picture picture (const String& ref, const String& what) const
    {
        Picture p;
        const auto r = ref.trim();
        if (r.isEmpty())
            return p;
        MemoryBlock data;
        if (r.startsWithIgnoreCase ("data:"))
        {
            const auto comma = r.indexOfChar (',');
            if (comma > 0 && r.substring (0, comma).containsIgnoreCase ("base64"))
            {
                MemoryOutputStream out (data, false);
                if (! Base64::convertFromBase64 (out, r.substring (comma + 1)))
                    data.reset();
            }
        }
        else
        {
            data = fileData (r);
            p.file = r;
        }
        if (data.getSize() == 0)
        {
            warnings.add ("Missing picture for " + what + (p.file.isNotEmpty() ? " (" + p.file + ")" : String()));
            return {};
        }
        auto& cache = DecodeCache::get();
        const auto hash = DecodeCache::hashOf (data);
        {
            const ScopedLock sl (cache.lock);
            if (auto it = cache.pictures.find (hash); it != cache.pictures.end())
            {
                p.image = it->second;
                return p;
            }
        }
        auto img = ImageFileFormat::loadFrom (data.getData(), data.getSize());
        if (! img.isValid())
        {
            warnings.add ("Can't read the picture for " + what + " (use PNG or JPG)");
            return {};
        }
        if (img.getWidth() > maxPictureSize || img.getHeight() > maxPictureSize)
        {
            const float s = (float) maxPictureSize / (float) jmax (img.getWidth(), img.getHeight());
            img = img.rescaled (jmax (1, roundToInt (img.getWidth() * s)), jmax (1, roundToInt (img.getHeight() * s)), Graphics::highResamplingQuality);
        }
        p.image = img.convertedToFormat (Image::ARGB);
        const ScopedLock sl (cache.lock);
        if (cache.pictures.size() > 48)
        {
            cache.pictures.clear();
            cache.blends.clear();   // keyed by picture, so they go too
        }
        cache.pictures[hash] = p.image;
        return p;
    }

    Fill fill (const var& o, Fill fallback, const String& what) const
    {
        if (! o.isObject())
            return fallback;
        Fill f;
        const auto type = str (o, "type", "solid");
        f.type = type == "gradient" ? Fill::Type::gradient : type == "image" ? Fill::Type::image : Fill::Type::solid;
        f.a = colour (o, "a", fallback.a);
        f.b = prop (o, "b").isVoid() ? f.a : colour (o, "b", f.a);
        f.angle = num (o, "angle", 180.0f, -3600.0f, 3600.0f);
        const auto fit = str (o, "fit", "stretch");
        f.fit = fit == "cover" ? Fill::Fit::cover : fit == "tile" ? Fill::Fit::tile : Fill::Fit::stretch;
        if (f.type == Fill::Type::image)
        {
            f.picture = picture (str (o, "img"), what);
            if (! f.picture.isValid())
                f.type = Fill::Type::solid;   // shows the base colour instead
        }
        return f;
    }

    Overlay overlay (const var& part, const String& what) const
    {
        Overlay ov;
        const auto o = prop (part, "overlay");
        if (! o.isObject())
            return ov;
        ov.name = str (o, "tex", "none");
        ov.strength = num (o, "strength", 0.6f, 0.0f, 1.0f);
        ov.softLight = str (o, "blend") == "soft-light";
        if (ov.name == "none" || ov.name.isEmpty())
            return ov;
        if (ov.name == "custom")
            ov.texture = picture (str (o, "custom"), what + " texture");
        else
        {
            // a named texture (like the metals in the designer) travels inside the skin file
            for (auto ext : { ".png", ".jpg", ".jpeg" })
            {
                const auto file = "textures/" + ov.name + ext;
                if (fileData (file).getSize() > 0)
                {
                    ov.texture = picture (file, what + " texture");
                    break;
                }
            }
            if (! ov.texture.isValid())
                warnings.add ("Missing texture \"" + ov.name + "\" for " + what);
        }
        if (ov.texture.isValid())
        {
            auto& cache = DecodeCache::get();
            const auto key = std::hash<void*>{} (ov.texture.image.getPixelData().get());
            const ScopedLock sl (cache.lock);
            if (auto it = cache.blends.find (key); it != cache.blends.end())
                std::tie (ov.lighter, ov.darker) = it->second;
            else
            {
                makeBlendImages (ov.texture.image, ov.lighter, ov.darker);
                DecodeCache::trim (cache.blends);
                cache.blends[key] = { ov.lighter, ov.darker };
            }
        }
        return ov;
    }

    static Effects effects (const var& part)
    {
        Effects e;
        const auto o = prop (part, "fx");
        if (! o.isObject())
            return e;
        e.blur = num (o, "blur", 0.0f, 0.0f, 24.0f);
        e.bloom = num (o, "bloom", 0.0f, 0.0f, 1.0f);
        e.bloomSize = num (o, "bloomSize", 8.0f, 1.0f, 48.0f);
        e.bloomThreshold = num (o, "bloomThreshold", 0.55f, 0.0f, 1.0f);
        return e;
    }

    Box box (const var& o, const Box& fallback, const String& what) const
    {
        Box b = fallback;
        if (! o.isObject())
            return b;
        b.fill = fill (prop (o, "fill"), fallback.fill, what);
        b.radius = num (o, "radius", fallback.radius, 0.0f, 64.0f);
        b.shape = num (o, "shape", 0.5f, 0.0f, 1.0f);
        b.outline = num (o, "outline", fallback.outline, 0.0f, 16.0f);
        b.outlineColour = colour (o, "outlineColor", fallback.outlineColour);
        b.shadowY = num (o, "shadowY", fallback.shadowY, -32.0f, 32.0f);
        b.shadowBlur = num (o, "shadowBlur", fallback.shadowBlur, 0.0f, 64.0f);
        b.shadowOpacity = num (o, "shadowOpacity", fallback.shadowOpacity, 0.0f, 1.0f);
        b.overlay = overlay (o, what);
        b.fx = effects (o);
        return b;
    }
};

Fill solidFill (Colour c) { Fill f; f.a = f.b = c; return f; }
Fill gradientFill (Colour a, Colour b) { Fill f; f.type = Fill::Type::gradient; f.a = a; f.b = b; return f; }
} // namespace

bool parseColour (const String& text, Colour& out)
{
    auto s = text.trim();
    if (s.startsWithChar ('#'))
        s = s.substring (1);
    if (! s.containsOnly ("0123456789abcdefABCDEF"))
        return false;
    if (s.length() == 3)
        s = String::charToString (s[0]) + s[0] + s[1] + s[1] + s[2] + s[2];
    if (s.length() == 6)
    {
        out = Colour ((uint32) (0xff000000u | (uint32) s.getHexValue32()));
        return true;
    }
    if (s.length() == 8)   // #rrggbbaa
    {
        const auto v = (uint32) s.getHexValue64();
        out = Colour ((uint8) (v >> 24), (uint8) (v >> 16), (uint8) (v >> 8), (uint8) v);
        return true;
    }
    return false;
}

void makeBlendImages (const Image& texture, Image& lighter, Image& darker, int maxSize)
{
    lighter = darker = {};
    if (! texture.isValid())
        return;
    auto src = texture;
    if (src.getWidth() > maxSize || src.getHeight() > maxSize)
    {
        const float s = (float) maxSize / (float) jmax (src.getWidth(), src.getHeight());
        src = src.rescaled (jmax (1, roundToInt (src.getWidth() * s)), jmax (1, roundToInt (src.getHeight() * s)), Graphics::highResamplingQuality);
    }
    src = src.convertedToFormat (Image::ARGB);
    lighter = Image (Image::ARGB, src.getWidth(), src.getHeight(), true);
    darker = Image (Image::ARGB, src.getWidth(), src.getHeight(), true);
    const Image::BitmapData in (src, Image::BitmapData::readOnly);
    Image::BitmapData dl (lighter, Image::BitmapData::writeOnly), dd (darker, Image::BitmapData::writeOnly);
    for (int y = 0; y < src.getHeight(); ++y)
        for (int x = 0; x < src.getWidth(); ++x)
        {
            const auto c = in.getPixelColour (x, y);
            // lighter than mid-grey brightens the surface, darker darkens it
            const float v = (0.2126f * c.getFloatRed() + 0.7152f * c.getFloatGreen() + 0.0722f * c.getFloatBlue()) - 0.5f;
            const float a = jlimit (0.0f, 1.0f, std::abs (v) * 2.0f) * c.getFloatAlpha();
            if (v > 0.0f)
                dl.setPixelColour (x, y, Colours::white.withAlpha (a));
            else
                dd.setPixelColour (x, y, Colours::black.withAlpha (a));
        }
}

bool fromJson (const var& json, const std::function<MemoryBlock (const String&)>& fileData, Design& out, String& error, StringArray* warningsOut)
{
    if (! json.isObject() || str (json, "format") != "emxd")
    {
        error = "This isn't an Emulon X skin";
        return false;
    }
    if (num (json, "version", 2.0f, 0.0f, 1000.0f) < 2.0f)
    {
        error = "This skin was made for an older Emulon X skin designer and can't be used";
        return false;
    }
    StringArray warnings;
    Reader rd { fileData, {}, warnings };
    Design d;
    static std::atomic<int> uids { 0 };
    d.uid = ++uids;
    d.name = str (json, "name", "Custom skin").trim().substring (0, 60);
    if (d.name.isEmpty())
        d.name = "Custom skin";
    d.author = str (json, "author").trim().substring (0, 60);
    d.hueShift = flag (json, "hueShift", true);

    const auto tokens = prop (json, "tokens");
    for (auto [key, target] : { std::pair<const char*, Colour*> { "accent", &d.accent }, { "text", &d.text }, { "muted", &d.muted },
                                { "surface", &d.surface }, { "background", &d.background }, { "display", &d.displayColour } })
    {
        *target = rd.colour (tokens, key, *target);
        rd.tokens[key] = *target;
    }

    d.fontName = str (json, "font", "Saira");
    d.upperCase = flag (json, "upper", true);
    d.fontFile = str (json, "fontFile");
    if (d.fontFile.isNotEmpty())
    {
        d.fontData = fileData (d.fontFile);
        if (d.fontData.getSize() == 0)
            warnings.add ("Missing font file " + d.fontFile);
    }

    // window
    const auto win = prop (json, "window");
    d.window = rd.fill (prop (win, "fill"), gradientFill (Colour (0xff3a0606), Colour (0xff050304)), "the background");
    const auto pattern = str (win, "texture", "none");
    d.pattern = pattern == "grid" ? Design::Pattern::grid : pattern == "noise" ? Design::Pattern::noise : Design::Pattern::none;
    d.patternColour = rd.colour (win, "textureColor", d.accent);
    d.patternOpacity = num (win, "textureOpacity", 0.12f, 0.0f, 1.0f);
    d.windowOverlay = rd.overlay (win, "the background");
    d.windowFx = Reader::effects (win);
    const auto frame = prop (json, "frame");
    d.frameWidth = num (frame, "width", 3.0f, 0.0f, 16.0f);
    d.frameColour = rd.colour (frame, "color", Colours::black);
    d.frameFx = Reader::effects (frame);

    const auto top = prop (json, "topbar");
    d.topBar = rd.fill (prop (top, "fill"), gradientFill (Colour (0xff1b1418), Colour (0xff0a0708)), "the top bar");
    d.topLine = num (top, "line", 1.0f, 0.0f, 8.0f);
    d.topLineColour = rd.colour (top, "lineColor", Colours::black);
    d.logoColour = rd.colour (top, "logoColor", Colours::white);
    d.logoGlow = num (top, "logoGlow", 0.8f, 0.0f, 1.0f);
    d.logo = rd.picture (str (top, "logo"), "the logo");
    d.topBarOverlay = rd.overlay (top, "the top bar");
    d.topBarFx = Reader::effects (top);

    // surfaces
    Box panel;
    panel.fill = gradientFill (Colour (0xff26232a), Colour (0xff0d0c10));
    panel.radius = 9.0f;
    panel.shadowY = 3.0f; panel.shadowBlur = 10.0f; panel.shadowOpacity = 0.7f;
    d.panel = rd.box (prop (json, "panel"), panel, "panels");
    d.heading = rd.colour (prop (json, "heading"), "color", d.text);
    d.headingFx = Reader::effects (prop (json, "heading"));

    Box inset;
    inset.fill = gradientFill (Colour (0xff1c1a1f), Colour (0xff111014));
    inset.radius = 8.0f; inset.outline = 0.0f;
    d.inset = rd.box (prop (json, "inset"), inset, "knob areas");
    d.insetDepth = num (prop (json, "inset"), "innerShadow", 0.6f, 0.0f, 1.0f);

    const auto disp = prop (json, "display");
    Box display;
    display.fill = solidFill (d.displayColour);
    display.radius = 6.0f;
    d.display = rd.box (disp, display, "displays");
    d.displayText = rd.colour (disp, "textColor", d.accent);
    d.displayGlow = num (disp, "glow", 0.55f, 0.0f, 1.0f);
    auto patternOf = [] (const String& p)
    {
        return p == "grid" ? Design::ScreenPattern::grid : p == "dots" ? Design::ScreenPattern::dots
             : p == "stripes" ? Design::ScreenPattern::stripes : p == "scanlines" ? Design::ScreenPattern::scanlines
             : Design::ScreenPattern::none;
    };
    d.scanlines = flag (disp, "scanlines", true);

    // screens (waveform and step-sequencer backgrounds): surface like the displays unless they have their own
    // look; pattern and glass are theirs alone (without settings they look like a display, as before)
    const auto scr = prop (json, "screen");
    d.screensLikeDisplays = flag (scr, "same", true);
    d.screen = d.screensLikeDisplays ? d.display : rd.box (scr, d.display, "screens");
    d.graphPattern = patternOf (str (scr, "pattern", d.scanlines ? "scanlines" : "none"));
    d.graphPatternStrength = num (scr, "patternStrength", 1.0f, 0.0f, 1.0f);
    d.graphPatternColour = rd.colour (scr, "patternColor", Colours::black);
    d.graphGlass = flag (scr, "glass", false);
    d.list = rd.colour (prop (json, "list"), "color", Colour (0xffff4a4a));

    // controls
    const auto btn = prop (json, "button");
    Box button;
    button.fill = gradientFill (Colour (0xff55515a), Colour (0xff1d1b20));
    button.radius = 5.0f;
    d.button = rd.box (btn, button, "buttons");
    d.buttonText = rd.colour (btn, "textColor", Colour (0xffdddddd));
    d.bevel = num (btn, "bevel", 0.5f, 0.0f, 1.0f);

    const auto on = prop (json, "buttonOn");
    Box buttonOn = button;
    buttonOn.fill = gradientFill (Colour (0xffc62828), Colour (0xff5a0808));
    d.buttonOn = rd.box (on, buttonOn, "buttons (on)");
    d.buttonOnText = rd.colour (on, "textColor", Colours::white);
    d.buttonOnGlow = num (on, "glow", 0.45f, 0.0f, 1.0f);

    const auto tab = prop (json, "tab");
    d.tabsLikeButtons = flag (tab, "same", true);
    if (! d.tabsLikeButtons)
    {
        // tabs have their own look: one shape, a fill for normal and one for the active tab
        d.tab = rd.box (tab, d.button, "tabs");
        d.tabOn = d.tab;
        d.tabOn.fill = rd.fill (prop (tab, "onFill"), d.buttonOn.fill, "the active tab");
        d.tabText = rd.colour (tab, "textColor", d.buttonText);
    }
    else
    {
        d.tab = d.button;
        d.tabOn = d.buttonOn;
        d.tabText = d.buttonText;
    }

    const auto knob = prop (json, "knob");
    const auto style = str (knob, "style", "cap");
    d.knobStyle = style == "ring" ? Design::KnobStyle::ring : style == "picture" ? Design::KnobStyle::picture : Design::KnobStyle::cap;
    d.capLight = rd.colour (knob, "capA", d.capLight);
    d.capDark = rd.colour (knob, "capB", d.capDark);
    d.ring = num (knob, "ring", 0.0f, 0.0f, 16.0f);
    d.ringTrack = rd.colour (knob, "ringTrack", d.ringTrack);
    d.ringValue = rd.colour (knob, "ringColor", d.accent);
    d.pointer = rd.colour (knob, "pointer", d.accent);
    d.pointerGlow = num (knob, "pointerGlow", 0.9f, 0.0f, 1.0f);
    if (d.knobStyle == Design::KnobStyle::picture)
    {
        const auto lower = prop (knob, "lower"), upper = prop (knob, "upper");
        d.knobLower = rd.picture (str (lower, "picture"), "the knob's lower layer");
        d.lowerTurns = flag (lower, "turns", false);
        // "picture" is the single turning picture of earlier skins
        d.knobUpper = rd.picture (upper.isObject() ? str (upper, "picture") : str (knob, "picture"), "the knob's upper layer");
        d.upperTurns = upper.isObject() ? flag (upper, "turns", true) : true;
        if (! d.knobLower.isValid() && ! d.knobUpper.isValid())
            d.knobStyle = Design::KnobStyle::cap;
    }
    d.knobOverlay = rd.overlay (knob, "knobs");
    d.knobFx = Reader::effects (knob);

    const auto fader = prop (json, "fader");
    d.faderTrack = rd.colour (fader, "track", d.faderTrack);
    d.faderFill = rd.colour (fader, "fill", d.accent);
    d.thumbTop = rd.colour (fader, "thumbA", d.thumbTop);
    d.thumbBottom = rd.colour (fader, "thumbB", d.thumbBottom);
    d.thumbPicture = rd.picture (str (fader, "thumbPic"), "the fader handle");
    d.faderOverlay = rd.overlay (fader, "faders");
    d.faderFx = Reader::effects (fader);

    d.label = rd.colour (prop (json, "label"), "color", d.text);

    out = std::move (d);
    if (warningsOut != nullptr)
        warningsOut->addArray (warnings);
    return true;
}

bool load (const File& file, Design& out, String& error, StringArray* warnings)
{
    if (! file.existsAsFile())
    {
        error = "The skin file is missing: " + file.getFullPathName();
        return false;
    }
    if (file.getSize() > maxTotalBytes)
    {
        error = "The skin file is too big";
        return false;
    }

    // A plain skin.json (what Skin Studio exports): pictures inside it, or next to it.
    if (file.hasFileExtension ("json"))
    {
        const auto json = JSON::parse (file.loadFileAsString());
        const auto dir = file.getParentDirectory();
        return fromJson (json, [dir] (const String& name)
                         {
                             MemoryBlock mb;
                             const auto f = dir.getChildFile (name);
                             if (f.isAChildOf (dir) && f.existsAsFile() && f.getSize() <= maxEntryBytes)
                                 f.loadFileAsData (mb);
                             return mb;
                         }, out, error, warnings);
    }

    ZipFile zip (file);
    std::map<String, int> index;
    int64 total = 0;
    for (int i = 0; i < zip.getNumEntries(); ++i)
        if (const auto* e = zip.getEntry (i); e != nullptr && ! e->isSymbolicLink)
        {
            index[e->filename.replaceCharacter ('\\', '/').toLowerCase()] = i;
            total += e->uncompressedSize;
        }
    if (index.empty())
    {
        // maybe it's the plain description after all
        const auto text = file.loadFileAsString().trimStart();
        if (text.startsWithChar ('{'))
            return fromJson (JSON::parse (text), [] (const String&) { return MemoryBlock(); }, out, error, warnings);
        error = "This isn't an Emulon X skin file";
        return false;
    }
    if (total > maxTotalBytes)
    {
        error = "The skin file is too big";
        return false;
    }
    auto read = [&zip, &index] (const String& name)
    {
        MemoryBlock mb;
        const auto it = index.find (name.trim().replaceCharacter ('\\', '/').toLowerCase());
        if (it == index.end())
            return mb;
        if (const auto* e = zip.getEntry (it->second); e == nullptr || e->uncompressedSize > maxEntryBytes)
            return mb;
        if (std::unique_ptr<InputStream> in (zip.createStreamForEntry (it->second)); in != nullptr)
            in->readIntoMemoryBlock (mb, maxEntryBytes);
        return mb;
    };
    const auto description = read ("skin.json");
    if (description.getSize() == 0)
    {
        error = "The skin file has no skin.json inside";
        return false;
    }
    return fromJson (JSON::parse (description.toString()), read, out, error, warnings);
}

bool readForEditing (const File& file, var& json, std::map<String, MemoryBlock>& files, String& error)
{
    files.clear();
    if (! file.existsAsFile() || file.getSize() > maxTotalBytes)
    {
        error = file.existsAsFile() ? "The skin file is too big" : "The skin file is missing";
        return false;
    }
    if (file.hasFileExtension ("json"))
        json = JSON::parse (file.loadFileAsString());
    else
    {
        ZipFile zip (file);
        int64 total = 0;
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* e = zip.getEntry (i);
            if (e == nullptr || e->isSymbolicLink || e->uncompressedSize > maxEntryBytes || e->filename.endsWithChar ('/'))
                continue;
            total += e->uncompressedSize;
            if (total > maxTotalBytes)
                break;
            MemoryBlock mb;
            if (std::unique_ptr<InputStream> in (zip.createStreamForEntry (i)); in != nullptr)
                in->readIntoMemoryBlock (mb, maxEntryBytes);
            const auto name = e->filename.replaceCharacter ('\\', '/');
            if (name.equalsIgnoreCase ("skin.json"))
                json = JSON::parse (mb.toString());
            else if (! name.contains ("..") && ! name.startsWithChar ('/'))
                files[name] = std::move (mb);
        }
    }
    if (! json.isObject() || str (json, "format") != "emxd")
    {
        error = "This isn't an Emulon X skin file";
        return false;
    }
    if (num (json, "version", 2.0f, 0.0f, 1000.0f) < 2.0f)
    {
        error = "This skin was made for an older Emulon X skin designer and can't be opened";
        return false;
    }
    return true;
}

bool readInfo (const File& file, String& name, String& author)
{
    var json;
    if (file.hasFileExtension ("json"))
        json = JSON::parse (file.loadFileAsString());
    else
    {
        ZipFile zip (file);
        const int i = zip.getIndexOfFileName ("skin.json", true);
        if (i < 0)
            return false;
        if (std::unique_ptr<InputStream> in (zip.createStreamForEntry (i)); in != nullptr)
            json = JSON::parse (in->readEntireStreamAsString());
    }
    if (! json.isObject() || str (json, "format") != "emxd")
        return false;
    name = str (json, "name", file.getFileNameWithoutExtension()).trim().substring (0, 60);
    author = str (json, "author").trim().substring (0, 60);
    if (name.isEmpty())
        name = file.getFileNameWithoutExtension();
    return true;
}

bool save (const File& file, const var& json, const std::map<String, MemoryBlock>& files, String& error)
{
    ZipFile::Builder zb;
    const auto text = JSON::toString (json);
    const auto now = Time::getCurrentTime();
    zb.addEntry (std::make_unique<MemoryInputStream> (text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 9, "skin.json", now);
    for (auto& [name, data] : files)
    {
        const bool compressed = ! (name.endsWithIgnoreCase (".png") || name.endsWithIgnoreCase (".jpg") || name.endsWithIgnoreCase (".jpeg"));
        zb.addEntry (std::make_unique<MemoryInputStream> (data, true), compressed ? 9 : 0, name, now);
    }
    const auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    {
        FileOutputStream out (tmp);
        if (! out.openedOk())
        {
            error = "Can't write " + file.getFullPathName();
            return false;
        }
        out.setPosition (0);
        out.truncate();
        if (! zb.writeToStream (out, nullptr))
        {
            error = "Writing the skin failed";
            return false;
        }
    }
    if (! tmp.moveFileTo (file))
    {
        tmp.deleteFile();
        error = "Can't write " + file.getFullPathName();
        return false;
    }
    return true;
}

void forEachColour (Design& d, const std::function<void (Colour&)>& fn)
{
    auto fill = [&fn] (Fill& f) { fn (f.a); fn (f.b); };
    auto box = [&] (Box& b) { fill (b.fill); fn (b.outlineColour); };
    for (auto* c : { &d.accent, &d.text, &d.muted, &d.surface, &d.background, &d.displayColour, &d.patternColour, &d.frameColour,
                     &d.topLineColour, &d.logoColour, &d.heading, &d.displayText, &d.list, &d.buttonText, &d.buttonOnText, &d.tabText,
                     &d.capLight, &d.capDark, &d.ringTrack, &d.ringValue, &d.pointer, &d.faderTrack, &d.faderFill, &d.thumbTop,
                     &d.thumbBottom, &d.label })
        fn (*c);
    fill (d.window);
    fill (d.topBar);
    fn (d.graphPatternColour);
    for (auto* b : { &d.panel, &d.inset, &d.display, &d.screen, &d.button, &d.buttonOn, &d.tab, &d.tabOn })
        box (*b);
}

Array<File> findSkins (const StringArray& folders)
{
    Array<File> result;
    for (auto& path : folders)
    {
        const File dir (path);
        if (dir.isDirectory())
            for (const auto& entry : RangedDirectoryIterator (dir, false, "*.emxd", File::findFiles))
                result.addIfNotAlreadyThere (entry.getFile());
    }
    std::sort (result.begin(), result.end(), [] (const File& a, const File& b) { return naturalLess (a.getFileName(), b.getFileName()); });
    return result;
}
} // namespace emx::skin
