// SynthyBoy - core/filters-serum tests: the pair kernels (Filter::processPair) give the samples of two process () calls to the last bit.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This test is compiled with the library's sources and without fused multiply-add (core/filters-serum/tests/CMakeLists.txt), so "to the last bit" can be asked of
// both forms of the kernel: the reference lanes (two plain floats) on every platform, and the NEON lanes where the build has them.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "synthyboy/filters/filter.h"
#include "filter_lanes.h"
#include "test_runner.h"

using namespace synthyboy::filters;

namespace
{
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 12345u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }           // 0 .. 1
    float bi() { return 2.0f * uni() - 1.0f; }                                                  // -1 .. 1
};

using PairFn = bool (*) (Filter&, Filter&, const float*, float*, const float*, float*, int);

struct KernelForm { const char* name; PairFn fn; bool available; };
const KernelForm kForms[] = { { "reference lanes", &Filter::processPairReference, true }, { "NEON lanes", &Filter::processPair, Filter::pairKernelAvailable() } };

const FilterType kTypes[] = { FilterType::ScreamLP, FilterType::ScreamBP, FilterType::EMSLadder, FilterType::MGLadder, FilterType::Diffusor, FilterType::Allpasses,
                              FilterType::LH, FilterType::LB, FilterType::LP, FilterType::LN, FilterType::HB, FilterType::HP, FilterType::HN, FilterType::BP, FilterType::BN,
                              FilterType::PP, FilterType::PN, FilterType::NN,
                              FilterType::CmbL, FilterType::FlgL, FilterType::PhsL, FilterType::CmbH, FilterType::FlgH, FilterType::PhsH, FilterType::CmbHL,
                              FilterType::FlgHL, FilterType::PhsHL };
const char* const kTypeNames[] = { "Scream LP", "Scream BP", "EMS Ladder", "MG Ladder", "Diffusor", "Allpasses",
                                   "Multi LH", "Multi LB", "Multi LP", "Multi LN", "Multi HB", "Multi HP", "Multi HN", "Multi BP", "Multi BN", "Multi PP", "Multi PN", "Multi NN",
                                   "Cmb L", "Flg L", "Phs L", "Cmb H", "Flg H", "Phs H", "Cmb HL", "Flg HL", "Phs HL" };

FilterParams randomParams (Rng& r, int hold, bool extreme)
{
    FilterParams p;
    p.cutoff = 20.0f * std::pow (1000.0f, r.uni());            // 20 Hz .. 20 kHz
    if (extreme)
    {
        static const float kEdges[] = { 20.0f, 20000.0f, 19999.0f, 20.5f, 21609.0f /* = 0.49 sr, the clamp */, 30000.0f /* above the clamp */ };
        p.cutoff = kEdges[r.next() % 6u];
    }
    p.resonance = extreme ? (r.uni() < 0.5f ? 1.0f : (r.uni() < 0.5f ? 0.97f : 0.0f)) : r.uni();
    p.drive = extreme ? 1.0f : r.uni();
    p.var = r.uni();
    p.blend = r.uni();
    p.mix = r.uni() < 0.5f ? 1.0f : r.uni();
    p.coefHold = hold;
    return p;
}

/** A test signal with everything the sanitizers and the clippers branch on: noise, bursts far beyond the clip points, silence, values below 1e-18, and now and then
    a NaN or an infinity. */
void fillInput (Rng& r, float* x, int n, int block)
{
    const int kind = block % 9;
    for (int i = 0; i < n; ++i)
    {
        float v = r.bi();
        if (kind == 3) v *= 12.0f;
        else if (kind == 5) v = 0.0f;
        else if (kind == 6) v *= 1.0e-19f;
        else if (kind == 7) v *= 0.05f;
        else if (kind == 8)
        {
            uint32_t sub = r.next() & 0x807fffffu; // a subnormal (or a zero) of either sign
            std::memcpy (&v, &sub, 4);
        }
        const uint32_t dice = r.next() % 997u;
        if (dice == 0) v = std::numeric_limits<float>::quiet_NaN();
        else if (dice == 1) v = std::numeric_limits<float>::infinity();
        else if (dice == 2) v = -std::numeric_limits<float>::infinity();
        else if (dice == 3) v = -0.0f;
        x[i] = v;
    }
}

bool sameBits (const float* a, const float* b, int n) { return std::memcmp (a, b, static_cast<size_t> (n) * sizeof (float)) == 0; }

/** Two pairs of filters with the same history: one pair runs process () twice per block, the other the kernel (or, in the blocks `mixedMono` picks, process () as
    well: the kernel must leave the state exactly as two calls leave it). Returns the number of blocks whose samples differ, or -1 if the kernel refused a block. */
int compare (PairFn fn, FilterType type, int hold, uint32_t seed, bool sameCutoff, bool extreme, bool mixedMono, long long* samples, bool sweep = false)
{
    Filter monoL, monoR, pairL, pairR;
    for (Filter* f : { &monoL, &monoR, &pairL, &pairR })
    {
        f->prepare (44100.0);
        f->setType (static_cast<int> (type));
    }
    Rng r (seed);
    float inL[64], inR[64], aL[64], aR[64], bL[64], bR[64];
    int bad = 0;
    FilterParams pl = randomParams (r, hold, extreme), pr = pl;
    for (int block = 0; block < 700; ++block)
    {
        if (block % 23 == 0)
        {
            pl = randomParams (r, hold, extreme);
            pr = pl;
            if (! sameCutoff)
                pr.cutoff = 20.0f * std::pow (1000.0f, r.uni());
        }
        else if (sweep)
        {
            // a sweep across the whole range, a new cutoff every block (up by a fifth of an octave per block, back to 20 Hz above 20 kHz)
            pl.cutoff = pl.cutoff * 1.15f > 20000.0f ? 20.0f : pl.cutoff * 1.15f;
            pr.cutoff = sameCutoff ? pl.cutoff : (pr.cutoff * 1.12f > 20000.0f ? 20.0f : pr.cutoff * 1.12f);
            pl.resonance = pr.resonance = std::min (1.0f, std::max (0.0f, pl.resonance + 0.05f * r.bi()));
        }
        else if (block % 5 == 0)
        {
            pl.cutoff = std::min (20000.0f, std::max (20.0f, pl.cutoff * (1.0f + 0.2f * r.bi()))); // a moving cutoff: the smoother and the hold are at work
            pr.cutoff = sameCutoff ? pl.cutoff : std::min (20000.0f, std::max (20.0f, pr.cutoff * (1.0f + 0.2f * r.bi())));
            pl.resonance = pr.resonance = std::min (1.0f, std::max (0.0f, pl.resonance + 0.1f * r.bi()));
        }
        if (type == FilterType::Diffusor)
            pr.var = pl.var; // the kernel needs the same number of stages on both sides (the engine always has: one parameter for both)
        monoL.setParams (pl);
        pairL.setParams (pl);
        monoR.setParams (pr);
        pairR.setParams (pr);
        static const int kSizes[] = { 32, 32, 32, 7, 1, 64, 13, 32 };
        const int n = kSizes[block % 8];
        fillInput (r, inL, n, block);
        fillInput (r, inR, n, block + 4);
        monoL.process (inL, aL, n);
        monoR.process (inR, aR, n);
        if (mixedMono && (block % 3) == 1)
        {
            pairL.process (inL, bL, n);
            pairR.process (inR, bR, n);
        }
        else if (! fn (pairL, pairR, inL, bL, inR, bR, n))
            return -1;
        if (! sameBits (aL, bL, n) || ! sameBits (aR, bR, n))
            ++bad;
        *samples += 2 * n;
    }
    return bad;
}
} // namespace

TEST (pair_kernels_give_the_samples_of_two_process_calls_to_the_last_bit)
{
    for (const KernelForm& form : kForms)
    {
        if (! form.available)
        {
            std::printf ("    %s: not in this build\n", form.name);
            continue;
        }
        for (size_t t = 0; t < sizeof kTypes / sizeof kTypes[0]; ++t)
        {
            long long samples = 0;
            int bad = 0, refused = 0, runs = 0;
            for (const int hold : { 1, 4, 3 })
                for (uint32_t seed = 1; seed <= 6; ++seed)
                    for (const bool sameCutoff : { true, false })
                    {
                        const bool extreme = seed == 5, mixed = seed % 2 == 0, sweep = seed == 3;
                        const int r = compare (form.fn, kTypes[t], hold, seed * 131u + static_cast<uint32_t> (t), sameCutoff, extreme, mixed, &samples, sweep);
                        ++runs;
                        if (r < 0) ++refused;
                        else bad += r;
                    }
            std::printf ("    %-16s %-12s %d runs, %lld samples: %d blocks differ, %d runs refused\n", form.name, kTypeNames[t], runs, samples, bad, refused);
            CHECK_EQ (bad, 0);
            CHECK_EQ (refused, 0);
        }
    }
}

TEST (lane_sanitize_and_soft_clip_equal_the_scalar_functions_on_every_bit_pattern)
{
#if SYNTHYBOY_FILTER_NEON
    // NeonLanes::sanitized and ::softClipped make their tests with the integer compares of dsp/neon_cmp.h: every one of the 2^32 inputs against sanitize () and
    // softClip () of filter_dsp.h (NaNs, infinities, zeros and subnormals included)
    long long badSan = 0, badClip = 0;
    for (uint64_t u = 0; u < (1ull << 32); u += 2)
    {
        const uint32_t b[2] = { static_cast<uint32_t> (u), static_cast<uint32_t> (u + 1) };
        float x[2], s[2], c[2];
        std::memcpy (x, b, 8);
        const NeonLanes v = NeonLanes::set (x[0], x[1]);
        const NeonLanes vs = NeonLanes::sanitized (v), vc = NeonLanes::softClipped (v);
        for (int l = 0; l < 2; ++l)
        {
            s[l] = vs.get (l);
            c[l] = vc.get (l);
            const float ws = sanitize (x[l]), wc = softClip (x[l]);
            badSan += std::memcmp (&s[l], &ws, 4) != 0 ? 1 : 0;
            // softClip of a NaN is a NaN in both (its payload is the hardware's): compared as "both NaN"
            badClip += (std::isnan (wc) ? ! std::isnan (c[l]) : std::memcmp (&c[l], &wc, 4) != 0) ? 1 : 0;
        }
    }
    std::printf ("    all 2^32 inputs: sanitized differs in %lld, softClipped in %lld\n", badSan, badClip);
    CHECK_EQ (badSan, 0);
    CHECK_EQ (badClip, 0);
#else
    std::printf ("    no NEON lanes in this build\n");
    CHECK (true);
#endif
}

TEST (pair_kernel_refuses_what_it_has_no_form_for_and_leaves_the_filters_alone)
{
    for (const KernelForm& form : kForms)
    {
        if (! form.available)
            continue;
        float in[32], outA[32], outB[32], ref[32];
        Rng r (7);
        for (float& v : in)
            v = r.bi();
        auto unchanged = [&] (Filter& a, Filter& b, Filter& twinA, Filter& twinB)
        {
            // after a refusal the filters continue exactly like their twins that were never offered to the kernel
            a.process (in, outA, 32);
            twinA.process (in, ref, 32);
            const bool okA = sameBits (outA, ref, 32);
            b.process (in, outB, 32);
            twinB.process (in, ref, 32);
            return okA && sameBits (outB, ref, 32);
        };
        auto make = [] (Filter& f, FilterType type, float var, int hold)
        {
            f.prepare (44100.0);
            f.setType (static_cast<int> (type));
            FilterParams p;
            p.cutoff = 900.0f;
            p.resonance = 0.6f;
            p.drive = 0.4f;
            p.var = var;
            p.coefHold = hold;
            f.setParams (p);
        };
        {
            Filter a, b, ta, tb; // a type without a kernel
            for (Filter* f : { &a, &b, &ta, &tb }) make (*f, FilterType::Low24, 0.5f, 4);
            CHECK (! form.fn (a, b, in, outA, in, outB, 32));
            CHECK (unchanged (a, b, ta, tb));
        }
        {
            Filter a, b, ta, tb; // two different types
            make (a, FilterType::ScreamLP, 0.5f, 4); make (ta, FilterType::ScreamLP, 0.5f, 4);
            make (b, FilterType::ScreamBP, 0.5f, 4); make (tb, FilterType::ScreamBP, 0.5f, 4);
            CHECK (! form.fn (a, b, in, outA, in, outB, 32));
            CHECK (unchanged (a, b, ta, tb));
        }
        {
            Filter a, b, ta, tb; // two different coefficient holds
            make (a, FilterType::EMSLadder, 0.5f, 4); make (ta, FilterType::EMSLadder, 0.5f, 4);
            make (b, FilterType::EMSLadder, 0.5f, 1); make (tb, FilterType::EMSLadder, 0.5f, 1);
            CHECK (! form.fn (a, b, in, outA, in, outB, 32));
            CHECK (unchanged (a, b, ta, tb));
        }
        {
            Filter a, b, ta, tb; // a Diffusor with 2 stages on one side and 7 on the other
            make (a, FilterType::Diffusor, 0.2f, 4); make (ta, FilterType::Diffusor, 0.2f, 4);
            make (b, FilterType::Diffusor, 0.9f, 4); make (tb, FilterType::Diffusor, 0.9f, 4);
            CHECK (! form.fn (a, b, in, outA, in, outB, 32));
            CHECK (unchanged (a, b, ta, tb));
        }
    }
}

TEST (pair_kernel_speed_for_information)
{
    // no assertion: the time of a stereo sample through two process () calls and through each form of the kernel, on the machine of the test (hold 4, the MPC's
    // profile): a settled cutoff (a held note), and a cutoff that a modulator moves every block (left and right apart, as with a filter pan)
    for (const bool moving : { false, true })
    for (size_t t = 0; t < sizeof kTypes / sizeof kTypes[0]; ++t)
    {
        double ns[3] = { 0.0, 0.0, 0.0 };
        for (int form = 0; form < 3; ++form)
        {
            if (form == 2 && ! Filter::pairKernelAvailable())
                continue;
            Filter l, r;
            for (Filter* f : { &l, &r })
            {
                f->prepare (44100.0);
                f->setType (static_cast<int> (kTypes[t]));
                FilterParams p;
                p.cutoff = 1800.0f;
                p.resonance = 0.55f;
                p.drive = 0.3f;
                p.var = 0.5f;
                p.coefHold = 4;
                f->setParams (p);
            }
            Rng rng (99);
            float inL[32], inR[32], outL[32], outR[32];
            float sink = 0.0f;
            const int blocks = 40000;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                if (moving)
                {
                    FilterParams p = l.getParams();
                    p.cutoff = 1800.0f * (1.0f + 0.5f * std::sin (0.013f * static_cast<float> (b)));
                    l.setParams (p);
                    p.cutoff *= 1.3f;
                    r.setParams (p);
                }
                for (int i = 0; i < 32; ++i)
                {
                    inL[i] = 0.5f * rng.bi();
                    inR[i] = 0.5f * rng.bi();
                }
                if (form == 0)
                {
                    l.process (inL, outL, 32);
                    r.process (inR, outR, 32);
                }
                else if (form == 1)
                    Filter::processPairReference (l, r, inL, outL, inR, outR, 32);
                else
                    Filter::processPair (l, r, inL, outL, inR, outR, 32);
                sink += outL[7] + outR[19];
            }
            const auto t1 = std::chrono::steady_clock::now();
            ns[form] = std::chrono::duration<double, std::nano> (t1 - t0).count() / (blocks * 32.0);
            if (sink == 1234.5f)
                std::puts ("");
        }
        std::printf ("    %-12s %s ns per stereo sample (with the input noise): two calls %.1f, reference lanes %.1f, NEON lanes %.1f\n", kTypeNames[t], moving ? "moving " : "settled", ns[0], ns[1], ns[2]);
    }
    CHECK (true);
}
