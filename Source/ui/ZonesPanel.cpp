// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ZonesPanel.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

ZonesPanel::ZonesPanel (EmulonXProcessor& p) : proc (p)
{
    list.setRowHeight (18);
    list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    list.setColour (ListBox::outlineColourId, Colours::transparentBlack);
    list.setMultipleSelectionEnabled (true);
    addAndMakeVisible (list);

    loadButton.setTooltip ("Add audio files (WAV, AIFF, FLAC, OGG, MP3) or an .emxp preset");
    loadButton.onClick = [this] { openLoadDialog(); };
    mapButton.setTooltip ("Spread the zones across the keyboard by their root notes");
    mapButton.onClick = [this] { proc.autoMap(); };
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
    clearButton.onClick = [this] { proc.clearZones(); };

    for (int i = 0; i < 3; ++i)
    {
        interpButtons[i].setTooltip ("Interpolation: Linear is lightest on the CPU, Sinc HQ sounds cleanest");
        interpButtons[i].onClick = [this, i] { proc.setInterpolation ((Interp) i); };
    }
    keepSpeedButton.setTooltip ("Pitch notes without changing their length (all zones)");
    keepSpeedButton.onClick = [this] { proc.setKeepSpeedForAll (! proc.allKeepSpeed()); };

    for (Component* c : { (Component*) &loadButton, (Component*) &mapButton, (Component*) &removeButton, (Component*) &clearButton,
                          (Component*) &interpButtons[0], (Component*) &interpButtons[1], (Component*) &interpButtons[2],
                          (Component*) &keepSpeedButton })
        addAndMakeVisible (c);

    proc.addChangeListener (this);
    refresh();
}

ZonesPanel::~ZonesPanel() { proc.removeChangeListener (this); }

void ZonesPanel::openLoadDialog()
{
    chooser = std::make_unique<FileChooser> ("Add samples or a preset", File(), loader::supportedWildcards());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                          [this] (const FileChooser& fc)
                          {
                              if (! fc.getResults().isEmpty())
                                  proc.loadFiles (fc.getResults());
                          });
}

void ZonesPanel::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void ZonesPanel::refresh()
{
    list.updateContent();
    list.repaint();
    for (int i = 0; i < 3; ++i)
        interpButtons[i].setToggleState ((int) proc.getInterpolation() == i, dontSendNotification);
    keepSpeedButton.setToggleState (proc.allKeepSpeed(), dontSendNotification);
    const bool any = ! proc.getZones().empty();
    mapButton.setEnabled (any);
    removeButton.setEnabled (any);
    clearButton.setEnabled (any);
    keepSpeedButton.setEnabled (any);
    repaint (statusArea.toNearestInt().expanded (2));
}

int ZonesPanel::getNumRows() { return (int) proc.getZones().size(); }

void ZonesPanel::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
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
    g.setColour (selected ? Colour (0xffff6060) : Colour (0xffdd3333));
    g.drawText (z.name, r.removeFromLeft (jmin (220, r.getWidth() / 2)), Justification::centredLeft, true);

    String info;
    if (z.problem.isNotEmpty())
        info = z.problem;
    else
    {
        info << noteName (p.loKey) << "-" << noteName (p.hiKey) << "  ROOT " << noteName (p.root);
        if (p.loVel > 1 || p.hiVel < 127) info << "  VEL " << p.loVel << "-" << p.hiVel;
        if (p.loopMode == LoopMode::loop) info << "  LOOP";
        if (p.loopMode == LoopMode::untilRelease) info << "  LOOP UNTIL RELEASE";
        if (p.keepSpeed) info << "  KEEP SPEED";
    }
    g.setColour (z.problem.isNotEmpty() ? Colour (0xffffb030) : Colour (0xff8a2a2a));
    g.setFont (theme::font (9.0f, 500, 0.04f));
    g.drawText (info, r, Justification::centredLeft, true);
}

void ZonesPanel::deleteKeyPressed (int) { removeButton.triggerClick(); }

void ZonesPanel::resized()
{
    auto r = getLocalBounds().reduced (11, 9);
    r.removeFromTop (22);
    auto buttons = r.removeFromBottom (26);
    r.removeFromBottom (6);
    statusArea = r.removeFromBottom (18).toFloat();
    r.removeFromBottom (6);
    listArea = r.toFloat();
    list.setBounds (r.reduced (3));

    auto place = [&buttons] (Button& b, int w) { b.setBounds (buttons.removeFromLeft (w)); buttons.removeFromLeft (5); };
    place (loadButton, 74);
    place (mapButton, 84);
    place (removeButton, 74);
    place (clearButton, 62);
    buttons.removeFromLeft (8);
    place (interpButtons[0], 66);
    place (interpButtons[1], 62);
    place (interpButtons[2], 74);
    place (keepSpeedButton, jmin (100, buttons.getWidth()));
}

void ZonesPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    theme::drawPanel (g, r);
    theme::drawHeading (g, r.reduced (10.0f, 8.0f), "Zones", "drop audio files or .emxp presets onto the window");
    theme::drawLed (g, listArea, {});

    if (proc.getZones().empty())
    {
        g.setColour (theme::redDark);
        g.setFont (theme::font (9.0f, 500, 0.06f));
        g.drawText ("NO ZONES YET - DRAG WAV / AIFF / FLAC FILES OR A PROTOTYPE .EMXP PRESET HERE, OR PRESS LOAD",
                    listArea.reduced (10.0f, 6.0f), Justification::topLeft, true);
    }

    String status = proc.isLoading() ? String ("LOADING...") : proc.getLastMessage();
    if (status.isEmpty())
        status = String ((int) proc.getZones().size()) + " ZONE(S)";
    theme::drawLed (g, statusArea, status, 9.0f, Justification::centredLeft);
}
} // namespace emx
