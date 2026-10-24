// Emulon X — reusable UI pieces: knob with LED readout, LED display,
// gear button and "coming soon" panel.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace emx
{
/** A slider that reports right-clicks (for the modulation menu) and can hand a left-click
    to someone else first (macro "learn": the next knob you click gets linked). */
class ModSlider final : public juce::Slider
{
public:
    std::function<void()> onRightClick;
    std::function<bool()> interceptClick;   // return true to swallow the click

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool swallowed = false;
};

/** Modulation shown around a knob: one arc per connection, plus a dot at the current value. */
struct ModRings
{
    struct Ring { float amount; bool envelope; };
    std::vector<Ring> rings;
    float base = 0.0f;       // knob position 0..1
    float current = -1.0f;   // modulated position, -1 = hide the dot
};
void drawModRings (juce::Graphics&, juce::Rectangle<float> knob, const ModRings&);

/** Knob (your artwork) + red LED value readout + caption underneath.
    Bound to a DAW parameter, so it is automatable and saved with the project. */
class EmxKnob final : public juce::Component
{
public:
    EmxKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, int knobSize);
    ~EmxKnob() override;

    /** Replaces the fixed caption with a name the user can double-click to rename. */
    void setEditableCaption (std::function<juce::String()> getName, std::function<void (const juce::String&)> setName);

    int getPreferredHeight() const noexcept { return ringMargin + knobSize + 4 + 16 + 3 + 14; }
    void setRingMargin (int m) { ringMargin = m; resized(); }

    /** Re-reads an editable caption (e.g. after a preset brought new macro names). */
    void refreshCaption();
    /** 0 fast, 1 normal, 2 fine (from the settings). */
    void setSensitivity (int level);

    void paint (juce::Graphics&) override;
    void resized() override;

    ModSlider slider;
    std::function<ModRings()> modRings;   // optional

    void paintOverChildren (juce::Graphics&) override;

private:
    class NameLabel;
    juce::String caption;
    int knobSize, ringMargin = 0;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<NameLabel> nameLabel;
    std::function<juce::String()> nameSource;
    juce::Rectangle<float> ledArea, captionArea;
};

/** Knob or fader for a value that is not a DAW parameter (zone settings in the editor):
    your artwork, a red LED readout and a caption. */
class ValueControl final : public juce::Component
{
public:
    enum class Style { knob, fader };
    ValueControl (const juce::String& caption, Style, int size = 40);

    void setRange (juce::NormalisableRange<double>, double defaultValue);
    void setValue (double) ;                       // does not call onChange
    double getValue() const { return slider.getValue(); }
    void setSensitivity (int level);               // 0 fast, 1 normal, 2 fine

    std::function<juce::String (double)> format;
    std::function<void (double)> onChange;

    int getPreferredHeight() const noexcept { return ringMargin + controlSize + 4 + 16 + 3 + 14; }
    int getPreferredWidth() const noexcept  { return style == Style::knob ? 62 : 54; }
    /** Space above the knob for modulation rings. */
    void setRingMargin (int m) { ringMargin = m; resized(); }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void setCaption (const juce::String& c) { caption = c.toUpperCase(); repaint(); }

    ModSlider slider;
    std::function<juce::String()> tooltipExtra;
    std::function<ModRings()> modRings;   // optional

private:
    juce::String caption;
    Style style;
    int controlSize, ringMargin = 0;
    juce::Rectangle<float> ledArea, captionArea;
};

/** A red LED text field. Click to type (or double-click, if set). */
class LedLabel final : public juce::Label
{
public:
    explicit LedLabel (float textHeight = 9.0f, bool singleClickEdit = true);
    void paint (juce::Graphics&) override;
    void setPlaceholder (const juce::String& p) { placeholder = p; }

private:
    juce::TextEditor* createEditorComponent() override;
    float textHeight;
    juce::String placeholder;
};

/** Red LED readout whose text comes from a function, refreshed by a timer. */
class LedDisplay final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    explicit LedDisplay (std::function<juce::String()> textSource, float textHeight = 9.0f);
    void refresh();
    void paint (juce::Graphics&) override;

private:
    std::function<juce::String()> source;
    juce::String shown;
    float textHeight;
};

/** Metal button with a gear icon. */
class GearButton final : public juce::Button
{
public:
    GearButton();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** Panel shown for parts that arrive in a later milestone. */
class ComingSoonPanel final : public juce::Component
{
public:
    ComingSoonPanel (juce::String title, juce::String subtitle, juce::String message);
    void paint (juce::Graphics&) override;

private:
    juce::String title, subtitle, message;
};
} // namespace emx
