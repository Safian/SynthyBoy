// SynthyBoy - MPC shell: the NEON form of the output stage (shell-mpc/output_stage.h) and of the output peak (shell-mpc/output_peak.h) gives the samples and the peak of the scalar loops, to the bit:
// the plain stage and the limiter stage, over special inputs (silence, NaN, both infinities, both zeros, subnormals, full scale, huge values that overflow in the gain), random blocks, every length 0 .. 40,
// null channels, gain changes in the middle of a run (the smoothing), the limiter in and out of reduction, in the default float mode and (AArch64) with flush-to-zero set as the shell sets it.
// Built with SB_TEST_NEON (the NEON forms on any ARM host). Run by tests/run_output_stage_test.sh.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "output_peak.h"
#include "output_stage.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using synthyboy_mpc::BasicOutputStage;
using synthyboy_mpc::OutputPeak;

static_assert (SYNTHYBOY_OUT_NEON == 1 && SYNTHYBOY_PEAK_NEON == 1, "this test is built with the NEON forms on");

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }

static uint32_t rngState = 20261118u;
static uint32_t rnd() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return rngState; }
static float uni() { return (float) (rnd() >> 8) * (1.0f / 16777216.0f); }
static float fromBits (uint32_t u) { float f; std::memcpy (&f, &u, 4); return f; }
static uint32_t bitsOf (float f) { uint32_t u; std::memcpy (&u, &f, 4); return u; }

static const float kSpecial[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 0.891251f, 0.89125f, 0.8913f, -0.8913f, 2.0f, -3.0f, 1.0e30f, 3.0e38f, -3.0e38f, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                  std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::quiet_NaN(), 1.0e-40f, -1.0e-40f, 1.0e-20f, 1.1754944e-38f };

static float sampleOf (int style)
{
    switch (style)
    {
        case 0: return kSpecial[rnd() % (sizeof (kSpecial) / sizeof (kSpecial[0]))];
        case 1: return 1.6f * uni() - 0.8f;                    // under the ceiling: the limiter's quiet path
        case 2: return 4.0f * uni() - 2.0f;                    // over it often
        case 3: return fromBits (rnd());                       // any bit pattern
        default: return rnd() % 16 == 0 ? kSpecial[rnd() % (sizeof (kSpecial) / sizeof (kSpecial[0]))] : 0.7f * (uni() - 0.5f);
    }
}

#if defined(__aarch64__)
static uint64_t readFpcr() { uint64_t v; asm volatile ("mrs %0, fpcr" : "=r"(v)); return v; }
static void writeFpcr (uint64_t v) { asm volatile ("msr fpcr, %0" :: "r"(v) : "memory"); }
#endif

template <bool Lim>
static long long compareStage (double sr, int runs)
{
    long long bad = 0, samples = 0;
    for (int run = 0; run < runs; ++run)
    {
        BasicOutputStage<Lim> fast, ref;
        ref.scalarOnly = true;
        fast.prepare (sr);
        ref.prepare (sr);
        const float gains[] = { 0.0f, 6.0f, -24.0f, 24.0f, 0.5f, -3.0f };
        float db = gains[run % 6];
        fast.setGainDb (db); ref.setGainDb (db);
        fast.reset(); ref.reset();
        for (int blk = 0; blk < 60; ++blk)
        {
            if (blk % 7 == 3)   // a gain change in the middle of the run: the smoothing, sample by sample first
            {
                db = gains[rnd() % 6];
                fast.setGainDb (db); ref.setGainDb (db);
            }
            const int n = (blk % 5 == 0) ? (int) (rnd() % 41) : 128 - (int) (rnd() % 3);
            const int style = (run + blk / 4) % 5;
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (auto& v : l) v = sampleOf (style);
            for (auto& v : r) v = sampleOf ((style + blk) % 5);
            std::vector<float> l1 = l, r1 = r, l2 = l, r2 = r;
            const int mode = (run + blk) % 7;   // 0 .. 4 stereo, 5 left only, 6 right only
            float* pl1 = mode == 6 ? nullptr : l1.data(); float* pr1 = mode == 5 ? nullptr : r1.data();
            float* pl2 = mode == 6 ? nullptr : l2.data(); float* pr2 = mode == 5 ? nullptr : r2.data();
            fast.process (pl1, pr1, n);
            ref.process (pl2, pr2, n);
            for (int i = 0; i < n; ++i)
            {
                const bool differ = bitsOf (l1[(size_t) i]) != bitsOf (l2[(size_t) i]) || bitsOf (r1[(size_t) i]) != bitsOf (r2[(size_t) i]);
                bad += differ;
                if (differ && bad < 4)
                    std::printf ("    differ: lim %d run %d block %d sample %d: in %08x %08x out %08x %08x vs %08x %08x\n", (int) Lim, run, blk, i, bitsOf (l[(size_t) i]), bitsOf (r[(size_t) i]), bitsOf (l1[(size_t) i]), bitsOf (r1[(size_t) i]), bitsOf (l2[(size_t) i]), bitsOf (r2[(size_t) i]));
            }
            bad += bitsOf (fast.gainReductionDb()) != bitsOf (ref.gainReductionDb());
            samples += n;
        }
    }
    std::printf ("    %lld samples (%s): %lld differ\n", samples, Lim ? "limiter" : "plain", bad);
    return bad;
}

/** A signalling NaN made quiet: the peak sees the output of the stage, which has no NaN at all; and the libm fmax of AArch64 answers a signalling NaN with a quiet one that then eats the maximum so far (a library
    property of the old loop that no real signal reaches), so the comparison is made with quiet NaNs. */
static float quiet (float f)
{
    uint32_t u = bitsOf (f);
    if ((u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0u)
        u |= 0x00400000u;
    return fromBits (u);
}

static long long comparePeak (int reps)
{
    long long bad = 0;
    for (int rep = 0; rep < reps; ++rep)
    {
        const int n = rep % 41, off = rep % 3, style = rep % 5;
        std::vector<float> l ((size_t) n + 4), r ((size_t) n + 4);
        for (auto& v : l) v = quiet (sampleOf (style));
        for (auto& v : r) v = quiet (sampleOf ((style + rep / 5) % 5));
        const int m = n > off ? n - off : 0;
        const bool d1 = bitsOf (OutputPeak::peakOf (l.data() + off, r.data() + off, m)) != bitsOf (OutputPeak::peakScalar (l.data() + off, r.data() + off, m));
        if (d1 && bad < 3) std::printf ("    peak differs: n %d style %d: %08x vs %08x\n", m, style, bitsOf (OutputPeak::peakOf (l.data() + off, r.data() + off, m)), bitsOf (OutputPeak::peakScalar (l.data() + off, r.data() + off, m)));
        bad += d1;
        bad += bitsOf (OutputPeak::peakOf (l.data() + off, nullptr, m)) != bitsOf (OutputPeak::peakScalar (l.data() + off, nullptr, m));
    }
    std::printf ("    %d peak blocks: %lld differ\n", reps, bad);
    return bad;
}

static void runAll (const char* mode)
{
    char what[160];
    for (double sr : { 44100.0, 48000.0 })
    {
        std::snprintf (what, sizeof what, "%s, %.0f Hz: plain stage, NEON = scalar to the bit", mode, sr);
        check (compareStage<false> (sr, 120) == 0, what);
        std::snprintf (what, sizeof what, "%s, %.0f Hz: limiter stage, NEON = scalar to the bit", mode, sr);
        check (compareStage<true> (sr, 120) == 0, what);
    }
    std::snprintf (what, sizeof what, "%s: output peak, NEON = fmax loop to the bit", mode);
    check (comparePeak (20000) == 0, what);
}

int main()
{
    runAll ("default float mode");
#if defined(__aarch64__)
    const uint64_t saved = readFpcr();
    writeFpcr (saved | (1ull << 24));   // FPCR.FZ: what the shell sets around the audio callback
    runAll ("flush to zero");
    writeFpcr (saved);
#endif
    std::printf ("%s\n", failures == 0 ? "output_stage_lanes_host: OK" : "output_stage_lanes_host: FAILED");
    return failures == 0 ? 0 : 1;
}
