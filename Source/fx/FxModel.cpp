// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxModel.h"

namespace emx::fx
{
using juce::String;
using juce::var;

float Module::value (int slot) const
{
    const auto* sp = specFor (type, settings.mode, slot);
    return sp != nullptr ? sp->fromNorm (norm[(size_t) slot]) : 0.0f;
}

Module makeModule (Type t, int id, double sr)
{
    Module m;
    m.id = id;
    m.type = t;
    m.settings.mode = def (t).defaultMode;
    for (int i = 0; i < maxSlots; ++i)
        if (const auto* sp = specFor (t, m.settings.mode, i))
            m.norm[(size_t) i] = sp->toNorm (sp->def);
    m.processor = createProcessor (t, sr);
    return m;
}

void setMode (Module& m, int mode)
{
    m.settings.mode = juce::jlimit (0, juce::jmax (0, (int) def (m.type).modes.size() - 1), mode);
    const auto& d = def (m.type);
    for (size_t i = 0; i < d.slots.size(); ++i)
        if (d.slots[i].ab >= 0)
            if (const auto* sp = specFor (m.type, m.settings.mode, (int) i))
                m.norm[i] = sp->toNorm (sp->def);
}

//== Envelopes ======================================================================
float Envelope::shapeAt (float x) const noexcept
{
    const auto& P = points;
    if (P.empty())
        return 0.0f;
    if (x <= P.front().x)
        return P.front().y;
    for (size_t i = 0; i + 1 < P.size(); ++i)
    {
        const auto& p0 = P[i];
        const auto& p1 = P[i + 1];
        if (x <= p1.x)
        {
            const float w = p1.x - p0.x;
            if (w <= 1.0e-9f)
                continue;
            return p0.y + (p1.y - p0.y) * curve ((x - p0.x) / w, p0.c);
        }
    }
    return P.back().y;
}

const std::vector<std::pair<String, std::vector<EnvPoint>>>& shapeLibrary()
{
    static const std::vector<std::pair<String, std::vector<EnvPoint>>> lib {
        { "Triangle", { { 0, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } } },
        { "Sine", { { 0, 0.5f, -0.45f }, { 0.25f, 1, 0.45f }, { 0.5f, 0.5f, -0.45f }, { 0.75f, 0, 0.45f }, { 1, 0.5f, 0 } } },
        { "Saw Down", { { 0, 1, 0 }, { 1, 0, 0 } } },
        { "Saw Up", { { 0, 0, 0 }, { 1, 1, 0 } } },
        { "Square", { { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } } },
        { "Pluck", { { 0, 1, 0.6f }, { 1, 0, 0 } } },
        { "Rise", { { 0, 0, 0.6f }, { 1, 1, 0 } } },
        { "Steps", { { 0, 0.25f, 0 }, { 0.25f, 0.25f, 0 }, { 0.25f, 0.75f, 0 }, { 0.5f, 0.75f, 0 }, { 0.5f, 0.5f, 0 },
                     { 0.75f, 0.5f, 0 }, { 0.75f, 1, 0 }, { 1, 1, 0 } } },
        { "Flat", { { 0, 0, 0 }, { 1, 0, 0 } } },
    };
    return lib;
}

Envelope makeEnvelope (bool ahdsr, const String& shape, int num, int den)
{
    Envelope e;
    e.ahdsr = ahdsr;
    e.num = num;
    e.den = den;
    for (auto& [name, pts] : shapeLibrary())
        if (name == shape)
            e.points = pts;
    if (e.points.empty())
        e.points = shapeLibrary().front().second;
    return e;
}

std::vector<Envelope> defaultEnvelopes()
{
    return { makeEnvelope (false, "Triangle"), makeEnvelope (true, "Triangle"),
             makeEnvelope (false, "Saw Down", 1, 8), makeEnvelope (false, "Square", 1, 2) };
}

//== Modulation keys =============================================================
String Source::key() const
{
    switch (kind)
    {
        case SrcKind::env:   return "e" + String (index);
        case SrcKind::macro: return "m" + String (index);
        case SrcKind::wheel: return "w";
    }
    return {};
}

bool Source::parse (const String& s, Source& out)
{
    if (s == "w") { out = { SrcKind::wheel, 0 }; return true; }
    if (s.length() < 2 || ! s.substring (1).containsOnly ("0123456789"))
        return false;
    if (s[0] == 'e') { out = { SrcKind::env, s.substring (1).getIntValue() }; return out.index < maxEnvelopes; }
    if (s[0] == 'm') { out = { SrcKind::macro, s.substring (1).getIntValue() }; return out.index >= 0 && out.index < maxMacros; }
    return false;
}

String Target::key (Type t) const
{
    if (module == masterVolume)
        return "master:vol";
    if (module == zonePitch)
        return "zone:semi";   // the processor writes the zone's position in the preset
    const auto& d = def (t);
    return "f" + String (module) + ":" + (slot >= 0 && slot < (int) d.slots.size() ? String (d.slots[(size_t) slot].id) : String());
}

//== JSON ===========================================================================
var chainToJson (const Chain& chain)
{
    juce::Array<var> arr;
    for (auto& m : chain)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", "f" + String (m.id));
        o->setProperty ("type", def (m.type).key);
        o->setProperty ("on", m.on);
        auto* st = new juce::DynamicObject();
        st->setProperty ("mode", m.settings.mode);
        if (m.type == Type::filter)
            st->setProperty ("slope", m.settings.slope);
        if (m.type == Type::delay)
        {
            st->setProperty ("sync", m.settings.sync);
            st->setProperty ("num", m.settings.num);
            st->setProperty ("den", m.settings.den);
            st->setProperty ("dot", m.settings.dot);
        }
        o->setProperty ("settings", var (st));
        auto* params = new juce::DynamicObject();
        const auto& d = def (m.type);
        for (size_t i = 0; i < d.slots.size(); ++i)
            if (specFor (m.type, m.settings.mode, (int) i) != nullptr)
                params->setProperty (d.slots[i].id, std::round (m.value ((int) i) * 100000.0) / 100000.0);
        o->setProperty ("params", var (params));
        arr.add (var (o));
    }
    return arr;
}

Chain chainFromJson (const var& json, std::map<String, int>& idMap, int& nextId, double sr)
{
    Chain out;
    if (auto* arr = json.getArray())
        for (auto& c : *arr)
        {
            const auto* d = defByKey (c["type"].toString());
            if (d == nullptr)
                continue;
            auto m = makeModule (d->type, nextId++, sr);
            m.on = c.hasProperty ("on") ? (bool) c["on"] : true;
            const auto st = c["settings"];
            if (st.hasProperty ("mode"))  m.settings.mode = juce::jlimit (0, (int) d->modes.size() - 1, (int) st["mode"]);
            if (st.hasProperty ("slope")) m.settings.slope = juce::jlimit (0, 1, (int) st["slope"]);
            if (st.hasProperty ("sync"))  m.settings.sync = (bool) st["sync"];
            if (st.hasProperty ("num"))   m.settings.num = juce::jlimit (1, 32, (int) st["num"]);
            if (st.hasProperty ("den"))   m.settings.den = juce::jlimit (1, 64, (int) st["den"]);
            if (st.hasProperty ("dot"))   m.settings.dot = juce::jlimit (0, 2, (int) st["dot"]);
            const auto params = c["params"];
            for (size_t i = 0; i < d->slots.size(); ++i)
                if (const auto* sp = specFor (d->type, m.settings.mode, (int) i))
                {
                    const auto pv = params[d->slots[i].id];
                    m.norm[i] = sp->toNorm (pv.isVoid() ? sp->def : (float) (double) pv);
                }
            if (c.hasProperty ("id"))
                idMap[c["id"].toString()] = m.id;
            out.push_back (std::move (m));
        }
    return out;
}

var envelopeToJson (const Envelope& e)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", e.kind == Envelope::steps ? "steps" : e.kind == Envelope::random ? "random" : e.ahdsr ? "ahdsr" : "shape");
    {
        juce::Array<var> st;
        for (auto v : e.stepValues)
            st.add (v);
        o->setProperty ("steps", st);
        o->setProperty ("numSteps", e.numSteps);
        o->setProperty ("randomType", e.randomType);
    }
    juce::Array<var> pts;
    for (auto& p : e.points)
    {
        auto* q = new juce::DynamicObject();
        q->setProperty ("x", p.x);
        q->setProperty ("y", p.y);
        q->setProperty ("c", p.c);
        pts.add (var (q));
    }
    o->setProperty ("pts", pts);
    o->setProperty ("loop", e.loop);
    o->setProperty ("trig", e.trigger == Envelope::retrig);
    o->setProperty ("sync", e.sync);
    o->setProperty ("num", e.num);
    o->setProperty ("den", e.den);
    o->setProperty ("dot", e.dot);
    o->setProperty ("hz", e.hz);
    o->setProperty ("grid", e.grid);
    for (auto [k, v] : { std::pair { "a", e.a }, std::pair { "h", e.h }, std::pair { "d", e.d }, std::pair { "s", e.s },
                         std::pair { "r", e.r }, std::pair { "ca", e.ca }, std::pair { "cd", e.cd }, std::pair { "cr", e.cr } })
        o->setProperty (k, v);
    return var (o);
}

Envelope envelopeFromJson (const var& j)
{
    auto e = makeEnvelope (false, "Triangle");
    if (! j.isObject())
        return e;
    e.ahdsr = j["type"].toString() == "ahdsr";
    e.kind = j["type"].toString() == "steps" ? Envelope::steps : j["type"].toString() == "random" ? Envelope::random : Envelope::classic;
    if (auto* st = j["steps"].getArray())
        for (int i = 0; i < juce::jmin (16, st->size()); ++i)
            e.stepValues[(size_t) i] = juce::jlimit (0.0f, 1.0f, (float) (double) (*st)[i]);
    if (j.hasProperty ("numSteps")) e.numSteps = juce::jlimit (1, 16, (int) j["numSteps"]);
    if (j.hasProperty ("randomType")) e.randomType = juce::jlimit (0, 3, (int) j["randomType"]);
    if (auto* arr = j["pts"].getArray(); arr != nullptr && arr->size() >= 2)
    {
        e.points.clear();
        for (auto& p : *arr)
            e.points.push_back ({ juce::jlimit (0.0f, 1.0f, (float) (double) p["x"]), juce::jlimit (0.0f, 1.0f, (float) (double) p["y"]),
                                  juce::jlimit (-1.0f, 1.0f, (float) (double) p["c"]) });
        e.points.front().x = 0.0f;
        e.points.back().x = 1.0f;
    }
    auto getB = [&j] (const char* k, bool def) { return j.hasProperty (k) ? (bool) j[k] : def; };
    auto getF = [&j] (const char* k, float def) { return j.hasProperty (k) ? (float) (double) j[k] : def; };
    auto getI = [&j] (const char* k, int def) { return j.hasProperty (k) ? (int) j[k] : def; };
    e.loop = getB ("loop", true);
    e.trigger = getB ("trig", true) ? Envelope::retrig : Envelope::free;
    e.sync = getB ("sync", true);
    e.num = juce::jlimit (1, 32, getI ("num", 1));
    e.den = juce::jlimit (1, 64, getI ("den", 4));
    e.dot = juce::jlimit (0, 2, getI ("dot", 0));
    e.hz = juce::jlimit (0.01f, 50.0f, getF ("hz", 2.0f));
    e.grid = juce::jlimit (0, 32, getI ("grid", 8));
    e.a = juce::jlimit (0.0f, 10.0f, getF ("a", 0.01f));
    e.h = juce::jlimit (0.0f, 10.0f, getF ("h", 0.0f));
    e.d = juce::jlimit (0.0f, 10.0f, getF ("d", 0.3f));
    e.s = juce::jlimit (0.0f, 1.0f, getF ("s", 0.6f));
    e.r = juce::jlimit (0.001f, 10.0f, getF ("r", 0.4f));
    e.ca = juce::jlimit (-1.0f, 1.0f, getF ("ca", 0.0f));
    e.cd = juce::jlimit (-1.0f, 1.0f, getF ("cd", 0.4f));
    e.cr = juce::jlimit (-1.0f, 1.0f, getF ("cr", 0.4f));
    return e;
}

var routeToJson (const Route& r, Type targetType)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("src", r.source.key());
    o->setProperty ("target", r.target.key (targetType));
    o->setProperty ("amount", std::round (r.amount * 1000.0f) / 1000.0f);
    return var (o);
}
} // namespace emx::fx
