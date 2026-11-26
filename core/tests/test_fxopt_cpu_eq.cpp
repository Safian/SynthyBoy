// SynthyBoy - core tests: the Equalizer after the CPU review item 4 (2026-11-26) is bit identical to the code before it (ref/fx_eq_before.h):
//  - a band design is made again only when the band's mode, frequency, gain or resonance changed (the exact bits), not on every control period;
//  - the Serum law does not interpolate the coefficients of a band whose previous and current values are the same finite bits (prev + (cur - prev) * t is exactly prev), and runs the bands
//    one after the other over the whole call.
// Every output sample of both laws is compared with memcmp over random parameter walks that keep the parameters for some periods (the cache and the steady path) and change some of them,
// over block splits, a period that is not the block, a reset and a law switch in the middle of a run.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "fx/fx_eq.h"
#include "fx/fx_makeup.h"
#include "ref/fx_eq_before.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
float uni (std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * static_cast<float> (rng() % 100001) / 100000.0f; }

Equalizer::Band randomBand (std::mt19937& rng, int modeCount, bool lowBand)
{
    Equalizer::Band b;
    b.mode = static_cast<int> (rng() % static_cast<unsigned> (modeCount));
    const unsigned pick = rng() % 12;
    b.hz = pick == 0 ? 5.0f : (pick == 1 ? 30000.0f : (pick == 2 ? 12000.0f : 20.0f * std::pow (1000.0f, uni (rng, 0.0f, 1.0f))));
    b.gainDb = rng() % 5 == 0 ? 0.0f : (rng() % 9 == 0 ? (rng() % 2 == 0 ? 40.0f : -40.0f) : uni (rng, -24.0f, 24.0f));
    b.res = rng() % 6 == 0 ? (rng() % 2 == 0 ? 0.0f : 1.0f) : uni (rng, 0.0f, 1.2f);
    (void) lowBand;
    return b;
}

Equalizer::Params randomParams (std::mt19937& rng, int law)
{
    Equalizer::Params p;
    p.law = law;
    p.low = randomBand (rng, 2, true);
    p.mid = randomBand (rng, 2, false);
    p.mid2 = randomBand (rng, 3, false);
    p.high = randomBand (rng, 2, false);
    return p;
}

/** Changes one to three of the bands (all fields of those bands), the others keep their exact values. */
void mutate (std::mt19937& rng, Equalizer::Params& p)
{
    const int n = 1 + static_cast<int> (rng() % 3);
    for (int k = 0; k < n; ++k)
        switch (rng() % 4)
        {
            case 0: p.low = randomBand (rng, 2, true); break;
            case 1: p.mid = randomBand (rng, 2, false); break;
            case 2: p.mid2 = randomBand (rng, 3, false); break;
            default: p.high = randomBand (rng, 2, false); break;
        }
}

cpuref::EqualizerBefore::Params convert (const Equalizer::Params& p)
{
    cpuref::EqualizerBefore::Params q;
    auto band = [] (const Equalizer::Band& s) {
        cpuref::EqualizerBefore::Band d;
        d.mode = s.mode;
        d.hz = s.hz;
        d.gainDb = s.gainDb;
        d.res = s.res;
        return d;
    };
    q.low = band (p.low);
    q.mid = band (p.mid);
    q.mid2 = band (p.mid2);
    q.high = band (p.high);
    q.law = p.law;
    return q;
}

/** The same bits, except that a NaN is a NaN (the sign and the payload of a NaN follow the operand order the compiler picked for a commutative operation: the arithmetic is the same). */
bool sameSamples (const float* a, const float* b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (std::memcmp (&a[i], &b[i], 4) != 0 && !(std::isnan (a[i]) && std::isnan (b[i])))
            return false;
    return true;
}

struct Result
{
    bool same = true;
    long long samples = 0;
};

/** One walk. `periods` control periods; every period one set () and one to three process () calls. `specials`: tiny values, zeros and a rare NaN in the program. */
Result walk (double sr, uint32_t seed, int periods, bool specials, bool legacyCaches)
{
    std::mt19937 rng (seed);
    Equalizer a;
    cpuref::EqualizerBefore b;
    a.prepare (sr);
    b.prepare (sr);
    synthyboy::own::makeup::setLegacyCachesForTest (legacyCaches);
    Equalizer::Params p = randomParams (rng, static_cast<int> (rng() % 2));
    Result res;
    for (int per = 0; per < periods; ++per)
    {
        const unsigned what = rng() % 10;
        if (what < 5)
        {
            // the same parameters again: the usual control period
        }
        else if (what < 9)
            mutate (rng, p);
        else if (what == 9)
        {
            if (rng() % 4 == 0)
                p.law = 1 - p.law; // a law switch in the middle of a run
            else
                p = randomParams (rng, p.law);
        }
        if (per % 97 == 96)
        {
            a.reset();
            b.reset();
        }
        a.set (p);
        b.set (convert (p));
        const int period = rng() % 7 == 0 ? 0 : (rng() % 5 == 0 ? 17 : 32); // 0: a caller without periods (max (period, 1) inside)
        // the calls of the period: the whole, two halves, or one longer than the period
        std::vector<std::pair<int, int>> calls; // t0, n
        switch (rng() % 4)
        {
            case 0: calls.push_back ({ 0, 32 }); break;
            case 1: { const int cut = 1 + static_cast<int> (rng() % 31); calls.push_back ({ 0, cut }); calls.push_back ({ cut, 32 - cut }); break; }
            case 2: calls.push_back ({ 0, 1 + static_cast<int> (rng() % 90) }); break;
            default: calls.push_back ({ 0, 7 }); calls.push_back ({ 7, 7 }); calls.push_back ({ 14, 18 }); break;
        }
        for (const auto& c : calls)
        {
            const int n = c.second;
            std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
            for (int i = 0; i < n; ++i)
            {
                float x = uni (rng, -0.5f, 0.5f), y = uni (rng, -0.5f, 0.5f);
                if (specials)
                {
                    const unsigned s = rng() % 400;
                    if (s == 0) x = 1.0e-20f;
                    else if (s == 1) y = 0.0f;
                    else if (s == 2) x = std::numeric_limits<float>::denorm_min();
                    else if (s == 3 && per % 50 == 49) x = std::numeric_limits<float>::quiet_NaN();
                }
                l[static_cast<size_t> (i)] = x;
                r[static_cast<size_t> (i)] = y;
            }
            std::vector<float> l2 = l, r2 = r;
            a.process (l.data(), r.data(), n, c.first, period);
            b.process (l2.data(), r2.data(), n, c.first, period);
            if (!sameSamples (l.data(), l2.data(), static_cast<size_t> (n)) || !sameSamples (r.data(), r2.data(), static_cast<size_t> (n)))
            {
                if (res.same)
                {
                    int at = 0;
                    while (at < n && sameSamples (&l[static_cast<size_t> (at)], &l2[static_cast<size_t> (at)], 1) && sameSamples (&r[static_cast<size_t> (at)], &r2[static_cast<size_t> (at)], 1))
                        ++at;
                    std::printf ("equalizer differs: sr %g seed %u period %d law %d call t0 %d n %d sample %d: %.9g vs %.9g / %.9g vs %.9g\n", sr, seed, per, p.law, c.first, n, at,
                                 static_cast<double> (l[static_cast<size_t> (at)]), static_cast<double> (l2[static_cast<size_t> (at)]), static_cast<double> (r[static_cast<size_t> (at)]), static_cast<double> (r2[static_cast<size_t> (at)]));
                }
                res.same = false;
            }
            res.samples += n;
        }
    }
    synthyboy::own::makeup::setLegacyCachesForTest (false);
    return res;
}
} // namespace

TEST (equalizer_is_bit_identical_to_the_code_before_over_parameter_walks)
{
    long long samples = 0;
    int walks = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (uint32_t seed = 1; seed <= 24; ++seed)
        {
            const Result r = walk (sr, seed * 7919u + static_cast<uint32_t> (sr), 400, (seed & 1) != 0, false);
            CHECK (r.same);
            samples += r.samples;
            ++walks;
        }
    std::printf ("equalizer walks bit identical to the code before: %d walks, %lld samples per channel\n", walks, samples);
}

// The same walks with the design caches switched off (the library switch of the bit identical caches, makeup::setLegacyCachesForTest): only the steady path and the band order differ then.
TEST (equalizer_is_bit_identical_with_the_design_cache_off)
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (uint32_t seed = 1; seed <= 24; ++seed)
        {
            const Result r = walk (sr, seed * 7919u + static_cast<uint32_t> (sr), 400, (seed & 1) != 0, true);
            CHECK (r.same);
        }
}

// A fixed program with the parameters of a preset that does not move: every control period the same, 2000 periods (the cache and the steady path are what runs).
TEST (equalizer_with_still_parameters_is_bit_identical)
{
    for (int law = 0; law < 2; ++law)
    {
        std::mt19937 rng (900 + static_cast<unsigned> (law));
        Equalizer a;
        cpuref::EqualizerBefore b;
        a.prepare (44100.0);
        b.prepare (44100.0);
        Equalizer::Params p = randomParams (rng, law);
        p.mid2.mode = 1;
        bool same = true;
        for (int per = 0; per < 2000; ++per)
        {
            a.set (p);
            b.set (convert (p));
            float l[32], r[32], l2[32], r2[32];
            for (int i = 0; i < 32; ++i)
            {
                l[i] = l2[i] = uni (rng, -0.5f, 0.5f);
                r[i] = r2[i] = uni (rng, -0.5f, 0.5f);
            }
            a.process (l, r, 32, 0, 32);
            b.process (l2, r2, 32, 0, 32);
            same = same && std::memcmp (l, l2, sizeof l) == 0 && std::memcmp (r, r2, sizeof r) == 0;
        }
        CHECK (same);
    }
}
