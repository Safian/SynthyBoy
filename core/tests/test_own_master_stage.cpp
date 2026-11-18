// SynthyBoy - own engine tests: the master stage of the engine (dsp/master_stage.h: smoothed master volume, stereo routing, Direct sum, safety limit) gives, in its hoisted scalar form and in its NEON form,
// the samples of the per sample loop it replaced (stageReference), to the bit, and leaves the volume smoother in the same state. Random blocks and special values (NaN, infinities, both zeros, subnormals,
// values around the 1.8 knee, huge values), every length 1 .. 32, every position in the control period, ramping and settled volume, routing on and off, a Direct gain that moves or stays.
// Compiled with the NEON forms on (also on Apple's arm64) and without fused multiply-add; links nothing of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "dsp/master_stage.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
uint32_t gState = 7771u;
uint32_t next() { gState ^= gState << 13; gState ^= gState >> 17; gState ^= gState << 5; return gState; }
float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
float fromBits (uint32_t u) { float f; std::memcpy (&f, &u, 4); return f; }
uint32_t bitsOf (float f) { uint32_t u; std::memcpy (&u, &f, 4); return u; }

const float kSpecial[] = { 0.0f, -0.0f, 1.0f, -1.0f, 1.8f, -1.8f, 1.8000001f, -1.8000001f, 1.7999999f, 2.5f, -7.0f, 100.0f, 3.0e38f, -3.0e38f, std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::quiet_NaN(), 1.0e-40f, -1.0e-40f, 1.0e-20f, 0.9f };

float sampleOf (int style)
{
    switch (style)
    {
        case 0: return kSpecial[next() % (sizeof (kSpecial) / sizeof (kSpecial[0]))];
        case 1: return 1.6f * uni() - 0.8f;
        case 2: return 5.0f * uni() - 2.5f;        // often past the knee
        case 3: return fromBits (next());
        default: return next() % 12 == 0 ? kSpecial[next() % (sizeof (kSpecial) / sizeof (kSpecial[0]))] : 3.0f * (uni() - 0.5f);
    }
}

long long run (bool lanes, int reps, long long& samples)
{
    long long bad = 0;
    for (int rep = 0; rep < reps; ++rep)
    {
        LinearSmoother a, b;
        a.reset (44100.0, 0.02);
        b.reset (44100.0, 0.02);
        const float g0 = 0.05f + 3.0f * uni();
        a.setCurrentAndTarget (g0);
        b.setCurrentAndTarget (g0);
        int controlPos = 0;
        for (int blk = 0; blk < 80; ++blk)
        {
            if (blk % 9 == 2)   // a new target: a ramp of 882 samples starts
            {
                const float t = rep % 5 == 0 ? 0.0f : 0.05f + 3.0f * uni();
                a.setTarget (t);
                b.setTarget (t);
            }
            const int n = 1 + static_cast<int> (next() % 32);
            synthyboy::own::master::Params p;
            const int rmode = (rep + blk) % 4;   // routing off (exact zero), on, on with the -3 dB centre, off again
            p.routeSin = rmode == 0 || rmode == 3 ? 0.0f : (rmode == 1 ? 0.7071068f * uni() : 0.7071068f);
            p.routeCos = rmode == 0 || rmode == 3 ? 1.0f : std::sqrt (1.0f - p.routeSin * p.routeSin);
            p.directNow = (blk % 3 == 0) ? 0.0f : uni() * 2.0f;
            p.directPrev = (blk % 4 == 0) ? p.directNow : ((rep % 7 == 0 && blk % 5 == 0) ? -0.0f : uni() * 2.0f);
            p.controlPos = controlPos;
            const int style = (rep + blk / 3) % 5;
            std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n)), dl (static_cast<size_t> (n)), dr (static_cast<size_t> (n));
            for (auto& v : l) v = sampleOf (style);
            for (auto& v : r) v = sampleOf ((style + blk) % 5);
            for (auto& v : dl) v = sampleOf ((style + 1) % 5);
            for (auto& v : dr) v = sampleOf ((style + 2) % 5);
            std::vector<float> l1 = l, r1 = r;
            synthyboy::own::master::stageReference (l.data(), r.data(), dl.data(), dr.data(), n, p, a);
            synthyboy::own::master::stage (l1.data(), r1.data(), dl.data(), dr.data(), n, p, b, lanes);
            for (int i = 0; i < n; ++i)
            {
                const bool d = bitsOf (l[static_cast<size_t> (i)]) != bitsOf (l1[static_cast<size_t> (i)]) || bitsOf (r[static_cast<size_t> (i)]) != bitsOf (r1[static_cast<size_t> (i)]);
                bad += d;
                if (d && bad < 4)
                    std::printf ("    differ: rep %d blk %d i %d: %08x %08x vs %08x %08x\n", rep, blk, i, bitsOf (l[static_cast<size_t> (i)]), bitsOf (r[static_cast<size_t> (i)]), bitsOf (l1[static_cast<size_t> (i)]), bitsOf (r1[static_cast<size_t> (i)]));
            }
            bad += (a.settled() != b.settled()) || bitsOf (a.targetValue()) != bitsOf (b.targetValue());
            LinearSmoother pa = a, pb = b;   // the same state: the next samples are the same
            for (int k = 0; k < 3; ++k)
                bad += bitsOf (pa.next()) != bitsOf (pb.next());
            samples += n;
            controlPos = (controlPos + n) % kControlPeriod;
        }
    }
    return bad;
}
} // namespace

TEST (own_master_stage_hoisted_scalar_form_is_the_per_sample_loop)
{
    long long samples = 0;
    const long long bad = run (false, 600, samples);
    std::printf ("    %lld samples: %lld differ\n", samples, bad);
    CHECK_EQ (bad, 0);
}

TEST (own_master_stage_neon_form_is_the_per_sample_loop)
{
    static_assert (SB_NEON == 1, "this test is built with the NEON forms on");
    long long samples = 0;
    const long long bad = run (true, 600, samples);
    std::printf ("    %lld samples: %lld differ\n", samples, bad);
    CHECK_EQ (bad, 0);
}

#if defined(__aarch64__)
TEST (own_master_stage_neon_form_is_the_per_sample_loop_with_flush_to_zero_set)
{
    uint64_t saved;
    asm volatile ("mrs %0, fpcr" : "=r"(saved));
    const uint64_t fz = saved | (1ull << 24);   // FPCR.FZ: what the MPC shell sets around the audio callback
    asm volatile ("msr fpcr, %0" :: "r"(fz) : "memory");
    long long samples = 0;
    const long long bad = run (true, 400, samples);
    asm volatile ("msr fpcr, %0" :: "r"(saved) : "memory");
    std::printf ("    %lld samples: %lld differ\n", samples, bad);
    CHECK_EQ (bad, 0);
}
#endif
