// SynthyBoy - own engine tests: the frozen reference of Hyper / Dimension (docs/plans/reverb-convolve.md task T06).
// fxopt_hyper_matches_live_at_pin proves, while nothing has changed yet, that the copy equals the live effect bit for bit (gate V0 then pins the checksums into kGolden and deletes this case).
// fxopt_hyper_reference_is_original asserts the checksums against kGolden for ever after (while kGolden is empty it prints UNPINNED and passes).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <map>
#include <string>

#include "fxopt_util.h"
#include "ref/hyper_reference.h"
#include "synthyboy/fx/hyper_dimension.h"
#include "test_runner.h"

namespace
{
// "unison=<u> hyper=<mix> dim=<mix>" -> the checksum of the reference render; filled at gate V0
const std::map<std::string, uint64_t> kGolden = {};

template <class H>
fxopt::Program render (int unison, float hyperMix, float dimMix, const fxopt::Program& in, int chunk)
{
    H effect;
    effect.prepare (44100.0, 512);
    typename H::Params p;
    p.hyperUnison = unison;
    p.hyperMix = hyperMix;
    p.dimMix = dimMix;
    effect.setParams (p);
    effect.reset();
    fxopt::Program out = in;
    for (size_t pos = 0; pos < out.l.size(); pos += static_cast<size_t> (chunk))
    {
        const int n = static_cast<int> (std::min (static_cast<size_t> (chunk), out.l.size() - pos));
        effect.process (out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

uint64_t hashOf (const fxopt::Program& p)
{
    std::vector<float> all (p.l);
    all.insert (all.end(), p.r.begin(), p.r.end());
    return fxopt::fnv1a (all.data(), all.size());
}

std::string keyOf (int unison, float hyperMix, float dimMix)
{
    char b[96];
    std::snprintf (b, sizeof b, "unison=%d hyper=%g dim=%g", unison, static_cast<double> (hyperMix), static_cast<double> (dimMix));
    return b;
}
} // namespace

TEST (fxopt_hyper_matches_live_at_pin)
{
    const fxopt::Program in = fxopt::makeProgram (3 * 44100, 44100.0, 11);
    for (int unison = 0; unison <= 7; ++unison)
        for (const float hyperMix : { 0.0f, 0.5f })
            for (const float dimMix : { 0.0f, 0.5f })
            {
                const fxopt::Program live = render<synthyboy::fx::HyperDimension> (unison, hyperMix, dimMix, in, 32);
                const fxopt::Program ref = render<synthyboy::fx::ref::HyperDimension> (unison, hyperMix, dimMix, in, 32);
                std::printf ("HYPER-REF-CHECKSUM %s %016llx\n", keyOf (unison, hyperMix, dimMix).c_str(), static_cast<unsigned long long> (hashOf (ref)));
                CHECK (live.l.size() == ref.l.size() && fxopt::sameBits (live.l.data(), ref.l.data(), live.l.size()) && fxopt::sameBits (live.r.data(), ref.r.data(), live.r.size()));
            }
}

TEST (fxopt_hyper_reference_is_original)
{
    if (kGolden.empty())
    {
        std::printf ("HYPER-REF UNPINNED\n");
        return;
    }
    const fxopt::Program in = fxopt::makeProgram (3 * 44100, 44100.0, 11);
    for (int unison = 0; unison <= 7; ++unison)
        for (const float hyperMix : { 0.0f, 0.5f })
            for (const float dimMix : { 0.0f, 0.5f })
            {
                const auto it = kGolden.find (keyOf (unison, hyperMix, dimMix));
                CHECK (it != kGolden.end());
                if (it == kGolden.end())
                    continue;
                CHECK_EQ (hashOf (render<synthyboy::fx::ref::HyperDimension> (unison, hyperMix, dimMix, in, 32)), it->second);
            }
}
