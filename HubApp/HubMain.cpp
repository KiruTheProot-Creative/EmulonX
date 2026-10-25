// Emulon X Hub — the launcher: download and update Emulon X, Emulon X SD, skins and banks.
//     EmulonXHub                      opens the Hub
//     EmulonXHub --tray               starts quietly in the tray (used when it starts with Windows)
//     EmulonXHub --self-test <folder> renders screenshots, checks the text files, then quits
//                                     (set EMX_HUB_SOURCE to a folder with EmulonX/hub/... to run without the internet)
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "HubUi.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "ui/EmxLookAndFeel.h"
#include "ui/Theme.h"
#include "PreviewView.h"
#include "HubAssets.h"

using namespace juce;

namespace
{
using namespace emx::hub;

/** Screenshots of every page plus checks of the text files. Returns the number of failures. */
int selfTest (const File& dir)
{
    dir.createDirectory();
    int failures = 0;
    auto check = [&failures] (bool ok, const String& what)
    {
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        if (! ok) ++failures;
    };
    auto pump = [] (int ms = 80) { MessageManager::getInstance()->runDispatchLoopUntil (ms); };

    // the text format: reading and writing a file gives the same file back
    const auto source = SystemStats::getEnvironmentVariable ("EMX_HUB_SOURCE", {});
    const auto hubFolder = File (source).getChildFile ("EmulonX").getChildFile ("hub");
    check (isNewer ("1.2.1ea", "1.2.0ff") && isNewer ("1.2.0af", "1.2.0ea") && ! isNewer ("1.2.0ea", "1.2.0ea") && isNewer ("0.2af", "0.1af"),
           "version numbers compare (numbers, then ea < af < bv < bf < ff)");
    check (slugOf (String::fromUTF8 ("EMULON XP \xc2\xb7 LIGHT")) == "emulon-xp-light", "names turn into folder names");

    Model model;
    bool done = false;
    model.refresh ([&done] { done = true; });
    for (int i = 0; i < 200 && ! done; ++i) pump (50);
    check (done, "loads the texts, skins, banks and releases");
    if (source.isNotEmpty())
    {
        check (model.textsLive, "texts come from the hub folder");
        for (auto& f : Model::textFiles())
        {
            const auto original = hubFolder.getChildFile (f).loadFileAsString().removeCharacters ("\r");
            check (model.fileText (f) == original, f + " is written back exactly as it was read");
            if (model.fileText (f) != original)
                hubFolder.getSiblingFile ("roundtrip-" + f).replaceWithText (model.fileText (f));
        }
        check (model.relEmx.found && model.relEmx.version == "1.2.0ea", "the newest Emulon X release is found (" + model.relEmx.version + ")");
        check (model.relSd.found && model.relSd.version == "0.1af", "Emulon X SD releases have their own tags (" + model.relSd.version + ")");
        check (model.relHub.found && model.relHub.version == "0.2af", "Hub releases have their own tags (" + model.relHub.version + ")");
        check (model.testSd.found && model.testSd.version == "0.2af" && ! model.testEmx.found, "a draft release is a test build (" + model.testSd.version + ")");
        {
            String link;
            bool got = false;
            Net::get().privateAssetLink (model.testSd.assetApi, [&] (String l, String) { link = l; got = true; });
            for (int i = 0; i < 100 && ! got; ++i) pump (30);
            check (link.isNotEmpty(), "a test build's private file can be reached");
        }
        bool community = false, released = false;
        for (auto& s : model.skins) community = community || (s.community && s.preview.isNotEmpty());
        for (auto& b : model.banks) released = released || (! b.isSoon() && b.link().isNotEmpty());
        check (community, "community skins come from the skins collection");
        check (released, "a bank released in the banks repository can be downloaded");
    }
    check (model.changedFiles().isEmpty(), "nothing counts as changed after loading");
    if (source.isNotEmpty())
    {
        // installing a skin and a bank (downloads from the pretend GitHub)
        const auto target = dir.getChildFile ("installed");
        target.deleteRecursively();
        for (auto& s : model.skins)
            if (s.community)
            {
                bool finished = false, ok = false;
                model.installSkin (s, target.getChildFile ("Skins"), [&] (bool o, const String& m) { finished = true; ok = o; if (! o) std::printf ("  %s\n", m.toRawUTF8()); });
                for (int i = 0; i < 100 && ! finished; ++i) pump (50);
                check (ok && model.installedVersion ("skin:" + s.id) == "1.0", "a community skin installs (and its checksum matches)");
                break;
            }
        for (auto& b : model.banks)
            if (! b.isSoon() && b.link().isNotEmpty())
            {
                bool finished = false, ok = false;
                model.installBank (b, target.getChildFile ("Banks"), [&] (bool o, const String&) { finished = true; ok = o; });
                for (int i = 0; i < 100 && ! finished; ++i) pump (50);
                check (ok && target.getChildFile ("Banks").getNumberOfChildFiles (File::findFiles) == 1, "a bank downloads into the chosen folder");
                break;
            }
    }
    model.emx.feats.add ("Self-test feature");
    check (model.changedFiles().contains ("emulon-x.md"), "an edit marks its file as changed");
    model.discardChanges();
    check (model.changedFiles().isEmpty(), "discard takes edits back");

    HubHolder holder (model);
    auto& hub = holder.hub;
    holder.setSize (HubComponent::W, HubComponent::H);
    hub.startIntro (true);
    pump (300);
    auto shot = [&] (const String& name)
    {
        // the first picture asks for the screenshots and previews; the second one shows them
        for (int i = 0; i < 4; ++i) pump (60);
        holder.createComponentSnapshot (holder.getLocalBounds(), true, 1.0f);
        for (int i = 0; i < 6; ++i) pump (60);
        const auto img = holder.createComponentSnapshot (holder.getLocalBounds(), true, 1.0f);
        const auto f = dir.getChildFile (name + ".png");
        f.deleteFile();
        FileOutputStream out (f);
        PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    };
    shot ("hub-emulon-x");
    hub.showTab (HubComponent::Tab::sd);
    shot ("hub-sd");
    hub.showTab (HubComponent::Tab::skins);
    shot ("hub-skins");
    for (auto& s : model.skins)
        if (s.community) { hub.selectSkin (s.id); break; }
    shot ("hub-skins-community");
    hub.showTab (HubComponent::Tab::banks);
    shot ("hub-banks");
    hub.toggleMenu();
    shot ("hub-notifications");
    hub.toggleMenu();
    hub.showTab (HubComponent::Tab::emx);
    hub.openWhere ("emx");
    shot ("hub-where");
    hub.closeOverlays();
    hub.openSettings();
    shot ("hub-settings");
    // submit: a skin saved by Emulon X SD's self-test, or none
    const auto skinFile = File (SystemStats::getEnvironmentVariable ("EMX_HUB_TEST_SKIN", {}));
    hub.openSubmit (skinFile, true);
    shot ("hub-submit");
    hub.closeOverlays();
    hub.setDevMode (true, "KiruTheProot-Creative");
    pump (200);
    shot ("hub-dev");
    model.emx.sub = "Changed in the self-test";
    model.sendChangeMessage();
    pump (200);
    hub.openSave();
    shot ("hub-dev-save");
    hub.closeOverlays();
    hub.showTab (HubComponent::Tab::sd);
    shot ("hub-dev-sd-test-build");
    hub.openWhere ("test:sd");
    shot ("hub-dev-test-where");
    hub.closeOverlays();
    hub.showTab (HubComponent::Tab::banks);
    shot ("hub-dev-banks");
    hub.setDevMode (false, {});
    model.discardChanges();
    hub.startIntro (false);
    pump (900);
    shot ("hub-intro-1");
    for (int i = 0; i < 30; ++i) pump (100);
    shot ("hub-intro-2");
    for (int i = 0; i < 40; ++i) pump (100);
    shot ("hub-after-intro");
    check (true, "screenshots");
    return failures;
}

class HubWindow final : public DocumentWindow
{
public:
    HubWindow (Model& m) : DocumentWindow ("Emulon X Hub", Colours::black, DocumentWindow::allButtons), holder (m)
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&holder, true);
        setResizable (true, false);
        constrainer.setFixedAspectRatio ((double) HubComponent::W / HubComponent::H);
        constrainer.setMinimumSize (960, 540);
        setConstrainer (&constrainer);
        const auto area = Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                              ? Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt() : Rectangle<int> (0, 0, 1600, 900);
        const float s = jmin (1.0f, (float) area.getWidth() * 0.85f / HubComponent::W, (float) area.getHeight() * 0.85f / HubComponent::H);
        centreWithSize (roundToInt (HubComponent::W * s), roundToInt (HubComponent::H * s));
    }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }
    HubHolder holder;

private:
    ComponentBoundsConstrainer constrainer;
};

class Tray final : public SystemTrayIconComponent
{
public:
    std::function<void()> onOpen, onQuit;
    Tray()
    {
        auto icon = emx::theme::image (emx::theme::Img::logo);
        int size = 0;
        for (int i = 0; i < HubAssets::namedResourceListSize; ++i)
            if (String (HubAssets::getNamedResourceOriginalFilename (HubAssets::namedResourceList[i])) == "tray-icon.png")
                if (auto* data = HubAssets::getNamedResource (HubAssets::namedResourceList[i], size))
                    icon = ImageCache::getFromMemory (data, size);
        setIconImage (icon, icon);
        setIconTooltip ("Emulon X Hub");
    }
    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            m.addItem ("Open Emulon X Hub", [this] { if (onOpen) onOpen(); });
            m.addSeparator();
            m.addItem ("Quit", [this] { if (onQuit) onQuit(); });
            m.showMenuAsync (PopupMenu::Options());
        }
        else if (onOpen)
            onOpen();
    }
};
} // namespace

class EmulonXHubApp final : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Emulon X Hub"; }
    const String getApplicationVersion() override { return emx::hub::hubVersion; }
    bool moreThanOneInstanceAllowed() override { return getCommandLineParameters().contains ("--self-test"); }

    void initialise (const String& commandLine) override
    {
        LookAndFeel::setDefaultLookAndFeel (&lnf);
        auto args = StringArray::fromTokens (commandLine, true);
        args.trim();
        for (auto& a : args) a = a.unquoted();
        if (const int i = args.indexOf ("--self-test"); i >= 0)
        {
            const int failures = selfTest (File::getCurrentWorkingDirectory().getChildFile (args[i + 1].isNotEmpty() ? args[i + 1] : "hub-selftest"));
            std::printf ("%d failed\n", failures);
            Net::get().shutdown();
            setApplicationReturnValue (failures == 0 ? 0 : 1);
            quit();
            return;
        }
        model = std::make_unique<Model>();
        window = std::make_unique<HubWindow> (*model);
        window->onClose = [this]
        {
            if (model->settings.flag ("tray", true))
            {
                window->setVisible (false);
                if (! model->settings.flag ("trayHintShown", false) && tray != nullptr)
                {
                   #if JUCE_WINDOWS
                    tray->showInfoBubble ("Emulon X Hub", "The Hub keeps running here, so it can tell you about updates. Right-click to quit.");
                   #endif
                    model->settings.set ("trayHintShown", true);
                }
            }
            else
                systemRequestedQuit();
        };
        auto& hub = window->holder.hub;
        hub.onQuitForUpdate = [this] { systemRequestedQuit(); };
        hub.onSettingsChanged = [this] { applySettings(); };
        hub.onSystemNotification = [this] (const String& title, const String& text)
        {
           #if JUCE_WINDOWS
            if (tray != nullptr) tray->showInfoBubble (title, text);
           #else
            ignoreUnused (title, text);
           #endif
        };
        applySettings();
        const bool quiet = args.contains ("--tray") && tray != nullptr;
        if (! quiet)
            window->setVisible (true);
        hub.startIntro (quiet);
        if (quiet)
            hub.refreshNow (true);
    }

    void applySettings()
    {
        const bool wantTray = model->settings.flag ("tray", true);
        if (wantTray && tray == nullptr)
        {
            tray = std::make_unique<Tray>();
            tray->onOpen = [this] { window->setVisible (true); window->toFront (true); };
            tray->onQuit = [this] { systemRequestedQuit(); };
        }
        else if (! wantTray)
            tray = nullptr;
       #if JUCE_WINDOWS
        const String runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\Emulon X Hub";
        if (model->settings.flag ("autostart", false))
            WindowsRegistry::setValue (runKey, "\"" + File::getSpecialLocation (File::currentExecutableFile).getFullPathName() + "\" --tray");
        else if (WindowsRegistry::valueExists (runKey))
            WindowsRegistry::deleteValue (runKey);
       #endif
    }

    void shutdown() override
    {
        tray = nullptr;
        window = nullptr;
        if (model != nullptr)
            Net::get().shutdown();
        model = nullptr;
        emx::sd::releaseFonts();
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (model != nullptr && ! model->changedFiles().isEmpty() && window != nullptr && window->isVisible())
        {
            // never lose dev-mode edits without asking
            if (! NativeMessageBox::showOkCancelBox (MessageBoxIconType::WarningIcon, "Emulon X Hub",
                                                     "You changed texts in dev mode that aren't saved to GitHub. Quit anyway?", window.get()))
                return;
        }
        quit();
    }

    void anotherInstanceStarted (const String&) override
    {
        if (window != nullptr)
        {
            window->setVisible (true);
            window->toFront (true);
        }
    }

private:
    emx::EmxLookAndFeel lnf;
    std::unique_ptr<Model> model;
    std::unique_ptr<HubWindow> window;
    std::unique_ptr<Tray> tray;
};

START_JUCE_APPLICATION (EmulonXHubApp)
