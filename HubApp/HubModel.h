// Emulon X Hub — everything the Hub shows and does: the texts (hub/*.md in the main repository), the skins and banks
// (their own repositories), the version numbers (GitHub Releases), what's installed, installing, and your settings.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "HubNet.h"
#include <map>

namespace emx::hub
{
//== The texts ======================================================================
struct Product
{
    juce::String key;                      // "emx" or "sd"
    juce::String name, sub, tags, descMd, logMd;
    juce::StringArray feats, shotFiles, shotCaptions;
    juce::StringPairArray extra;           // version / size / download, kept as written
};

struct SkinItem
{
    juce::String id, name, by, image, descMd, size, download, sha256, version, preview;
    bool included = false;     // comes with Emulon X
    bool community = false;    // from the EmulonX-Skins repository
};

struct BankItem
{
    juce::String id, name, by, descMd, presets, size, download, image, version, sha256;
    bool official = false, soon = false, fromIndex = false;
    int colour = 0;
    // a bank written in hub/banks.md that's also released in the banks repository: the repository's facts win
    bool released = false;
    juce::String rDownload, rVersion, rSha, rSize, rPresets;

    bool isSoon() const { return released ? false : soon; }
    juce::String link() const { return released ? rDownload : download; }
    juce::String ver() const { return released ? rVersion : version; }
    juce::String sha() const { return released ? rSha : sha256; }
    juce::String sizeText() const { return released && rSize.isNotEmpty() ? rSize : size; }
    juce::String presetsText() const { return released && rPresets.isNotEmpty() ? rPresets : presets; }
};

struct NewsItem
{
    juce::String title, text, date, open;
};

struct Release
{
    juce::String tag, version, name, notes, page, assetUrl, assetName;
    juce::String assetApi;     // drafts: the file is only reachable through the API, with a sign-in
    juce::int64 assetSize = 0;
    bool draft = false;
    bool found = false;
};

//== Markdown files (the simple format described in hub/README.md) ==================
struct MdDoc
{
    juce::StringPairArray meta;
    juce::String intro;
    struct Section { juce::String title; juce::StringArray lines; };
    std::vector<Section> secs;
};
MdDoc parseMd (const juce::String&);
Product productFrom (const MdDoc&, const juce::String& key);
juce::String productMd (const Product&);

juce::String slugOf (const juce::String&);
bool yes (const juce::String&);
/** True if version a is newer than b ("1.2.0ea" style: numbers first, then ea < af < bv < bf < ff). */
bool isNewer (const juce::String& a, const juce::String& b);
juce::String formatBytes (juce::int64);
juce::String today();

//== Pictures from the web (kept on disk so they show straight away next time) ======
class ImageStore final : public juce::ChangeBroadcaster
{
public:
    /** The picture, or an invalid image while it loads (you get a change message when it arrives). */
    juce::Image get (const juce::String& url);
    /** A picture that isn't on the web yet (dev mode uploads). */
    void put (const juce::String& url, const juce::Image&);

    juce::File cacheDir;

private:
    std::map<juce::String, juce::Image> images;
    std::set<juce::String> loading, failed;
};

//== Your settings ==================================================================
class HubSettings
{
public:
    HubSettings();
    juce::PropertiesFile& props() { return *file; }
    juce::String get (const juce::String& k, const juce::String& fallback = {}) const { return file->getValue (k, fallback); }
    void set (const juce::String& k, const juce::var& v) { file->setValue (k, v); file->saveIfNeeded(); }
    bool flag (const juce::String& k, bool fallback) const { return file->getBoolValue (k, fallback); }
    juce::StringArray list (const juce::String& k) const;
    void setList (const juce::String& k, const juce::StringArray&);
    juce::File cacheFolder() const;

private:
    std::unique_ptr<juce::PropertiesFile> file;
};

//== The Hub's model ==================================================================
class Model final : public juce::ChangeBroadcaster
{
public:
    Model();

    // what the Hub shows
    Product emx, sd;
    std::vector<SkinItem> skins;
    std::vector<BankItem> banks;
    std::vector<NewsItem> news;
    Release relEmx, relSd, relHub;
    /** Test builds: draft releases, which GitHub only shows to accounts that can change the repository (dev mode). */
    Release testEmx, testSd, testHub;
    const Release& release (const juce::String& key, bool test) const
    {
        return key == "emx" ? (test ? testEmx : relEmx) : key == "sd" ? (test ? testSd : relSd) : (test ? testHub : relHub);
    }
    bool textsLive = false;       // came from GitHub just now (otherwise the last copy or the built-in one)
    bool releasesLive = false;
    juce::String clientId;        // the Hub's GitHub sign-in app (hub/hub.json)

    HubSettings settings;
    ImageStore images;

    /** Loads everything from GitHub. done() runs once it's all in (or failed). */
    void refresh (std::function<void()> done);
    juce::String hubImageUrl (const juce::String& file) const;

    //== Dev mode: the texts as files ===============================================
    static const juce::StringArray& textFiles();   // emulon-x.md, emulon-x-sd.md, skins.md, banks.md, news.md
    juce::String fileText (const juce::String& file) const;
    juce::StringArray changedFiles() const;
    void snapshot();                // the current texts are what's on GitHub
    void discardChanges();
    std::map<juce::String, juce::MemoryBlock> newPictures;   // file name -> data, waiting to be saved
    juce::String addPicture (const juce::File&);             // returns the file name in hub/images (or empty)

    //== Installed things =========================================================
    juce::String installedVersion (const juce::String& key) const;   // "emx", "sd", "hub", "skin:<id>", "bank:<id>"
    juce::File installedFolder (const juce::String& key) const;
    /** The program to start: the Emulon X standalone app ("emx") or Emulon X SD ("sd"). */
    juce::File appFile (const juce::String& key) const;
    struct Place { juce::String label, note; juce::File folder; };
    /** The folders an install of `key` asks about, filled in with where it is now (or the usual place). */
    std::vector<Place> placesFor (const juce::String& key) const;

    //== Installing (one at a time) ===============================================
    struct Job { juce::String key, label; float progress = 0.0f; bool active = false; };
    const Job& job() const { return currentJob; }
    using Done = std::function<void (bool ok, const juce::String& message)>;
    void installApp (const juce::String& key, const juce::Array<juce::File>& folders, Done, bool testBuild = false);
    void installSkin (const SkinItem&, const juce::File& folder, Done);
    void installBank (const BankItem&, const juce::File& folder, Done);
    void cancelJob();

    //== Notifications ===========================================================
    struct Note { juce::String id, title, text, open; bool automatic = false; };
    std::vector<Note> notes() const;
    int unreadCount() const;
    void markAllSeen();
    bool notifyWhenOut (const juce::String& bankId) const;
    void setNotifyWhenOut (const juce::String& bankId, bool);

    std::vector<SkinItem> fromIndexSkins;   // community skins as they came from the skins repository
    std::vector<BankItem> fromIndexBanks;

private:
    void applyTexts (const std::map<juce::String, juce::String>&);
    void applyIndexes (const juce::var& skinsIndex, const juce::var& banksIndex);
    void applyReleases (const juce::var&);
    void setJob (const juce::String& key, const juce::String& label, float p);
    void endJob();
    void record (const juce::String& key, const juce::String& version, const juce::File& where);
    void runInstaller (const juce::String& key, const juce::File& setup, const juce::String& params, const juce::String& version, Done);
    void watchInstaller (const juce::String& key, const juce::File& log, const juce::String& version, Done);
    void downloadApp (const juce::String& key, const Release&, const juce::String& url, const juce::Array<juce::File>& folders, Done);

    std::map<juce::String, juce::String> baseline;
    std::map<juce::String, juce::String> baseText;   // the texts as loaded, to undo dev edits
    Job currentJob;
    std::shared_ptr<Net::Download> download;
    std::unique_ptr<juce::Timer> installerWatch;
    bool reapplyBanks = false;
};

inline constexpr const char* hubVersion =
#ifdef EMX_HUB_VERSION
    EMX_HUB_VERSION;
#else
    "0.1af";
#endif
} // namespace emx::hub
