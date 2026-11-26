// SynthyBoy - own engine tests: the Compressor's look-ahead limiter. CPU review item M1 (docs/release/CPU-REVIEW-MEMORY.md): no `% cap` library calls; CPU review item 1 (2026-11-26): the
// minimum over the look-ahead is a monotone deque and the box filter a running sum in double. The old process () stays selectable (Limiter::reference / setReferenceLimiter); the two run in
// step on random and special inputs, every look-ahead and capacity, block splits and parameter changes. The state of the min filter and the release (the required gain ring, the released gain
// ring, the held input) is compared with memcmp (bit identical); the box filter gain, and with it the output, within 2e-6 relative (measured worst case printed; the sum is not bit identical).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "dsp/fastmath.h"
#include "fx/fx_compressor.h"
#include "test_runner.h"

TEST (fast_exp2_preserves_nan_and_saturates_infinities)
{
    using synthyboy::own::fastmath::exp2;
    volatile float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK (std::isnan (exp2 (nan)));
    CHECK (exp2 (-std::numeric_limits<float>::infinity()) == 0.0f);
    CHECK (exp2 (std::numeric_limits<float>::infinity()) == exp2 (120.0f));
}

namespace synthyboy
{
namespace own
{
struct CompressorOptTest
{
    using Limiter = Compressor::Limiter;
    static bool sameLimiter (const Limiter& a, const Limiter& b)
    {
        return a.pos == b.pos && a.cap == b.cap && std::memcmp (&a.relGain, &b.relGain, 4) == 0 && std::memcmp (&a.prevL, &b.prevL, 4) == 0 && std::memcmp (&a.prevR, &b.prevR, 4) == 0
               && std::memcmp (&a.prev2L, &b.prev2L, 4) == 0 && std::memcmp (&a.prev2R, &b.prev2R, 4) == 0
               && std::memcmp (a.xl.data(), b.xl.data(), a.xl.size() * 4) == 0 && std::memcmp (a.xr.data(), b.xr.data(), a.xr.size() * 4) == 0
               && std::memcmp (a.req.data(), b.req.data(), a.req.size() * 4) == 0 && std::memcmp (a.g1.data(), b.g1.data(), a.g1.size() * 4) == 0;
    }
    static Limiter& limiter (Compressor& c, int b) { return c.lim[b]; }
    static int lookAhead (const Compressor& c) { return c.lookAhead; }
};
} // namespace own
} // namespace synthyboy

using namespace synthyboy::own;

namespace
{
float bitsToFloat (uint32_t u)
{
    float f;
    std::memcpy (&f, &u, 4);
    return f;
}

float special (std::mt19937& rng)
{
    static const float v[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 3.0e38f, -3.0e38f, 1.0e-39f, -1.0e-39f, 1.17549435e-38f, 1.0e-30f, 2.0f, 100.0f,
                               std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::denorm_min(), 1.0e-20f };
    if (rng() % 11 == 0)
        return bitsToFloat (rng()); // any bit pattern
    return v[rng() % (sizeof v / sizeof v[0])];
}

/** One input sample: mostly audio, with bursts and special values at the given rate (0: none). */
float sampleOf (std::mt19937& rng, int specialRate, float amp)
{
    if (specialRate > 0 && static_cast<int> (rng() % static_cast<unsigned> (specialRate)) == 0)
        return special (rng);
    return amp * (static_cast<float> (rng() % 200001) / 100000.0f - 1.0f);
}

bool sameBits (const float* a, const float* b, size_t n) { return std::memcmp (a, b, n * sizeof (float)) == 0; }

/** The bound of the running sum against the serial float sum of the reference, relative: the error of the serial sum grows with the window (3e-6 up to the 72 samples of 48 kHz: 1.4e-6 measured on a stack of the factory
    range, 2.3e-6 in the harshest scenario below; 6e-8 per window sample beyond), the double sum is the accurate one. */
float gainBound (int la) { return std::max (3.0e-6f, 6.0e-8f * static_cast<float> (la + 1)); }
double gWorstGainRel = 0.0;

bool closeVal (float a, float b, float rel, float absTol, double* worst = nullptr)
{
    if (std::isnan (a) || std::isnan (b))
        return std::isnan (a) && std::isnan (b);
    if (a == b)
        return true;
    if (std::isinf (a) || std::isinf (b))
        return false;
    const double d = std::fabs (static_cast<double> (a) - static_cast<double> (b)), m = std::max (std::fabs (static_cast<double> (a)), std::fabs (static_cast<double> (b)));
    if (worst != nullptr && d > absTol)
        *worst = std::max (*worst, d / m);
    return d <= static_cast<double> (rel) * m + static_cast<double> (absTol);
}

/** One limiter step of the reference and the new code: the gains and the outputs agree within the bound. */
bool sameOut (const CompressorOptTest::Limiter& a, const CompressorOptTest::Limiter& b, const float* ao, const float* bo, int la)
{
    const float tol = gainBound (la);
    return closeVal (a.lastGain, b.lastGain, tol, 1.0e-30f, &gWorstGainRel) && closeVal (ao[0], bo[0], tol, 1.0e-30f) && closeVal (ao[1], bo[1], tol, 1.0e-30f);
}

/** Two blocks of the whole compressor: every sample within the bound of the limiter's gain (the dry delay is exact, the mix and the makeup are products). */
bool closeBlock (const float* a, const float* b, size_t n, float peakIn, int la)
{
    const float tol = 2.0f * gainBound (la);
    for (size_t i = 0; i < n; ++i)
        if (!closeVal (a[i], b[i], tol, 1.0e-5f * std::max (1.0f, std::isfinite (peakIn) ? peakIn : 1.0f)))
            return false;
    return true;
}
} // namespace

// The limiter alone: every look-ahead length and every capacity from the look-ahead + 1 on (non power of two included), random and special input, a ceiling and a release that change between
// samples, a look-ahead that changes between blocks, and the case of a look-ahead that is not below the capacity (the reference path).
TEST (limiter_matches_reference_for_every_length_and_capacity)
{
    std::mt19937 rng (12345);
    int runs = 0;
    for (int la = 1; la <= 200; la += (la < 40 ? 1 : 7))
        for (int extra : { 1, 2, 7, 8, 9, 20 })
        {
            const int cap = la + extra;
            for (int variant = 0; variant < 3; ++variant)
            {
                CompressorOptTest::Limiter a, b;
                a.prepare (cap);
                b.prepare (cap);
                a.reference = true;
                const int specialRate = variant == 0 ? 0 : (variant == 1 ? 300 : 20);
                bool ok = true;
                int laNow = la;
                const int n = 2 * cap + 600;
                for (int i = 0; i < n && ok; ++i)
                {
                    if (i % 97 == 0)
                        laNow = 1 + static_cast<int> (rng() % static_cast<unsigned> (la + 1)); // the look-ahead may change between blocks (0 < laNow <= la < cap)
                    const float amp = (i / 150) % 3 == 0 ? 0.2f : ((i / 150) % 3 == 1 ? 3.0f : 40.0f);
                    const float inL = sampleOf (rng, specialRate, amp), inR = sampleOf (rng, specialRate, amp);
                    float ceilAmp = 0.1f + static_cast<float> (rng() % 400) / 100.0f;
                    if (variant == 2 && rng() % 40 == 0)
                        ceilAmp = special (rng);
                    const float rel = static_cast<float> (rng() % 1000) / 1000.0f * (rng() % 16 == 0 ? 1.0f : 0.05f);
                    float ao[2], bo[2];
                    a.process (inL, inR, ceilAmp, rel, laNow, ao[0], ao[1]);
                    b.process (inL, inR, ceilAmp, rel, laNow, bo[0], bo[1]);
                    ok = sameOut (a, b, ao, bo, laNow) && CompressorOptTest::sameLimiter (a, b);
                    if (!ok)
                        std::printf ("limiter differs: la %d cap %d variant %d sample %d (%s) gain %.9g reference %.9g\n", laNow, cap, variant, i, sameOut (a, b, ao, bo, laNow) ? "state" : "gain / output", static_cast<double> (b.lastGain), static_cast<double> (a.lastGain));
                }
                CHECK (ok);
                ++runs;
            }
        }
    std::printf ("limiter runs: %d\n", runs);
}

TEST (limiter_with_look_ahead_not_below_capacity_uses_the_old_indexing)
{
    std::mt19937 rng (777);
    for (int cap : { 8, 9, 13, 64, 75 })
        for (int la : { cap, cap + 1, cap + 5, 3 * cap })
        {
            CompressorOptTest::Limiter a, b;
            a.prepare (cap);
            b.prepare (cap);
            a.reference = true;
            bool ok = true;
            for (int i = 0; i < 4 * cap + 50 && ok; ++i)
            {
                const float inL = sampleOf (rng, 50, 4.0f), inR = sampleOf (rng, 50, 4.0f);
                float ao[2], bo[2];
                a.process (inL, inR, 1.0f, 0.01f, la, ao[0], ao[1]);
                b.process (inL, inR, 1.0f, 0.01f, la, bo[0], bo[1]);
                ok = sameOut (a, b, ao, bo, la) && CompressorOptTest::sameLimiter (a, b);
            }
            CHECK (ok);
        }
}

// A single special value among silence and among loud audio: NaN, infinities, signed zeros, subnormals at every ring position (so the odd value enters and leaves the window at every offset).
TEST (limiter_special_value_at_every_position)
{
    const float vals[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f, 1.0e-39f, 3.0e38f, -3.0e38f, 1.0e-37f };
    for (int la : { 1, 2, 5, 66, 72 })
        for (float v : vals)
            for (int at = 0; at < la + 12; ++at)
                for (int loud = 0; loud < 2; ++loud)
                {
                    const int cap = la + 8;
                    CompressorOptTest::Limiter a, b;
                    a.prepare (cap);
                    b.prepare (cap);
                    a.reference = true;
                    std::mt19937 rng (static_cast<unsigned> (la * 1000 + at));
                    bool ok = true;
                    for (int i = 0; i < 3 * cap + 20 && ok; ++i)
                    {
                        const float x = i == at + cap ? v : (loud ? 2.0f * (static_cast<float> (rng() % 2001) / 1000.0f - 1.0f) : 0.0f);
                        float ao[2], bo[2];
                        a.process (x, -x, 0.8f, 0.02f, la, ao[0], ao[1]);
                        b.process (x, -x, 0.8f, 0.02f, la, bo[0], bo[1]);
                        ok = sameOut (a, b, ao, bo, la) && CompressorOptTest::sameLimiter (a, b);
                    }
                    CHECK (ok);
                }
}

// The bound of the running sum (CPU review item 1): a long run of audio that limits hard, with the release of the factory range, against the serial float sum of the reference. The gain
// of every sample is compared (not only the output, which is the gain times a held input that may be zero), and the state of the min filter and the release memcmp. The running sum is
// also compared with the exact sum of the window (long double): it is the accurate one, the serial float sum is what drifts (the more the longer the window).
TEST (limiter_running_sum_stays_within_the_bound_of_the_serial_sum)
{
    std::mt19937 rng (2026);
    double worstRef[2] = { 0.0, 0.0 }, worstExact = 0.0; // vs the reference: up to the 72 samples of 48 kHz, longer windows; the running sum vs the exact sum
    long long samples = 0;
    for (int la : { 1, 2, 7, 33, 66, 72, 144 })
        for (float ceilAmp : { 0.05f, 0.5f, 0.9f })
            for (float relCoef : { 1.0f / 5.0f, 1.0f / 1800.0f, 1.0f / 40000.0f })
            {
                const int cap = la + 8;
                CompressorOptTest::Limiter a, b;
                a.prepare (cap);
                b.prepare (cap);
                a.reference = true;
                bool ok = true;
                for (int i = 0; i < 24000 && ok; ++i)
                {
                    // bursts of loud audio between quiet stretches: the gain moves in the whole range, and sits at exactly 1 in between
                    const bool loud = (i / 1500) % 3 == 1 || (i / 97) % 11 == 0;
                    const float amp = loud ? 4.0f : 0.02f;
                    const float inL = amp * (static_cast<float> (rng() % 200001) / 100000.0f - 1.0f), inR = amp * (static_cast<float> (rng() % 200001) / 100000.0f - 1.0f);
                    float ao[2], bo[2];
                    if (i % 32 == 0)
                        b.resumNext(); // what Compressor::process does at the start of every call
                    a.process (inL, inR, ceilAmp, relCoef, la, ao[0], ao[1]);
                    b.process (inL, inR, ceilAmp, relCoef, la, bo[0], bo[1]);
                    ++samples;
                    ok = sameOut (a, b, ao, bo, la) && CompressorOptTest::sameLimiter (a, b);
                    long double exact = 0.0L;
                    for (int k = 0; k <= la; ++k)
                        exact += static_cast<long double> (b.g1[static_cast<size_t> ((b.pos - k + 4 * cap) % cap)]);
                    exact /= static_cast<long double> (la + 1);
                    worstExact = std::max (worstExact, static_cast<double> (std::fabs ((static_cast<long double> (b.lastGain) - exact) / exact)));
                    worstRef[la > 72 ? 1 : 0] = std::max (worstRef[la > 72 ? 1 : 0], std::fabs (static_cast<double> (a.lastGain) - static_cast<double> (b.lastGain)) / static_cast<double> (a.lastGain));
                }
                CHECK (ok);
            }
    std::printf ("limiter running sum over %lld samples: worst relative gain difference to the serial float sum %.3g (windows up to 73 samples; bound 3e-6) and %.3g (up to 145; bound 6e-8 per sample), to the exact sum %.3g\n",
                 samples, worstRef[0], worstRef[1], worstExact);
    CHECK (worstRef[0] <= 3.0e-6);
    CHECK (worstRef[1] <= 6.0e-8 * 145.0);
    CHECK (worstExact <= 2.0e-7); // one float rounding of the sum and one of the quotient
}

// The exact fast paths: no required gain below 1 in the window gives the minimum 1.0f, and a released gain of exactly 1.0f for a whole window gives the gain exactly 1.0f (the same bits as the
// reference), also at the first limited sample and again after a reset (the release stalls at 1 - 2^-24 after a limit, so exact ones occur from a reset to the first limit only).
TEST (limiter_exact_fast_paths_give_exactly_one)
{
    for (int la : { 1, 5, 66 })
    {
        const int cap = la + 8;
        CompressorOptTest::Limiter a, b;
        a.prepare (cap);
        b.prepare (cap);
        a.reference = true;
        int exactOnes = 0;
        for (int i = 0; i < 40000; ++i)
        {
            if (i == 20000)
            {
                a.reset();
                b.reset();
            }
            // 3000 samples below the ceiling, then 400 of loud audio, then quiet again; the same after the reset
            const int ph = i % 20000;
            const float amp = ph < 3000 ? 0.1f : (ph < 3400 ? 3.0f : 0.1f);
            const float inL = amp * std::sin (0.07f * static_cast<float> (i)), inR = 0.5f * inL;
            float ao[2], bo[2];
            if (i % 32 == 0)
                b.resumNext();
            a.process (inL, inR, 0.5f, 0.05f, la, ao[0], ao[1]);
            b.process (inL, inR, 0.5f, 0.05f, la, bo[0], bo[1]);
            CHECK (sameOut (a, b, ao, bo, la) && CompressorOptTest::sameLimiter (a, b));
            if (ph < 3000)
            {
                ++exactOnes;
                CHECK (b.lastGain == 1.0f);
                CHECK (a.lastGain == 1.0f); // (the serial sum of la + 1 ones is exact too)
                CHECK (std::memcmp (ao, bo, 8) == 0);
            }
        }
        CHECK (exactOnes == 6000);
    }
}

// The whole Compressor, every mode, band setting and sample rate: random blocks of odd lengths, parameter changes between the blocks.
TEST (compressor_matches_reference_over_blocks_and_parameter_changes)
{
    std::mt19937 rng (4242);
    auto rnd = [&] (float lo, float hi) { return lo + (hi - lo) * static_cast<float> (rng() % 10001) / 10000.0f; };
    int blocks = 0;
    for (double sr : { 8000.0, 22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int pass = 0; pass < 3; ++pass)
        {
            Compressor a, b;
            a.prepare (sr);
            b.prepare (sr);
            a.setReferenceLimiter (true);
            bool ok = true;
            for (int blk = 0; blk < 14 && ok; ++blk)
            {
                Compressor::Params p;
                p.mix = rnd (0.0f, 1.0f);
                p.attack = rnd (0.0f, 1.0f);
                p.release = rnd (0.0f, 1.0f);
                p.crossLow = rnd (60.0f, 400.0f);
                p.crossHigh = rnd (1000.0f, 6000.0f);
                p.bands = static_cast<int> (rng() % 4);
                p.mode = (blk % 4 == 3) ? static_cast<int> (rng() % 3) : 1; // mostly limit
                for (auto& bd : p.band)
                {
                    bd.lowerDb = rnd (-60.0f, -10.0f);
                    bd.upperDb = rnd (-30.0f, 6.0f);
                    bd.lowerRatio = rnd (-1.0f, 1.0f);
                    bd.upperRatio = rnd (0.0f, 1.0f);
                    bd.gainDb = rnd (-6.0f, 12.0f);
                }
                a.set (p);
                b.set (p);
                const bool fast = rng() % 2 != 0;
                a.fast = b.fast = fast;
                const int n = 1 + static_cast<int> (rng() % 333);
                std::vector<float> la (static_cast<size_t> (n)), ra (static_cast<size_t> (n));
                const float amp = pass == 0 ? 0.3f : (pass == 1 ? 2.0f : 30.0f);
                for (int i = 0; i < n; ++i)
                {
                    la[static_cast<size_t> (i)] = sampleOf (rng, pass == 2 ? 2000 : 0, amp) * std::sin (0.05f * static_cast<float> (i));
                    ra[static_cast<size_t> (i)] = sampleOf (rng, pass == 2 ? 2000 : 0, amp);
                }
                std::vector<float> lb = la, rb = ra;
                float peakIn = 0.0f;
                for (int i = 0; i < n; ++i)
                    if (std::isfinite (la[static_cast<size_t> (i)]) && std::isfinite (ra[static_cast<size_t> (i)]))
                        peakIn = std::max ({ peakIn, std::fabs (la[static_cast<size_t> (i)]), std::fabs (ra[static_cast<size_t> (i)]) });
                a.process (la.data(), ra.data(), n);
                b.process (lb.data(), rb.data(), n);
                ++blocks;
                const int look = CompressorOptTest::lookAhead (a);
                ok = closeBlock (la.data(), lb.data(), la.size(), peakIn, look) && closeBlock (ra.data(), rb.data(), ra.size(), peakIn, look);
                for (int k = 0; k < 3; ++k)
                    ok = ok && CompressorOptTest::sameLimiter (CompressorOptTest::limiter (a, k), CompressorOptTest::limiter (b, k));
                if (!ok)
                    std::printf ("compressor differs: sr %g pass %d block %d mode %d bands %d n %d\n", sr, pass, blk, p.mode, p.bands, n);
            }
            CHECK (ok);
        }
    std::printf ("compressor blocks compared: %d\n", blocks);
}

// A reset in the middle of a run and a second prepare () with another rate keep the two in step.
TEST (compressor_reset_and_reprepare)
{
    std::mt19937 rng (99);
    Compressor a, b;
    a.setReferenceLimiter (true);
    bool ok = true;
    for (double sr : { 48000.0, 44100.0, 96000.0 })
    {
        a.prepare (sr);
        b.prepare (sr);
        a.setReferenceLimiter (true);
        Compressor::Params p;
        p.mode = 1;
        p.bands = 3;
        p.band[1].upperDb = -6.0f;
        a.set (p);
        b.set (p);
        for (int blk = 0; blk < 6; ++blk)
        {
            if (blk == 3)
            {
                a.reset();
                b.reset();
            }
            std::vector<float> la (200), ra (200);
            for (int i = 0; i < 200; ++i) { la[static_cast<size_t> (i)] = sampleOf (rng, 0, 1.5f); ra[static_cast<size_t> (i)] = sampleOf (rng, 0, 1.5f); }
            std::vector<float> lb = la, rb = ra;
            a.process (la.data(), ra.data(), 200);
            b.process (lb.data(), rb.data(), 200);
            ok = ok && closeBlock (la.data(), lb.data(), 200, 1.5f, Compressor::lookAheadFor (sr)) && closeBlock (ra.data(), rb.data(), 200, 1.5f, Compressor::lookAheadFor (sr));
        }
    }
    CHECK (ok);
}

// Timing (printed, not asserted): the limit mode of one band over 128 sample blocks at 48 kHz, old against new.
TEST (compressor_limiter_timing_report)
{
    using clock = std::chrono::steady_clock;
    double ms[2] = { 0, 0 };
    for (int mode = 0; mode < 2; ++mode)
    {
        Compressor c;
        c.prepare (48000.0);
        c.setReferenceLimiter (mode == 0);
        Compressor::Params p;
        p.mode = 1;
        p.bands = 3;
        p.band[1].upperDb = -12.0f;
        c.set (p);
        std::vector<float> l (128), r (128);
        std::mt19937 rng (5);
        for (auto& x : l) x = sampleOf (rng, 0, 1.0f);
        r = l;
        const auto t0 = clock::now();
        for (int i = 0; i < 4000; ++i)
        {
            std::vector<float> tl = l, tr = r;
            c.process (tl.data(), tr.data(), 128);
        }
        ms[mode] = std::chrono::duration<double, std::milli> (clock::now() - t0).count() / 4000.0;
    }
    std::printf ("limiter block (128 samples, 1 band): old %.4f ms, new %.4f ms\n", ms[0], ms[1]);
}

// Multiband parallel mix phase alignment across crossover frequencies (audit part 2, rank 8).
// Follows the compressor setup and block processing of compressor_matches_reference_over_blocks_and_parameter_changes.
TEST (compressor_multiband_parallel_mix_has_no_crossover_notch)
{
    constexpr double kPi = 3.14159265358979323846;
    auto measureRatio = [] (int bands, int mode, double f) -> double {
        const int totalSamples = 9600;
        const int blockSize = 64;
        std::vector<float> inL (static_cast<size_t> (totalSamples));
        std::vector<float> inR (static_cast<size_t> (totalSamples));
        for (int i = 0; i < totalSamples; ++i)
        {
            const double phase = 2.0 * kPi * f * static_cast<double> (i) / 48000.0;
            const float s = static_cast<float> (std::sin (phase));
            inL[static_cast<size_t> (i)] = s;
            inR[static_cast<size_t> (i)] = s;
        }

        Compressor comp;
        comp.prepare (48000.0);
        Compressor::Params p;
        p.mix = 0.5f;
        p.crossLow = 160.0f;
        p.crossHigh = 2500.0f;
        p.bands = bands;
        p.mode = mode;
        for (auto& bd : p.band)
        {
            bd.upperDb = 12.0f;
            bd.lowerDb = -100.0f;
            bd.upperRatio = 0.0f;
            bd.lowerRatio = 0.0f;
            bd.gainDb = 0.0f;
        }
        comp.set (p);

        std::vector<float> outL = inL;
        std::vector<float> outR = inR;
        for (int offset = 0; offset < totalSamples; offset += blockSize)
            comp.process (outL.data() + offset, outR.data() + offset, blockSize);

        const int startSample = 4800;
        const int endSample = 9600;
        const int count = endSample - startSample;
        double inSumSq = 0.0;
        double outSumSq = 0.0;
        for (int i = startSample; i < endSample; ++i)
        {
            inSumSq += static_cast<double> (inL[static_cast<size_t> (i)]) * static_cast<double> (inL[static_cast<size_t> (i)]);
            outSumSq += static_cast<double> (outL[static_cast<size_t> (i)]) * static_cast<double> (outL[static_cast<size_t> (i)]);
        }
        const double inRms = std::sqrt (inSumSq / static_cast<double> (count));
        const double outRms = std::sqrt (outSumSq / static_cast<double> (count));
        return outRms / inRms;
    };

    // 1. Multiband (bands = 0), mode = 0 (Compress), f = 2500 Hz
    {
        const double ratio = measureRatio (0, 0, 2500.0);
        CHECK (std::fabs (ratio - 1.0) < 0.02);
    }
    // 2. Multiband (bands = 0), mode = 0 (Compress), f = 160 Hz
    {
        const double ratio = measureRatio (0, 0, 160.0);
        CHECK (std::fabs (ratio - 1.0) < 0.02);
    }
    // 3. Low Band (bands = 1), mode = 0 (Compress), f = 160 Hz
    {
        const double ratio = measureRatio (1, 0, 160.0);
        CHECK (std::fabs (ratio - 1.0) < 0.02);
    }
    // 4. High Band (bands = 2), mode = 0 (Compress), f = 2500 Hz
    {
        const double ratio = measureRatio (2, 0, 2500.0);
        CHECK (std::fabs (ratio - 1.0) < 0.02);
    }
    // 5. Multiband (bands = 0), mode = 1 (Limit), f = 2500 Hz
    {
        const double ratio = measureRatio (0, 1, 2500.0);
        CHECK (std::fabs (ratio - 1.0) < 0.02);
    }

    // 6. Single-band checksum for bit-identity verification
    {
        Compressor comp;
        comp.prepare (48000.0);
        Compressor::Params p;
        p.bands = 3;
        p.mode = 0;
        p.mix = 0.5f;
        for (auto& bd : p.band)
        {
            bd.upperDb = -20.0f;
            bd.upperRatio = 0.5f;
            bd.lowerDb = -100.0f;
            bd.lowerRatio = 0.0f;
            bd.gainDb = 0.0f;
        }
        comp.set (p);

        struct Lcg
        {
            uint32_t state;
            explicit Lcg (uint32_t seed) : state (seed) {}
            uint32_t next() noexcept { state = state * 1664525u + 1013904223u; return state; }
            float sample() noexcept { return static_cast<float> (next()) * (2.0f / 4294967296.0f) - 1.0f; }
        };

        const int n = 512;
        std::vector<float> bufL (static_cast<size_t> (n)), bufR (static_cast<size_t> (n));
        Lcg rng (123456789u);
        for (int i = 0; i < n; ++i)
        {
            bufL[static_cast<size_t> (i)] = rng.sample();
            bufR[static_cast<size_t> (i)] = rng.sample();
        }

        comp.process (bufL.data(), bufR.data(), n);

        double sumAbs = 0.0;
        for (int i = 0; i < n; ++i)
            sumAbs += static_cast<double> (std::fabs (bufL[static_cast<size_t> (i)])) + static_cast<double> (std::fabs (bufR[static_cast<size_t> (i)]));

        std::printf ("COMPMIX-CHECKSUM single_band sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     sumAbs,
                     static_cast<double> (bufL[n - 8]), static_cast<double> (bufL[n - 7]),
                     static_cast<double> (bufL[n - 6]), static_cast<double> (bufL[n - 5]),
                     static_cast<double> (bufL[n - 4]), static_cast<double> (bufL[n - 3]),
                     static_cast<double> (bufL[n - 2]), static_cast<double> (bufL[n - 1]));
    }
}

