// SynthyBoy - shell-mpc acceptance: instances created and closed on several threads while the display thread reports and the overlay thread polls (SYNTHYBOY_MPC_OVERLAY build).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: close_stress_host <plugin library with the overlay> [cycles per thread, default 120]
// Three threads run create / use / close cycles of one, two and three instances at a time. "Use" writes the Play selectors and parameters (the display thread reports them with
// audioMasterAutomate), loads a chunk and asks for parameters, with random pauses of a few milliseconds, so closes meet display ticks, first reports and overlay polls. The host callback
// is the check: it is called with the AEffect of the instance, and an instance whose effClose has RETURNED must never call the host again (a callback after effClose returned is a use
// after free of the host's own bookkeeping). The check is only as good as the registry below: an address may be reused by a later instance, so the registry is updated under one lock
// while a new instance is created and marked alive before any callback of it is judged. Run under ThreadSanitizer and AddressSanitizer too (a thread that outlives its instance, a member
// used after it was destroyed and a leaked instance show there).
#include "mpc_vst2_abi.h"
#include "play_params.h"
#include "synthyboy/params.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace pl = synthyboy_mpc::play;

static std::recursive_mutex regMu;
static thread_local bool creating = false;   // this thread is inside VSTPluginMain: a callback it makes itself is the new instance's (the address may still be marked closed from an earlier instance)
enum class State { alive, closing, closed };
static std::map<AEffect*, State> registry;
static std::atomic<int> violations { 0 }, callbacks { 0 }, firstViolation { 0 };

static VstIntPtr hostCallback (AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion)
        return 2400;
    callbacks.fetch_add (1);
    if (creating)
        return 1;
    std::lock_guard<std::recursive_mutex> g (regMu);   // (blocks while a creation holds it: the new instance is marked alive before its first callback is judged)
    const auto it = registry.find (e);
    if (it != registry.end() && it->second == State::closed)
    {
        violations.fetch_add (1);
        int zero = 0;
        firstViolation.compare_exchange_strong (zero, opcode * 100000 + index + 1);
    }
    return 1;
}

using Main = AEffect* (*) (audioMasterCallback);
static Main entry = nullptr;

static AEffect* make()
{
    std::lock_guard<std::recursive_mutex> g (regMu);
    creating = true;
    AEffect* e = entry (hostCallback);
    creating = false;
    if (! e) return nullptr;
    registry[e] = State::alive;
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    return e;
}
static void closeIt (AEffect* e)
{
    {
        std::lock_guard<std::recursive_mutex> g (regMu);
        registry[e] = State::closing;   // callbacks are still allowed while effClose runs (the display thread is joined inside it)
    }
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);   // the AEffect is gone when this returns
    std::lock_guard<std::recursive_mutex> g (regMu);
    if (registry[e] == State::closing)   // (not already an instance of the same address that another thread created in the meantime)
        registry[e] = State::closed;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: close_stress_host <plugin> [cycles]\n"); return 2; }
    const int cycles = argc > 2 ? std::atoi (argv[2]) : 120;
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    const int n = static_cast<int> (synthyboy::paramCount());
    const int sel0 = n + 4 + 32 + 2 + 1;
    std::atomic<int> created { 0 }, failed { 0 };
    std::vector<std::thread> threads;
    for (int t = 0; t < 3; ++t)
        threads.emplace_back ([&, t] {
            std::mt19937 rng (static_cast<unsigned> (1234 + t));
            const int per = t + 1;   // one, two and three instances at a time
            auto pause = [&] (int maxMs) { std::this_thread::sleep_for (std::chrono::microseconds (std::uniform_int_distribution<int> (0, maxMs * 1000) (rng))); };
            for (int c = 0; c < cycles; ++c)
            {
                std::vector<AEffect*> live;
                for (int i = 0; i < per; ++i)
                {
                    AEffect* e = make();
                    if (! e) { failed.fetch_add (1); continue; }
                    created.fetch_add (1);
                    live.push_back (e);
                }
                for (int round = 0; round < 3; ++round)
                {
                    for (AEffect* e : live)
                    {
                        e->setParameter (e, sel0 + static_cast<int> (rng() % 3), static_cast<float> (rng() % 100) / 100.0f);   // selector: the display thread reports the group
                        e->setParameter (e, static_cast<int> (rng() % static_cast<unsigned> (n)), static_cast<float> (rng() % 100) / 100.0f);
                        (void) e->getParameter (e, sel0);
                        if (rng() % 7 == 0)
                        {
                            void* data = nullptr;
                            const VstIntPtr size = e->dispatcher (e, effGetChunk, 0, 0, &data, 0.0f);
                            if (data && size > 0) e->dispatcher (e, effSetChunk, 0, size, data, 0.0f);
                        }
                    }
                    pause (round == 0 ? 3 : 12);
                }
                // close in a random order, some at once, some after a pause (a display tick or the first report in between)
                while (! live.empty())
                {
                    const size_t k = rng() % live.size();
                    closeIt (live[k]);
                    live.erase (live.begin() + static_cast<long> (k));
                    if (rng() % 2) pause (8);
                }
            }
        });
    for (auto& th : threads) th.join();
    std::this_thread::sleep_for (std::chrono::milliseconds (500));   // a late callback of a closed instance would come now
    const int fails = (violations.load() != 0) + (failed.load() != 0);
    std::printf ("%s  %d instances created and closed on 3 threads, %d host callbacks, %d after an effClose had returned (first: opcode*100000+index+1 = %d), %d creations failed\n",
                 fails ? "FAIL" : "PASS", created.load(), callbacks.load(), violations.load(), firstViolation.load(), failed.load());
    std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
