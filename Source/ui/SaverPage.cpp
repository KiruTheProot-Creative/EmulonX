// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SaverPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

static const char* const commonTypes[] { "Ambient", "Arp", "Bass", "Bells", "Brass", "Choir", "Drum Kit", "Drums", "FX", "Guitar", "Keys",
                                         "Lead", "Loop", "Organ", "Pad", "Percussion", "Piano", "Pluck", "Strings", "Synth", "Texture", "Voice", "Wind" };

SaverPage::SaverPage (EmulonXProcessor& p) : proc (p), presetPanel (p), bankPanel (p)
{
    addAndMakeVisible (presetPanel);
    addAndMakeVisible (bankPanel);

    auto setup = [this] (TextEditor& t, const String& hintText, int maxLen, const String& tip)
    {
        t.setFont (theme::font (11.0f, 600, 0.03f));
        t.setColour (TextEditor::textColourId, theme::ledText);
        t.setIndents (8, 6);
        t.setTextToShowWhenEmpty (hintText, theme::textMuted.withAlpha (0.6f));
        t.setInputRestrictions (maxLen);
        t.setTooltip (tip);
        t.onTextChange = [this] { startTimer (500); };
        t.onReturnKey = [this] { commit(); unfocusAllComponents(); };
        t.onFocusLost = [this] { commit(); };
        addAndMakeVisible (t);
    };
    setup (type, "Pad, Bass, Lead... type anything", 32, "The preset's type - shown on the Main tab's display");
    setup (author, "Your name", 48, "Who made the preset - shown on the Main tab's display");
    setup (tags, "warm, slow, analog...", 200, "Words that help finding the preset later");
    typeMenu.setTooltip ("Pick a common type (or just type your own)");
    typeMenu.onClick = [this]
    {
        PopupMenu m;
        for (auto* t : commonTypes)
            m.addItem (t, [this, t] { type.setText (t, false); commit(); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&typeMenu));
    };
    addAndMakeVisible (typeMenu);

    proc.addChangeListener (this);
    refresh();
}

SaverPage::~SaverPage()
{
    stopTimer();
    proc.removeChangeListener (this);
}

void SaverPage::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void SaverPage::timerCallback()
{
    stopTimer();
    commit();
}

void SaverPage::refresh()
{
    const auto& info = proc.getPresetInfo();
    const auto last = proc.getSettings().properties().getValue ("presetAuthor");
    author.setTextToShowWhenEmpty (last.isNotEmpty() ? last + "  (used when you save)" : String ("Your name"), theme::textMuted.withAlpha (0.6f));
    if (! type.hasKeyboardFocus (true) && ! isTimerRunning())   type.setText (info.type, false);
    if (! author.hasKeyboardFocus (true) && ! isTimerRunning()) author.setText (info.author, false);
    if (! tags.hasKeyboardFocus (true) && ! isTimerRunning())   tags.setText (info.tags, false);
}

void SaverPage::commit()
{
    stopTimer();
    auto info = proc.getPresetInfo();
    const auto t = type.getText().trim(), a = author.getText().trim(), g = tags.getText().trim();
    if (t == info.type && a == info.author && g == info.tags)
        return;
    info.type = t;
    info.author = a;
    info.tags = g;
    if (a.isNotEmpty())
        proc.getSettings().properties().setValue ("presetAuthor", a);   // offered for the next preset
    proc.setPresetInfo (info);
}

void SaverPage::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (214);
    infoArea = top.removeFromRight (jmin (330, top.getWidth() / 3)).toFloat();
    top.removeFromRight (8);
    presetPanel.setBounds (top);
    r.removeFromTop (8);
    bankPanel.setBounds (r);

    auto a = infoArea.toNearestInt().reduced (11, 9);
    a.removeFromTop (22 + 6);
    infoInset = a.toFloat();
    auto in = a.reduced (12, 10);
    auto field = [&] (int i, TextEditor& t, Component* extra)
    {
        labels[i] = in.removeFromTop (14);
        auto row = in.removeFromTop (26);
        if (extra != nullptr)
        {
            extra->setBounds (row.removeFromRight (28));
            row.removeFromRight (4);
        }
        t.setBounds (row);
        in.removeFromTop (8);
    };
    field (0, type, &typeMenu);
    field (1, author, nullptr);
    field (2, tags, nullptr);
}

void SaverPage::paint (Graphics& g)
{
    theme::drawPanel (g, infoArea.reduced (1.0f));
    theme::drawHeading (g, infoArea.reduced (11.0f, 9.0f), "Preset info", "saved with the preset");
    theme::drawInset (g, infoInset);
    static const char* titles[] { "TYPE", "MADE BY", "TAGS" };
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.5f, 700, 0.14f));
    for (int i = 0; i < 3; ++i)
        g.drawText (titles[i], labels[i].toFloat(), Justification::centredLeft, false);
}
} // namespace emx
