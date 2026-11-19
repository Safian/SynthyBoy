// SynthyBoy - shared shell output stage: fixed make-up gain (+9 dB default) and stereo-linked zero-latency peak limiter (-1 dBFS ceiling).
// Keeps desktop shells (JUCE VST3/AU, CLAP) at the exact same output loudness and headroom ceiling as the MPC shell (shell-mpc/output_stage.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace synthyboy
{

/** Shared output stage constants matching shell-mpc/output_stage.h (make-up gain +9 dB, -1 dBFS peak ceiling, 120 ms release). */
class ShellOutputStage
{
public:
    static constexpr float kDefaultGainDb = 9.0f;
    static constexpr float kCeiling = 0.891251f; // -1.0 dBFS
    static constexpr float kReleaseSeconds = 0.120f;

    ShellOutputStage() noexcept { prepare (44100.0); }

    void prepare (double sr) noexcept
    {
        const double safeSr = (std::isfinite (sr) && sr > 0.0) ? sr : 44100.0;
        relCoef = std::exp (-1.0f / (kReleaseSeconds * static_cast<float> (safeSr)));
        reset();
    }

    void reset() noexcept { env = 1.0f; }

    void setGainDb (float db) noexcept
    {
        if (! std::isfinite (db))
            db = 0.0f;
        gainDb = std::clamp (db, -24.0f, 24.0f);
        gainLin = gainDb == 0.0f ? 1.0f : std::pow (10.0f, gainDb / 20.0f);
    }

    void process (float* l, float* r, int n) noexcept
    {
        if (n <= 0 || (l == nullptr && r == nullptr))
            return;
        const bool same = (l == r);
        for (int i = 0; i < n; ++i)
        {
            float a = (l != nullptr && std::isfinite (l[i])) ? l[i] * gainLin : 0.0f;
            float b = (! same && r != nullptr && std::isfinite (r[i])) ? r[i] * gainLin : a;
            if (! std::isfinite (a)) a = 0.0f;
            if (! std::isfinite (b)) b = 0.0f;

            const float pk = std::max (std::fabs (a), std::fabs (b));
            const float tgt = pk > kCeiling ? kCeiling / pk : 1.0f;
            if (tgt < env)
                env = tgt;
            else
            {
                env = 1.0f + (env - 1.0f) * relCoef;
                if (env > 0.99999f)
                    env = 1.0f;
                if (env > tgt)
                    env = tgt;
            }

            const float g = env;
            if (l != nullptr)
            {
                const float outL = g == 1.0f ? a : a * g;
                l[i] = std::isfinite (outL) ? outL : 0.0f;
            }
            if (! same && r != nullptr)
            {
                const float outR = g == 1.0f ? b : b * g;
                r[i] = std::isfinite (outR) ? outR : 0.0f;
            }
        }
    }

private:
    float gainDb = kDefaultGainDb;
    float gainLin = 2.81838293f; // 10^(9/20)
    float env = 1.0f;
    float relCoef = 0.99981105f;
};

} // namespace synthyboy
