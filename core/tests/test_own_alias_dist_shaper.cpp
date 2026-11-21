// SynthyBoy - own engine tests: X-Shaper transfer curves in Distortion (docs/plans/distortion-antialiasing.md task X2).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "alias_dist_support.h"
#include "fx/fx_adaa.h"
#include "osc/content_set.h"
#include "synthyboy/content_state.h"
#include "test_runner.h"

using namespace synthyboy;

TEST (own_alias_dist_shaper_constant_curves)
{
    // (a) constant curves A = B = 0.5, type 12, any drive: x = {0.3, -0.3, 0.3} after a warm-up of the sign flip:
    // the value at a sample away from a sign change is +-0.5 within 1e-5 (today clamp (0.3 * g))
    StateContent c;
    c.shaperA = c.shaperB = { ContentPoint (0.0f, 0.5f), ContentPoint (1.0f, 0.5f) };
    const auto set = own::ContentSet::build (c, nullptr);
    const auto& y = set->shaper[0];
    const auto& I = set->shaperInt[0];

    for (const float driveDb : { 0.0f, 12.0f, 24.0f })
    {
        own::Distortion d;
        d.prepare (aliasfx::kFs);
        d.set (aliasfx::params (12, driveDb));
        d.setShaper (y.data(), I.data(), y.data(), I.data());

        // { 0.3, 0.3, -0.3, -0.3, 0.3, 0.3 }: indices 1, 3, 5 are away from a sign change
        std::vector<float> l = { 0.3f, 0.3f, -0.3f, -0.3f, 0.3f, 0.3f }, r = l;
        d.process (l.data(), r.data(), static_cast<int> (l.size()));
        CHECK_NEAR (l[1], 0.5, 1e-5);
        CHECK_NEAR (l[3], -0.5, 1e-5);
        CHECK_NEAR (l[5], 0.5, 1e-5);
    }
}

TEST (own_alias_dist_shaper_identity_null_pointers)
{
    // (b) null pointers (identity): type 12 and drive +20 dB, x = 0.3 repeated: 0.3 within 1e-6 (today 1.0: the drive no longer scales, A150);
    // x = 1.7 -> 1.0; type 13 identity with a 200 Hz sine of 0.5 after 0.5 s: output within 1e-2 of the input (the 10 Hz blocker)
    own::Distortion d;
    d.prepare (aliasfx::kFs);
    d.set (aliasfx::params (12, 20.0f));
    d.setShaper (nullptr, nullptr, nullptr, nullptr);

    std::vector<float> l1 = { 0.3f, 0.3f, 0.3f }, r1 = l1;
    d.process (l1.data(), r1.data(), static_cast<int> (l1.size()));
    CHECK_NEAR (l1[1], 0.3, 1e-6);
    CHECK_NEAR (l1[2], 0.3, 1e-6);

    std::vector<float> l2 = { 1.7f, 1.7f }, r2 = l2;
    d.process (l2.data(), r2.data(), static_cast<int> (l2.size()));
    CHECK_NEAR (l2[1], 1.0, 1e-6);

    // type 13 identity with 200 Hz sine of 0.5 after 0.5 s
    d.reset();
    d.set (aliasfx::params (13, 0.0f));
    d.setShaper (nullptr, nullptr, nullptr, nullptr);

    const int total = static_cast<int> (aliasfx::kFs * 0.6); // 0.6 seconds
    std::vector<float> in (total);
    for (int i = 0; i < total; ++i)
        in[i] = static_cast<float> (0.5 * std::sin (2.0 * 3.141592653589793 * 200.0 * static_cast<double> (i) / aliasfx::kFs));

    std::vector<float> l3 (in), r3 (in);
    d.process (l3.data(), r3.data(), total);

    const int start = static_cast<int> (aliasfx::kFs * 0.5);
    const int nCycle = 441; // 2 cycles of 200 Hz at 44100 Hz
    double sc = 0.0, ss = 0.0;
    for (int i = 0; i < nCycle; ++i)
    {
        const double ph = 2.0 * 3.141592653589793 * 200.0 * static_cast<double> (start + i) / aliasfx::kFs;
        sc += static_cast<double> (l3[start + i]) * std::cos (ph);
        ss += static_cast<double> (l3[start + i]) * std::sin (ph);
    }
    const double amp = (2.0 / static_cast<double> (nCycle)) * std::sqrt (sc * sc + ss * ss);
    CHECK_NEAR (amp, 0.5, 1e-2);
}

TEST (own_alias_dist_shaper_morph)
{
    // (c) morph: A = y = 0, B = y = 1 (constant tables), type 12:
    // drive -30 dB -> 0, drive 0 dB -> 0.5 sign (x), drive +30 dB -> sign (x) (within 1e-5, away from zero crossings)
    StateContent c;
    c.shaperA = { ContentPoint (0.0f, 0.0f), ContentPoint (1.0f, 0.0f) };
    c.shaperB = { ContentPoint (0.0f, 1.0f), ContentPoint (1.0f, 1.0f) };
    const auto set = own::ContentSet::build (c, nullptr);

    own::Distortion d;
    d.prepare (aliasfx::kFs);
    d.setShaper (set->shaper[0].data(), set->shaperInt[0].data(), set->shaper[1].data(), set->shaperInt[1].data());

    // drive -30 dB -> 0
    d.set (aliasfx::params (12, -30.0f));
    std::vector<float> l1 = { 0.3f, 0.3f, -0.3f, -0.3f }, r1 = l1;
    d.process (l1.data(), r1.data(), 4);
    CHECK_NEAR (l1[1], 0.0, 1e-5);
    CHECK_NEAR (l1[3], 0.0, 1e-5);

    // drive 0 dB -> 0.5 sign (x)
    d.set (aliasfx::params (12, 0.0f));
    std::vector<float> l2 = { 0.3f, 0.3f, -0.3f, -0.3f }, r2 = l2;
    d.process (l2.data(), r2.data(), 4);
    CHECK_NEAR (l2[1], 0.5, 1e-5);
    CHECK_NEAR (l2[3], -0.5, 1e-5);

    // drive +30 dB -> sign (x)
    d.set (aliasfx::params (12, 30.0f));
    std::vector<float> l3 = { 0.3f, 0.3f, -0.3f, -0.3f }, r3 = l3;
    d.process (l3.data(), r3.data(), 4);
    CHECK_NEAR (l3[1], 1.0, 1e-5);
    CHECK_NEAR (l3[3], -1.0, 1e-5);
}

TEST (own_alias_dist_shaper_aliasing_suppression)
{
    // (d) alias: A = B = the steep curve (0, 0), (0.1, 1), (1, 1), type 12, a sine of 0.5:
    // aliasDb (on) <= aliasDb (off) - 6.0 where "off" is the naive table lookup computed in the test: on <= naive - 6.0
    StateContent c;
    c.shaperA = c.shaperB = { ContentPoint (0.0f, 0.0f), ContentPoint (0.1f, 1.0f), ContentPoint (1.0f, 1.0f) };
    const auto set = own::ContentSet::build (c, nullptr);
    const std::vector<float> in = aliasfx::sine (0.5);

    // "on": Distortion with ADAA active
    own::Distortion d;
    d.prepare (aliasfx::kFs);
    d.set (aliasfx::params (12, 0.0f));
    d.setShaper (set->shaper[0].data(), set->shaperInt[0].data(), set->shaper[1].data(), set->shaperInt[1].data());
    std::vector<float> l (in), r (in);
    for (size_t pos = 0; pos < l.size(); pos += 32)
        d.process (l.data() + pos, r.data() + pos, static_cast<int> (std::min (size_t (32), l.size() - pos)));

    const aliasm::Result on = aliasm::measure (l.data() + aliasfx::kSkip, aliasfx::kN, aliasfx::f0(), aliasfx::kFs);

    // "naive": naive table lookup of the same curve without ADAA
    std::vector<float> naive_out (in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        const float x = in[i];
        const float u = std::min (std::fabs (x), 1.0f);
        const float c_val = own::adaa::shaperC (set->shaper[0].data(), u);
        naive_out[i] = (x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f)) * c_val;
    }
    const aliasm::Result naive = aliasm::measure (naive_out.data() + aliasfx::kSkip, aliasfx::kN, aliasfx::f0(), aliasfx::kFs);

    std::printf ("    DIST-AA type=12 steep alias naive %.2f dB on %.2f dB (difference %.2f), harm naive %.2f on %.2f\n",
                 naive.aliasDb, on.aliasDb, on.aliasDb - naive.aliasDb, naive.harmDb, on.harmDb);
    CHECK (on.aliasDb <= naive.aliasDb - 6.0);
}

TEST (own_alias_dist_shaper_block_split_and_reset)
{
    // (e) block split and reset as E1
    for (const int type : { 12, 13 })
        for (const bool fast : { false, true })
            CHECK (aliasfx::blockSplitAndReset (type, fast));

    // Also with custom curve tables set
    StateContent c;
    c.shaperA = c.shaperB = { ContentPoint (0.0f, 0.2f), ContentPoint (0.6f, 0.9f), ContentPoint (1.0f, 1.0f) };
    const auto set = own::ContentSet::build (c, nullptr);

    for (const int type : { 12, 13 })
    {
        const auto p = aliasfx::params (type, 0.0f);
        std::vector<float> in (4096);
        aliasfx::Lcg rng (135792468u);
        for (auto& v : in)
            v = 0.8f * rng.sample();

        auto runShaped = [&] (size_t chunk) {
            own::Distortion dist;
            dist.prepare (aliasfx::kFs);
            dist.set (p);
            dist.setShaper (set->shaper[0].data(), set->shaperInt[0].data(), set->shaper[1].data(), set->shaperInt[1].data());
            std::vector<float> l (in), r (in);
            for (size_t pos = 0; pos < l.size(); pos += chunk)
                dist.process (l.data() + pos, r.data() + pos, static_cast<int> (std::min (chunk, l.size() - pos)));
            return l;
        };

        const auto a = runShaped (32), b = runShaped (7), c_out = runShaped (128);
        CHECK (aliasfx::same (a, b));
        CHECK (aliasfx::same (a, c_out));

        own::Distortion warm, fresh;
        warm.prepare (aliasfx::kFs);
        fresh.prepare (aliasfx::kFs);
        warm.set (p);
        fresh.set (p);
        warm.setShaper (set->shaper[0].data(), set->shaperInt[0].data(), set->shaper[1].data(), set->shaperInt[1].data());
        fresh.setShaper (set->shaper[0].data(), set->shaperInt[0].data(), set->shaper[1].data(), set->shaperInt[1].data());

        std::vector<float> w1 (in.begin(), in.begin() + 1000), w2 (w1);
        warm.process (w1.data(), w2.data(), 1000);
        warm.reset();
        warm.setShaper (set->shaper[0].data(), set->shaperInt[0].data(), set->shaper[1].data(), set->shaperInt[1].data());

        std::vector<float> l1 (in.begin() + 1000, in.begin() + 2000), r1 (l1), l2 (l1), r2 (l1);
        warm.process (l1.data(), r1.data(), 1000);
        fresh.process (l2.data(), r2.data(), 1000);
        CHECK (aliasfx::same (l1, l2));
        CHECK (aliasfx::same (r1, r2));
    }
}

TEST (own_alias_dist_shaper_checksums_of_types_0_to_11)
{
    for (int type = 0; type <= 11; ++type)
        aliasfx::checksum (type);
}
