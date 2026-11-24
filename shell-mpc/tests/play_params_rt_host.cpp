// SynthyBoy - MPC shell test: the lookup of the Play page's real parameters is real-time safe. The host may call getParameter / setParameter from the audio thread; the table behind
// them must already be built when the library is loaded (it used to be built lazily, in a function-local static, by whichever thread came first: about 150 scans of the parameter table
// and the guard lock on the audio thread). Also counts every global operator new from the start of main (the lookup must never allocate).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "play_params.h"

static std::atomic<long> gAllocs { 0 };
static std::atomic<bool> gCount { false };

void* operator new (std::size_t n)
{
    if (gCount.load (std::memory_order_relaxed))
        gAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

int main()
{
    namespace pl = synthyboy_mpc::play;
    int bad = 0;
    if (! pl::mapBuilt()) { std::printf ("FAIL: the table of the real parameters is not built before the first lookup (it would be built by the first caller, possibly the audio thread)\n"); ++bad; }
    gAllocs = 0;
    gCount = true;
    int found = 0;
    for (int g = 0; g < pl::kGroups; ++g)
        for (int n = 0; n <= pl::kMaxInstances + 1; ++n)
            for (int s = 0; s < pl::kSlots; ++s)
                found += pl::realIndex (g, n, s) >= 0;
    int units = 0, beatTargets = 0;
    for (int n = 0; n <= pl::kInstances[1] + 1; ++n)
    {
        units += pl::envUnitsIndex (n) >= 0;
        for (int s = -1; s <= pl::kSlots; ++s)
            beatTargets += pl::envBeatsIndex (n, s) >= 0;
    }
    gCount = false;
    if (gAllocs.load() != 0) { std::printf ("FAIL: lookup allocated %ld times on its first calls (the audio thread must never allocate)\n", gAllocs.load()); ++bad; }
    // Sub and Noise have four legacy slots plus the two native PAN/LEVEL aliases.
    if (found != 6 + 6 + 8 * (3 + pl::kInstances[1] + pl::kInstances[2])) { std::printf ("FAIL: %d real parameters found\n", found); ++bad; }
    if (units != pl::kInstances[1] || beatTargets != 3 * pl::kInstances[1]) { std::printf ("FAIL: %d ENV units parameters, %d beat targets\n", units, beatTargets); ++bad; }
    std::string why;
    if (! pl::uniform (&why)) { std::printf ("FAIL: not uniform: %s\n", why.c_str()); ++bad; }
    std::printf (bad ? "FAILED\n" : "OK\n");
    return bad ? 1 : 0;
}
