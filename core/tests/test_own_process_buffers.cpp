// SynthyBoy - OwnEngine::process writes every frame of the output buffers it is given and reads none of them: what the buffers held before the call changes nothing.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// process () used to clear the left buffer before the 32 frame chunks that copy the Main mix over it (a dead write, removed). Two engines get the same events; one is given zero filled buffers, the
// other buffers full of NaN and of large values. Every block (stereo and mono, every block size around the control period and above the 4096 frames of prepare) must have the same bits. An
// engine that was not prepared still writes silence to both channels. (The allocation free mono block of any size is test_rt_alloc.cpp, process_oversized_mono_block_neither_allocates_nor_frees.)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
std::unique_ptr<IEngine> makeEngine (Tier tier)
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (48000.0, 128, qualityForTier (tier));
    for (const char* on : { "osc1.on", "osc2.on", "reverb.on", "delay.on", "chorus.on" })
        if (const ParamInfo* info = findParamByKey (on))
            e->setParam (info->id, 1.0f);
    if (const ParamInfo* info = findParamByKey ("osc1.bus1_send"))
        e->setParam (info->id, 50.0f);
    if (const ParamInfo* info = findParamByKey ("reverb.rack"))
        e->setParam (info->id, 1.0f);
    return e;
}

bool sameBits (const std::vector<float>& a, const std::vector<float>& b, int n)
{
    return std::memcmp (a.data(), b.data(), sizeof (float) * static_cast<size_t> (n)) == 0;
}
} // namespace

TEST (own_process_output_buffers_content_before_the_call_changes_nothing)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
        for (const bool mono : { false, true })
        {
            auto clean = makeEngine (tier);
            auto dirty = makeEngine (tier);
            int mismatches = 0;
            long blocks = 0;
            for (const int frames : { 1, 7, 31, 32, 33, 64, 100, 128, 257, 4097 })
            {
                std::vector<float> cl (static_cast<size_t> (frames)), cr (cl), dl (cl), dr (cl);
                for (int round = 0; round < 6; ++round)
                {
                    std::vector<Event> events;
                    if (round == 0)
                        for (int k : { 48, 60, 67 })
                            events.push_back (sbtest::noteOn (frames > 1 ? frames / 3 : 0, k, 0.8f, 0, k));
                    if (round == 4)
                        for (int k : { 48, 60, 67 })
                            events.push_back (sbtest::noteOff (frames > 1 ? frames / 2 : 0, k));
                    std::fill (cl.begin(), cl.end(), 0.0f);
                    std::fill (cr.begin(), cr.end(), 0.0f);
                    for (size_t i = 0; i < dl.size(); ++i)
                    {
                        dl[i] = (i % 3 == 0) ? nan : 1.0e30f;
                        dr[i] = (i % 2 == 0) ? nan : -1.0e30f;
                    }
                    const Event* ev = events.empty() ? nullptr : events.data();
                    const int ne = static_cast<int> (events.size());
                    clean->process (ev, ne, cl.data(), mono ? nullptr : cr.data(), frames);
                    dirty->process (ev, ne, dl.data(), mono ? nullptr : dr.data(), frames);
                    if (!sameBits (cl, dl, frames) || (!mono && !sameBits (cr, dr, frames)))
                        ++mismatches;
                    ++blocks;
                }
            }
            std::printf ("    tier %d, %s: %ld blocks\n", static_cast<int>(tier), mono ? "mono" : "stereo", blocks);
            CHECK_EQ (mismatches, 0);
        }
}

TEST (own_process_unprepared_engine_writes_silence_to_both_channels)
{
    auto engine = createOwnEngine();
    std::vector<float> l (100, 0.7f), r (100, -0.7f);
    engine->process (nullptr, 0, l.data(), r.data(), 100);
    bool silent = true;
    for (int i = 0; i < 100; ++i)
        silent = silent && l[static_cast<size_t> (i)] == 0.0f && r[static_cast<size_t> (i)] == 0.0f;
    CHECK (silent);
    std::fill (l.begin(), l.end(), 0.7f);
    engine->process (nullptr, 0, l.data(), nullptr, 100);
    silent = true;
    for (int i = 0; i < 100; ++i)
        silent = silent && l[static_cast<size_t> (i)] == 0.0f;
    CHECK (silent);
}
