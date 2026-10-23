// Emulon X — audio processor (runs the sound; the DAW talks to this).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/SamplerEngine.h"
#include "engine/SampleLoader.h"
#include "engine/ReleasePool.h"

namespace emx
{
inline constexpr int numMacros = 20;
inline juce::String macroParamId (int index) { return "macro" + juce::String (index + 1); }
inline const juce::String masterParamId { "master" };

/** A zone as the editor sees it (message thread only). */
struct ModelZone
{
    int id = 0;
    std::shared_ptr<const SampleData> sample;   // null while loading or if the file is missing
    juce::String source, name, problem;
    ZoneParams params;
    int order = 0;
};

class EmulonXProcessor final : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::Timer
{
public:
    EmulonXProcessor();
    ~EmulonXProcessor() override;

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

    float takePeak (int channel) noexcept { return peak[channel].exchange (0.0f); }
    int getActiveVoices() const noexcept  { return activeVoices.load(); }
    double getCpuLoad() const noexcept    { return loadMeasurer.getLoadAsProportion(); }

    //== Zones ===================================================================
    const std::vector<ModelZone>& getZones() const noexcept { return zones; }
    Interp getInterpolation() const noexcept { return interp; }
    void setInterpolation (Interp);
    void setKeepSpeedForAll (bool);
    bool allKeepSpeed() const;
    void removeZone (int id);
    void clearZones();
    void autoMap();

    /** Loads audio files / presets in the background. Several audio files dropped
        at once are spread across the keyboard automatically. */
    void loadFiles (const juce::Array<juce::File>&);
    bool isLoading() const noexcept { return pendingLoads.load() > 0; }
    juce::String getLastMessage() const { return lastMessage; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;
    void publish();                  // model -> new snapshot for the audio thread
    void writeZonesToState();
    void restoreZonesFromState();
    void addLoaded (LoadResult&&, bool autoMapAfter);

    // Model (message thread)
    std::vector<ModelZone> zones;
    Interp interp = Interp::sinc;
    int nextZoneId = 1, nextOrder = 0;
    juce::String lastMessage;

    // Hand-over to the audio thread
    juce::SpinLock snapshotLock;
    std::shared_ptr<const ZoneSet> pendingSnapshot, audioSnapshot;
    ReleasePool releasePool;

    juce::ThreadPool loaderThread { juce::ThreadPoolOptions{}.withThreadName ("Emulon X loader").withNumberOfThreads (1) };
    std::atomic<int> pendingLoads { 0 };
    int loadGeneration = 0;   // bumped when a project is restored; older loads are ignored
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    // Audio
    SamplerEngine engine;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    juce::SmoothedValue<float> masterGain;
    std::atomic<float>* masterDb = nullptr;
    std::atomic<float> peak[2] { 0.0f, 0.0f };
    std::atomic<int> activeVoices { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmulonXProcessor)
};
} // namespace emx
