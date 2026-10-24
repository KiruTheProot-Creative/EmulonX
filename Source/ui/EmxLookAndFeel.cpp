// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EmxLookAndFeel.h"
#include "Theme.h"

namespace emx
{
using namespace juce;

EmxLookAndFeel::EmxLookAndFeel() { refreshColours(); }

void EmxLookAndFeel::refreshColours()
{
    setColour (TextButton::textColourOffId, theme::hue (0xffdddddd));
    setColour (TextButton::textColourOnId, Colours::white);
    setColour (PopupMenu::backgroundColourId, theme::hue (0xff141217));
    setColour (PopupMenu::textColourId, theme::hue (0xffff5050));
    setColour (PopupMenu::highlightedBackgroundColourId, theme::hue (0xffa01010));
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (Label::textColourId, theme::red);
    setColour (Label::textWhenEditingColourId, theme::red);
    setColour (TextEditor::backgroundColourId, theme::ledBg);
    setColour (TextEditor::textColourId, theme::red);
    setColour (TextEditor::highlightColourId, theme::red.withAlpha (0.35f));
    setColour (TextEditor::outlineColourId, Colours::black);
    setColour (TextEditor::focusedOutlineColourId, theme::red);
    setColour (CaretComponent::caretColourId, theme::red);
    setColour (ComboBox::textColourId, theme::red);
    setColour (ComboBox::arrowColourId, theme::red);
    setColour (ListBox::textColourId, theme::red);
    setColour (TreeView::backgroundColourId, Colours::transparentBlack);
    setColour (TreeView::linesColourId, theme::redDark.withAlpha (0.5f));
    setColour (HyperlinkButton::textColourId, theme::red);

    if (theme::isXp())
    {
        const bool light = theme::getSkin() == theme::Skin::xpLight;
        setColour (TextButton::textColourOffId, light ? Colour (0xff101010) : Colour (0xffe8e8e8));
        setColour (TextButton::textColourOnId, light ? Colour (0xff0a2a7a) : Colours::white);
        setColour (PopupMenu::backgroundColourId, light ? Colours::white : Colour (0xff2b2b2b));
        setColour (PopupMenu::textColourId, light ? Colour (0xff101010) : Colour (0xffeeeeee));
        setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff316ac5));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (Label::textColourId, theme::ledText);
        setColour (Label::textWhenEditingColourId, theme::ledText);
        setColour (TextEditor::textColourId, theme::ledText);
        setColour (TextEditor::highlightColourId, Colour (0xff316ac5).withAlpha (0.4f));
        setColour (TextEditor::outlineColourId, theme::edge);
        setColour (CaretComponent::caretColourId, theme::ledText);
        setColour (ComboBox::textColourId, theme::ledText);
        setColour (ListBox::textColourId, theme::ledText);
        setColour (TreeView::linesColourId, theme::edge.withAlpha (0.6f));
        setColour (HyperlinkButton::textColourId, light ? Colour (0xff0b47c4) : Colour (0xff8db6ff));
    }
}

//== Knob: your knob artwork + a glowing pointer ================================
void EmxLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, Slider&)
{
    const auto size = (float) jmin (w, h);
    const auto r = Rectangle<float> (size, size).withCentre (Rectangle<int> (x, y, w, h).toFloat().getCentre());
    if (theme::isXp())
    {
        xpKnob (g, r, startAngle, endAngle, pos);
        return;
    }

    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    const auto angle = startAngle + pos * (endAngle - startAngle);
    if (theme::getSkin() == theme::Skin::custom && theme::customKnobRotates())
    {
        // the skin's knob picture turns itself (its marker points up at the middle position)
        const auto img = theme::image (theme::Img::knob);
        if (img.isValid())
        {
            const auto fit = RectanglePlacement (RectanglePlacement::centred).getTransformToFit (img.getBounds().toFloat(), r);
            theme::drawImageWithShadow (g, img, fit.followedBy (AffineTransform::rotation (angle, r.getCentreX(), r.getCentreY())));
            return;
        }
    }
    if (theme::customShadows())
    {
        const auto img = theme::image (theme::Img::knob);
        theme::drawImageWithShadow (g, img, RectanglePlacement (RectanglePlacement::centred).getTransformToFit (img.getBounds().toFloat(), r));
    }
    else
        g.drawImage (theme::image (theme::Img::knob), r, RectanglePlacement::centred);

    const auto c = r.getCentre();
    // Pointer runs from near the centre (2%) to 42% of the knob size, like the prototype.
    Line<float> line (c.x, c.y - size * 0.02f, c.x, c.y - size * 0.42f);
    Path pointer;
    pointer.addLineSegment (line, jmax (2.0f, size * 0.055f));
    pointer.applyTransform (AffineTransform::rotation (angle, c.x, c.y));

    DropShadow (theme::red.withAlpha (0.9f), roundToInt (size * 0.12f), {}).drawForPath (g, pointer);
    ColourGradient grad (Colours::white, c.x + std::sin (angle) * size * 0.42f, c.y - std::cos (angle) * size * 0.42f,
                         theme::red, c.x, c.y, false);
    g.setGradientFill (grad);
    g.fillPath (pointer);
}

//== Fader: your slider artwork + a metal cap ===================================
void EmxLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                       Slider::SliderStyle style, Slider& s)
{
    if (s.getProperties().contains ("emxDrawnByParent"))
        return;
    juce::ignoreUnused (minPos, maxPos);
    if (style != Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }

    const auto track = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    if (theme::isXp())
    {
        xpFader (g, track, pos);
        return;
    }
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (theme::image (theme::Img::slider), track.withSizeKeepingCentre (jmin ((float) w, h * 0.234f), (float) h),
                 RectanglePlacement::stretchToFit);

    const auto capH = 16.0f;
    auto cap = Rectangle<float> (track.getCentreX() - w * 0.5f - 4.0f, pos - capH * 0.5f, w + 8.0f, capH);
    if (theme::hasImage (theme::Img::faderCap))
    {
        // the skin's fader knob: as wide as the usual cap, its own shape, centred on the value
        const auto img = theme::image (theme::Img::faderCap);
        const float iw = cap.getWidth(), ih = iw * (float) img.getHeight() / (float) jmax (1, img.getWidth());
        const auto dest = Rectangle<float> (iw, ih).withCentre ({ track.getCentreX(), pos });
        theme::drawImageWithShadow (g, img, RectanglePlacement (RectanglePlacement::stretchToFit).getTransformToFit (img.getBounds().toFloat(), dest));
        return;
    }
    g.setGradientFill (ColourGradient::vertical (theme::hue (0xff8a8590), cap.getY(), theme::hue (0xff202025), cap.getBottom()));
    g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (Colours::black);
    g.drawRoundedRectangle (cap, 4.0f, 1.0f);
    g.setColour (theme::red);
    g.fillRect (cap.reduced (3.0f, 0).withSizeKeepingCentre (cap.getWidth() - 6.0f, 2.0f));
}

//== Buttons: brushed metal, glowing red when switched on =======================
void EmxLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    if (theme::isXp())
    {
        xpButton (g, b, highlighted, down);
        return;
    }
    if (const auto which = b.getToggleState() && theme::hasImage (theme::Img::buttonOn) ? theme::Img::buttonOn : theme::Img::button;
        theme::hasImage (which))
    {
        // the skin's button pictures
        auto r = b.getLocalBounds().toFloat().reduced (0.5f, 1.0f);
        if (down)
            r = r.translated (0, 1.0f);
        theme::drawSliced (g, theme::image (which), r, 12);
        if (b.getToggleState() && which == theme::Img::button)   // no "on" picture: tint it
        {
            g.setColour (theme::red.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 4.0f);
        }
        if (highlighted && b.isEnabled())
        {
            g.setColour (Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (r, 4.0f);
        }
        return;
    }
    auto r = b.getLocalBounds().toFloat().reduced (0.5f, 1.0f);
    if (down)
        r = r.translated (0, 1.0f);

    if (b.getToggleState())
    {
        DropShadow (theme::red.withAlpha (0.45f), 10, {}).drawForRectangle (g, r.toNearestInt());
        g.setGradientFill (ColourGradient::vertical (theme::hue (0xffc62828), r.getY(), theme::hue (0xff5a0808), r.getBottom()));
    }
    else
    {
        g.setGradientFill (ColourGradient::vertical (theme::hue (0xff55515a), r.getY(), theme::hue (0xff1d1b20), r.getBottom()));
    }
    g.fillRoundedRectangle (r, 5.0f);

    g.setColour ((b.getToggleState() ? theme::hue (0xffff7a7a) : theme::hue (0xff7a747f)).withAlpha (highlighted ? 1.0f : 0.8f));
    g.fillRect (r.getX() + 4.0f, r.getY() + 1.0f, r.getWidth() - 8.0f, 1.0f);
    g.setColour (Colours::black);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

Font EmxLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return theme::font (jmin (10.0f, buttonHeight * 0.42f), 700, 0.08f);
}

//== Menus =======================================================================
void EmxLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    if (theme::isXp())
    {
        g.fillAll (findColour (PopupMenu::backgroundColourId));
        g.setColour (theme::getSkin() == theme::Skin::xpLight ? Colour (0xffaca899) : Colour (0xff5a5a5a));
        g.drawRect (0, 0, w, h);
        return;
    }
    g.setGradientFill (ColourGradient::vertical (theme::hue (0xff26232a), 0.0f, theme::hue (0xff111014), (float) h));
    g.fillAll();
    g.setColour (Colours::black);
    g.drawRect (0, 0, w, h);
}

Font EmxLookAndFeel::getPopupMenuFont() { return theme::font (11.0f, 500, 0.06f); }

void EmxLookAndFeel::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name)
{
    g.setFont (theme::font (9.0f, 700, 0.14f));
    g.setColour (theme::textStrong);
    g.drawText (theme::isXp() ? name : name.toUpperCase(), area.reduced (12, 0), Justification::bottomLeft, true);
}

Font EmxLookAndFeel::getLabelFont (Label& l)
{
    return theme::font ((float) jmax (8, l.getHeight() / 2), 500, 0.04f);
}

//== Drop-downs: LED box with a red arrow =======================================
void EmxLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h);
    theme::drawLed (g, r, {});
    if (theme::isXp())
    {
        // drop-down button on the right: light blue, dark arrow
        const bool light = theme::getSkin() == theme::Skin::xpLight;
        auto btn = r.removeFromRight ((float) h).reduced (2.0f);
        const bool active = box.isPopupActive();
        g.setGradientFill (ColourGradient::vertical (light ? Colour (active ? 0xff9cb9ee : 0xffc6d7fb) : Colour (active ? 0xff2d4d82 : 0xff40618f), btn.getY(),
                                                     light ? Colour (active ? 0xff7d9fe0 : 0xffa4bff2) : Colour (0xff27406a), btn.getBottom()));
        g.fillRoundedRectangle (btn, 2.0f);
        g.setColour (light ? Colour (0xffb7c9ee) : Colour (0xff1a2c4d));
        g.drawRoundedRectangle (btn.reduced (0.5f), 2.0f, 1.0f);
        const auto a = btn.withSizeKeepingCentre (7.0f, 4.0f);
        Path arrow;
        arrow.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
        g.setColour (box.isEnabled() ? (light ? Colour (0xff4d6185) : Colours::white) : Colour (0xff9a9a9a));
        g.fillPath (arrow);
        return;
    }
    if (box.isPopupActive() || box.hasKeyboardFocus (true))
    {
        g.setColour (theme::red.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }
    const auto a = r.removeFromRight ((float) h).withSizeKeepingCentre (8.0f, 5.0f);
    Path arrow;
    arrow.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
    g.setColour (box.isEnabled() ? theme::red : theme::redDark);
    g.fillPath (arrow);
}

Font EmxLookAndFeel::getComboBoxFont (ComboBox&) { return theme::font (9.5f, 500, 0.05f); }

void EmxLookAndFeel::drawComboBoxTextWhenNothingSelected (Graphics& g, ComboBox& box, Label& label)
{
    // JUCE draws this at half strength, which is hard to read on black
    g.setColour (box.isEnabled() ? theme::textMuted : theme::textMuted.withAlpha (0.5f));
    g.setFont (label.getLookAndFeel().getLabelFont (label));
    g.drawFittedText (box.getTextWhenNothingSelected(), getLabelBorderSize (label).subtractedFrom (label.getLocalBounds()),
                      label.getJustificationType(), jmax (1, (int) ((float) label.getHeight() / label.getFont().getHeight())),
                      label.getMinimumHorizontalScale());
}

void EmxLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (4, 1, box.getWidth() - box.getHeight() - 4, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

PopupMenu::Options EmxLookAndFeel::getOptionsForComboBoxPopupMenu (ComboBox& box, Label& label)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label).withStandardItemHeight (22);
}

//== Explorer tree: small red triangles =========================================
void EmxLookAndFeel::drawTreeviewPlusMinusBox (Graphics& g, const Rectangle<float>& area, Colour, bool isOpen, bool over)
{
    const auto c = area.getCentre();
    if (theme::isXp())
    {
        // [+] / [-] boxes
        const bool light = theme::getSkin() == theme::Skin::xpLight;
        const auto box = Rectangle<float> (9.0f, 9.0f).withCentre (c);
        g.setGradientFill (ColourGradient::vertical (light ? Colours::white : Colour (0xff3a3a3a), box.getY(),
                                                     light ? Colour (0xffd7d4c6) : Colour (0xff262626), box.getBottom()));
        g.fillRect (box);
        g.setColour (over ? Colour (0xff316ac5) : theme::edge);
        g.drawRect (box, 1.0f);
        g.setColour (light ? Colour (0xff101010) : Colour (0xffeeeeee));
        g.fillRect (box.getX() + 2.0f, c.y - 0.5f, 5.0f, 1.0f);
        if (! isOpen)
            g.fillRect (c.x - 0.5f, box.getY() + 2.0f, 1.0f, 5.0f);
        return;
    }
    Path p;
    if (isOpen)
        p.addTriangle (c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
    else
        p.addTriangle (c.x - 2.0f, c.y - 3.5f, c.x - 2.0f, c.y + 3.5f, c.x + 2.5f, c.y);
    g.setColour (over ? theme::hue (0xffff6060) : theme::redDim);
    g.fillPath (p);
}

void EmxLookAndFeel::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical,
                                    int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? Rectangle<int> (x, thumbStart, w, thumbSize) : Rectangle<int> (thumbStart, y, thumbSize, h);
    if (theme::isXp())
    {
        const bool light = theme::getSkin() == theme::Skin::xpLight;
        g.setColour (light ? Colour (0xfff3f1ec) : Colour (0xff232323));
        g.fillRect (x, y, w, h);
        auto t = thumb.toFloat().reduced (1.0f);
        g.setGradientFill (vertical ? ColourGradient::horizontal (light ? Colour (over || down ? 0xffd7e3fd : 0xffc8d7fa) : Colour (over || down ? 0xff5574a6 : 0xff4a6390), t.getX(),
                                                                  light ? Colour (0xffb1c8f6) : Colour (0xff334b74), t.getRight())
                                    : ColourGradient::vertical (light ? Colour (over || down ? 0xffd7e3fd : 0xffc8d7fa) : Colour (over || down ? 0xff5574a6 : 0xff4a6390), t.getY(),
                                                                light ? Colour (0xffb1c8f6) : Colour (0xff334b74), t.getBottom()));
        g.fillRoundedRectangle (t, 2.0f);
        g.setColour (light ? Colour (0xff7f9ddf) : Colour (0xff1e2f4f));
        g.drawRoundedRectangle (t, 2.0f, 1.0f);
        return;
    }
    g.setColour ((down ? theme::red : over ? theme::redDim : theme::redDark).withAlpha (0.8f));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f, 2.0f), 3.0f);
}
//== Tooltips: dark in Classic, pale yellow in XP ===============================
Rectangle<int> EmxLookAndFeel::getTooltipBounds (const String& text, Point<int> screenPos, Rectangle<int> parentArea)
{
    if (! theme::isXp())
        return LookAndFeel_V4::getTooltipBounds (text, screenPos, parentArea);
    const auto f = theme::font (11.0f, 500);
    GlyphArrangement ga;
    ga.addLineOfText (f, text, 0, 0);
    const int w = jmin (420, (int) ga.getBoundingBox (0, -1, true).getWidth() + 14), h = (int) f.getHeight() + 8;
    return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                           screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
               .constrainedWithin (parentArea);
}

void EmxLookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
{
    if (! theme::isXp())
    {
        LookAndFeel_V4::drawTooltip (g, text, width, height);
        return;
    }
    g.fillAll (Colour (0xffffffe1));
    g.setColour (Colour (0xff767676));
    g.drawRect (0, 0, width, height);
    g.setColour (Colours::black);
    g.setFont (theme::font (11.0f, 500));
    g.drawFittedText (text, Rectangle<int> (width, height).reduced (6, 2), Justification::centredLeft, 2);
}

//== EMULON XP drawing ==========================================================
void EmxLookAndFeel::xpButton (Graphics& g, Button& b, bool highlighted, bool down)
{
    const bool light = theme::getSkin() == theme::Skin::xpLight;
    auto r = b.getLocalBounds().toFloat().reduced (0.5f, 1.0f);
    const bool on = b.getToggleState(), enabled = b.isEnabled();

    if (b.getProperties().contains ("emxTab"))
    {
        // taskbar-style buttons on the blue title band
        const Colour top = on ? Colour (0xff1e52b7) : Colour (highlighted ? 0xff5aa0ff : 0xff3c8cf3);
        const Colour bottom = on ? Colour (0xff163f94) : Colour (highlighted ? 0xff3a77e6 : 0xff2563d9);
        g.setGradientFill (ColourGradient::vertical (light ? top : top.darker (0.45f), r.getY(), light ? bottom : bottom.darker (0.45f), r.getBottom()));
        g.fillRoundedRectangle (r, 3.0f);
        if (on)
        {
            g.setGradientFill (ColourGradient::vertical (Colours::black.withAlpha (0.35f), r.getY(), Colours::transparentBlack, r.getY() + 5.0f));
            g.fillRoundedRectangle (r, 3.0f);
        }
        else
        {
            g.setColour (Colours::white.withAlpha (0.35f));
            g.fillRect (r.getX() + 3.0f, r.getY() + 1.0f, r.getWidth() - 6.0f, 1.0f);
        }
        g.setColour (Colour (0xff0f2f7a).withAlpha (0.9f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        b.setColour (TextButton::textColourOffId, Colours::white);
        b.setColour (TextButton::textColourOnId, Colours::white);
        return;
    }

    if (on && enabled)
    {
        // switched on: pale blue, like a pressed toolbar toggle
        g.setGradientFill (ColourGradient::vertical (light ? Colour (0xffe3ecfb) : Colour (0xff3d6fc4), r.getY(),
                                                     light ? Colour (0xffbcd0f2) : Colour (0xff23498f), r.getBottom()));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (light ? Colour (0xff316ac5) : Colour (0xff86aef0));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        return;
    }

    if (light)
        g.setGradientFill (ColourGradient::vertical (down ? Colour (0xffe5e4dd) : Colours::white, r.getY(),
                                                     down ? Colour (0xfff2f1ea) : Colour (0xffe3e0d4), r.getBottom()));
    else
        g.setGradientFill (ColourGradient::vertical (down ? Colour (0xff2c2c2c) : Colour (0xff4c4c4c), r.getY(),
                                                     down ? Colour (0xff3a3a3a) : Colour (0xff2e2e2e), r.getBottom()));
    g.fillRoundedRectangle (r, 3.0f);

    if (highlighted && enabled && ! down)
    {
        // the orange hover glow on the inside edge
        g.setGradientFill (ColourGradient::vertical (Colour (0xfffff0cf), r.getY(), Colour (0xfff8b330), r.getBottom()));
        g.drawRoundedRectangle (r.reduced (1.5f), 2.0f, 2.0f);
    }
    g.setColour (enabled ? (light ? Colour (0xff003c74) : Colour (0xff111111)) : Colour (0xffb5b2a5).withAlpha (light ? 1.0f : 0.4f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void EmxLookAndFeel::xpKnob (Graphics& g, Rectangle<float> r, float startAngle, float endAngle, float pos)
{
    const bool light = theme::getSkin() == theme::Skin::xpLight;
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;

    // value arc around the knob
    const float arcR = rad - 2.0f, arcW = jmax (2.0f, rad * 0.12f);
    Path trackArc, valueArc;
    trackArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    valueArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, startAngle + pos * (endAngle - startAngle), true);
    g.setColour (light ? Colour (0xffd3d0c2) : Colour (0xff3a3a3a));
    g.strokePath (trackArc, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (theme::red);
    g.strokePath (valueArc, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

    // the knob body: a glossy round button
    const auto body = r.reduced (rad * 0.24f);
    g.setGradientFill (ColourGradient (light ? Colours::white : Colour (0xff6a6a6a), body.getX(), body.getY(),
                                       light ? Colour (0xffcfcbbb) : Colour (0xff262626), body.getRight(), body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (light ? Colour (0xff7f9db9) : Colour (0xff111111));
    g.drawEllipse (body, 1.0f);

    // pointer
    const float angle = startAngle + pos * (endAngle - startAngle);
    Path pointer;
    pointer.addLineSegment (Line<float> (c.x, c.y - body.getWidth() * 0.1f, c.x, c.y - body.getWidth() * 0.42f), jmax (2.0f, rad * 0.1f));
    pointer.applyTransform (AffineTransform::rotation (angle, c.x, c.y));
    g.setColour (light ? Colour (0xff1c3f94) : Colours::white);
    g.fillPath (pointer);
}

void EmxLookAndFeel::xpFader (Graphics& g, Rectangle<float> track, float pos)
{
    const bool light = theme::getSkin() == theme::Skin::xpLight;
    // sunken groove
    auto groove = track.withSizeKeepingCentre (4.0f, track.getHeight() - 6.0f);
    g.setColour (light ? Colour (0xffecebe6) : Colour (0xff1a1a1a));
    g.fillRoundedRectangle (groove, 2.0f);
    g.setColour (light ? Colour (0xff9d9c99) : Colour (0xff555555));
    g.drawRoundedRectangle (groove, 2.0f, 1.0f);
    // filled part below the thumb
    g.setColour (theme::red.withAlpha (0.85f));
    g.fillRoundedRectangle (groove.withTop (jlimit (groove.getY(), groove.getBottom(), pos)).reduced (1.0f, 0.0f), 1.5f);
    // thumb
    auto thumb = Rectangle<float> (track.getCentreX() - 11.0f, pos - 6.0f, 22.0f, 12.0f);
    g.setGradientFill (ColourGradient::vertical (light ? Colours::white : Colour (0xff5a5a5a), thumb.getY(),
                                                 light ? Colour (0xffdcdad0) : Colour (0xff2e2e2e), thumb.getBottom()));
    g.fillRoundedRectangle (thumb, 2.0f);
    g.setColour (light ? Colour (0xff1c5180) : Colour (0xff111111));
    g.drawRoundedRectangle (thumb, 2.0f, 1.0f);
    g.setColour (Colour (0xff3ca33c));   // the little green accent of the era
    g.fillRect (thumb.getX() + 2.0f, thumb.getBottom() - 2.5f, thumb.getWidth() - 4.0f, 1.5f);
}
} // namespace emx
