// SynthyBoy - shared shell output stage: fixed make-up gain (+9 dB default, no limiter).
// Keeps desktop shells (JUCE VST3/AU, CLAP) at the exact same output loudness and open dynamics as the MPC shell default (shell-mpc/output_stage.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace synthyboy
{

/** Shared output stage matching shell-mpc/output_stage.h default (+9 dB pure make-up gain, no peak limiting). */
class ShellOutputStage
{
public:
    static constexpr float kDefaultGainDb = 9.0f;

    ShellOutputStage() noexcept = default;

    void prepare (double) noexcept {}

    void reset() noexcept {}

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
            if (l != nullptr)
            {
                const float outL = std::isfinite (l[i]) ? l[i] * gainLin : 0.0f;
                l[i] = std::isfinite (outL) ? outL : 0.0f;
            }
            if (! same && r != nullptr)
            {
                const float outR = std::isfinite (r[i]) ? r[i] * gainLin : 0.0f;
                r[i] = std::isfinite (outR) ? outR : 0.0f;
            }
        }
    }

private:
    float gainDb = kDefaultGainDb;
    float gainLin = 2.81838293f; // 10^(9/20)
};

} // namespace synthyboy
