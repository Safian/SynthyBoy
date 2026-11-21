// SynthyBoy - own engine: the voice.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "voice.h"
#include "filter_setup.h"
#include "osc_options.h"
#include "../env_lfo/lfo_eval.h"
#include "../dsp/simd.h"
#include "../dsp/peak.h"
#include "../dsp/section_timing.h"

#include <cstring>

namespace synthyboy
{
namespace own
{
namespace
{

float noiseSample (int colour, float* s, Rng& r, float level, bool fast) noexcept
{
    // approximately Gaussian white noise (sum of six uniform numbers), level matched to the reference engine by measurement
    float w;
    if (fast)
    {
        // tier low: the six uniform numbers come from three draws of the generator (a 16 bit uniform number from each half of a draw): the same sum, half the generator work
        const uint32_t x0 = r.next(), x1 = r.next(), x2 = r.next();
        const float u = static_cast<float> (x0 >> 16) + static_cast<float> (x0 & 0xffffu) + static_cast<float> (x1 >> 16) + static_cast<float> (x1 & 0xffffu)
                      + static_cast<float> (x2 >> 16) + static_cast<float> (x2 & 0xffffu);
        w = (u * (1.0f / 65536.0f) - 3.0f) * 0.78f;
    }
    else
        w = (r.uni() + r.uni() + r.uni() + r.uni() + r.uni() + r.uni() - 3.0f) * 0.78f;
    // (the factor 0.78: in band density of the reference, its noise is band limited at the top: bands 20 .. 20 kHz measured +0.7 dB at equal total power)
    switch (colour)
    {
        case 1: // pink (three pole filter of white noise)
        {
            s[0] = 0.99765f * s[0] + w * 0.0990460f;
            s[1] = 0.96300f * s[1] + w * 0.2965164f;
            s[2] = 0.57000f * s[2] + w * 1.0526913f;
            return (s[0] + s[1] + s[2] + w * 0.1848f) * 0.25f * 0.82f;
        }
        case 2: // brown
            s[0] = (s[0] + 0.02f * w) / 1.02f;
            return s[0] * 3.5f * 1.12f;
        case 3: // Geiger counter: sparse clicks
            return r.uni() < 0.0008f ? r.bi() * 6.3f : 0.0f;
        case 4: // tape hiss: high passed white noise
        {
            const float y = (w - s[0]) * 0.5f;
            s[0] = w;
            return y * 0.8f;
        }
        case 5: // vinyl: crackle with a decaying click
        {
            if (r.uni() < 0.0004f)
                s[0] = r.bi();
            s[0] *= 0.96f;
            return (s[0] + (r.uni() < 0.02f ? w * 0.05f : 0.0f)) * 1.93f;
        }
        case 6: // digital hash: sample and hold at a sixth of the rate
        {
            if (--s[1] <= 0.0f)
            {
                s[0] = w;
                s[1] = 6.0f;
            }
            return s[0];
        }
        case 7: // velvet noise
            return r.uni() < 0.015f ? (r.next() & 1u ? 1.76f : -1.76f) : 0.0f;
        case 8: // atmosphere: low passed noise
        {
            s[0] += 0.02f * (w - s[0]);
            s[1] += 0.02f * (s[0] - s[1]);
            return s[1] * 6.0f;
        }
        default:
            return w;
    }
    (void) level;
}
/** The groups of keys whose derived settings the voice keeps (bit k of Voice::touchedGroups () = group k): the envelopes, the LFOs, the random LFOs, the oscillators, the sub, the
    noise and the filters; 0 = every other key. */
enum ParamGroup : int { kGroupOther = 0, kGroupEnv = 1, kGroupLfo = 7, kGroupRlfo = 17, kGroupOsc = 21, kGroupSub = 24, kGroupNoise = 25, kGroupFilter = 26, kNumGroups = 28 };
static_assert (kNumGroups <= 32, "the groups are bits of a 32 bit mask");

/** The group of every parameter index, built once (Voice::prepare: never on the audio thread). The index structs of param_index.h hold only `int` fields. */
const uint8_t* paramGroups() noexcept
{
    static const std::vector<uint8_t> table = []
    {
        std::vector<uint8_t> t (paramCount(), static_cast<uint8_t> (kGroupOther));
        auto mark = [&t] (const void* idxStruct, size_t bytes, int group)
        {
            const int* f = static_cast<const int*> (idxStruct);
            for (size_t i = 0; i < bytes / sizeof (int); ++i)
                if (f[i] >= 0 && static_cast<size_t> (f[i]) < t.size())
                    t[static_cast<size_t> (f[i])] = static_cast<uint8_t> (group);
        };
        static_assert (sizeof (EnvIdx) % sizeof (int) == 0 && sizeof (LfoIdx) % sizeof (int) == 0 && sizeof (RlfoIdx) % sizeof (int) == 0 && sizeof (OscIdx) % sizeof (int) == 0
                       && sizeof (SubIdx) % sizeof (int) == 0 && sizeof (NoiseIdx) % sizeof (int) == 0 && sizeof (FilterIdx) % sizeof (int) == 0, "int fields only");
        const ParamIndex& pi = paramIndex();
        for (int e = 0; e < 6; ++e)
            mark (&pi.env[e], sizeof (EnvIdx), kGroupEnv + e);
        for (int l = 0; l < 10; ++l)
            mark (&pi.lfo[l], sizeof (LfoIdx), kGroupLfo + l);
        for (int r = 0; r < 4; ++r)
            mark (&pi.rlfo[r], sizeof (RlfoIdx), kGroupRlfo + r);
        for (int o = 0; o < 3; ++o)
        {
            mark (&pi.osc[o], sizeof (OscIdx), kGroupOsc + o);
            mark (libKeys (o).idx, sizeof (LibKeys::idx), kGroupOsc + o);
        }
        mark (&pi.sub, sizeof (SubIdx), kGroupSub);
        mark (&pi.noise, sizeof (NoiseIdx), kGroupNoise);
        const int noiseFilter = findParamIndex (hashKey ("noise.filter"));
        mark (&noiseFilter, sizeof (int), kGroupNoise);
        for (int f = 0; f < 2; ++f)
            mark (&pi.filter[f], sizeof (FilterIdx), kGroupFilter + f);
        return t;
    }();
    return table.data();
}
} // namespace

/** Bit k: some slot of the last evaluation touched a key of group k (the list of touched keys, and the pad pressure layer on the Filter 1 cutoff). A full list (it cannot be: one entry
    per slot) counts as every group. */
uint32_t Voice::touchedGroups() const noexcept
{
    if (numTouched >= kNumModSlots)
        return ~0u;
    uint32_t m = padCutTouched ? (1u << kGroupFilter) : 0u;
    for (int i = 0; i < numTouched; ++i)
        m |= 1u << groupOf[touched[i]];
    return m;
}

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    groupOf = paramGroups();
    pidx = &paramIndex();
    for (int o = 0; o < 3; ++o)
        libk[o] = &libKeys (o);
    laws = &destLawOf (0);
    infos = paramTable();
    noiseFilterKey = findParamIndex (hashKey ("noise.filter"));
    mod.assign (paramCount(), 0.0f);
    modR.assign (paramCount(), 0.0f);
    modP = mod.data();
    modRP = modR.data();
    filt[0].prepare (sampleRate);
    filt[1].prepare (sampleRate);
    for (int o = 0; o < 3; ++o)
    {
        osc[o].prepareMorph();
        lib[o].prepare (sampleRate);
    }
    noiseSv.prepare (sampleRate, 64);
}

void Voice::reset() noexcept
{
    setActive (false);
    released = false;
    for (auto& e : env)
        e.kill();
    filt[0].reset();
    filt[1].reset();
    clearMod();
    outL = outR = 0.0f;
    for (int tg = 0; tg < 4; ++tg)
    {
        lastBusL[tg] = 0.0f;
        lastBusR[tg] = 0.0f;
        declickL[tg] = 0.0f;
        declickR[tg] = 0.0f;
    }
    declickPos = 0;
    killFading = false;
    killElapsed = 0.0f;
    lastLevel = 0.0f;
}

void Voice::clearMod() noexcept
{
    for (int i = 0; i < numTouched; ++i)
    {
        mod[static_cast<size_t> (touched[i])] = 0.0f;
        modR[static_cast<size_t> (touched[i])] = 0.0f;
    }
    numTouched = 0;
    if (padCutTouched) // the pad pressure layer is kept out of `touched`: the envelope cache reads that list as "the patch is modulated"
    {
        mod[static_cast<size_t> (padCutIdx)] = 0.0f;
        modR[static_cast<size_t> (padCutIdx)] = 0.0f;
        padCutTouched = false;
    }
}

float Voice::modulatedSlow (int idx, float b, float d) const noexcept
{
    return modulatedValue (infos[idx], laws[idx], b, d, (gl->economy & kEconomyMath) != 0);
}

/** A source through its curve; with `glide` (curve.velocity.legato / curve.note.legato) a change of the target after a legato note-on moves to it with the one pole time constant
    master.portamento.time (coefficient 1 - exp (-dt / time), document 04 section 7); a new note starts at the target. */
float Voice::curvedSource (float x, const CompiledModCurve* curve, float& state, bool glide, float dt, float portSeconds) noexcept
{
    float target = x;
    if (curve != nullptr && curve->present)
        target = std::clamp (shapes::ShapeData::evaluateTable (curve->table.data(), curve->table.size(), clamp01 (x)), 0.0f, 1.0f);
    if (!glide || curveFresh || portSeconds <= 1.0e-4f)
        state = target;
    else
        state += (target - state) * (1.0f - std::exp (-dt / portSeconds));
    return state;
}

void Voice::start (const NoteStart& n, float lastOutL, float lastOutR) noexcept
{
    setActive (true);
    released = false;
    killFading = false;
    killElapsed = 0.0f;
    keyNum = n.key;
    played = n.playedKey >= 0 ? n.playedKey : n.key;
    tunedKey = tuneTrack = n.tunedNote;
    chan = n.channel;
    id = n.noteId;
    noteSerial = n.serial;
    vel = n.velocity;
    curveFresh = true;
    releaseVel = 0.0f;
    tuneExpr = 0.0f;
    polyPressure = 0.0f;
    smoothedPressure = 0.0f;
    slide = 0.0f;
    exprX = 0.0f;
    rng.seed (n.serial * 31u + static_cast<uint32_t> (n.key) * 7u + 1u);
    noiseRng.seed (n.serial * 17u + 5u);
    sequence = n.sequence;
    {
        Rng vr;
        for (int i = 0; i < 4; ++i)
        {
            vr.seed (n.sequence * 2654435761u + static_cast<uint32_t> (i) * 40503u + 17u);
            vcRand[i] = vr.bi();
        }
    }
    randomPerNote = rng.uni();
    rand2 = rng.uni();
    randDiscrete = static_cast<float> (rng.next() % 8u) / 7.0f;
    altParity = static_cast<int> (n.serial & 1u);
    glideOffset = n.glide ? n.glideFromSemis - n.tunedNote : 0.0f;
    glideTime = -1.0f; // set at the first tick from the portamento parameters
    glideElapsed = 0.0f;
    lastSemis = n.tunedNote;
    for (int i = 0; i < 4; ++i)
    {
        rlfo[i].seed (n.serial * 101u + static_cast<uint32_t> (i) * 13u + 3u);
        rlfoR[i].seed (n.serial * 211u + static_cast<uint32_t> (i) * 29u + 7u);
        rlfoOut[i] = 0.0f;
    }
    for (int l = 0; l < 10; ++l)
    {
        lfoCore[l].restart();
        lfoAge[l] = 0.0f;
        lfoReadPos[l] = 0.0f;
        lfoSmoothL[l] = lfoSmoothR[l] = 0.0f;
    }
    for (auto& g : noiseState)
        for (auto& x : g)
            x = 0.0f;
    for (auto& g : noiseFilt)
        for (auto& x : g)
            x = 0.0f;
    for (auto& g : noiseLp)
        for (auto& x : g)
            x = 0.0f;
    for (auto& e : env)
        e.noteOn (0.0f);
    clearMod();
    for (auto& st : slotState)
        st = SlotState();
    voiceAge = 0.0f;
    tickCount = 0;
    envCacheValid = false;
    vcCacheValid = false;
    for (bool& ok : envSetOk)
        ok = false;
    for (auto& ls : lfoSet)
        ls.ok = false;
    for (auto& rs : rlfoSet)
        rs.ok = false;
    for (auto& op : oscPart)
        op.ok = false;
    subPart.ok = false;
    ampPrev = ampCur = 0.0f;
    velGainPrev = velGain = 0.0f;
    std::fill (std::begin (srcLevel), std::end (srcLevel), 0.0f);
    declickL[0] = lastOutL;
    declickR[0] = lastOutR;
    for (int tg = 1; tg < 4; ++tg)
    {
        declickL[tg] = lastBusL[tg];
        declickR[tg] = lastBusR[tg];
    }
    bool anyDeclick = false;
    for (int tg = 0; tg < 4; ++tg)
        if (declickL[tg] != 0.0f || declickR[tg] != 0.0f)
            anyDeclick = true;
    declickPos = anyDeclick ? 64 : 0;
    for (int o = 0; o < 3; ++o)
        std::fill (std::begin (oscMono[o]), std::end (oscMono[o]), 0.0f);
    for (int j = 0; j < 4; ++j)
        std::fill (std::begin (extMono[j]), std::end (extMono[j]), 0.0f);
    filt[0].reset();
    filt[1].reset();
    for (auto& o : osc)
        o.clearState();
    for (auto& l : lib)
        l.resetForNote();
    for (int i = 0; i < 4; ++i)
        pitchTapOn[i] = false;
    sub.clearState();
    fresh = true;
}

void Voice::legatoTo (const NoteStart& n) noexcept
{
    sequence = n.sequence;
    vcCacheValid = false;
    // the step of the voice control (vc.envFactor) follows `sequence`, which the all envelope cache key (epoch, tempo, nothing touched) does not hold: the next tick must
    // derive the settings again (the per envelope cache is keyed on the factor itself). The same for the code of the reference switch, which uses the same test.
    envCacheValid = false;
    glideOffset = (lastSemis - n.tunedNote);
    glideElapsed = 0.0f;
    glideTime = -1.0f;
    keyNum = n.key;
    played = n.playedKey >= 0 ? n.playedKey : n.key;
    tunedKey = tuneTrack = n.tunedNote;
    // envelopes marked "legato inverted" listen to every note: they restart from their current value
    for (int i = 0; i < 6; ++i)
        if (legatoInv[i])
            env[i].noteOn (env[i].current());
    chan = n.channel;
    id = n.noteId;
    noteSerial = n.serial;
    vel = n.velocity;
    released = false;
}

void Voice::release (float velocity) noexcept
{
    if (!active || released)
        return;
    released = true;
    releaseVel = velocity;
    polyPressure = 0.0f;
    for (auto& e : env)
        e.noteOff();
    if (gl != nullptr)
        for (int o = 0; o < 3; ++o)
            lib[o].noteOff (P (libk[o]->idx[lk_sLoopExit]) >= 0.5f);
}

void Voice::kill() noexcept
{
    setActive (false);
    released = true;
    for (auto& e : env)
        e.kill();
    clearMod();
    outL = outR = 0.0f;
    lastLevel = 0.0f;
}

float Voice::customShape (const CompiledShape& cs, float u) noexcept
{
    return evaluateCustomShape (cs, src, scratchShape, u);
}

float Voice::lfoValue (int l, const TickGlobals& g, float& valueRight, int steps) noexcept
{
    // V3: the code of lfoValueReference () with the settings of the LFO kept while their inputs are the same (the same expressions of the same values)
    LfoSettings& st = lfoSet[l];
    uint32_t factorBits, concertABits;
    std::memcpy (&factorBits, &vc.lfoFactor, 4);
    std::memcpy (&concertABits, &g.concertA, 4);
    if (!(st.ok && g.baseEpoch != 0 && st.epoch == g.baseEpoch && (lastTouchedGroups & (1u << (kGroupLfo + l))) == 0 && st.bpm == g.bpm && st.factorBits == factorBits
          && st.concertABits == concertABits && st.sampleRate == g.sampleRate && st.content == g.lfoContent && st.monoCore == g.monoCore))
    {
        const LfoIdx& li = pidx->lfo[l];
        st.shape = static_cast<int> (P (li.shape) + 0.5f);
        st.sync = static_cast<int> (P (li.sync) + 0.5f);
        st.syncType = std::clamp (static_cast<int> (P (li.syncType) + 0.5f), 0, 5);
        st.rateKnob = P (li.rate);
        st.division = static_cast<int> (P (li.division) + 0.5f);
        st.keyTranspose = P (li.keyTranspose);
        st.keyTune = P (li.keyTune);
        st.rate = vc.lfoFactor * lfoRateHz (st.sync, st.rateKnob, st.division, g.bpm, g.concertA, lastSemis, st.keyTranspose, st.keyTune);
        st.knob = P (li.phase) * (1.0f / 360.0f);
        st.delay = P (li.delay) * 0.001f;
        st.fade = P (li.fadeIn) * 0.001f;
        st.stereo = P (li.stereo) * (1.0f / 720.0f);
        st.mono = st.syncType >= 2 && li.mono >= 0 && g.base[li.mono] >= 0.5f && g.monoCore != nullptr;
        st.host = li.host >= 0 && g.base[li.host] >= 0.5f;
        st.smooth = P (li.smooth) >= 0.5f;
        st.swing = li.swing >= 0 ? clamp01 (P (li.swing) * 0.01f) : 0.0f;
        st.dir = static_cast<int> (P (li.direction) + 0.5f);
        st.custom = (st.shape == 12 && g.lfoContent != nullptr && g.lfoContent->lfo[static_cast<size_t> (l)].present) ? &g.lfoContent->lfo[static_cast<size_t> (l)] : nullptr;
        st.half = st.smooth ? 0.2f * P (li.smoothTime) * 0.001f : 0.0f; // half life of the one pole (document 03, 3.4)
        for (int k = 0; k < 2; ++k)
        {
            st.dt[k] = static_cast<float> ((k == 0 ? 1 : 4) * kControlPeriod / g.sampleRate);
            st.coef[k] = st.smooth && st.half >= 0.0002f ? 1.0f - std::exp2 (-st.dt[k] / st.half) : 0.0f;
        }
        st.ok = !g.referenceControl && (lastTouchedGroups & (1u << (kGroupLfo + l))) == 0;
        st.epoch = g.baseEpoch;
        st.bpm = g.bpm;
        st.factorBits = factorBits;
        st.concertABits = concertABits;
        st.sampleRate = g.sampleRate;
        st.content = g.lfoContent;
        st.monoCore = g.monoCore;
    }
    const int ki = steps == 4 ? 1 : 0;
    const float dt = steps == 1 || steps == 4 ? st.dt[ki] : static_cast<float> (steps * kControlPeriod / g.sampleRate);
    const int shape = st.shape;
    const int sync = st.sync;
    const int syncType = st.syncType;
    const float rate = sync == 4 ? vc.lfoFactor * lfoRateHz (sync, st.rateKnob, st.division, g.bpm, g.concertA, lastSemis, st.keyTranspose, st.keyTune) : st.rate;
    const float knob = st.knob;
    const float delay = st.delay, fade = st.fade;
    const float stereo = st.stereo;
    const bool mono = st.mono;
    const bool host = st.host;

    float pos, pos0;
    if (syncType == 1)
    {
        // free-run: every voice reads the same clock (the engine's, or the host song position with the HOST lock)
        float clock;
        if (host && g.playing && sync != 4)
        {
            const double c = static_cast<double> (rate) * g.songSeconds;
            clock = static_cast<float> (c - std::floor (c));
        }
        else if (sync == 4)
        {
            lfoCore[l].advance (0, 0.0f, rate, 0.0f, dt, true);
            clock = lfoCore[l].offset;
        }
        else
            clock = g.lfoPhase != nullptr ? g.lfoPhase[l] : lfoCore[l].offset;
        pos = clock + knob;
        pos -= ffloor (pos);
        pos0 = pos;
        lfoReadPos[l] = clock; // display only: the free-running clock (the offset of the core does not move for it)
    }
    else
    {
        if (mono)
        {
            pos = g.monoCore[l].position (syncType, knob);
        }
        else
        {
            lfoCore[l].advance (syncType, knob, rate, delay, dt, !released);
            pos = lfoCore[l].position (syncType, knob);
        }
        pos0 = syncType == 0 ? knob - ffloor (knob) : (syncType == 2 ? std::min (knob, 1.0f) : 0.0f);
        lfoReadPos[l] = mono ? g.monoCore[l].offset : lfoCore[l].offset; // display only
    }
    lfoAge[l] += dt;
    const bool smooth = st.smooth;
    float fadeAmount = 1.0f;
    if (!smooth)
    {
        if (lfoAge[l] < delay)
            fadeAmount = 0.0f;
        else if (fade > 0.0f)
            fadeAmount = clamp01 ((lfoAge[l] - delay) / fade);
    }
    const float swing = st.swing;
    const int dir = st.dir;
    const CompiledShape* custom = st.custom;
    auto shapeAt = [&] (float u, bool wrap) -> float // 0 .. 1
    {
        return lfoShapeAt (shape, u, wrap, swing, dir, custom, src, scratchShape, noteSerial + static_cast<uint32_t> (l));
    };
    const bool wrap = syncType <= 1;
    const bool smoothStep = smooth && st.half >= 0.0002f;
    const float coef = steps == 1 || steps == 4 ? st.coef[ki] : (smoothStep ? 1.0f - std::exp2 (-dt / st.half) : 0.0f);
    auto lane = [&] (float sign, float& smoothed) -> float
    {
        const float s = shapeAt (pos + sign * stereo, wrap);
        float out = s;
        if (fadeAmount < 1.0f)
            out = lerp (shapeAt (pos0 + sign * stereo, wrap), s, fadeAmount);
        float v = 2.0f * out - 1.0f;
        if (smooth)
        {
            if (smoothStep)
                smoothed += (v - smoothed) * coef;
            else
                smoothed = v;
            v = smoothed;
        }
        else
            smoothed = v;
        return v;
    };
    const float left = lane (1.0f, lfoSmoothL[l]);
    valueRight = stereo != 0.0f ? lane (-1.0f, lfoSmoothR[l]) : left;
    if (stereo == 0.0f)
        lfoSmoothR[l] = lfoSmoothL[l];
    return left;
}

float Voice::lfoValueReference (int l, const TickGlobals& g, float& valueRight, int steps) noexcept
{
    const LfoIdx& li = paramIndex().lfo[l];
    const float dt = static_cast<float> (steps * kControlPeriod / g.sampleRate);
    const int shape = static_cast<int> (P (li.shape) + 0.5f);
    const int sync = static_cast<int> (P (li.sync) + 0.5f);
    const int syncType = std::clamp (static_cast<int> (P (li.syncType) + 0.5f), 0, 5);
    const float rate = vc.lfoFactor * lfoRateHz (sync, P (li.rate), static_cast<int> (P (li.division) + 0.5f), g.bpm, g.concertA, lastSemis, P (li.keyTranspose), P (li.keyTune));
    const float knob = P (li.phase) * (1.0f / 360.0f);
    const float delay = P (li.delay) * 0.001f, fade = P (li.fadeIn) * 0.001f;
    const float stereo = P (li.stereo) * (1.0f / 720.0f);
    const bool mono = syncType >= 2 && li.mono >= 0 && g.base[li.mono] >= 0.5f && g.monoCore != nullptr;
    const bool host = li.host >= 0 && g.base[li.host] >= 0.5f;

    float pos, pos0;
    if (syncType == 1)
    {
        // free-run: every voice reads the same clock (the engine's, or the host song position with the HOST lock)
        float clock;
        if (host && g.playing && sync != 4)
        {
            const double c = static_cast<double> (rate) * g.songSeconds;
            clock = static_cast<float> (c - std::floor (c));
        }
        else if (sync == 4)
        {
            lfoCore[l].advance (0, 0.0f, rate, 0.0f, dt, true);
            clock = lfoCore[l].offset;
        }
        else
            clock = g.lfoPhase != nullptr ? g.lfoPhase[l] : lfoCore[l].offset;
        pos = clock + knob;
        pos -= ffloor (pos);
        pos0 = pos;
        lfoReadPos[l] = clock; // display only: the free-running clock (the offset of the core does not move for it)
    }
    else
    {
        if (mono)
        {
            pos = g.monoCore[l].position (syncType, knob);
        }
        else
        {
            lfoCore[l].advance (syncType, knob, rate, delay, dt, !released);
            pos = lfoCore[l].position (syncType, knob);
        }
        pos0 = syncType == 0 ? knob - ffloor (knob) : (syncType == 2 ? std::min (knob, 1.0f) : 0.0f);
        lfoReadPos[l] = mono ? g.monoCore[l].offset : lfoCore[l].offset; // display only
    }
    lfoAge[l] += dt;
    const bool smooth = P (li.smooth) >= 0.5f;
    float fadeAmount = 1.0f;
    if (!smooth)
    {
        if (lfoAge[l] < delay)
            fadeAmount = 0.0f;
        else if (fade > 0.0f)
            fadeAmount = clamp01 ((lfoAge[l] - delay) / fade);
    }
    const float swing = li.swing >= 0 ? clamp01 (P (li.swing) * 0.01f) : 0.0f;
    const int dir = static_cast<int> (P (li.direction) + 0.5f);
    const CompiledShape* custom = (shape == 12 && g.lfoContent != nullptr && g.lfoContent->lfo[static_cast<size_t> (l)].present) ? &g.lfoContent->lfo[static_cast<size_t> (l)] : nullptr;
    auto shapeAt = [&] (float u, bool wrap) -> float // 0 .. 1
    {
        return lfoShapeAt (shape, u, wrap, swing, dir, custom, src, scratchShape, noteSerial + static_cast<uint32_t> (l));
    };
    const bool wrap = syncType <= 1;
    auto lane = [&] (float sign, float& smoothed) -> float
    {
        const float s = shapeAt (pos + sign * stereo, wrap);
        float out = s;
        if (fadeAmount < 1.0f)
            out = lerp (shapeAt (pos0 + sign * stereo, wrap), s, fadeAmount);
        float v = 2.0f * out - 1.0f;
        if (smooth)
        {
            const float half = 0.2f * P (li.smoothTime) * 0.001f; // half life of the one pole (document 03, 3.4)
            if (half >= 0.0002f)
                smoothed += (v - smoothed) * (1.0f - std::exp2 (-dt / half));
            else
                smoothed = v;
            v = smoothed;
        }
        else
            smoothed = v;
        return v;
    };
    const float left = lane (1.0f, lfoSmoothL[l]);
    valueRight = stereo != 0.0f ? lane (-1.0f, lfoSmoothR[l]) : left;
    if (stereo == 0.0f)
        lfoSmoothR[l] = lfoSmoothL[l];
    return left;
}

void Voice::fillOscPart (int o, OscPart& op) noexcept
{
    // the expressions of Voice::tick as they were, on the keys of oscillator o (and the stereo spread of the master, a plain value)
    const OscIdx& oi = pidx->osc[o];
    const LibKeys& lk = *libk[o];
    op.dest = static_cast<int> (P (oi.destination) + 0.5f);
    op.blend = oi.filterBlend >= 0 ? clamp01 (P (oi.filterBlend) * 0.01f) : 0.0f;
    op.send[0] = oi.bus1Send >= 0 ? std::max (0.0f, P (oi.bus1Send) * 0.01f) : 0.0f;
    op.send[1] = oi.bus2Send >= 0 ? std::max (0.0f, P (oi.bus2Send) * 0.01f) : 0.0f;
    op.env1 = P (oi.env1) >= 0.5f;
    op.wtChoice = std::clamp (static_cast<int> (P (oi.wavetable) + 0.5f), 0, kNumWavetableChoices - 1);
    op.track = P (oi.pitchTrack) >= 0.5f;
    op.bendTrack = P (oi.bendTrack) >= 0.5f;
    op.fixedNote = P (oi.fixedPitch);
    op.pmode = static_cast<int> (P (lk.idx[lk_pitchMode]) + 0.5f);
    const float coarse = P (oi.pitchCoarse);
    float coarseSemis = coarse;
    if (op.pmode == 1)
        coarseSemis = 12.0f * std::log2 (std::max (1.0f, 1.0f + ffloor (coarse)));
    else if (op.pmode == 2)
        coarseSemis = coarse >= 0.0f ? 12.0f * std::log2 (1.0f + coarse) : 12.0f * std::log2 (1.0f / (1.0f - coarse));
    op.coarseSemis = coarseSemis;
    op.ratioSrc = op.pmode == 2 ? static_cast<int> (P (lk.idx[lk_ratioSrc]) + 0.5f) - 1 : -1;
    op.transposeSnap = static_cast<int> (P (oi.transposeSnap) + 0.5f);
    op.transpose = P (oi.transpose);
    op.tune001 = P (oi.tune) * 0.01f;
    op.frame = clamp01 (P (oi.frame) * 0.01f);
    op.frameSpread = std::clamp (P (oi.frameSpread) * 0.01f, -1.0f, 1.0f);
    op.level = std::max (0.0f, P (oi.level) * 0.01f);
    op.pan = P (oi.pan) * 0.01f;
    op.panRight = PR (oi.pan) * 0.01f;
    op.voicesRaw = static_cast<int> (P (oi.uniVoices) + 0.5f);
    const float detune = clamp01 (P (oi.uniDetune) * 0.01f);
    op.detuneSemis = detune * std::max (P (oi.uniRange), 0.0f);
    op.curve = P (oi.uniCurve);
    op.blendShape = std::clamp (P (oi.uniBlendShape) * 0.01f, -1.0f, 1.0f);
    op.stack = static_cast<int> (P (oi.uniStack) + 0.5f);
    op.stackRepeat = static_cast<int> (P (oi.uniStackRepeat) + 0.5f);
    op.mode = static_cast<int> (P (oi.uniMode) + 0.5f);
    op.spread = clamp01 (P (oi.uniSpread) * 0.01f) * clamp01 (gl->base[pidx->master.stereoSpread] * 0.01f);
    op.uniBlend = clamp01 (P (oi.uniBlend) * 0.01f);
    op.law = oi.uniLaw >= 0 ? static_cast<int> (gl->base[oi.uniLaw] + 0.5f) : 0;
    op.warp[0] = static_cast<int> (P (oi.warpType) + 0.5f);
    op.warpAmount[0] = clamp01 (P (oi.warpAmount) * 0.01f);
    op.warpPhase[0] = P (oi.warpPhase) * (1.0f / 360.0f);
    op.warpSpread[0] = std::clamp (P (oi.warpSpread) * 0.01f, -1.0f, 1.0f);
    op.warp[1] = static_cast<int> (P (oi.warp2Type) + 0.5f);
    op.warpAmount[1] = clamp01 (P (oi.warp2Amount) * 0.01f);
    op.warpPhase[1] = P (oi.warp2Phase) * (1.0f / 360.0f);
    op.warpSpread[1] = std::clamp (P (oi.warp2Spread) * 0.01f, -1.0f, 1.0f);
    op.morphType = static_cast<int> (P (lk.idx[lk_morphType]) + 0.5f);
    op.morphAmount = clamp01 (P (lk.idx[lk_morphAmount]) * 0.01f);
    op.morphSpread = std::clamp (P (lk.idx[lk_morphSpread]) * 0.01f, -1.0f, 1.0f);
    op.smooth = P (lk.idx[lk_smooth]) >= 0.5f;
    op.remapOn = P (lk.idx[lk_remapOn]) >= 0.5f;
    op.remapSlot = std::clamp (static_cast<int> (P (lk.idx[lk_remapSlot]) + 0.5f), 0, kNumRemapSlots - 1);
    op.remapRise = P (lk.idx[lk_remapRise]);
    op.remapFall = P (lk.idx[lk_remapFall]);
    op.libValid = false;
}

void Voice::evaluateVoiceControl (const TickGlobals& g) noexcept
{
    const VoiceCtlIdx& v = pidx->vctl;
    if (v.length < 0)
        return;
    const int length = std::clamp (static_cast<int> (g.base[v.length] + 0.5f), 1, 8);
    const int step = static_cast<int> ((sequence - 1u) % static_cast<uint32_t> (length));
    vcStep = step;
    auto val = [&] (int k) { return g.base[v.step[step][k]]; };
    const float rp = g.base[v.randPan] * 0.01f, rc = g.base[v.randCutoff] * 0.01f, re = g.base[v.randEnv] * 0.01f;
    const float rd = g.base[v.randDetune] * (g.base[v.rand10x] >= 0.5f ? 10.0f : 1.0f);
    vc.pan = val (0) * 0.01f + vcRand[0] * rp;
    vc.detuneSemis = (val (1) + vcRand[3] * rd) * 0.01f;
    vc.cutoffSemis = (val (2) * 0.01f + vcRand[1] * rc) * 36.0f;
    vc.envFactor = std::pow (4.0f, val (3) * 0.01f + vcRand[2] * re);
    vc.mod1 = val (4) * 0.01f;
    vc.mod2 = val (5) * 0.01f;
    vc.index = length > 1 ? static_cast<float> (step) / static_cast<float> (length - 1) : 0.0f;
    for (int k = 0; k < 5; ++k)
        vc.mask[k] = g.base[v.osc[k]] >= 0.5f;
    // the scalings (25 .. 400 %): the envelope times follow the percentage, the LFO rate is the inverse of it in Percent mode and the percentage in Rate mode
    vc.envFactor *= g.base[v.scaleEnv] * 0.01f;
    const float lf = g.base[v.scaleLfo] * 0.01f;
    vc.lfoFactor = g.base[v.scaleLfoMode] >= 0.5f ? lf : 1.0f / std::max (lf, 0.01f);
}

void Voice::tick (const TickGlobals& g, int voiceIndex) noexcept
{
    SB_SECTION (sections::tickOther);
    gl = &g;
    index = voiceIndex;
    const ParamIndex& pi = *pidx;
    const double dt = kControlPeriod / g.sampleRate;
    const float fdt = static_cast<float> (dt);
    // the parameter reads of this function with the snapshot and the delta arrays in locals (CPU-REVIEW-VOICE-MOD.md V5): a call between two reads (modulatedSlow, an oscillator
    // update) no longer forces this->modP and gl->base to be loaded again. These hide the members P () and PR () inside tick (); the same values.
    const float* const pBase = g.base;
    const float* const pMod = modP;
    const float* const pModR = modRP;
    const auto P = [this, pBase, pMod] (int idx) noexcept { return readParam (pBase, pMod, idx); };
    const auto PR = [this, pBase, pModR] (int idx) noexcept { return readParam (pBase, pModR, idx); };

    // ---- pitch of the note
    const MasterIdx& mi = pi.master;
    if (glideTime < 0.0f)
    {
        float t = g.base[mi.portTime] * 0.001f;
        if (g.base[mi.portScaled] >= 0.5f)
            t *= std::min(std::fabs (glideOffset) / 12.0f, 4.0f);
        glideTime = t;
        glideSlope = g.base[mi.portSlope];
        glideElapsed = 0.0f;
        if (!(t > 0.0f))
            glideOffset = 0.0f;
        else
            glideStart = glideOffset;
    }
    if (glideOffset != 0.0f && glideTime > 0.0f)
    {
        glideElapsed += fdt;
        const float x = clamp01 (glideElapsed / glideTime);
        glideOffset = glideStart * (1.0f - powerShape (x, -glideSlope * 0.5f));
        if (x >= 1.0f)
            glideOffset = 0.0f;
    }
    const float bend = g.channelBendSemis != nullptr ? g.channelBendSemis[chan & 15] : 0.0f;
    // the tuned note follows the tuning (A4, scale, MTS-ESP) with a 20 ms one pole: a retuned note glides, a new note starts on its value
    if (g.tunedNotes != nullptr && !g.tuningNoteOnOnly)
    {
        const float target = g.tunedNotes[keyNum & 127];
        if (target == target)
            {
                if (tuneCoefDt != fdt)
                {
                    tuneCoefDt = fdt;
                    tuneCoef = 1.0f - std::exp (-fdt / 0.02f); // the same value every period
                }
                tuneTrack += (target - tuneTrack) * tuneCoef;
            }
    }
    // the master transpose and tune are added after the matrix of this period (noteSemis below): a slot on master.tune moves the pitch like one on an oscillator tune
    const float noteSemisKnobless = tuneTrack + glideOffset + bend + tuneExpr;
    lastSemis = tuneTrack + glideOffset;

    if (g.baseEpoch == 0 || !vcCacheValid || vcCacheEpoch != g.baseEpoch || vcCacheSequence != sequence) // epoch 0: the caller gives no epoch, nothing is kept
    {
        evaluateVoiceControl (g);
        vcCacheValid = true;
        vcCacheEpoch = g.baseEpoch;
        vcCacheSequence = sequence;
    }
    auto envSettings = [&] (int e, EnvSettings& s, bool finalise)
    {
        const EnvIdx& ei = pi.env[e];
        legatoInv[e] = ei.legatoInverted >= 0 && g.base[ei.legatoInverted] >= 0.5f;
        if (ei.units >= 0 && g.base[ei.units] >= 0.5f) // beat units: the times follow the host tempo
        {
            const float spb = 60.0f / static_cast<float> (std::max (g.bpm, 1.0));
            s.delay = P (ei.delayBeats) * spb;
            s.attack = P (ei.attackBeats) * spb;
            s.hold = P (ei.holdBeats) * spb;
            s.decay = P (ei.decayBeats) * spb;
            s.release = P (ei.releaseBeats) * spb;
        }
        else
        {
            s.delay = P (ei.delay) * 0.001f;
            s.attack = P (ei.attack) * 0.001f;
            s.hold = P (ei.hold) * 0.001f;
            s.decay = P (ei.decay) * 0.001f;
            s.release = P (ei.release) * 0.001f;
        }
        s.sustain = clamp01 (P (ei.sustain) * 0.01f);
        s.attackCurve = -P (ei.attackCurve);
        s.decayCurve = P (ei.decayCurve);
        s.releaseCurve = P (ei.releaseCurve);
        if (finalise)
            envLen[e][0] = s.attack + s.delay, envLen[e][1] = s.hold, envLen[e][2] = s.decay, envLen[e][3] = s.release, envLen[e][4] = s.attack, envLen[e][5] = s.delay;
        s.delay *= vc.envFactor;
        s.attack *= vc.envFactor;
        s.hold *= vc.envFactor;
        s.decay *= vc.envFactor;
        s.release *= vc.envFactor;
    };
    // the velocity and note sources read through their curves (content.curves); with curve.velocity.legato / curve.note.legato a legato transition glides with master.portamento.time
    const CompiledModCurve* velCurve = g.lfoContent != nullptr ? &g.lfoContent->velocityCurve : nullptr;
    const CompiledModCurve* noteCurve = g.lfoContent != nullptr ? &g.lfoContent->noteCurve : nullptr;
    const bool velGlide = mi.curveVelLegato >= 0 && g.base[mi.curveVelLegato] >= 0.5f, noteGlide = mi.curveNoteLegato >= 0 && g.base[mi.curveNoteLegato] >= 0.5f;
    const float portSeconds = g.base[mi.portTime] * 0.001f;
    // ---- a note starts with its constant sources already in the matrix (document 03 1.3a, document 04: the interpolation of a voice is reset to the new value at a note
    //      start), so an envelope time that a macro or the velocity modulates is read modulated at the very first period
    if (fresh)
    {
        clearMod();
        std::fill (std::begin (src), std::end (src), 0.0f);
        for (int m = 0; m < 8; ++m)
            src[srcMacro (m)] = g.macro[m];
        src[kSrcVelocity] = curvedSource (vel, velCurve, velCurved, velGlide, fdt, portSeconds);
        src[kSrcNote] = curvedSource (std::clamp ((lastSemis + bend) * (1.0f / 127.0f), 0.0f, 1.0f), noteCurve, noteCurved, noteGlide, fdt, portSeconds);
        src[kSrcAftertouch] = g.channelAftertouch != nullptr ? g.channelAftertouch[chan & 15] : 0.0f;
        src[kSrcModWheel] = g.channelModWheel != nullptr ? g.channelModWheel[chan & 15] : 0.0f;
        src[kSrcRandom] = randomPerNote;
        src[kSrcSlide] = slide;
        src[kSrcVoiceIndex] = vc.index;
        src[kSrcVoiceMod1] = 0.5f + 0.5f * vc.mod1;
        src[kSrcVoiceMod2] = 0.5f + 0.5f * vc.mod2;
        src[kSrcNoteOnAlt1] = altParity ? 1.0f : 0.0f;
        src[kSrcNoteOnAlt2] = altParity ? 0.0f : 1.0f;
        src[kSrcNoteOnRand2] = rand2;
        src[kSrcNoteOnRandDiscrete] = randDiscrete;
        src[kSrcFixed] = 1.0f;
        std::copy (std::begin (src), std::end (src), std::begin (srcR));
        EvalContext ctx;
        ctx.src = src;
        ctx.srcR = srcR;
        ctx.global = false;
        ctx.delta = mod.data();
        ctx.deltaRight = modR.data();
        ctx.touched = touched;
        ctx.numTouched = &numTouched;
        ctx.maxTouched = kNumModSlots;
        ctx.state = slotState;
        ctx.dt = 0.0f;
        ctx.bpm = g.bpm;
        ctx.voiceAge = voiceAge;
        ctx.reference = g.referenceControl;
        { SB_SECTION (sections::tickMatrix); g.matrix->evaluate (ctx); }
        // the control rate envelopes are evaluated for the same chunk as the note start: as sources they already have their value at the end of the first step
        for (int e = 0; e < 6; ++e)
        {
            Envelope tmp = env[e];
            EnvSettings es;
            envSettings (e, es, false);
            const float v = tmp.advance (es, dt);
            src[srcEnv (e)] = v * v;
        }
        std::copy (std::begin (src), std::end (src), std::begin (srcR));
        clearMod();
        { SB_SECTION (sections::tickMatrix); g.matrix->evaluate (ctx); }
    }
    // ---- envelopes (times read with last period's modulation)
    {
    SB_SECTION (sections::tickEnvLfo);
    const bool envFromCache = g.baseEpoch != 0 && envCacheValid && envCacheEpoch == g.baseEpoch && envCacheBpm == g.bpm && numTouched == 0 && !fresh;
    // V1: an envelope none of whose keys a slot touched keeps the settings it derived from the parameters alone (the keys of the other groups may be modulated). A legato note
    // (Voice::legatoTo) clears envCacheValid, because the key of envFromCache has no note sequence (test_own_voice_legato_env.cpp); the per envelope cache below is used only
    // where envFromCache is not, and is keyed on the factor itself.
    lastTouchedGroups = g.referenceControl ? ~0u : touchedGroups();
    const uint32_t envTouched = lastTouchedGroups;
    uint32_t envFactorBits;
    std::memcpy (&envFactorBits, &vc.envFactor, 4);
    for (int e = 0; e < 6; ++e)
    {
        // tier low: an envelope (not the amplitude envelope) that no slot reads is advanced every fourth period (staggered) by four periods
        const bool decimated = (g.economy & kEconomyControl) != 0 && !fresh && e > 0 && ((g.usedSources >> srcEnv (e)) & 1u) == 0;
        const bool skipped = decimated && ((tickCount + static_cast<uint32_t> (e)) & 3u) != 0u; // envOut keeps its last value
        EnvSettings s;
        if (envFromCache)
        {
            if (skipped)
                continue;
            s = envCache[e];
        }
        else
        {
            const bool envUntouched = (envTouched & (1u << (kGroupEnv + e))) == 0;
            uint32_t setFactorBits;
            std::memcpy (&setFactorBits, &envSetFactor[e], 4);
            if (!fresh && envUntouched && envSetOk[e] && g.baseEpoch != 0 && envSetEpoch[e] == g.baseEpoch && envSetBpm[e] == g.bpm && setFactorBits == envFactorBits)
            {
                // envCache[e], envLen[e] and legatoInv[e] hold what envSettings () gives now (the same keys, epoch, tempo and factor)
                if (skipped)
                    continue;
                s = envCache[e];
            }
            else
            {
                envSettings (e, s, true);
                envSetOk[e] = !fresh && envUntouched;
                if (envSetOk[e])
                    s.fillDens(); // V13: settings that are kept get the denominators of their curves once
                envCache[e] = s;
                envSetEpoch[e] = g.baseEpoch;
                envSetBpm[e] = g.bpm;
                envSetFactor[e] = vc.envFactor;
                if (skipped)
                    continue;
            }
        }
        if (fresh && e == 0)
        {
            const float v0 = env[0].advance (s, 0.0); // the level at the note's first sample
            envStart = v0 * v0;
        }
        const double stepDt = decimated ? 4.0 * dt : dt;
        const float v = env[e].advance (s, stepDt);
        envOut[e] = v * v;
    }
    if (!envFromCache)
    {
        envCacheValid = numTouched == 0 && !fresh; // settings read with modulation are not the ones of the parameters alone
        envCacheEpoch = g.baseEpoch;
        envCacheBpm = g.bpm;
    }
    // ---- LFOs
    // Economy group Control (tier low): an LFO that no slot reads only feeds the display, so it is evaluated every fourth period (staggered) over four periods' time
    // instead of every period: the phase and the fade keep their pace, the display gets a value 1.4 ms old at most.
    ++tickCount;
    for (int l = 0; l < 10; ++l)
    {
        if ((g.economy & kEconomyControl) != 0 && !fresh && ((g.usedSources >> srcLfo (l)) & 1u) == 0)
        {
            if (((tickCount + static_cast<uint32_t> (l)) & 3u) != 0u)
                continue; // lfoOut / lfoOutR keep their last values
            lfoOut[l] = g.referenceControl ? lfoValueReference (l, g, lfoOutR[l], 4) : lfoValue (l, g, lfoOutR[l], 4);
            continue;
        }
        lfoOut[l] = g.referenceControl ? lfoValueReference (l, g, lfoOutR[l]) : lfoValue (l, g, lfoOutR[l]);
    }
    for (int r = 0; r < 4; ++r)
    {
        // tier low: a random LFO that no slot reads is stepped every fourth period (staggered) by four periods' worth of cycles
        int steps = 1;
        if ((g.economy & kEconomyControl) != 0 && !fresh && ((g.usedSources >> srcRlfo (r)) & 1u) == 0)
        {
            if (((tickCount + static_cast<uint32_t> (r)) & 3u) != 0u)
                continue; // rlfoOut / rlfoOutR keep their last values
            steps = 4;
        }
        const float fdtS = fdt * static_cast<float> (steps);
        const RlfoIdx& ri = pi.rlfo[r];
        if (g.referenceControl)
        {
            const int syncType = static_cast<int> (P (ri.syncType) + 0.5f);
            if (syncType == 1 && g.rlfoValue != nullptr)
                rlfoOut[r] = rlfoOutR[r] = g.rlfoValue[r];
            else
            {
                const int sync = static_cast<int> (P (ri.sync) + 0.5f);
                const float rate = vc.lfoFactor * lfoRateHz (sync, P (ri.rate), static_cast<int> (P (ri.division) + 0.5f), g.bpm, g.concertA, lastSemis, P (ri.keyTranspose), P (ri.keyTune));
                const int style = static_cast<int> (P (ri.style) + 0.5f);
                rlfoOut[r] = rlfo[r].advance (style, rate * fdtS);
                // stereo: the right lane has random numbers of its own
                rlfoOutR[r] = (ri.stereo >= 0 && g.base[ri.stereo] >= 0.5f) ? rlfoR[r].advance (style, rate * fdtS) : rlfoOut[r];
            }
            continue;
        }
        // V3: the same with the keys and the rate of the random LFO kept while their inputs are the same and no slot touched its keys
        RlfoSettings& rs = rlfoSet[r];
        uint32_t factorBits, concertABits;
        std::memcpy (&factorBits, &vc.lfoFactor, 4);
        std::memcpy (&concertABits, &g.concertA, 4);
        const bool untouched = (lastTouchedGroups & (1u << (kGroupRlfo + r))) == 0;
        if (!(rs.ok && untouched && g.baseEpoch != 0 && rs.epoch == g.baseEpoch && rs.bpm == g.bpm && rs.factorBits == factorBits && rs.concertABits == concertABits))
        {
            rs.syncType = static_cast<int> (P (ri.syncType) + 0.5f);
            rs.sync = static_cast<int> (P (ri.sync) + 0.5f);
            rs.rateKnob = P (ri.rate);
            rs.division = static_cast<int> (P (ri.division) + 0.5f);
            rs.keyTranspose = P (ri.keyTranspose);
            rs.keyTune = P (ri.keyTune);
            rs.rate = vc.lfoFactor * lfoRateHz (rs.sync, rs.rateKnob, rs.division, g.bpm, g.concertA, lastSemis, rs.keyTranspose, rs.keyTune);
            rs.style = static_cast<int> (P (ri.style) + 0.5f);
            rs.stereo = ri.stereo >= 0 && g.base[ri.stereo] >= 0.5f;
            rs.ok = untouched;
            rs.epoch = g.baseEpoch;
            rs.bpm = g.bpm;
            rs.factorBits = factorBits;
            rs.concertABits = concertABits;
        }
        if (rs.syncType == 1 && g.rlfoValue != nullptr)
            rlfoOut[r] = rlfoOutR[r] = g.rlfoValue[r];
        else
        {
            const float rate = rs.sync == 4 ? vc.lfoFactor * lfoRateHz (rs.sync, rs.rateKnob, rs.division, g.bpm, g.concertA, lastSemis, rs.keyTranspose, rs.keyTune) : rs.rate;
            rlfoOut[r] = rlfo[r].advance (rs.style, rate * fdtS);
            // stereo: the right lane has random numbers of its own
            rlfoOutR[r] = rs.stereo ? rlfoR[r].advance (rs.style, rate * fdtS) : rlfoOut[r];
        }
    }

    }
    // ---- sources
    {
    SB_SECTION (sections::tickSources);
    std::fill (std::begin (src), std::end (src), 0.0f);
    for (int e = 0; e < 6; ++e)
        src[srcEnv (e)] = envOut[e];
    for (int l = 0; l < 10; ++l)
        src[srcLfo (l)] = 0.5f + 0.5f * lfoOut[l];
    for (int r = 0; r < 4; ++r)
        src[srcRlfo (r)] = 0.5f + 0.5f * rlfoOut[r];
    for (int m = 0; m < 8; ++m)
        src[srcMacro (m)] = g.macro[m];
    src[kSrcVelocity] = curvedSource (vel, velCurve, velCurved, velGlide, fdt, portSeconds);
    src[kSrcNote] = curvedSource (std::clamp ((lastSemis + bend) * (1.0f / 127.0f), 0.0f, 1.0f), noteCurve, noteCurved, noteGlide, fdt, portSeconds);
    curveFresh = false;
    src[kSrcAftertouch] = g.channelAftertouch != nullptr ? g.channelAftertouch[chan & 15] : 0.0f;
    src[kSrcPitchWheel] = 0.5f + 0.5f * (mi.bendRange >= 0 && g.base[mi.bendRange] > 0.0f && g.channelBendSemis != nullptr ? std::clamp (bend / g.base[mi.bendRange], -1.0f, 1.0f) : 0.0f);
    src[kSrcModWheel] = g.channelModWheel != nullptr ? g.channelModWheel[chan & 15] : 0.0f;
    src[kSrcRandom] = randomPerNote;
    src[kSrcLift] = released ? releaseVel : 0.5f;
    src[kSrcReleaseVelocity] = released ? releaseVel : 0.5f;
    src[kSrcSlide] = slide;
    { const float n12 = (lastSemis + bend) * (1.0f / 12.0f); src[kSrcOctNote] = n12 - ffloor (n12); }
    src[kSrcPolyAftertouch] = polyPressure;
    src[kSrcVoiceIndex] = vc.index;
    src[kSrcVoiceMod1] = 0.5f + 0.5f * vc.mod1;
    src[kSrcVoiceMod2] = 0.5f + 0.5f * vc.mod2;
    src[kSrcOscA] = srcLevel[0];
    src[kSrcOscB] = srcLevel[1];
    src[kSrcOscC] = srcLevel[2];
    src[kSrcNoise] = srcLevel[3];
    src[kSrcSub] = srcLevel[4];
    src[kSrcFilter1] = srcLevel[5];
    src[kSrcFilter2] = srcLevel[6];
    src[kSrcStereo] = 1.0f;
    src[kSrcActiveVoices] = static_cast<float> (g.activeVoices) * (1.0f / 32.0f);
    src[kSrcNoteOnAlt1] = altParity ? 1.0f : 0.0f;
    src[kSrcNoteOnAlt2] = altParity ? 0.0f : 1.0f;
    src[kSrcNoteOnRand2] = rand2;
    src[kSrcNoteOnRandDiscrete] = randDiscrete;
    src[kSrcExpressionX] = 0.5f + 0.5f * exprX;
    src[kSrcFixed] = 1.0f;
    }

    // ---- matrix
    clearMod();
    {
        std::copy (std::begin (src), std::end (src), std::begin (srcR));
        for (int l = 0; l < 10; ++l)
            srcR[srcLfo (l)] = 0.5f + 0.5f * lfoOutR[l];
        for (int r = 0; r < 4; ++r)
            srcR[srcRlfo (r)] = 0.5f + 0.5f * rlfoOutR[r];
        srcR[kSrcStereo] = 0.0f;
        voiceAge += fdt;
        EvalContext ctx;
        ctx.src = src;
        ctx.srcR = srcR;
        ctx.global = false;
        ctx.delta = mod.data();
        ctx.deltaRight = modR.data();
        ctx.touched = touched;
        ctx.numTouched = &numTouched;
        ctx.maxTouched = kNumModSlots;
        ctx.state = slotState;
        ctx.dt = fdt;
        ctx.bpm = g.bpm;
        ctx.voiceAge = voiceAge;
        ctx.reference = g.referenceControl;
        { SB_SECTION (sections::tickMatrix); g.matrix->evaluate (ctx); }
        // MPC shell: the poly aftertouch of a pad goes to the Filter 1 cutoff (a layer over the patch: the knob and the matrix slots are not touched). One pole smoothing
        // (time constant kPadPressureSeconds) of the ~170 / s stream; the delta is in semitones, the control domain of the cutoff. Off: nothing is computed.
        // If the preset's modulation matrix already has a Poly Aftertouch -> Filter 1 Cutoff route, the layer does not add to that voice (no doubling).
        if (g.padPressureToCutoff > 0.0f)
        {
            if (fresh)
                smoothedPressure = polyPressure;
            else
            {
                if (padCoefDt != fdt)
                {
                    padCoefDt = fdt;
                    padCoef = 1.0f - std::exp (-fdt / kPadPressureSeconds);
                }
                smoothedPressure += (polyPressure - smoothedPressure) * padCoef;
            }
            const bool matrixHandlesCutoff = g.matrix != nullptr && g.matrix->hasPolyAftertouchToFilter1Cutoff();
            const float deltaCutoff = matrixHandlesCutoff ? 0.0f : g.padPressureToCutoff * smoothedPressure * kPadPressureSemitones;
            padCutIdx = pi.filter[0].cutoff;
            if (deltaCutoff != 0.0f && padCutIdx >= 0)
            {
                mod[static_cast<size_t> (padCutIdx)] += deltaCutoff;
                modR[static_cast<size_t> (padCutIdx)] += deltaCutoff;
                padCutTouched = true;
            }
        }
        else
            smoothedPressure = polyPressure;
    }
    // the pitch of the note with the modulated master transpose and tune (docs/engine-spec/01:176, 04:191); the readers of the pitch are all below this line
    const float noteSemis = noteSemisKnobless + P (mi.transpose) + P (mi.tune) * 0.01f;

    // ---- amplitude: envelope 1, velocity
    {
    SB_SECTION (sections::tickAmp);
    float killGain = 1.0f;
    if (killFading)
    {
        killElapsed += fdt;
        killGain = std::max (0.0f, 1.0f - killElapsed * (1.0f / g.killSeconds)); // linear fade of the envelope value in 50 ms (document 01, 3.2; tier low: shorter)
    }
    ampPrev = ampCur;
    ampCur = envOut[0] * killGain * killGain;
    velGainPrev = velGain;
    {
        const float track = g.base[mi.velocityTrack] * 0.01f;
        const float v2 = vel * vel;
        velGain = track >= 0.0f ? lerp (1.0f, v2, track) : lerp (1.0f, 1.0f - v2, -track);
    }
    if (fresh)
    {
        // a note starts at its sample at the level of its envelope: no ramp from zero across the first period (an attack of 0 ms is instant)
        ampPrev = envStart;
        velGainPrev = velGain;
    }
    if (env[0].finished() || killFading)
    {
        // the voice ends when the amplitude envelope is done (the last period still fades to zero)
        if ((ampCur <= 0.0f && ampPrev <= 0.0f) || (killFading && killGain <= 0.0f && ampPrev <= 0.0f))
        {
            setActive (false);
            outL = outR = 0.0f;
            lastLevel = 0.0f;
            return;
        }
    }

    }
    // ---- oscillators
    // the groups of keys this period's matrix touched (V7; the pad pressure layer on the Filter 1 cutoff among them): what the oscillators, the sub and the filters read now
    const uint32_t nowTouched = g.referenceControl ? ~0u : touchedGroups();
    if (fresh)
    {
        for (int o = 0; o < 3; ++o)
        {
            osc[o].start (P (pi.osc[o].phase) * (1.0f / 360.0f), clamp01 (P (pi.osc[o].randomPhase) * 0.01f), rng);
            osc[o].setMorphStagger (WtOsc::morphStaggerSeed (noteSerial, o)); // the morph rebuilds of a chord's voices fall in different periods (WtOsc::setMorphStagger)
        }
        sub.start (P (pi.sub.phase) * 0.01f, 0.0f, rng);
    }
    // a content set that replaced the one a sample-type player reads stops that player
    for (int o = 0; o < 3; ++o)
    {
        SB_SECTION (sections::tickOscParams);
        const OscIdx& oi = pi.osc[o];
        const LibKeys& lk = *libk[o];
        const int declaredType = std::clamp (static_cast<int> (P (oi.type) + 0.5f), 0, 4);
        // docs/ARCHITECTURE.md section 6: the plugin tolerates absence of content (built-in tables) and flags it. A Sample, Granular, Spectral or Multisample oscillator whose file is
        // not there plays its built-in wavetable (oscN.wavetable) instead of silence; missingMask () tells the instance view (bit o: oscillator o has no file). Content present: the type as set.
        const bool fileMissing = declaredType != kOscTypeWavetable && (g.content == nullptr || (declaredType == 4 ? g.content->instrument[o] == nullptr : !g.content->sample[o].valid));
        const int type = fileMissing ? kOscTypeWavetable : declaredType;
        if (fileMissing && P (oi.on) >= 0.5f)
            missingBits |= 1u << o;
        else
            missingBits &= ~(1u << o);
        lib[o].setType (type, fresh);
        const bool fadingFromWt = lib[o].fadingFromWavetable();
        oscOn[o] = P (oi.on) >= 0.5f;
        oscWarpOnly[o] = false;
        float foldSemis = 0.0f;   // Key Fold: the notes outside the key range are played as the note an octave (or more) away that is inside
        if (oscOn[o])
        {
            const int kl = static_cast<int> (g.base[oi.keyLow] + 0.5f), kh = static_cast<int> (g.base[oi.keyHigh] + 0.5f);
            const int vl = static_cast<int> (g.base[oi.velLow] + 0.5f), vh = static_cast<int> (g.base[oi.velHigh] + 0.5f);
            const int v127 = static_cast<int> (vel * 127.0f + 0.5f);
            int key = keyNum;
            if ((key < kl || key > kh) && g.base[oi.keyFold] >= 0.5f)
            {
                key = foldKey (key, kl, kh);
                foldSemis = static_cast<float> (key - keyNum);
            }
            if (key < kl || key > kh || v127 < vl || v127 > vh)
            {
                // outside the ranges: silent; with Key Warp the oscillator keeps running as the source of the warps of the others (it is not routed, see the sources below)
                if (g.base[oi.keyWarp] >= 0.5f)
                    oscWarpOnly[o] = true;
                else
                    oscOn[o] = false;
            }
        }
        OscTick& t = oscTick[o];
        t.jump = g.noSmoothing;
        t.active = oscOn[o];
        if (!oscOn[o])
        {
            // an oscillator that is off: no routing parameters are read (nothing routes it), and no update beyond the minimum
            t.level = 0.0f;
            {
                SB_SECTION (sections::tickOscUpdate);
                // (a silent tick is applied with the next one that is not: WtOsc::deferSilent)
                if (!osc[o].deferSilent (t, g.sampleRate, false, false))
                    osc[o].update (t, g.sampleRate, rng);
                pitchTapOn[o] = false;
                lib[o].updateOff (g.content);
            }
            continue;
        }
        // V7: the part of the oscillator that depends on its keys alone, kept while the epoch is the same and no slot touched its keys (the reference: read every period)
        OscPart& op = oscPart[o];
        {
            const bool untouched = (nowTouched & (1u << (kGroupOsc + o))) == 0;
            if (!(op.ok && untouched && g.baseEpoch != 0 && op.epoch == g.baseEpoch))
            {
                fillOscPart (o, op);
                op.ok = untouched;
                op.epoch = g.baseEpoch;
            }
        }
        oscDest[o] = op.dest;
        oscBlend[o] = op.blend;
        oscSend[o][0] = op.send[0];
        oscSend[o][1] = op.send[1];
        oscEnv1[o] = op.env1;
        const ContentSet* cs = g.content;
        const int wtChoice = op.wtChoice;
        // audio thread: the engine resolved the table of this block (the chosen one when built, else the one the oscillator had: never a substitute)
        t.wt = wtChoice == kWavetablePresetTable ? (cs != nullptr ? cs->userTable[o].get() : nullptr) : (g.wtResolved != nullptr ? g.wtResolved[o] : builtinWavetableIfReady (wtChoice));
        if (t.wt == nullptr)
            t.wt = builtinWavetableIfReady (kWavetableSaw); // the preset table needs its content (a missing file: the saw): the original meaning of the fallback
        // ---- pitch: the coarse term of the pitch mode and the Ratio source
        const bool track = op.track;
        const bool bendTrack = op.bendTrack;
        const float fixedNote = op.fixedNote;   // Fixed Pitch: the note an oscillator without pitch tracking plays; 0 (the default) is the oscillator's own C3
        float base = track ? noteSemis + foldSemis - (bendTrack ? 0.0f : bend) : (fixedNote >= 0.5f ? fixedNote : 60.0f);
        const int pmode = op.pmode;
        const float coarseSemis = op.coarseSemis;
        if (pmode == 2)
        {
            // Ratio SRC: the pitch of another source (as of the last period) replaces the note; a loop, itself or a source that is off is the note
            const int srcIdx = op.ratioSrc; // 0 .. 2 oscillators, 3 sub
            bool ok = srcIdx >= 0 && srcIdx != o && srcIdx < 4 && pitchTapOn[srcIdx];
            for (int s = srcIdx, hops = 0; ok && s < 3; ++hops)
            {
                if (hops >= 4)
                    ok = false;
                else if (g.base[libk[s]->idx[lk_pitchMode]] < 1.5f)
                    break; // the chain ends at a source that plays its own note
                else
                {
                    const int next = static_cast<int> (g.base[libk[s]->idx[lk_ratioSrc]] + 0.5f) - 1;
                    if (next < 0 || next >= 4)
                        break;
                    if (next == o)
                        ok = false;
                    s = next;
                }
            }
            if (ok)
                base = pitchTap[srcIdx];
        }
        t.semis = base + snapTranspose (op.transposeSnap, base, op.transpose) + op.tune001 + coarseSemis + (vc.mask[o] && track ? vc.detuneSemis : 0.0f);
        pitchTap[o] = t.semis;
        pitchTapOn[o] = true;
        t.frame = op.frame;
        t.frameSpread = op.frameSpread;
        t.level = op.level;
        const float vcPan = vc.mask[o] ? vc.pan : 0.0f;
        t.pan = std::clamp (op.pan + vcPan, -1.0f, 1.0f);
        t.panRight = std::clamp (op.panRight + vcPan, -1.0f, 1.0f);
        t.voices = std::clamp (op.voicesRaw, 1, std::max (1, g.maxUnison));
        t.detuneSemis = op.detuneSemis;
        t.curve = op.curve;
        t.blendShape = op.blendShape;
        t.stack = op.stack;
        t.stackRepeat = op.stackRepeat;
        t.mode = op.mode;
        t.spread = op.spread;
        t.blend = op.uniBlend;
        t.law = op.law;
        t.warp[0] = op.warp[0];
        t.warpAmount[0] = op.warpAmount[0];
        t.warpPhase[0] = op.warpPhase[0];
        t.warpSpread[0] = op.warpSpread[0];
        t.warp[1] = op.warp[1];
        t.warpAmount[1] = op.warpAmount[1];
        t.warpPhase[1] = op.warpPhase[1];
        t.warpSpread[1] = op.warpSpread[1];
        t.morphType = op.morphType;
        t.morphAmount = op.morphAmount;
        t.morphSpread = op.morphSpread;
        t.smooth = op.smooth;
        if (op.remapOn)
        {
            // frame position remap (02 6.7): the drawn curve of the slot, then a one pole slew with RISE (up) / FALL (down)
            const int rslot = op.remapSlot;
            float target = t.frame;
            if (cs != nullptr && cs->remap[o][rslot].size() == 257)
            {
                const float xx = clamp01 (t.frame) * 256.0f;
                const int ci = std::min (static_cast<int> (xx), 255);
                target = lerp (cs->remap[o][rslot][static_cast<size_t> (ci)], cs->remap[o][rslot][static_cast<size_t> (ci) + 1], xx - static_cast<float> (ci));
            }
            const float ms = target > remapCur[o] ? op.remapRise : op.remapFall;
            if (fresh || !remapInit[o] || ms <= 0.0f)
                remapCur[o] = target;
            else
                remapCur[o] += (target - remapCur[o]) * (1.0f - std::exp (-fdt * 1000.0f / ms));
            remapInit[o] = true;
            t.frame = clamp01 (remapCur[o]);
        }
        else
            remapInit[o] = false;
        t.oversample = g.oversample;
        t.lowWarp = (g.economy & kEconomyWarp) != 0;
        t.lowMorph = (g.economy & kEconomyMorph) != 0;
        t.fastMath = (g.economy & kEconomyMath) != 0;
        t.scalarWarp = g.scalarWarp;
        t.shareMorph = g.lowShareMorph;
        t.remap = nullptr;
        if (cs != nullptr)
        {
            const int slot = op.remapSlot;
            if (cs->remap[o][slot].size() == 257)
                t.remap = cs->remap[o][slot].data();
        }
        const bool wtRuns = type == kOscTypeWavetable || fadingFromWt;
        if (wtRuns)
        {
            // the cross modulation entries of the list name the other two oscillators in order
            SB_SECTION (sections::tickOscUpdate);
            if (!osc[o].deferSilent (t, g.sampleRate, true, fresh)) // level 0 of this period: the render is exactly zero, the update waits for the next period that is not
            {
                osc[o].update (t, g.sampleRate, rng);
                osc[o].updateMorph (g.sampleRate, fresh, g.morphCache);
            }
        }
        else
        {
            t.active = false;
        }
        if (type != kOscTypeWavetable || lib[o].libraryRunning())
        {
            LibParams lp;
            if (!op.libValid)
            {
                for (int i = 0; i < kLibKeyCount; ++i)
                    op.libV[i] = lk.idx[i] >= 0 ? P (lk.idx[i]) : 0.0f;
                op.libValid = op.ok;
            }
            std::copy (std::begin (op.libV), std::end (op.libV), std::begin (lp.v));
            lp.type = type;
            lp.noteSemis = noteSemis;
            lp.noteRaw = static_cast<float> (keyNum);
            lp.transposeTune = op.transpose + op.tune001;
            lp.pitchTrack = track;
            lp.bendTrack = bendTrack;
            lp.velocity = vel;
            lp.bpm = static_cast<float> (g.bpm);
            lp.hostRate = g.sampleRate;
            lp.quality = g.quality;
            lp.key = keyNum;
            lp.serial = noteSerial;
            lp.switchKey = g.switchKey != nullptr ? g.switchKey[o] : -1;
            lp.scratch = g.spectralScratch;
            lp.referenceKernels = g.scalarContent;
            lp.chokeLatest = g.chokeLatest != nullptr ? g.chokeLatest + o * 256 : nullptr;
            lp.chokeCounter = g.chokeCounter != nullptr ? g.chokeCounter + o : nullptr;
            SB_SECTION (sections::tickOscUpdate);
            lib[o].update (t, lp, cs, o);
        }
    }

    // ---- sub oscillator
    {
        SB_SECTION (sections::tickSub);
        const SubIdx& si = pi.sub;
        // V7: the part of the sub that depends on its keys alone, kept as the oscillators' (the reference: read every period)
        SubPart& sp = subPart;
        const bool subUntouched = (nowTouched & (1u << kGroupSub)) == 0;
        if (!(sp.ok && subUntouched && g.baseEpoch != 0 && sp.epoch == g.baseEpoch))
        {
            sp.on = P (si.on) >= 0.5f;
            sp.dest = static_cast<int> (P (si.destination) + 0.5f);
            sp.blend = si.filterBlend >= 0 ? clamp01 (P (si.filterBlend) * 0.01f) : 0.0f;
            sp.send[0] = si.bus1Send >= 0 ? std::max (0.0f, P (si.bus1Send) * 0.01f) : 0.0f;
            sp.send[1] = si.bus2Send >= 0 ? std::max (0.0f, P (si.bus2Send) * 0.01f) : 0.0f;
            sp.env1 = P (si.env1) >= 0.5f;
            static const int kSubTable[6] = { 0, kRoundedRectTable, 3, 1, 2, 4 };
            sp.table = kSubTable[std::clamp (static_cast<int> (P (si.waveform) + 0.5f), 0, 5)];
            sp.octave12 = std::round (P (si.octave)) * 12.0f;
            sp.tune = P (si.tune);
            sp.level = std::max (0.0f, P (si.level) * 0.01f);
            sp.pan = P (si.pan) * 0.01f;
            sp.ok = subUntouched;
            sp.epoch = g.baseEpoch;
        }
        subCh.on = sp.on;
        subCh.dest = sp.dest;
        subBlend = sp.blend;
        subSend[0] = sp.send[0];
        subSend[1] = sp.send[1];
        subCh.env1 = sp.env1;
        subTick.active = subCh.on;
        subTick.wt = builtinWavetableIfReady (sp.table);
        subTick.semis = noteSemis + sp.octave12 + sp.tune + (vc.mask[4] ? vc.detuneSemis : 0.0f);
        // sub.mono (document 02, 9): only the voice with the lowest note of the sounding voices (within half a semitone) carries the sub; the gate moves the level, which the
        // oscillator ramps across the period
        const float subGate = (si.mono >= 0 && g.base[static_cast<size_t> (si.mono)] >= 0.5f && tunedKey > g.lowestNote + 0.5f) ? 0.0f : 1.0f;
        subTick.level = subCh.on ? sp.level * subGate : 0.0f;
        subTick.pan = std::clamp (sp.pan + (vc.mask[4] ? vc.pan : 0.0f), -1.0f, 1.0f);
        subTick.panRight = subTick.pan;
        subTick.jump = g.noSmoothing;
        subTick.voices = 1;
        subTick.spread = 0.0f;
        subTick.blend = 1.0f;
        subTick.frame = 0.0f;
        if (!sub.deferSilent (subTick, g.sampleRate, false, false))
            sub.update (subTick, g.sampleRate, rng);
        pitchTap[3] = subTick.semis;
        pitchTapOn[3] = subCh.on;
    }
    // ---- noise
    if (!noiseCh.on && noiseGain == 0.0f && noiseGainPrev == 0.0f && P (pi.noise.on) < 0.5f)
    {
        // off and settled: the rest of the block only prepares what an active noise source needs, and it does all of it again in the period in which the source is switched on
    }
    else
    {
        const NoiseIdx& ni = pi.noise;
        noiseCh.on = P (ni.on) >= 0.5f;
        noiseCh.dest = static_cast<int> (P (ni.destination) + 0.5f);
        noiseBlend = ni.filterBlend >= 0 ? clamp01 (P (ni.filterBlend) * 0.01f) : 0.0f;
        noiseSend[0] = ni.bus1Send >= 0 ? std::max (0.0f, P (ni.bus1Send) * 0.01f) : 0.0f;
        noiseSend[1] = ni.bus2Send >= 0 ? std::max (0.0f, P (ni.bus2Send) * 0.01f) : 0.0f;
        noiseCh.env1 = P (ni.env1) >= 0.5f;
        noiseGainPrev = noiseGain;
        noiseGain = noiseCh.on ? std::max (0.0f, P (ni.level) * 0.01f) : 0.0f;
        noisePan = std::clamp (P (ni.pan) * 0.01f + (vc.mask[3] ? vc.pan : 0.0f), -1.0f, 1.0f);
        noiseStereo = clamp01 (P (ni.stereo) * 0.01f);
        noiseColour = std::clamp (static_cast<int> (P (ni.sample) + 0.5f), 0, 8);
        noiseFilterAmount = noiseFilterKey >= 0 ? clamp01 (std::fabs (P (noiseFilterKey) * 0.01f)) * (P (noiseFilterKey) < 0.0f ? -1.0f : 1.0f) : 0.0f;
        noisePitchSemis = P (ni.pitch) + P (ni.fine) * 0.01f + (P (ni.keytrack) >= 0.5f ? noteSemis - 60.0f : 0.0f);
        // noise.type = Sample: a content sample is the source (a missing file plays the generator)
        const ContentSet* ncs = g.content;
        noiseUseSample = noiseCh.on && ni.type >= 0 && P (ni.type) >= 0.5f && ncs != nullptr && ncs->sample[3].valid;
        if (noiseUseSample)
        {
            sampleosc::SampleOscParams sp;
            sp.loopMode = P (ni.bounce) >= 0.5f ? sampleosc::LoopMode::pingPong : (P (ni.loop) >= 0.5f ? sampleosc::LoopMode::forward : sampleosc::LoopMode::none);
            sp.startOffset = clamp01 (P (ni.phase));
            sp.endOffset = 1.0f;
            sp.loopStart = 0.0f;
            sp.loopEnd = 1.0f;
            sp.quality = sampleosc::QualityTier::high;
            if (fresh || noiseBound != ncs)
            {
                noiseBound = ncs;
                noiseSv.reset();
                if (P (ni.randomPhase) >= 0.5f)
                    sp.startOffset = noiseRng.uni();
                noiseSv.noteOn (60, 1.0f, &ncs->sample[3].view, sp);
            }
            noiseSv.setParams (sp);
            noiseSv.setSemitoneOffset (P (ni.pitch) + P (ni.fine) * 0.01f + (P (ni.keytrack) >= 0.5f ? noteSemis - 60.0f : 0.0f));
        }
    }
    // ---- filters
    routing = static_cast<int> (g.base[pi.filterRouting] + 0.5f);
    for (int f = 0; f < 2; ++f)
    {
        SB_SECTION (sections::tickFilterParams);
        const FilterIdx& fi = pi.filter[f];
        filtOn[f] = P (fi.on) >= 0.5f;
        filtEnv1[f] = P (fi.env1) >= 0.5f;
        filtDest[f] = static_cast<int> (P (fi.destination) + 0.5f);
        filtSend[f][0] = fi.bus1Send >= 0 ? std::max (0.0f, P (fi.bus1Send) * 0.01f) : 0.0f;
        filtSend[f][1] = fi.bus2Send >= 0 ? std::max (0.0f, P (fi.bus2Send) * 0.01f) : 0.0f;
        // a filter that is off (and was not just started with the note) is not configured: nothing reads its settings, and when it is switched on the settings
        // of that period are taken twice (the smoothing starts at them, as at a note start)
        if (!filtOn[f] && !fresh)
            filtStale[f] = true;
        else
        {
            FilterSettings& s = filtSet[f];
            fillFilterSettings (s, fi, P, PR, fi.cleanMode >= 0 && g.base[fi.cleanMode] >= 0.5f,
                                keyNum, vc.cutoffSemis, (g.economy & kEconomyMath) != 0);
            s.scalarOnly = g.scalarFilters;
            s.noSmoothing = g.noSmoothing;
            filt[f].set (s);
            if (g.noSmoothing)
                filt[f].set (s); // the same settings twice: the coefficients do not ramp
            if (fresh)
                filt[f].reset(); // the smoothers start at the targets of this note, not at the ones of the note this voice played before
            if (filtStale[f])
            {
                filt[f].set (s);
                filtStale[f] = false;
            }
        }
        filtLevelPrev[f] = filtLevel[f];
        filtLevel[f] = dbToGain (P (fi.level));
        if (fresh || g.noSmoothing)
            filtLevelPrev[f] = filtLevel[f];
    }
    fresh = false;
}

namespace
{
/** The largest magnitude of n samples. */
inline float peakOf (const float* x, int n) noexcept
{
    return peak::peakAbs (x, n);   // NEON where built (dsp/peak.h): the same float as the scalar loop it replaced, NaN passed over
}

/** The first write into a routing buffer (CPU-REVIEW-VOICE-MOD.md V6): d[k] = 0.0f + a[k] * g, lane by lane the operations of simd::addScaled on a buffer of +0 (the same +0 + product, so a
    product of -0 gives +0 as before; NEON: the same vmulq / vaddq). */
inline void firstScaled (float* d, const float* a, float g, int n) noexcept
{
    int k = 0;
#if SB_NEON
    const float32x4_t vg = vdupq_n_f32 (g), zero = vdupq_n_f32 (0.0f);
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (d + k, vaddq_f32 (zero, vmulq_f32 (vld1q_f32 (a + k), vg)));
#endif
    for (; k < n; ++k)
        d[k] = 0.0f + a[k] * g;
}
/** The same for simd::addTo: d[k] = 0.0f + b[k]. */
inline void firstAdd (float* d, const float* b, int n) noexcept
{
    int k = 0;
#if SB_NEON
    const float32x4_t zero = vdupq_n_f32 (0.0f);
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (d + k, vaddq_f32 (zero, vld1q_f32 (b + k)));
#endif
    for (; k < n; ++k)
        d[k] = 0.0f + b[k];
}
} // namespace

void Voice::render (const MixOut& out, int n, int t0) noexcept
{
    if (!active)
        return;
    SB_SECTION (sections::renderOther);
    const float invP = 1.0f / static_cast<float> (kControlPeriod);
    float oscL[3][kControlPeriod], oscR[3][kControlPeriod];
    // the targets: 0 Main, 1 Bus 1, 2 Bus 2, 3 Direct; "E" = through the amplitude envelope (env 1 on), "N" = past it
    // only the targets and filter inputs that something is added to are cleared (usedT / usedFin); the rest is never read
    float tE_L[4][kControlPeriod], tE_R[4][kControlPeriod], tN_L[4][kControlPeriod], tN_R[4][kControlPeriod];
    float fin_L[2][kControlPeriod], fin_R[2][kControlPeriod];
    float fout_L[2][kControlPeriod], fout_R[2][kControlPeriod];
    // the mono outputs of Noise, Sub, Filter 1, Filter 2 of this chunk (the modulators of the warps in the next one). V6: written straight into extMono (nothing reads extMono after the
    // oscillators of this chunk), and only the samples of a source that is off are zeroed; the reference (TickGlobals::referenceControl) fills a zeroed local array and copies it.
    const bool ref = gl->referenceControl;
    float extNextRef[4][kControlPeriod];
    if (ref)
        std::memset (extNextRef, 0, sizeof extNextRef);
    float (*const extNext)[kControlPeriod] = ref ? extNextRef : extMono;
    bool extWritten[2] = {};   // Noise, Sub
    bool usedT[2][4] = {}; // [through env 1][target]
    bool usedFin[2] = {};
    // V6: the first write into a buffer is 0 + a * g (firstScaled), no fill with zeros before it; a buffer read with nothing written into it (a filter input) is zeroed then
    auto touchFin = [&] (int f)
    {
        if (!usedFin[f])
        {
            std::fill (fin_L[f], fin_L[f] + kControlPeriod, 0.0f);
            std::fill (fin_R[f], fin_R[f] + kControlPeriod, 0.0f);
            usedFin[f] = true;
        }
    };

    auto addScaled = [&] (float* dl, float* dr, const float* a, const float* b, float g)
    {
        simd::addScaled (dl, a, g, n);
        simd::addScaled (dr, b, g, n);
    };
    auto toFin = [&] (int f, const float* a, const float* b, float g)
    {
        if (!usedFin[f] && !ref)
        {
            firstScaled (fin_L[f], a, g, n);
            firstScaled (fin_R[f], b, g, n);
            usedFin[f] = true;
            return;
        }
        touchFin (f);
        addScaled (fin_L[f], fin_R[f], a, b, g);
    };
    auto toTarget = [&] (int target, bool env1, const float* a, const float* b, float g)
    {
        float* zl = env1 ? tE_L[target] : tN_L[target];
        float* zr = env1 ? tE_R[target] : tN_R[target];
        if (!usedT[env1][target])
        {
            usedT[env1][target] = true;
            if (!ref)
            {
                firstScaled (zl, a, g, n);
                firstScaled (zr, b, g, n);
                return;
            }
            std::fill (zl, zl + kControlPeriod, 0.0f);
            std::fill (zr, zr + kControlPeriod, 0.0f);
        }
        addScaled (zl, zr, a, b, g);
    };
    // a source: its destination (0 Filter 1, 1 Filter 2, 2 both, 3 Effects only, 4 Direct out, 5 Filter blend, 6 None) and its taps to the buses
    auto route = [&] (int dest, bool env1, const float* a, const float* b, float blend, float send1, float send2)
    {
        switch (dest)
        {
            case 0: toFin (0, a, b, 1.0f); break;
            case 1: toFin (1, a, b, 1.0f); break;
            case 2: toFin (0, a, b, 1.0f); toFin (1, a, b, 1.0f); break;
            case 3: toTarget (0, env1, a, b, 1.0f); break;
            case 4: toTarget (3, env1, a, b, 1.0f); break;
            case 5: // filter blend: an equal power crossfade between the filters (0 = Filter 1 only)
                toFin (0, a, b, std::cos (blend * 1.5707963f));
                toFin (1, a, b, std::sin (blend * 1.5707963f));
                break;
            default: break;
        }
        if (send1 > 0.0f)
            toTarget (1, env1, a, b, send1);
        if (send2 > 0.0f)
            toTarget (2, env1, a, b, send2);
    };

    // ---- sources
    for (int o = 0; o < 3; ++o)
    {
        if (!oscOn[o])
            continue; // nothing reads oscL / oscR of an oscillator that is off (route () is not reached)
        const float* cross[6];
        int c = 0;
        for (int j = 0; j < 3; ++j)
            if (j != o)
                cross[c++] = oscMono[j] + t0;
        for (int j = 0; j < 4; ++j)
            cross[2 + j] = extMono[j] + t0; // Noise, Sub, Filter 1, Filter 2: the last chunk (the filters run after the oscillators)
        float w0 = 1.0f, w1 = 1.0f;
        if (lib[o].type() != kOscTypeWavetable || lib[o].libraryRunning())
            lib[o].wavetableWeights (t0, n, w0, w1);
        if (w0 > 0.0f || w1 > 0.0f)
        {
            osc[o].render (t0, n, oscL[o], oscR[o], cross, gl->sampleRate);
            if (w0 != 1.0f || w1 != 1.0f)
            {
                // (k + 1) / n = (k + 1) * (1 / n) exactly when n is a power of two (see WtOsc::render)
                const bool nPow2 = (n & (n - 1)) == 0;
                const float invN = 1.0f / static_cast<float> (n);
                for (int k = 0; k < n; ++k)
                {
                    const float g = lerp (w0, w1, nPow2 ? static_cast<float> (k + 1) * invN : static_cast<float> (k + 1) / static_cast<float> (n));
                    oscL[o][k] *= g;
                    oscR[o][k] *= g;
                }
            }
        }
        else
        {
            std::fill (oscL[o], oscL[o] + n, 0.0f);
            std::fill (oscR[o], oscR[o] + n, 0.0f);
        }
        if (lib[o].libraryRunning())
        {
            float tl[kControlPeriod], tr[kControlPeriod];
            { SB_SECTION (sections::oscLib); lib[o].render (t0, n, tl, tr, cross); }
            for (int k = 0; k < n; ++k)
            {
                oscL[o][k] += tl[k];
                oscR[o][k] += tr[k];
            }
        }
        simd::halfSum (oscMono[o] + t0, oscL[o], oscR[o], n);
        srcLevel[o] = oscMono[o][t0 + n - 1];
        if (oscWarpOnly[o])
            continue;   // Key Warp: out of its key range the oscillator only warps the others (its signal is in oscMono), it is not routed and not in the mixer strips
        if (out.srcSq != nullptr)
        {
            const float pk = peakOf (oscMono[o] + t0, n);
            out.srcSq[1 + o] += pk * pk;   // the strips of the mixer: Sub 0, A 1, B 2, C 3, Noise 4, Filter 1 5, Filter 2 6
        }
        route (oscDest[o], oscEnv1[o], oscL[o], oscR[o], oscBlend[o], oscSend[o][0], oscSend[o][1]);
    }
    if (subCh.on)
    {
        float sl[kControlPeriod], sr2[kControlPeriod];
        { SB_SECTION (sections::sub); sub.render (t0, n, sl, sr2, nullptr, gl->sampleRate); }
        for (int k = 0; k < n; ++k)
            extNext[1][t0 + k] = 0.5f * (sl[k] + sr2[k]);
        extWritten[1] = true;
        srcLevel[4] = 0.5f * (sl[n - 1] + sr2[n - 1]);
        if (out.srcSq != nullptr)
        {
            const float pk = peakOf (extNext[1] + t0, n);
            out.srcSq[0] += pk * pk;
        }
        route (subCh.dest, subCh.env1, sl, sr2, subBlend, subSend[0], subSend[1]);
    }
    if (noiseCh.on && (noiseGain > 0.0f || noiseGainPrev > 0.0f))
    {
        SB_SECTION (sections::noise);
        // noise.pitch: the generator played at another rate (down: the band shrinks, up: white but quieter) and noise.filter (docs/engine-spec/02 section 10)
        const NoiseCoeffs& nc = noiseCoeffs.get (noisePitchSemis, noiseFilterAmount, gl->sampleRate);
        const float pitchGain = nc.pitchGain, lpC = nc.lpC, fC = nc.fC;
        const float fa = noiseFilterAmount;
        auto shapeNoiseCh = [&] (int ch, float x) noexcept
        {
            float* lp = noiseLp[ch];
            if (lpC < 1.0f)
                for (int i = 0; i < 4; ++i)
                    x = (lp[i] += lpC * (x - lp[i]));
            x *= pitchGain;
            float* f = noiseFilt[ch];
            f[0] += fC * (x - f[0]);
            f[1] += fC * (f[0] - f[1]);
            if (fa < -1.0e-4f)
                return f[1];
            if (fa > 1.0e-4f)
                return x - f[1];
            return x;
        };
        auto shapeNoise = [&] (float& l, float& r) noexcept
        {
            l = shapeNoiseCh (0, l);
            r = shapeNoiseCh (1, r);
        };
        float nl[kControlPeriod], nr[kControlPeriod];
        float gl2, gr2;
        panGains (noisePan, gl2, gr2);
        if (noiseUseSample)
        {
            noiseSv.process (nl, nr, n);
            for (int k = 0; k < n; ++k)
            {
                const float t = static_cast<float> (t0 + k + 1) * invP;
                const float g = lerp (noiseGainPrev, noiseGain, t);
                nl[k] *= g * 2.0f * gl2; // the sample voice's own centre pan law (-3 dB) is undone
                nr[k] *= g * 2.0f * gr2;
            }
        }
        else
        for (int k = 0; k < n; ++k)
        {
            const float t = static_cast<float> (t0 + k + 1) * invP;
            const float g = lerp (noiseGainPrev, noiseGain, t);
            float a = noiseSample (noiseColour, noiseState[0], noiseRng, g, (gl->economy & kEconomyMath) != 0);
            float b = noiseStereo > 0.0f ? lerp (a, noiseSample (noiseColour, noiseState[1], noiseRng, g, (gl->economy & kEconomyMath) != 0), noiseStereo) : a;
            if (noiseColour <= 3)
                shapeNoise (a, b);
            nl[k] = a * g * 1.41421356f * gl2;
            nr[k] = b * g * 1.41421356f * gr2;
        }
        for (int k = 0; k < n; ++k)
            extNext[0][t0 + k] = 0.5f * (nl[k] + nr[k]);
        extWritten[0] = true;
        srcLevel[3] = 0.5f * (nl[n - 1] + nr[n - 1]);
        if (out.srcSq != nullptr)
        {
            const float pk = peakOf (extNext[0] + t0, n);
            out.srcSq[4] += pk * pk;
        }
        route (noiseCh.dest, noiseCh.env1, nl, nr, noiseBlend, noiseSend[0], noiseSend[1]);
    }

    // ---- filters (series routing feeds one into the other)
    auto runFilter = [&] (int f)
    {
        if (!filtOn[f] && !usedFin[f])
        {
            std::fill (extNext[2 + f] + t0, extNext[2 + f] + t0 + n, 0.0f);
            srcLevel[5 + f] = 0.0f;
            return;
        }
        touchFin (f);
        if (filtOn[f])
        {
            SB_SECTION (f == 0 ? sections::filter1 : sections::filter2);
            filt[f].process (fin_L[f], fin_R[f], fout_L[f], fout_R[f], n, t0);
        }
        else
        {
            std::copy (fin_L[f], fin_L[f] + n, fout_L[f]);
            std::copy (fin_R[f], fin_R[f] + n, fout_R[f]);
        }
        {
            float gk[kControlPeriod];
            simd::rampLerp (gk, filtLevelPrev[f], filtLevel[f], t0, n, invP);
            simd::mulBy (fout_L[f], gk, n);
            simd::mulBy (fout_R[f], gk, n);
        }
        simd::halfSum (extNext[2 + f] + t0, fout_L[f], fout_R[f], n);
        srcLevel[5 + f] = 0.5f * (fout_L[f][n - 1] + fout_R[f][n - 1]);
        if (out.srcSq != nullptr)
        {
            const float pk = peakOf (extNext[2 + f] + t0, n);
            out.srcSq[5 + f] += pk * pk;
        }
    };
    auto toMain = [&] (int f, bool suppress)
    {
        if (!usedFin[f])
            return;
        // the output of a filter: Effects only (Main), Direct out or None; the taps to the buses stay whatever the destination is
        if (!suppress && filtDest[f] != 2)
            toTarget (filtDest[f] == 1 ? 3 : 0, filtEnv1[f], fout_L[f], fout_R[f], 1.0f);
        if (filtSend[f][0] > 0.0f)
            toTarget (1, filtEnv1[f], fout_L[f], fout_R[f], filtSend[f][0]);
        if (filtSend[f][1] > 0.0f)
            toTarget (2, filtEnv1[f], fout_L[f], fout_R[f], filtSend[f][1]);
    };
    // series: the output of the first filter is added to the input of the second (the first write into an empty input: 0 + x, V6)
    auto addFin = [&] (int f, const float* l, const float* r)
    {
        if (!usedFin[f] && !ref)
        {
            firstAdd (fin_L[f], l, n);
            firstAdd (fin_R[f], r, n);
            usedFin[f] = true;
            return;
        }
        touchFin (f);
        simd::addTo (fin_L[f], l, n);
        simd::addTo (fin_R[f], r, n);
    };
    if (routing == 2) // series 2 -> 1
    {
        runFilter (1);
        if (usedFin[1])
            addFin (0, fout_L[1], fout_R[1]);
        runFilter (0);
        toMain (0, false);
        toMain (1, true); // the output of the first filter went on into the second: only its taps stay
    }
    else if (routing == 1) // series 1 -> 2
    {
        runFilter (0);
        if (usedFin[0])
            addFin (1, fout_L[0], fout_R[0]);
        runFilter (1);
        toMain (1, false);
        toMain (0, true);
    }
    else
    {
        runFilter (0);
        runFilter (1);
        toMain (0, false);
        toMain (1, false);
    }

    if (ref)
        for (int j = 0; j < 4; ++j)
            std::copy (extNext[j] + t0, extNext[j] + t0 + n, extMono[j] + t0);
    else
        for (int j = 0; j < 2; ++j)
            if (!extWritten[j])
                std::fill (extMono[j] + t0, extMono[j] + t0 + n, 0.0f); // (Filter 1 and 2 are written by runFilter in every routing)
    // ---- amplitude, velocity, declick, fade (only the targets that received something are mixed: the others would add exact zeros)
    SB_SECTION (sections::renderFinal);
    float lastL = 0.0f, lastR = 0.0f;
    float ampK[kControlPeriod], vgK[kControlPeriod];
    simd::rampLerp (ampK, ampPrev, ampCur, t0, n, invP);
    simd::rampLerp (vgK, velGainPrev, velGain, t0, n, invP);
    for (int tg = 0; tg < 4; ++tg)
    {
        const bool ue = usedT[1][tg], un = usedT[0][tg];
        const bool hasDeclick = declickPos > 0 && (declickL[tg] != 0.0f || declickR[tg] != 0.0f);
        if (tg != 0 && !ue && !un && !hasDeclick)
        {
            lastBusL[tg] = lastBusR[tg] = 0.0f;
            continue;
        }
        // the sound of this target for the period: ((E * amp) + N) * velocity gain, left and right
        float ml[kControlPeriod], mr[kControlPeriod];
        auto mixLane = [&] (float* m, const float* e, const float* nn)
        {
            int k = 0;
#if SB_NEON
            const float32x4_t zero = vdupq_n_f32 (0.0f);
            for (; k + 4 <= n; k += 4)
            {
                const float32x4_t ea = ue ? vmulq_f32 (vld1q_f32 (e + k), vld1q_f32 (ampK + k)) : zero;
                const float32x4_t sum = vaddq_f32 (ea, un ? vld1q_f32 (nn + k) : zero);
                vst1q_f32 (m + k, vmulq_f32 (sum, vld1q_f32 (vgK + k)));
            }
#endif
            for (; k < n; ++k)
                m[k] = ((ue ? e[k] * ampK[k] : 0.0f) + (un ? nn[k] : 0.0f)) * vgK[k];
        };
        mixLane (ml, tE_L[tg], tN_L[tg]);
        mixLane (mr, tE_R[tg], tN_R[tg]);
        if (hasDeclick)
        {
            const int dcN = std::min (n, declickPos);
            for (int k = 0; k < dcN; ++k)
            {
                const float w = static_cast<float> (declickPos - k) * (1.0f / 64.0f);
                ml[k] += declickL[tg] * w;
                mr[k] += declickR[tg] * w;
            }
        }
        lastBusL[tg] = ml[n - 1];
        lastBusR[tg] = mr[n - 1];
        if (tg == 0)
        {
            lastL = ml[n - 1];
            lastR = mr[n - 1];
        }
        simd::addScaled (out.l[tg], ml, 1.0f, n); // d += m * 1.0f is d += m exactly
        simd::addScaled (out.r[tg], mr, 1.0f, n);
    }
    declickPos = std::max (0, declickPos - n);
    outL = lastL;
    outR = lastR;
    lastLevel = std::max (std::fabs (lastL), std::fabs (lastR));
}
} // namespace own
} // namespace synthyboy
