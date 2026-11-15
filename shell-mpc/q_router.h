// SynthyBoy - MPC shell (overlay build only): the Q proxies. Four plugin parameters "Q 1" .. "Q 4" after the last Play proxy; the four hardware knobs (Q-Links) of the host are mapped to them on the
// pages the plugin draws itself (docs/MPC.md "Q proxies"). Pure logic over atomics, no state of the plugin (the plugin passes in how to read and write a real parameter), so that the host tests can use it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The active GROUP is up to four TARGETS, each the table index of an engine parameter or "none" (setGroup, called by the UI: the overlay thread). A Q proxy has no value of its own:
//   - getParameter on Q n: the normalised value of the target now, 0 when it has none (value);
//   - setParameter on Q n: the same write as the host's own write to the target (hostWrite -> the plugin's writeRealParam); nothing when the target is none, when the value is the one the target has
//     (an echo of a report) or inside the restore window (the chunk wins: docs/MPC.md "Project save and reload"; the same rule as the Play proxies);
//   - when the group changes the host is told the four new values (poll, the display thread: audioMasterAutomate), so the knob display of the host follows; a target that changes by another route
//     (its own page, a preset, automation, the touch screen of the plugin's own UI) is reported the same way; a write the host made to a Q proxy makes the poll report the real parameter too.
// Audio thread rule: value () and hostWrite () (the getParameter / setParameter paths) use only relaxed / acquire-release atomics: no allocation, no lock, no loop longer than four steps.
// Not saved in the chunk, not offered for automation: an automation recorded on a Q proxy replays into whatever group is active when it plays (docs/MPC.md).
#pragma once

#include <atomic>
#include <algorithm>
#include <cmath>

namespace synthyboy_mpc {

class QRouter
{
public:
    static constexpr int kCount = 4;
    static constexpr int kNone = -1;
    static constexpr float kTolerance = 1.0e-6f;   // an echo / a change smaller than this is none (one millionth: as for the Play proxies)

    enum class Write { none, echo, restoring, written };

    // A target is a table index, optionally with a VIEW of it in the bits above 16: a Q knob that is not the parameter itself. kOct: the octaves (steps of 12) of a transpose parameter -48 .. +48
    // (Q 1 of the Tune group), kSem: what is left inside the octave (Q 2). transpose = 12 * oct + sem; the two knobs no longer do the same thing.
    static constexpr int kModeShift = 16, kIndexMask = 0xffff;
    static constexpr int kPlain = 0, kOct = 1, kSem = 2;
    static constexpr int encode (int tableIndex, int mode) noexcept { return tableIndex < 0 ? kNone : (tableIndex | (mode << kModeShift)); }
    static int indexOf (int encoded) noexcept { return encoded < 0 ? kNone : (encoded & kIndexMask); }
    static int modeOf (int encoded) noexcept { return encoded < 0 ? kPlain : (encoded >> kModeShift); }
    static constexpr float kTransposeRange = 96.0f;   // -48 .. +48
    static int octOfTranspose (int t) noexcept { return t / 12; }
    static int semOfTranspose (int t) noexcept { return t - 12 * (t / 12); }
    /** The normalised value a view shows for the normalised transpose n. */
    static float viewValue (int mode, float n) noexcept
    {
        if (mode == kPlain) return n;
        const int t = static_cast<int> (std::lround (n * kTransposeRange - 48.0f));
        return mode == kOct ? static_cast<float> (octOfTranspose (t) + 4) / 8.0f : static_cast<float> (semOfTranspose (t) + 11) / 22.0f;
    }
    /** The normalised transpose after the view was written with v (the part the view does not show is kept). */
    static float viewWrite (int mode, float v, float n) noexcept
    {
        if (mode == kPlain) return v;
        const int t = static_cast<int> (std::lround (n * kTransposeRange - 48.0f));
        const int nt = mode == kOct ? static_cast<int> (std::lround (v * 8.0f - 4.0f)) * 12 + semOfTranspose (t) : octOfTranspose (t) * 12 + static_cast<int> (std::lround (v * 22.0f - 11.0f));
        return static_cast<float> (std::clamp (nt, -48, 48) + 48) / kTransposeRange;
    }

    QRouter() noexcept
    {
        for (int q = 0; q < kCount; ++q)
        {
            target_[q].store (kNone, std::memory_order_relaxed);
            last_[q].store (0.0f, std::memory_order_relaxed);
            realDue_[q].store (kNone, std::memory_order_relaxed);
        }
    }

    /** The UI sets the active group: targets[0 .. n-1] (n at most 4; the rest are none). A target outside 0 .. paramCount - 1 is none. Any thread; the poll reports the new values when the group changed. */
    void setGroup (const int* targets, int n, int paramCount) noexcept
    {
        bool changed = false;
        for (int q = 0; q < kCount; ++q)
        {
            const int t = (targets != nullptr && q < n && targets[q] >= 0 && indexOf (targets[q]) < paramCount) ? targets[q] : kNone;
            changed = target_[q].exchange (t, std::memory_order_relaxed) != t || changed;
        }
        if (changed)
            groupDue_.store (true, std::memory_order_release);
    }

    /** The table index of the target of Q q (0 .. 3), kNone when it has none. */
    int target (int q) const noexcept { return indexOf (encoded (q)); }
    int mode (int q) const noexcept { return modeOf (encoded (q)); }
    int encoded (int q) const noexcept { return q >= 0 && q < kCount ? target_[q].load (std::memory_order_relaxed) : kNone; }

    /** What the host reads from Q q: the normalised value of the target (readNorm (tableIndex)), 0 when none. */
    template <class Read>
    float value (int q, Read&& readNorm) const noexcept
    {
        const int t = target (q);
        return t == kNone ? 0.0f : viewValue (mode (q), readNorm (t));
    }

    /** The host writes Q q. The write goes to the target through write (tableIndex, normalised) unless the target is none, the value is the target's own (echo) or the restore window is open. */
    template <class Read, class WriteFn>
    Write hostWrite (int q, float host, bool restoring, Read&& readNorm, WriteFn&& write) noexcept
    {
        const int t = target (q);
        if (t == kNone)
            return Write::none;
        const float v = std::isfinite (host) ? (host < 0.0f ? 0.0f : (host > 1.0f ? 1.0f : host)) : 0.0f;
        const float cur = readNorm (t);
        if (std::fabs (v - viewValue (mode (q), cur)) <= kTolerance)
            return Write::echo;
        if (restoring)
            return Write::restoring;
        const float nv = viewWrite (mode (q), v, cur);
        write (t, nv);
        last_[q].store (value (q, readNorm), std::memory_order_relaxed);   // the host knows this value: the poll does not report it back
        realDue_[q].store (t, std::memory_order_release);
        return Write::written;
    }

    /** The display thread, every tick. reportQ (q, value): the host is told the value of Q q; reportReal (tableIndex): the host is told the real parameter changed. A group change reports all four Q
        values once; otherwise a value is reported only when it differs from what the host last heard. */
    template <class Read, class ReportQ, class ReportReal>
    void poll (Read&& readNorm, ReportQ&& reportQ, ReportReal&& reportReal) noexcept
    {
        if (! lastValid_)
        {
            for (int q = 0; q < kCount; ++q)
                last_[q].store (value (q, readNorm), std::memory_order_relaxed);
            lastValid_ = true;
        }
        if (groupDue_.exchange (false, std::memory_order_acq_rel))
            for (int q = 0; q < kCount; ++q)
            {
                const float v = value (q, readNorm);
                last_[q].store (v, std::memory_order_relaxed);
                reportQ (q, v);
            }
        for (int q = 0; q < kCount; ++q)
        {
            const float v = value (q, readNorm);
            if (std::fabs (v - last_[q].load (std::memory_order_relaxed)) > kTolerance)
            {
                last_[q].store (v, std::memory_order_relaxed);
                reportQ (q, v);
            }
            const int real = realDue_[q].exchange (kNone, std::memory_order_acq_rel);
            if (real != kNone)
                reportReal (real);
        }
    }

private:
    std::atomic<int> target_[kCount];
    std::atomic<float> last_[kCount];       // the value of each Q as the host last heard it (a host write or a report)
    std::atomic<int> realDue_[kCount];      // the real parameter a host write of Q q changed: the poll tells the host (kNone: nothing due)
    std::atomic<bool> groupDue_ { false };
    bool lastValid_ = false;                // display thread only
};

} // namespace synthyboy_mpc
