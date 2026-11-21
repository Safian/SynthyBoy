// SynthyBoy - own engine tests: which styles of the classic filter models (0 .. 7) are really different (docs/engine-spec/05-filters.md section 13). The style slots above the
// count of filterModelStyleCount () repeat another style of the model: the same samples, bit for bit; the styles below it differ.
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

// ---- The delay lines of the three families (combs, diffuser stages, reverb combs) share one arena (docs/plans/memory-footprint.md, task T2) ----
namespace synthyboy::filters
{
// defined in core/filters-serum/src/filter.cpp: the heap bytes and blocks the delay lines of a filter own
size_t filterDelayBytes (const Filter& f) noexcept;
size_t filterDelayAllocs (const Filter& f) noexcept;
} // namespace synthyboy::filters

namespace
{
using synthyboy::filters::Filter;
using synthyboy::filters::FilterParams;
using synthyboy::filters::FilterType;

constexpr int kArenaBlock = 4096;

FilterParams arenaParams()
{
    FilterParams p;
    p.cutoff = 800.0f;
    p.resonance = 0.6f;
    p.drive = 0.2f;
    p.var = 0.5f;
    p.blend = 0.3f;
    p.mix = 1.0f;
    return p;
}

// a saw plus noise burst, deterministic
std::vector<float> arenaBurst()
{
    std::vector<float> x (static_cast<size_t> (kArenaBlock));
    sbtest::Rng rng (0xa12e4aull);
    float phase = 0.0f;
    for (auto& v : x)
    {
        v = (2.0f * phase - 1.0f) * 0.4f + static_cast<float> (rng.uniform() * 2.0 - 1.0) * 0.1f;
        phase += 110.0f / 44100.0f;
        if (phase >= 1.0f)
            phase -= 1.0f;
    }
    return x;
}

std::vector<float> arenaRun (Filter& f, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    f.process (in.data(), out.data(), static_cast<int> (in.size()));
    return out;
}

uint64_t arenaHash (const std::vector<float>& v)
{
    uint64_t h = 1469598103934665603ull;
    for (const float x : v)
    {
        uint32_t bits;
        std::memcpy (&bits, &x, sizeof (bits));
        for (int b = 0; b < 4; ++b)
        {
            h ^= (bits >> (8 * b)) & 0xffu;
            h *= 1099511628211ull;
        }
    }
    return h;
}

void arenaPrepare (Filter& f, double sr, FilterType type)
{
    f.prepare (sr);
    f.setType (static_cast<int> (type));
    f.setParams (arenaParams());
    f.reset();
}
} // namespace

TEST (own_filter_delay_arena_memory_and_checksum)
{
    const FilterType types[] = { FilterType::Comb2, FilterType::CmbL, FilterType::Allpasses, FilterType::Diffusor, FilterType::Combs, FilterType::Reverb };
    const std::vector<float> burst = arenaBurst();
    uint64_t all = 1469598103934665603ull;
    for (const FilterType t : types)
    {
        Filter f;
        arenaPrepare (f, 44100.0, t);
        // before: 66,428 bytes in 16 blocks; after: 30,388 bytes in 1 block
        CHECK (synthyboy::filters::filterDelayBytes (f) <= 32768u);
        CHECK_EQ (synthyboy::filters::filterDelayAllocs (f), static_cast<size_t> (1));
        const std::vector<float> out = arenaRun (f, burst);
        const uint64_t h = arenaHash (out);
        std::printf ("    FILTER-ARENA-CHECKSUM type=%d %016llx\n", static_cast<int> (t), static_cast<unsigned long long> (h));
        all = (all ^ h) * 1099511628211ull;
        float peak = 0.0f;
        for (const float x : out)
            peak = std::max (peak, std::fabs (x));
        CHECK (peak > 0.0f);
    }
    std::printf ("    FILTER-ARENA-CHECKSUM all %016llx\n", static_cast<unsigned long long> (all));

    for (const double sr : { 44100.0, 96000.0 })
    {
        Filter f;
        arenaPrepare (f, sr, FilterType::Reverb);
        std::printf ("    filter delay memory at %.0f Hz: %zu bytes in %zu block(s)\n", sr, synthyboy::filters::filterDelayBytes (f), synthyboy::filters::filterDelayAllocs (f));
        CHECK_EQ (synthyboy::filters::filterDelayAllocs (f), static_cast<size_t> (1));
    }
}

// Switching the family of a running filter leaves nothing of the earlier family in the delay lines: on silence the filter says what a fresh one says.
TEST (own_filter_delay_arena_family_switch_leaves_no_residue)
{
    const std::vector<float> burst = arenaBurst();
    const std::vector<float> silence (static_cast<size_t> (kArenaBlock), 0.0f);

    Filter f;
    arenaPrepare (f, 44100.0, FilterType::Reverb);
    arenaRun (f, burst);

    const FilterType chain[] = { FilterType::Diffusor, FilterType::Comb2, FilterType::Combs };
    for (const FilterType t : chain)
    {
        f.setType (static_cast<int> (t));
        f.setParams (arenaParams());
        Filter fresh;
        arenaPrepare (fresh, 44100.0, t);
        CHECK (arenaRun (f, silence) == arenaRun (fresh, silence));
        arenaRun (f, burst);   // dirty the lines of this family before the next switch
    }
}
