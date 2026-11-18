// SynthyBoy - MPC shell: the peak of what the plugin hands to the host (after the make-up gain), for the status file of the overlay ("output_peak": the largest sample since the last status write, "output_peak_total": since the
// plugin was loaded, "output_blocks": blocks counted). Lock free: the audio thread does one pass over the block and relaxed atomic stores; the overlay thread reads and resets. One set for the whole library (the instances share it).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cmath>

// the NEON form (same condition as output_stage.h)
#if ((defined(__arm__) || (defined(__aarch64__) && defined(__linux__))) && defined(__ARM_NEON) || defined(SB_TEST_NEON)) && ! defined(SB_NO_NEON)
 #include <arm_neon.h>
 #define SYNTHYBOY_PEAK_NEON 1
#else
 #define SYNTHYBOY_PEAK_NEON 0
#endif

namespace synthyboy_mpc {

struct OutputPeak
{
    std::atomic<float> window { 0.0f }, total { 0.0f };
    std::atomic<unsigned long> blocks { 0 };

    /** The largest absolute sample of n samples, a NaN passed over (what std::fmax (p, std::fabs (x)) gives): the NEON form masks a NaN lane to zero with an integer compare on the magnitude bits and takes the
        vector maximum of numbers; a subnormal lane is treated as the float maximum of the build treats it (flushed to zero in flush mode, as the shell runs the audio thread). */
    static float peakOfBlock (const float* x, int n) noexcept
    {
        int i = 0;
        float p = 0.0f;
#if SYNTHYBOY_PEAK_NEON
        if (n >= 4)
        {
            float32x4_t acc = vdupq_n_f32 (0.0f);
            for (; i + 4 <= n; i += 4)
            {
                const uint32x4_t u = vandq_u32 (vreinterpretq_u32_f32 (vld1q_f32 (x + i)), vdupq_n_u32 (0x7fffffffu));
                const uint32x4_t keep = vcleq_u32 (u, vdupq_n_u32 (0x7f800000u));   // not a NaN
                acc = vmaxq_f32 (acc, vreinterpretq_f32_u32 (vandq_u32 (u, keep)));
            }
            const float32x2_t h = vpmax_f32 (vget_low_f32 (acc), vget_high_f32 (acc));
            p = vget_lane_f32 (vpmax_f32 (h, h), 0);
        }
#endif
        for (; i < n; ++i)
            p = std::fmax (p, std::fabs (x[i]));
        return p;
    }
    /** The reference loop of peakOfBlock for both channels (the old code of note ()); tests compare against it. */
    static float peakScalar (const float* l, const float* r, int n) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            p = std::fmax (p, std::fabs (l[i]));
            if (r != nullptr)
                p = std::fmax (p, std::fabs (r[i]));
        }
        return p;
    }
    static float peakOf (const float* l, const float* r, int n) noexcept
    {
        const float a = peakOfBlock (l, n);
        return r != nullptr ? std::fmax (a, peakOfBlock (r, n)) : a;
    }

    /** The audio thread: the largest absolute sample of the block (both channels; r may be null). */
    void note (const float* l, const float* r, int n) noexcept
    {
        const float p = peakOf (l, r, n);
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
