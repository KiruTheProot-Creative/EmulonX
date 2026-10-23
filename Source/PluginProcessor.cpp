// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "engine/TestTone.h"

namespace emx
{
static float dbToGain (float db) { return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db); }

juce::AudioProcessorValueTreeState::ParameterLayout EmulonXProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::NormalisableRange<float> volRange (-60.0f, 6.0f, 0.01f);
    volRange.setSkewForCentre (-12.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { masterParamId, 1 }, "Master Volume", volRange, 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
            {
                return v <= -59.9f ? juce::String ("-inf dB")
                                   : (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
            })));

    for (int i = 0; i < numMacros; ++i)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { macroParamId (i), 1 }, "Macro " + juce::String (i + 1),
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));

    return layout;
}

EmulonXProcessor::EmulonXProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EmulonX", createParameterLayout())
{
    masterDb = apvts.getRawParameterValue (masterParamId);

    for (int i = 0; i < 16; ++i)
        synth.addVoice (new TestVoice());
    synth.addSound (new TestSound());
}

bool EmulonXProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void EmulonXProcessor::prepareToPlay (double sampleRate, int maxBlockSize)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    loadMeasurer.reset (sampleRate, maxBlockSize);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (dbToGain (masterDb->load()));
}

void EmulonXProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, n);

    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, n);

    masterGain.setTargetValue (dbToGain (masterDb->load()));
    masterGain.applyGain (buffer, n);

    int active = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (synth.getVoice (i)->isVoiceActive())
            ++active;
    activeVoices.store (active);

    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        const auto m = buffer.getMagnitude (ch, 0, n);
        if (m > peak[ch].load())
            peak[ch].store (m);
    }
    if (buffer.getNumChannels() == 1)
        peak[1].store (peak[0].load());
}

juce::AudioProcessorEditor* EmulonXProcessor::createEditor()
{
    return new EmulonXEditor (*this);
}

//== Macro names live in the plugin state, so they are saved with the DAW project.
juce::String EmulonXProcessor::getMacroName (int index) const
{
    const auto name = apvts.state.getProperty ("macroName" + juce::String (index + 1)).toString();
    return name.isNotEmpty() ? name : "MACRO " + juce::String (index + 1);
}

void EmulonXProcessor::setMacroName (int index, const juce::String& name)
{
    apvts.state.setProperty ("macroName" + juce::String (index + 1), name.trim().substring (0, 12), nullptr);
}

void EmulonXProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void EmulonXProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
} // namespace emx

// Entry point the plugin wrappers (VST3 / AU / Standalone) call.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new emx::EmulonXProcessor();
}
