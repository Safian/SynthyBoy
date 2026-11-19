// SynthyBoy - own engine tests: the voice filter (FilterUnit) with its vector forms gives the samples of its scalar loops (FilterSettings::scalarOnly, the
// --scalar-filters switch of the bench): the pair kernels of the Serum filters and the NEON runaway test and limit, for every Serum model and style.
// Compiled with the library's sources, the NEON forms on (also on Apple's arm64) and without fused multiply-add (core/tests/CMakeLists.txt).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

#include "voice/filter_unit.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
struct HRng
{
    uint32_t s;
    explicit HRng (uint32_t seed) : s (seed * 2654435761u + 3u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return 2.0f * uni() - 1.0f; }
};
} // namespace

TEST (filter_unit_with_its_vector_forms_equals_its_scalar_loops_for_every_serum_model)
{
    // every Serum model and style; random settings (the two sides apart or not, clean mode, mix, tier low or not), cutoffs that move every block, and inputs with
    // loud bursts (runaways), silence, subnormals, tiny values, NaN and infinities
    const int blocks = 200;
    long long bad = 0, total = 0, runaways = 0;
    for (int model = synthyboy::filters::kFirstSerumModel; model <= synthyboy::filters::kLastSerumModel; ++model)
        for (int style = 0; style < synthyboy::filters::serumModelStyleCount (model); ++style)
            for (int run = 0; run < 6; ++run)
            {
                HRng r (static_cast<uint32_t> (model * 1000 + style * 10 + run));
                FilterUnit a, b;
                a.prepare (44100.0);
                b.prepare (44100.0);
                FilterSettings st;
                st.model = model;
                st.style = style;
                auto randomise = [&] ()
                {
                    st.cutoffHz = 20.0f * std::pow (1000.0f, r.uni());
                    st.cutoffHzR = run % 2 == 0 ? -1.0f : 20.0f * std::pow (1000.0f, r.uni());
                    st.resonance = run == 5 ? 1.0f : r.uni();
                    st.drive = run == 5 ? 1.0f : r.uni();
                    st.var = r.uni();
                    st.blend = r.uni();
                    st.mix = r.uni() < 0.6f ? 1.0f : r.uni();
                    st.clean = run == 3;
                    st.fast = run != 4; // tier low (hold 4) except one run
                };
                randomise();
                for (int blk = 0; blk < blocks; ++blk)
                {
                    if (blk % 29 == 0) randomise();
                    else
                    {
                        st.cutoffHz = std::min (20000.0f, std::max (20.0f, st.cutoffHz * (1.0f + 0.2f * r.bi())));
                        if (st.cutoffHzR > 0.0f) st.cutoffHzR = std::min (20000.0f, std::max (20.0f, st.cutoffHzR * (1.0f + 0.2f * r.bi())));
                    }
                    FilterSettings sa = st, sb = st;
                    sa.scalarOnly = false;
                    sb.scalarOnly = true;
                    a.set (sa);
                    b.set (sb);
                    const int n = blk % 7 == 3 ? 13 : 32;
                    float inL[32], inR[32], oa[2][32], ob[2][32];
                    const int kind = blk % 7;
                    for (int i = 0; i < n; ++i)
                        for (float* x : { &inL[i], &inR[i] })
                        {
                            float v = r.bi();
                            if (kind == 1) v *= 60.0f; else if (kind == 2) v = 0.0f; else if (kind == 3) { uint32_t u = r.next() & 0x807fffffu; std::memcpy (&v, &u, 4); } else if (kind == 4) v *= 1e-19f;
                            const uint32_t d = r.next() % 2000u;
                            if (d == 0) v = std::numeric_limits<float>::quiet_NaN(); else if (d == 1) v = INFINITY;
                            *x = v;
                        }
                    a.process (inL, inR, oa[0], oa[1], n, 0);
                    b.process (inL, inR, ob[0], ob[1], n, 0);
                    bool silent = true;
                    for (int i = 0; i < n; ++i) silent = silent && ob[0][i] == 0.0f && ob[1][i] == 0.0f;
                    runaways += silent && kind != 2 ? 1 : 0;
                    bad += (std::memcmp (oa[0], ob[0], static_cast<size_t> (n) * 4) != 0 || std::memcmp (oa[1], ob[1], static_cast<size_t> (n) * 4) != 0) ? 1 : 0;
                    total += 2 * n;
                }
            }
    std::printf ("    %lld samples: %lld blocks differ (%lld silent blocks with input: runaways or mix 0)\n", total, bad, runaways);
    CHECK_EQ (bad, 0);
    CHECK (runaways > 0);
}
