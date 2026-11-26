// SynthyBoy - core tests: the Add Harmonics warp of the spectral oscillator (core/spectral/src/spectral.cpp, applyWarp) with the multiplications by 0.5 and 0.25 for the second and the fourth
// harmonic and the product a * m[k] formed once per bin gives the very bits of the loop with the division of every harmonic. shapeFrame (through computeMagnitudeSpectrum, which runs the
// same code as the voice) is run on random analyses, amounts and variations with the old loop (the test hook) and the new one, and the magnitudes, frequencies and phases of the output are
// compared bit for bit. Compiled without fused multiply-add like the engine's other bit identity tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/spectral/spectral.h"
#include "test_runner.h"

namespace synthyboy
{
namespace spectral
{
namespace testhooks
{
extern bool referenceAddHarmonics; // core/spectral/src/spectral.cpp
}
} // namespace spectral
} // namespace synthyboy

using namespace synthyboy::spectral;
using synthyboy::sampleosc::SampleData;

namespace
{
constexpr double kSr = 48000.0;
struct TestRng
{
    uint64_t s;
    explicit TestRng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t> (s >> 16); }
    float uni() { return static_cast<float> (next() & 0xFFFFFF) / 16777216.0f; }
    float bi() { return uni() * 2.0f - 1.0f; }
};

struct Tone
{
    SampleData data;
    Analysis analysis;
    explicit Tone (const std::vector<float>& samples)
    {
        data.loadFromFrames (samples.data(), nullptr, static_cast<int64_t> (samples.size()), kSr);
        analysis.build (data.view());
    }
};

std::vector<float> noisyChord (TestRng& r, double seconds)
{
    std::vector<float> v (static_cast<size_t> (seconds * kSr), 0.0f);
    const int partials = 4 + static_cast<int> (r.next() % 30u);
    for (int h = 1; h <= partials; ++h)
    {
        const double hz = 80.0 + 60.0 * (r.uni() + static_cast<double> (r.next() % 8u)) * h * (r.uni() < 0.3f ? 1.0 : 0.37);
        const float amp = (r.uni() < 0.25f ? 0.0f : 0.3f) / static_cast<float> (h) * (0.5f + r.uni());
        const double ph = 6.283185307179586 * static_cast<double> (r.uni());
        for (size_t i = 0; i < v.size(); ++i)
            v[i] += amp * static_cast<float> (std::sin (6.283185307179586 * hz * static_cast<double> (i) / kSr + ph));
    }
    for (float& x : v)
        x += 0.002f * r.bi(); // a noise floor: the small magnitudes (and subnormal quotients in the weak bins) are part of the test
    return v;
}
} // namespace

TEST (the_add_harmonics_warp_with_multiplications_equals_the_loop_with_divisions)
{
    TestRng r (7);
    static Scratch a, b, c; // c: the run without the warp (its own scratch: the fields a warp does not write keep the values of the last run that did)
    long long compared = 0, bad = 0, changed = 0;
    for (int tone = 0; tone < 6; ++tone)
    {
        const Tone t (noisyChord (r, 1.0 + 0.5 * tone));
        for (int i = 0; i < 160; ++i)
        {
            SpectralParams p;
            p.scratch = &a;
            p.hostRate = r.uni() < 0.5f ? 48000.0 : 44100.0;
            p.pitchRatio = r.uni() < 0.4f ? 1.0f : 0.5f + 1.5f * r.uni();
            p.freqShiftHz = r.uni() < 0.7f ? 0.0f : 200.0f * r.bi();
            const int slot = static_cast<int> (r.next() % 2u);
            p.warpType[slot] = static_cast<int> (Warp::addHarmonics);
            p.warpAmount[slot] = r.uni() < 0.1f ? (r.uni() < 0.5f ? 0.0f : 1.0f) : r.uni();
            p.warpVar[slot] = r.uni() < 0.1f ? (r.uni() < 0.5f ? 0.0f : 1.0f) : r.uni(); // n = 1 + int (v * 3.99): 1 .. 4 harmonics added
            if (r.uni() < 0.3f)
            {
                p.warpType[1 - slot] = static_cast<int> (r.uni() < 0.5f ? Warp::comb : Warp::addSubharmonics);
                p.warpAmount[1 - slot] = r.uni();
                p.warpVar[1 - slot] = r.uni();
            }
            const double frame = static_cast<double> (r.uni()) * static_cast<double> (t.analysis.numFrames() - 1);
            float oa[kBins], ob[kBins];
            testhooks::referenceAddHarmonics = true;
            computeMagnitudeSpectrum (t.analysis, p, frame, ob, b);
            testhooks::referenceAddHarmonics = false;
            p.scratch = &a;
            computeMagnitudeSpectrum (t.analysis, p, frame, oa, a);
            ++compared;
            const bool same = std::memcmp (oa, ob, sizeof oa) == 0 && std::memcmp (a.om, b.om, sizeof a.om) == 0 && std::memcmp (a.of, b.of, sizeof a.of) == 0
                              && std::memcmp (a.oth, b.oth, sizeof a.oth) == 0 && std::memcmp (a.tm, b.tm, sizeof a.tm) == 0 && std::memcmp (a.tf, b.tf, sizeof a.tf) == 0;
            bad += same ? 0 : 1;
            // the warp must do something: the output differs from the one without it
            SpectralParams q = p;
            q.warpType[slot] = -1;
            q.warpType[1 - slot] = p.warpType[1 - slot];
            float oq[kBins];
            computeMagnitudeSpectrum (t.analysis, q, frame, oq, c);
            changed += std::memcmp (oa, oq, sizeof oa) != 0 ? 1 : 0;
        }
    }
    testhooks::referenceAddHarmonics = false;
    std::printf ("    %lld frames shaped with the warp: %lld differ in the magnitudes, frequencies or phases from the loop with divisions; the warp changed the magnitudes in %lld\n", compared, bad, changed);
    CHECK (changed > compared / 2);
    CHECK_EQ (bad, 0);
}

TEST (x_divided_by_2_and_4_is_x_times_0_5_and_0_25_for_every_float_class)
{
    // the identity the warp relies on, on random bit patterns (normal, subnormal, zero, infinity, NaN as a class): the quotient and the product are the same float
    TestRng r (9);
    long long bad = 0, n = 0;
    for (long long i = 0; i < 40000000; ++i)
    {
        uint32_t bits = (r.next() << 16) ^ r.next();
        if (i % 4 == 0)
            bits &= 0x807fffffu; // subnormals and zeros
        float x;
        std::memcpy (&x, &bits, 4);
        volatile float xv = x;
        const float v2 = xv / 2.0f, m2 = xv * 0.5f, v4 = xv / 4.0f, m4 = xv * 0.25f;
        const bool nan2 = std::isnan (v2) && std::isnan (m2), nan4 = std::isnan (v4) && std::isnan (m4);
        ++n;
        if (!(nan2 || std::memcmp (&v2, &m2, 4) == 0) || !(nan4 || std::memcmp (&v4, &m4, 4) == 0))
            ++bad;
    }
    std::printf ("    %lld random bit patterns: %lld where x / 2 or x / 4 differs from the product\n", n, bad);
    CHECK_EQ (bad, 0);
}
