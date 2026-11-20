// SynthyBoy - core/fx-serum-b: the partition plan of the non-uniform convolution (docs/plans/reverb-convolve.md sections 3.4.1, 3.4.2 and 3.4.5). A pure function of the impulse length: how many
// partitions each of the three frequency domain stages holds, and the memory and the cost that follow. Header only, no state.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>

namespace synthyboy::fx
{
/** One frequency domain stage: block size B (the real transform has 2B points), the impulse offset o = 3B (1 x B for the synchronous stage 1), the number of 128 sample ticks per block (the work is
    spread over this many units, one per tick) and the stagger in ticks that keeps the large transforms of the stages on different ticks. */
struct ConvStageSpec
{
    int block, offset, ticks, stagger;
};

inline constexpr ConvStageSpec kConvStages[3] = { { 128, 128, 1, 0 }, { 1024, 3072, 8, 4 }, { 8192, 24576, 64, 1 } };
inline constexpr int kConvLevel2Min = 5120, kConvLevel3Min = 45056, kConvHeadTaps = 128, kConvMaxFrames = 524288;

/** The partitions per stage for an impulse of `frames` frames (the head holds the first 128 taps; stage 1 covers [128, 3072) with at most 23 partitions, stage 2 [3072, 24576) with at most 21). */
struct ConvPlan
{
    int frames;
    int n[3];
};

namespace detail
{
constexpr int ceilDiv (int a, int b) noexcept
{
    return (a + b - 1) / b;
}
} // namespace detail

/** The plan with the two thresholds given (the break-even points of the cost model; planFor () uses kConvLevel2Min and kConvLevel3Min). */
constexpr ConvPlan planWith (int frames, int level2Min, int level3Min) noexcept
{
    ConvPlan p { frames < 1 ? 1 : (frames > kConvMaxFrames ? kConvMaxFrames : frames), { 0, 0, 0 } };
    const int f = p.frames;
    if (f <= kConvHeadTaps)
        return p;
    if (f <= level2Min)
    {
        p.n[0] = detail::ceilDiv (f - kConvHeadTaps, 128);
        return p;
    }
    p.n[0] = 23;
    if (f <= level3Min)
    {
        p.n[1] = detail::ceilDiv (f - 3072, 1024);
        return p;
    }
    p.n[1] = 21;
    p.n[2] = detail::ceilDiv (f - 24576, 8192);
    return p;
}

constexpr ConvPlan planFor (int frames) noexcept
{
    return planWith (frames, kConvLevel2Min, kConvLevel3Min);
}

/** The partitions each stage can hold (the plan of the longest impulse the state is prepared for: headroom times the first impulse, at most kConvMaxFrames). */
struct IrCapacity
{
    int n[3];
};

constexpr IrCapacity capacityFor (int frames, double headroom = 1.5) noexcept
{
    const double scaled = headroom * static_cast<double> (frames);
    int up = static_cast<int> (scaled);
    if (static_cast<double> (up) < scaled)
        ++up;
    const ConvPlan p = planFor (up > kConvMaxFrames ? kConvMaxFrames : up);
    return { { p.n[0], p.n[1], p.n[2] } };
}

/** Bytes of the partition spectra of a plan: 2 channels x 8 bytes (one complex float) x partitions x bins (B + 1 per stage). */
constexpr size_t irSpectraBytes (const ConvPlan& p) noexcept
{
    return 16u * (static_cast<size_t> (p.n[0]) * 129u + static_cast<size_t> (p.n[1]) * 1025u + static_cast<size_t> (p.n[2]) * 8193u);
}

/** Bytes of the input spectrum history (the frequency delay line) of a capacity: (partitions + 1) spectra per stage that is in use. */
constexpr size_t fdlBytes (const IrCapacity& c) noexcept
{
    return 16u * ((c.n[0] > 0 ? static_cast<size_t> (c.n[0] + 1) * 129u : 0u) + (c.n[1] > 0 ? static_cast<size_t> (c.n[1] + 1) * 1025u : 0u) + (c.n[2] > 0 ? static_cast<size_t> (c.n[2] + 1) * 8193u : 0u));
}

/** The model cost of a plan in real flops per sample: the head (128 taps x 2 channels), per stage in use the forward and inverse real transforms of both channels (2.5 N log2 N flops per
    transform, N = 2B, per block of B samples) and the complex multiply-adds (8 flops, 2 channels, bins / B per sample and partition). HYPOTHESIS until the device measures it (T01). */
constexpr double planCost (const ConvPlan& p) noexcept
{
    double flops = 512.0;
    const double transforms[3] = { 160.0, 220.0, 280.0 }; // 2 x 2 x 2.5 x N log2 N / B for N = 256, 2048, 16384
    const double bins[3] = { 129.0 / 128.0, 1025.0 / 1024.0, 8193.0 / 8192.0 };
    for (int s = 0; s < 3; ++s)
        if (p.n[s] > 0)
            flops += transforms[s] + 16.0 * static_cast<double> (p.n[s]) * bins[s];
    return flops;
}
} // namespace synthyboy::fx
