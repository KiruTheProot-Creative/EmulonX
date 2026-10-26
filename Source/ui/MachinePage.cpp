// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MachinePage.h"
#include "EditorPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

MachinePage::MachinePage (EmulonXProcessor& p) : proc (p)
{
    grid4.onClick = [this] { edit ([] (MachineSettings& m) { m.grid = 4; }); selected = jmin (selected, 15); };
    grid8.onClick = [this] { edit ([] (MachineSettings& m) { m.grid = 8; }); };
    grid4.setTooltip ("16 pads");
    grid8.setTooltip ("64 pads");
    playButton.setTooltip ("Run the step sequencer (it follows your DAW's tempo and position while the DAW plays)");
    playButton.onClick = [this] { edit ([] (MachineSettings& m) { m.playing = ! m.playing; }); };
    learnButton.setTooltip ("Then hit a key: the selected pad plays that note");
    learnButton.onClick = [this] { learning = ! learning; learnButton.setToggleState (learning, dontSendNotification); };
    clearButton.setTooltip ("Clear the selected pad's steps in this variation");
    clearButton.onClick = [this] { const int pd = selected; edit ([pd] (MachineSettings& m) { m.steps[(size_t) m.variation][(size_t) pd] = {}; }); };
    copyButton.setTooltip ("Copy this whole variation (every pad) to the next one");
    copyButton.onClick = [this] { edit ([] (MachineSettings& m) { if (m.variation < MachineSettings::numVariations - 1) { m.steps[(size_t) m.variation + 1] = m.steps[(size_t) m.variation]; ++m.variation; } }); };
    for (int i = 0; i < MachineSettings::numVariations; ++i)
    {
        variations[i].setButtonText (String::charToString ((juce_wchar) ('A' + i)));
        variations[i].setTooltip ("Variation " + variations[i].getButtonText() + " (each has its own steps for every pad)");
        variations[i].onClick = [this, i] { edit ([i] (MachineSettings& m) { m.variation = i; }); };
        addAndMakeVisible (variations[i]);
    }
    choke.addItem ("NO CHOKE", 1);
    for (int g = 1; g <= 8; ++g)
        choke.addItem ("CHOKE GROUP " + String (g), g + 1);
    choke.setTooltip ("Pads in the same group cut each other off (a closed hi-hat stops the open one)");
    choke.onChange = [this] { const int g = choke.getSelectedId() - 1; if (g >= 0) editPad ([g] (PadSettings& pd) { pd.choke = g; }); };
    for (int d : { 8, 16, 32 })
        stepLength.addItem ("1/" + String (d), d);
    stepLength.setTooltip ("How long one step is");
    stepLength.onChange = [this] { const int d = stepLength.getSelectedId(); if (d > 0) edit ([d] (MachineSettings& m) { m.den = d; }); };
    nameLabel.setTooltip ("The pad's name - click to rename");
    nameLabel.onTextChange = [this] { const auto t = nameLabel.getText().trim().toUpperCase(); editPad ([t] (PadSettings& pd) { pd.name = t; }); };

    noteBox = std::make_unique<ValueBox> ("NOTE");
    noteBox->setRange (0, 127, 1);
    noteBox->format = [] (double v) { return noteName (roundToInt (v)); };
    noteBox->onChange = [this] (double v) { const int n = roundToInt (v); editPad ([n] (PadSettings& pd) { pd.note = n; }); };
    noteBox->setTooltip ("The note of the preset this pad plays (drag, type, or use LEARN)");
    lengthBox = std::make_unique<ValueBox> ("LENGTH");
    lengthBox->setRange (1, 16, 1);
    lengthBox->format = [] (double v) { return String (roundToInt (v)) + " STEPS"; };
    lengthBox->onChange = [this] (double v) { const int l = roundToInt (v); edit ([l] (MachineSettings& m) { m.length = l; }); };
    swingBox = std::make_unique<ValueBox> ("SWING");
    swingBox->setRange (0, 100, 1);
    swingBox->format = [] (double v) { return String (roundToInt (v)) + "%"; };
    swingBox->onChange = [this] (double v) { const float s = (float) (v / 100.0); edit ([s] (MachineSettings& m) { m.swing = s; }); };

    level.setRange ({ 0.0, 1.0, 0.01 }, 1.0);
    level.format = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    level.onChange = [this] (double v) { editPad ([v] (PadSettings& pd) { pd.level = (float) v; }); };
    pan.setRange ({ -1.0, 1.0, 0.01 }, 0.0);
    pan.format = [] (double v) { const int x = roundToInt (v * 100.0); return x == 0 ? String ("C") : (x < 0 ? "L" + String (-x) : "R" + String (x)); };
    pan.onChange = [this] (double v) { editPad ([v] (PadSettings& pd) { pd.pan = (float) v; }); };
    tune.setRange ({ -24.0, 24.0, 0.1 }, 0.0);
    tune.format = [] (double v) { return (v > 0 ? "+" : "") + String (v, 1) + " ST"; };
    tune.onChange = [this] (double v) { editPad ([v] (PadSettings& pd) { pd.tune = (float) v; }); };
    decay.setRange ({ 0.0, 1.0, 0.01 }, 1.0);
    decay.format = [] (double v) { return v >= 0.995 ? String ("FULL") : String (roundToInt (v * 100.0)) + "%"; };
    decay.onChange = [this] (double v) { editPad ([v] (PadSettings& pd) { pd.decay = (float) v; }); };
    decay.slider.setTooltip ("Full = the sound as it is; lower = it fades out sooner");

    for (Component* c : { (Component*) &grid4, (Component*) &grid8, (Component*) &playButton, (Component*) &learnButton, (Component*) &clearButton,
                          (Component*) &copyButton, (Component*) &choke, (Component*) &stepLength, (Component*) &nameLabel, (Component*) noteBox.get(),
                          (Component*) lengthBox.get(), (Component*) swingBox.get(), (Component*) &level, (Component*) &pan, (Component*) &tune, (Component*) &decay })
        addAndMakeVisible (c);

    proc.addChangeListener (this);
    refresh();
}

MachinePage::~MachinePage() { proc.removeChangeListener (this); }

void MachinePage::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void MachinePage::edit (std::function<void (MachineSettings&)> f)
{
    auto m = proc.getMachineSettings();
    f (m);
    proc.setMachineSettings (m);
}

void MachinePage::editPad (std::function<void (PadSettings&)> f)
{
    const int pd = selected;
    edit ([pd, f] (MachineSettings& m) { f (m.pads[(size_t) pd]); });
}

void MachinePage::refresh()
{
    const auto& m = proc.getMachineSettings();
    selected = jlimit (0, m.padCount() - 1, selected);
    const auto& pd = m.pads[(size_t) selected];
    grid4.setToggleState (m.grid == 4, dontSendNotification);
    grid8.setToggleState (m.grid == 8, dontSendNotification);
    playButton.setToggleState (m.playing, dontSendNotification);
    playButton.setButtonText (m.playing ? "STOP" : "PLAY");
    for (int i = 0; i < MachineSettings::numVariations; ++i)
        variations[i].setToggleState (m.variation == i, dontSendNotification);
    choke.setSelectedId (pd.choke + 1, dontSendNotification);
    stepLength.setSelectedId (m.den, dontSendNotification);
    if (! nameLabel.isBeingEdited())
        nameLabel.setText (pd.name, dontSendNotification);
    noteBox->setValue (pd.note);
    lengthBox->setValue (m.length);
    swingBox->setValue (std::round (m.swing * 100.0f));
    level.setValue (pd.level);
    pan.setValue (pd.pan);
    tune.setValue (pd.tune);
    decay.setValue (pd.decay);
    repaint();
}

void MachinePage::tick()
{
    std::bitset<128> now;
    for (int n = 0; n < 128; ++n)
        now[(size_t) n] = proc.keyboardState.isNoteOnForChannels (0xffff, n);
    if (learning)
        for (int n = 0; n < 128; ++n)
            if (now[(size_t) n] && ! held[(size_t) n])
            {
                editPad ([n] (PadSettings& pd) { pd.note = n; });
                learning = false;
                learnButton.setToggleState (false, dontSendNotification);
                break;
            }
    const int step = proc.getMachineStep();
    if (now != held || step != lastStep)
    {
        // pads light up when their step plays
        flash.reset();
        if (step >= 0 && step != lastStep)
        {
            const auto& m = proc.getMachineSettings();
            for (int i = 0; i < m.padCount(); ++i)
                if (m.steps[(size_t) m.variation][(size_t) i][(size_t) step] > 0)
                    flash.set ((size_t) i);
        }
        held = now;
        lastStep = step;
        repaint();
    }
}

void MachinePage::resized()
{
    auto r = getLocalBounds();
    seqArea = r.removeFromBottom (150).toFloat();
    r.removeFromBottom (8);
    padsArea = r.removeFromLeft (jmin (r.getHeight() + 40, 420)).toFloat();
    r.removeFromLeft (8);
    padPanel = r.toFloat();

    {
        auto a = padsArea.toNearestInt().reduced (11, 9);
        auto head = a.removeFromTop (22);
        grid8.setBounds (head.removeFromRight (56).reduced (0, 1));
        head.removeFromRight (3);
        grid4.setBounds (head.removeFromRight (56).reduced (0, 1));
        a.removeFromTop (6);
        const float side = (float) jmin (a.getWidth(), a.getHeight());
        padGrid = a.toFloat().withSizeKeepingCentre (side, side);
    }
    {
        auto a = padPanel.toNearestInt().reduced (11, 9);
        a.removeFromTop (22 + 6);
        padInset = a.toFloat();
        auto in = a.reduced (12, 10);
        auto row = in.removeFromTop (24);
        labels[0] = row.removeFromLeft (52).toFloat();
        nameLabel.setBounds (row.removeFromLeft (jmin (row.getWidth(), 220)));
        in.removeFromTop (8);
        row = in.removeFromTop (ValueBox::labelledHeight);
        noteBox->setBounds (row.removeFromLeft (100));
        row.removeFromLeft (8);
        learnButton.setBounds (row.removeFromLeft (70).removeFromBottom (22));
        row.removeFromLeft (16);
        choke.setBounds (row.removeFromLeft (jmin (row.getWidth(), 160)).removeFromBottom (22));
        in.removeFromTop (10);
        const int kh = level.getPreferredHeight();
        auto knobs = in.removeFromTop (kh);
        const int step = jmin (90, knobs.getWidth() / 4);
        for (auto* k : { &level, &pan, &tune, &decay })
            k->setBounds (knobs.removeFromLeft (step).withSizeKeepingCentre (64, kh));
        labels[1] = in.removeFromTop (30).toFloat();
    }
    {
        auto a = seqArea.toNearestInt().reduced (11, 9);
        auto head = a.removeFromTop (22);
        head.removeFromLeft (110);
        playButton.setBounds (head.removeFromLeft (64).reduced (0, 1));
        head.removeFromLeft (14);
        labels[2] = head.removeFromLeft (64).toFloat();
        for (auto& v : variations)
        {
            v.setBounds (head.removeFromLeft (26).reduced (0, 1));
            head.removeFromLeft (2);
        }
        head.removeFromLeft (8);
        copyButton.setBounds (head.removeFromLeft (104).reduced (0, 1));
        head.removeFromLeft (4);
        clearButton.setBounds (head.removeFromLeft (60).reduced (0, 1));
        stepLength.setBounds (head.removeFromRight (70).reduced (0, 1));
        head.removeFromRight (4);
        labels[3] = head.removeFromRight (38).toFloat();
        a.removeFromTop (8);
        auto side = a.removeFromRight (190);
        a.removeFromRight (8);
        stepsArea = a.toFloat();
        auto sb = side.withTrimmedTop (4);
        lengthBox->setBounds (sb.removeFromTop (ValueBox::labelledHeight).removeFromLeft (90));
        sb = side.withTrimmedTop (4);
        swingBox->setBounds (sb.removeFromTop (ValueBox::labelledHeight).removeFromRight (90));
        labels[4] = side.withTrimmedTop (4 + ValueBox::labelledHeight + 6).toFloat();
    }
}

Rectangle<float> MachinePage::padRect (int i) const
{
    const int g = proc.getMachineSettings().grid;
    const float gap = g == 4 ? 7.0f : 3.0f;
    const float cell = (padGrid.getWidth() - gap * (float) (g - 1)) / (float) g;
    const int row = g - 1 - i / g, col = i % g;   // pad 1 bottom left, like hardware
    return { padGrid.getX() + (float) col * (cell + gap), padGrid.getY() + (float) row * (cell + gap), cell, cell };
}

Rectangle<float> MachinePage::stepRect (int s) const
{
    const float gap = 4.0f, w = (stepsArea.getWidth() - gap * 15.0f) / 16.0f;
    return { stepsArea.getX() + (float) s * (w + gap), stepsArea.getY(), w, stepsArea.getHeight() };
}

int MachinePage::padAt (Point<float> p) const
{
    for (int i = 0; i < proc.getMachineSettings().padCount(); ++i)
        if (padRect (i).contains (p))
            return i;
    return -1;
}

int MachinePage::stepAt (Point<float> p) const
{
    if (! stepsArea.contains (p))
        return -1;
    for (int s = 0; s < 16; ++s)
        if (stepRect (s).expanded (2.0f, 0.0f).contains (p))
            return s;
    return -1;
}

void MachinePage::mouseDown (const MouseEvent& e)
{
    if (const int pd = padAt (e.position); pd >= 0)
    {
        selected = pd;
        playingPad = pd;
        const auto& m = proc.getMachineSettings();
        const auto r = padRect (pd);
        const float vel = jlimit (0.15f, 1.0f, 0.35f + 0.65f * (e.position.y - r.getY()) / r.getHeight());   // lower = harder, like the keys
        proc.keyboardState.noteOn (proc.getKeyboardChannel(), m.pads[(size_t) pd].note, vel);
        refresh();
        return;
    }
    if (const int s = stepAt (e.position); s >= 0)
    {
        const auto& m = proc.getMachineSettings();
        const int cur = m.steps[(size_t) m.variation][(size_t) selected][(size_t) s];
        stepPaintOn = cur == 0;
        dragStep = s;
        const int pd = selected;
        const int vel = stepPaintOn ? jlimit (1, 127, roundToInt (127.0f * (1.0f - (e.position.y - stepsArea.getY()) / stepsArea.getHeight()) + 10.0f)) : 0;
        edit ([pd, s, vel] (MachineSettings& mm) { mm.steps[(size_t) mm.variation][(size_t) pd][(size_t) s] = (juce::uint8) vel; });
    }
}

void MachinePage::mouseDrag (const MouseEvent& e)
{
    if (dragStep < 0)
        return;
    const int s = stepAt ({ e.position.x, stepsArea.getCentreY() });
    if (s < 0)
        return;
    const int pd = selected;
    // dragging up / down on a lit step sets its velocity; across paints steps on or off
    const int vel = stepPaintOn ? jlimit (1, 127, roundToInt (127.0f * (1.0f - (e.position.y - stepsArea.getY()) / stepsArea.getHeight()) + 10.0f)) : 0;
    edit ([pd, s, vel] (MachineSettings& mm) { mm.steps[(size_t) mm.variation][(size_t) pd][(size_t) s] = (juce::uint8) vel; });
}

void MachinePage::mouseUp (const MouseEvent&)
{
    if (playingPad >= 0)
        proc.keyboardState.noteOff (proc.getKeyboardChannel(), proc.getMachineSettings().pads[(size_t) playingPad].note, 0.0f);
    playingPad = -1;
    dragStep = -1;
}

void MachinePage::paint (Graphics& g)
{
    const auto& m = proc.getMachineSettings();

    // Pads
    theme::drawPanel (g, padsArea.reduced (1.0f));
    theme::drawHeading (g, padsArea.reduced (11.0f, 9.0f), "Pads", "click to play - lower = harder");
    theme::drawInset (g, padGrid.expanded (6.0f));
    for (int i = 0; i < m.padCount(); ++i)
    {
        const auto r = padRect (i);
        const auto& pd = m.pads[(size_t) i];
        const bool sel = i == selected, lit = held[(size_t) pd.note] || flash[(size_t) i];
        Colour top = Colour (0xff45454c), bottom = Colour (0xff232327);
        if (lit)      { top = theme::hue (0xffff5a5a); bottom = theme::hue (0xff9a1018); }
        else if (sel) { top = theme::hue (0xffd8222c); bottom = theme::hue (0xff7e101a); }
        if (theme::isFlat())
        {
            const auto& f = theme::flat();
            top = bottom = lit ? f.accent.brighter (0.3f) : sel ? f.accent : f.button;
        }
        g.setGradientFill (ColourGradient::vertical (top, r.getY(), bottom, r.getBottom()));
        g.fillRoundedRectangle (r, m.grid == 4 ? 6.0f : 3.0f);
        g.setColour (sel ? Colours::white.withAlpha (0.9f) : Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), m.grid == 4 ? 6.0f : 3.0f, sel ? 1.6f : 1.0f);
        if (m.grid == 4)
        {
            auto t = r.reduced (7.0f, 6.0f);
            g.setColour (theme::textOn (bottom));
            g.setFont (theme::font (9.0f, 800, 0.08f));
            g.drawText (pd.name, t.removeFromTop (12.0f), Justification::topLeft, true);
            g.setColour (theme::textOn (bottom).withAlpha (0.7f));
            g.setFont (theme::font (8.0f, 600, 0.06f));
            g.drawText (noteName (pd.note), t.removeFromBottom (11.0f), Justification::bottomLeft, false);
            if (pd.choke > 0)
                g.drawText ("C" + String (pd.choke), Rectangle<float> (r.getRight() - 30.0f, r.getBottom() - 17.0f, 23.0f, 11.0f), Justification::bottomRight, false);
        }
        else
        {
            g.setColour (theme::textOn (bottom).withAlpha (0.8f));
            g.setFont (theme::font (6.5f, 700, 0.02f));
            g.drawText (noteName (pd.note), r.reduced (2.0f), Justification::centred, false);
        }
    }

    // Selected pad
    theme::drawPanel (g, padPanel.reduced (1.0f));
    theme::drawHeading (g, padPanel.reduced (11.0f, 9.0f), "Pad " + String (selected + 1), "what this pad plays and how");
    theme::drawInset (g, padInset);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.5f, 700, 0.12f));
    g.drawText ("NAME", labels[0], Justification::centredLeft, false);
    g.setFont (theme::font (8.0f, 500, 0.04f));
    g.drawFittedText ("Each pad plays a note of the preset (its zone), with its own level, pan, tune and decay. "
                      "Drum kits from Emulator X banks map one sound per note - point the pads at them.",
                      labels[1].toNearestInt(), Justification::topLeft, 2, 1.0f);

    // Sequencer
    theme::drawPanel (g, seqArea.reduced (1.0f));
    theme::drawHeading (g, seqArea.reduced (11.0f, 9.0f), "Sequencer");
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.5f, 700, 0.12f));
    g.drawText ("VARIATION", labels[2], Justification::centredLeft, false);
    g.drawText ("STEP", labels[3], Justification::centredRight, false);
    theme::drawScreen (g, stepsArea.expanded (4.0f, 4.0f));
    const int len = m.length, cur = proc.getMachineStep();
    for (int s = 0; s < 16; ++s)
    {
        const auto r = stepRect (s);
        const int vel = m.steps[(size_t) m.variation][(size_t) selected][(size_t) s];
        g.setColour (s < len ? Colour (0xff1e1e23) : Colour (0xff121214));
        g.fillRoundedRectangle (r, 3.0f);
        if (vel > 0)
        {
            auto bar = r.withTrimmedTop (r.getHeight() * (1.0f - (float) vel / 127.0f));
            g.setGradientFill (ColourGradient::vertical (theme::hue (s == cur ? 0xffff9090 : 0xffff3a3a), bar.getY(), theme::hue (0xff8a1018), bar.getBottom()));
            g.fillRoundedRectangle (bar, 3.0f);
        }
        g.setColour (s == cur ? Colours::white.withAlpha (0.9f) : (s % 4 == 0 ? Colours::white.withAlpha (0.25f) : Colours::black.withAlpha (0.5f)));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, s == cur ? 1.5f : 1.0f);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (7.0f, 700, 0.05f));
        g.drawText (String (s + 1), r.withTrimmedTop (r.getHeight() - 12.0f), Justification::centred, false);
    }
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 500, 0.04f));
    g.drawFittedText ("Click a step for pad " + String (selected + 1) + ", drag across to paint, up / down sets how hard.",
                      labels[4].toNearestInt(), Justification::topLeft, 3, 1.0f);
}
} // namespace emx
