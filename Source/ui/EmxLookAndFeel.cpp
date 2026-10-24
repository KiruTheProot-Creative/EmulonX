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
}

//== Knob: your knob artwork + a glowing pointer ================================
void EmxLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, Slider&)
{
    const auto size = (float) jmin (w, h);
    const auto r = Rectangle<float> (size, size).withCentre (Rectangle<int> (x, y, w, h).toFloat().getCentre());

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
    g.setGradientFill (ColourGradient::vertical (theme::hue (0xff26232a), 0.0f, theme::hue (0xff111014), (float) h));
    g.fillAll();
    g.setColour (Colours::black);
    g.drawRect (0, 0, w, h);
}

Font EmxLookAndFeel::getPopupMenuFont() { return theme::font (11.0f, 500, 0.06f); }

void EmxLookAndFeel::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name)
{
    g.setFont (theme::font (9.0f, 700, 0.14f));
    g.setColour (Colours::white);
    g.drawText (name.toUpperCase(), area.reduced (12, 0), Justification::bottomLeft, true);
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
    g.setColour ((down ? theme::red : over ? theme::redDim : theme::redDark).withAlpha (0.8f));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f, 2.0f), 3.0f);
}
} // namespace emx
