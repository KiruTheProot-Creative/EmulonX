// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PlayKeys.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

namespace
{
const bool blackKey[]   { false, true, false, true, false, false, true, false, true, false, true, false };
const int  whiteIndex[] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
int whiteNumber (int n) { return (n / 12) * 7 + whiteIndex[n % 12]; }
}

PlayKeys::PlayKeys (EmulonXProcessor& p) : proc (p)
{
    setTooltip ("Click to play (lower on the key = louder). Wheels: pitch (springs back) and modulation. Arrows shift the octaves.");
}

void PlayKeys::resized()
{
    auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
    for (int i = 0; i < 2; ++i)
    {
        wheelArea[i] = r.removeFromLeft (22.0f);
        r.removeFromLeft (6.0f);
    }
    auto oct = r.removeFromLeft (20.0f);
    octUp = oct.removeFromTop (oct.getHeight() * 0.5f).reduced (0.0f, 2.0f);
    octDown = oct.reduced (0.0f, 2.0f);
    r.removeFromLeft (6.0f);
    keysArea = r;
    keyW = keysArea.getWidth() / (float) jmax (1, whiteNumber (lastNote) - whiteNumber (firstNote) + 1);
}

PlayKeys::KeyRect PlayKeys::keyRect (int n) const
{
    const int wi = whiteNumber (n) - whiteNumber (firstNote);
    if (! blackKey[n % 12])
        return { keysArea.getX() + (float) wi * keyW, keyW, false };
    const float bw = keyW * 0.62f;
    return { keysArea.getX() + (float) (wi + 1) * keyW - bw * 0.5f, bw, true };
}

int PlayKeys::noteAt (Point<float> pt) const
{
    if (! keysArea.contains (pt))
        return -1;
    if (pt.y < keysArea.getY() + keysArea.getHeight() * 0.62f)
        for (int n = firstNote; n <= lastNote; ++n)
            if (blackKey[n % 12])
                if (const auto k = keyRect (n); pt.x >= k.x && pt.x < k.x + k.w)
                    return n;
    for (int n = firstNote; n <= lastNote; ++n)
        if (! blackKey[n % 12])
            if (const auto k = keyRect (n); pt.x >= k.x && pt.x < k.x + k.w)
                return n;
    return -1;
}

int PlayKeys::velocityAt (float y, int note) const
{
    const float keyH = keysArea.getHeight() * (blackKey[note % 12] ? 0.62f : 1.0f);
    return jlimit (1, 127, roundToInt (20.0f + 107.0f * (y - keysArea.getY()) / jmax (1.0f, keyH)));
}

void PlayKeys::tick()
{
    std::bitset<128> now;
    for (int n = 0; n < 128; ++n)
        now[(size_t) n] = proc.keyboardState.isNoteOnForChannels (0xffff, n);
    const float b = proc.getUiPitchBend(), m = proc.getUiModWheel();
    if (now != held || std::abs (b - shownBend) > 0.001f || std::abs (m - shownMod) > 0.001f)
    {
        held = now;
        shownBend = b;
        shownMod = m;
        repaint();
    }
}

void PlayKeys::paint (Graphics& g)
{
    theme::drawPanel (g, getLocalBounds().toFloat().reduced (1.0f));

    // wheels: a dark slot with a lit bar where the wheel sits
    for (int i = 0; i < 2; ++i)
    {
        const auto w = wheelArea[i];
        theme::drawLed (g, w, {});
        const float v = i == 0 ? (shownBend + 1.0f) * 0.5f : shownMod;
        const float y = w.getBottom() - 6.0f - v * (w.getHeight() - 12.0f);
        // ridges, like a real wheel
        g.setColour (theme::textMuted.withAlpha (0.18f));
        for (float ry = w.getY() + 4.0f; ry < w.getBottom() - 3.0f; ry += 5.0f)
            g.fillRect (w.getX() + 4.0f, ry, w.getWidth() - 8.0f, 1.0f);
        const auto bar = Rectangle<float> (w.getX() + 3.0f, y - 3.0f, w.getWidth() - 6.0f, 6.0f);
        if (! theme::isLightSkin())
            DropShadow (theme::red.withAlpha (0.8f), 6, {}).drawForRectangle (g, bar.toNearestInt());
        g.setColour (theme::red);
        g.fillRoundedRectangle (bar, 1.5f);
        if (i == 0)
        {
            g.setColour (theme::textMuted.withAlpha (0.5f));
            g.fillRect (w.getX() + 1.0f, w.getCentreY() - 0.5f, 2.5f, 1.0f);
        }
    }
    g.setColour (theme::textMuted);
    g.setFont (theme::font (7.0f, 700, 0.1f));

    // octave arrows
    for (int i = 0; i < 2; ++i)
    {
        const auto r = i == 0 ? octUp : octDown;
        theme::drawLed (g, r, {});
        Path tri;
        const auto c = r.getCentre();
        if (i == 0) tri.addTriangle (c.x - 4.0f, c.y + 2.5f, c.x + 4.0f, c.y + 2.5f, c.x, c.y - 3.0f);
        else        tri.addTriangle (c.x - 4.0f, c.y - 2.5f, c.x + 4.0f, c.y - 2.5f, c.x, c.y + 3.0f);
        const bool canMove = i == 0 ? lastNote < 127 : firstNote > 0;
        g.setColour (canMove ? theme::red : theme::redDark);
        g.fillPath (tri);
    }

    // keys
    for (int black = 0; black < 2; ++black)
        for (int n = firstNote; n <= lastNote; ++n)
        {
            if (blackKey[n % 12] != (black == 1))
                continue;
            const auto k = keyRect (n);
            const auto r = Rectangle<float> (k.x, keysArea.getY(), k.w, keysArea.getHeight() * (k.black ? 0.62f : 1.0f));
            Colour top, bottom;
            if (k.black) { top = Colour (0xff3a373f); bottom = Colour (0xff09080b); }   // real piano colours in every skin
            else         { top = Colour (theme::isMetal() ? 0xffc6d6e6 : 0xffcfc9c9); bottom = Colour (theme::isMetal() ? 0xfff1f6fb : 0xfff2eeee); }
            if (keyTint)
                if (const auto t = keyTint (n); ! t.isTransparent())
                {
                    top = k.black ? t.darker (0.3f) : t.interpolatedWith (Colours::white, 0.45f);
                    bottom = k.black ? t.darker (1.2f) : t.interpolatedWith (Colours::white, 0.7f);
                }
            if (held[(size_t) n]) { top = theme::hue (0xffff6a6a); bottom = theme::hue (0xffc01818); }
            g.setGradientFill (ColourGradient::vertical (top, r.getY(), bottom, r.getBottom()));
            g.fillRect (r);
            g.setColour (Colours::black.withAlpha (0.7f));
            g.drawRect (r, 1.0f);
            if (n % 12 == 0 && ! k.black)
            {
                g.setColour (Colour (0xff6a6060));
                g.setFont (theme::font (7.0f, 700, 0.02f));
                g.drawText (noteName (n), r.withTrimmedTop (r.getHeight() - 12.0f), Justification::centred, false);
            }
        }
}

void PlayKeys::setWheel (int which, float y)
{
    const auto w = wheelArea[which];
    const float v = jlimit (0.0f, 1.0f, (w.getBottom() - 6.0f - y) / jmax (1.0f, w.getHeight() - 12.0f));
    if (which == 0) proc.setUiPitchBend (v * 2.0f - 1.0f);
    else            proc.setUiModWheel (v);
}

void PlayKeys::mouseDown (const MouseEvent& e)
{
    dragWheel = -1;
    for (int i = 0; i < 2; ++i)
        if (wheelArea[i].expanded (3.0f).contains (e.position))
        {
            dragWheel = i;
            setWheel (i, e.position.y);
            return;
        }
    if (octUp.contains (e.position) || octDown.contains (e.position))
    {
        const int d = octUp.contains (e.position) ? 12 : -12;
        if (firstNote + d >= 0 && lastNote + d <= 127)
        {
            firstNote += d;
            lastNote += d;
            resized();
            repaint();
        }
        return;
    }
    const int note = noteAt (e.position);
    if (note < 0)
        return;
    playingNote = note;
    proc.keyboardState.noteOn (proc.getKeyboardChannel(), note, (float) velocityAt (e.position.y, note) / 127.0f);
}

void PlayKeys::mouseDrag (const MouseEvent& e)
{
    if (dragWheel >= 0)
    {
        setWheel (dragWheel, e.position.y);
        return;
    }
    if (playingNote < 0)
        return;
    const int note = noteAt (e.position);
    if (note >= 0 && note != playingNote)
    {
        proc.keyboardState.noteOff (proc.getKeyboardChannel(), playingNote, 0.0f);
        playingNote = note;
        proc.keyboardState.noteOn (proc.getKeyboardChannel(), note, (float) velocityAt (e.position.y, note) / 127.0f);
    }
}

void PlayKeys::mouseUp (const MouseEvent&)
{
    if (dragWheel == 0)
        proc.setUiPitchBend (0.0f);   // the pitch wheel springs back
    dragWheel = -1;
    if (playingNote >= 0)
        proc.keyboardState.noteOff (proc.getKeyboardChannel(), playingNote, 0.0f);
    playingNote = -1;
}
} // namespace emx
