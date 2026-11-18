// SynthyBoy - own engine tests: the helper threads of the engine (CPU review T1). The wake-up flag (dsp/helper_thread.h) loses no wake-up and never waits on a timeout; the engine's effect
// module builder thread wakes on a live type change, stops cleanly, and calls the shell's start hook on its own thread; the hook can be set and removed.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "dsp/helper_thread.h"
#include "synthyboy/engine.h"
#include "synthyboy/helper_thread_hook.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
using namespace std::chrono;

namespace
{
/** Runs `fn` on a thread and waits up to `limitMs` for it; false (and the thread is released by `rescue`) when it does not finish: a lost wake-up shows as a failed check, not a hung test. */
template <class Fn, class Rescue>
bool finishesWithin (int limitMs, Fn fn, Rescue rescue)
{
    std::atomic<bool> done { false };
    std::thread t ([&] { fn(); done.store (true); });
    const auto end = steady_clock::now() + milliseconds (limitMs);
    while (! done.load() && steady_clock::now() < end)
        std::this_thread::sleep_for (milliseconds (1));
    const bool ok = done.load();
    if (! ok)
        rescue();
    t.join();
    return ok;
}
} // namespace

TEST (wake_signal_notify_before_wait_is_kept_and_several_notifies_are_one_wake)
{
    own::WakeSignal w;
    w.notify();
    w.notify();
    w.notify();
    CHECK (finishesWithin (2000, [&] { w.wait(); }, [&] { w.notify(); }));   // the flag was set before the wait
    // the three notifies were one wake: a second wait blocks until the next notify
    std::atomic<bool> woke { false };
    std::thread t ([&] { w.wait(); woke.store (true); });
    std::this_thread::sleep_for (milliseconds (40));
    CHECK (! woke.load());
    w.notify();
    t.join();
    CHECK (woke.load());
}

TEST (wake_signal_loses_no_wake_up_in_a_long_ping_pong)
{
    // the producer publishes work items and notifies; the consumer waits, then takes everything published. Whatever the interleaving, the consumer must see every item
    // (a lost wake-up leaves items unseen while it sleeps without a timeout)
    own::WakeSignal w;
    std::atomic<int> published { 0 }, taken { 0 };
    std::atomic<bool> stop { false };
    const int total = 200000;
    std::thread consumer ([&]
    {
        for (;;)
        {
            w.wait();
            taken.store (published.load (std::memory_order_acquire), std::memory_order_release);
            if (stop.load (std::memory_order_acquire) && taken.load() == total)
                break;
        }
    });
    for (int i = 1; i <= total; ++i)
    {
        published.store (i, std::memory_order_release);
        w.notify();
        if (i % 1000 == 0)
            std::this_thread::yield();
    }
    stop.store (true, std::memory_order_release);
    w.notify();
    const auto end = steady_clock::now() + seconds (10);
    while (taken.load() != total && steady_clock::now() < end)
        std::this_thread::sleep_for (milliseconds (1));
    CHECK_EQ (taken.load(), total);
    if (taken.load() != total)
        w.notify();   // rescue
    consumer.join();
}

TEST (wake_signal_stop_wakes_a_sleeping_waiter_at_once)
{
    for (int round = 0; round < 50; ++round)
    {
        own::WakeSignal w;
        std::atomic<bool> stop { false };
        std::thread t ([&] { while (! stop.load (std::memory_order_acquire)) w.wait(); });
        if (round % 2 == 0)
            std::this_thread::sleep_for (milliseconds (2));   // sleeping, or not yet waiting: both must stop
        stop.store (true, std::memory_order_release);
        w.notify();
        const auto t0 = steady_clock::now();
        t.join();
        CHECK (steady_clock::now() - t0 < seconds (2));
    }
}

namespace
{
std::atomic<int> gHookCalls { 0 };
std::atomic<std::thread::id> gHookThread;
char gHookName[32];
void testHook (const char* name)
{
    std::strncpy (gHookName, name, sizeof gHookName - 1);
    gHookThread.store (std::this_thread::get_id());
    gHookCalls.fetch_add (1);
}

void setKey (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}
} // namespace

TEST (engine_fx_builder_thread_starts_with_the_hook_and_stops_cleanly)
{
    gHookCalls.store (0);
    setHelperThreadStartHook (&testHook);
    for (int i = 0; i < 20; ++i)
    {
        auto e = createOwnEngine();
        Quality q;
        q.realtime = true;
        e->prepare (48000.0, 128, q);   // starts the builder thread
        if (i % 3 == 0)
            std::this_thread::sleep_for (milliseconds (3));
        if (i % 5 == 0)
            e->prepare (44100.0, 256, q);   // stops and starts it again
        e.reset();                          // stops it: must not hang
    }
    setHelperThreadStartHook (nullptr);
    CHECK (gHookCalls.load() >= 20);
    CHECK (std::string (gHookName) == "sb-fx-build");
    CHECK (gHookThread.load() != std::this_thread::get_id());   // it ran on the helper thread, not on the thread that created it
    // offline: no builder thread, no hook call
    gHookCalls.store (0);
    setHelperThreadStartHook (&testHook);
    {
        auto e = createOwnEngine();
        Quality q;
        q.realtime = false;
        e->prepare (48000.0, 128, q);
    }
    setHelperThreadStartHook (nullptr);
    CHECK_EQ (gHookCalls.load(), 0);
}

// A live change of an effect slot's type: the builder thread (no timeout, woken by the setParam) makes the module and the audio thread takes it over; the output then differs from that of an engine
// without the slot. A lost wake-up would leave the module unbuilt for ever.
TEST (engine_live_fx_type_change_wakes_the_builder)
{
    auto make = [] (int mode)   // 0 no slot, 1 slot on with every parameter but no type (the control), 2 the slot with its type set live
    {
        auto e = createOwnEngine();
        setKey (*e, "osc2.on", 0.0f);
        setKey (*e, "filter1.on", 0.0f);
        Quality q;
        q.realtime = true;
        e->prepare (48000.0, 128, q);
        e->readyContent();
        if (mode >= 1)
        {
            setKey (*e, "fxi01.on", 1.0f);
            setKey (*e, "fxi01.p01", 1.0f);
            setKey (*e, "fxi01.p02", 1.0f);
        }
        if (mode == 2)
            setKey (*e, "fxi01.type", 4.0f);   // Distortion
        return e;
    };
    auto a = make (2), b = make (0), c = make (1);
    std::vector<float> la (128), ra (128), lb (128), rb (128), lc (128), rc (128);
    bool differs = false, controlSame = true;
    const auto t0 = steady_clock::now();
    for (int blk = 0; blk < 4000 && ! differs; ++blk)
    {
        std::vector<Event> ev;
        if (blk == 0)
            ev.push_back (sbtest::noteOn (0, 48));
        a->process (ev.data(), static_cast<int> (ev.size()), la.data(), ra.data(), 128);
        b->process (ev.data(), static_cast<int> (ev.size()), lb.data(), rb.data(), 128);
        c->process (ev.data(), static_cast<int> (ev.size()), lc.data(), rc.data(), 128);
        controlSame = controlSame && std::memcmp (lb.data(), lc.data(), 128 * sizeof (float)) == 0;   // without a type the slot changes nothing
        differs = std::memcmp (la.data(), lb.data(), 128 * sizeof (float)) != 0;
        std::this_thread::sleep_for (microseconds (500));
    }
    std::printf ("    the slot's module took over after %.1f ms\n", duration<double, std::milli> (steady_clock::now() - t0).count());
    CHECK (differs);
    CHECK (controlSame);
}
