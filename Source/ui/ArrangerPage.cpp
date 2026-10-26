// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ArrangerPage.h"
#include "EditorPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../engine/SampleLoader.h"

namespace emx
{
using namespace juce;

// Column widths (the preset name takes what is left)
namespace col
{
static constexpr int part = 44, ch = 74, key = 50, vol = 74, pan = 58, ms = 25, out = 78, meter = 70, gap = 6;
}

//== One part ====================================================================
class ArrangerPage::Row final : public Component
{
public:
    Row (EmulonXProcessor& m, int index) : main (m), k (index)
    {
        partButton.setButtonText (String (k + 1).paddedLeft ('0', 2));
        partButton.setTooltip ("Edit and play this part (the other tabs and the keyboard switch to it)");
        partButton.onClick = [this] { main.setEditedPart (k); };

        channel.addItem ("ALL", 100);
        for (int c = 1; c <= 16; ++c)
            channel.addItem ("CH " + String (c), c);
        channel.setTooltip ("The MIDI channel this part listens to - set your DAW track's MIDI output to it");
        channel.onChange = [this] { const int id = channel.getSelectedId(); if (id > 0) setInfo ([id] (auto& p) { p.channel = id == 100 ? 0 : id; }); };

        keyLo.setRange (0, 127, 1);
        keyHi.setRange (0, 127, 1);
        keyLo.format = keyHi.format = [] (double v) { return noteName (roundToInt (v)); };
        keyLo.setTooltip ("Lowest note this part plays (for splits)");
        keyHi.setTooltip ("Highest note this part plays (for splits)");
        keyLo.onChange = [this] (double v) { const int x = roundToInt (v); setInfo ([x] (auto& p) { p.keyLo = x; p.keyHi = jmax (p.keyHi, x); }); };
        keyHi.onChange = [this] (double v) { const int x = roundToInt (v); setInfo ([x] (auto& p) { p.keyHi = x; p.keyLo = jmin (p.keyLo, x); }); };

        volume.setRange (-60.0, 6.0, 0.1);
        volume.format = [] (double v) { return v <= -59.9 ? String ("-INF") : (v > 0.0 ? "+" : "") + String (v, 1); };
        volume.setTooltip ("The part's volume in dB (the same as the Main tab's VOLUME knob of that part)");
        volume.onChange = [this] (double v) { if (auto* p = main.getPart (k)) p->setPartVolume ((float) v); };
        pan.setRange (-100.0, 100.0, 1.0);
        pan.format = [] (double v) { const int x = roundToInt (v); return x == 0 ? String ("C") : (x < 0 ? "L" + String (-x) : "R" + String (x)); };
        pan.setTooltip ("The part's pan");
        pan.onChange = [this] (double v) { if (auto* p = main.getPart (k)) p->setPartPan ((float) (v / 100.0)); };

        mute.setButtonText ("M");
        solo.setButtonText ("S");
        mute.setTooltip ("Mute this part");
        solo.setTooltip ("Solo: only soloed parts play");
        mute.onClick = [this] { setInfo ([] (auto& p) { p.mute = ! p.mute; }); };
        solo.onClick = [this] { setInfo ([] (auto& p) { p.solo = ! p.solo; }); };

        output.addItem ("MAIN", 1);
        for (int o = 2; o <= EmulonXProcessor::numOutputs; ++o)
            output.addItem ("OUT " + String (o), o);
        output.setTooltip ("Which output of the plugin this part comes out of (turn the extra outputs on in your DAW; if one is off, the part uses MAIN)");
        output.onChange = [this] { const int id = output.getSelectedId(); if (id > 0) setInfo ([id] (auto& p) { p.output = id - 1; }); };

        for (Component* c : { (Component*) &partButton, (Component*) &channel, (Component*) &keyLo, (Component*) &keyHi, (Component*) &volume,
                              (Component*) &pan, (Component*) &mute, (Component*) &solo, (Component*) &output })
            addAndMakeVisible (c);
    }

    void refresh()
    {
        const auto info = main.getPartInfo (k);
        auto* p = main.getPart (k);
        used = p != nullptr;
        name = used ? p->getPresetName() : String();
        partButton.setToggleState (main.getEditedPartIndex() == k, dontSendNotification);
        channel.setSelectedId (info.channel == 0 ? 100 : info.channel, dontSendNotification);
        keyLo.setValue (info.keyLo);
        keyHi.setValue (info.keyHi);
        volume.setValue (used ? p->getPartVolume() : 0.0);
        pan.setValue (used ? std::round (p->getPartPan() * 100.0f) : 0.0);
        mute.setToggleState (info.mute, dontSendNotification);
        solo.setToggleState (info.solo, dontSendNotification);
        output.setSelectedId (info.output + 1, dontSendNotification);
        for (auto* c : { (Component*) &volume, (Component*) &pan })
            c->setEnabled (used);
        setAlpha (k == 0 || main.isArrangerOn() ? 1.0f : 0.45f);
        repaint();
    }

    /** Level meter and name changes made in the part itself (from the timer). */
    void tick()
    {
        const float pk = main.takePartPeak (k);
        const float lvl = jmax (pk, level * 0.82f);
        auto* p = main.getPart (k);
        const auto now = p != nullptr ? p->getPresetName() : String();
        if (p != nullptr)   // the Main tab's knobs change these too
        {
            volume.setValue (p->getPartVolume());
            pan.setValue (std::round (p->getPartPan() * 100.0f));
        }
        if (std::abs (lvl - level) > 0.002f || now != name || (p != nullptr) != used)
        {
            if (now != name || (p != nullptr) != used)
                refresh();
            level = lvl;
            repaint (meterArea.expanded (2.0f).toNearestInt());
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        partButton.setBounds (r.removeFromLeft (col::part));
        r.removeFromLeft (col::gap);
        meterArea = r.removeFromRight (col::meter).toFloat().reduced (0.0f, 5.0f);
        r.removeFromRight (col::gap);
        output.setBounds (r.removeFromRight (col::out));
        r.removeFromRight (col::gap);
        solo.setBounds (r.removeFromRight (col::ms));
        r.removeFromRight (2);
        mute.setBounds (r.removeFromRight (col::ms));
        r.removeFromRight (col::gap);
        pan.setBounds (r.removeFromRight (col::pan));
        r.removeFromRight (col::gap);
        volume.setBounds (r.removeFromRight (col::vol));
        r.removeFromRight (col::gap);
        keyHi.setBounds (r.removeFromRight (col::key));
        r.removeFromRight (2);
        keyLo.setBounds (r.removeFromRight (col::key));
        r.removeFromRight (col::gap);
        channel.setBounds (r.removeFromRight (col::ch));
        r.removeFromRight (col::gap);
        nameArea = r.toFloat();
    }

    void paint (Graphics& g) override
    {
        const bool edited = main.getEditedPartIndex() == k;
        theme::drawScreen (g, nameArea, false);
        auto t = nameArea.reduced (8.0f, 0.0f);
        if (used)
        {
            g.setColour (theme::ledText);
            g.setFont (theme::font (10.0f, 700, 0.08f));
            g.drawText (name.toUpperCase(), t, Justification::centredLeft, true);
        }
        else
        {
            g.setColour (theme::textMuted.withAlpha (0.6f));
            g.setFont (theme::font (8.0f, 600, 0.12f));
            g.drawText ("EMPTY - CLICK TO ADD A PRESET", t, Justification::centredLeft, true);
        }
        if (edited)
        {
            g.setColour (theme::red.withAlpha (0.9f));
            g.drawRoundedRectangle (nameArea.reduced (0.5f), 4.0f, 1.4f);
        }

        // level meter
        g.setColour (Colour (0xff121214));
        g.fillRoundedRectangle (meterArea, 2.0f);
        const float db = level > 0.00001f ? Decibels::gainToDecibels (level) : -60.0f;
        const float x = jlimit (0.0f, 1.0f, (db + 48.0f) / 54.0f);
        if (x > 0.0f)
        {
            auto bar = meterArea.reduced (1.5f).withWidth ((meterArea.getWidth() - 3.0f) * x);
            g.setGradientFill (ColourGradient::horizontal (theme::hue (0xff8a1018), meterArea.getX(), theme::hue (db > -3.0f ? 0xffffd0d0 : 0xffff3a3a), meterArea.getRight()));
            g.fillRoundedRectangle (bar, 1.5f);
        }
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (meterArea.reduced (0.5f), 2.0f, 1.0f);
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (! nameArea.contains (e.position))
            return;
        if (! used || e.mods.isPopupMenu() || main.getEditedPartIndex() == k)
            showMenu();
        else
            main.setEditedPart (k);
    }

private:
    void setInfo (std::function<void (EmulonXProcessor::PartInfo&)> f)
    {
        auto info = main.getPartInfo (k);
        f (info);
        main.setPartInfo (k, info);
    }

    void showMenu()
    {
        PopupMenu m;
        auto& mainRef = main;
        const int idx = k;
        m.addItem (used ? "Edit and play this part" : "Add an empty part (Init) and edit it", [&mainRef, idx] { mainRef.setEditedPart (idx); });
        m.addItem ("Load a preset into this part...", [this]
        {
            chooser = std::make_unique<FileChooser> ("Load a preset into part " + String (k + 1), main.getSettings().getUserPresetFolder(), "*.emxp;*.exb");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f == File())
                    return;
                main.setEditedPart (k);   // makes the part if needed; the window then shows it
                if (auto* p = main.getPart (k))
                    p->loadFiles ({ f });
            });
        });
        const int from = main.getEditedPartIndex();
        if (from != k)
            m.addItem ("Copy part " + String (from + 1) + " here (the whole preset)", [&mainRef, from, idx] { mainRef.copyPartTo (from, idx); });
        m.addSeparator();
        m.addItem (k == 0 ? "Reset part 1 (Init preset)" : "Clear this part", used, false, [&mainRef, idx] { mainRef.clearPart (idx); });
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (nameArea.toNearestInt())));
    }

    EmulonXProcessor& main;
    const int k;
    bool used = false;
    String name;
    float level = 0.0f;
    TextButton partButton, mute, solo;
    ComboBox channel, output;
    ValueBox keyLo, keyHi, volume, pan;
    Rectangle<float> nameArea, meterArea;
    std::unique_ptr<FileChooser> chooser;
};

//== Page ========================================================================
ArrangerPage::ArrangerPage (EmulonXProcessor& p) : proc (p)
{
    multiButton.setTooltip ("Multimode: all 16 parts play, each on its own MIDI channel");
    singleButton.setTooltip ("One preset (part 1), on the MIDI channel from the settings");
    multiButton.onClick = [this] { proc.setArrangerOn (true); };
    singleButton.onClick = [this] { proc.setArrangerOn (false); };
    allNotesOff.setTooltip ("Stop every sound in every part");
    allNotesOff.onClick = [this]
    {
        for (int k = 0; k < EmulonXProcessor::numParts; ++k)
            if (auto* part = proc.getPart (k))
                part->panic();
    };
    for (Component* c : { (Component*) &multiButton, (Component*) &singleButton, (Component*) &allNotesOff })
        addAndMakeVisible (c);
    for (int k = 0; k < EmulonXProcessor::numParts; ++k)
        addAndMakeVisible (rows.add (new Row (proc, k)));
    proc.addChangeListener (this);
    refresh();
}

ArrangerPage::~ArrangerPage() { proc.removeChangeListener (this); }

void ArrangerPage::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void ArrangerPage::refresh()
{
    multiButton.setToggleState (proc.isArrangerOn(), dontSendNotification);
    singleButton.setToggleState (! proc.isArrangerOn(), dontSendNotification);
    for (auto* r : rows)
        r->refresh();
    shownVersion = proc.getArrangerVersion();
    repaint();
}

void ArrangerPage::tick()
{
    if (shownVersion != proc.getArrangerVersion())
        refresh();
    for (auto* r : rows)
        r->tick();
}

void ArrangerPage::resized()
{
    panel = getLocalBounds().toFloat();
    auto a = getLocalBounds().reduced (11, 9);
    auto head = a.removeFromTop (22);
    allNotesOff.setBounds (head.removeFromRight (110).reduced (0, 1));
    head.removeFromRight (12);
    singleButton.setBounds (head.removeFromRight (66).reduced (0, 1));
    head.removeFromRight (3);
    multiButton.setBounds (head.removeFromRight (66).reduced (0, 1));
    a.removeFromTop (6);
    hint = a.removeFromBottom (16).toFloat();
    a.removeFromBottom (4);
    table = a.toFloat();
    auto in = a.reduced (10, 6);
    columns = in.removeFromTop (14).toFloat();
    const int rh = in.getHeight() / EmulonXProcessor::numParts;
    for (auto* r : rows)
        r->setBounds (in.removeFromTop (rh));
}

void ArrangerPage::paint (Graphics& g)
{
    theme::drawPanel (g, panel.reduced (1.0f));
    theme::drawHeading (g, panel.reduced (11.0f, 9.0f), "Arranger", "16 presets at once - each part listens to its own MIDI channel");
    theme::drawInset (g, table);

    // column titles, lined up with the rows' layout
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.0f, 700, 0.14f));
    auto c = columns;
    auto take = [&c] (int w, bool right = false) { auto r = right ? c.removeFromRight ((float) w) : c.removeFromLeft ((float) w); return r; };
    g.drawText ("PART", take (col::part), Justification::centred, false);
    take (col::gap);
    g.drawText ("LEVEL", take (col::meter, true), Justification::centred, false);
    take (col::gap, true);
    g.drawText ("OUTPUT", take (col::out, true), Justification::centred, false);
    take (col::gap, true);
    g.drawText ("M / S", take (col::ms * 2 + 2, true), Justification::centred, false);
    take (col::gap, true);
    g.drawText ("PAN", take (col::pan, true), Justification::centred, false);
    take (col::gap, true);
    g.drawText ("VOL DB", take (col::vol, true), Justification::centred, false);
    take (col::gap, true);
    g.drawText ("KEYS", take (col::key * 2 + 2, true), Justification::centred, false);
    take (col::gap, true);
    g.drawText ("MIDI", take (col::ch, true), Justification::centred, false);
    take (col::gap, true);
    g.drawText ("PRESET", c.withTrimmedLeft (8.0f), Justification::centredLeft, false);

    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 500, 0.04f));
    g.drawText (proc.isArrangerOn()
                    ? "Click a part to edit it - the Explorer then loads presets into that part. Set each DAW track's MIDI output to the part's channel."
                    : "Single mode: only part 1 plays, on the MIDI channel from the settings. Switch to MULTI to play all parts.",
                hint, Justification::centredLeft, true);
}
} // namespace emx
