// Emulon X — reusable UI pieces: knob with LED readout, LED display,
// gear button, level meter and "coming soon" panel.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace emx
{
/** Knob (your artwork) + red LED value readout + caption underneath.
    Bound to a DAW parameter, so it is automatable and saved with the project. */
class EmxKnob final : public juce::Component
{
public:
    EmxKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, int knobSize);
    ~EmxKnob() override;

    /** Replaces the fixed caption with a name the user can double-click to rename. */
    void setEditableCaption (std::function<juce::String()> getName, std::function<void (const juce::String&)> setName);

    int getPreferredHeight() const noexcept { return knobSize + 4 + 16 + 3 + 14; }

    /** Re-reads an editable caption (e.g. after a preset brought new macro names). */
    void refreshCaption();
    /** 0 fast, 1 normal, 2 fine (from the settings). */
    void setSensitivity (int level);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;

private:
    class NameLabel;
    juce::String caption;
    int knobSize;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<NameLabel> nameLabel;
    std::function<juce::String()> nameSource;
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
class LedDisplay final : public juce::Component
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

/** Two vertical output meters with a smooth fall-off. */
class LevelMeter final : public juce::Component
{
public:
    void push (float left, float right);
    float getPeakDb() const noexcept;
    void paint (juce::Graphics&) override;

private:
    float level[2] {};
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
