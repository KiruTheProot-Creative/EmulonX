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
      bankPanel (p)
{
    addAndMakeVisible (presetPanel);
    addAndMakeVisible (bankPanel);

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
    for (auto* k : macros)
    {
        k->refreshCaption();
        k->setSensitivity (sens);
    }
}

void MainPage::tick() {}

void MainPage::resized()
{
    auto r = getLocalBounds();
    presetPanel.setBounds (r.removeFromTop (214));

    // Macros sit at the very bottom; the bank fills everything in between
    macroArea = r.removeFromBottom (2 * macros[0]->getPreferredHeight() + 60).toFloat();
    r.removeFromBottom (8);
    r.removeFromTop (8);
    bankPanel.setBounds (r);

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
    theme::drawPanel (g, macroArea.reduced (1.0f));
    theme::drawHeading (g, macroArea.reduced (11.0f, 9.0f), "Macro controller", "automatable in your DAW - double-click a name to rename - right-click to link to FX");
    theme::drawInset (g, macroInset);
}
} // namespace emx
