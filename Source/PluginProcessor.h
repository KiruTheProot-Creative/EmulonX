// Emulon X — audio processor (runs the sound; the DAW talks to this).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/SamplerEngine.h"
#include "engine/SampleLoader.h"
#include "engine/ReleasePool.h"
#include "fx/FxEngine.h"
#include "engine/Arpeggiator.h"
#include "engine/SampleEdit.h"
#include "Settings.h"

namespace emx
{
inline constexpr int numMacros = fx::maxMacros;
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
    fx::Chain fx;   // this zone's own FX channel (runs before the whole-preset chain)
    // Warps baked into this zone's sample: what each one was, and the sample before it (for "undo last warp").
    juce::StringArray warps;
    std::vector<std::shared_ptr<const SampleData>> beforeWarp;
};

/** Splicer mode: one sample chopped into slices, one slice per key. The slices are ordinary zones
    (rebuilt from the cuts); the zones from before are kept to go back to. */
struct SpliceState
{
    bool on = false;
    std::vector<int> cuts;     // slice starts in frames, the first is 0
    int firstKey = 48;
    bool toEnd = false;        // each slice plays on to the end of the sample (otherwise only its own part)
    std::vector<ModelZone> samplerZones;
    int samplerSelected = 0;

    juce::var toJson() const
    {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> c;
        for (auto x : cuts) c.add (x);
        o->setProperty ("cuts", c);
        o->setProperty ("firstKey", firstKey);
        o->setProperty ("toEnd", toEnd);
        return juce::var (o);
    }
};

/** Mono / poly playing, saved with the preset. */
struct VoiceSettings
{
    bool mono = false, legato = true;
    float glide = 0.0f;   // seconds

    juce::var toJson() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("mode", mono ? "mono" : "poly");
        o->setProperty ("legato", legato);
        o->setProperty ("glide", glide);
        return juce::var (o);
    }
    static VoiceSettings fromJson (const juce::var& j)
    {
        VoiceSettings v;
        if (j.isObject())
        {
            v.mono = j["mode"].toString() == "mono";
            v.legato = j.hasProperty ("legato") ? (bool) j["legato"] : true;
            v.glide = juce::jlimit (0.0f, 5.0f, (float) (double) j["glide"]);
        }
        return v;
    }
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
    void panic() noexcept                 { panicRequested = true; keyboardState.allNotesOff (0); }

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

    //== Zone editing (Editor tab) ===============================================
    int getSelectedZoneId() const noexcept { return selectedZoneId; }
    const ModelZone* getSelectedZone() const noexcept;
    int getSelectedIndex() const noexcept;
    void selectZone (int id);
    void selectNextZone (int direction);
    /** Changes one zone's settings; takes effect immediately, even on sounding notes. */
    void setZoneParams (int id, ZoneParams);
    /** Copies attack, decay, sustain and release from one zone to another (toId -1 = every other zone). One undo step. */
    void copyEnvelope (int fromId, int toId);
    void duplicateZone (int id);

    //== Editing the sample (V2) ===================================================
    /** Bakes a warp into the zone's sample (in splicer mode: into the sliced sample). One undo step. */
    void warpZone (int id, const edit::Warp&);
    /** Takes back the zone's last warp. */
    void unwarpZone (int id);

    /** Splicer mode. Turning it on chops the selected zone's sample (hits found automatically). */
    const SpliceState& getSplice() const noexcept { return splice; }
    bool isSplicer() const noexcept { return splice.on; }
    void setSplicer (bool on);
    void setSliceCuts (std::vector<int> cuts);
    void findSliceHits (float sensitivity);
    void splitSlicesEvenly (int count);
    void setSliceFirstKey (int key);
    void setSlicesToEnd (bool);
    /** Keeps the slices as ordinary zones and goes back to sampler mode. */
    void slicesToZones();
    /** The sliced sample (null when not in splicer mode). */
    std::shared_ptr<const SampleData> getSliceSample() const;
    /** Pitch, amp, envelope and keep speed from p go to every slice (splicer mode). */
    void setSliceVoice (const ZoneParams& p);

    //== Undo for zone edits ======================================================
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    //== FX, envelopes and modulation (FX tab) ===================================
    /** chainId 0 = the whole-preset chain, otherwise a zone id. */
    const fx::Chain* getFxChain (int chainId) const;
    const fx::Setup& getFxSetup() const noexcept { return fxSetup; }
    /** Goes up when effects are added, removed, moved or change mode (the FX tab rebuilds then). */
    int getFxStructureVersion() const noexcept { return fxStructure; }
    const fx::Module* findFxModule (int moduleId, int* chainId = nullptr) const;
    void addFx (int chainId, fx::Type);
    void removeFx (int moduleId);
    void moveFx (int moduleId, int delta);
    void setFxOn (int moduleId, bool on);
    void setFxMode (int moduleId, int mode);
    void setFxSettings (int moduleId, const fx::Settings&);
    void setFxParam (int moduleId, int slot, float norm);
    void setFxBpm (float bpm);
    void addEnvelope();
    void removeEnvelope (int index);
    void setEnvelope (int index, const fx::Envelope&);
    bool hasRoute (fx::Source, fx::Target) const;
    void toggleRoute (fx::Source, fx::Target);
    void setRouteAmount (int routeIndex, float amount);
    void removeRoute (int routeIndex);
    juce::String describeSource (fx::Source) const;
    juce::String describeTarget (fx::Target) const;
    /** The master volume as a 0..1 position on a linear dB scale (what modulation moves). */
    float getMasterVolumeNorm() const;
    fx::Engine& getFxEngine() noexcept { return fxEngine; }
    /** Macro "learn": while set, the next FX knob clicked gets linked to this macro (-1 = off). */
    int getLearnMacro() const noexcept { return learnMacro; }
    void setLearnMacro (int index);

    //== Arpeggiator and voice mode =============================================
    const ArpSettings& getArpSettings() const noexcept { return arpSettings; }
    void setArpSettings (const ArpSettings&);
    const VoiceSettings& getVoiceSettings() const noexcept { return voiceSettings; }
    void setVoiceSettings (const VoiceSettings&);
    Arpeggiator& getArp() noexcept { return arp; }

    /** Where the newest voice of each zone is playing (for the waveform). */
    int getPlayheads (SamplerEngine::Playhead* out, int maxCount) const noexcept;

    /** Notes held on MIDI input or on the on-screen keyboard. The editor's keyboard plays
        through this, and it is safe to use from any thread. */
    juce::MidiKeyboardState keyboardState;
    int getKeyboardChannel() const noexcept { return juce::jmax (1, setMidiChannel.load()); }

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
    /** Puts the bank's presets in name order (Windows-explorer style); returns where the preset
        that was at keepIndex ended up (-1 if none). */
    int sortBank (int keepIndex);
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
    void mapByRoots();

    // FX helpers
    fx::Module* findFx (int moduleId, int* chainId = nullptr);
    void publishFx (bool structureChanged);
    void pushFxSnapshot();
    juce::var fxToJson (bool withZoneChains) const;
    void applyFxJson (const juce::var& fxJson, const std::vector<juce::var>& zoneChainsJson, bool applyVolume);
    fx::Chain copyChainWithNewIds (const fx::Chain&);

    struct UndoState { std::vector<ModelZone> zones; Interp interp; int selected; fx::Setup fx; ArpSettings arp; VoiceSettings voice; SpliceState splice; };
    void rebuildSlices (std::shared_ptr<const SampleData>, const ZoneParams& voice);
    bool needsEmbedding() const;
    void applySpliceJson (const juce::var&);
    void publishPlayModes();
    /** Remembers the zones before a change. Repeated changes with the same tag within a
        moment (one knob drag) become one undo step. */
    void pushUndo (const juce::String& tag);
    void clearUndo();
    std::vector<UndoState> undoStack, redoStack;
    juce::String lastUndoTag;
    juce::uint32 lastUndoTime = 0;

    /** Runs `work` on the loader thread, then `done` with its result on the message thread. */
    template <typename Result>
    void runInBackground (std::function<Result()> work, std::function<void (Result&)> done);

    juce::SharedResourcePointer<Settings> settings;

    // Model (message thread)
    std::vector<ModelZone> zones;
    Interp interp = Interp::sinc;
    int nextZoneId = 1, nextOrder = 0, filesWritten = 0, selectedZoneId = 0;
    juce::String presetName { "Init" }, lastMessage, bankName { "My Bank" };
    juce::File presetFile;
    fx::Setup fxSetup;
    ArpSettings arpSettings;
    VoiceSettings voiceSettings;
    SpliceState splice;
    int nextFxId = 1, fxStructure = 0, learnMacro = -1;
    std::vector<BankEntry> bank;

    // Embedded copy of the preset for the DAW project (built in the background)
    juce::CriticalSection embedLock;
    juce::MemoryBlock embeddedPreset;
    bool embedDirty = true, embedBuilding = false;

    // Hand-over to the audio thread
    juce::SpinLock snapshotLock;
    std::shared_ptr<const ZoneSet> pendingSnapshot, audioSnapshot;
    std::shared_ptr<const SampleData> pendingPreview, audioPreview;
    std::shared_ptr<const fx::Snapshot> pendingFx, audioFx;
    ArpSettings pendingArp;
    bool arpChanged = false;
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
    std::atomic<bool> setMono { false }, setLegato { true };
    std::atomic<float> setGlide { 0.0f };

    // Audio
    SamplerEngine engine;
    fx::Engine fxEngine;
    Arpeggiator arp;
    juce::MidiBuffer processedMidi, arpMidi;
    static constexpr int subBlock = 64, maxBuses = 32;
    float busData[maxBuses][2][subBlock] {};
    float mixL[subBlock] {}, mixR[subBlock] {};
    double currentSampleRate = 44100.0;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    juce::SmoothedValue<float> masterGain;
    std::atomic<float>* masterDb = nullptr;
    std::atomic<float>* macroValues[numMacros] {};
    std::atomic<float> peak[2] { 0.0f, 0.0f };
    std::atomic<int> activeVoices { 0 };
    static constexpr int maxPlayheads = 16;
    std::atomic<int> playheadCount { 0 }, playheadZone[maxPlayheads] {};
    std::atomic<double> playheadPos[maxPlayheads] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmulonXProcessor)
};
} // namespace emx
