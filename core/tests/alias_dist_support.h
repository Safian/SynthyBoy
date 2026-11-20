// SynthyBoy - core tests: shared helpers of the distortion anti-aliasing tests (docs/plans/distortion-antialiasing.md section 4.0 "alias test skeleton").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "alias_measure.h"
#include "fx/fx_effects.h"
#include "synthyboy/aa_switch.h"

namespace aliasfx
{
constexpr double kFs = 44100.0;
constexpr size_t kN = 65536;
constexpr size_t kBin = 1487;
constexpr size_t kSkip = 16384;

inline double f0()
{
    return static_cast<double> (kBin) * kFs / static_cast<double> (kN);
}

struct Lcg
{
    uint32_t state;
    explicit Lcg (uint32_t seed) : state (seed) {}
    uint32_t next() noexcept { state = state * 1664525u + 1013904223u; return state; }
    float sample() noexcept { return static_cast<float> (next()) * (2.0f / 4294967296.0f) - 1.0f; }
};

/** The coherent sine of peak 0.5: kSkip + kN samples, computed in double, stored as float. */
inline std::vector<float> sine (double peak = 0.5)
{
    std::vector<float> x (kSkip + kN);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float> (peak * std::sin (6.283185307179586 * static_cast<double> ((kBin * i) % kN) / static_cast<double> (kN)));
    return x;
}

/** The left channel of a Distortion run (both channels carry the input), the input processed in chunks of `chunk`. */
inline std::vector<float> run (const own::Distortion::Params& p, bool fast, const std::vector<float>& in, size_t chunk = 32)
{
    own::Distortion d;
    d.prepare (kFs);
    d.fast = fast;
    d.set (p);
    std::vector<float> l (in), r (in);
    for (size_t pos = 0; pos < l.size(); pos += chunk)
        d.process (l.data() + pos, r.data() + pos, static_cast<int> (std::min (chunk, l.size() - pos)));
    return l;
}

inline own::Distortion::Params params (int type, float driveDb)
{
    own::Distortion::Params p;
    p.type = type;
    p.driveDb = driveDb;
    p.mix = 1.0f;
    p.order = 0;
    return p;
}

/** ALIAS_DB and HARM_DB of the effect for a sine of peak 0.5. `on` = the anti-aliasing active (aa::kDistortion clear). The switch is restored to 0. */
inline aliasm::Result measure (int type, float driveDb, bool fast, bool on)
{
    synthyboy::aa::setOff (on ? 0u : synthyboy::aa::kDistortion);
    const std::vector<float> y = run (params (type, driveDb), fast, sine());
    synthyboy::aa::setOff (0);
    return aliasm::measure (y.data() + kSkip, kN, f0(), kFs);
}

/** The DIST-AA-CHECKSUM line of a type (drive +12 dB, LCG input, order 0, mix 1, 512 samples), the format of core/tests/test_own_fx.cpp (DIST-CHECKSUM). */
inline void checksum (int type)
{
    own::Distortion dist;
    dist.prepare (kFs);
    dist.set (params (type, 12.0f));
    const int n = 512;
    std::vector<float> bufL (n), bufR (n);
    Lcg rng (123456789u);
    for (int i = 0; i < n; ++i)
    {
        bufL[i] = rng.sample();
        bufR[i] = rng.sample();
    }
    dist.process (bufL.data(), bufR.data(), n);
    double sumAbs = 0.0;
    for (int i = 0; i < n; ++i)
        sumAbs += static_cast<double> (std::fabs (bufL[i])) + static_cast<double> (std::fabs (bufR[i]));
    std::printf ("DIST-AA-CHECKSUM type=%d sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", type, sumAbs, static_cast<double> (bufL[n - 8]), static_cast<double> (bufL[n - 7]),
                 static_cast<double> (bufL[n - 6]), static_cast<double> (bufL[n - 5]), static_cast<double> (bufL[n - 4]), static_cast<double> (bufL[n - 3]),
                 static_cast<double> (bufL[n - 2]), static_cast<double> (bufL[n - 1]));
}

inline bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

/** The common checks of the converted types: block split (32, 7, 128 give the same bits) and reset () against a fresh instance. Returns true when both hold. */
inline bool blockSplitAndReset (int type, bool fast)
{
    const own::Distortion::Params p = params (type, 12.0f);
    std::vector<float> in (4096);
    Lcg rng (987654321u);
    for (auto& v : in)
        v = 0.8f * rng.sample();
    const std::vector<float> a = run (p, fast, in, 32), b = run (p, fast, in, 7), c = run (p, fast, in, 128);
    bool ok = same (a, b) && same (a, c);

    own::Distortion warm, fresh;
    warm.prepare (kFs);
    fresh.prepare (kFs);
    warm.fast = fresh.fast = fast;
    warm.set (p);
    fresh.set (p);
    std::vector<float> w1 (in.begin(), in.begin() + 1000), w2 (w1);
    warm.process (w1.data(), w2.data(), 1000);
    warm.reset();
    std::vector<float> l1 (in.begin() + 1000, in.begin() + 2000), r1 (l1), l2 (l1), r2 (l1);
    warm.process (l1.data(), r1.data(), 1000);
    fresh.process (l2.data(), r2.data(), 1000);
    ok = ok && same (l1, l2) && same (r1, r2);
    return ok;
}
} // namespace aliasfx
