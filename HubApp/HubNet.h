// Emulon X Hub — talking to GitHub: reading files and lists, the GitHub API (with your sign-in),
// signing in (device flow: you confirm a code on github.com), downloads, saving files, submitting skins.
// Everything runs in the background; callbacks come back on the message thread.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace emx::hub
{
inline constexpr const char* repoOwner = "KiruTheProot-Creative";
inline constexpr const char* mainRepo = "EmulonX";
inline constexpr const char* skinsRepo = "EmulonX-Skins";
inline constexpr const char* banksRepo = "EmulonX-Banks";

/** https://raw.githubusercontent.com/<owner>/<repo>/main/<path> (or the folder in EMX_HUB_SOURCE for testing). */
juce::String rawUrl (const juce::String& repo, const juce::String& path);

struct Response
{
    int status = 0;
    juce::MemoryBlock data;
    juce::String error;
    juce::StringPairArray headers;
    bool ok() const { return status >= 200 && status < 300; }
    juce::String text() const { return data.toString(); }
    juce::var json() const { return juce::JSON::parse (text()); }
};

class Net
{
public:
    using Callback = std::function<void (const Response&)>;
    static Net& get();

    /** Your GitHub sign-in (empty = not signed in). */
    void setToken (const juce::String& t) { const juce::ScopedLock sl (lock); token = t; }
    juce::String getToken() const { const juce::ScopedLock sl (lock); return token; }

    /** Any web request. `body` is sent as is (JSON or form data). */
    void request (const juce::String& method, const juce::String& url, const juce::String& body, const juce::String& headers, Callback);
    /** A file from the web (raw GitHub files, pictures). */
    void fetch (const juce::String& url, Callback cb) { request ("GET", url, {}, {}, std::move (cb)); }
    /** The GitHub API: path like "/repos/owner/repo/releases". Uses your sign-in when there is one. */
    void api (const juce::String& method, const juce::String& path, const juce::var& body, Callback);

    /** Downloads into `dest` (via a temporary file). progress: 0..1 (or -1 when the size is unknown). */
    struct Download
    {
        std::atomic<bool> cancelled { false };
    };
    std::shared_ptr<Download> download (const juce::String& url, const juce::File& dest,
                                        std::function<void (float)> progress, std::function<void (bool ok, juce::String error)> done);

    /** The real download address of a draft release's file (only with a sign-in that can see drafts):
        asks GitHub's API for it without following the redirect. done gets an empty string if it can't. */
    void privateAssetLink (const juce::String& assetApiUrl, std::function<void (juce::String link, juce::String error)> done);

    void shutdown();   // stop background work (app exit)

private:
    juce::ThreadPool pool { 4 };
    juce::CriticalSection lock;
    juce::String token;
};

//== Signing in with GitHub (device flow) ==========================================
/** Shows a code that you confirm on github.com/login/device; then calls onDone with the sign-in. */
class GitHubSignIn final : private juce::Timer
{
public:
    std::function<void (const juce::String& code, const juce::String& url)> onCode;
    std::function<void (bool ok, const juce::String& tokenOrError)> onDone;

    /** clientId: the Emulon X Hub's GitHub app (from hub/hub.json). scope e.g. "public_repo". */
    void start (const juce::String& clientId, const juce::String& scope);
    void cancel() { stopTimer(); deviceCode = {}; }
    ~GitHubSignIn() override { stopTimer(); *alive = false; }

private:
    void timerCallback() override;
    juce::String clientId, deviceCode;
    int intervalMs = 5000;
    double expires = 0;
    bool waiting = false;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

//== Saving and submitting =========================================================
struct RepoFile
{
    juce::String path;
    juce::MemoryBlock data;
};

/** Writes files into a repository branch with your sign-in (one commit per file, same message). */
void commitFiles (const juce::String& owner, const juce::String& repo, const juce::String& branch, std::vector<RepoFile> files,
                  const juce::String& message, std::function<void (bool ok, juce::String error)> done);

/** Sends a skin in for review: copies EmulonX-Skins to your account (a fork), adds skins/<slug>/ there and
    opens a pull request. step() reports progress; done gets the pull request's web address. */
void submitSkin (const juce::String& slug, std::vector<RepoFile> files, const juce::String& title, const juce::String& body,
                 std::function<void (const juce::String& step)> step, std::function<void (bool ok, juce::String urlOrError)> done);
} // namespace emx::hub
