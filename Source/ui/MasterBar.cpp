// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MasterBar.h"
#include "Theme.h"
#include "FxPage.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

MasterBar::MasterBar (EmulonXProcessor& p) : proc (p)
{
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.getProperties().set ("emxDrawnByParent", true);   // the look-and-feel leaves it to us
    slider.setSliderSnapsToMousePosition (false);   // no sudden jumps in volume: drag from where it is
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, masterParamId, slider);
    if (auto* prm = p.apvts.getParameter (masterParamId))
        slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };

    // right-click to modulate it, or click it while a macro is waiting to be linked
    const fx::Target volTarget { fx::Target::masterVolume, 0 };
    slider.onRightClick = [this, volTarget] { modui::showMenu (proc, volTarget, this); };
    slider.interceptClick = [this, volTarget] { return modui::tryLearn (proc, volTarget); };
    slider.setTooltip ("Master volume - drag, double-click resets, right-click to modulate. The bars inside show the output level.");
    addAndMakeVisible (slider);
}

MasterBar::~MasterBar() = default;

void MasterBar::tick()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const float pk = proc.takePeak (ch);
        level[ch] = jmax (pk, level[ch] * 0.86f);
        if (pk >= 1.0f)
            clipUntil = Time::getMillisecondCounterHiRes() + 1500.0;
    }
    repaint();
}

float MasterBar::peakDb() const { return Decibels::gainToDecibels (jmax (level[0], level[1]), -100.0f); }

Rectangle<float> MasterBar::track() const { return slider.getBounds().toFloat().reduced (2.0f, 5.0f); }

void MasterBar::resized()
{
    auto r = getLocalBounds();
    labelArea = r.removeFromLeft (30).toFloat();
    readoutArea = r.removeFromRight (64).toFloat().withSizeKeepingCentre (64.0f, 24.0f);
    r.removeFromRight (6);
    slider.setBounds (r);
}

void MasterBar::paint (Graphics& g)
{
    g.setColour (theme::isXp() ? Colours::white : theme::textMuted);   // XP: on the blue title band
    g.setFont (theme::font (8.0f, 700, 0.12f));
    g.drawText ("VOL", labelArea, Justification::centredLeft, false);

    const auto t = track();
    theme::drawLed (g, t, {});

    // stereo meter inside the track: left on top, right below (-60 dB .. 0 dB)
    auto inner = t.reduced (3.0f, 3.0f);
    const float barH = (inner.getHeight() - 2.0f) * 0.5f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = Decibels::gainToDecibels (level[ch], -60.0f);
        const float w = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * inner.getWidth();
        if (w < 0.5f)
            continue;
        const auto bar = Rectangle<float> (inner.getX(), inner.getY() + (float) ch * (barH + 2.0f), w, barH);
        ColourGradient grad (theme::hue (0xff6a0808), inner.getX(), 0.0f, theme::hue (0xffffd0d0), inner.getRight(), 0.0f, false);
        grad.addColour (0.7, theme::red);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar, 1.5f);
    }
    if (Time::getMillisecondCounterHiRes() < clipUntil)   // went over 0 dB
    {
        g.setColour (Colours::white);
        g.fillRoundedRectangle (inner.removeFromRight (3.0f), 1.0f);
    }

    // modulation: range along the bottom edge, current position as a small tick
    const float base = (float) slider.valueToProportionOfLength (slider.getValue());
    const auto mod = modui::rings (proc, { fx::Target::masterVolume, 0 }, base);
    const auto xAt = [&t] (float n) { return t.getX() + 4.0f + jlimit (0.0f, 1.0f, n) * (t.getWidth() - 8.0f); };
    for (size_t j = 0; j < mod.rings.size() && j < 3; ++j)
    {
        const auto& ring = mod.rings[j];
        const float a = xAt (base), b = xAt (base + ring.amount);
        g.setColour ((ring.envelope ? theme::hue (0xffffd6d6) : theme::hue (0xffffb030)).withAlpha (0.9f));
        g.fillRoundedRectangle (jmin (a, b), t.getBottom() + 1.5f + (float) j * 3.0f, jmax (2.0f, std::abs (b - a)), 2.0f, 1.0f);
    }

    // volume thumb
    const float x = xAt (base);
    const auto thumb = Rectangle<float> (x - 2.5f, t.getY() - 3.0f, 5.0f, t.getHeight() + 6.0f);
    if (theme::isXp())
    {
        g.setColour (theme::getSkin() == theme::Skin::xpLight ? Colour (0xff1c3f94) : Colours::white);
        g.fillRoundedRectangle (thumb, 2.0f);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (thumb, 2.0f, 1.0f);
    }
    else
    {
        DropShadow (theme::red.withAlpha (0.7f), 6, {}).drawForRectangle (g, thumb.toNearestInt());
        g.setColour (Colours::white);
        g.fillRoundedRectangle (thumb, 2.0f);
    }
    if (mod.current >= 0.0f && ! mod.rings.empty())
    {
        g.setColour (theme::textStrong.withAlpha (0.85f));
        g.fillEllipse (xAt (mod.current) - 2.0f, t.getCentreY() - 2.0f, 4.0f, 4.0f);
    }

    // readout: the volume while you adjust it, otherwise the output peak
    const bool adjusting = slider.isMouseButtonDown() || slider.isMouseOver();
    const float pk = peakDb();
    const String text = adjusting ? slider.getTextFromValue (slider.getValue())
                                  : (pk < -59.0f ? String ("-INF DB") : (pk > 0 ? "+" : "") + String (pk, 1) + " DB");
    theme::drawLed (g, readoutArea, text.toUpperCase(), 9.0f, Justification::centred,
                    ! adjusting && Time::getMillisecondCounterHiRes() < clipUntil ? (theme::isXp() ? Colour (0xffd03030) : Colours::white) : theme::red);
}
} // namespace emx
