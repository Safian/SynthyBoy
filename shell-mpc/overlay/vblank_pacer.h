// SynthyBoy - MPC overlay: the real refresh period and the presentation pace. Used by the plugin (the CPU drawing) and by the GPU helper; the vblank source is an interface so that the
// Mac tests drive the same code with a fake clock.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What was measured (docs/MPC.md "Overlay (experimental)", "Pacing"): the legacy plane update (SETPLANE) of the MPC XL (RK3588, panel real rate 56.46 Hz, the mode says 60) does not
// take one vblank per picture when it is called back to back: it alternates between waiting one and two vblanks (36.5 pictures a second, intervals of 17.7 and 35.4 ms), and a call
// made a few milliseconds after a vblank is a coin toss between the next vblank and the one after (the later in the period, the more often). A call made AT a vblank (the thread woken
// by the vblank wait itself, nothing sleeping in between) is accepted for the very next vblank every time, and with one call every second vblank the intervals are exactly two
// vblanks: 28.2 pictures a second on the XL, 30 on a 60 Hz panel. So the pace is: a stride of K vblanks (K = the nearest whole number of vblanks to 1/30 s), the update issued
// the moment the vblank of a slot on the grid is reached. Where no vblank can be waited for, a timer of 1/30 s does it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace synthyboy_mpc { namespace overlay {

struct VblankStamp
{
    uint32_t seq = 0;      // the vblank counter
    double ms = 0.0;       // when it happened (CLOCK_MONOTONIC scale)
};

class IVblankClock
{
public:
    virtual ~IVblankClock() = default;
    /** The counter and time of the latest vblank, at once. false: not available. */
    virtual bool last (VblankStamp& s) = 0;
    /** Blocks until the counter has reached `seq` (wrap-safe: a target in the past returns at once with the current stamp). false: failed. */
    virtual bool waitUntil (uint32_t seq, VblankStamp& s) = 0;
    /** Blocks until the next vblank. */
    virtual bool waitNext (VblankStamp& s) = 0;
    virtual double nowMs() = 0;
    virtual void sleepUntilMs (double ms) = 0;
};

/** The real period of the refresh: `count` consecutive vblanks timed from their own time stamps (not from the mode's number, which is wrong on the XL). false: the clock fails. */
inline bool measureVblankPeriod (IVblankClock& clock, int count, double& periodMs)
{
    VblankStamp first, now;
    if (! clock.waitNext (first))
        return false;
    uint32_t last = first.seq;
    double lastMs = first.ms;
    for (int i = 0; i < count; ++i)
    {
        if (! clock.waitNext (now))
            return false;
        last = now.seq;
        lastMs = now.ms;
    }
    const uint32_t n = last - first.seq;
    if (n == 0 || lastMs <= first.ms)
        return false;
    periodMs = (lastMs - first.ms) / n;
    return periodMs > 2.0 && periodMs < 100.0;
}

/** The stride in vblanks for about `targetFps` pictures a second: the nearest whole number (60 Hz -> 2, 56.46 Hz -> 2), at least 1, at most 6. */
inline int paceStride (double periodMs, double targetFps = 30.0)
{
    if (periodMs <= 0.0 || targetFps <= 0.0)
        return 2;
    const long k = std::lround (1000.0 / (periodMs * targetFps));
    return static_cast<int> (std::max (1L, std::min (6L, k)));
}

/** Statistics of the presentation intervals: counts of the interval in vblanks (index 1 .. 8, 9 = more) and in 2 ms buckets (0 .. 63, the last one = 126 ms and more). Only intervals
    between two pictures in a run (less than 200 ms apart) count, so a pause is not a hiccup. */
struct PaceStats
{
    static constexpr int kVb = 10, kMs = 64;
    uint32_t vb[kVb] = {};
    uint32_t ms[kMs] = {};
    uint32_t count = 0;
    void add (uint32_t vblanks, double intervalMs)
    {
        ++vb[vblanks >= static_cast<uint32_t> (kVb) - 1 ? kVb - 1 : vblanks];
        const int b = static_cast<int> (intervalMs / 2.0);
        ++ms[b < 0 ? 0 : (b >= kMs ? kMs - 1 : b)];
        ++count;
    }
};

/** Decides WHEN the plane update of the next picture is made. waitSlot () returns the moment it must be issued (immediately, no further sleeping); done () is told when the update
    has returned, for the statistics. One instance per presenting thread. */
class FramePacer
{
public:
    /** `useVblank` false (no vblank source, or it failed): a timer of `timerMs` paces (the clock is then only used for nowMs / sleepUntilMs). `periodMs` is the measured vblank
        period, `stride` the vblanks per picture. */
    FramePacer (IVblankClock& clock, double periodMs, int stride, bool useVblank, double timerMs = 33.3)
        : clock_ (clock), periodMs_ (periodMs), stride_ (std::max (1, stride)), vblank_ (useVblank), timerMs_ (timerMs) {}

    bool onGrid() const noexcept { return vblank_; }
    int stride() const noexcept { return stride_; }
    double periodMs() const noexcept { return periodMs_; }
    const PaceStats& stats() const noexcept { return stats_; }

    /** The next picture is presented at once (g134): the plugin confirmed one of our pages after a guard plane-off, so the plane is off and the picture is the first one of a new run; waiting for the next stride
        boundary of the old grid only delayed it by up to a stride (35 ms on the XL: 'PageOk sent -> first present' was one whole stride). ONCE: waitSlot () does not wait and msUntilSlot () is 0 for that picture,
        and the update it makes (which lands on the next vblank) starts a new grid, so the picture after it is a stride later exactly as in a run; every other picture keeps the grid. Idempotent until used. */
    void presentNextImmediately() noexcept { immediate_ = true; }
    bool immediatePending() const noexcept { return immediate_; }

    /** Blocks until the slot; the update must follow at once. A failing vblank source switches the pacer to the timer for the rest of its life. */
    void waitSlot()
    {
        if (immediate_)
        {
            immediate_ = false;
            if (vblank_)
            {
                VblankStamp cur;
                if (! clock_.last (cur))
                    return fallBack();
                slotSeq_ = cur.seq + 1u;                    // the update lands on the next vblank: that is the slot of this picture
                slotMs_ = cur.ms + periodMs_;
                haveSlot_ = true;
            }
            else
            {
                timerSlotMs_ = clock_.nowMs();
                haveTimer_ = true;
            }
            return;
        }
        if (vblank_)
        {
            VblankStamp cur;
            if (! clock_.last (cur))
                return fallBack();
            uint32_t target;
            if (haveSlot_ && cur.ms - slotMs_ < 4.0 * stride_ * periodMs_)
            {
                target = slotSeq_ + static_cast<uint32_t> (stride_);       // the next slot on the grid
                while (static_cast<int32_t> (target - cur.seq) <= 0)       // that one has passed (the picture was late): the next one after it
                    target += static_cast<uint32_t> (stride_);
            }
            else
                target = cur.seq + 1;                                      // nothing was shown lately: the very next vblank starts a new grid
            VblankStamp at;
            if (! clock_.waitUntil (target, at))
                return fallBack();
            slotSeq_ = at.seq;
            slotMs_ = at.ms;
            haveSlot_ = true;
            return;
        }
        const double now = clock_.nowMs();
        if (haveTimer_ && now - timerSlotMs_ < 4.0 * timerMs_)
        {
            double next = timerSlotMs_ + timerMs_;
            while (next < now - 1.0)
                next += timerMs_;
            if (next > now)
                clock_.sleepUntilMs (next);
            timerSlotMs_ = next;
        }
        else
            timerSlotMs_ = now;
        haveTimer_ = true;
    }

    /** How long until the next slot (milliseconds, estimated from the last one and the measured period; 0 when there is no grid to keep or the slot is due). Lets the caller start
        drawing just early enough, so that the picture is as fresh as it can be when the slot comes. */
    double msUntilSlot()
    {
        if (immediate_)
            return 0.0;
        const double now = clock_.nowMs();
        if (vblank_)
        {
            VblankStamp cur;
            if (! haveSlot_ || ! clock_.last (cur) || cur.ms - slotMs_ >= 4.0 * stride_ * periodMs_)
                return 0.0;
            uint32_t target = slotSeq_ + static_cast<uint32_t> (stride_);
            while (static_cast<int32_t> (target - cur.seq) <= 0)
                target += static_cast<uint32_t> (stride_);
            const double due = slotMs_ + static_cast<double> (target - slotSeq_) * periodMs_ - now;
            return due > 0.0 ? due : 0.0;
        }
        if (! haveTimer_ || now - timerSlotMs_ >= 4.0 * timerMs_)
            return 0.0;
        double next = timerSlotMs_ + timerMs_;
        while (next < now - 1.0)
            next += timerMs_;
        return next > now ? next - now : 0.0;
    }

    /** The update has returned: the interval to the previous one goes into the statistics. */
    void done()
    {
        double t = clock_.nowMs();
        uint32_t seq = 0;
        VblankStamp s;
        if (vblank_ && clock_.last (s))
        {
            t = s.ms;
            seq = s.seq;
        }
        if (haveDone_ && t - doneMs_ < 200.0)
            stats_.add (vblank_ ? seq - doneSeq_ : static_cast<uint32_t> (std::lround ((t - doneMs_) / std::max (1.0, periodMs_))), t - doneMs_);
        haveDone_ = true;
        doneMs_ = t;
        doneSeq_ = seq;
    }

private:
    void fallBack()
    {
        vblank_ = false;
        haveSlot_ = false;
        haveTimer_ = false;
        haveDone_ = false;
    }

    IVblankClock& clock_;
    double periodMs_;
    int stride_;
    bool vblank_;
    double timerMs_;
    bool haveSlot_ = false, haveDone_ = false, haveTimer_ = false, immediate_ = false;
    uint32_t slotSeq_ = 0, doneSeq_ = 0;
    double slotMs_ = 0.0, doneMs_ = 0.0, timerSlotMs_ = 0.0;
    PaceStats stats_;
};

}} // namespace synthyboy_mpc::overlay
