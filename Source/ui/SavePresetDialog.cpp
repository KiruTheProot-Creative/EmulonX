// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SavePresetDialog.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

static const char* const commonTypes[] { "Ambient", "Arp", "Bass", "Bells", "Brass", "Choir", "Drum Kit", "Drums", "FX", "Guitar", "Keys",
                                         "Lead", "Loop", "Organ", "Pad", "Percussion", "Piano", "Pluck", "Strings", "Synth", "Texture", "Voice", "Wind" };

SavePresetDialog::SavePresetDialog (EmulonXProcessor& p) : proc (p)
{
    const auto& info = proc.getPresetInfo();
    auto setup = [this] (TextEditor& t, const String& text, const String& hint, int maxLen)
    {
        t.setFont (theme::font (11.0f, 600, 0.03f));
        t.setColour (TextEditor::textColourId, theme::ledText);
        t.setIndents (8, 6);
        t.setText (text, false);
        t.setTextToShowWhenEmpty (hint, theme::textMuted.withAlpha (0.6f));
        t.setInputRestrictions (maxLen);
        t.setSelectAllWhenFocused (true);
        t.onTextChange = [this] { update(); };
        t.onReturnKey = [this] { save(); };
        t.onEscapeKey = [this] { if (onClose) onClose(); };
        addAndMakeVisible (t);
    };
    const auto current = proc.getPresetName() == "Init" ? String() : proc.getPresetName();
    setup (name, current, "Name of the preset", 40);
    setup (type, info.type, "Pad, Bass, Lead... type anything", 32);
    auto lastAuthor = proc.getSettings().properties().getValue ("presetAuthor");
    setup (author, info.author.isNotEmpty() ? info.author : lastAuthor, "Your name", 48);
    setup (tags, info.tags, "warm, slow, analog... (helps finding it later)", 200);
    setup (notes, info.description, "How to play it, what the macros do...", 1000);
    notes.setMultiLine (true, true);
    notes.setReturnKeyStartsNewLine (true);
    notes.onReturnKey = nullptr;

    typeMenu.setTooltip ("Pick a common type (or just type your own)");
    typeMenu.onClick = [this] { showTypeMenu(); };
    folder = proc.getPresetFile().existsAsFile() ? proc.getPresetFile().getParentDirectory() : proc.getSettings().getUserPresetFolder();
    folderButton.setTooltip ("Choose the folder the preset goes into");
    folderButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Save the preset in...", folder);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
        {
            if (fc.getResult().isDirectory())
            {
                folder = fc.getResult();
                update();
            }
        });
    };
    addToBank.setClickingTogglesState (true);
    addToBank.setTooltip ("Puts the saved preset in the bank list too (export the list as one .emxb later)");
    cancelButton.onClick = [this] { if (onClose) onClose(); };
    saveButton.onClick = [this] { save(); };
    for (Component* c : { (Component*) &typeMenu, (Component*) &folderButton, (Component*) &addToBank, (Component*) &cancelButton, (Component*) &saveButton })
        addAndMakeVisible (c);
    setWantsKeyboardFocus (true);
    update();
}

SavePresetDialog::~SavePresetDialog() = default;

void SavePresetDialog::focusFirst()
{
    name.grabKeyboardFocus();
}

File SavePresetDialog::targetFile() const
{
    const auto n = name.getText().trim();
    return folder.getChildFile (loader::safeFileName (n.isNotEmpty() ? n : String ("Init")) + ".emxp");
}

void SavePresetDialog::update()
{
    const bool exists = targetFile().existsAsFile();
    saveButton.setButtonText (exists ? "REPLACE" : "SAVE");
    saveButton.setEnabled (name.getText().trim().isNotEmpty() && ! proc.getZones().empty());
    repaint();
}

void SavePresetDialog::showTypeMenu()
{
    PopupMenu m;
    for (auto* t : commonTypes)
        m.addItem (t, [this, t] { type.setText (t, true); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&typeMenu));
}

void SavePresetDialog::save()
{
    if (! saveButton.isEnabled())
        return;
    PresetInfo info;
    info.type = type.getText().trim();
    info.author = author.getText().trim();
    info.tags = tags.getText().trim();
    info.description = notes.getText().trim();
    info.created = proc.getPresetInfo().created.isNotEmpty() ? proc.getPresetInfo().created : Time::getCurrentTime().toISO8601 (true).substring (0, 10);
    if (info.author.isNotEmpty())
        proc.getSettings().properties().setValue ("presetAuthor", info.author);   // remembered for the next preset

    const auto file = targetFile();
    proc.setPresetName (name.getText().trim());
    proc.setPresetInfo (info);
    proc.savePresetAs (file);
    if (addToBank.getToggleState())
        proc.addCurrentPresetToBank();
    if (onClose)
        onClose();
}

void SavePresetDialog::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (560, 450);
    auto r = panel.reduced (24, 18);
    r.removeFromTop (34);   // title
    auto field = [&] (int i, TextEditor& t, int h, Component* extra = nullptr)
    {
        labels[i] = r.removeFromTop (14);
        auto row = r.removeFromTop (h);
        if (extra != nullptr)
        {
            extra->setBounds (row.removeFromRight (28));
            row.removeFromRight (4);
        }
        t.setBounds (row);
        r.removeFromTop (8);
    };
    field (0, name, 26);
    {
        // type and author side by side
        auto both = r.removeFromTop (14 + 26);
        auto left = both.removeFromLeft ((both.getWidth() - 12) / 2);
        both.removeFromLeft (12);
        labels[1] = left.removeFromTop (14);
        typeMenu.setBounds (left.removeFromRight (28));
        left.removeFromRight (4);
        type.setBounds (left);
        labels[2] = both.removeFromTop (14);
        author.setBounds (both);
        r.removeFromTop (8);
    }
    field (3, tags, 26);
    field (4, notes, 72);
    labels[5] = r.removeFromTop (14);
    {
        auto row = r.removeFromTop (26);
        folderButton.setBounds (row.removeFromRight (90));
        row.removeFromRight (6);
        folderArea = row;
    }
    fileLine = r.removeFromTop (18);
    r.removeFromTop (4);
    addToBank.setBounds (r.removeFromTop (24).removeFromLeft (210));
    auto buttons = r.removeFromBottom (30);
    infoLine = buttons.removeFromLeft (buttons.getWidth() - 220);
    saveButton.setBounds (buttons.removeFromRight (104));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (104));
}

void SavePresetDialog::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));
    theme::drawPanel (g, panel.toFloat(), 12.0f);
    auto head = panel.reduced (24, 18).removeFromTop (28).toFloat();
    theme::drawGlowText (g, "SAVE PRESET", head, theme::font (16.0f, 900, 0.16f), Colours::white, theme::red.withAlpha (0.7f), 8);

    static const char* titles[] { "NAME", "TYPE", "MADE BY", "TAGS", "NOTES", "SAVE IN" };
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.5f, 700, 0.14f));
    for (int i = 0; i < 6; ++i)
        g.drawText (titles[i], labels[i].toFloat(), Justification::centredLeft, false);

    theme::drawScreen (g, folderArea.toFloat(), false);
    g.setColour (theme::ledText);
    g.setFont (theme::font (8.5f, 600, 0.04f));
    g.drawFittedText (folder.getFullPathName(), folderArea.reduced (8, 0), Justification::centredLeft, 1, 0.8f);

    const auto f = targetFile();
    g.setColour (f.existsAsFile() ? theme::red : theme::textMuted);
    g.setFont (theme::font (8.0f, 600, 0.06f));
    g.drawText (f.getFileName() + (f.existsAsFile() ? "  -  A PRESET WITH THIS NAME IS THERE ALREADY (IT GETS REPLACED)" : String()),
                fileLine.toFloat(), Justification::centredLeft, true);

    const int zones = (int) proc.getZones().size();
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.5f, 500, 0.06f));
    g.drawText (zones == 0 ? String ("NOTHING TO SAVE YET - ADD SAMPLES FIRST")
                           : String (zones) + (zones == 1 ? " ZONE" : " ZONES") + "  -  SAMPLES PACKED INSIDE THE PRESET",
                infoLine.toFloat(), Justification::centredLeft, true);
}

void SavePresetDialog::mouseDown (const MouseEvent& e)
{
    if (! panel.contains (e.getPosition()) && onClose)
        onClose();
}

bool SavePresetDialog::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey)
    {
        if (onClose) onClose();
        return true;
    }
    return false;
}
} // namespace emx
