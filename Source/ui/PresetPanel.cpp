// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PresetPanel.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

PresetPanel::PresetPanel (EmulonXProcessor& p)
    : proc (p),
      status ([this]
      {
          if (proc.isBusy())
              return String ("WORKING...");
          const auto m = proc.getLastMessage();
          return m.isNotEmpty() ? m : String ((int) proc.getZones().size()) + " ZONE(S)";
      }, 9.0f)
{
    nameLabel.setTooltip ("Preset name - click to rename");
    nameLabel.setPlaceholder ("UNTITLED");
    nameLabel.onTextChange = [this] { proc.setPresetName (nameLabel.getText()); };
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (status);

    list.setRowHeight (18);
    list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    list.setColour (ListBox::outlineColourId, Colours::transparentBlack);
    list.setMultipleSelectionEnabled (true);
    addAndMakeVisible (list);

    loadButton.setTooltip ("Open an .emxp preset (replaces this one) or add audio files as zones");
    loadButton.onClick = [this] { openLoadDialog(); };
    saveButton.setTooltip ("Save this preset as an .emxp file with all samples packed inside");
    saveButton.onClick = [this] { save(); };
    saveAsButton.setTooltip ("Save under a new file name");
    saveAsButton.onClick = [this] { saveAs(); };
    newButton.setTooltip ("Start an empty preset");
    newButton.onClick = [this] { proc.newPreset(); };

    mapButton.setTooltip ("Spread the zones across the keyboard by their root notes");
    mapButton.onClick = [this] { proc.autoMap(); };
    removeButton.setTooltip ("Remove the selected zones (Delete key)");
    removeButton.onClick = [this]
    {
        std::vector<int> ids;
        for (int i = 0; i < list.getNumSelectedRows(); ++i)
        {
            const int row = list.getSelectedRow (i);
            if (row >= 0 && row < (int) proc.getZones().size())
                ids.push_back (proc.getZones()[(size_t) row].id);
        }
        for (auto id : ids)
            proc.removeZone (id);
        list.deselectAllRows();
    };
    clearButton.setTooltip ("Remove all zones (keeps the name and macros)");
    clearButton.onClick = [this] { proc.clearZones(); };

    for (int i = 0; i < 3; ++i)
    {
        interpButtons[i].setTooltip ("Interpolation: Linear is lightest on the CPU, Sinc HQ sounds cleanest");
        interpButtons[i].onClick = [this, i] { proc.setInterpolation ((Interp) i); };
    }
    keepSpeedButton.setTooltip ("Pitch notes without changing their length (all zones)");
    keepSpeedButton.onClick = [this] { proc.setKeepSpeedForAll (! proc.allKeepSpeed()); };

    for (Component* c : { (Component*) &loadButton, (Component*) &saveButton, (Component*) &saveAsButton, (Component*) &newButton,
                          (Component*) &mapButton, (Component*) &removeButton, (Component*) &clearButton,
                          (Component*) &interpButtons[0], (Component*) &interpButtons[1], (Component*) &interpButtons[2],
                          (Component*) &keepSpeedButton })
        addAndMakeVisible (c);

    proc.addChangeListener (this);
    refresh();
}

PresetPanel::~PresetPanel() { proc.removeChangeListener (this); }

void PresetPanel::openLoadDialog()
{
    chooser = std::make_unique<FileChooser> ("Open a preset or add samples", proc.getSettings().getUserPresetFolder(),
                                             loader::supportedWildcards());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                          [this] (const FileChooser& fc)
                          {
                              if (! fc.getResults().isEmpty())
                                  proc.loadFiles (fc.getResults());
                          });
}

void PresetPanel::save()
{
    const auto f = proc.getPresetFile();
    if (f != File() && f.getParentDirectory().isDirectory()
        && f.getFileNameWithoutExtension() == loader::safeFileName (proc.getPresetName()))
        proc.savePresetAs (f);
    else
        saveAs();
}

void PresetPanel::saveAs()
{
    const auto start = proc.getSettings().getUserPresetFolder().getChildFile (loader::safeFileName (proc.getPresetName()) + ".emxp");
    chooser = std::make_unique<FileChooser> ("Save preset", start, "*.emxp");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == File())
                                  return;
                              if (! f.hasFileExtension ("emxp"))
                                  f = f.withFileExtension ("emxp");
                              proc.savePresetAs (f);
                          });
}

void PresetPanel::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void PresetPanel::refresh()
{
    if (! nameLabel.isBeingEdited())
        nameLabel.setText (proc.getPresetName(), dontSendNotification);
    status.refresh();
    list.updateContent();
    list.repaint();
    for (int i = 0; i < 3; ++i)
        interpButtons[i].setToggleState ((int) proc.getInterpolation() == i, dontSendNotification);
    keepSpeedButton.setToggleState (proc.allKeepSpeed(), dontSendNotification);
    const bool any = ! proc.getZones().empty();
    for (auto* b : { &saveButton, &saveAsButton, &mapButton, &removeButton, &clearButton, &keepSpeedButton })
        b->setEnabled (any);
    repaint();
}

int PresetPanel::getNumRows() { return (int) proc.getZones().size(); }

void PresetPanel::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
{
    const auto& zs = proc.getZones();
    if (row < 0 || row >= (int) zs.size())
        return;
    const auto& z = zs[(size_t) row];
    const auto& p = z.params;

    if (selected)
    {
        g.setColour (theme::red.withAlpha (0.18f));
        g.fillRoundedRectangle (Rectangle<float> (0, 0, (float) width, (float) height).reduced (2.0f, 1.0f), 3.0f);
    }

    auto r = Rectangle<int> (0, 0, width, height).reduced (8, 0);
    g.setFont (theme::font (10.0f, 500, 0.04f));
    g.setColour (selected ? theme::hue (0xffff6060) : theme::listText);
    g.drawText (z.name, r.removeFromLeft (jmin (200, r.getWidth() / 2)), Justification::centredLeft, true);

    String info;
    if (z.problem.isNotEmpty())
        info = z.problem;
    else
    {
        info << noteName (p.loKey) << "-" << noteName (p.hiKey) << "  ROOT " << noteName (p.root);
        if (p.loVel > 1 || p.hiVel < 127) info << "  VEL " << p.loVel << "-" << p.hiVel;
        if (p.loopMode == LoopMode::loop) info << "  LOOP";
        if (p.loopMode == LoopMode::untilRelease) info << "  LOOP TIL REL";
        if (p.keepSpeed) info << "  KEEP SPEED";
    }
    g.setColour (z.problem.isNotEmpty() ? theme::hue (0xffffb030) : theme::textMuted);
    g.setFont (theme::font (9.0f, 500, 0.04f));
    g.drawText (info, r, Justification::centredLeft, true);
}

void PresetPanel::deleteKeyPressed (int) { removeButton.triggerClick(); }

void PresetPanel::listBoxItemClicked (int row, const MouseEvent&)
{
    if (row >= 0 && row < (int) proc.getZones().size())
        proc.selectZone (proc.getZones()[(size_t) row].id);
}

void PresetPanel::listBoxItemDoubleClicked (int row, const MouseEvent&) { returnKeyPressed (row); }

void PresetPanel::returnKeyPressed (int row)
{
    if (row >= 0 && row < (int) proc.getZones().size())
        proc.selectZone (proc.getZones()[(size_t) row].id);
    if (onOpenZone)
        onOpenZone();
}

void PresetPanel::resized()
{
    auto r = getLocalBounds().reduced (11, 9);
    auto head = r.removeFromTop (18);
    head.removeFromLeft (62);   // "PRESET"
    nameLabel.setBounds (head.removeFromLeft (jmin (230, head.getWidth() / 2)));
    head.removeFromLeft (8);
    status.setBounds (head);
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (24);
    r.removeFromBottom (7);

    auto side = r.removeFromRight (92);
    r.removeFromRight (7);
    for (auto* b : { &loadButton, &saveButton, &saveAsButton, &newButton })
    {
        b->setBounds (side.removeFromTop (24));
        side.removeFromTop (6);
    }

    listArea = r.toFloat();
    list.setBounds (r.reduced (3));

    auto place = [&bottom] (Button& b, int w) { b.setBounds (bottom.removeFromLeft (w)); bottom.removeFromLeft (5); };
    place (mapButton, 80);
    place (removeButton, 70);
    place (clearButton, 58);
    bottom.removeFromLeft (10);
    place (interpButtons[0], 62);
    place (interpButtons[1], 58);
    place (interpButtons[2], 70);
    bottom.removeFromLeft (10);
    place (keepSpeedButton, jmin (96, bottom.getWidth()));
}

void PresetPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    theme::drawPanel (g, r);
    theme::drawHeading (g, r.reduced (10.0f, 9.0f), "Preset");
    theme::drawLed (g, listArea, {});

    if (proc.getZones().empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (9.0f, 500, 0.06f));
        g.drawFittedText ("NO ZONES YET.\nDRAG AUDIO FILES, AN .EMXP PRESET OR AN .EMXB BANK ONTO THE WINDOW,\n"
                          "DOUBLE-CLICK SOMETHING IN THE EXPLORER, OR PRESS LOAD.",
                          listArea.reduced (10.0f, 8.0f).toNearestInt(), Justification::topLeft, 4, 1.0f);
    }
}
} // namespace emx
