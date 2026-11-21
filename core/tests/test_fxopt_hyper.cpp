// SynthyBoy - own engine tests: Hyper / Dimension, hoists, one delay buffer and skipped blocks (docs/plans/reverb-convolve.md task H1).
// The output must stay bit identical to the frozen reference while `unison` is constant; the memory and the cost fall; a change of `unison` reads the real recent input (on purpose).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

#include "fxopt_util.h"
#include "ref/hyper_reference.h"
#include "synthyboy/fx/hyper_dimension.h"
#include "test_runner.h"

namespace
{
using Live = synthyboy::fx::HyperDimension;
using Ref = synthyboy::fx::ref::HyperDimension;

/** One render of `frames` samples at `sr`: the mixes ramp from 0 to the target across the render when `ramp`, retrigger () at the half. */
template <class H>
fxopt::Program render (double sr, int unison, float hyperMix, float dimMix, float levelDb, bool ramp, const fxopt::Program& in, int block)
{
    H effect;
    effect.prepare (sr, 512);
    typename H::Params p;
    p.hyperUnison = unison;
    p.levelDb = levelDb;
    p.hyperMix = hyperMix;
    p.dimMix = dimMix;
    effect.setParams (p);
    effect.reset();
    fxopt::Program out = in;
    const size_t total = out.l.size();
    bool retriggered = false;
    for (size_t pos = 0; pos < total; pos += static_cast<size_t> (block))
    {
        const int n = static_cast<int> (std::min (static_cast<size_t> (block), total - pos));
        if (ramp)
        {
            const float frac = static_cast<float> (pos) / static_cast<float> (total);
            p.hyperMix = hyperMix * frac;
            p.dimMix = dimMix * frac;
            effect.setParams (p);
        }
        if (!retriggered && pos >= total / 2)
        {
            effect.retrigger();
            retriggered = true;
        }
        effect.process (out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

bool same (const fxopt::Program& a, const fxopt::Program& b)
{
    return a.l.size() == b.l.size() && fxopt::sameBits (a.l.data(), b.l.data(), a.l.size()) && fxopt::sameBits (a.r.data(), b.r.data(), a.r.size());
}

uint64_t hashOf (const fxopt::Program& p)
{
    std::vector<float> all (p.l);
    all.insert (all.end(), p.r.begin(), p.r.end());
    return fxopt::fnv1a (all.data(), all.size());
}
} // namespace

TEST (fxopt_hyper_bit_identical)
{
    const float mixes[4][2] = { { 0.5f, 0.5f }, { 0.5f, 0.0f }, { 0.0f, 0.5f }, { 0.0f, 0.0f } };
    for (const double sr : { 44100.0, 48000.0 })
    {
        const fxopt::Program in = fxopt::makeProgram (static_cast<size_t> (3 * sr), sr, 31);
        for (int unison = 0; unison <= 7; ++unison)
            for (const auto& m : mixes)
                for (const float level : { 0.0f, 3.0f })
                    for (const bool ramp : { false, true })
                    {
                        if (sr != 44100.0 && (level != 0.0f || ramp)) // the second rate: the plain case only
                            continue;
                        const fxopt::Program live = render<Live> (sr, unison, m[0], m[1], level, ramp, in, 32);
                        const fxopt::Program ref = render<Ref> (sr, unison, m[0], m[1], level, ramp, in, 32);
                        if (level == 0.0f && !ramp)
                            std::printf ("HYPER-CHECKSUM unison=%d mixes=%g/%g sr=%g %016llx\n", unison, static_cast<double> (m[0]), static_cast<double> (m[1]), sr,
                                         static_cast<unsigned long long> (hashOf (live)));
                        CHECK (same (live, ref));
                    }
    }
    // the block split: 7 and 500 samples (the ramp case changes the parameters per block, so it is compared at one split only)
    const fxopt::Program in = fxopt::makeProgram (3 * 44100, 44100.0, 32);
    for (const int block : { 7, 500 })
        for (const int unison : { 0, 4, 7 })
            CHECK (same (render<Live> (44100.0, unison, 0.5f, 0.5f, 0.0f, false, in, block), render<Ref> (44100.0, unison, 0.5f, 0.5f, 0.0f, false, in, block)));
}

TEST (fxopt_hyper_memory)
{
    Live effect;
    effect.prepare (44100.0, 128);
    std::printf ("    Hyper / Dimension memory %zu bytes (before: 180224)\n", effect.memoryBytes());
    CHECK (effect.memoryBytes() <= 17000u);
}

TEST (fxopt_hyper_speed)
{
    if (sbtest::instrumented())
        return;
    const fxopt::Program in = fxopt::makeProgram (2 * 44100, 44100.0, 33);
    // unison 4, Dimension off, Hyper 0.5: the libm calls fall from 14 to 4 per sample, the divisions from 4 to 0 (model 0.39 x86)
    const double r4 = fxopt::cpuRatio ([&] { (void) render<Live> (44100.0, 4, 0.5f, 0.0f, 0.0f, false, in, 32); }, [&] { (void) render<Ref> (44100.0, 4, 0.5f, 0.0f, 0.0f, false, in, 32); }, 5);
    std::printf ("    hyper cpu ratio unison 4, dim off: %.3f (limit 0.70)\n", r4);
    CHECK (r4 <= 0.70);
    const double r7 = fxopt::cpuRatio ([&] { (void) render<Live> (44100.0, 7, 0.5f, 0.5f, 0.0f, false, in, 32); }, [&] { (void) render<Ref> (44100.0, 7, 0.5f, 0.5f, 0.0f, false, in, 32); }, 5);
    std::printf ("    hyper cpu ratio unison 7, dim on: %.3f (limit 0.85)\n", r7);
    CHECK (r7 <= 0.85);
}

TEST (fxopt_hyper_unison_change_reads_recent_input)
{
    // white noise, Hyper only, no detune: unison 2 for 1 s, then 7. In the first 4 ms after the change the reference reads the never written buffers of the new voices (zeros) and only voices 0 and 1
    // sound; the shared buffer gives all seven: the rms ratio is sqrt (7 / 2) = 1.87.
    const double sr = 44100.0;
    const size_t total = static_cast<size_t> (sr) + 2000;
    fxopt::Program in;
    in.l.resize (total);
    in.r.resize (total);
    uint32_t s = 88172645u;
    for (size_t i = 0; i < total; ++i)
    {
        s = s * 1664525u + 1013904223u;
        in.l[i] = in.r[i] = static_cast<float> (s) * (0.2f / 4294967296.0f) - 0.1f;
    }
    auto run = [&] (auto& effect) {
        typename std::remove_reference<decltype (effect)>::type::Params p;
        effect.prepare (sr, 512);
        p.hyperMix = 1.0f;
        p.dimMix = 0.0f;
        p.hyperDetune = 0.0f;
        p.hyperUnison = 2;
        effect.setParams (p);
        effect.reset();
        fxopt::Program out = in;
        const size_t change = static_cast<size_t> (sr);
        for (size_t pos = 0; pos < total; pos += 100)
        {
            if (pos == change)
            {
                p.hyperUnison = 7;
                effect.setParams (p);
            }
            effect.process (out.l.data() + pos, out.r.data() + pos, static_cast<int> (std::min<size_t> (100, total - pos)));
        }
        double sum = 0.0;
        for (size_t i = change; i < change + 176; ++i) // 4 ms
            sum += static_cast<double> (out.l[i]) * out.l[i] + static_cast<double> (out.r[i]) * out.r[i];
        return std::sqrt (sum / 352.0);
    };
    Live live;
    Ref ref;
    const double rl = run (live), rr = run (ref);
    std::printf ("    rms in the first 4 ms after the unison change: live %.5f reference %.5f ratio %.3f (expected 1.87)\n", rl, rr, rr > 0.0 ? rl / rr : 0.0);
    CHECK (rr > 0.0);
    CHECK (rl / rr >= 1.5);
}
