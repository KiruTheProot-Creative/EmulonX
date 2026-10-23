// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
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
    publish();
    startTimer (1000);
}

EmulonXProcessor::~EmulonXProcessor()
{
    stopTimer();
    loaderThread.removeAllJobs (true, 10000);
    alive.reset();
}

bool EmulonXProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono();
}

void EmulonXProcessor::prepareToPlay (double sampleRate, int maxBlockSize)
{
    engine.prepare (sampleRate);
    loadMeasurer.reset (sampleRate, maxBlockSize);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (dbToGain (masterDb->load()));
}

void EmulonXProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, n);

    // Pick up a new zone snapshot if the editor published one (never blocks).
    {
        const SpinLock::ScopedTryLockType tl (snapshotLock);
        if (tl.isLocked() && pendingSnapshot != audioSnapshot)
        {
            audioSnapshot = pendingSnapshot;   // the release pool keeps the old one alive
            engine.setZoneSet (audioSnapshot);
        }
    }

    buffer.clear();
    if (buffer.getNumChannels() == 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float monoRight[256];

    auto renderRange = [&] (int from, int to)
    {
        if (R != nullptr)
        {
            engine.render (L + from, R + from, to - from);
            return;
        }
        for (int s = from; s < to; s += 256)   // mono output: fold right into left
        {
            const int len = jmin (256, to - s);
            std::fill (monoRight, monoRight + len, 0.0f);
            engine.render (L + s, monoRight, len);
            for (int i = 0; i < len; ++i)
                L[s + i] = (L[s + i] + monoRight[i]) * 0.5f;
        }
    };

    // Sample-accurate MIDI: render up to each event, then apply it.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = jlimit (0, n, meta.samplePosition);
        if (t > pos)
            renderRange (pos, t);
        pos = jmax (pos, t);
        engine.handleMidi (meta.getMessage());
    }
    if (pos < n)
        renderRange (pos, n);

    masterGain.setTargetValue (dbToGain (masterDb->load()));
    masterGain.applyGain (buffer, n);

    activeVoices.store (engine.getActiveVoiceCount());
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

void EmulonXProcessor::timerCallback() { releasePool.collect(); }

//== Zones model =================================================================
void EmulonXProcessor::publish()
{
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
    writeZonesToState();
    sendChangeMessage();
}

void EmulonXProcessor::setInterpolation (Interp i)
{
    interp = i;
    publish();
}

void EmulonXProcessor::setKeepSpeedForAll (bool ks)
{
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
    zones.erase (std::remove_if (zones.begin(), zones.end(), [id] (auto& z) { return z.id == id; }), zones.end());
    publish();
}

void EmulonXProcessor::clearZones()
{
    zones.clear();
    lastMessage = {};
    publish();
}

void EmulonXProcessor::autoMap()
{
    // Each distinct root key gets the keys halfway to its neighbours (same as the prototype).
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

void EmulonXProcessor::loadFiles (const Array<File>& files)
{
    int audioCount = 0, skipped = 0;
    for (auto& f : files)
    {
        if (! loader::isSupported (f)) ++skipped;
        else if (! f.hasFileExtension ("emxp;zip")) ++audioCount;
    }
    const bool mapAfter = audioCount > 1;
    if (skipped > 0)
        lastMessage = String (skipped) + " FILE(S) SKIPPED (UNSUPPORTED TYPE)";

    std::weak_ptr<bool> weak = alive;
    const int gen = loadGeneration;
    for (auto& f : files)
    {
        if (! loader::isSupported (f))
            continue;
        ++pendingLoads;
        loaderThread.addJob ([this, f, mapAfter, weak, gen]
        {
            auto result = loader::load (f);
            MessageManager::callAsync ([this, weak, result, mapAfter, gen]() mutable
            {
                if (weak.expired() || gen != loadGeneration)
                    return;
                --pendingLoads;
                addLoaded (std::move (result), mapAfter && pendingLoads.load() == 0);
            });
        });
    }
    sendChangeMessage();
}

void EmulonXProcessor::addLoaded (LoadResult&& r, bool autoMapAfter)
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
    if (r.interp >= 0)
        interp = (Interp) r.interp;

    if (! r.errors.isEmpty())
        lastMessage = r.errors.joinIntoString ("; ").toUpperCase();
    else if (r.presetName.isNotEmpty())
        lastMessage = "LOADED PRESET " + r.presetName.toUpperCase() + " (" + String ((int) r.zones.size()) + " ZONES)";
    else
        lastMessage = "ADDED " + String ((int) r.zones.size()) + " ZONE(S)";

    if (autoMapAfter)
        autoMap();
    else
        publish();
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
         .setProperty ("a", p.attack, nullptr).setProperty ("d", p.decay, nullptr)
         .setProperty ("s", p.sustain, nullptr).setProperty ("r", p.release, nullptr)
         .setProperty ("gain", p.gainDb, nullptr).setProperty ("pan", p.pan, nullptr)
         .setProperty ("tune", p.tuneCents, nullptr).setProperty ("ks", p.keepSpeed, nullptr)
         .setProperty ("grain", p.grainMs, nullptr);
        zt.appendChild (c, nullptr);
    }
    apvts.state.removeChild (apvts.state.getChildWithName (zonesId), nullptr);
    apvts.state.appendChild (zt, nullptr);
}

void EmulonXProcessor::restoreZonesFromState()
{
    loaderThread.removeAllJobs (true, 2000);
    ++loadGeneration;
    pendingLoads = 0;
    zones.clear();
    const auto zt = apvts.state.getChildWithName (zonesId);
    interp = (Interp) jlimit (0, 2, (int) zt.getProperty ("interp", 2));

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
        p.attack = c["a"]; p.decay = c["d"]; p.sustain = c["s"]; p.release = c["r"];
        p.gainDb = c["gain"]; p.pan = c["pan"]; p.tuneCents = c["tune"];
        p.keepSpeed = c["ks"]; p.grainMs = c["grain"];
        zones.push_back (z);
    }

    std::weak_ptr<bool> weak = alive;
    const int gen = loadGeneration;
    for (auto& z : zones)
    {
        ++pendingLoads;
        loaderThread.addJob ([this, id = z.id, source = z.source, weak, gen]
        {
            String err;
            auto data = loader::loadSource (source, err);
            MessageManager::callAsync ([this, weak, id, data, err, gen]
            {
                if (weak.expired() || gen != loadGeneration)
                    return;
                --pendingLoads;
                for (auto& mz : zones)
                    if (mz.id == id)
                    {
                        mz.sample = data;
                        mz.problem = data != nullptr ? String() : err.toUpperCase();
                        if (data != nullptr)
                            mz.params.clampTo (data->length());
                    }
                if (err.isNotEmpty())
                    lastMessage = err.toUpperCase();
                publish();
            });
        });
    }
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
    apvts.state.setProperty ("macroName" + String (index + 1), name.trim().substring (0, 12), nullptr);
}

void EmulonXProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void EmulonXProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            if (MessageManager::existsAndIsCurrentThread())
                restoreZonesFromState();
            else
            {
                std::weak_ptr<bool> weak = alive;
                MessageManager::callAsync ([this, weak] { if (! weak.expired()) restoreZonesFromState(); });
            }
        }
}
} // namespace emx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new emx::EmulonXProcessor();
}
