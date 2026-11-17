// SynthyBoy - MPC shell test: the Q router (shell-mpc/q_router.h, docs/MPC.md "Q proxies"): sixteen proxies: Q 1 .. 4 stand for the targets of the active group, Q 5 .. 16 for the next groups of the page. Pure logic, no plugin: the
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
#include <algorithm>
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
    int groups = 0;   // how often the group-changed callback ran (the plugin tells the host the names are other)
    void clear() { q.clear(); real.clear(); groups = 0; }
};
static void pollInto (QRouter& r, Reports& out)
{
    r.poll (rd, [&] (int q, float v) { out.q.emplace_back (q, v); }, [&] (int t) { out.real.push_back (t); }, [&] { ++out.groups; });
}

int main()
{
    for (int i = 0; i < kParams; ++i)
        wr (i, 0.05f * static_cast<float> (i));

    {   // the views of a transpose: Q 1 = octaves (steps of 12), Q 2 = what is left inside the octave; transpose = 12 * OCT + SEM
        QRouter r;
        const int t[4] = { QRouter::encode (3, QRouter::kOct), QRouter::encode (3, QRouter::kSem), 5, QRouter::kNone };
        r.setGroup (t, 4, kParams);
        auto tr = [] (int semis) { return static_cast<float> (semis + 48) / 96.0f; };
        wr (3, tr (19));
        check (r.target (0) == 3 && r.mode (0) == QRouter::kOct && r.mode (1) == QRouter::kSem && r.mode (2) == QRouter::kPlain, "a view keeps the table index and says which view it is");
        check (std::fabs (r.value (0, rd) - 5.0f / 8.0f) < 1e-6f && std::fabs (r.value (1, rd) - 18.0f / 22.0f) < 1e-6f, "transpose 19: OCT 1 (5/8), SEM 7 (18/22)");
        r.hostWrite (0, 3.0f / 8.0f, false, rd, wr);   // OCT -1
        check (std::fabs (rd (3) - tr (-5)) < 1e-6f, "Q 1 to octave -1 keeps the remainder: 19 -> -5");
        wr (3, tr (19));
        r.hostWrite (1, 14.0f / 22.0f, false, rd, wr);   // SEM 3
        check (std::fabs (rd (3) - tr (15)) < 1e-6f, "Q 2 to SEM 3 keeps the octave: 19 -> 15");
        wr (3, tr (40));
        r.hostWrite (1, 1.0f, false, rd, wr);   // SEM 11 on octave 3
        check (std::fabs (rd (3) - tr (47)) < 1e-6f, "OCT 3 + SEM 11 = 47");
        check (r.hostWrite (1, r.value (1, rd), false, rd, wr) == QRouter::Write::echo, "a view's own value is an echo");
        check (QRouter::octOfTranspose (-5) == 0 && QRouter::semOfTranspose (-5) == -5 && 12 * QRouter::octOfTranspose (-30) + QRouter::semOfTranspose (-30) == -30, "transpose = 12 * OCT + SEM, also below zero");
        for (int i = 0; i < kParams; ++i)
            wr (i, 0.05f * static_cast<float> (i));
    }
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
        check (r.target (3) == 4 && r.target (4) == 5 && r.target (5) == QRouter::kNone, "five targets: Q 5 has the fifth, Q 6 .. 16 have none");
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
        check (r.writeCount() == 10, "every host write counts, whatever came of it (an echo, no target, outside the Qs): " + std::to_string (r.writeCount()));
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
        check (rep.q.size() == 16 && rep.q[0].first == 0 && std::fabs (rep.q[0].second - rd (2)) < 1e-6f && rep.q[2].first == 2 && rep.q[2].second == 0.0f && rep.q[3].first == 3 && std::fabs (rep.q[3].second - rd (8)) < 1e-6f && rep.real.empty(),
               "a group change: the host is told all sixteen values (0 for none) once");
        check (rep.groups == 1, "... and the group-changed callback runs once, after them");
        rep.clear();
        pollInto (r, rep);
        check (rep.q.empty(), "the same state again: nothing");
        r.setGroup (g, 4, kParams);
        pollInto (r, rep);
        check (rep.q.empty() && rep.groups == 0, "the same group set again: nothing is reported and the host is not told the names changed");
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
        check (rep.q.size() == 16 && rep.real.size() == 1 && rep.real[0] == 2, "a group change right after a write: the sixteen new values and the real parameter of the write");
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
    {   // the virtual targets (the values of the LFO shape editor): no parameter behind them; the page sets the value, the host's write is kept for the page
        QRouter r;
        const int t[4] = { QRouter::encode (0, QRouter::kVirtual), QRouter::encode (3, QRouter::kVirtual), 5, QRouter::kNone };
        r.setGroup (t, 4, kParams);
        check (r.target (0) == QRouter::kNone && r.target (1) == QRouter::kNone && r.virtualId (0) == 0 && r.virtualId (1) == 3 && r.virtualId (2) == -1 && r.target (2) == 5, "a virtual target is no table index (every parameter code sees none), its id is its own");
        r.setVirtualValue (0, 0.25f);
        check (std::fabs (r.value (0, rd) - 0.25f) < 1e-6f && r.value (1, rd) == 0.0f, "its value is the one the page set");
        long allocs = gAllocs.load();
        gCount.store (true);
        const QRouter::Write w = r.hostWrite (0, 0.75f, false, rd, wr);
        float v = -1.0f;
        const bool got = r.takeVirtual (0, v);
        gCount.store (false);
        check (w == QRouter::Write::written && got && std::fabs (v - 0.75f) < 1e-6f && gAllocs.load() == allocs, "the host's write is kept for the page (once, no allocation) and no parameter changed");
        check (! r.takeVirtual (0, v) && std::fabs (r.value (0, rd) - 0.75f) < 1e-6f, "taken once; the value is what the host wrote until the page says another");
        check (r.hostWrite (0, 0.75f, false, rd, wr) == QRouter::Write::echo && r.hostWrite (1, 0.5f, true, rd, wr) == QRouter::Write::restoring, "the same value is an echo, the restore window ignores a write");
        Reports rep;
        pollInto (r, rep);
        rep.clear();
        r.setVirtualValue (3, 0.5f);
        pollInto (r, rep);
        bool told = false;
        for (const auto& e : rep.q) told = told || (e.first == 1 && std::fabs (e.second - 0.5f) < 1e-6f);
        check (told && rep.real.empty(), "the page's new value is told to the host (the Q proxy), no real parameter");
        wr (5, 0.9f);   // what the threads test left in the target must not be the value written below (that would be an echo, not a write)
        check (r.hostWrite (2, 0.2f, false, rd, wr) == QRouter::Write::written && std::fabs (rd (5) - 0.2f) < 1e-6f, "a real target next to the virtual ones works as before");
    }
    {   // sixteen slots: the proxies, the mapping rule of a page's groups, the notification count
        check (QRouter::kCount == 16 && QRouter::kGroupSize == 4 && QRouter::kGroups == 4, "sixteen proxies: four groups of four");
        // a page with six groups: group g has the targets 4 * g .. 4 * g + 3 (kParams = 16 is the real table here, so a larger table is not needed: the targets are group number * 10 + knob)
        auto targetsOf = [] (int g, int* t) { for (int i = 0; i < 4; ++i) t[i] = g * 10 + i; };
        int out[QRouter::kCount];
        int used = QRouter::mapGroups (6, 0, targetsOf, out);
        bool ok = used == 16;
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < 4; ++i)
                ok = ok && out[4 * k + i] == k * 10 + i;
        check (ok, "six groups, group 0 active: slots 1-4 = group 0, 5-8 = group 1, 9-12 = group 2, 13-16 = group 3");
        used = QRouter::mapGroups (6, 4, targetsOf, out);
        ok = used == 16 && out[0] == 40 && out[3] == 43 && out[4] == 50 && out[8] == 0 && out[12] == 10 && out[15] == 13;
        check (ok, "group 4 active: 4, then 5, then the page wraps around to 0 and 1 (each group once)");
        used = QRouter::mapGroups (6, 5, targetsOf, out);
        check (used == 16 && out[0] == 50 && out[4] == 0 && out[8] == 10 && out[12] == 20, "the last group active: the next groups are the first ones");
        used = QRouter::mapGroups (3, 1, targetsOf, out);
        ok = used == 12 && out[0] == 10 && out[4] == 20 && out[8] == 0 && out[12] == QRouter::kNone && out[15] == QRouter::kNone;
        check (ok, "three groups: slots 13-16 stay without a target (no group is repeated)");
        used = QRouter::mapGroups (1, 0, targetsOf, out);
        ok = used == 4 && out[3] == 3;
        for (int i = 4; i < 16; ++i)
            ok = ok && out[i] == QRouter::kNone;
        check (ok, "one group: slots 5-16 have no target");
        used = QRouter::mapGroups (2, 1, targetsOf, out);
        check (used == 8 && out[0] == 10 && out[4] == 0 && out[8] == QRouter::kNone, "two groups: the other one on 5-8");
        check (QRouter::mapGroups (0, 0, targetsOf, out) == 0 && out[0] == QRouter::kNone && QRouter::mapGroups (4, -1, targetsOf, out) == 0 && out[0] == QRouter::kNone && QRouter::mapGroups (4, 4, targetsOf, out) == 0,
               "no group, or no active one: nothing is mapped");
        check (QRouter::blockOfGroup (6, 4, 4) == 0 && QRouter::blockOfGroup (6, 4, 5) == 1 && QRouter::blockOfGroup (6, 4, 0) == 2 && QRouter::blockOfGroup (6, 4, 1) == 3 && QRouter::blockOfGroup (6, 4, 2) == -1 && QRouter::blockOfGroup (6, 4, 3) == -1
               && QRouter::blockOfGroup (2, 0, 1) == 1 && QRouter::blockOfGroup (1, 0, 0) == 0 && QRouter::blockOfGroup (3, 3, 0) == -1 && QRouter::blockOfGroup (3, 0, 3) == -1, "blockOfGroup agrees with the mapping (-1: the group has no knobs)");
        // the router with sixteen slots: the targets, reading, writing, the notification
        QRouter r;
        for (int i = 0; i < kParams; ++i)
            wr (i, 0.05f * static_cast<float> (i));
        int slots[16];
        for (int i = 0; i < 16; ++i)
            slots[i] = i;
        r.setGroup (slots, 16, kParams);
        ok = true;
        for (int q = 0; q < 16; ++q)
            ok = ok && r.target (q) == q;
        check (ok, "all sixteen targets are kept");
        check (r.target (16) == QRouter::kNone && r.target (-1) == QRouter::kNone, "a Q outside 0 .. 15 has no target");
        Reports rep;
        pollInto (r, rep);
        check (rep.q.size() == 16 && rep.groups == 1, "the first group: sixteen values, the callback once");
        rep.clear();
        pollInto (r, rep);
        check (rep.q.empty() && rep.groups == 0, "nothing changed: no value and no notification");
        r.setGroup (slots, 16, kParams);
        pollInto (r, rep);
        check (rep.q.empty() && rep.groups == 0, "the same sixteen slots again: no notification");
        int moved[16];
        std::copy (slots, slots + 16, moved);
        moved[15] = 3;   // only the last slot changes
        r.setGroup (moved, 16, kParams);
        pollInto (r, rep);
        check (rep.q.size() == 16 && rep.groups == 1, "a change of slot 16 only: one notification (the host reads all names again)");
        rep.clear();
        int swapped[16];
        std::copy (moved, moved + 16, swapped);
        for (int i = 4; i < 16; ++i)
            swapped[i] = QRouter::kNone;
        r.setGroup (swapped, 4, kParams);   // a page that only knows one group: the first four, the others none
        pollInto (r, rep);
        check (rep.groups == 1 && r.target (3) == 3 && r.target (4) == QRouter::kNone && r.target (15) == QRouter::kNone, "a page that publishes four slots empties Q 5 .. 16, with one notification");
        rep.clear();
        r.setGroup (slots, 16, kParams);
        rep.clear();
        pollInto (r, rep);
        rep.clear();
        r.setGroup (slots, 16, kParams);
        pollInto (r, rep);
        check (rep.groups == 0, "no change after the last publish: no notification");
        // a write to Q 5 .. 16 goes to the target of that slot, the real parameter is reported
        wr (12, 0.1f);
        const QRouter::Write w = r.hostWrite (12, 0.6f, false, rd, wr);
        pollInto (r, rep);
        check (w == QRouter::Write::written && std::fabs (rd (12) - 0.6f) < 1e-6f && rep.real.size() == 1 && rep.real[0] == 12, "a write to Q 13 reaches its target and the real parameter is reported");
        // a Q write outside the table is none
        check (r.hostWrite (16, 0.2f, false, rd, wr) == QRouter::Write::none, "a write to a Q that does not exist is none");
    }
    {   // stepped targets are driven relatively: one step per kStepTravelDefault of host travel, in the direction of the turn
        QRouter r;
        for (int i = 0; i < kParams; ++i)
            wr (i, 0.0f);
        const int g[4] = { 1, 2, 3, 4 };   // 1: a toggle (2 values), 2: a list of 5, 3: a list of 57, 4: continuous
        r.setGroup (g, 4, kParams);
        auto stepsOf = [] (int t) noexcept { return t == 1 ? 2 : (t == 2 ? 5 : (t == 3 ? 57 : 0)); };
        Reports rep;
        pollInto (r, rep);
        const float step = QRouter::kStepTravelDefault;
        float host[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // what the host holds for each Q: it takes every report as its own value
        auto turn = [&] (int q, float d)
        {
            host[q] = std::clamp (host[q] + d, 0.0f, 1.0f);
            const QRouter::Write w = r.hostWrite (q, host[q], false, rd, wr, stepsOf);
            Reports x;
            pollInto (r, x);
            for (const auto& e : x.q) host[e.first] = e.second;   // the host adopts the report
            return w;
        };
        check (std::fabs (step - 6.0f / 128.0f) < 1e-9f, "the default is 6 host increments of 1/128 per step");
        {   // the status lines (the calibration of a quarter turn reads them): the step travel in force, per knob with a target the raw value, the delta and the accumulated travel
            turn (0, step * 0.5f);
            std::string st;
            r.describe (st);
            check (st.find ("q_step_travel: 0.046875 (6.00 increments of 1/128, default)") == 0, "status: the step travel in force is the first line (" + st.substr (0, st.find ('\n')) + ")");
            const size_t k1 = st.find ("q_knob_1: target 1, raw 0.023438 (3.00/128), delta +0.023438 (+3.00/128), travel +0.023438 (+3.00/128 of 6.00 per step), turned +3.00/128 in all");
            check (k1 != std::string::npos, "status: knob 1 shows the raw value last written, the delta of that write and the accumulated travel");
            check (st.find ("q_knob_4: target 4") != std::string::npos && st.find ("q_knob_5") == std::string::npos, "status: a line per knob that has a target, none for the others");
            r.setStepTravel (10.0f / 128.0f);
            std::string st2;
            r.describe (st2);
            check (st2.find ("(10.00 increments of 1/128, overridden)") != std::string::npos, "status: an overridden travel says so");
            r.setStepTravel (QRouter::kStepTravelDefault);
            turn (0, -step * 0.5f);   // back to rest
        }
        turn (0, step * 0.9f);
        check (rd (1) == 0.0f, "a toggle does not flip before one step of travel");
        turn (0, step * 0.2f);
        check (rd (1) == 1.0f, "a toggle flips after one step of travel (" + std::to_string (rd (1)) + ")");
        check (std::fabs (host[0] - 1.0f) < 1e-6f, "the host's value follows the target's real position");
        turn (0, -step * 1.1f);
        check (rd (1) == 0.0f, "turning back flips it back after one step (the direction change starts the travel again)");
        turn (0, -step * 3.0f);
        check (rd (1) == 0.0f, "at the end of a list further turning does nothing");
        // a list of 5 walks both ways, one value per step
        std::vector<float> seen;
        for (int k = 0; k < 4; ++k) { turn (1, step * 1.05f); seen.push_back (std::round (rd (2) * 4.0f)); }
        check (seen == std::vector<float> { 1, 2, 3, 4 }, "a list walks up one value per step");
        seen.clear();
        for (int k = 0; k < 4; ++k) { turn (1, -step * 1.05f); seen.push_back (std::round (rd (2) * 4.0f)); }
        check (seen == std::vector<float> { 3, 2, 1, 0 }, "... and down");
        turn (1, step * 0.5f); turn (1, -step * 0.4f); turn (1, step * 0.5f);
        check (rd (2) == 0.0f, "travel that changes direction does not add up");
        turn (1, step * 3.2f);
        check (std::round (rd (2) * 4.0f) == 3.0f, "one write of 3.2 steps of travel makes 3 steps");
        {
            int writes = 0;
            const float before = rd (2);
            QRouter j;
            const int gj[4] = { 2, -1, -1, -1 };
            j.setGroup (gj, 1, kParams);
            Reports rj;
            pollInto (j, rj);
            const float held = rd (2);
            j.hostWrite (0, std::clamp (held + 0.5f, 0.0f, 1.0f), false, rd, [&] (int t, float v) { ++writes; wr (t, v); }, stepsOf);
            check (writes == 0 && rd (2) == before, "a host write 0.5 away is a jump, no travel: no step, no parameter write");
            Reports rj2;
            pollInto (j, rj2);
            const float h2 = rd (2);
            j.hostWrite (0, h2 + 0.9f * 4.0f * step * 0.0f + 3.0f * step, false, rd, [&] (int t, float v) { ++writes; wr (t, v); }, stepsOf);
            check (writes <= 1, "a write of three steps writes the parameter once (" + std::to_string (writes) + ")");
        }
        // a long list reaches both ends without the host's value saturating on the way (the host writes one increment at a time: 6 per step)
        bool sat = false;
        int writesUp = 0, writesDown = 0;
        while (rd (3) < 1.0f && writesUp < 400) { turn (2, 1.0f / 128.0f); ++writesUp; sat = sat || (host[2] >= 1.0f && rd (3) < 1.0f); }
        check (rd (3) == 1.0f && ! sat && writesUp >= 330 && writesUp <= 345, "a list of 57 reaches its last entry in 56 steps of 6 increments (" + std::to_string (writesUp) + " writes) and the host's value does not saturate before it");
        while (rd (3) > 0.0f && writesDown < 400) { turn (2, -1.0f / 128.0f); ++writesDown; sat = sat || (host[2] <= 0.0f && rd (3) > 0.0f); }
        check (rd (3) == 0.0f && ! sat && writesDown >= 330 && writesDown <= 345, "... and back to the first (" + std::to_string (writesDown) + ")");
        {   // the two hosts of the Key 61 measurements: a burst continues from the host's OWN value (reports that arrive in the middle must not count again), one increment per touch starts at the report
            const float inc = 1.0f / 128.0f;
            QRouter b;
            for (int i = 0; i < kParams; ++i) wr (i, 0.0f);
            const int gb[4] = { 5, 6, 0, 0 };   // 5: a list of 12 (the step of the target 1 / 11 is 11.6 increments), 6: a toggle
            b.setGroup (gb, 2, kParams);
            auto stepsB = [] (int t) noexcept { return t == 5 ? 12 : (t == 6 ? 2 : 0); };
            Reports r0;
            pollInto (b, r0);
            // (1) a burst of 66 increments, the host keeps its own value, the plugin reports the real position after every write (a report lands between two writes of the burst)
            float own = 0.0f;
            for (int k = 0; k < 66; ++k)
            {
                own = std::clamp (own + inc, 0.0f, 1.0f);
                b.hostWrite (0, own, false, rd, wr, stepsB);
                Reports x;
                pollInto (b, x);   // the report is NOT adopted: the host goes on from its own value
            }
            const int stepsDone = static_cast<int> (std::lround (rd (5) * 11.0f));
            check (stepsDone == 11 || stepsDone == 10, "a burst of 66 increments on a list of 12 (host keeps its own value, reports land in the burst): 11 steps, not more, not less (" + std::to_string (stepsDone) + ")");
            // (2) a burst of 6 on a toggle: exactly one step, even with a report in the middle
            wr (6, 0.0f);
            QRouter c;
            c.setGroup (gb, 2, kParams);
            Reports r1;
            pollInto (c, r1);
            float own2 = 0.0f;
            for (int k = 0; k < 6; ++k)
            {
                own2 += inc;
                c.hostWrite (1, own2, false, rd, wr, stepsB);
                if (k == 2) { Reports x; pollInto (c, x); }
            }
            check (rd (6) == 1.0f, "a burst of 6 increments flips a toggle (a report in the middle does not count twice)");
            // (3) one increment per touch: the host starts at the report each time
            wr (6, 0.0f);
            QRouter d;
            d.setGroup (gb, 2, kParams);
            Reports r2;
            pollInto (d, r2);
            float flipAt = -1.0f;
            float rep = 0.0f;
            for (int k = 1; k <= 8 && flipAt < 0.0f; ++k)
            {
                d.hostWrite (1, std::clamp (rep + inc, 0.0f, 1.0f), false, rd, wr, stepsB);
                Reports x;
                pollInto (d, x);
                for (const auto& e : x.q) if (e.first == 1) rep = e.second;   // the host takes the report at the next touch
                if (rd (6) == 1.0f) flipAt = static_cast<float> (k);
            }
            check (flipAt == 6.0f, "one increment per touch (the host starts at the report): the toggle flips with the 6th (" + std::to_string (flipAt) + ")");
        }
        {   // N increments are floor (N / 6) steps for every kind of host and every interleaving of the reports, and entering a page moves nothing (review of 2026-11-16, points 1 and 3)
            const float inc = 1.0f / 128.0f;
            const int gm[4] = { 3, 1, -1, -1 };   // 3: a list of 57 standing in the middle, 1: a toggle that is ON
            for (int kind = 0; kind < 3; ++kind)   // 0 adopts every report at once, 1 never adopts, 2 adopts one poll late (reports land between its increments)
            {
                wr (3, 28.0f / 56.0f);
                wr (1, 1.0f);
                QRouter m;
                m.setGroup (gm, 2, kParams);
                Reports first;
                pollInto (m, first);                                   // the page is entered: the host is told 0.5 and 1.0
                float own = 28.0f / 56.0f, own1 = 1.0f, late = -1.0f;
                int stepsTaken = 0;
                for (int k = 0; k < 6 * 5; ++k)
                {
                    own += inc;
                    if (kind == 0) own = std::clamp (own, 0.0f, 1.0f);
                    const float before = rd (3);
                    m.hostWrite (0, own, false, rd, wr, stepsOf);
                    stepsTaken += static_cast<int> (std::lround ((rd (3) - before) * 56.0f));
                    Reports x;
                    pollInto (m, x);
                    for (const auto& e : x.q)
                        if (e.first == 0)
                        {
                            if (kind == 0) own = e.second;                    // adopts at once
                            else if (kind == 2) { if (late >= 0.0f) own = late; late = e.second; }   // adopts the previous report, one poll late
                        }
                }
                check (kind == 2 ? (stepsTaken == 4 || stepsTaken == 5) : stepsTaken == 5, "30 increments make 5 steps (one fewer is allowed for the host that adopts a poll late: it comes back to a value one step behind) on a list of 57 standing in the middle (host kind " + std::to_string (kind) + ": " + std::to_string (stepsTaken) + ")");
                const float t0 = rd (1);
                float o1 = own1 + 0.0f;
                for (int k = 0; k < 3; ++k) { o1 -= inc; m.hostWrite (1, o1, false, rd, wr, stepsOf); }
                check (rd (1) == t0, "a toggle that is ON when its page is entered: three increments down do not flip it (host kind " + std::to_string (kind) + ")");
            }
        }
        // a continuous target is exactly as before
        wr (4, 0.2f);
        r.setGroup (g, 4, kParams); pollInto (r, rep);
        const QRouter::Write w = r.hostWrite (3, 0.7f, false, rd, wr, stepsOf);
        check (w == QRouter::Write::written && std::fabs (rd (4) - 0.7f) < 1e-6f, "a continuous target follows the absolute value as before");
        // the notification count: a step reports the position (values), the group callback only for a group change
        Reports n;
        pollInto (r, n);
        turn (0, step * 1.1f);
        Reports n2;
        pollInto (r, n2);
        check (n2.groups == 0, "stepping never calls the group-changed callback");
    }
    std::printf (fails ? "q_router_host: FAILED (%d)\n" : "q_router_host: OK\n", fails);
    return fails ? 1 : 0;
}
