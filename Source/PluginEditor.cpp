// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"
#include "ui/Theme.h"

namespace emx
{
using namespace juce;

//== Root ========================================================================
EmulonXEditor::Root::Root (EmulonXEditor& e)
    : owner (e),
      voicesLed ([&e] { return "VOICES " + String (e.proc.getActiveVoices()); }),
      cpuLed ([&e] { return "CPU " + String (roundToInt (e.proc.getCpuLoad() * 100.0)) + "%"; }),
      masterBar (e.proc),
      explorer (e.proc),
      mainPage (e.proc),
      editorPage (e.proc),
      fxPage (e.proc),
      arpPage (e.proc)
{
    int index = 0;
    for (auto* b : { &tabMain, &tabEditor, &tabFx, &tabArp })
    {
        b->setClickingTogglesState (false);
        b->getProperties().set ("emxTab", true);   // the XP skin draws these like taskbar buttons
        b->onClick = [this, index] { showPage (index); };
        addAndMakeVisible (b);
        ++index;
    }

    gear.onClick = [this] { owner.showSettings (settingsPanel == nullptr); };
    for (Component* c : { (Component*) &masterBar, (Component*) &voicesLed, (Component*) &cpuLed, (Component*) &gear, (Component*) &explorer,
                          (Component*) &mainPage, (Component*) &editorPage, (Component*) &fxPage, (Component*) &arpPage })
        addAndMakeVisible (c);

    mainPage.getPresetPanel().onOpenZone = [this] { showPage (1); };
    showPage (0);
}

void EmulonXEditor::Root::showPage (int index)
{
    tabMain.setToggleState (index == 0, dontSendNotification);
    tabEditor.setToggleState (index == 1, dontSendNotification);
    tabFx.setToggleState (index == 2, dontSendNotification);
    tabArp.setToggleState (index == 3, dontSendNotification);
    mainPage.setVisible (index == 0);
    editorPage.setVisible (index == 1);
    fxPage.setVisible (index == 2);
    arpPage.setVisible (index == 3);
}

void EmulonXEditor::Root::resized()
{
    auto r = getLocalBounds().reduced (46, 40);

    auto header = r.removeFromTop (34);
    titleArea = header.removeFromLeft (190).toFloat();
    header.removeFromLeft (8);
    for (auto* b : { &tabMain, &tabEditor, &tabFx, &tabArp })
    {
        b->setBounds (header.removeFromLeft (84).withSizeKeepingCentre (84, 28));
        header.removeFromLeft (6);
    }
    gear.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 28));
    header.removeFromRight (8);
    cpuLed.setBounds (header.removeFromRight (78).withSizeKeepingCentre (78, 24));
    header.removeFromRight (8);
    voicesLed.setBounds (header.removeFromRight (100).withSizeKeepingCentre (100, 24));
    header.removeFromRight (16);
    masterBar.setBounds (header.removeFromRight (jmin (330, header.getWidth())));

    r.removeFromTop (8);
    explorer.setBounds (r.removeFromLeft (225));
    r.removeFromLeft (8);
    for (auto* page : { (Component*) &mainPage, (Component*) &editorPage, (Component*) &fxPage, (Component*) &arpPage })
        page->setBounds (r);
    if (settingsPanel != nullptr)
        settingsPanel->setBounds (getLocalBounds());
}

void EmulonXEditor::Root::paint (Graphics& g)
{
    theme::drawBackground (g, getLocalBounds(), titleArea.getBottom());

    theme::drawLogo (g, titleArea.expanded (0.0f, 2.0f));
}

void EmulonXEditor::Root::paintOverChildren (Graphics& g)
{
    theme::drawFrame (g, getLocalBounds());
}

//== Editor ======================================================================
EmulonXEditor::EmulonXEditor (EmulonXProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    applyHue();
    setLookAndFeel (&lnf);
    root = std::make_unique<Root> (*this);
    addAndMakeVisible (*root);
    setResizable (false, false);
    setWantsKeyboardFocus (true);
    setUiScale (settings->getUiScale());
    updateTooltips();
    settings->addChangeListener (this);
    startTimerHz (30);
}

EmulonXEditor::~EmulonXEditor()
{
    stopTimer();
    settings->removeChangeListener (this);
    root.reset();
    setLookAndFeel (nullptr);
}

void EmulonXEditor::changeListenerCallback (ChangeBroadcaster*)
{
    if (std::abs (settings->getUiScale() - uiScale) > 0.001f)
        setUiScale (settings->getUiScale());
    updateTooltips();
    if (applyHue())
    {
        sendLookAndFeelChange();   // components that cache colours pick up the new ones
        repaint();
    }
}

bool EmulonXEditor::applyHue()
{
    const auto skin = (theme::Skin) settings->getSkin();
    const float h = skin == theme::Skin::classic ? settings->getHueShift() : 0.0f;   // hue shift is Classic only
    if (std::abs (h - appliedHue) < 0.01f && skin == appliedSkin)
        return false;
    theme::setSkin (skin);    // shared by every open Emulon X window
    theme::setHueShift (h);
    lnf.refreshColours();
    appliedHue = h;
    appliedSkin = skin;
    return true;
}

void EmulonXEditor::updateTooltips()
{
    if (settings->getShowHints() && tooltips == nullptr)
        tooltips = std::make_unique<TooltipWindow> (this, 600);
    else if (! settings->getShowHints())
        tooltips.reset();
}

void EmulonXEditor::showSettings (bool show, int page)
{
    if (show)
    {
        if (root->settingsPanel == nullptr)
        {
            root->settingsPanel = std::make_unique<SettingsPanel> (proc);
            root->settingsPanel->onClose = [this]
            {
                // Closing from inside one of its own callbacks: delete it afterwards.
                Component::SafePointer<EmulonXEditor> safe (this);
                MessageManager::callAsync ([safe] { if (safe != nullptr) safe->showSettings (false); });
            };
            root->addAndMakeVisible (*root->settingsPanel);
            root->settingsPanel->setBounds (root->getLocalBounds());
        }
        root->settingsPanel->showPage (page);
        root->settingsPanel->grabKeyboardFocus();
    }
    else
    {
        root->settingsPanel.reset();
    }
    root->gear.setToggleState (show, dontSendNotification);
}

void EmulonXEditor::setUiScale (float scale)
{
    uiScale = scale;
    root->setTransform (AffineTransform::scale (scale));
    setSize (roundToInt (baseWidth * scale), roundToInt (baseHeight * scale));
}

void EmulonXEditor::resized()
{
    root->setBounds (0, 0, baseWidth, baseHeight);
}

bool EmulonXEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (loader::isSupported (File (f)))
            return true;
    return false;
}

void EmulonXEditor::filesDropped (const StringArray& files, int, int)
{
    dragging = false;
    repaint();
    Array<File> list;
    for (auto& f : files)
        list.add (File (f));
    proc.loadFiles (list);
    root->showPage (0);
}

void EmulonXEditor::paintOverChildren (Graphics& g)
{
    if (! dragging)
        return;
    auto r = getLocalBounds().toFloat().reduced (20.0f * uiScale);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 12.0f * uiScale);
    g.setColour (theme::red);
    g.drawRoundedRectangle (r, 12.0f * uiScale, 3.0f * uiScale);
    theme::drawGlowText (g, "DROP SAMPLES, PRESETS OR BANKS", r, theme::font (28.0f * uiScale, 900, 0.14f), Colours::white,
                         theme::red, roundToInt (14.0f * uiScale), Justification::centred);
}

bool EmulonXEditor::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey && proc.getLearnMacro() >= 0)
    {
        proc.setLearnMacro (-1);
        return true;
    }
    const auto m = k.getModifiers();
    if (m.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (m.isShiftDown()) proc.redo(); else proc.undo();
        return true;
    }
    if (m.isCommandDown() && (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y'))
    {
        proc.redo();
        return true;
    }
    return false;
}

void EmulonXEditor::timerCallback()
{
    root->voicesLed.refresh();
    root->cpuLed.refresh();
    root->masterBar.tick();
    if (root->mainPage.isVisible())
        root->mainPage.tick();
    if (root->editorPage.isVisible())
        root->editorPage.tick();
    if (root->fxPage.isVisible())
        root->fxPage.tick();
    if (root->arpPage.isVisible())
        root->arpPage.tick();
}

} // namespace emx
