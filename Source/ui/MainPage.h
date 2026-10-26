// Emulon X — V3 Main tab: the preset LCD (live output + bank / number / name / category),
// the preset's own volume and pan, and the 16 macros. Saving and loading live in the toolbar.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "../Settings.h"

namespace emx
{
class EmulonXProcessor;

class MainPage final : public juce::Component,
                       private juce::ChangeListener
{
public:
    explicit MainPage (EmulonXProcessor&);
    ~MainPage() override;

    /** Called ~30x per second by the editor (the LCD follows the sound). */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;   // on the LCD's name: rename the preset

    /** "Orbit-3" -> "OR3", "Proteus 2" -> "PR2", "Vintage Keys" -> "VIN". */
    static juce::String bankCode (const juce::String& bankName);
    /** "amb:Galactic End" -> "AMBIENT" (E-mu style name prefixes). */
    static juce::String categoryOf (const juce::String& presetName);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    EmulonXProcessor& proc;
    juce::SharedResourcePointer<Settings> settings;
    juce::OwnedArray<EmxKnob> macros;
    ValueControl volume { "VOLUME", ValueControl::Style::knob, 46 }, pan { "PAN", ValueControl::Style::knob, 46 };
    std::unique_ptr<TypeInField> rename;
    juce::Rectangle<float> nameArea, presetArea, scopeArea, infoArea, outArea, outInset, macroArea, macroInset;
    float scopeLo[256] {}, scopeHi[256] {};
};
} // namespace emx
