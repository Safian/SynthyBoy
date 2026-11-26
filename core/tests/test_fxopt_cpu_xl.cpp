// SynthyBoy - core tests: the XL-only effect changes of the CPU review round of 2026-11-26, all bit identical:
//  - Reverb type 0 (the 16 line network): the four groups share the lane phases (4 cosines and 4 sines per sample instead of 16 libm calls); the per call coefficients (the input band pass,
//    the shelf coefficients of the cores, the network's lengths, gains and shelves) are made again only when the settings or the rate changed. Against the frozen reverb of
//    core/tests/ref/reverb_reference.h on random parameter walks, every sample memcmp, all six algorithms and the five rooms of type 4;
//  - HalfBand2x: the three 15 sample histories are doubled ring buffers (no shift of 15 floats on every sample). Against the frozen copy (ref/fx_oversampler_before.h), special values included;
//  - Bode: no sine and cosine of the blur LFO while the blur is 0 (a zero times a sine is a zero and the delay time plus it is the delay time). Against the long way (a blur of 1e-30, which makes the
//    same zero modulation by the arithmetic of the full expression).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "bode.h"
#include "fx/fx_oversampler.h"
#include "fx/fx_reverb.h"
#include "fxopt_reverb_util.h"
#include "ref/fx_oversampler_before.h"
#include "ref/reverb_reference.h"
#include "test_runner.h"

using synthyboy::own::Reverb;
using RefReverb = synthyboy::own::ref::Reverb;

namespace
{
float uni (std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * static_cast<float> (rng() % 100001) / 100000.0f; }
bool sameSamples (const float* a, const float* b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        const float x = a[i] + 0.0f, y = b[i] + 0.0f; // (a negative zero and a zero are the same here, as in the reverb tests)
        if (std::memcmp (&x, &y, 4) != 0 && !(std::isnan (x) && std::isnan (y)))
            return false;
    }
    return true;
}

template <class P>
P randomParams (std::mt19937& rng, int type, int room)
{
    P p;
    p.mix = uni (rng, 0.3f, 1.0f);
    p.size = uni (rng, 0.0f, 1.0f);
    p.decay = uni (rng, 0.2f, 8.0f);
    p.predelay = rng() % 3 == 0 ? 0.0f : uni (rng, 0.0f, 0.1f);
    p.lowCut = uni (rng, 20.0f, 400.0f);
    p.highCut = uni (rng, 2000.0f, 18000.0f);
    p.highShelfDb = uni (rng, -24.0f, 0.0f);
    p.highShelfHz = uni (rng, 500.0f, 6000.0f);
    p.lowShelfDb = uni (rng, -24.0f, 0.0f);
    p.lowShelfHz = uni (rng, 40.0f, 400.0f);
    p.chorusAmount = rng() % 4 == 0 ? 0.0f : uni (rng, 0.0f, 1.0f);
    p.chorusRate = uni (rng, 0.05f, 20.0f);
    p.width = rng() % 3 == 0 ? 1.0f : uni (rng, 0.0f, 1.5f);
    p.diffA = uni (rng, 0.0f, 1.0f);
    p.diffB = uni (rng, 0.0f, 1.0f);
    p.erSize = uni (rng, 0.0f, 1.0f);
    p.feedback = uni (rng, 0.0f, 1.0f);
    p.type = type;
    p.room = room;
    return p;
}

template <class P>
void copyParams (const Reverb::Params& s, P& d)
{
    d.mix = s.mix; d.size = s.size; d.decay = s.decay; d.predelay = s.predelay; d.lowCut = s.lowCut; d.highCut = s.highCut; d.highShelfDb = s.highShelfDb; d.highShelfHz = s.highShelfHz;
    d.lowShelfDb = s.lowShelfDb; d.lowShelfHz = s.lowShelfHz; d.chorusAmount = s.chorusAmount; d.chorusRate = s.chorusRate; d.width = s.width; d.diffA = s.diffA; d.diffB = s.diffB;
    d.erSize = s.erSize; d.feedback = s.feedback; d.type = s.type; d.room = s.room;
}
} // namespace

// A walk: the settings stay for some periods (the caches hit), then some of them change (one field, a few, or all, and the type); the chunks are one control period and other lengths.
TEST (reverb_after_the_cpu_changes_is_bit_identical_to_the_frozen_reverb_on_parameter_walks)
{
    int walks = 0;
    for (double sr : { 44100.0, 48000.0 })
        for (const auto& tc : fxopt::reverbCases())
        {
            std::mt19937 rng (static_cast<uint32_t> (tc.first * 100 + tc.second * 10 + (sr > 46000.0 ? 1 : 0)) + 77u);
            Reverb live;
            RefReverb ref;
            live.prepare (sr);
            ref.prepare (sr);
            Reverb::Params p = randomParams<Reverb::Params> (rng, tc.first, tc.second);
            bool same = true;
            for (int per = 0; per < 700; ++per)
            {
                const unsigned what = rng() % 12;
                if (what < 7)
                {
                    // unchanged
                }
                else if (what < 9)
                    p.mix = uni (rng, 0.0f, 1.0f); // a modulated mix: the rest is the same
                else if (what == 9 || what == 11)
                {
                    // exactly one of the settings the caches are made of changes (each in turn: a cache that misses one is caught)
                    const Reverb::Params q = randomParams<Reverb::Params> (rng, tc.first, tc.second);
                    switch (rng() % 9)
                    {
                        case 0: p.decay = q.decay; break;
                        case 1: p.size = q.size; break;
                        case 2: p.highShelfDb = q.highShelfDb; break;
                        case 3: p.lowShelfDb = q.lowShelfDb; break;
                        case 4: p.highShelfHz = q.highShelfHz; break;
                        case 5: p.lowShelfHz = q.lowShelfHz; break;
                        case 6: p.chorusAmount = q.chorusAmount; break;
                        case 7: p.highCut = q.highCut; break;
                        default: p.lowCut = q.lowCut; break;
                    }
                }
                else if (what == 10)
                    p = randomParams<Reverb::Params> (rng, tc.first, tc.second);
                RefReverb::Params q;
                copyParams (p, q);
                live.set (p);
                ref.set (q);
                const int n = per % 5 == 0 ? 1 + static_cast<int> (rng() % 60) : 32;
                std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
                for (int i = 0; i < n; ++i)
                {
                    const bool quiet = (per / 40) % 2 == 1;
                    l[static_cast<size_t> (i)] = quiet ? 0.0f : uni (rng, -0.4f, 0.4f);
                    r[static_cast<size_t> (i)] = quiet ? 0.0f : uni (rng, -0.4f, 0.4f);
                }
                std::vector<float> l2 = l, r2 = r;
                live.process (l.data(), r.data(), n);
                ref.process (l2.data(), r2.data(), n);
                same = same && sameSamples (l.data(), l2.data(), static_cast<size_t> (n)) && sameSamples (r.data(), r2.data(), static_cast<size_t> (n));
            }
            CHECK (same);
            ++walks;
        }
    std::printf ("reverb walks bit identical to the frozen reverb: %d\n", walks);
}

TEST (half_band_oversampler_with_doubled_ring_buffers_is_bit_identical)
{
    using synthyboy::own::HalfBand2x;
    using Before = synthyboy::own::cpuref::HalfBand2x;
    std::mt19937 rng (3);
    bool same = true;
    long long n = 0;
    for (int run = 0; run < 6; ++run)
    {
        HalfBand2x a;
        Before b;
        for (int i = 0; i < 40000; ++i)
        {
            float x = uni (rng, -1.5f, 1.5f);
            if (run >= 3)
            {
                const unsigned s = rng() % 500;
                if (s == 0) x = 1.0e-30f;
                else if (s == 1) x = std::numeric_limits<float>::denorm_min();
                else if (s == 2) x = 1.0e30f;
                else if (s == 3 && i > 20000 && run == 5) x = std::numeric_limits<float>::quiet_NaN();
            }
            if (i == 25000 && (run & 1) != 0)
            {
                a.reset();
                b.reset();
            }
            const auto ua = a.up (x);
            const auto ub = b.up (x);
            const float da = a.down (ua[0] * 0.9f, ua[1] * 0.8f), db = b.down (ub[0] * 0.9f, ub[1] * 0.8f);
            const float xa[3] = { ua[0], ua[1], da }, xb[3] = { ub[0], ub[1], db };
            same = same && sameSamples (xa, xb, 3);
            ++n;
        }
    }
    CHECK (same);
    std::printf ("half band 2x: %lld samples (up and down) bit identical to the shifted histories\n", n);
}

TEST (bode_with_zero_blur_is_bit_identical_to_the_long_way)
{
    using synthyboy::fx::Bode;
    std::mt19937 rng (11);
    for (double sr : { 44100.0, 48000.0 })
        for (int variant = 0; variant < 6; ++variant)
        {
            Bode a, b;
            a.prepare (sr, 64);
            b.prepare (sr, 64);
            Bode::Params p;
            p.monoInput = variant % 2 == 1;
            p.shift = uni (rng, -1.0f, 1.0f);
            p.range = uni (rng, 50.0f, 5000.0f);
            p.dir = uni (rng, -1.0f, 1.0f);
            p.width = uni (rng, 0.0f, 1.0f);
            p.delay = variant == 0 ? 0.0f : uni (rng, 0.0001f, 1.5f); // 0: the clamp to 0.0001
            p.bpm = variant == 5;
            p.tempoBpm = 123.0;
            p.delayBeats = 0.25;
            p.feedback = uni (rng, -0.9f, 0.9f);
            p.balance = uni (rng, -1.0f, 1.0f);
            p.mix = uni (rng, 0.3f, 1.0f);
            p.level = uni (rng, -6.0f, 6.0f);
            p.blur = 0.0f;
            a.params = p;
            p.blur = 1.0e-30f; // the long way: blur * 0.002 * sin (phase) is about 1e-33 and the delay time plus it is the delay time
            b.params = p;
            bool same = true;
            for (int blk = 0; blk < 120; ++blk)
            {
                float l[64], r[64], l2[64], r2[64];
                for (int i = 0; i < 64; ++i)
                {
                    l[i] = l2[i] = uni (rng, -0.5f, 0.5f);
                    r[i] = r2[i] = uni (rng, -0.5f, 0.5f);
                }
                if (blk == 60)
                {
                    // the blur opens and closes again in the middle of the run (the phases have run on in both): the opened part is compared with itself
                    // (not part of the comparison: both get the same value afterwards)
                    Bode::Params q = a.params;
                    q.blur = 0.5f;
                    a.params = q;
                    b.params = q;
                }
                if (blk == 61)
                {
                    Bode::Params q = a.params;
                    q.blur = 0.0f;
                    a.params = q;
                    q.blur = 1.0e-30f;
                    b.params = q;
                }
                a.process (l, r, 64);
                b.process (l2, r2, 64);
                same = same && sameSamples (l, l2, 64) && sameSamples (r, r2, 64);
            }
            CHECK (same);
        }
}
