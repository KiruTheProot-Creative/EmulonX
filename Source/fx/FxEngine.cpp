// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxEngine.h"

namespace emx::fx
{
std::shared_ptr<Snapshot> Snapshot::build (const Setup& setup, const std::vector<std::pair<int, const Chain*>>& zones)
{
    auto s = std::make_shared<Snapshot>();
    s->envelopes = setup.envelopes;
    s->bpm = setup.bpm;

    auto convert = [&] (const Chain& chain, ChainRT& out)
    {
        for (auto& m : chain)
        {
            if (m.processor == nullptr)
                continue;
            ModuleRT rt;
            rt.id = m.id;
            rt.type = m.type;
            rt.on = m.on;
            rt.settings = m.settings;
            rt.norm = m.norm;
            for (int i = 0; i < maxSlots; ++i)
                rt.spec[(size_t) i] = specFor (m.type, m.settings.mode, i);
            rt.processor = m.processor.get();
            s->keepAlive.push_back (m.processor);
            for (auto& r : setup.routes)
                if (r.target.module == m.id && r.target.slot >= 0 && r.target.slot < maxSlots && rt.spec[(size_t) r.target.slot] != nullptr)
                    rt.routes.push_back ({ r.target.slot, r.source.kind, r.source.index, r.amount });
            out.modules.push_back (std::move (rt));
        }
    };

    for (auto& [zoneId, chain] : zones)
        if (chain != nullptr && ! chain->empty())
        {
            ChainRT c;
            c.zoneId = zoneId;
            convert (*chain, c);
            s->zoneChains.push_back (std::move (c));
        }
    convert (setup.master, s->master);
    for (auto& r : setup.routes)
        if (r.target.module < 0)
            s->volumeRoutes.push_back ({ 0, r.source.kind, r.source.index, r.amount });
    return s;
}

void Engine::reset() noexcept
{
    for (auto& e : env)
        e = {};
    T = 0;
}

float Engine::sourceValue (SrcKind kind, int index) const noexcept
{
    switch (kind)
    {
        case SrcKind::env:   return index >= 0 && index < maxEnvelopes ? envNow[index] : 0.0f;
        case SrcKind::macro: return macros != nullptr && index >= 0 && index < macroCount ? macros[index] : 0.0f;
        case SrcKind::wheel: return wheel;
    }
    return 0.0f;
}

float Engine::modulated (float base, const std::vector<Snapshot::RouteRT>& routes, int slot) const noexcept
{
    float n = base;
    for (auto& r : routes)
        if (r.slot == slot)
            n += r.amount * sourceValue (r.kind, r.index);
    return juce::jlimit (0.0f, 1.0f, n);
}

void Engine::beginBlock (int numSamples, double sampleRate, double bpm, juce::uint32 noteOns, bool gateOpen,
                         const float* macros01, int numMacros, float modWheel) noexcept
{
    sr = sampleRate;
    bpmNow = bpm;
    bpmUsed.store ((float) bpm, std::memory_order_relaxed);
    macros = macros01;
    macroCount = numMacros;
    wheel = modWheel;
    outIndex = 0;

    const int numEnv = snap != nullptr ? juce::jmin (maxEnvelopes, (int) snap->envelopes.size()) : 0;

    // Note-on retriggers every envelope; letting go of all keys releases them (like the prototype).
    if (noteOns != lastNoteOns)
    {
        lastNoteOns = noteOns;
        for (int i = 0; i < numEnv; ++i)
        {
            auto& s = env[i];
            s.t0 = T; s.l0 = s.last; s.gate = true; s.trig = true;
        }
    }
    if (lastGate && ! gateOpen)
        for (int i = 0; i < numEnv; ++i)
            if (env[i].gate)
            {
                env[i].gate = false;
                env[i].tr0 = T;
                env[i].lr = env[i].last;
            }
    lastGate = gateOpen;

    for (int i = 0; i < numEnv; ++i)
    {
        const auto& E = snap->envelopes[(size_t) i];
        auto& s = env[i];
        float v = 0.0f, x = 0.0f;
        if (E.ahdsr)
        {
            if (s.gate)
            {
                const float t = (float) ((double) (T - s.t0) / sr);
                x = t;
                const float A = E.a, H = E.h, D = E.d, S = E.s;
                v = t < A ? s.l0 + (1 - s.l0) * curve (t / juce::jmax (1.0e-6f, A), E.ca)
                  : t < A + H ? 1.0f
                  : t < A + H + D ? 1.0f + (S - 1.0f) * curve ((t - A - H) / juce::jmax (1.0e-6f, D), E.cd)
                  : S;
            }
            else if (s.trig)
            {
                const float t = (float) ((double) (T - s.tr0) / sr);
                x = t;
                v = t < E.r ? s.lr * (1.0f - curve (t / E.r, E.cr)) : 0.0f;
            }
        }
        else
        {
            const double period = juce::jmax (0.001, E.sync ? syncSeconds (E.num, E.den, E.dot, bpmNow) : 1.0 / E.hz);
            double ph = (double) (E.trigger == Envelope::retrig ? T - s.t0 : T) / sr / period;
            ph = E.loop ? ph - std::floor (ph) : juce::jmin (ph, 1.0);
            x = (float) ph;
            v = E.shapeAt (x);
        }
        s.last = v;
        envNow[i] = v;
        envValue[i].store (v, std::memory_order_relaxed);
        envPhase[i].store (x, std::memory_order_relaxed);
        envStage[i].store (s.gate ? 1 : s.trig ? 2 : 0, std::memory_order_relaxed);
    }
    T += numSamples;
}

void Engine::processChain (const Snapshot::ChainRT& chain, float* L, float* R, int n) noexcept
{
    for (auto& m : chain.modules)
    {
        float v[maxSlots] {};
        float shown[maxSlots] {};
        for (int i = 0; i < maxSlots; ++i)
            if (const auto* sp = m.spec[(size_t) i])
            {
                const float norm = m.routes.empty() ? m.norm[(size_t) i] : modulated (m.norm[(size_t) i], m.routes, i);
                shown[i] = norm;
                v[i] = sp->fromNorm (norm);
            }
        if (m.on && m.processor != nullptr)
            m.processor->process (v, m.settings, L, R, n, sr, bpmNow);

        if (outIndex < maxModOut)
        {
            auto& o = modOut[outIndex++];
            o.id.store (m.id, std::memory_order_relaxed);
            for (int i = 0; i < maxSlots; ++i)
                o.value[i].store (shown[i], std::memory_order_relaxed);
            if (m.type == Type::comp && m.on && m.processor != nullptr)
                o.gainReduction.store (juce::jmin (o.gainReduction.load (std::memory_order_relaxed), m.processor->gainReductionDb),
                                       std::memory_order_relaxed);
            modOutCount.store (outIndex, std::memory_order_relaxed);
        }
    }
}

float Engine::modulateVolume (float norm01) noexcept
{
    if (snap == nullptr || snap->volumeRoutes.empty())
    {
        volumeOut.store (-1.0f, std::memory_order_relaxed);
        return norm01;
    }
    const float n = modulated (norm01, snap->volumeRoutes, 0);
    volumeOut.store (n, std::memory_order_relaxed);
    return n;
}
} // namespace emx::fx
