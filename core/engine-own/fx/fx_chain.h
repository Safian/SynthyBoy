// SynthyBoy - own engine: the effect chain of the master bus (order fx.order.1 .. 16, on / off per module).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>

#include "../matrix/mod_matrix.h"
#include "fx_effects.h"
#include "synthyboy/fx/hyper_dimension.h"
#include "synthyboy/fx/utility.h"
#include "synthyboy/quality.h"

namespace synthyboy
{
namespace own
{
/** Positions in paramTable() of the effect keys (built once). */
struct FxIndex
{
    int order[16];
    int on[16]; // by module (the fx.order choices 0 .. 15); -1 for modules without an on key
    int level[16];
    // chorus
    int chMix, chRate, chSync, chDivision, chDepth, chFeedback, chDelay1, chDelay2, chVoices, chCutoff, chSpread;
    int cpMix, cpAttack, cpRelease, cpBands, cpXLow, cpXHigh, cpBand[3][5]; // lower thr, upper thr, lower ratio, upper ratio, gain
    int dlMix, dlFeedback, dlTime, dlTime2, dlSync, dlDivision, dlDivision2, dlStyle, dlCutoff, dlSpread;
    int diType, diDrive, diMix, diOrder, diCutoff, diRes, diBlend;
    int eqBand[3][4]; // mode, cutoff, gain, resonance (low, mid, high)
    int eqMid2[4];
    int flMix, flFeedback, flRate, flSync, flDivision, flDepth, flCenter, flPhase;
    int phMix, phFeedback, phRate, phSync, phDivision, phDepth, phCenter, phBlend, phPhase;
    int rvMix, rvSize, rvDecay, rvPre, rvLowCut, rvHighCut, rvHsCut, rvHsGain, rvLsCut, rvLsGain, rvChAmount, rvChRate, rvType, rvWidth, rvDiffA, rvDiffB;
    int hdRate, hdUnison, hdDetune, hdMix, hdDimSize, hdDimMix, hdLevel;
    int utPolL, utPolR, utLpf, utHpf, utMonoBass, utMonoFreq, utWidth, utPan, utMix, utLevel;
    int ffOn, ffModel, ffStyle, ffCutoff, ffRes, ffDrive, ffBlend, ffMix, ffVar, ffLevel;
};
const FxIndex& fxIndex() noexcept;

class FxChain
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    /** Reads the parameters (once per control period). `delta` is the global modulation in control units (may be zero), `bpm` the host tempo. */
    void update (const float* base, const float* delta, double bpm, uint32_t fxMask, int periodSamples) noexcept;
    void process (float* l, float* r, int n) noexcept;
    bool anyActive() const noexcept { return active; }

private:
    float G (const float* base, const float* delta, int idx) const noexcept;
    float lfoRate (const float* base, const float* delta, int rateIdx, int syncIdx, int divIdx, double bpm) const noexcept;

    double sr = 48000.0;
    bool active = false;
    int order[16] = {};
    bool on[16] = {};
    float levelGain[16] = {};
    float levelGainPrev[16] = {};
    Chorus chorus;
    Compressor compressor;
    Delay delay;
    Distortion distortion;
    Equalizer eq;
    FilterUnit fxFilter;
    float fxFilterMix = 1.0f;
    Flanger flanger;
    Phaser phaser;
    Reverb reverb;
    fx::HyperDimension hyper;
    fx::Utility utility;
    int period = 32;
};
} // namespace own
} // namespace synthyboy
