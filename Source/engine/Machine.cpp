// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Machine.h"

namespace emx
{
using namespace juce;

MachineSettings::MachineSettings()
{
    static const char* names[] { "KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "TOM LO", "TOM MID", "TOM HI",
                                 "RIDE", "CRASH", "RIM", "SHAKER", "PERC 1", "PERC 2", "FX 1", "FX 2" };
    for (int i = 0; i < maxPads; ++i)
    {
        pads[(size_t) i].note = jlimit (0, 127, 36 + i);
        pads[(size_t) i].name = i < 16 ? names[i] : "PAD " + String (i + 1);
    }
    // (no choke groups by default: pads stay neutral until you change them)
    // a first beat in variation A
    auto& a = steps[0];
    for (int s : { 0, 8 }) a[0][(size_t) s] = 110;
    for (int s : { 4, 12 }) a[1][(size_t) s] = 105;
    for (int s = 0; s < 16; s += 2) a[3][(size_t) s] = (juce::uint8) (s % 4 == 0 ? 96 : 70);
}

int MachineSettings::padForNote (int note) const noexcept
{
    for (int i = 0; i < padCount(); ++i)
        if (pads[(size_t) i].note == note)
            return i;
    return -1;
}

var MachineSettings::toJson() const
{
    auto* o = new DynamicObject();
    o->setProperty ("grid", grid);
    o->setProperty ("variation", variation);
    o->setProperty ("length", length);
    o->setProperty ("den", den);
    o->setProperty ("swing", swing);
    Array<var> ps;
    for (int i = 0; i < maxPads; ++i)
    {
        const auto& p = pads[(size_t) i];
        auto* q = new DynamicObject();
        q->setProperty ("note", p.note);
        q->setProperty ("level", p.level);
        q->setProperty ("pan", p.pan);
        q->setProperty ("tune", p.tune);
        q->setProperty ("decay", p.decay);
        q->setProperty ("choke", p.choke);
        q->setProperty ("name", p.name);
        ps.add (var (q));
    }
    o->setProperty ("pads", ps);
    // steps: one string per variation and pad, two hex digits per step (compact and readable)
    Array<var> vs;
    for (int v = 0; v < numVariations; ++v)
    {
        Array<var> rows;
        for (int i = 0; i < maxPads; ++i)
        {
            String row;
            bool any = false;
            for (int s = 0; s < numSteps; ++s)
            {
                row << String::toHexString ((int) steps[(size_t) v][(size_t) i][(size_t) s]).paddedLeft ('0', 2);
                any = any || steps[(size_t) v][(size_t) i][(size_t) s] > 0;
            }
            rows.add (any ? var (row) : var());
        }
        vs.add (rows);
    }
    o->setProperty ("steps", vs);
    return var (o);
}

MachineSettings MachineSettings::fromJson (const var& j)
{
    MachineSettings m;
    if (! j.isObject())
        return m;
    m.grid = (int) j.getProperty ("grid", 4) == 8 ? 8 : 4;
    m.variation = jlimit (0, numVariations - 1, (int) j.getProperty ("variation", 0));
    m.length = jlimit (1, numSteps, (int) j.getProperty ("length", 16));
    m.den = jlimit (1, 64, (int) j.getProperty ("den", 16));
    m.swing = jlimit (0.0f, 1.0f, (float) (double) j.getProperty ("swing", 0.0));
    if (auto* ps = j["pads"].getArray())
        for (int i = 0; i < jmin (maxPads, ps->size()); ++i)
        {
            const auto& q = (*ps)[i];
            auto& p = m.pads[(size_t) i];
            p.note = jlimit (0, 127, (int) q.getProperty ("note", p.note));
            p.level = jlimit (0.0f, 1.0f, (float) (double) q.getProperty ("level", 1.0));
            p.pan = jlimit (-1.0f, 1.0f, (float) (double) q.getProperty ("pan", 0.0));
            p.tune = jlimit (-24.0f, 24.0f, (float) (double) q.getProperty ("tune", 0.0));
            p.decay = jlimit (0.0f, 1.0f, (float) (double) q.getProperty ("decay", 1.0));
            p.choke = jlimit (0, 8, (int) q.getProperty ("choke", 0));
            p.name = q.getProperty ("name", p.name).toString();
        }
    if (auto* vs = j["steps"].getArray())
    {
        m.steps = {};
        for (int v = 0; v < jmin (numVariations, vs->size()); ++v)
            if (auto* rows = (*vs)[v].getArray())
                for (int i = 0; i < jmin (maxPads, rows->size()); ++i)
                {
                    const auto row = (*rows)[i].toString();
                    for (int s = 0; s < numSteps && s * 2 + 1 < row.length(); ++s)
                        m.steps[(size_t) v][(size_t) i][(size_t) s] = (juce::uint8) jlimit (0, 127, row.substring (s * 2, s * 2 + 2).getHexValue32());
                }
    }
    return m;
}

void MachineSequencer::process (MidiBuffer& out, int n, double bpm, std::optional<double> hostPpq) noexcept
{
    // note-offs due in this block
    for (int i = 0; i < numSounding;)
    {
        auto& s = sounding[(size_t) i];
        if (s.offIn < n)
        {
            out.addEvent (MidiMessage::noteOff (1, s.note), (int) jlimit ((int64) 0, (int64) n - 1, s.offIn));
            s = sounding[(size_t) --numSounding];
        }
        else
        {
            s.offIn -= n;
            ++i;
        }
    }
    if (! settings.playing)
    {
        if (wasPlaying)
        {
            currentStep.store (-1);
            lastStep = -1;
            internalPpq = 0.0;
        }
        wasPlaying = false;
        return;
    }
    if (! wasPlaying)
        lastStep = -1;
    wasPlaying = true;

    const double stepBeats = 4.0 / jmax (1, settings.den);
    const double beatsPerSample = jmax (1.0, bpm) / 60.0 / sr;
    const double samplesPerStep = stepBeats / beatsPerSample;
    const double swingBeats = settings.swing * 0.5 * stepBeats;
    const double ppq0 = hostPpq.has_value() ? *hostPpq : internalPpq;
    const int len = jlimit (1, MachineSettings::numSteps, settings.length);

    // every step whose start lies in this block
    const int64 first = (int64) std::floor (ppq0 / stepBeats);
    for (int64 k = first; k <= first + 1 + (int64) (n / jmax (1.0, samplesPerStep)); ++k)
    {
        const double start = (double) k * stepBeats + ((k & 1) ? swingBeats : 0.0);
        const double pos = (start - ppq0) / beatsPerSample;
        if (k <= lastStep || pos >= n)
            continue;
        if (pos < -1.0 && ! (lastStep < 0 && k == first))   // already past (but play the first step right away)
            continue;
        lastStep = k;
        const int step = (int) (((k % len) + len) % len);
        currentStep.store (step);
        const int at = jlimit (0, n - 1, (int) std::floor (jmax (0.0, pos)));
        const auto& row = settings.steps[(size_t) jlimit (0, MachineSettings::numVariations - 1, settings.variation)];
        for (int p = 0; p < settings.padCount(); ++p)
            if (const int vel = row[(size_t) p][(size_t) step]; vel > 0)
            {
                const int note = settings.pads[(size_t) p].note;
                out.addEvent (MidiMessage::noteOn (1, note, (juce::uint8) vel), at);
                if (numSounding < (int) sounding.size())
                    sounding[(size_t) numSounding++] = { note, at + (int64) (samplesPerStep * 0.5) - n };
            }
    }
    if (! hostPpq.has_value())
        internalPpq += n * beatsPerSample;
}
} // namespace emx
