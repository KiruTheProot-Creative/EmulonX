// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EmxLookAndFeel.h"
#include "Theme.h"
#include "SkinDraw.h"

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
    if (theme::isFlat())
    {
        const auto& st = theme::flat();
        const bool emutal = theme::getSkin() == theme::Skin::emutal;
        setColour (TextButton::textColourOffId, st.light ? st.text : st.text.withAlpha (0.85f));
        setColour (TextButton::textColourOnId, st.light ? Colours::white : emutal ? theme::textOn (theme::red) : theme::red.brighter (0.2f));
        setColour (PopupMenu::backgroundColourId, st.panel);
        setColour (PopupMenu::textColourId, st.text);
        setColour (PopupMenu::highlightedBackgroundColourId, st.light ? theme::red : theme::red.withAlpha (0.35f));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        for (auto id : { (int) Label::textColourId, (int) Label::textWhenEditingColourId, (int) TextEditor::textColourId,
                         (int) CaretComponent::caretColourId, (int) ComboBox::textColourId, (int) ListBox::textColourId })
            setColour (id, st.text);
        setColour (TextEditor::outlineColourId, st.displayEdge);
        setColour (TextEditor::focusedOutlineColourId, theme::red);
        setColour (TextEditor::highlightColourId, theme::red.withAlpha (0.35f));
    }
    if (theme::isMetal())
    {
        setColour (TextButton::textColourOffId, Colour (0xff15191c));
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (PopupMenu::backgroundColourId, Colour (0xffeef0f2));
        setColour (PopupMenu::textColourId, Colour (0xff15191c));
        setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff2a9a8f));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (HyperlinkButton::textColourId, Colour (0xff0b5f58));
    }
    if (const auto* d = theme::isCustom() ? theme::customSkin() : nullptr)
    {
        setColour (TextButton::textColourOffId, d->buttonText);
        setColour (TextButton::textColourOnId, d->buttonOnText);
        setColour (PopupMenu::highlightedBackgroundColourId, d->isLight() ? d->accent : d->accent.withAlpha (0.4f));
        setColour (PopupMenu::highlightedTextColourId, d->isLight() ? theme::textOn (d->accent) : Colours::white);
        for (auto id : { (int) Label::textColourId, (int) Label::textWhenEditingColourId, (int) TextEditor::textColourId,
                         (int) CaretComponent::caretColourId, (int) ComboBox::textColourId, (int) ListBox::textColourId })
            setColour (id, d->displayText);
        setColour (HyperlinkButton::textColourId, d->accent);
    }
}

//== Knob: your knob artwork + a glowing pointer ================================
void EmxLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, Slider&)
{
    const auto size = (float) jmin (w, h);
    const auto r = Rectangle<float> (size, size).withCentre (Rectangle<int> (x, y, w, h).toFloat().getCentre());
    if (const auto* d = theme::isCustom() ? theme::customSkin() : nullptr)
    {
        skin::drawKnob (g, *d, r, startAngle, endAngle, pos);
        return;
    }
    if (theme::isXp())
    {
        xpKnob (g, r, startAngle, endAngle, pos);
        return;
    }
    if (theme::isMetal())
    {
        metalKnob (g, r, startAngle, endAngle, pos);
        return;
    }
    if (theme::isFlat())
    {
        flatKnob (g, r, startAngle, endAngle, pos);
        return;
    }

    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (theme::image (theme::Img::knob), r, RectanglePlacement::centred);

    const auto angle = startAngle + pos * (endAngle - startAngle);
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
    if (const auto* d = theme::isCustom() ? theme::customSkin() : nullptr)
    {
        skin::drawFader (g, *d, track, pos);
        return;
    }
    if (theme::isXp())
    {
        xpFader (g, track, pos);
        return;
    }
    if (theme::isMetal())
    {
        metalFader (g, track, pos);
        return;
    }
    if (theme::isFlat())
    {
        flatFader (g, track, pos);
        return;
    }
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (theme::image (theme::Img::slider), track.withSizeKeepingCentre (jmin ((float) w, h * 0.234f), (float) h),
                 RectanglePlacement::stretchToFit);

    const auto capH = 16.0f;
    auto cap = Rectangle<float> (track.getCentreX() - w * 0.5f - 4.0f, pos - capH * 0.5f, w + 8.0f, capH);
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
    if (const auto* d = theme::isCustom() ? theme::customSkin() : nullptr)
    {
        const bool tab = b.getProperties().contains ("emxTab"), on = b.getToggleState(), enabled = b.isEnabled();
        skin::drawButton (g, *d, b.getLocalBounds().toFloat().reduced (0.5f, 1.0f), on, tab, highlighted, down, enabled);
        b.setColour (TextButton::textColourOffId, skin::buttonTextColour (*d, false, tab, enabled));
        b.setColour (TextButton::textColourOnId, skin::buttonTextColour (*d, true, tab, enabled));
        return;
    }
    if (theme::isXp())
    {
        xpButton (g, b, highlighted, down);
        return;
    }
    if (theme::isMetal())
    {
        metalButton (g, b, highlighted, down);
        return;
    }
    if (theme::isFlat())
    {
        flatButton (g, b, highlighted, down);
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
    if (theme::isMetal())
    {
        g.fillAll (Colour (0xffeef0f2));
        g.setColour (Colour (0xff59636c));
        g.drawRect (0, 0, w, h);
        return;
    }
    if (theme::isFlat())
    {
        g.fillAll (theme::flat().panel);
        g.setColour (theme::flat().panelEdge);
        g.drawRect (0, 0, w, h);
        return;
    }
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
    if (theme::isMetal())
    {
        // a small metal box with a dark arrow
        auto btn = r.removeFromRight ((float) h).reduced (2.0f);
        g.setGradientFill (ColourGradient::vertical (Colour (box.isPopupActive() ? 0xffb9c1c8 : 0xffe9edf0), btn.getY(), Colour (0xffa9b1b8), btn.getBottom()));
        g.fillRoundedRectangle (btn, 2.0f);
        g.setColour (Colour (0xff59636c));
        g.drawRoundedRectangle (btn.reduced (0.5f), 2.0f, 1.0f);
        const auto a = btn.withSizeKeepingCentre (7.0f, 4.0f);
        Path arrow;
        arrow.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
        g.setColour (box.isEnabled() ? Colour (0xff1b2126) : Colour (0xff8a939b));
        g.fillPath (arrow);
        return;
    }
    if (theme::isFlat())
    {
        // a thin chevron
        const auto a = r.removeFromRight ((float) h).withSizeKeepingCentre (8.0f, 4.5f);
        Path chevron;
        chevron.startNewSubPath (a.getX(), a.getY());
        chevron.lineTo (a.getCentreX(), a.getBottom());
        chevron.lineTo (a.getRight(), a.getY());
        g.setColour (box.isEnabled() ? (box.isPopupActive() ? theme::red : theme::flat().muted) : theme::flat().muted.withAlpha (0.4f));
        g.strokePath (chevron, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        return;
    }
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
    if (theme::isXp() || theme::isMetal())
    {
        // [+] / [-] boxes
        const bool light = theme::getSkin() == theme::Skin::xpLight || theme::isMetal();
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
    if (! theme::isXp() && ! theme::isFlat())   // EMUL8R counts as flat here
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
    if (theme::isMetal())
    {
        g.fillAll (Colour (0xffffffe1));   // the pale yellow of the era
        g.setColour (Colour (0xff767676));
        g.drawRect (0, 0, width, height);
        g.setColour (Colours::black);
        g.setFont (theme::font (9.5f, 500));
        g.drawFittedText (text, Rectangle<int> (width, height).reduced (6, 2), Justification::centredLeft, 2);
        return;
    }
    if (theme::isFlat())
    {
        const auto& st = theme::flat();
        g.fillAll (st.panel);
        g.setColour (st.panelEdge);
        g.drawRect (0, 0, width, height);
        g.setColour (st.text);
        g.setFont (theme::font (9.5f, 500));
        g.drawFittedText (text, Rectangle<int> (width, height).reduced (6, 2), Justification::centredLeft, 2);
        return;
    }
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
//== EMURUM / EMUTAL drawing ====================================================
void EmxLookAndFeel::flatButton (Graphics& g, Button& b, bool highlighted, bool down)
{
    const auto& st = theme::flat();
    const bool emutal = theme::getSkin() == theme::Skin::emutal;
    const bool on = b.getToggleState(), enabled = b.isEnabled();
    auto r = b.getLocalBounds().toFloat().reduced (0.5f, 1.0f);
    const float rad = jmin (st.radius, 5.0f);

    if (st.outline > 0.0f)
    {
        // a chunky toy button: a darker edge underneath, the face sinks in when pressed
        const bool tab = b.getProperties().contains ("emxTab");
        const float lipH = down ? 1.5f : 3.5f;
        auto face = r.withTrimmedBottom (3.5f).translated (0.0f, down ? 2.0f : 0.0f);
        const auto faceCol = ! enabled ? Colour (0xffe9edf1)
                           : on ? (tab ? theme::red : st.onFill)
                           : highlighted ? st.buttonHover : st.button;
        g.setColour (on && enabled ? faceCol.darker (0.45f) : st.lip);
        g.fillRoundedRectangle (face.translated (0.0f, lipH), rad + 1.0f);
        g.setColour (faceCol);
        g.fillRoundedRectangle (face, rad + 1.0f);
        if (! on)
        {
            g.setColour (Colours::white.withAlpha (0.7f));
            g.fillRoundedRectangle (face.reduced (4.0f, 0.0f).withHeight (2.0f).translated (0.0f, 2.5f), 1.0f);
        }
        g.setColour (enabled ? st.outlineColour : st.outlineColour.withAlpha (0.35f));
        g.drawRoundedRectangle (face.withBottom (face.getBottom() + lipH).reduced (1.0f), rad + 1.0f, 2.0f);
        b.setColour (TextButton::textColourOffId, enabled ? st.text : st.muted);
        b.setColour (TextButton::textColourOnId, Colours::white);
        return;
    }

    if (b.getProperties().contains ("emxTab"))
    {
        if (emutal)
        {
            // text tabs; the active one is underlined in the accent colour
            b.setColour (TextButton::textColourOffId, highlighted ? st.text : st.muted);
            b.setColour (TextButton::textColourOnId, theme::red);
            if (on)
            {
                g.setColour (theme::red);
                g.fillRoundedRectangle (r.getX() + 10.0f, r.getBottom() - 3.0f, r.getWidth() - 20.0f, 2.0f, 1.0f);
            }
            return;
        }
        // boxed tabs with a small indicator light
        g.setColour (highlighted && ! on ? st.buttonHover : st.button);
        g.fillRoundedRectangle (r, rad);
        g.setColour (st.buttonEdge);
        g.drawRoundedRectangle (r, rad, 1.0f);
        auto dot = Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
        if (on)
            DropShadow (theme::red.withAlpha (0.8f), 6, {}).drawForRectangle (g, dot.toNearestInt());
        g.setColour (on ? theme::red : st.knobTrack);
        g.fillEllipse (dot);
        b.setColour (TextButton::textColourOffId, st.muted);
        b.setColour (TextButton::textColourOnId, st.text);
        return;
    }

    if (on && enabled)
    {
        if (emutal)
        {
            g.setColour (down ? theme::red.darker (0.2f) : theme::red);
            g.fillRoundedRectangle (r, rad);
        }
        else
        {
            g.setColour (st.button.brighter (0.08f));
            g.fillRoundedRectangle (r, rad);
            g.setColour (theme::red);
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        }
        return;
    }
    g.setColour (! enabled ? st.button.withAlpha (0.5f) : down ? st.button.darker (0.2f) : highlighted ? st.buttonHover : st.button);
    g.fillRoundedRectangle (r, rad);
    g.setColour (st.buttonEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

void EmxLookAndFeel::flatKnob (Graphics& g, Rectangle<float> r, float startAngle, float endAngle, float pos)
{
    const auto& st = theme::flat();
    const bool emutal = theme::getSkin() == theme::Skin::emutal;
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    if (st.outline > 0.0f)
    {
        // a toy knob: thick ring with an outline, white cap, dark rounded pointer
        const float arcR = rad - 4.0f, arcW = jmax (3.0f, rad * 0.2f);
        Path track, value;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (st.outlineColour);
        g.strokePath (track, PathStrokeType (arcW + 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (st.knobTrack);
        g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (theme::red);
        g.strokePath (value, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
        const auto body = r.reduced (rad * 0.38f);
        g.setColour (Colours::black.withAlpha (0.2f));
        g.fillEllipse (body.translated (0.0f, 2.0f));
        g.setColour (st.knobBody);
        g.fillEllipse (body);
        g.setColour (st.outlineColour);
        g.drawEllipse (body.reduced (1.0f), 2.0f);
        Path pointer;
        pointer.addLineSegment (Line<float> (c.x, c.y - body.getWidth() * 0.08f, c.x, c.y - body.getWidth() * 0.36f), jmax (2.5f, rad * 0.11f));
        pointer.applyTransform (AffineTransform::rotation (angle, c.x, c.y));
        g.fillPath (pointer);
        return;
    }

    // ring: grey track, accent value
    const float arcR = rad - 2.5f, arcW = jmax (2.0f, rad * (emutal ? 0.13f : 0.10f));
    Path track, value;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (st.knobTrack);
    g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (theme::red);
    g.strokePath (value, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

    // body
    const auto body = r.reduced (rad * (emutal ? 0.30f : 0.27f));
    if (emutal)
    {
        g.setColour (st.knobBody);
        g.fillEllipse (body);
        g.setColour (st.bg);
        g.drawEllipse (body, 1.0f);
    }
    else
    {
        g.setGradientFill (ColourGradient (Colour (0xff6a7482), body.getX(), body.getY(), Colour (0xff262c34), body.getRight(), body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (Colour (0xff0b0d10));
        g.drawEllipse (body, 1.0f);
    }
    // pointer
    Path pointer;
    pointer.addLineSegment (Line<float> (c.x, c.y - body.getWidth() * (emutal ? 0.12f : 0.05f), c.x, c.y - body.getWidth() * 0.46f),
                            jmax (1.6f, rad * 0.075f));
    pointer.applyTransform (AffineTransform::rotation (angle, c.x, c.y));
    g.setColour (st.text);
    g.fillPath (pointer);
}

void EmxLookAndFeel::flatFader (Graphics& g, Rectangle<float> track, float pos)
{
    const auto& st = theme::flat();
    if (st.outline > 0.0f)
    {
        auto groove = track.withSizeKeepingCentre (8.0f, track.getHeight() - 8.0f);
        g.setColour (st.knobTrack);
        g.fillRoundedRectangle (groove, 4.0f);
        g.setColour (theme::red);
        g.fillRoundedRectangle (groove.withTop (jlimit (groove.getY(), groove.getBottom(), pos)), 4.0f);
        g.setColour (st.outlineColour);
        g.drawRoundedRectangle (groove, 4.0f, 2.0f);
        const auto thumb = Rectangle<float> (track.getCentreX() - 12.0f, pos - 7.0f, 24.0f, 14.0f);
        g.setColour (Colours::black.withAlpha (0.2f));
        g.fillRoundedRectangle (thumb.translated (0.0f, 2.0f), 4.0f);
        g.setColour (Colours::white);
        g.fillRoundedRectangle (thumb, 4.0f);
        g.setColour (st.outlineColour);
        g.drawRoundedRectangle (thumb.reduced (1.0f), 4.0f, 2.0f);
        return;
    }
    auto groove = track.withSizeKeepingCentre (4.0f, track.getHeight() - 6.0f);
    g.setColour (st.knobTrack);
    g.fillRoundedRectangle (groove, 2.0f);
    g.setColour (theme::red);
    g.fillRoundedRectangle (groove.withTop (jlimit (groove.getY(), groove.getBottom(), pos)), 2.0f);
    const auto thumb = Rectangle<float> (track.getCentreX() - 10.0f, pos - 4.5f, 20.0f, 9.0f);
    g.setColour (theme::getSkin() == theme::Skin::emutal ? Colour (0xffdcdce0) : Colour (0xff9aa5b4));
    g.fillRoundedRectangle (thumb, 2.5f);
    g.setColour (st.bg);
    g.drawRoundedRectangle (thumb, 2.5f, 1.0f);
}
//== EMUL8R drawing =============================================================
void EmxLookAndFeel::metalButton (Graphics& g, Button& b, bool highlighted, bool down)
{
    const bool on = b.getToggleState(), enabled = b.isEnabled();
    auto r = b.getLocalBounds().toFloat().reduced (0.5f, 1.5f);
    if (b.getProperties().contains ("emxTab"))
    {
        // dark blue-grey tab strip; the active tab glows teal
        g.setGradientFill (on ? ColourGradient::vertical (Colour (0xff5fd3c6), r.getY(), Colour (0xff1f8378), r.getBottom())
                              : ColourGradient::vertical (Colour (highlighted ? 0xff6a7680 : 0xff5a6670), r.getY(), Colour (0xff3a444d), r.getBottom()));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colours::white.withAlpha (on ? 0.45f : 0.18f));
        g.fillRect (r.getX() + 3.0f, r.getY() + 1.0f, r.getWidth() - 6.0f, 1.0f);
        g.setColour (Colour (0xff1b2126));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        b.setColour (TextButton::textColourOffId, Colour (0xffe3e9ee));
        b.setColour (TextButton::textColourOnId, Colours::white);
        return;
    }
    const float rad = jmin (r.getHeight() * 0.5f, 9.0f);   // metal pill
    if (down)
        r = r.translated (0.0f, 0.5f);
    if (on && enabled)
        g.setGradientFill (ColourGradient::vertical (Colour (0xff74e0d4), r.getY(), Colour (0xff23897e), r.getBottom()));
    else if (! enabled)
        g.setGradientFill (ColourGradient::vertical (Colour (0xffd7dce0), r.getY(), Colour (0xffbcc3c9), r.getBottom()));
    else
        g.setGradientFill (ColourGradient::vertical (Colour (down ? 0xffb7bfc6 : highlighted ? 0xfff7f9fa : 0xffedf0f2), r.getY(),
                                                     Colour (down ? 0xffd9dee2 : 0xffaab2b9), r.getBottom()));
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::white.withAlpha (on ? 0.4f : 0.8f));
    g.fillRoundedRectangle (r.reduced (rad * 0.6f, 0.0f).withHeight (1.2f).translated (0.0f, 1.2f), 0.6f);
    g.setColour (enabled ? (on ? Colour (0xff165c55) : Colour (0xff4f5961)) : Colour (0xff8a939b));
    g.drawRoundedRectangle (r, rad, 1.0f);
}

void EmxLookAndFeel::metalKnob (Graphics& g, Rectangle<float> r, float startAngle, float endAngle, float pos)
{
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    // dark collar
    const auto collar = r.reduced (rad * 0.08f);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillEllipse (collar.translated (0.0f, 1.5f));
    g.setGradientFill (ColourGradient::vertical (Colour (0xff4a535b), collar.getY(), Colour (0xff1c2227), collar.getBottom()));
    g.fillEllipse (collar);
    // glossy teal cap
    const auto cap = r.reduced (rad * 0.2f);
    g.setGradientFill (ColourGradient (Colour (0xff8beee2), cap.getX() + cap.getWidth() * 0.3f, cap.getY() + cap.getHeight() * 0.25f,
                                       Colour (0xff15695f), cap.getRight(), cap.getBottom(), true));
    g.fillEllipse (cap);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillEllipse (cap.getX() + cap.getWidth() * 0.18f, cap.getY() + cap.getHeight() * 0.1f, cap.getWidth() * 0.45f, cap.getHeight() * 0.3f);
    g.setColour (Colour (0xff0f3f3a));
    g.drawEllipse (cap, 1.0f);
    // white marker near the edge
    const float mr = cap.getWidth() * 0.36f;
    const Point<float> m (c.x + mr * std::sin (angle), c.y - mr * std::cos (angle));
    g.setColour (Colours::white);
    g.fillEllipse (m.x - 2.2f, m.y - 2.2f, 4.4f, 4.4f);
    juce::ignoreUnused (endAngle);
}

void EmxLookAndFeel::metalFader (Graphics& g, Rectangle<float> track, float pos)
{
    auto slot = track.withSizeKeepingCentre (5.0f, track.getHeight() - 6.0f);
    g.setColour (Colour (0xff20272c));
    g.fillRoundedRectangle (slot, 2.5f);
    g.setColour (Colours::white.withAlpha (0.4f));
    g.drawLine (slot.getRight() + 0.5f, slot.getY() + 2.0f, slot.getRight() + 0.5f, slot.getBottom() - 2.0f, 1.0f);
    auto cap = Rectangle<float> (track.getCentreX() - 12.0f, pos - 7.0f, 24.0f, 14.0f);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 2.5f);
    g.setGradientFill (ColourGradient::vertical (Colour (0xfff0f3f5), cap.getY(), Colour (0xff9aa3ab), cap.getBottom()));
    g.fillRoundedRectangle (cap, 2.5f);
    g.setColour (Colour (0xff4f5961));
    g.drawRoundedRectangle (cap, 2.5f, 1.0f);
    g.setColour (Colour (0xff2aa598));
    g.fillRect (cap.reduced (4.0f, 0.0f).withSizeKeepingCentre (cap.getWidth() - 8.0f, 2.0f));
}
} // namespace emx
