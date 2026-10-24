// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ArpPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

static String patternLetter (int p) { return String::charToString ((juce_wchar) ('A' + jlimit (0, 7, p))); }

//==================================================================================
// Arrangement: which pattern each slot plays, in a loop
//==================================================================================
class ArrangeBar final : public Component, public SettableTooltipClient
{
public:
    explicit ArrangeBar (ArpPage& p) : page (p)
    {
        setTooltip ("Click a slot to choose its pattern - click + to add a slot - right-click removes a slot");
    }

    void tick()
    {
        const int s = page.getProcessor().getArp().currentSlot.load();
        if (s != shownSlot) { shownSlot = s; repaint(); }
    }

    void paint (Graphics& g) override
    {
        const auto& a = page.getProcessor().getArpSettings();
        const float w = (float) getWidth() / (float) ArpSettings::maxSlots;
        for (int i = 0; i < ArpSettings::maxSlots; ++i)
        {
            auto r = Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (2.0f, 1.0f);
            if (i < a.arrangementLength)
            {
                const int pat = a.arrangement[(size_t) i];
                const bool playing = i == shownSlot && a.enabled;
                const bool editing = pat == page.getEditPattern();
                if (playing)
                    DropShadow (theme::red.withAlpha (0.8f), 8, {}).drawForRectangle (g, r.toNearestInt());
                g.setGradientFill (ColourGradient::vertical (playing ? theme::hue (0xffff4a4a) : editing ? theme::hue (0xff9a1a1a) : theme::hue (0xff5a1010), r.getY(),
                                                             playing ? theme::hue (0xffa50f0f) : theme::hue (0xff2a0606), r.getBottom()));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (Colours::black);
                g.drawRoundedRectangle (r, 4.0f, 1.0f);
                g.setColour (theme::textOn (playing ? theme::hue (0xffd02c2c) : editing ? theme::hue (0xff9a1a1a) : theme::hue (0xff5a1010)));
                g.setFont (theme::font (10.0f, 900, 0.06f));
                g.drawText (patternLetter (pat), r, Justification::centred, false);
            }
            else
            {
                theme::drawLed (g, r, i == a.arrangementLength ? "+" : "", 10.0f, Justification::centred, theme::textMuted);
            }
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        auto& proc = page.getProcessor();
        const auto& a = proc.getArpSettings();
        const int i = (int) (e.position.x / ((float) getWidth() / (float) ArpSettings::maxSlots));
        if (i < 0 || i >= ArpSettings::maxSlots)
            return;
        if (i >= a.arrangementLength)
        {
            if (i == a.arrangementLength && ! e.mods.isPopupMenu())
            {
                const int pat = page.getEditPattern();
                page.edit ([pat] (ArpSettings& s) { s.arrangement[(size_t) s.arrangementLength] = pat; ++s.arrangementLength; });
            }
            return;
        }
        if (e.mods.isPopupMenu())
        {
            if (a.arrangementLength > 1)
                page.edit ([i] (ArpSettings& s) { removeSlot (s, i); });
            return;
        }
        PopupMenu m;
        m.addSectionHeader ("Slot " + String (i + 1));
        for (int p = 0; p < ArpSettings::numPatterns; ++p)
            m.addItem ("Play pattern " + patternLetter (p), true, a.arrangement[(size_t) i] == p,
                       [this, i, p] { page.edit ([i, p] (ArpSettings& s) { s.arrangement[(size_t) i] = p; }); });
        m.addSeparator();
        m.addItem ("Edit pattern " + patternLetter (a.arrangement[(size_t) i]), [this, p = a.arrangement[(size_t) i]] { page.setEditPattern (p); });
        m.addItem ("Insert a slot after this one", a.arrangementLength < ArpSettings::maxSlots, false, [this, i]
        {
            page.edit ([i] (ArpSettings& s)
            {
                for (int k = s.arrangementLength; k > i + 1; --k)
                    s.arrangement[(size_t) k] = s.arrangement[(size_t) k - 1];
                s.arrangement[(size_t) i + 1] = s.arrangement[(size_t) i];
                ++s.arrangementLength;
            });
        });
        m.addItem ("Remove this slot", a.arrangementLength > 1, false, [this, i] { page.edit ([i] (ArpSettings& s) { removeSlot (s, i); }); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
    }

private:
    static void removeSlot (ArpSettings& s, int i)
    {
        for (int k = i; k < s.arrangementLength - 1; ++k)
            s.arrangement[(size_t) k] = s.arrangement[(size_t) k + 1];
        s.arrangementLength = jmax (1, s.arrangementLength - 1);
    }

    ArpPage& page;
    int shownSlot = -1;
};

//==================================================================================
// 16 steps of the pattern being edited: velocity bars, on/off, transpose (semitones)
//==================================================================================
class StepGrid final : public Component
{
public:
    explicit StepGrid (ArpPage& p) : page (p) {}

    void tick()
    {
        auto& arp = page.getProcessor().getArp();
        const int s = arp.currentPattern.load() == page.getEditPattern() ? arp.currentStep.load() : -1;
        if (s != shownStep) { shownStep = s; repaint(); }
    }

    void paint (Graphics& g) override
    {
        const auto& a = page.getProcessor().getArpSettings();
        const auto& pat = a.patterns[(size_t) page.getEditPattern()];
        const float colW = (float) getWidth() / 16.0f;
        const auto bars = barsArea(), nums = numbersArea(), notes = transposeArea();
        theme::drawScreen (g, bars);
        g.setColour (theme::red.withAlpha (0.08f));
        for (int i = 1; i < 4; ++i)
            g.fillRect (bars.getX(), bars.getY() + bars.getHeight() * (float) i / 4.0f, bars.getWidth(), 1.0f);

        for (int i = 0; i < 16; ++i)
        {
            const auto& st = pat.steps[(size_t) i];
            const bool inUse = i < pat.length;
            const bool playing = i == shownStep && a.enabled;
            auto col = Rectangle<float> ((float) i * colW, bars.getY(), colW, bars.getHeight()).reduced (4.0f, 6.0f);
            if (playing)
            {
                g.setColour (theme::red.withAlpha (0.18f));
                g.fillRoundedRectangle (col.expanded (2.0f, 4.0f), 4.0f);
            }
            const float h = col.getHeight() * st.velocity;
            auto bar = col.withTop (col.getBottom() - h);
            const float alpha = ! inUse ? 0.18f : st.on ? 1.0f : 0.3f;
            if (h > 0.5f)
            {
                g.setGradientFill (ColourGradient::vertical (theme::hue (0xffff5050).withAlpha (alpha), bar.getY(),
                                                             theme::hue (0xff7a0a0a).withAlpha (alpha), bar.getBottom()));
                g.fillRoundedRectangle (bar, 2.5f);
                if (st.on && inUse)
                {
                    g.setColour (Colours::white.withAlpha (playing ? 1.0f : 0.7f));
                    g.fillRect (bar.withHeight (2.0f));
                }
            }

            auto nb = Rectangle<float> ((float) i * colW, nums.getY(), colW, nums.getHeight()).reduced (3.0f, 1.0f);
            const bool lit = st.on && inUse;
            g.setColour (lit ? (playing ? theme::hue (0xffff3030) : theme::hue (0xff8a1818)) : theme::hue (0xff2a2629));
            g.fillRoundedRectangle (nb, 4.0f);
            g.setColour (Colours::black);
            g.drawRoundedRectangle (nb, 4.0f, 1.0f);
            const auto numFill = lit ? (playing ? theme::hue (0xffff3030) : theme::hue (0xff8a1818)) : theme::hue (0xff2a2629);
            g.setColour (inUse ? theme::textOn (numFill).withAlpha (lit ? 1.0f : 0.6f) : theme::textMuted.withAlpha (0.55f));
            g.setFont (theme::font (8.5f, 700, 0.04f));
            g.drawText (String (i + 1), nb, Justification::centred, false);

            auto tb = Rectangle<float> ((float) i * colW, notes.getY(), colW, notes.getHeight()).reduced (3.0f, 1.0f);
            const int t = st.transpose;
            theme::drawLed (g, tb, t == 0 ? String ("0") : (t > 0 ? "+" : "") + String (t), 8.0f, Justification::centred,
                            inUse ? (t == 0 ? theme::textMuted : theme::red) : theme::textMuted.withAlpha (0.45f));
        }

        const float lx = (float) pat.length * colW;
        if (pat.length < 16)
        {
            g.setColour (theme::isXp() || theme::isLightSkin() ? theme::windowBg.withAlpha (0.6f) : Colours::black.withAlpha (0.45f));
            g.fillRect (Rectangle<float> (lx, bars.getY(), (float) getWidth() - lx, bars.getHeight()).reduced (0.0f, 2.0f));
            g.setColour (theme::red);
            g.fillRect (lx - 1.0f, bars.getY() + 2.0f, 2.0f, bars.getHeight() - 4.0f);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        mode = Mode::none;
        const int i = stepAt (e.position.x);
        if (i < 0)
            return;
        const int pat = page.getEditPattern();
        if (barsArea().contains (e.position))
        {
            mode = Mode::velocity;
            setVelocity (i, e.position.y);
        }
        else if (numbersArea().contains (e.position))
            page.edit ([pat, i] (ArpSettings& a) { auto& s = a.patterns[(size_t) pat].steps[(size_t) i]; s.on = ! s.on; });
        else if (transposeArea().contains (e.position))
        {
            mode = Mode::transpose;
            dragStep = i;
            dragStart = page.getProcessor().getArpSettings().patterns[(size_t) pat].steps[(size_t) i].transpose;
            moved = false;
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (mode == Mode::velocity)
        {
            const int i = stepAt (e.position.x);
            if (i >= 0)
                setVelocity (i, e.position.y);
        }
        else if (mode == Mode::transpose && std::abs (e.getDistanceFromDragStartY()) > 2)
        {
            moved = true;
            setTranspose (dragStep, dragStart - e.getDistanceFromDragStartY() / 6);   // 6 px per semitone
        }
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (mode == Mode::transpose && ! moved)
            setTranspose (dragStep, dragStart + (e.mods.isPopupMenu() ? -1 : 1));   // click +1, right-click -1
        mode = Mode::none;
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (transposeArea().contains (e.position))
            if (const int i = stepAt (e.position.x); i >= 0)
                setTranspose (i, 0);
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (! transposeArea().contains (e.position))
            return;
        if (const int i = stepAt (e.position.x); i >= 0)
        {
            const int cur = page.getProcessor().getArpSettings().patterns[(size_t) page.getEditPattern()].steps[(size_t) i].transpose;
            setTranspose (i, cur + (w.deltaY > 0 ? 1 : -1));
        }
    }

private:
    enum class Mode { none, velocity, transpose };
    Rectangle<float> barsArea() const      { return getLocalBounds().toFloat().withTrimmedBottom (62.0f); }
    Rectangle<float> numbersArea() const   { return getLocalBounds().toFloat().withTop ((float) getHeight() - 56.0f).withHeight (26.0f); }
    Rectangle<float> transposeArea() const { return getLocalBounds().toFloat().withTop ((float) getHeight() - 24.0f); }
    int stepAt (float x) const { const int i = (int) (x / ((float) getWidth() / 16.0f)); return i >= 0 && i < 16 ? i : -1; }

    void setVelocity (int i, float y)
    {
        const auto b = barsArea().reduced (0.0f, 6.0f);
        const float v = jlimit (0.0f, 1.0f, (b.getBottom() - y) / b.getHeight());
        const int pat = page.getEditPattern();
        page.edit ([pat, i, v] (ArpSettings& a)
        {
            auto& s = a.patterns[(size_t) pat].steps[(size_t) i];
            s.velocity = v;
            if (v > 0.02f) s.on = true;
        });
    }

    void setTranspose (int i, int semis)
    {
        const int pat = page.getEditPattern();
        const int v = jlimit (-24, 24, semis);
        page.edit ([pat, i, v] (ArpSettings& a) { a.patterns[(size_t) pat].steps[(size_t) i].transpose = v; });
    }

    ArpPage& page;
    int shownStep = -1, dragStep = 0, dragStart = 0;
    bool moved = false;
    Mode mode = Mode::none;
};

//==================================================================================
// ArpPage
//==================================================================================
ArpPage::ArpPage (EmulonXProcessor& p)
    : proc (p),
      rateLed ([this]
      {
          const auto& a = proc.getArpSettings();
          const double secs = a.stepBeats() * 60.0 / jmax (1.0f, proc.getFxEngine().bpmUsed.load());
          return String (a.num) + "/" + String (a.den) + (a.dot == 1 ? " DOT" : a.dot == 2 ? " TRI" : "") + "  "
                 + (secs < 1.0 ? String (roundToInt (secs * 1000.0)) + " MS" : String (secs, 2) + " S");
      }, 8.5f),
      heldLed ([this]
      {
          String t;
          for (int n = 0; n < 128; ++n)
              if ((proc.getArp().heldMask[n >> 6].load() >> (n & 63)) & 1)
                  t << noteName (n) << "  ";
          const int pat = proc.getArp().currentPattern.load();
          String playing = pat >= 0 && proc.getArpSettings().enabled ? "   |   PATTERN " + patternLetter (pat) : String();
          return (t.isEmpty() ? String ("HOLD SOME KEYS...") : "PLAYING FROM:  " + t) + playing;
      }, 9.0f)
{
    arrange = std::make_unique<ArrangeBar> (*this);
    grid = std::make_unique<StepGrid> (*this);

    onButton.setClickingTogglesState (false);
    onButton.setTooltip ("Switch the arpeggiator on / off");
    onButton.onClick = [this] { edit ([] (ArpSettings& a) { a.enabled = ! a.enabled; }); };
    latchButton.setTooltip ("Keep playing after you let go; the next chord replaces the old one");
    latchButton.onClick = [this] { edit ([] (ArpSettings& a) { a.latch = ! a.latch; }); };
    velPattern.setTooltip ("Velocity comes from the bars of the pattern");
    velPlayed.setTooltip ("Velocity is how hard you played the keys");
    velPattern.onClick = [this] { edit ([] (ArpSettings& a) { a.playedVelocity = false; }); };
    velPlayed.onClick = [this] { edit ([] (ArpSettings& a) { a.playedVelocity = true; }); };

    for (int m = 0; m < ArpSettings::numModes; ++m)
        modeBox.addItem (String (ArpSettings::modeName (m)).toUpperCase(), m + 1);
    modeBox.setTooltip ("In which order the held notes are played");
    modeBox.onChange = [this] { const int m = modeBox.getSelectedId() - 1; edit ([m] (ArpSettings& a) { a.mode = m; }); };

    for (int d : { 1, 2, 4, 8, 16, 32, 64 })
        denBox.addItem ("/ " + String (d), d);
    denBox.setTooltip ("Step length: 1/16 = sixteenth notes");
    denBox.onChange = [this] { const int d = denBox.getSelectedId(); edit ([d] (ArpSettings& a) { a.den = d; }); };
    const char* dotTips[] { "Straight", "Dotted (x1.5)", "Triplet (x2/3)" };
    for (int i = 0; i < 3; ++i)
    {
        dots[i].setTooltip (dotTips[i]);
        dots[i].onClick = [this, i] { edit ([i] (ArpSettings& a) { a.dot = i; }); };
    }

    beats.setRange (NormalisableRange<double> (1, 32, 1), 1);
    beats.format = [] (double v) { return String (roundToInt (v)); };
    beats.onChange = [this] (double v) { edit ([v] (ArpSettings& a) { a.num = roundToInt (v); }); };
    beats.slider.setTooltip ("How many of the chosen note lengths one step lasts");
    octaves.setRange (NormalisableRange<double> (1, 4, 1), 1);
    octaves.format = [] (double v) { return String (roundToInt (v)) + " OCT"; };
    octaves.onChange = [this] (double v) { edit ([v] (ArpSettings& a) { a.octaves = roundToInt (v); }); };
    octaves.slider.setTooltip ("Repeat the held notes over this many octaves");
    gate.setRange (NormalisableRange<double> (0.05, 1.0, 0.01), 0.75);
    gate.format = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    gate.onChange = [this] (double v) { edit ([v] (ArpSettings& a) { a.gate = (float) v; }); };
    gate.slider.setTooltip ("How long each note is held, as a part of the step");
    swing.setRange (NormalisableRange<double> (0.0, 1.0, 0.01), 0.0);
    swing.format = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    swing.onChange = [this] (double v) { edit ([v] (ArpSettings& a) { a.swing = (float) v; }); };
    swing.slider.setTooltip ("Pushes every second step later for a shuffled feel");
    length.setRange (NormalisableRange<double> (1, 16, 1), 8);
    length.format = [] (double v) { return String (roundToInt (v)); };
    length.onChange = [this] (double v) { const int pat = editPattern; edit ([v, pat] (ArpSettings& a) { a.patterns[(size_t) pat].length = roundToInt (v); }); };
    length.slider.setTooltip ("Length of this pattern in steps");

    for (int i = 0; i < ArpSettings::numPatterns; ++i)
    {
        auto* b = patternTabs.add (new TextButton (patternLetter (i)));
        b->setTooltip ("Edit pattern " + patternLetter (i) + " - put it in the arrangement above to play it");
        b->onClick = [this, i] { setEditPattern (i); };
        addAndMakeVisible (b);
    }
    copyButton.setTooltip ("Copy this pattern");
    copyButton.onClick = [this] { clipboard = proc.getArpSettings().patterns[(size_t) editPattern]; pasteButton.setEnabled (true); };
    pasteButton.setTooltip ("Replace this pattern with the copied one");
    pasteButton.setEnabled (false);
    pasteButton.onClick = [this]
    {
        if (! clipboard) return;
        const auto c = *clipboard;
        const int pat = editPattern;
        edit ([c, pat] (ArpSettings& a) { a.patterns[(size_t) pat] = c; });
    };

    auto forSteps = [this] (std::function<void (ArpStep&, size_t)> f)
    {
        const int pat = editPattern;
        edit ([f, pat] (ArpSettings& a) { auto& st = a.patterns[(size_t) pat].steps; for (size_t i = 0; i < st.size(); ++i) f (st[i], i); });
    };
    allOn.setTooltip ("Switch every step on");
    allOn.onClick = [forSteps] { forSteps ([] (ArpStep& s, size_t) { s.on = true; }); };
    accents.setTooltip ("Loud first step of every four, the rest softer");
    accents.onClick = [forSteps] { forSteps ([] (ArpStep& s, size_t i) { s.velocity = i % 4 == 0 ? 1.0f : 0.6f; }); };
    randomVel.setTooltip ("Random velocities");
    randomVel.onClick = [forSteps] { forSteps ([] (ArpStep& s, size_t) { s.velocity = 0.35f + 0.65f * Random::getSystemRandom().nextFloat(); }); };
    flat.setTooltip ("All steps the same velocity");
    flat.onClick = [forSteps] { forSteps ([] (ArpStep& s, size_t) { s.velocity = 0.8f; }); };
    clearNotes.setTooltip ("Reset every step's transpose to 0");
    clearNotes.onClick = [forSteps] { forSteps ([] (ArpStep& s, size_t) { s.transpose = 0; }); };

    for (Component* c : { (Component*) &onButton, (Component*) &latchButton, (Component*) &velPattern, (Component*) &velPlayed,
                          (Component*) &modeBox, (Component*) &denBox, (Component*) &dots[0], (Component*) &dots[1], (Component*) &dots[2],
                          (Component*) &beats, (Component*) &octaves, (Component*) &gate, (Component*) &swing, (Component*) &length,
                          (Component*) &rateLed, (Component*) &heldLed, (Component*) arrange.get(), (Component*) grid.get(),
                          (Component*) &copyButton, (Component*) &pasteButton, (Component*) &allOn, (Component*) &accents,
                          (Component*) &randomVel, (Component*) &flat, (Component*) &clearNotes })
        addAndMakeVisible (c);

    proc.addChangeListener (this);
    refresh();
}

ArpPage::~ArpPage() { proc.removeChangeListener (this); }

void ArpPage::changeListenerCallback (ChangeBroadcaster*) { if (isVisible()) refresh(); }

void ArpPage::edit (std::function<void (ArpSettings&)> f)
{
    auto a = proc.getArpSettings();
    f (a);
    proc.setArpSettings (a);
}

void ArpPage::setEditPattern (int p)
{
    editPattern = jlimit (0, ArpSettings::numPatterns - 1, p);
    refresh();
}

void ArpPage::refresh()
{
    const auto& a = proc.getArpSettings();
    onButton.setToggleState (a.enabled, dontSendNotification);
    onButton.setButtonText (a.enabled ? "ARP ON" : "ARP OFF");
    latchButton.setToggleState (a.latch, dontSendNotification);
    velPattern.setToggleState (! a.playedVelocity, dontSendNotification);
    velPlayed.setToggleState (a.playedVelocity, dontSendNotification);
    modeBox.setSelectedId (a.mode + 1, dontSendNotification);
    denBox.setSelectedId (a.den, dontSendNotification);
    for (int i = 0; i < 3; ++i)
        dots[i].setToggleState (a.dot == i, dontSendNotification);
    beats.setValue (a.num);
    octaves.setValue (a.octaves);
    gate.setValue (a.gate);
    swing.setValue (a.swing);
    length.setValue (a.patterns[(size_t) editPattern].length);
    for (int i = 0; i < patternTabs.size(); ++i)
        patternTabs[i]->setToggleState (i == editPattern, dontSendNotification);
    rateLed.refresh();
    arrange->repaint();
    grid->repaint();
    repaint (patternArea.toNearestInt());
}

void ArpPage::tick()
{
    grid->tick();
    arrange->tick();
    heldLed.refresh();
    rateLed.refresh();
    const int playing = proc.getArpSettings().enabled ? proc.getArp().currentPattern.load() : -1;
    if (playing != shownPlayingPattern)
    {
        shownPlayingPattern = playing;
        repaint (patternArea.toNearestInt());
    }
}

void ArpPage::resized()
{
    auto r = getLocalBounds();
    topArea = r.removeFromTop (150).toFloat();
    r.removeFromTop (8);
    bottomArea = r.removeFromBottom (60).toFloat();
    r.removeFromBottom (8);
    patternArea = r.toFloat();

    const int kh = beats.getPreferredHeight();
    auto t = topArea.toNearestInt().reduced (11, 9);
    t.removeFromTop (26);
    t = t.reduced (6, 4);

    onButton.setBounds (t.removeFromLeft (96).withSizeKeepingCentre (96, 40));
    t.removeFromLeft (18);

    auto c1 = t.removeFromLeft (170);
    modeLabel = c1.removeFromTop (14).toFloat();
    modeBox.setBounds (c1.removeFromTop (24));
    c1.removeFromTop (10);
    velLabel = c1.removeFromTop (14).toFloat();
    auto vr = c1.removeFromTop (24);
    velPattern.setBounds (vr.removeFromLeft (80));
    vr.removeFromLeft (6);
    velPlayed.setBounds (vr);
    t.removeFromLeft (18);

    beats.setBounds (t.removeFromLeft (62).withSizeKeepingCentre (62, kh));
    auto c2 = t.removeFromLeft (104);
    rateLabel = c2.removeFromTop (14).toFloat();
    denBox.setBounds (c2.removeFromTop (24));
    c2.removeFromTop (6);
    auto dr = c2.removeFromTop (22);
    for (auto& d : dots)
    {
        d.setBounds (dr.removeFromLeft (33));
        dr.removeFromLeft (2);
    }
    c2.removeFromTop (6);
    rateLed.setBounds (c2.removeFromTop (20));
    t.removeFromLeft (14);

    for (auto* k : { &octaves, &gate, &swing })
        k->setBounds (t.removeFromLeft (66).withSizeKeepingCentre (62, kh));
    t.removeFromLeft (10);
    latchButton.setBounds (t.removeFromLeft (80).withSizeKeepingCentre (80, 32));

    // Pattern panel: arrangement row, then [pattern tabs | grid], then tools.
    auto pa = patternArea.toNearestInt().reduced (11, 9);
    pa.removeFromTop (26);
    auto arrRow = pa.removeFromTop (26);
    arrangeLabel = arrRow.removeFromLeft (84).toFloat();
    arrange->setBounds (arrRow);
    pa.removeFromTop (10);
    auto tools = pa.removeFromBottom (26);
    pa.removeFromBottom (8);

    auto side = pa.removeFromLeft (76);
    patternLabel = side.removeFromTop (14).toFloat();
    auto tabs = side.removeFromTop (4 * 26);
    for (int i = 0; i < patternTabs.size(); ++i)
        patternTabs[i]->setBounds (tabs.getX() + (i % 2) * 38, tabs.getY() + (i / 2) * 26, 35, 23);
    side.removeFromTop (6);
    length.setBounds (side.removeFromTop (kh).withSizeKeepingCentre (62, kh));
    side.removeFromTop (6);
    copyButton.setBounds (side.removeFromTop (22).withWidth (73));
    side.removeFromTop (4);
    pasteButton.setBounds (side.removeFromTop (22).withWidth (73));
    pa.removeFromLeft (8);
    grid->setBounds (pa);

    for (auto* b : { &allOn, &accents, &randomVel, &flat, &clearNotes })
    {
        b->setBounds (tools.removeFromLeft (b == &clearNotes ? 90 : 82));
        tools.removeFromLeft (6);
    }

    auto bt = bottomArea.toNearestInt().reduced (11, 9);
    bt.removeFromTop (20);
    heldLed.setBounds (bt.removeFromTop (22));
}

void ArpPage::paint (Graphics& g)
{
    theme::drawPanel (g, topArea.reduced (1.0f));
    theme::drawInset (g, theme::drawHeading (g, topArea.reduced (10.0f, 9.0f), "Arpeggiator",
                                             "plays the keys you hold one after another - follows your DAW's tempo and beat"));
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 700, 0.1f));
    g.drawText ("MODE", modeLabel, Justification::bottomLeft, false);
    g.drawText ("VELOCITY", velLabel, Justification::bottomLeft, false);
    g.drawText ("RATE", rateLabel, Justification::bottomLeft, false);

    theme::drawPanel (g, patternArea.reduced (1.0f));
    theme::drawHeading (g, patternArea.reduced (10.0f, 9.0f), "Patterns",
                        "bars = velocity - number = step on/off - bottom row = semitones (drag, wheel, click +1, right-click -1, double-click 0)");
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 700, 0.1f));
    g.drawText ("ARRANGE", arrangeLabel, Justification::centredLeft, false);
    g.drawText ("PATTERN", patternLabel, Justification::bottomLeft, false);

    // Mark the pattern that is playing right now.
    if (shownPlayingPattern >= 0 && shownPlayingPattern < patternTabs.size())
    {
        auto r = patternTabs[shownPlayingPattern]->getBounds().toFloat().expanded (2.0f);
        g.setColour (theme::textStrong.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 5.0f, 1.5f);
    }

    theme::drawPanel (g, bottomArea.reduced (1.0f));
    theme::drawHeading (g, bottomArea.reduced (10.0f, 9.0f), "Notes",
                        proc.getArpSettings().enabled ? String ("arpeggiator is on") : String ("switch the arp on, then hold a chord"));
}
} // namespace emx
