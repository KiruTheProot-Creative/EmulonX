// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Arpeggiator.h"

namespace emx
{
using namespace juce;

const char* ArpSettings::modeName (int m)
{
    static const char* names[] { "Up", "Down", "Up / Down", "Down / Up", "As Played", "Random", "Chord" };
    return names[jlimit (0, (int) numModes - 1, m)];
}

ArpSettings::ArpSettings()
{
    for (size_t i = 0; i < steps.size(); ++i)
        steps[i].velocity = i % 4 == 0 ? 1.0f : 0.75f;
}

double ArpSettings::stepBeats() const noexcept
{
    static const double mult[] { 1.0, 1.5, 2.0 / 3.0 };
    return (double) num / (double) jmax (1, den) * 4.0 * mult[jlimit (0, 2, dot)];
}

bool ArpSettings::operator== (const ArpSettings& o) const noexcept
{
    if (enabled != o.enabled || mode != o.mode || num != o.num || den != o.den || dot != o.dot || octaves != o.octaves
        || std::abs (gate - o.gate) > 1.0e-6f || std::abs (swing - o.swing) > 1.0e-6f || latch != o.latch
        || playedVelocity != o.playedVelocity || length != o.length)
        return false;
    for (size_t i = 0; i < steps.size(); ++i)
        if (steps[i].on != o.steps[i].on || std::abs (steps[i].velocity - o.steps[i].velocity) > 1.0e-6f || steps[i].octave != o.steps[i].octave)
            return false;
    return true;
}

var ArpSettings::toJson() const
{
    auto* o = new DynamicObject();
    o->setProperty ("enabled", enabled);
    o->setProperty ("mode", mode);
    o->setProperty ("num", num);
    o->setProperty ("den", den);
    o->setProperty ("dot", dot);
    o->setProperty ("octaves", octaves);
    o->setProperty ("gate", gate);
    o->setProperty ("swing", swing);
    o->setProperty ("latch", latch);
    o->setProperty ("playedVelocity", playedVelocity);
    o->setProperty ("length", length);
    Array<var> st;
    for (auto& s : steps)
    {
        auto* q = new DynamicObject();
        q->setProperty ("on", s.on);
        q->setProperty ("velocity", std::round (s.velocity * 1000.0f) / 1000.0f);
        q->setProperty ("octave", s.octave);
        st.add (var (q));
    }
    o->setProperty ("steps", st);
    return var (o);
}

ArpSettings ArpSettings::fromJson (const var& j)
{
    ArpSettings a;
    if (! j.isObject())
        return a;
    auto getI = [&j] (const char* k, int d) { return j.hasProperty (k) ? (int) j[k] : d; };
    auto getF = [&j] (const char* k, float d) { return j.hasProperty (k) ? (float) (double) j[k] : d; };
    auto getB = [&j] (const char* k, bool d) { return j.hasProperty (k) ? (bool) j[k] : d; };
    a.enabled = getB ("enabled", false);
    a.mode = jlimit (0, (int) numModes - 1, getI ("mode", 0));
    a.num = jlimit (1, 32, getI ("num", 1));
    a.den = jlimit (1, 64, getI ("den", 16));
    a.dot = jlimit (0, 2, getI ("dot", 0));
    a.octaves = jlimit (1, 4, getI ("octaves", 1));
    a.gate = jlimit (0.05f, 1.0f, getF ("gate", 0.75f));
    a.swing = jlimit (0.0f, 1.0f, getF ("swing", 0.0f));
    a.latch = getB ("latch", false);
    a.playedVelocity = getB ("playedVelocity", false);
    a.length = jlimit (1, 16, getI ("length", 8));
    if (auto* arr = j["steps"].getArray())
        for (int i = 0; i < jmin (16, arr->size()); ++i)
        {
            const auto& s = arr->getReference (i);
            a.steps[(size_t) i].on = s.hasProperty ("on") ? (bool) s["on"] : true;
            a.steps[(size_t) i].velocity = jlimit (0.0f, 1.0f, s.hasProperty ("velocity") ? (float) (double) s["velocity"] : 0.8f);
            a.steps[(size_t) i].octave = jlimit (-1, 2, (int) s["octave"]);
        }
    return a;
}

//==================================================================================
Arpeggiator::Arpeggiator() = default;

void Arpeggiator::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    reset();
}

void Arpeggiator::reset() noexcept
{
    numHeld = 0;
    numSounding = 0;
    physical.reset();
    seqIndex = 0;
    lastStep = -1;
    internalPpq = 0.0;
    currentStep.store (-1);
    publishHeld();
}

void Arpeggiator::setSettings (const ArpSettings& s) noexcept
{
    if (! s.latch && settings.latch)
    {
        // Latch switched off: forget the notes that are no longer held down.
        int k = 0;
        for (int i = 0; i < numHeld; ++i)
            if (physical.test ((size_t) held[(size_t) i].note))
                held[(size_t) k++] = held[(size_t) i];
        numHeld = k;
        publishHeld();
    }
    settings = s;
}

void Arpeggiator::publishHeld() noexcept
{
    uint64 m[2] {};
    for (int i = 0; i < numHeld; ++i)
    {
        const int n = held[(size_t) i].note;
        m[n >> 6] |= (uint64) 1 << (n & 63);
    }
    heldMask[0].store (m[0]);
    heldMask[1].store (m[1]);
}

void Arpeggiator::stopAll (int pos, MidiBuffer& out) noexcept
{
    for (int i = 0; i < numSounding; ++i)
        out.addEvent (MidiMessage::noteOff (1, sounding[(size_t) i].note), pos);
    numSounding = 0;
}

void Arpeggiator::noteInput (int note, int velocity, bool on, int pos, MidiBuffer& out) noexcept
{
    if (on)
    {
        // With latch, a new chord (after all keys were up) replaces the old one.
        if (settings.latch && physical.none())
            numHeld = 0;
        if (numHeld == 0 && numSounding == 0)
        {
            startNow = true;   // start the pattern right away (free-running clock)
            seqIndex = 0;
        }
        physical.set ((size_t) note);
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i].note == note)
                return;
        if (numHeld < (int) held.size())
            held[(size_t) numHeld++] = { note, velocity };
    }
    else
    {
        physical.reset ((size_t) note);
        if (settings.latch)
            return;
        int k = 0;
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i].note != note)
                held[(size_t) k++] = held[(size_t) i];
        numHeld = k;
        if (numHeld == 0)
        {
            stopAll (pos, out);
            seqIndex = 0;
        }
    }
    publishHeld();
}

void Arpeggiator::trigger (int64 stepIndex, int pos, int numSamples, double samplesPerStep, MidiBuffer& out) noexcept
{
    const auto& s = settings;
    const int len = jlimit (1, 16, s.length);
    const int stepNo = (int) (((stepIndex % len) + len) % len);
    currentStep.store (stepNo);
    if (numHeld == 0)
        return;
    const auto& step = s.steps[(size_t) stepNo];
    if (! step.on)
        return;

    // The notes to cycle through: held notes (sorted or as played), repeated per octave.
    int base[32];
    int vel[32];
    const int nh = numHeld;
    for (int i = 0; i < nh; ++i) { base[i] = held[(size_t) i].note; vel[i] = held[(size_t) i].velocity; }
    if (s.mode != ArpSettings::played)
        for (int i = 1; i < nh; ++i)   // insertion sort (tiny)
            for (int j = i; j > 0 && base[j - 1] > base[j]; --j)
            {
                std::swap (base[j - 1], base[j]);
                std::swap (vel[j - 1], vel[j]);
            }
    const int oct = jlimit (1, 4, s.octaves);
    const int total = nh * oct;
    auto noteAt = [&] (int i) { return base[i % nh] + 12 * (i / nh); };

    int chosen[32];
    int numChosen = 0, velocity = 100;
    if (s.mode == ArpSettings::chord)
    {
        const int o = seqIndex % oct;
        for (int i = 0; i < nh; ++i)
            chosen[numChosen++] = base[i] + 12 * o;
        velocity = vel[0];
    }
    else
    {
        int idx = 0;
        switch (s.mode)
        {
            case ArpSettings::up:
            case ArpSettings::played: idx = seqIndex % total; break;
            case ArpSettings::down:   idx = total - 1 - seqIndex % total; break;
            case ArpSettings::upDown:
            case ArpSettings::downUp:
            {
                const int cycle = jmax (1, 2 * total - 2);
                int p = total > 1 ? seqIndex % cycle : 0;
                if (s.mode == ArpSettings::downUp)
                    p = total > 1 ? (p + total - 1) % cycle : 0;
                idx = p < total ? p : cycle - p;
                break;
            }
            case ArpSettings::random: idx = rng.nextInt (total); break;
            default: break;
        }
        chosen[numChosen++] = noteAt (idx);
        velocity = vel[idx % nh];
    }
    ++seqIndex;

    if (! s.playedVelocity)
        velocity = jlimit (1, 127, roundToInt (step.velocity * 127.0f));
    if (step.velocity <= 0.001f && ! s.playedVelocity)
        return;   // a zero-velocity step is a rest

    const auto gateSamples = (int64) jmax (1.0, s.gate * samplesPerStep - 1.0);
    for (int c = 0; c < numChosen; ++c)
    {
        const int note = jlimit (0, 127, chosen[c] + 12 * step.octave);
        // Same note still sounding? Switch it off first.
        for (int i = 0; i < numSounding; ++i)
            if (sounding[(size_t) i].note == note)
            {
                out.addEvent (MidiMessage::noteOff (1, note), pos);
                sounding[(size_t) i] = sounding[(size_t) --numSounding];
                break;
            }
        out.addEvent (MidiMessage::noteOn (1, note, (uint8) velocity), pos);
        if (pos + gateSamples < numSamples)
            out.addEvent (MidiMessage::noteOff (1, note), (int) (pos + gateSamples));   // ends inside this block
        else if (numSounding < (int) sounding.size())
            sounding[(size_t) numSounding++] = { note, gateSamples + pos - numSamples };   // relative to the end of this block
    }
}

void Arpeggiator::process (const MidiBuffer& in, MidiBuffer& out, int n, double bpm, std::optional<double> hostPpq) noexcept
{
    out.clear();
    const bool enabled = settings.enabled;

    // Switched off: let notes through again, and finish what the arp was playing.
    if (! enabled)
    {
        if (wasEnabled)
        {
            stopAll (0, out);
            numHeld = 0;
            physical.reset();
            currentStep.store (-1);
            publishHeld();
        }
        wasEnabled = false;
        for (const auto meta : in)
            out.addEvent (meta.getMessage(), meta.samplePosition);
        return;
    }
    wasEnabled = true;

    // Notes that end inside this block (they were scheduled relative to the previous block end).
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

    const double stepBeats = jmax (1.0e-3, settings.stepBeats());
    const double beatsPerSample = jmax (1.0, bpm) / 60.0 / sampleRate;
    const double samplesPerStep = stepBeats / beatsPerSample;
    const double swingBeats = settings.swing * 0.5 * stepBeats;

    auto stepStart = [&] (int64 k) { return (double) k * stepBeats + ((k & 1) ? swingBeats : 0.0); };

    // Walk through the block: input events and step boundaries in time order.
    auto it = in.begin();
    int pos = 0;
    while (pos < n)
    {
        const int nextEvent = it != in.end() ? jlimit (0, n - 1, (*it).samplePosition) : n;

        // Next step boundary at or after `pos`.
        const double ppqHere = hostPpq.has_value() ? *hostPpq + pos * beatsPerSample : internalPpq + pos * beatsPerSample;
        int64 k = (int64) std::floor (ppqHere / stepBeats);
        if (hostPpq.has_value() && (k < lastStep - 1 || k > lastStep + 100000))
            lastStep = k - 1;   // the host jumped (loop, relocate): follow it
        if (stepStart (k) < ppqHere - 1.0e-9)
            ++k;
        if (k <= lastStep)
            k = lastStep + 1;
        const double tk = stepStart (k);
        const int stepPos = (int) std::ceil ((tk - (ppqHere - pos * beatsPerSample)) / beatsPerSample - 1.0e-6);

        if (nextEvent <= stepPos && nextEvent < n)
        {
            const auto meta = *it;
            const auto m = meta.getMessage();
            if (m.isNoteOn())
                noteInput (m.getNoteNumber(), m.getVelocity(), true, nextEvent, out);
            else if (m.isNoteOff())
                noteInput (m.getNoteNumber(), 0, false, nextEvent, out);
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                stopAll (nextEvent, out);
                numHeld = 0;
                physical.reset();
                publishHeld();
                out.addEvent (m, nextEvent);
            }
            else
                out.addEvent (m, nextEvent);   // bend, mod wheel, pedal... pass through
            ++it;

            // First key without the host playing: start the pattern exactly here
            // (once every key of a chord pressed at this same moment has arrived).
            const bool moreNow = it != in.end() && jlimit (0, n - 1, (*it).samplePosition) == nextEvent;
            if (startNow && ! hostPpq.has_value() && moreNow)
            {
                pos = nextEvent;
                continue;
            }
            if (startNow && ! hostPpq.has_value())
            {
                startNow = false;
                internalPpq = -nextEvent * beatsPerSample;   // step 0 lands on this sample
                lastStep = -1;
                trigger (0, nextEvent, n, samplesPerStep, out);
                lastStep = 0;
            }
            startNow = false;
            pos = nextEvent;
            continue;
        }
        if (stepPos >= n)
            break;
        trigger (k, jmax (pos, stepPos), n, samplesPerStep, out);
        lastStep = k;
        pos = jmax (pos, stepPos);
        if (stepPos <= pos)
            ++pos;   // never trigger twice at one sample
    }
    // Remaining input events (none should be left, but be safe).
    for (; it != in.end(); ++it)
        out.addEvent ((*it).getMessage(), jlimit (0, n - 1, (*it).samplePosition));

    if (! hostPpq.has_value())
        internalPpq += n * beatsPerSample;
    else
        internalPpq = *hostPpq + n * beatsPerSample;
}
} // namespace emx
