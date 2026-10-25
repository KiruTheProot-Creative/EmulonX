// SPDX-License-Identifier: AGPL-3.0-or-later
#include "HubModel.h"
#include "HubAssets.h"
#include "Settings.h"
#include <juce_cryptography/juce_cryptography.h>

namespace emx::hub
{
using namespace juce;

namespace
{
[[maybe_unused]] const char* emxAppId = "{FD936028-B870-420E-9A62-5FF89CBD3473}";
[[maybe_unused]] const char* sdAppId = "{B82616AB-20BE-463B-9A40-0B92677B574A}";
[[maybe_unused]] const char* hubAppId = "{418FA9BC-6948-48A4-A480-4EE7CD914ED1}";

struct LambdaTimer final : Timer
{
    std::function<void()> f;
    void timerCallback() override { if (f) f(); }
};

String builtIn (const String& originalName)
{
    for (int i = 0; i < HubAssets::namedResourceListSize; ++i)
        if (String (HubAssets::getNamedResourceOriginalFilename (HubAssets::namedResourceList[i])) == originalName)
        {
            int size = 0;
            if (auto* data = HubAssets::getNamedResource (HubAssets::namedResourceList[i], size))
                return String::fromUTF8 (data, size);
        }
    return {};
}

#if JUCE_WINDOWS
String uninstallValue (const char* appId, const String& name)
{
    const String key = String ("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + appId + "_is1\\" + name;
    for (auto root : { "HKEY_LOCAL_MACHINE\\", "HKEY_CURRENT_USER\\" })
    {
        const auto v = WindowsRegistry::getValue (root + key, {}, WindowsRegistry::WoW64_64bit);
        if (v.isNotEmpty())
            return v;
        const auto v32 = WindowsRegistry::getValue (root + key, {}, WindowsRegistry::WoW64_32bit);
        if (v32.isNotEmpty())
            return v32;
    }
    return {};
}
String emxRegistry (const String& name)
{
    return WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\Software\\Emulon X\\" + name, {}, WindowsRegistry::WoW64_64bit);
}
#endif

File documents() { return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Emulon X"); }
File programs()  { return File::getSpecialLocation (File::globalApplicationsDirectory); }

StringPairArray kv (const StringArray& lines, int& consumed)
{
    StringPairArray m;
    consumed = 0;
    while (consumed < lines.size())
    {
        const auto& l = lines[consumed];
        const int colon = l.indexOfChar (':');
        if (colon <= 0 || ! CharacterFunctions::isLetter (l[0]) || ! l.substring (0, colon).containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ "))
            break;
        m.set (l.substring (0, colon).trim().toLowerCase(), l.substring (colon + 1).trim());
        ++consumed;
    }
    return m;
}

StringArray items (const StringArray& lines)
{
    StringArray out;
    for (auto l : lines)
    {
        l = l.trim();
        if (l.startsWith ("- "))
            out.add (l.substring (2).trim());
    }
    return out;
}

String settingsBlock (const StringPairArray& pairs)
{
    StringArray out;
    for (int i = 0; i < pairs.size(); ++i)
    {
        const auto v = pairs.getAllValues()[i];
        if (v.isEmpty() || v == "no")
            continue;
        out.add (pairs.getAllKeys()[i] + ": " + v);
    }
    return out.joinIntoString ("\n");
}

String listMd (const String& title, const String& head, const std::vector<std::pair<String, std::pair<StringPairArray, String>>>& list)
{
    String s = "---\ntitle: " + title + "\n---\n" + head + "\n";
    for (auto& [name, rest] : list)
    {
        s << "\n## " << name << "\n" << settingsBlock (rest.first) << "\n";
        if (rest.second.isNotEmpty())
            s << "\n" << rest.second.trim() << "\n";
    }
    return s;
}

const char* skinsHead = "Each skin starts with `## NAME`. Settings go right under it: `by`, `image`, `included: yes` for skins that come with Emulon X, and for downloadable skins `download` (link to the .emxd file) and `size`. The description follows after an empty line.";
const char* banksHead = "Each bank starts with `## NAME`. Settings: `by`, `official: yes`, `soon: yes` while it isn't out yet, `presets`, `size`, `download` (link to the .emxb file), `image` (a cover picture in hub/images; without one the Hub draws a cover in `colour`, a number from 0 to 360 for the hue). The description follows after an empty line.";
const char* newsHead = "Each notification starts with `## Title`. Settings: `date` (YYYY-MM-DD; newer ones show first and only unseen ones pop up), `text` (one short line) and `open` (which tab it opens: emulon-x, sd, skins or banks, optionally with an item, like `banks: EMULON X BASE BANK`).";

StringPairArray pairs (std::initializer_list<std::pair<const char*, String>> list)
{
    StringPairArray p;
    for (auto& [k, v] : list)
        p.set (k, v);
    return p;
}

Array<int> versionNumbers (const String& v)
{
    Array<int> n;
    String cur;
    for (auto c : v + " ")
    {
        if (CharacterFunctions::isDigit (c))
            cur << c;
        else if (cur.isNotEmpty())
        {
            n.add (cur.getIntValue());
            cur = {};
            if (n.size() == 3)
                break;
        }
    }
    while (n.size() < 3)
        n.add (0);
    return n;
}

int tagRank (const String& v)
{
    const auto t = v.retainCharacters ("abcdefghijklmnopqrstuvwxyz").toLowerCase();
    if (t.endsWith ("ea")) return 0;
    if (t.endsWith ("af")) return 1;
    if (t.endsWith ("bv")) return 2;
    if (t.endsWith ("bf")) return 3;
    return 4;
}
} // namespace

//== Text helpers ===================================================================
String slugOf (const String& t)
{
    String s;
    bool dash = false;
    for (auto c : t.toLowerCase())
    {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            if (dash && s.isNotEmpty())
                s << '-';
            s << c;
            dash = false;
        }
        else
            dash = true;
    }
    return s;
}

bool yes (const String& v)
{
    const auto t = v.trim().toLowerCase();
    return t == "yes" || t == "true" || t == "1";
}

bool isNewer (const String& a, const String& b)
{
    if (a.isEmpty())
        return false;
    if (b.isEmpty())
        return true;
    const auto x = versionNumbers (a), y = versionNumbers (b);
    for (int i = 0; i < 3; ++i)
        if (x[i] != y[i])
            return x[i] > y[i];
    return tagRank (a) > tagRank (b);
}

String formatBytes (int64 n)
{
    if (n <= 0) return {};
    if (n < 1024 * 1024) return String (jmax (1, roundToInt ((double) n / 1024.0))) + " KB";
    const double mb = (double) n / (1024.0 * 1024.0);
    return (mb < 10.0 ? String (mb, 1) : String (roundToInt (mb))) + " MB";
}

String today() { return Time::getCurrentTime().formatted ("%Y-%m-%d"); }

MdDoc parseMd (const String& textIn)
{
    MdDoc doc;
    auto lines = StringArray::fromLines (textIn.removeCharacters ("\r"));
    int start = 0;
    if (lines.size() > 0 && lines[0].trim() == "---")
    {
        for (int j = 1; j < lines.size(); ++j)
            if (lines[j].trim() == "---")
            {
                StringArray fm;
                for (int k = 1; k < j; ++k)
                    fm.add (lines[k]);
                int used = 0;
                doc.meta = kv (fm, used);
                start = j + 1;
                break;
            }
    }
    StringArray intro;
    MdDoc::Section* cur = nullptr;
    for (int i = start; i < lines.size(); ++i)
    {
        const auto& l = lines[i];
        if (l.startsWith ("## "))
        {
            doc.secs.push_back ({ l.substring (3).trim(), {} });
            cur = &doc.secs.back();
        }
        else if (cur != nullptr)
            cur->lines.add (l);
        else
            intro.add (l);
    }
    doc.intro = intro.joinIntoString ("\n").trim();
    return doc;
}

Product productFrom (const MdDoc& md, const String& key)
{
    Product p;
    p.key = key;
    p.name = md.meta["title"];
    p.sub = md.meta["subtitle"];
    p.tags = md.meta["tags"];
    p.descMd = md.intro;
    for (auto k : { "version", "size", "download" })
        if (md.meta.containsKey (k))
            p.extra.set (k, md.meta[k]);
    for (auto& s : md.secs)
    {
        const auto t = s.title.toLowerCase();
        if (t == "features")
            p.feats = items (s.lines);
        else if (t == "screenshots")
            for (auto& x : items (s.lines))
            {
                p.shotFiles.add (x.upToFirstOccurrenceOf ("|", false, false).trim());
                p.shotCaptions.add (x.fromFirstOccurrenceOf ("|", false, false).trim());
            }
        else if (t == "what's new")
        {
            StringArray keep;
            for (auto& l : s.lines)
                if (l.trim().isNotEmpty())
                    keep.add (l);
            p.logMd = keep.joinIntoString ("\n");
        }
    }
    return p;
}

String productMd (const Product& p)
{
    auto head = pairs ({ { "title", p.name }, { "subtitle", p.sub }, { "tags", p.tags } });
    head.addArray (p.extra);
    String s = "---\n" + settingsBlock (head) + "\n---\n" + p.descMd.trim() + "\n\n## Features\n";
    for (auto& f : p.feats)
        s << "- " << f << "\n";
    s << "\n## Screenshots\n";
    for (int i = 0; i < p.shotFiles.size(); ++i)
        s << "- " << p.shotFiles[i] << " | " << p.shotCaptions[i] << "\n";
    s << "\n## What's new\n" << p.logMd.trim() << "\n";
    return s;
}

//== Pictures =======================================================================
Image ImageStore::get (const String& url)
{
    if (url.isEmpty())
        return {};
    if (auto it = images.find (url); it != images.end())
        return it->second;
    if (loading.count (url) || failed.count (url))
        return {};
    loading.insert (url);

    const auto cache = cacheDir.getChildFile ("img-" + String::toHexString (url.hashCode64()));
    if (cache.existsAsFile())
        if (auto img = ImageFileFormat::loadFrom (cache); img.isValid())
            images[url] = img;

    Net::get().fetch (url, [this, url, cache] (const Response& r)
    {
        loading.erase (url);
        if (! r.ok())
        {
            if (! images.count (url))
                failed.insert (url);
            return;
        }
        auto img = ImageFileFormat::loadFrom (r.data.getData(), r.data.getSize());
        if (! img.isValid())
            return;
        cache.replaceWithData (r.data.getData(), r.data.getSize());
        images[url] = img;
        sendChangeMessage();
    });
    return images.count (url) ? images[url] : Image();
}

void ImageStore::put (const String& url, const Image& img)
{
    images[url] = img;
    failed.erase (url);
    sendChangeMessage();
}

//== Settings =======================================================================
HubSettings::HubSettings()
{
    PropertiesFile::Options o;
    o.applicationName = "Emulon X Hub";
    o.folderName = "Emulon X";
    o.filenameSuffix = "settings";
    o.osxLibrarySubFolder = "Application Support";
    o.millisecondsBeforeSaving = 500;
    file = std::make_unique<PropertiesFile> (o);
}

StringArray HubSettings::list (const String& k) const
{
    StringArray a;
    a.addTokens (file->getValue (k), "\n", {});
    a.removeEmptyStrings();
    return a;
}

void HubSettings::setList (const String& k, const StringArray& a) { set (k, a.joinIntoString ("\n")); }

File HubSettings::cacheFolder() const
{
    auto d = file->getFile().getParentDirectory().getChildFile ("Hub cache");
    d.createDirectory();
    return d;
}

//== Model ==========================================================================
Model::Model()
{
    images.cacheDir = settings.cacheFolder();
    // start with the last copy from GitHub, or the one built into the app
    std::map<String, String> texts;
    for (auto& f : textFiles())
    {
        const auto cached = settings.cacheFolder().getChildFile ("text-" + f);
        texts[f] = cached.existsAsFile() ? cached.loadFileAsString() : builtIn (f);
    }
    applyTexts (texts);
    const auto hubJson = JSON::parse (builtIn ("hub.json"));
    clientId = hubJson.getProperty ("githubClientId", "").toString();
    if (clientId.isEmpty())
        clientId = settings.get ("githubClientId");
    applyIndexes (JSON::parse (settings.cacheFolder().getChildFile ("skins-index.json")),
                  JSON::parse (settings.cacheFolder().getChildFile ("banks-index.json")));
    applyReleases (JSON::parse (settings.cacheFolder().getChildFile ("releases.json")));
    snapshot();
}

const StringArray& Model::textFiles()
{
    static const StringArray f { "emulon-x.md", "emulon-x-sd.md", "skins.md", "banks.md", "news.md" };
    return f;
}

String Model::hubImageUrl (const String& file) const
{
    if (file.isEmpty())
        return {};
    if (file.startsWith ("http"))
        return file;
    return rawUrl (mainRepo, "hub/images/" + file);
}

void Model::refresh (std::function<void()> done)
{
    struct State
    {
        std::map<String, String> texts;
        int okTexts = 0, pending = 0;
        var skinsIndex, banksIndex, releases;
        String hubJson;
    };
    auto st = std::make_shared<State>();
    auto finish = [this, st, done]
    {
        if (--st->pending > 0)
            return;
        textsLive = st->okTexts == textFiles().size();
        // keep any file that couldn't be fetched as it was
        for (auto& f : textFiles())
            if (! st->texts.count (f))
                st->texts[f] = baseText.count (f) ? baseText[f] : builtIn (f);
        const auto changed = changedFiles();
        if (changed.isEmpty())   // never throw away edits in dev mode
        {
            applyTexts (st->texts);
            snapshot();
        }
        if (st->hubJson.isNotEmpty())
            if (auto j = JSON::parse (st->hubJson); j.isObject())
                if (auto id = j.getProperty ("githubClientId", "").toString(); id.isNotEmpty())
                {
                    clientId = id;
                    settings.set ("githubClientId", id);
                }
        applyIndexes (st->skinsIndex, st->banksIndex);
        applyReleases (st->releases);
        sendChangeMessage();
        if (done)
            done();
    };

    st->pending = textFiles().size() + 4;
    for (auto& f : textFiles())
        Net::get().fetch (rawUrl (mainRepo, "hub/" + f), [this, st, f, finish] (const Response& r)
        {
            if (r.ok() && r.text().contains ("---"))
            {
                st->texts[f] = r.text();
                st->okTexts++;
                settings.cacheFolder().getChildFile ("text-" + f).replaceWithText (r.text());
            }
            finish();
        });
    Net::get().fetch (rawUrl (mainRepo, "hub/hub.json"), [st, finish] (const Response& r)
    {
        if (r.ok()) st->hubJson = r.text();
        finish();
    });
    Net::get().fetch (rawUrl (skinsRepo, "index.json"), [this, st, finish] (const Response& r)
    {
        if (r.ok() && r.json().isObject())
        {
            st->skinsIndex = r.json();
            settings.cacheFolder().getChildFile ("skins-index.json").replaceWithText (r.text());
        }
        else
            st->skinsIndex = JSON::parse (settings.cacheFolder().getChildFile ("skins-index.json"));
        finish();
    });
    Net::get().fetch (rawUrl (banksRepo, "index.json"), [this, st, finish] (const Response& r)
    {
        if (r.ok() && r.json().isObject())
        {
            st->banksIndex = r.json();
            settings.cacheFolder().getChildFile ("banks-index.json").replaceWithText (r.text());
        }
        else
            st->banksIndex = JSON::parse (settings.cacheFolder().getChildFile ("banks-index.json"));
        finish();
    });
    const auto local = SystemStats::getEnvironmentVariable ("EMX_HUB_SOURCE", {});
    auto onReleases = [this, st, finish] (const Response& r)
    {
        releasesLive = r.ok() && r.json().isArray();
        if (releasesLive)
        {
            st->releases = r.json();
            settings.cacheFolder().getChildFile ("releases.json").replaceWithText (r.text());
        }
        else
            st->releases = JSON::parse (settings.cacheFolder().getChildFile ("releases.json"));
        finish();
    };
    if (local.isNotEmpty())
        Net::get().fetch (URL (File (local).getChildFile ("releases.json")).toString (false), onReleases);
    else
        Net::get().api ("GET", "/repos/" + String (repoOwner) + "/" + mainRepo + "/releases?per_page=50", var(), onReleases);
}

void Model::applyTexts (const std::map<String, String>& t)
{
    baseText = t;
    auto text = [&t] (const char* f) { auto it = t.find (f); return it != t.end() ? it->second : String(); };
    emx = productFrom (parseMd (text ("emulon-x.md")), "emx");
    sd = productFrom (parseMd (text ("emulon-x-sd.md")), "sd");

    std::vector<SkinItem> community;
    for (auto& s : skins)
        if (s.community)
            community.push_back (s);
    skins.clear();
    for (auto& s : parseMd (text ("skins.md")).secs)
    {
        int used = 0;
        const auto m = kv (s.lines, used);
        StringArray rest;
        for (int i = used; i < s.lines.size(); ++i)
            rest.add (s.lines[i]);
        SkinItem k;
        k.name = s.title;
        k.id = slugOf (s.title);
        k.by = m["by"];
        k.image = m["image"];
        k.included = yes (m["included"]);
        k.size = m["size"];
        k.download = m["download"];
        k.descMd = rest.joinIntoString ("\n").trim();
        skins.push_back (k);
    }
    for (auto& c : community)
        skins.push_back (c);

    banks.clear();
    std::vector<BankItem> fromMd;
    for (auto& s : parseMd (text ("banks.md")).secs)
    {
        int used = 0;
        const auto m = kv (s.lines, used);
        StringArray rest;
        for (int i = used; i < s.lines.size(); ++i)
            rest.add (s.lines[i]);
        BankItem b;
        b.name = s.title;
        b.id = slugOf (s.title);
        b.by = m["by"];
        b.official = yes (m["official"]);
        b.soon = yes (m["soon"]);
        b.presets = m["presets"];
        b.size = m["size"];
        b.colour = (m.containsKey ("colour") ? m["colour"] : m["color"]).getIntValue();
        b.image = m["image"];
        b.download = m["download"];
        b.descMd = rest.joinIntoString ("\n").trim();
        fromMd.push_back (b);
    }
    banks = fromMd;
    reapplyBanks = true;

    news.clear();
    for (auto& s : parseMd (text ("news.md")).secs)
    {
        int used = 0;
        const auto m = kv (s.lines, used);
        news.push_back ({ s.title, m["text"], m["date"], m["open"] });
    }
}

void Model::applyIndexes (const var& si, const var& biIn)
{
    var bi = biIn;
    if (! bi.isObject() && reapplyBanks && ! fromIndexBanks.empty())
    {
        // the texts were read again: put the banks from the repository back
        Array<var> list;
        for (auto& b : fromIndexBanks)
        {
            auto* o = new DynamicObject();
            o->setProperty ("id", b.id); o->setProperty ("name", b.name); o->setProperty ("author", b.by);
            o->setProperty ("official", b.official); o->setProperty ("soon", b.soon); o->setProperty ("description", b.descMd);
            o->setProperty ("presets", b.presets); o->setProperty ("version", b.version); o->setProperty ("download", b.download);
            o->setProperty ("sha256", b.sha256); o->setProperty ("colour", b.colour); o->setProperty ("cover", b.image); o->setProperty ("size", b.size);
            list.add (var (o));
        }
        auto* root = new DynamicObject();
        root->setProperty ("banks", list);
        bi = var (root);
    }
    if (! si.isObject() && reapplyBanks)
    {
        skins.erase (std::remove_if (skins.begin(), skins.end(), [] (auto& s) { return s.community; }), skins.end());
        for (auto& k : fromIndexSkins)
            skins.push_back (k);
    }
    reapplyBanks = false;
    if (auto* list = si.getProperty ("skins", var()).getArray())
    {
        skins.erase (std::remove_if (skins.begin(), skins.end(), [] (auto& s) { return s.community; }), skins.end());
        fromIndexSkins.clear();
        for (auto& v : *list)
        {
            SkinItem k;
            k.community = true;
            k.id = v.getProperty ("id", "").toString();
            k.name = v.getProperty ("name", k.id).toString();
            k.by = v.getProperty ("author", "").toString();
            k.descMd = v.getProperty ("description", "").toString();
            k.version = v.getProperty ("version", "1.0").toString();
            k.download = v.getProperty ("skin", "").toString();
            k.preview = v.getProperty ("preview", "").toString();
            k.sha256 = v.getProperty ("sha256", "").toString();
            k.size = formatBytes ((int64) v.getProperty ("size", 0));
            if (k.id.isEmpty() || k.download.isEmpty())
                continue;
            fromIndexSkins.push_back (k);
            skins.push_back (k);
        }
    }
    if (auto* list = bi.getProperty ("banks", var()).getArray())
    {
        banks.erase (std::remove_if (banks.begin(), banks.end(), [] (auto& b) { return b.fromIndex; }), banks.end());
        fromIndexBanks.clear();
        for (auto& v : *list)
        {
            BankItem b;
            b.fromIndex = true;
            b.id = v.getProperty ("id", "").toString();
            b.name = v.getProperty ("name", b.id).toString();
            b.by = v.getProperty ("author", "").toString();
            b.official = (bool) v.getProperty ("official", false);
            b.soon = (bool) v.getProperty ("soon", false);
            b.descMd = v.getProperty ("description", "").toString();
            b.presets = v.getProperty ("presets", "").toString();
            b.version = v.getProperty ("version", "").toString();
            b.download = v.getProperty ("download", "").toString();
            b.sha256 = v.getProperty ("sha256", "").toString();
            b.colour = (int) v.getProperty ("colour", 0);
            b.image = v.getProperty ("cover", "").toString();
            const auto sz = v.getProperty ("size", var());
            b.size = sz.isString() ? sz.toString() : formatBytes ((int64) sz);
            if (b.presets == "0")
                b.presets = {};
            fromIndexBanks.push_back (b);
            // the same bank in hub/banks.md: the repository says whether it's out and where to get it
            bool merged = false;
            for (auto& m : banks)
                if (! m.fromIndex && slugOf (m.name) == slugOf (b.name))
                {
                    m.released = ! b.soon && b.download.isNotEmpty();
                    m.rDownload = b.download;
                    m.rVersion = b.version;
                    m.rSha = b.sha256;
                    m.rSize = b.size;
                    m.rPresets = b.presets;
                    merged = true;
                }
            if (! merged)
                banks.push_back (b);
        }
    }
}

void Model::applyReleases (const var& list)
{
    auto pick = [&list] (const String& prefix, bool drafts)
    {
        Release best;
        if (auto* a = list.getArray())
            for (auto& r : *a)
            {
                if ((bool) r.getProperty ("draft", false) != drafts)
                    continue;
                const auto tag = r.getProperty ("tag_name", "").toString();
                if (! tag.startsWithIgnoreCase (prefix) || ! CharacterFunctions::isDigit (tag[prefix.length()]))
                    continue;
                const auto version = tag.substring (prefix.length());
                if (best.found && ! isNewer (version, best.version))
                    continue;
                Release rel;
                rel.found = true;
                rel.draft = drafts;
                rel.tag = tag;
                rel.version = version;
                rel.name = r.getProperty ("name", tag).toString();
                rel.notes = r.getProperty ("body", "").toString();
                rel.page = r.getProperty ("html_url", "").toString();
                if (auto* assets = r.getProperty ("assets", var()).getArray())
                    for (auto& asset : *assets)
                    {
                        const auto n = asset.getProperty ("name", "").toString();
                       #if JUCE_WINDOWS
                        const bool fits = n.endsWithIgnoreCase (".exe") && n.containsIgnoreCase ("setup");
                       #elif JUCE_MAC
                        const bool fits = n.endsWithIgnoreCase (".zip") && n.containsIgnoreCase ("mac");
                       #else
                        const bool fits = n.endsWithIgnoreCase (".exe") && n.containsIgnoreCase ("setup");
                       #endif
                        if (fits)
                        {
                            rel.assetName = n;
                            rel.assetUrl = asset.getProperty ("browser_download_url", "").toString();
                            rel.assetSize = (int64) asset.getProperty ("size", 0);
                            rel.assetApi = asset.getProperty ("url", "").toString();
                        }
                    }
                if (drafts && rel.assetApi.isEmpty())
                    continue;   // a draft without its installer yet
                best = rel;
            }
        return best;
    };
    relEmx = pick ("v", false);
    relSd = pick ("sd-v", false);
    relHub = pick ("hub-v", false);
    testEmx = pick ("v", true);
    testSd = pick ("sd-v", true);
    testHub = pick ("hub-v", true);
}

//== Dev mode =======================================================================
String Model::fileText (const String& f) const
{
    if (f == "emulon-x.md") return productMd (emx);
    if (f == "emulon-x-sd.md") return productMd (sd);
    if (f == "skins.md")
    {
        std::vector<std::pair<String, std::pair<StringPairArray, String>>> list;
        for (auto& s : skins)
            if (! s.community)
                list.push_back ({ s.name, { pairs ({ { "by", s.by }, { "image", s.image }, { "included", s.included ? "yes" : "" },
                                                     { "size", s.included ? String() : s.size }, { "download", s.included ? String() : s.download } }),
                                            s.descMd } });
        return listMd ("EMULON SKINS", skinsHead, list);
    }
    if (f == "banks.md")
    {
        std::vector<std::pair<String, std::pair<StringPairArray, String>>> list;
        for (auto& b : banks)
            if (! b.fromIndex)
                list.push_back ({ b.name, { pairs ({ { "by", b.by }, { "official", b.official ? "yes" : "" }, { "soon", b.soon ? "yes" : "" },
                                                     { "presets", b.presets }, { "size", b.size }, { "colour", b.image.isEmpty() ? String (b.colour) : String() },
                                                     { "image", b.image }, { "download", b.download } }),
                                            b.descMd } });
        return listMd ("EMULON BANKS", banksHead, list);
    }
    if (f == "news.md")
    {
        auto sorted = news;
        std::stable_sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.date > b.date; });
        std::vector<std::pair<String, std::pair<StringPairArray, String>>> list;
        for (auto& n : sorted)
            list.push_back ({ n.title, { pairs ({ { "date", n.date }, { "text", n.text }, { "open", n.open } }), {} } });
        return listMd ("NEWS", newsHead, list);
    }
    return {};
}

StringArray Model::changedFiles() const
{
    StringArray out;
    for (auto& f : textFiles())
        if (auto it = baseline.find (f); it == baseline.end() || it->second != fileText (f))
            out.add (f);
    return out;
}

void Model::snapshot()
{
    baseline.clear();
    for (auto& f : textFiles())
        baseline[f] = fileText (f);
}

void Model::discardChanges()
{
    applyTexts (baseText);
    applyIndexes (var(), var());   // puts the repository lists back
    newPictures.clear();
    snapshot();
    sendChangeMessage();
}

String Model::addPicture (const File& f)
{
    if (f.getSize() > 6 * 1024 * 1024)
        return {};
    MemoryBlock data;
    if (! f.loadFileAsData (data))
        return {};
    const auto img = ImageFileFormat::loadFrom (data.getData(), data.getSize());
    if (! img.isValid())
        return {};
    auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".jpeg") ext = ".jpg";
    if (ext != ".png" && ext != ".jpg") ext = ".png";
    const auto base = slugOf (f.getFileNameWithoutExtension());
    auto name = base + ext;
    for (int n = 2; newPictures.count (name); ++n)
        name = base + "-" + String (n) + ext;
    newPictures[name] = data;
    images.put (hubImageUrl (name), img);
    return name;
}

//== Installed ======================================================================
String Model::installedVersion (const String& key) const
{
    if (key == "hub")
        return hubVersion;
   #if JUCE_WINDOWS
    if (key == "emx") return uninstallValue (emxAppId, "DisplayVersion");
    if (key == "sd")  return uninstallValue (sdAppId, "DisplayVersion");
   #endif
    const auto rec = settings.get ("installed." + key);
    if (rec.isEmpty())
        return {};
    const auto path = rec.fromFirstOccurrenceOf ("|", false, false);
    if (path.isNotEmpty() && ! File (path).exists())
        return {};
    const auto v = rec.upToFirstOccurrenceOf ("|", false, false);
    return v.isEmpty() ? String ("1.0") : v;
}

File Model::installedFolder (const String& key) const
{
   #if JUCE_WINDOWS
    if (key == "emx") { const auto p = uninstallValue (emxAppId, "Inno Setup: App Path"); if (p.isNotEmpty()) return File (p); }
    if (key == "sd")  { const auto p = uninstallValue (sdAppId, "Inno Setup: App Path"); if (p.isNotEmpty()) return File (p); }
   #endif
    const auto path = settings.get ("installed." + key).fromFirstOccurrenceOf ("|", false, false);
    return path.isEmpty() ? File() : File (path).getParentDirectory();
}

File Model::appFile (const String& key) const
{
   #if JUCE_WINDOWS
    if (key == "emx")
    {
        // the standalone app goes into its own folder (picked when installing), not the VST3 folder
        auto dir = emxRegistry ("StandaloneFolder");
        if (dir.isEmpty()) dir = settings.get ("where.emx.1");
        if (dir.isEmpty()) dir = programs().getChildFile ("Emulon X").getFullPathName();
        return File (dir).getChildFile ("Emulon X.exe");
    }
    if (key == "sd")
    {
        auto dir = installedFolder ("sd");
        if (dir == File()) dir = programs().getChildFile ("Emulon X SD");
        return dir.getChildFile ("Emulon X SD.exe");
    }
   #elif JUCE_MAC
    if (key == "emx") return File ("/Applications/Emulon X.app");
    if (key == "sd")  return File ("/Applications/Emulon X SD.app");
   #endif
    ignoreUnused (key);
    return {};
}

std::vector<Model::Place> Model::placesFor (const String& key) const
{
    std::vector<Place> p;
    // where it is now, else where you put it last time, else the usual place
    auto choose = [this, &key] (int i, const File& now, const File& usual)
    {
        if (now != File())
            return now;
        const auto r = settings.get ("where." + key + "." + String (i));
        return r.isNotEmpty() ? File (r) : usual;
    };
    if (key == "emx")
    {
        File vst3 = programs().getChildFile ("Common Files").getChildFile ("VST3");
        File standalone = programs().getChildFile ("Emulon X");
        File base = documents().getChildFile ("Base Banks");
        File vst3Now, standaloneNow, baseNow;
       #if JUCE_WINDOWS
        vst3Now = installedFolder ("emx");
        if (auto s = emxRegistry ("StandaloneFolder"); s.isNotEmpty()) standaloneNow = File (s);
        if (auto b = emxRegistry ("BaseBanksFolder"); b.isNotEmpty()) baseNow = File (b);
       #elif JUCE_MAC
        vst3 = File ("/Library/Audio/Plug-Ins/VST3");
        standalone = File ("/Applications");
       #endif
        p.push_back ({ "Emulon X plugin (VST3)", "Ableton Live and most DAWs look in the standard VST3 folder.", choose (0, vst3Now, vst3) });
        p.push_back ({ "Standalone app", "Emulon X without a DAW.", choose (1, standaloneNow, standalone) });
        p.push_back ({ "Base banks", "The sounds that come with Emulon X.", choose (2, baseNow, base) });
    }
    else if (key == "sd")
    {
        p.push_back ({ "Emulon X SD (skin designer)", {}, choose (0, installedFolder ("sd"), programs().getChildFile ("Emulon X SD")) });
    }
    else if (key.startsWith ("skin:"))
    {
        SharedResourcePointer<emx::Settings> emxSettings;
        const auto dir = choose (0, installedFolder (key), emxSettings->getSkinsFolder());
        p.push_back ({ "Skin file (.emxd)", "Emulon X looks for skins in its skins folders. If you pick another folder, the Hub adds it to Emulon X's skins folders.", dir });
    }
    else if (key.startsWith ("bank:"))
    {
        SharedResourcePointer<emx::Settings> emxSettings;
        const auto base = emxSettings->getFolders (emx::Settings::Folder::base);
        const auto dir = choose (0, installedFolder (key), base.isEmpty() ? documents().getChildFile ("Base Banks") : File (base[0]));
        p.push_back ({ "Bank (.emxb)", "Your banks folder, so the bank shows up in Emulon X right away. If you pick another folder, the Hub adds it to Emulon X's bank folders.", dir });
    }
    return p;
}

void Model::record (const String& key, const String& version, const File& where)
{
    settings.set ("installed." + key, version + "|" + where.getFullPathName());
}

//== Installing =====================================================================
void Model::setJob (const String& key, const String& label, float p)
{
    currentJob = { key, label, p, true };
    sendChangeMessage();
}

void Model::endJob()
{
    currentJob = {};
    download = nullptr;
    if (installerWatch != nullptr)
    {
        // this often runs inside the watch timer's own callback: stop it now, delete it a moment later
        installerWatch->stopTimer();
        auto* t = installerWatch.release();
        MessageManager::callAsync ([t] { delete t; });
    }
    sendChangeMessage();
}

void Model::cancelJob()
{
    if (download != nullptr)
        download->cancelled = true;
}

void Model::installApp (const String& key, const Array<File>& folders, Done done, bool testBuild)
{
    if (currentJob.active)
        return;
    const auto rel = release (key, testBuild);
    if (! rel.found || (testBuild ? rel.assetApi.isEmpty() : rel.assetUrl.isEmpty()))
    {
        done (false, testBuild ? String ("There's no test build with an installer right now.")
                               : String ("There's no download for this yet. It appears here as soon as it's released on GitHub."));
        return;
    }
    for (int i = 0; i < folders.size(); ++i)
        settings.set ("where." + key + "." + String (i), folders[i].getFullPathName());
    if (! testBuild)
    {
        downloadApp (key, rel, rel.assetUrl, folders, done);
        return;
    }
    // a draft's file is private: ask GitHub (with your sign-in) for a short-lived link first
    setJob (key, "ASKING GITHUB...", 0.0f);
    Net::get().privateAssetLink (rel.assetApi, [this, key, rel, folders, done] (String link, String error)
    {
        if (link.isEmpty())
        {
            endJob();
            done (false, error);
            return;
        }
        downloadApp (key, rel, link, folders, done);
    });
}

void Model::downloadApp (const String& key, const Release& rel, const String& url, const Array<File>& folders, Done done)
{
   #if JUCE_WINDOWS
    const auto setup = File::getSpecialLocation (File::tempDirectory).getChildFile ("Emulon X Hub").getChildFile (rel.assetName);
   #else
    const auto setup = File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads").getChildFile (rel.assetName);
   #endif
    setJob (key, "DOWNLOADING 0%", 0.0f);
    const auto version = rel.version;
    download = Net::get().download (url, setup, [this, key] (float p)
    {
        setJob (key, p < 0 ? String ("DOWNLOADING...") : "DOWNLOADING " + String (roundToInt (p * 100.0f)) + "%", jmax (0.0f, p) * 0.8f);
    }, [this, key, folders, setup, version, done] (bool ok, String error)
    {
        if (! ok)
        {
            endJob();
            done (false, error);
            return;
        }
       #if JUCE_WINDOWS
        auto q = [] (const File& f) { return "\"" + f.getFullPathName() + "\""; };
        String params = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS";
        if (key == "emx" && folders.size() >= 3)
            params << " /DIR=" << q (folders[0]) << " /STANDALONEDIR=" << q (folders[1]) << " /BANKSDIR=" << q (folders[2]);
        else if (key == "sd" && folders.size() >= 1)
            params << " /DIR=" << q (folders[0]);
        else if (key == "hub")
            params = "/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS";
        runInstaller (key, setup, params, version, done);
       #else
        endJob();
        setup.revealToUser();
        done (true, "Downloaded to your Downloads folder. Open the zip and move the files where you want them.");
       #endif
    });
}

void Model::runInstaller (const String& key, const File& setup, const String& paramsIn, const String& version, Done done)
{
    const auto log = setup.getSiblingFile ("install-" + key + ".log");
    log.deleteFile();
    const auto params = paramsIn + " /LOG=\"" + log.getFullPathName() + "\"";
    setJob (key, "WAITING FOR WINDOWS...", 0.82f);
    // Windows asks for permission before the installer starts, and starting it waits for that answer:
    // do it in the background so the Hub doesn't freeze meanwhile
    Thread::launch ([this, key, setup, params, log, version, done]
    {
        const bool started = setup.startAsProcess (params);
        MessageManager::callAsync ([this, key, started, log, version, done]
        {
            if (! started)
            {
                endJob();
                done (false, "The installer didn't start. If Windows asked for permission, please click Yes next time.");
                return;
            }
            if (key == "hub")
            {
                // the installer closes the Hub and opens the new one when it's done
                endJob();
                done (true, "hub-restart");
                return;
            }
            watchInstaller (key, log, version, done);
        });
    });
}

void Model::watchInstaller (const String& key, const File& log, const String& version, Done done)
{
    setJob (key, "INSTALLING...", 0.85f);
    auto timer = std::make_unique<LambdaTimer>();
    const auto started = Time::getMillisecondCounter();
    timer->f = [this, key, log, started, version, done]
    {
        auto finish = done;   // endJob() deletes this timer (a moment later); keep what we still need
        const auto waited = Time::getMillisecondCounter() - started;
        if (! log.existsAsFile())
        {
            if (waited > 120000)
            {
                endJob();
                finish (false, "The installer didn't start. If Windows asked for permission, please click Yes next time.");
            }
            return;
        }
        const auto text = log.loadFileAsString();
        const bool closed = text.contains ("Log closed.");
        if (text.contains ("Installation process succeeded"))
        {
            if (! closed && waited < 30 * 60 * 1000)
                return;
            const auto v = version;
            endJob();
            finish (true, v);
            return;
        }
        if (closed || waited > 30 * 60 * 1000)
        {
            endJob();
            finish (false, "The installer stopped before it was done.");
            return;
        }
        setJob (key, "INSTALLING...", jmin (0.98f, currentJob.progress + 0.004f));
    };
    timer->startTimer (500);
    installerWatch = std::move (timer);
}

namespace
{
bool checksumOk (const File& f, const String& sha)
{
    if (sha.isEmpty())
        return true;
    FileInputStream in (f);
    return in.openedOk() && SHA256 (in).toHexString().equalsIgnoreCase (sha);
}
} // namespace

void Model::installSkin (const SkinItem& s, const File& folder, Done done)
{
    if (currentJob.active)
        return;
    if (s.download.isEmpty())
    {
        done (false, "This skin has no download link yet.");
        return;
    }
    const auto key = "skin:" + s.id;
    settings.set ("where." + key + ".0", folder.getFullPathName());
    const auto dest = folder.getChildFile (File::createLegalFileName (s.name.isNotEmpty() ? s.name : s.id) + ".emxd");
    setJob (key, "DOWNLOADING 0%", 0.0f);
    download = Net::get().download (s.download, dest, [this, key] (float p)
    {
        setJob (key, p < 0 ? String ("DOWNLOADING...") : "DOWNLOADING " + String (roundToInt (p * 100.0f)) + "%", jmax (0.0f, p));
    }, [this, key, s, dest, folder, done] (bool ok, String error)
    {
        endJob();
        if (ok && ! checksumOk (dest, s.sha256))
        {
            dest.deleteFile();
            ok = false;
            error = "The download was damaged. Please try again.";
        }
        if (! ok)
        {
            done (false, error);
            return;
        }
        SharedResourcePointer<emx::Settings> emxSettings;
        if (! emxSettings->getFolders (emx::Settings::Folder::skins).contains (folder.getFullPathName()))
            emxSettings->addFolder (emx::Settings::Folder::skins, folder);
        record (key, s.version.isEmpty() ? String ("1.0") : s.version, dest);
        done (true, {});
    });
}

void Model::installBank (const BankItem& b, const File& folder, Done done)
{
    if (currentJob.active)
        return;
    if (b.link().isEmpty())
    {
        done (false, "This bank has no download link yet.");
        return;
    }
    const auto key = "bank:" + b.id;
    settings.set ("where." + key + ".0", folder.getFullPathName());
    auto fileName = URL (b.link()).getFileName();
    if (! fileName.endsWithIgnoreCase (".emxb"))
        fileName = File::createLegalFileName (b.name) + ".emxb";
    const auto dest = folder.getChildFile (URL::removeEscapeChars (fileName));
    setJob (key, "DOWNLOADING 0%", 0.0f);
    download = Net::get().download (b.link(), dest, [this, key] (float p)
    {
        setJob (key, p < 0 ? String ("DOWNLOADING...") : "DOWNLOADING " + String (roundToInt (p * 100.0f)) + "%", jmax (0.0f, p));
    }, [this, key, b, dest, folder, done] (bool ok, String error)
    {
        endJob();
        if (ok && ! checksumOk (dest, b.sha()))
        {
            dest.deleteFile();
            ok = false;
            error = "The download was damaged. Please try again.";
        }
        if (! ok)
        {
            done (false, error);
            return;
        }
        SharedResourcePointer<emx::Settings> emxSettings;
        const auto path = folder.getFullPathName();
        if (! emxSettings->getFolders (emx::Settings::Folder::base).contains (path)
            && ! emxSettings->getFolders (emx::Settings::Folder::other).contains (path))
            emxSettings->addFolder (emx::Settings::Folder::other, folder);
        record (key, b.ver().isEmpty() ? String ("1.0") : b.ver(), dest);
        done (true, {});
    });
}

//== Notifications ==================================================================
std::vector<Model::Note> Model::notes() const
{
    std::vector<Note> list;
    const auto emxHave = installedVersion ("emx");
    if (relEmx.found && emxHave.isNotEmpty() && isNewer (relEmx.version, emxHave))
        list.push_back ({ "update-emx-" + relEmx.version, "Emulon X " + relEmx.version + " is available", "From GitHub Releases - see what's new", "emulon-x", true });
    const auto sdHave = installedVersion ("sd");
    if (relSd.found && sdHave.isNotEmpty() && isNewer (relSd.version, sdHave))
        list.push_back ({ "update-sd-" + relSd.version, "Emulon X SD " + relSd.version + " is available", "An update for the skin designer", "sd", true });
    if (relHub.found && isNewer (relHub.version, hubVersion))
        list.push_back ({ "update-hub-" + relHub.version, "Emulon X Hub " + relHub.version + " is available", "Click to update the Hub", "hub", true });
    for (auto& b : banks)
        if (! b.isSoon() && b.link().isNotEmpty() && b.soon && notifyWhenOut (b.id) && installedVersion ("bank:" + b.id).isEmpty())
            list.push_back ({ "bank-out-" + b.id, b.name + " is out", "Download it now", "banks: " + b.name, true });
    auto sorted = news;
    std::stable_sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.date > b.date; });
    for (auto& n : sorted)
        list.push_back ({ "news-" + slugOf (n.title) + "-" + n.date, n.title, n.text, n.open, false });
    const auto known = settings.list ("knownSkins");
    for (auto& s : skins)
        if (s.community && ! known.contains (s.id))
            list.push_back ({ "skin-" + s.id, "New community skin: " + s.name, "By " + s.by + " - made in Emulon X SD", "skins: " + s.name, true });
    return list;
}

int Model::unreadCount() const
{
    const auto seen = settings.list ("seenNotes");
    int n = 0;
    for (auto& note : notes())
        if (! seen.contains (note.id))
            ++n;
    return n;
}

void Model::markAllSeen()
{
    auto seen = settings.list ("seenNotes");
    for (auto& note : notes())
        seen.addIfNotAlreadyThere (note.id);
    while (seen.size() > 300)
        seen.remove (0);
    settings.setList ("seenNotes", seen);
    sendChangeMessage();
}

bool Model::notifyWhenOut (const String& bankId) const { return ! settings.list ("mutedBanks").contains (bankId); }

void Model::setNotifyWhenOut (const String& bankId, bool on)
{
    auto l = settings.list ("mutedBanks");
    if (on) l.removeString (bankId); else l.addIfNotAlreadyThere (bankId);
    settings.setList ("mutedBanks", l);
    sendChangeMessage();
}
} // namespace emx::hub
