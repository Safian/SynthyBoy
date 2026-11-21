// SynthyBoy - core tests: helpers of the reverb optimisation tests (docs/plans/reverb-convolve.md sections 4.0 and 4.2): the parameter sets D, S, P and D0, the type list and a render of
// the live reverb or of the frozen reference in chunks.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "fxopt_util.h"

namespace fxopt
{
/** The parameter sets of the plan (4.0): D = the defaults of Reverb::Params with mix 1 and decay 1.5; S = size 0.2, decay 0.3, chorusAmount 1.0, diffA 0, diffB 1, highShelfDb -6, lowShelfDb -6,
    highShelfHz 800; P = D plus predelay 0.05; D0 = D with chorusAmount 0 (the LFO terms are zero). */
template <class Params>
Params reverbParams (char set, int type, int room)
{
    Params p;
    p.mix = 1.0f;
    p.decay = 1.5f;
    p.type = type;
    p.room = room;
    if (set == 'S')
    {
        p.size = 0.2f;
        p.decay = 0.3f;
        p.chorusAmount = 1.0f;
        p.diffA = 0.0f;
        p.diffB = 1.0f;
        p.highShelfDb = -6.0f;
        p.lowShelfDb = -6.0f;
        p.highShelfHz = 800.0f;
    }
    else if (set == 'P')
        p.predelay = 0.05f;
    else if (set == 'Z') // D0
        p.chorusAmount = 0.0f;
    return p;
}

/** Every algorithm of the plan: types 0..5, and for type 4 the rooms 0..4 (the other types use room 2). */
inline std::vector<std::pair<int, int>> reverbCases()
{
    std::vector<std::pair<int, int>> v;
    for (int type = 0; type <= 5; ++type)
    {
        if (type == 4)
            for (int room = 0; room <= 4; ++room)
                v.emplace_back (type, room);
        else
            v.emplace_back (type, 2);
    }
    return v;
}

/** `R` (live or reference Reverb) prepared at `sr`, set to `p`, run over `in` in chunks of `chunk` samples; the output of both channels. */
template <class R>
Program renderReverb (const typename R::Params& p, double sr, const Program& in, int chunk)
{
    R reverb;
    reverb.prepare (sr);
    reverb.set (p);
    Program out = in;
    for (size_t pos = 0; pos < out.l.size(); pos += static_cast<size_t> (chunk))
    {
        const int n = static_cast<int> (std::min (static_cast<size_t> (chunk), out.l.size() - pos));
        reverb.process (out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

/** The checksum of both channels (left then right). */
inline uint64_t hashProgram (const Program& p)
{
    std::vector<float> all (p.l);
    all.insert (all.end(), p.r.begin(), p.r.end());
    return fnv1a (all.data(), all.size());
}

inline bool sameProgram (const Program& a, const Program& b)
{
    return a.l.size() == b.l.size() && sameBits (a.l.data(), b.l.data(), a.l.size()) && sameBits (a.r.data(), b.r.data(), a.r.size());
}
} // namespace fxopt
