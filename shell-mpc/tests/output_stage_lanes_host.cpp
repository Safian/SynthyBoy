// SynthyBoy - MPC shell: the NEON form of the output stage (core/include/synthyboy/output_stage.h) and of the output peak (shell-mpc/output_peak.h) gives the samples and the peak of the scalar loops,
// to the bit: special inputs (silence, NaN, both infinities, both zeros, subnormals, full scale, huge values that overflow in the gain), quiet stretches (the idle path), random blocks, every length
// 0 .. 40, null channels and one buffer for both, gain changes in the middle of a run (the smoothing), the limiter switched on and off (the crossfade), resets, in and out of reduction, at three
// sample rates, in the default float mode and (AArch64) with flush-to-zero set as the shell sets it.
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

using synthyboy_mpc::OutputPeak;

static_assert (SYNTHYBOY_OUT_NEON == 1 && SYNTHYBOY_PEAK_NEON == 1, "this test is built with the NEON forms on");

// constant initialisation: both diagnostics objects can be built at compile time, so the namespace-scope globals need no dynamic-init guard
constexpr synthyboy_mpc::OutputPeak kConstPeak {};
constexpr synthyboy_mpc::NoteDiagnostics kConstNd {};

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

static long long compareStage (double sr, int runs, unsigned long long& groups)
{
    long long bad = 0, samples = 0;
    for (int run = 0; run < runs; ++run)
    {
        synthyboy::OutputStage fast, ref;
        ref.scalarOnly = true;
        const float gains[] = { 0.0f, 6.0f, -24.0f, 24.0f, 0.5f, -3.0f, 9.0f };
        float db = gains[run % 7];
        bool on = run % 3 != 0;
        for (auto* st : { &fast, &ref }) { st->setLimiter (on); st->prepare (sr); st->setGainDb (db); st->reset(); }
        int style = run % 5, quiet = 0;
        for (int blk = 0; blk < 120; ++blk)
        {
            if (blk % 9 == 4)   // a gain change in the middle of the run: the smoothing, sample by sample first
            {
                db = gains[rnd() % 7];
                fast.setGainDb (db); ref.setGainDb (db);
            }
            if (blk % 13 == 7)   // the switch: the crossfade, then the other path
            {
                on = ! on;
                fast.setLimiter (on); ref.setLimiter (on);
            }
            if (blk % 31 == 30) { fast.reset(); ref.reset(); }
            if (blk % 6 == 0) { style = (int) (rnd() % 5); quiet = (int) (rnd() % 3); }
            const int n = (blk % 5 == 0) ? (int) (rnd() % 41) : 128 - (int) (rnd() % 3);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            // quiet stretches (the idle path of the limiter) between the special and loud ones
            for (auto& v : l) v = quiet > 0 ? 0.02f * (uni() - 0.5f) : sampleOf (style);
            for (auto& v : r) v = quiet > 0 ? 0.02f * (uni() - 0.5f) : sampleOf ((style + blk) % 5);
            std::vector<float> l1 = l, r1 = r, l2 = l, r2 = r;
            const int mode = (run + blk) % 8;   // 0 .. 4 stereo, 5 left only, 6 right only, 7 the same buffer for both
            float* pl1 = mode == 6 ? nullptr : l1.data(); float* pr1 = mode == 5 ? nullptr : (mode == 7 ? l1.data() : r1.data());
            float* pl2 = mode == 6 ? nullptr : l2.data(); float* pr2 = mode == 5 ? nullptr : (mode == 7 ? l2.data() : r2.data());
            fast.process (pl1, pr1, n);
            ref.process (pl2, pr2, n);
            for (int i = 0; i < n; ++i)
            {
                const bool differ = bitsOf (l1[(size_t) i]) != bitsOf (l2[(size_t) i]) || bitsOf (r1[(size_t) i]) != bitsOf (r2[(size_t) i]);
                bad += differ;
                if (differ && bad < 4)
                    std::printf ("    differ: run %d block %d sample %d: in %08x %08x out %08x %08x vs %08x %08x\n", run, blk, i, bitsOf (l[(size_t) i]), bitsOf (r[(size_t) i]), bitsOf (l1[(size_t) i]), bitsOf (r1[(size_t) i]), bitsOf (l2[(size_t) i]), bitsOf (r2[(size_t) i]));
            }
            bad += bitsOf (fast.gainReductionDb()) != bitsOf (ref.gainReductionDb());
            bad += bitsOf (fast.lastGain()) != bitsOf (ref.lastGain());
            samples += n;
        }
        groups += fast.vectorGroups();
        bad += ref.vectorGroups() != 0u;
    }
    std::printf ("    %lld samples: %lld differ\n", samples, bad);
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
    char what[200];
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        unsigned long long groups = 0;
        const long long bad = compareStage (sr, 150, groups);
        std::snprintf (what, sizeof what, "%s, %.0f Hz: the stage (gain, look-ahead limiter on / off / switching, idle and limiting), NEON = scalar to the bit (%llu groups of four by NEON)", mode, sr, groups);
        check (bad == 0 && groups > 10000, what);
    }
    std::snprintf (what, sizeof what, "%s: output peak, NEON = fmax loop to the bit", mode);
    check (comparePeak (20000) == 0, what);
}

int main()
{
    check (&synthyboy_mpc::outputPeak() == &synthyboy_mpc::gOutputPeak, "outputPeak() returns the namespace-scope constant-initialised object");
    check (&synthyboy_mpc::noteDiagnostics() == &synthyboy_mpc::gNoteDiagnostics, "noteDiagnostics() returns the namespace-scope constant-initialised object");
    check (kConstPeak.blocks.load() == 0 && kConstNd.totalNotes.load() == 0, "the compile-time-built objects start at zero");
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
