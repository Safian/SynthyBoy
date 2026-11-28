// effClose must not hang the host, and no callback may reach it too early or too late (robustness review F5, F12, F2). The overlay plugin is loaded like the host loads it and given a host callback that BLOCKS.
//   A  the display thread is inside audioMasterAutomate / UpdateDisplay and the host call does not return: effClose returns within the limit, no new host call starts after effClose began, and when the
//      host call finally returns the abandoned instance ends by itself (AddressSanitizer: no use after free).
//   B  no host callback (other than the identification call) before the host has called the dispatcher once: the pointer is not known to the host yet.
// Usage: close_hang_host <plugin>
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <unistd.h>

#include "mpc_vst2_abi.h"

using Main = AEffect* (*) (audioMasterCallback);
static Main entry = nullptr;
static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FAIL %s\n", what); ++fails; } else std::printf ("ok   %s\n", what); }
static double nowSec() { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void sleepMs (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }

static std::atomic<bool> blockCalls { false }, release { false }, closeBegan { false };
static std::atomic<int> blockedNow { 0 }, callsTotal { 0 }, callsAfterClose { 0 }, earlyCalls { 0 };
static std::atomic<bool> dispatcherCalled { false };
static std::thread::id mainThread;

static VstIntPtr host (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion)
        return 2400;
    if (opcode != audioMasterAutomate && opcode != audioMasterUpdateDisplay)
        return 0;
    if (std::this_thread::get_id() == mainThread)
        return 1;
    ++callsTotal;
    if (! dispatcherCalled.load())
        ++earlyCalls;
    if (closeBegan.load())
        ++callsAfterClose;
    if (blockCalls.load())
    {
        ++blockedNow;
        while (! release.load())
            sleepMs (5);
        --blockedNow;
    }
    return 1;
}

static AEffect* make (bool open)
{
    AEffect* e = entry (host);
    if (e && open)
    {
        e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
        dispatcherCalled.store (true);
    }
    return e;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: close_hang_host <plugin>\n"); return 2; }
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) return 2;
    mainThread = std::this_thread::get_id();

    // B: nothing before the host has called us
    {
        dispatcherCalled.store (false);
        earlyCalls.store (0);
        AEffect* e = make (false);
        sleepMs (700);   // six ticks of the display thread; the overlay report is due from the first one
        check (earlyCalls.load() == 0, "B: no callback before the host has called the dispatcher once");
        e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
        dispatcherCalled.store (true);
        callsTotal.store (0);
        for (int i = 0; i < 30 && callsTotal.load() == 0; ++i)
            sleepMs (50);
        check (callsTotal.load() > 0, "B: the reports that were due are made once the host knows the effect");
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
    }

    // A: the host call blocks
    {
        blockCalls.store (true);
        release.store (false);
        closeBegan.store (false);
        callsAfterClose.store (0);
        AEffect* e = make (true);
        for (int i = 0; i < 40 && blockedNow.load() == 0; ++i)
            sleepMs (50);
        check (blockedNow.load() > 0, "A: the display thread is inside a host call that does not return");
        closeBegan.store (true);
        std::atomic<bool> done { false };
        const double t0 = nowSec();
        std::thread closer ([&] { e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f); done.store (true); });
        while (! done.load() && nowSec() - t0 < 8.0)
            sleepMs (10);
        const double took = nowSec() - t0;
        std::printf ("INFO effClose took %.2f s\n", done.load() ? took : -1.0);
        check (done.load() && took < 5.0, "A: effClose returns within the limit although the host call never returns");
        closer.join();
        sleepMs (400);
        check (callsAfterClose.load() == 0, "A: no host call starts after effClose began");
        release.store (true);   // the host call returns now: the abandoned thread ends and frees the instance
        for (int i = 0; i < 100 && blockedNow.load() > 0; ++i)
            sleepMs (20);
        sleepMs (300);
        check (blockedNow.load() == 0 && callsAfterClose.load() == 0, "A: after the host call returned the thread ended without another call");
        blockCalls.store (false);
    }

    std::printf (fails == 0 ? "OK\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
