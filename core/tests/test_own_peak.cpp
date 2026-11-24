// SynthyBoy - own engine tests: the NEON block peak (dsp/peak.h: the strip, bus, master and effect chain peaks) gives the float of the scalar loops it replaced, for random blocks and
// for blocks made of every special value (silence, NaN, both infinities, both zeros, subnormals, full scale), mono and stereo, every length from 0 to 40, unaligned starts, in the
// default float mode (not on 32 bit ARM: its NEON flushes subnormals whatever the mode is) and, on both ARM forms, with the flush-to-zero bit set as the MPC shell sets it. Compiled with the NEON forms on (also on Apple's arm64); links nothing of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "dsp/peak.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
uint32_t gState = 20261118u;
uint32_t next() { gState ^= gState << 13; gState ^= gState >> 17; gState ^= gState << 5; return gState; }
float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
float fromBits (uint32_t u) { float f; std::memcpy (&f, &u, 4); return f; }
uint32_t bitsOf (float f) { uint32_t u; std::memcpy (&u, &f, 4); return u; }

const float kSpecial[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.25f, 1.8f, -1.8f, 3.0e38f, -3.0e38f, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::quiet_NaN(), fromBits (0x7fa00000u), fromBits (0xffc00001u),
                           1.0e-40f, -1.0e-40f, fromBits (1u), fromBits (0x80000001u), fromBits (0x007fffffu), fromBits (0x00800000u), 1.1754944e-38f, 1.0e-20f, 123.456f };

float sample (int style)
{
    switch (style)
    {
        case 0: return kSpecial[next() % (sizeof (kSpecial) / sizeof (kSpecial[0]))];
        case 1: return 2.0f * uni() - 1.0f;
        case 2: return fromBits (next());                                   // any bit pattern: NaNs, subnormals, huge values
        case 3: return next() % 8 == 0 ? kSpecial[next() % (sizeof (kSpecial) / sizeof (kSpecial[0]))] : 0.001f * (uni() - 0.5f);
        default: return 0.0f;
    }
}

#if defined(__aarch64__)
uint64_t readFpcr() { uint64_t v; asm volatile ("mrs %0, fpcr" : "=r"(v)); return v; }
void writeFpcr (uint64_t v) { asm volatile ("msr fpcr, %0" :: "r"(v) : "memory"); }   // (the memory clobber keeps the calls on the right side of it)
constexpr uint64_t kFlushBit = 1ull << 24;   // FPCR.FZ
#elif defined(__arm__)
uint64_t readFpcr() { uint32_t v; asm volatile ("vmrs %0, fpscr" : "=r"(v)); return v; }
void writeFpcr (uint64_t v) { const uint32_t w = static_cast<uint32_t> (v); asm volatile ("vmsr fpscr, %0" :: "r"(w) : "memory"); }
constexpr uint64_t kFlushBit = 1ull << 24;   // FPSCR.FZ
#endif

long long runAll()
{
    long long bad = 0, cases = 0;
    for (int rep = 0; rep < 12000; ++rep)
    {
        const int style = rep % 5, n = rep % 41, off = rep % 3;
        std::vector<float> l (static_cast<size_t> (n + 4)), r (static_cast<size_t> (n + 4));
        for (auto& v : l) v = sample (style);
        for (auto& v : r) v = sample ((style + rep / 5) % 5);
        const float* pl = l.data() + off;
        const float* pr = r.data() + off;
        const int m = n > off ? n - off : 0;
        const float a = peak::peakAbs (pl, m), ar = peak::peakAbsScalar (pl, m);
        const auto stereo = peak::stereoPeaks (pl, pr, m);
        CHECK_EQ (bitsOf (stereo.left), bitsOf (peak::peakAbsScalar (pl, m)));
        CHECK_EQ (bitsOf (stereo.right), bitsOf (peak::peakAbsScalar (pr, m)));
        const float b = peak::peakAbsPair (pl, pr, m), br = peak::peakAbsPairScalar (pl, pr, m);
        bad += (bitsOf (a) != bitsOf (ar)) + (bitsOf (b) != bitsOf (br));
        cases += 2;
        if (bitsOf (a) != bitsOf (ar) || bitsOf (b) != bitsOf (br))
            std::printf ("    mismatch: n %d style %d: %08x vs %08x, pair %08x vs %08x\n", m, style, bitsOf (a), bitsOf (ar), bitsOf (b), bitsOf (br));
    }
    // a single special value in a block of zeros, at every position, mono and stereo on either side
    for (const float v : kSpecial)
        for (int n = 1; n <= 40; ++n)
            for (int pos = 0; pos < n; ++pos)
                for (int side = 0; side < 3; ++side)
                {
                    std::vector<float> l (static_cast<size_t> (n), 0.0f), r (static_cast<size_t> (n), 0.0f);
                    (side == 1 ? r : l)[static_cast<size_t> (pos)] = v;
                    if (side == 2)
                        r[static_cast<size_t> (pos)] = 0.75f;   // a finite right sample next to a special left one
                    bad += bitsOf (peak::peakAbs (l.data(), n)) != bitsOf (peak::peakAbsScalar (l.data(), n));
                    bad += bitsOf (peak::peakAbsPair (l.data(), r.data(), n)) != bitsOf (peak::peakAbsPairScalar (l.data(), r.data(), n));
                    cases += 2;
                }
    std::printf ("    %lld comparisons, %lld differ\n", cases, bad);
    return bad;
}
} // namespace

#if !defined(__arm__)   // (the NEON of ARMv7 flushes subnormals whatever the float mode is: on 32 bit ARM only the flush-to-zero test below is the contract, the shell runs in that mode)
TEST (own_neon_peak_is_the_scalar_peak_in_the_default_float_mode)
{
    static_assert (SB_PEAK_NEON == 1, "this test is built with the NEON forms on");
    CHECK_EQ (runAll(), 0);
}
#endif

#if defined(__aarch64__) || defined(__arm__)
TEST (own_neon_peak_is_the_scalar_peak_with_flush_to_zero_set)
{
    const uint64_t saved = readFpcr();
    writeFpcr (saved | kFlushBit);   // FZ: what the MPC shell sets
    const long long bad = runAll();
    // the subnormal case must really be in flush mode: a subnormal alone gives 0 in both forms
    volatile float subSrc = 1.0e-40f;   // (not a compile time constant)
    float sub[8];
    for (float& v : sub) v = subSrc;
    const float flushedVec = peak::peakAbs (sub, 8), flushedRef = peak::peakAbsScalar (sub, 8);
    writeFpcr (saved);
    std::printf ("    flush mode: subnormal block peak %g (vector), %g (scalar)\n", static_cast<double> (flushedVec), static_cast<double> (flushedRef));
    CHECK_EQ (bitsOf (flushedVec), bitsOf (flushedRef));
    CHECK_EQ (bad, 0);
}
#endif

TEST (own_neon_peak_matches_plain_values)
{
    const float x[7] = { 0.1f, -0.9f, 0.3f, 0.2f, -0.05f, 0.7f, -0.8f };
    CHECK_EQ (peak::peakAbs (x, 7), 0.9f);
    CHECK_EQ (peak::peakAbs (x, 0), 0.0f);
    const float y[7] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f };
    CHECK_EQ (peak::peakAbsPair (x, y, 7), 1.5f);
}
