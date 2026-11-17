// The developer log on the audio thread (shell-mpc/kit/log.h, MPCKIT_DEV_LOG 1): MPCKIT_LOG from the calling thread does not allocate, takes no lock and does no I/O. The global operator new of this executable
// is replaced by a counting one; the counter is read around the logging calls only.
// Usage: log_rt_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#define MPCKIT_DEV_LOG 1
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <unistd.h>

#include "kit/log.h"

static std::atomic<unsigned long> g_allocs { 0 };
static std::atomic<bool> g_count { false };
void* operator new (std::size_t n)
{
    if (g_count.load (std::memory_order_relaxed))
        g_allocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n != 0 ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

int main()
{
    using namespace mpckit::devlog;
    const std::string base = "/tmp/sb-log-rt-" + std::to_string (static_cast<long> (getpid()));
    Settings s;
    s.logPath = base + ".log"; s.confPath = base + ".conf"; s.capBytes = 1 << 20; s.thread = false;
    if (FILE* f = std::fopen (s.confPath.c_str(), "wb")) { std::fputs ("all=debug\n", f); std::fclose (f); }
    Logger& lg = logger();
    lg.start (s);   // may allocate: the plugin's constructor

    int fails = 0;
    g_allocs.store (0);
    g_count.store (true);
    for (int i = 0; i < 500; ++i)
    {
        MPCKIT_LOG ("audio", debug, "block %d of %s: %.3f ms, peak %f", i, "name", 0.25 * i, 0.5);
        MPCKIT_LOG ("audio", trace, "not wanted at trace %d", i);   // switched off by level: no formatting at all
    }
    { MPCKIT_SCOPE ("audio", "scope"); }
    g_count.store (false);
    if (g_allocs.load() != 0)
    {
        std::printf ("FAIL MPCKIT_LOG allocated %lu times on the calling thread\n", g_allocs.load());
        ++fails;
    }
    lg.drainNow();
    lg.stop();
    std::remove (s.logPath.c_str());
    std::remove (s.confPath.c_str());
    if (fails == 0)
        std::printf ("log_rt_host: all passed\n");
    return fails == 0 ? 0 : 1;
}
