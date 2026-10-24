// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "NaturalSort.h"
#include "PluginEditor.h"

namespace emx
{
using namespace juce;

static float dbToGain (float db) { return db <= -59.9f ? 0.0f : Decibels::decibelsToGain (db); }

AudioProcessorValueTreeState::ParameterLayout EmulonXProcessor::createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    NormalisableRange<float> volRange (-60.0f, 6.0f, 0.01f);
    volRange.setSkewForCentre (-12.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { masterParamId, 1 }, "Master Volume", volRange, 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
            {
                return v <= -59.9f ? String ("-inf dB") : (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
            })));

    for (int i = 0; i < numMacros; ++i)
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { macroParamId (i), 1 }, "Macro " + String (i + 1),
            NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })));

    return layout;
}

EmulonXProcessor::EmulonXProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EmulonX", createParameterLayout())
{
    masterDb = apvts.getRawParameterValue (masterParamId);
    for (int i = 0; i < numMacros; ++i)
        macroValues[i] = apvts.getRawParameterValue (macroParamId (i));
    interp = (Interp) jlimit (0, 2, settings->getDefaultInterp());
    settings->addChangeListener (this);
    applySettings();
    publish();
    startTimer (1000);
}

EmulonXProcessor::~EmulonXProcessor()
{
    stopTimer();
    settings->removeChangeListener (this);
    loaderThread.removeAllJobs (true, 10000);
    alive.reset();
}

template <typename Result>
void EmulonXProcessor::runInBackground (std::function<Result()> work, std::function<void (Result&)> done)
{
    ++pendingJobs;
    sendChangeMessage();
    std::weak_ptr<bool> weak = alive;
    const int gen = loadGeneration;
    loaderThread.addJob ([this, work, done, weak, gen]
    {
        auto result = std::make_shared<Result> (work());
        MessageManager::callAsync ([this, done, weak, gen, result]
        {
            if (weak.expired())
                return;
            if (pendingJobs.load() > 0)
                --pendingJobs;
            if (gen == loadGeneration)
                done (*result);
            sendChangeMessage();
        });
    });
}

void EmulonXProcessor::message (const String& text)
{
    lastMessage = text.toUpperCase();
    sendChangeMessage();
}

//== Settings =====================================================================
void EmulonXProcessor::changeListenerCallback (ChangeBroadcaster*) { applySettings(); }

void EmulonXProcessor::applySettings()
{
    setMaxVoices = settings->getMaxVoices();
    setCpuLimit = (float) settings->getCpuLimit() / 100.0f;
    setStealQuietest = settings->getStealQuietest();
    setMidiChannel = settings->getMidiChannel();
    setVelocityCurve = settings->getVelocityCurve();
    setBendRange = settings->getBendRange();
    emx::setMiddleCOctave (settings->getMiddleCOctave());
    setTuneCents = (float) (1200.0 * std::log2 (jlimit (400.0f, 480.0f, settings->getA4()) / 440.0));
    if (settings->getEmbedSamples())
    {
        const ScopedLock sl (embedLock);
        embedDirty = true;
    }
}

//== Audio ========================================================================
bool EmulonXProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono();
}

void EmulonXProcessor::prepareToPlay (double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    arp.prepare (sampleRate);
    processedMidi.ensureSize (8192);
    arpMidi.ensureSize (8192);
    loadMeasurer.reset (sampleRate, maxBlockSize);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (dbToGain (masterDb->load()));

    // Effects keep their buffers sized for the sample rate they were made at; resize them now
    // (audio is stopped while the host calls this).
    std::shared_ptr<const fx::Snapshot> fxNow;
    {
        const SpinLock::ScopedLockType sl (snapshotLock);
        fxNow = pendingFx;
    }
    if (fxNow != nullptr)
        for (auto& proc : fxNow->keepAlive)
            proc->prepare (sampleRate);
    fxEngine.reset();
}

static int applyVelocityCurve (int v, int curve)
{
    if (curve == 1)
        return v;
    const double x = jlimit (1, 127, v) / 127.0;
    return jlimit (1, 127, (int) std::lround (127.0 * std::pow (x, curve == 0 ? 0.6 : 1.7)));
}

void EmulonXProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, n);

    engine.setMaxVoices (setMaxVoices.load());
    engine.setStealQuietest (setStealQuietest.load());
    engine.setGlobalTune (setTuneCents.load());
    engine.setBendRange ((float) setBendRange.load());
    engine.setVoiceMode (setMono.load(), setLegato.load(), setGlide.load());
    if (panicRequested.exchange (false))
    {
        engine.allNotesOff();
        arp.reset();
    }

    // Pick up new zones / preview sample if the editor published some (never blocks).
    {
        const SpinLock::ScopedTryLockType tl (snapshotLock);
        if (tl.isLocked())
        {
            if (pendingSnapshot != audioSnapshot)
            {
                audioSnapshot = pendingSnapshot;   // the release pool keeps the old one alive
                engine.setZoneSet (audioSnapshot);
            }
            if (pendingFx != audioFx)
                audioFx = pendingFx;
            if (arpChanged)
            {
                arp.setSettings (pendingArp);
                arpChanged = false;
            }
            if (previewChanged)
            {
                audioPreview = pendingPreview;
                previewPos = 0.0;
                previewChanged = false;
            }
        }
    }

    buffer.clear();
    if (buffer.getNumChannels() == 0)
        return;

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    // Notes from the on-screen keyboard join the incoming MIDI (and the keyboard shows what is held).
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    // What the FX need this block: tempo (from the host if it has one), macros, zone channels.
    const fx::Snapshot* fxs = audioFx.get();
    fxEngine.setSnapshot (fxs);
    double bpm = fxs != nullptr ? fxs->bpm : 120.0;
    std::optional<double> hostPpq;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto hostBpm = pos->getBpm(); hostBpm.hasValue() && *hostBpm > 1.0)
                bpm = *hostBpm;
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition(); ppq.hasValue())
                    hostPpq = *ppq;
        }
    float macros01[numMacros];
    for (int i = 0; i < numMacros; ++i)
        macros01[i] = macroValues[i]->load() / 100.0f;

    SamplerEngine::Bus buses[maxBuses];
    const fx::Snapshot::ChainRT* busChains[maxBuses];
    int numBuses = 0;
    if (fxs != nullptr)
        for (auto& c : fxs->zoneChains)
            if (numBuses < maxBuses)
            {
                busChains[numBuses] = &c;
                buses[numBuses] = { c.zoneId, busData[numBuses][0], busData[numBuses][1] };
                ++numBuses;
            }

    // Channel filter and velocity curve, then the arpeggiator (if it is on).
    const int channel = setMidiChannel.load(), curve = setVelocityCurve.load();
    processedMidi.clear();
    for (const auto meta : midi)
    {
        auto msg = meta.getMessage();
        if (channel > 0 && msg.getChannel() > 0 && msg.getChannel() != channel)
            continue;
        if (msg.isNoteOn())
            msg = MidiMessage::noteOn (msg.getChannel(), msg.getNoteNumber(), (uint8) applyVelocityCurve (msg.getVelocity(), curve));
        processedMidi.addEvent (msg, jlimit (0, n - 1, meta.samplePosition));
    }
    const MidiBuffer* notes = &processedMidi;
    if (arp.isActive() || arpSettings.enabled)
    {
        arp.process (processedMidi, arpMidi, n, bpm, hostPpq);
        notes = &arpMidi;
    }
    const float masterDbNow = masterDb->load();
    auto midiIt = notes->begin();

    // Work in short steps so envelopes and modulation move smoothly.
    for (int start = 0; start < n; start += subBlock)
    {
        const int len = jmin (subBlock, n - start);
        std::fill (mixL, mixL + len, 0.0f);
        std::fill (mixR, mixR + len, 0.0f);
        for (int b = 0; b < numBuses; ++b)
        {
            std::fill (busData[b][0], busData[b][0] + len, 0.0f);
            std::fill (busData[b][1], busData[b][1] + len, 0.0f);
        }

        auto renderPiece = [&] (int from, int to)
        {
            if (to <= from)
                return;
            SamplerEngine::Bus shifted[maxBuses];
            for (int b = 0; b < numBuses; ++b)
                shifted[b] = { buses[b].zoneId, buses[b].left + from, buses[b].right + from };
            engine.render (mixL + from, mixR + from, to - from, shifted, numBuses);
        };

        // Sample-accurate MIDI, with the channel filter and velocity curve from the settings.
        int pos = 0;
        for (; midiIt != notes->end(); ++midiIt)
        {
            const auto meta = *midiIt;
            const int t = jlimit (0, n, meta.samplePosition) - start;
            if (t >= len)
                break;
            if (t > pos)
                renderPiece (pos, t);
            pos = jmax (pos, t);
            engine.handleMidi (meta.getMessage());
        }
        renderPiece (pos, len);

        // Effects: each zone's own channel first, then the whole-preset chain.
        fxEngine.beginBlock (len, currentSampleRate, bpm, engine.getNoteOnCount(), engine.isGateOpen(),
                             macros01, numMacros, engine.getModWheel());
        {
            // Modulated SEMI knobs (used from the next step on, ~1 ms later).
            std::pair<int, float> pm[64];
            const int count = fxEngine.modulateZonePitch (pm, 64);
            SamplerEngine::PitchMod mods[64];
            for (int i = 0; i < count; ++i)
                mods[i] = { pm[i].first, pm[i].second };
            engine.setPitchMods (mods, count);
        }
        for (int b = 0; b < numBuses; ++b)
        {
            fxEngine.processChain (*busChains[b], busData[b][0], busData[b][1], len);
            for (int i = 0; i < len; ++i)
            {
                mixL[i] += busData[b][0][i];
                mixR[i] += busData[b][1][i];
            }
        }
        if (fxs != nullptr && ! fxs->master.modules.empty())
            fxEngine.processChain (fxs->master, mixL, mixR, len);

        // Explorer preview: plays the sample once at its own pitch (not through the effects).
        if (audioPreview != nullptr)
        {
            const auto& smp = *audioPreview;
            const double step = smp.sampleRate / currentSampleRate;
            const float* c0 = smp.audio.getReadPointer (0);
            const float* c1 = smp.audio.getNumChannels() > 1 ? smp.audio.getReadPointer (1) : c0;
            for (int i = 0; i < len; ++i)
            {
                if (previewPos >= smp.length() - 1)
                {
                    audioPreview.reset();   // the release pool still holds it
                    break;
                }
                mixL[i] += interpolate (c0, previewPos, smp.length(), Interp::linear, nullptr) * 0.5f;
                mixR[i] += interpolate (c1, previewPos, smp.length(), Interp::linear, nullptr) * 0.5f;
                previewPos += step;
            }
        }

        // Master volume (can be modulated, like in the prototype: linear dB scale).
        const auto& vs = fx::masterVolumeSpec();
        const float volDb = vs.fromNorm (fxEngine.modulateVolume (vs.toNorm (jmax (-60.0f, masterDbNow))));
        masterGain.setTargetValue (dbToGain (masterDbNow <= -59.9f ? -60.0f : volDb));
        for (int i = 0; i < len; ++i)
        {
            const float g = masterGain.getNextValue();
            if (outR != nullptr)
            {
                outL[start + i] = mixL[i] * g;
                outR[start + i] = mixR[i] * g;
            }
            else
                outL[start + i] = (mixL[i] + mixR[i]) * 0.5f * g;
        }
    }

    // CPU guard: past the limit, fade out one voice per block until the load drops.
    if (const auto limit = setCpuLimit.load(); limit > 0.0f && loadMeasurer.getLoadAsProportion() > limit)
        engine.stealOne();

    activeVoices.store (engine.getActiveVoiceCount());
    {
        SamplerEngine::Playhead ph[maxPlayheads];
        const int count = engine.getPlayheads (ph, maxPlayheads);
        for (int i = 0; i < count; ++i)
        {
            playheadZone[i].store (ph[i].zoneId, std::memory_order_relaxed);
            playheadPos[i].store (ph[i].position, std::memory_order_relaxed);
        }
        playheadCount.store (count, std::memory_order_release);
    }
    for (int ch = 0; ch < jmin (2, buffer.getNumChannels()); ++ch)
    {
        const auto m = buffer.getMagnitude (ch, 0, n);
        if (m > peak[ch].load())
            peak[ch].store (m);
    }
    if (buffer.getNumChannels() == 1)
        peak[1].store (peak[0].load());
}

AudioProcessorEditor* EmulonXProcessor::createEditor() { return new EmulonXEditor (*this); }

void EmulonXProcessor::timerCallback()
{
    releasePool.collect();

    // Keep an up-to-date packed copy of the preset for the DAW project (built off the UI thread).
    bool build = false;
    {
        const ScopedLock sl (embedLock);
        build = embedDirty && ! embedBuilding && settings->getEmbedSamples() && ! isBusy();
        if (build)
            embedBuilding = true;
    }
    if (build)
    {
        auto p = makePresetToSave();
        {
            const ScopedLock sl (embedLock);
            embedDirty = false;
        }
        std::weak_ptr<bool> weak = alive;
        loaderThread.addJob ([this, p, weak]
        {
            String err;
            auto bytes = std::make_shared<MemoryBlock> (loader::presetToBytes (p, err));
            MessageManager::callAsync ([this, weak, bytes]
            {
                if (weak.expired())
                    return;
                const ScopedLock sl (embedLock);
                embedBuilding = false;
                if (! embedDirty)
                    embeddedPreset = std::move (*bytes);
            });
        });
    }
}

//== Preset model =================================================================
MacroList EmulonXProcessor::currentMacros() const
{
    MacroList m;
    for (int i = 0; i < numMacros; ++i)
        m.emplace_back (getMacroName (i), apvts.getRawParameterValue (macroParamId (i))->load());
    return m;
}

PresetToSave EmulonXProcessor::makePresetToSave() const
{
    PresetToSave p;
    p.name = presetName;
    p.interp = interp;
    p.macros = currentMacros();
    p.fx = fxToJson (false);
    p.arp = arpSettings.toJson();
    p.voice = voiceSettings.toJson();
    auto ordered = zones;
    std::sort (ordered.begin(), ordered.end(), [] (auto& a, auto& b) { return a.order < b.order; });
    for (auto& z : ordered)
        if (z.sample != nullptr)
            p.zones.push_back ({ z.sample, z.params, z.fx.empty() ? var() : fx::chainToJson (z.fx) });
    return p;
}

void EmulonXProcessor::applyMacros (const MacroList& macros)
{
    for (int i = 0; i < numMacros; ++i)
    {
        const bool has = i < (int) macros.size();
        setMacroName (i, has ? macros[(size_t) i].first : String());
        if (auto* p = apvts.getParameter (macroParamId (i)))
            p->setValueNotifyingHost (p->convertTo0to1 (has ? jlimit (0.0f, 100.0f, macros[(size_t) i].second) : 0.0f));
    }
}

void EmulonXProcessor::publish()
{
    if (getSelectedZone() == nullptr)
        selectedZoneId = zones.empty() ? 0 : zones.front().id;

    auto set = std::make_shared<ZoneSet>();
    set->interp = interp;
    auto ordered = zones;
    std::sort (ordered.begin(), ordered.end(), [] (auto& a, auto& b) { return a.order < b.order; });
    for (auto& z : ordered)
        if (z.sample != nullptr)
        {
            set->zones.push_back ({ z.id, z.sample, z.params });
            releasePool.add (z.sample);
        }

    std::shared_ptr<const ZoneSet> snapshot = set;
    releasePool.add (snapshot);
    {
        const SpinLock::ScopedLockType sl (snapshotLock);
        pendingSnapshot = snapshot;
    }
    pushFxSnapshot();
    {
        const ScopedLock sl (embedLock);
        embedDirty = true;
    }
    writeZonesToState();
    sendChangeMessage();
}

void EmulonXProcessor::setPresetName (const String& n)
{
    presetName = n.trim().isNotEmpty() ? n.trim().substring (0, 40) : String ("Init");
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    publish();
}

void EmulonXProcessor::newPreset()
{
    clearUndo();
    zones.clear();
    presetName = "Init";
    presetFile = File();
    fxSetup = fx::Setup();
    arpSettings = ArpSettings();
    voiceSettings = VoiceSettings();
    publishPlayModes();
    ++fxStructure;
    interp = (Interp) jlimit (0, 2, settings->getDefaultInterp());
    applyMacros ({});
    message ("New preset");
    publish();
}

void EmulonXProcessor::savePresetAs (const File& file)
{
    auto p = makePresetToSave();
    if (p.zones.empty())
    {
        message ("Nothing to save yet - add some zones first");
        return;
    }
    runInBackground<String> ([p, file] { String err; return loader::savePreset (file, p, err) ? String() : err; },
                             [this, file] (String& err)
                             {
                                 if (err.isEmpty())
                                 {
                                     presetFile = file;
                                     ++filesWritten;
                                     message ("Saved " + file.getFileName());
                                 }
                                 else
                                     message (err);
                             });
}

void EmulonXProcessor::setInterpolation (Interp i)
{
    if (i == interp)
        return;
    pushUndo ("interp");
    interp = i;
    publish();
}

void EmulonXProcessor::setKeepSpeedForAll (bool ks)
{
    pushUndo ("keepspeed");
    for (auto& z : zones)
        z.params.keepSpeed = ks;
    publish();
}

bool EmulonXProcessor::allKeepSpeed() const
{
    for (auto& z : zones)
        if (! z.params.keepSpeed)
            return false;
    return ! zones.empty();
}

void EmulonXProcessor::removeZone (int id)
{
    pushUndo ("remove");
    if (id == selectedZoneId)
    {
        const int idx = getSelectedIndex();
        if (idx + 1 < (int) zones.size())
            selectedZoneId = zones[(size_t) idx + 1].id;
        else if (idx > 0)
            selectedZoneId = zones[(size_t) idx - 1].id;
    }
    zones.erase (std::remove_if (zones.begin(), zones.end(), [id] (auto& z) { return z.id == id; }), zones.end());
    publish();
}

void EmulonXProcessor::clearZones()
{
    pushUndo ("clear");
    zones.clear();
    lastMessage = {};
    publish();
}

void EmulonXProcessor::autoMap()
{
    pushUndo ("automap");
    mapByRoots();
}

void EmulonXProcessor::mapByRoots()
{
    std::vector<int> roots;
    for (auto& z : zones)
        if (std::find (roots.begin(), roots.end(), z.params.root) == roots.end())
            roots.push_back (z.params.root);
    std::sort (roots.begin(), roots.end());
    for (auto& z : zones)
    {
        const auto i = (size_t) (std::find (roots.begin(), roots.end(), z.params.root) - roots.begin());
        z.params.loKey = i == 0 ? 0 : (roots[i - 1] + roots[i]) / 2 + 1;
        z.params.hiKey = i + 1 < roots.size() ? (roots[i] + roots[i + 1]) / 2 : 127;
    }
    publish();
}

//== Zone editing =================================================================
const ModelZone* EmulonXProcessor::getSelectedZone() const noexcept
{
    for (auto& z : zones)
        if (z.id == selectedZoneId)
            return &z;
    return nullptr;
}

int EmulonXProcessor::getSelectedIndex() const noexcept
{
    for (size_t i = 0; i < zones.size(); ++i)
        if (zones[i].id == selectedZoneId)
            return (int) i;
    return -1;
}

void EmulonXProcessor::selectZone (int id)
{
    if (id == selectedZoneId)
        return;
    selectedZoneId = id;
    sendChangeMessage();
}

void EmulonXProcessor::selectNextZone (int direction)
{
    if (zones.empty())
        return;
    const int n = (int) zones.size();
    const int idx = getSelectedIndex();
    selectZone (zones[(size_t) (((idx < 0 ? 0 : idx + direction) % n + n) % n)].id);
}

void EmulonXProcessor::setZoneParams (int id, ZoneParams p)
{
    for (auto& z : zones)
        if (z.id == id)
        {
            if (z.sample != nullptr)
                p.clampTo (z.sample->length());
            p.attack = jlimit (0.001f, 10.0f, p.attack);
            p.decay = jlimit (0.001f, 10.0f, p.decay);
            p.release = jlimit (0.001f, 10.0f, p.release);
            p.sustain = jlimit (0.0f, 1.0f, p.sustain);
            p.gainDb = jlimit (-48.0f, 24.0f, p.gainDb);
            p.pan = jlimit (-100.0f, 100.0f, p.pan);
            p.tuneCents = jlimit (-100.0f, 100.0f, p.tuneCents);
            p.semitones = jlimit (-24, 24, p.semitones);
            p.grainMs = jlimit (20.0f, 150.0f, p.grainMs);
            pushUndo ("params" + String (id));
            z.params = p;
            publish();
            return;
        }
}

void EmulonXProcessor::copyEnvelope (int fromId, int toId)
{
    const ModelZone* from = nullptr;
    for (auto& z : zones)
        if (z.id == fromId)
            from = &z;
    if (from == nullptr)
        return;
    const auto src = from->params;
    bool any = false;
    for (auto& z : zones)
        if (z.id != fromId && (toId < 0 || z.id == toId))
        {
            if (! any)
                pushUndo ("envcopy" + String (fromId) + ">" + String (toId) + String (Time::getMillisecondCounter()));
            any = true;
            z.params.attack = src.attack;
            z.params.decay = src.decay;
            z.params.sustain = src.sustain;
            z.params.release = src.release;
        }
    if (any)
        publish();
}

void EmulonXProcessor::duplicateZone (int id)
{
    for (size_t i = 0; i < zones.size(); ++i)
        if (zones[i].id == id)
        {
            pushUndo ("duplicate");
            auto copy = zones[i];
            copy.id = nextZoneId++;
            copy.fx = copyChainWithNewIds (copy.fx);
            zones.insert (zones.begin() + (std::ptrdiff_t) i + 1, copy);
            for (size_t k = 0; k < zones.size(); ++k)
                zones[k].order = (int) k;
            nextOrder = (int) zones.size();
            selectedZoneId = copy.id;
            publish();
            return;
        }
}

void EmulonXProcessor::pushUndo (const String& tag)
{
    const auto now = Time::getMillisecondCounter();
    const bool sameGesture = tag == lastUndoTag && now - lastUndoTime < 1500 && ! undoStack.empty();
    lastUndoTag = tag;
    lastUndoTime = now;
    if (sameGesture)
        return;
    undoStack.push_back ({ zones, interp, selectedZoneId, fxSetup, arpSettings, voiceSettings });
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void EmulonXProcessor::clearUndo()
{
    undoStack.clear();
    redoStack.clear();
    lastUndoTag = {};
}

void EmulonXProcessor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back ({ zones, interp, selectedZoneId, fxSetup, arpSettings, voiceSettings });
    auto st = std::move (undoStack.back());
    undoStack.pop_back();
    zones = std::move (st.zones);
    interp = st.interp;
    selectedZoneId = st.selected;
    fxSetup = std::move (st.fx);
    arpSettings = st.arp;
    voiceSettings = st.voice;
    publishPlayModes();
    ++fxStructure;
    lastUndoTag = {};
    message ("Undo");
    publish();
}

void EmulonXProcessor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back ({ zones, interp, selectedZoneId, fxSetup, arpSettings, voiceSettings });
    auto st = std::move (redoStack.back());
    redoStack.pop_back();
    zones = std::move (st.zones);
    interp = st.interp;
    selectedZoneId = st.selected;
    fxSetup = std::move (st.fx);
    arpSettings = st.arp;
    voiceSettings = st.voice;
    publishPlayModes();
    ++fxStructure;
    lastUndoTag = {};
    message ("Redo");
    publish();
}

int EmulonXProcessor::getPlayheads (SamplerEngine::Playhead* out, int maxCount) const noexcept
{
    const int count = jmin (maxCount, playheadCount.load (std::memory_order_acquire));
    for (int i = 0; i < count; ++i)
        out[i] = { playheadZone[i].load (std::memory_order_relaxed), playheadPos[i].load (std::memory_order_relaxed) };
    return count;
}

//== Loading ======================================================================
void EmulonXProcessor::loadFiles (const Array<File>& files)
{
    Array<File> audio;
    int skipped = 0;
    for (auto& f : files)
    {
        if (loader::isAudioFile (f))
            audio.add (f);
        else if (loader::isBankFile (f) || (f.hasFileExtension ("zip") && ZipFile (f).getEntry ("bank.json") != nullptr))
            openBank (f);
        else if (loader::isSupported (f))
            runInBackground<LoadResult> ([f] { return loader::loadPreset (f); },
                                         [this, f] (LoadResult& r) { addLoaded (std::move (r), false, f); });
        else
            ++skipped;
    }
    if (skipped > 0)
        message (String (skipped) + " file(s) skipped (unsupported type)");

    const bool mapAfter = audio.size() > 1;
    for (auto& f : audio)
        runInBackground<LoadResult> ([f] { return loader::loadAudioFile (f); },
                                     [this, mapAfter] (LoadResult& r) { addLoaded (std::move (r), mapAfter && pendingJobs.load() == 0); });
}

void EmulonXProcessor::loadPresetFromBankFile (const File& bankFile, const String& entry, const String& name)
{
    runInBackground<LoadResult> ([bankFile, entry, name]
                                 {
                                     const auto bytes = loader::readBankEntry (bankFile, entry);
                                     if (bytes.isEmpty())
                                     {
                                         LoadResult r;
                                         r.errors.add (name + ": missing in " + bankFile.getFileName());
                                         return r;
                                     }
                                     return loader::loadPresetFromMemory (bytes, "emxb|" + bankFile.getFullPathName() + "|" + entry, name);
                                 },
                                 [this] (LoadResult& r) { addLoaded (std::move (r), false); });
}

void EmulonXProcessor::addLoaded (LoadResult&& r, bool autoMapAfter, const File& fromFile)
{
    if (r.isPreset)
    {
        if (r.zones.empty())
        {
            message (r.errors.isEmpty() ? String ("The preset has no zones") : r.errors.joinIntoString ("; "));
            return;
        }
        clearUndo();
        zones.clear();
        presetName = r.presetName;
        presetFile = loader::isPresetFile (fromFile) ? fromFile : File();
        if (r.interp >= 0)
            interp = (Interp) r.interp;
        if (! r.macros.empty())
            applyMacros (r.macros);
        updateHostDisplay (ChangeDetails().withProgramChanged (true));
    }

    if (! r.isPreset && ! r.zones.empty())
        pushUndo ("add" + String (nextZoneId));
    if (! r.zones.empty())
        selectedZoneId = nextZoneId;   // the first new zone opens in the editor
    for (auto& lz : r.zones)
    {
        ModelZone z;
        z.id = nextZoneId++;
        z.order = nextOrder++;
        z.sample = lz.sample;
        z.source = lz.sample->source;
        z.name = lz.sample->name;
        z.params = lz.params;
        if (! r.isPreset)
            z.params.keepSpeed = settings->getDefaultKeepSpeed();
        zones.push_back (std::move (z));
    }
    if (r.isPreset)
    {
        std::vector<var> zoneFx;
        for (auto& lz : r.zones)
            zoneFx.push_back (lz.fx);
        applyFxJson (r.fx, zoneFx, true);
        arpSettings = ArpSettings::fromJson (r.arp);
        voiceSettings = VoiceSettings::fromJson (r.voice);
        publishPlayModes();
    }

    if (! r.errors.isEmpty())
        message (r.errors.joinIntoString ("; "));
    else if (r.isPreset)
        message ("Loaded " + r.presetName + " (" + String ((int) r.zones.size()) + " zones)");
    else
        message ("Added " + String ((int) r.zones.size()) + " zone(s)");

    if (autoMapAfter)
        mapByRoots();
    else
        publish();
}

//== Bank =========================================================================
void EmulonXProcessor::addCurrentPresetToBank()
{
    auto p = makePresetToSave();
    if (p.zones.empty())
    {
        message ("Nothing to add yet - add some zones first");
        return;
    }
    runInBackground<MemoryBlock> ([p] { String err; return loader::presetToBytes (p, err); },
                                  [this, name = p.name] (MemoryBlock& bytes)
                                  {
                                      if (bytes.isEmpty())
                                          return message ("Could not add the preset");
                                      bank.push_back ({ name, std::move (bytes) });
                                      message ("Added " + name + " to the bank");
                                  });
}

void EmulonXProcessor::removeBankEntry (int index)
{
    if (index >= 0 && index < (int) bank.size())
        bank.erase (bank.begin() + index);
    sendChangeMessage();
}

int EmulonXProcessor::sortBank (int keepIndex)
{
    std::vector<int> order ((size_t) bank.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = (int) i;
    std::stable_sort (order.begin(), order.end(), [this] (int a, int b) { return naturalLess (bank[(size_t) a].name, bank[(size_t) b].name); });
    std::vector<BankEntry> sorted;
    int newIndex = -1;
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (order[i] == keepIndex)
            newIndex = (int) i;
        sorted.push_back (std::move (bank[(size_t) order[i]]));
    }
    bank = std::move (sorted);
    sendChangeMessage();
    return newIndex;
}

void EmulonXProcessor::clearBank()
{
    bank.clear();
    sendChangeMessage();
}

void EmulonXProcessor::loadBankEntry (int index)
{
    if (index < 0 || index >= (int) bank.size())
        return;
    const auto entry = bank[(size_t) index];
    runInBackground<LoadResult> ([entry, index] { return loader::loadPresetFromMemory (entry.data, "bank|" + String (index), entry.name); },
                                 [this] (LoadResult& r) { addLoaded (std::move (r), false); });
}

void EmulonXProcessor::openBank (const File& file)
{
    using Loaded = std::pair<BankInfo, std::vector<BankEntry>>;
    runInBackground<Loaded> ([file]
                             {
                                 Loaded l;
                                 l.first = loader::readBankInfo (file);
                                 for (int i = 0; i < l.first.presetEntries.size(); ++i)
                                     l.second.push_back ({ l.first.presetNames[i], loader::readBankEntry (file, l.first.presetEntries[i]) });
                                 return l;
                             },
                             [this] (Loaded& l)
                             {
                                 if (l.first.error.isNotEmpty())
                                     return message (l.first.error);
                                 bankName = l.first.name;
                                 bank = std::move (l.second);
                                 message ("Opened bank " + bankName + " (" + String ((int) bank.size()) + " presets)");
                             });
}

void EmulonXProcessor::exportBank (const File& file)
{
    if (bank.empty())
        return message ("The bank is empty - add presets first");
    std::vector<std::pair<String, MemoryBlock>> items;
    for (auto& e : bank)
        items.emplace_back (e.name, e.data);
    runInBackground<String> ([file, items, name = bankName] { String err; return loader::saveBank (file, name, items, err) ? String() : err; },
                             [this, file] (String& err)
                             {
                                 if (err.isEmpty())
                                     ++filesWritten;
                                 message (err.isEmpty() ? "Exported " + file.getFileName() : err);
                             });
}

//== Preview ======================================================================
void EmulonXProcessor::previewFile (const File& file)
{
    runInBackground<LoadResult> ([file] { return loader::loadAudioFile (file); },
                                 [this] (LoadResult& r)
                                 {
                                     if (r.zones.empty())
                                         return message (r.errors.joinIntoString ("; "));
                                     releasePool.add (r.zones[0].sample);
                                     const SpinLock::ScopedLockType sl (snapshotLock);
                                     pendingPreview = r.zones[0].sample;
                                     previewChanged = true;
                                 });
}

void EmulonXProcessor::stopPreview()
{
    const SpinLock::ScopedLockType sl (snapshotLock);
    pendingPreview.reset();
    previewChanged = true;
}

//== State: zones are saved with the DAW project =================================
static const Identifier zonesId ("ZONES"), zoneId ("ZONE");

void EmulonXProcessor::writeZonesToState()
{
    ValueTree zt (zonesId);
    zt.setProperty ("interp", (int) interp, nullptr);
    auto ordered = zones;
    std::sort (ordered.begin(), ordered.end(), [] (auto& a, auto& b) { return a.order < b.order; });
    for (auto& z : ordered)
    {
        const auto& p = z.params;
        ValueTree c (zoneId);
        c.setProperty ("source", z.source, nullptr)
         .setProperty ("name", z.name, nullptr)
         .setProperty ("root", p.root, nullptr).setProperty ("lo", p.loKey, nullptr).setProperty ("hi", p.hiKey, nullptr)
         .setProperty ("vlo", p.loVel, nullptr).setProperty ("vhi", p.hiVel, nullptr)
         .setProperty ("loop", (int) p.loopMode, nullptr).setProperty ("ls", p.loopStart, nullptr)
         .setProperty ("le", p.loopEnd, nullptr).setProperty ("xf", p.crossfade, nullptr)
         .setProperty ("ss", p.sampleStart, nullptr).setProperty ("se", p.sampleEnd, nullptr)
         .setProperty ("rs", p.randomStart, nullptr).setProperty ("re", p.randomEnd, nullptr)
         .setProperty ("a", p.attack, nullptr).setProperty ("d", p.decay, nullptr)
         .setProperty ("s", p.sustain, nullptr).setProperty ("r", p.release, nullptr)
         .setProperty ("gain", p.gainDb, nullptr).setProperty ("pan", p.pan, nullptr)
         .setProperty ("tune", p.tuneCents, nullptr).setProperty ("ks", p.keepSpeed, nullptr)
         .setProperty ("semi", p.semitones, nullptr)
         .setProperty ("grain", p.grainMs, nullptr);
        zt.appendChild (c, nullptr);
    }
    apvts.state.removeChild (apvts.state.getChildWithName (zonesId), nullptr);
    apvts.state.appendChild (zt, nullptr);
    apvts.state.setProperty ("presetName", presetName, nullptr);
    apvts.state.setProperty ("presetFile", presetFile.getFullPathName(), nullptr);
    apvts.state.setProperty ("fx", JSON::toString (fxToJson (true), true), nullptr);
    apvts.state.setProperty ("arp", JSON::toString (arpSettings.toJson(), true), nullptr);
    apvts.state.setProperty ("voice", JSON::toString (voiceSettings.toJson(), true), nullptr);
}

void EmulonXProcessor::restoreZonesFromState (const MemoryBlock& embedded)
{
    loaderThread.removeAllJobs (true, 2000);
    ++loadGeneration;
    pendingJobs = 0;
    clearUndo();
    {
        const ScopedLock sl (embedLock);
        embedBuilding = false;
    }
    zones.clear();
    presetName = apvts.state.getProperty ("presetName", "Init").toString();
    presetFile = File (apvts.state.getProperty ("presetFile", "").toString());
    const auto zt = apvts.state.getChildWithName (zonesId);
    interp = (Interp) jlimit (0, 2, (int) zt.getProperty ("interp", 2));

    // Projects saved with "samples inside the project": everything comes from the packed preset.
    const auto stateFx = JSON::parse (apvts.state.getProperty ("fx", "").toString());
    arpSettings = ArpSettings::fromJson (JSON::parse (apvts.state.getProperty ("arp", "").toString()));
    voiceSettings = VoiceSettings::fromJson (JSON::parse (apvts.state.getProperty ("voice", "").toString()));
    publishPlayModes();
    if (! embedded.isEmpty())
    {
        const auto name = presetName;
        runInBackground<LoadResult> ([embedded, name] { return loader::loadPresetFromMemory (embedded, "project", name); },
                                     [this, stateFx] (LoadResult& r)
                                     {
                                         for (auto& lz : r.zones)
                                         {
                                             ModelZone z;
                                             z.id = nextZoneId++;
                                             z.order = nextOrder++;
                                             z.sample = lz.sample;
                                             z.source = lz.sample->source;
                                             z.name = lz.sample->name;
                                             z.params = lz.params;
                                             zones.push_back (std::move (z));
                                         }
                                         std::vector<var> zoneFx;
                                         for (auto& lz : r.zones)
                                             zoneFx.push_back (lz.fx);
                                         if (stateFx.isObject())
                                             applyFxJson (stateFx, {}, false);
                                         else
                                             applyFxJson (r.fx, zoneFx, false);
                                         {
                                             const ScopedLock sl (embedLock);
                                             embedDirty = true;
                                         }
                                         publish();
                                     });
        publish();
        return;
    }

    // Otherwise each zone re-loads its sample from where it was on disk.
    for (const auto& c : zt)
    {
        ModelZone z;
        z.id = nextZoneId++;
        z.order = nextOrder++;
        z.source = c["source"].toString();
        z.name = c["name"].toString();
        z.problem = "LOADING";
        auto& p = z.params;
        p.root = c["root"]; p.loKey = c["lo"]; p.hiKey = c["hi"]; p.loVel = c["vlo"]; p.hiVel = c["vhi"];
        p.loopMode = (LoopMode) jlimit (0, 2, (int) c["loop"]);
        p.loopStart = c["ls"]; p.loopEnd = c["le"]; p.crossfade = c["xf"];
        p.sampleStart = c["ss"]; p.sampleEnd = c["se"];
        p.randomStart = c["rs"]; p.randomEnd = c["re"];
        p.attack = c["a"]; p.decay = c["d"]; p.sustain = c["s"]; p.release = c["r"];
        p.gainDb = c["gain"]; p.pan = c["pan"]; p.tuneCents = c["tune"]; p.semitones = c["semi"];
        p.keepSpeed = c["ks"]; p.grainMs = c["grain"];
        zones.push_back (z);
    }
    applyFxJson (stateFx, {}, false);

    using Loaded = std::pair<std::shared_ptr<const SampleData>, String>;
    for (auto& z : zones)
        runInBackground<Loaded> ([source = z.source]
                                 {
                                     String err;
                                     auto data = loader::loadSource (source, err);
                                     return Loaded { data, err };
                                 },
                                 [this, id = z.id] (Loaded& l)
                                 {
                                     for (auto& mz : zones)
                                         if (mz.id == id)
                                         {
                                             mz.sample = l.first;
                                             mz.problem = l.first != nullptr ? String() : l.second.toUpperCase();
                                             if (l.first != nullptr)
                                                 mz.params.clampTo (l.first->length());
                                         }
                                     if (l.second.isNotEmpty())
                                         message (l.second);
                                     publish();
                                 });
    publish();
}

//== Macro names live in the plugin state, so they are saved with the DAW project.
String EmulonXProcessor::getMacroName (int index) const
{
    const auto name = apvts.state.getProperty ("macroName" + String (index + 1)).toString();
    return name.isNotEmpty() ? name : "MACRO " + String (index + 1);
}

void EmulonXProcessor::setMacroName (int index, const String& name)
{
    const auto clean = name.trim().substring (0, 12);
    if (clean.isEmpty() || clean.equalsIgnoreCase ("MACRO " + String (index + 1)))
        apvts.state.removeProperty ("macroName" + String (index + 1), nullptr);
    else
        apvts.state.setProperty ("macroName" + String (index + 1), clean, nullptr);
}

//== Project save / load ==========================================================
// Layout: "EMXS", version, XML (parameters + zone list), then optionally the whole
// preset with its samples, so the project works even if the sample files move.
void EmulonXProcessor::getStateInformation (MemoryBlock& dest)
{
    MemoryBlock xmlBin;
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, xmlBin);

    MemoryBlock packed;
    const bool allLoaded = std::all_of (zones.begin(), zones.end(), [] (auto& z) { return z.sample != nullptr; });
    if (settings->getEmbedSamples() && ! zones.empty() && allLoaded)
    {
        const ScopedLock sl (embedLock);
        if (embedDirty && MessageManager::existsAndIsCurrentThread())
        {
            String err;
            embeddedPreset = loader::presetToBytes (makePresetToSave(), err);
            embedDirty = false;
        }
        packed = embeddedPreset;
    }

    MemoryOutputStream out (dest, false);
    out.write ("EMXS", 4);
    out.writeInt (1);
    out.writeInt64 ((int64) xmlBin.getSize());
    out.write (xmlBin.getData(), xmlBin.getSize());
    out.writeInt64 ((int64) packed.getSize());
    if (! packed.isEmpty())
        out.write (packed.getData(), packed.getSize());
}

void EmulonXProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<XmlElement> xml;
    MemoryBlock packed;

    if (size >= 8 && std::memcmp (data, "EMXS", 4) == 0)
    {
        MemoryInputStream in (data, (size_t) size, false);
        in.skipNextBytes (4);
        in.readInt();   // version
        const auto xmlSize = in.readInt64();
        MemoryBlock xmlBin;
        if (xmlSize > 0 && xmlSize <= size)
            in.readIntoMemoryBlock (xmlBin, (ssize_t) xmlSize);
        xml = getXmlFromBinary (xmlBin.getData(), (int) xmlBin.getSize());
        const auto packedSize = in.readInt64();
        if (packedSize > 0 && packedSize <= size)
            in.readIntoMemoryBlock (packed, (ssize_t) packedSize);
    }
    else
    {
        xml = getXmlFromBinary (data, size);   // projects saved with milestone 1 or 2
    }

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (ValueTree::fromXml (*xml));
    {
        const ScopedLock sl (embedLock);
        embeddedPreset = packed;
        embedDirty = packed.isEmpty();
    }
    if (MessageManager::existsAndIsCurrentThread())
        restoreZonesFromState (packed);
    else
    {
        std::weak_ptr<bool> weak = alive;
        MessageManager::callAsync ([this, weak, packed] { if (! weak.expired()) restoreZonesFromState (packed); });
    }
}
} // namespace emx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new emx::EmulonXProcessor();
}
