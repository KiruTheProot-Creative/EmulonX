// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Widgets.h"
#include "Theme.h"

namespace emx
{
using namespace juce;

//== ModSlider =================================================================
void ModSlider::mouseDown (const MouseEvent& e)
{
    swallowed = false;
    if (e.mods.isPopupMenu())
    {
        swallowed = true;
        if (onRightClick)
            onRightClick();
        return;
    }
    if (interceptClick && interceptClick())
    {
        swallowed = true;
        return;
    }
    Slider::mouseDown (e);
}

void ModSlider::mouseDrag (const MouseEvent& e) { if (! swallowed) Slider::mouseDrag (e); }
void ModSlider::mouseUp (const MouseEvent& e)   { if (! swallowed) Slider::mouseUp (e); swallowed = false; }

void drawModRings (Graphics& g, Rectangle<float> knob, const ModRings& m)
{
    if (m.rings.empty())
        return;
    const auto c = knob.getCentre();
    const float baseR = knob.getWidth() * 0.5f + 2.5f;
    auto angle = [] (float n) { return MathConstants<float>::pi * (-0.75f + 1.5f * jlimit (0.0f, 1.0f, n)); };
    for (size_t j = 0; j < m.rings.size() && j < 4; ++j)
    {
        const auto& r = m.rings[j];
        const float rad = baseR + (float) j * 3.5f;
        float a0 = angle (m.base), a1 = angle (m.base + r.amount);
        if (std::abs (a1 - a0) < 0.02f)
            a1 = a0 + 0.02f;
        Path arc;
        arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, jmin (a0, a1), jmax (a0, a1), true);
        g.setColour ((r.envelope ? theme::hue (0xffffd6d6) : theme::hue (0xffffb030)).withAlpha (0.9f));
        g.strokePath (arc, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    if (m.current >= 0.0f)
    {
        const float a = angle (m.current), rad = knob.getWidth() * 0.44f;
        const Point<float> pt (c.x + rad * std::sin (a), c.y - rad * std::cos (a));
        DropShadow (Colours::white.withAlpha (0.8f), 5, {}).drawForRectangle (g, Rectangle<int> ((int) pt.x - 2, (int) pt.y - 2, 4, 4));
        g.setColour (Colours::white);
        g.fillEllipse (pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
    }
}

//== EmxKnob ====================================================================
class EmxKnob::NameLabel final : public Label
{
public:
    NameLabel()
    {
        setEditable (false, true, false);
        setJustificationType (Justification::centred);
    }

    void paint (Graphics& g) override
    {
        if (! isBeingEdited())
            theme::drawLed (g, getLocalBounds().toFloat(), getText(), 8.0f);
    }
};

EmxKnob::EmxKnob (AudioProcessorValueTreeState& state, const String& paramId, const String& captionText, int size)
    : caption (captionText.toUpperCase()), knobSize (size)
{
    slider.setSliderStyle (Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (160);
    slider.setPopupMenuEnabled (false);
    addAndMakeVisible (slider);

    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
    {
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (p->getName (64) + " - drag up/down, double-click to reset");
    }

    slider.onValueChange = [this] { repaint (ledArea.toNearestInt().expanded (2)); };
}

EmxKnob::~EmxKnob() = default;

void EmxKnob::setEditableCaption (std::function<String()> getName, std::function<void (const String&)> setName)
{
    nameLabel = std::make_unique<NameLabel>();
    nameSource = getName;
    nameLabel->setText (getName(), dontSendNotification);
    nameLabel->setTooltip ("Double-click to rename");
    nameLabel->onTextChange = [this, getName, setName]
    {
        setName (nameLabel->getText());
        nameLabel->setText (getName(), dontSendNotification);
    };
    addAndMakeVisible (*nameLabel);
    resized();
}

void EmxKnob::refreshCaption()
{
    if (nameLabel != nullptr && nameSource != nullptr && ! nameLabel->isBeingEdited())
        nameLabel->setText (nameSource(), dontSendNotification);
}

void EmxKnob::setSensitivity (int level)
{
    slider.setMouseDragSensitivity (level == 0 ? 90 : level == 2 ? 400 : 160);
}

void EmxKnob::resized()
{
    auto r = getLocalBounds().toFloat();
    r.removeFromTop ((float) ringMargin);
    slider.setBounds (r.removeFromTop ((float) knobSize).withSizeKeepingCentre ((float) knobSize, (float) knobSize).toNearestInt());
    r.removeFromTop (4.0f);
    ledArea = r.removeFromTop (16.0f).withSizeKeepingCentre (jmin (r.getWidth(), 62.0f), 16.0f);
    r.removeFromTop (3.0f);
    captionArea = r.removeFromTop (14.0f);
    if (nameLabel != nullptr)
        nameLabel->setBounds (captionArea.withSizeKeepingCentre (jmin (captionArea.getWidth(), 74.0f), 14.0f).toNearestInt());
}

void EmxKnob::paintOverChildren (Graphics& g)
{
    if (modRings)
        drawModRings (g, slider.getBounds().toFloat(), modRings());
}

void EmxKnob::paint (Graphics& g)
{
    theme::drawLed (g, ledArea, slider.getTextFromValue (slider.getValue()), 9.0f);
    if (nameLabel == nullptr)
    {
        g.setColour (Colours::white);
        g.setFont (theme::font (8.0f, 700, 0.12f));
        g.drawText (caption, captionArea, Justification::centredTop, false);
    }
}

//== ValueControl ==============================================================
ValueControl::ValueControl (const String& captionText, Style st, int size)
    : caption (captionText.toUpperCase()), style (st), controlSize (size)
{
    slider.setSliderStyle (style == Style::knob ? Slider::RotaryVerticalDrag : Slider::LinearVertical);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    if (style == Style::knob)
        slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setPopupMenuEnabled (false);
    slider.onValueChange = [this]
    {
        repaint (ledArea.toNearestInt().expanded (2));
        if (onChange)
            onChange (slider.getValue());
    };
    slider.setTooltip ("Drag up/down, mouse wheel, double-click to reset");
    setSensitivity (1);
    addAndMakeVisible (slider);
}

void ValueControl::setRange (NormalisableRange<double> range, double defaultValue)
{
    slider.setNormalisableRange (range);
    slider.setDoubleClickReturnValue (true, defaultValue);
}

void ValueControl::setValue (double v)
{
    if (std::abs (slider.getValue() - v) > 1.0e-9)
    {
        slider.setValue (v, dontSendNotification);
        repaint (ledArea.toNearestInt().expanded (2));
    }
}

void ValueControl::setSensitivity (int level)
{
    if (style == Style::knob)
        slider.setMouseDragSensitivity (level == 0 ? 90 : level == 2 ? 400 : 160);
}

void ValueControl::resized()
{
    auto r = getLocalBounds().toFloat();
    r.removeFromTop ((float) ringMargin);
    if (style == Style::knob)
        slider.setBounds (r.removeFromTop ((float) controlSize).withSizeKeepingCentre ((float) controlSize, (float) controlSize).toNearestInt());
    else
        slider.setBounds (r.removeFromTop ((float) controlSize).withSizeKeepingCentre (24.0f, (float) controlSize).toNearestInt());
    r.removeFromTop (4.0f);
    ledArea = r.removeFromTop (16.0f).withSizeKeepingCentre (jmin (r.getWidth(), 56.0f), 16.0f);
    r.removeFromTop (3.0f);
    captionArea = r.removeFromTop (14.0f);
}

void ValueControl::paintOverChildren (Graphics& g)
{
    if (modRings && style == Style::knob)
        drawModRings (g, slider.getBounds().toFloat(), modRings());
}

void ValueControl::paint (Graphics& g)
{
    const auto v = slider.getValue();
    theme::drawLed (g, ledArea, format ? format (v) : String (v, 2), 8.5f);
    g.setColour (isEnabled() ? Colours::white : theme::textMuted);
    g.setFont (theme::font (8.0f, 700, 0.12f));
    g.drawFittedText (caption, captionArea.toNearestInt(), Justification::centredTop, 1, 0.6f);
}

//== LedLabel ==================================================================
LedLabel::LedLabel (float h, bool singleClick) : textHeight (h)
{
    setEditable (singleClick, ! singleClick, false);
    setJustificationType (Justification::centredLeft);
    setMouseCursor (MouseCursor::IBeamCursor);
}

void LedLabel::paint (Graphics& g)
{
    if (isBeingEdited())
        return;
    const auto t = getText();
    theme::drawLed (g, getLocalBounds().toFloat(), t.isEmpty() ? placeholder : t, textHeight,
                    Justification::centredLeft, t.isEmpty() ? theme::textMuted : theme::red);
}

TextEditor* LedLabel::createEditorComponent()
{
    auto* ed = Label::createEditorComponent();
    ed->setFont (theme::font (textHeight + 1.0f, 500, 0.06f));
    ed->setIndents (6, 0);
    ed->setJustification (Justification::centredLeft);
    return ed;
}

//== LedDisplay =================================================================
LedDisplay::LedDisplay (std::function<String()> textSource, float h) : source (std::move (textSource)), textHeight (h) {}

void LedDisplay::refresh()
{
    auto t = source();
    if (t != shown)
    {
        shown = t;
        repaint();
    }
}

void LedDisplay::paint (Graphics& g)
{
    if (shown.isEmpty())
        shown = source();
    theme::drawLed (g, getLocalBounds().toFloat(), shown, textHeight);
}

//== GearButton =================================================================
GearButton::GearButton() : Button ("Settings") { setTooltip ("Settings"); }

void GearButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, {}, highlighted, down);

    const auto c = getLocalBounds().toFloat().getCentre().translated (0, down ? 1.0f : 0.0f);
    const float R = 7.0f, r = 5.0f;
    Path gear;
    gear.addEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    for (int i = 0; i < 8; ++i)
    {
        Path tooth;
        tooth.addRectangle (c.x - 1.6f, c.y - R - 0.5f, 3.2f, 4.0f);
        tooth.applyTransform (AffineTransform::rotation (MathConstants<float>::twoPi * (float) i / 8.0f, c.x, c.y));
        gear.addPath (tooth);
    }
    g.setColour (highlighted ? Colours::white : theme::hue (0xffdddddd));
    g.fillPath (gear);
    g.setColour (theme::hue (0xff2b292f));
    g.fillEllipse (c.x - 2.2f, c.y - 2.2f, 4.4f, 4.4f);
}

//== ComingSoonPanel ============================================================
ComingSoonPanel::ComingSoonPanel (String t, String s, String m) : title (std::move (t)), subtitle (std::move (s)), message (std::move (m)) {}

void ComingSoonPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    theme::drawPanel (g, r);
    auto inner = theme::drawHeading (g, r.reduced (10.0f, 8.0f), title, subtitle);
    theme::drawLed (g, inner.withSizeKeepingCentre (jmin (inner.getWidth(), 520.0f), 34.0f), message, 10.0f);
}
} // namespace emx
