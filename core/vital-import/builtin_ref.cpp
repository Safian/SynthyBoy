// SynthyBoy - core: reference shapes of the built-in wavetables and LFO shapes, and the matching of imported content against them.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The formulas are those of core/engine-vital/adapter/vital_tables.cpp (our own code, which cannot be linked here: the importer is
// Vital free). core/vital-import/tests cross-checks the two when the Vital engine is built (SYNTHYBOY_WITH_VITAL).

#include <algorithm>
#include <cmath>
#include <vector>

#include "synthyboy/wavetable_render.h"
#include "vital_import_internal.h"
#include "synthyboy/params.h"

namespace synthyboy
{
namespace vitalimport
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kN = wtrender::kFrameSize;
constexpr int kMaxHarmonic = kN / 2 - 1;
constexpr int kMorphFrames = 64;

struct Spectrum
{
    double a[kMaxHarmonic + 1] = {};
    double b[kMaxHarmonic + 1] = {};
};

void pulseSpectrum (Spectrum& s, double duty)
{
    for (int k = 1; k <= kMaxHarmonic; ++k)
    {
        const double c = 2.0 / (kPi * k);
        s.a[k] += c * std::sin (2.0 * kPi * k * duty);
        s.b[k] += c * (1.0 - std::cos (2.0 * kPi * k * duty));
    }
}

void spectrumOf (int table, double t, Spectrum& s)
{
    switch (table)
    {
        case 0: s.b[1] = 1.0; break;
        case 1: for (int k = 1; k <= kMaxHarmonic; ++k) s.b[k] = -2.0 / (kPi * k); break;
        case 2: for (int k = 1; k <= kMaxHarmonic; k += 2) s.b[k] = 4.0 / (kPi * k); break;
        case 3:
            for (int k = 1; k <= kMaxHarmonic; k += 2)
                s.b[k] = (8.0 / (kPi * kPi)) * ((k / 2) % 2 == 0 ? 1.0 : -1.0) / (static_cast<double> (k) * k);
            break;
        case 4: pulseSpectrum (s, 0.25); break;
        case 13: // Saw Down (GROUPB WP-18): -saw (phase + 180 degrees), b[k] = 2 (-1)^k / (pi k): the waveform of Vital's Init table
            for (int k = 1; k <= kMaxHarmonic; ++k)
                s.b[k] = 2.0 * (k % 2 == 0 ? 1.0 : -1.0) / (kPi * k);
            break;
        case 5:
            for (int k = 1; k <= kMaxHarmonic; ++k)
                s.b[k] = (2.0 / (kPi * k)) * (k % 2 == 1 ? 1.0 + t : 1.0 - t);
            break;
        case 6:
            s.b[1] = 1.0 - t;
            for (int k = 1; k <= kMaxHarmonic; ++k)
                s.b[k] += t * 2.0 / (kPi * k);
            break;
        case 7:
        case 8:
        {
            const double count = 1.0 + 63.0 * t;
            for (int k = 1; k <= 64; ++k)
            {
                if (table == 8 && k % 2 == 0)
                    continue;
                s.b[k] = std::clamp (count - k + 1.0, 0.0, 1.0) / k;
            }
            break;
        }
        case 9:
        {
            const double c1 = 2.0 + 10.0 * t, c2 = 6.0 + 26.0 * t;
            for (int k = 1; k <= kMaxHarmonic; ++k)
            {
                const double g1 = std::exp (-std::pow ((k - c1) / (1.0 + 0.4 * c1), 2.0));
                const double g2 = 0.6 * std::exp (-std::pow ((k - c2) / (1.0 + 0.4 * c2), 2.0));
                s.b[k] = (g1 + g2) / std::sqrt (static_cast<double> (k)) + (k == 1 ? 0.15 : 0.0);
            }
            break;
        }
        case 11: pulseSpectrum (s, 0.05 + 0.45 * t); break;
        case 12:
        {
            static const int partials[] = { 1, 2, 3, 4, 6, 8, 11, 15, 19, 26, 34 };
            for (const int k : partials)
            {
                const double weight = std::exp (-k / (4.0 + 40.0 * t)) + 0.3 * std::exp (-std::pow ((k - 3.0 - 20.0 * t) / 4.0, 2.0));
                s.b[k] += weight * ((k % 3 == 0) ? 0.8 : 1.0);
                s.a[k] += 0.4 * weight * (k % 2 == 0 ? 1.0 : -1.0);
            }
            break;
        }
        default: break;
    }
}

void normalise (float* f)
{
    double mean = 0.0;
    for (int i = 0; i < kN; ++i)
        mean += f[i];
    mean /= kN;
    float peak = 0.0f;
    for (int i = 0; i < kN; ++i)
    {
        f[i] -= static_cast<float> (mean);
        peak = std::max (peak, std::fabs (f[i]));
    }
    const float scale = peak > 1.0e-9f ? 1.0f / peak : 1.0f;
    for (int i = 0; i < kN; ++i)
        f[i] *= scale;
}

constexpr int kDistHarmonics = 64;
constexpr double kDbFloor = -90.0;

void dbProfile (const float* frame, double* out)
{
    std::vector<float> n (frame, frame + kN);
    normalise (n.data()); // SPEC-GAP: the design does not say whether the compared frames are level normalised; they are, so that the
                          // distance measures the spectral shape
    double mag[kDistHarmonics + 1];
    wtrender::harmonicMagnitudes (n.data(), kDistHarmonics, mag);
    for (int k = 1; k <= kDistHarmonics; ++k)
        out[k - 1] = std::max (kDbFloor, 20.0 * std::log10 (mag[k] + 1.0e-12));
}

constexpr double kPositions[5] = { 0.0, 0.25, 0.5, 0.75, 1.0 };

struct BuiltinProfiles
{
    double db[kBuiltinWavetables][5][kDistHarmonics];
    BuiltinProfiles()
    {
        std::vector<float> f (kN);
        for (int t = 0; t < kBuiltinWavetables; ++t)
            for (int p = 0; p < 5; ++p)
            {
                const int frames = builtinWavetableFrames (t);
                const int idx = static_cast<int> (std::lround (kPositions[p] * (frames - 1)));
                builtinWavetableFrame (t, frames > 1 ? static_cast<double> (idx) / (frames - 1) : 0.0, f.data());
                dbProfile (f.data(), db[t][p]);
            }
    }
};

const BuiltinProfiles& profiles()
{
    static const BuiltinProfiles p;
    return p;
}
} // namespace

int builtinWavetableFrames (int table) noexcept { return table <= 4 || table == 13 ? 1 : kMorphFrames; }

void builtinWavetableFrame (int table, double t, float* out)
{
    if (table == 10) // FM sweep: time domain
    {
        const double index = 6.0 * t;
        for (int n = 0; n < kN; ++n)
        {
            const double phase = 2.0 * kPi * n / kN;
            out[n] = static_cast<float> (std::sin (phase + index * std::sin (2.0 * phase)));
        }
    }
    else
    {
        Spectrum s;
        spectrumOf (table, t, s);
        wtrender::synthesizeHarmonics (s.a, s.b, kMaxHarmonic, out);
    }
    normalise (out);
}

void builtinLfoShape (int shape, float* out) noexcept
{
    // SPEC-GAP: shapes 0..4 are Vital's own initial lines whose exact orientation the design leaves to black-box goldens (11, C.2); the
    // choices here: sine 0.5 + 0.5 sin, triangle 0 -> 1 -> 0, saws, square high in the first half. Shapes 5..11 are the lines of
    // core/engine-vital/adapter/vital_tables.cpp (applyLfoShape, our code).
    const auto steps = [] (const float* levels, int n, double x) { return static_cast<float> (levels[std::min (n - 1, static_cast<int> (x * n))]); };
    static const float up[4] = { 0.0f, 1.0f / 3, 2.0f / 3, 1.0f };
    static const float down[4] = { 1.0f, 2.0f / 3, 1.0f / 3, 0.0f };
    static const float rnd[8] = { 0.2f, 0.9f, 0.5f, 1.0f, 0.1f, 0.7f, 0.35f, 0.8f };
    for (int i = 0; i < kLfoRefSamples; ++i)
    {
        const double x = static_cast<double> (i) / kLfoRefSamples;
        double v = 0.0;
        switch (shape)
        {
            case 0: v = 0.5 + 0.5 * std::sin (2.0 * kPi * x); break;
            case 1: v = 1.0 - std::fabs (2.0 * x - 1.0); break;
            case 2: v = x; break;
            case 3: v = 1.0 - x; break;
            case 4: v = x < 0.5 ? 1.0 : 0.0; break;
            case 5: v = x < 0.25 ? 1.0 : 0.0; break;
            case 6: v = steps (up, 4, x); break;
            case 7: v = steps (down, 4, x); break;
            case 8: v = steps (rnd, 8, x); break;
            case 9: v = x * x * x; break;
            case 10: v = (1.0 - x) * (1.0 - x) * (1.0 - x); break;
            default: v = std::sin (kPi * x); break;
        }
        out[i] = static_cast<float> (v);
    }
}

int nearestBuiltinWavetable (const float* frames, int numFrames, double& distance)
{
    double mine[5][kDistHarmonics];
    for (int p = 0; p < 5; ++p)
    {
        const int idx = static_cast<int> (std::lround (kPositions[p] * (numFrames - 1)));
        dbProfile (frames + static_cast<size_t> (idx) * kN, mine[p]);
    }
    const BuiltinProfiles& ref = profiles();
    int best = 0;
    double bestD = 1.0e300;
    for (int t = 0; t < kBuiltinWavetables; ++t)
    {
        double sum = 0.0;
        for (int p = 0; p < 5; ++p)
            for (int k = 0; k < kDistHarmonics; ++k)
                sum += std::fabs (mine[p][k] - ref.db[t][p][k]);
        const double d = sum / (5.0 * kDistHarmonics);
        if (d < bestD) // ties: the lower index
        {
            bestD = d;
            best = t;
        }
    }
    distance = bestD;
    return best;
}

int matchBuiltinSingleCycle (const float* frames, int numFrames, bool allowSawDown)
{
    if (numFrames < 1)
        return -1;
    for (int f = 1; f < numFrames; ++f)
    {
        double m = 0.0;
        for (int i = 0; i < kN; ++i)
            m = std::max (m, static_cast<double> (std::fabs (frames[static_cast<size_t> (f) * kN + static_cast<size_t> (i)] - frames[i])));
        if (m > 1.0e-4)
            return -1;
    }
    constexpr int kCompare = 40; // SPEC-GAP: compared up to harmonic 40: a table sampled naively (half a sample of phase offset, Gibbs ripple) must still match
    std::vector<float> a (frames, frames + kN);
    normalise (a.data());
    std::vector<double> ac (kCompare + 1), as (kCompare + 1), bc (kCompare + 1), bs (kCompare + 1);
    wtrender::analyzeHarmonics (a.data(), kCompare, ac.data(), as.data());
    std::vector<float> ref (kN);
    int best = -1;
    double bestErr = 0.01;
    for (int t = 0; t < (allowSawDown ? 6 : 5); ++t)
    {
        builtinWavetableFrame (t == 5 ? 13 : t, 0.0, ref.data());   // candidate 5 is Saw Down (slot 13)
        wtrender::analyzeHarmonics (ref.data(), kCompare, bc.data(), bs.data());
        // SPEC-GAP: the level is not part of the comparison (a band limited reference has Gibbs overshoot, so a peak normalised naive table
        // would differ by its level alone): both coefficient vectors are scaled to unit energy over the compared harmonics.
        double ea = 0.0, eb = 0.0;
        for (int k = 1; k <= kCompare; ++k)
        {
            ea += ac[static_cast<size_t> (k)] * ac[static_cast<size_t> (k)] + as[static_cast<size_t> (k)] * as[static_cast<size_t> (k)];
            eb += bc[static_cast<size_t> (k)] * bc[static_cast<size_t> (k)] + bs[static_cast<size_t> (k)] * bs[static_cast<size_t> (k)];
        }
        const double ga = ea > 0.0 ? 1.0 / std::sqrt (ea) : 0.0, gb = eb > 0.0 ? 1.0 / std::sqrt (eb) : 0.0;
        double num = 0.0, den = 0.0;
        for (int k = 1; k <= kCompare; ++k)
        {
            const double dc = ac[static_cast<size_t> (k)] * ga - bc[static_cast<size_t> (k)] * gb, ds = as[static_cast<size_t> (k)] * ga - bs[static_cast<size_t> (k)] * gb;
            num += dc * dc + ds * ds;
            den += (bc[static_cast<size_t> (k)] * gb) * (bc[static_cast<size_t> (k)] * gb) + (bs[static_cast<size_t> (k)] * gb) * (bs[static_cast<size_t> (k)] * gb);
        }
        const double err = den > 0.0 ? std::sqrt (num / den) : 1.0;
        if (err <= bestErr)
        {
            bestErr = err;
            best = t == 5 ? kWavetableSawDown : t;
        }
    }
    return best;
}

int matchBuiltinLfoShape (const float* s, int& nearest, double& error)
{
    float ref[kLfoRefSamples];
    nearest = 0;
    error = 1.0e300;
    for (int shape = 0; shape < kBuiltinLfoShapes; ++shape)
    {
        builtinLfoShape (shape, ref);
        double sum = 0.0;
        for (int i = 0; i < kLfoRefSamples; ++i)
            sum += (static_cast<double> (s[i]) - ref[i]) * (static_cast<double> (s[i]) - ref[i]);
        const double e = std::sqrt (sum / kLfoRefSamples); // values span 0..1: the RMS error is relative to full scale
        if (e < error)
        {
            error = e;
            nearest = shape;
        }
    }
    return error <= 0.01 ? nearest : -1;
}
} // namespace vitalimport
} // namespace synthyboy
