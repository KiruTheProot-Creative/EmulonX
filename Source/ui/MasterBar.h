// Emulon X — the master volume in the top bar: a slim slider with the stereo level meter
// running inside it, plus a dB readout.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"

namespace emx
{
class EmulonXProcessor;

class MasterBar final : public juce::Component
{
public:
    explicit MasterBar (EmulonXProcessor&);
    ~MasterBar() override;

    /** ~30x per second: reads the output peaks. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> track() const;
    float peakDb() const;

    EmulonXProcessor& proc;
    ModSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float level[2] {};
    double clipUntil = 0.0;   // time until the clip light goes off
    juce::Rectangle<float> labelArea, readoutArea;
};
} // namespace emx
