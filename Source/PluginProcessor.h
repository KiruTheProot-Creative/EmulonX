// Emulon X — audio processor (runs the sound; the DAW talks to this).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/SamplerEngine.h"
#include "engine/SampleLoader.h"
#include "engine/ReleasePool.h"
#include "Settings.h"

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

struct BankEntry
{
    juce::String name;
    juce::MemoryBlock data;   // a complete .emxp
};

class EmulonXProcessor final : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::ChangeListener,
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
    const juce::String getProgramName (int) override { return presetName; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== Used by the editor (message thread) ====================================
    juce::AudioProcessorValueTreeState apvts;
    Settings& getSettings() noexcept { return *settings; }

    juce::String getMacroName (int index) const;
    void setMacroName (int index, const juce::String& name);

    float takePeak (int channel) noexcept { return peak[channel].exchange (0.0f); }
    int getActiveVoices() const noexcept  { return activeVoices.load(); }
    double getCpuLoad() const noexcept    { return loadMeasurer.getLoadAsProportion(); }
    void panic() noexcept                 { panicRequested = true; }

    //== Preset ==================================================================
    const juce::String& getPresetName() const noexcept { return presetName; }
    void setPresetName (const juce::String&);
    void newPreset();
    /** Saves the current preset (samples packed inside) in the background. */
    void savePresetAs (const juce::File&);
    juce::File getPresetFile() const noexcept { return presetFile; }

    //== Zones ===================================================================
    const std::vector<ModelZone>& getZones() const noexcept { return zones; }
    Interp getInterpolation() const noexcept { return interp; }
    void setInterpolation (Interp);
    void setKeepSpeedForAll (bool);
    bool allKeepSpeed() const;
    void removeZone (int id);
    void clearZones();
    void autoMap();

    /** Audio files add zones; a preset replaces the current one; a bank opens in the bank list. */
    void loadFiles (const juce::Array<juce::File>&);
    void loadPresetFromBankFile (const juce::File& bank, const juce::String& entry, const juce::String& name);
    bool isBusy() const noexcept { return pendingJobs.load() > 0; }
    juce::String getLastMessage() const { return lastMessage; }
    /** Goes up whenever a preset or bank file is written, so the explorer can rescan. */
    int getFilesWrittenCounter() const noexcept { return filesWritten; }

    //== Bank (a list of presets that can be exported as one .emxb) =============
    const juce::String& getBankName() const noexcept { return bankName; }
    void setBankName (const juce::String& n) { bankName = n; sendChangeMessage(); }
    const std::vector<BankEntry>& getBank() const noexcept { return bank; }
    void addCurrentPresetToBank();
    void removeBankEntry (int index);
    void clearBank();
    void loadBankEntry (int index);
    void openBank (const juce::File&);
    void exportBank (const juce::File&);

    //== Preview (audition a sample from the explorer) ==========================
    void previewFile (const juce::File&);
    void stopPreview();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // settings changed
    void applySettings();

    void publish();
    void writeZonesToState();
    void restoreZonesFromState (const juce::MemoryBlock& embeddedPreset);
    void addLoaded (LoadResult&&, bool autoMapAfter, const juce::File& fromFile = {});
    void applyMacros (const MacroList&);
    PresetToSave makePresetToSave() const;
    MacroList currentMacros() const;
    void message (const juce::String&);

    /** Runs `work` on the loader thread, then `done` with its result on the message thread. */
    template <typename Result>
    void runInBackground (std::function<Result()> work, std::function<void (Result&)> done);

    juce::SharedResourcePointer<Settings> settings;

    // Model (message thread)
    std::vector<ModelZone> zones;
    Interp interp = Interp::sinc;
    int nextZoneId = 1, nextOrder = 0, filesWritten = 0;
    juce::String presetName { "Init" }, lastMessage, bankName { "My Bank" };
    juce::File presetFile;
    juce::var presetFx;
    std::vector<BankEntry> bank;

    // Embedded copy of the preset for the DAW project (built in the background)
    juce::CriticalSection embedLock;
    juce::MemoryBlock embeddedPreset;
    bool embedDirty = true, embedBuilding = false;

    // Hand-over to the audio thread
    juce::SpinLock snapshotLock;
    std::shared_ptr<const ZoneSet> pendingSnapshot, audioSnapshot;
    std::shared_ptr<const SampleData> pendingPreview, audioPreview;
    bool previewChanged = false;
    double previewPos = 0.0;
    ReleasePool releasePool;

    juce::ThreadPool loaderThread { juce::ThreadPoolOptions{}.withThreadName ("Emulon X loader").withNumberOfThreads (1) };
    std::atomic<int> pendingJobs { 0 };
    int loadGeneration = 0;   // bumped when a project is restored; older loads are ignored
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    // Settings mirrored for the audio thread
    std::atomic<int> setMaxVoices { 64 }, setMidiChannel { 0 }, setVelocityCurve { 1 }, setBendRange { 2 };
    std::atomic<float> setCpuLimit { 0.85f }, setTuneCents { 0.0f };
    std::atomic<bool> setStealQuietest { false }, panicRequested { false };

    // Audio
    SamplerEngine engine;
    double currentSampleRate = 44100.0;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    juce::SmoothedValue<float> masterGain;
    std::atomic<float>* masterDb = nullptr;
    std::atomic<float> peak[2] { 0.0f, 0.0f };
    std::atomic<int> activeVoices { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmulonXProcessor)
};
} // namespace emx
