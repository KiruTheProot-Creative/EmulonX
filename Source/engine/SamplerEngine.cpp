// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SamplerEngine.h"

namespace emx
{
SamplerEngine::SamplerEngine()
{
    SincTables::get();
    constexpr int combSize = 1024;
    combMemory.assign ((size_t) maxVoiceSlots * 2 * combSize, 0.0f);
    for (size_t i = 0; i < voices.size(); ++i)
    {
        voices[i].emu.comb = combMemory.data() + i * 2 * combSize;
        voices[i].emu.combSize = combSize;
    }
}

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
    else if (m.isControllerOfType (4))
        emuIn.pedal = (float) m.getControllerValue() / 127.0f;
    else if (m.isControllerOfType (65))
        emuIn.footswitch2 = m.getControllerValue() >= 64;
    else if (m.isChannelPressure())
        emuIn.pressure = (float) m.getChannelPressureValue() / 127.0f;
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

    // Machine: a pad in a choke group cuts off the other pads of its group (closed hat stops the open one)
    const auto& pad = padMods[(size_t) juce::jlimit (0, 127, note)];
    if (pad.used && pad.choke > 0)
        for (auto& o : voices)
            if (o.active && o.note != note && padMods[(size_t) o.note].used && padMods[(size_t) o.note].choke == pad.choke)
            {
                o.stolen = true;
                o.released = true;
            }

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
        v.padGain = pad.used ? pad.gain : 1.0f;
        v.padPan = pad.used ? pad.pan : 0.0f;
        v.padSemis = pad.used ? pad.semis : 0.0f;
        v.padFade = 1.0f;
        v.padAge = 0;
        // decay below 100 %: the sound fades out after a while (shorter = sooner and faster)
        v.padFadeAt = pad.used && pad.decay < 0.999f ? (juce::int64) (sampleRate * 0.02 + sampleRate * 3.0 * pad.decay * pad.decay) : -1;
        v.padFadeCoef = (float) std::exp (-4.6 / (sampleRate * (0.03 + 0.6 * pad.decay)));
        v.zoneId = z.id;
        v.sample = z.sample;
        v.params = p;
        v.k = k;
        v.grain = juce::jmax (256, (int) std::lround (p.grainMs / 1000.0 * sampleRate)) & ~1;
        v.grainCounter = 0;
        int startAt = juce::jlimit (0, juce::jmax (0, z.sample->length() - 1), p.sampleStart);
        if (p.randomStart && p.randomEnd > startAt)
        {
            // Random start inside the marked area, moved to the nearest rising zero crossing (no click).
            int r = startAt + random.nextInt (p.randomEnd - startAt + 1);
            const float* d = z.sample->audio.getReadPointer (0);
            const int len = z.sample->length();
            for (int o = 0; o < 256; ++o)
            {
                if (r + o > 0 && r + o < len && d[r + o - 1] < 0.0f && d[r + o] >= 0.0f) { r += o; break; }
                if (r - o > 0 && r - o < len && d[r - o - 1] < 0.0f && d[r - o] >= 0.0f) { r -= o; break; }
            }
            startAt = juce::jlimit (startAt, juce::jmax (startAt, p.randomEnd), r);
        }
        v.pos = startAt;
        v.chorusPos = startAt;
        v.chorusInLoop = false;
        v.read0 = startAt;
        v.read1 = startAt;   // the second grain is fully audible at the start, so the attack is intact
        v.glide = glideFrom;
        v.glideStep = glideStepFor (glideFrom);
        v.level = 0.0f;
        v.stage = 0;
        v.released = v.stolen = v.inLoop = false;
        v.keepSpeed = p.keepSpeed;
        v.age = ++ageCounter;
        if (p.design != nullptr)
        {
            emu::startNote (v.emu, *p.design, random);
            v.level = 1.0f;          // E-mu voices: `level` is only the quick fade when a voice is stolen
            v.emuGain = 0.0f;
        }
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
        if (v.params.design != nullptr)
        {
            // E-mu voices: envelopes, LFOs and cords run every 32 samples
            for (int done = 0; done < numSamples && v.active; done += 32)
                renderEmuVoice (v, v.params, outL + done, outR + done, juce::jmin (32, numSamples - done));
        }
        else
            renderVoice (v, v.params, outL, outR, numSamples);
        if (! v.active)
            v.sample.reset();
    }
}

void SamplerEngine::renderEmuVoice (Voice& v, const ZoneParams& z, float* L, float* R, int N) noexcept
{
    constexpr float chorusSpread = 0.6f;   // measured against Emulator X (how strongly the copies beat on one side)
    const auto& d = *z.design;
    emuIn.modWheel = modWheel;
    emuIn.pitchWheel = bend;
    emuIn.footswitch1 = sustainDown;
    if (v.released && ! v.emu.released)
        emu::releaseNote (v.emu);
    const float g0 = v.emuGain;
    const bool sounding = emu::control (v.emu, d, emuIn, v.note, v.velocity, sampleRate, N);
    const float g1 = v.emu.ampGain;
    v.emuGain = g1;

    float a[32], b[32];
    std::fill (a, a + N, 0.0f);
    std::fill (b, b + N, 0.0f);
    // E-mu's voice chorus plays the sample twice, detuned against each other and spread left / right
    // (measured: 100 % = 50 cents between the copies; Galactic End's 11.8 % beats at 5.9 cents)
    const bool chorus = d.chorusAmount > 0.05f;
    const float pitch = v.emu.pitchSemis, detune = d.chorusAmount / 100.0f * 0.5f;
    if (chorus)
        v.emu.pitchSemis = pitch - detune * 0.5f;
    const float glideBefore = v.glide;
    renderVoice (v, z, a, b, N, true);   // the sample at the modulated pitch (stops at its end)
    if (chorus)
    {
        float c[32], e[32];
        std::fill (c, c + N, 0.0f);
        std::fill (e, e + N, 0.0f);
        const double pos = v.pos;
        const bool inLoop = v.inLoop, active = v.active;
        const float glideAfter = v.glide;
        v.pos = v.chorusPos;
        v.inLoop = v.chorusInLoop;
        v.glide = glideBefore;
        v.emu.pitchSemis = pitch + detune * 0.5f;
        renderVoice (v, z, c, e, N, true);
        v.chorusPos = v.pos;
        v.chorusInLoop = v.inLoop;
        v.pos = pos;
        v.inLoop = inLoop;
        v.active = active;
        v.glide = glideAfter;
        v.emu.pitchSemis = pitch;
        // width spreads the copies apart (measured: at 100 % both copies are still heard on both sides, the
        // far one about 8 dB down); scaled so the level stays the same
        const float w = juce::jlimit (0.0f, 1.0f, d.chorusWidth / 100.0f);
        const float k = 1.0f - chorusSpread * w, norm = 1.0f / std::sqrt (1.0f + k * k);
        const float near = norm, far = k * norm;
        for (int i = 0; i < N; ++i)
        {
            const float l1 = a[i], r1 = b[i], l2 = c[i], r2 = e[i];
            a[i] = near * l1 + far * l2;
            b[i] = far * r1 + near * r2;
        }
    }

    const float vol = juce::Decibels::decibelsToGain (z.gainDb, -100.0f);
    const float panAngle = (juce::jlimit (-100.0f, 100.0f, z.pan + v.emu.pan + v.padPan * 100.0f) / 100.0f + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float gl = vol * std::cos (panAngle) * juce::MathConstants<float>::sqrt2 * 0.5f;   // 0.5 = headroom, as for other zones
    const float gr = vol * std::sin (panAngle) * juce::MathConstants<float>::sqrt2 * 0.5f;
    for (int i = 0; i < N; ++i)
    {
        const float g = g0 + (g1 - g0) * (float) (i + 1) / (float) N;
        float l = a[i], r = b[i];
        emu::filter (v.emu, d, sampleRate, l, r);
        L[i] += l * g * gl;
        R[i] += r * g * gr;
    }
    if (! sounding)
        v.active = false;   // the amp envelope has finished
}

void SamplerEngine::renderVoice (Voice& v, const ZoneParams& z, float* L, float* R, int N, bool rawForEmu) noexcept
{
    const auto& S = *v.sample;
    const int n = S.length();
    const float* c0 = S.audio.getReadPointer (0);
    const bool stereo = S.audio.getNumChannels() > 1;
    const float* c1 = stereo ? S.audio.getReadPointer (1) : c0;
    const auto mode = zoneSet != nullptr ? zoneSet->interp : Interp::sinc;
    const auto sr = sampleRate;

    float semis = (float) z.semitones;
    for (int m = 0; m < numPitchMods; ++m)
        if (pitchMods[(size_t) m].zoneId == v.zoneId)
            semis = pitchMods[(size_t) m].semitones;   // already includes the knob position
    double bendSemis = bend * bendRange, emuSemis = 0.0;
    if (rawForEmu && z.design != nullptr)
    {
        bendSemis = bend * (bend >= 0.0f ? z.design->bendUp : z.design->bendDown);
        emuSemis = v.emu.pitchSemis;
    }
    const double ratio = std::pow (2.0, (v.note + v.glide + semis - z.root + (z.tuneCents + globalTuneCents) / 100.0 + bendSemis + emuSemis + v.padSemis) / 12.0);
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
    const int playEnd = z.sampleEnd > 0 ? juce::jmin (n, z.sampleEnd) : n;   // sample end marker
    const bool looping = z.loopMode != LoopMode::off && ! (z.loopMode == LoopMode::untilRelease && v.released) && loopLen >= 16;
    const auto dir = z.loopDir;
    // Positions run on past the loop end; this folds them back into the loop for each direction.
    // Forward repeats every loop length, backward too (end to start), ping-pong every two lengths.
    const double period = dir == LoopDir::pingPong ? 2.0 * loopLen : (double) loopLen;
    auto fold = [ls, le, loopLen, dir] (double x) noexcept
    {
        if (x < le)
            return x;
        const double o = x - le;
        if (dir == LoopDir::forward)
            return ls + std::fmod (o, (double) loopLen);
        if (dir == LoopDir::backward)
            return le - 1.0 - std::fmod (o, (double) (loopLen - 1));
        const double q = std::fmod (o, 2.0 * loopLen);
        return q < loopLen ? le - 1.0 - q * (loopLen - 1.0) / loopLen : ls + (q - loopLen) * (loopLen - 1.0) / loopLen;
    };
    // A note that stops looping (released in "until release") carries on from where it really is in the sample.
    if (! looping && v.pos >= le && v.pos < le + period && v.inLoop)
    {
        const double real = fold (v.pos);
        v.pos = real;
        v.read0 = fold (v.read0);
        v.read1 = fold (v.read1);
    }
    v.inLoop = looping;
    // The cross-fade smooths the jump from the loop end back to its start, so it only applies going forward.
    const int xf = looping && dir == LoopDir::forward ? juce::jmax (0, juce::jmin (z.crossfade, ls, loopLen / 2)) : 0;
    const float* table = mode == Interp::sinc ? SincTables::get().forStep (step) : nullptr;

    auto read = [&] (const float* ch, double x) noexcept -> float
    {
        if (looping)
        {
            if (x >= le)
                x = fold (x);
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

    const float gain = (rawForEmu ? juce::MathConstants<float>::sqrt2 : juce::Decibels::decibelsToGain (z.gainDb, -100.0f) * (float) v.velocity / 127.0f) * v.padGain;
    const float panAngle = (juce::jlimit (-100.0f, 100.0f, z.pan + v.padPan * 100.0f) / 100.0f + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    float gl = gain * std::cos (panAngle) * juce::MathConstants<float>::sqrt2 * 0.5f;   // 0.5 = headroom, as in the prototype
    float gr = gain * std::sin (panAngle) * juce::MathConstants<float>::sqrt2 * 0.5f;
    if (rawForEmu)
        gl = gr = v.padGain;   // the E-mu layer adds gain and pan (the pad's pan joins it there)

    if (v.released && ! rawForEmu)
        v.stage = 3;
    if (rawForEmu)
        v.stage = v.stolen ? 3 : 4;   // 4: no envelope here; 3 with the steal fade

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
        else if (v.stage == 3)
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
            if (looping && v.pos >= le + period)
                v.pos -= period;
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
                        x = fold (x);
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
            if (looping && v.pos >= le + period)
                v.pos -= period;
        }

        if (! looping && v.pos >= playEnd)
        {
            v.active = false;
            return;
        }

        if (v.padFadeAt >= 0 && ++v.padAge > v.padFadeAt)
        {
            v.padFade *= v.padFadeCoef;
            if (v.padFade < 1.0e-4f) { v.active = false; return; }
        }
        L[i] += a * v.level * gl * v.padFade;
        R[i] += b * v.level * gr * v.padFade;
    }
}
} // namespace emx
