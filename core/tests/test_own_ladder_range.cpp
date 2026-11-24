// SynthyBoy - own engine tests: the Ladder model (2) keeps its response over the whole cutoff range. A saw at C4 through the 12 dB Ladder at resonance 22 %: the RMS
// of the output must not fall away toward the top of the range (it fell from -35 dB at 18 kHz to -68 dB at 20 kHz when the feedback sum of the four stages ignored the
// (1 - G) of each stage), at 44.1 and at 48 kHz, and the filter stays finite and limited.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <vector>

#include "voice/classic_filters.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
/** RMS in dB of a C4 saw (amplitude 0.5) through the Ladder at the given cutoff; false in `finite` if a sample is not finite or beyond 16. */
double ladderRmsDb (double sr, float cutoffHz, float res, bool& finite)
{
    ClassicFilter f;
    f.prepare (sr);
    ClassicParams p;
    p.model = 2;
    p.style = 0;
    p.cutoffHz = cutoffHz;
    p.res = res;
    p.driveDb = 0.0f;
    p.blendRaw = 0.0f; // blend 0: the 12 dB low pass
    const int block = 32, total = static_cast<int> (sr * 0.6), skip = static_cast<int> (sr * 0.2);
    std::vector<float> in (block), out (block);
    double phase = 0.0, acc = 0.0;
    int counted = 0;
    finite = true;
    for (int pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i)
        {
            in[static_cast<size_t> (i)] = static_cast<float> (0.5 * (2.0 * phase - 1.0));
            phase += 261.6256 / sr;
            if (phase >= 1.0)
                phase -= 1.0;
        }
        f.process (in.data(), out.data(), block, p, p, 0);
        for (int i = 0; i < block; ++i)
        {
            const float y = out[static_cast<size_t> (i)];
            if (!std::isfinite (y) || std::fabs (y) > 16.0f)
                finite = false;
            if (pos >= skip)
            {
                acc += static_cast<double> (y) * y;
                ++counted;
            }
        }
    }
    return 10.0 * std::log10 (acc / counted + 1.0e-30);
}
} // namespace

TEST (own_ladder_rms_over_cutoff)
{
    const double rates[2] = { 44100.0, 48000.0 };
    const float cutoffs[] = { 2000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 14000.0f, 16000.0f, 17000.0f, 18000.0f, 19000.0f, 20000.0f };
    for (double sr : rates)
    {
        double prev = -1.0e9, at10k = 0.0;
        for (float c : cutoffs)
        {
            bool finite = true;
            const double db = ladderRmsDb (sr, c, 0.22f, finite);
            std::printf ("    ladder sr %.0f cutoff %.0f Hz: %.2f dB\n", sr, c, db);
            CHECK (finite);
            CHECK (db > prev - 1.5); // monotone: opening the filter never makes the output quieter (1.5 dB for the moving resonance peak)
            prev = db;
            if (c == 10000.0f)
                at10k = db;
            if (c >= 14000.0f)
                CHECK (db > at10k - 1.5);
        }
        bool finite = true;
        const double top = ladderRmsDb (sr, 20000.0f, 0.22f, finite);
        CHECK (top > at10k - 1.5 && top < at10k + 6.0);
    }
}

TEST (own_ladder_stable_at_top_with_resonance)
{
    // high resonance at the top of the range: finite and limited (no runaway), at both rates
    for (double sr : { 44100.0, 48000.0 })
        for (float res : { 0.5f, 0.78f, 1.0f })
        {
            bool finite = true;
            (void) ladderRmsDb (sr, 20000.0f, res, finite);
            CHECK (finite);
        }
}
