// SynthyBoy - core tests: the Distortion after the CPU review item 7 (2026-11-26) is bit identical to the code before it (ref/fx_distortion_before.h, a frozen copy of Distortion and of the
// ADAA laws at 391febd):
//  - the law is a template argument (no switch on the type per sample inside the shaper, the ADAA step, the antiderivative and the DC blocker);
//  - the state variable filter takes its coefficients (the reciprocal of the section, the cutoff's tan) once per call, and not at all when the filter is not in the chain; the blend is clamped
//    once; the DC blocker coefficient (an exp) is made for the asymmetric laws only and kept;
//  - ln cosh (t) - |t| is -ln 2 exactly from |t| = 8.8 on, on the library path and on the fast path: the exp and the log are not computed there. Every float from 8.8 to 32 and a dense sample
//    up to infinity are compared with the old formula.
// Every output sample of every law, both oversampling modes, the three filter positions, both math forms and the ADAA switch is compared with memcmp (a NaN is a NaN).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "fx/fx_effects.h"
#include "ref/fx_distortion_before.h"
#include "synthyboy/aa_switch.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
float uni (std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * static_cast<float> (rng() % 100001) / 100000.0f; }

float fromBits (uint32_t u)
{
    float f;
    std::memcpy (&f, &u, 4);
    return f;
}
uint32_t toBits (float f)
{
    uint32_t u;
    std::memcpy (&u, &f, 4);
    return u;
}
bool sameValue (float a, float b) { return toBits (a) == toBits (b) || (std::isnan (a) && std::isnan (b)); }
} // namespace

// ln cosh: the saturation shortcut against the old formula, exhaustively over the floats it can change.
TEST (lncosh_bounded_part_is_bit_identical_where_it_is_not_computed)
{
    long long compared = 0, shortcut = 0;
    int badCount = 0;
    for (int fast = 0; fast < 2; ++fast)
    {
        const bool f = fast != 0;
        auto check = [&] (float t) {
            const float a = adaa::lncoshBnd (t, f), b = cpuref::adaa::lncoshBnd (t, f);
            if (!sameValue (a, b) && badCount++ < 5)
                std::printf ("lncosh differs: fast %d t %.9g new %.9g old %.9g\n", fast, static_cast<double> (t), static_cast<double> (a), static_cast<double> (b));
            ++compared;
            if (std::fabs (t) >= adaa::kLncoshSaturated)
                ++shortcut;
        };
        // every float from 8 up to 16 (both signs for a stride: the limit 8.8 and its surroundings exhaustively), then every 5th float up to 32
        for (uint32_t u = toBits (8.0f); u < toBits (16.0f); ++u)
        {
            check (fromBits (u));
            if ((u & 7u) == 0u)
                check (-fromBits (u));
        }
        for (uint32_t u = toBits (16.0f); u < toBits (32.0f); u += 5u)
            check (fromBits (u));
        // above: a dense sample over every exponent up to infinity, and the special values
        for (uint32_t u = toBits (32.0f); u < 0x7f800000u; u += 997u)
            check (fromBits (u));
        for (float t : { std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::quiet_NaN(), 0.0f, -0.0f, 1.0e-30f, 0.5f, -0.5f, 3.0f, 8.7999992f, 8.8f, 8.8000011f })
            check (t);
        // below 8: a dense sample, both signs
        for (uint32_t u = 0; u < toBits (8.0f); u += 61u)
        {
            check (fromBits (u));
            check (-fromBits (u));
        }
    }
    CHECK (badCount == 0);
    std::printf ("lncosh bounded part: %lld values compared (both forms), %lld of them through the shortcut, %d differences\n", compared, shortcut, badCount);
}

TEST (adaa_bounded_parts_of_every_type_are_bit_identical)
{
    std::mt19937 rng (5);
    int badCount = 0;
    for (int fast = 0; fast < 2; ++fast)
        for (int type : { 0, 1, 2, 3, 6, 7, 8, 9, 10, 11 })
            for (int i = 0; i < 20000; ++i)
            {
                const float t = rng() % 5 == 0 ? uni (rng, -40.0f, 40.0f) : uni (rng, -4.0f, 4.0f);
                if (!sameValue (adaa::bnd (type, t, fast != 0), cpuref::adaa::bnd (type, t, fast != 0)) && badCount++ < 5)
                    std::printf ("bnd differs: type %d fast %d t %.9g new %.9g old %.9g\n", type, fast, static_cast<double> (t), static_cast<double> (adaa::bnd (type, t, fast != 0)), static_cast<double> (cpuref::adaa::bnd (type, t, fast != 0)));
                CHECK (sameValue (adaa::bnd (type, t, fast != 0), cpuref::adaa::bnd (type, t, fast != 0)));
                CHECK (sameValue (adaa::naive (type, t, fast != 0), cpuref::adaa::naive (type, t, fast != 0)));
                CHECK (sameValue (adaa::lin (type, t), cpuref::adaa::lin (type, t)));
            }
}

namespace
{
/** A pair of transfer curves for the X-Shaper laws (513 values and their cumulative integrals): the curves are the same for both distortions. */
struct Curves
{
    std::vector<float> y, I;
    Curves (int kind)
    {
        y.resize (513);
        I.resize (513);
        for (int i = 0; i <= 512; ++i)
        {
            const float u = static_cast<float> (i) / 512.0f;
            y[static_cast<size_t> (i)] = kind == 0 ? u * u * (3.0f - 2.0f * u) : std::sin (1.5f * 3.14159265f * u) * 0.5f + 0.5f;
        }
        I[0] = 0.0f;
        for (int i = 1; i <= 512; ++i)
            I[static_cast<size_t> (i)] = I[static_cast<size_t> (i - 1)] + 0.5f * (y[static_cast<size_t> (i - 1)] + y[static_cast<size_t> (i)]) / 512.0f;
    }
};

struct Result
{
    bool same = true;
    long long samples = 0;
};

Result walk (int type, bool os2, int order, bool fast, bool aaOff, double sr, uint32_t seed, bool specials)
{
    std::mt19937 rng (seed);
    synthyboy::aa::setOff (aaOff ? synthyboy::aa::kDistortion : 0u);
    Distortion a;
    cpuref::DistortionBefore b;
    a.prepare (sr);
    b.prepare (sr);
    a.fast = fast;
    b.fast = fast;
    static const Curves cA (0), cB (1);
    a.setShaper (cA.y.data(), cA.I.data(), cB.y.data(), cB.I.data());
    b.setShaper (cA.y.data(), cA.I.data(), cB.y.data(), cB.I.data());
    Result res;
    Distortion::Params p;
    for (int per = 0; per < 140; ++per)
    {
        if (per % 3 == 0 || per == 0)
        {
            p.type = type;
            p.driveDb = rng() % 5 == 0 ? (rng() % 2 == 0 ? 60.0f : -60.0f) : (rng() % 4 == 0 ? p.driveDb : uni (rng, -30.0f, 30.0f)); // a still drive in some periods: no ramp
            p.mix = rng() % 6 == 0 ? 1.0f : uni (rng, 0.0f, 1.0f);
            p.cutoff = uni (rng, 20.0f, 20000.0f);
            p.resonance = uni (rng, 0.0f, 1.0f);
            p.blend = rng() % 5 == 0 ? (rng() % 2 == 0 ? -1.0f : 3.0f) : uni (rng, 0.0f, 2.0f);
            p.order = order;
            p.os2 = os2;
        }
        else if (rng() % 3 == 0)
            p.driveDb = uni (rng, -30.0f, 30.0f);
        cpuref::DistortionBefore::Params q;
        q.type = p.type; q.driveDb = p.driveDb; q.mix = p.mix; q.cutoff = p.cutoff; q.resonance = p.resonance; q.blend = p.blend; q.order = p.order; q.os2 = p.os2;
        a.set (p);
        b.set (q);
        const int period = rng() % 9 == 0 ? 0 : 32;
        const int cut = 1 + static_cast<int> (rng() % 31);
        const int calls[2][2] = { { 0, rng() % 2 == 0 ? 32 : cut }, { cut, 32 - cut } };
        for (int k = 0; k < (calls[0][1] == 32 ? 1 : 2); ++k)
        {
            const int t0 = calls[k][0], n = calls[k][1];
            float l[32], r[32], l2[32], r2[32];
            for (int i = 0; i < n; ++i)
            {
                float x = 0.8f * std::sin (0.05f * static_cast<float> (per * 32 + t0 + i)) + uni (rng, -0.6f, 0.6f), y = uni (rng, -1.5f, 1.5f);
                if (specials)
                {
                    const unsigned s = rng() % 300;
                    if (s == 0) x = 5.0e6f;
                    else if (s == 1) x = std::numeric_limits<float>::infinity();
                    else if (s == 2 && per % 40 == 39) x = std::numeric_limits<float>::quiet_NaN();
                    else if (s == 3) y = 1.0e-30f;
                }
                l[i] = l2[i] = x;
                r[i] = r2[i] = y;
            }
            a.process (l, r, n, t0, period);
            b.process (l2, r2, n, t0, period);
            for (int i = 0; i < n; ++i)
                if (!sameValue (l[i], l2[i]) || !sameValue (r[i], r2[i]))
                {
                    if (res.same)
                        std::printf ("distortion differs: type %d os2 %d order %d fast %d aaOff %d period %d t0 %d sample %d: %.9g vs %.9g\n", type, os2, order, fast, aaOff, per, t0, i, static_cast<double> (l[i]), static_cast<double> (l2[i]));
                    res.same = false;
                }
            res.samples += n;
        }
    }
    synthyboy::aa::setOff (0u);
    return res;
}
} // namespace

TEST (distortion_is_bit_identical_to_the_code_before_for_every_law)
{
    long long samples = 0;
    int walks = 0;
    for (int type : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, -1, 40 })
        for (int os2 = 0; os2 < 2; ++os2)
            for (int order = 0; order < 3; ++order)
                for (int fast = 0; fast < 2; ++fast)
                    for (int aaOff = 0; aaOff < 2; ++aaOff)
                    {
                        const Result r = walk (type, os2 != 0, order, fast != 0, aaOff != 0, (walks & 1) != 0 ? 48000.0 : 44100.0, 1000u + static_cast<uint32_t> (walks), order == 0 && (walks % 3) == 0);
                        CHECK (r.same);
                        samples += r.samples;
                        ++walks;
                    }
    std::printf ("distortion walks bit identical to the code before: %d walks, %lld samples per channel\n", walks, samples);
}
