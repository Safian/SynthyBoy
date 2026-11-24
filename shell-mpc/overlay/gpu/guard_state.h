// SynthyBoy - MPC overlay: "hidden until a fresh positive page detection" after a page guard plane-off (docs/MPC.md, Main layer page guard).
// The commit shim turns our plane off with a host commit whose markers do not match our page and sets LayerBlock::guard_plane_off. The helper does not look at the host's picture; the plugin's page
// detector does. So after the flag the helper stays hidden (no render, no SETPLANE) until the plugin says: a detection made AFTER the helper reported the guard found one of our views on the current
// host framebuffer. The handshake carries an epoch (no clocks): helper -> plugin kMsgGuard(epoch); plugin -> helper kMsgPageOk(epoch, view) on the first positive detection after it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>

namespace synthyboy_mpc { namespace overlay { namespace gpu {

/** The helper's side. */
struct GuardGate
{
    bool hidden = false;
    uint32_t epoch = 0;
    int confirmedView = -1;

    /** The helper read guard_plane_off while it shows something. Returns the epoch to report to the plugin. */
    uint32_t onGuard() noexcept
    {
        hidden = true;
        ++epoch;
        confirmedView = -1;
        return epoch;
    }
    /** The plugin's answer: a detection after guard `ep` found `view`. An answer to an older guard is ignored. */
    void onPageOk (uint32_t ep, int view) noexcept
    {
        if (hidden && ep == epoch)
            confirmedView = view;
    }
    /** True: a picture of `viewIndex` (the view the helper was told to show) must not be presented now. */
    bool blocks (int viewIndex) const noexcept { return hidden && ! (viewIndex >= 0 && confirmedView == viewIndex); }
    void release() noexcept { hidden = false; confirmedView = -1; }
};

/** The plugin's side: a guard report waits for the first positive detection after it. */
struct GuardConfirm
{
    bool pending = false;
    uint32_t epoch = 0;

    void onGuard (uint32_t ep) noexcept { pending = true; epoch = ep; }
    /** `detectedView`: the view found on the current host framebuffer by a detection run now (-1: none). True: send kMsgPageOk(ep, detectedView). */
    bool take (int detectedView, uint32_t& ep) noexcept
    {
        if (! pending || detectedView < 0)
            return false;
        pending = false;
        ep = epoch;
        return true;
    }
    void clear() noexcept { pending = false; }
};

/** The retry of the page detection while a guard is pending (g132). After a guard plane-off the helper is hidden and waits for the plugin's detection; the immediate one (on kMsgGuard) often finds nothing yet (the host's new
    page is not on the scanned-out framebuffer), and the next regular one is up to a cycle (100 ms) away: the overlay reappeared 53 .. 142 ms (multiples of the vblank) after the host's flip. While the guard is pending the detection
    is therefore repeated every vblank (kEveryMs) and the overlay thread is woken on that timer ONLY then: no wake-up at rest (pending is false: nothing here changes the thread's wait). Bounded: after kMaxAttempts the regular
    cycle alone goes on (the pending flag stays: a later confirmation still releases the helper); a NEW guard epoch starts the count again; a confirmation (pending false) stops it. */
struct GuardRetry
{
    static constexpr double kEveryMs = 16.0;
    static constexpr int kMaxAttempts = 190;   // ~3 regular cycles of 100 ms at 16 ms
    int attempts = 0;
    uint32_t epoch = 0;
    double lastMs = -1.0e9;
    unsigned long detections = 0;   // the extra detections asked for (status file)
    unsigned long wakeups = 0;      // the ticks whose wait was shortened to kEveryMs (status file: nothing at rest)

    /** True: run a page detection now. */
    bool due (bool pending, uint32_t ep, double nowMs) noexcept
    {
        if (! pending)
        {
            attempts = 0;
            return false;
        }
        if (ep != epoch)
        {
            epoch = ep;
            attempts = 0;
        }
        if (attempts >= kMaxAttempts || nowMs - lastMs < kEveryMs)
            return false;
        lastMs = nowMs;
        ++attempts;
        ++detections;
        return true;
    }
    /** The retry is running: the thread must not sleep longer than kEveryMs. */
    bool active (bool pending, uint32_t ep) const noexcept { return pending && (ep != epoch || attempts < kMaxAttempts); }
    /** The wait the overlay thread may take after a tick whose own wait is `wait`. */
    double limit (double wait, bool pending, uint32_t ep) noexcept
    {
        if (! active (pending, ep) || wait <= kEveryMs)
            return wait;
        ++wakeups;
        return kEveryMs;
    }
};

/** How long the helper's main loop may sleep when it has no picture to draw (g134). Before, every pass was capped at 1000 ms (62 wake-ups in 62 s at rest). What the cap was for, one duty each (gpu-helper/main.cpp loop):
    (1) the GETPLANE of a picture that stays (another program may have taken the plane): needs a wake only as often as that check is made, kVerifyMs, and only while there is a picture on the plane;
    (2) the debug dump file (kFlagDump) and the bench poll (SYNTHYBOY_OVERLAY_BENCH): debug builds, kPollMs;
    (3) the guard flag when no GuardWatcher could be started (no futex thread / eventfd): the flag is polled, kPollMs;
    (4) nothing else: the heartbeat of the LayerBlock is written by the commit shim (the helper only publishes pictures), the status file is the plugin's, the socket (messages, quit, hang-up) and the guard flag
        (eventfd) wake the poll, device loss shows in the plane check or in the next update, and the 5 ms retry of an unreadable scene is its own usleep.
    kQuietMs only bounds the sleep so that a termination signal that arrives between the stop flag test and the poll still ends the process. */
struct LoopTimeout
{
    static constexpr double kVerifyMs = 3000.0;    // a still picture is checked against the display this often (3 s: a plane lost to a host mode set or another program is noticed within 3 s) (the check right after an update stays at one per second)
    static constexpr double kPollMs = 1000.0;
    static constexpr double kQuietMs = 60000.0;

    /** `needsPoll`: the dump file, the bench or a flag nobody waits for. `verifyActive`: a picture is on the plane and nothing is waiting to be drawn. Milliseconds, never below 1. */
    static double idleWaitMs (double nowMs, double lastVerifyMs, bool verifyActive, bool needsPoll) noexcept
    {
        if (needsPoll)
            return kPollMs;
        if (! verifyActive)
            return kQuietMs;
        double due = lastVerifyMs + kVerifyMs - nowMs;
        if (due < 1.0)
            due = 1.0;
        if (due > kVerifyMs)
            due = kVerifyMs;
        return std::ceil (due);
    }
};

/** The loop's wake-up counters (status file: loop_wakes, loop_wakes_idle). A wake-up is every return of poll in the loop; an idle one is a timeout that found nothing to do. */
struct LoopWakes
{
    uint32_t wakes = 0, idle = 0;
    void count (int pollResult, bool didWork) noexcept
    {
        ++wakes;
        if (pollResult == 0 && ! didWork)
            ++idle;
    }
};

}}}
