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
      explorer (e.proc),
      mainPage (e.proc),
      editorPage ("Editor", "zones - loops - keymap", "THE ZONE EDITOR ARRIVES IN MILESTONE 4"),
      fxPage ("FX", "chains - envelopes - modulation", "EFFECTS AND MODULATION ARRIVE IN MILESTONE 5")
{
    int index = 0;
    for (auto* b : { &tabMain, &tabEditor, &tabFx })
    {
        b->setClickingTogglesState (false);
        b->onClick = [this, index] { showPage (index); };
        addAndMakeVisible (b);
        ++index;
    }

    gear.onClick = [this] { owner.showSettings (settingsPanel == nullptr); };
    for (Component* c : { (Component*) &voicesLed, (Component*) &cpuLed, (Component*) &gear, (Component*) &explorer,
                          (Component*) &mainPage, (Component*) &editorPage, (Component*) &fxPage })
        addAndMakeVisible (c);

    showPage (0);
}

void EmulonXEditor::Root::showPage (int index)
{
    tabMain.setToggleState (index == 0, dontSendNotification);
    tabEditor.setToggleState (index == 1, dontSendNotification);
    tabFx.setToggleState (index == 2, dontSendNotification);
    mainPage.setVisible (index == 0);
    editorPage.setVisible (index == 1);
    fxPage.setVisible (index == 2);
}

void EmulonXEditor::Root::resized()
{
    auto r = getLocalBounds().reduced (46, 40);

    auto header = r.removeFromTop (34);
    titleArea = header.removeFromLeft (190).toFloat();
    header.removeFromLeft (8);
    for (auto* b : { &tabMain, &tabEditor, &tabFx })
    {
        b->setBounds (header.removeFromLeft (84).withSizeKeepingCentre (84, 28));
        header.removeFromLeft (6);
    }
    gear.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 28));
    header.removeFromRight (8);
    cpuLed.setBounds (header.removeFromRight (78).withSizeKeepingCentre (78, 24));
    header.removeFromRight (8);
    voicesLed.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 24));

    r.removeFromTop (8);
    explorer.setBounds (r.removeFromLeft (225));
    r.removeFromLeft (8);
    for (auto* page : { (Component*) &mainPage, (Component*) &editorPage, (Component*) &fxPage })
        page->setBounds (r);
    if (settingsPanel != nullptr)
        settingsPanel->setBounds (getLocalBounds());
}

void EmulonXEditor::Root::paint (Graphics& g)
{
    g.fillAll (Colours::black);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (theme::image (theme::Img::background), getLocalBounds().toFloat(),
                 RectanglePlacement (RectanglePlacement::xLeft | RectanglePlacement::yMid | RectanglePlacement::fillDestination));
    g.setGradientFill (ColourGradient::horizontal (Colours::black.withAlpha (0.15f), 0.0f, Colours::black.withAlpha (0.75f), (float) getWidth()));
    g.fillAll();

    const auto f = theme::font (21.0f, 900, 0.14f);
    auto t = titleArea;
    GlyphArrangement ga;
    ga.addLineOfText (f, "EMULON ", 0, 0);
    const auto w = ga.getBoundingBox (0, -1, true).getWidth();
    theme::drawGlowText (g, "EMULON", t, f, Colours::white, theme::red.withAlpha (0.8f), 10);
    theme::drawGlowText (g, "X", t.withTrimmedLeft (w), f, theme::red, theme::red.withAlpha (0.9f), 12);
}

void EmulonXEditor::Root::paintOverChildren (Graphics& g)
{
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    theme::drawNineSlice (g, theme::image (theme::Img::frame), getLocalBounds(), 120, 30);
}

//== Editor ======================================================================
EmulonXEditor::EmulonXEditor (EmulonXProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    root = std::make_unique<Root> (*this);
    addAndMakeVisible (*root);
    setResizable (false, false);
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

void EmulonXEditor::timerCallback()
{
    root->voicesLed.refresh();
    root->cpuLed.refresh();
    if (root->mainPage.isVisible())
        root->mainPage.tick();
}

} // namespace emx
