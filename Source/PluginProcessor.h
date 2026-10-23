// Emulon X — audio processor (runs the sound; the DAW talks to this).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace emx
{
inline constexpr int numMacros = 20;
inline juce::String macroParamId (int index) { return "macro" + juce::String (index + 1); }
inline const juce::String masterParamId { "master" };

class EmulonXProcessor final : public juce::AudioProcessor
{
public:
    EmulonXProcessor();
    ~EmulonXProcessor() override = default;

    //== AudioProcessor ==========================================================
    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Emulon X"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== Used by the editor (message thread) ====================================
    juce::AudioProcessorValueTreeState apvts;

    juce::String getMacroName (int index) const;
    void setMacroName (int index, const juce::String& name);

    /** Highest output level since the last call, per channel (0 = left, 1 = right). */
    float takePeak (int channel) noexcept { return peak[channel].exchange (0.0f); }
    int getActiveVoices() const noexcept  { return activeVoices.load(); }
    double getCpuLoad() const noexcept    { return loadMeasurer.getLoadAsProportion(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::Synthesiser synth;   // milestone 1: test tone; milestone 2: the sampler engine
    juce::AudioProcessLoadMeasurer loadMeasurer;
    juce::SmoothedValue<float> masterGain;
    std::atomic<float>* masterDb = nullptr;

    std::atomic<float> peak[2] { 0.0f, 0.0f };
    std::atomic<int> activeVoices { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmulonXProcessor)
};
} // namespace emx
