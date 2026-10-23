// Milestone 1 placeholder: a simple sine voice so MIDI -> audio can be checked
// in a DAW. Replaced by the real sampler engine in milestone 2.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace emx
{
struct TestSound final : juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class TestVoice final : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<TestSound*> (s) != nullptr; }
    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;
    using SynthesiserVoice::renderNextBlock;
    void setCurrentPlaybackSampleRate (double newRate) override;

private:
    double phase = 0.0, increment = 0.0;
    float level = 0.0f;
    juce::ADSR adsr;
};
} // namespace emx
