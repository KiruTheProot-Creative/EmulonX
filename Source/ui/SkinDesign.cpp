// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SkinDesign.h"

namespace emx
{
using namespace juce;

const char* SkinDesign::partKey (int p)
{
    static const char* k[] { "background", "frame", "knob", "fader", "panel", "inset", "display", "button", "button-on", "logo", "fader-knob" };
    return k[jlimit (0, numParts - 1, p)];
}

const char* SkinDesign::partTitle (int p)
{
    static const char* t[] { "BACKGROUND", "FRAME", "KNOB", "FADER", "PANEL", "KNOB AREA", "DISPLAY", "BUTTON", "BUTTON (ON)", "LOGO", "FADER KNOB" };
    return t[jlimit (0, numParts - 1, p)];
}

const char* SkinDesign::partHelp (int p)
{
    static const char* h[] {
        "Fills the whole window. 1920 x 1200 or larger.",
        "Drawn over the edges of the window (the middle stays see-through). 1920 x 1080 PNG with transparency, like the template.",
        "Square, e.g. 256 x 256 PNG. A pointer turns on top of it, or the image itself turns (see KNOB TURNS).",
        "The fader track, tall and thin, e.g. 35 x 150 PNG. The fader knob slides along it.",
        "Behind every panel: fills it (or keeps its corners, see PICTURES), e.g. 96 x 96 PNG.",
        "Behind groups of knobs inside a panel, fills them, e.g. 80 x 80 PNG.",
        "Fills value displays and text fields, e.g. 48 x 24 PNG.",
        "Fills normal buttons, e.g. 64 x 32 PNG.",
        "Fills buttons that are switched on, e.g. 64 x 32 PNG.",
        "Top-left logo, transparent PNG (wide, e.g. 871 x 200).",
        "The knob that slides on a fader, e.g. 64 x 32 PNG (drawn about 32 px wide)." };
    return h[jlimit (0, numParts - 1, p)];
}

const char* SkinDesign::colourKey (int i)
{
    static const char* k[] { "accent", "text", "muted", "display-text", "display-bg", "list" };
    return k[jlimit (0, numColours - 1, i)];
}

const char* SkinDesign::colourTitle (int i)
{
    static const char* t[] { "ACCENT", "TEXT", "SOFT TEXT", "DISPLAY TEXT", "DISPLAY", "LISTS" };
    return t[jlimit (0, numColours - 1, i)];
}

bool SkinDesign::isEmpty() const
{
    for (auto& b : images)
        if (! b.isEmpty())
            return false;
    return font.isEmpty() && colours.empty();
}

var SkinDesign::toJson() const
{
    auto* o = new DynamicObject();
    o->setProperty ("format", "emxd");
    o->setProperty ("version", 1);
    o->setProperty ("name", name);
    o->setProperty ("author", author);
    o->setProperty ("hueShift", hueShift);
    o->setProperty ("knobRotates", knobRotates);
    o->setProperty ("shadows", shadows);
    o->setProperty ("keepCorners", keepCorners);
    auto* c = new DynamicObject();
    for (auto& [k, col] : colours)
        c->setProperty (k, "#" + col.toDisplayString (false).toLowerCase());
    o->setProperty ("colours", var (c));
    auto* im = new DynamicObject();
    for (int p = 0; p < numParts; ++p)
        if (! images[p].isEmpty())
            im->setProperty (partKey (p), String (partKey (p)) + "." + images[p].ext);
    o->setProperty ("images", var (im));
    if (! font.isEmpty())
        o->setProperty ("font", "font." + font.ext);
    return var (o);
}

void SkinDesign::fromJson (const var& v)
{
    name = v.getProperty ("name", "My Skin").toString().substring (0, 60);
    author = v.getProperty ("author", "").toString().substring (0, 60);
    hueShift = (bool) v.getProperty ("hueShift", true);
    knobRotates = (bool) v.getProperty ("knobRotates", false);
    shadows = (bool) v.getProperty ("shadows", true);
    keepCorners = (bool) v.getProperty ("keepCorners", false);
    colours.clear();
    if (auto* c = v.getProperty ("colours", var()).getDynamicObject())
        for (auto& p : c->getProperties())
        {
            auto s = p.value.toString().trim().removeCharacters ("#");
            if (s.length() == 6 && s.containsOnly ("0123456789abcdefABCDEF"))
                colours[p.name.toString()] = Colour ((uint32) (0xff000000u | (uint32) s.getHexValue32()));
        }
}

//== Reading =======================================================================
namespace
{
bool isImageExt (const String& e) { return e == "png" || e == "jpg" || e == "jpeg"; }
bool isFontExt (const String& e)  { return e == "ttf" || e == "otf"; }

/** Takes a file by its name ("knob.png", "font.ttf", "skin.json") into the design. */
void take (SkinDesign& d, const String& fileName, MemoryBlock data, var& json)
{
    const auto lower = fileName.fromLastOccurrenceOf ("/", false, false).toLowerCase();
    const auto base = lower.upToLastOccurrenceOf (".", false, false), ext = lower.fromLastOccurrenceOf (".", false, false);
    if (lower == "skin.json")
    {
        json = JSON::parse (data.toString());
        return;
    }
    if (isFontExt (ext))
    {
        d.font = { std::move (data), ext };
        return;
    }
    if (! isImageExt (ext))
        return;
    for (int p = 0; p < SkinDesign::numParts; ++p)
        if (base == SkinDesign::partKey (p) || (p == SkinDesign::fader && base == "slider") || (p == SkinDesign::buttonOn && base == "button_on")
            || (p == SkinDesign::faderKnob && (base == "fader_knob" || base == "fader-cap")))
            d.images[p] = { std::move (data), ext == "jpeg" ? String ("jpg") : ext };
}
} // namespace

bool SkinDesign::load (const File& f, SkinDesign& out, String& error)
{
    SkinDesign d;
    var json;
    if (f.isDirectory())
    {
        for (const auto& entry : RangedDirectoryIterator (f, false, "*", File::findFiles))
        {
            MemoryBlock mb;
            if (entry.getFile().getSize() < 64 * 1024 * 1024 && entry.getFile().loadFileAsData (mb))
                take (d, entry.getFile().getFileName(), std::move (mb), json);
        }
    }
    else
    {
        ZipFile zip (f);
        if (zip.getNumEntries() == 0)
        {
            error = "Not an Emulon X design file";
            return false;
        }
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* e = zip.getEntry (i);
            if (e == nullptr || e->isSymbolicLink || e->uncompressedSize > 64 * 1024 * 1024)
                continue;
            std::unique_ptr<InputStream> in (zip.createStreamForEntry (i));
            if (in == nullptr)
                continue;
            MemoryBlock mb;
            in->readIntoMemoryBlock (mb);
            take (d, e->filename, std::move (mb), json);
        }
    }
    if (json.isObject())
        d.fromJson (json);
    else if (f.isDirectory())
        d.name = f.getFileName();
    if (d.isEmpty() && ! json.isObject())
    {
        error = "No skin.json, images or font found";
        return false;
    }
    out = std::move (d);
    return true;
}

//== Writing =======================================================================
bool SkinDesign::save (const File& f, String& error) const
{
    ZipFile::Builder zb;
    const auto json = JSON::toString (toJson());
    const auto now = Time::getCurrentTime();
    zb.addEntry (std::make_unique<MemoryInputStream> (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 9, "skin.json", now);
    for (int p = 0; p < numParts; ++p)
        if (! images[p].isEmpty())
            zb.addEntry (std::make_unique<MemoryInputStream> (images[p].data, true), 0, String (partKey (p)) + "." + images[p].ext, now);
    if (! font.isEmpty())
        zb.addEntry (std::make_unique<MemoryInputStream> (font.data, true), 9, "font." + font.ext, now);

    auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
    {
        FileOutputStream out (tmp);
        if (! out.openedOk())
        {
            error = "Can't write " + f.getFullPathName();
            return false;
        }
        out.setPosition (0);
        out.truncate();
        if (! zb.writeToStream (out, nullptr))
        {
            error = "Writing the design failed";
            return false;
        }
    }
    if (! tmp.moveFileTo (f))
    {
        tmp.deleteFile();
        error = "Can't write " + f.getFullPathName();
        return false;
    }
    return true;
}

bool SkinDesign::saveFolder (const File& dir, String& error) const
{
    if (! dir.createDirectory())
    {
        error = "Can't create " + dir.getFullPathName();
        return false;
    }
    bool ok = dir.getChildFile ("skin.json").replaceWithText (JSON::toString (toJson()));
    for (int p = 0; p < numParts; ++p)
        if (! images[p].isEmpty())
            ok = ok && dir.getChildFile (String (partKey (p)) + "." + images[p].ext).replaceWithData (images[p].data.getData(), images[p].data.getSize());
    if (! font.isEmpty())
        ok = ok && dir.getChildFile ("font." + font.ext).replaceWithData (font.data.getData(), font.data.getSize());
    if (! ok)
        error = "Writing into " + dir.getFullPathName() + " failed";
    return ok;
}
} // namespace emx
