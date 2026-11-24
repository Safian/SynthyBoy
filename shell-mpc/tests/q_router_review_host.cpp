// Robustness review of shell-mpc/q_router.h (review of 2026-11-16, docs: .hw-local/mpc-ui-plans/robustness-review.md). These tests FAIL on the current code on purpose: each one reproduces a finding.
//   R1  after a group change the first write of a stepped Q proxy is measured from the host's last write for the OLD target (base_ is never set by a group report), so one detent jumps many steps
//   R2  a group change (display thread) while the host writes a Q proxy (host thread): the router reads the slot's code several times, so the index of one group and the view mode of another get mixed
//       and the write lands on a parameter the user did not mean with a value the view computed (a plain target gets a transpose-quantised value)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "q_router.h"

using synthyboy_mpc::QRouter;

static int fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++fails;
}

constexpr int kParams = 16;
static std::atomic<float> gParam[kParams];
static float rd (int t) noexcept { return gParam[t].load (std::memory_order_relaxed); }
static void wr (int t, float v) { gParam[t].store (v, std::memory_order_relaxed); }
static int steps8 (int) noexcept { return 8; }   // every parameter is a list of 8 entries here

int main()
{
    {   // R1
        for (auto& p : gParam)
            p.store (0.0f);
        QRouter r;
        const int a[4] = { 0, QRouter::kNone, QRouter::kNone, QRouter::kNone };
        r.setGroup (a, 4, kParams);
        r.poll (rd, [] (int, float) {}, [] (int) {}, [] {});          // the host hears Q 1 = 0 (target A at entry 0)
        float host = 0.0f;
        for (int i = 0; i < 12; ++i)                                  // the player turns Q 1 up by 12 host increments (1/128 each): A steps up a few times
        {
            host += 1.0f / 128.0f;
            r.hostWrite (0, host, false, rd, wr, steps8);
            r.poll (rd, [] (int, float) {}, [] (int) {}, [] {});
        }
        const float lastWrite = host;
        std::printf ("INFO  A is at entry %ld after 12 increments, the host's last write was %.4f\n", std::lround (rd (0) * 7.0f), static_cast<double> (lastWrite));
        wr (1, 2.0f / 7.0f);                                          // parameter B stands at entry 2 of 8 (0.286)
        const int b[4] = { 1, QRouter::kNone, QRouter::kNone, QRouter::kNone };
        r.setGroup (b, 4, kParams);                                   // the page moves to the next group: Q 1 now stands for B
        float told = -1.0f;
        r.poll (rd, [&] (int q, float v) { if (q == 0) told = v; }, [] (int) {}, [] {});   // the host is told B's value (the dial jumps to 0.286)
        check (std::fabs (told - 2.0f / 7.0f) < 1e-4f, "the group change reports Q 1 = B's value to the host");
        const float turned = told + 1.0f / 128.0f;                    // ONE detent of the knob from where the dial now is
        r.hostWrite (0, turned, false, rd, wr, steps8);
        const long entryB = std::lround (rd (1) * 7.0f);
        std::printf ("INFO  B is at entry %ld after ONE detent (it was 2; travel per step is 3 detents, so at most 0 or 1 step is right)\n", entryB);
        check (entryB >= 2 && entryB <= 3, "R1: one detent after a group change moves B by at most one entry");
    }

    {   // R2
        for (auto& p : gParam)
            p.store (0.123f);
        QRouter r;
        const int plain[4] = { 5, QRouter::kNone, QRouter::kNone, QRouter::kNone };
        const int oct[4] = { QRouter::encode (7, QRouter::kOct), QRouter::kNone, QRouter::kNone, QRouter::kNone };
        r.setGroup (plain, 4, kParams);
        std::atomic<bool> stop { false };
        std::thread flipper ([&]
        {
            bool p = false;
            while (! stop.load (std::memory_order_relaxed))
            {
                r.setGroup (p ? plain : oct, 4, kParams);   // the display thread / overlay thread: the page changes its group
                p = ! p;
            }
        });
        const float v = 0.37f;
        long wrong = 0, total = 0;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds (3);
        long iter = 0;
        while (std::chrono::steady_clock::now() < end)
        {
            for (int k = 0; k < 20000; ++k, ++iter)
            {
                int wt = -1;
                float wv = -1.0f;
                r.hostWrite (0, v, false, rd, [&] (int t, float nv) { wt = t; wv = nv; });   // the host thread's setParameter on Q 1
                if (wt == 5)
                {
                    ++total;
                    if (wv != v)
                        ++wrong;   // a plain target must get exactly the host's value
                }
            }
        }
        stop.store (true);
        flipper.join();
        std::printf ("INFO  %ld writes made, %ld of them to the plain target (5); %ld got a value other than the host's\n", iter, total, wrong);
        check (wrong == 0, "R2: every write to a plain target carries exactly the host's value, whatever the other thread does to the group");
    }

    std::printf (fails == 0 ? "OK\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
