// SynthyBoy - host tests for gpu/guard_state.h: after a page guard plane-off the helper stays hidden until a fresh positive page detection.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "../gpu/guard_state.h"

#include <cstdio>
#include <cstdlib>

using namespace synthyboy_mpc::overlay::gpu;

static int g_fail = 0;
static void check (bool ok, const char* what)
{
    if (! ok)
    {
        std::fprintf (stderr, "FAIL: %s\n", what);
        ++g_fail;
    }
}

/** One helper loop pass: true when it presents (a scene is waiting and the gate lets it through). */
static bool helperPass (GuardGate& g, bool dirty, int viewIndex)
{
    if (! dirty || g.blocks (viewIndex))
        return false;
    g.release();
    return true;
}

static void testRetry()
{
    GuardRetry r;
    check (! r.due (false, 0, 0.0) && r.attempts == 0, "retry: nothing pending, nothing due");
    check (r.limit (100.0, false, 0) == 100.0 && r.wakeups == 0, "retry: at rest the wait is untouched, no wake-up counted");
    check (r.due (true, 1, 0.0) && ! r.due (true, 1, 10.0) && r.due (true, 1, 16.5), "retry: due at the start, not within 16 ms, due again after it");
    check (r.limit (100.0, true, 1) == GuardRetry::kEveryMs && r.wakeups == 1 && r.limit (5.0, true, 1) == 5.0 && r.wakeups == 1, "retry: a longer wait is cut to one vblank (counted), a shorter one stays");
    check (! r.due (false, 1, 40.0) && r.attempts == 0, "retry: a confirmation (not pending) stops and resets");
    GuardRetry b;
    double t = 0.0;
    int n = 0;
    for (int i = 0; i < 1000; ++i, t += 16.5)
        n += b.due (true, 7, t);
    check (n == GuardRetry::kMaxAttempts && ! b.active (true, 7) && b.limit (100.0, true, 7) == 100.0, "retry: bounded at kMaxAttempts, then the regular cycle alone");
    check (b.active (true, 8) && b.due (true, 8, t + 20.0) && b.attempts == 1, "retry: a new epoch starts the count again");
}

int main()
{
    testRetry();
    { GuardGate g; check (helperPass (g, true, 3), "no guard: presents"); }

    // the guard fires, the detector sees no known page (Load dialog): no present, however many scenes and ticks
    {
        GuardGate g;
        GuardConfirm c;
        c.onGuard (g.onGuard());
        uint32_t ep = 0;
        for (int tick = 0; tick < 20; ++tick)
        {
            check (! c.take (-1, ep), "no page detected: no confirmation sent");
            check (! helperPass (g, true, 3), "Load dialog: helper must not present");
        }
        check (g.hidden, "still hidden");
    }

    // page change to our page B: the guard fires, the markers of B are read: one confirmation, the helper presents
    {
        GuardGate g;
        GuardConfirm c;
        c.onGuard (g.onGuard());
        check (! helperPass (g, true, 5), "before the detection: hidden");
        uint32_t ep = 0;
        check (c.take (5, ep), "positive detection of B sends the confirmation");
        g.onPageOk (ep, 5);   // it may reach the helper before the show of B: the view is compared at the present
        check (! helperPass (g, true, 4), "confirmation for B does not release a picture of another view");
        check (helperPass (g, true, 5), "B confirmed: presents");
        check (! g.hidden, "released");
        check (! c.take (5, ep), "one confirmation per guard");
    }

    // a confirmation of an older guard is ignored
    {
        GuardGate g;
        GuardConfirm c;
        c.onGuard (g.onGuard());
        uint32_t old = 0;
        check (c.take (2, old), "first guard confirmed by the plugin");
        const uint32_t second = g.onGuard();   // a second guard before the answer was read
        g.onPageOk (old, 2);
        check (! helperPass (g, true, 2), "answer to the first guard does not release the second");
        c.onGuard (second);
        uint32_t ep = 0;
        check (c.take (2, ep) && ep == second, "second guard confirmed");
        g.onPageOk (ep, 2);
        check (helperPass (g, true, 2), "second guard released");
    }

    { GuardGate g; g.onGuard(); g.release(); check (helperPass (g, true, 1), "after a hide the gate is open"); }

    // g134: how long the helper's loop sleeps when there is nothing to draw
    {
        using LT = LoopTimeout;
        check (LT::idleWaitMs (0.0, 0.0, true, false) == 3000.0, "rest, a picture on the plane: the sleep is the verify period (3 s), not 1 s");
        check (LT::idleWaitMs (1000.0, 0.0, true, false) == 2000.0, "... shortened to the time until the check is due");
        check (LT::idleWaitMs (3000.0, 0.0, true, false) == 1.0 && LT::idleWaitMs (50000.0, 0.0, true, false) == 1.0, "... never below 1 ms (a check that is due is made at once, no zero-wait spin)");
        check (LT::idleWaitMs (1000.4, 0.0, true, false) == 2000.0, "... rounded up, so the wake finds the check due");
        check (LT::idleWaitMs (0.0, 0.0, false, false) == LT::kQuietMs && LT::kQuietMs > LT::kVerifyMs, "nothing on the plane: only the signal bound");
        check (LT::idleWaitMs (0.0, 0.0, true, true) == 1000.0 && LT::idleWaitMs (0.0, 0.0, false, true) == 1000.0, "debug dump / bench / a guard flag nobody waits for: the old 1 s poll");
        // a simulated minute at rest: the loop wakes only when the sleep ends
        double t = 0.0, last = 0.0;
        int wakes = 0;
        LoopWakes w;
        while (t < 60000.0)
        {
            t += LT::idleWaitMs (t, last, true, false);
            ++wakes;
            const bool due = t - last >= LT::kVerifyMs;
            if (due)
                last = t;
            w.count (0, due);
        }
        check (wakes <= 20 && w.wakes == static_cast<uint32_t> (wakes) && w.idle == 0u, "a minute at rest: at most 20 wake-ups (was 60), each one a check that was due (none idle)");
        LoopWakes c;
        c.count (1, true);
        c.count (0, false);
        c.count (0, true);
        check (c.wakes == 3u && c.idle == 1u, "LoopWakes: every return counts, only a timeout without a duty is idle");
    }

    if (g_fail != 0)
        return EXIT_FAILURE;
    std::puts ("guard_state_host: all passed");
    return 0;
}
