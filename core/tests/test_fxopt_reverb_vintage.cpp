// SynthyBoy - own engine tests: the per-chunk hoist of the vintage reverb (type 3) (docs/plans/reverb-convolve.md task R1). The output must not change by a bit; the cost per sample must fall.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>

#include "fx/fx_reverb.h"
#include "fxopt_reverb_util.h"
#include "ref/reverb_reference.h"
#include "test_runner.h"

using synthyboy::own::Reverb;
using RefReverb = synthyboy::own::ref::Reverb;

TEST (fxopt_vintage_bit_identical)
{
    for (const int sr : { 44100, 48000 })
    {
        const fxopt::Program in = fxopt::makeProgram (static_cast<size_t> (3 * sr), static_cast<double> (sr), 21);
        for (const char set : { 'D', 'S', 'P' })
            for (const auto& tc : fxopt::reverbCases())
                for (const int chunk : { 32, 7 })
                {
                    // type 3 is what R1 changes; every other type must stay equal to the reference
                    const fxopt::Program live = fxopt::renderReverb<Reverb> (fxopt::reverbParams<Reverb::Params> (set, tc.first, tc.second), sr, in, chunk);
                    const fxopt::Program ref = fxopt::renderReverb<RefReverb> (fxopt::reverbParams<RefReverb::Params> (set, tc.first, tc.second), sr, in, chunk);
                    if (tc.first == 3 && chunk == 32)
                        std::printf ("VINTAGE-CHECKSUM set=%c sr=%d %016llx\n", set, sr, static_cast<unsigned long long> (fxopt::hashProgram (live)));
                    CHECK (fxopt::sameProgram (live, ref));
                }
    }
}

TEST (fxopt_vintage_speed)
{
    if (sbtest::instrumented())
        return;
    const double sr = 44100.0;
    const fxopt::Program in = fxopt::makeProgram (2 * 44100, sr, 22);
    const Reverb::Params pl = fxopt::reverbParams<Reverb::Params> ('D', 3, 2);
    const RefReverb::Params pr = fxopt::reverbParams<RefReverb::Params> ('D', 3, 2);
    const double ratio = fxopt::cpuRatio ([&] { (void) fxopt::renderReverb<Reverb> (pl, sr, in, 32); }, [&] { (void) fxopt::renderReverb<RefReverb> (pr, sr, in, 32); }, 5);
    std::printf ("    vintage cpu ratio live / reference %.3f (model 0.65 ARM, 0.72 x86; limit 0.85)\n", ratio);
    CHECK (ratio <= 0.85);
}
