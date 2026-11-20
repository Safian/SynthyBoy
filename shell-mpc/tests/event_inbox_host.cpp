// SynthyBoy - MPC shell: acceptance test of the event hand-over (event_inbox.h): one producer thread and the consumer (the audio thread) running at the same time lose and duplicate nothing,
// keep the order and the offsets; the reserve for note-offs; several producers.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "event_inbox.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

struct Ev { int seq = 0; int frame = 0; bool off = false; };

// lock-freedom probe: the first copy of an event marked `stall` (the copy into the ring) parks the copying thread (up to 200 ms) until the second producer has finished its push.
static std::atomic<bool> p1InCopy { false }, p2Done { false }, p2DoneWhileStalled { false }, stallUsed { false };
struct StallingEv
{
    int seq = 0; bool stall = false;
    StallingEv() = default;
    StallingEv (int s, bool st) : seq (s), stall (st) {}
    StallingEv& operator= (const StallingEv& o)
    {
        if (o.stall && ! stallUsed.exchange (true)) {
            p1InCopy.store (true, std::memory_order_release);
            const auto t0 = std::chrono::steady_clock::now();
            while (! p2Done.load (std::memory_order_acquire) && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (200))
                std::this_thread::yield();
            p2DoneWhileStalled.store (p2Done.load (std::memory_order_acquire), std::memory_order_release);
        }
        seq = o.seq;
        return *this;
    }
};

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }

int main()
{
    using Inbox = synthyboy_mpc::EventInbox<Ev, 1024>;
    {
        // one producer, one consumer, 200000 events: every one arrives once, in order, with its frame
        static Inbox in;
        const int total = 200000;
        std::atomic<bool> done { false };
        std::thread producer ([&] {
            for (int i = 0; i < total;) {
                Ev e; e.seq = i; e.frame = i % 4096; e.off = (i % 3) == 0;
                if (in.push (e, e.off)) ++i; else std::this_thread::yield();   // a full ring: wait (the plugin drops, the test must count every event)
            }
            done.store (true);
        });
        int next = 0; bool ordered = true, frames = true;
        Ev e;
        while (next < total) {
            if (in.pop (e)) {
                ordered = ordered && e.seq == next;
                frames = frames && e.frame == next % 4096 && e.off == ((next % 3) == 0);
                ++next;
            } else if (done.load() && in.size() == 0 && next < total) {
                break;
            }
        }
        producer.join();
        check (next == total && ordered, "one producer: every event arrives once and in order");
        check (frames, "the sample offsets and the kind survive");
        check (in.size() == 0 && ! in.pop (e), "nothing is left over or duplicated");
    }
    {
        // the reserve: when the ring is nearly full only note-offs are accepted
        Inbox in;
        int accepted = 0;
        for (int i = 0; i < 1024; ++i) { Ev e; e.seq = i; if (in.push (e, false)) ++accepted; }
        check (accepted == 1024 - 256, "ordinary events stop 256 slots before the end");
        int offs = 0;
        for (int i = 0; i < 400; ++i) { Ev e; e.off = true; if (in.push (e, true)) ++offs; }
        check (offs == 256, "note-offs still fit into the reserve");
        check (in.droppedCount() == 256u + 144u, "the rest is counted as dropped");
    }
    {
        // two producers, one consumer: all events once
        static Inbox in;
        const int per = 50000;
        std::vector<std::thread> t;
        for (int p = 0; p < 2; ++p)
            t.emplace_back ([&, p] { for (int i = 0; i < per;) { Ev e; e.seq = p * per + i; if (in.push (e, true)) ++i; else std::this_thread::yield(); } });
        std::vector<char> seen ((size_t) (2 * per), 0);
        int got = 0; bool dup = false; Ev e;
        while (got < 2 * per) {
            if (in.pop (e)) { if (seen[(size_t) e.seq]) dup = true; seen[(size_t) e.seq] = 1; ++got; }
        }
        for (auto& th : t) th.join();
        check (got == 2 * per && ! dup, "two producers: no event lost, none twice");
    }
    {
        // lock-freedom: a producer stalled inside its push must not hold up a second producer
        using SInbox = synthyboy_mpc::EventInbox<StallingEv, 1024>;
        static SInbox in;
        double p2Ms = 1.0e9;
        std::thread p1 ([&] { in.push (StallingEv (1, true), true); });
        std::thread p2 ([&] {
            while (! p1InCopy.load (std::memory_order_acquire)) std::this_thread::yield();
            const auto t0 = std::chrono::steady_clock::now();
            in.push (StallingEv (2, false), true);
            p2Ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            p2Done.store (true, std::memory_order_release);
        });
        p1.join(); p2.join();
        check (p2DoneWhileStalled.load(), "lock-free: producer 2 finishes while producer 1 is stalled inside its push");
        check (p2Ms < 50.0, "lock-free: producer 2 needed less than 50 ms");
        StallingEv got; int n = 0, sum = 0;
        while (in.pop (got)) { ++n; sum += got.seq; }
        check (n == 2 && sum == 3, "both stalled-test events arrive");
    }
    {
        // four producers, one consumer: exactly 100000 events, none twice, none lost
        static Inbox in;
        const int per = 25000, np = 4;
        std::vector<std::thread> t;
        for (int p = 0; p < np; ++p)
            t.emplace_back ([&, p] { for (int i = 0; i < per;) { Ev e; e.seq = p * per + i; if (in.push (e, true)) ++i; else std::this_thread::yield(); } });
        std::vector<char> seen ((size_t) (np * per), 0);
        int got = 0; bool dup = false; Ev e;
        while (got < np * per) {
            if (in.pop (e)) { if (seen[(size_t) e.seq]) dup = true; seen[(size_t) e.seq] = 1; ++got; }
        }
        for (auto& th : t) th.join();
        check (got == 100000 && ! dup && ! in.pop (e) && in.size() == 0, "four producers x 25000: exactly 100000 events, no duplicate, no loss");
    }
    std::printf ("%s  (%d failures)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
