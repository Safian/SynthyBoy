// SynthyBoy - own engine tests: the Comb filter model (6) resonates at its cutoff. With keytrack 100 the cutoff is the pitch of the note, and the comb used to sound 1.1 %
// (about 19 cents) flat because the phase delay of its loop filter was not taken off the delay line. The ring of an impulse is measured with a fine scan of its spectrum.
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
/** Frequency of the strongest spectral peak of the impulse response of the Comb (resonance 100 %, comb offset 42) with the cutoff f0, searched within +-6 % of f0 (Hann window,
    Goertzel at steps of 0.02 %, parabolic refinement). */
double combPeakHz (double sr, double f0)
{
    ClassicFilter f;
    f.prepare (sr);
    ClassicParams p;
    p.model = 6;
    p.style = 0;
    p.cutoffHz = static_cast<float> (f0);
    p.res = 1.0f;
    p.combOffset = 42.0f;
    p.blendRaw = 1.0f;
    const int block = 32, total = 32768;
    std::vector<float> in (block, 0.0f), out (static_cast<size_t> (total));
    for (int pos = 0; pos < total; pos += block)
    {
        std::fill (in.begin(), in.end(), 0.0f);
        if (pos == 0)
            in[0] = 0.5f;
        f.process (in.data(), out.data() + pos, block, p, p, 0);
    }
    std::vector<double> x (static_cast<size_t> (total));
    for (int i = 0; i < total; ++i)
        x[static_cast<size_t> (i)] = static_cast<double> (out[static_cast<size_t> (i)]) * (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / total));
    auto power = [&] (double hz)
    {
        const double w = 2.0 * 3.14159265358979323846 * hz / sr, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const double s0 = x[static_cast<size_t> (i)] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - c * s1 * s2;
    };
    const double step = f0 * 0.0002;
    double bestHz = f0, best = -1.0, pm = 0.0, pp = 0.0;
    for (double hz = f0 * 0.94; hz <= f0 * 1.06; hz += step)
    {
        const double pw = power (hz);
        if (pw > best)
        {
            best = pw;
            bestHz = hz;
        }
    }
    pm = power (bestHz - step);
    pp = power (bestHz + step);
    const double den = pm - 2.0 * best + pp;
    if (den < 0.0)
        bestHz += step * 0.5 * (pm - pp) / den;
    return bestHz;
}
} // namespace

TEST (own_comb_resonates_at_its_cutoff)
{
    for (double sr : { 44100.0, 48000.0 })
        for (int note : { 24, 36, 48, 60, 72, 84 }) // C1 .. C6
        {
            const double f0 = 8.175798915643707 * std::pow (2.0, note / 12.0);
            const double hz = combPeakHz (sr, f0);
            const double cents = 1200.0 * std::log2 (hz / f0);
            std::printf ("    comb sr %.0f note %d: cutoff %.2f Hz rings at %.2f Hz (%+.2f cents)\n", sr, note, f0, hz, cents);
            CHECK (std::fabs (cents) < 2.0); // it was -19 cents
        }
}
