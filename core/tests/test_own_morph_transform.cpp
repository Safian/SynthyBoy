// SynthyBoy - own engine tests: the NEON inverse transform of the spectral morph cycle (morph.cpp inverseFft) gives the very bits of its scalar loops, for every morph type, both transform
// sizes (the full one and the half size one of tier low) and a range of amounts and band limits. Compiled with the NEON form switched on (also on Apple's arm64, where the engine runs the
// scalar code) and without fused multiply-add, together with the morph and the wavetable sources it needs (core/tests/CMakeLists.txt): it links nothing else of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstring>
#include <vector>

#include "osc/morph.h"
#include "test_runner.h"

using namespace synthyboy::own;

TEST (own_morph_neon_transform_equals_the_scalar_transform)
{
    constexpr int kCyc = Wavetable::kCycle;
    std::vector<std::vector<float>> cc (2, std::vector<float> (kCyc / 2 + 1, 0.0f)), ss = cc;
    for (int f = 0; f < 2; ++f)
        for (int h = 1; h <= 1023; ++h)
        {
            ss[static_cast<size_t> (f)][static_cast<size_t> (h)] = 1.0f / static_cast<float> (h) * (f != 0 ? 0.7f : 1.0f);
            cc[static_cast<size_t> (f)][static_cast<size_t> (h)] = 0.3f / static_cast<float> (h) * static_cast<float> ((h * 7 + f) % 5 - 2);
        }
    Wavetable wt;
    wt.build (cc, ss, false);
    static MorphScratch scratch;
    std::vector<float> a (static_cast<size_t> (kCyc) + 4), b (static_cast<size_t> (kCyc) + 4);
    int builds = 0, differ = 0;
    for (int type = 0; type < 12; ++type)
        for (float m : { 0.0f, 0.17f, 0.5f, 0.83f, 1.0f })
            for (int K : { 40, 300, 1000 })
                for (int fast = 0; fast < 2; ++fast)
                {
                    buildMorphCycle (wt, 0.4f, true, type, m, K, 0.004f, 44100.0, a.data() + 1, scratch, fast != 0, false);
                    buildMorphCycle (wt, 0.4f, true, type, m, K, 0.004f, 44100.0, b.data() + 1, scratch, fast != 0, true);
                    ++builds;
                    differ += std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) != 0 ? 1 : 0;
                }
    CHECK_EQ (builds, 360);
    CHECK_EQ (differ, 0);
}
