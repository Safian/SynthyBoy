// SynthyBoy - own engine: the table positions of the keys the engine reads, resolved once (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "synthyboy/modulation.h"
#include "synthyboy/params.h"

namespace synthyboy
{
namespace own
{
struct OscIdx
{
    int on, level, pan, transpose, tune, wavetable, frame, frameSpread, phase, randomPhase, destination, env1, pitchCoarse, pitchTrack, bendTrack;
    int uniVoices, uniDetune, uniCurve, uniSpread, uniBlend, uniMode, uniRange, uniLaw, uniBlendShape, uniStack, uniStackRepeat;
    int warpType, warpAmount, warpPhase, warpSpread, warp2Type, warp2Amount, warp2Phase, warp2Spread;
    int keyLow, keyHigh, velLow, velHigh, type;
};

struct SubIdx
{
    int on, waveform, octave, tune, level, pan, destination, env1, phase;
};

struct NoiseIdx
{
    int on, level, pan, destination, env1, sample, stereo, keytrack, pitch, fine;
};

struct FilterIdx
{
    int on, model, style, cutoff, resonance, drive, blend, mix, keytrack, var, pan, level, env1, destination, combOffset, formantX, formantY, formantRes, formantSpread, formantTranspose;
};

struct EnvIdx
{
    int delay, attack, hold, decay, sustain, release, attackCurve, decayCurve, releaseCurve;
    int units, legatoInverted, delayBeats, attackBeats, holdBeats, decayBeats, releaseBeats;
};

struct LfoIdx
{
    int shape, rate, sync, division, syncType, phase, fadeIn, delay, smooth, smoothTime, stereo, keyTranspose, keyTune, mono, direction;
};

struct RlfoIdx
{
    int style, rate, sync, division, syncType, stereo, keyTranspose, keyTune;
};

struct MasterIdx
{
    int volume, polyphony, bendRange, velocityTrack, transpose, tune, legato, portTime, portSlope, portAlways, portScaled, bendDownRange, bendDownLink,
        stereoSpread, sameNote, stealPriority, voiceOverride, polyLegato,
        scaleKey, scaleType, scaleMode, tuningA4, tuningScale, tuningMts, mpeOn, mpeBendRange, mpeXyz, mpeYModwheel, mpeBendToX, mpeYBipolar, stereoRouting,
        curveVelLegato, curveNoteLegato;
};

/** Positions in paramTable() of every key the engine reads (-1 when a key does not exist). Built once, immutable. */
struct ParamIndex
{
    OscIdx osc[3];
    SubIdx sub;
    NoiseIdx noise;
    FilterIdx filter[2];
    EnvIdx env[6];
    LfoIdx lfo[10];
    RlfoIdx rlfo[4];
    int macro[8];
    int filterRouting;
    MasterIdx master;
    ModSlotParams slot[kNumModSlots];
    int count = 0; // paramCount ()
};

const ParamIndex& paramIndex() noexcept;
} // namespace own
} // namespace synthyboy
