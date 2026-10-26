// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"
#include "ui/Theme.h"
#include "ui/SkinFile.h"

namespace emx
{
using namespace juce;

//== Root ========================================================================
EmulonXEditor::Root::Root (EmulonXEditor& e)
    : owner (e),
      statusLed ([&e] { return e.proc.getLastMessage().toUpperCase(); }, 8.0f),
      voicesLed ([&e] { return "VOICES " + String (e.proc.getActiveVoices()); }),
      cpuLed ([&e] { return "CPU " + String (roundToInt (e.proc.getCpuLoad() * 100.0)) + "%"; }),
      masterBar (e.proc),
      explorer (e.proc),
      mainPage (e.proc),
      editorPage (e.proc),
      fxPage (e.proc),
      arpPage (e.proc),
      keys (e.proc)
{
    pages[0] = &mainPage; pages[1] = &editorPage; pages[2] = &fxPage;
    pages[3] = &arpPage;  pages[4] = &machinePage; pages[5] = &arrangerPage;
    for (int i = 0; i < numPages; ++i)
    {
        auto& b = tabs[i];
        b.setClickingTogglesState (false);
        b.getProperties().set ("emxTab", true);   // the XP skin draws these like taskbar buttons
        b.onClick = [this, i] { showPage (i); };
        addAndMakeVisible (b);
        addChildComponent (pages[i]);
    }

    saveButton.setButtonText (String::fromUTF8 ("SAVE \xe2\x96\xbe"));
    saveButton.setTooltip ("New, load and save presets - and merge preset files into one bank (.emxb)");
    saveButton.onClick = [this] { showSaveMenu(); };
    partButton.setTooltip ("The Arranger part you are playing and editing");
    partButton.onClick = [this] { showPartMenu(); };
    statusLed.setTooltip ("What Emulon X did last");
    gear.onClick = [this] { owner.showSettings (settingsPanel == nullptr); };
    for (Component* c : { (Component*) &saveButton, (Component*) &partButton, (Component*) &statusLed, (Component*) &masterBar,
                          (Component*) &voicesLed, (Component*) &cpuLed, (Component*) &gear, (Component*) &explorer, (Component*) &keys })
        addAndMakeVisible (c);

    showPage (0);
}

void EmulonXEditor::Root::showPage (int index)
{
    page = jlimit (0, numPages - 1, index);
    for (int i = 0; i < numPages; ++i)
    {
        tabs[i].setToggleState (i == page, dontSendNotification);
        pages[i]->setVisible (i == page);
    }
    keys.setVisible (page != 1);   // the Editor has its own keyboard and needs the height
    resized();
}

void EmulonXEditor::Root::showSaveMenu()
{
    auto& proc = owner.proc;
    PopupMenu m;
    m.addItem ("New preset", [&proc] { proc.newPreset(); });
    m.addItem ("Load preset or samples...", [this, &proc]
    {
        chooser = std::make_unique<FileChooser> ("Open a preset or add samples", proc.getSettings().getUserPresetFolder(), loader::supportedWildcards());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [&proc] (const FileChooser& fc) { if (! fc.getResults().isEmpty()) proc.loadFiles (fc.getResults()); });
    });
    m.addSeparator();
    auto saveAs = [this, &proc]
    {
        const auto start = proc.getSettings().getUserPresetFolder().getChildFile (loader::safeFileName (proc.getPresetName()) + ".emxp");
        chooser = std::make_unique<FileChooser> ("Save preset", start, "*.emxp");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [&proc] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == File())
                                      return;
                                  if (! f.hasFileExtension ("emxp"))
                                      f = f.withFileExtension ("emxp");
                                  proc.savePresetAs (f);
                              });
    };
    m.addItem ("Save preset", [&proc, saveAs]
    {
        const auto f = proc.getPresetFile();
        if (f != File() && f.getParentDirectory().isDirectory() && f.getFileNameWithoutExtension() == loader::safeFileName (proc.getPresetName()))
            proc.savePresetAs (f);
        else
            saveAs();
    });
    m.addItem ("Save preset as...", saveAs);
    m.addSeparator();
    m.addItem ("Merge presets into a bank (.emxb)...", [this, &proc]
    {
        // pick the presets (several at once), then where the bank goes
        chooser = std::make_unique<FileChooser> ("Choose the presets for the bank", proc.getSettings().getUserPresetFolder(), "*.emxp");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this, &proc] (const FileChooser& fc)
                              {
                                  const auto presets = fc.getResults();
                                  if (presets.isEmpty())
                                      return;
                                  Component::SafePointer<Root> safe (this);
                                  MessageManager::callAsync ([safe, presets, &proc]
                                  {
                                      if (safe == nullptr)
                                          return;
                                      const auto start = proc.getSettings().getUserPresetFolder().getChildFile ("My Bank.emxb");
                                      safe->chooser = std::make_unique<FileChooser> ("Save the bank", start, "*.emxb");
                                      safe->chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                                                                  [presets, &proc] (const FileChooser& fc2)
                                                                  {
                                                                      auto f = fc2.getResult();
                                                                      if (f == File())
                                                                          return;
                                                                      if (! f.hasFileExtension ("emxb"))
                                                                          f = f.withFileExtension ("emxb");
                                                                      proc.mergePresetFilesIntoBank (presets, f);
                                                                  });
                                  });
                              });
    });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&saveButton));
}

void EmulonXEditor::Root::showPartMenu()
{
    PopupMenu m;
    m.addSectionHeader ("Arranger parts arrive with the Arranger tab");
    m.addItem ("Open the Arranger", [this] { showPage (5); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&partButton));
}

void EmulonXEditor::Root::resized()
{
    auto r = getLocalBounds().reduced (46, 40);

    // toolbar: logo, SAVE menu, part, status ... volume, voices, CPU, settings
    auto header = r.removeFromTop (34);
    titleArea = header.removeFromLeft (190).toFloat();
    header.removeFromLeft (14);
    saveButton.setBounds (header.removeFromLeft (84).withSizeKeepingCentre (84, 28));
    header.removeFromLeft (8);
    partButton.setButtonText ("PART 01 - CH " + String (owner.proc.getKeyboardChannel()));
    partButton.setBounds (header.removeFromLeft (124).withSizeKeepingCentre (124, 28));
    header.removeFromLeft (8);
    gear.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 28));
    header.removeFromRight (8);
    cpuLed.setBounds (header.removeFromRight (70).withSizeKeepingCentre (70, 24));
    header.removeFromRight (6);
    voicesLed.setBounds (header.removeFromRight (86).withSizeKeepingCentre (86, 24));
    header.removeFromRight (10);
    masterBar.setBounds (header.removeFromRight (jmin (290, header.getWidth())));
    header.removeFromRight (10);
    statusLed.setBounds (header.withSizeKeepingCentre (header.getWidth(), 24));

    r.removeFromTop (8);
    explorer.setBounds (r.removeFromLeft (225));
    r.removeFromLeft (8);

    // tabs along the top of the right side, the keyboard along the bottom (not on the Editor)
    auto tabRow = r.removeFromTop (30);
    const int tw = (tabRow.getWidth() - (numPages - 1) * 6) / numPages;
    for (int i = 0; i < numPages; ++i)
    {
        tabs[i].setBounds (tabRow.removeFromLeft (tw).withSizeKeepingCentre (tw, 28));
        tabRow.removeFromLeft (6);
    }
    r.removeFromTop (6);
    if (keys.isVisible())
    {
        keys.setBounds (r.removeFromBottom (92));
        r.removeFromBottom (8);
    }
    for (auto* p : pages)
        p->setBounds (r);
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
    if (getMiddleCOctave() != settings->getMiddleCOctave())
    {
        setMiddleCOctave (settings->getMiddleCOctave());   // note names everywhere change
        repaint();
    }
    updateTooltips();
    if (applyHue())
    {
        sendLookAndFeelChange();   // components that cache colours pick up the new ones
        repaint();
    }
}

/** Brings the theme (shared by every open Emulon X window) in line with the settings. Returns a
    number that changes whenever the look changed, so each window knows to refresh. */
static int syncThemeWithSettings (Settings& s)
{
    static int generation = 0;
    static String loadedKey;
    static float lastHue = 1000.0f;
    static theme::Skin lastSkin = theme::Skin::classic;

    auto skin = (theme::Skin) s.getSkin();
    if (skin == theme::Skin::custom)
    {
        const File file (s.getCustomSkin());
        const auto key = file.getFullPathName() + "|" + String (file.getLastModificationTime().toMilliseconds()) + "|" + String (file.getSize());
        if (key != loadedKey)
        {
            auto design = std::make_shared<skin::Design>();
            String error;
            StringArray warnings;
            if (s.getCustomSkin().isNotEmpty() && skin::load (file, *design, error, &warnings))
            {
                theme::setCustomSkin (design);
                theme::setCustomSkinMessage (warnings.isEmpty() ? String() : warnings[0] + (warnings.size() > 1 ? " (+" + String (warnings.size() - 1) + " more)" : String()));
                loadedKey = key;
                lastSkin = theme::Skin::classic;   // apply below
                ++generation;
            }
            else
            {
                theme::setCustomSkinMessage (error.isNotEmpty() ? error : String ("No skin file chosen"));
                loadedKey = {};
                skin = theme::Skin::classic;   // missing or broken: Classic instead
            }
        }
    }
    const float h = theme::skinAllowsColourShift (skin) ? s.getHueShift() : 0.0f;
    if (skin != lastSkin || std::abs (h - lastHue) > 0.01f)
    {
        theme::setSkin (skin);
        theme::setHueShift (h);
        lastSkin = skin;
        lastHue = h;
        ++generation;
    }
    return generation;
}

bool EmulonXEditor::applyHue()
{
    const int gen = syncThemeWithSettings (*settings);
    if (gen == appliedGeneration)
        return false;
    lnf.refreshColours();
    appliedGeneration = gen;
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
        if (loader::isSupported (File (f)) || File (f).hasFileExtension ("emxd"))
            return true;
    return false;
}

void EmulonXEditor::filesDropped (const StringArray& files, int, int)
{
    dragging = false;
    repaint();
    Array<File> list;
    for (auto& f : files)
    {
        const File file (f);
        if (file.hasFileExtension ("emxd"))   // a skin: add it to the skins and use it
        {
            skin::Design d;
            String error;
            if (! skin::load (file, d, error))
            {
                theme::setCustomSkinMessage (file.getFileName() + ": " + error);
                showSettings (true, 4);
                continue;
            }
            const auto dest = settings->getSkinsFolder().getChildFile (file.getFileName());
            if (file == dest || file.copyFileTo (dest))
                settings->useCustomSkin (dest);
            continue;
        }
        list.add (file);
    }
    if (! list.isEmpty())
    {
        proc.loadFiles (list);
        if (root->page != 1)
            root->showPage (0);
    }
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
    theme::drawGlowText (g, "DROP SAMPLES, PRESETS, BANKS OR SKINS", r, theme::font (28.0f * uiScale, 900, 0.14f), Colours::white,
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
    root->statusLed.refresh();
    root->voicesLed.refresh();
    root->cpuLed.refresh();
    root->masterBar.tick();
    if (root->keys.isVisible())
        root->keys.tick();
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
