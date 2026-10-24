// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"
#include "FxPage.h"

namespace emx
{
using namespace juce;

MainPage::MainPage (EmulonXProcessor& p)
    : proc (p),
      presetPanel (p),
      bankPanel (p),
      masterKnob (p.apvts, masterParamId, "Volume", 58),
      peakLed ([this]
      {
          const auto db = meter.getPeakDb();
          return db < -59.0f ? String ("-INF DB") : (db > 0 ? "+" : "") + String (db, 1) + " DB";
      }, 10.0f)
{
    // Master volume: right-click to modulate it, or click it while a macro is waiting to be linked.
    const fx::Target volTarget { fx::Target::masterVolume, 0 };
    masterKnob.slider.onRightClick = [this, volTarget] { modui::showMenu (proc, volTarget, &masterKnob); };
    masterKnob.slider.interceptClick = [this, volTarget] { return modui::tryLearn (proc, volTarget); };
    masterKnob.modRings = [this, volTarget] { return modui::rings (proc, volTarget, proc.getMasterVolumeNorm()); };
    masterKnob.setRingMargin (8);
    masterKnob.slider.setTooltip ("Master volume - drag, double-click resets, right-click to modulate");

    addAndMakeVisible (presetPanel);
    addAndMakeVisible (bankPanel);
    addAndMakeVisible (masterKnob);
    addAndMakeVisible (meter);
    addAndMakeVisible (peakLed);

    for (int i = 0; i < numMacros; ++i)
    {
        auto* k = macros.add (new EmxKnob (p.apvts, macroParamId (i), {}, 40));
        k->setEditableCaption ([this, i] { return proc.getMacroName (i); },
                               [this, i] (const String& n) { proc.setMacroName (i, n); });
        k->slider.onRightClick = [this, i, k] { modui::showMacroMenu (proc, i, k); };
        k->slider.setTooltip ("Automatable in your DAW - right-click to link it to FX knobs");
        addAndMakeVisible (k);
    }

    proc.addChangeListener (this);
    settings->addChangeListener (this);
    changeListenerCallback (nullptr);
}

MainPage::~MainPage()
{
    proc.removeChangeListener (this);
    settings->removeChangeListener (this);
}

void MainPage::changeListenerCallback (ChangeBroadcaster*)
{
    // A preset may have brought new macro names; the settings may have a new knob sensitivity.
    const int sens = settings->getKnobSensitivity();
    masterKnob.setSensitivity (sens);
    for (auto* k : macros)
    {
        k->refreshCaption();
        k->setSensitivity (sens);
    }
}

void MainPage::tick()
{
    meter.push (proc.takePeak (0), proc.takePeak (1));
    peakLed.refresh();
    if (! proc.getFxSetup().routes.empty())
        masterKnob.repaint();
}

void MainPage::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (214);
    const int w3 = (top.getWidth() - 16) / 3;
    presetPanel.setBounds (top.removeFromLeft (2 * w3 + 8));
    top.removeFromLeft (8);
    masterArea = top.toFloat();

    // Macros sit at the very bottom; the bank fills everything in between
    macroArea = r.removeFromBottom (2 * macros[0]->getPreferredHeight() + 60).toFloat();
    r.removeFromBottom (8);
    r.removeFromTop (8);
    bankPanel.setBounds (r);

    // Master panel content
    auto m = masterArea.reduced (10.0f, 8.0f).withTrimmedTop (22.0f).reduced (4.0f, 6.0f);
    auto row = m.withSizeKeepingCentre (m.getWidth(), (float) masterKnob.getPreferredHeight()).toNearestInt();
    const int col = row.getWidth() / 3;
    masterKnob.setBounds (row.removeFromLeft (col));
    meter.setBounds (row.removeFromLeft (col).withSizeKeepingCentre (26, row.getHeight() - 10));
    peakLed.setBounds (row.withSizeKeepingCentre (jmin (row.getWidth(), 78), 22));

    // Macro panel
    macroInset = macroArea.reduced (10.0f, 8.0f).withTrimmedTop (22.0f);
    auto grid = macroInset.reduced (6.0f, 8.0f).toNearestInt();
    constexpr int perRow = numMacros / 2;   // two rows of 8
    const int cellW = grid.getWidth() / perRow, cellH = macros[0]->getPreferredHeight();
    for (int i = 0; i < macros.size(); ++i)
        macros[i]->setBounds (grid.getX() + (i % perRow) * cellW, grid.getY() + (i / perRow) * (cellH + 10), cellW, cellH);
}

void MainPage::paint (Graphics& g)
{
    theme::drawPanel (g, masterArea.reduced (1.0f));
    auto mi = theme::drawHeading (g, masterArea.reduced (11.0f, 9.0f), "Master");
    theme::drawInset (g, mi);

    theme::drawPanel (g, macroArea.reduced (1.0f));
    theme::drawHeading (g, macroArea.reduced (11.0f, 9.0f), "Macro controller", "automatable in your DAW - double-click a name to rename - right-click to link to FX");
    theme::drawInset (g, macroInset);
}
} // namespace emx
