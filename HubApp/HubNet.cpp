// SPDX-License-Identifier: AGPL-3.0-or-later
#include "HubNet.h"

namespace emx::hub
{
using namespace juce;

String rawUrl (const String& repo, const String& path)
{
    // for testing without the internet: EMX_HUB_SOURCE=<folder> with <repo>/<path> files in it
    const auto local = SystemStats::getEnvironmentVariable ("EMX_HUB_SOURCE", {});
    if (local.isNotEmpty())
        return URL (File (local).getChildFile (repo).getChildFile (path)).toString (false);
    return "https://raw.githubusercontent.com/" + String (repoOwner) + "/" + repo + "/main/" + path;
}

Net& Net::get()
{
    static Net n;
    return n;
}

void Net::shutdown() { pool.removeAllJobs (true, 4000); }

namespace
{
Response perform (const String& method, const String& address, const String& body, const String& headers, int redirects = 8)
{
    Response r;
    URL url (address);
    if (url.isLocalFile())
    {
        const auto f = url.getLocalFile();
        if (f.existsAsFile() && f.loadFileAsData (r.data))
            r.status = 200;
        else
            r.status = 404;
        return r;
    }
    if (body.isNotEmpty())
        url = url.withPOSTData (body);
    auto options = URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (20000)
                       .withExtraHeaders ("User-Agent: EmulonX-Hub\r\n" + headers)
                       .withStatusCode (&r.status)
                       .withResponseHeaders (&r.headers)
                       .withNumRedirectsToFollow (redirects)
                       .withHttpRequestCmd (method);
    if (auto in = url.createInputStream (options))
        in->readIntoMemoryBlock (r.data);
    else
        r.error = "No connection";
    if (! r.ok() && r.error.isEmpty())
        r.error = "HTTP " + String (r.status);
    return r;
}
} // namespace

void Net::request (const String& method, const String& url, const String& body, const String& headers, Callback cb)
{
    pool.addJob ([method, url, body, headers, cb]
    {
        auto r = perform (method, url, body, headers);
        MessageManager::callAsync ([cb, r] { if (cb) cb (r); });
    });
}

void Net::api (const String& method, const String& path, const var& body, Callback cb)
{
    String headers = "Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    const auto t = getToken();
    if (t.isNotEmpty())
        headers << "Authorization: Bearer " << t << "\r\n";
    String text;
    if (! body.isVoid())
    {
        text = JSON::toString (body, true);
        headers << "Content-Type: application/json\r\n";
    }
    request (method, "https://api.github.com" + path, text, headers, std::move (cb));
}

void Net::privateAssetLink (const String& assetApiUrl, std::function<void (String, String)> done)
{
    const auto t = getToken();
    pool.addJob ([assetApiUrl, t, done]
    {
        String headers = "Accept: application/octet-stream\r\n";
        if (t.isNotEmpty())
            headers << "Authorization: Bearer " << t << "\r\n";
        auto r = perform ("GET", assetApiUrl, {}, headers, 0);
        String link;
        for (auto& k : r.headers.getAllKeys())
            if (k.equalsIgnoreCase ("location"))
                link = r.headers[k];
        if (URL (assetApiUrl).isLocalFile())
            link = assetApiUrl;
        const auto error = link.isNotEmpty() ? String() : "GitHub didn't hand out the test build (" + (r.error.isNotEmpty() ? r.error : String (r.status)) + ")";
        MessageManager::callAsync ([done, link, error] { done (link, error); });
    });
}

std::shared_ptr<Net::Download> Net::download (const String& address, const File& dest, std::function<void (float)> progress,
                                              std::function<void (bool, String)> done)
{
    auto handle = std::make_shared<Download>();
    pool.addJob ([address, dest, progress, done, handle]
    {
        auto finish = [done] (bool ok, const String& e) { MessageManager::callAsync ([done, ok, e] { if (done) done (ok, e); }); };
        const auto tmp = dest.getSiblingFile (dest.getFileName() + ".download");
        dest.getParentDirectory().createDirectory();
        URL url (address);
        if (url.isLocalFile())
        {
            const bool ok = url.getLocalFile().copyFileTo (dest);
            finish (ok, ok ? String() : String ("Can't copy the file"));
            return;
        }
        int status = 0;
        auto in = url.createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (20000)
                                             .withExtraHeaders ("User-Agent: EmulonX-Hub\r\n")
                                             .withStatusCode (&status)
                                             .withNumRedirectsToFollow (8));
        if (in == nullptr || status >= 400)
        {
            finish (false, status >= 400 ? "The download isn't available (HTTP " + String (status) + ")" : String ("No connection"));
            return;
        }
        const auto total = in->getTotalLength();
        {
            tmp.deleteFile();
            FileOutputStream out (tmp);
            if (! out.openedOk())
            {
                finish (false, "Can't write to " + dest.getParentDirectory().getFullPathName());
                return;
            }
            HeapBlock<char> buffer (64 * 1024);
            int64 got = 0;
            double lastReport = 0;
            while (! in->isExhausted())
            {
                if (handle->cancelled)
                {
                    out.flush();
                    tmp.deleteFile();
                    finish (false, "Cancelled");
                    return;
                }
                const auto n = in->read (buffer, 64 * 1024);
                if (n <= 0)
                    break;
                out.write (buffer, (size_t) n);
                got += n;
                const auto now = Time::getMillisecondCounterHiRes();
                if (progress && now - lastReport > 100.0)
                {
                    lastReport = now;
                    const float p = total > 0 ? (float) ((double) got / (double) total) : -1.0f;
                    MessageManager::callAsync ([progress, p] { progress (p); });
                }
            }
            out.flush();
            if (total > 0 && got < total)
            {
                tmp.deleteFile();
                finish (false, "The download stopped early");
                return;
            }
        }
        if (! tmp.moveFileTo (dest))
        {
            tmp.deleteFile();
            finish (false, "Can't write " + dest.getFullPathName());
            return;
        }
        finish (true, {});
    });
    return handle;
}

//== Sign in =======================================================================
void GitHubSignIn::start (const String& id, const String& scope)
{
    clientId = id;
    deviceCode = {};
    auto alive_ = alive;
    Net::get().request ("POST", "https://github.com/login/device/code", "client_id=" + URL::addEscapeChars (id, true) + "&scope=" + URL::addEscapeChars (scope, true),
                        "Accept: application/json\r\nContent-Type: application/x-www-form-urlencoded\r\n", [this, alive_] (const Response& r)
    {
        if (! *alive_)
            return;
        const auto j = r.json();
        deviceCode = j.getProperty ("device_code", "").toString();
        if (! r.ok() || deviceCode.isEmpty())
        {
            if (onDone) onDone (false, "GitHub didn't answer (" + (r.error.isNotEmpty() ? r.error : j.getProperty ("error_description", "unknown").toString()) + ")");
            return;
        }
        intervalMs = jmax (5, (int) j.getProperty ("interval", 5)) * 1000;
        expires = Time::getMillisecondCounterHiRes() + (double) (int) j.getProperty ("expires_in", 900) * 1000.0;
        if (onCode) onCode (j.getProperty ("user_code", "").toString(), j.getProperty ("verification_uri", "https://github.com/login/device").toString());
        startTimer (intervalMs);
    });
}

void GitHubSignIn::timerCallback()
{
    if (deviceCode.isEmpty() || waiting)
        return;
    if (Time::getMillisecondCounterHiRes() > expires)
    {
        stopTimer();
        if (onDone) onDone (false, "The code ran out - please try again");
        return;
    }
    waiting = true;
    auto alive_ = alive;
    Net::get().request ("POST", "https://github.com/login/oauth/access_token",
                        "client_id=" + URL::addEscapeChars (clientId, true) + "&device_code=" + URL::addEscapeChars (deviceCode, true)
                            + "&grant_type=urn:ietf:params:oauth:grant-type:device_code",
                        "Accept: application/json\r\nContent-Type: application/x-www-form-urlencoded\r\n", [this, alive_] (const Response& r)
    {
        if (! *alive_)
            return;
        waiting = false;
        const auto j = r.json();
        const auto token = j.getProperty ("access_token", "").toString();
        const auto error = j.getProperty ("error", "").toString();
        if (token.isNotEmpty())
        {
            stopTimer();
            if (onDone) onDone (true, token);
        }
        else if (error == "slow_down")
            startTimer (intervalMs += 5000);
        else if (error.isNotEmpty() && error != "authorization_pending")
        {
            stopTimer();
            if (onDone) onDone (false, error == "access_denied" ? String ("You cancelled the sign-in") : j.getProperty ("error_description", error).toString());
        }
    });
}

//== Saving files ==================================================================
namespace
{
void commitNext (String owner, String repo, String branch, std::shared_ptr<std::vector<RepoFile>> files, size_t index, String message,
                 std::function<void (bool, String)> done)
{
    if (index >= files->size())
    {
        done (true, {});
        return;
    }
    const auto path = (*files)[index].path;
    const auto base = "/repos/" + owner + "/" + repo + "/contents/" + URL::addEscapeChars (path, false).replace ("%2F", "/");
    Net::get().api ("GET", base + "?ref=" + branch, var(), [=] (const Response& existing)
    {
        auto* o = new DynamicObject();
        o->setProperty ("message", message);
        o->setProperty ("branch", branch);
        const auto& data = (*files)[index].data;
        o->setProperty ("content", Base64::toBase64 (data.getData(), data.getSize()));
        if (existing.ok())
            o->setProperty ("sha", existing.json().getProperty ("sha", ""));
        Net::get().api ("PUT", base, var (o), [=] (const Response& r)
        {
            if (! r.ok())
            {
                done (false, "Saving " + path + " failed (" + r.error + ": " + r.json().getProperty ("message", "").toString() + ")");
                return;
            }
            commitNext (owner, repo, branch, files, index + 1, message, done);
        });
    });
}
} // namespace

void commitFiles (const String& owner, const String& repo, const String& branch, std::vector<RepoFile> files, const String& message,
                  std::function<void (bool, String)> done)
{
    commitNext (owner, repo, branch, std::make_shared<std::vector<RepoFile>> (std::move (files)), 0, message, std::move (done));
}

void submitSkin (const String& slug, std::vector<RepoFile> files, const String& title, const String& body,
                 std::function<void (const String&)> step, std::function<void (bool, String)> done)
{
    auto shared = std::make_shared<std::vector<RepoFile>> (std::move (files));
    const String upstream = "/repos/" + String (repoOwner) + "/" + skinsRepo;
    step ("Checking your GitHub account...");
    Net::get().api ("GET", "/user", var(), [=] (const Response& me)
    {
        const auto login = me.json().getProperty ("login", "").toString();
        if (! me.ok() || login.isEmpty())
        {
            done (false, "Please sign in with GitHub again");
            return;
        }
        step ("Making your copy of the skins collection...");
        Net::get().api ("POST", upstream + "/forks", var (new DynamicObject()), [=] (const Response& fork)
        {
            if (! fork.ok() && fork.status != 202)
            {
                done (false, "GitHub didn't let us copy the skins collection (" + fork.error + ")");
                return;
            }
            const auto forkName = fork.json().getProperty ("full_name", login + "/" + skinsRepo).toString();
            // a fresh copy takes a moment; then bring an older one up to date
            auto attempt = std::make_shared<int> (0);
            auto waitForFork = std::make_shared<std::function<void()>>();
            *waitForFork = [=]
            {
                Net::get().api ("GET", upstream + "/git/ref/heads/main", var(), [=] (const Response& ref)
                {
                    const auto sha = ref.json().getProperty ("object", var()).getProperty ("sha", "").toString();
                    if (! ref.ok() || sha.isEmpty())
                    {
                        done (false, "The skins collection can't be read right now");
                        return;
                    }
                    auto* sync = new DynamicObject();
                    sync->setProperty ("branch", "main");
                    Net::get().api ("POST", "/repos/" + forkName + "/merge-upstream", var (sync), [=] (const Response&)
                    {
                        const auto branch = "skin-" + slug + "-" + String (Time::currentTimeMillis() / 1000);
                        auto* refBody = new DynamicObject();
                        refBody->setProperty ("ref", "refs/heads/" + branch);
                        refBody->setProperty ("sha", sha);
                        Net::get().api ("POST", "/repos/" + forkName + "/git/refs", var (refBody), [=] (const Response& made)
                        {
                            if (! made.ok())
                            {
                                if (++*attempt < 10)
                                {
                                    Timer::callAfterDelay (3000, [waitForFork] { (*waitForFork)(); });
                                    return;
                                }
                                done (false, "Your copy of the skins collection isn't ready (" + made.error + ")");
                                return;
                            }
                            step ("Uploading the skin...");
                            const auto owner = forkName.upToFirstOccurrenceOf ("/", false, false);
                            commitFiles (owner, skinsRepo, branch, *shared, "Add skin: " + title, [=] (bool ok, String error)
                            {
                                if (! ok)
                                {
                                    done (false, error);
                                    return;
                                }
                                step ("Sending it in for review...");
                                auto* pr = new DynamicObject();
                                pr->setProperty ("title", "New skin: " + title);
                                pr->setProperty ("head", owner + ":" + branch);
                                pr->setProperty ("base", "main");
                                pr->setProperty ("body", body);
                                Net::get().api ("POST", upstream + "/pulls", var (pr), [=] (const Response& pull)
                                {
                                    if (! pull.ok())
                                        done (false, "Sending for review failed (" + pull.error + ": " + pull.json().getProperty ("message", "").toString() + ")");
                                    else
                                        done (true, pull.json().getProperty ("html_url", "").toString());
                                    *waitForFork = nullptr;   // break the cycle
                                });
                            });
                        });
                    });
                });
            };
            Timer::callAfterDelay (1500, [waitForFork] { (*waitForFork)(); });
        });
    });
}
} // namespace emx::hub
