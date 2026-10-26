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
#include "engine/Machine.h"
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
    /** asPart: one of the Arranger's extra parts (made by the main instance; no extra outputs, no parts of its own). */
    explicit EmulonXProcessor (bool asPart = false);
    ~EmulonXProcessor() override;

    //== AudioProcessor ==========================================================
    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void releaseResources() override {}
    bool canAddBus (bool isInput) const override    { return ! isInput && ! isPart; }
    bool canRemoveBus (bool isInput) const override { return ! isInput && ! isPart; }
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

    //== V3: on-screen wheels, output scope, the part's own level ====================
    /** Pitch wheel from the on-screen keyboard, -1..1 (springs back to 0 on release). */
    void setUiPitchBend (float v) noexcept { uiBend.store (juce::jlimit (-1.0f, 1.0f, v)); uiBendDirty.store (true); }
    /** Mod wheel from the on-screen keyboard, 0..1. */
    void setUiModWheel (float v) noexcept  { uiMod.store (juce::jlimit (0.0f, 1.0f, v)); uiModDirty.store (true); }
    float getUiPitchBend() const noexcept  { return uiBend.load(); }
    float getUiModWheel() const noexcept   { return uiMod.load(); }
    /** The output as min / max per short step, oldest first (for the Main page's LCD). Returns the count. */
    static constexpr int scopeSize = 256;
    int readScope (float* lo, float* hi) const noexcept;
    /** The preset's own volume (dB, -60 = off) and pan (-1..1), after the effects and before the master. */
    float getPartVolume() const noexcept { return partVolumeDb.load(); }
    float getPartPan() const noexcept    { return partPan.load(); }
    void setPartVolume (float db);
    void setPartPan (float pan);
    /** Writes the chosen preset files into one bank (.emxb), in the background. */
    void mergePresetFilesIntoBank (const juce::Array<juce::File>& presets, const juce::File& bankFile);

    //== Preset ==================================================================
    const juce::String& getPresetName() const noexcept { return presetName; }
    /** Where the current preset came from (for the Main page's LCD): bank name and the preset's number in it. */
    struct PresetSource { juce::String bank; int number = -1; };
    const PresetSource& getPresetSource() const noexcept { return presetSource; }
    void setPresetName (const juce::String&);
    /** Type, author, notes and tags (filled in by the Save Preset window). */
    const PresetInfo& getPresetInfo() const noexcept { return presetInfo; }
    void setPresetInfo (const PresetInfo&);
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
    /** chainId 0 = the whole-preset chain, auxChainId (0 / 1) = the AUX buses, otherwise a zone id. */
    const fx::Chain* getFxChain (int chainId) const;
    static constexpr int auxChainId (int bus) noexcept { return -1 - bus; }
    /** How much of the sound goes to AUX 1 / AUX 2 (0..1). */
    void setAuxSend (int bus, float amount);
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

    //== Machine (V3): drum pads and step sequencer ===============================
    const MachineSettings& getMachineSettings() const noexcept { return machineSettings; }
    void setMachineSettings (const MachineSettings&);
    int getMachineStep() const noexcept { return machineSeq.currentStep.load(); }

    /** Where the newest voice of each zone is playing (for the waveform). */
    int getPlayheads (SamplerEngine::Playhead* out, int maxCount) const noexcept;

    /** Notes held on MIDI input or on the on-screen keyboard. The editor's keyboard plays
        through this, and it is safe to use from any thread. */
    juce::MidiKeyboardState keyboardState;
    int getKeyboardChannel() const noexcept
    {
        const int pc = partChannel.load();
        return pc > 0 ? pc : juce::jmax (1, setMidiChannel.load());
    }

    //== Arranger (V3): up to 16 presets at once, each on its own MIDI channel ==========
    // Part 1 is this instance; parts 2..16 are full instances of their own (made when used), so
    // every part has the whole engine: zones, effects, arpeggiator, machine, macros.
    static constexpr int numParts = 16, numOutputs = 16;
    struct PartInfo
    {
        int channel = 1;              // 1..16 (0 = every channel)
        int keyLo = 0, keyHi = 127;   // the notes it plays
        bool mute = false, solo = false;
        int output = 0;               // 0 = main out, k = Out k+1 (falls back to main when the DAW has it off)
    };
    bool isArrangerOn() const noexcept { return arrangerOn.load(); }
    void setArrangerOn (bool on);
    /** The part (0 = this one); null if that part is empty. */
    EmulonXProcessor* getPart (int index) noexcept;
    bool isPartUsed (int index) const noexcept;
    int getEditedPartIndex() const noexcept { return editedPart; }
    /** The part the editor shows and the keyboard plays. */
    EmulonXProcessor& getEditedPart() noexcept;
    /** Makes the part (Init preset) if it is empty, then edits it. */
    void setEditedPart (int index);
    void clearPart (int index);
    /** Copies the edited part's whole state (preset, FX, arp, machine) into another part. */
    void copyPartTo (int from, int to);
    PartInfo getPartInfo (int index) const noexcept;
    void setPartInfo (int index, PartInfo);
    /** Goes up whenever parts are made, cleared or changed (the Arranger tab refreshes). */
    int getArrangerVersion() const noexcept { return arrangerVersion; }
    /** Recent level of a part (0..1, for the Arranger's meters). */
    float takePartPeak (int index) noexcept;

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
    static BusesProperties makeBuses (bool asPart);
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
    PresetSource presetSource;
    PresetInfo presetInfo;
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
    MachineSettings machineSettings, pendingMachine;
    std::array<SamplerEngine::PadMod, 128> pendingPadMods {};
    bool machineChanged = false;
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

    // Arranger
    void renderOwnPart (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void getPartState (juce::MemoryBlock&);
    int setPartState (const void*, int);   // returns the bytes it read
    EmulonXProcessor* makePart (int index);
    void storePartInfo (int index, const PartInfo&);
    void applyArrangerState (const juce::MemoryBlock&);
    const bool isPart;
    std::array<std::unique_ptr<EmulonXProcessor>, numParts> parts;   // [0] stays empty (that's this)
    std::vector<std::unique_ptr<EmulonXProcessor>> retiredParts;
    juce::SpinLock partsLock;
    float childMaster = 1.0f;
    std::atomic<bool> arrangerOn { false };
    int editedPart = 0, arrangerVersion = 0, preparedBlock = 512;
    std::atomic<int> partChannel { -1 }, partKeyLo { 0 }, partKeyHi { 127 };   // routing of this instance (-1 = from the settings)
    std::array<PartInfo, numParts> partInfos;
    std::atomic<int> partOutput[numParts] {};
    std::atomic<bool> partMute[numParts] {}, partSolo[numParts] {};
    std::atomic<float> partPeak[numParts] {};
    juce::MidiBuffer hostMidi, partMidi;
    juce::AudioBuffer<float> partBuffer;

    // Audio
    SamplerEngine engine;
    fx::Engine fxEngine;
    Arpeggiator arp;
    MachineSequencer machineSeq;
    juce::MidiBuffer processedMidi, arpMidi, machineMidi;
    static constexpr int subBlock = 64, maxBuses = 32;
    float busData[maxBuses][2][subBlock] {};
    float mixL[subBlock] {}, mixR[subBlock] {};
    float auxL[2][subBlock] {}, auxR[2][subBlock] {};
    double currentSampleRate = 44100.0;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    juce::SmoothedValue<float> masterGain;
    std::atomic<float>* masterDb = nullptr;
    std::atomic<float>* macroValues[numMacros] {};
    std::atomic<float> peak[2] { 0.0f, 0.0f };
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> uiBend { 0.0f }, uiMod { 0.0f }, partVolumeDb { 0.0f }, partPan { 0.0f };
    std::atomic<bool> uiBendDirty { false }, uiModDirty { false };
    std::atomic<float> scopeLo[scopeSize] {}, scopeHi[scopeSize] {};
    std::atomic<int> scopeWrite { 0 };
    juce::SmoothedValue<float> partGainL, partGainR;
    static constexpr int maxPlayheads = 16;
    std::atomic<int> playheadCount { 0 }, playheadZone[maxPlayheads] {};
    std::atomic<double> playheadPos[maxPlayheads] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmulonXProcessor)
};
} // namespace emx
