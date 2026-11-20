// SynthyBoy - own engine tests: the aliasing meter (docs/plans/distortion-antialiasing.md task A1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "alias_measure.h"
#include "test_runner.h"

namespace
{
constexpr size_t kN = 65536;
constexpr double kFs = 44100.0;
constexpr size_t kBin = 1487;

// A sum of coherent tones: (bin, amplitude) pairs, computed in double, stored as float.
std::vector<float> tones (const std::vector<std::pair<size_t, double>>& t)
{
    std::vector<float> x (kN);
    for (size_t i = 0; i < kN; ++i)
    {
        double s = 0.0;
        for (const auto& b : t)
            s += b.second * std::sin (6.283185307179586 * static_cast<double> ((b.first * i) % kN) / static_cast<double> (kN) + 0.3);
        x[i] = static_cast<float> (s);
    }
    return x;
}

double f0 ()
{
    return static_cast<double> (kBin) * kFs / static_cast<double> (kN);
}

std::vector<std::pair<size_t, double>> harmonics ()
{
    std::vector<std::pair<size_t, double>> t;
    for (size_t h = 1; h <= 5; ++h)
        t.emplace_back (h * kBin, 1.0 / static_cast<double> (h));
    return t;
}
} // namespace

TEST (own_alias_meter_reads_a_tone_between_the_harmonics)
{
    // harmonics 1 .. 5 of amplitude 1 / h plus one tone at bin 3 * 1487 + 300 of amplitude 1.2098e-3: sum 1 / h^2 = 1.463611, tone power ratio 1e-6
    auto t = harmonics();
    t.emplace_back (3 * kBin + 300, 1.2098e-3);
    const aliasm::Result r = aliasm::measure (tones (t).data(), kN, f0(), kFs);
    std::printf ("    alias %.3f dB (expected -60.00), harm %.3f dB\n", r.aliasDb, r.harmDb);
    CHECK_NEAR (r.aliasDb, -60.0, 0.2);
}

TEST (own_alias_meter_harmonics_alone_read_below_minus_85_db)
{
    const aliasm::Result r = aliasm::measure (tones (harmonics()).data(), kN, f0(), kFs);
    std::printf ("    alias %.3f dB\n", r.aliasDb);
    CHECK (r.aliasDb < -85.0);
}

TEST (own_alias_meter_ignores_a_tone_next_to_a_harmonic_and_reads_one_further_away)
{
    // The periodic 4-term window has a spectrum of 7 bins (offsets -3 .. +3), so a tone 2 bins from the harmonic is entirely inside the exclusion of +-5 bins.
    // (A tone 4 bins away would leak 2 and 3 bins further, outside it: the plan's "+ 4 bins" cannot read below -85 dB; + 2 is used.)
    auto closeT = std::vector<std::pair<size_t, double>> { { kBin, 1.0 }, { kBin + 2, 1.0e-3 } };
    const aliasm::Result rn = aliasm::measure (tones (closeT).data(), kN, f0(), kFs);
    std::printf ("    tone at + 2 bins: alias %.3f dB\n", rn.aliasDb);
    CHECK (rn.aliasDb < -85.0);

    // The same tone 10 bins away: a clear tone of amplitude 1e-3 against 1.0 = -60 dB.
    auto distantT = std::vector<std::pair<size_t, double>> { { kBin, 1.0 }, { kBin + 10, 1.0e-3 } };
    const aliasm::Result rf = aliasm::measure (tones (distantT).data(), kN, f0(), kFs);
    std::printf ("    tone at + 10 bins: alias %.3f dB (expected -60.00)\n", rf.aliasDb);
    CHECK_NEAR (rf.aliasDb, -60.0, 0.3);
}

TEST (own_alias_meter_reads_a_unit_sine_as_0_db_harmonic_level)
{
    const aliasm::Result r = aliasm::measure (tones ({ { kBin, 1.0 } }).data(), kN, f0(), kFs);
    std::printf ("    unit sine: harm %.4f dB\n", r.harmDb);
    CHECK_NEAR (r.harmDb, 0.0, 0.05);
    const aliasm::Result h = aliasm::measure (tones ({ { kBin, 0.5 } }).data(), kN, f0(), kFs);
    CHECK_NEAR (h.harmDb, -6.0206, 0.05);
}
