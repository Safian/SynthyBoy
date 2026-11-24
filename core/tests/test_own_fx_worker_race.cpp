// SynthyBoy - own engine test: the effect module builder thread against the audio thread (meant for ThreadSanitizer; plain builds only check that the sound stays finite).
// The builder thread (OwnEngine::startFxWorker -> FxPool::typeChanged) must not read what only the audio thread owns (FxPool::Slot::cur, written by FxChain::configureSlot when it takes a
// fresh module over). A TSan run of shell-mpc/tests/stub_host reported exactly this pair (fx_chain.cpp:134 read on the builder thread against fx_chain.cpp:254 write on the audio thread).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
void setKey (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}
} // namespace

// A control thread sets the same type key again and again (every set wakes the builder thread, which asks typeChanged () whether the slot has its module yet) while the audio thread
// processes and takes the freshly built module over. Without a handoff between the two the builder's read of the slot's current module races with the audio thread's write.
TEST (fx_builder_thread_and_audio_thread_do_not_race_on_the_slot_module)
{
    for (int round = 0; round < sbtest::scaled (20, 6); ++round)
    {
        auto e = createOwnEngine();
        setKey (*e, "osc2.on", 0.0f);
        setKey (*e, "filter1.on", 0.0f);
        Quality q;
        q.realtime = true;
        e->prepare (48000.0, 128, q);   // starts the builder thread
        e->readyContent();
        setKey (*e, "fxi01.on", 1.0f);
        setKey (*e, "fxi01.p01", 1.0f);
        setKey (*e, "fxi01.p02", 1.0f);

        std::atomic<bool> done { false };
        std::thread control ([&]
        {
            while (! done.load (std::memory_order_acquire))
            {
                setKey (*e, "fxi01.type", 4.0f);   // Distortion; the same value again: the builder finds the type already seen and looks at the slot
                std::this_thread::sleep_for (std::chrono::microseconds (100));
            }
        });

        std::vector<float> l (128), r (128);
        bool finite = true;
        for (int blk = 0; blk < 300; ++blk)
        {
            std::vector<Event> ev;
            if (blk == 0)
                ev.push_back (sbtest::noteOn (0, 48));
            e->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 128);
            for (int i = 0; i < 128; ++i)
                finite = finite && std::isfinite (l[static_cast<size_t> (i)]);
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
        done.store (true, std::memory_order_release);
        control.join();
        CHECK (finite);
    }
}
