// SynthyBoy - core tests: audio fingerprint of the sound of state version 2 (R5 of docs/ui-design/13-new-keys.md: a version 2
// document must still render the same audio after the table gained its version 3 keys).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The expected values in test_new_keys.cpp were computed by this very header compiled against commit 9c97a69 (before any
// version 3 change), for both engines, release build.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "golden_v2.h"
#include "synthyboy/engine.h"
#include "test_util.h"

namespace sbgolden
{
using synthyboy::Scale;
inline uint64_t audioFnv (const sbtest::Rendered& r)
{
    uint64_t h = 14695981039346656037ull;
    for (const auto* ch : { &r.left, &r.right })
        for (float f : *ch)
        {
            uint32_t bits = 0;
            std::memcpy (&bits, &f, sizeof bits);
            for (int k = 0; k < 4; ++k)
            {
                h ^= (bits >> (8 * k)) & 0xffu;
                h *= 1099511628211ull;
            }
        }
    return h;
}

/** Renders two overlapping notes. busy = false: the default patch. busy = true: the first 880 parameters (the keys of version 2)
    set to deterministic pseudo-random values first. Returns 0 if the engine does not exist in this build. */
inline sbtest::Rendered audioRender (synthyboy::EngineKind kind, bool busy)
{
    using namespace synthyboy;
    auto engine = createEngine (kind);
    if (engine == nullptr)
        return {};

    engine->setParam (kParamMasterPolyphony, 32.0f);
    if (busy)
    {
        sbtest::Rng rng (0x5b57a7e5ull);
        const auto* table = paramTable();
        for (size_t i = 0; i < 880; ++i)
        {
            // a stepped parameter is drawn from the values it had in version 2 (its lists have grown since)
            const double u = rng.uniform();
            const float plain = table[i].scale == Scale::stepped ? std::floor (static_cast<float> (u) * (maxInV2 (table[i]) + 1.0f))
                                                                 : fromNormalised (table[i], static_cast<float> (u));
            engine->setParam (table[i].id, plain);
        }
    }
    engine->prepare (48000.0, 512, Quality {});

    const std::vector<Event> events = { sbtest::noteOn (0, 60, 0.8f), sbtest::noteOn (2000, 67, 0.6f), sbtest::noteOff (20000, 60),
                                        sbtest::noteOff (30000, 67) };
    return sbtest::render (*engine, events, 48000, 480);
}

inline uint64_t audioHash (synthyboy::EngineKind kind, bool busy)
{
    const auto r = audioRender (kind, busy);
    return r.left.empty() ? 0 : audioFnv (r);
}

/** RMS of the left channel in 8 equal segments: the tolerant fingerprint for builds whose float code generation differs. */
inline std::vector<double> segmentRms (const sbtest::Rendered& r)
{
    std::vector<double> out;
    const size_t n = r.left.size() / 8;
    for (size_t seg = 0; seg < 8 && n > 0; ++seg)
    {
        double sum = 0.0;
        for (size_t i = seg * n; i < (seg + 1) * n; ++i)
            sum += static_cast<double> (r.left[i]) * static_cast<double> (r.left[i]);
        out.push_back (std::sqrt (sum / static_cast<double> (n)));
    }
    return out;
}
} // namespace sbgolden
