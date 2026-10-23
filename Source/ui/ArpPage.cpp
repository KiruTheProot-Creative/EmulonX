// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ArpPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

//==================================================================================
// 16 steps: velocity bars, on/off, octave
//==================================================================================
class StepGrid final : public Component
{
public:
    explicit StepGrid (EmulonXProcessor& p) : proc (p) {}

    void tick()
    {
        const int s = proc.getArp().currentStep.load();
        if (s != shownStep)
        {
            shownStep = s;
            repaint();
        }
    }

    void paint (Graphics& g) override
    {
        const auto& a = proc.getArpSettings();
        const float colW = (float) getWidth() / 16.0f;
        const auto bars = barsArea(), nums = numbersArea(), octs = octaveArea();
        theme::drawLed (g, bars, {});

        // guide lines at 25 / 50 / 75 %
        g.setColour (theme::red.withAlpha (0.08f));
        for (int i = 1; i < 4; ++i)
            g.fillRect (bars.getX(), bars.getY() + bars.getHeight() * (float) i / 4.0f, bars.getWidth(), 1.0f);

        for (int i = 0; i < 16; ++i)
        {
            const auto& st = a.steps[(size_t) i];
            const bool inUse = i < a.length;
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
                g.setGradientFill (ColourGradient::vertical (Colour (0xffff5050).withAlpha (alpha), bar.getY(),
                                                             Colour (0xff7a0a0a).withAlpha (alpha), bar.getBottom()));
                g.fillRoundedRectangle (bar, 2.5f);
                if (st.on && inUse)
                {
                    g.setColour (Colours::white.withAlpha (playing ? 1.0f : 0.7f));
                    g.fillRect (bar.withHeight (2.0f));
                }
            }

            // Step number (click to switch off)
            auto nb = Rectangle<float> ((float) i * colW, nums.getY(), colW, nums.getHeight()).reduced (3.0f, 1.0f);
            const bool lit = st.on && inUse;
            g.setColour (lit ? (playing ? Colour (0xffff3030) : Colour (0xff8a1818)) : Colour (0xff2a2629));
            g.fillRoundedRectangle (nb, 4.0f);
            g.setColour (Colours::black);
            g.drawRoundedRectangle (nb, 4.0f, 1.0f);
            g.setColour (inUse ? Colours::white.withAlpha (lit ? 1.0f : 0.5f) : theme::textMuted.withAlpha (0.4f));
            g.setFont (theme::font (8.5f, 700, 0.04f));
            g.drawText (String (i + 1), nb, Justification::centred, false);

            // Octave cell
            auto ob = Rectangle<float> ((float) i * colW, octs.getY(), colW, octs.getHeight()).reduced (3.0f, 1.0f);
            theme::drawLed (g, ob, st.octave == 0 ? String ("0") : (st.octave > 0 ? "+" : "") + String (st.octave), 8.0f,
                            Justification::centred, inUse ? (st.octave == 0 ? theme::redDark : theme::red) : theme::redDark.withAlpha (0.4f));
        }

        // Length marker
        const float lx = (float) a.length * colW;
        if (a.length < 16)
        {
            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillRect (Rectangle<float> (lx, bars.getY(), (float) getWidth() - lx, bars.getHeight()).reduced (0.0f, 2.0f));
            g.setColour (theme::red);
            g.fillRect (lx - 1.0f, bars.getY() + 2.0f, 2.0f, bars.getHeight() - 4.0f);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        const int i = stepAt (e.position.x);
        if (i < 0)
            return;
        auto a = proc.getArpSettings();
        if (barsArea().contains (e.position))
        {
            dragging = true;
            setVelocity (a, i, e.position.y);
            proc.setArpSettings (a);
        }
        else if (numbersArea().contains (e.position))
        {
            a.steps[(size_t) i].on = ! a.steps[(size_t) i].on;
            proc.setArpSettings (a);
        }
        else if (octaveArea().contains (e.position))
        {
            auto& o = a.steps[(size_t) i].octave;
            o = e.mods.isPopupMenu() ? (o <= -1 ? 2 : o - 1) : (o >= 2 ? -1 : o + 1);
            proc.setArpSettings (a);
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging)
            return;
        const int i = stepAt (e.position.x);
        if (i < 0)
            return;
        auto a = proc.getArpSettings();
        setVelocity (a, i, e.position.y);
        proc.setArpSettings (a);
    }

    void mouseUp (const MouseEvent&) override { dragging = false; }

private:
    Rectangle<float> barsArea() const    { return getLocalBounds().toFloat().withTrimmedBottom (62.0f); }
    Rectangle<float> numbersArea() const { return getLocalBounds().toFloat().withTop ((float) getHeight() - 56.0f).withHeight (26.0f); }
    Rectangle<float> octaveArea() const  { return getLocalBounds().toFloat().withTop ((float) getHeight() - 24.0f); }
    int stepAt (float x) const { const int i = (int) (x / ((float) getWidth() / 16.0f)); return i >= 0 && i < 16 ? i : -1; }
    void setVelocity (ArpSettings& a, int i, float y) const
    {
        const auto b = barsArea().reduced (0.0f, 6.0f);
        a.steps[(size_t) i].velocity = jlimit (0.0f, 1.0f, (b.getBottom() - y) / b.getHeight());
        if (a.steps[(size_t) i].velocity > 0.02f)
            a.steps[(size_t) i].on = true;
    }

    EmulonXProcessor& proc;
    int shownStep = -1;
    bool dragging = false;
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
          return t.isEmpty() ? String ("HOLD SOME KEYS...") : "PLAYING FROM:  " + t;
      }, 9.0f),
      grid (std::make_unique<StepGrid> (p))
{
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
    octaves.slider.setTooltip ("Repeat the notes over this many octaves");
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
    length.onChange = [this] (double v) { edit ([v] (ArpSettings& a) { a.length = roundToInt (v); }); };
    length.slider.setTooltip ("Pattern length in steps");

    allOn.setTooltip ("Switch every step on");
    allOn.onClick = [this] { edit ([] (ArpSettings& a) { for (auto& s : a.steps) s.on = true; }); };
    accents.setTooltip ("Loud first step of every four, the rest softer");
    accents.onClick = [this] { edit ([] (ArpSettings& a) { for (size_t i = 0; i < a.steps.size(); ++i) a.steps[i].velocity = i % 4 == 0 ? 1.0f : 0.6f; }); };
    randomVel.setTooltip ("Random velocities");
    randomVel.onClick = [this]
    {
        edit ([] (ArpSettings& a) { auto& r = Random::getSystemRandom(); for (auto& s : a.steps) s.velocity = 0.35f + 0.65f * r.nextFloat(); });
    };
    flat.setTooltip ("All steps the same velocity");
    flat.onClick = [this] { edit ([] (ArpSettings& a) { for (auto& s : a.steps) s.velocity = 0.8f; }); };
    clearOct.setTooltip ("Reset every step's octave to 0");
    clearOct.onClick = [this] { edit ([] (ArpSettings& a) { for (auto& s : a.steps) s.octave = 0; }); };

    for (Component* c : { (Component*) &onButton, (Component*) &latchButton, (Component*) &velPattern, (Component*) &velPlayed,
                          (Component*) &modeBox, (Component*) &denBox, (Component*) &dots[0], (Component*) &dots[1], (Component*) &dots[2],
                          (Component*) &beats, (Component*) &octaves, (Component*) &gate, (Component*) &swing, (Component*) &length,
                          (Component*) &rateLed, (Component*) &heldLed, (Component*) grid.get(), (Component*) &allOn,
                          (Component*) &accents, (Component*) &randomVel, (Component*) &flat, (Component*) &clearOct })
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
    length.setValue (a.length);
    rateLed.refresh();
    grid->repaint();
}

void ArpPage::tick()
{
    grid->tick();
    heldLed.refresh();
    rateLed.refresh();
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

    // Pattern
    auto pa = patternArea.toNearestInt().reduced (11, 9);
    pa.removeFromTop (26);
    auto tools = pa.removeFromBottom (26);
    pa.removeFromBottom (8);
    auto side = pa.removeFromLeft (76);
    length.setBounds (side.withSizeKeepingCentre (62, kh));
    pa.removeFromLeft (8);
    gridArea = pa.toFloat();
    grid->setBounds (pa);
    for (auto* b : { &allOn, &accents, &randomVel, &flat, &clearOct })
    {
        b->setBounds (tools.removeFromLeft (b == &clearOct ? 100 : 82));
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
    theme::drawHeading (g, patternArea.reduced (10.0f, 9.0f), "Pattern",
                        "drag the bars for velocity - click a number to make a rest - click an octave cell to change it (right-click goes down)");

    theme::drawPanel (g, bottomArea.reduced (1.0f));
    theme::drawHeading (g, bottomArea.reduced (10.0f, 9.0f), "Notes",
                        proc.getArpSettings().enabled ? String ("arpeggiator is on") : String ("switch the arp on, then hold a chord"));
}
} // namespace emx
