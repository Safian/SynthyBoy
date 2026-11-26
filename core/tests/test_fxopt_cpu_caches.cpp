// SynthyBoy - core tests: the per control period caches of the effects (CPU review item 5, 2026-11-26) are bit identical to computing everything every period.
// What is cached (on the exact bits of what it is computed from): the compressor's crossover designs and its libm prelude (envelope times, thresholds, makeup, ceiling, Serum constants;
// and the designs / constants the mode does not read are not made), the delay's glide and loop filter coefficients, the chorus' loop filter coefficients, the flanger's equal power mix
// gains, the phaser's refresh of the stage and feedback coefficients. makeup::setLegacyCachesForTest (true) is the old path: every cache is bypassed and every design and constant is
// made on every call (also those a mode does not read). Each effect runs the same random parameter walk (parameters kept for a few periods, then changed) twice, once each way, and every
// output sample is compared with memcmp.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "fx/fx_effects.h"
#include "fx/fx_makeup.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
float uni (std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * static_cast<float> (rng() % 100001) / 100000.0f; }
bool keep (std::mt19937& rng) { return rng() % 10 < 6; } // the parameters of the period before: the common case

std::vector<float> compressorRun (uint32_t seed, double sr, bool fast, bool legacy)
{
    makeup::setLegacyCachesForTest (legacy);
    std::mt19937 rng (seed);
    Compressor c;
    c.prepare (sr);
    c.fast = fast;
    Compressor::Params p;
    std::vector<float> out;
    for (int per = 0; per < 300; ++per)
    {
        if (!keep (rng))
        {
            const unsigned what = rng() % 5;
            if (what == 0)
            {
                p.bands = static_cast<int> (rng() % 4);
                p.mode = static_cast<int> (rng() % 3);
            }
            else if (what == 1)
            {
                p.crossLow = uni (rng, 40.0f, 500.0f);
                p.crossHigh = uni (rng, 800.0f, 9000.0f);
            }
            else if (what == 2)
            {
                p.attack = uni (rng, 0.0f, 1.0f);
                p.release = uni (rng, 0.0f, 1.0f);
                p.mix = uni (rng, 0.0f, 1.0f);
            }
            else
                for (auto& bd : p.band)
                {
                    bd.lowerDb = uni (rng, -60.0f, -10.0f);
                    bd.upperDb = uni (rng, -30.0f, 6.0f);
                    bd.lowerRatio = rng() % 3 == 0 ? 0.0f : uni (rng, -1.0f, 1.0f);
                    bd.upperRatio = uni (rng, 0.0f, 1.0f);
                    bd.gainDb = rng() % 3 == 0 ? 0.0f : uni (rng, -6.0f, 12.0f);
                }
        }
        c.set (p);
        const int n = 1 + static_cast<int> (rng() % 40);
        std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
        {
            l[static_cast<size_t> (i)] = uni (rng, -1.5f, 1.5f) * std::sin (0.03f * static_cast<float> (per * 32 + i));
            r[static_cast<size_t> (i)] = uni (rng, -1.5f, 1.5f);
        }
        c.process (l.data(), r.data(), n);
        out.insert (out.end(), l.begin(), l.end());
        out.insert (out.end(), r.begin(), r.end());
    }
    makeup::setLegacyCachesForTest (false);
    return out;
}

std::vector<float> delayRun (uint32_t seed, double sr, bool legacy)
{
    makeup::setLegacyCachesForTest (legacy);
    std::mt19937 rng (seed);
    Delay d;
    d.prepare (sr);
    Delay::Params p;
    std::vector<float> out;
    for (int per = 0; per < 600; ++per)
    {
        if (!keep (rng))
        {
            if (rng() % 2 == 0)
            {
                p.timeL = uni (rng, 0.002f, 0.6f);
                p.timeR = uni (rng, 0.002f, 0.6f);
                p.style = static_cast<int> (rng() % 5);
            }
            else
            {
                p.cutoff = uni (rng, 40.0f, 8000.0f);
                p.spread = uni (rng, 0.0f, 1.0f);
                p.feedback = uni (rng, -0.9f, 0.9f);
                p.mix = uni (rng, 0.0f, 1.0f);
                p.hq = rng() % 2 == 0;
            }
        }
        d.set (p);
        const int n = 32;
        float l[n], r[n];
        for (int i = 0; i < n; ++i)
        {
            l[i] = uni (rng, -0.5f, 0.5f);
            r[i] = uni (rng, -0.5f, 0.5f);
        }
        d.process (l, r, n);
        out.insert (out.end (), l, l + n);
        out.insert (out.end (), r, r + n);
    }
    makeup::setLegacyCachesForTest (false);
    return out;
}

std::vector<float> chorusRun (uint32_t seed, double sr, bool fast, bool legacy)
{
    makeup::setLegacyCachesForTest (legacy);
    std::mt19937 rng (seed);
    Chorus c;
    c.prepare (sr);
    c.fast = fast;
    Chorus::Params p;
    std::vector<float> out;
    for (int per = 0; per < 400; ++per)
    {
        if (!keep (rng))
        {
            p.mix = uni (rng, 0.0f, 1.0f);
            p.rate = rng() % 4 == 0 ? 0.0f : uni (rng, 0.05f, 3.0f);
            p.depth = uni (rng, 0.0f, 1.0f);
            p.feedback = uni (rng, -0.9f, 0.9f);
            p.delay1 = uni (rng, 0.0005f, 0.01f);
            p.delay2 = uni (rng, 0.002f, 0.02f);
            p.cutoff = uni (rng, 40.0f, 8000.0f);
            p.spread = uni (rng, 0.0f, 1.0f);
            p.voices = 1 + static_cast<int> (rng() % 4);
        }
        c.set (p);
        const int n = 32;
        float l[n], r[n];
        for (int i = 0; i < n; ++i)
        {
            l[i] = uni (rng, -0.5f, 0.5f);
            r[i] = uni (rng, -0.5f, 0.5f);
        }
        c.process (l, r, n);
        out.insert (out.end (), l, l + n);
        out.insert (out.end (), r, r + n);
    }
    makeup::setLegacyCachesForTest (false);
    return out;
}

std::vector<float> flangerRun (uint32_t seed, double sr, bool legacy)
{
    makeup::setLegacyCachesForTest (legacy);
    std::mt19937 rng (seed);
    Flanger f;
    f.prepare (sr);
    Flanger::Params p;
    std::vector<float> out;
    for (int per = 0; per < 400; ++per)
    {
        if (!keep (rng))
        {
            p.mix = uni (rng, 0.0f, 1.0f);
            p.feedback = uni (rng, -0.9f, 0.9f);
            p.rate = rng() % 4 == 0 ? 0.0f : uni (rng, 0.05f, 3.0f);
            p.depth = uni (rng, 0.0f, 1.0f);
            p.centerHz = uni (rng, 40.0f, 8000.0f);
            p.phaseOffset = uni (rng, 0.0f, 1.0f);
        }
        f.set (p);
        const int n = 32;
        float l[n], r[n];
        for (int i = 0; i < n; ++i)
        {
            l[i] = uni (rng, -0.5f, 0.5f);
            r[i] = uni (rng, -0.5f, 0.5f);
        }
        f.process (l, r, n);
        out.insert (out.end (), l, l + n);
        out.insert (out.end (), r, r + n);
    }
    makeup::setLegacyCachesForTest (false);
    return out;
}

std::vector<float> phaserRun (uint32_t seed, double sr, bool fast, bool legacy)
{
    makeup::setLegacyCachesForTest (legacy);
    std::mt19937 rng (seed);
    Phaser f;
    f.prepare (sr);
    f.fast = fast;
    Phaser::Params p;
    std::vector<float> out;
    for (int per = 0; per < 500; ++per)
    {
        if (!keep (rng))
        {
            p.mix = uni (rng, 0.0f, 1.0f);
            p.feedback = uni (rng, 0.0f, 1.0f);
            p.rate = rng() % 3 == 0 ? 0.0f : uni (rng, 0.02f, 4.0f); // 0: a static LFO, the refresh finds the same cutoff
            p.depthSemis = rng() % 6 == 0 ? 0.0f : uni (rng, 1.0f, 36.0f); // 0: no sweep
            p.centerHz = rng() % 8 == 0 ? 30000.0f : uni (rng, 30.0f, 8000.0f); // above the clamp: the cutoff stands at 0.45 sr
            p.blend = uni (rng, 0.0f, 1.0f);
            p.phaseOffset = uni (rng, 0.0f, 1.0f);
            p.depth2 = rng() % 3 == 0 ? uni (rng, 1.0f, 12.0f) : 0.0f;
        }
        f.set (p);
        const int n = 1 + static_cast<int> (rng() % 64);
        std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
        {
            l[static_cast<size_t> (i)] = uni (rng, -0.5f, 0.5f);
            r[static_cast<size_t> (i)] = uni (rng, -0.5f, 0.5f);
        }
        f.process (l.data(), r.data(), n);
        out.insert (out.end(), l.begin(), l.end());
        out.insert (out.end(), r.begin(), r.end());
    }
    makeup::setLegacyCachesForTest (false);
    return out;
}

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}
} // namespace

TEST (compressor_caches_and_skipped_designs_are_bit_identical)
{
    int runs = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int fast = 0; fast < 2; ++fast)
            for (uint32_t seed = 1; seed <= 12; ++seed)
            {
                const uint32_t s = seed * 104729u + static_cast<uint32_t> (sr) + static_cast<uint32_t> (fast);
                CHECK (same (compressorRun (s, sr, fast != 0, false), compressorRun (s, sr, fast != 0, true)));
                ++runs;
            }
    std::printf ("compressor runs compared (cached vs everything every call): %d\n", runs);
}

TEST (delay_caches_are_bit_identical)
{
    for (double sr : { 44100.0, 48000.0 })
        for (uint32_t seed = 1; seed <= 8; ++seed)
            CHECK (same (delayRun (seed * 7u, sr, false), delayRun (seed * 7u, sr, true)));
}

TEST (chorus_and_flanger_caches_are_bit_identical)
{
    for (double sr : { 44100.0, 48000.0 })
        for (uint32_t seed = 1; seed <= 8; ++seed)
        {
            CHECK (same (chorusRun (seed * 11u, sr, false, false), chorusRun (seed * 11u, sr, false, true)));
            CHECK (same (chorusRun (seed * 11u + 1u, sr, true, false), chorusRun (seed * 11u + 1u, sr, true, true)));
            CHECK (same (flangerRun (seed * 13u, sr, false), flangerRun (seed * 13u, sr, true)));
        }
}

TEST (phaser_refresh_cache_is_bit_identical)
{
    for (double sr : { 44100.0, 48000.0 })
        for (int fast = 0; fast < 2; ++fast)
            for (uint32_t seed = 1; seed <= 10; ++seed)
                CHECK (same (phaserRun (seed * 17u + static_cast<uint32_t> (fast), sr, fast != 0, false), phaserRun (seed * 17u + static_cast<uint32_t> (fast), sr, fast != 0, true)));
}
