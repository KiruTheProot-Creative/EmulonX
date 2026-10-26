// Emulon X — the FX part of the processor: editing chains, envelopes and modulation,
// and reading / writing them in presets and DAW projects.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

namespace emx
{
using namespace juce;

//== Lookup ==========================================================================
const fx::Chain* EmulonXProcessor::getFxChain (int chainId) const
{
    if (chainId == 0)
        return &fxSetup.master;
    if (chainId == auxChainId (0) || chainId == auxChainId (1))
        return &fxSetup.aux[chainId == auxChainId (0) ? 0 : 1];
    for (auto& z : zones)
        if (z.id == chainId)
            return &z.fx;
    return nullptr;
}

fx::Module* EmulonXProcessor::findFx (int moduleId, int* chainId)
{
    for (auto& m : fxSetup.master)
        if (m.id == moduleId)
        {
            if (chainId != nullptr) *chainId = 0;
            return &m;
        }
    for (auto& z : zones)
        for (auto& m : z.fx)
            if (m.id == moduleId)
            {
                if (chainId != nullptr) *chainId = z.id;
                return &m;
            }
    for (int k = 0; k < 2; ++k)
        for (auto& m : fxSetup.aux[k])
            if (m.id == moduleId)
            {
                if (chainId != nullptr) *chainId = auxChainId (k);
                return &m;
            }
    return nullptr;
}

void EmulonXProcessor::setAuxSend (int bus, float amount)
{
    if (bus < 0 || bus > 1)
        return;
    pushUndo ("auxsend" + String (bus));
    fxSetup.auxSend[bus] = jlimit (0.0f, 1.0f, amount);
    publishFx (false);
}

const fx::Module* EmulonXProcessor::findFxModule (int moduleId, int* chainId) const
{
    return const_cast<EmulonXProcessor*> (this)->findFx (moduleId, chainId);
}

//== Publishing ======================================================================
void EmulonXProcessor::pushFxSnapshot()
{
    std::vector<std::pair<int, const fx::Chain*>> zoneChains;
    std::vector<std::pair<int, int>> zoneSemis;
    for (auto& z : zones)
    {
        if (! z.fx.empty())
            zoneChains.emplace_back (z.id, &z.fx);
        zoneSemis.emplace_back (z.id, z.params.semitones);
    }
    std::shared_ptr<const fx::Snapshot> snap = fx::Snapshot::build (fxSetup, zoneChains, zoneSemis);
    releasePool.add (snap);
    const SpinLock::ScopedLockType sl (snapshotLock);
    pendingFx = snap;
}

void EmulonXProcessor::publishFx (bool structureChanged)
{
    pushFxSnapshot();
    if (structureChanged)
        ++fxStructure;
    {
        const ScopedLock sl (embedLock);
        embedDirty = true;
    }
    apvts.state.setProperty ("fx", JSON::toString (fxToJson (true), true), nullptr);
    sendChangeMessage();
}

//== Effects =========================================================================
void EmulonXProcessor::addFx (int chainId, fx::Type type)
{
    fx::Chain* chain = chainId == 0 ? &fxSetup.master : nullptr;
    if (chainId == auxChainId (0) || chainId == auxChainId (1))
        chain = &fxSetup.aux[chainId == auxChainId (0) ? 0 : 1];
    for (auto& z : zones)
        if (z.id == chainId)
            chain = &z.fx;
    if (chain == nullptr)
        return;
    pushUndo ("fxadd");
    chain->push_back (fx::makeModule (type, nextFxId++, currentSampleRate));
    publishFx (true);
}

void EmulonXProcessor::removeFx (int moduleId)
{
    auto erase = [moduleId] (fx::Chain& c)
    {
        const auto before = c.size();
        c.erase (std::remove_if (c.begin(), c.end(), [moduleId] (auto& m) { return m.id == moduleId; }), c.end());
        return c.size() != before;
    };
    pushUndo ("fxremove");
    bool found = erase (fxSetup.master);
    for (auto& z : zones)
        found = erase (z.fx) || found;
    for (auto& c : fxSetup.aux)
        found = erase (c) || found;
    if (! found)
        return;
    auto& r = fxSetup.routes;
    r.erase (std::remove_if (r.begin(), r.end(), [moduleId] (auto& q) { return q.target.module == moduleId; }), r.end());
    publishFx (true);
}

void EmulonXProcessor::moveFx (int moduleId, int delta)
{
    auto move = [&] (fx::Chain& c)
    {
        for (size_t i = 0; i < c.size(); ++i)
            if (c[i].id == moduleId)
            {
                const int to = (int) i + delta;
                if (to < 0 || to >= (int) c.size())
                    return true;
                pushUndo ("fxmove");
                std::swap (c[i], c[(size_t) to]);
                publishFx (true);
                return true;
            }
        return false;
    };
    if (move (fxSetup.master) || move (fxSetup.aux[0]) || move (fxSetup.aux[1]))
        return;
    for (auto& z : zones)
        if (move (z.fx))
            return;
}

void EmulonXProcessor::setFxOn (int moduleId, bool on)
{
    if (auto* m = findFx (moduleId))
    {
        pushUndo ("fxon" + String (moduleId));
        m->on = on;
        publishFx (false);
    }
}

void EmulonXProcessor::setFxMode (int moduleId, int mode)
{
    if (auto* m = findFx (moduleId))
    {
        pushUndo ("fxmode" + String (moduleId));
        fx::setMode (*m, mode);
        publishFx (true);
    }
}

void EmulonXProcessor::setFxSettings (int moduleId, const fx::Settings& st)
{
    if (auto* m = findFx (moduleId))
    {
        pushUndo ("fxset" + String (moduleId));
        const bool layoutChanged = st.sync != m->settings.sync || st.mode != m->settings.mode;
        m->settings = st;
        publishFx (layoutChanged);
    }
}

void EmulonXProcessor::setFxParam (int moduleId, int slot, float norm)
{
    if (auto* m = findFx (moduleId); m != nullptr && slot >= 0 && slot < fx::maxSlots)
    {
        pushUndo ("fxp" + String (moduleId) + ":" + String (slot));
        m->norm[(size_t) slot] = jlimit (0.0f, 1.0f, norm);
        publishFx (false);
    }
}

void EmulonXProcessor::setFxBpm (float bpm)
{
    pushUndo ("bpm");
    fxSetup.bpm = jlimit (20.0f, 300.0f, bpm);
    publishFx (false);
}

fx::Chain EmulonXProcessor::copyChainWithNewIds (const fx::Chain& chain)
{
    fx::Chain out;
    for (auto& m : chain)
    {
        auto copy = m;
        copy.id = nextFxId++;
        copy.processor = fx::createProcessor (m.type, currentSampleRate);
        out.push_back (std::move (copy));
    }
    return out;
}

//== Envelopes =======================================================================
void EmulonXProcessor::addEnvelope()
{
    if ((int) fxSetup.envelopes.size() >= fx::maxEnvelopes)
        return;
    pushUndo ("envadd");
    fxSetup.envelopes.push_back (fx::makeEnvelope (false, "Triangle"));
    publishFx (true);
}

void EmulonXProcessor::removeEnvelope (int index)
{
    auto& envs = fxSetup.envelopes;
    if (envs.size() < 2 || index < 0 || index >= (int) envs.size())
        return;
    pushUndo ("envremove");
    envs.erase (envs.begin() + index);
    auto& r = fxSetup.routes;
    r.erase (std::remove_if (r.begin(), r.end(), [index] (auto& q) { return q.source.kind == fx::SrcKind::env && q.source.index == index; }), r.end());
    for (auto& q : r)
        if (q.source.kind == fx::SrcKind::env && q.source.index > index)
            --q.source.index;
    publishFx (true);
}

void EmulonXProcessor::setEnvelope (int index, const fx::Envelope& e)
{
    if (index < 0 || index >= (int) fxSetup.envelopes.size())
        return;
    pushUndo ("env" + String (index));
    const bool typeChanged = fxSetup.envelopes[(size_t) index].ahdsr != e.ahdsr
                          || fxSetup.envelopes[(size_t) index].sync != e.sync
                          || fxSetup.envelopes[(size_t) index].kind != e.kind;
    fxSetup.envelopes[(size_t) index] = e;
    publishFx (typeChanged);
}

//== Modulation routes ===============================================================
bool EmulonXProcessor::hasRoute (fx::Source src, fx::Target t) const
{
    for (auto& r : fxSetup.routes)
        if (r.source == src && r.target == t)
            return true;
    return false;
}

void EmulonXProcessor::toggleRoute (fx::Source src, fx::Target t)
{
    pushUndo ("route");
    auto& routes = fxSetup.routes;
    for (size_t i = 0; i < routes.size(); ++i)
        if (routes[i].source == src && routes[i].target == t)
        {
            routes.erase (routes.begin() + (std::ptrdiff_t) i);
            message (describeSource (src) + " disconnected");
            publishFx (true);
            return;
        }
    routes.push_back ({ src, t, 0.5f });
    message (describeSource (src) + " > " + describeTarget (t));
    publishFx (true);
}

void EmulonXProcessor::setRouteAmount (int index, float amount)
{
    if (index < 0 || index >= (int) fxSetup.routes.size())
        return;
    pushUndo ("routeamt" + String (index));
    fxSetup.routes[(size_t) index].amount = jlimit (-1.0f, 1.0f, amount);
    publishFx (false);
}

void EmulonXProcessor::removeRoute (int index)
{
    if (index < 0 || index >= (int) fxSetup.routes.size())
        return;
    pushUndo ("routeremove");
    fxSetup.routes.erase (fxSetup.routes.begin() + index);
    publishFx (true);
}

String EmulonXProcessor::describeSource (fx::Source s) const
{
    switch (s.kind)
    {
        case fx::SrcKind::env:   return "ENV " + String (s.index + 1);
        case fx::SrcKind::macro: return getMacroName (s.index);
        case fx::SrcKind::wheel: return "MOD WHEEL";
    }
    return {};
}

String EmulonXProcessor::describeTarget (fx::Target t) const
{
    if (t.isMaster())
        return "MASTER - VOLUME";
    if (t.isZonePitch())
    {
        for (size_t i = 0; i < zones.size(); ++i)
            if (zones[i].id == t.slot)
                return "Z" + String ((int) i + 1) + " - SEMITONES";
        return "(REMOVED ZONE) - SEMITONES";
    }
    int chainId = 0;
    const auto* m = findFxModule (t.module, &chainId);
    if (m == nullptr)
        return "(REMOVED)";
    const auto* chain = getFxChain (chainId);
    String name = fx::def (m->type).name;
    int same = 0, pos = 0;
    for (auto& o : *chain)
        if (o.type == m->type)
        {
            ++same;
            if (o.id == m->id)
                pos = same;
        }
    if (same > 1)
        name << " " << pos;
    String prefix;
    if (chainId != 0)
        for (size_t i = 0; i < zones.size(); ++i)
            if (zones[i].id == chainId)
                prefix = "Z" + String ((int) i + 1) + " - ";
    const auto* sp = fx::specFor (m->type, m->settings.mode, t.slot);
    return (prefix + name + " - " + (sp != nullptr ? String (sp->label) : String ("?"))).toUpperCase();
}

//== Arpeggiator and voice mode ======================================================
void EmulonXProcessor::publishPlayModes()
{
    {
        const SpinLock::ScopedLockType sl (snapshotLock);
        pendingArp = arpSettings;
        arpChanged = true;
    }
    setMono = voiceSettings.mono;
    setLegato = voiceSettings.legato;
    setGlide = voiceSettings.glide;
}

void EmulonXProcessor::setArpSettings (const ArpSettings& a)
{
    if (a == arpSettings)
        return;
    pushUndo (a.enabled != arpSettings.enabled ? "arpon" : "arp");
    arpSettings = a;
    publishPlayModes();
    apvts.state.setProperty ("arp", JSON::toString (arpSettings.toJson(), true), nullptr);
    {
        const ScopedLock sl (embedLock);
        embedDirty = true;
    }
    sendChangeMessage();
}

void EmulonXProcessor::setVoiceSettings (const VoiceSettings& v)
{
    pushUndo (v.mono != voiceSettings.mono ? "voicemode" : "voice");
    voiceSettings = v;
    publishPlayModes();
    apvts.state.setProperty ("voice", JSON::toString (voiceSettings.toJson(), true), nullptr);
    {
        const ScopedLock sl (embedLock);
        embedDirty = true;
    }
    sendChangeMessage();
}

void EmulonXProcessor::setLearnMacro (int index)
{
    learnMacro = index;
    if (index >= 0)
        message ("Click any FX knob or the master volume to link " + getMacroName (index) + " (Esc cancels)");
    else
        sendChangeMessage();
}

float EmulonXProcessor::getMasterVolumeNorm() const
{
    return fx::masterVolumeSpec().toNorm (jmax (-60.0f, masterDb->load()));
}

//== JSON (presets and projects) =====================================================
var EmulonXProcessor::fxToJson (bool withZoneChains) const
{
    auto* o = new DynamicObject();
    o->setProperty ("bpm", fxSetup.bpm);
    o->setProperty ("volume", std::round (jmax (-60.0f, masterDb->load()) * 100.0f) / 100.0f);
    o->setProperty ("chain", fx::chainToJson (fxSetup.master));
    if (! fxSetup.aux[0].empty() || ! fxSetup.aux[1].empty() || fxSetup.auxSend[0] > 0.0f || fxSetup.auxSend[1] > 0.0f)
    {
        o->setProperty ("aux", Array<var> { fx::chainToJson (fxSetup.aux[0]), fx::chainToJson (fxSetup.aux[1]) });
        o->setProperty ("auxSend", Array<var> { fxSetup.auxSend[0], fxSetup.auxSend[1] });
    }
    Array<var> mods;
    for (auto& r : fxSetup.routes)
    {
        if (r.target.isMaster())
        {
            mods.add (fx::routeToJson (r, fx::Type::filter));
            continue;
        }
        if (r.target.isZonePitch())
        {
            // Zones are stored by their position in the preset.
            auto ordered = zones;
            std::sort (ordered.begin(), ordered.end(), [] (auto& a, auto& b) { return a.order < b.order; });
            for (size_t i = 0; i < ordered.size(); ++i)
                if (ordered[i].id == r.target.slot)
                {
                    auto j = fx::routeToJson (r, fx::Type::filter);
                    if (auto* jo = j.getDynamicObject())
                        jo->setProperty ("target", "zone" + String ((int) i) + ":semi");
                    mods.add (j);
                }
            continue;
        }
        if (const auto* m = findFxModule (r.target.module))
            if (fx::specFor (m->type, m->settings.mode, r.target.slot) != nullptr)
                mods.add (fx::routeToJson (r, m->type));
    }
    o->setProperty ("mods", mods);
    Array<var> envs;
    for (auto& e : fxSetup.envelopes)
        envs.add (fx::envelopeToJson (e));
    o->setProperty ("envelopes", envs);
    if (withZoneChains)
    {
        auto ordered = zones;
        std::sort (ordered.begin(), ordered.end(), [] (auto& a, auto& b) { return a.order < b.order; });
        Array<var> zc;
        for (auto& z : ordered)
            zc.add (fx::chainToJson (z.fx));
        o->setProperty ("zoneChains", zc);
    }
    return var (o);
}

void EmulonXProcessor::applyFxJson (const var& j, const std::vector<var>& zoneChainsJson, bool applyVolume)
{
    fxSetup = fx::Setup();
    std::map<String, int> idMap;

    // Zone channels: from the zones themselves (presets) or by position (projects).
    std::vector<var> zc = zoneChainsJson;
    if (auto* arr = j["zoneChains"].getArray())
        zc.assign (arr->begin(), arr->end());
    auto ordered = std::vector<ModelZone*>();
    for (auto& z : zones)
        ordered.push_back (&z);
    std::sort (ordered.begin(), ordered.end(), [] (auto* a, auto* b) { return a->order < b->order; });
    for (size_t i = 0; i < ordered.size(); ++i)
        ordered[i]->fx = i < zc.size() ? fx::chainFromJson (zc[i], idMap, nextFxId, currentSampleRate) : fx::Chain();

    if (j.isObject())
    {
        fxSetup.master = fx::chainFromJson (j["chain"], idMap, nextFxId, currentSampleRate);
        if (auto* aux = j["aux"].getArray())
            for (int k = 0; k < jmin (2, aux->size()); ++k)
                fxSetup.aux[k] = fx::chainFromJson ((*aux)[k], idMap, nextFxId, currentSampleRate);
        if (auto* sends = j["auxSend"].getArray())
            for (int k = 0; k < jmin (2, sends->size()); ++k)
                fxSetup.auxSend[k] = jlimit (0.0f, 1.0f, (float) (double) (*sends)[k]);
        if (auto* mods = j["mods"].getArray())
            for (auto& q : *mods)
            {
                fx::Route r;
                if (! fx::Source::parse (q["src"].toString(), r.source))
                    continue;
                r.amount = jlimit (-1.0f, 1.0f, (float) (double) q["amount"]);
                const auto target = q["target"].toString();
                if (target == "master:vol")
                    r.target = { fx::Target::masterVolume, 0 };
                else if (target.startsWith ("zone") && target.endsWith (":semi"))
                {
                    const int index = target.fromFirstOccurrenceOf ("zone", false, false).upToFirstOccurrenceOf (":", false, false).getIntValue();
                    if (index < 0 || index >= (int) ordered.size())
                        continue;
                    r.target = { fx::Target::zonePitch, ordered[(size_t) index]->id };
                }
                else
                {
                    const auto it = idMap.find (target.upToFirstOccurrenceOf (":", false, false));
                    if (it == idMap.end())
                        continue;
                    const auto* m = findFxModule (it->second);
                    if (m == nullptr)
                        continue;
                    r.target = { m->id, fx::slotIndex (m->type, target.fromFirstOccurrenceOf (":", false, false)) };
                    if (r.target.slot < 0)
                        continue;
                }
                fxSetup.routes.push_back (r);
            }
        if (auto* envs = j["envelopes"].getArray(); envs != nullptr && ! envs->isEmpty())
        {
            fxSetup.envelopes.clear();
            for (auto& e : *envs)
                if ((int) fxSetup.envelopes.size() < fx::maxEnvelopes)
                    fxSetup.envelopes.push_back (fx::envelopeFromJson (e));
        }
        if (j.hasProperty ("bpm"))
            fxSetup.bpm = jlimit (20.0f, 300.0f, (float) (double) j["bpm"]);
        if (applyVolume && j.hasProperty ("volume"))
            if (auto* p = apvts.getParameter (masterParamId))
                p->setValueNotifyingHost (p->convertTo0to1 (jlimit (-60.0f, 6.0f, (float) (double) j["volume"])));
    }
    ++fxStructure;
}
} // namespace emx
