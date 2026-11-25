// SynthyBoy - MPC overlay: where the native envelope widget (SDK `Envelope`, type DAHDSR, historical host skin) draws the points of its curve. Measured on the
// Key 61 (firmware 3.10.1.12) from screenshots of the primary plane at several settings (docs/MPC.md "Overlay (experimental)", results file "D terv, 5. proba").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The rule (all numbers in SCREEN pixels of the landscape UI, 1280 x 800):
//  - the curve is a polyline: delay (flat on the bottom line), attack (a straight line up to the top line), hold (flat on top), decay (a straight line down to the sustain
//    level), sustain (flat), release (a straight line down to the bottom line); the stages follow each other from left to right;
//  - the WIDTH of a time stage (delay, attack, hold, decay, release) is kUnit * n, n = the normalised value of its parameter (the host's 0 .. 1 position: the knob's range with
//    the skew of the parameter, ParamInfo::skew: n = (value / max) ^ skew); the sustain segment has the fixed width kUnit; the level of the sustain is the knob 0 .. 100 %;
//  - the left end of the polyline (delay 0) is at kOriginX; the top line is at kTopY (level 100 %), the bottom line at kBottomY (level 0).
//  The widget draws the stage handles (A, H, D, S, De, R: circles of a 16 px radius) over the polyline: they hide the line near the corners, which is why the corners are read
//  from the rule and not from the pixels. The line colour is kLineRgb (checked against the framebuffer at run time before the dot is drawn).
#pragma once

#include <algorithm>
#include <cmath>

namespace synthyboy_mpc { namespace overlay { namespace envgeom {

constexpr double kOriginX = 44.7, kUnit = 199.0, kTopY = 195.5, kBottomY = 383.5;
constexpr unsigned kLineRgb = 0xd31145;

/** The normalised values of the parameters of one envelope (0 .. 1) and the sustain level (0 .. 1). */
struct Norm
{
    double delay = 0.0, attack = 0.0, hold = 0.0, decay = 0.0, sustain = 1.0, release = 0.0;
};

struct Point
{
    bool valid = false;
    double x = 0.0, y = 0.0;
};

/** x of the end of each stage (screen pixels): delay, attack, hold, decay, sustain, release. */
inline void stageEnds (const Norm& n, double ends[6]) noexcept
{
    double x = kOriginX;
    const double w[6] = { n.delay, n.attack, n.hold, n.decay, 1.0, n.release };
    for (int i = 0; i < 6; ++i)
    {
        x += kUnit * std::clamp (w[i], 0.0, 1.0);
        ends[i] = x;
    }
}

inline double sustainY (const Norm& n) noexcept { return kBottomY - (kBottomY - kTopY) * std::clamp (n.sustain, 0.0, 1.0); }

/** The point of the curve for the fine stage of LiveValues::envFineStage (1 delay, 2 attack, 3 hold, 4 decay, 5 sustain, 6 release) and the fraction 0 .. 1 of it that has run.
    Not valid for the other stages (idle, finished). The sustain has no progress: the dot stands in the middle of its segment. */
inline Point dotFor (const Norm& n, int stage, double fraction) noexcept
{
    Point p;
    if (stage < 1 || stage > 6)
        return p;
    double e[6];
    stageEnds (n, e);
    const double f = std::clamp (fraction, 0.0, 1.0);
    const double x0 = stage == 1 ? kOriginX : e[stage - 2], x1 = e[stage - 1];
    const double sy = sustainY (n);
    double y0 = kBottomY, y1 = kBottomY;
    switch (stage)
    {
        case 1: y0 = y1 = kBottomY; break;
        case 2: y0 = kBottomY; y1 = kTopY; break;
        case 3: y0 = y1 = kTopY; break;
        case 4: y0 = kTopY; y1 = sy; break;
        case 5: y0 = y1 = sy; break;
        case 6: y0 = sy; y1 = kBottomY; break;
        default: break;
    }
    const double t = stage == 5 ? 0.5 : f;
    p.valid = true;
    p.x = x0 + (x1 - x0) * t;
    p.y = y0 + (y1 - y0) * t;
    return p;
}

}}} // namespace synthyboy_mpc::overlay::envgeom
