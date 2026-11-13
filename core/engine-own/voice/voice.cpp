// SynthyBoy - own engine: the voice.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "voice.h"
#include "../dsp/simd.h"
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
} // namespace

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
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
    active = false;
    released = false;
    for (auto& e : env)
        e.kill();
    filt[0].reset();
    filt[1].reset();
    clearMod();
    outL = outR = 0.0f;
    declickL = declickR = 0.0f;
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
}

float Voice::modulatedSlow (int idx, float b, float d) const noexcept
{
    return modulatedValue (paramTable()[idx], destLawOf (static_cast<size_t> (idx)), b, d, gl->quality == 0);
}

void Voice::start (const NoteStart& n, float lastOutL, float lastOutR) noexcept
{
    active = true;
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
    releaseVel = 0.0f;
    tuneExpr = 0.0f;
    polyPressure = 0.0f;
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
    ampPrev = ampCur = 0.0f;
    velGainPrev = velGain = 0.0f;
    std::fill (std::begin (srcLevel), std::end (srcLevel), 0.0f);
    declickL = lastOutL;
    declickR = lastOutR;
    declickPos = (lastOutL != 0.0f || lastOutR != 0.0f) ? 64 : 0;
    for (int o = 0; o < 3; ++o)
        std::fill (std::begin (oscMono[o]), std::end (oscMono[o]), 0.0f);
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
    for (auto& e : env)
        e.noteOff();
    if (gl != nullptr)
        for (int o = 0; o < 3; ++o)
            lib[o].noteOff (P (libKeys (o).idx[lk_sLoopExit]) >= 0.5f);
}

void Voice::kill() noexcept
{
    active = false;
    released = true;
    for (auto& e : env)
        e.kill();
    clearMod();
    outL = outR = 0.0f;
    lastLevel = 0.0f;
}

float Voice::customShape (const CompiledShape& cs, float u) noexcept
{
    if (!cs.anyMod)
        return cs.fixed.evaluate (u);
    // the points move by amount * source (y clamped to 0 .. 1, x between the neighbours, the end points keep x): 13-new-keys.md A45
    auto srcValue = [&] (int choice)
    {
        const float v = src[std::clamp (choice, 0, kNumSourceIds - 1)];
        const bool bipolar = (choice >= kSrcLfo1 && choice < kSrcLfo1 + 8) || (choice >= kSrcRlfo1 && choice < kSrcRlfo1 + 4) || choice == kSrcLfo9 || choice == kSrcLfo10;
        return bipolar ? 2.0f * v - 1.0f : v;
    };
    scratchShape.numPoints = static_cast<size_t> (cs.num);
    for (int i = 0; i < cs.num; ++i)
    {
        const CompiledPoint& p = cs.points[static_cast<size_t> (i)];
        shapes::Point q = p.point;
        if (p.y.source != 0)
            q.y = clamp01 (q.y + p.y.amount * srcValue (p.y.source));
        if (p.x.source != 0 && i > 0 && i + 1 < cs.num)
            q.x = std::clamp (q.x + p.x.amount * srcValue (p.x.source), scratchShape.points[static_cast<size_t> (i - 1)].x, cs.points[static_cast<size_t> (i + 1)].point.x);
        if (p.curve.source != 0)
            q.curve = std::clamp (q.curve + 10.0f * p.curve.amount * srcValue (p.curve.source), -20.0f, 20.0f);
        scratchShape.points[static_cast<size_t> (i)] = q;
    }
    return scratchShape.evaluate (u);
}

float Voice::lfoValue (int l, const TickGlobals& g, float& valueRight, int steps) noexcept
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
        if (wrap)
            u -= ffloor (u);
        else
            u = clamp01 (u);
        u = lfoSwing (u, swing);
        if (dir == 1)
            u = 1.0f - u;
        else if (dir == 2)
            u = 1.0f - std::fabs (2.0f * u - 1.0f);
        if (custom != nullptr)
            return clamp01 (customShape (*custom, u));
        return 0.5f + 0.5f * lfoShape (shape, u, noteSerial + static_cast<uint32_t> (l));
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

void Voice::evaluateVoiceControl (const TickGlobals& g) noexcept
{
    const VoiceCtlIdx& v = paramIndex().vctl;
    if (v.length < 0)
        return;
    const int length = std::clamp (static_cast<int> (g.base[v.length] + 0.5f), 1, 8);
    const int step = static_cast<int> ((sequence - 1u) % static_cast<uint32_t> (length));
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
    const ParamIndex& pi = paramIndex();
    const double dt = kControlPeriod / g.sampleRate;
    const float fdt = static_cast<float> (dt);

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
    if (g.tunedNotes != nullptr)
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
    const float noteSemis = tuneTrack + glideOffset + bend + tuneExpr + g.base[mi.transpose] + g.base[mi.tune] * 0.01f;
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
            envLen[e][0] = s.attack + s.delay, envLen[e][1] = s.hold, envLen[e][2] = s.decay, envLen[e][3] = s.release;
        s.delay *= vc.envFactor;
        s.attack *= vc.envFactor;
        s.hold *= vc.envFactor;
        s.decay *= vc.envFactor;
        s.release *= vc.envFactor;
    };
    // ---- a note starts with its constant sources already in the matrix (document 03 1.3a, document 04: the interpolation of a voice is reset to the new value at a note
    //      start), so an envelope time that a macro or the velocity modulates is read modulated at the very first period
    if (fresh)
    {
        clearMod();
        std::fill (std::begin (src), std::end (src), 0.0f);
        for (int m = 0; m < 8; ++m)
            src[srcMacro (m)] = g.macro[m];
        src[kSrcVelocity] = vel;
        src[kSrcNote] = std::clamp ((lastSemis + bend) * (1.0f / 127.0f), 0.0f, 1.0f);
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
    for (int e = 0; e < 6; ++e)
    {
        EnvSettings s;
        if (envFromCache)
            s = envCache[e];
        else
        {
            envSettings (e, s, true);
            envCache[e] = s;
        }
        if (fresh && e == 0)
        {
            const float v0 = env[0].advance (s, 0.0); // the level at the note's first sample
            envStart = v0 * v0;
        }
        // tier low: an envelope (not the amplitude envelope) that no slot reads is advanced every fourth period (staggered) by four periods
        double stepDt = dt;
        if (g.quality == 0 && !fresh && e > 0 && ((g.usedSources >> srcEnv (e)) & 1u) == 0)
        {
            if (((tickCount + static_cast<uint32_t> (e)) & 3u) != 0u)
                continue; // envOut keeps its last value
            stepDt = 4.0 * dt;
        }
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
    // Tier low (g.quality == 0, "economy"): an LFO that no slot reads only feeds the display, so it is evaluated every fourth period (staggered) over four periods' time
    // instead of every period: the phase and the fade keep their pace, the display gets a value 1.4 ms old at most.
    ++tickCount;
    for (int l = 0; l < 10; ++l)
    {
        if (g.quality == 0 && !fresh && ((g.usedSources >> srcLfo (l)) & 1u) == 0)
        {
            if (((tickCount + static_cast<uint32_t> (l)) & 3u) != 0u)
                continue; // lfoOut / lfoOutR keep their last values
            lfoOut[l] = lfoValue (l, g, lfoOutR[l], 4);
            continue;
        }
        lfoOut[l] = lfoValue (l, g, lfoOutR[l]);
    }
    for (int r = 0; r < 4; ++r)
    {
        // tier low: a random LFO that no slot reads is stepped every fourth period (staggered) by four periods' worth of cycles
        int steps = 1;
        if (g.quality == 0 && !fresh && ((g.usedSources >> srcRlfo (r)) & 1u) == 0)
        {
            if (((tickCount + static_cast<uint32_t> (r)) & 3u) != 0u)
                continue; // rlfoOut / rlfoOutR keep their last values
            steps = 4;
        }
        const float fdtS = fdt * static_cast<float> (steps);
        const RlfoIdx& ri = pi.rlfo[r];
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
    }

    }
    // ---- sources
    std::fill (std::begin (src), std::end (src), 0.0f);
    for (int e = 0; e < 6; ++e)
        src[srcEnv (e)] = envOut[e];
    for (int l = 0; l < 10; ++l)
        src[srcLfo (l)] = 0.5f + 0.5f * lfoOut[l];
    for (int r = 0; r < 4; ++r)
        src[srcRlfo (r)] = 0.5f + 0.5f * rlfoOut[r];
    for (int m = 0; m < 8; ++m)
        src[srcMacro (m)] = g.macro[m];
    src[kSrcVelocity] = vel;
    src[kSrcNote] = std::clamp ((lastSemis + bend) * (1.0f / 127.0f), 0.0f, 1.0f);
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
        { SB_SECTION (sections::tickMatrix); g.matrix->evaluate (ctx); }
    }

    // ---- amplitude: envelope 1, velocity
    float killGain = 1.0f;
    if (killFading)
    {
        killElapsed += fdt;
        killGain = std::max (0.0f, 1.0f - killElapsed * (1.0f / 0.05f)); // linear fade of the envelope value in 50 ms (document 01, 3.2)
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
            active = false;
            outL = outR = 0.0f;
            lastLevel = 0.0f;
            return;
        }
    }

    // ---- oscillators
    if (fresh)
    {
        for (int o = 0; o < 3; ++o)
            osc[o].start (P (pi.osc[o].phase) * (1.0f / 360.0f), clamp01 (P (pi.osc[o].randomPhase) * 0.01f), rng);
        sub.start (P (pi.sub.phase) * 0.01f, 0.0f, rng);
    }
    // a content set that replaced the one a sample-type player reads stops that player
    for (int o = 0; o < 3; ++o)
    {
        SB_SECTION (sections::tickOscParams);
        const OscIdx& oi = pi.osc[o];
        const LibKeys& lk = libKeys (o);
        const int type = std::clamp (static_cast<int> (P (oi.type) + 0.5f), 0, 4);
        lib[o].setType (type, fresh);
        const bool fadingFromWt = lib[o].fadingFromWavetable();
        oscOn[o] = P (oi.on) >= 0.5f;
        if (oscOn[o])
        {
            const int kl = static_cast<int> (g.base[oi.keyLow] + 0.5f), kh = static_cast<int> (g.base[oi.keyHigh] + 0.5f);
            const int vl = static_cast<int> (g.base[oi.velLow] + 0.5f), vh = static_cast<int> (g.base[oi.velHigh] + 0.5f);
            const int v127 = static_cast<int> (vel * 127.0f + 0.5f);
            if (keyNum < kl || keyNum > kh || v127 < vl || v127 > vh)
                oscOn[o] = false;
        }
        oscDest[o] = static_cast<int> (P (oi.destination) + 0.5f);
        oscBlend[o] = oi.filterBlend >= 0 ? clamp01 (P (oi.filterBlend) * 0.01f) : 0.0f;
        oscSend[o][0] = oi.bus1Send >= 0 ? std::max (0.0f, P (oi.bus1Send) * 0.01f) : 0.0f;
        oscSend[o][1] = oi.bus2Send >= 0 ? std::max (0.0f, P (oi.bus2Send) * 0.01f) : 0.0f;
        oscEnv1[o] = P (oi.env1) >= 0.5f;
        OscTick& t = oscTick[o];
        t.active = oscOn[o];
        if (!oscOn[o])
        {
            t.level = 0.0f;
            {
                SB_SECTION (sections::tickOscUpdate);
                osc[o].update (t, g.sampleRate, rng);
                pitchTapOn[o] = false;
                lib[o].updateOff (g.content);
            }
            continue;
        }
        const ContentSet* cs = g.content;
        int wtChoice = std::clamp (static_cast<int> (P (oi.wavetable) + 0.5f), 0, kNumWavetableChoices - 1);
        t.wt = wtChoice == kWavetablePresetTable ? (cs != nullptr ? cs->userTable[o].get() : nullptr) : builtinWavetable (wtChoice);
        if (t.wt == nullptr)
            t.wt = builtinWavetable (kWavetableSaw); // the preset table needs its content (a missing file: the saw)
        // ---- pitch: the coarse term of the pitch mode and the Ratio source
        const bool track = P (oi.pitchTrack) >= 0.5f;
        const bool bendTrack = P (oi.bendTrack) >= 0.5f;
        float base = track ? noteSemis - (bendTrack ? 0.0f : bend) : 60.0f;
        const int pmode = static_cast<int> (P (lk.idx[lk_pitchMode]) + 0.5f);
        const float coarse = P (oi.pitchCoarse);
        float coarseSemis = coarse;
        if (pmode == 1)
            coarseSemis = 12.0f * std::log2 (std::max (1.0f, 1.0f + ffloor (coarse)));
        else if (pmode == 2)
            coarseSemis = coarse >= 0.0f ? 12.0f * std::log2 (1.0f + coarse) : 12.0f * std::log2 (1.0f / (1.0f - coarse));
        if (pmode == 2)
        {
            // Ratio SRC: the pitch of another source (as of the last period) replaces the note; a loop, itself or a source that is off is the note
            const int srcIdx = static_cast<int> (P (lk.idx[lk_ratioSrc]) + 0.5f) - 1; // 0 .. 2 oscillators, 3 sub
            bool ok = srcIdx >= 0 && srcIdx != o && srcIdx < 4 && pitchTapOn[srcIdx];
            for (int s = srcIdx, hops = 0; ok && s < 3; ++hops)
            {
                if (hops >= 4)
                    ok = false;
                else if (g.base[libKeys (s).idx[lk_pitchMode]] < 1.5f)
                    break; // the chain ends at a source that plays its own note
                else
                {
                    const int next = static_cast<int> (g.base[libKeys (s).idx[lk_ratioSrc]] + 0.5f) - 1;
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
        t.semis = base + P (oi.transpose) + P (oi.tune) * 0.01f + coarseSemis + (vc.mask[o] && track ? vc.detuneSemis : 0.0f);
        pitchTap[o] = t.semis;
        pitchTapOn[o] = true;
        t.frame = clamp01 (P (oi.frame) * 0.01f);
        t.frameSpread = std::clamp (P (oi.frameSpread) * 0.01f, -1.0f, 1.0f);
        t.level = std::max (0.0f, P (oi.level) * 0.01f);
        const float vcPan = vc.mask[o] ? vc.pan : 0.0f;
        t.pan = std::clamp (P (oi.pan) * 0.01f + vcPan, -1.0f, 1.0f);
        t.panRight = std::clamp (PR (oi.pan) * 0.01f + vcPan, -1.0f, 1.0f);
        t.voices = std::clamp (static_cast<int> (P (oi.uniVoices) + 0.5f), 1, std::max (1, g.maxUnison));
        const float detune = clamp01 (P (oi.uniDetune) * 0.01f);
        t.detuneSemis = detune * std::max (P (oi.uniRange), 0.0f);
        t.curve = P (oi.uniCurve);
        t.blendShape = std::clamp (P (oi.uniBlendShape) * 0.01f, -1.0f, 1.0f);
        t.stack = static_cast<int> (P (oi.uniStack) + 0.5f);
        t.stackRepeat = static_cast<int> (P (oi.uniStackRepeat) + 0.5f);
        t.mode = static_cast<int> (P (oi.uniMode) + 0.5f);
        t.spread = clamp01 (P (oi.uniSpread) * 0.01f) * clamp01 (g.base[mi.stereoSpread] * 0.01f);
        t.blend = clamp01 (P (oi.uniBlend) * 0.01f);
        t.law = oi.uniLaw >= 0 ? static_cast<int> (g.base[oi.uniLaw] + 0.5f) : 0;
        t.warp[0] = static_cast<int> (P (oi.warpType) + 0.5f);
        t.warpAmount[0] = clamp01 (P (oi.warpAmount) * 0.01f);
        t.warpPhase[0] = P (oi.warpPhase) * (1.0f / 360.0f);
        t.warp[1] = static_cast<int> (P (oi.warp2Type) + 0.5f);
        t.warpAmount[1] = clamp01 (P (oi.warp2Amount) * 0.01f);
        t.warpPhase[1] = P (oi.warp2Phase) * (1.0f / 360.0f);
        t.morphType = static_cast<int> (P (lk.idx[lk_morphType]) + 0.5f);
        t.morphAmount = clamp01 (P (lk.idx[lk_morphAmount]) * 0.01f);
        t.morphSpread = std::clamp (P (lk.idx[lk_morphSpread]) * 0.01f, -1.0f, 1.0f);
        t.smooth = P (lk.idx[lk_smooth]) >= 0.5f;
        if (P (lk.idx[lk_remapOn]) >= 0.5f)
        {
            // frame position remap (02 6.7): the drawn curve of the slot, then a one pole slew with RISE (up) / FALL (down)
            const int rslot = std::clamp (static_cast<int> (P (lk.idx[lk_remapSlot]) + 0.5f), 0, kNumRemapSlots - 1);
            float target = t.frame;
            if (cs != nullptr && cs->remap[o][rslot].size() == 257)
            {
                const float xx = clamp01 (t.frame) * 256.0f;
                const int ci = std::min (static_cast<int> (xx), 255);
                target = lerp (cs->remap[o][rslot][static_cast<size_t> (ci)], cs->remap[o][rslot][static_cast<size_t> (ci) + 1], xx - static_cast<float> (ci));
            }
            const float ms = target > remapCur[o] ? P (lk.idx[lk_remapRise]) : P (lk.idx[lk_remapFall]);
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
        t.lowCost = g.quality == 0;
        t.shareMorph = g.lowShareMorph;
        t.remap = nullptr;
        if (cs != nullptr)
        {
            const int slot = std::clamp (static_cast<int> (P (lk.idx[lk_remapSlot]) + 0.5f), 0, kNumRemapSlots - 1);
            if (cs->remap[o][slot].size() == 257)
                t.remap = cs->remap[o][slot].data();
        }
        const bool wtRuns = type == kOscTypeWavetable || fadingFromWt;
        if (wtRuns)
        {
            // the cross modulation entries of the list name the other two oscillators in order
            SB_SECTION (sections::tickOscUpdate);
            osc[o].update (t, g.sampleRate, rng);
            osc[o].updateMorph (g.sampleRate, fresh);
        }
        else
        {
            t.active = false;
        }
        if (type != kOscTypeWavetable || lib[o].libraryRunning())
        {
            LibParams lp;
            for (int i = 0; i < kLibKeyCount; ++i)
                lp.v[i] = lk.idx[i] >= 0 ? P (lk.idx[i]) : 0.0f;
            lp.type = type;
            lp.noteSemis = noteSemis;
            lp.noteRaw = static_cast<float> (keyNum);
            lp.transposeTune = P (oi.transpose) + P (oi.tune) * 0.01f;
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
            lp.chokeLatest = g.chokeLatest != nullptr ? g.chokeLatest + o * 256 : nullptr;
            lp.chokeCounter = g.chokeCounter != nullptr ? g.chokeCounter + o : nullptr;
            SB_SECTION (sections::tickOscUpdate);
            lib[o].update (t, lp, cs, o);
        }
    }

    // ---- sub oscillator
    {
        const SubIdx& si = pi.sub;
        subCh.on = P (si.on) >= 0.5f;
        subCh.dest = static_cast<int> (P (si.destination) + 0.5f);
        subBlend = si.filterBlend >= 0 ? clamp01 (P (si.filterBlend) * 0.01f) : 0.0f;
        subSend[0] = si.bus1Send >= 0 ? std::max (0.0f, P (si.bus1Send) * 0.01f) : 0.0f;
        subSend[1] = si.bus2Send >= 0 ? std::max (0.0f, P (si.bus2Send) * 0.01f) : 0.0f;
        subCh.env1 = P (si.env1) >= 0.5f;
        subTick.active = subCh.on;
        static const int kSubTable[6] = { 0, kRoundedRectTable, 3, 1, 2, 4 };
        subTick.wt = builtinWavetable (kSubTable[std::clamp (static_cast<int> (P (si.waveform) + 0.5f), 0, 5)]);
        subTick.semis = noteSemis + std::round (P (si.octave)) * 12.0f + P (si.tune) + (vc.mask[4] ? vc.detuneSemis : 0.0f);
        subTick.level = subCh.on ? std::max (0.0f, P (si.level) * 0.01f) : 0.0f;
        subTick.pan = std::clamp (P (si.pan) * 0.01f + (vc.mask[4] ? vc.pan : 0.0f), -1.0f, 1.0f);
        subTick.voices = 1;
        subTick.spread = 0.0f;
        subTick.blend = 1.0f;
        subTick.frame = 0.0f;
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
        static const int kNoiseFilterKey = findParamIndex (hashKey ("noise.filter"));
        noiseFilterAmount = kNoiseFilterKey >= 0 ? clamp01 (std::fabs (P (kNoiseFilterKey) * 0.01f)) * (P (kNoiseFilterKey) < 0.0f ? -1.0f : 1.0f) : 0.0f;
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
            s.fast = g.quality == 0;
            s.model = static_cast<int> (P (fi.model) + 0.5f);
            s.style = static_cast<int> (P (fi.style) + 0.5f);
            const float kt = semitonesToRatio ((static_cast<float> (keyNum) - 60.0f) * P (fi.keytrack) * 0.01f);
            const float vcCut = semitonesToRatio (vc.cutoffSemis);
            const float panShift = fi.pan >= 0 ? P (fi.pan) * 0.01f : 0.0f; // +-100 % = +-12 semitones, opposite in the two channels (document 05, 1.2)
            s.cutoffHz = P (fi.cutoff) * kt * vcCut * std::exp2 (-panShift);
            s.cutoffHzR = PR (fi.cutoff) * kt * vcCut * std::exp2 (panShift);
            s.clean = fi.cleanMode >= 0 && g.base[fi.cleanMode] >= 0.5f;
            s.resonance = P (fi.resonance) * 0.01f;
            s.drive = P (fi.drive) * (1.0f / 20.0f);
            s.driveDb = P (fi.drive);
            s.combOffset = P (fi.combOffset);
            s.formantX = P (fi.formantX) * 0.01f;
            s.formantY = P (fi.formantY) * 0.01f;
            s.formantRes = P (fi.formantRes) * 0.01f;
            s.formantSpread = P (fi.formantSpread) * 0.01f;
            s.formantTranspose = P (fi.formantTranspose);
            s.blend = P (fi.blend) * 0.01f;
            s.mix = P (fi.mix) * 0.01f;
            s.var = P (fi.var) * 0.01f;
            filt[f].set (s);
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
        if (fresh)
            filtLevelPrev[f] = filtLevel[f];
    }
    fresh = false;
}

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
    float extNext[4][kControlPeriod] = {};
    float fout_L[2][kControlPeriod], fout_R[2][kControlPeriod];
    bool usedT[2][4] = {}; // [through env 1][target]
    bool usedFin[2] = {};
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
    auto toTarget = [&] (int target, bool env1, const float* a, const float* b, float g)
    {
        if (!usedT[env1][target])
        {
            float* zl = env1 ? tE_L[target] : tN_L[target];
            float* zr = env1 ? tE_R[target] : tN_R[target];
            std::fill (zl, zl + kControlPeriod, 0.0f);
            std::fill (zr, zr + kControlPeriod, 0.0f);
            usedT[env1][target] = true;
        }
        addScaled (env1 ? tE_L[target] : tN_L[target], env1 ? tE_R[target] : tN_R[target], a, b, g);
    };
    // a source: its destination (0 Filter 1, 1 Filter 2, 2 both, 3 Effects only, 4 Direct out, 5 Filter blend, 6 None) and its taps to the buses
    auto route = [&] (int dest, bool env1, const float* a, const float* b, float blend, float send1, float send2)
    {
        switch (dest)
        {
            case 0: touchFin (0); addScaled (fin_L[0], fin_R[0], a, b, 1.0f); break;
            case 1: touchFin (1); addScaled (fin_L[1], fin_R[1], a, b, 1.0f); break;
            case 2: touchFin (0); touchFin (1); addScaled (fin_L[0], fin_R[0], a, b, 1.0f); addScaled (fin_L[1], fin_R[1], a, b, 1.0f); break;
            case 3: toTarget (0, env1, a, b, 1.0f); break;
            case 4: toTarget (3, env1, a, b, 1.0f); break;
            case 5: // filter blend: an equal power crossfade between the filters (0 = Filter 1 only)
                touchFin (0);
                touchFin (1);
                addScaled (fin_L[0], fin_R[0], a, b, std::cos (blend * 1.5707963f));
                addScaled (fin_L[1], fin_R[1], a, b, std::sin (blend * 1.5707963f));
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
                cross[c++] = oscMono[j];
        for (int j = 0; j < 4; ++j)
            cross[2 + j] = extMono[j]; // Noise, Sub, Filter 1, Filter 2: the last chunk (the filters run after the oscillators)
        float w0 = 1.0f, w1 = 1.0f;
        if (lib[o].type() != kOscTypeWavetable || lib[o].libraryRunning())
            lib[o].wavetableWeights (t0, n, w0, w1);
        if (w0 > 0.0f || w1 > 0.0f)
        {
            osc[o].render (t0, n, oscL[o], oscR[o], cross, gl->sampleRate);
            if (w0 != 1.0f || w1 != 1.0f)
                for (int k = 0; k < n; ++k)
                {
                    const float g = lerp (w0, w1, static_cast<float> (k + 1) / static_cast<float> (n));
                    oscL[o][k] *= g;
                    oscR[o][k] *= g;
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
        simd::halfSum (oscMono[o], oscL[o], oscR[o], n);
        srcLevel[o] = oscMono[o][n - 1];
        route (oscDest[o], oscEnv1[o], oscL[o], oscR[o], oscBlend[o], oscSend[o][0], oscSend[o][1]);
    }
    if (subCh.on)
    {
        float sl[kControlPeriod], sr2[kControlPeriod];
        { SB_SECTION (sections::sub); sub.render (t0, n, sl, sr2, nullptr, gl->sampleRate); }
        for (int k = 0; k < n; ++k)
            extNext[1][k] = 0.5f * (sl[k] + sr2[k]);
        srcLevel[4] = 0.5f * (sl[n - 1] + sr2[n - 1]);
        route (subCh.dest, subCh.env1, sl, sr2, subBlend, subSend[0], subSend[1]);
    }
    if (noiseCh.on && (noiseGain > 0.0f || noiseGainPrev > 0.0f))
    {
        SB_SECTION (sections::noise);
        // noise.pitch: the generator played at another rate (down: the band shrinks, up: white but quieter) and noise.filter (docs/engine-spec/02 section 10)
        const float rate = std::exp2 (noisePitchSemis * (1.0f / 12.0f));
        const float oct = std::log2 (std::max (rate, 1.0e-3f));
        const float pitchGain = oct <= 0.0f ? 1.0f : (oct <= 1.0f ? lerp (1.0f, 0.757f, oct) : (oct <= 2.0f ? lerp (0.757f, 0.628f, oct - 1.0f) : 0.628f * std::pow (0.83f, oct - 2.0f)));
        const float lpC = rate < 0.999f ? 1.0f - std::exp (-kTwoPif * std::min (0.45f, 0.4f * rate) * 1.0f) : 1.0f;
        const float fa = noiseFilterAmount;
        const float fHz = fa < 0.0f ? 20000.0f * std::pow (0.002f, -fa) : 20.0f * std::pow (800.0f, std::max (fa, 0.0f));
        const float fC = std::min (0.99f, 1.0f - std::exp (-kTwoPif * std::min (fHz, 0.45f * static_cast<float> (gl->sampleRate)) / static_cast<float> (gl->sampleRate)));
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
            float a = noiseSample (noiseColour, noiseState[0], noiseRng, g, gl->quality == 0);
            float b = noiseStereo > 0.0f ? lerp (a, noiseSample (noiseColour, noiseState[1], noiseRng, g, gl->quality == 0), noiseStereo) : a;
            if (noiseColour <= 3)
                shapeNoise (a, b);
            nl[k] = a * g * 1.41421356f * gl2;
            nr[k] = b * g * 1.41421356f * gr2;
        }
        for (int k = 0; k < n; ++k)
            extNext[0][k] = 0.5f * (nl[k] + nr[k]);
        srcLevel[3] = 0.5f * (nl[n - 1] + nr[n - 1]);
        route (noiseCh.dest, noiseCh.env1, nl, nr, noiseBlend, noiseSend[0], noiseSend[1]);
    }

    // ---- filters (series routing feeds one into the other)
    auto runFilter = [&] (int f)
    {
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
        simd::halfSum (extNext[2 + f], fout_L[f], fout_R[f], n);
        srcLevel[5 + f] = 0.5f * (fout_L[f][n - 1] + fout_R[f][n - 1]);
    };
    auto toMain = [&] (int f, bool suppress)
    {
        // the output of a filter: Effects only (Main), Direct out or None; the taps to the buses stay whatever the destination is
        if (!suppress && filtDest[f] != 2)
            toTarget (filtDest[f] == 1 ? 3 : 0, filtEnv1[f], fout_L[f], fout_R[f], 1.0f);
        if (filtSend[f][0] > 0.0f)
            toTarget (1, filtEnv1[f], fout_L[f], fout_R[f], filtSend[f][0]);
        if (filtSend[f][1] > 0.0f)
            toTarget (2, filtEnv1[f], fout_L[f], fout_R[f], filtSend[f][1]);
    };
    if (routing == 2) // series 2 -> 1
    {
        runFilter (1);
        touchFin (0);
        simd::addTo (fin_L[0], fout_L[1], n);
        simd::addTo (fin_R[0], fout_R[1], n);
        runFilter (0);
        toMain (0, false);
        toMain (1, true); // the output of the first filter went on into the second: only its taps stay
    }
    else if (routing == 1) // series 1 -> 2
    {
        runFilter (0);
        touchFin (1);
        simd::addTo (fin_L[1], fout_L[0], n);
        simd::addTo (fin_R[1], fout_R[0], n);
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

    for (int j = 0; j < 4; ++j)
    {
        std::copy (extNext[j], extNext[j] + n, extMono[j]);
        std::fill (extNext[j] + n, extNext[j] + kControlPeriod, 0.0f);
    }
    // ---- amplitude, velocity, declick, fade (only the targets that received something are mixed: the others would add exact zeros)
    SB_SECTION (sections::renderFinal);
    float lastL = 0.0f, lastR = 0.0f;
    float ampK[kControlPeriod], vgK[kControlPeriod];
    simd::rampLerp (ampK, ampPrev, ampCur, t0, n, invP);
    simd::rampLerp (vgK, velGainPrev, velGain, t0, n, invP);
    for (int tg = 0; tg < 4; ++tg)
    {
        const bool ue = usedT[1][tg], un = usedT[0][tg];
        if (tg != 0 && !ue && !un)
            continue;
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
        if (tg == 0)
        {
            // the declick tail of the note this voice played before fades out over the first samples
            for (int k = 0; k < n && declickPos > 0; ++k)
            {
                const float w = static_cast<float> (declickPos) * (1.0f / 64.0f);
                ml[k] += declickL * w;
                mr[k] += declickR * w;
                --declickPos;
            }
            lastL = ml[n - 1];
            lastR = mr[n - 1];
        }
        simd::addScaled (out.l[tg], ml, 1.0f, n); // d += m * 1.0f is d += m exactly
        simd::addScaled (out.r[tg], mr, 1.0f, n);
    }
    outL = lastL;
    outR = lastR;
    lastLevel = std::max (std::fabs (lastL), std::fabs (lastR));
}
} // namespace own
} // namespace synthyboy
