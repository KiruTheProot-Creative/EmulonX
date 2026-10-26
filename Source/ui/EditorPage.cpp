// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EditorPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../engine/SampleLoader.h"
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

/** 176400 -> "176,400" */
String framesText (double v)
{
    auto s = String (roundToInt (v));
    for (int i = s.length() - 3; i > 0; i -= 3)
        s = s.substring (0, i) + "," + s.substring (i);
    return s;
}

const int whiteIndex[] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
const bool isBlack[]   { false, true, false, true, false, false, true, false, true, false, true, false };
int whiteNumber (int n) { return (n / 12) * 7 + whiteIndex[n % 12]; }

const Colour warpYellow { 0xffffcc33 };
const Colour amber { 0xffffb030 };
} // namespace

Colour zoneColour (int)
{
    // every zone in the skin's accent colour (the selected one stands out by its outline and brightness)
    return theme::hue (0xffd8222c);
}

//==================================================================================
// WaveformView
//==================================================================================
WaveformView::WaveformView (EmulonXProcessor& p) : proc (p) {}

const ModelZone* WaveformView::zone() const { return proc.getSelectedZone(); }

int WaveformView::viewKey (const ModelZone& z) const { return proc.isSplicer() ? -1 : z.id; }

Range<double> WaveformView::view (const ModelZone& z)
{
    const double n = z.sample != nullptr ? z.sample->length() : 1.0;
    auto it = views.find (viewKey (z));
    if (it == views.end() || it->second.getEnd() > n + 0.5)
        return views[viewKey (z)] = { 0.0, n };
    return it->second;
}

void WaveformView::setView (const ModelZone& z, double a, double b)
{
    const double n = z.sample != nullptr ? z.sample->length() : 1.0;
    const double w = jlimit (jmin (32.0, n), n, b - a);
    a = jlimit (0.0, n - w, a);
    views[viewKey (z)] = { a, a + w };
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
    if (z.sample == nullptr || mode != Mode::loop)
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

int WaveformView::hitCut (const ModelZone& z, Point<float> pos)
{
    if (z.sample == nullptr || mode != Mode::slices)
        return -1;
    const auto v = view (z);
    const double spp = v.getLength() / jmax (1, getWidth());
    const auto& cuts = proc.getSplice().cuts;
    int best = -1;
    float bestD = 6.0f;
    for (size_t i = 1; i < cuts.size(); ++i)
        if (const float d = std::abs ((float) ((cuts[i] - v.getStart()) / spp) - pos.x); d < bestD)
        {
            bestD = d;
            best = (int) i;
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
            if (ph[i].zoneId == z->id || (proc.isSplicer() && mode == Mode::slices))
                playheads.push_back (ph[i].position);
    }
    if (playheads != lastPlayheads)
    {
        lastPlayheads = playheads;
        repaint();
    }
}

void WaveformView::paintWave (Graphics& g, const ModelZone& z, Range<double> v, bool warped)
{
    const auto& audio = z.sample->audio;
    const int n = audio.getNumSamples(), chans = audio.getNumChannels();
    const float W = (float) getWidth(), H = (float) getHeight();
    const double spp = v.getLength() / W;
    const auto X = [&] (double f) { return (float) ((f - v.getStart()) / spp); };
    const float mid = H * 0.5f, amp = H * 0.5f - 8.0f;

    const int from = warped ? jlimit (0, n, warp.from) : 0;
    const int to = warped ? (warp.to < 0 ? n : jlimit (from, n, warp.to)) : n;
    const float ref = warped ? edit::warpReference (*z.sample, from, to) : 1.0f;
    const auto shape = [&] (float x) { return warped ? edit::warpedSample (warp, x, ref) : x; };

    const Colour line = warped ? warpYellow : theme::hue (0xffff3b3b).withMultipliedAlpha (mode == Mode::warp ? 0.55f : 1.0f);
    const Colour glow = warped ? warpYellow.withAlpha (0.22f) : theme::red.withAlpha (mode == Mode::warp ? 0.12f : 0.25f);

    if (spp >= 1.0)
    {
        // Min/max per pixel column, drawn twice: a soft glow, then the solid line.
        const int stride = jmax (1, (int) (spp / 200.0));
        RectangleList<float> cols;
        for (int i = 0; i < (int) W; ++i)
        {
            const int f0 = jmax (from, (int) (v.getStart() + i * spp));
            const int f1 = jmin (to, (int) (v.getStart() + (i + 1) * spp) + 1);
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int c = 0; c < chans; ++c)
            {
                const float* d = audio.getReadPointer (c);
                for (int j = f0; j < f1; j += stride)
                {
                    const float s = shape (d[j]);
                    lo = jmin (lo, s);
                    hi = jmax (hi, s);
                }
            }
            if (hi >= lo)
                cols.addWithoutMerging ({ (float) i, mid - hi * amp, 1.0f, jmax (1.0f, (hi - lo) * amp) });
        }
        g.setColour (glow);
        for (auto& r : cols)
            g.fillRect (r.expanded (1.0f, 1.5f));
        g.setColour (line);
        g.fillRectList (cols);
    }
    else
    {
        auto sampleAt = [&] (int j)
        {
            float s = 0.0f;
            for (int c = 0; c < chans; ++c)
                s += shape (audio.getSample (c, j));
            return s / (float) chans;
        };
        Path path;
        const int f0 = jmax (from, (int) v.getStart()), f1 = jmin (to - 1, (int) std::ceil (v.getEnd()));
        for (int j = f0; j <= f1; ++j)
        {
            const Point<float> pt (X (j), mid - sampleAt (j) * amp);
            if (j == f0) path.startNewSubPath (pt); else path.lineTo (pt);
        }
        g.setColour (glow);
        g.strokePath (path, PathStrokeType (4.0f));
        g.setColour (line);
        g.strokePath (path, PathStrokeType (1.5f));
        if (spp < 0.15 && ! warped)
        {
            g.setColour (theme::textStrong);
            for (int j = f0; j <= f1; ++j)
                g.fillRect (X (j) - 1.5f, mid - sampleAt (j) * amp - 1.5f, 3.0f, 3.0f);
        }
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

    const auto& p = z->params;
    const int n = z->sample->length();
    const auto v = view (*z);
    const float W = bounds.getWidth(), H = bounds.getHeight();
    const double spp = v.getLength() / W;
    const auto X = [&] (double f) { return (float) ((f - v.getStart()) / spp); };
    const float mid = H * 0.5f;
    const auto& cuts = proc.getSplice().cuts;
    const int selIndex = proc.getSelectedIndex();

    // Backgrounds: loop region, slices, or the part a warp changes
    if (mode == Mode::loop && p.loopMode != LoopMode::off)
    {
        g.setColour (theme::hue (0x24ff3232));
        g.fillRect (Rectangle<float>::leftTopRightBottom (X (p.loopStart), 0.0f, X (p.loopEnd), H));
        if (const int xe = effectiveCrossfade (p); xe > 0 && p.loopDir == LoopDir::forward)
        {
            g.setColour (theme::hue (0x21ffd2d2));
            g.fillRect (Rectangle<float>::leftTopRightBottom (X (p.loopEnd - xe), 0.0f, X (p.loopEnd), H));
            g.fillRect (Rectangle<float>::leftTopRightBottom (X (p.loopStart - xe), 0.0f, X (p.loopStart), H));
        }
    }
    if (mode == Mode::slices)
        for (size_t i = 0; i < cuts.size(); ++i)
        {
            const int b = i + 1 < cuts.size() ? cuts[i + 1] : n;
            g.setColour (zoneColour ((int) i).withAlpha ((int) i == selIndex ? 0.30f : 0.12f));
            g.fillRect (Rectangle<float>::leftTopRightBottom (X (cuts[i]), 0.0f, X (b), H));
        }
    if (mode == Mode::warp && (warp.from > 0 || (warp.to >= 0 && warp.to < n)))
    {
        g.setColour (warpYellow.withAlpha (0.08f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (X (warp.from), 0.0f, X (warp.to < 0 ? n : warp.to), H));
    }
    g.setColour (theme::red.withAlpha (0.3f));
    g.fillRect (0.0f, mid, W, 1.0f);

    paintWave (g, *z, v, false);
    if (mode == Mode::warp)
        paintWave (g, *z, v, true);

    if (mode == Mode::loop)
    {
        // Sample start / end: darken what is not played, amber markers with flags at the bottom
        const int se = p.sampleEnd > 0 ? p.sampleEnd : n;
        g.setColour (Colours::black.withAlpha (0.5f));
        if (p.sampleStart > 0)
            g.fillRect (Rectangle<float>::leftTopRightBottom (jmax (0.0f, X (0)), 0.0f, X (p.sampleStart), H));
        if (se < n)
            g.fillRect (Rectangle<float>::leftTopRightBottom (X (se), 0.0f, jmin (W, X (n)), H));
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

        // Random start area: shaded from the sample start to its own (dashed) marker
        if (p.randomStart && p.randomEnd > p.sampleStart)
        {
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

        if (p.loopMode != LoopMode::off)
        {
            // which way the loop plays: arrows inside the loop
            const float a = X (p.loopStart), b = X (p.loopEnd);
            const float w = jmin (b, W) - jmax (a, 0.0f);
            if (w > 70.0f)
            {
                const float y = H - 24.0f, len = jmin (110.0f, w * 0.32f);
                auto arrow = [&] (float x0, float x1, bool both)
                {
                    Path pa;
                    pa.startNewSubPath (x0, y);
                    pa.lineTo (x1, y);
                    g.strokePath (pa, PathStrokeType (1.6f));
                    const float d = x1 > x0 ? -7.0f : 7.0f;
                    Path head;
                    head.addTriangle (x1, y, x1 + d, y - 4.0f, x1 + d, y + 4.0f);
                    if (both)
                        head.addTriangle (x0, y, x0 - d, y - 4.0f, x0 - d, y + 4.0f);
                    g.fillPath (head);
                };
                g.setColour (theme::textStrong.withAlpha (0.85f));
                const float c = (jmax (a, 0.0f) + jmin (b, W)) * 0.5f;
                if (p.loopDir == LoopDir::pingPong)
                    arrow (c - len, c + len, true);
                else
                {
                    const bool fwd = p.loopDir == LoopDir::forward;
                    for (float off : { -len - 12.0f, 12.0f })
                        arrow (fwd ? c + off : c + off + len, fwd ? c + off + len : c + off, false);
                }
            }

            // Loop markers with LS / LE flags
            g.setFont (theme::font (8.5f, 900, 0.0f));
            for (int which = 0; which < 2; ++which)
            {
                const float px = std::round (X (which == 0 ? p.loopStart : p.loopEnd));
                if (px < -24.0f || px > W + 24.0f)
                    continue;
                g.setColour (theme::hue (0xffff3030));
                g.fillRect (px - 1.0f, 0.0f, 2.0f, H);
                const auto flag = Rectangle<float> (which == 0 ? px : px - 19.0f, 0.0f, 19.0f, 14.0f);
                g.fillRect (flag);
                g.setColour (Colours::white);
                g.drawText (which == 0 ? "LS" : "LE", flag, Justification::centred, false);
            }
        }
    }

    if (mode == Mode::slices)
    {
        g.setFont (theme::font (8.5f, 900, 0.0f));
        for (size_t i = 0; i < cuts.size(); ++i)
        {
            const float px = std::round (X (cuts[i]));
            if (px < -24.0f || px > W + 2.0f)
                continue;
            const auto c = zoneColour ((int) i);
            if (i > 0)
            {
                g.setColour (Colours::white.withAlpha ((int) i == selIndex || (int) i == selIndex + 1 ? 0.95f : 0.6f));
                g.fillRect (px - 1.0f, 0.0f, 2.0f, H);
            }
            const auto tag = Rectangle<float> (px + (i > 0 ? 1.0f : 2.0f), 2.0f, 18.0f, 13.0f);
            g.setColour (c);
            g.fillRect (tag);
            if ((int) i == selIndex)
            {
                g.setColour (Colours::white);
                g.drawRect (tag, 1.0f);
            }
            g.setColour (theme::textOn (c));
            g.drawText (String ((int) i + 1), tag, Justification::centred, false);
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
    const bool onHandle = z != nullptr && (hitMarker (*z, e.position) >= 0 || hitCut (*z, e.position) >= 0);
    setMouseCursor (onHandle ? MouseCursor::LeftRightResizeCursor
                             : mode == Mode::slices ? MouseCursor::PointingHandCursor : MouseCursor::DraggingHandCursor);
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    dragMode = -1;
    const auto* z = zone();
    if (z == nullptr || z->sample == nullptr)
        return;
    dragView = view (*z);
    if (mode == Mode::slices)
    {
        const int cut = hitCut (*z, e.position);
        if (e.mods.isPopupMenu())
        {
            if (cut > 0)   // right-click a cut: remove it
            {
                auto cuts = proc.getSplice().cuts;
                cuts.erase (cuts.begin() + cut);
                proc.setSliceCuts (std::move (cuts));
            }
            return;
        }
        if (cut > 0)
        {
            dragMode = 6;
            dragCut = cut;
            dragCuts = proc.getSplice().cuts;
            return;
        }
        // click a slice: pick it
        const double f = frameAt (*z, e.position.x);
        const auto& cuts = proc.getSplice().cuts;
        int k = 0;
        for (size_t i = 0; i < cuts.size(); ++i)
            if (f >= cuts[i]) k = (int) i;
        const auto& zs = proc.getZones();
        if (k < (int) zs.size() && zs[(size_t) k].id != z->id)
            proc.selectZone (zs[(size_t) k].id);
        dragMode = 5;
        return;
    }
    const int hit = hitMarker (*z, e.position);
    dragMode = hit >= 0 ? hit : 5;
}

void WaveformView::mouseDrag (const MouseEvent& e)
{
    const auto* z = zone();
    if (z == nullptr || z->sample == nullptr || dragMode < 0)
        return;
    const double spp = dragView.getLength() / jmax (1, getWidth());
    if (dragMode == 5)
    {
        const double d = -(double) e.getDistanceFromDragStartX() * spp;
        setView (*z, dragView.getStart() + d, dragView.getEnd() + d);
    }
    else if (dragMode == 6)
    {
        if (dragCut <= 0 || dragCut >= (int) dragCuts.size())
            return;
        const int n = z->sample->length();
        const int lo = dragCuts[(size_t) dragCut - 1] + 64;
        const int hi = (dragCut + 1 < (int) dragCuts.size() ? dragCuts[(size_t) dragCut + 1] : n) - 64;
        if (hi <= lo)
            return;
        auto cuts = dragCuts;
        cuts[(size_t) dragCut] = jlimit (lo, hi, roundToInt (dragView.getStart() + e.position.x * spp));
        if (cuts != proc.getSplice().cuts)
            proc.setSliceCuts (std::move (cuts));
    }
    else if (onMarkerMoved)
        onMarkerMoved (dragMode, roundToInt (dragView.getStart() + e.position.x * spp));
}

void WaveformView::mouseUp (const MouseEvent&)
{
    dragMode = -1;
    dragCut = -1;
}

void WaveformView::mouseDoubleClick (const MouseEvent& e)
{
    const auto* z = zone();
    if (mode == Mode::slices && z != nullptr && z->sample != nullptr)
    {
        // double-click: a new cut here
        const int f = roundToInt (frameAt (*z, e.position.x));
        auto cuts = proc.getSplice().cuts;
        for (auto c : cuts)
            if (std::abs (c - f) < 64)
                return;
        cuts.push_back (f);
        proc.setSliceCuts (std::move (cuts));
        return;
    }
    zoomAll();
}

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
// CloseUpView
//==================================================================================
void CloseUpView::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    theme::drawScreen (g, bounds);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.5f, 700, 0.1f));
    g.drawText ("CLOSE-UP - 3 CYCLES", bounds.reduced (7.0f, 4.0f), Justification::topLeft, false);

    const auto* z = proc.getSelectedZone();
    if (z == nullptr || z->sample == nullptr)
        return;
    const auto& audio = z->sample->audio;
    const int n = audio.getNumSamples(), chans = audio.getNumChannels();
    if (n < 64)
        return;
    const auto mono = [&] (int j) { float s = 0.0f; for (int c = 0; c < chans; ++c) s += audio.getSample (c, j); return s / (float) chans; };

    // three cycles: from one rising zero crossing to the fourth
    const int c0 = jlimit (1, n - 2, centre);
    std::vector<int> xs;
    for (int j = c0; j < n && j < c0 + 30000 && xs.size() < 4; ++j)
        if (mono (j - 1) < 0.0f && mono (j) >= 0.0f && (xs.empty() || j - xs.back() >= 12))
            xs.push_back (j);
    int a = c0, b = jmin (n, c0 + 1024);
    if (xs.size() >= 4) { a = xs[0]; b = xs[3]; }
    if (b - a < 48) b = jmin (n, a + 256);

    const int from = jlimit (0, n, warp.from), to = warp.to < 0 ? n : jlimit (from, n, warp.to);
    const float ref = edit::warpReference (*z->sample, from, to);
    auto area = bounds.reduced (4.0f, 16.0f).withTrimmedBottom (-10.0f);
    const float mid = area.getCentreY(), amp = area.getHeight() * 0.5f;
    g.setColour (theme::red.withAlpha (0.25f));
    g.fillRect (area.getX(), mid, area.getWidth(), 1.0f);

    Path before, after;
    const int steps = jmin (b - a, (int) area.getWidth() * 2);
    for (int i = 0; i <= steps; ++i)
    {
        const int j = jmin (n - 1, a + (int) ((juce::int64) (b - a) * i / jmax (1, steps)));
        const float x = area.getX() + area.getWidth() * (float) i / (float) jmax (1, steps);
        float s = 0.0f, w = 0.0f;
        for (int c = 0; c < chans; ++c)
        {
            const float v = audio.getSample (c, j);
            s += v;
            w += j >= from && j < to ? edit::warpedSample (warp, v, ref) : v;
        }
        s /= (float) chans; w /= (float) chans;
        const Point<float> p0 (x, mid - jlimit (-1.2f, 1.2f, s / ref) * amp), p1 (x, mid - jlimit (-1.2f, 1.2f, w / ref) * amp);
        if (i == 0) { before.startNewSubPath (p0); after.startNewSubPath (p1); }
        else        { before.lineTo (p0); after.lineTo (p1); }
    }
    g.setColour (theme::hue (0xffff3b3b).withAlpha (0.7f));
    g.strokePath (before, PathStrokeType (1.3f));
    g.setColour (warpYellow.withAlpha (0.25f));
    g.strokePath (after, PathStrokeType (4.0f));
    g.setColour (warpYellow);
    g.strokePath (after, PathStrokeType (1.8f));
}

//==================================================================================
// ValueBox
//==================================================================================
ValueBox::ValueBox (String l) : label (l.toUpperCase()) {}

Rectangle<float> ValueBox::ledArea() const
{
    auto r = getLocalBounds().toFloat();
    return label.isEmpty() ? r : r.withTrimmedTop (14.0f);
}

void ValueBox::paint (Graphics& g)
{
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    if (label.isNotEmpty())
    {
        g.setColour (theme::textMuted.withMultipliedAlpha (alpha));
        g.setFont (theme::font (7.5f, 700, 0.1f));
        g.drawText (label, getLocalBounds().toFloat().removeFromTop (12.0f), Justification::centredLeft, true);
    }
    theme::drawLed (g, ledArea(), format ? format (value) : String (value), 9.5f, Justification::centred,
                    theme::ledText.withMultipliedAlpha (alpha));
}

void ValueBox::set (double v)
{
    if (inc > 0.0)
        v = lo + std::round ((v - lo) / inc) * inc;
    v = jlimit (lo, hi, v);
    if (exactlyEqual (v, value))
        return;
    value = v;
    repaint();
    if (onChange)
        onChange (v);
}

void ValueBox::mouseDown (const MouseEvent&) { dragStart = value; }

void ValueBox::mouseDrag (const MouseEvent& e)
{
    if (readOnly || ! isEnabled())
        return;
    const double perPixel = (hi - lo) / jmax (1.0f, dragPixels) * (e.mods.isShiftDown() ? 0.05 : 1.0);
    set (dragStart - (double) e.getDistanceFromDragStartY() * perPixel);
}

void ValueBox::mouseDoubleClick (const MouseEvent&)
{
    if (readOnly || ! isEnabled())
        return;
    typeIn = std::make_unique<TypeInField> (*this, ledArea().toNearestInt(), format ? format (value) : String (value),
                                            [this] (const String& typed)
                                            {
                                                double v = 0.0;
                                                if (parse)
                                                {
                                                    if (parse (typed, v)) set (v);
                                                    return;
                                                }
                                                const auto t = typed.trim().toUpperCase();
                                                // a typed name ("C3", "OFF") that one of the values shows
                                                if (format && inc > 0.0 && (hi - lo) / inc <= 4000.0)
                                                    for (double x = lo; x <= hi + inc * 0.5; x += inc)
                                                        if (format (x).toUpperCase() == t) { set (x); return; }
                                                if (t.containsAnyOf ("0123456789"))
                                                    set (t.retainCharacters ("0123456789.-").getDoubleValue());
                                            });
}

//==================================================================================
// KeymapView
//==================================================================================
KeymapView::KeymapView (EmulonXProcessor& p) : proc (p) {}

KeymapView::KeyRect KeymapView::keyRect (int n) const
{
    const int wi = whiteNumber (n) - whiteNumber (firstNote);
    if (! isBlack[n % 12])
        return { (float) wi * keyW, keyW, false };
    const float bw = keyW * 0.62f;
    return { (float) (wi + 1) * keyW - bw * 0.5f, bw, true };
}

int KeymapView::noteAt (Point<float> pt) const
{
    if (pt.y < keysTop || pt.y > keysTop + keysHeight)
        return -1;
    if (pt.y < keysTop + keysHeight * 0.62f)
        for (int n = firstNote; n <= lastNote; ++n)
            if (isBlack[n % 12])
                if (const auto k = keyRect (n); pt.x >= k.x && pt.x < k.x + k.w)
                    return n;
    for (int n = firstNote; n <= lastNote; ++n)
        if (! isBlack[n % 12])
            if (const auto k = keyRect (n); pt.x >= k.x && pt.x < k.x + k.w)
                return n;
    return -1;
}

int KeymapView::velocityAt (float y, int note) const
{
    const float keyH = keysHeight * (isBlack[note % 12] ? 0.62f : 1.0f);
    return jlimit (1, 127, roundToInt (20.0f + 107.0f * (y - keysTop) / jmax (1.0f, keyH)));
}

int KeymapView::zoneIndexOf (int id) const
{
    const auto& zs = proc.getZones();
    for (size_t i = 0; i < zs.size(); ++i)
        if (zs[i].id == id)
            return (int) i;
    return -1;
}

void KeymapView::layoutLanes()
{
    const auto& zs = proc.getZones();

    // the keys shown: at least C1..C6, more if a zone reaches further (always whole octaves from a C)
    int lo = 36, hi = 96;
    for (auto& z : zs)
    {
        lo = jmin (lo, z.params.loKey);
        hi = jmax (hi, z.params.hiKey);
    }
    firstNote = (lo / 12) * 12;
    lastNote = jmin (127, ((hi + 11) / 12) * 12);
    keyW = (float) getWidth() / (float) jmax (1, whiteNumber (lastNote) - whiteNumber (firstNote) + 1);

    lanes.assign (zs.size(), 0);
    if (proc.isSplicer())
        numLanes = 1;
    else
    {
        std::vector<size_t> order (zs.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort (order.begin(), order.end(), [&zs] (size_t a, size_t b) { return zs[a].params.loKey < zs[b].params.loKey; });
        std::vector<int> laneEnd;
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
    }
    keysTop = (float) getHeight() - keysHeight - labelsHeight;
    laneH = jmin (laneStep, (keysTop - 4.0f - 10.0f) / (float) jmax (1, numLanes));
}

float KeymapView::velY (int velocity) const
{
    // Kontakt style: key across, velocity up (127 at the top)
    const float top = 5.0f, bottom = keysTop - 9.0f;
    return bottom - (float) jlimit (0, 128, velocity) / 128.0f * (bottom - top);
}

KeymapView::Drag KeymapView::edgeAt (Rectangle<float> r, Point<float> p) const
{
    if (p.x < r.getX() + 6.0f) return Drag::resizeLo;
    if (p.x > r.getRight() - 6.0f) return Drag::resizeHi;
    if (stacked) return Drag::move;   // lanes: no velocity edges
    if (p.y < r.getY() + 5.0f) return Drag::resizeVelHi;
    if (p.y > r.getBottom() - 5.0f) return Drag::resizeVelLo;
    return Drag::move;
}

int KeymapView::getPreferredHeight()
{
    if (lanes.size() != proc.getZones().size())
        layoutLanes();
    int count = 1;
    if (! proc.isSplicer())
    {
        // the same packing as layoutLanes, without needing a size yet
        const auto& zs = proc.getZones();
        std::vector<size_t> order (zs.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort (order.begin(), order.end(), [&zs] (size_t a, size_t b) { return zs[a].params.loKey < zs[b].params.loKey; });
        std::vector<int> laneEnd;
        for (auto i : order)
        {
            int l = 0;
            while (l < (int) laneEnd.size() && laneEnd[(size_t) l] >= zs[i].params.loKey)
                ++l;
            if (l == (int) laneEnd.size())
                laneEnd.push_back (-1);
            laneEnd[(size_t) l] = zs[i].params.hiKey;
        }
        count = jlimit (1, 6, (int) laneEnd.size());
    }
    float mapH = proc.isSplicer() ? 110.0f : 150.0f;   // key x velocity map
    if (stacked && ! proc.isSplicer())
        mapH = jmax (64.0f, (float) count * laneStep + 10.0f);   // one lane per row of zones (more than 6 get squeezed)
    return roundToInt (mapH + 6.0f + keysHeight + labelsHeight);
}

Rectangle<float> KeymapView::barRect (size_t i) const
{
    const auto& p = proc.getZones()[i].params;
    const int a0 = jmax (p.loKey, firstNote), b0 = jmin (p.hiKey, lastNote);
    const auto a = keyRect (a0), b = keyRect (b0);
    if (proc.isSplicer())
    {
        // slices side by side, one semitone wide each (the keyboard below shows which key plays which)
        const float step = (float) getWidth() / (float) jmax (1, lastNote - firstNote + 1);
        return { (float) (p.loKey - firstNote) * step + 1.0f, 5.0f, jmax (4.0f, step - 2.0f), keysTop - 16.0f };
    }
    if (stacked)
    {
        const float top = 5.0f + (float) (i < lanes.size() ? lanes[i] : 0) * laneH;
        return { a.x + 1.0f, top, jmax (6.0f, b.x + b.w - a.x - 2.0f), laneH - 5.0f };
    }
    const float top = velY (p.hiVel + 1), bottom = velY (p.loVel);
    return { a.x + 1.0f, top, jmax (6.0f, b.x + b.w - a.x - 2.0f), jmax (6.0f, bottom - top) };
}

void KeymapView::tick()
{
    std::bitset<128> now;
    for (int n = 0; n < 128; ++n)
        now[(size_t) n] = proc.keyboardState.isNoteOnForChannels (0xffff, n);
    if (now != held)
    {
        held = now;
        repaint();
    }
}

void KeymapView::paint (Graphics& g)
{
    const auto& zs = proc.getZones();
    if (lanes.size() != zs.size())
        layoutLanes();
    const auto* sel = proc.getSelectedZone();
    const int selIndex = sel != nullptr ? zoneIndexOf (sel->id) : -1;
    const bool splicer = proc.isSplicer();

    // The map: an LED screen behind the bars
    const auto mapArea = Rectangle<float> (0.0f, 0.0f, (float) getWidth(), keysTop - 4.0f);
    theme::drawScreen (g, mapArea, false);
    if (zs.empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (9.0f, 500, 0.06f));
        g.drawText ("NO ZONES YET - ADD SAMPLES OR DROP THEM HERE", mapArea.reduced (12.0f, 0.0f), Justification::centredLeft, false);
    }

    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (mapArea.reduced (1.0f).toNearestInt());
        // a held key lights the bars that play it
        for (int pass = 0; pass < 2; ++pass)   // selected bar last, on top
            for (size_t i = 0; i < zs.size(); ++i)
            {
                const bool on = (int) i == selIndex;
                if (on != (pass == 1))
                    continue;
                const auto& p = zs[i].params;
                if (p.hiKey < firstNote || p.loKey > lastNote)
                    continue;
                const auto r = barRect (i);
                bool sounding = false;
                for (int n = p.loKey; n <= p.hiKey && ! sounding; ++n)
                    sounding = held[(size_t) n];
                const auto c = zoneColour ((int) i);
                // see-through, so overlapping zones (velocity layers, stacked keys) all stay visible
                const bool solid = splicer || stacked;
                const auto fill = on ? c.brighter (0.25f) : c.darker (0.35f);
                g.setColour (fill.withAlpha (solid ? (on ? 1.0f : 0.85f) : (on ? 0.72f : 0.42f)).brighter (sounding ? 0.35f : 0.0f));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (on ? Colours::white : c.brighter (0.5f).withAlpha (0.8f));
                g.drawRoundedRectangle (r.reduced (on ? 1.0f : 0.5f), 4.0f, on ? 2.0f : 1.0f);

                const auto tc = theme::textOn (c);
                if (splicer)
                {
                    g.setColour (tc);
                    g.setFont (theme::font (jmin (10.0f, r.getWidth() * 0.7f), 800, 0.0f));
                    g.drawText (String ((int) i + 1), r.withHeight (18.0f), Justification::centred, false);
                    continue;
                }
                // the root note: a white tick
                if (p.root >= p.loKey && p.root <= p.hiKey && p.root >= firstNote && p.root <= lastNote)
                {
                    const auto k = keyRect (p.root);
                    g.setColour (Colours::white.withAlpha (0.85f));
                    g.fillRect (k.x + k.w * 0.5f - 1.0f, r.getBottom() - 9.0f, 2.0f, 6.0f);
                }
                if (r.getWidth() > 26.0f && r.getHeight() > 14.0f)
                {
                    auto t = r.reduced (6.0f, 3.0f);
                    g.setColour (Colours::white);
                    g.setFont (theme::font (9.5f, 800, 0.04f));
                    g.drawText (zs[i].name.toUpperCase(), t.removeFromTop (13.0f), Justification::centredLeft, true);
                    if (r.getHeight() >= 32.0f)
                    {
                        g.setColour (Colours::white.withAlpha (0.8f));
                        g.setFont (theme::font (7.5f, 500, 0.04f));
                        g.drawText (noteName (p.loKey) + "-" + noteName (p.hiKey) + "  VEL " + String (p.loVel) + "-" + String (p.hiVel),
                                    t.removeFromTop (11.0f), Justification::centredLeft, true);
                    }
                }
            }
    }

    // Keyboard: keys inside the selected zone (or each slice's key) take its colour
    for (int black = 0; black < 2; ++black)
        for (int n = firstNote; n <= lastNote; ++n)
        {
            if (isBlack[n % 12] != (black == 1))
                continue;
            const auto k = keyRect (n);
            const auto r = Rectangle<float> (k.x, keysTop, k.w, keysHeight * (k.black ? 0.62f : 1.0f));
            const bool down = held[(size_t) n];
            int tint = -1;
            if (splicer)
            {
                for (size_t i = 0; i < zs.size(); ++i)
                    if (zs[i].params.loKey == n) tint = (int) i;
            }
            else if (sel != nullptr && n >= sel->params.loKey && n <= sel->params.hiKey)
                tint = selIndex;

            Colour top, bottom;
            if (k.black)
            {
                top = Colour (0xff3a373f); bottom = Colour (0xff09080b);   // real piano colours in every skin
                if (tint >= 0) { top = zoneColour (tint).darker (0.2f); bottom = zoneColour (tint).darker (1.2f); }
            }
            else
            {
                top = Colour (theme::isMetal() ? 0xffc6d6e6 : 0xffcfc9c9); bottom = Colour (theme::isMetal() ? 0xfff1f6fb : 0xfff2eeee);
                if (tint >= 0) { top = zoneColour (tint).interpolatedWith (Colours::white, 0.45f); bottom = zoneColour (tint).interpolatedWith (Colours::white, 0.65f); }
            }
            if (down) { top = theme::hue (0xffff6a6a); bottom = theme::hue (0xffc01818); }
            g.setGradientFill (ColourGradient::vertical (top, r.getY(), bottom, r.getBottom()));
            g.fillRect (r);
            g.setColour (Colours::black.withAlpha (0.7f));
            g.drawRect (r, 1.0f);

            if (! splicer && sel != nullptr && n == sel->params.root)
            {
                g.setColour (theme::red);
                g.fillEllipse (r.getCentreX() - 3.0f, r.getBottom() - (k.black ? 10.0f : 14.0f), 6.0f, 6.0f);
            }
        }

    // octave names under the keys
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 600, 0.04f));
    for (int n = firstNote; n <= lastNote; n += 12)
    {
        const auto k = keyRect (n);
        g.drawText (noteName (n), Rectangle<float> (k.x + 2.0f, keysTop + keysHeight + 1.0f, 40.0f, labelsHeight - 2.0f),
                    Justification::centredLeft, false);
    }
}

void KeymapView::mouseMove (const MouseEvent& e)
{
    const auto* sel = proc.getSelectedZone();
    if (e.position.y < keysTop)
        for (size_t i = 0; i < proc.getZones().size(); ++i)
            if (barRect (i).contains (e.position))
            {
                if (proc.isSplicer())
                {
                    setMouseCursor (MouseCursor::PointingHandCursor);
                    return;
                }
                const auto r = barRect (i);
                const auto d = sel != nullptr && proc.getZones()[i].id == sel->id ? edgeAt (r, e.position) : Drag::move;
                setMouseCursor (d == Drag::resizeLo || d == Drag::resizeHi ? MouseCursor::LeftRightResizeCursor
                                : d == Drag::resizeVelLo || d == Drag::resizeVelHi ? MouseCursor::UpDownResizeCursor : MouseCursor::DraggingHandCursor);
                return;
            }
    setMouseCursor (e.position.y >= keysTop ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void KeymapView::mouseDown (const MouseEvent& e)
{
    drag = Drag::none;
    const auto& zs = proc.getZones();

    if (e.position.y < keysTop)
    {
        const auto* sel = proc.getSelectedZone();
        clickCandidates.clear();
        cycleOnClick = false;
        for (size_t i = 0; i < zs.size(); ++i)
            if (barRect (i).contains (e.position))
                clickCandidates.push_back (zs[i].id);
        if (clickCandidates.empty())
            return;
        if (proc.isSplicer())
        {
            proc.selectZone (clickCandidates.front());
            return;
        }
        // the selected zone wins (it is drawn on top); a plain click on it again reaches the one below
        int pick = clickCandidates.back();
        if (sel != nullptr && std::find (clickCandidates.begin(), clickCandidates.end(), sel->id) != clickCandidates.end())
        {
            pick = sel->id;
            cycleOnClick = clickCandidates.size() > 1;
        }
        for (size_t i = 0; i < zs.size(); ++i)
            if (zs[i].id == pick)
            {
                drag = sel != nullptr && pick == sel->id ? edgeAt (barRect (i), e.position) : Drag::move;
                if (drag != Drag::move)
                    cycleOnClick = false;
                dragZone = pick;
                dragLo = zs[i].params.loKey;
                dragHi = zs[i].params.hiKey;
                dragLoVel = zs[i].params.loVel;
                dragHiVel = zs[i].params.hiVel;
            }
        proc.selectZone (pick);
        return;
    }

    const int note = noteAt (e.position);
    if (note < 0)
        return;
    if (e.mods.isShiftDown() && ! proc.isSplicer())
    {
        if (const auto* sel = proc.getSelectedZone())
        {
            auto p = sel->params;
            p.root = note;
            proc.setZoneParams (sel->id, p);
        }
        return;
    }
    if (proc.isSplicer())   // playing a slice's key picks that slice
        for (auto& z : zs)
            if (z.params.loKey == note && z.id != proc.getSelectedZoneId())
                proc.selectZone (z.id);
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
    if (e.getDistanceFromDragStart() > 3)
        cycleOnClick = false;
    const float velStep = (velY (0) - velY (128)) / 128.0f;
    if (drag == Drag::move)
    {
        const int d = jlimit (-dragLo, 127 - dragHi, roundToInt ((float) e.getDistanceFromDragStartX() / (keyW * 7.0f / 12.0f)));
        p.loKey = dragLo + d;
        p.hiKey = dragHi + d;
        if (! stacked && std::abs (e.getDistanceFromDragStartY()) > 6)   // dragging up / down moves the velocity window
        {
            const int dv = jlimit (1 - dragLoVel, 127 - dragHiVel, -roundToInt ((float) e.getDistanceFromDragStartY() / jmax (0.5f, velStep)));
            p.loVel = dragLoVel + dv;
            p.hiVel = dragHiVel + dv;
        }
    }
    else if (drag == Drag::resizeVelLo || drag == Drag::resizeVelHi)
    {
        const int v = jlimit (1, 127, roundToInt ((velY (0) - e.position.y) / jmax (0.5f, velStep)));
        if (drag == Drag::resizeVelLo) p.loVel = jmin (v, p.hiVel);
        else                           p.hiVel = jmax (v, p.loVel);
    }
    else
    {
        int note = noteAt ({ e.position.x, keysTop + keysHeight - 2.0f });
        if (note < 0)
            note = e.position.x < 0 ? firstNote : lastNote;
        if (drag == Drag::resizeLo) p.loKey = jmin (note, p.hiKey);
        else                        p.hiKey = jmax (note, p.loKey);
    }
    if (p.loKey != z->params.loKey || p.hiKey != z->params.hiKey || p.loVel != z->params.loVel || p.hiVel != z->params.hiVel)
        proc.setZoneParams (dragZone, p);
}

void KeymapView::mouseUp (const MouseEvent& e)
{
    if (cycleOnClick && e.mouseWasClicked() && clickCandidates.size() > 1)
    {
        // Kontakt style: clicking the selected zone again picks the next one under the mouse
        auto it = std::find (clickCandidates.begin(), clickCandidates.end(), dragZone);
        const auto next = it == clickCandidates.end() || std::next (it) == clickCandidates.end() ? clickCandidates.front() : *std::next (it);
        proc.selectZone (next);
    }
    cycleOnClick = false;
    if (drag == Drag::play && playingNote >= 0)
        proc.keyboardState.noteOff (proc.getKeyboardChannel(), playingNote, 0.0f);
    playingNote = -1;
    drag = Drag::none;
}

//==================================================================================
// The scrolling part of the page
//==================================================================================
class EditorPage::Stack final : public Component
{
public:
    explicit Stack (EditorPage& p) : page (p) {}
    void paint (Graphics& g) override { page.paintStack (g); }
    void mouseUp (const MouseEvent& e) override
    {
        if (e.mouseWasClicked())
            page.stackClicked (e.getPosition());
    }
    void mouseMove (const MouseEvent& e) override
    {
        bool onHead = false;
        for (auto& h : page.headArea)
            onHead = onHead || h.contains (e.position);
        setMouseCursor (onHead ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    }

private:
    EditorPage& page;
};

//==================================================================================
// EditorPage
//==================================================================================
EditorPage::EditorPage (EmulonXProcessor& p)
    : proc (p),
      zoneName ([this]
      {
          const auto& zs = proc.getZones();
          const auto* z = proc.getSelectedZone();
          if (z == nullptr)
              return String ("NO ZONE");
          if (proc.isSplicer())
          {
              const auto s = proc.getSliceSample();
              return (s != nullptr ? s->name.toUpperCase() : String ("SPLICER")) + "  -  " + String ((int) zs.size()) + " SLICES  -  "
                     + noteName (zs.front().params.loKey) + "-" + noteName (zs.back().params.loKey);
          }
          return "ZONE " + String (proc.getSelectedIndex() + 1) + "/" + String ((int) zs.size()) + "  -  " + z->name.toUpperCase()
                 + "  -  " + noteName (z->params.loKey) + "-" + noteName (z->params.hiKey);
      }, 10.0f),
      stack (std::make_unique<Stack> (*this)),
      wave (p), closeUp (p),
      emuCtx { p,
               [this]() -> const emu::Design* { const auto* z = proc.getSelectedZone(); return z != nullptr ? z->params.design.get() : nullptr; },
               [this] (std::function<void (emu::Design&)> f) { editDesign (std::move (f)); },
               [this]
               {
                   // the zone's ADSR becomes the amp envelope; nothing else changes the sound until edited
                   if (const auto* z = proc.getSelectedZone())
                   {
                       auto q = z->params;
                       q.design = emu::makeDesign (q.attack, q.decay, q.sustain, q.release);
                       proc.setZoneParams (z->id, q);
                   }
               },
               [this]
               {
                   if (const auto* z = proc.getSelectedZone())
                   {
                       auto q = z->params;
                       q.design.reset();
                       proc.setZoneParams (z->id, q);
                   }
               },
               [this]
               {
                   const auto* z = proc.getSelectedZone();
                   if (z == nullptr || z->params.design == nullptr)
                       return;
                   const auto d = z->params.design;
                   const int from = z->id;
                   for (auto& other : std::vector<ModelZone> (proc.getZones()))
                       if (other.id != from)
                       {
                           auto q = other.params;
                           q.design = d;
                           emu::adsrFromAmp (*d, q.attack, q.decay, q.sustain, q.release);
                           proc.setZoneParams (other.id, q);
                       }
               } },
      emuFilter (emuCtx), emuEnv (emuCtx), emuLfo (emuCtx), emuCords (emuCtx),
      keymap (p)
{
    //== Strip =====================================================================
    const char* modeTips[] { "Sampler: zones spread over the keys, each with its own sample",
                             "Splicer: one sample chopped into slices, one slice per key (drum loops, phrases)" };
    for (int i = 0; i < 2; ++i)
    {
        modeButtons[i].setTooltip (modeTips[i]);
        modeButtons[i].onClick = [this, i] { proc.setSplicer (i == 1); };
        addAndMakeVisible (modeButtons[i]);
    }
    prevButton.setTooltip ("Previous zone");
    nextButton.setTooltip ("Next zone");
    prevButton.onClick = [this] { proc.selectNextZone (-1); };
    nextButton.onClick = [this] { proc.selectNextZone (1); };
    const char* showTips[] { "Show or hide the SAMPLE panel (loop, warp, slices)", "Show or hide the VOICE panel (pitch, amp, envelope)",
                             "Show or hide the KEYMAP panel" };
    for (int i = 0; i < 3; ++i)
    {
        showButtons[i].setTooltip (showTips[i]);
        showButtons[i].onClick = [this, i] { setPanelShown (i, ! shown[i]); };
        addAndMakeVisible (showButtons[i]);
        interpButtons[i].setTooltip ("Playback quality for the whole preset: Linear is lightest on the CPU, Sinc HQ sounds cleanest");
        interpButtons[i].onClick = [this, i] { proc.setInterpolation ((Interp) i); };
        addAndMakeVisible (interpButtons[i]);
    }
    allButton.setTooltip ("Open every panel");
    allButton.onClick = [this] { for (int i = 0; i < 3; ++i) shown[i] = true; refresh(); };
    undoButton.setTooltip ("Undo the last change (Ctrl+Z)");
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.setTooltip ("Redo (Ctrl+Shift+Z)");
    redoButton.onClick = [this] { proc.redo(); };
    for (Component* c : { (Component*) &prevButton, (Component*) &nextButton, (Component*) &zoneName, (Component*) &allButton,
                          (Component*) &undoButton, (Component*) &redoButton })
        addAndMakeVisible (c);

    viewport.setViewedComponent (stack.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    //== SAMPLE ====================================================================
    for (int i = 0; i < 2; ++i)
        tabButtons[i].onClick = [this, i]
        {
            tab = i == 1 ? Tab::warp : (proc.isSplicer() ? Tab::slices : Tab::loop);
            refresh();
        };
    wave.onMarkerMoved = [this] (int which, int frame) { setLoopPoint (which, frame, false); };

    //   loop
    const char* loopTips[] { "No loop: the sample plays once", "Loop while the note is held and during the release",
                             "Loop while the key is held, then play on to the end of the sample" };
    const char* dirTips[] { "The loop plays start to end, again and again", "The loop plays forward, then backward, then forward...",
                            "The loop plays end to start, again and again" };
    for (int i = 0; i < 3; ++i)
    {
        loopButtons[i].setTooltip (loopTips[i]);
        loopButtons[i].onClick = [this, i] { edit ([i] (ZoneParams& q) { q.loopMode = (LoopMode) i; }); };
        dirButtons[i].setTooltip (dirTips[i]);
        dirButtons[i].onClick = [this, i] { edit ([i] (ZoneParams& q) { q.loopDir = (LoopDir) i; }); };
    }
    for (auto* b : { &startBox, &endBox, &loopStartBox, &loopEndBox, &randomBox })
    {
        b->format = framesText;
        b->setDragPixels (400.0f);
    }
    startBox.setTooltip ("Where the sample starts playing (amber marker) - drag up / down, shift for fine moves, double-click to type");
    endBox.setTooltip ("Where the sample stops when it isn't looping (amber marker)");
    loopStartBox.setTooltip ("Loop start (LS flag) - drag up / down, shift for fine moves, double-click to type");
    loopEndBox.setTooltip ("Loop end (LE flag)");
    randomBox.setTooltip ("End of the random start area (dashed amber marker): each note starts somewhere between START and here");
    startBox.onChange = [this] (double v) { setLoopPoint (2, roundToInt (v), true); };
    endBox.onChange = [this] (double v) { setLoopPoint (3, roundToInt (v), true); };
    loopStartBox.onChange = [this] (double v) { setLoopPoint (0, roundToInt (v), true); };
    loopEndBox.onChange = [this] (double v) { setLoopPoint (1, roundToInt (v), true); };
    randomBox.onChange = [this] (double v) { setLoopPoint (4, roundToInt (v), true); };
    fadeBox.setTooltip ("Cross-fade at the loop seam, so the jump from end to start doesn't click (forward loops)");
    fadeBox.onChange = [this] (double v)
    {
        const auto* z = proc.getSelectedZone();
        if (z == nullptr || z->sample == nullptr)
            return;
        const int frames = roundToInt (v / 1000.0 * z->sample->sampleRate);
        edit ([frames] (ZoneParams& q) { q.crossfade = jlimit (0, q.loopEnd - q.loopStart, frames); });
    };
    fadeBox.format = [] (double v) { return String (roundToInt (v)) + " MS"; };
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
    zeroButton.setTooltip ("Move both loop points to the nearest zero crossings (avoids clicks)");
    zeroButton.onClick = [this]
    {
        if (auto* z = proc.getSelectedZone(); z != nullptr && z->sample != nullptr)
        {
            const int a = WaveformView::nearestZeroCrossing (z->sample->audio, z->params.loopStart);
            const int b = WaveformView::nearestZeroCrossing (z->sample->audio, z->params.loopEnd);
            edit ([a, b] (ZoneParams& q) { if (b - a >= 16) { q.loopStart = a; q.loopEnd = b; } });
        }
    };
    snapButton.setTooltip ("Dragged loop flags jump to zero crossings");
    snapButton.onClick = [this] { wave.snapToZero = ! wave.snapToZero; snapButton.setToggleState (wave.snapToZero, dontSendNotification); };
    zoomAllButton.setTooltip ("Show the whole sample (or double-click the waveform)");
    zoomAllButton.onClick = [this] { wave.zoomAll(); };

    //   warp
    for (int i = 0; i < edit::numWarpShapes; ++i)
    {
        shapeButtons[i].setButtonText (edit::warpShapeName ((edit::WarpShape) i));
        shapeButtons[i].setTooltip (String (edit::warpShapeName ((edit::WarpShape) i)) + ": " + edit::warpShapeHint ((edit::WarpShape) i));
        shapeButtons[i].onClick = [this, i] { warp.shape = (edit::WarpShape) i; refresh(); };
    }
    for (auto* b : { &amountBox, &mixBox })
    {
        b->setRange (0.0, 100.0, 1.0);
        b->format = [] (double v) { return String (roundToInt (v)) + "%"; };
    }
    amountBox.setTooltip ("How strongly the wave is reshaped - drag up / down");
    mixBox.setTooltip ("How much of the reshaped wave is mixed with the original");
    amountBox.onChange = [this] (double v) { warp.amount = (float) v / 100.0f; updateWarpPreview(); };
    mixBox.onChange = [this] (double v) { warp.mix = (float) v / 100.0f; updateWarpPreview(); };
    whereButtons[0].setTooltip ("Reshape the whole sample");
    whereButtons[0].onClick = [this] { warpPart = false; refresh(); };
    whereButtons[1].onClick = [this] { warpPart = true; refresh(); };
    undoWarpButton.setTooltip ("Take back the last warp");
    undoWarpButton.onClick = [this] { proc.unwarpZone (proc.getSelectedZoneId()); };
    applyButton.setTooltip ("Bake the yellow wave into the sample (it is saved inside the preset)");
    applyButton.setToggleState (true, dontSendNotification);
    applyButton.onClick = [this]
    {
        applyButton.setToggleState (true, dontSendNotification);
        proc.warpZone (proc.getSelectedZoneId(), currentWarp());
    };

    //   slices
    findHitsButton.setTooltip ("Put a cut at every hit (drum strike, note start)");
    findHitsButton.onClick = [this] { proc.findSliceHits ((float) sensBox.getValue() / 100.0f); };
    sensBox.setRange (0.0, 100.0, 1.0);
    sensBox.setValue (60.0);
    sensBox.format = [] (double v) { return String (roundToInt (v)) + "%"; };
    sensBox.setTooltip ("Higher finds quieter hits (then press FIND HITS)");
    for (int i = 0; i < 4; ++i)
    {
        const int count = 4 << i;
        evenButtons[i].setTooltip ("Cut the sample into " + String (count) + " equal slices");
        evenButtons[i].onClick = [this, count] { proc.splitSlicesEvenly (count); };
    }
    firstKeyBox.setRange (0.0, 127.0, 1.0);
    firstKeyBox.format = [] (double v) { return noteName (roundToInt (v)); };
    firstKeyBox.setDragPixels (300.0f);
    firstKeyBox.setTooltip ("The key that plays the first slice; the others follow one key each");
    firstKeyBox.onChange = [this] (double v) { proc.setSliceFirstKey (roundToInt (v)); };
    toZonesButton.setTooltip ("Keep the slices as ordinary zones and go back to sampler mode");
    toZonesButton.onClick = [this] { proc.slicesToZones(); };

    //== VOICE =====================================================================
    auto intRange = [] (int lo, int hi) { return NormalisableRange<double> (lo, hi, 1.0); };
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
    keepSpeed.setTooltip ("Pitch without changing the length, like the Emulator X");
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
    attack.onChange  = [this] (double v) { edit ([v] (ZoneParams& q) { q.attack = (float) v; }, true); };
    decay.onChange   = [this] (double v) { edit ([v] (ZoneParams& q) { q.decay = (float) v; }, true); };
    release.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.release = (float) v; }, true); };
    sustain.setRange (NormalisableRange<double> (0.0, 1.0, 0.001), 1.0);
    sustain.format = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    sustain.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.sustain = (float) v; }, true); };
    attack.slider.setTooltip ("Fade-in time when a key is pressed");
    decay.slider.setTooltip ("Time to fall to the sustain level");
    sustain.slider.setTooltip ("Level while the key is held");
    release.slider.setTooltip ("Fade-out time after the key is let go");
    copyEnvButton.setTooltip ("Give every zone this attack, decay, sustain and release");
    copyEnvButton.onClick = [this] { if (const auto* z = proc.getSelectedZone()) proc.copyEnvelope (z->id, -1); };

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

    //== KEYMAP ====================================================================
    for (auto* b : { &rootBox, &loKeyBox, &hiKeyBox })
    {
        b->setRange (0.0, 127.0, 1.0);
        b->format = [] (double v) { return noteName (roundToInt (v)); };
        b->setDragPixels (300.0f);
    }
    for (auto* b : { &loVelBox, &hiVelBox })
    {
        b->setRange (1.0, 127.0, 1.0);
        b->format = [] (double v) { return String (roundToInt (v)); };
        b->setDragPixels (300.0f);
    }
    rootBox.setTooltip ("The key where the sample plays at its own pitch (or shift-click a key)");
    loKeyBox.setTooltip ("Lowest key this zone plays on");
    hiKeyBox.setTooltip ("Highest key this zone plays on");
    loVelBox.setTooltip ("Softest velocity this zone answers to");
    hiVelBox.setTooltip ("Hardest velocity this zone answers to");
    rootBox.onChange  = [this] (double v) { edit ([v] (ZoneParams& q) { q.root = roundToInt (v); }); };
    loKeyBox.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.loKey = jmin (roundToInt (v), q.hiKey); }); };
    hiKeyBox.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.hiKey = jmax (roundToInt (v), q.loKey); }); };
    loVelBox.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.loVel = jmin (roundToInt (v), q.hiVel); }); };
    hiVelBox.onChange = [this] (double v) { edit ([v] (ZoneParams& q) { q.hiVel = jmax (roundToInt (v), q.loVel); }); };
    addZoneButton.setTooltip ("Add samples as new zones");
    addZoneButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Add samples", proc.getSettings().getUserPresetFolder(), loader::supportedWildcards());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this] (const FileChooser& fc)
                              {
                                  if (! fc.getResults().isEmpty())
                                      proc.loadFiles (fc.getResults());
                              });
    };
    stackButton.setTooltip ("Stack: every zone on its own lane, side by side");
    overlapButton.setTooltip ("Overlap: key across, velocity up - zones lie on top of each other, see-through");
    stackButton.onClick = [this] { KeymapView::stacked = true; refresh(); resized(); };
    overlapButton.onClick = [this] { KeymapView::stacked = false; refresh(); resized(); };
    autoMapButton.setTooltip ("Spread the zones across the keyboard by their root notes");
    autoMapButton.onClick = [this] { proc.autoMap(); };
    dupButton.setTooltip ("Copy this zone (same sample and settings)");
    dupButton.onClick = [this] { proc.duplicateZone (proc.getSelectedZoneId()); };
    deleteButton.setTooltip ("Remove this zone");
    deleteButton.onClick = [this] { proc.removeZone (proc.getSelectedZoneId()); };
    sliceKeyBox.setReadOnly (true);
    sliceKeyBox.format = [] (double v) { return noteName (roundToInt (v)); };
    sliceKeyBox.setRange (0.0, 127.0);
    sliceLenBox.setReadOnly (true);
    sliceLenBox.setRange (0.0, 1.0e9);
    sliceLenBox.format = [] (double ms) { return ms < 1000.0 ? String (roundToInt (ms)) + " MS" : String (ms / 1000.0, 2) + " S"; };
    sliceOnlyButton.setTooltip ("Each slice stops where the next one starts");
    sliceOnlyButton.onClick = [this] { proc.setSlicesToEnd (false); };
    toEndButton.setTooltip ("Each slice plays on to the end of the sample");
    toEndButton.onClick = [this] { proc.setSlicesToEnd (true); };

    // everything in the scrolling stack
    stackItems = { &tabButtons[0], &tabButtons[1], &wave,
                   &loopButtons[0], &loopButtons[1], &loopButtons[2], &dirButtons[0], &dirButtons[1], &dirButtons[2],
                   &startBox, &endBox, &loopStartBox, &loopEndBox, &fadeBox, &randomButton, &randomBox, &zeroButton, &snapButton, &zoomAllButton,
                   &amountBox, &mixBox, &whereButtons[0], &whereButtons[1], &closeUp, &undoWarpButton, &applyButton,
                   &findHitsButton, &sensBox, &evenButtons[0], &evenButtons[1], &evenButtons[2], &evenButtons[3], &firstKeyBox, &toZonesButton,
                   &copyEnvButton, &semi, &tune, &keepSpeed, &grain, &gain, &pan, &attack, &decay, &sustain, &release,
                   &polyButton, &monoButton, &legatoButton, &glide,
                   &keymap, &stackButton, &overlapButton, &rootBox, &loKeyBox, &hiKeyBox, &loVelBox, &hiVelBox, &addZoneButton, &autoMapButton, &dupButton, &deleteButton,
                   &sliceKeyBox, &sliceLenBox, &sliceOnlyButton, &toEndButton };
    for (auto& b : shapeButtons)
        stackItems.push_back (&b);
    for (auto* c : stackItems)
        stack->addChildComponent (c);
    for (EmuPanel* e : { (EmuPanel*) &emuFilter, (EmuPanel*) &emuEnv, (EmuPanel*) &emuLfo, (EmuPanel*) &emuCords })
    {
        e->onFold = [this] { layoutStack(); };
        stack->addChildComponent (e);
    }

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

void EditorPage::editDesign (std::function<void (emu::Design&)> change)
{
    const auto* z = proc.getSelectedZone();
    if (z == nullptr || z->params.design == nullptr)
        return;
    auto q = z->params;
    auto d = std::make_shared<emu::Design> (*q.design);
    change (*d);
    emu::adsrFromAmp (*d, q.attack, q.decay, q.sustain, q.release);   // the VOICE panel's ADSR shows the same envelope
    q.design = d;
    proc.setZoneParams (z->id, q);
}

void EditorPage::edit (std::function<void (ZoneParams&)> change, bool adsr)
{
    if (const auto* z = proc.getSelectedZone())
    {
        auto q = z->params;
        change (q);
        if (adsr && q.design != nullptr)
        {
            // EMU engine zones: the ADSR faders write the 6-stage amp envelope
            auto d = std::make_shared<emu::Design> (*q.design);
            emu::setAmpFromAdsr (*d, q.attack, q.decay, q.sustain, q.release);
            q.design = d;
        }
        if (proc.isSplicer())
            proc.setSliceVoice (q);   // the VOICE panel is for all slices
        else
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

EditorPage::Tab EditorPage::shownTab() const
{
    if (tab == Tab::warp)
        return Tab::warp;
    return proc.isSplicer() ? Tab::slices : Tab::loop;
}

edit::Warp EditorPage::currentWarp() const
{
    auto w = warp;
    w.from = 0;
    w.to = -1;
    if (const auto* z = proc.getSelectedZone(); z != nullptr && z->sample != nullptr && warpPart)
    {
        const auto& q = z->params;
        if (proc.isSplicer())
        {
            w.from = q.sampleStart;
            w.to = q.sampleEnd > 0 ? q.sampleEnd : z->sample->length();
        }
        else if (q.loopMode != LoopMode::off)
        {
            w.from = q.loopStart;
            w.to = q.loopEnd;
        }
    }
    return w;
}

void EditorPage::updateWarpPreview()
{
    const auto w = currentWarp();
    wave.setWarp (w);
    int centre = 0;
    if (const auto* z = proc.getSelectedZone(); z != nullptr && z->sample != nullptr)
    {
        const int n = z->sample->length();
        centre = w.to > 0 ? (w.from + w.to) / 2 : (z->params.loopMode != LoopMode::off ? (z->params.loopStart + z->params.loopEnd) / 2 : n * 3 / 10);
    }
    closeUp.setWarp (w, centre);
}

void EditorPage::showTab (Tab t) { tab = t; refresh(); }
void EditorPage::setPanelShown (int panel, bool s) { shown[jlimit (0, 2, panel)] = s; refresh(); }
void EditorPage::scrollTo (int y) { viewport.setViewPosition (0, y); }

void EditorPage::refresh()
{
    const auto* z = proc.getSelectedZone();
    const bool has = z != nullptr, hasAudio = has && z->sample != nullptr;
    const bool splicer = proc.isSplicer();
    const auto t = shownTab();

    //== Strip
    modeButtons[0].setToggleState (! splicer, dontSendNotification);
    modeButtons[1].setToggleState (splicer, dontSendNotification);
    zoneName.refresh();
    for (int i = 0; i < 3; ++i)
    {
        showButtons[i].setToggleState (shown[i], dontSendNotification);
        interpButtons[i].setToggleState ((int) proc.getInterpolation() == i, dontSendNotification);
    }
    for (auto* b : { &prevButton, &nextButton })
        b->setEnabled (proc.getZones().size() > 1);
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());

    //== SAMPLE
    tabButtons[0].setButtonText (splicer ? "SLICES" : "LOOP");
    tabButtons[1].setButtonText ("WARP");
    tabButtons[0].setToggleState (t != Tab::warp, dontSendNotification);
    tabButtons[1].setToggleState (t == Tab::warp, dontSendNotification);
    tabButtons[0].setTooltip (splicer ? "Cut the sample into slices" : "When and which way the sample loops");
    tabButtons[1].setTooltip ("Reshape the waveform itself, then bake it into the sample");
    wave.setMode (t == Tab::warp ? WaveformView::Mode::warp : t == Tab::slices ? WaveformView::Mode::slices : WaveformView::Mode::loop);

    for (int i = 0; i < 3; ++i)
    {
        loopButtons[i].setToggleState (has && (int) z->params.loopMode == i, dontSendNotification);
        loopButtons[i].setEnabled (hasAudio);
        dirButtons[i].setToggleState (has && (int) z->params.loopDir == i, dontSendNotification);
    }
    const bool looping = hasAudio && z->params.loopMode != LoopMode::off;
    for (Component* c : { (Component*) &dirButtons[0], (Component*) &dirButtons[1], (Component*) &dirButtons[2], (Component*) &loopStartBox,
                          (Component*) &loopEndBox, (Component*) &zeroButton })
        c->setEnabled (looping);
    fadeBox.setEnabled (looping && z->params.loopDir == LoopDir::forward);
    for (Component* c : { (Component*) &startBox, (Component*) &endBox, (Component*) &randomButton, (Component*) &zoomAllButton })
        c->setEnabled (hasAudio);
    snapButton.setToggleState (wave.snapToZero, dontSendNotification);
    randomButton.setToggleState (hasAudio && z->params.randomStart, dontSendNotification);
    randomBox.setEnabled (hasAudio && z->params.randomStart);
    if (hasAudio)
    {
        const auto& q = z->params;
        const int n = z->sample->length();
        for (auto* b : { &startBox, &endBox, &loopStartBox, &loopEndBox, &randomBox })
            b->setRange (0.0, n);
        startBox.setValue (q.sampleStart);
        endBox.setValue (q.sampleEnd > 0 ? q.sampleEnd : n);
        loopStartBox.setValue (q.loopStart);
        loopEndBox.setValue (q.loopEnd);
        randomBox.setValue (q.randomEnd);
        const double msLoop = (q.loopEnd - q.loopStart) / z->sample->sampleRate * 1000.0;
        fadeBox.setRange (0.0, jmax (1.0, std::floor (msLoop)), 1.0);
        fadeBox.setValue (std::round (q.crossfade / z->sample->sampleRate * 1000.0));
    }

    for (int i = 0; i < edit::numWarpShapes; ++i)
        shapeButtons[i].setToggleState ((int) warp.shape == i, dontSendNotification);
    amountBox.setValue (std::round (warp.amount * 100.0f));
    mixBox.setValue (std::round (warp.mix * 100.0f));
    whereButtons[1].setButtonText (splicer ? "SLICE" : "LOOP");
    whereButtons[1].setTooltip (splicer ? "Reshape only the selected slice" : "Reshape only the loop");
    whereButtons[1].setEnabled (splicer || looping);
    if (! whereButtons[1].isEnabled())
        warpPart = false;
    whereButtons[0].setToggleState (! warpPart, dontSendNotification);
    whereButtons[1].setToggleState (warpPart, dontSendNotification);
    applyButton.setEnabled (hasAudio);
    undoWarpButton.setEnabled (has && ! z->warps.isEmpty());

    if (splicer)
    {
        firstKeyBox.setValue (proc.getSplice().firstKey);
        const int count = (int) proc.getSplice().cuts.size();
        for (int i = 0; i < 4; ++i)
            evenButtons[i].setToggleState (count == (4 << i), dontSendNotification);
    }

    //== VOICE
    for (Component* c : { (Component*) &semi, (Component*) &tune, (Component*) &keepSpeed, (Component*) &gain, (Component*) &pan,
                          (Component*) &attack, (Component*) &decay, (Component*) &sustain, (Component*) &release })
        c->setEnabled (has);
    if (has)
    {
        const auto& q = z->params;
        tune.setValue (q.tuneCents);
        semi.setValue (q.semitones);
        grain.setValue (q.grainMs);
        keepSpeed.setToggleState (q.keepSpeed, dontSendNotification);
        gain.setValue (q.gainDb); pan.setValue (q.pan);
        attack.setValue (q.attack); decay.setValue (q.decay); sustain.setValue (q.sustain); release.setValue (q.release);
    }
    grain.setEnabled (has && z->params.keepSpeed);
    copyEnvButton.setEnabled (has && proc.getZones().size() > 1);

    const auto& vs = proc.getVoiceSettings();
    polyButton.setToggleState (! vs.mono, dontSendNotification);
    monoButton.setToggleState (vs.mono, dontSendNotification);
    legatoButton.setToggleState (vs.legato, dontSendNotification);
    legatoButton.setEnabled (vs.mono);
    glide.setValue (vs.glide);
    glide.setEnabled (vs.mono);

    //== KEYMAP
    stackButton.setToggleState (KeymapView::stacked, dontSendNotification);
    overlapButton.setToggleState (! KeymapView::stacked, dontSendNotification);
    for (Component* c : { (Component*) &rootBox, (Component*) &loKeyBox, (Component*) &hiKeyBox, (Component*) &loVelBox, (Component*) &hiVelBox,
                          (Component*) &dupButton, (Component*) &deleteButton, (Component*) &autoMapButton })
        c->setEnabled (has);
    if (has)
    {
        const auto& q = z->params;
        rootBox.setValue (q.root); loKeyBox.setValue (q.loKey); hiKeyBox.setValue (q.hiKey);
        loVelBox.setValue (q.loVel); hiVelBox.setValue (q.hiVel);
        sliceKeyBox.setValue (q.loKey);
        if (hasAudio)
            sliceLenBox.setValue (((q.sampleEnd > 0 ? q.sampleEnd : z->sample->length()) - q.sampleStart) / z->sample->sampleRate * 1000.0);
    }
    sliceOnlyButton.setToggleState (! proc.getSplice().toEnd, dontSendNotification);
    toEndButton.setToggleState (proc.getSplice().toEnd, dontSendNotification);

    const int sens = settings->getKnobSensitivity();
    for (auto* c : { &semi, &tune, &grain, &gain, &pan, &glide })
        c->setSensitivity (sens);

    for (EmuPanel* e : { (EmuPanel*) &emuFilter, (EmuPanel*) &emuEnv, (EmuPanel*) &emuLfo, (EmuPanel*) &emuCords })
        e->refresh();
    updateWarpPreview();
    layoutStack();
    keymap.layoutLanes();
    keymap.repaint();
    wave.repaint();
    stack->repaint();
    repaint();
}

void EditorPage::tick()
{
    wave.tick();
    keymap.tick();
    if (! proc.getFxSetup().routes.empty())
        semi.repaint();   // modulation ring dot
}

//== Layout ==========================================================================
void EditorPage::resized()
{
    auto r = getLocalBounds();
    stripArea = r.removeFromTop (44).toFloat();
    stripTexts.clear();
    {
        auto s = stripArea.toNearestInt().reduced (10, 9);
        auto label = [&] (const String& t, int w)
        {
            stripTexts.push_back ({ s.removeFromLeft (w).toFloat(), t, false, Justification::centredLeft });
        };
        auto place = [&] (Component& c, int w, int gap = 0) { c.setBounds (s.removeFromLeft (w)); s.removeFromLeft (gap); };
        label ("MODE", 40);
        place (modeButtons[0], 76);
        place (modeButtons[1], 72, 14);
        // right side first, so the zone name gets whatever is left
        allButton.setBounds (s.removeFromRight (42));
        s.removeFromRight (12);
        for (int i = 2; i >= 0; --i)
            interpButtons[i].setBounds (s.removeFromRight (i == 2 ? 62 : 56));
        stripTexts.push_back ({ s.removeFromRight (58).toFloat(), "QUALITY", false, Justification::centredLeft });
        s.removeFromRight (10);
        redoButton.setBounds (s.removeFromRight (50));
        s.removeFromRight (4);
        undoButton.setBounds (s.removeFromRight (50));
        s.removeFromRight (14);
        for (int i = 2; i >= 0; --i)
        {
            showButtons[i].setBounds (s.removeFromRight (i == 2 ? 74 : 66));
            s.removeFromRight (i > 0 ? 4 : 0);
        }
        stripTexts.push_back ({ s.removeFromRight (44).toFloat(), "SHOW", false, Justification::centredLeft });
        s.removeFromRight (12);
        place (prevButton, 26, 4);
        nextButton.setBounds (s.removeFromRight (26));
        s.removeFromRight (4);
        zoneName.setBounds (s);
    }
    r.removeFromTop (8);
    viewport.setBounds (r);
    layoutStack();
}

void EditorPage::layoutStack()
{
    const int W = jmax (300, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
    const bool splicer = proc.isSplicer();
    const auto t = shownTab();
    const auto* z = proc.getSelectedZone();

    texts.clear();
    for (auto* c : stackItems)
        c->setVisible (false);
    for (auto& g : groupArea) g = {};
    chipArea = {};
    auto show = [] (Component& c, Rectangle<int> b) { c.setBounds (b); c.setVisible (true); };
    auto text = [this] (Rectangle<int> r, const String& s, bool hint = false, Justification j = Justification::centredLeft)
    {
        texts.push_back ({ r.toFloat(), s, hint, j });
    };
    auto textWidth = [] (const String& s, float h, int weight, float kern)
    {
        GlyphArrangement ga;
        ga.addLineOfText (theme::font (h, weight, kern), s, 0.0f, 0.0f);
        return (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth()) + 2;
    };
    // a row of buttons that belong together; returns the space after them
    auto buttons = [&] (Rectangle<int>& row, std::initializer_list<TextButton*> bs, int gapAfter = 12)
    {
        for (auto* b : bs)
        {
            const int w = textWidth (b->getButtonText(), 9.0f, 700, 0.1f) + 20;
            show (*b, row.removeFromLeft (w));
            row.removeFromLeft (2);
        }
        row.removeFromLeft (gapAfter);
    };
    auto rowLabel = [&] (Rectangle<int>& row, const String& s)
    {
        const int w = textWidth (s, 7.5f, 700, 0.1f) + 8;
        text (row.removeFromLeft (w), s);
    };

    int y = 0;
    const int pad = 10, headH = 22, gap = 8, folded = 2 * pad + headH - 2;

    //== SAMPLE ====================================================================
    {
        String info;
        if (z != nullptr && z->sample != nullptr)
        {
            const auto& s = *z->sample;
            info << s.name << "  -  " << String (roundToInt (s.sampleRate)) << " HZ  -  " << s.audio.getNumChannels() << " CH  -  "
                 << String (s.length() / s.sampleRate, 2) << " S";
            if (! z->warps.isEmpty())
                info << "  -  WARPED";
        }
        const int h = shown[0] ? 300 : folded;
        panelArea[0] = Rectangle<int> (0, y, W, h).toFloat();
        auto inner = Rectangle<int> (pad, y + pad, W - 2 * pad, h - 2 * pad);
        auto head = inner.removeFromTop (headH);
        headArea[0] = head.toFloat();
        texts.push_back ({ head.toFloat(), "SAMPLE|" + (shown[0] ? info : String (splicer ? "loop, warp and slices" : "loop and warp")), false, Justification::centredLeft, 0 });
        if (shown[0])
        {
            {
                auto hr = head;
                show (tabButtons[1], hr.removeFromRight (62).reduced (0, 1));
                hr.removeFromRight (2);
                show (tabButtons[0], hr.removeFromRight (62).reduced (0, 1));
                headArea[0] = head.withRight (hr.getRight() - 8).toFloat();
            }
            inner.removeFromTop (6);
            auto tools = inner.removeFromTop (24);
            inner.removeFromTop (8);
            auto bottom = inner.removeFromBottom (t == Tab::loop ? ValueBox::labelledHeight : 24);
            inner.removeFromBottom (8);
            auto waveArea = inner;

            if (t == Tab::loop)
            {
                rowLabel (tools, "LOOP");
                buttons (tools, { &loopButtons[0], &loopButtons[1], &loopButtons[2] }, 16);
                rowLabel (tools, "DIRECTION");
                buttons (tools, { &dirButtons[0], &dirButtons[1], &dirButtons[2] }, 12);
                if (z != nullptr)
                    text (tools, z->params.loopDir == LoopDir::forward ? "start -> end, again and again"
                               : z->params.loopDir == LoopDir::pingPong ? "forward, then backward, then forward..." : "end -> start, again and again", true);
                show (wave, waveArea);

                auto b = bottom;
                for (auto* vb : { &startBox, &endBox, &loopStartBox, &loopEndBox, &fadeBox })
                {
                    show (*vb, b.removeFromLeft (98));
                    b.removeFromLeft (8);
                }
                b.removeFromLeft (8);
                show (randomButton, b.removeFromLeft (textWidth ("RANDOM START", 9.0f, 700, 0.1f) + 20).removeFromBottom (22));
                b.removeFromLeft (4);
                show (randomBox, b.removeFromLeft (98));
                b.removeFromLeft (16);
                auto rb = b.removeFromBottom (22);
                show (zoomAllButton, rb.removeFromRight (textWidth ("SHOW ALL", 9.0f, 700, 0.1f) + 20));
                rb.removeFromRight (4);
                show (snapButton, rb.removeFromRight (52));
                rb.removeFromRight (4);
                show (zeroButton, rb.removeFromRight (62));
                rb.removeFromRight (10);
                text (rb.withTop (rb.getY() - 14), "drag the LS / LE flags - wheel = zoom", true, Justification::bottomRight);
            }
            else if (t == Tab::warp)
            {
                rowLabel (tools, "SHAPE");
                for (auto& b : shapeButtons)
                {
                    show (b, tools.removeFromLeft (textWidth (b.getButtonText(), 9.0f, 700, 0.1f) + 18));
                    tools.removeFromLeft (2);
                }
                tools.removeFromLeft (14);
                rowLabel (tools, "AMOUNT");
                show (amountBox, tools.removeFromLeft (62));
                tools.removeFromLeft (12);
                rowLabel (tools, "MIX");
                show (mixBox, tools.removeFromLeft (62));
                tools.removeFromLeft (16);
                rowLabel (tools, "WHERE");
                buttons (tools, { &whereButtons[0], &whereButtons[1] });

                show (closeUp, waveArea.removeFromRight (232));
                waveArea.removeFromRight (8);
                show (wave, waveArea);

                auto b = bottom;
                show (applyButton, b.removeFromRight (textWidth ("APPLY TO SAMPLE", 9.0f, 700, 0.1f) + 24));
                b.removeFromRight (4);
                if (z != nullptr && ! z->warps.isEmpty())
                {
                    show (undoWarpButton, b.removeFromRight (56));
                    b.removeFromRight (8);
                    // the warps baked in so far, newest last (as many as fit)
                    String tags;
                    for (int i = jmax (0, z->warps.size() - 4); i < z->warps.size(); ++i)
                        tags << (tags.isEmpty() ? "" : "|") << z->warps[i];
                    const int tw = jmin (b.getWidth() / 2, 4 * 104);
                    chipArea = b.removeFromRight (tw).toFloat();
                    texts.push_back ({ chipArea, tags, false, Justification::centredRight, -1, true });
                }
                text (b, String (edit::warpShapeHint (warp.shape)) + "  -  yellow = how it will sound, red = the sample now", true);
            }
            else
            {
                show (findHitsButton, tools.removeFromLeft (textWidth ("FIND HITS", 9.0f, 700, 0.1f) + 20));
                tools.removeFromLeft (8);
                rowLabel (tools, "SENSITIVITY");
                show (sensBox, tools.removeFromLeft (62));
                tools.removeFromLeft (16);
                rowLabel (tools, "SPLIT EVENLY");
                buttons (tools, { &evenButtons[0], &evenButtons[1], &evenButtons[2], &evenButtons[3] }, 16);
                rowLabel (tools, "FIRST KEY");
                show (firstKeyBox, tools.removeFromLeft (56));
                show (wave, waveArea);

                auto b = bottom;
                show (toZonesButton, b.removeFromRight (textWidth ("TURN SLICES INTO ZONES", 9.0f, 700, 0.1f) + 22));
                b.removeFromRight (8);
                if (z != nullptr)
                {
                    chipArea = b.removeFromRight (120).toFloat();
                    texts.push_back ({ chipArea, "SLICE " + String (proc.getSelectedIndex() + 1) + " -> " + noteName (z->params.loKey), false,
                                       Justification::centredRight, -1, true });
                    b.removeFromRight (8);
                }
                text (b, "click a slice to pick it (or play its key) - drag a cut to move it - double-click to add a cut - right-click a cut to remove it", true);
            }
        }
        y += h + gap;
    }

    //== VOICE =====================================================================
    {
        const int h = shown[1] ? 196 : folded;
        panelArea[1] = Rectangle<int> (0, y, W, h).toFloat();
        auto inner = Rectangle<int> (pad, y + pad, W - 2 * pad, h - 2 * pad);
        auto head = inner.removeFromTop (headH);
        headArea[1] = head.toFloat();
        texts.push_back ({ head.toFloat(), "VOICE|" + String (splicer ? "settings for all slices" : (shown[1] ? "settings of the selected zone" : "pitch, amp, envelope")),
                           false, Justification::centredLeft, 1 });
        if (shown[1])
        {
            if (! splicer)
            {
                show (copyEnvButton, head.removeFromRight (textWidth (copyEnvButton.getButtonText(), 9.0f, 700, 0.1f) + 22).reduced (0, 1));
                headArea[1] = head.withRight (copyEnvButton.getX() - 8).toFloat();
            }
            inner.removeFromTop (8);
            const int g = 8, total = inner.getWidth() - 3 * g;
            const int widths[4] { total * 24 / 100, total * 17 / 100, total * 37 / 100, 0 };
            Rectangle<int> areas[4];
            auto row = inner;
            for (int i = 0; i < 3; ++i)
            {
                areas[i] = row.removeFromLeft (widths[i]);
                row.removeFromLeft (g);
            }
            areas[3] = row;
            const char* titles[] { "PITCH", "AMP", "ENVELOPE", "VOICE" };
            for (int i = 0; i < 4; ++i)
            {
                groupArea[i] = areas[i].toFloat();
                auto a = areas[i].reduced (10, 8);
                text (a.removeFromTop (12), titles[i]);
                areas[i] = a;
            }
            auto cells = [&] (Rectangle<int> a, std::initializer_list<Component*> items, int w, int hgt)
            {
                const int step = a.getWidth() / (int) items.size();
                int i = 0;
                for (auto* c : items)
                    show (*c, Rectangle<int> (a.getX() + step * i++, a.getY(), step, a.getHeight()).withSizeKeepingCentre (jmin (w, step), hgt));
            };
            {
                auto a = areas[0];
                const int step = a.getWidth() / 3;
                cells (a.removeFromLeft (step * 2), { &semi, &tune }, 62, semi.getPreferredHeight());
                auto col = a.withSizeKeepingCentre (jmin (a.getWidth(), 86), 26 + 6 + grain.getPreferredHeight());
                show (keepSpeed, col.removeFromTop (24).withSizeKeepingCentre (jmin (col.getWidth(), 84), 24));
                col.removeFromTop (4);
                show (grain, col.withSizeKeepingCentre (62, grain.getPreferredHeight()));
            }
            cells (areas[1], { &gain, &pan }, 62, gain.getPreferredHeight());
            cells (areas[2], { &attack, &decay, &sustain, &release }, 54, jmin (areas[2].getHeight(), attack.getPreferredHeight()));
            {
                auto a = areas[3];
                auto top = a.removeFromTop (24);
                buttons (top, { &polyButton, &monoButton, &legatoButton }, 0);
                a.removeFromTop (4);
                show (glide, a.withSizeKeepingCentre (62, jmin (a.getHeight(), glide.getPreferredHeight())));
            }
        }
        y += h + gap;
    }

    //== EMU ENGINE (V3) ===========================================================
    for (EmuPanel* e : { (EmuPanel*) &emuFilter, (EmuPanel*) &emuEnv, (EmuPanel*) &emuLfo, (EmuPanel*) &emuCords })
    {
        const bool use = ! splicer && (e == &emuFilter || (z != nullptr && z->params.design != nullptr));
        e->setVisible (use);
        if (! use)
            continue;
        const int h = e->getPreferredHeight();
        e->setBounds (0, y, W, h);
        y += h + gap;
    }

    //== KEYMAP ====================================================================
    {
        const int mapH = shown[2] ? keymap.getPreferredHeight() : 0;
        const int h = shown[2] ? pad + headH + 8 + mapH + pad : folded;
        panelArea[2] = Rectangle<int> (0, y, W, h).toFloat();
        auto inner = Rectangle<int> (pad, y + pad, W - 2 * pad, h - 2 * pad);
        auto head = inner.removeFromTop (headH);
        headArea[2] = head.toFloat();
        texts.push_back ({ head.toFloat(), "KEYMAP|" + String (splicer ? "one slice per key - click a slice or play its key"
                                                                       : KeymapView::stacked ? "every zone on its own lane - drag to move, edges resize - shift-click a key sets the root"
                                                                                             : "key across, velocity up - drag to move, edges resize - click again for the zone below - shift-click a key sets the root"),
                           false, Justification::centredLeft, 2 });
        if (shown[2])
        {
            inner.removeFromTop (8);
            auto side = inner.removeFromRight (246);
            inner.removeFromRight (10);
            show (keymap, inner);
            keymap.layoutLanes();

            const int colW = (side.getWidth() - 8) / 2;
            auto pair = [&] (ValueBox& a, ValueBox* b)
            {
                auto row = side.removeFromTop (ValueBox::labelledHeight);
                show (a, row.removeFromLeft (colW));
                if (b != nullptr)
                    show (*b, row.removeFromRight (colW));
                side.removeFromTop (6);
            };
            if (splicer)
            {
                text (side.removeFromTop (14), "SLICE " + String (proc.getSelectedIndex() + 1) + " OF " + String ((int) proc.getZones().size()));
                side.removeFromTop (4);
                pair (sliceKeyBox, &sliceLenBox);
                auto row = side.removeFromTop (24);
                buttons (row, { &sliceOnlyButton, &toEndButton }, 0);
                side.removeFromTop (6);
                text (side.removeFromTop (28), "Every slice plays its own part of the sample at its original pitch.", true, Justification::topLeft);
            }
            else
            {
                {
                    auto row = side.removeFromTop (24);
                    buttons (row, { &stackButton, &overlapButton }, 0);
                    side.removeFromTop (8);
                }
                text (side.removeFromTop (14), z != nullptr ? "ZONE " + String (proc.getSelectedIndex() + 1) + "  -  drag the values" : String ("NO ZONE"));
                side.removeFromTop (4);
                pair (rootBox, nullptr);
                pair (loKeyBox, &hiKeyBox);
                pair (loVelBox, &hiVelBox);
                auto row = side.removeFromTop (24);
                buttons (row, { &addZoneButton, &autoMapButton }, 0);
                side.removeFromTop (4);
                row = side.removeFromTop (24);
                buttons (row, { &dupButton, &deleteButton }, 0);
            }
        }
        y += h;
    }

    stack->setSize (W, y + 4);
    stack->repaint();
}

void EditorPage::stackClicked (Point<int> pos)
{
    for (int i = 0; i < 3; ++i)
        if (headArea[i].contains (pos.toFloat()))
        {
            setPanelShown (i, ! shown[i]);
            return;
        }
}

//== Painting =========================================================================
void EditorPage::paint (Graphics& g)
{
    theme::drawPanel (g, stripArea.reduced (1.0f));
    g.setFont (theme::font (7.5f, 700, 0.12f));
    g.setColour (theme::textMuted);
    for (auto& t : stripTexts)
        g.drawText (t.text, t.r, t.just, false);
    // thin separators between the strip's groups
    g.setColour (Colours::black.withAlpha (0.45f));
    for (auto* c : { (Component*) &modeButtons[1], (Component*) &nextButton })
        g.fillRect ((float) c->getRight() + 7.0f, stripArea.getY() + 11.0f, 1.0f, stripArea.getHeight() - 22.0f);
}

void EditorPage::paintStack (Graphics& g)
{
    for (int i = 0; i < 3; ++i)
        if (! panelArea[i].isEmpty())
            theme::drawPanel (g, panelArea[i].reduced (1.0f));
    for (auto& a : groupArea)
        if (! a.isEmpty())
            theme::drawInset (g, a);

    for (auto& t : texts)
    {
        if (t.chips)
        {
            // little rounded tags, right-aligned
            auto r = t.r;
            auto items = StringArray::fromTokens (t.text, "|", "");
            g.setFont (theme::font (7.5f, 700, 0.08f));
            for (int i = items.size(); --i >= 0;)
            {
                GlyphArrangement ga;
                ga.addLineOfText (g.getCurrentFont(), items[i], 0, 0);
                const float w = ga.getBoundingBox (0, -1, true).getWidth() + 16.0f;
                if (w > r.getWidth())
                    break;
                auto chip = r.removeFromRight (w).withSizeKeepingCentre (w, 18.0f);
                r.removeFromRight (4.0f);
                const auto c = shownTab() == Tab::warp ? warpYellow : theme::red;
                g.setColour (c.withAlpha (0.12f));
                g.fillRoundedRectangle (chip, 9.0f);
                g.setColour (c.withAlpha (0.8f));
                g.drawRoundedRectangle (chip.reduced (0.5f), 9.0f, 1.0f);
                g.drawText (items[i], chip, Justification::centred, false);
            }
            continue;
        }
        if (t.heading >= 0)
        {
            // a panel title: chevron, then the heading and its hint
            const int bar = t.text.indexOfChar ('|');
            const bool open = shown[t.heading];
            const auto c = Point<float> (t.r.getX() + 5.0f, t.r.getCentreY());
            Path chevron;
            if (open) { chevron.startNewSubPath (c.x - 4.0f, c.y - 2.0f); chevron.lineTo (c.x, c.y + 2.5f); chevron.lineTo (c.x + 4.0f, c.y - 2.0f); }
            else      { chevron.startNewSubPath (c.x - 2.0f, c.y - 4.0f); chevron.lineTo (c.x + 2.5f, c.y); chevron.lineTo (c.x - 2.0f, c.y + 4.0f); }
            g.setColour (theme::textStrong);
            g.strokePath (chevron, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
            theme::drawHeading (g, t.r.withTrimmedLeft (16.0f).withSizeKeepingCentre (t.r.getWidth() - 16.0f, 16.0f).withX (t.r.getX() + 16.0f),
                                t.text.substring (0, bar), t.text.substring (bar + 1));
            continue;
        }
        g.setColour (theme::textMuted);
        if (t.hint)
            g.setFont (theme::font (8.0f, 500, 0.04f));
        else
            g.setFont (theme::font (7.5f, 700, 0.12f));
        g.drawFittedText (t.text, t.r.toNearestInt(), t.just, t.hint && t.r.getHeight() > 26.0f ? 3 : 1, 1.0f);
    }
}
} // namespace emx
