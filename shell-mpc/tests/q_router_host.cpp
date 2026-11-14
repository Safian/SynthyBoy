// SynthyBoy - MPC shell test: the Q router (shell-mpc/q_router.h, docs/MPC.md "Q proxies"): four proxies that stand for the target of the active group. Pure logic, no plugin: the
// parameters are a small array here. Checks the group (up to four targets, "none", out of range), reading, writing (echo, restore window, clamping, NaN), what the display thread reports
// (a group change: all four values once; a changed target: that Q once; a host write: the real parameter, not the Q itself), no allocation on the getParameter / setParameter paths, and
// threads writing, reading, changing the group and polling at the same time (run it under ThreadSanitizer too).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "q_router.h"

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

using synthyboy_mpc::QRouter;

static int fails = 0;
static void check (bool ok, const std::string& what)
{
    if (! ok || std::getenv ("Q_ROUTER_VERBOSE"))
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++fails;
}

constexpr int kParams = 16;
static std::atomic<float> gParam[kParams];
static float rd (int t) noexcept { return gParam[t].load (std::memory_order_relaxed); }
static void wr (int t, float v) { gParam[t].store (v, std::memory_order_relaxed); }

struct Reports
{
    std::vector<std::pair<int, float>> q;
    std::vector<int> real;
    void clear() { q.clear(); real.clear(); }
};
static void pollInto (QRouter& r, Reports& out)
{
    r.poll (rd, [&] (int q, float v) { out.q.emplace_back (q, v); }, [&] (int t) { out.real.push_back (t); });
}

int main()
{
    for (int i = 0; i < kParams; ++i)
        wr (i, 0.05f * static_cast<float> (i));

    {
        QRouter r;
        bool allNone = true;
        for (int q = 0; q < 4; ++q)
            allNone = allNone && r.target (q) == QRouter::kNone && r.value (q, rd) == 0.0f;
        check (allNone && r.target (-1) == QRouter::kNone && r.target (4) == QRouter::kNone, "a new router has no targets: the Q proxies read 0");
        int written = 0;
        check (r.hostWrite (0, 0.5f, false, rd, [&] (int, float) { ++written; }) == QRouter::Write::none && written == 0, "a write to a Q without a target does nothing");
    }
    {
        QRouter r;
        const int g[4] = { 3, QRouter::kNone, 9, 15 };
        r.setGroup (g, 4, kParams);
        check (r.target (0) == 3 && r.target (1) == QRouter::kNone && r.target (2) == 9 && r.target (3) == 15, "setGroup: targets in order, 'none' stays none");
        check (std::fabs (r.value (0, rd) - 0.15f) < 1e-6f && r.value (1, rd) == 0.0f && std::fabs (r.value (2, rd) - 0.45f) < 1e-6f, "reading Q n is the normalised value of its target (0 for none)");
        const int fewer[2] = { 5, 6 };
        r.setGroup (fewer, 2, kParams);
        check (r.target (0) == 5 && r.target (1) == 6 && r.target (2) == QRouter::kNone && r.target (3) == QRouter::kNone, "a group of two: the other Qs have none");
        const int bad[4] = { -5, kParams, kParams + 100, 2 };
        r.setGroup (bad, 4, kParams);
        check (r.target (0) == QRouter::kNone && r.target (1) == QRouter::kNone && r.target (2) == QRouter::kNone && r.target (3) == 2, "a target outside 0 .. paramCount - 1 is none");
        r.setGroup (nullptr, 4, kParams);
        check (r.target (3) == QRouter::kNone, "no group (null): all none");
        const int five[5] = { 1, 2, 3, 4, 5 };
        r.setGroup (five, 5, kParams);
        check (r.target (3) == 4 && r.target (4) == QRouter::kNone, "more than four targets: the first four");
    }
    {
        QRouter r;
        const int g[4] = { 2, 4, QRouter::kNone, 8 };
        r.setGroup (g, 4, kParams);
        std::vector<std::pair<int, float>> writes;
        auto w = [&] (int t, float v) { writes.emplace_back (t, v); wr (t, v); };
        check (r.hostWrite (1, 0.8f, false, rd, w) == QRouter::Write::written && writes.size() == 1 && writes[0].first == 4 && writes[0].second == 0.8f && rd (4) == 0.8f, "setParameter on Q 2 writes the target (4) with the value");
        check (r.hostWrite (1, 0.8f, false, rd, w) == QRouter::Write::echo && writes.size() == 1, "the value the target has is an echo: nothing is written");
        check (r.hostWrite (1, 0.8f + 5e-7f, false, rd, w) == QRouter::Write::echo && writes.size() == 1, "... within one millionth");
        check (r.hostWrite (0, 0.9f, true, rd, w) == QRouter::Write::restoring && writes.size() == 1 && rd (2) == 0.1f, "inside the restore window the write is ignored (the chunk wins)");
        check (r.hostWrite (2, 0.9f, false, rd, w) == QRouter::Write::none && writes.size() == 1, "a Q with no target: nothing");
        check (r.hostWrite (0, 7.0f, false, rd, w) == QRouter::Write::written && rd (2) == 1.0f && r.hostWrite (0, -3.0f, false, rd, w) == QRouter::Write::written && rd (2) == 0.0f, "a value outside 0 .. 1 is limited");
        check (r.hostWrite (0, std::nanf (""), false, rd, w) == QRouter::Write::echo && rd (2) == 0.0f, "NaN is taken as 0 (the target already has it)");
        check (r.hostWrite (7, 0.5f, false, rd, w) == QRouter::Write::none && r.hostWrite (-1, 0.5f, false, rd, w) == QRouter::Write::none, "a Q outside 0 .. 3: nothing");
    }
    {
        // what the display thread reports
        QRouter r;
        Reports rep;
        pollInto (r, rep);
        check (rep.q.empty() && rep.real.empty(), "nothing to report at the start");
        const int g[4] = { 2, 4, QRouter::kNone, 8 };
        r.setGroup (g, 4, kParams);
        pollInto (r, rep);
        check (rep.q.size() == 4 && rep.q[0].first == 0 && std::fabs (rep.q[0].second - rd (2)) < 1e-6f && rep.q[2].first == 2 && rep.q[2].second == 0.0f && rep.q[3].first == 3 && std::fabs (rep.q[3].second - rd (8)) < 1e-6f && rep.real.empty(),
               "a group change: the host is told all four values (0 for none) once");
        rep.clear();
        pollInto (r, rep);
        check (rep.q.empty(), "the same state again: nothing");
        r.setGroup (g, 4, kParams);
        pollInto (r, rep);
        check (rep.q.empty(), "the same group set again: nothing is reported");
        wr (4, 0.77f);   // the target changes by another route (its own page, a preset, the touch screen)
        pollInto (r, rep);
        check (rep.q.size() == 1 && rep.q[0].first == 1 && rep.q[0].second == 0.77f, "a target that changes elsewhere: that Q is reported once");
        rep.clear();
        r.hostWrite (3, 0.31f, false, rd, [] (int t, float v) { wr (t, v); });
        pollInto (r, rep);
        check (rep.q.empty() && rep.real.size() == 1 && rep.real[0] == 8, "a host write of Q 4: the REAL parameter is reported (its own page follows), the Q is not reported back to the host that wrote it");
        rep.clear();
        pollInto (r, rep);
        check (rep.q.empty() && rep.real.empty(), "... once");
        // a new group while a host write is pending
        r.hostWrite (0, 0.6f, false, rd, [] (int t, float v) { wr (t, v); });
        const int g2[4] = { 10, 11, 12, 13 };
        r.setGroup (g2, 4, kParams);
        pollInto (r, rep);
        check (rep.q.size() == 4 && rep.real.size() == 1 && rep.real[0] == 2, "a group change right after a write: the four new values and the real parameter of the write");
        // the host that echoes every report back changes nothing and starts no loop
        rep.clear();
        int writes = 0;
        for (const auto& q : std::vector<std::pair<int, float>> { { 0, rd (10) }, { 1, rd (11) }, { 2, rd (12) }, { 3, rd (13) } })
            r.hostWrite (q.first, q.second, false, rd, [&] (int, float) { ++writes; });
        pollInto (r, rep);
        check (writes == 0 && rep.q.empty() && rep.real.empty(), "a host that sets every reported value back: no write, no new report");
    }
    {
        // real-time rule: the getParameter / setParameter paths never allocate
        QRouter r;
        const int g[4] = { 1, 2, 3, 4 };
        gAllocs = 0;
        gCount = true;
        r.setGroup (g, 4, kParams);
        float sum = 0.0f;
        for (int i = 0; i < 1000; ++i)
        {
            sum += r.value (i & 3, rd);
            r.hostWrite (i & 3, 0.001f * static_cast<float> (i % 900), (i & 7) == 0, rd, [] (int t, float v) { wr (t, v); });
        }
        gCount = false;
        check (gAllocs.load() == 0 && sum >= 0.0f, "value, hostWrite and setGroup do not allocate");
        check (std::atomic<int>::is_always_lock_free && std::atomic<float>::is_always_lock_free, "the atomics are lock free");
    }
    {
        // threads: the audio thread (value / hostWrite), the UI (setGroup), the display thread (poll) at the same time
        QRouter r;
        std::atomic<bool> stop { false };
        std::atomic<long> reported { 0 };
        std::thread ui ([&] { int k = 0; while (! stop.load()) { const int g[4] = { k % 16, (k + 3) % 16, (k % 5 == 0) ? QRouter::kNone : (k + 7) % 16, (k + 11) % 16 }; r.setGroup (g, 4, kParams); ++k; std::this_thread::yield(); } });
        std::thread audio ([&] { int k = 0; while (! stop.load()) { r.value (k & 3, rd); r.hostWrite (k & 3, static_cast<float> (k % 100) / 100.0f, (k % 31) == 0, rd, [] (int t, float v) { wr (t, v); }); ++k; } });
        std::thread display ([&] { while (! stop.load()) { r.poll (rd, [&] (int, float v) { if (! (v >= 0.0f && v <= 1.0f)) reported.store (-1000000); else ++reported; }, [&] (int t) { if (t < 0 || t >= kParams) reported.store (-1000000); }); std::this_thread::yield(); } });
        std::this_thread::sleep_for (std::chrono::milliseconds (400));
        stop.store (true);
        ui.join(); audio.join(); display.join();
        check (reported.load() > 0, "threads: group changes, writes, reads and reports at the same time: every value reported is in range and every real index is a table index (" + std::to_string (reported.load()) + " reports)");
    }
    std::printf (fails ? "q_router_host: FAILED (%d)\n" : "q_router_host: OK\n", fails);
    return fails ? 1 : 0;
}
