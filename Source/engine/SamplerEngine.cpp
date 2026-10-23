// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SamplerEngine.h"

namespace emx
{
SamplerEngine::SamplerEngine() { SincTables::get(); }

void SamplerEngine::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    reset();
}

void SamplerEngine::reset() noexcept
{
    for (auto& v : voices)
    {
        v.active = false;
        v.sample.reset();   // the release pool still owns the data, so this never frees memory
    }
    keysDown.reset();
    sustainDown = false;
    stackSize = 0;
    monoNote = -1;
}

void SamplerEngine::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isSustainPedalOn())
        setSustain (true);
    else if (m.isSustainPedalOff())
        setSustain (false);
    else if (m.isControllerOfType (1))
        modWheel = (float) m.getControllerValue() / 127.0f;
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        allNotesOff();
}

void SamplerEngine::noteOn (int note, int velocity) noexcept
{
    if (velocity <= 0)
    {
        noteOff (note);
        return;
    }
    note = juce::jlimit (0, 127, note);
    keysDown.set ((size_t) note);
    ++noteOnCount;
    if (! mono)
    {
        startVoicesFor (note, velocity);
        return;
    }
    // Mono: remember the order of held keys, so letting go returns to the previous one.
    int k = 0;
    for (int i = 0; i < stackSize; ++i)
        if (noteStack[i] != note)
            noteStack[k++] = noteStack[i];
    stackSize = k;
    if (stackSize < 128)
        noteStack[stackSize++] = note;
    const int previous = monoNote >= 0 ? monoNote : lastNote;
    monoNote = note;
    monoVelocity = velocity;
    monoNoteOn (note, velocity, previous);
}

void SamplerEngine::noteOff (int note) noexcept
{
    note = juce::jlimit (0, 127, note);
    keysDown.reset ((size_t) note);
    if (mono)
    {
        int k = 0;
        for (int i = 0; i < stackSize; ++i)
            if (noteStack[i] != note)
                noteStack[k++] = noteStack[i];
        stackSize = k;
        if (note == monoNote && ! sustainDown && stackSize > 0)
        {
            const int back = noteStack[stackSize - 1];   // return to the key still held
            const int previous = monoNote;
            monoNote = back;
            monoNoteOn (back, monoVelocity, previous);
            return;
        }
        if (note == monoNote && stackSize == 0)
        {
            lastNote = monoNote;
            monoNote = -1;
        }
    }
    if (sustainDown)
        return;
    for (auto& v : voices)
        if (v.active && v.note == note)
            v.released = true;
}

float SamplerEngine::glideStepFor (float semitones) const noexcept
{
    return glideTime > 0.0f ? std::abs (semitones) / (float) (glideTime * sampleRate) : 0.0f;
}

void SamplerEngine::monoNoteOn (int note, int velocity, int previous) noexcept
{
    // The pitch that is sounding right now (a voice may still be gliding).
    float fromPitch = (float) previous;
    bool overlapping = false;
    for (auto& v : voices)
        if (v.active && ! v.stolen && ! v.released && previous >= 0 && v.note == previous)
        {
            overlapping = true;
            fromPitch = (float) v.note + v.glide;
            break;
        }

    if (legato && overlapping && zoneSet != nullptr)
    {
        // Legato: voices whose zone also plays the new note just change pitch (no restart).
        std::bitset<1024> handled;
        for (auto& v : voices)
        {
            if (! v.active || v.stolen || v.released || v.note != previous)
                continue;
            int index = -1;
            for (size_t zi = 0; zi < zoneSet->zones.size(); ++zi)
                if (zoneSet->zones[zi].id == v.zoneId)
                    index = (int) zi;
            const auto* z = index >= 0 ? &zoneSet->zones[(size_t) index] : nullptr;
            const bool plays = z != nullptr && note >= z->params.loKey && note <= z->params.hiKey
                               && velocity >= z->params.loVel && velocity <= z->params.hiVel;
            if (plays)
            {
                const float current = (float) v.note + v.glide;
                v.note = note;
                v.glide = glideTime > 0.0f ? current - (float) note : 0.0f;
                v.glideStep = glideStepFor (v.glide);
                if (index < 1024)
                    handled.set ((size_t) index);
            }
            else
            {
                v.stolen = v.released = true;   // a different zone takes over
            }
        }
        startVoicesFor (note, velocity, glideTime > 0.0f ? fromPitch - (float) note : 0.0f, &handled);
        return;
    }

    // Retrigger: fade what is sounding and start the new note (gliding unless legato is on).
    for (auto& v : voices)
        if (v.active && ! v.stolen)
            v.stolen = v.released = true;
    const bool glideNow = glideTime > 0.0f && previous >= 0 && ! legato;
    startVoicesFor (note, velocity, glideNow ? fromPitch - (float) note : 0.0f);
}

void SamplerEngine::setSustain (bool down) noexcept
{
    sustainDown = down;
    if (! down)
        for (auto& v : voices)
            if (v.active && ! keysDown.test ((size_t) v.note))
                v.released = true;
}

void SamplerEngine::allNotesOff() noexcept
{
    keysDown.reset();
    sustainDown = false;
    for (auto& v : voices)
        if (v.active)
        {
            v.released = true;
            v.stolen = true;
        }
}

int SamplerEngine::getActiveVoiceCount() const noexcept
{
    int n = 0;
    for (auto& v : voices)
        if (v.active && ! v.stolen)
            ++n;
    return n;
}

int SamplerEngine::getPlayheads (Playhead* out, int maxCount) const noexcept
{
    int n = 0;
    for (auto& v : voices)
        if (v.active && n < maxCount)
            out[n++] = { v.zoneId, v.pos };
    return n;
}

SamplerEngine::Voice* SamplerEngine::freeSlot() noexcept
{
    for (auto& v : voices)
        if (! v.active)
            return &v;
    // Every slot busy (only possible with many fading voices): reuse the oldest.
    Voice* oldest = &voices[0];
    for (auto& v : voices)
        if (v.age < oldest->age)
            oldest = &v;
    return oldest;
}

bool SamplerEngine::stealOne() noexcept
{
    // Released notes go first, then the oldest (or quietest) sounding voice.
    Voice* victim = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.stolen && v.released && (victim == nullptr || v.age < victim->age))
            victim = &v;
    if (victim == nullptr)
        for (auto& v : voices)
            if (v.active && ! v.stolen)
            {
                if (victim == nullptr) { victim = &v; continue; }
                if (stealQuietest ? (v.level * (float) v.velocity < victim->level * (float) victim->velocity)
                                  : (v.age < victim->age))
                    victim = &v;
            }
    if (victim == nullptr)
        return false;
    victim->stolen = true;     // fades out over ~6 ms instead of clicking
    victim->released = true;
    return true;
}

void SamplerEngine::startVoicesFor (int note, int velocity, float glideFrom, const std::bitset<1024>* skipZones) noexcept
{
    if (zoneSet == nullptr)
        return;

    for (size_t zi = 0; zi < zoneSet->zones.size(); ++zi)
    {
        const auto& z = zoneSet->zones[zi];
        const auto& p = z.params;
        if (z.sample == nullptr || note < p.loKey || note > p.hiKey || velocity < p.loVel || velocity > p.hiVel)
            continue;
        if (skipZones != nullptr && zi < 1024 && skipZones->test (zi))
            continue;

        while (getActiveVoiceCount() >= maxVoices && stealOne()) {}

        auto& v = *freeSlot();
        const double k = z.sample->sampleRate / sampleRate;
        v.active = true;
        v.note = note;
        v.velocity = velocity;
        v.zoneId = z.id;
        v.sample = z.sample;
        v.params = p;
        v.k = k;
        v.grain = juce::jmax (256, (int) std::lround (p.grainMs / 1000.0 * sampleRate)) & ~1;
        v.grainCounter = 0;
        v.pos = 0.0;
        v.read0 = 0.0;
        v.read1 = 0.0;   // the second grain is fully audible at the start, so the attack is intact
        v.glide = glideFrom;
        v.glideStep = glideStepFor (glideFrom);
        v.level = 0.0f;
        v.stage = 0;
        v.released = v.stolen = false;
        v.keepSpeed = p.keepSpeed;
        v.age = ++ageCounter;
    }
}

void SamplerEngine::render (float* L, float* R, int numSamples) noexcept
{
    render (L, R, numSamples, nullptr, 0);
}

void SamplerEngine::render (float* L, float* R, int numSamples, const Bus* buses, int numBuses) noexcept
{
    const ZoneSet* set = zoneSet.get();
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        if (set != nullptr)
            if (auto* z = set->find (v.zoneId))
                v.params = z->params;          // live edits apply to held notes
        float* outL = L;
        float* outR = R;
        for (int b = 0; b < numBuses; ++b)
            if (buses[b].zoneId == v.zoneId)
            {
                outL = buses[b].left;
                outR = buses[b].right;
                break;
            }
        renderVoice (v, v.params, outL, outR, numSamples);
        if (! v.active)
            v.sample.reset();
    }
}

void SamplerEngine::renderVoice (Voice& v, const ZoneParams& z, float* L, float* R, int N) noexcept
{
    const auto& S = *v.sample;
    const int n = S.length();
    const float* c0 = S.audio.getReadPointer (0);
    const bool stereo = S.audio.getNumChannels() > 1;
    const float* c1 = stereo ? S.audio.getReadPointer (1) : c0;
    const auto mode = zoneSet != nullptr ? zoneSet->interp : Interp::sinc;
    const auto sr = sampleRate;

    const double ratio = std::pow (2.0, (v.note + v.glide - z.root + (z.tuneCents + globalTuneCents) / 100.0 + bend * bendRange) / 12.0);
    if (std::abs (v.glide) > 0.0f)
    {
        // Glide: slide the remaining distance toward the note at a steady speed.
        const float move = v.glideStep * (float) N;
        v.glide = std::abs (v.glide) <= move ? 0.0f : v.glide - (v.glide > 0.0f ? move : -move);
    }
    const double k = v.k, step = ratio * k;

    // Loop region (re-evaluated every block so edits are heard immediately).
    const int ls = juce::jlimit (0, juce::jmax (0, n - 1), z.loopStart);
    const int le = juce::jmax (ls + 1, juce::jmin (z.loopEnd, n));
    const int loopLen = le - ls;
    const bool looping = z.loopMode != LoopMode::off && ! (z.loopMode == LoopMode::untilRelease && v.released) && loopLen >= 16;
    const int xf = looping ? juce::jmax (0, juce::jmin (z.crossfade, ls, loopLen / 2)) : 0;
    const float* table = mode == Interp::sinc ? SincTables::get().forStep (step) : nullptr;

    auto read = [&] (const float* ch, double x) noexcept -> float
    {
        if (looping)
        {
            if (x >= le)
                x = ls + std::fmod (x - ls, (double) loopLen);
            if (xf > 0 && x >= le - xf)
            {
                // Crossfade: fade the loop end into the audio just before the loop start.
                const auto u = (float) ((x - le + xf) / xf);
                return interpolate (ch, x, n, mode, table) * (1.0f - u) + interpolate (ch, x - loopLen, n, mode, table) * u;
            }
        }
        return interpolate (ch, x, n, mode, table);
    };

    // Envelope coefficients (same curves as the prototype).
    const float attackInc = 1.0f / (float) juce::jmax (1.0, z.attack * sr);
    const float decayCoef = (float) std::exp (-4.6 / juce::jmax (1.0, z.decay * sr));
    const float releaseCoef = v.stolen ? (float) std::exp (-4.6 / (0.006 * sr))
                                       : (float) std::exp (-4.6 / juce::jmax (1.0, z.release * sr));

    const float gain = juce::Decibels::decibelsToGain (z.gainDb, -100.0f) * (float) v.velocity / 127.0f;
    const float panAngle = (juce::jlimit (-100.0f, 100.0f, z.pan) / 100.0f + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float gl = gain * std::cos (panAngle) * juce::MathConstants<float>::sqrt2 * 0.5f;   // 0.5 = headroom, as in the prototype
    const float gr = gain * std::sin (panAngle) * juce::MathConstants<float>::sqrt2 * 0.5f;

    if (v.released)
        v.stage = 3;

    for (int i = 0; i < N; ++i)
    {
        if (v.stage == 0)
        {
            v.level += attackInc;
            if (v.level >= 1.0f) { v.level = 1.0f; v.stage = 1; }
        }
        else if (v.stage == 1)
        {
            v.level = z.sustain + (v.level - z.sustain) * decayCoef;
        }
        else
        {
            v.level *= releaseCoef;
            if (v.level < 1.0e-4f) { v.active = false; return; }
        }

        float a, b;
        if (! v.keepSpeed)
        {
            const double x = v.pos;
            a = read (c0, x);
            b = stereo ? read (c1, x) : a;
            v.pos += step;
            if (looping && v.pos >= le)
                v.pos -= loopLen;
        }
        else
        {
            // Keep-speed: two overlapping grains (Hann windows) read at the new pitch
            // while the play position advances at normal speed.
            const int G = v.grain, h = G >> 1;
            // WSOLA: start each new grain where the waveform lines up with the grain
            // that is already playing, so the phase stays continuous (no pitch drift).
            auto align = [&] (double target, double otherX) noexcept
            {
                if (std::abs (step - k) < 1.0e-9)
                    return target;
                // Cheap linear reads are plenty for comparing waveforms.
                auto quick = [&] (double x) noexcept
                {
                    if (looping && x >= le)
                        x = ls + std::fmod (x - ls, (double) loopLen);
                    return interpolate (c0, x, n, Interp::linear, nullptr);
                };
                constexpr int M = 32;              // compare 32 points, 3 output samples apart
                float ref[M];
                for (int j = 0; j < M; ++j)
                    ref[j] = quick (otherX + 3.0 * j * step);
                const int range = juce::jmax (8, (int) std::lround (juce::jmin (0.012 * S.sampleRate, 0.25 * G * k)));
                auto score = [&] (int d) noexcept
                {
                    float ab = 0.0f, bb = 1.0e-9f;
                    for (int j = 0; j < M; ++j)
                    {
                        const float cand = quick (target + d + 3.0 * j * step);
                        ab += ref[j] * cand;
                        bb += cand * cand;
                    }
                    return ab / std::sqrt (bb);
                };
                int best = 0;
                float bestScore = -1.0e30f;
                for (int d = -range; d <= range; d += 6)   // coarse search...
                    if (const float s = score (d); s > bestScore) { bestScore = s; best = d; }
                const int coarse = best;
                for (int d = coarse - 5; d <= coarse + 5; ++d)   // ...then refine
                    if (const float s = score (d); s > bestScore) { bestScore = s; best = d; }
                return target + best;
            };
            if (v.grainCounter == 0) v.read0 = align (v.pos, v.read1);
            else if (v.grainCounter == h) v.read1 = align (v.pos, v.read0);
            const double p0 = (double) v.grainCounter / G;
            const double x0 = v.read0, x1 = v.read1;
            v.read0 += step;
            v.read1 += step;
            const auto sn = (float) std::sin (juce::MathConstants<double>::pi * p0);
            const float w0 = sn * sn, w1 = 1.0f - w0;
            a = w0 * read (c0, x0) + w1 * read (c0, x1);
            b = stereo ? w0 * read (c1, x0) + w1 * read (c1, x1) : a;
            if (++v.grainCounter >= G)
                v.grainCounter = 0;
            v.pos += k;
            if (looping && v.pos >= le)
                v.pos -= loopLen;
        }

        if (! looping && v.pos >= n)
        {
            v.active = false;
            return;
        }

        L[i] += a * v.level * gl;
        R[i] += b * v.level * gr;
    }
}
} // namespace emx
