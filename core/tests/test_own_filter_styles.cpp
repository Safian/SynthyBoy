// SynthyBoy - own engine tests: which styles of the classic filter models (0 .. 7) are really different (docs/engine-spec/05-filters.md section 13). The style slots above the
// count of filterModelStyleCount () repeat another style of the model: the same samples, bit for bit; the styles below it differ.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <vector>

#include "synthyboy/filters/filter_types.h"
#include "voice/classic_filters.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy::own;

namespace
{
std::vector<float> run (int model, int style, float blendRaw)
{
    ClassicFilter f;
    f.prepare (48000.0);
    ClassicParams p;
    p.model = model;
    p.style = style;
    p.cutoffHz = 1000.0f;
    p.res = 0.5f;
    p.driveDb = 0.0f;
    p.blendRaw = blendRaw;
    sbtest::Rng rng (0x57715eull);
    const int total = 4096, block = 32;
    std::vector<float> in (static_cast<size_t> (block)), out (static_cast<size_t> (total));
    for (int pos = 0; pos < total; pos += block)
    {
        for (auto& x : in)
            x = static_cast<float> (rng.uniform() * 2.0 - 1.0) * 0.5f;
        f.process (in.data(), out.data() + pos, block, p, p, 0);
    }
    return out;
}

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a == b;
}
} // namespace

TEST (own_filter_styles_above_the_count_repeat_another_style)
{
    using synthyboy::filters::filterModelStyleCount;
    const float blends[2] = { 0.0f, 1.3f };
    for (float bl : blends)
    {
        // Diode: 12 dB (0) and 24 dB (everything else)
        for (int s = 2; s < 12; ++s)
            CHECK (same (run (4, 1, bl), run (4, s, bl)));
        CHECK (!same (run (4, 0, bl), run (4, 1, bl)));
        // Formant: A, B, C (vocal tract) and then C again
        for (int s = 3; s < 12; ++s)
            CHECK (same (run (5, 2, bl), run (5, s, bl)));
        CHECK (!same (run (5, 0, bl), run (5, 1, bl)));
        CHECK (!same (run (5, 1, bl), run (5, 2, bl)));
        // Comb: six styles (comb / positive flange / negative flange, each with the blend filter or the band filter), a slot above 5 is style 5
        for (int s = 6; s < 12; ++s)
            CHECK (same (run (6, 5, bl), run (6, s, bl)));
        for (int s = 1; s < 6; ++s)
            CHECK (!same (run (6, 0, bl), run (6, s, bl)));
        CHECK (!same (run (6, 1, bl), run (6, 2, bl)));
        CHECK (!same (run (6, 1, bl), run (6, 4, bl)));
        // Phaser: 0 and 1 (inverted); the rest is 0
        for (int s = 2; s < 12; ++s)
            CHECK (same (run (7, 0, bl), run (7, s, bl)));
        CHECK (!same (run (7, 0, bl), run (7, 1, bl)));
        // Analog, Digital: the style slots 6 .. 11 are the 24 dB structure with the plain weights of style B
        for (int model : { 0, 3 })
        {
            for (int s = 6; s < 12; ++s)
                CHECK (same (run (model, 1, bl), run (model, s, bl)));
            for (int s = 1; s < 5; ++s)
                CHECK (!same (run (model, 0, bl), run (model, s, bl)));
        }
    }
    // the counts the UI reads
    CHECK_EQ (filterModelStyleCount (4), 2);
    CHECK_EQ (filterModelStyleCount (5), 3);
    CHECK_EQ (filterModelStyleCount (6), 6);
    CHECK_EQ (filterModelStyleCount (7), 2);

    using synthyboy::filters::filterModelStyleName;
    using synthyboy::filters::filterModelStyleNames;
    CHECK_EQ (std::string (filterModelStyleName (4, 0)), "Style A (12 dB)");
    CHECK_EQ (std::string (filterModelStyleName (4, 1)), "Style B (24 dB)");
    CHECK_EQ (std::string (filterModelStyleName (5, 0)), "A-O-I-E");
    CHECK_EQ (std::string (filterModelStyleName (5, 1)), "A-I-U-O");
    CHECK_EQ (std::string (filterModelStyleName (5, 2)), "Vocal Tract");
    CHECK_EQ (std::string (filterModelStyleName (6, 0)), "Comb");
    CHECK_EQ (std::string (filterModelStyleName (7, 0)), "Positive Feedback");
    CHECK_EQ (std::string (filterModelStyleName (7, 1)), "Negative Feedback");
    CHECK (filterModelStyleNames (6) != nullptr);
    CHECK_EQ (std::string (filterModelStyleNames (6)[0]), "Comb");
}
