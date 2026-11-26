// SynthyBoy - own engine tests: the shared decimation of the oversampled (warped) oscillator path (WtOsc, voices with a constant gain are summed at the oversampled rate and decimated
// once per channel) against the decimator per unison voice it replaced (WtOsc::setSharedDecimator (false)). The two are not bit identical (the float sums are in another order);
// the test bounds the difference, checks that the per voice history stays bit identical, and that a render where every gain ramps is still bit identical.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "osc/wt_osc.h"
#include "test_runner.h"

namespace synthyboy
{
namespace own
{
struct WtOscSharedDecimTest
{
    static bool sameHistory (const WtOsc& a, const WtOsc& b)
    {
        return a.history.size() == b.history.size() && (a.history.empty() || std::memcmp (a.history.data(), b.history.data(), a.history.size() * sizeof (float)) == 0);
    }
};
} // namespace own
} // namespace synthyboy

using namespace synthyboy::own;

namespace
{
struct TestRng
{
    uint64_t s;
    explicit TestRng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t> (s >> 16); }
    float uni() { return static_cast<float> (next() & 0xFFFFFF) / 16777216.0f; }
    float bi() { return uni() * 2.0f - 1.0f; }
};

std::unique_ptr<Wavetable> makeTable (TestRng& r)
{
    const int frames = 3;
    std::vector<std::vector<float>> cc (frames, std::vector<float> (Wavetable::kCycle / 2 + 1, 0.0f)), ss = cc;
    for (int f = 0; f < frames; ++f)
        for (int h = 1; h <= 300; ++h)
        {
            cc[static_cast<size_t> (f)][static_cast<size_t> (h)] = r.bi() / static_cast<float> (h);
            ss[static_cast<size_t> (f)][static_cast<size_t> (h)] = r.bi() / static_cast<float> (h);
        }
    auto t = std::make_unique<Wavetable>();
    t->build (cc, ss, true);
    return t;
}

struct Result
{
    double maxErr = 0.0;   // largest absolute difference of a sample
    double maxRef = 0.0;   // largest absolute sample of the per voice render
    long differing = 0, differingLater = 0, samples = 0; // differingLater: from the second control period of the note on (the first one has no previous gain, so every voice is steady)
    bool historyEqual = true;
};

enum class Motion { Held, Ramping, Mixed };

Result run (const Wavetable* wt, int voices, int warp0, int warp1, bool lowWarp, bool fastMath, Motion motion, uint64_t seed, int periods = 160)
{
    TestRng r (seed);
    Result res;
    WtOsc a, b;
    a.prepareMorph();
    b.prepareMorph();
    b.setSharedDecimator (false);
    Rng ra, rb;
    OscTick t;
    t.wt = wt;
    t.semis = 48.0f + 24.0f * r.uni();
    t.voices = voices;
    t.detuneSemis = 0.4f;
    t.curve = 1.0f;
    t.mode = 1;
    t.spread = 0.8f;
    t.blend = 0.7f;
    t.level = 0.6f;
    t.frame = 0.4f;
    t.pan = 0.2f;
    t.panRight = -0.2f;
    t.warp[0] = warp0;
    t.warp[1] = warp1;
    t.warpAmount[0] = 0.3f + 0.4f * r.uni();
    t.warpAmount[1] = 0.3f + 0.4f * r.uni();
    t.lowWarp = lowWarp;
    t.fastMath = fastMath;
    t.active = true;
    a.start (0.0f, 0.5f, ra);
    b.start (0.0f, 0.5f, rb);
    for (int per = 0; per < periods; ++per)
    {
        if (motion == Motion::Ramping || (motion == Motion::Mixed && per % 7 == 3))
        {
            t.level = 0.2f + 0.7f * r.uni();
            t.pan = r.bi();
            t.panRight = r.bi();
        }
        if (motion == Motion::Mixed && per % 11 == 5)
            t.spread = r.uni(); // moves the per voice pans: ramps on the voices of a lane
        a.update (t, 44100.0, ra);
        b.update (t, 44100.0, rb);
        int t0 = 0;
        while (t0 < kControlPeriod)
        {
            const int n = std::min (kControlPeriod - t0, per % 3 == 0 ? 32 : 5 + per % 13);
            float La[32], Ra[32], Lb[32], Rb[32];
            a.render (t0, n, La, Ra, nullptr, 44100.0);
            b.render (t0, n, Lb, Rb, nullptr, 44100.0);
            for (int k = 0; k < n; ++k)
            {
                res.maxErr = std::max ({ res.maxErr, std::fabs (static_cast<double> (La[k]) - Lb[k]), std::fabs (static_cast<double> (Ra[k]) - Rb[k]) });
                res.maxRef = std::max ({ res.maxRef, std::fabs (static_cast<double> (Lb[k])), std::fabs (static_cast<double> (Rb[k])) });
                res.differing += (La[k] != Lb[k]) + (Ra[k] != Rb[k]);
                if (per > 0)
                    res.differingLater += (La[k] != Lb[k]) + (Ra[k] != Rb[k]);
                res.samples += 2;
            }
            res.historyEqual = res.historyEqual && WtOscSharedDecimTest::sameHistory (a, b);
            t0 += n;
        }
    }
    return res;
}
} // namespace

TEST (own_wt_osc_shared_decimation_stays_within_float_rounding_of_the_per_voice_decimators)
{
    TestRng r (7);
    const auto table = makeTable (r);
    const int warps[][2] = { { kWarpSync, 0 }, { kWarpSyncVar, 0 }, { kWarpFmA, 0 }, { kWarpQuantize, 0 }, { kWarpFormant, kWarpBend }, { kWarpFlip, kWarpMirror }, { kWarpPulse, kWarpSync } };
    const int voiceCounts[] = { 1, 2, 3, 4, 7, 8, 16 };
    double worst = 0.0, worstRel = 0.0;
    long differing = 0, samples = 0;
    int cfg = 0;
    for (const auto& w : warps)
        for (const int voices : voiceCounts)
            for (int variant = 0; variant < 3; ++variant) // 4x exact sums, 4x fast math (four partial sums), 2x (32 taps)
            {
                const bool low = variant == 2, fast = variant != 0;
                const Result held = run (table.get(), voices, w[0], w[1], low, fast, Motion::Held, static_cast<uint64_t> (++cfg));
                const Result mixed = run (table.get(), voices, w[0], w[1], low, fast, Motion::Mixed, static_cast<uint64_t> (cfg) + 1000);
                const Result ramp = run (table.get(), voices, w[0], w[1], low, fast, Motion::Ramping, static_cast<uint64_t> (cfg) + 2000);
                for (const Result* x : { &held, &mixed, &ramp })
                {
                    worst = std::max (worst, x->maxErr);
                    worstRel = std::max (worstRel, x->maxRef > 0.0 ? x->maxErr / x->maxRef : 0.0);
                    differing += x->differing;
                    samples += x->samples;
                    CHECK (x->historyEqual); // the per voice history is the raw oversampled signal in both forms
                    CHECK (x->maxErr <= 2.0e-6 * x->maxRef); // float rounding of the reordered sums: about 1e-7 relative per voice, measured 9e-7 of the peak at most (peak up to +7 dBFS)
                }
                CHECK_EQ (ramp.differingLater, 0L); // every gain ramps in every later period: no voice is shared, the very bits of the per voice form
                if (voices < WtOsc::kSharedDecimatorMin || fast)
                    CHECK_EQ (held.differing, 0L); // and the Math economy / 2x decimators never share
            }
    std::printf ("wt_osc shared decimation: %d configurations x 3 motions, %ld samples, %ld differ, max abs error %.3g (%.1f dBFS), max relative to the block peak %.3g\n", cfg, samples, differing, worst,
                 20.0 * std::log10 (std::max (worst, 1e-30)), worstRel);
    CHECK (differing > 0); // the shared path really ran
}
