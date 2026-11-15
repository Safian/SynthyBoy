// SynthyBoy - MPC shell: the peak of what the plugin hands to the host (after the make-up gain), for the status file of the overlay ("output_peak": the largest sample since the last status write, "output_peak_total": since the
// plugin was loaded, "output_blocks": blocks counted). Lock free: the audio thread does one pass over the block and relaxed atomic stores; the overlay thread reads and resets. One set for the whole library (the instances share it).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cmath>

namespace synthyboy_mpc {

struct OutputPeak
{
    std::atomic<float> window { 0.0f }, total { 0.0f };
    std::atomic<unsigned long> blocks { 0 };

    /** The audio thread: the largest absolute sample of the block (both channels; r may be null). */
    void note (const float* l, const float* r, int n) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            p = std::fmax (p, std::fabs (l[i]));
            if (r != nullptr)
                p = std::fmax (p, std::fabs (r[i]));
        }
        if (p > window.load (std::memory_order_relaxed))
            window.store (p, std::memory_order_relaxed);
        if (p > total.load (std::memory_order_relaxed))
            total.store (p, std::memory_order_relaxed);
        blocks.fetch_add (1, std::memory_order_relaxed);
    }
    /** The overlay thread: the peak since the last call. */
    float takeWindow() noexcept { return window.exchange (0.0f, std::memory_order_relaxed); }
};

inline OutputPeak& outputPeak() noexcept
{
    static OutputPeak p;
    return p;
}

} // namespace synthyboy_mpc
