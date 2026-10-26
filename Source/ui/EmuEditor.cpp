// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EmuEditor.h"
#include "EditorPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

namespace
{
int textWidth (const String& s, float h = 9.0f, int weight = 700, float kern = 0.1f)
{
    GlyphArrangement ga;
    ga.addLineOfText (theme::font (h, weight, kern), s, 0.0f, 0.0f);
    return (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth()) + 2;
}

int macroIndexOfSource (int src)
{
    if (src == 20 || src == 21) return src - 20;
    if (src >= 32 && src <= 37) return src - 30;
    if (src >= 40 && src <= 47) return src - 32;
    return -1;
}

String hz (double v) { return v >= 1000.0 ? String (v / 1000.0, v >= 10000.0 ? 1 : 2) + " KHZ" : String (roundToInt (v)) + " HZ"; }
} // namespace

//==================================================================================
// EmuPanel
//==================================================================================
EmuPanel::EmuPanel (EmuContext& c, String t, String h) : ctx (c), title (std::move (t)), hint (std::move (h)) {}

int EmuPanel::getPreferredHeight()
{
    return open ? pad + headH + 8 + contentHeight() + pad : 2 * pad + headH - 2;
}

Rectangle<int> EmuPanel::headRow() const { return { pad, pad, getWidth() - 2 * pad, headH }; }
Rectangle<int> EmuPanel::contentArea() const { return { pad, pad + headH + 8, getWidth() - 2 * pad, jmax (0, getHeight() - 2 * pad - headH - 8) }; }

void EmuPanel::resized()
{
    headRight = getWidth() - pad;
    for (auto* c : getChildren())
        c->setVisible (false);
    if (open)
        layoutContent (contentArea());
}

void EmuPanel::paint (Graphics& g)
{
    theme::drawPanel (g, getLocalBounds().toFloat().reduced (1.0f));
    auto head = headRow().toFloat().withRight ((float) headRight - 8.0f);
    const auto c = Point<float> (head.getX() + 5.0f, head.getCentreY());
    Path chevron;
    if (open) { chevron.startNewSubPath (c.x - 4.0f, c.y - 2.0f); chevron.lineTo (c.x, c.y + 2.5f); chevron.lineTo (c.x + 4.0f, c.y - 2.0f); }
    else      { chevron.startNewSubPath (c.x - 2.0f, c.y - 4.0f); chevron.lineTo (c.x + 2.5f, c.y); chevron.lineTo (c.x - 2.0f, c.y + 4.0f); }
    g.setColour (theme::textStrong);
    g.strokePath (chevron, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    theme::drawHeading (g, head.withTrimmedLeft (16.0f).withSizeKeepingCentre (head.getWidth() - 16.0f, 16.0f).withX (head.getX() + 16.0f), title, hint);
    if (open)
        paintContent (g);
}

void EmuPanel::drawGroup (Graphics& g, Rectangle<int> r, const String& t)
{
    theme::drawInset (g, r.toFloat());
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.5f, 700, 0.12f));
    g.drawText (t, r.reduced (10, 8).removeFromTop (12), Justification::centredLeft, false);
}

void EmuPanel::mouseUp (const MouseEvent& e)
{
    if (e.mouseWasClicked() && headRow().withRight (headRight - 8).contains (e.getPosition()))
        setOpen (! open);
}

void EmuPanel::mouseMove (const MouseEvent& e)
{
    setMouseCursor (headRow().withRight (headRight - 8).contains (e.getPosition()) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

//==================================================================================
// FILTER & CHORUS
//==================================================================================
EmuFilterPanel::EmuFilterPanel (EmuContext& c)
    : EmuPanel (c, "EMU ENGINE", "filter, chorus and bend of the selected zone")
{
    enableButton.setTooltip ("Gives this zone the E-mu style voice: 6-stage envelopes, filter types, two LFOs and 36 cords");
    enableButton.onClick = [this] { if (ctx.enable) ctx.enable(); };
    offButton.setClickingTogglesState (false);
    offButton.setTooltip ("Turn the EMU engine off for this zone (back to the simple ADSR voice)");
    offButton.onClick = [this] { if (ctx.disable) ctx.disable(); };
    copyButton.setTooltip ("Give every zone this zone's EMU engine settings (envelopes, filter, LFOs, cords)");
    copyButton.onClick = [this] { if (ctx.copyToAll) ctx.copyToAll(); };

    // filter types: E-mu's named ones, then the morph types (approximated)
    filterType.addSectionHeading ("CLASSIC");
    for (int t : { 127, 1, 0, 2, 8, 9, 16, 17, 18, 32, 33, 34 })
        filterType.addItem (emu::filterNames()[t].toUpperCase(), t + 1);
    filterType.addSectionHeading ("MOTION");
    for (int t : { 64, 65, 66, 72, 80, 81 })
        filterType.addItem (emu::filterNames()[t].toUpperCase(), t + 1);
    for (int block = 128; block < 256; block += 16)
    {
        PopupMenu sub;
        for (int t = block; t < block + 16; ++t)
            sub.addItem (t + 1, "MORPH " + String (t));
        filterType.getRootMenu()->addSubMenu ("MORPH " + String (block) + "-" + String (block + 15), sub);
    }
    filterType.setTooltip ("Filter type (morph types are approximations so far)");
    filterType.onChange = [this]
    {
        const int t = filterType.getSelectedId() - 1;
        if (t >= 0)
            ctx.edit ([t] (emu::Design& d) { d.filterType = t; });
    };

    freq.setRange ({ 0.0, 1.0, 0.001 }, 1.0);
    freq.format = [] (double v) { return hz (57.0 * std::pow (20000.0 / 57.0, v)); };
    freq.onChange = [this] (double v) { ctx.edit ([v] (emu::Design& d) { d.cutoff = (float) v; }); };
    res.setRange ({ 0.0, 1.0, 0.001 }, 0.0);
    res.format = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    res.onChange = [this] (double v) { ctx.edit ([v] (emu::Design& d) { d.resonance = (float) v; }); };
    chorus.setRange ({ 0.0, 100.0, 0.1 }, 0.0);
    chorus.format = [] (double v) { return v < 0.05 ? String ("OFF") : String (v, 1) + "%"; };
    chorus.onChange = [this] (double v) { ctx.edit ([v] (emu::Design& d) { d.chorusAmount = (float) v; }); };
    chorus.slider.setTooltip ("Plays the sample twice, detuned against each other (100% = 50 cents)");
    width.setRange ({ 0.0, 100.0, 1.0 }, 100.0);
    width.format = [] (double v) { return String (roundToInt (v)) + "%"; };
    width.onChange = [this] (double v) { ctx.edit ([v] (emu::Design& d) { d.chorusWidth = (float) v; }); };
    width.slider.setTooltip ("How far apart (left / right) the two chorus copies sit");
    for (auto* b : { &bendUp, &bendDown })
    {
        b->setRange ({ 0.0, 24.0, 1.0 }, 2.0);
        b->format = [] (double v) { return String (roundToInt (v)) + " ST"; };
    }
    bendUp.onChange = [this] (double v) { ctx.edit ([v] (emu::Design& d) { d.bendUp = roundToInt (v); }); };
    bendDown.onChange = [this] (double v) { ctx.edit ([v] (emu::Design& d) { d.bendDown = roundToInt (v); }); };

    for (Component* comp : { (Component*) &enableButton, (Component*) &offButton, (Component*) &copyButton, (Component*) &filterType,
                             (Component*) &freq, (Component*) &res, (Component*) &chorus, (Component*) &width, (Component*) &bendUp, (Component*) &bendDown })
        addChildComponent (comp);
}

void EmuFilterPanel::refresh()
{
    const auto* d = ctx.design();
    const bool now = d != nullptr;
    if (now != has)
    {
        has = now;
        if (onFold) onFold();   // a different height
    }
    if (d != nullptr)
    {
        filterType.setSelectedId (d->filterType + 1, dontSendNotification);
        freq.setValue (d->cutoff);
        res.setValue (d->resonance);
        chorus.setValue (d->chorusAmount);
        width.setValue (d->chorusWidth);
        bendUp.setValue (d->bendUp);
        bendDown.setValue (d->bendDown);
        freq.setEnabled (d->filterType != 127);
        res.setEnabled (d->filterType != 127);
        width.setEnabled (d->chorusAmount > 0.05f);
    }
    offButton.setToggleState (has, dontSendNotification);
    hint = has ? "filter, chorus and bend of the selected zone" : "the E-mu style voice: 6-stage envelopes, filters, LFOs, cords";
    repaint();
}

int EmuFilterPanel::contentHeight() { return has ? 12 + 6 + 24 + 4 + freq.getPreferredHeight() + 14 : 30; }

void EmuFilterPanel::layoutContent (Rectangle<int> r)
{
    auto show = [] (Component& c, Rectangle<int> b) { c.setBounds (b); c.setVisible (true); };
    if (! has)
    {
        show (enableButton, r.removeFromLeft (textWidth (enableButton.getButtonText()) + 24).withSizeKeepingCentre (textWidth (enableButton.getButtonText()) + 24, 26));
        return;
    }
    {
        auto head = headRow();
        show (copyButton, head.removeFromRight (textWidth (copyButton.getButtonText()) + 22).reduced (0, 1));
        head.removeFromRight (4);
        show (offButton, head.removeFromRight (textWidth (offButton.getButtonText()) + 22).reduced (0, 1));
        headRight = offButton.getX();
    }
    const int g = 8, total = r.getWidth() - 2 * g;
    groups[0] = r.removeFromLeft (total * 46 / 100);
    r.removeFromLeft (g);
    groups[1] = r.removeFromLeft (total * 27 / 100);
    r.removeFromLeft (g);
    groups[2] = r;
    {
        auto a = groups[0].reduced (10, 8);
        a.removeFromTop (12 + 6);
        auto top = a.removeFromTop (24);
        show (filterType, top.removeFromLeft (jmin (top.getWidth(), 230)));
        a.removeFromTop (4);
        const int step = a.getWidth() / 2;
        show (freq, a.removeFromLeft (step).withSizeKeepingCentre (62, freq.getPreferredHeight()));
        show (res, a.withSizeKeepingCentre (62, res.getPreferredHeight()));
    }
    for (int i = 1; i < 3; ++i)
    {
        auto a = groups[i].reduced (10, 8);
        a.removeFromTop (12);
        Component* items[2] { i == 1 ? (Component*) &chorus : (Component*) &bendUp, i == 1 ? (Component*) &width : (Component*) &bendDown };
        const int step = a.getWidth() / 2;
        show (*items[0], a.removeFromLeft (step).withSizeKeepingCentre (62, freq.getPreferredHeight()).withY (a.getBottom() - freq.getPreferredHeight()));
        show (*items[1], a.withSizeKeepingCentre (62, freq.getPreferredHeight()).withY (a.getBottom() - freq.getPreferredHeight()));
    }
}

void EmuFilterPanel::paintContent (Graphics& g)
{
    if (! has)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (8.0f, 500, 0.04f));
        auto r = contentArea();
        r.removeFromLeft (enableButton.getRight() - r.getX() + 14);
        g.drawText ("This zone plays with the simple voice (ADSR on the VOICE panel). Imported Emulator X presets use the EMU engine already.",
                    r, Justification::centredLeft, true);
        return;
    }
    drawGroup (g, groups[0], "FILTER");
    drawGroup (g, groups[1], "CHORUS");
    drawGroup (g, groups[2], "PITCH BEND");
}

//==================================================================================
// 6-stage envelopes
//==================================================================================
float EnvGraph::yFor (float level) const
{
    const auto r = getLocalBounds().toFloat().reduced (10.0f, 14.0f);
    if (which == 0)
        return r.getBottom() - jlimit (0.0f, 100.0f, level) / 100.0f * r.getHeight();
    return r.getCentreY() - jlimit (-100.0f, 100.0f, level) / 100.0f * r.getHeight() * 0.5f;
}

std::array<Point<float>, 7> EnvGraph::points (const emu::Envelope& e) const
{
    const auto r = getLocalBounds().toFloat().reduced (10.0f, 14.0f);
    double secs[6] {};
    float prev = 0.0f;
    for (int i = 0; i < 6; ++i)
    {
        float lvl = e.level[(size_t) i];
        if (i == 3 && e.rate[3] <= 0.0f)
            lvl = e.level[2];   // decay 2 at rate 0: hold
        const double full = emu::rateSeconds (e.rate[(size_t) i]);
        secs[i] = i < 4 ? full * std::abs (lvl - prev) / 100.0 : (std::abs (lvl - prev) > 0.01f ? full : 0.0);
        prev = lvl;
    }
    // widths on a log scale, so 10 ms and 100 s both show; the sustain gets a fixed piece
    auto w = [] (double s) { return 6.0f + 120.0f * (float) (std::log1p (s * 8.0) / std::log1p (164.0 * 8.0)); };
    float total = 0.0f;
    for (auto s : secs) total += w (s);
    const float sustainW = r.getWidth() * 0.16f;
    const float scale = (r.getWidth() - sustainW) / jmax (1.0f, total);   // always fill the width
    std::array<Point<float>, 7> p;
    float x = r.getX();
    p[0] = { x, yFor (0.0f) };
    float lvl = 0.0f;
    for (int i = 0; i < 6; ++i)
    {
        if (i == 4)
            x += sustainW;
        x += w (secs[i]) * scale;
        lvl = (i == 3 && e.rate[3] <= 0.0f) ? e.level[2] : e.level[(size_t) i];
        p[(size_t) i + 1] = { x, yFor (lvl) };
    }
    return p;
}

void EnvGraph::paint (Graphics& g)
{
    theme::drawScreen (g, getLocalBounds().toFloat());
    const auto* d = ctx.design();
    if (d == nullptr)
        return;
    const auto& e = d->env[(size_t) which];
    const auto p = points (e);
    const auto r = getLocalBounds().toFloat().reduced (10.0f, 14.0f);

    // grid: zero line and stage names
    const float sustainW = r.getWidth() * 0.16f;
    g.setColour (theme::red.withAlpha (0.18f));
    g.fillRect (r.getX(), yFor (0.0f) - 0.5f, r.getWidth(), 1.0f);
    const char* names[] { "A1", "A2", "D1", "D2", "R1", "R2" };
    g.setFont (theme::font (7.0f, 700, 0.08f));
    for (int i = 0; i < 6; ++i)
    {
        const float from = i == 4 ? p[4].x + sustainW : p[(size_t) i].x, to = p[(size_t) i + 1].x;
        g.setColour (theme::red.withAlpha (0.12f));
        g.fillRect (to - 0.5f, r.getY(), 1.0f, r.getHeight());
        g.setColour (theme::red.withAlpha (0.6f));
        g.drawText (names[i], Rectangle<float> (jmin (from, to - 14.0f), 1.0f, jmax (14.0f, to - from), 12.0f), Justification::centred, false);
    }

    Path line;
    line.startNewSubPath (p[0]);
    for (int i = 1; i <= 4; ++i)
        line.lineTo (p[(size_t) i]);
    const float sustainEnd = p[4].x + sustainW;   // the sustain plateau, then release 1 and 2
    line.lineTo (sustainEnd, p[4].y);
    line.lineTo (p[5]);
    line.lineTo (p[6]);

    Path fill (line);
    fill.lineTo (p[6].x, yFor (0.0f));
    fill.lineTo (p[0].x, yFor (0.0f));
    fill.closeSubPath();
    g.setColour (theme::red.withAlpha (0.16f));
    g.fillPath (fill);
    g.setColour (theme::red);
    g.strokePath (line, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    if (e.rate[3] <= 0.0f)
    {
        g.setColour (theme::red.withAlpha (0.7f));
        g.setFont (theme::font (7.0f, 700, 0.1f));
        g.drawText ("SUSTAIN", Rectangle<float> (p[4].x, p[4].y - 14.0f, sustainEnd - p[4].x, 12.0f), Justification::centred, false);
    }
    for (int i = 1; i < 7; ++i)
    {
        const bool hot = i == hover || i == dragPoint;
        g.setColour (hot ? Colours::white : theme::red);
        g.fillEllipse (Rectangle<float> (hot ? 9.0f : 7.0f, hot ? 9.0f : 7.0f).withCentre (p[(size_t) i]));
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawEllipse (Rectangle<float> (hot ? 9.0f : 7.0f, hot ? 9.0f : 7.0f).withCentre (p[(size_t) i]), 1.0f);
    }
}

void EnvGraph::mouseMove (const MouseEvent& e)
{
    const auto* d = ctx.design();
    int h = -1;
    if (d != nullptr)
    {
        const auto p = points (d->env[(size_t) which]);
        for (int i = 1; i < 7; ++i)
            if (p[(size_t) i].getDistanceFrom (e.position) < 8.0f)
                h = i;
    }
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
        repaint();
    }
}

void EnvGraph::mouseDown (const MouseEvent& e)
{
    mouseMove (e);
    dragPoint = hover;
    if (const auto* d = ctx.design(); d != nullptr && dragPoint > 0)
    {
        startRate = d->env[(size_t) which].rate[(size_t) dragPoint - 1];
        startLevel = d->env[(size_t) which].level[(size_t) dragPoint - 1];
    }
}

void EnvGraph::mouseDrag (const MouseEvent& e)
{
    if (dragPoint <= 0)
        return;
    const int stage = dragPoint - 1;
    const auto r = getLocalBounds().toFloat().reduced (10.0f, 14.0f);
    const float range = which == 0 ? 100.0f : 200.0f;
    const float level = jlimit (which == 0 ? 0.0f : -100.0f, 100.0f, startLevel - (float) e.getDistanceFromDragStartY() * range / jmax (1.0f, r.getHeight()));
    const float rate = jlimit (0.0f, 127.0f, startRate + (float) e.getDistanceFromDragStartX() * (e.mods.isShiftDown() ? 0.03f : 0.15f));
    const int w = which;
    ctx.edit ([w, stage, level, rate] (emu::Design& d)
    {
        d.env[(size_t) w].level[(size_t) stage] = std::round (level * 10.0f) / 10.0f;
        d.env[(size_t) w].rate[(size_t) stage] = std::round (rate * 10.0f) / 10.0f;
    });
}

void EnvGraph::mouseUp (const MouseEvent&) { dragPoint = -1; repaint(); }

EmuEnvPanel::EmuEnvPanel (EmuContext& c)
    : EmuPanel (c, "6-STAGE ENVELOPES", "drag the points: up / down = level, left / right = rate (shift = fine)"), graph (c)
{
    const char* tips[] { "The amp envelope (loudness)", "The filter envelope (route it with a cord, e.g. to Filter Frequency)", "The aux envelope (route it anywhere with a cord)" };
    for (int i = 0; i < 3; ++i)
    {
        envButtons[i].setTooltip (tips[i]);
        envButtons[i].onClick = [this, i] { graph.which = i; refresh(); };
        addChildComponent (envButtons[i]);
    }
    addChildComponent (graph);
    const char* names[] { "ATTACK 1", "ATTACK 2", "DECAY 1", "DECAY 2", "RELEASE 1", "RELEASE 2" };
    for (int i = 0; i < 6; ++i)
    {
        auto* rb = rates.add (new ValueBox ("RATE"));
        rb->setRange (0.0, 127.0, 0.5);
        rb->setDragPixels (500.0f);
        rb->format = [] (double v)
        {
            const double s = emu::rateSeconds (v);
            return String (v, 1) + "  " + (s < 1.0 ? String (roundToInt (s * 1000.0)) + "MS" : String (s, s < 10.0 ? 2 : 1) + "S");
        };
        rb->onChange = [this, i] (double v) { const int w = graph.which; ctx.edit ([w, i, v] (emu::Design& d) { d.env[(size_t) w].rate[(size_t) i] = (float) v; }); };
        rb->setTooltip (String (names[i]) + ": rate (0-127) and the time for a full 0-100% move");
        auto* lb = levels.add (new ValueBox ("LEVEL"));
        lb->setRange (-100.0, 100.0, 0.1);
        lb->setDragPixels (400.0f);
        lb->format = [] (double v) { return String (v, 1) + "%"; };
        lb->onChange = [this, i] (double v) { const int w = graph.which; ctx.edit ([w, i, v] (emu::Design& d) { d.env[(size_t) w].level[(size_t) i] = (float) v; }); };
        lb->setTooltip (String (names[i]) + ": the level this stage moves to");
        addChildComponent (rb);
        addChildComponent (lb);
    }
}

EmuEnvPanel::~EmuEnvPanel() = default;

void EmuEnvPanel::refresh()
{
    for (int i = 0; i < 3; ++i)
        envButtons[i].setToggleState (graph.which == i, dontSendNotification);
    if (const auto* d = ctx.design())
    {
        const auto& e = d->env[(size_t) graph.which];
        for (int i = 0; i < 6; ++i)
        {
            levels[i]->setRange (graph.which == 0 ? 0.0 : -100.0, 100.0, 0.1);
            rates[i]->setValue (e.rate[(size_t) i]);
            levels[i]->setValue (e.level[(size_t) i]);
        }
    }
    graph.repaint();
    repaint();
}

int EmuEnvPanel::contentHeight() { return ctx.design() != nullptr ? 150 + 8 + 12 + 2 * ValueBox::labelledHeight + 6 : 24; }

void EmuEnvPanel::layoutContent (Rectangle<int> r)
{
    if (ctx.design() == nullptr)
        return;
    auto show = [] (Component& c, Rectangle<int> b) { c.setBounds (b); c.setVisible (true); };
    {
        auto head = headRow();
        for (int i = 2; i >= 0; --i)
        {
            show (envButtons[i], head.removeFromRight (textWidth (envButtons[i].getButtonText()) + 20).reduced (0, 1));
            head.removeFromRight (2);
        }
        headRight = envButtons[0].getX();
    }
    show (graph, r.removeFromTop (150));
    r.removeFromTop (8);
    const int colW = (r.getWidth() - 5 * 8) / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto col = r.removeFromLeft (colW);
        r.removeFromLeft (8);
        stageLabels[i] = col.removeFromTop (12);
        show (*rates[i], col.removeFromTop (ValueBox::labelledHeight));
        col.removeFromTop (6);
        show (*levels[i], col.removeFromTop (ValueBox::labelledHeight));
    }
}

void EmuEnvPanel::paintContent (Graphics& g)
{
    if (ctx.design() == nullptr)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (8.0f, 500, 0.04f));
        g.drawText ("Turn on the EMU engine for this zone to use 6-stage envelopes.", contentArea(), Justification::centredLeft, true);
        return;
    }
    const char* names[] { "ATTACK 1", "ATTACK 2", "DECAY 1", "DECAY 2 (0 = HOLD)", "RELEASE 1", "RELEASE 2" };
    g.setColour (theme::textStrong);
    g.setFont (theme::font (7.5f, 700, 0.12f));
    for (int i = 0; i < 6; ++i)
        g.drawText (names[i], stageLabels[i], Justification::centredLeft, true);
}

//==================================================================================
// LFOs
//==================================================================================
EmuLfoPanel::EmuLfoPanel (EmuContext& c) : EmuPanel (c, "LFOs", "two per voice - route them with cords (e.g. LFO 1 ~ to Fine Pitch for vibrato)")
{
    for (int i = 0; i < 2; ++i)
    {
        auto& l = lfos[i];
        for (int s = 0; s < emu::lfoShapeNames().size(); ++s)
            l.shape.addItem (emu::lfoShapeNames()[s].toUpperCase(), s + 1);
        l.shape.onChange = [this, i] { const int v = lfos[i].shape.getSelectedId() - 1; if (v >= 0) ctx.edit ([i, v] (emu::Design& d) { d.lfo[(size_t) i].shape = v; }); };
        for (int dv = emu::numDivisions - 1; dv >= 0; --dv)
            if (dv != 4 && dv != 5 && dv != 6)   // (duplicates in E-mu's list)
                l.division.addItem (emu::divisionName (dv), dv + 1);
        l.division.onChange = [this, i] { const int v = lfos[i].division.getSelectedId() - 1; if (v >= 0) ctx.edit ([i, v] (emu::Design& d) { d.lfo[(size_t) i].division = v; }); };
        l.sync.setTooltip ("Follow the song's tempo");
        l.sync.onClick = [this, i] { ctx.edit ([i] (emu::Design& d) { d.lfo[(size_t) i].sync = ! d.lfo[(size_t) i].sync; }); };
        l.keySync.setTooltip ("Restart the LFO with every note");
        l.keySync.onClick = [this, i] { ctx.edit ([i] (emu::Design& d) { d.lfo[(size_t) i].keySync = ! d.lfo[(size_t) i].keySync; }); };
        l.rate.setRange ({ 0.01, 30.0, 0.001, 0.3 }, 1.0);
        l.rate.format = [] (double v) { return String (v, v < 1.0 ? 2 : 1) + " HZ"; };
        l.rate.onChange = [this, i] (double v) { ctx.edit ([i, v] (emu::Design& d) { d.lfo[(size_t) i].hz = (float) v; }); };
        l.delay.setRange ({ 0.0, 10.0, 0.01, 0.5 }, 0.0);
        l.delay.format = [] (double v) { return v < 0.005 ? String ("OFF") : String (v, 2) + " S"; };
        l.delay.onChange = [this, i] (double v) { ctx.edit ([i, v] (emu::Design& d) { d.lfo[(size_t) i].delay = (float) v; }); };
        l.delay.slider.setTooltip ("Time before the LFO fades in");
        l.variation.setRange ({ 0.0, 100.0, 1.0 }, 0.0);
        l.variation.format = [] (double v) { return String (roundToInt (v)) + "%"; };
        l.variation.onChange = [this, i] (double v) { ctx.edit ([i, v] (emu::Design& d) { d.lfo[(size_t) i].variation = (float) v; }); };
        l.variation.slider.setTooltip ("Every note gets a slightly different rate (more = more random)");
        for (Component* comp : { (Component*) &l.shape, (Component*) &l.division, (Component*) &l.sync, (Component*) &l.keySync,
                                 (Component*) &l.rate, (Component*) &l.delay, (Component*) &l.variation })
            addChildComponent (comp);
    }
}

void EmuLfoPanel::refresh()
{
    if (const auto* d = ctx.design())
        for (int i = 0; i < 2; ++i)
        {
            auto& l = lfos[i];
            const auto& f = d->lfo[(size_t) i];
            l.shape.setSelectedId (f.shape + 1, dontSendNotification);
            l.division.setSelectedId (f.division + 1, dontSendNotification);
            l.sync.setToggleState (f.sync, dontSendNotification);
            l.keySync.setToggleState (f.keySync, dontSendNotification);
            l.rate.setValue (f.hz);
            l.delay.setValue (f.delay);
            l.variation.setValue (f.variation);
        }
    resized();
    repaint();
}

int EmuLfoPanel::contentHeight() { return ctx.design() != nullptr ? 12 + 6 + lfos[0].rate.getPreferredHeight() + 16 : 24; }

void EmuLfoPanel::layoutContent (Rectangle<int> r)
{
    const auto* d = ctx.design();
    if (d == nullptr)
        return;
    auto show = [] (Component& c, Rectangle<int> b) { c.setBounds (b); c.setVisible (true); };
    const int half = (r.getWidth() - 8) / 2;
    for (int i = 0; i < 2; ++i)
    {
        auto& l = lfos[i];
        l.group = i == 0 ? r.removeFromLeft (half) : r.removeFromRight (half);
        auto a = l.group.reduced (10, 8);
        a.removeFromTop (12 + 6);
        auto left = a.removeFromLeft (150);
        show (l.shape, left.removeFromTop (24));
        left.removeFromTop (6);
        auto row = left.removeFromTop (24);
        show (l.sync, row.removeFromLeft (row.getWidth() / 2 - 2));
        row.removeFromLeft (4);
        show (l.keySync, row);
        left.removeFromTop (6);
        l.preview = left.removeFromTop (jmin (left.getHeight(), 26));
        a.removeFromLeft (8);
        const int step = a.getWidth() / 3;
        auto cell = [&] (int k) { return Rectangle<int> (a.getX() + step * k, a.getY(), step, a.getHeight()).withSizeKeepingCentre (jmin (66, step), l.rate.getPreferredHeight()); };
        if (d->lfo[(size_t) i].sync)
            show (l.division, cell (0).withSizeKeepingCentre (jmin (80, step), 24));
        else
            show (l.rate, cell (0));
        show (l.delay, cell (1));
        show (l.variation, cell (2));
    }
}

void EmuLfoPanel::paintContent (Graphics& g)
{
    const auto* d = ctx.design();
    if (d == nullptr)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (8.0f, 500, 0.04f));
        g.drawText ("Turn on the EMU engine for this zone to use its LFOs.", contentArea(), Justification::centredLeft, true);
        return;
    }
    for (int i = 0; i < 2; ++i)
    {
        drawGroup (g, lfos[i].group, "LFO " + String (i + 1));
        // a little picture of the shape
        auto p = lfos[i].preview.toFloat();
        theme::drawScreen (g, p, false);
        p = p.reduced (6.0f, 5.0f);
        Path wave;
        const int shape = d->lfo[(size_t) i].shape;
        for (int k = 0; k <= 60; ++k)
        {
            const double ph = k / 30.0;
            const double q = ph - std::floor (ph);
            double v = 0.0;
            switch (shape)
            {
                case 1: v = q < 0.5 ? 4.0 * q - 1.0 : 3.0 - 4.0 * q; break;
                case 2: v = q < 0.5 ? 1.0 : -1.0; break;
                case 3: v = 2.0 * q - 1.0; break;
                case 4: { const double r = std::sin (std::floor (ph * 4.0) * 12.9898) * 43758.5453; v = (r - std::floor (r)) * 2.0 - 1.0; break; }
                default: v = std::sin (2.0 * MathConstants<double>::pi * q); break;
            }
            const float x = p.getX() + p.getWidth() * (float) k / 60.0f, y = p.getCentreY() - (float) v * p.getHeight() * 0.5f;
            if (k == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
        }
        g.setColour (theme::isMetal() ? theme::textStrong : theme::red);
        g.strokePath (wave, PathStrokeType (1.5f));
    }
}

//==================================================================================
// CORDS
//==================================================================================
class EmuCordsPanel::Row final : public Component
{
public:
    Row (EmuCordsPanel& o, int index) : owner (o), cord (index)
    {
        source.onChange = [this] { const int v = source.getSelectedId() - 1; if (v >= 0) set ([v] (emu::Cord& c) { c.source = v; }); };
        dest.onChange   = [this] { const int v = dest.getSelectedId() - 1;   if (v >= 0) set ([v] (emu::Cord& c) { c.dest = v; }); };
        amount.setRange (-100.0, 100.0, 0.5);
        amount.setDragPixels (300.0f);
        amount.format = [] (double v) { return (v > 0 ? "+" : "") + String (v, std::abs (v) < 10.0 ? 1 : 0) + "%"; };
        amount.onChange = [this] (double v) { set ([v] (emu::Cord& c) { c.amount = (float) v; }); };
        amount.setTooltip ("How much the source moves the destination (drag up / down, double-click to type)");
        remove.setTooltip ("Remove this cord");
        remove.onClick = [this] { set ([] (emu::Cord& c) { c = {}; }); };
        source.setTooltip ("What moves it");
        dest.setTooltip ("What it moves");
        for (Component* c : { (Component*) &source, (Component*) &dest, (Component*) &amount, (Component*) &remove })
            addAndMakeVisible (c);
    }

    void fillLists()
    {
        source.clear (dontSendNotification);
        String group;
        auto heading = [] (int id)
        {
            if (id == 0) return "";
            if (id <= 14) return "KEYBOARD";
            if (id <= 23 && macroIndexOfSource (id) < 0) return "CONTROLLERS";
            if (macroIndexOfSource (id) >= 0) return "MACROS (MIDI A-P)";
            if (id >= 72 && id <= 90) return "ENVELOPES";
            if (id >= 96 && id <= 105 && id != 98 && id != 99 && id != 100 && id != 101) return "LFOs";
            return "OTHER";
        };
        for (int id : emu::sourceIds())
        {
            const String h = heading (id);
            if (h != group && h.isNotEmpty())
                source.addSectionHeading (h);
            group = h;
            String name = emu::sourceName (id).toUpperCase();
            if (const int m = macroIndexOfSource (id); m >= 0)
                name = "MACRO " + String::charToString ((juce_wchar) ('A' + m)) + "  -  " + owner.ctx.proc.getMacroName (m).toUpperCase();
            source.addItem (name, id + 1);
        }
        dest.clear (dontSendNotification);
        group = {};
        auto dheading = [] (int id)
        {
            if (id == 0) return "";
            if (id >= 47 && id <= 55) return "PITCH AND SAMPLE";
            if (id >= 56 && id <= 58) return "FILTER";
            if ((id >= 64 && id <= 71) || id == 50 || id == 51 || id == 8) return "AMP, CHORUS, SENDS";
            if (id >= 72 && id <= 93) return "ENVELOPES";
            if (id >= 96 && id <= 105) return "LFOs";
            return "OTHER CORDS' AMOUNTS";
        };
        for (int id : emu::destIds())
        {
            const String h = dheading (id);
            if (h != group && h.isNotEmpty())
                dest.addSectionHeading (h);
            group = h;
            dest.addItem (emu::destName (id).toUpperCase(), id + 1);
        }
    }

    void show (const emu::Cord& c)
    {
        if (source.getNumItems() == 0)
            fillLists();
        source.setSelectedId (c.source + 1, dontSendNotification);
        dest.setSelectedId (c.dest + 1, dontSendNotification);
        if (source.getSelectedId() == 0) source.setText (emu::sourceName (c.source).toUpperCase(), dontSendNotification);
        if (dest.getSelectedId() == 0) dest.setText (emu::destName (c.dest).toUpperCase(), dontSendNotification);
        amount.setValue (c.amount);
        // a cord that sets another cord's amount: say which
        controls = c.dest >= 168 && c.dest < 204 ? c.dest - 167 : 0;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        numberArea = r.removeFromLeft (28);
        remove.setBounds (r.removeFromRight (24).withSizeKeepingCentre (22, 20));
        r.removeFromRight (6);
        amount.setBounds (r.removeFromRight (86).withSizeKeepingCentre (86, 20));
        r.removeFromRight (10);
        const int w = (r.getWidth() - 24) / 2;
        source.setBounds (r.removeFromLeft (w).withSizeKeepingCentre (w, 22));
        arrowArea = r.removeFromLeft (24);
        dest.setBounds (r.withSizeKeepingCentre (r.getWidth(), 22));
    }

    void paint (Graphics& g) override
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (8.0f, 700, 0.08f));
        g.drawText (String (cord + 1).paddedLeft ('0', 2), numberArea, Justification::centredLeft, false);
        const auto c = arrowArea.toFloat().getCentre();
        Path a;
        a.startNewSubPath (c.x - 6.0f, c.y);
        a.lineTo (c.x + 5.0f, c.y);
        a.startNewSubPath (c.x + 1.0f, c.y - 3.5f);
        a.lineTo (c.x + 5.0f, c.y);
        a.lineTo (c.x + 1.0f, c.y + 3.5f);
        g.setColour (theme::red);
        g.strokePath (a, PathStrokeType (1.5f));
        g.setColour (Colours::black.withAlpha (0.3f));
        g.fillRect (0.0f, (float) getHeight() - 1.0f, (float) getWidth(), 1.0f);
    }

    int cord;

private:
    void set (std::function<void (emu::Cord&)> change)
    {
        const int i = cord;
        owner.ctx.edit ([i, change] (emu::Design& d)
        {
            if ((int) d.cords.size() <= i)
                d.cords.resize ((size_t) i + 1);
            change (d.cords[(size_t) i]);
        });
    }

    EmuCordsPanel& owner;
    juce::ComboBox source, dest;
    ValueBox amount;
    juce::TextButton remove { "X" };
    juce::Rectangle<int> numberArea, arrowArea;
    int controls = 0;
};

EmuCordsPanel::EmuCordsPanel (EmuContext& c)
    : EmuPanel (c, "CORDS", "the mod matrix: source -> destination, amount (cords can also change other cords' amounts)")
{
    addButton.setTooltip ("Add a cord (uses the next free one of the 36)");
    addButton.onClick = [this]
    {
        const auto* d = ctx.design();
        if (d == nullptr)
            return;
        int free = -1;
        for (int i = 0; i < emu::maxCords && free < 0; ++i)
            if (i >= (int) d->cords.size() || (d->cords[(size_t) i].source == 0 && d->cords[(size_t) i].dest == 0))
                free = i;
        if (free < 0)
            return;
        ctx.edit ([free] (emu::Design& dd)
        {
            if ((int) dd.cords.size() <= free)
                dd.cords.resize ((size_t) free + 1);
            dd.cords[(size_t) free] = { 17, 56, 0.0f };   // a start: mod wheel -> filter frequency, amount 0
        });
    };
    allButton.setClickingTogglesState (false);
    allButton.setTooltip ("Show every one of the 36 cords, or only the ones in use");
    allButton.onClick = [this] { showAll = ! showAll; refresh(); if (onFold) onFold(); };
    addChildComponent (addButton);
    addChildComponent (allButton);
}

EmuCordsPanel::~EmuCordsPanel() = default;

std::vector<int> EmuCordsPanel::shownRows() const
{
    std::vector<int> v;
    const auto* d = ctx.design();
    if (d == nullptr)
        return v;
    for (int i = 0; i < emu::maxCords; ++i)
    {
        const bool used = i < (int) d->cords.size() && (d->cords[(size_t) i].source != 0 || d->cords[(size_t) i].dest != 0);
        if (showAll || used)
            v.push_back (i);
    }
    return v;
}

void EmuCordsPanel::refresh()
{
    const auto shown = shownRows();
    bool changed = (int) shown.size() != rows.size();
    for (int k = 0; ! changed && k < rows.size(); ++k)
        changed = rows[k]->cord != shown[(size_t) k];
    if (changed)
    {
        rows.clear();
        for (int i : shown)
            addChildComponent (rows.add (new Row (*this, i)));
        if (onFold) onFold();
    }
    if (const auto* d = ctx.design())
        for (auto* r : rows)
            r->show (r->cord < (int) d->cords.size() ? d->cords[(size_t) r->cord] : emu::Cord {});
    allButton.setButtonText (showAll ? "USED ONLY" : "SHOW ALL 36");
    resized();
}

int EmuCordsPanel::contentHeight()
{
    if (ctx.design() == nullptr)
        return 24;
    return jmax (24, rows.size() * 28);
}

void EmuCordsPanel::layoutContent (Rectangle<int> r)
{
    if (ctx.design() == nullptr)
        return;
    {
        auto head = headRow();
        allButton.setBounds (head.removeFromRight (textWidth ("SHOW ALL 36") + 22).reduced (0, 1));
        allButton.setVisible (true);
        head.removeFromRight (4);
        addButton.setBounds (head.removeFromRight (textWidth (addButton.getButtonText()) + 22).reduced (0, 1));
        addButton.setVisible (true);
        headRight = addButton.getX();
    }
    for (auto* row : rows)
    {
        row->setBounds (r.removeFromTop (28));
        row->setVisible (true);
    }
}

void EmuCordsPanel::paintContent (Graphics& g)
{
    if (ctx.design() == nullptr)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (8.0f, 500, 0.04f));
        g.drawText ("Turn on the EMU engine for this zone to use cords.", contentArea(), Justification::centredLeft, true);
    }
    else if (rows.isEmpty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (8.0f, 500, 0.04f));
        g.drawText ("No cords yet - press + ADD CORD", contentArea(), Justification::centredLeft, true);
    }
}
} // namespace emx
