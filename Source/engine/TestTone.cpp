// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestTone.h"

namespace emx
{
void TestVoice::setCurrentPlaybackSampleRate (double newRate)
{
    SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
    if (newRate > 0)
        adsr.setSampleRate (newRate);
}

void TestVoice::startNote (int midiNote, float velocity, juce::SynthesiserSound*, int)
{
    const auto sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    increment = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (midiNote) / sr;
    phase = 0.0;
    level = 0.15f * velocity;
    adsr.setSampleRate (sr);
    adsr.setParameters ({ 0.005f, 0.3f, 0.6f, 0.4f });
    adsr.noteOn();
}

void TestVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
        adsr.noteOff();
    else
    {
        adsr.reset();
        clearCurrentNote();
    }
}

void TestVoice::renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    if (! isVoiceActive())
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        if (! adsr.isActive())
        {
            clearCurrentNote();
            break;
        }

        const auto s = (float) (std::sin (phase) * 0.7 + std::sin (2.0 * phase) * 0.2) * level * adsr.getNextSample();
        phase += increment;
        if (phase > juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            out.addSample (ch, startSample + i, s);
    }
}
} // namespace emx
