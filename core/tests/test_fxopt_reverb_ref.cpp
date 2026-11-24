// SynthyBoy - own engine tests: the frozen reference of the reverb (docs/plans/reverb-convolve.md task T04).
// fxopt_reverb_ref_matches_live_at_pin proves, while nothing has changed yet, that the copy equals the live reverb bit for bit (gate V0 then pins the checksums into kGolden and deletes this case).
// fxopt_reverb_reference_is_original asserts the checksums against kGolden for ever after (while kGolden is empty it prints UNPINNED and passes).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <map>
#include <string>

#include "fx/fx_reverb.h"
#include "fxopt_reverb_util.h"
#include "ref/reverb_reference.h"
#include "test_runner.h"

using synthyboy::own::Reverb;
using RefReverb = synthyboy::own::ref::Reverb;

namespace
{
// "type=<t> room=<r> sr=<sr> set=<D|S>" -> the checksum of the reference render; filled at gate V0
const std::map<std::string, uint64_t> kGolden = {};

std::string keyOf (int type, int room, int sr, char set)
{
    char b[96];
    std::snprintf (b, sizeof b, "type=%d room=%d sr=%d set=%c", type, room, sr, set);
    return b;
}
} // namespace

TEST (fxopt_reverb_ref_matches_live_at_pin)
{
    for (const int sr : { 44100, 48000 })
    {
        const fxopt::Program in = fxopt::makeProgram (static_cast<size_t> (3 * sr), static_cast<double> (sr), 7);
        for (const char set : { 'D', 'S' })
            for (const auto& tc : fxopt::reverbCases())
            {
                const fxopt::Program live = fxopt::renderReverb<Reverb> (fxopt::reverbParams<Reverb::Params> (set, tc.first, tc.second), sr, in, 32);
                const fxopt::Program ref = fxopt::renderReverb<RefReverb> (fxopt::reverbParams<RefReverb::Params> (set, tc.first, tc.second), sr, in, 32);
                std::printf ("REVERB-REF-CHECKSUM type=%d room=%d sr=%d set=%c %016llx\n", tc.first, tc.second, sr, set, static_cast<unsigned long long> (fxopt::hashProgram (ref)));
                CHECK (fxopt::sameProgram (live, ref));
            }
    }
}

TEST (fxopt_reverb_reference_is_original)
{
    if (kGolden.empty())
    {
        std::printf ("REVERB-REF UNPINNED\n");
        return;
    }
    for (const int sr : { 44100, 48000 })
    {
        const fxopt::Program in = fxopt::makeProgram (static_cast<size_t> (3 * sr), static_cast<double> (sr), 7);
        for (const char set : { 'D', 'S' })
            for (const auto& tc : fxopt::reverbCases())
            {
                const auto it = kGolden.find (keyOf (tc.first, tc.second, sr, set));
                CHECK (it != kGolden.end());
                if (it == kGolden.end())
                    continue;
                const fxopt::Program ref = fxopt::renderReverb<RefReverb> (fxopt::reverbParams<RefReverb::Params> (set, tc.first, tc.second), sr, in, 32);
                CHECK_EQ (fxopt::hashProgram (ref), it->second);
            }
    }
}
