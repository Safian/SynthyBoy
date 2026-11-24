// SynthyBoy - core/fx-serum-b tests: the frozen reference of Convolve (docs/plans/reverb-convolve.md task T05).
// conv_reference_matches_live_at_pin proves, while nothing has changed yet, that the copy equals the live Convolve bit for bit (gate V0 then pins the checksums into kGolden and deletes this case).
// conv_reference_is_original asserts the checksums against kGolden for ever after (while kGolden is empty it prints UNPINNED and passes).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "conv_util.h"
#include "convolve.h"
#include "fxopt_util.h"
#include "ref/convolve_reference.h"
#include "test_runner.h"

namespace
{
// "ir=<frames> block=<n>" -> the checksum of the reference render; filled at gate V0
const std::map<std::string, uint64_t> kGolden = {};

template <class C>
fxopt::Program render (int irFrames, int block, const fxopt::Program& in)
{
    C convolve;
    convolve.prepare (44100.0, 512);
    const std::vector<float> ir = convutil::decayingNoise (irFrames, static_cast<double> (irFrames) * 0.3 + 1.0, 5);
    convolve.params.tone = 0.4f;
    convolve.params.preDelay = 0.01f;
    convolve.loadIr (ir.data(), irFrames, 1, 44100.0);
    fxopt::Program out = in;
    for (size_t pos = 0; pos < out.l.size(); pos += static_cast<size_t> (block))
    {
        const int n = static_cast<int> (std::min (static_cast<size_t> (block), out.l.size() - pos));
        convolve.process (out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

uint64_t hashOf (const fxopt::Program& p)
{
    std::vector<float> all (p.l);
    all.insert (all.end(), p.r.begin(), p.r.end());
    return fxopt::fnv1a (all.data(), all.size());
}

std::string keyOf (int irFrames, int block)
{
    char b[64];
    std::snprintf (b, sizeof b, "ir=%d block=%d", irFrames, block);
    return b;
}

const int kIrFrames[4] = { 1, 129, 3000, 20000 };
const int kBlocks[3] = { 1, 37, 512 };
} // namespace

TEST (conv_reference_matches_live_at_pin)
{
    const fxopt::Program in = fxopt::makeProgram (40000, 44100.0, 13);
    for (const int irFrames : kIrFrames)
        for (const int block : kBlocks)
        {
            const fxopt::Program live = render<synthyboy::fx::Convolve> (irFrames, block, in);
            const fxopt::Program ref = render<synthyboy::fx::ref::Convolve> (irFrames, block, in);
            std::printf ("CONVOLVE-REF-CHECKSUM %s %016llx\n", keyOf (irFrames, block).c_str(), static_cast<unsigned long long> (hashOf (ref)));
            CHECK (fxopt::sameBits (live.l.data(), ref.l.data(), live.l.size()) && fxopt::sameBits (live.r.data(), ref.r.data(), live.r.size()));
        }
}

TEST (conv_reference_is_original)
{
    if (kGolden.empty())
    {
        std::printf ("CONVOLVE-REF UNPINNED\n");
        return;
    }
    const fxopt::Program in = fxopt::makeProgram (40000, 44100.0, 13);
    for (const int irFrames : kIrFrames)
        for (const int block : kBlocks)
        {
            const auto it = kGolden.find (keyOf (irFrames, block));
            CHECK (it != kGolden.end());
            if (it == kGolden.end())
                continue;
            CHECK_EQ (hashOf (render<synthyboy::fx::ref::Convolve> (irFrames, block, in)), it->second);
        }
}
