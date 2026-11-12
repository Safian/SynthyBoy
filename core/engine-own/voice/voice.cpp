// SynthyBoy - own engine: the voice.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "voice.h"

#include <cstring>

namespace synthyboy
{
namespace own
{
namespace
{

float noiseSample (int colour, float* s, Rng& r, float level) noexcept
{
    // approximately Gaussian white noise (sum of six uniform numbers), level matched to the reference engine by measurement
    const float w = (r.uni() + r.uni() + r.uni() + r.uni() + r.uni() + r.uni() - 3.0f) * 0.7189f;
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
    filt[0].prepare (sampleRate);
    filt[1].prepare (sampleRate);
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

float Voice::PR (int idx) const noexcept
{
    const float b = gl->base[idx];
    const float d = modR[static_cast<size_t> (idx)];
    return d == 0.0f ? b : modulatedValue (paramTable()[idx], destLawOf (static_cast<size_t> (idx)), b, d);
}

float Voice::P (int idx) const noexcept
{
    const float b = gl->base[idx];
    const float d = mod[static_cast<size_t> (idx)];
    return d == 0.0f ? b : modulatedValue (paramTable()[idx], destLawOf (static_cast<size_t> (idx)), b, d);
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
    for (auto& e : env)
        e.noteOn (0.0f);
    clearMod();
    for (auto& st : slotState)
        st = SlotState();
    voiceAge = 0.0f;
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
    osc[0] = WtOsc();
    osc[1] = WtOsc();
    osc[2] = WtOsc();
    sub = WtOsc();
    fresh = true;
}

void Voice::legatoTo (const NoteStart& n) noexcept
{
    sequence = n.sequence;
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

float Voice::lfoValue (int l, const TickGlobals& g, float& valueRight) noexcept
{
    const LfoIdx& li = paramIndex().lfo[l];
    const float dt = static_cast<float> (kControlPeriod / g.sampleRate);
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
        pos -= std::floor (pos);
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
        pos0 = syncType == 0 ? knob - std::floor (knob) : (syncType == 2 ? std::min (knob, 1.0f) : 0.0f);
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
    const CompiledShape* custom = (shape == 12 && g.content != nullptr && g.content->lfo[static_cast<size_t> (l)].present) ? &g.content->lfo[static_cast<size_t> (l)] : nullptr;
    auto shapeAt = [&] (float u, bool wrap) -> float // 0 .. 1
    {
        if (wrap)
            u -= std::floor (u);
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
            tuneTrack += (target - tuneTrack) * (1.0f - std::exp (-fdt / 0.02f));
    }
    const float noteSemis = tuneTrack + glideOffset + bend + tuneExpr + g.base[mi.transpose] + g.base[mi.tune] * 0.01f;
    lastSemis = tuneTrack + glideOffset;

    evaluateVoiceControl (g);
    // ---- envelopes (times read with last period's modulation)
    for (int e = 0; e < 6; ++e)
    {
        const EnvIdx& ei = pi.env[e];
        EnvSettings s;
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
        s.delay *= vc.envFactor;
        s.attack *= vc.envFactor;
        s.hold *= vc.envFactor;
        s.decay *= vc.envFactor;
        s.release *= vc.envFactor;
        if (fresh && e == 0)
        {
            const float v0 = env[0].advance (s, 0.0); // the level at the note's first sample
            envStart = v0 * v0;
        }
        const float v = env[e].advance (s, dt);
        envOut[e] = v * v;
    }
    // ---- LFOs
    for (int l = 0; l < 10; ++l)
        lfoOut[l] = lfoValue (l, g, lfoOutR[l]);
    for (int r = 0; r < 4; ++r)
    {
        const RlfoIdx& ri = pi.rlfo[r];
        const int syncType = static_cast<int> (P (ri.syncType) + 0.5f);
        if (syncType == 1 && g.rlfoValue != nullptr)
            rlfoOut[r] = rlfoOutR[r] = g.rlfoValue[r];
        else
        {
            const int sync = static_cast<int> (P (ri.sync) + 0.5f);
            const float rate = vc.lfoFactor * lfoRateHz (sync, P (ri.rate), static_cast<int> (P (ri.division) + 0.5f), g.bpm, g.concertA, lastSemis, P (ri.keyTranspose), P (ri.keyTune));
            const int style = static_cast<int> (P (ri.style) + 0.5f);
            rlfoOut[r] = rlfo[r].advance (style, rate * fdt);
            // stereo: the right lane has random numbers of its own
            rlfoOutR[r] = (ri.stereo >= 0 && g.base[ri.stereo] >= 0.5f) ? rlfoR[r].advance (style, rate * fdt) : rlfoOut[r];
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
    { const float n12 = (lastSemis + bend) * (1.0f / 12.0f); src[kSrcOctNote] = n12 - std::floor (n12); }
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
        g.matrix->evaluate (ctx);
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
    for (int o = 0; o < 3; ++o)
    {
        const OscIdx& oi = pi.osc[o];
        oscOn[o] = P (oi.on) >= 0.5f && static_cast<int> (P (oi.type) + 0.5f) == kOscTypeWavetable;
        if (oscOn[o])
        {
            const int kl = static_cast<int> (g.base[oi.keyLow] + 0.5f), kh = static_cast<int> (g.base[oi.keyHigh] + 0.5f);
            const int vl = static_cast<int> (g.base[oi.velLow] + 0.5f), vh = static_cast<int> (g.base[oi.velHigh] + 0.5f);
            const int v127 = static_cast<int> (vel * 127.0f + 0.5f);
            if (keyNum < kl || keyNum > kh || v127 < vl || v127 > vh)
                oscOn[o] = false;
        }
        oscDest[o] = static_cast<int> (P (oi.destination) + 0.5f);
        oscEnv1[o] = P (oi.env1) >= 0.5f;
        OscTick& t = oscTick[o];
        t.active = oscOn[o];
        if (!oscOn[o])
        {
            t.level = 0.0f;
            osc[o].update (t, g.sampleRate, rng);
            continue;
        }
        t.wt = builtinWavetable (std::clamp (static_cast<int> (P (oi.wavetable) + 0.5f), 0, kNumWavetableChoices - 1));
        if (t.wt == nullptr)
            t.wt = builtinWavetable (kWavetableSaw); // the preset table needs the content store (not implemented yet): the saw
        const bool track = P (oi.pitchTrack) >= 0.5f;
        const bool bendTrack = P (oi.bendTrack) >= 0.5f;
        const float base = track ? noteSemis - (bendTrack ? 0.0f : bend) : 60.0f;
        t.semis = base + P (oi.transpose) + P (oi.tune) * 0.01f + P (oi.pitchCoarse) + (vc.mask[o] && track ? vc.detuneSemis : 0.0f);
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
        t.warp[0] = static_cast<int> (P (oi.warpType) + 0.5f);
        t.warpAmount[0] = clamp01 (P (oi.warpAmount) * 0.01f);
        t.warpPhase[0] = P (oi.warpPhase) * (1.0f / 360.0f);
        t.warp[1] = static_cast<int> (P (oi.warp2Type) + 0.5f);
        t.warpAmount[1] = clamp01 (P (oi.warp2Amount) * 0.01f);
        t.warpPhase[1] = P (oi.warp2Phase) * (1.0f / 360.0f);
        // the cross modulation entries of the list name the other two oscillators in order
        osc[o].update (t, g.sampleRate, rng);
    }

    // ---- sub oscillator
    {
        const SubIdx& si = pi.sub;
        subCh.on = P (si.on) >= 0.5f;
        subCh.dest = static_cast<int> (P (si.destination) + 0.5f);
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
    }
    // ---- noise
    {
        const NoiseIdx& ni = pi.noise;
        noiseCh.on = P (ni.on) >= 0.5f;
        noiseCh.dest = static_cast<int> (P (ni.destination) + 0.5f);
        noiseCh.env1 = P (ni.env1) >= 0.5f;
        noiseGainPrev = noiseGain;
        noiseGain = noiseCh.on ? std::max (0.0f, P (ni.level) * 0.01f) : 0.0f;
        noisePan = std::clamp (P (ni.pan) * 0.01f + (vc.mask[3] ? vc.pan : 0.0f), -1.0f, 1.0f);
        noiseStereo = clamp01 (P (ni.stereo) * 0.01f);
        noiseColour = std::clamp (static_cast<int> (P (ni.sample) + 0.5f), 0, 8);
    }
    // ---- filters
    routing = static_cast<int> (g.base[pi.filterRouting] + 0.5f);
    for (int f = 0; f < 2; ++f)
    {
        const FilterIdx& fi = pi.filter[f];
        filtOn[f] = P (fi.on) >= 0.5f;
        filtEnv1[f] = P (fi.env1) >= 0.5f;
        filtDest[f] = static_cast<int> (P (fi.destination) + 0.5f);
        FilterSettings& s = filtSet[f];
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
        filtLevelPrev[f] = filtLevel[f];
        filtLevel[f] = dbToGain (P (fi.level));
        if (fresh)
            filtLevelPrev[f] = filtLevel[f];
    }
    fresh = false;
}

void Voice::render (float* L, float* R, int n, int t0) noexcept
{
    if (!active)
        return;
    const float invP = 1.0f / static_cast<float> (kControlPeriod);
    float oscL[3][kControlPeriod], oscR[3][kControlPeriod];
    float mainE_L[kControlPeriod] = {}, mainE_R[kControlPeriod] = {}, mainN_L[kControlPeriod] = {}, mainN_R[kControlPeriod] = {};
    float fin_L[2][kControlPeriod] = {}, fin_R[2][kControlPeriod] = {};
    float fout_L[2][kControlPeriod], fout_R[2][kControlPeriod];

    auto route = [&] (int dest, bool env1, const float* a, const float* b)
    {
        auto add = [&] (float* dl, float* dr)
        {
            for (int k = 0; k < n; ++k)
            {
                dl[k] += a[k];
                dr[k] += b[k];
            }
        };
        switch (dest)
        {
            case 0: case 5: add (fin_L[0], fin_R[0]); break;
            case 1: add (fin_L[1], fin_R[1]); break;
            case 2: add (fin_L[0], fin_R[0]); add (fin_L[1], fin_R[1]); break;
            case 3: case 4: if (env1) add (mainE_L, mainE_R); else add (mainN_L, mainN_R); break;
            default: break;
        }
    };

    // ---- sources
    for (int o = 0; o < 3; ++o)
    {
        if (!oscOn[o])
        {
            std::fill (oscL[o], oscL[o] + n, 0.0f);
            std::fill (oscR[o], oscR[o] + n, 0.0f);
            continue;
        }
        const float* cross[2];
        int c = 0;
        for (int j = 0; j < 3; ++j)
            if (j != o)
                cross[c++] = oscMono[j];
        osc[o].render (t0, n, oscL[o], oscR[o], cross, gl->sampleRate);
        for (int k = 0; k < n; ++k)
            oscMono[o][k] = 0.5f * (oscL[o][k] + oscR[o][k]);
        srcLevel[o] = oscMono[o][n - 1];
        route (oscDest[o], oscEnv1[o], oscL[o], oscR[o]);
    }
    if (subCh.on)
    {
        float sl[kControlPeriod], sr2[kControlPeriod];
        sub.render (t0, n, sl, sr2, nullptr, gl->sampleRate);
        srcLevel[4] = 0.5f * (sl[n - 1] + sr2[n - 1]);
        route (subCh.dest, subCh.env1, sl, sr2);
    }
    if (noiseCh.on && (noiseGain > 0.0f || noiseGainPrev > 0.0f))
    {
        float nl[kControlPeriod], nr[kControlPeriod];
        float gl2, gr2;
        panGains (noisePan, gl2, gr2);
        for (int k = 0; k < n; ++k)
        {
            const float t = static_cast<float> (t0 + k + 1) * invP;
            const float g = lerp (noiseGainPrev, noiseGain, t);
            const float a = noiseSample (noiseColour, noiseState[0], noiseRng, g);
            const float b = noiseStereo > 0.0f ? lerp (a, noiseSample (noiseColour, noiseState[1], noiseRng, g), noiseStereo) : a;
            nl[k] = a * g * 1.41421356f * gl2;
            nr[k] = b * g * 1.41421356f * gr2;
        }
        srcLevel[3] = 0.5f * (nl[n - 1] + nr[n - 1]);
        route (noiseCh.dest, noiseCh.env1, nl, nr);
    }

    // ---- filters (series routing feeds one into the other)
    auto runFilter = [&] (int f)
    {
        if (filtOn[f])
            filt[f].process (fin_L[f], fin_R[f], fout_L[f], fout_R[f], n, t0);
        else
        {
            std::copy (fin_L[f], fin_L[f] + n, fout_L[f]);
            std::copy (fin_R[f], fin_R[f] + n, fout_R[f]);
        }
        for (int k = 0; k < n; ++k)
        {
            const float t = static_cast<float> (t0 + k + 1) * invP;
            const float g = lerp (filtLevelPrev[f], filtLevel[f], t);
            fout_L[f][k] *= g;
            fout_R[f][k] *= g;
        }
        srcLevel[5 + f] = 0.5f * (fout_L[f][n - 1] + fout_R[f][n - 1]);
    };
    auto toMain = [&] (int f, bool suppress)
    {
        if (suppress || filtDest[f] == 2)
            return;
        float* dl = filtEnv1[f] ? mainE_L : mainN_L;
        float* dr = filtEnv1[f] ? mainE_R : mainN_R;
        for (int k = 0; k < n; ++k)
        {
            dl[k] += fout_L[f][k];
            dr[k] += fout_R[f][k];
        }
    };
    if (routing == 2) // series 2 -> 1
    {
        runFilter (1);
        for (int k = 0; k < n; ++k)
        {
            fin_L[0][k] += fout_L[1][k];
            fin_R[0][k] += fout_R[1][k];
        }
        runFilter (0);
        toMain (0, false);
    }
    else if (routing == 1) // series 1 -> 2
    {
        runFilter (0);
        for (int k = 0; k < n; ++k)
        {
            fin_L[1][k] += fout_L[0][k];
            fin_R[1][k] += fout_R[0][k];
        }
        runFilter (1);
        toMain (1, false);
    }
    else
    {
        runFilter (0);
        runFilter (1);
        toMain (0, false);
        toMain (1, false);
    }

    // ---- amplitude, velocity, declick, fade
    float lastL = 0.0f, lastR = 0.0f;
    for (int k = 0; k < n; ++k)
    {
        const float t = static_cast<float> (t0 + k + 1) * invP;
        const float amp = lerp (ampPrev, ampCur, t);
        const float vg = lerp (velGainPrev, velGain, t);
        float l = (mainE_L[k] * amp + mainN_L[k]) * vg;
        float r = (mainE_R[k] * amp + mainN_R[k]) * vg;
        if (declickPos > 0)
        {
            const float w = static_cast<float> (declickPos) * (1.0f / 64.0f);
            l += declickL * w;
            r += declickR * w;
            --declickPos;
        }
        L[k] += l;
        R[k] += r;
        lastL = l;
        lastR = r;
    }
    outL = lastL;
    outR = lastR;
    lastLevel = std::max (std::fabs (lastL), std::fabs (lastR));
}
} // namespace own
} // namespace synthyboy
