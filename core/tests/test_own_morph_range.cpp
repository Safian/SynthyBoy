// SynthyBoy - own engine tests: the level of the spectral morph types over the amount range (buildMorphCycle on a saw table and on a sparse "bell" table).
// Smear at 100 % and High Pass from 60 % gave silence, Random Amplitudes jumped between -73 dB and +6 dB from one step of the amount to the next.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <vector>

#include "osc/morph.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
constexpr int kCyc = Wavetable::kCycle;

/** A one frame table with the sine coefficients given (index = harmonic). */
Wavetable makeTable (const std::vector<std::pair<int, float>>& partials)
{
    std::vector<std::vector<float>> cc (1, std::vector<float> (kCyc / 2 + 1, 0.0f)), ss (1, std::vector<float> (kCyc / 2 + 1, 0.0f));
    for (const auto& p : partials)
        ss[0][static_cast<size_t> (p.first)] = p.second;
    Wavetable wt;
    wt.build (cc, ss, false);
    return wt;
}

Wavetable sawTable()
{
    std::vector<std::pair<int, float>> p;
    for (int h = 1; h <= 1023; ++h)
        p.push_back ({ h, 1.0f / static_cast<float> (h) });
    return makeTable (p);
}

/** RMS of the cycle that buildMorphCycle makes. */
double cycleRms (const Wavetable& wt, int type, float m, int K)
{
    static MorphScratch scratch;
    static std::vector<float> buf (static_cast<size_t> (kCyc) + 4);
    buildMorphCycle (wt, 0.0f, false, type, m, K, 0.005f, 44100.0, buf.data() + 1, scratch);
    double acc = 0.0;
    for (int i = 0; i < kCyc; ++i)
        acc += static_cast<double> (buf[static_cast<size_t> (i) + 1]) * buf[static_cast<size_t> (i) + 1];
    return std::sqrt (acc / kCyc);
}

double relDb (const Wavetable& wt, int type, float m, int K)
{
    return 20.0 * std::log10 ((cycleRms (wt, type, m, K) + 1.0e-12) / cycleRms (wt, kMorphNone, 0.0f, K));
}
} // namespace

TEST (own_morph_smear_never_silent)
{
    const Wavetable saw = sawTable();
    for (int K : { 40, 84, 200 })
        for (int i = 0; i <= 20; ++i)
        {
            const float m = static_cast<float> (i) / 20.0f;
            const double db = relDb (saw, kMorphSmear, m, K);
            if (i % 5 == 0)
                std::printf ("    smear K %d m %.2f: %.1f dB against the dry table\n", K, static_cast<double> (m), db);
            CHECK (db > -50.0); // the end of the range (100 %) was -inf dB
        }
}

TEST (own_morph_high_pass_keeps_the_top_harmonic)
{
    const Wavetable saw = sawTable();
    for (int K : { 20, 42, 84, 200 })
    {
        double prevOk = 0.0;
        for (int i = 0; i <= 20; ++i)
        {
            const float m = static_cast<float> (i) / 20.0f;
            const double db = relDb (saw, kMorphHighPass, m, K);
            if (i % 5 == 0)
                std::printf ("    high pass K %d m %.2f: %.1f dB against the dry table\n", K, static_cast<double> (m), db);
            CHECK (db > -60.0); // from 60 % (cutoff 64 harmonics) up the notes above about C5 gave -inf dB
            CHECK (db <= prevOk + 0.1 || i == 0); // a higher cutoff never makes the output louder
            prevOk = db;
        }
        // the cutoff is inside the band: unchanged (25 % = harmonic 5.66: the first four are gone, as the vector of O14 says)
        CHECK (relDb (saw, kMorphHighPass, 0.25f, K) < -0.5);
    }
}

TEST (own_morph_random_amplitudes_keeps_its_level)
{
    const Wavetable saw = sawTable();
    const Wavetable bell = makeTable ({ { 1, 1.0f }, { 3, 0.7f }, { 5, 0.45f }, { 8, 0.3f } }); // few partials: the draw used to delete all of them
    for (const Wavetable* wt : { &saw, &bell })
        for (int K : { 64, 200 })
        {
            double worst = 0.0;
            for (int i = 0; i <= 40; ++i)
            {
                const float m = static_cast<float> (i) / 40.0f;
                const double db = relDb (*wt, kMorphRandomAmplitudes, m, K);
                worst = std::max (worst, std::fabs (db));
                CHECK (std::fabs (db) < 1.0); // energy kept at the dry value: no silence, no jump at any amount
            }
            std::printf ("    random amplitudes %s K %d: largest level change %.2f dB over 0 .. 100 %%\n", wt == &saw ? "saw" : "bell", K, worst);
        }
    // the amount 0 is the plain table
    CHECK (std::fabs (relDb (saw, kMorphRandomAmplitudes, 0.0f, 64)) < 1.0e-4);
    // deterministic: the same arguments give the same cycle
    CHECK (cycleRms (bell, kMorphRandomAmplitudes, 0.3f, 64) == cycleRms (bell, kMorphRandomAmplitudes, 0.3f, 64));
}
