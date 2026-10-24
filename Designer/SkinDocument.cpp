// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SkinDocument.h"
#include "SdAssets.h"

namespace emx::sd
{
using namespace juce;

namespace
{
struct Preset { const char* title; const char* resource; };
const Preset presets[] {
    { "EMULON CLASSIC", "classic_json" }, { "EMULON XP - LIGHT", "xplight_json" }, { "EMULON XP - DARK", "xpdark_json" },
    { "EMURUM", "emurum_json" }, { "EMUTAL", "emutal_json" }, { "EMUBLOX", "emublox_json" }, { "EMUL8R", "emul8r_json" } };

String resourceText (const char* name)
{
    int size = 0;
    if (const auto* data = SdAssets::getNamedResource (name, size))
        return String::fromUTF8 (data, size);
    return {};
}

File draftFolder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Emulon X SD");
}

/** Every string in the description that names a file inside the skin. */
void collectFileRefs (const var& v, StringArray& out)
{
    if (auto* o = v.getDynamicObject())
    {
        for (auto& p : o->getProperties())
        {
            if (p.name.toString() == "tex" && p.value.isString())
            {
                const auto t = p.value.toString();
                if (t != "none" && t != "custom" && t.isNotEmpty())
                    out.addIfNotAlreadyThere ("textures/" + t + ".jpg");
            }
            collectFileRefs (p.value, out);
        }
    }
    else if (auto* a = v.getArray())
    {
        for (auto& x : *a)
            collectFileRefs (x, out);
    }
    else if (v.isString())
    {
        const auto s = v.toString();
        if (s.startsWith ("images/") || s.startsWith ("textures/") || s.startsWith ("fonts/"))
            out.addIfNotAlreadyThere (s);
    }
}
} // namespace

SkinDocument::SkinDocument() { reset(); }

//== Paths =========================================================================
var& SkinDocument::at (const String& path, bool create)
{
    static var nothing;
    nothing = var();
    var* v = &json;
    const auto parts = StringArray::fromTokens (path, ".", {});
    for (int i = 0; i < parts.size(); ++i)
    {
        auto* o = v->getDynamicObject();
        if (o == nullptr)
            return nothing;
        const Identifier id (parts[i]);
        if (! o->hasProperty (id))
        {
            if (! create)
                return nothing;
            o->setProperty (id, i == parts.size() - 1 ? var() : var (new DynamicObject()));
        }
        else if (create && i < parts.size() - 1 && ! o->getProperty (id).isObject())
            o->setProperty (id, var (new DynamicObject()));
        v = o->getProperties().getVarPointer (id);
        if (v == nullptr)
            return nothing;
    }
    return *v;
}

var SkinDocument::get (const String& path) const
{
    return const_cast<SkinDocument*> (this)->at (path, false);
}

void SkinDocument::set (const String& path, const var& value, const String& undoTag)
{
    if (get (path) == value && ! value.isVoid())
        return;
    pushUndo (undoTag.isNotEmpty() ? undoTag : path);
    at (path, true) = value;
    changed (false);
}

void SkinDocument::remove (const String& path, const String& undoTag)
{
    const auto parent = path.upToLastOccurrenceOf (".", false, false);
    const auto key = path.fromLastOccurrenceOf (".", false, false);
    auto* o = (parent == path ? json : get (parent)).getDynamicObject();
    if (o == nullptr || ! o->hasProperty (key))
        return;
    pushUndo (undoTag.isNotEmpty() ? undoTag : path);
    o->removeProperty (key);
    changed (false);
}

const StringArray& SkinDocument::tokenNames()
{
    static const StringArray n { "accent", "text", "muted", "surface", "background", "display" };
    return n;
}

const StringArray& SkinDocument::tokenTitles()
{
    static const StringArray n { "ACCENT", "TEXT", "SOFT TEXT", "SURFACE", "BACKGROUND", "DISPLAY" };
    return n;
}

Colour SkinDocument::token (const String& name) const
{
    Colour c (0xffff00ff);
    skin::parseColour (get ("tokens." + name).toString(), c);
    return c;
}

Colour SkinDocument::colour (const String& path, Colour fallback) const
{
    const auto s = get (path).toString().trim();
    if (s.startsWithChar ('@'))
        return tokenNames().contains (s.substring (1)) ? token (s.substring (1)) : fallback;
    Colour c;
    return skin::parseColour (s, c) ? c : fallback;
}

String SkinDocument::getName() const   { return get ("name").toString(); }
String SkinDocument::getAuthor() const { return get ("author").toString(); }
void SkinDocument::setName (const String& n)   { set ("name", n.substring (0, 60), "name"); }
void SkinDocument::setAuthor (const String& a) { set ("author", a.substring (0, 60), "author"); }

//== Files =========================================================================
String SkinDocument::addFile (const String& folder, const String& originalName, const MemoryBlock& data)
{
    auto base = File::createLegalFileName (originalName.upToLastOccurrenceOf (".", false, false)).toLowerCase().replaceCharacter (' ', '-');
    if (base.isEmpty())
        base = "file";
    auto ext = originalName.fromLastOccurrenceOf (".", false, false).toLowerCase();
    if (ext == originalName.toLowerCase() || ext.isEmpty())
        ext = "png";
    // the same file again: use the one already inside
    for (auto& [name, existing] : files)
        if (name.startsWith (folder + "/") && existing == data)
            return name;
    auto name = folder + "/" + base + "." + ext;
    for (int n = 2; files.count (name) > 0; ++n)
        name = folder + "/" + base + "-" + String (n) + "." + ext;
    files[name] = data;
    return name;
}

MemoryBlock SkinDocument::fileData (const String& name) const
{
    if (auto it = files.find (name); it != files.end())
        return it->second;
    if (name.startsWith ("textures/"))
        return builtInTexture (name.fromFirstOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false));
    return {};
}

const StringArray& SkinDocument::builtInTextures()
{
    static const StringArray ids { "metal-brushed", "metal-scratched", "metal-wavy", "metal-polished" };
    return ids;
}

const StringArray& SkinDocument::builtInTextureTitles()
{
    static const StringArray t { "BRUSHED", "SCRATCHED", "WAVY", "POLISHED" };
    return t;
}

MemoryBlock SkinDocument::builtInTexture (const String& id)
{
    if (! builtInTextures().contains (id))
        return {};
    int size = 0;
    if (const auto* data = SdAssets::getNamedResource ((id.removeCharacters ("-") + "_jpg").toRawUTF8(), size))
        return MemoryBlock (data, (size_t) size);
    return {};
}

namespace
{
/** Every own texture a part uses: { overlay prefix ("panel.overlay"), file }. */
void collectCustomTextures (const var& v, const String& path, std::vector<std::pair<String, String>>& out)
{
    if (auto* o = v.getDynamicObject())
    {
        if (o->getProperty ("tex").toString() == "custom" && o->getProperty ("custom").toString().isNotEmpty())
            out.push_back ({ path, o->getProperty ("custom").toString() });
        for (auto& p : o->getProperties())
            collectCustomTextures (p.value, path.isEmpty() ? p.name.toString() : path + "." + p.name.toString(), out);
    }
}
} // namespace

StringArray SkinDocument::ownTextures() const
{
    StringArray out;
    if (auto* arr = get ("textureLibrary").getArray())
        for (auto& v : *arr)
            if (v.toString().isNotEmpty())
                out.addIfNotAlreadyThere (v.toString());
    std::vector<std::pair<String, String>> used;
    collectCustomTextures (json, {}, used);
    for (auto& u : used)
        out.addIfNotAlreadyThere (u.second);
    return out;
}

void SkinDocument::addOwnTexture (const String& f)
{
    Array<var> arr;
    if (auto* existing = get ("textureLibrary").getArray())
        arr = *existing;
    if (! arr.contains (f))
        arr.add (f);
    set ("textureLibrary", arr, "texture-library");
}

void SkinDocument::removeOwnTexture (const String& f)
{
    Array<var> arr;
    if (auto* existing = get ("textureLibrary").getArray())
        arr = *existing;
    arr.removeAllInstancesOf (f);
    set ("textureLibrary", arr);
}

bool SkinDocument::textureInUse (const String& f, const String& exceptPrefix) const
{
    std::vector<std::pair<String, String>> used;
    collectCustomTextures (json, {}, used);
    for (auto& u : used)
        if (u.second == f && u.first != exceptPrefix)
            return true;
    return false;
}

void SkinDocument::migrate()
{
    // earlier picture knobs had one turning picture: it becomes the upper layer
    const auto old = get ("knob.picture").toString();
    if (old.isNotEmpty() && get ("knob.upper.picture").isVoid())
    {
        at ("knob.upper.picture", true) = old;
        at ("knob.upper.turns", true) = true;
    }
    if (auto* k = get ("knob").getDynamicObject())
        k->removeProperty ("picture");
}

//== Starting points ===============================================================
const StringArray& SkinDocument::presetTitles()
{
    static const StringArray t = []
    {
        StringArray s;
        for (auto& p : presets)
            s.add (p.title);
        return s;
    }();
    return t;
}

void SkinDocument::startFrom (int index)
{
    if (index < 0 || index >= (int) std::size (presets))
        return;
    const auto keepName = getName(), keepAuthor = getAuthor();
    pushUndo ("start-from-" + String (index) + String (Time::getMillisecondCounter()));
    json = JSON::parse (resourceText (presets[index].resource));
    if (keepName.isNotEmpty())
        at ("name", true) = keepName;
    at ("author", true) = keepAuthor;
    changed (true);
}

void SkinDocument::reset()
{
    json = JSON::parse (resourceText (presets[0].resource));
    at ("name", true) = "My Skin";
    at ("author", true) = "";
    files.clear();
    file = File();
    undoStack.clear();
    redoStack.clear();
    lastTag = {};
    ++revision;
    savedRevision = revision;
    ++structureRevision;
    sendChangeMessage();
}

//== Opening / saving ===============================================================
void SkinDocument::importDataUrls (var& v)
{
    if (auto* o = v.getDynamicObject())
    {
        auto& props = o->getProperties();
        for (int i = 0; i < props.size(); ++i)
            if (auto* child = props.getVarPointerAt (i))
                importDataUrls (*child);
    }
    else if (v.isString() && v.toString().startsWithIgnoreCase ("data:image/"))
    {
        // pictures inside the description (Skin Studio prototype): move them into the skin as files
        const auto s = v.toString();
        const auto comma = s.indexOfChar (',');
        MemoryBlock mb;
        MemoryOutputStream out (mb, false);
        if (comma > 0 && Base64::convertFromBase64 (out, s.substring (comma + 1)))
        {
            out.flush();
            const auto ext = s.containsIgnoreCase ("image/jpeg") ? String ("jpg") : String ("png");
            v = addFile ("images", "picture." + ext, mb);
        }
    }
}

bool SkinDocument::open (const File& f, String& error)
{
    var newJson;
    std::map<String, MemoryBlock> newFiles;
    if (! skin::readForEditing (f, newJson, newFiles, error))
        return false;
    pushUndo ("open" + String (Time::getMillisecondCounter()));
    json = newJson;
    files = std::move (newFiles);
    importDataUrls (json);
    migrate();
    file = f.hasFileExtension ("emxd") ? f : File();
    changed (true);
    savedRevision = revision;
    return true;
}

std::map<String, MemoryBlock> SkinDocument::usedFiles() const
{
    StringArray refs;
    collectFileRefs (json, refs);
    std::map<String, MemoryBlock> out;
    for (auto& r : refs)
    {
        auto data = fileData (r);
        if (data.getSize() > 0)
            out[r] = std::move (data);
    }
    return out;
}

bool SkinDocument::save (const File& f, String& error)
{
    if (! skin::save (f, json, usedFiles(), error))
        return false;
    file = f;
    savedRevision = revision;
    sendChangeMessage();
    return true;
}

//== Undo ==========================================================================
String SkinDocument::snapshot() const { return JSON::toString (json, true); }

void SkinDocument::restore (const String& s)
{
    json = JSON::parse (s);
    changed (true);
}

void SkinDocument::pushUndo (const String& tag)
{
    const auto now = Time::getMillisecondCounterHiRes();
    if (tag != lastTag || now - lastChange > 900.0 || undoStack.empty())
    {
        undoStack.push_back (snapshot());
        if (undoStack.size() > 300)
            undoStack.erase (undoStack.begin());
    }
    redoStack.clear();
    lastTag = tag;
    lastChange = now;
}

void SkinDocument::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (snapshot());
    const auto s = undoStack.back();
    undoStack.pop_back();
    lastTag = {};
    restore (s);
}

void SkinDocument::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (snapshot());
    const auto s = redoStack.back();
    redoStack.pop_back();
    lastTag = {};
    restore (s);
}

void SkinDocument::changed (bool structure)
{
    ++revision;
    if (structure)
        ++structureRevision;
    sendChangeMessage();
}

//== Rendering =====================================================================
std::shared_ptr<const skin::Design> SkinDocument::design() const
{
    if (designRevision != revision || cachedDesign == nullptr)
    {
        auto d = std::make_shared<skin::Design>();
        String error;
        warnings.clear();
        if (! skin::fromJson (json, [this] (const String& name) { return fileData (name); }, *d, error, &warnings))
            warnings.insert (0, error);
        cachedDesign = d;
        designRevision = revision;
    }
    return cachedDesign;
}

//== Draft ==========================================================================
void SkinDocument::saveDraft()
{
    const auto dir = draftFolder();
    dir.createDirectory();
    String error;
    if (skin::save (dir.getChildFile ("draft.emxd"), json, usedFiles(), error))
        dir.getChildFile ("draft.txt").replaceWithText (file.getFullPathName() + "\n" + (hasUnsavedChanges() ? "changed" : "saved"));
}

bool SkinDocument::loadDraft()
{
    const auto dir = draftFolder();
    var newJson;
    std::map<String, MemoryBlock> newFiles;
    String error;
    if (! skin::readForEditing (dir.getChildFile ("draft.emxd"), newJson, newFiles, error))
        return false;
    json = newJson;
    files = std::move (newFiles);
    migrate();
    const auto info = StringArray::fromLines (dir.getChildFile ("draft.txt").loadFileAsString());
    file = info[0].isNotEmpty() && File::isAbsolutePath (info[0]) ? File (info[0]) : File();
    undoStack.clear();
    redoStack.clear();
    ++revision;
    savedRevision = info[1] == "saved" ? revision : -1;
    ++structureRevision;
    sendChangeMessage();
    return true;
}
} // namespace emx::sd
