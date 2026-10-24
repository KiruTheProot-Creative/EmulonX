// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxPage.h"
#include <optional>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

//==================================================================================
// Modulation helpers
//==================================================================================
namespace modui
{
void showMenu (EmulonXProcessor& proc, fx::Target t, Component* near)
{
    PopupMenu m;
    m.addSectionHeader ("Modulate " + proc.describeTarget (t));
    auto item = [&proc, &m, t] (fx::Source src)
    {
        m.addItem (proc.describeSource (src), true, proc.hasRoute (src, t), [&proc, src, t] { proc.toggleRoute (src, t); });
    };
    m.addSectionHeader ("Envelopes");
    for (int i = 0; i < (int) proc.getFxSetup().envelopes.size(); ++i)
        item ({ fx::SrcKind::env, i });
    m.addSectionHeader ("Macros");
    for (int i = 0; i < numMacros; ++i)
        item ({ fx::SrcKind::macro, i });
    m.addSectionHeader ("Controllers");
    item ({ fx::SrcKind::wheel, 0 });

    bool any = false;
    for (auto& r : proc.getFxSetup().routes)
        any = any || r.target == t;
    if (any)
    {
        m.addSeparator();
        m.addItem ("Remove all modulation", [&proc, t]
        {
            std::vector<fx::Source> srcs;
            for (auto& r : proc.getFxSetup().routes)
                if (r.target == t)
                    srcs.push_back (r.source);
            for (auto& s : srcs)
                proc.toggleRoute (s, t);
        });
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (near).withMinimumWidth (180));
}

bool tryLearn (EmulonXProcessor& proc, fx::Target t)
{
    const int macro = proc.getLearnMacro();
    if (macro < 0)
        return false;
    const fx::Source src { fx::SrcKind::macro, macro };
    proc.setLearnMacro (-1);
    if (! proc.hasRoute (src, t))
        proc.toggleRoute (src, t);
    return true;
}

ModRings rings (EmulonXProcessor& proc, fx::Target t, float base)
{
    ModRings r;
    r.base = base;
    for (auto& q : proc.getFxSetup().routes)
        if (q.target == t)
            r.rings.push_back ({ q.amount, q.source.kind == fx::SrcKind::env });
    if (r.rings.empty())
        return r;
    auto& eng = proc.getFxEngine();
    if (t.isMaster())
        r.current = eng.volumeOut.load();
    else if (t.isZonePitch())
    {
        const int count = eng.pitchOutCount.load();
        for (int i = 0; i < count; ++i)
            if (eng.pitchOutZone[i].load() == t.slot)
                r.current = eng.pitchOutNorm[i].load();
    }
    else
    {
        const int count = eng.modOutCount.load();
        for (int i = 0; i < count; ++i)
            if (eng.modOut[i].id.load() == t.module && t.slot >= 0 && t.slot < fx::maxSlots)
            {
                r.current = eng.modOut[i].value[t.slot].load();
                break;
            }
    }
    return r;
}

void showMacroMenu (EmulonXProcessor& proc, int macro, Component* near)
{
    PopupMenu m;
    const fx::Source src { fx::SrcKind::macro, macro };
    m.addSectionHeader (proc.getMacroName (macro));
    if (proc.getLearnMacro() == macro)
        m.addItem ("Cancel linking", [&proc] { proc.setLearnMacro (-1); });
    else
        m.addItem ("Link: click an FX knob or the master volume next", [&proc, macro] { proc.setLearnMacro (macro); });
    std::vector<fx::Target> targets;
    for (auto& r : proc.getFxSetup().routes)
        if (r.source == src)
            targets.push_back (r.target);
    if (! targets.empty())
    {
        m.addSectionHeader ("Connected to (click to remove)");
        for (auto& t : targets)
            m.addItem ("Remove  " + proc.describeTarget (t), [&proc, src, t] { proc.toggleRoute (src, t); });
    }
    else
        m.addItem ("Not connected yet - or right-click any FX knob", false, false, nullptr);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (near).withMinimumWidth (200));
}
} // namespace modui

//==================================================================================
// One effect in the chain
//==================================================================================
/** Metal button with an up or down triangle. */
class ArrowButton final : public TextButton
{
public:
    explicit ArrowButton (bool pointsUp) : up (pointsUp) {}
    void paintButton (Graphics& g, bool over, bool down) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, {}, over, down);
        const auto c = getLocalBounds().toFloat().getCentre().translated (0.0f, down ? 1.0f : 0.0f);
        Path p;
        if (up) p.addTriangle (c.x - 4.5f, c.y + 2.5f, c.x + 4.5f, c.y + 2.5f, c.x, c.y - 3.0f);
        else    p.addTriangle (c.x - 4.5f, c.y - 2.5f, c.x + 4.5f, c.y - 2.5f, c.x, c.y + 3.0f);
        g.setColour (isEnabled() ? (over ? theme::textStrong : theme::hue (0xffdddddd)) : theme::hue (0xff666666));
        g.fillPath (p);
    }
private:
    bool up;
};

class FxCard final : public Component
{
public:
    FxCard (EmulonXProcessor& p, int id) : proc (p), moduleId (id),
        grLed ([this] { return "GR " + String (shownGr, 1) + " DB"; }, 8.5f),
        timeLed ([this]
        {
            const auto* m = proc.findFxModule (moduleId);
            if (m == nullptr) return String();
            const auto& s = m->settings;
            const double secs = fx::syncSeconds (s.num, s.den, s.dot, proc.getFxEngine().bpmUsed.load());
            return String (s.num) + "/" + String (s.den) + (s.dot == 1 ? " DOT" : s.dot == 2 ? " TRI" : "")
                   + "  " + (secs < 1.0 ? String (roundToInt (secs * 1000.0)) + " MS" : String (secs, 2) + " S");
        }, 8.0f)
    {
        const auto* m = proc.findFxModule (moduleId);
        jassert (m != nullptr);
        const auto& d = fx::def (m->type);
        type = m->type;

        // Title ("Filter 2" when the chain has more than one)
        int chainId = 0;
        proc.findFxModule (moduleId, &chainId);
        int same = 0, pos = 0;
        for (auto& o : *proc.getFxChain (chainId))
            if (o.type == m->type) { ++same; if (o.id == moduleId) pos = same; }
        title = String (d.name).toUpperCase() + (same > 1 ? " " + String (pos) : String());

        power.setClickingTogglesState (false);
        power.setTooltip ("Switch this effect on / off");
        power.onClick = [this] { if (auto* mm = proc.findFxModule (moduleId)) proc.setFxOn (moduleId, ! mm->on); };
        upButton.setTooltip ("Move up (effects run top to bottom)");
        downButton.setTooltip ("Move down");
        removeButton.setTooltip ("Remove this effect");
        upButton.onClick = [this] { proc.moveFx (moduleId, -1); };
        downButton.onClick = [this] { proc.moveFx (moduleId, 1); };
        removeButton.onClick = [this] { proc.removeFx (moduleId); };
        for (Button* b : { (Button*) &power, (Button*) &upButton, (Button*) &downButton, (Button*) &removeButton })
            addAndMakeVisible (b);

        if (d.modes.size() > 1)
        {
            for (size_t i = 0; i < d.modes.size(); ++i)
                modeBox.addItem (String (d.modes[i]).toUpperCase(), (int) i + 1);
            modeBox.setSelectedItemIndex (m->settings.mode, dontSendNotification);
            modeBox.setTooltip ("Type");
            modeBox.onChange = [this] { proc.setFxMode (moduleId, modeBox.getSelectedItemIndex()); };
            addAndMakeVisible (modeBox);
        }
        if (type == fx::Type::comp)
            addAndMakeVisible (grLed);

        if (type == fx::Type::filter)
        {
            for (int i = 0; i < 2; ++i)
            {
                slope[i].setTooltip (i == 0 ? "Gentle slope (12 dB per octave)" : "Steep slope (24 dB per octave)");
                slope[i].onClick = [this, i]
                {
                    if (auto* mm = proc.findFxModule (moduleId)) { auto s = mm->settings; s.slope = i; proc.setFxSettings (moduleId, s); }
                };
                addAndMakeVisible (slope[i]);
            }
        }

        if (type == fx::Type::delay)
        {
            syncButton.setTooltip ("Delay time follows the tempo");
            syncButton.onClick = [this]
            {
                if (auto* mm = proc.findFxModule (moduleId)) { auto s = mm->settings; s.sync = ! s.sync; proc.setFxSettings (moduleId, s); }
            };
            addAndMakeVisible (syncButton);
            if (m->settings.sync)
            {
                numKnob = std::make_unique<ValueControl> ("Beats", ValueControl::Style::knob, 40);
                numKnob->setRange (NormalisableRange<double> (1, 32, 1), 1);
                numKnob->format = [] (double v) { return String (roundToInt (v)); };
                numKnob->setRingMargin (6);
                numKnob->onChange = [this] (double v)
                {
                    if (auto* mm = proc.findFxModule (moduleId)) { auto s = mm->settings; s.num = roundToInt (v); proc.setFxSettings (moduleId, s); }
                };
                addAndMakeVisible (*numKnob);
                for (int den : { 1, 2, 4, 8, 16, 32, 64 })
                    denBox.addItem ("/ " + String (den), den);
                denBox.onChange = [this]
                {
                    if (auto* mm = proc.findFxModule (moduleId)) { auto s = mm->settings; s.den = denBox.getSelectedId(); proc.setFxSettings (moduleId, s); }
                };
                addAndMakeVisible (denBox);
                const char* dotNames[] { "STR", "DOT", "TRI" };
                const char* dotTips[] { "Straight", "Dotted (x1.5)", "Triplet (x2/3)" };
                for (int i = 0; i < 3; ++i)
                {
                    dots[i].setButtonText (dotNames[i]);
                    dots[i].setTooltip (dotTips[i]);
                    dots[i].onClick = [this, i]
                    {
                        if (auto* mm = proc.findFxModule (moduleId)) { auto s = mm->settings; s.dot = i; proc.setFxSettings (moduleId, s); }
                    };
                    addAndMakeVisible (dots[i]);
                }
                addAndMakeVisible (timeLed);
            }
        }

        for (int slot = 0; slot < (int) d.slots.size(); ++slot)
        {
            const auto* sp = fx::specFor (m->type, m->settings.mode, slot);
            if (sp == nullptr || (type == fx::Type::delay && slot == 0 && m->settings.sync))
                continue;
            auto c = std::make_unique<ValueControl> (sp->label, ValueControl::Style::knob, 40);
            c->setRange (NormalisableRange<double> (0.0, 1.0, 0.0), sp->toNorm (sp->def));
            c->format = [sp] (double v) { return sp->format (sp->fromNorm ((float) v)); };
            c->setRingMargin (6);
            c->onChange = [this, slot] (double v) { proc.setFxParam (moduleId, slot, (float) v); };
            const fx::Target target { moduleId, slot };
            auto* raw = c.get();
            c->slider.onRightClick = [this, target, raw] { modui::showMenu (proc, target, raw); };
            c->slider.interceptClick = [this, target] { return modui::tryLearn (proc, target); };
            c->modRings = [this, target, raw] { return modui::rings (proc, target, (float) raw->getValue()); };
            c->slider.setTooltip ("Drag, wheel, double-click resets - right-click to modulate");
            addAndMakeVisible (*c);
            knobs.push_back ({ slot, std::move (c) });
        }
        sync();
    }

    int getModuleId() const noexcept { return moduleId; }
    static int heightNeeded() { return 26 + 6 + (6 + 40 + 4 + 16 + 3 + 14) + 6; }

    void sync()
    {
        const auto* m = proc.findFxModule (moduleId);
        if (m == nullptr)
            return;
        power.setToggleState (m->on, dontSendNotification);
        power.setButtonText (m->on ? "ON" : "OFF");
        for (auto& k : knobs)
        {
            k.c->setValue (m->norm[(size_t) k.slot]);
            k.c->setAlpha (m->on ? 1.0f : 0.4f);
        }
        if (modeBox.getNumItems() > 0)
            modeBox.setSelectedItemIndex (m->settings.mode, dontSendNotification);
        for (int i = 0; i < 2; ++i)
            slope[i].setToggleState (m->settings.slope == i, dontSendNotification);
        syncButton.setToggleState (m->settings.sync, dontSendNotification);
        if (numKnob != nullptr)
        {
            numKnob->setValue (m->settings.num);
            denBox.setSelectedId (m->settings.den, dontSendNotification);
            for (int i = 0; i < 3; ++i)
                dots[i].setToggleState (m->settings.dot == i, dontSendNotification);
            timeLed.refresh();
        }
    }

    void tick()
    {
        auto& eng = proc.getFxEngine();
        if (type == fx::Type::comp)
        {
            const int count = eng.modOutCount.load();
            for (int i = 0; i < count; ++i)
                if (eng.modOut[i].id.load() == moduleId)
                {
                    const float gr = eng.modOut[i].gainReduction.exchange (0.0f);
                    shownGr = jmin (gr, shownGr * 0.85f);   // fall back smoothly
                    grLed.refresh();
                    break;
                }
        }
        if (numKnob != nullptr)
            timeLed.refresh();
        for (auto& k : knobs)
            if (! k.c->modRings ().rings.empty())
                k.c->repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 0);
        auto head = r.removeFromTop (26).reduced (0, 3);
        power.setBounds (head.removeFromLeft (40));
        head.removeFromLeft (8);
        removeButton.setBounds (head.removeFromRight (24));
        head.removeFromRight (4);
        downButton.setBounds (head.removeFromRight (30));
        head.removeFromRight (4);
        upButton.setBounds (head.removeFromRight (30));
        head.removeFromRight (8);
        titleArea = head.removeFromLeft (118).toFloat();
        if (modeBox.getNumItems() > 0)
            modeBox.setBounds (head.removeFromLeft (jmin (124, head.getWidth())));
        head.removeFromLeft (6);
        if (type == fx::Type::comp)
            grLed.setBounds (head.removeFromLeft (jmin (84, head.getWidth())));

        r.removeFromTop (6);
        auto body = r.withTrimmedBottom (6);
        const int kh = 6 + 40 + 4 + 16 + 3 + 14;
        if (type == fx::Type::filter)
        {
            auto col = body.removeFromLeft (58).withSizeKeepingCentre (58, 54);
            slope[0].setBounds (col.removeFromTop (24));
            col.removeFromTop (6);
            slope[1].setBounds (col.removeFromTop (24));
            body.removeFromLeft (6);
        }
        if (type == fx::Type::delay)
        {
            syncButton.setBounds (body.removeFromLeft (50).withSizeKeepingCentre (50, 24).translated (0, -12));
            body.removeFromLeft (4);
            if (numKnob != nullptr)
            {
                numKnob->setBounds (body.removeFromLeft (52).withSizeKeepingCentre (52, kh));
                auto col = body.removeFromLeft (92).withSizeKeepingCentre (92, 76);
                denBox.setBounds (col.removeFromTop (22));
                col.removeFromTop (5);
                auto dotRow = col.removeFromTop (22);
                for (int i = 0; i < 3; ++i)
                {
                    dots[i].setBounds (dotRow.removeFromLeft (29));
                    dotRow.removeFromLeft (2);
                }
                col.removeFromTop (5);
                timeLed.setBounds (col.removeFromTop (20));
                body.removeFromLeft (6);
            }
        }
        if (! knobs.empty())
        {
            const int cell = jmin (62, body.getWidth() / (int) knobs.size());
            for (auto& k : knobs)
                k.c->setBounds (body.removeFromLeft (cell).withSizeKeepingCentre (cell, kh));
        }
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setGradientFill (ColourGradient::vertical (theme::hue (0xff1c1a1f), r.getY(), theme::hue (0xff111014), r.getBottom()));
        g.fillRoundedRectangle (r, 7.0f);
        auto head = r.withHeight (26.0f);
        {
            Graphics::ScopedSaveState s (g);
            Path clip;
            clip.addRoundedRectangle (head.getX(), head.getY(), head.getWidth(), head.getHeight(), 7.0f, 7.0f, true, true, false, false);
            g.reduceClipRegion (clip);
            g.setGradientFill (ColourGradient::vertical (theme::hue (0xff3a3640), head.getY(), theme::hue (0xff1e1c22), head.getBottom()));
            g.fillRect (head);
        }
        g.setColour (Colours::black);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        g.fillRect (r.getX(), head.getBottom(), r.getWidth(), 1.0f);
        const auto* m = proc.findFxModule (moduleId);
        theme::drawGlowText (g, title, titleArea, theme::font (9.5f, 700, 0.12f),
                             m != nullptr && m->on ? theme::textStrong : theme::textMuted, theme::red.withAlpha (0.6f), 6);
    }

private:
    EmulonXProcessor& proc;
    const int moduleId;
    fx::Type type = fx::Type::filter;
    String title;
    TextButton power { "ON" }, removeButton { "X" };
    ArrowButton upButton { true }, downButton { false };
    ComboBox modeBox;
    float shownGr = 0.0f;
    LedDisplay grLed, timeLed;
    TextButton slope[2] { TextButton ("12 DB"), TextButton ("24 DB") };
    TextButton syncButton { "SYNC" };
    std::unique_ptr<ValueControl> numKnob;
    ComboBox denBox;
    TextButton dots[3];
    struct KnobRef { int slot; std::unique_ptr<ValueControl> c; };
    std::vector<KnobRef> knobs;
    Rectangle<float> titleArea;
};

//==================================================================================
// Envelope graph (Vital-style: drag points and curves)
//==================================================================================
class EnvelopeGraph final : public Component
{
public:
    EnvelopeGraph (EmulonXProcessor& p, FxPage& pg) : proc (p), page (pg) {}

    void tick()
    {
        auto& eng = proc.getFxEngine();
        const int i = page.getSelectedEnvelope();
        if (i < 0 || i >= fx::maxEnvelopes)
            return;
        const float v = eng.envValue[i].load(), x = eng.envPhase[i].load();
        const int st = eng.envStage[i].load();
        if (std::abs (v - lastV) > 1.0e-4f || std::abs (x - lastX) > 1.0e-4f || st != lastStage)
        {
            lastV = v; lastX = x; lastStage = st;
            repaint();
        }
    }

    void paint (Graphics& g) override
    {
        theme::drawLed (g, getLocalBounds().toFloat(), {});
        const auto* E = env();
        if (E == nullptr)
            return;
        const auto geo = geometry();
        const int G = ! E->ahdsr && E->grid > 0 ? E->grid : 4;
        g.setColour (theme::red.withAlpha (0.13f));
        for (int i = 0; i <= G; ++i)
        {
            g.fillRect (std::round (geo.X ((float) i / G)), geo.p, 1.0f, geo.H - 2 * geo.p);
            g.fillRect (geo.p, std::round (geo.Y ((float) i / G)), geo.W - 2 * geo.p, 1.0f);
        }

        Path line, fill;
        std::vector<Point<float>> pts;
        if (! E->ahdsr)
        {
            const int n = (int) std::ceil (geo.W - 2 * geo.p);
            for (int i = 0; i <= n; ++i)
            {
                const float u = (float) i / (float) n;
                pts.push_back ({ geo.X (u), geo.Y (E->shapeAt (u)) });
            }
        }
        else
        {
            const auto A = ahd (*E, geo);
            auto seg = [&] (float a, float b, std::function<float (float)> f)
            {
                const int n = jmax (2, (int) std::ceil ((b - a) / 2.0f));
                for (int i = 0; i <= n; ++i)
                {
                    const float u = (float) i / (float) n;
                    pts.push_back ({ a + (b - a) * u, geo.Y (f (u)) });
                }
            };
            seg (A.x0, A.xa, [E] (float u) { return fx::curve (u, E->ca); });
            seg (A.xa, A.xh, [] (float) { return 1.0f; });
            seg (A.xh, A.xd, [E] (float u) { return 1.0f + (E->s - 1.0f) * fx::curve (u, E->cd); });
            seg (A.xd, A.xs, [E] (float) { return E->s; });
            seg (A.xs, A.xr, [E] (float u) { return E->s * (1.0f - fx::curve (u, E->cr)); });
        }
        if (pts.empty())
            return;
        fill.startNewSubPath (pts.front().x, geo.Y (0));
        for (size_t i = 0; i < pts.size(); ++i)
        {
            fill.lineTo (pts[i]);
            if (i == 0) line.startNewSubPath (pts[i]); else line.lineTo (pts[i]);
        }
        fill.lineTo (pts.back().x, geo.Y (0));
        fill.closeSubPath();
        g.setGradientFill (ColourGradient::vertical (theme::hue (0x59ff2828), geo.p, theme::hue (0x00ff2828), geo.H));
        g.fillPath (fill);
        g.setColour (theme::red.withAlpha (0.35f));
        g.strokePath (line, PathStrokeType (5.0f));
        g.setColour (theme::hue (0xffff4040));
        g.strokePath (line, PathStrokeType (2.0f));

        g.setFont (theme::font (8.0f, 700, 0.06f));
        for (auto& h : hits (*E, geo))
        {
            const bool big = h.kind == 'p' || h.kind == 'A' || h.kind == 'H' || h.kind == 'D' || h.kind == 'R';
            if (big)
            {
                g.setColour (Colours::white);
                g.fillEllipse (h.x - 5, h.y - 5, 10, 10);
                g.setColour (theme::hue (0xffcc0000));
                g.drawEllipse (h.x - 5, h.y - 5, 10, 10, 1.5f);
                if (h.kind != 'p')
                {
                    g.setColour (theme::hue (0xffffd0d0));
                    g.drawText (h.kind == 'D' ? "D/S" : String::charToString ((juce_wchar) h.kind),
                                Rectangle<float> (h.x + 6, h.y - 16, 30, 12), Justification::centredLeft, false);
                }
            }
            else
            {
                g.setColour (theme::hue (0xffffb0b0));
                g.drawEllipse (h.x - 3.5f, h.y - 3.5f, 7, 7, 1.5f);
            }
        }

        // Playhead
        float px = -1.0f;
        if (! E->ahdsr)
            px = lastStage >= 0 ? geo.X (lastX) : -1.0f;
        else
        {
            const auto A = ahd (*E, geo);
            if (lastStage == 1) px = lastX < E->a + E->h + E->d ? A.x0 + lastX * A.sx : A.xd + (A.xs - A.xd) * 0.5f;
            else if (lastStage == 2 && lastX < E->r) px = A.xs + lastX * A.sx;
        }
        if (px >= 0.0f)
        {
            g.setColour (theme::hue (0x80ffdcdc));
            g.fillRect (px, geo.p, 1.0f, geo.H - 2 * geo.p);
            g.setColour (Colours::white);
            g.fillEllipse (px - 4.5f, geo.Y (lastV) - 4.5f, 9, 9);
        }
    }

    void mouseMove (const MouseEvent& e) override
    {
        const auto* E = env();
        setMouseCursor (E != nullptr && pick (*E, geometry(), e.position).has_value() ? MouseCursor::DraggingHandCursor
                                                                                    : MouseCursor::CrosshairCursor);
    }

    void mouseDown (const MouseEvent& e) override
    {
        dragging = {};
        const auto* E = env();
        if (E == nullptr)
            return;
        const auto geo = geometry();
        if (auto h = pick (*E, geo, e.position))
        {
            dragging = *h;
            startEnv = *E;
            startSx = E->ahdsr ? ahd (*E, geo).sx : 1.0f;
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        const auto* cur = env();
        if (cur == nullptr || dragging.kind == 0)
            return;
        auto E = *cur;
        const auto geo = geometry();
        const float dx = (float) e.getDistanceFromDragStartX(), dy = (float) e.getDistanceFromDragStartY();
        const auto& S = startEnv;
        switch (dragging.kind)
        {
            case 'p':
            {
                auto& P = E.points;
                const int i = dragging.index;
                float x = geo.iX (e.position.x), y = geo.iY (e.position.y);
                if (E.grid > 0) { x = std::round (x * E.grid) / E.grid; y = std::round (y * E.grid) / E.grid; }
                P[(size_t) i].y = jlimit (0.0f, 1.0f, y);
                P[(size_t) i].x = i == 0 ? 0.0f : i == (int) P.size() - 1 ? 1.0f : jlimit (P[(size_t) i - 1].x, P[(size_t) i + 1].x, x);
                break;
            }
            case 'c':
            {
                const auto& a = S.points[(size_t) dragging.index];
                const auto& b = S.points[(size_t) dragging.index + 1];
                E.points[(size_t) dragging.index].c = jlimit (-1.0f, 1.0f, a.c + (b.y >= a.y ? 1.0f : -1.0f) * dy / 60.0f);
                break;
            }
            case 'a': E.ca = jlimit (-1.0f, 1.0f, S.ca + dy / 60.0f); break;
            case 'd': E.cd = jlimit (-1.0f, 1.0f, S.cd - dy / 60.0f); break;
            case 'r': E.cr = jlimit (-1.0f, 1.0f, S.cr - dy / 60.0f); break;
            case 'A': E.a = jlimit (0.0f, 10.0f, S.a + dx / startSx); break;
            case 'H': E.h = jlimit (0.0f, 10.0f, S.h + dx / startSx); break;
            case 'D': E.d = jlimit (0.0f, 10.0f, S.d + dx / startSx); E.s = jlimit (0.0f, 1.0f, geo.iY (e.position.y)); break;
            case 'R': E.r = jlimit (0.001f, 10.0f, S.r + dx / startSx); break;
            default: return;
        }
        proc.setEnvelope (page.getSelectedEnvelope(), E);
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const auto* cur = env();
        if (cur == nullptr || cur->ahdsr)
            return;
        auto E = *cur;
        const auto geo = geometry();
        for (auto& h : hits (E, geo))
            if (h.kind == 'p' && e.position.getDistanceFrom ({ h.x, h.y }) < 11.0f)
            {
                if (h.index > 0 && h.index < (int) E.points.size() - 1)
                {
                    E.points.erase (E.points.begin() + h.index);
                    proc.setEnvelope (page.getSelectedEnvelope(), E);
                }
                return;
            }
        float x = jlimit (0.0f, 1.0f, geo.iX (e.position.x)), y = jlimit (0.0f, 1.0f, geo.iY (e.position.y));
        if (E.grid > 0) { x = std::round (x * E.grid) / E.grid; y = std::round (y * E.grid) / E.grid; }
        size_t i = 1;
        while (i < E.points.size() - 1 && E.points[i].x <= x)
            ++i;
        E.points.insert (E.points.begin() + (std::ptrdiff_t) i, { x, y, 0.0f });
        proc.setEnvelope (page.getSelectedEnvelope(), E);
    }

private:
    struct Geo
    {
        float W, H, p;
        float X (float x) const { return p + x * (W - 2 * p); }
        float Y (float y) const { return H - p - y * (H - 2 * p); }
        float iX (float px) const { return (px - p) / (W - 2 * p); }
        float iY (float py) const { return (H - p - py) / (H - 2 * p); }
    };
    struct Ahd { float sx, x0, xa, xh, xd, xs, xr; };
    struct Hit { int kind = 0; int index = 0; float x = 0, y = 0; };

    const fx::Envelope* env() const
    {
        const auto& envs = proc.getFxSetup().envelopes;
        const int i = page.getSelectedEnvelope();
        return i >= 0 && i < (int) envs.size() ? &envs[(size_t) i] : nullptr;
    }
    Geo geometry() const { return { (float) getWidth(), (float) getHeight(), 12.0f }; }
    static Ahd ahd (const fx::Envelope& E, const Geo& g)
    {
        const float iw = g.W - 2 * g.p, sx = iw * 0.8f / jmax (E.a + E.h + E.d + E.r, 0.001f);
        Ahd a;
        a.sx = sx; a.x0 = g.p; a.xa = a.x0 + E.a * sx; a.xh = a.xa + E.h * sx; a.xd = a.xh + E.d * sx;
        a.xs = a.xd + iw * 0.2f; a.xr = a.xs + E.r * sx;
        return a;
    }
    static std::vector<Hit> hits (const fx::Envelope& E, const Geo& g)
    {
        std::vector<Hit> hs;
        if (! E.ahdsr)
        {
            for (size_t i = 0; i < E.points.size(); ++i)
                hs.push_back ({ 'p', (int) i, g.X (E.points[i].x), g.Y (E.points[i].y) });
            for (size_t i = 0; i + 1 < E.points.size(); ++i)
            {
                const auto& a = E.points[i];
                const auto& b = E.points[i + 1];
                if (b.x - a.x < 0.02f || std::abs (b.y - a.y) < 0.02f)
                    continue;
                hs.push_back ({ 'c', (int) i, g.X ((a.x + b.x) * 0.5f), g.Y (a.y + (b.y - a.y) * fx::curve (0.5f, a.c)) });
            }
        }
        else
        {
            const auto A = ahd (E, g);
            hs.push_back ({ 'A', 0, A.xa, g.Y (1) });
            hs.push_back ({ 'H', 0, A.xh, g.Y (1) });
            hs.push_back ({ 'D', 0, A.xd, g.Y (E.s) });
            hs.push_back ({ 'R', 0, A.xr, g.Y (0) });
            hs.push_back ({ 'a', 0, (A.x0 + A.xa) * 0.5f, g.Y (fx::curve (0.5f, E.ca)) });
            hs.push_back ({ 'd', 0, (A.xh + A.xd) * 0.5f, g.Y (1 + (E.s - 1) * fx::curve (0.5f, E.cd)) });
            hs.push_back ({ 'r', 0, (A.xs + A.xr) * 0.5f, g.Y (E.s * (1 - fx::curve (0.5f, E.cr))) });
        }
        return hs;
    }
    static std::optional<Hit> pick (const fx::Envelope& E, const Geo& g, Point<float> pos)
    {
        std::optional<Hit> best;
        float bd = 11.0f;
        for (auto& h : hits (E, g))
        {
            const float d = pos.getDistanceFrom ({ h.x, h.y });
            if (d < bd - 0.01f || (best && std::abs (d - bd) < 0.01f && h.kind == 'H' && pos.x > h.x))
            {
                bd = d;
                best = h;
            }
        }
        return best;
    }

    EmulonXProcessor& proc;
    FxPage& page;
    Hit dragging;
    fx::Envelope startEnv;
    float startSx = 1.0f, lastV = 0.0f, lastX = 0.0f;
    int lastStage = -1;
};

//==================================================================================
// Destinations: routes of the selected envelope, plus all macro / mod-wheel routes
//==================================================================================
class DestinationList final : public Component
{
public:
    DestinationList (EmulonXProcessor& p) : proc (p) {}

    void rebuild (int envIndex)
    {
        rows.clear();
        layout.clear();
        const auto& routes = proc.getFxSetup().routes;
        auto addRows = [&] (const String& header, std::function<bool (const fx::Route&)> pick, const String& emptyText)
        {
            layout.push_back ({ header, -1, true });
            bool any = false;
            for (int i = 0; i < (int) routes.size(); ++i)
                if (pick (routes[(size_t) i]))
                {
                    any = true;
                    auto row = std::make_unique<Row> (proc, i);
                    addAndMakeVisible (*row);
                    layout.push_back ({ {}, (int) rows.size(), false });
                    rows.push_back (std::move (row));
                }
            if (! any)
                layout.push_back ({ emptyText, -1, false });
        };
        addRows ("ENV " + String (envIndex + 1) + " DESTINATIONS",
                 [envIndex] (const fx::Route& r) { return r.source.kind == fx::SrcKind::env && r.source.index == envIndex; },
                 "RIGHT-CLICK ANY FX KNOB (OR THE MASTER VOLUME) AND PICK ENV " + String (envIndex + 1));
        addRows ("MACROS & MOD WHEEL",
                 [] (const fx::Route& r) { return r.source.kind != fx::SrcKind::env; },
                 "RIGHT-CLICK A KNOB, OR RIGHT-CLICK A MACRO ON THE MAIN TAB > LINK");
        setSize (getWidth(), jmax (40, (int) layout.size() * rowH + 4));
        resized();
        repaint();
    }

    void syncValues()
    {
        for (auto& r : rows)
            r->sync();
    }

    void resized() override
    {
        int y = 2;
        for (auto& item : layout)
        {
            if (item.row >= 0)
                rows[(size_t) item.row]->setBounds (0, y, getWidth(), rowH);
            y += rowH;
        }
    }

    void paint (Graphics& g) override
    {
        int y = 2;
        for (auto& item : layout)
        {
            if (item.row < 0)
            {
                g.setColour (item.header ? theme::textStrong : theme::listText);
                g.setFont (item.header ? theme::font (8.5f, 700, 0.12f) : theme::font (7.5f, 500, 0.05f));
                g.drawText (item.text, 6, y, getWidth() - 12, rowH, Justification::centredLeft, true);
            }
            y += rowH;
        }
    }

private:
    static constexpr int rowH = 28;

    class Row final : public Component
    {
    public:
        Row (EmulonXProcessor& p, int idx) : proc (p), index (idx)
        {
            amount.setSliderStyle (Slider::RotaryVerticalDrag);
            amount.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            amount.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
            amount.setRange (-1.0, 1.0, 0.01);
            amount.setDoubleClickReturnValue (true, 0.5);
            amount.setTooltip ("How much (negative = upside down)");
            amount.onValueChange = [this] { proc.setRouteAmount (index, (float) amount.getValue()); repaint(); };
            addAndMakeVisible (amount);
            removeButton.setTooltip ("Disconnect");
            removeButton.onClick = [this]
            {
                const int i = index;
                Component::SafePointer<Row> safe (this);
                MessageManager::callAsync ([safe, i, &pr = proc] { if (safe != nullptr) pr.removeRoute (i); });
            };
            addAndMakeVisible (removeButton);
            sync();
        }

        void sync()
        {
            const auto& routes = proc.getFxSetup().routes;
            if (index < (int) routes.size())
            {
                const auto& r = routes[(size_t) index];
                label = proc.describeSource (r.source).toUpperCase() + "  >  " + proc.describeTarget (r.target);
                amount.setValue (r.amount, dontSendNotification);
            }
            repaint();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 2);
            removeButton.setBounds (r.removeFromRight (24).withSizeKeepingCentre (24, 20));
            r.removeFromRight (4);
            ledArea = r.removeFromRight (48).toFloat().withSizeKeepingCentre (48, 18);
            amount.setBounds (r.removeFromRight (26));
            r.removeFromRight (4);
            labelArea = r.toFloat();
        }

        void paint (Graphics& g) override
        {
            g.setColour (theme::listText);
            g.setFont (theme::font (8.0f, 500, 0.03f));
            g.drawText (label, labelArea.reduced (2.0f, 0.0f), Justification::centredLeft, true);
            const int v = roundToInt (amount.getValue() * 100.0);
            theme::drawLed (g, ledArea, (v > 0 ? "+" : "") + String (v) + "%", 8.0f);
            g.setColour (Colours::black.withAlpha (0.4f));
            g.fillRect (0.0f, (float) getHeight() - 1.0f, (float) getWidth(), 1.0f);
        }

    private:
        EmulonXProcessor& proc;
        int index;
        Slider amount;
        TextButton removeButton { "X" };
        String label;
        Rectangle<float> labelArea, ledArea;
    };

    struct Item { String text; int row; bool header; };
    EmulonXProcessor& proc;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<Item> layout;
};

//==================================================================================
// FxPage
//==================================================================================
FxPage::FxPage (EmulonXProcessor& p)
    : proc (p),
      bpmLed ([this] { return "SYNC " + String (proc.getFxEngine().bpmUsed.load(), 1); }, 8.5f)
{
    channelBox.setTooltip ("Which channel you are editing: the whole preset, or one zone's own effects");
    channelBox.onChange = [this] { setChannel (channelBox.getSelectedId() == 1 ? 0 : channelBox.getSelectedId() - 1); };
    addAndMakeVisible (channelBox);

    addBox.setTextWhenNothingSelected ("+ ADD EFFECT");
    for (auto t : fx::addOrder())
        addBox.addItem (String (fx::def (t).name).toUpperCase(), (int) t + 1);
    addBox.onChange = [this]
    {
        const int id = addBox.getSelectedId();
        addBox.setSelectedId (0, dontSendNotification);
        if (id > 0)
        {
            proc.addFx (channel, (fx::Type) (id - 1));
            MessageManager::callAsync ([safe = Component::SafePointer<FxPage> (this)]
            {
                if (safe != nullptr)
                    safe->chainView.setViewPosition (0, safe->chainContent.getHeight());
            });
        }
    };
    addAndMakeVisible (addBox);

    bpmBox.setJustificationType (Justification::centred);
    bpmBox.setTooltip ("Tempo for synced delays and envelopes when the DAW doesn't send one (click to type)");
    bpmBox.onEditorShow = [this] { if (auto* ed = bpmBox.getCurrentTextEditor()) ed->setInputRestrictions (6, "0123456789."); };
    bpmBox.onTextChange = [this] { proc.setFxBpm (bpmBox.getText().getFloatValue()); };
    addAndMakeVisible (bpmBox);
    bpmLed.setTooltip ("The tempo synced effects follow right now (your DAW's tempo when it sends one)");
    addAndMakeVisible (bpmLed);

    chainView.setViewedComponent (&chainContent, false);
    chainView.setScrollBarsShown (true, false);
    addAndMakeVisible (chainView);

    // Envelopes
    envAdd.setTooltip ("Add an envelope");
    envAdd.onClick = [this] { proc.addEnvelope(); selectEnvelope ((int) proc.getFxSetup().envelopes.size() - 1); };
    envRemove.setTooltip ("Remove this envelope (and its connections)");
    envRemove.onClick = [this] { proc.removeEnvelope (envIndex); selectEnvelope (jmin (envIndex, (int) proc.getFxSetup().envelopes.size() - 1)); };
    typeShape.setTooltip ("A shape you draw, looping or one-shot, synced to the tempo");
    typeAhdsr.setTooltip ("Attack - hold - decay - sustain - release, started by each note");
    auto setType = [this] (bool ahdsr)
    {
        auto e = proc.getFxSetup().envelopes[(size_t) envIndex];
        e.ahdsr = ahdsr;
        proc.setEnvelope (envIndex, e);
    };
    typeShape.onClick = [setType] { setType (false); };
    typeAhdsr.onClick = [setType] { setType (true); };
    shapeBox.setTextWhenNothingSelected ("LOAD SHAPE...");
    int sid = 1;
    for (auto& [name, pts] : fx::shapeLibrary())
        shapeBox.addItem (name.toUpperCase(), sid++);
    shapeBox.onChange = [this]
    {
        const int id = shapeBox.getSelectedId();
        shapeBox.setSelectedId (0, dontSendNotification);
        if (id <= 0) return;
        auto e = proc.getFxSetup().envelopes[(size_t) envIndex];
        e.points = fx::shapeLibrary()[(size_t) id - 1].second;
        proc.setEnvelope (envIndex, e);
    };
    for (int gsz : { 0, 4, 8, 16, 32 })
        gridBox.addItem (gsz == 0 ? String ("GRID OFF") : "GRID " + String (gsz), gsz + 1);
    gridBox.setTooltip ("Points snap to this grid");
    gridBox.onChange = [this]
    {
        auto e = proc.getFxSetup().envelopes[(size_t) envIndex];
        e.grid = gridBox.getSelectedId() - 1;
        proc.setEnvelope (envIndex, e);
    };
    for (Component* c : { (Component*) &envAdd, (Component*) &envRemove, (Component*) &typeShape, (Component*) &typeAhdsr,
                          (Component*) &shapeBox, (Component*) &gridBox })
        addAndMakeVisible (c);

    graph = std::make_unique<EnvelopeGraph> (proc, *this);
    addAndMakeVisible (*graph);
    destinations = std::make_unique<DestinationList> (proc);
    destView.setViewedComponent (destinations.get(), false);
    destView.setScrollBarsShown (true, false);
    addAndMakeVisible (destView);

    proc.addChangeListener (this);
    rebuild();
}

FxPage::~FxPage()
{
    proc.removeChangeListener (this);
    cards.clear();
}

void FxPage::visibilityChanged()
{
    if (isVisible())
        changeListenerCallback (nullptr);
}

void FxPage::changeListenerCallback (ChangeBroadcaster*)
{
    if (! isVisible())
        return;
    if (proc.getFxStructureVersion() != builtStructure || (int) proc.getZones().size() != builtZoneCount
        || proc.getFxChain (channel) == nullptr)
        rebuild();
    else
        syncValues();
}

void FxPage::setChannel (int chainId)
{
    channel = proc.getFxChain (chainId) != nullptr ? chainId : 0;
    builtStructure = -1;
    rebuild();
}

void FxPage::selectEnvelope (int index)
{
    envIndex = jlimit (0, jmax (0, (int) proc.getFxSetup().envelopes.size() - 1), index);
    builtStructure = -1;
    rebuild();
}

void FxPage::rebuildChannelBox()
{
    channelBox.clear (dontSendNotification);
    auto count = [] (const fx::Chain& c) { return c.empty() ? String() : "  (" + String ((int) c.size()) + " FX)"; };
    channelBox.addItem ("WHOLE PRESET" + count (proc.getFxSetup().master), 1);
    const auto& zs = proc.getZones();
    for (size_t i = 0; i < zs.size(); ++i)
        channelBox.addItem ("ZONE " + String ((int) i + 1) + " - " + zs[i].name.toUpperCase() + count (zs[i].fx), zs[i].id + 1);
    channelBox.setSelectedId (channel == 0 ? 1 : channel + 1, dontSendNotification);
}

void FxPage::rebuild()
{
    if (proc.getFxChain (channel) == nullptr)
        channel = 0;
    envIndex = jlimit (0, jmax (0, (int) proc.getFxSetup().envelopes.size() - 1), envIndex);
    builtStructure = proc.getFxStructureVersion();
    builtZoneCount = (int) proc.getZones().size();
    rebuildChannelBox();

    // Cards (keep the scroll position)
    const int scrollY = chainView.getViewPositionY();
    cards.clear();
    const auto* chain = proc.getFxChain (channel);
    const int w = jmax (100, chainView.getWidth() - chainView.getScrollBarThickness() - 4);
    int y = 4;
    for (auto& m : *chain)
    {
        auto* c = cards.add (new FxCard (proc, m.id));
        chainContent.addAndMakeVisible (c);
        c->setBounds (2, y, w, FxCard::heightNeeded());
        y += FxCard::heightNeeded() + 6;
    }
    chainContent.setSize (w + 4, jmax (y, 40));
    chainView.setViewPosition (0, scrollY);

    // Envelope tabs
    envTabs.clear();
    for (int i = 0; i < (int) proc.getFxSetup().envelopes.size(); ++i)
    {
        auto* b = envTabs.add (new TextButton (String (i + 1)));
        b->setToggleState (i == envIndex, dontSendNotification);
        b->onClick = [this, i] { selectEnvelope (i); };
        addAndMakeVisible (b);
    }
    envAdd.setEnabled ((int) proc.getFxSetup().envelopes.size() < fx::maxEnvelopes);
    envRemove.setEnabled (proc.getFxSetup().envelopes.size() > 1);
    rebuildEnvelopeOptions();
    destinations->setSize (jmax (100, destView.getWidth() - destView.getScrollBarThickness()), destinations->getHeight());
    destinations->rebuild (envIndex);
    resized();
    syncValues();
    repaint();
}

void FxPage::rebuildEnvelopeOptions()
{
    envOptions.clear();
    envSyncers.clear();
    const auto& envs = proc.getFxSetup().envelopes;
    if (envIndex >= (int) envs.size())
        return;
    const auto& E = envs[(size_t) envIndex];
    shapeBox.setVisible (! E.ahdsr);
    gridBox.setVisible (! E.ahdsr);

    auto editEnv = [this] (std::function<void (fx::Envelope&)> f)
    {
        auto e = proc.getFxSetup().envelopes[(size_t) envIndex];
        f (e);
        proc.setEnvelope (envIndex, e);
    };
    // Every option registers how to show the current envelope, so changes appear right away.
    auto toggle = [this, editEnv] (const String& text, const String& tip, std::function<bool (const fx::Envelope&)> isOn,
                                   std::function<void (fx::Envelope&)> f)
    {
        auto* b = new TextButton (text);
        b->setTooltip (tip);
        b->onClick = [editEnv, f] { editEnv (f); };
        envOptions.add (b);
        addAndMakeVisible (b);
        envSyncers.push_back ([b, isOn] (const fx::Envelope& e) { b->setToggleState (isOn (e), dontSendNotification); });
        return b;
    };
    auto knob = [this, editEnv] (const String& caption, NormalisableRange<double> range, double def, std::function<String (double)> fmt,
                                 std::function<double (const fx::Envelope&)> get, std::function<void (fx::Envelope&, double)> f)
    {
        auto* k = new ValueControl (caption, ValueControl::Style::knob, 40);
        k->setRange (range, def);
        k->format = fmt;
        k->onChange = [editEnv, f] (double v) { editEnv ([f, v] (fx::Envelope& e) { f (e, v); }); };
        envOptions.add (k);
        addAndMakeVisible (k);
        envSyncers.push_back ([k, get] (const fx::Envelope& e) { k->setValue (get (e)); });
        return k;
    };

    auto timeRange = [] (bool allowZero)
    {
        return NormalisableRange<double> (0.0, 10.0,
            [allowZero] (double, double, double n) { return allowZero && n < 0.005 ? 0.0 : 0.001 * std::pow (1.0e4, n); },
            [] (double, double, double v) { return v <= 0.0 ? 0.0 : jlimit (0.0, 1.0, std::log (v / 0.001) / std::log (1.0e4)); });
    };
    auto timeFmt = [] (double t) { return t <= 0.0 ? String ("0 MS") : t < 1.0 ? String (roundToInt (t * 1000.0)) + " MS" : String (t, 2) + " S"; };
    using Env = fx::Envelope;

    if (! E.ahdsr)
    {
        toggle ("LOOP", "Repeat the shape", [] (const Env& e) { return e.loop; }, [] (Env& e) { e.loop = true; });
        toggle ("1-SHOT", "Play the shape once per note", [] (const Env& e) { return ! e.loop; }, [] (Env& e) { e.loop = false; });
        toggle ("RETRIG", "Restart the shape with every note", [] (const Env& e) { return e.trigger == Env::retrig; },
                [] (Env& e) { e.trigger = Env::retrig; });
        toggle ("FREE", "Keep running on its own clock, ignore notes", [] (const Env& e) { return e.trigger == Env::free; },
                [] (Env& e) { e.trigger = Env::free; });
        toggle ("SYNC", "Length follows the tempo", [] (const Env& e) { return e.sync; }, [] (Env& e) { e.sync = ! e.sync; });
        if (E.sync)
        {
            knob ("Beats", NormalisableRange<double> (1, 32, 1), 1, [] (double v) { return String (roundToInt (v)); },
                  [] (const Env& e) { return (double) e.num; }, [] (Env& e, double v) { e.num = roundToInt (v); });
            auto* den = new ComboBox();
            for (int d : { 1, 2, 4, 8, 16, 32, 64 })
                den->addItem ("/ " + String (d), d);
            den->onChange = [editEnv, den] { const int v = den->getSelectedId(); editEnv ([v] (Env& e) { e.den = v; }); };
            envOptions.add (den);
            addAndMakeVisible (den);
            envSyncers.push_back ([den] (const Env& e) { den->setSelectedId (e.den, dontSendNotification); });
            toggle ("STR", "Straight", [] (const Env& e) { return e.dot == 0; }, [] (Env& e) { e.dot = 0; });
            toggle ("DOT", "Dotted (x1.5)", [] (const Env& e) { return e.dot == 1; }, [] (Env& e) { e.dot = 1; });
            toggle ("TRI", "Triplet (x2/3)", [] (const Env& e) { return e.dot == 2; }, [] (Env& e) { e.dot = 2; });
        }
        else
        {
            knob ("Rate", NormalisableRange<double> (0.05, 20.0,
                      [] (double, double, double n) { return 0.05 * std::pow (400.0, n); },
                      [] (double, double, double v) { return jlimit (0.0, 1.0, std::log (v / 0.05) / std::log (400.0)); }),
                  2.0, [] (double v) { return String (v, 2) + " HZ"; },
                  [] (const Env& e) { return (double) e.hz; }, [] (Env& e, double v) { e.hz = (float) v; });
        }
    }
    else
    {
        knob ("Attack", timeRange (true), 0.01, timeFmt, [] (const Env& e) { return (double) e.a; }, [] (Env& e, double v) { e.a = (float) v; });
        knob ("Hold", timeRange (true), 0.0, timeFmt, [] (const Env& e) { return (double) e.h; }, [] (Env& e, double v) { e.h = (float) v; });
        knob ("Decay", timeRange (true), 0.3, timeFmt, [] (const Env& e) { return (double) e.d; }, [] (Env& e, double v) { e.d = (float) v; });
        knob ("Sustain", NormalisableRange<double> (0.0, 1.0, 0.01), 0.6, [] (double v) { return String (roundToInt (v * 100.0)) + "%"; },
              [] (const Env& e) { return (double) e.s; }, [] (Env& e, double v) { e.s = (float) v; });
        knob ("Release", timeRange (false), 0.4, timeFmt, [] (const Env& e) { return (double) e.r; },
              [] (Env& e, double v) { e.r = (float) jmax (0.001, v); });
    }
    syncEnvelopeOptions();
    layoutEnvelopeOptions();
}

void FxPage::syncEnvelopeOptions()
{
    const auto& envs = proc.getFxSetup().envelopes;
    if (envIndex >= (int) envs.size())
        return;
    const auto& E = envs[(size_t) envIndex];
    typeShape.setToggleState (! E.ahdsr, dontSendNotification);
    typeAhdsr.setToggleState (E.ahdsr, dontSendNotification);
    gridBox.setSelectedId (E.grid + 1, dontSendNotification);
    for (auto& f : envSyncers)
        f (E);
}

void FxPage::layoutEnvelopeOptions()
{
    auto r = optionsArea.toNearestInt();
    if (r.isEmpty() || envOptions.isEmpty())
        return;
    const auto& E = proc.getFxSetup().envelopes[(size_t) envIndex];
    const int kh = 40 + 4 + 16 + 3 + 14;
    if (E.ahdsr)
    {
        const int cell = r.getWidth() / envOptions.size();
        for (auto* c : envOptions)
            c->setBounds (r.removeFromLeft (cell).withSizeKeepingCentre (jmin (62, cell), kh));
        return;
    }
    // LOOP / 1-SHOT, RETRIG / FREE  |  SYNC + beats / division  (or SYNC + rate)
    auto col = r.removeFromLeft (140).withSizeKeepingCentre (140, 54);
    auto rowA = col.removeFromTop (24);
    envOptions[0]->setBounds (rowA.removeFromLeft (68));
    envOptions[1]->setBounds (rowA.removeFromRight (68));
    col.removeFromTop (6);
    auto rowB = col.removeFromTop (24);
    envOptions[2]->setBounds (rowB.removeFromLeft (68));
    envOptions[3]->setBounds (rowB.removeFromRight (68));
    r.removeFromLeft (12);
    envOptions[4]->setBounds (r.removeFromLeft (52).withSizeKeepingCentre (52, 24));
    r.removeFromLeft (6);
    if (E.sync && envOptions.size() >= 10)
    {
        envOptions[5]->setBounds (r.removeFromLeft (60).withSizeKeepingCentre (60, kh));
        auto c2 = r.removeFromLeft (100).withSizeKeepingCentre (100, 52);
        envOptions[6]->setBounds (c2.removeFromTop (22));
        c2.removeFromTop (6);
        for (int i = 7; i < 10; ++i)
        {
            envOptions[i]->setBounds (c2.removeFromLeft (32).withHeight (22));
            c2.removeFromLeft (2);
        }
    }
    else if (envOptions.size() >= 6)
        envOptions[5]->setBounds (r.removeFromLeft (62).withSizeKeepingCentre (62, kh));
}

void FxPage::syncValues()
{
    if (! bpmBox.isBeingEdited())
        bpmBox.setText (String (roundToInt (proc.getFxSetup().bpm)), dontSendNotification);
    for (auto* c : cards)
        c->sync();
    destinations->syncValues();
    syncEnvelopeOptions();   // buttons, boxes and knobs show the envelope as it is now
    graph->repaint();
}

void FxPage::tick()
{
    bpmLed.refresh();
    for (auto* c : cards)
        c->tick();
    graph->tick();
}

void FxPage::resized()
{
    auto r = getLocalBounds();
    chainArea = r.removeFromLeft (522).toFloat();
    r.removeFromLeft (8);
    envArea = r.toFloat();

    //== Chain side
    {
        auto c = chainArea.toNearestInt().reduced (11, 9);
        c.removeFromTop (26);
        auto row = c.removeFromTop (24);
        channelLabel = row.removeFromLeft (58).toFloat();
        channelBox.setBounds (row.removeFromLeft (176));
        row.removeFromLeft (8);
        addBox.setBounds (row.removeFromLeft (130));
        bpmLed.setBounds (row.removeFromRight (78));
        row.removeFromRight (5);
        bpmBox.setBounds (row.removeFromRight (40));
        c.removeFromTop (4);
        hintArea = c.removeFromTop (16).toFloat();
        c.removeFromTop (4);
        chainListArea = c.toFloat();
        chainView.setBounds (c.reduced (3, 4));
        const int w = jmax (100, chainView.getWidth() - chainView.getScrollBarThickness() - 4);
        for (auto* card : cards)
            card->setSize (w, card->getHeight());
        chainContent.setSize (w + 4, chainContent.getHeight());
    }

    //== Envelope side
    {
        auto e = envArea.toNearestInt().reduced (11, 9);
        e.removeFromTop (26);
        auto row = e.removeFromTop (24);
        envRemove.setBounds (row.removeFromRight (26));
        row.removeFromRight (4);
        envAdd.setBounds (row.removeFromRight (26));
        row.removeFromRight (10);
        if (! envTabs.isEmpty())
        {
            const int tw = jmin (28, (row.getWidth() + 3) / envTabs.size() - 3);
            for (auto* t : envTabs)
            {
                t->setBounds (row.removeFromLeft (tw));
                row.removeFromLeft (3);
            }
        }
        e.removeFromTop (6);
        auto row2 = e.removeFromTop (24);
        typeShape.setBounds (row2.removeFromLeft (62));
        row2.removeFromLeft (4);
        typeAhdsr.setBounds (row2.removeFromLeft (62));
        gridBox.setBounds (row2.removeFromRight (88));
        row2.removeFromRight (5);
        shapeBox.setBounds (row2.removeFromRight (120));
        e.removeFromTop (6);
        graphArea = e.removeFromTop (150).toFloat();
        graph->setBounds (graphArea.toNearestInt());
        e.removeFromTop (6);
        optionsArea = e.removeFromTop (84).toFloat();
        layoutEnvelopeOptions();
        e.removeFromTop (6);
        destArea = e.toFloat();
        destView.setBounds (e.reduced (3, 3));
        destinations->setSize (jmax (100, destView.getWidth() - destView.getScrollBarThickness()), destinations->getHeight());
    }
}

void FxPage::paint (Graphics& g)
{
    theme::drawPanel (g, chainArea.reduced (1.0f));
    theme::drawHeading (g, chainArea.reduced (10.0f, 9.0f), "FX chain", "top to bottom - right-click a knob to modulate it");
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 700, 0.1f));
    g.drawText ("CHANNEL", channelLabel, Justification::centredLeft, false);
    g.setFont (theme::font (7.5f, 500, 0.06f));
    g.drawText (channel == 0 ? "RUNS ON EVERYTHING, AFTER THE ZONE CHANNELS."
                             : "RUNS ONLY ON THIS ZONE, BEFORE THE WHOLE-PRESET CHAIN.",
                hintArea, Justification::centredLeft, true);
    theme::drawLed (g, chainListArea, {});
    if (cards.isEmpty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (9.0f, 500, 0.06f));
        g.drawFittedText ("NO EFFECTS ON THIS CHANNEL YET - USE \"+ ADD EFFECT\".\nTHEY RUN FROM TOP TO BOTTOM.",
                          chainListArea.reduced (12.0f, 10.0f).toNearestInt(), Justification::topLeft, 3, 1.0f);
    }

    theme::drawPanel (g, envArea.reduced (1.0f));
    theme::drawHeading (g, envArea.reduced (10.0f, 9.0f), "Envelopes", "modulation sources");
    theme::drawInset (g, optionsArea);
    theme::drawLed (g, destArea, {});
}
} // namespace emx
