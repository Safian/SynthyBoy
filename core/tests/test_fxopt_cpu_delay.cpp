// SynthyBoy - core tests: the Delay after the CPU review item 3 (2026-11-26) against the frozen reference of the code before it (ref/fx_delay_before.h).
//  - bit identical: the read position, its split and the 8 point Lagrange weights are kept while the glide of the delay time has settled (the float no longer changes), the two
//    LoopBand::set of a call are one; the cubic read (hq off) and the Lagrange read with the weights of the first version (Delay::divisionWeights) give the very bits (memcmp of every sample);
//  - not bit identical (owner approved): the 8 Lagrange weights as products of (t - m) times constants 1 / prod (k - m), no division. Rounding level; the bound is printed and checked.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "fx/fx_effects.h"
#include "ref/fx_delay_before.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
float uni (std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * static_cast<float> (rng() % 100001) / 100000.0f; }

Delay::Params randomParams (std::mt19937& rng, int style, bool hq, bool extreme)
{
    Delay::Params p;
    p.mix = uni (rng, 0.0f, 1.0f);
    p.feedback = uni (rng, -1.0f, 1.0f);
    p.timeL = extreme && rng() % 3 == 0 ? (rng() % 2 == 0 ? 0.00001f : 3.9f) : uni (rng, 0.002f, 1.5f);
    p.timeR = extreme && rng() % 3 == 0 ? (rng() % 2 == 0 ? 0.00001f : 3.9f) : uni (rng, 0.002f, 1.5f);
    p.cutoff = uni (rng, 20.0f, 9000.0f);
    p.spread = uni (rng, 0.0f, 1.0f);
    p.offsetL = uni (rng, 0.5f, 2.0f);
    p.offsetR = uni (rng, 0.5f, 2.0f);
    p.style = style;
    p.hq = hq;
    return p;
}

/** One run of the new Delay and the reference over the same program: parameter changes every few thousand samples (the time glides, then the glide settles and the position stands still),
    blocks of every length. Returns the largest absolute sample difference (0 = the same bits), and `same` is false at the first difference of the bits. */
struct Run
{
    double maxDiff = 0.0;
    bool same = true;
};
Run compare (double sr, int style, bool hq, bool division, uint32_t seed, bool extreme, int segments, int segmentLength)
{
    std::mt19937 rng (seed);
    Delay a;
    cpuref::DelayBefore b;
    a.prepare (sr);
    b.prepare (sr);
    a.divisionWeights = division;
    Run run;
    for (int seg = 0; seg < segments; ++seg)
    {
        const Delay::Params p = randomParams (rng, style, hq, extreme);
        a.set (p);
        cpuref::DelayBefore::Params q;
        q.mix = p.mix; q.feedback = p.feedback; q.timeL = p.timeL; q.timeR = p.timeR; q.cutoff = p.cutoff; q.spread = p.spread; q.offsetL = p.offsetL; q.offsetR = p.offsetR; q.style = p.style; q.hq = p.hq;
        b.set (q);
        int done = 0;
        while (done < segmentLength)
        {
            const int n = 1 + static_cast<int> (rng() % 333);
            std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
            for (int i = 0; i < n; ++i)
            {
                // a program with silence in it (the tails and the settled state show) and a click now and then
                const bool quiet = ((done + i) / 3000) % 3 == 2;
                l[static_cast<size_t> (i)] = quiet ? 0.0f : uni (rng, -0.5f, 0.5f);
                r[static_cast<size_t> (i)] = quiet ? 0.0f : uni (rng, -0.5f, 0.5f);
            }
            std::vector<float> l2 = l, r2 = r;
            a.process (l.data(), r.data(), n);
            b.process (l2.data(), r2.data(), n);
            for (int i = 0; i < n; ++i)
            {
                if (std::memcmp (&l[static_cast<size_t> (i)], &l2[static_cast<size_t> (i)], 4) != 0 || std::memcmp (&r[static_cast<size_t> (i)], &r2[static_cast<size_t> (i)], 4) != 0)
                    run.same = false;
                run.maxDiff = std::max ({ run.maxDiff, static_cast<double> (std::fabs (l[static_cast<size_t> (i)] - l2[static_cast<size_t> (i)])), static_cast<double> (std::fabs (r[static_cast<size_t> (i)] - r2[static_cast<size_t> (i)])) });
            }
            done += n;
        }
    }
    return run;
}
} // namespace

// The settled glide is the case that is new: a segment of 60000 samples (the half life of the glide is 20 ms, about 900 samples: the float stops changing after about 25 half lives) after every
// parameter change, in every style, with the cubic read and with the Lagrange read in the division form: every sample the same bits.
TEST (delay_is_bit_identical_to_the_code_before_with_a_settling_glide)
{
    int runs = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int style = 0; style <= 4; ++style)
            for (int mode = 0; mode < 2; ++mode) // 0: the cubic read, 1: hq with the division weights
            {
                const Run run = compare (sr, style, mode == 1, true, 1000u + static_cast<uint32_t> (style * 7 + mode) + static_cast<uint32_t> (sr), style % 2 == 0, 3, sr > 50000.0 ? 40000 : 60000);
                CHECK (run.same);
                CHECK (run.maxDiff == 0.0);
                ++runs;
            }
    std::printf ("delay runs bit identical to the code before: %d\n", runs);
}

// Extreme times (the clamps at 3 samples and 4 seconds), block lengths of 1 and many short segments.
TEST (delay_is_bit_identical_to_the_code_before_with_extreme_times_and_short_segments)
{
    for (int hq = 0; hq < 2; ++hq)
        for (int style = 0; style <= 4; ++style)
        {
            const Run run = compare (44100.0, style, hq != 0, true, 555u + static_cast<uint32_t> (style) + static_cast<uint32_t> (hq * 100), true, 60, 2500);
            CHECK (run.same);
        }
}

// The products of (t - m) times 1 / prod (k - m) against the 56 divisions: the weights, over a dense grid of t and the neighbourhood of the nodes.
TEST (lagrange8_product_weights_are_within_rounding_of_the_division_weights)
{
    std::mt19937 rng (8);
    double worst = 0.0, worstSum = 0.0;
    long long compared = 0;
    auto check = [&] (float t) {
        float a[8], b[8];
        DelayLine::lagrange8Weights (t, a, true);
        DelayLine::lagrange8Weights (t, b, false);
        float sum = 0.0f;
        for (int k = 0; k < 8; ++k)
        {
            if (a[k] != 0.0f || b[k] != 0.0f)
                worst = std::max (worst, std::fabs (static_cast<double> (a[k]) - static_cast<double> (b[k])) / std::max (std::fabs (static_cast<double> (a[k])), 1.0e-30));
            else
                CHECK (a[k] == b[k]);
            sum += b[k];
            ++compared;
        }
        worstSum = std::max (worstSum, static_cast<double> (std::fabs (sum - 1.0f)));
    };
    for (int i = 0; i <= 20000; ++i)
        check (static_cast<float> (i) / 20000.0f);
    for (int i = 0; i < 200000; ++i)
        check (static_cast<float> (rng() % 16777216u) / 16777216.0f);
    for (float t : { 0.0f, 1.0e-30f, 1.0e-7f, 0.5f, 0.99999994f, 0.25f, 0.75f })
        check (t);
    std::printf ("lagrange8 weights: %lld compared, worst relative difference product form vs division form %.3g (bound 1.5e-6), worst |sum - 1| of the product weights %.3g\n", compared, worst, worstSum);
    CHECK (worst <= 1.5e-6);
    CHECK (worstSum <= 5.0e-7);
}

// The Delay with the product weights (hq on, the default of the code) against the reference: the difference stays at the rounding level of the weights (the echoes of a program of amplitude
// 0.5 with a feedback up to 1 and the loop limiter).
TEST (delay_hq_with_product_weights_stays_within_rounding_of_the_code_before)
{
    double worst = 0.0;
    for (int style = 0; style <= 4; ++style)
    {
        const Run run = compare (48000.0, style, true, false, 4000u + static_cast<uint32_t> (style), false, 3, 40000);
        worst = std::max (worst, run.maxDiff);
        CHECK (run.maxDiff <= 3.0e-5);
    }
    std::printf ("delay hq, product weights: largest sample difference to the code before %.3g (program amplitude 0.5, bound 3e-5)\n", worst);
}
