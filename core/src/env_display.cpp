// SynthyBoy - core: the shape of an envelope of the own engine as the voice plays it (see env_display.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synthyboy/env_display.h"

#include <algorithm>
#include <cmath>

#include "dsp/util.h"
#include "voice/param_index.h"

namespace synthyboy
{
namespace
{
constexpr double kMinStage = 0.015;     // the least width of a stage that has a duration (share of the axis)
constexpr double kSustainShare = 0.25;  // the sustain segment against the timed stages
constexpr double kSustainMin = 0.1;     // seconds
}

bool EnvDisplay::compute (const ParamValues& v, const Request& r, Geometry& g, float* out, int n) noexcept
{
    g = Geometry {};
    if (out == nullptr || n < 2)
        return false;
    std::fill (out, out + n, 0.0f);
    if (r.env < 1 || r.env > 6)
        return false;
    const own::EnvIdx& e = own::paramIndex().env[r.env - 1];
    if (e.attack < 0 || e.sustain < 0 || e.units < 0 || e.attackBeats < 0)
        return false;
    auto P = [&] (int i) { return i >= 0 ? static_cast<double> (v.plain[static_cast<size_t> (i)]) : 0.0; };
    double t[6] = {};   // seconds: delay, attack, hold, decay, sustain segment, release
    if (P (e.units) >= 0.5)
    {
        const double spb = 60.0 / std::max (r.bpm, 1.0);
        t[0] = P (e.delayBeats) * spb; t[1] = P (e.attackBeats) * spb; t[2] = P (e.holdBeats) * spb; t[3] = P (e.decayBeats) * spb; t[5] = P (e.releaseBeats) * spb;
    }
    else
    {
        t[0] = P (e.delay) * 0.001; t[1] = P (e.attack) * 0.001; t[2] = P (e.hold) * 0.001; t[3] = P (e.decay) * 0.001; t[5] = P (e.release) * 0.001;
    }
    for (double& x : t)
        x = std::max (x, 0.0);
    const double sustain = std::clamp (P (e.sustain) * 0.01, 0.0, 1.0);
    const double timed = t[0] + t[1] + t[2] + t[3] + t[5];
    t[4] = std::max (kSustainMin, kSustainShare * timed);
    double total = timed + t[4];
    double w[6];
    double sum = 0.0;
    for (int i = 0; i < 6; ++i)
    {
        w[i] = t[i] / total;
        if (i != 4 && t[i] > 0.0)
            w[i] = std::max (w[i], kMinStage);
        sum += w[i];
    }
    double x = 0.0;
    for (int i = 0; i < 6; ++i)
    {
        x += w[i] / sum;
        g.end[i] = i == 5 ? 1.0 : x;
    }
    g.sustain = sustain;
    const float aC = static_cast<float> (-P (e.attackCurve)), dC = static_cast<float> (P (e.decayCurve)), rC = static_cast<float> (P (e.releaseCurve));
    for (int i = 0; i < n; ++i)
    {
        const double px = static_cast<double> (i) / static_cast<double> (n - 1);
        double y = 0.0;
        if (px <= g.end[0])
            y = 0.0;
        else if (px <= g.end[1])
            y = own::powerShape (static_cast<float> ((px - g.end[0]) / std::max (g.end[1] - g.end[0], 1.0e-9)), aC);
        else if (px <= g.end[2])
            y = 1.0;
        else if (px <= g.end[3])
            y = 1.0 + (sustain - 1.0) * own::powerShape (static_cast<float> ((px - g.end[2]) / std::max (g.end[3] - g.end[2], 1.0e-9)), dC);
        else if (px <= g.end[4])
            y = sustain;
        else
            y = sustain * (1.0 - own::powerShape (static_cast<float> ((px - g.end[4]) / std::max (g.end[5] - g.end[4], 1.0e-9)), rC));
        out[i] = static_cast<float> (std::clamp (y, 0.0, 1.0));
    }
    return true;
}

bool EnvDisplay::dotFor (const Geometry& g, const float* y, int n, float fineStage, float fraction, double& x, double& level) noexcept
{
    const int stage = static_cast<int> (fineStage + 0.5f);
    if (stage < 1 || stage > 6 || y == nullptr || n < 2)
        return false;
    const double x0 = stage == 1 ? 0.0 : g.end[stage - 2], x1 = g.end[stage - 1];
    const double f = stage == 5 ? 0.5 : std::clamp (static_cast<double> (fraction), 0.0, 1.0);
    x = x0 + (x1 - x0) * f;
    const double pos = std::clamp (x, 0.0, 1.0) * (n - 1);
    const int i = std::min (static_cast<int> (pos), n - 2);
    level = y[i] * (1.0 - (pos - i)) + y[i + 1] * (pos - i);
    return true;
}
} // namespace synthyboy
