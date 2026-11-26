// SynthyBoy - core tests: the FROZEN REFERENCE of the phaser before the CPU review items 5 and 8 (2026-11-26): Phaser as it was at 391febd (the refresh of the stage and feedback
// coefficients computed every time, the 24 stage states guarded against denormals per stage and sample). Test only; never edit it (test_fxopt_cpu_phaser.cpp compares the Phaser with it).
// Namespace synthyboy::own::cpuref.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "../../engine-own/fx/fx_effects.h"

namespace synthyboy
{
namespace own
{
namespace cpuref
{
class PhaserBefore
{
public:
    bool fast = false; // tier low: tanh, sin, exp, exp2 and pow by the cheaper forms of dsp/fastmath.h
    static constexpr int kStages = 12;
    struct Params { float mix = 0.5f, feedback = 0.5f, rate = 0.12f, depthSemis = 24.0f, centerHz = 2000.0f, blend = 0.5f, phaseOffset = 120.0f / 360.0f, depth2 = 0.0f; };
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept { for (auto& c : z) for (auto& x : c) x = 0.0f; phase = 0.0f; fb[0] = fb[1] = 0.0f; for (auto& x : bpA) x = 0.0f; for (auto& x : bpB) x = 0.0f; }
    /** The LFO runs on while the module is skipped on silence (FxChain, tier low): the same per sample steps as process (). */
    void idle (int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float inc = p.rate / sr;
        for (int s = 0; s < n; ++s)
        {
            phase += inc;
            phase -= ffloor (phase);
        }
    }
    void set (const Params& np) noexcept { p = np; }
    /** The phaser of document 06, 8: the twelve stage all-pass filter of document 05, 7 (taps after 4, 8 and 12 stages by the blend), fed with a cutoff that follows a
        triangle LFO (the right channel offset by the phase offset), a band passed feedback with tanh, output (x + all-pass) / 2, linear dry / wet. */
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float inc = p.rate / sr;
        const float fbk = clamp01 (p.feedback);
        const float braw = clamp01 (p.blend) * 2.0f;
        const float p1 = clamp01 (1.0f - braw), p5 = clamp01 (braw - 1.0f), p3 = 1.0f - p1 - p5;
        const float centreNote = 12.0f * std::log2 (std::max (p.centerHz, 1.0f) / 8.1757989f);
        const bool spread = std::fabs (p.depth2) > 1.0e-4f;
        float* io[2] = { l, r };
        float hc1[2] = {}, hc2[2] = {}, hS[2][kStages] = {};
        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            phase -= ffloor (phase);
            for (int c = 0; c < 2; ++c)
            {
                // the cutoff of the stages, and the coefficients of the feedback band pass, follow the LFO: every sample, at tier low every eighth (the LFO moves the
                // cutoff by a few thousandths of an octave in that time)
                if (!fast || (i & 7) == 0)
                {
                    const float ph = phase + (c == 1 ? p.phaseOffset : -p.phaseOffset) * 0.5f;
                    const float x01 = ph - ffloor (ph);
                    const float v = 2.0f * std::fabs (2.0f * x01 - 1.0f) - 1.0f; // triangle -1 .. 1
                    const float e2 = (centreNote + p.depthSemis * v) * (1.0f / 12.0f);
                    const float hz = std::clamp (8.1757989f * (fast ? fastmath::exp2 (e2) : std::exp2 (e2)), 20.0f, 0.45f * sr);
                    hc1[c] = OnePole::coefficient (hz / 20.0f, sampleRate);
                    hc2[c] = OnePole::coefficient (hz * 20.0f, sampleRate);
                    if (spread)
                    {
                        // depth 2: the stage cutoffs spread linearly in semitones over [-depth2 / 2, +depth2 / 2] around the centre (stage 0 lowest for a positive value)
                        for (int s = 0; s < kStages; ++s)
                        {
                            const float hs = std::max (hz * std::exp2 (p.depth2 * (static_cast<float> (s) / 11.0f - 0.5f) * (1.0f / 12.0f)), 5.0f);
                            const float g = phaserStageG (hs, sampleRate);
                            hS[c][s] = g / (1.0f + g);
                        }
                    }
                    else
                    {
                        const float g = phaserStageG (hz, sampleRate);
                        const float a0 = g / (1.0f + g);
                        for (int s = 0; s < kStages; ++s)
                            hS[c][s] = a0;
                    }
                }
                const float c1 = hc1[c], c2 = hc2[c];
                const float* aS = hS[c];
                const float dry = io[c][i];
                float x = dry * (1.0f + 0.5f * fbk) + fb[c];
                float tap[3] = { 0.0f, 0.0f, 0.0f };
                for (int s = 0; s < kStages; ++s)
                {
                    const float vv = (x - z[c][s]) * aS[s];
                    const float lp = vv + z[c][s];
                    z[c][s] = flushDenormal (lp + vv);
                    x = x - 2.0f * lp;
                    if (s == 3) tap[0] = x;
                    if (s == 7) tap[1] = x;
                    if (s == 11) tap[2] = x;
                }
                const float ap = p1 * tap[0] + p3 * tap[1] + p5 * tap[2];
                bpA[c] = flushDenormal (bpA[c] + c2 * (ap - bpA[c]));
                bpB[c] = flushDenormal (bpB[c] + c1 * (bpA[c] - bpB[c]));
                fb[c] = fast ? fastmath::tanh (fbk * (bpA[c] - bpB[c])) : std::tanh (fbk * (bpA[c] - bpB[c]));
                const float wet = 0.5f * (dry + ap);
                io[c][i] = dry * (1.0f - p.mix) + wet * p.mix; // linear crossfade (document 06, section 1)
            }
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    float z[2][kStages] = {};
    float phase = 0.0f, fb[2] = {}, bpA[2] = {}, bpB[2] = {};
};


} // namespace cpuref
} // namespace own
} // namespace synthyboy
