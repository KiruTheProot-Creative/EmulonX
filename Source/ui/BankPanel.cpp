// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BankPanel.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

BankPanel::BankPanel (EmulonXProcessor& p) : proc (p)
{
    nameLabel.setTooltip ("Bank name - click to rename");
    nameLabel.setPlaceholder ("MY BANK");
    nameLabel.onTextChange = [this]
    {
        const auto n = nameLabel.getText().trim();
        proc.setBankName (n.isNotEmpty() ? n.substring (0, 40) : String ("My Bank"));
    };
    addAndMakeVisible (nameLabel);

    list.setRowHeight (18);
    list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    list.setColour (ListBox::outlineColourId, Colours::transparentBlack);
    addAndMakeVisible (list);

    addButton.setTooltip ("Put a copy of the current preset (with its samples) into the bank");
    addButton.onClick = [this] { proc.addCurrentPresetToBank(); };
    loadButton.setTooltip ("Load the selected preset (or double-click it)");
    loadButton.onClick = [this] { proc.loadBankEntry (list.getSelectedRow()); };
    removeButton.setTooltip ("Take the selected preset out of the bank");
    removeButton.onClick = [this] { proc.removeBankEntry (list.getSelectedRow()); };
    clearButton.setTooltip ("Empty the bank");
    clearButton.onClick = [this] { proc.clearBank(); };

    openButton.setTooltip ("Open an .emxb bank file into this list");
    openButton.onClick = [this]
    {
        auto folders = proc.getSettings().getFolders (Settings::Folder::base);
        chooser = std::make_unique<FileChooser> ("Open bank", folders.isEmpty() ? File() : File (folders[0]), "*.emxb;*.zip");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  if (fc.getResult() != File())
                                      proc.openBank (fc.getResult());
                              });
    };

    exportButton.setTooltip ("Save every preset in the list as one .emxb bank file");
    exportButton.onClick = [this]
    {
        auto folders = proc.getSettings().getFolders (Settings::Folder::other);
        if (folders.isEmpty())
            folders = proc.getSettings().getFolders (Settings::Folder::base);
        const auto dir = folders.isEmpty() ? proc.getSettings().getUserPresetFolder() : File (folders[0]);
        chooser = std::make_unique<FileChooser> ("Export bank", dir.getChildFile (loader::safeFileName (proc.getBankName()) + ".emxb"), "*.emxb");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [this] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == File())
                                      return;
                                  if (! f.hasFileExtension ("emxb"))
                                      f = f.withFileExtension ("emxb");
                                  proc.exportBank (f);
                              });
    };

    for (auto* b : { &addButton, &loadButton, &removeButton, &openButton, &exportButton, &clearButton })
        addAndMakeVisible (b);

    proc.addChangeListener (this);
    refresh();
}

BankPanel::~BankPanel() { proc.removeChangeListener (this); }

void BankPanel::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void BankPanel::refresh()
{
    if (! nameLabel.isBeingEdited())
        nameLabel.setText (proc.getBankName(), dontSendNotification);
    list.updateContent();
    list.repaint();
    const bool any = ! proc.getBank().empty();
    for (auto* b : { &loadButton, &removeButton, &exportButton, &clearButton })
        b->setEnabled (any);
    addButton.setEnabled (! proc.getZones().empty());
    repaint();
}

int BankPanel::getNumRows() { return (int) proc.getBank().size(); }

void BankPanel::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
{
    const auto& bank = proc.getBank();
    if (row < 0 || row >= (int) bank.size())
        return;
    if (selected)
    {
        g.setColour (theme::red.withAlpha (0.18f));
        g.fillRoundedRectangle (Rectangle<float> (0, 0, (float) width, (float) height).reduced (2.0f, 1.0f), 3.0f);
    }
    auto r = Rectangle<int> (0, 0, width, height).reduced (8, 0);
    g.setFont (theme::font (9.0f, 500, 0.04f));
    g.setColour (theme::textMuted);
    g.drawText (String (row + 1).paddedLeft ('0', 2), r.removeFromLeft (26), Justification::centredLeft, false);
    g.setFont (theme::font (10.0f, 500, 0.04f));
    g.setColour (selected ? theme::hue (0xffff6060) : theme::listText);
    g.drawText (bank[(size_t) row].name, r, Justification::centredLeft, true);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 500, 0.04f));
    g.drawText (String (bank[(size_t) row].data.getSize() / (1024.0 * 1024.0), 1) + " MB", r, Justification::centredRight, false);
}

void BankPanel::listBoxItemDoubleClicked (int row, const MouseEvent&) { proc.loadBankEntry (row); }
void BankPanel::deleteKeyPressed (int row) { proc.removeBankEntry (row); }
void BankPanel::returnKeyPressed (int row) { proc.loadBankEntry (row); }

void BankPanel::resized()
{
    auto r = getLocalBounds().reduced (11, 9);
    auto head = r.removeFromTop (18);
    head.removeFromLeft (50);   // "BANK"
    nameLabel.setBounds (head.removeFromLeft (jmin (230, head.getWidth() / 2)));
    r.removeFromTop (8);

    auto side = r.removeFromRight (2 * 104 + 6);
    r.removeFromRight (7);
    auto colA = side.removeFromLeft (104);
    side.removeFromLeft (6);
    auto colB = side;
    for (auto* b : { &addButton, &loadButton, &removeButton })
    {
        b->setBounds (colA.removeFromTop (24));
        colA.removeFromTop (6);
    }
    for (auto* b : { &openButton, &exportButton, &clearButton })
    {
        b->setBounds (colB.removeFromTop (24));
        colB.removeFromTop (6);
    }
    listArea = r.toFloat();
    list.setBounds (r.reduced (3));
}

void BankPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    theme::drawPanel (g, r);
    theme::drawHeading (g, r.reduced (10.0f, 9.0f), "Bank");

    auto head = getLocalBounds().reduced (11, 9).removeFromTop (18).toFloat();
    head.removeFromLeft (50.0f + (float) jmin (230, (getWidth() - 72) / 2) + 10.0f);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 500, 0.1f));
    g.drawText (String ((int) proc.getBank().size()) + " PRESET(S) - EXPORT AS ONE .EMXB FILE", head, Justification::centredLeft, true);

    theme::drawLed (g, listArea, {});
    if (proc.getBank().empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (9.0f, 500, 0.06f));
        g.drawFittedText ("EMPTY. PRESS ADD CURRENT TO COLLECT PRESETS, OR OPEN AN .EMXB BANK.",
                          listArea.reduced (10.0f, 8.0f).toNearestInt(), Justification::topLeft, 2, 1.0f);
    }
}
} // namespace emx
