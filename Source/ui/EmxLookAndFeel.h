// Emulon X — draws JUCE's knobs, sliders, buttons and menus in the Emulon style.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace emx
{
class EmxLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    EmxLookAndFeel();
    /** Re-reads the theme colours (after the hue setting changed). */
    void refreshColours();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;

    juce::Font getLabelFont (juce::Label&) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawComboBoxTextWhenNothingSelected (juce::Graphics&, juce::ComboBox&, juce::Label&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;

    void drawTreeviewPlusMinusBox (juce::Graphics&, const juce::Rectangle<float>& area, juce::Colour, bool isOpen, bool isMouseOver) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

private:
    // EMULON XP versions
    void xpButton (juce::Graphics&, juce::Button&, bool highlighted, bool down);
    void xpKnob (juce::Graphics&, juce::Rectangle<float>, float startAngle, float endAngle, float pos);
    void xpFader (juce::Graphics&, juce::Rectangle<float> track, float pos);
    // EMURUM / EMUTAL versions
    void flatButton (juce::Graphics&, juce::Button&, bool highlighted, bool down);
    void flatKnob (juce::Graphics&, juce::Rectangle<float>, float startAngle, float endAngle, float pos);
    void flatFader (juce::Graphics&, juce::Rectangle<float> track, float pos);
    // EMUL8R versions
    void metalButton (juce::Graphics&, juce::Button&, bool highlighted, bool down);
    void metalKnob (juce::Graphics&, juce::Rectangle<float>, float startAngle, float endAngle, float pos);
    void metalFader (juce::Graphics&, juce::Rectangle<float> track, float pos);
};
} // namespace emx
