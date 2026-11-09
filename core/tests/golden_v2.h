// SynthyBoy - core tests: fingerprint of the 880 keys of state version 2 (append-only rule R2/R3 of docs/ui-design/13-new-keys.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The fingerprint covers everything a stored state, preset or host automation lane relies on: position, id, key, display name,
// group, range, default, skew, grid, scale, unit, flags, decimals and the choice names. A choice list that gained entries in
// version 3 only counts the entries it had in version 2 (the appended ones are fixed by the choice-append test), and the upper
// bound of a stepped parameter is left out for the same reason (it grows with the list).
// The expected value was computed from the table of commit 9c97a69 (before any version 3 change) with SB_GOLDEN_ORIGINAL defined.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "synthyboy/params.h"

namespace sbgolden
{
inline void mix (uint64_t& h, const void* data, size_t n)
{
    const auto* p = static_cast<const unsigned char*> (data);
    for (size_t i = 0; i < n; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull; // FNV-1a, 64 bit
    }
}

inline void mixString (uint64_t& h, const char* s)
{
    mix (h, s, std::strlen (s) + 1);
}

inline void mixFloat (uint64_t& h, float f)
{
    uint32_t bits = 0;
    std::memcpy (&bits, &f, sizeof bits);
    mix (h, &bits, sizeof bits);
}

template <typename T>
inline void mixInt (uint64_t& h, T v)
{
    const auto w = static_cast<uint64_t> (v);
    mix (h, &w, sizeof w);
}

/** Number of choice names key had in state version 2 (the list sizes of docs/ui-design/13-new-keys.md section 3). */
inline size_t choicesInV2 (const synthyboy::ParamInfo& p)
{
    size_t n = p.numChoices;
#ifndef SB_GOLDEN_ORIGINAL
    const std::string key = p.key;
    const auto endsWith = [&] (const char* s) { const size_t m = std::strlen (s); return key.size() >= m && key.compare (key.size() - m, m, s) == 0; };
    if (endsWith (".wavetable"))
        n -= 1;
    else if (endsWith (".destination") && key.compare (0, 3, "mod") != 0)
        n -= 2; // oscN.destination, noise.destination
    else if (key.compare (0, 3, "lfo") == 0 && endsWith (".shape"))
        n -= 1;
    else if (key.compare (0, 4, "rlfo") == 0 && endsWith (".style"))
        n -= 1;
    else if (endsWith (".model"))
        n -= 19;
    else if (endsWith (".style") && (key.compare (0, 6, "filter") == 0 || key.compare (0, 8, "fxfilter") == 0))
        n -= 6;
    else if (key.compare (0, 9, "fx.order.") == 0)
        n -= 7;
    else if (key == "distortion.type")
        n -= 8; // block B17 (13-new-keys.md 4.17): Tube .. X-Shaper (Asym)
    else if (key == "delay.style")
        n -= 1; // MANUALCHECK (13-new-keys.md 4.19): Tap->Delay
    else if (key.compare (0, 3, "osc") == 0 && endsWith (".warp.type"))
        n -= 41; // block B21 (13-new-keys.md 4.22): the warp modes 11 .. 51 (oscN.warp2.type is a version 3 key)
#endif
    return n;
}

/** The largest value key could take in state version 2 (the lists and the mod source / destination choices have grown since). */
inline float maxInV2 (const synthyboy::ParamInfo& p)
{
    using synthyboy::ChoiceSet;
    switch (p.choiceSet)
    {
        case ChoiceSet::list:           return static_cast<float> (choicesInV2 (p)) - 1.0f;
        case ChoiceSet::modSource:      return 28.0f;
        case ChoiceSet::modDestination: return 269.0f;
        case ChoiceSet::none:           break;
    }
    return p.max;
}

/** Fingerprint of the first `count` table entries (880 for the golden). */
inline uint64_t fingerprint (size_t count)
{
    uint64_t h = 14695981039346656037ull;
    const auto* t = synthyboy::paramTable();
    for (size_t i = 0; i < count; ++i)
    {
        const auto& p = t[i];
        mixInt (h, p.order);
        mixInt (h, p.id);
        mixString (h, p.key);
        mixString (h, p.name);
        mixString (h, p.group);
        mixFloat (h, p.min);
        mixFloat (h, p.def);
        mixFloat (h, p.skew);
        mixFloat (h, p.interval);
        mixInt (h, static_cast<int> (p.scale));
        mixInt (h, static_cast<int> (p.unit));
        mixInt (h, p.flags);
        mixInt (h, p.decimals);
        mixInt (h, static_cast<int> (p.choiceSet));
        if (p.choiceSet == synthyboy::ChoiceSet::list)
        {
            const size_t n = choicesInV2 (p);
            mixInt (h, n);
            for (size_t k = 0; k < n; ++k)
                mixString (h, p.choices[k]);
        }
        else if (p.scale != synthyboy::Scale::stepped)
        {
            mixFloat (h, p.max);
        }
    }
    return h;
}
} // namespace sbgolden
