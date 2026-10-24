// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EditorPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"
#include "FxPage.h"

namespace emx
{
using namespace juce;

namespace
{
int effectiveCrossfade (const ZoneParams& p)
{
    return jmax (0, jmin (p.crossfade, p.loopStart, (p.loopEnd - p.loopStart) / 2));
}

String timeText (double t) { return t < 1.0 ? String (roundToInt (t * 1000.0)) + " MS" : String (t, 2) + " S"; }

NormalisableRange<double> timeRange()
{
    return { 0.001, 10.0,
             [] (double, double, double n) { return 0.001 * std::pow (1.0e4, n); },
             [] (double, double, double v) { return std::log (jmax (0.001, v) / 0.001) / std::log (1.0e4); } };
}

const int whiteIndex[] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
const bool isBlack[]   { false, true, false, true, false, false, true, false, true, false, true, false };
} // namespace

//==================================================================================
// WaveformView
//==================================================================================
WaveformView::WaveformView (EmulonXProcessor& p) : proc (p) {}

const ModelZone* WaveformView::zone() const { return proc.getSelectedZone(); }

Range<double> WaveformView::view (const ModelZone& z)
{
    const double n = z.sample != nullptr ? z.sample->length() : 1.0;
    auto it = views.find (z.id);
    if (it == views.end() || it->second.getEnd() > n + 0.5)
        return views[z.id] = { 0.0, n };
    return it->second;
}

void WaveformView::setView (const ModelZone& z, double a, double b)
{
    const double n = z.sample != nullptr ? z.sample->length() : 1.0;
    const double w = jlimit (jmin (32.0, n), n, b - a);
    a = jlimit (0.0, n - w, a);
    views[z.id] = { a, a + w };
    repaint();
}

void WaveformView::zoomAll()
{
    if (auto* z = zone())
        setView (*z, 0.0, z->sample != nullptr ? z->sample->length() : 1.0);
}

void WaveformView::zoomAround (int frame, int halfWidth)
{
    if (auto* z = zone())
        setView (*z, frame - halfWidth, frame + halfWidth);
}

double WaveformView::frameAt (const ModelZone& z, float x)
{
    const auto v = view (z);
    return v.getStart() + (double) x / jmax (1, getWidth()) * v.getLength();
}

int WaveformView::hitMarker (const ModelZone& z, Point<float> pos)
{
    if (z.sample == nullptr)
        return -1;
    const auto v = view (z);
    const double spp = v.getLength() / jmax (1, getWidth());
    const auto px = [&] (int f) { return (float) ((f - v.getStart()) / spp); };
    const auto& p = z.params;
    const bool loopOn = p.loopMode != LoopMode::off;
    const bool upper = pos.y < (float) getHeight() * 0.5f;   // loop flags sit at the top, sample flags at the bottom
    int best = -1;
    float bestScore = 1.0e9f;
    const int frames[5] { p.loopStart, p.loopEnd, p.sampleStart, p.sampleEnd > 0 ? p.sampleEnd : z.sample->length(), p.randomEnd };
    for (int i = 0; i < 5; ++i)
    {
        if ((i < 2 && ! loopOn) || (i == 4 && ! p.randomStart))
            continue;
        const float d = std::abs (px (frames[i]) - pos.x);
        if (d >= 8.0f)
            continue;
        const float score = d + ((i < 2) == upper ? 0.0f : 6.0f);
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

int WaveformView::nearestZeroCrossing (const AudioBuffer<float>& b, int f)
{
    const int n = b.getNumSamples();
    const float* d = b.getReadPointer (0);
    for (int o = 0; o < 8192; ++o)
        for (int j : { f + o, f - o })
            if (j > 0 && j < n && d[j - 1] < 0.0f && d[j] >= 0.0f)
                return j;
    return f;
}

void WaveformView::tick()
{
    playheads.clear();
    if (auto* z = zone())
    {
        SamplerEngine::Playhead ph[16];
        const int count = proc.getPlayheads (ph, 16);
        for (int i = 0; i < count; ++i)
            if (ph[i].zoneId == z->id)
                playheads.push_back (ph[i].position);
    }
    if (playheads != lastPlayheads)
    {
        lastPlayheads = playheads;
        repaint();
    }
}

void WaveformView::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    theme::drawScreen (g, bounds);
    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (getLocalBounds().reduced (2));
        g.setColour (theme::red.withAlpha (0.07f));
        for (float x = 34.0f; x < bounds.getWidth(); x += 34.0f) g.fillRect (x, 0.0f, 1.0f, bounds.getHeight());
        for (float y = 34.0f; y < bounds.getHeight(); y += 34.0f) g.fillRect (0.0f, y, bounds.getWidth(), 1.0f);
    }

    const auto* z = zone();
    g.setFont (theme::font (9.0f, 500, 0.06f));
    if (z == nullptr || z->sample == nullptr)
    {
        g.setColour (theme::textMuted);
        g.drawText (z == nullptr ? "NO ZONE - ADD SAMPLES ON THE MAIN TAB OR DROP THEM HERE"
                                 : (z->problem.isNotEmpty() ? z->problem : String ("LOADING...")),
                    bounds.reduced (12.0f, 8.0f), Justification::topLeft, true);
        return;
    }

    Graphics::ScopedSaveState clipState (g);
    g.reduceClipRegion (getLocalBounds().reduced (1));

    const auto& audio = z->sample->audio;
    const auto& p = z->params;
    const int n = audio.getNumSamples(), chans = audio.getNumChannels();
    const auto v = view (*z);
    const float W = bounds.getWidth(), H = bounds.getHeight();
    const double spp = v.getLength() / W;
    const auto X = [&] (double f) { return (float) ((f - v.getStart()) / spp); };
    const float mid = H * 0.5f, amp = H * 0.5f - 8.0f;

    // Loop region and crossfade zones
    if (p.loopMode != LoopMode::off)
    {
        g.setColour (theme::hue (0x24ff3232));
        g.fillRect (Rectangle<float>::leftTopRightBottom (X (p.loopStart), 0.0f, X (p.loopEnd), H));
        if (const int xe = effectiveCrossfade (p); xe > 0)
        {
            g.setColour (theme::hue (0x21ffd2d2));
            g.fillRect (Rectangle<float>::leftTopRightBottom (X (p.loopEnd - xe), 0.0f, X (p.loopEnd), H));
            g.fillRect (Rectangle<float>::leftTopRightBottom (X (p.loopStart - xe), 0.0f, X (p.loopStart), H));
        }
    }
    g.setColour (theme::red.withAlpha (0.3f));
    g.fillRect (0.0f, mid, W, 1.0f);

    auto sampleAt = [&] (int j)
    {
        float s = 0.0f;
        for (int c = 0; c < chans; ++c)
            s += audio.getSample (c, j);
        return s / (float) chans;
    };

    if (spp >= 1.0)
    {
        // Min/max per pixel column, drawn twice: a soft glow, then the solid line.
        const int stride = jmax (1, (int) (spp / 200.0));
        RectangleList<float> cols;
        for (int i = 0; i < (int) W; ++i)
        {
            const int f0 = jmax (0, (int) (v.getStart() + i * spp));
            const int f1 = jmin (n, (int) (v.getStart() + (i + 1) * spp) + 1);
            float lo = 1.0f, hi = -1.0f;
            for (int c = 0; c < chans; ++c)
            {
                const float* d = audio.getReadPointer (c);
                for (int j = f0; j < f1; j += stride)
                {
                    lo = jmin (lo, d[j]);
                    hi = jmax (hi, d[j]);
                }
            }
            if (hi >= lo)
                cols.addWithoutMerging ({ (float) i, mid - hi * amp, 1.0f, jmax (1.0f, (hi - lo) * amp) });
        }
        g.setColour (theme::red.withAlpha (0.25f));
        for (auto& r : cols)
            g.fillRect (r.expanded (1.0f, 1.5f));
        g.setColour (theme::hue (0xffff3b3b));
        g.fillRectList (cols);
    }
    else
    {
        Path line;
        const int f0 = jmax (0, (int) v.getStart()), f1 = jmin (n - 1, (int) std::ceil (v.getEnd()));
        for (int j = f0; j <= f1; ++j)
        {
            const Point<float> pt (X (j), mid - sampleAt (j) * amp);
            if (j == f0) line.startNewSubPath (pt); else line.lineTo (pt);
        }
        g.setColour (theme::red.withAlpha (0.3f));
        g.strokePath (line, PathStrokeType (4.0f));
        g.setColour (theme::hue (0xffff3b3b));
        g.strokePath (line, PathStrokeType (1.5f));
        if (spp < 0.15)
        {
            g.setColour (theme::textStrong);
            for (int j = f0; j <= f1; ++j)
                g.fillRect (X (j) - 1.5f, mid - sampleAt (j) * amp - 1.5f, 3.0f, 3.0f);
        }
    }

    // Sample start / end: darken what is not played, amber markers with flags at the bottom
    {
        const int se = p.sampleEnd > 0 ? p.sampleEnd : n;
        g.setColour (Colours::black.withAlpha (0.5f));
        if (p.sampleStart > 0)
            g.fillRect (Rectangle<float>::leftTopRightBottom (jmax (0.0f, X (0)), 0.0f, X (p.sampleStart), H));
        if (se < n)
            g.fillRect (Rectangle<float>::leftTopRightBottom (X (se), 0.0f, jmin (W, X (n)), H));
        const Colour amber (0xffffb030);
        for (int which = 0; which < 2; ++which)
        {
            const float px = std::round (X (which == 0 ? p.sampleStart : se));
            if (px < -20.0f || px > W + 20.0f)
                continue;
            g.setColour (amber);
            g.fillRect (px - 1.0f, 0.0f, 2.0f, H);
            Path flag;
            if (which == 0) flag.addTriangle (px, H - 15.0f, px + 13.0f, H - 7.5f, px, H);
            else            flag.addTriangle (px, H - 15.0f, px - 13.0f, H - 7.5f, px, H);
            g.fillPath (flag);
        }
    }

    // Random start area: shaded from the sample start to its own (dashed) marker
    if (p.randomStart && p.randomEnd > p.sampleStart)
    {
        const Colour amber (0xffffb030);
        g.setColour (amber.withAlpha (0.16f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (X (p.sampleStart), 0.0f, X (p.randomEnd), H));
        const float px = std::round (X (p.randomEnd));
        if (px >= -20.0f && px <= W + 20.0f)
        {
            g.setColour (amber);
            for (float yy = 0.0f; yy < H; yy += 8.0f)
                g.fillRect (px - 1.0f, yy, 2.0f, 4.0f);
            Path diamond;
            diamond.addQuadrilateral (px, H - 16.0f, px + 6.0f, H - 9.0f, px, H - 2.0f, px - 6.0f, H - 9.0f);
            g.fillPath (diamond);
        }
    }

    // Loop markers
    if (p.loopMode != LoopMode::off)
    {
        g.setFont (theme::font (9.0f, 900, 0.0f));
        for (int which = 0; which < 2; ++which)
        {
            const float px = std::round (X (which == 0 ? p.loopStart : p.loopEnd));
            if (px < -20.0f || px > W + 20.0f)
                continue;
            g.setColour (Colours::white);
            g.fillRect (px - 1.0f, 0.0f, 2.0f, H);
            const auto flag = Rectangle<float> (which == 0 ? px : px - 15.0f, 0.0f, 15.0f, 15.0f);
            g.fillRect (flag);
            g.setColour (theme::hue (0xff990000));
            g.drawText (which == 0 ? "S" : "E", flag, Justification::centred, false);
        }
    }

    // Playheads
    g.setColour (theme::hue (0xffffd0d0));
    for (auto ph : playheads)
        if (const float px = X (ph); px >= 0.0f && px <= W)
            g.fillRect (px, 0.0f, 1.5f, H);

    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 500, 0.04f));
    g.drawText (String (roundToInt (v.getStart())), bounds.reduced (5.0f, 3.0f), Justification::bottomLeft, false);
    g.drawText (String (roundToInt (v.getEnd())), bounds.reduced (5.0f, 3.0f), Justification::bottomRight, false);
}

void WaveformView::mouseMove (const MouseEvent& e)
{
    const auto* z = zone();
    setMouseCursor (z != nullptr && hitMarker (*z, e.position) >= 0 ? MouseCursor::LeftRightResizeCursor
                                                                      : MouseCursor::DraggingHandCursor);
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    dragMode = -1;
    if (const auto* z = zone(); z != nullptr && z->sample != nullptr)
    {
        const int hit = hitMarker (*z, e.position);
        dragMode = hit >= 0 ? hit : 2;
        dragView = view (*z);
    }
}

void WaveformView::mouseDrag (const MouseEvent& e)
{
    const auto* z = zone();
    if (z == nullptr || z->sample == nullptr || dragMode < 0)
        return;
    if (dragMode == 2)
    {
        const double d = -(double) e.getDistanceFromDragStartX() * dragView.getLength() / jmax (1, getWidth());
        setView (*z, dragView.getStart() + d, dragView.getEnd() + d);
    }
    else if (onMarkerMoved)
    {
        const double spp = dragView.getLength() / jmax (1, getWidth());
        onMarkerMoved (dragMode, roundToInt (dragView.getStart() + e.position.x * spp));
    }
}

void WaveformView::mouseDoubleClick (const MouseEvent&) { zoomAll(); }

void WaveformView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto* z = zone();
    if (z == nullptr || z->sample == nullptr)
        return;
    const auto v = view (*z);
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const double d = -(e.mods.isShiftDown() ? w.deltaY : w.deltaX) * v.getLength() * 0.5;
        setView (*z, v.getStart() + d, v.getEnd() + d);
    }
    else if (std::abs (w.deltaY) > 0.0f)
    {
        const double fx = frameAt (*z, e.position.x);
        const double f = w.deltaY > 0 ? 0.8 : 1.25;
        setView (*z, fx - (fx - v.getStart()) * f, fx + (v.getEnd() - fx) * f);
    }
}

//==================================================================================
// KeymapView
//==================================================================================
KeymapView::KeymapView (EmulonXProcessor& p) : proc (p) {}

KeymapView::KeyRect KeymapView::keyRect (int n) const
{
    const int wi = (n / 12) * 7 + whiteIndex[n % 12];
    if (! isBlack[n % 12])
        return { (float) wi * keyW, keyW, false };
    const float bw = keyW * 0.62f;
    return { (float) (wi + 1) * keyW - bw * 0.5f, bw, true };
}

int KeymapView::noteAt (Point<float> pt) const
{
    if (pt.y < keysTop)
        return -1;
    const float keyH = (float) getHeight() - keysTop;
    if (pt.y < keysTop + keyH * 0.6f)
        for (int n = 0; n < 128; ++n)
            if (isBlack[n % 12])
                if (const auto k = keyRect (n); pt.x >= k.x && pt.x < k.x + k.w)
                    return n;
    for (int n = 0; n < 128; ++n)
        if (! isBlack[n % 12])
            if (const auto k = keyRect (n); pt.x >= k.x && pt.x < k.x + k.w)
                return n;
    return -1;
}

int KeymapView::velocityAt (float y, int note) const
{
    const float keyH = ((float) getHeight() - keysTop) * (isBlack[note % 12] ? 0.6f : 1.0f);
    return jlimit (1, 127, roundToInt (20.0f + 107.0f * (y - keysTop) / jmax (1.0f, keyH)));
}

void KeymapView::layoutLanes()
{
    keyW = (float) getWidth() / 75.0f;

    const auto& zs = proc.getZones();
    std::vector<size_t> order (zs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort (order.begin(), order.end(), [&zs] (size_t a, size_t b) { return zs[a].params.loKey < zs[b].params.loKey; });

    std::vector<int> laneEnd;
    lanes.assign (zs.size(), 0);
    for (auto i : order)
    {
        int l = 0;
        while (l < (int) laneEnd.size() && laneEnd[(size_t) l] >= zs[i].params.loKey)
            ++l;
        if (l == (int) laneEnd.size())
            laneEnd.push_back (-1);
        laneEnd[(size_t) l] = zs[i].params.hiKey;
        lanes[i] = l;
    }
    numLanes = jmax (1, (int) laneEnd.size());
    // The keyboard takes whatever the zone bars don't need (66 to 100 px tall).
    const float keysH = jlimit (66.0f, 100.0f, (float) getHeight() - ((float) jmax (2, numLanes) * 19.0f + 8.0f));
    keysTop = (float) getHeight() - keysH;
    laneH = jlimit (6.0f, 19.0f, (keysTop - 6.0f) / (float) numLanes);
}

Rectangle<float> KeymapView::barRect (size_t i) const
{
    const auto& p = proc.getZones()[i].params;
    const auto a = keyRect (p.loKey), b = keyRect (p.hiKey);
    return { a.x, 2.0f + (float) lanes[i] * laneH, jmax (6.0f, b.x + b.w - a.x), laneH - 2.0f };
}

void KeymapView::tick()
{
    std::bitset<128> now;
    for (int n = 0; n < 128; ++n)
        now[(size_t) n] = proc.keyboardState.isNoteOnForChannels (0xffff, n);
    if (now != held)
    {
        held = now;
        repaint (0, (int) keysTop, getWidth(), getHeight() - (int) keysTop);
    }
}

void KeymapView::paint (Graphics& g)
{
    const auto& zs = proc.getZones();
    if (lanes.size() != zs.size())
        layoutLanes();
    const auto* sel = proc.getSelectedZone();

    // Zone bars
    if (zs.empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (9.0f, 500, 0.06f));
        g.drawText ("NO ZONES YET", Rectangle<float> (8.0f, 0.0f, (float) getWidth() - 16.0f, keysTop), Justification::centredLeft, false);
    }
    for (int pass = 0; pass < 2; ++pass)   // selected bar last, on top
        for (size_t i = 0; i < zs.size(); ++i)
        {
            const bool on = sel != nullptr && zs[i].id == sel->id;
            if (on != (pass == 1))
                continue;
            const auto r = barRect (i);
            if (on)
            {
                DropShadow (theme::red.withAlpha (0.7f), 8, {}).drawForRectangle (g, r.toNearestInt());
                g.setGradientFill (ColourGradient::vertical (theme::hue (0xffff4a4a), r.getY(), theme::hue (0xffa50f0f), r.getBottom()));
            }
            else
                g.setGradientFill (ColourGradient::vertical (theme::hue (0xff7a1414), r.getY(), theme::hue (0xff3a0606), r.getBottom()));
            g.fillRoundedRectangle (r, 3.5f);
            g.setColour ((on ? theme::hue (0xffffb0b0) : theme::hue (0x59ff7878)));
            g.fillRect (r.getX() + 3.0f, r.getY() + 1.0f, r.getWidth() - 6.0f, 1.0f);
            g.setColour (Colours::black);
            g.drawRoundedRectangle (r, 3.5f, 1.0f);

            const auto& p = zs[i].params;
            if (p.root >= p.loKey && p.root <= p.hiKey)
            {
                const auto k = keyRect (p.root);
                g.setColour (Colours::white);
                g.fillRect (k.x + k.w * 0.5f - 1.0f, r.getY() + 2.0f, 2.0f, r.getHeight() - 4.0f);
            }
            if (on)
            {
                g.setColour (Colours::white.withAlpha (0.28f));
                g.fillRect (r.withWidth (5.0f).reduced (0.0f, 1.0f));
                g.fillRect (r.withLeft (r.getRight() - 5.0f).reduced (0.0f, 1.0f));
            }
            if (laneH >= 11.0f)
            {
                g.setColour (theme::isXp() || theme::isLightSkin() ? theme::textOn (on ? theme::hue (0xffff4a4a) : theme::hue (0xff7a1414)) : (on ? Colours::white : theme::hue (0xffffb0b0)));
                g.setFont (theme::font (jmin (8.0f, laneH * 0.5f), 700, 0.06f));
                g.drawText (zs[i].name, r.reduced (8.0f, 0.0f), Justification::centredLeft, true);
            }
        }

    // Keyboard
    const float keyH = (float) getHeight() - keysTop;
    for (int black = 0; black < 2; ++black)
        for (int n = 0; n < 128; ++n)
        {
            if (isBlack[n % 12] != (black == 1))
                continue;
            const auto k = keyRect (n);
            const auto r = Rectangle<float> (k.x, keysTop, k.w, keyH * (k.black ? 0.6f : 1.0f));
            const bool down = held[(size_t) n];
            if (k.black)
                g.setGradientFill (ColourGradient::vertical (down ? theme::hue (0xffff3a3a) : Colour (0xff3a373f), r.getY(),   // real piano colours in every skin
                                                             down ? theme::hue (0xff7a0808) : Colour (0xff09080b), r.getBottom()));
            else
                g.setGradientFill (ColourGradient::vertical (down ? theme::hue (0xffff6a6a) : Colour (theme::isMetal() ? 0xffc6d6e6 : 0xffcfc9c9), r.getY(),   // EMUL8R: bluish keys
                                                             down ? theme::hue (0xffc01818) : Colour (theme::isMetal() ? 0xfff1f6fb : 0xfff2eeee), r.getBottom()));
            g.fillRect (r);
            g.setColour (Colours::black);
            g.drawRect (r, 1.0f);

            if (sel != nullptr && n >= sel->params.loKey && n <= sel->params.hiKey)
            {
                g.setColour (theme::red);
                g.fillRect (r.withHeight (4.0f).reduced (1.0f, 0.0f));
            }
            if (sel != nullptr && n == sel->params.root)
            {
                g.setColour (theme::red);
                g.fillEllipse (r.getCentreX() - 3.0f, r.getBottom() - (k.black ? 10.0f : 16.0f), 6.0f, 6.0f);
            }
            if (n % 12 == 0)
            {
                g.setColour (Colour (0xff444444));
                g.setFont (theme::font (6.5f, 700, 0.0f));
                g.drawText (noteName (n), r.withTrimmedBottom (2.0f), Justification::centredBottom, false);
            }
        }
}

void KeymapView::mouseMove (const MouseEvent& e)
{
    const auto* sel = proc.getSelectedZone();
    for (size_t i = 0; i < proc.getZones().size(); ++i)
        if (barRect (i).contains (e.position))
        {
            const auto r = barRect (i);
            const bool edge = sel != nullptr && proc.getZones()[i].id == sel->id
                              && (e.position.x < r.getX() + 6.0f || e.position.x > r.getRight() - 6.0f);
            setMouseCursor (edge ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor);
            return;
        }
    setMouseCursor (MouseCursor::NormalCursor);
}

void KeymapView::mouseDown (const MouseEvent& e)
{
    drag = Drag::none;
    const auto& zs = proc.getZones();

    if (e.position.y < keysTop)
    {
        const auto* sel = proc.getSelectedZone();
        // Selected bar first (it is drawn on top), then the others.
        for (int pass = 0; pass < 2 && drag == Drag::none; ++pass)
            for (size_t i = 0; i < zs.size(); ++i)
            {
                const bool isSel = sel != nullptr && zs[i].id == sel->id;
                if (isSel != (pass == 0) || ! barRect (i).contains (e.position))
                    continue;
                const auto r = barRect (i);
                drag = isSel && e.position.x < r.getX() + 6.0f ? Drag::resizeLo
                     : isSel && e.position.x > r.getRight() - 6.0f ? Drag::resizeHi : Drag::move;
                dragZone = zs[i].id;
                dragLo = zs[i].params.loKey;
                dragHi = zs[i].params.hiKey;
                proc.selectZone (dragZone);
                break;
            }
        return;
    }

    const int note = noteAt (e.position);
    if (note < 0)
        return;
    if (e.mods.isShiftDown())
    {
        if (const auto* sel = proc.getSelectedZone())
        {
            auto p = sel->params;
            p.root = note;
            proc.setZoneParams (sel->id, p);
        }
        return;
    }
    drag = Drag::play;
    playingNote = note;
    proc.keyboardState.noteOn (proc.getKeyboardChannel(), note, (float) velocityAt (e.position.y, note) / 127.0f);
}

void KeymapView::mouseDrag (const MouseEvent& e)
{
    if (drag == Drag::play)
    {
        const int note = noteAt (e.position);
        if (note >= 0 && note != playingNote)
        {
            proc.keyboardState.noteOff (proc.getKeyboardChannel(), playingNote, 0.0f);
            playingNote = note;
            proc.keyboardState.noteOn (proc.getKeyboardChannel(), note, (float) velocityAt (e.position.y, note) / 127.0f);
        }
        return;
    }
    if (drag == Drag::none)
        return;

    const ModelZone* z = nullptr;
    for (auto& mz : proc.getZones())
        if (mz.id == dragZone)
            z = &mz;
    if (z == nullptr)
        return;
    auto p = z->params;
    if (drag == Drag::move)
    {
        const int d = jlimit (-dragLo, 127 - dragHi, roundToInt ((float) e.getDistanceFromDragStartX() / (keyW * 7.0f / 12.0f)));
        p.loKey = dragLo + d;
        p.hiKey = dragHi + d;
    }
    else
    {
        int note = noteAt ({ e.position.x, keysTop + 1.0f });
        if (note < 0)
            note = e.position.x < 0 ? 0 : 127;
        if (drag == Drag::resizeLo) p.loKey = jmin (note, p.hiKey);
        else                        p.hiKey = jmax (note, p.loKey);
    }
    if (p.loKey != z->params.loKey || p.hiKey != z->params.hiKey || p.root != z->params.root)
        proc.setZoneParams (dragZone, p);
}

void KeymapView::mouseUp (const MouseEvent&)
{
    if (drag == Drag::play && playingNote >= 0)
        proc.keyboardState.noteOff (proc.getKeyboardChannel(), playingNote, 0.0f);
    playingNote = -1;
    drag = Drag::none;
}

//==================================================================================
// Horizontal slider for a position in the sample (in frames)
//==================================================================================
class EditorPage::FrameSlider final : public Component, public SettableTooltipClient
{
public:
    explicit FrameSlider (String labelText) : label (std::move (labelText)) {}

    std::function<void (int)> onChange;
    void setRange (int maximum) { maxValue = jmax (1, maximum); repaint(); }
    void setValue (int v) { if (v != value) { value = v; repaint(); } }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (isEnabled() ? theme::textMuted : theme::textMuted.withAlpha (0.4f));
        g.setFont (theme::font (7.5f, 700, 0.08f));
        g.drawText (label, r.removeFromLeft (labelW), Justification::centredLeft, true);
        track = r;
        theme::drawLed (g, r, {});
        const float pos = (float) jlimit (0.0, 1.0, (double) value / maxValue);
        auto fill = r.reduced (2.0f, 3.0f);
        const float x = fill.getX() + fill.getWidth() * pos;
        const float alpha = isEnabled() ? 1.0f : 0.35f;
        g.setGradientFill (ColourGradient::horizontal (theme::hue (0xff5a0808).withAlpha (alpha), fill.getX(),
                                                       theme::hue (0xffd02a2a).withAlpha (alpha), jmax (fill.getX() + 1.0f, x)));
        g.fillRoundedRectangle (fill.withRight (x), 2.5f);
        g.setColour (theme::textStrong.withAlpha (alpha));
        g.fillRoundedRectangle (x - 1.5f, r.getY() + 1.0f, 3.0f, r.getHeight() - 2.0f, 1.5f);
        g.setColour (theme::hue (0xffffd0d0).withAlpha (alpha));
        g.setFont (theme::font (7.5f, 500, 0.04f));
        g.drawText (String (value), r.reduced (6.0f, 0.0f), Justification::centredRight, false);
    }

    void mouseDown (const MouseEvent& e) override { dragStart = value; set (e, true); }
    void mouseDrag (const MouseEvent& e) override { set (e, false); }

private:
    void set (const MouseEvent& e, bool first)
    {
        if (! isEnabled() || track.getWidth() <= 4.0f)
            return;
        int v;
        if (e.mods.isShiftDown())   // fine: drag moves 1/20 as far
        {
            if (first) return;
            v = dragStart + roundToInt ((double) e.getDistanceFromDragStartX() / track.getWidth() * maxValue / 20.0);
        }
        else
            v = roundToInt ((double) (e.position.x - track.getX()) / track.getWidth() * maxValue);
        v = jlimit (0, maxValue, v);
        if (v != value && onChange)
            onChange (v);
    }

    String label;
    int value = 0, maxValue = 1, dragStart = 0;
    static constexpr float labelW = 44.0f;
    Rectangle<float> track;
};

//==================================================================================
// EditorPage
//==================================================================================
EditorPage::EditorPage (EmulonXProcessor& p)
    : proc (p),
      zoneName ([this]
      {
          const auto* z = proc.getSelectedZone();
          if (z == nullptr)
              return String ("NO ZONE");
          return "ZONE " + String (proc.getSelectedIndex() + 1) + "/" + String ((int) proc.getZones().size()) + "  -  " + z->name;
      }, 10.0f),
      wave (p),
      loopInfo ([this]
      {
          const auto* z = proc.getSelectedZone();
          if (z == nullptr || z->sample == nullptr)
              return String ("-");
          const auto& q = z->params;
          const int len = q.loopEnd - q.loopStart, xe = effectiveCrossfade (q);
          String t = q.loopMode == LoopMode::off ? String ("NO LOOP") : q.loopMode == LoopMode::loop ? String ("LOOP") : String ("LOOP UNTIL RELEASE");
          t << "  -  LENGTH " << len << " (" << String (len / z->sample->sampleRate * 1000.0, 1) << " MS)";
          if (q.crossfade > 0)
              t << "  -  X-FADE " << xe << (xe < q.crossfade ? " (LIMITED: NEEDS AUDIO BEFORE THE LOOP START)" : "");
          return t;
      }, 9.0f),
      keymap (p)
{
    //== Top bar ===================================================================
    prevButton.setTooltip ("Previous zone");
    nextButton.setTooltip ("Next zone");
    prevButton.onClick = [this] { proc.selectNextZone (-1); };
    nextButton.onClick = [this] { proc.selectNextZone (1); };
    dupButton.setTooltip ("Copy this zone (same sample and settings)");
    dupButton.onClick = [this] { proc.duplicateZone (proc.getSelectedZoneId()); };
    deleteButton.setTooltip ("Remove this zone");
    deleteButton.onClick = [this] { proc.removeZone (proc.getSelectedZoneId()); };
    mapButton.setTooltip ("Spread the zones across the keyboard by their root notes");
    mapButton.onClick = [this] { proc.autoMap(); };
    undoButton.setTooltip ("Undo the last zone change (Ctrl+Z)");
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.setTooltip ("Redo (Ctrl+Shift+Z)");
    redoButton.onClick = [this] { proc.redo(); };
    for (int i = 0; i < 3; ++i)
    {
        interpButtons[i].setTooltip ("Interpolation for the whole preset: Linear is lightest on the CPU, Sinc HQ sounds cleanest");
        interpButtons[i].onClick = [this, i] { proc.setInterpolation ((Interp) i); };
    }

    //== Sample & loop =============================================================
    wave.onMarkerMoved = [this] (int which, int frame) { setLoopPoint (which, frame, false); };
    const char* loopTips[] { "No loop: the sample plays once", "Loop while the note is held and during the release",
                             "Loop while the key is held, then play on to the end of the sample" };
    for (int i = 0; i < 3; ++i)
    {
        loopButtons[i].setTooltip (loopTips[i]);
        loopButtons[i].onClick = [this, i] { edit ([i] (ZoneParams& q) { q.loopMode = (LoopMode) i; }); };
    }
    for (auto* box : { &startBox, &endBox, &fadeBox })
    {
        box->setJustificationType (Justification::centred);
        box->setTooltip ("Click to type an exact value in sample frames");
        box->onEditorShow = [box] { if (auto* ed = box->getCurrentTextEditor()) ed->setInputRestrictions (9, "0123456789"); };
    }
    startBox.onTextChange = [this] { setLoopPoint (0, startBox.getText().getIntValue(), true); };
    endBox.onTextChange = [this] { setLoopPoint (1, endBox.getText().getIntValue(), true); };
    fadeBox.onTextChange = [this]
    {
        const int v = fadeBox.getText().getIntValue();
        edit ([v] (ZoneParams& q) { q.crossfade = jlimit (0, q.loopEnd - q.loopStart, v); });
    };
    zeroStart.setTooltip ("Move the loop start to the nearest zero crossing (avoids clicks)");
    zeroEnd.setTooltip ("Move the loop end to the nearest zero crossing (avoids clicks)");
    zeroStart.onClick = [this]
    {
        if (auto* z = proc.getSelectedZone(); z != nullptr && z->sample != nullptr)
            setLoopPoint (0, WaveformView::nearestZeroCrossing (z->sample->audio, z->params.loopStart), true);
    };
    zeroEnd.onClick = [this]
    {
        if (auto* z = proc.getSelectedZone(); z != nullptr && z->sample != nullptr)
            setLoopPoint (1, WaveformView::nearestZeroCrossing (z->sample->audio, z->params.loopEnd), true);
    };
    snapButton.setTooltip ("Dragged loop markers jump to zero crossings");
    snapButton.onClick = [this] { wave.snapToZero = ! wave.snapToZero; snapButton.setToggleState (wave.snapToZero, dontSendNotification); };
    zoomAllButton.setTooltip ("Show the whole sample (or double-click the waveform)");
    zoomAllButton.onClick = [this] { wave.zoomAll(); };
    zoomStartButton.setTooltip ("Zoom in on the loop start");
    zoomStartButton.onClick = [this] { if (auto* z = proc.getSelectedZone()) wave.zoomAround (z->params.loopStart); };
    zoomEndButton.setTooltip ("Zoom in on the loop end");
    zoomEndButton.onClick = [this] { if (auto* z = proc.getSelectedZone()) wave.zoomAround (z->params.loopEnd); };

    const char* sliderNames[] { "START", "END", "L START", "L END", "X-FADE" };
    const char* sliderTips[] { "Where the sample starts playing (amber marker) - shift-drag for fine moves",
                               "Where the sample stops when it isn't looping (amber marker)",
                               "Loop start (white S flag)", "Loop end (white E flag)", "Crossfade length at the loop seam" };
    for (int i = 0; i < 5; ++i)
    {
        sliders[i] = std::make_unique<FrameSlider> (sliderNames[i]);
        sliders[i]->setTooltip (sliderTips[i]);
        addAndMakeVisible (*sliders[i]);
    }
    sliders[0]->onChange = [this] (int v) { setLoopPoint (2, v, true); };
    sliders[1]->onChange = [this] (int v) { setLoopPoint (3, v, true); };
    sliders[2]->onChange = [this] (int v) { setLoopPoint (0, v, true); };
    sliders[3]->onChange = [this] (int v) { setLoopPoint (1, v, true); };
    sliders[4]->onChange = [this] (int v) { edit ([v] (ZoneParams& q) { q.crossfade = jlimit (0, q.loopEnd - q.loopStart, v); }); };
    randomSlider = std::make_unique<FrameSlider> ("RND TO");
    randomSlider->setTooltip ("End of the random start area (dashed amber marker): each note starts somewhere between SMP START and here");
    randomSlider->onChange = [this] (int v) { setLoopPoint (4, v, true); };
    addAndMakeVisible (*randomSlider);
    randomButton.setTooltip ("Random start: every note starts at a different spot inside the shaded area, so repeated notes sound less alike");
    randomButton.onClick = [this]
    {
        edit ([] (ZoneParams& q)
        {
            q.randomStart = ! q.randomStart;
            if (q.randomStart && q.randomEnd <= q.sampleStart)   // first time: a quarter of the sample
                q.randomEnd = q.sampleStart + jmax (1, ((q.sampleEnd > 0 ? q.sampleEnd : q.loopEnd) - q.sampleStart) / 4);
        });
    };
    addAndMakeVisible (randomButton);

    //== Zone settings =============================================================
    auto noteFmt = [] (double v) { return noteName (roundToInt (v)); };
    auto intRange = [] (int lo, int hi) { return NormalisableRange<double> (lo, hi, 1.0); };

    root.setRange (intRange (0, 127), 60);   root.format = noteFmt;
    loKey.setRange (intRange (0, 127), 0);   loKey.format = noteFmt;
    hiKey.setRange (intRange (0, 127), 127); hiKey.format = noteFmt;
    loVel.setRange (intRange (1, 127), 1);   loVel.format = [] (double v) { return String (roundToInt (v)); };
    hiVel.setRange (intRange (1, 127), 127); hiVel.format = loVel.format;
    root.onChange  = [this] (double v) { edit ([v] (ZoneParams& q) { q.root = roundToInt (v); }); };
    loKey.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.loKey = jmin (roundToInt (v), q.hiKey); }); };
    hiKey.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.hiKey = jmax (roundToInt (v), q.loKey); }); };
    loVel.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.loVel = jmin (roundToInt (v), q.hiVel); }); };
    hiVel.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.hiVel = jmax (roundToInt (v), q.loVel); }); };
    root.slider.setTooltip ("The note at which the sample plays at its original pitch (or shift-click a key)");
    loKey.slider.setTooltip ("Lowest key this zone plays on");
    hiKey.slider.setTooltip ("Highest key this zone plays on");
    loVel.slider.setTooltip ("Softest velocity this zone answers to (for velocity layers)");
    hiVel.slider.setTooltip ("Hardest velocity this zone answers to");

    semi.setRange (intRange (-24, 24), 0);
    semi.format = [] (double v) { const int st = roundToInt (v); return (st > 0 ? "+" : "") + String (st) + " ST"; };
    semi.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.semitones = roundToInt (v); }); };
    semi.slider.setTooltip ("Pitch in semitones (12 = one octave) - right-click to move it with an envelope, macro or the mod wheel");
    auto semiTarget = [this] { return fx::Target { fx::Target::zonePitch, proc.getSelectedZoneId() }; };
    semi.slider.onRightClick = [this, semiTarget] { if (proc.getSelectedZone() != nullptr) modui::showMenu (proc, semiTarget(), &semi); };
    semi.slider.interceptClick = [this, semiTarget] { return proc.getSelectedZone() != nullptr && modui::tryLearn (proc, semiTarget()); };
    semi.modRings = [this, semiTarget] { return modui::rings (proc, semiTarget(), (float) ((semi.getValue() + 24.0) / 48.0)); };
    for (auto* k : { &semi, &tune, &grain })
        k->setRingMargin (6);

    tune.setRange (intRange (-100, 100), 0);
    tune.format = [] (double v) { const int c = roundToInt (v); return (c > 0 ? "+" : "") + String (c) + " CT"; };
    tune.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.tuneCents = (float) roundToInt (v); }); };
    tune.slider.setTooltip ("Fine tuning in cents (100 cents = 1 semitone)");
    grain.setRange (intRange (20, 150), 60);
    grain.format = [] (double v) { return String (roundToInt (v)) + " MS"; };
    grain.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.grainMs = (float) roundToInt (v); }); };
    grain.slider.setTooltip ("Keep speed grain size: short for drums and speech, long for pads");
    keepSpeed.setTooltip ("Pitch this zone without changing its length, like the Emulator X");
    keepSpeed.onClick = [this] { edit ([] (ZoneParams& q) { q.keepSpeed = ! q.keepSpeed; }); };

    gain.setRange (NormalisableRange<double> (-24.0, 12.0, 0.5), 0.0);
    gain.format = [] (double v) { return (v > 0 ? "+" : "") + String (v, 1) + " DB"; };
    gain.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.gainDb = (float) v; }); };
    pan.setRange (intRange (-100, 100), 0);
    pan.format = [] (double v) { const int c = roundToInt (v); return c == 0 ? String ("C") : (c < 0 ? "L" + String (-c) : "R" + String (c)); };
    pan.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.pan = (float) roundToInt (v); }); };

    for (auto* t : { &attack, &decay, &release })
    {
        t->setRange (timeRange(), 0.25);
        t->format = timeText;
    }
    attack.slider.setDoubleClickReturnValue (true, 0.002);
    decay.slider.setDoubleClickReturnValue (true, 0.3);
    attack.onChange  = [this] (double v) { edit ([v] (ZoneParams& q) { q.attack = (float) v; }); };
    decay.onChange   = [this] (double v) { edit ([v] (ZoneParams& q) { q.decay = (float) v; }); };
    release.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.release = (float) v; }); };
    sustain.setRange (NormalisableRange<double> (0.0, 1.0, 0.001), 1.0);
    sustain.format = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    sustain.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.sustain = (float) v; }); };
    envCopyBox.setTextWhenNothingSelected ("COPY FROM...");
    envCopyBox.setTextWhenNoChoicesAvailable ("ONLY ONE ZONE");
    envCopyBox.setTooltip ("Copy attack, decay, sustain and release from another zone - or give every zone this envelope");
    envCopyBox.onChange = [this]
    {
        const int id = envCopyBox.getSelectedId();
        envCopyBox.setSelectedId (0, dontSendNotification);
        const auto* z = proc.getSelectedZone();
        if (id == 0 || z == nullptr)
            return;
        if (id == allZonesItem)
            proc.copyEnvelope (z->id, -1);   // this envelope -> every zone
        else
            proc.copyEnvelope (id, z->id);   // that zone's envelope -> this one
    };
    addAndMakeVisible (envCopyBox);
    attack.slider.setTooltip ("Fade-in time when a key is pressed");
    decay.slider.setTooltip ("Time to fall to the sustain level");
    sustain.slider.setTooltip ("Level while the key is held");
    release.slider.setTooltip ("Fade-out time after the key is let go");

    for (Component* c : { (Component*) &prevButton, (Component*) &nextButton, (Component*) &zoneName, (Component*) &dupButton,
                          (Component*) &deleteButton, (Component*) &mapButton, (Component*) &undoButton, (Component*) &redoButton, (Component*) &interpButtons[0],
                          (Component*) &interpButtons[1], (Component*) &interpButtons[2], (Component*) &wave,
                          (Component*) &loopButtons[0], (Component*) &loopButtons[1], (Component*) &loopButtons[2],
                          (Component*) &startBox, (Component*) &endBox, (Component*) &fadeBox, (Component*) &zeroStart,
                          (Component*) &zeroEnd, (Component*) &snapButton, (Component*) &zoomAllButton,
                          (Component*) &zoomStartButton, (Component*) &zoomEndButton, (Component*) &loopInfo,
                          (Component*) &root, (Component*) &loKey, (Component*) &hiKey, (Component*) &loVel, (Component*) &hiVel,
                          (Component*) &semi, (Component*) &tune, (Component*) &keepSpeed, (Component*) &grain, (Component*) &gain, (Component*) &pan,
                          (Component*) &attack, (Component*) &decay, (Component*) &sustain, (Component*) &release, (Component*) &keymap,
                          (Component*) &polyButton, (Component*) &monoButton, (Component*) &legatoButton, (Component*) &glide })
        addAndMakeVisible (c);

    //== Voice mode ================================================================
    polyButton.setTooltip ("Play chords: every note gets its own voice");
    monoButton.setTooltip ("One note at a time (for leads and basses); letting go returns to a key you still hold");
    legatoButton.setTooltip ("Mono: overlapping notes slide without restarting the sample or envelope (glide only then)");
    polyButton.onClick = [this] { auto v = proc.getVoiceSettings(); v.mono = false; proc.setVoiceSettings (v); };
    monoButton.onClick = [this] { auto v = proc.getVoiceSettings(); v.mono = true; proc.setVoiceSettings (v); };
    legatoButton.onClick = [this] { auto v = proc.getVoiceSettings(); v.legato = ! v.legato; proc.setVoiceSettings (v); };
    glide.setRange (NormalisableRange<double> (0.0, 2.0,
                        [] (double, double, double n) { return n < 0.01 ? 0.0 : 0.005 * std::pow (400.0, n); },
                        [] (double, double, double v) { return v <= 0.0 ? 0.0 : jlimit (0.0, 1.0, std::log (v / 0.005) / std::log (400.0)); }),
                    0.0);
    glide.format = [] (double t) { return t <= 0.0 ? String ("OFF") : t < 1.0 ? String (roundToInt (t * 1000.0)) + " MS" : String (t, 2) + " S"; };
    glide.onChange = [this] (double v) { auto s = proc.getVoiceSettings(); s.glide = (float) v; proc.setVoiceSettings (s); };
    glide.slider.setTooltip ("Mono: time for the pitch to slide to the next note"); 

    proc.addChangeListener (this);
    settings->addChangeListener (this);
    refresh();
}

EditorPage::~EditorPage()
{
    proc.removeChangeListener (this);
    settings->removeChangeListener (this);
}

void EditorPage::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void EditorPage::edit (std::function<void (ZoneParams&)> change)
{
    if (const auto* z = proc.getSelectedZone())
    {
        auto q = z->params;
        change (q);
        proc.setZoneParams (z->id, q);
    }
}

void EditorPage::setLoopPoint (int which, int frame, bool exact)
{
    const auto* z = proc.getSelectedZone();
    if (z == nullptr || z->sample == nullptr)
        return;
    const int n = z->sample->length();
    if (wave.snapToZero && ! exact)
        frame = WaveformView::nearestZeroCrossing (z->sample->audio, frame);
    auto q = z->params;
    const int se = q.sampleEnd > 0 ? q.sampleEnd : n;
    if (which == 0)      q.loopStart = jlimit (0, jmax (0, q.loopEnd - 16), frame);
    else if (which == 1) q.loopEnd = jlimit (jmin (n, q.loopStart + 16), n, frame);
    else if (which == 2) q.sampleStart = jlimit (0, jmax (0, se - 16), frame);
    else if (which == 3) q.sampleEnd = jlimit (jmin (n, q.sampleStart + 16), n, frame);
    else                 q.randomEnd = jlimit (q.sampleStart, jmax (q.sampleStart, se - 1), frame);
    if (q.loopStart != z->params.loopStart || q.loopEnd != z->params.loopEnd || q.randomEnd != z->params.randomEnd
        || q.sampleStart != z->params.sampleStart || q.sampleEnd != z->params.sampleEnd)
        proc.setZoneParams (z->id, q);
}

void EditorPage::refresh()
{
    const auto* z = proc.getSelectedZone();
    const bool has = z != nullptr, hasAudio = has && z->sample != nullptr;

    // envelope copy list: every other zone, plus "to all zones"
    envCopyBox.clear (dontSendNotification);
    if (has && proc.getZones().size() > 1)
    {
        envCopyBox.addSectionHeading ("Copy the envelope from");
        int n = 0;
        for (auto& other : proc.getZones())
        {
            ++n;
            if (other.id == z->id)
                continue;
            const auto& q = other.params;
            envCopyBox.addItem ("ZONE " + String (n) + "  -  " + other.name + "    (" + timeText (q.attack) + " / " + timeText (q.decay)
                                    + " / " + String (roundToInt (q.sustain * 100.0f)) + "% / " + timeText (q.release) + ")",
                                other.id);
        }
        envCopyBox.addSeparator();
        envCopyBox.addItem ("GIVE EVERY ZONE THIS ENVELOPE", allZonesItem);
    }
    envCopyBox.setEnabled (has && proc.getZones().size() > 1);

    zoneName.refresh();
    loopInfo.refresh();
    for (int i = 0; i < 3; ++i)
    {
        interpButtons[i].setToggleState ((int) proc.getInterpolation() == i, dontSendNotification);
        loopButtons[i].setToggleState (has && (int) z->params.loopMode == i, dontSendNotification);
        loopButtons[i].setEnabled (hasAudio);
    }
    for (auto* b : { &prevButton, &nextButton })
        b->setEnabled (proc.getZones().size() > 1);
    for (auto* b : { &dupButton, &deleteButton, &mapButton })
        b->setEnabled (has);
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());

    const bool looping = hasAudio && z->params.loopMode != LoopMode::off;
    for (Component* c : { (Component*) &startBox, (Component*) &endBox, (Component*) &fadeBox, (Component*) &zeroStart,
                          (Component*) &zeroEnd, (Component*) &zoomStartButton, (Component*) &zoomEndButton })
        c->setEnabled (looping);
    zoomAllButton.setEnabled (hasAudio);
    snapButton.setToggleState (wave.snapToZero, dontSendNotification);

    if (hasAudio)
    {
        const auto& q = z->params;
        const int n = z->sample->length();
        for (int i = 0; i < 4; ++i)
            sliders[i]->setRange (n);
        sliders[4]->setRange (jmax (1, q.loopEnd - q.loopStart));
        sliders[0]->setValue (q.sampleStart);
        sliders[1]->setValue (q.sampleEnd > 0 ? q.sampleEnd : n);
        sliders[2]->setValue (q.loopStart);
        sliders[3]->setValue (q.loopEnd);
        sliders[4]->setValue (q.crossfade);
        randomSlider->setRange (n);
        randomSlider->setValue (q.randomEnd);
    }
    randomButton.setToggleState (hasAudio && z->params.randomStart, dontSendNotification);
    randomButton.setEnabled (hasAudio);
    randomSlider->setEnabled (hasAudio && z->params.randomStart);
    randomSlider->repaint();
    sliders[0]->setEnabled (hasAudio);
    sliders[1]->setEnabled (hasAudio);
    for (int i = 2; i < 5; ++i)
        sliders[i]->setEnabled (looping);
    for (auto& sl : sliders)
        sl->repaint();

    auto setBox = [] (LedLabel& box, const String& t) { if (! box.isBeingEdited()) box.setText (t, dontSendNotification); };
    setBox (startBox, hasAudio ? String (z->params.loopStart) : String());
    setBox (endBox, hasAudio ? String (z->params.loopEnd) : String());
    setBox (fadeBox, hasAudio ? String (z->params.crossfade) : String());

    for (Component* c : { (Component*) &root, (Component*) &loKey, (Component*) &hiKey, (Component*) &loVel, (Component*) &hiVel,
                          (Component*) &semi, (Component*) &tune, (Component*) &keepSpeed, (Component*) &gain, (Component*) &pan,
                          (Component*) &attack, (Component*) &decay, (Component*) &sustain, (Component*) &release })
        c->setEnabled (has);
    if (has)
    {
        const auto& q = z->params;
        root.setValue (q.root);   loKey.setValue (q.loKey); hiKey.setValue (q.hiKey);
        loVel.setValue (q.loVel); hiVel.setValue (q.hiVel);
        tune.setValue (q.tuneCents);
        semi.setValue (q.semitones);
        grain.setValue (q.grainMs);
        keepSpeed.setToggleState (q.keepSpeed, dontSendNotification);
        gain.setValue (q.gainDb); pan.setValue (q.pan);
        attack.setValue (q.attack); decay.setValue (q.decay); sustain.setValue (q.sustain); release.setValue (q.release);
    }
    grain.setEnabled (has && z->params.keepSpeed);

    const auto& vs = proc.getVoiceSettings();
    polyButton.setToggleState (! vs.mono, dontSendNotification);
    monoButton.setToggleState (vs.mono, dontSendNotification);
    legatoButton.setToggleState (vs.legato, dontSendNotification);
    legatoButton.setEnabled (vs.mono);
    glide.setValue (vs.glide);
    glide.setEnabled (vs.mono);

    const int sens = settings->getKnobSensitivity();
    for (auto* c : { &root, &loKey, &hiKey, &loVel, &hiVel, &semi, &tune, &grain, &gain, &pan })
        c->setSensitivity (sens);

    keymap.layoutLanes();
    keymap.repaint();
    wave.repaint();
    repaint();
}

void EditorPage::tick()
{
    wave.tick();
    keymap.tick();
    if (! proc.getFxSetup().routes.empty())
        semi.repaint();   // modulation ring dot
}

void EditorPage::resized()
{
    auto r = getLocalBounds();

    //== Top bar ===================================================================
    topArea = r.removeFromTop (46).toFloat();
    {
        auto t = topArea.toNearestInt().reduced (11, 10);
        prevButton.setBounds (t.removeFromLeft (30));
        t.removeFromLeft (5);
        auto interp = t.removeFromRight (3 * 66 + 10);
        for (auto& b : interpButtons)
        {
            b.setBounds (interp.removeFromLeft (66));
            interp.removeFromLeft (5);
        }
        t.removeFromRight (14);
        redoButton.setBounds (t.removeFromRight (56));
        t.removeFromRight (5);
        undoButton.setBounds (t.removeFromRight (56));
        t.removeFromRight (14);
        mapButton.setBounds (t.removeFromRight (82));
        t.removeFromRight (5);
        deleteButton.setBounds (t.removeFromRight (68));
        t.removeFromRight (5);
        dupButton.setBounds (t.removeFromRight (88));
        t.removeFromRight (10);
        nextButton.setBounds (t.removeFromRight (30));
        t.removeFromRight (5);
        zoneName.setBounds (t);
    }
    r.removeFromTop (8);

    //== Sample & loop =============================================================
    sampleArea = r.removeFromTop (250).toFloat();
    {
        auto s = sampleArea.toNearestInt().reduced (11, 9);
        s.removeFromTop (24);
        auto info = s.removeFromBottom (18);
        loopInfo.setBounds (info);
        s.removeFromBottom (6);
        auto row = s.removeFromBottom (24);
        s.removeFromBottom (6);
        auto sliderRow = s.removeFromBottom (20);
        randomButton.setBounds (sliderRow.removeFromLeft (66));
        sliderRow.removeFromLeft (8);
        const int sw = (sliderRow.getWidth() - 5 * 8) / 6;
        for (auto* sl : { sliders[0].get(), sliders[1].get(), randomSlider.get(), sliders[2].get(), sliders[3].get(), sliders[4].get() })
        {
            sl->setBounds (sliderRow.removeFromLeft (sw));
            sliderRow.removeFromLeft (8);
        }
        s.removeFromBottom (7);
        waveArea = s.toFloat();
        wave.setBounds (s);

        auto place = [&row] (Component& c, int w, int gap = 5) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); };
        place (loopButtons[0], 42);
        place (loopButtons[1], 50);
        place (loopButtons[2], 108, 14);
        labelStart = row.removeFromLeft (38).toFloat();
        place (startBox, 66);
        place (zeroStart, 58, 12);
        labelEnd = row.removeFromLeft (28).toFloat();
        place (endBox, 66);
        place (zeroEnd, 58, 12);
        labelFade = row.removeFromLeft (44).toFloat();
        place (fadeBox, 58, 8);
        place (snapButton, 50, 12);
        zoomEndButton.setBounds (row.removeFromRight (62));
        row.removeFromRight (5);
        zoomStartButton.setBounds (row.removeFromRight (62));
        row.removeFromRight (5);
        zoomAllButton.setBounds (row.removeFromRight (40));
    }
    r.removeFromTop (8);

    //== Zone settings: four panels + voice mode ===================================
    auto params = r.removeFromTop (172);
    const int gap = 8, total = params.getWidth() - 4 * gap;
    const int wMap = total * 28 / 100, wPitch = total * 25 / 100, wAmp = total * 13 / 100, wEnv = total * 20 / 100;
    mapArea = params.removeFromLeft (wMap).toFloat();     params.removeFromLeft (gap);
    pitchArea = params.removeFromLeft (wPitch).toFloat(); params.removeFromLeft (gap);
    ampArea = params.removeFromLeft (wAmp).toFloat();     params.removeFromLeft (gap);
    envArea = params.removeFromLeft (wEnv).toFloat();     params.removeFromLeft (gap);
    voiceArea = params.toFloat();
    {
        auto v = voiceArea.reduced (10.0f, 9.0f).withTrimmedTop (22.0f).reduced (6.0f, 8.0f).toNearestInt();
        auto col = v.removeFromLeft (64);
        polyButton.setBounds (col.removeFromTop (24));
        col.removeFromTop (6);
        monoButton.setBounds (col.removeFromTop (24));
        col.removeFromTop (6);
        legatoButton.setBounds (col.removeFromTop (24));
        glide.setBounds (v.withSizeKeepingCentre (jmin (62, v.getWidth()), glide.getPreferredHeight()));
    }

    auto layoutRow = [] (Rectangle<float> panel, std::initializer_list<Component*> items, int itemW, int itemH)
    {
        auto inner = panel.reduced (10.0f, 9.0f).withTrimmedTop (22.0f).toNearestInt();
        const int count = (int) items.size();
        const int step = inner.getWidth() / count;
        int i = 0;
        for (auto* c : items)
        {
            auto cell = Rectangle<int> (inner.getX() + i * step, inner.getY(), step, inner.getHeight());
            c->setBounds (cell.withSizeKeepingCentre (jmin (itemW, step), itemH));
            ++i;
        }
    };
    const int kh = root.getPreferredHeight();
    layoutRow (mapArea, { &root, &loKey, &hiKey, &loVel, &hiVel }, 62, kh);
    layoutRow (pitchArea, { &semi, &tune, &keepSpeed, &grain }, 62, semi.getPreferredHeight());
    keepSpeed.setBounds (keepSpeed.getBounds().withSizeKeepingCentre (keepSpeed.getWidth(), 30).translated (0, -8));
    layoutRow (ampArea, { &gain, &pan }, 62, kh);
    layoutRow (envArea, { &attack, &decay, &sustain, &release }, 54, attack.getPreferredHeight());
    {
        // in the panel's title row, on the right
        auto head = envArea.reduced (10.0f, 9.0f).removeFromTop (16.0f).toNearestInt();
        envCopyBox.setBounds (head.removeFromRight (jmin (104, head.getWidth() - 70)).expanded (0, 1));
    }
    r.removeFromTop (8);

    //== Keymap ====================================================================
    keyArea = r.toFloat();
    keyInset = keyArea.reduced (10.0f, 9.0f).withTrimmedTop (22.0f);
    keymap.setBounds (keyInset.reduced (6.0f, 5.0f).toNearestInt());
}

void EditorPage::paint (Graphics& g)
{
    theme::drawPanel (g, topArea.reduced (1.0f));

    theme::drawPanel (g, sampleArea.reduced (1.0f));
    String info;
    if (const auto* z = proc.getSelectedZone(); z != nullptr && z->sample != nullptr)
        info << String (roundToInt (z->sample->sampleRate)) << " HZ - " << z->sample->audio.getNumChannels() << " CH - "
             << z->sample->length() << " FRAMES   |   WHEEL = ZOOM - DRAG = SCROLL - DRAG S / E FLAGS - DOUBLE-CLICK = ALL";
    theme::drawHeading (g, sampleArea.reduced (10.0f, 9.0f), "Sample & loop", info);

    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 700, 0.1f));
    g.drawText ("START", labelStart, Justification::centredLeft, false);
    g.drawText ("END", labelEnd, Justification::centredLeft, false);
    g.drawText ("X-FADE", labelFade, Justification::centredLeft, false);

    for (auto [area, title] : { std::pair { mapArea, "Mapping" }, std::pair { pitchArea, "Pitch" },
                                std::pair { ampArea, "Amp" }, std::pair { envArea, "Envelope" }, std::pair { voiceArea, "Voice" } })
    {
        theme::drawPanel (g, area.reduced (1.0f));
        theme::drawInset (g, theme::drawHeading (g, area.reduced (10.0f, 9.0f), title));
    }

    theme::drawPanel (g, keyArea.reduced (1.0f));
    theme::drawHeading (g, keyArea.reduced (10.0f, 9.0f), "Keymap",
                        "bars = zones: click to edit, drag to move, drag the ends to resize - keys: click to play, shift-click sets the root");
    theme::drawLed (g, keyInset, {});
}
} // namespace emx
