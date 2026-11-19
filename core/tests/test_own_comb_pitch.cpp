// SynthyBoy - own engine tests: the Comb filter model (6) resonates at its cutoff. With keytrack 100 the cutoff is the pitch of the note, and the comb used to sound 1.1 %
// (about 19 cents) flat because the phase delay of its loop filter was not taken off the delay line. The ring of an impulse is measured with a fine scan of its spectrum.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <vector>

#include "voice/classic_filters.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy::own;

namespace
{
/** Frequency of the strongest spectral peak of the impulse response of the Comb (resonance 100 %, comb offset 42) with the cutoff f0, searched within +-6 % of f0 (Hann window,
    Goertzel at steps of 0.02 %, parabolic refinement). */
double combPeakHz (double sr, double f0, int style = 0, float combOffset = 42.0f, float blendRaw = 1.0f, float res = 1.0f)
{
    ClassicFilter f;
    f.prepare (sr);
    ClassicParams p;
    p.model = 6;
    p.style = style;
    p.cutoffHz = static_cast<float> (f0);
    p.res = res;
    p.combOffset = combOffset;
    p.blendRaw = blendRaw;
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
    const double step = f0 * (style == 0 ? 0.0002 : 0.0003);
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

// the five other styles of the Comb (document 05, 8): comb / positive flange / negative flange with the blend filter (1, 2 and 0 is the plain comb) and with the band filter (3, 4, 5)
TEST (own_comb_styles_resonate_at_their_cutoff)
{
    for (int style = 1; style <= 5; ++style)
        for (int note : { 24, 36, 48, 60, 72, 84 })
        {
            const double f0 = 8.175798915643707 * std::pow (2.0, note / 12.0);
            // the blend styles at the default comb offset (42 semitones, blend 0: low pass); the band styles centred on the pitch (offset 0, half width 2 octaves)
            const bool band = style >= 3;
            const double hz = combPeakHz (48000.0, f0, style, band ? 0.0f : 42.0f, band ? 1.0f : 0.0f);
            const double cents = 1200.0 * std::log2 (hz / f0);
            std::printf ("    comb style %d note %d: cutoff %.2f Hz rings at %.2f Hz (%+.2f cents)\n", style, note, f0, hz, cents);
            CHECK (std::fabs (cents) < 2.0);
        }
}

TEST (own_comb_styles_are_bounded_and_stable_at_full_resonance)
{
    for (int style = 0; style <= 5; ++style)
        for (float res : { 0.5f, 1.0f })
        {
            ClassicFilter f;
            f.prepare (48000.0);
            ClassicParams p;
            p.model = 6;
            p.style = style;
            p.cutoffHz = 330.0f;
            p.res = res;
            p.blendRaw = 1.0f;
            sbtest::Rng rng (0xc0b5ull);
            float peak = 0.0f;
            bool finite = true;
            std::vector<float> in (32), out (32);
            for (int pos = 0; pos < 96000; pos += 32)
            {
                for (auto& x : in)
                    x = pos < 9600 ? static_cast<float> (rng.uniform() * 2.0 - 1.0) * 0.5f : 0.0f; // a 200 ms noise burst, then the ring
                f.process (in.data(), out.data(), 32, p, p, 0);
                for (float y : out)
                {
                    finite = finite && std::isfinite (y);
                    peak = std::max (peak, std::fabs (y));
                }
            }
            std::printf ("    comb style %d resonance %.1f: peak %.3f\n", style, static_cast<double> (res), static_cast<double> (peak));
            CHECK (finite);
            CHECK (peak < 4.0f);
        }
}

// The level law of the plain comb: a sine at the resonance comes out at scale / (1 - fb |H|) = (1 - 0.5 fb^2) / (1 - 0.98 * 0.996) = about +26.7 dB (fb is 0.98 at resonance 100 %).
// That is the whole gain of the model: a short noise burst puts only the part of its energy that falls into the narrow peaks (a few Hz wide) into the ring, so a pluck made
// of a burst is far below the burst's own level (the Karplus Harp make-up) without any defect in the normalisation.
TEST (own_comb_level_law_at_the_resonance)
{
    const double sr = 48000.0, f0 = 261.6256;
    ClassicFilter f;
    f.prepare (sr);
    ClassicParams p;
    p.model = 6;
    p.style = 0;
    p.cutoffHz = static_cast<float> (f0);
    p.res = 1.0f;
    p.combOffset = 42.0f;
    std::vector<float> in (32), out (32);
    double acc = 0.0;
    int counted = 0;
    for (int pos = 0; pos < 96000; pos += 32)
    {
        for (int i = 0; i < 32; ++i)
            in[static_cast<size_t> (i)] = static_cast<float> (0.01 * std::sin (2.0 * 3.14159265358979323846 * f0 * static_cast<double> (pos + i) / sr));
        f.process (in.data(), out.data(), 32, p, p, 0);
        if (pos >= 72000)
            for (float y : out)
            {
                acc += static_cast<double> (y) * y;
                ++counted;
            }
    }
    const double gainDb = 20.0 * std::log10 (std::sqrt (2.0 * acc / counted) / 0.01);
    std::printf("    comb gain at the resonance: %.2f dB (law: +26.7)\n", gainDb);
    CHECK (gainDb > 24.0 && gainDb < 29.0);
}
