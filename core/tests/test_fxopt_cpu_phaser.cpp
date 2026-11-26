// SynthyBoy - core tests: the Phaser after the CPU review items 5 and 8 (2026-11-26) against the frozen reference of the code before them (ref/fx_phaser_before.h).
//  - item 5 (bit identical): the refresh of the stage and feedback coefficients is kept while the cutoff, depth 2 and rate are the same;
//  - item 8 (not bit identical, owner approved): the 24 stage states are guarded against the decay into denormals once per call (a control period) instead of every stage and sample. A
//    program that keeps the states above 1e-18 gives the very bits; where a state decays below 1e-18 inside a call the reference flushes it to 0 at once, the new code a little later, and the
//    outputs differ by that state's contribution (3.1e-17 absolute at most, measured); both are exactly silent when the decay is over.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "fx/fx_effects.h"
#include "ref/fx_phaser_before.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
float uni (std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * static_cast<float> (rng() % 100001) / 100000.0f; }

struct Result
{
    bool same = true;      // every sample the same bits
    double maxDiff = 0.0;  // the largest absolute difference
    double maxAbs = 0.0;   // the largest output magnitude
    long long silentBoth = 0, silentNew = 0, silentOld = 0;
};

/** `program`: 0 = noise all the time (no state decays below the guard), 1 = noise in bursts with long silences (the tails decay through 1e-18). */
Result walk (double sr, bool fast, int program, uint32_t seed)
{
    std::mt19937 rng (seed);
    Phaser a;
    cpuref::PhaserBefore b;
    a.prepare (sr);
    b.prepare (sr);
    a.fast = fast;
    b.fast = fast;
    Phaser::Params p;
    Result res;
    for (int per = 0; per < 900; ++per)
    {
        if (per % 20 == 0)
        {
            p.mix = uni (rng, 0.2f, 1.0f);
            p.feedback = uni (rng, 0.0f, 1.0f);
            p.rate = rng() % 3 == 0 ? 0.0f : uni (rng, 0.02f, 4.0f);
            p.depthSemis = rng() % 6 == 0 ? 0.0f : uni (rng, 1.0f, 36.0f);
            p.centerHz = uni (rng, 30.0f, 8000.0f);
            p.blend = uni (rng, 0.0f, 1.0f);
            p.phaseOffset = uni (rng, 0.0f, 1.0f);
            p.depth2 = rng() % 3 == 0 ? uni (rng, 1.0f, 12.0f) : 0.0f;
        }
        cpuref::PhaserBefore::Params q;
        q.mix = p.mix; q.feedback = p.feedback; q.rate = p.rate; q.depthSemis = p.depthSemis; q.centerHz = p.centerHz; q.blend = p.blend; q.phaseOffset = p.phaseOffset; q.depth2 = p.depth2;
        a.set (p);
        b.set (q);
        const bool quiet = program == 1 && (per % 150) >= 20; // 20 periods of noise, then 130 of silence
        const int n = 1 + static_cast<int> (rng() % 40);
        std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
        {
            l[static_cast<size_t> (i)] = quiet ? 0.0f : uni (rng, -0.5f, 0.5f);
            r[static_cast<size_t> (i)] = quiet ? 0.0f : uni (rng, -0.5f, 0.5f);
        }
        std::vector<float> l2 = l, r2 = r;
        a.process (l.data(), r.data(), n);
        b.process (l2.data(), r2.data(), n);
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const float x = c == 0 ? l[static_cast<size_t> (i)] : r[static_cast<size_t> (i)], y = c == 0 ? l2[static_cast<size_t> (i)] : r2[static_cast<size_t> (i)];
                if (std::memcmp (&x, &y, 4) != 0)
                    res.same = false;
                res.maxDiff = std::max (res.maxDiff, std::fabs (static_cast<double> (x) - static_cast<double> (y)));
                res.maxAbs = std::max (res.maxAbs, std::fabs (static_cast<double> (y)));
                if (quiet)
                {
                    res.silentNew += x == 0.0f ? 1 : 0;
                    res.silentOld += y == 0.0f ? 1 : 0;
                    res.silentBoth += (x == 0.0f && y == 0.0f) ? 1 : 0;
                }
            }
    }
    return res;
}
} // namespace

TEST (phaser_is_bit_identical_to_the_code_before_while_no_state_decays_below_the_guard)
{
    int walks = 0;
    for (double sr : { 44100.0, 48000.0 })
        for (int fast = 0; fast < 2; ++fast)
            for (uint32_t seed = 1; seed <= 8; ++seed)
            {
                const Result r = walk (sr, fast != 0, 0, seed * 31u + static_cast<uint32_t> (fast));
                CHECK (r.same);
                ++walks;
            }
    std::printf ("phaser walks bit identical to the code before: %d\n", walks);
}

TEST (phaser_decay_differs_from_the_per_sample_guard_only_by_the_part_below_1e_minus_18)
{
    double worst = 0.0;
    long long silentBoth = 0, silentNew = 0, silentOld = 0;
    int differing = 0, walks = 0;
    for (double sr : { 44100.0, 48000.0 })
        for (int fast = 0; fast < 2; ++fast)
            for (uint32_t seed = 1; seed <= 12; ++seed)
            {
                const Result r = walk (sr, fast != 0, 1, seed * 131u + static_cast<uint32_t> (fast));
                worst = std::max (worst, r.maxDiff);
                silentBoth += r.silentBoth;
                silentNew += r.silentNew;
                silentOld += r.silentOld;
                differing += r.same ? 0 : 1;
                ++walks;
                CHECK (r.maxDiff <= 1.0e-15);
            }
    std::printf ("phaser decay: %d walks, %d with a difference, largest absolute difference %.3g (bound 1e-15), silent samples: both %lld, new code %lld, reference %lld\n", walks, differing,
                 worst, silentBoth, silentNew, silentOld);
    // the decay ends in exact silence in both
    CHECK (silentNew > 0 && silentOld > 0);
    CHECK (silentNew >= silentOld - silentOld / 20); // the new code is silent for about the same samples (within 5 %: its states are flushed a call later)
}
