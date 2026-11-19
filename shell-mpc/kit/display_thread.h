// MpcKit - the DISPLAY THREAD of a plugin: a normal-priority thread (never the audio thread) that tells the host what changed since the last look - a parameter a plugin-drawn page wrote, a Q proxy whose
// target moved, the instance frame of "Overlay Instance", a live display value - with audioMasterAutomate / audioMasterUpdateDisplay. The plugin REGISTERS its features; the class runs them in order once
// a tick, each inside a PollGuard (shell-mpc/poll_guard.h): an exception is caught, counted, written to the status file and, five times in a row, switches that feature off - it never leaves the thread
// (std::terminate would end the MPC application). The thread sleeps on a fixed grid: a short period while something is going on (a feature says so with setBusy), a long one otherwise.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//     mpckit::DisplayThread display (this, "my-display", { std::chrono::milliseconds (50), std::chrono::milliseconds (100) });
//     display.add ("overlay report", [&] { ... audioMasterAutomate ... });
//     display.add ("Q proxies", [&] { qBank.poll (reportQ, reportReal, groupChanged); });
//     thread = std::thread ([&] { display.run ([&] { return stop.load(); }, [&] { return hostKnows.load(); }); });
//
// WHY THE TWO CALLBACKS OF run (): `stop` ends the thread (checked after every sleep, so a stop is answered within one period); `ready` says the host has called the plugin at least once (before that the host may
// not hold the effect's pointer yet: no callback into it): while it is false the tick is skipped and the grid goes on.
//
// THREADS. add / addUnguarded before run (); setBusy from the features (on the display thread); run () on the display thread only.
//
// NO DEPENDENCY on the synthesizer engine of SynthyBoy.
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "../poll_guard.h"
#include "../thread_policy.h"

namespace mpckit {

class DisplayThread
{
public:
    struct Timing
    {
        std::chrono::milliseconds active { 100 };   // the period while a feature says something is going on
        std::chrono::milliseconds idle { 100 };     // the period otherwise: a look, no call to the host
    };

    /** `owner` tells the instances apart in the status file, `threadName` is the name of the thread (at most 15 characters on Linux). */
    DisplayThread (const void* owner, const char* threadName) : owner_ (owner), name_ (threadName) {}
    DisplayThread (const void* owner, const char* threadName, Timing timing) : owner_ (owner), name_ (threadName), timing_ (timing) {}

    /** A feature: `tick` runs inside a PollGuard named `name` once per tick, in the order the features were added. */
    void add (const char* name, std::function<void()> tick)
    {
        features_.push_back (std::make_unique<Feature> (name, owner_, std::move (tick)));
    }
    /** A step that is not guarded (it cannot throw, or catches for itself): runs in its place in the order. */
    void addUnguarded (std::function<void()> tick) { features_.push_back (std::make_unique<Feature> (nullptr, owner_, std::move (tick))); }

    /** A feature says that something is going on (a voice sounds, a value moves): the next tick comes after the active period; false: the idle one. The last word of a tick counts. */
    void setBusy (bool busy) noexcept { busy_.store (busy, std::memory_order_relaxed); }
    bool busy() const noexcept { return busy_.load (std::memory_order_relaxed); }
    /** The active period always (measurements: the sweep of the cursors without a voice). */
    void setAlwaysActive (bool on) noexcept { always_ = on; }

    /** The thread's whole life: own scheduling (normal policy, nice 0: not lower than normal, because this thread holds host locks while it is inside a callback), then the grid of ticks until `stop` returns true. */
    void run (const std::function<bool()>& stop, const std::function<bool()>& ready)
    {
        synthyboy_mpc::useNormalScheduling (0, name_);
        auto next = std::chrono::steady_clock::now();
        while (! stop())
        {
            next += always_ || busy_.load (std::memory_order_relaxed) ? timing_.active : timing_.idle;
            const auto now = std::chrono::steady_clock::now();
            if (next < now)
                next = now;   // a tick that was late (a host call blocked): no catch-up burst of calls, one tick now and the grid goes on from here
            std::this_thread::sleep_until (next);
            if (stop())
                break;
            if (! ready())
                continue;
            tick();
        }
    }

    /** One tick: every feature once, in order (run () does it; the tests do it without a thread). */
    void tick()
    {
        for (const std::unique_ptr<Feature>& f : features_)
        {
            if (f->guarded)
                f->guard.run (f->fn);
            else
                f->fn();
        }
    }

    /** How often feature `index` (in the order added) failed, and whether it is switched off (for the tests and the status). */
    long failures (size_t index) const noexcept { return index < features_.size() ? features_[index]->guard.failures : 0; }
    bool off (size_t index) const noexcept { return index < features_.size() && features_[index]->guard.off; }

private:
    struct Feature
    {
        Feature (const char* featureName, const void* owner, std::function<void()> tickFn) : guarded (featureName != nullptr), guard (featureName != nullptr ? featureName : "", owner), fn (std::move (tickFn)) {}
        bool guarded;
        synthyboy_mpc::PollGuard guard;
        std::function<void()> fn;
    };

    const void* owner_;
    const char* name_;
    Timing timing_;
    std::atomic<bool> busy_ { false };   // written by the audio / host thread (setBusy), read by the display thread
    bool always_ = false;
    std::vector<std::unique_ptr<Feature>> features_;
};

} // namespace mpckit
