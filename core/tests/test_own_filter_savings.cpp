// SynthyBoy - own engine tests: the CPU savings of the classic voice filters (docs/dsp-knowledge/filters.md, "Savings of 2026-11-26"). Every change that must not change a sample
// is compared with the code it replaced, bit for bit (the floats' bit patterns, not ==); the changes that may differ state their bound and are tested against it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "synthyboy/filters/filter.h"
#include "synthyboy/filters/filter_types.h"
#include "voice/classic_filters.h"
#include "voice/filter_unit.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy::own;

namespace
{
uint32_t bitsOf (float x)
{
    uint32_t u;
    std::memcpy (&u, &x, sizeof u);
    return u;
}

bool sameBits (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
}

/** Noise in -amp .. amp; with `special` a few values of every kind a recursion can meet (denormals, tiny, huge, signed zeros). */
void fillInput (sbtest::Rng& rng, std::vector<float>& v, float amp, bool special)
{
    static const float kSpecial[] = { 0.0f, -0.0f, 1.0e-30f, -1.0e-30f, 1.0e-39f, -1.0e-39f, 1.0e-18f, 1.0f, -1.0f, 8.0f, -8.0f, 1000.0f };
    for (auto& x : v)
    {
        x = static_cast<float> (rng.uniform() * 2.0 - 1.0) * amp;
        if (special && rng.u32() % 16u == 0u)
            x = kSpecial[rng.u32() % (sizeof kSpecial / sizeof kSpecial[0])];
    }
}

ClassicParams makeParams (int model, int style, bool fast, float cutoff, float res, float driveDb, float blendRaw)
{
    ClassicParams p;
    p.model = model;
    p.style = style;
    p.cutoffHz = cutoff;
    p.res = res;
    p.driveDb = driveDb;
    p.blendRaw = blendRaw;
    p.fast = fast;
    p.combOffset = 30.0f;
    return p;
}

int styleCount (int model)
{
    switch (model)
    {
        case 0: case 1: case 3: return 5;
        case 2: return 5;
        case 4: return 2;
        case 5: return 3;
        case 6: return 6;
        default: return 3;
    }
}
} // namespace

/** Item 1: the two channels of a filter with equal parameters share the setup of every piece. Against the separate passes the code had before (the left channel, adoptRamp,
    the right channel), every model and style, tier low and exact, parameters still / only the cutoff moving / everything moving, a block grid cut at odd places. */
TEST (own_filter_savings_twin_channels_are_bit_identical_to_separate_passes)
{
    long compared = 0;
    for (int model = 0; model <= 7; ++model)
        for (int style = 0; style < styleCount (model); ++style)
            for (int fastI = 0; fastI < 2; ++fastI)
                for (int motion = 0; motion < 3; ++motion)
                {
                    ClassicFilter ra, rb, ta, tb;
                    for (ClassicFilter* f : { &ra, &rb, &ta, &tb })
                        f->prepare (44100.0);
                    sbtest::Rng rng (0x7714ull + static_cast<uint64_t> (model * 1000 + style * 100 + fastI * 10 + motion));
                    ClassicParams prev = makeParams (model, style, fastI != 0, 900.0f, 0.4f, 6.0f, 0.7f);
                    const int cuts[3][3] = { { 32, 0, 0 }, { 5, 27, 0 }, { 8, 8, 16 } };
                    for (int tick = 0; tick < 60; ++tick)
                    {
                        ClassicParams cur = prev;
                        if (motion >= 1)
                            cur.cutoffHz = std::clamp (prev.cutoffHz * (0.8f + 0.5f * static_cast<float> (rng.uniform())), 60.0f, 18000.0f);
                        if (motion >= 2)
                        {
                            cur.res = clamp01 (prev.res + 0.2f * static_cast<float> (rng.uniform() - 0.5));
                            cur.driveDb = std::clamp (prev.driveDb + 4.0f * static_cast<float> (rng.uniform() - 0.5), 0.0f, 24.0f);
                            cur.blendRaw = std::clamp (prev.blendRaw + 0.3f * static_cast<float> (rng.uniform() - 0.5), 0.0f, 2.0f);
                        }
                        const int* cut = cuts[tick % 3];
                        int t0 = 0;
                        for (int part = 0; part < 3 && cut[part] > 0; ++part)
                        {
                            const int n = cut[part];
                            std::vector<float> inA (static_cast<size_t> (n)), inB (static_cast<size_t> (n)), oRA (static_cast<size_t> (n)), oRB (static_cast<size_t> (n)),
                                oTA (static_cast<size_t> (n)), oTB (static_cast<size_t> (n));
                            fillInput (rng, inA, 0.6f, tick % 7 == 0);
                            fillInput (rng, inB, 0.6f, tick % 7 == 0);
                            // the code before: the left channel, its ramp setups to the right one, the right channel
                            ra.process (inA.data(), oRA.data(), n, prev, cur, t0);
                            rb.adoptRamp (ra);
                            rb.process (inB.data(), oRB.data(), n, prev, cur, t0);
                            ClassicFilter::processTwin (ta, tb, inA.data(), oTA.data(), inB.data(), oTB.data(), n, prev, cur, t0);
                            CHECK (sameBits (oRA, oTA));
                            CHECK (sameBits (oRB, oTB));
                            compared += 2 * n;
                            t0 += n;
                        }
                        prev = cur;
                    }
                }
    std::printf ("SAVINGS-TWIN compared %ld samples\n", compared);
}

namespace
{
FilterSettings settingsFor (int model, int style, bool fast, float cutoff, float cutoffR, float res, float drive)
{
    FilterSettings s;
    s.model = model;
    s.style = style;
    s.fast = fast;
    s.cutoffHz = cutoff;
    s.cutoffHzR = cutoffR;
    s.resonance = res;
    s.driveDb = drive;
    s.blend = 0.35f;
    s.mix = 1.0f;
    s.combOffset = 30.0f;
    return s;
}
} // namespace

/** The same at the level of the voice's FilterUnit: the classic models 0 .. 7 with the pan 0 (equal cutoffs, the twin pass) and a pan (different cutoffs), against the unit with
    `scalarOnly` (which keeps the separate passes). */
TEST (own_filter_savings_filter_unit_twin_matches_the_scalar_only_passes)
{
    for (int model = 0; model <= 7; ++model)
        for (int style = 0; style < styleCount (model); ++style)
            for (int fastI = 0; fastI < 2; ++fastI)
                for (int pan = 0; pan < 2; ++pan)
                {
                    FilterUnit newer, older;
                    newer.prepare (44100.0);
                    older.prepare (44100.0);
                    sbtest::Rng rng (0x9a11ull + static_cast<uint64_t> (model * 977 + style * 61 + fastI * 7 + pan));
                    float cutoff = 1200.0f;
                    for (int tick = 0; tick < 50; ++tick)
                    {
                        cutoff = std::clamp (cutoff * (0.85f + 0.3f * static_cast<float> (rng.uniform())), 80.0f, 16000.0f);
                        FilterSettings s = settingsFor (model, style, fastI != 0, cutoff, pan != 0 ? cutoff * 1.3f : -1.0f, 0.5f * static_cast<float> (rng.uniform()), 3.0f);
                        s.scalarOnly = false;
                        newer.set (s);
                        s.scalarOnly = true;
                        older.set (s);
                        std::vector<float> inL (32), inR (32), a1 (32), a2 (32), b1 (32), b2 (32);
                        fillInput (rng, inL, 0.5f, false);
                        fillInput (rng, inR, 0.5f, false);
                        newer.process (inL.data(), inR.data(), a1.data(), a2.data(), 32, 0);
                        older.process (inL.data(), inR.data(), b1.data(), b2.data(), 32, 0);
                        CHECK (sameBits (a1, b1));
                        CHECK (sameBits (a2, b2));
                    }
                }
}
