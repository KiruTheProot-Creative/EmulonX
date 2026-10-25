// Emulon X Hub — the window: intro, the four tabs, installing, notifications, dev mode and submitting skins.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "HubModel.h"
#include "HubWidgets.h"

namespace emx::hub
{
class HubComponent final : public juce::Component, private juce::ChangeListener, private juce::Timer, private juce::AsyncUpdater
{
public:
    static constexpr int W = 1280, H = 720;
    explicit HubComponent (Model&);
    ~HubComponent() override;

    /** Shown by the system tray (Windows pop-up) when the window is hidden. */
    std::function<void (const juce::String& title, const juce::String& text)> onSystemNotification;
    /** The Hub updates itself: quit so the installer can replace it. */
    std::function<void()> onQuitForUpdate;
    std::function<void()> onSettingsChanged;   // tray / start with Windows

    void startIntro (bool skip);
    void refreshNow (bool fromTimer);
    enum class Tab { emx, sd, skins, banks };
    void showTab (Tab);
    void openTarget (const juce::String& open);

    // used by the self-test
    void setDevMode (bool on, const juce::String& user);
    void openWhere (const juce::String& key);
    void openSubmit (const juce::File& skin = {}, bool skipSignIn = false);
    void openSave();
    void openSettings();
    void toggleMenu();
    void closeOverlays();
    void selectSkin (const juce::String& id) { skinSel = id; rebuild(); }
    void selectBank (const juce::String& id) { bankSel = id; rebuild(); }
    bool introRunning() const { return introStage > 0; }
    juce::String debugState() const;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    struct Dialog;
    struct Overlay;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    /** Rebuilds the page a moment later (never while one of its own buttons is still being clicked). */
    void rebuild() { triggerAsyncUpdate(); }
    void rebuildNow();
    void handleAsyncUpdate() override { rebuildNow(); }
    void dispose (std::unique_ptr<Overlay>&);
    void buildProduct (Product&);
    void buildSkins();
    void buildBanks();
    void layoutBody();
    void updateInstallBox();
    void edited();
    juce::String signature() const;

    void showDialog (std::unique_ptr<Dialog>);
    void closeDialog();
    void message (const juce::String& heading, const juce::String& text);
    void flash (const juce::String&);
    void toast (const Model::Note&);
    void openNote (const Model::Note&);
    void signIn (std::function<void (juce::String login)> then);
    void devLogin();
    void install (const juce::String& key);
    void pickPicture (std::function<void (juce::String name)>);
    void drawCover (juce::Graphics&, juce::Rectangle<float>, const BankItem&, bool small) const;
    void drawImageCover (juce::Graphics&, juce::Rectangle<float>, const juce::Image&, bool fit, float radius) const;
    std::unique_ptr<TextItem> text (const juce::String& t, TextItem::Options o, std::function<void (const juce::String&)> edit = {}, const juce::String& id = {});

    Model& model;
    Tab tab = Tab::emx;
    juce::String skinSel, bankSel;
    int shotIndex[2] { 0, 0 };
    bool showLog = false, dev = false;
    juce::String devUser, lastSignature;
    juce::String checkStatus;

    // header
    juce::OwnedArray<HubButton> tabs;
    std::unique_ptr<HubButton> bell, gear;
    // body
    juce::Viewport leftView;
    std::unique_ptr<Column> leftCol;
    std::unique_ptr<juce::Component> installBox, right;
    std::vector<std::unique_ptr<juce::Component>> keep;   // helpers owned for the current page
    // footer and dev bar
    HubButton replay { "REPLAY INTRO", look::Style::grey, 9.5f }, devBtn { "DEV LOGIN", look::Style::grey, 9.5f };
    std::unique_ptr<juce::Component> devBar, frameLayer;
    void paintFrameLayer (juce::Graphics&);
    void restack();
    // overlays
    std::unique_ptr<Overlay> menu, toastBox, modal;
    juce::String flashText;
    double flashUntil = 0;
    double toastUntil = 0;
    // intro
    int introStage = 0;          // 0 done, 1 Kiru logo, 2 Emulon X logo + loading
    double introStart = 0, stageStart = 0;
    bool loaded = false, skipStage = false;
    juce::String loadText;
    juce::Image kiruLogo;
    // background work
    std::unique_ptr<GitHubSignIn> signInFlow;
    std::unique_ptr<juce::FileChooser> chooser;
    double lastRefresh = 0;
    juce::String focusKey;
    int focusCaret = 0;
    juce::Point<int> leftScroll;
    juce::String pageKey;
    juce::Image backgroundImage;
};

/** Holds the Hub and scales it to the window (keeps 16:9, black bars if needed). */
class HubHolder final : public juce::Component
{
public:
    explicit HubHolder (Model& m) : hub (m) { addAndMakeVisible (hub); setSize (HubComponent::W, HubComponent::H); }
    HubComponent hub;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    void resized() override
    {
        const float s = juce::jmin ((float) getWidth() / HubComponent::W, (float) getHeight() / HubComponent::H);
        hub.setBounds (0, 0, HubComponent::W, HubComponent::H);
        hub.setTransform (juce::AffineTransform::scale (s).translated (((float) getWidth() - HubComponent::W * s) * 0.5f,
                                                                       ((float) getHeight() - HubComponent::H * s) * 0.5f));
    }
};
} // namespace emx::hub
