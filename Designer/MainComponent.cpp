// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainComponent.h"
#include "ui/Theme.h"
#include "Settings.h"

namespace emx::sd
{
using namespace juce;

namespace
{
String hex (Colour c) { return "#" + c.toDisplayString (false).toLowerCase(); }

float luminance (Colour c)
{
    auto lin = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin (c.getFloatRed()) + 0.7152f * lin (c.getFloatGreen()) + 0.0722f * lin (c.getFloatBlue());
}

float contrast (Colour a, Colour b)
{
    const float x = luminance (a), y = luminance (b);
    return (jmax (x, y) + 0.05f) / (jmin (x, y) + 0.05f);
}
} // namespace

//== Parts panel ===================================================================
PartsPanel::PartsPanel (SkinDocument& d) : doc (d)
{
    for (int i = 0; i < SkinDocument::tokenNames().size(); ++i)
    {
        auto* s = palette.add (new ColourSwatch());
        const auto name = SkinDocument::tokenNames()[i];
        s->read = [this, name] { return doc.token (name); };
        s->write = [this, name] (Colour c) { doc.set ("tokens." + name, hex (c), "token-" + name); };
        s->setTooltip ("Palette colour: everything linked to it follows");
        addAndMakeVisible (s);
    }
    hueOn.setTooltip ("Players may turn this skin to another colour in Emulon X's settings");
    hueOff.setTooltip ("The skin keeps its colours");
    hueOn.onClick = [this] { doc.set ("hueShift", true); };
    hueOff.onClick = [this] { doc.set ("hueShift", false); };
    addAndMakeVisible (hueOn);
    addAndMakeVisible (hueOff);
    setMouseCursor (MouseCursor::PointingHandCursor);
    doc.addChangeListener (this);
    changeListenerCallback (nullptr);
}

PartsPanel::~PartsPanel() { doc.removeChangeListener (this); }

void PartsPanel::changeListenerCallback (ChangeBroadcaster*)
{
    const bool on = doc.get ("hueShift").isVoid() || (bool) doc.get ("hueShift");
    hueOn.setToggleState (on, dontSendNotification);
    hueOff.setToggleState (! on, dontSendNotification);
    for (auto* s : palette)
        s->repaint();
    repaint();
}

Colour PartsPanel::swatchFor (const String& key) const
{
    const auto d = doc.design();
    if (key == "window") return d->window.average();
    if (key == "frame") return d->frameColour;
    if (key == "topbar") return d->topBar.average();
    if (key == "panel") return d->panel.fill.average();
    if (key == "heading") return d->heading;
    if (key == "inset") return d->inset.fill.average();
    if (key == "display") return d->display.fill.average();
    if (key == "list") return d->list;
    if (key == "button") return d->button.fill.average();
    if (key == "buttonOn") return d->buttonOn.fill.average();
    if (key == "tab") return d->tab.fill.average();
    if (key == "knob") return d->ring > 0.01f ? d->ringValue : d->capLight;
    if (key == "fader") return d->faderFill;
    if (key == "label") return d->label;
    return d->text;
}

std::vector<PartsPanel::Item> PartsPanel::layoutItems() const
{
    std::vector<Item> items;
    int y = 30;
    String group;
    for (auto& p : allParts())
    {
        if (group != p.group)
        {
            group = p.group;
            y += 18;
        }
        items.push_back ({ p.key, p.title, p.group, { 0, y, getWidth(), 23 } });
        y += 24;
    }
    return items;
}

void PartsPanel::resized()
{
    const auto items = layoutItems();
    paletteTop = items.back().r.getBottom() + 18;
    const int w = (getWidth() - 16) / 3;
    for (int i = 0; i < palette.size(); ++i)
        palette[i]->setBounds ((i % 3) * (w + 8), paletteTop + 26 + (i / 3) * 44, w, 24);
    const int hy = paletteTop + 26 + 2 * 44 + 12;
    hueOn.setBounds (getWidth() - 100, hy, 50, 24);
    hueOff.setBounds (getWidth() - 50, hy, 50, 24);
}

void PartsPanel::paint (Graphics& g)
{
    theme::drawHeading (g, getLocalBounds().toFloat(), "Parts", "click one, or click the preview");
    String group;
    for (auto& it : layoutItems())
    {
        if (group != it.group)
        {
            group = it.group;
            g.setColour (theme::textMuted);
            g.setFont (theme::font (7.0f, 600, 0.2f));
            g.drawText (group, it.r.translated (8, -17).withHeight (14), Justification::centredLeft, false);
        }
        const auto r = it.r.toFloat();
        const bool sel = it.key == selected, hov = it.key == hovered;
        if (sel || hov)
        {
            g.setColour (theme::red.withAlpha (sel ? 0.18f : 0.08f));
            g.fillRoundedRectangle (r, 5.0f);
            if (sel)
            {
                g.setColour (theme::red.withAlpha (0.5f));
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            }
        }
        const auto sw = Rectangle<float> (r.getX() + 8.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f);
        g.setColour (swatchFor (it.key));
        g.fillRoundedRectangle (sw, 3.0f);
        g.setColour (Colours::black);
        g.drawRoundedRectangle (sw, 3.0f, 1.0f);
        g.setColour (sel ? Colours::white : theme::listText);
        g.setFont (theme::font (10.5f, 500, 0.03f));
        g.drawText (it.title, r.withTrimmedLeft (30.0f), Justification::centredLeft, false);
    }
    // palette
    auto pr = Rectangle<float> (0.0f, (float) paletteTop, (float) getWidth(), 20.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillRect (0.0f, pr.getY() - 6.0f, (float) getWidth(), 1.0f);
    theme::drawHeading (g, pr.withHeight (40.0f), "Palette", "linked colours follow");
    for (int i = 0; i < palette.size(); ++i)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (6.5f, 600, 0.1f));
        g.drawText (SkinDocument::tokenTitles()[i], palette[i]->getBounds().translated (0, 25).withHeight (12), Justification::centred, false);
    }
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.5f, 500, 0.06f));
    g.drawText ("COLOUR SHIFT", Rectangle<int> (0, hueOn.getY(), 110, 24), Justification::centredLeft, false);
    g.setFont (theme::font (7.5f, 500, 0.02f));
    g.drawFittedText ("Lets players turn this skin to another colour in Emulon X's settings.", Rectangle<int> (0, hueOn.getBottom() + 4, getWidth(), 30),
                      Justification::topLeft, 2, 0.9f);
}

void PartsPanel::mouseMove (const MouseEvent& e)
{
    String k;
    for (auto& it : layoutItems())
        if (it.r.contains (e.getPosition()))
            k = it.key;
    if (k != hovered)
    {
        hovered = k;
        repaint();
    }
}

void PartsPanel::mouseExit (const MouseEvent&)
{
    hovered = {};
    repaint();
}

void PartsPanel::mouseUp (const MouseEvent& e)
{
    for (auto& it : layoutItems())
        if (it.r.contains (e.getPosition()) && onSelect)
            onSelect (it.key);
}

//== Main ==========================================================================
MainComponent::MainComponent() : parts (doc), preview (doc), inspector (doc)
{
    setWantsKeyboardFocus (true);
    startFrom.setTextWhenNothingSelected ("START FROM...");
    startFrom.addItemList (SkinDocument::presetTitles(), 1);
    startFrom.setTooltip ("Start over from one of Emulon X's own skins (keeps the name; can be undone)");
    startFrom.onChange = [this]
    {
        const int i = startFrom.getSelectedItemIndex();
        if (i < 0)
            return;
        doc.startFrom (i);
        setStatus ("Started from " + SkinDocument::presetTitles()[i] + ".");
        startFrom.setSelectedId (0, dontSendNotification);
    };
    undoButton.onClick = [this] { doc.undo(); };
    redoButton.onClick = [this] { doc.redo(); };
    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Y)");
    newButton.setTooltip ("A new skin, starting from Emulon Classic");
    newButton.onClick = [this]
    {
        doc.reset();
        setStatus ("New skin.");
    };
    openButton.setTooltip ("Open a skin file (.emxd), or a skin.json from the Skin Studio prototype (Ctrl+O)");
    openButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Open a skin", SharedResourcePointer<Settings>()->getSkinsFolder(), "*.emxd;*.json");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
                openFile (fc.getResult());
        });
    };
    saveButton.setTooltip ("Save the skin file (Ctrl+S)");
    saveButton.onClick = [this] { save (false); };
    saveAsButton.onClick = [this] { save (true); };
    mySkinsButton.setTooltip ("Saves into your Emulon X skins folder, so it shows up in Emulon X's Settings > Customization");
    mySkinsButton.onClick = [this] { saveToMySkins(); };

    for (auto* ed : { &nameEditor, &authorEditor })
    {
        ed->setFont (theme::font (10.0f, 500, 0.04f));
        ed->setJustification (Justification::centredLeft);
        ed->setIndents (8, 0);
        ed->setInputRestrictions (60);
    }
    nameEditor.setTextToShowWhenEmpty ("SKIN NAME", theme::textMuted);
    authorEditor.setTextToShowWhenEmpty ("YOUR NAME", theme::textMuted);
    nameEditor.onTextChange = [this] { doc.setName (nameEditor.getText()); };
    authorEditor.onTextChange = [this] { doc.setAuthor (authorEditor.getText()); };

    for (Component* c : { (Component*) &startFrom, (Component*) &undoButton, (Component*) &redoButton, (Component*) &newButton,
                          (Component*) &openButton, (Component*) &saveButton, (Component*) &saveAsButton, (Component*) &mySkinsButton,
                          (Component*) &nameEditor, (Component*) &authorEditor, (Component*) &parts, (Component*) &preview,
                          (Component*) &normalButton, (Component*) &hoverButton, (Component*) &inspector })
        addAndMakeVisible (c);

    normalButton.setToggleState (true, dontSendNotification);
    normalButton.onClick = [this] { normalButton.setToggleState (true, dontSendNotification); hoverButton.setToggleState (false, dontSendNotification); preview.setHoverState (false); };
    hoverButton.onClick = [this] { normalButton.setToggleState (false, dontSendNotification); hoverButton.setToggleState (true, dontSendNotification); preview.setHoverState (true); };
    hoverButton.setTooltip ("See how buttons look with the mouse over them");

    parts.onSelect = [this] (const String& k) { select (k); };
    preview.onSelect = [this] (const String& k) { select (k); };

    doc.addChangeListener (this);
    if (! doc.loadDraft())
        doc.reset();
    draftRevision = doc.getRevision();
    refreshToolbar();
    startTimerHz (4);
}

MainComponent::~MainComponent()
{
    doc.removeChangeListener (this);
    if (doc.getRevision() != draftRevision)
        doc.saveDraft();
}

void MainComponent::select (const String& key)
{
    parts.setSelected (key);
    preview.setSelected (key);
    inspector.show (key);
}

bool MainComponent::openFile (const File& f)
{
    String error;
    if (! doc.open (f, error))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Can't open " + f.getFileName(), error);
        return false;
    }
    setStatus ("Opened " + f.getFileName() + ".");
    return true;
}

void MainComponent::setStatus (const String& s)
{
    status = s;
    statusTime = Time::getMillisecondCounterHiRes();
    repaint (footerArea);
}

void MainComponent::changeListenerCallback (ChangeBroadcaster*)
{
    refreshToolbar();
    lastChangeTime = Time::getMillisecondCounterHiRes();
    repaint (footerArea);
}

void MainComponent::refreshToolbar()
{
    undoButton.setEnabled (doc.canUndo());
    redoButton.setEnabled (doc.canRedo());
    if (! nameEditor.hasKeyboardFocus (true) && nameEditor.getText() != doc.getName())
        nameEditor.setText (doc.getName(), false);
    if (! authorEditor.hasKeyboardFocus (true) && authorEditor.getText() != doc.getAuthor())
        authorEditor.setText (doc.getAuthor(), false);
    if (onTitleChanged)
        onTitleChanged ("Emulon X SD - " + (doc.getName().isNotEmpty() ? doc.getName() : String ("Untitled")) + (doc.hasUnsavedChanges() ? " *" : ""));
}

void MainComponent::timerCallback()
{
    // keep the work in progress, a moment after the last change
    if (doc.getRevision() != draftRevision && Time::getMillisecondCounterHiRes() - lastChangeTime > 1500.0)
    {
        doc.saveDraft();
        draftRevision = doc.getRevision();
    }
}

void MainComponent::writeTo (const File& f)
{
    String error;
    if (doc.save (f, error))
        setStatus ("Saved " + f.getFullPathName());
    else
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Can't save", error);
    refreshToolbar();
}

void MainComponent::save (bool askForFile)
{
    if (! askForFile && doc.getFile() != File())
    {
        writeTo (doc.getFile());
        return;
    }
    const auto name = File::createLegalFileName (doc.getName().isNotEmpty() ? doc.getName() : String ("My Skin"));
    const auto start = doc.getFile() != File() ? doc.getFile() : SharedResourcePointer<Settings>()->getSkinsFolder().getChildFile (name + ".emxd");
    chooser = std::make_unique<FileChooser> ("Save the skin", start, "*.emxd");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == File())
                                  return;
                              if (! f.hasFileExtension ("emxd"))
                                  f = f.withFileExtension ("emxd");
                              writeTo (f);
                          });
}

void MainComponent::saveToMySkins()
{
    const auto folder = SharedResourcePointer<Settings>()->getSkinsFolder();
    if (doc.getFile().isAChildOf (folder))
    {
        writeTo (doc.getFile());
        return;
    }
    const auto f = folder.getChildFile (File::createLegalFileName (doc.getName().isNotEmpty() ? doc.getName() : String ("My Skin")) + ".emxd");
    if (! f.existsAsFile())
    {
        writeTo (f);
        return;
    }
    SafePointer<MainComponent> safe (this);
    AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                                .withTitle ("Replace " + f.getFileName() + "?")
                                .withMessage ("Your skins folder already has a skin with this name.")
                                .withButton ("Replace").withButton ("Cancel"),
                            [safe, f] (int result)
                            {
                                if (safe != nullptr && result == 1)
                                    safe->writeTo (f);
                            });
}

bool MainComponent::keyPressed (const KeyPress& k)
{
    const auto m = k.getModifiers();
    if (! m.isCommandDown())
        return false;
    const auto c = CharacterFunctions::toLowerCase ((juce_wchar) k.getKeyCode());
    if (c == 'z') { if (m.isShiftDown()) doc.redo(); else doc.undo(); return true; }
    if (c == 'y') { doc.redo(); return true; }
    if (c == 's') { save (m.isShiftDown()); return true; }
    if (c == 'o') { openButton.triggerClick(); return true; }
    return false;
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (12);
    toolbarArea = r.removeFromTop (54);
    r.removeFromTop (10);
    footerArea = r.removeFromBottom (34);
    r.removeFromBottom (10);
    leftArea = r.removeFromLeft (226);
    r.removeFromLeft (10);
    rightArea = r.removeFromRight (330);
    r.removeFromRight (10);
    centreArea = r;

    auto t = toolbarArea.reduced (14, 13);
    t.removeFromLeft (200);   // logo
    startFrom.setBounds (t.removeFromLeft (190));
    t.removeFromLeft (8);
    undoButton.setBounds (t.removeFromLeft (64));
    t.removeFromLeft (6);
    redoButton.setBounds (t.removeFromLeft (64));
    t.removeFromLeft (6);
    newButton.setBounds (t.removeFromLeft (56));
    mySkinsButton.setBounds (t.removeFromRight (150));
    t.removeFromRight (6);
    saveAsButton.setBounds (t.removeFromRight (90));
    t.removeFromRight (6);
    saveButton.setBounds (t.removeFromRight (64));
    t.removeFromRight (6);
    openButton.setBounds (t.removeFromRight (76));
    t.removeFromRight (10);
    authorEditor.setBounds (t.removeFromRight (jmin (140, t.getWidth() / 3)));
    t.removeFromRight (6);
    nameEditor.setBounds (t.removeFromRight (jmin (190, t.getWidth() - 10)));

    parts.setBounds (leftArea.reduced (12));
    auto c = centreArea.reduced (12);
    auto ch = c.removeFromTop (26);
    hoverButton.setBounds (ch.removeFromRight (70).reduced (0, 1));
    normalButton.setBounds (ch.removeFromRight (70).reduced (0, 1));
    c.removeFromTop (6);
    preview.setBounds (c);
    inspector.setBounds (rightArea.reduced (12));
}

void MainComponent::paint (Graphics& g)
{
    theme::drawBackground (g, getLocalBounds(), 60.0f);
    for (auto a : { toolbarArea, leftArea, centreArea, rightArea, footerArea })
        theme::drawPanel (g, a.toFloat(), 9.0f);

    // logo
    auto logoArea = toolbarArea.reduced (14, 10).withWidth (190).toFloat();
    theme::drawLogo (g, logoArea.removeFromTop (24.0f));
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.0f, 600, 0.3f));
    g.drawText ("SD  -  SKIN DESIGNER", logoArea.withHeight (12.0f).translated (2.0f, 1.0f), Justification::centredLeft, false);

    theme::drawHeading (g, centreArea.reduced (12).toFloat(), "Live preview", "hover to find a part - click to edit it");

    // readability: the text colours against what they sit on
    const auto d = doc.design();
    const std::pair<const char*, std::pair<Colour, Colour>> checks[] {
        { "PANEL TITLES", { d->heading, d->panel.fill.average() } },
        { "DISPLAY TEXT", { d->displayText, d->display.fill.average() } },
        { "LIST TEXT", { d->list, d->display.fill.average() } },
        { "BUTTON TEXT", { d->buttonText, d->button.fill.average() } },
        { "ON-BUTTON TEXT", { d->buttonOnText, d->buttonOn.fill.average() } },
        { "KNOB CAPTIONS", { d->label, d->inset.fill.average() } } };
    auto f = footerArea.reduced (14, 7);
    g.setColour (theme::textStrong);
    g.setFont (theme::font (8.5f, 700, 0.14f));
    g.drawText ("READABILITY", f.removeFromLeft (100), Justification::centredLeft, false);
    for (auto& [name, cols] : checks)
    {
        const float ratio = contrast (cols.first, cols.second);
        const bool good = ratio >= 4.5f;
        const auto text = String (name) + "  " + String (ratio, 1) + ":1";
        const int w = (int) GlyphArrangement::getStringWidth (theme::font (7.5f, 600, 0.04f), text) + 30;
        auto chip = f.removeFromLeft (w).toFloat();
        f.removeFromLeft (8);
        g.setColour (Colour (0xff0b090c));
        g.fillRoundedRectangle (chip, chip.getHeight() * 0.5f);
        g.setColour (Colours::black);
        g.drawRoundedRectangle (chip, chip.getHeight() * 0.5f, 1.0f);
        const auto col = good ? Colour (0xff4ed38a) : Colour (0xffffb030);
        g.setColour (col);
        g.fillEllipse (chip.getX() + 9.0f, chip.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setFont (theme::font (7.5f, 600, 0.04f));
        g.drawText (text, chip.withTrimmedLeft (22.0f), Justification::centredLeft, false);
    }
    // status or the first problem with the skin
    String note = status;
    if (note.isEmpty() || Time::getMillisecondCounterHiRes() - statusTime > 8000.0)
        note = doc.getWarnings().isEmpty() ? (doc.getFile() != File() ? doc.getFile().getFileName() + (doc.hasUnsavedChanges() ? " - not saved yet" : " - saved")
                                                                     : String ("Your work is kept automatically - save it as a skin file when you're happy."))
                                           : doc.getWarnings()[0];
    g.setColour (doc.getWarnings().isEmpty() ? theme::textMuted : Colour (0xffffb030));
    g.setFont (theme::font (7.5f, 500, 0.03f));
    g.drawFittedText (note, f, Justification::centredRight, 1, 0.8f);
}
} // namespace emx::sd
