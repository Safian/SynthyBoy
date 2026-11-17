// SynthyBoy - MPC shell (overlay build only): the Q proxies. Sixteen plugin parameters "Q 1" .. "Q 16" after the last Play proxy; the hardware knobs (Q-Links) of the host are mapped to them on the
// pages the plugin draws itself (docs/MPC.md "Q proxies"). Pure logic over atomics, no state of the plugin (the plugin passes in how to read and write a real parameter), so that the host tests can use it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The page's targets are up to sixteen SLOTS (four groups of four, mapGroups): slots 1-4 are the ACTIVE group, 5-8 / 9-12 / 13-16 the next groups of the page in reading order. Each slot is the table index of an engine parameter or "none" (setGroup, called by the UI: the overlay thread). A Q proxy has no value of its own:
//   - getParameter on Q n: the normalised value of the target now, 0 when it has none (value);
//   - setParameter on Q n: the same write as the host's own write to the target (hostWrite -> the plugin's writeRealParam); nothing when the target is none, when the value is the one the target has
//     (an echo of a report) or inside the restore window (the chunk wins: docs/MPC.md "Project save and reload"; the same rule as the Play proxies);
//   - when the group changes the host is told the sixteen new values (poll, the display thread: audioMasterAutomate), so the knob display of the host follows (and the plugin then tells it that the names changed: audioMasterUpdateDisplay, see docs/MPC.md); a target that changes by another route
//     (its own page, a preset, automation, the touch screen of the plugin's own UI) is reported the same way; a write the host made to a Q proxy makes the poll report the real parameter too.
// Audio thread rule: value () and hostWrite () (the getParameter / setParameter paths) use only relaxed / acquire-release atomics: no allocation, no lock, no loop longer than sixteen steps.
// Not saved in the chunk, not offered for automation: an automation recorded on a Q proxy replays into whatever group is active when it plays (docs/MPC.md).
#pragma once

#include <atomic>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cmath>

namespace synthyboy_mpc {

class QRouter
{
public:
    static constexpr int kGroupSize = 4;                  // the knobs of one group (the four Q-Links of the Key 61, one row of the XL)
    static constexpr int kGroups = 4;                     // the groups reachable at once
    static constexpr int kCount = kGroupSize * kGroups;   // 16 proxies "Q 1" .. "Q 16"
    static constexpr int kNone = -1;
    static constexpr float kTolerance = 1.0e-6f;   // an echo / a change smaller than this is none (one millionth: as for the Play proxies)

    enum class Write { none, echo, restoring, written };

    // A target is a table index, optionally with a VIEW of it in the bits above 16: a Q knob that is not the parameter itself. kOct: the octaves (steps of 12) of a transpose parameter -48 .. +48
    // (Q 1 of the Tune group), kSem: what is left inside the octave (Q 2). transpose = 12 * oct + sem; the two knobs no longer do the same thing.
    static constexpr int kModeShift = 16, kIndexMask = 0xffff;
    static constexpr int kPlain = 0, kOct = 1, kSem = 2;
    /** kVirtual: the target is no parameter but a VIRTUAL value of the plugin-drawn page (the index is its id 0 .. kNumVirtual - 1: the values of the LFO shape editor). The page tells the router the value the target has
        now (setVirtualValue); a write of the host to the Q proxy is kept for the page (takeVirtual) and not written to any parameter. target (q) is none for it: every table index code ignores it. */
    static constexpr int kVirtual = 3, kNumVirtual = 8;
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

    /** The slot block (0: the active group, 1 .. 3: the next groups in reading order, wrapping, each group once) a group of a page with `nGroups` groups gets when group `active` is the active one; -1 when the
        group has no block (more than four groups) or the arguments are out of range. Slots 4 * block .. 4 * block + 3 are its four knobs. */
    static int blockOfGroup (int nGroups, int active, int group) noexcept
    {
        if (nGroups <= 0 || active < 0 || active >= nGroups || group < 0 || group >= nGroups)
            return -1;
        const int k = (group - active + nGroups) % nGroups;
        return k < kGroups ? k : -1;
    }
    /** The 16 slots of a page: `targetsOf (group, int* four)` fills the four targets of a group (table indices, encoded or -1). Fewer than four groups leave the rest of the slots without a target.
        `out` gets kCount values; returns how many slots are in use (a multiple of four, 0 when the page has no group). */
    template <class TargetsOf>
    static int mapGroups (int nGroups, int active, TargetsOf&& targetsOf, int* out) noexcept
    {
        for (int i = 0; i < kCount; ++i)
            out[i] = kNone;
        if (nGroups <= 0 || active < 0 || active >= nGroups)
            return 0;
        const int blocks = std::min (nGroups, kGroups);
        for (int k = 0; k < blocks; ++k)
            targetsOf ((active + k) % nGroups, out + k * kGroupSize);
        return blocks * kGroupSize;
    }

    static uint64_t pack (uint32_t gen, int code) noexcept { return (static_cast<uint64_t> (gen) << 32) | static_cast<uint32_t> (code); }
    static int codeOf (uint64_t slot) noexcept { return static_cast<int32_t> (static_cast<uint32_t> (slot & 0xffffffffull)); }
    static uint32_t genOf (uint64_t slot) noexcept { return static_cast<uint32_t> (slot >> 32); }

    QRouter() noexcept
    {
        for (int q = 0; q < kCount; ++q)
        {
            slot_[q].store (pack (0u, kNone), std::memory_order_relaxed);
            last_[q].store (0.0f, std::memory_order_relaxed);
            realDue_[q].store (kNone, std::memory_order_relaxed);
        }
    }

    /** The UI sets the slots: targets[0 .. n-1] (n at most 16; the rest are none: a page that knows one group passes four). A target outside 0 .. paramCount - 1 is none. Any thread; the poll reports the new values when the group changed. */
    void setGroup (const int* targets, int n, int paramCount) noexcept
    {
        bool changed = false;
        for (int q = 0; q < kCount; ++q)
        {
            const int t = (targets != nullptr && q < n && targets[q] >= 0 && indexOf (targets[q]) < paramCount) ? targets[q] : kNone;
            // the slot is ONE word: the generation (it counts the changes of the target of this slot) and the code, so a reader sees both of the same moment (a write of the host never mixes the index of one group with the
            // mode of another), and the state of the stepping that belongs to the old target is known to be stale (hostWrite starts again for a new generation)
            uint64_t old = slot_[q].load (std::memory_order_relaxed);
            while (codeOf (old) != t && ! slot_[q].compare_exchange_weak (old, pack (genOf (old) + 1u, t), std::memory_order_relaxed))
            {
            }
            if (codeOf (old) != t)
                changed = true;
        }
        if (changed)
            groupDue_.store (true, std::memory_order_release);
    }

    /** The table index of the target of Q q (0 .. 15), kNone when it has none (or a virtual one). */
    int target (int q) const noexcept { return targetOfCode (encoded (q)); }
    static int targetOfCode (int e) noexcept { return modeOf (e) == kVirtual ? kNone : indexOf (e); }
    static int virtualIdOfCode (int e) noexcept { return e >= 0 && modeOf (e) == kVirtual && indexOf (e) < kNumVirtual ? indexOf (e) : -1; }
    /** The virtual id of the target of Q q (0 .. kNumVirtual - 1), -1 when it has none or a real one. */
    int virtualId (int q) const noexcept { return virtualIdOfCode (encoded (q)); }
    /** The page: the value (0 .. 1) virtual id has now; the host is told when it differs from what it last heard (the poll). */
    void setVirtualValue (int id, float v) noexcept { if (id >= 0 && id < kNumVirtual) vValue_[id].store (std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed); }
    /** The page: the value the host wrote to a Q proxy whose target is virtual id, once (false: nothing written since the last call). */
    bool takeVirtual (int id, float& v) noexcept
    {
        if (id < 0 || id >= kNumVirtual || ! vDue_[id].exchange (false, std::memory_order_acq_rel))
            return false;
        v = vIn_[id].load (std::memory_order_relaxed);
        return true;
    }
    /** How many times the host wrote a Q proxy since the router exists (any outcome: an echo or a write to a Q without a target counts too). The overlay thread reads it. */
    unsigned long writeCount() const noexcept { return writes_.load (std::memory_order_relaxed); }
    int mode (int q) const noexcept { return modeOf (encoded (q)); }
    int encoded (int q) const noexcept { return q >= 0 && q < kCount ? codeOf (slot_[q].load (std::memory_order_relaxed)) : kNone; }

    /** What the host reads from Q q: the normalised value of the target (readNorm (tableIndex)), 0 when none. */
    template <class Read>
    float value (int q, Read&& readNorm) const noexcept
    {
        return valueOfCode (encoded (q), readNorm);   // the code is read ONCE: the kind, the index and the view of one moment
    }
    template <class Read>
    float valueOfCode (int e, Read&& readNorm) const noexcept
    {
        if (const int vid = virtualIdOfCode (e); vid >= 0)
            return vValue_[vid].load (std::memory_order_relaxed);
        const int t = targetOfCode (e);
        return t == kNone ? 0.0f : viewValue (modeOf (e), readNorm (t));
    }

    /** Relative stepping of STEPPED targets (a toggle, a list, an integer stepper): the proxy's absolute 0 .. 1 is the wrong control for them (a toggle needs half of the knob's travel, a short list a third), so the travel of
        the host's value since its last write is accumulated and one step of the target happens per kStepTravelDefault of travel, in the direction of the turn. Proxy units; the plugin may override it from a debug file. */
    static constexpr float kStepTravelDefault = 6.0f / 128.0f;   // 6 host increments of 1/128 per step (owner, MPC XL, 2026-11-16: the first 3 were half of what feels right); the file /tmp/synthyboy-overlay-q-step-travel still overrides it
    static constexpr int kMaxStepsPerWrite = 4;   // the most steps one host write makes (and the move, in steps, above which a write is a jump and no travel)
    static constexpr float kOneIncrement = 1.0f / 128.0f;   // what one click of a Q-Link moves the host's value (Key 61, measured: the writes of a burst are 1/128 apart)
    void setStepTravel (float t) noexcept { stepTravel_.store (t > 1.0e-4f ? t : kStepTravelDefault, std::memory_order_relaxed); }
    float stepTravel() const noexcept { return stepTravel_.load (std::memory_order_relaxed); }
    /** For the status file: the raw value the host wrote to Q q last, the delta of that write, the travel accumulated for the next step. */
    float lastRaw (int q) const noexcept { return q >= 0 && q < kCount ? dbgRaw_[q].load (std::memory_order_relaxed) : 0.0f; }
    float lastDelta (int q) const noexcept { return q >= 0 && q < kCount ? dbgDelta_[q].load (std::memory_order_relaxed) : 0.0f; }
    float turnSum (int q) const noexcept { return q >= 0 && q < kCount ? dbgSum_[q].load (std::memory_order_relaxed) : 0.0f; }
    float travel (int q) const noexcept { return q >= 0 && q < kCount ? acc_[q].load (std::memory_order_relaxed) : 0.0f; }
    /** The status file lines of the stepping: the travel per step in force, and for every knob that has a stepped-capable target the last raw value the host wrote, the delta of that write (both in proxy units 0 .. 1 and
        in 1/128 increments) and the travel accumulated towards the next step. `q_step_travel` says where the number comes from (`overridden` by the debug file or the default). A knob without a target has no line. */
    void describe (std::string& out) const
    {
        char b[240];
        const float st = stepTravel();
        std::snprintf (b, sizeof b, "q_step_travel: %.6f (%.2f increments of 1/128, %s)\n", static_cast<double> (st), static_cast<double> (st * 128.0f), std::fabs (st - kStepTravelDefault) < 1.0e-7f ? "default" : "overridden");
        out += b;
        for (int q = 0; q < kCount; ++q)
        {
            const int t = target (q);
            if (t == kNone)
                continue;
            std::snprintf (b, sizeof b, "q_knob_%d: target %d, raw %.6f (%.2f/128), delta %+.6f (%+.2f/128), travel %+.6f (%+.2f/128 of %.2f per step), turned %+.2f/128 in all\n", q + 1, t, static_cast<double> (lastRaw (q)), static_cast<double> (lastRaw (q) * 128.0f),
                           static_cast<double> (lastDelta (q)), static_cast<double> (lastDelta (q) * 128.0f), static_cast<double> (travel (q)), static_cast<double> (travel (q) * 128.0f), static_cast<double> (st * 128.0f), static_cast<double> (turnSum (q) * 128.0f));
            out += b;
        }
    }

    /** As hostWrite below, with `stepsOf (tableIndex)`: the number of values of a stepped target (a toggle 2, a list its entries), 0 for a continuous one. A stepped plain target is driven relatively (see kStepTravelDefault):
        steps happen in the direction of the turn, a change of direction starts the travel again, at the ends nothing happens; the poll then reports the target's real position to the host, so the host's own value never
        runs into its limit while steps are left. The virtual targets have no step count here and stay absolute (setVirtualSteps gives one). */
    template <class Read, class WriteFn, class StepsOf>
    Write hostWrite (int q, float host, bool restoring, Read&& readNorm, WriteFn&& write, StepsOf&& stepsOf) noexcept
    {
        if (q < 0 || q >= kCount)
            return hostWrite (q, host, restoring, readNorm, write);
        const uint64_t slot = slot_[q].load (std::memory_order_relaxed);   // the slot is read ONCE for the whole call: the target, its view and its generation are one moment
        const int e = codeOf (slot), t = targetOfCode (e);
        const int n = (t == kNone || modeOf (e) != kPlain || virtualIdOfCode (e) >= 0) ? 0 : stepsOf (t);
        if (n < 2)
            return writeSlot (q, slot, host, restoring, readNorm, write);
        writes_.fetch_add (1, std::memory_order_relaxed);
        const float v = std::isfinite (host) ? std::clamp (host, 0.0f, 1.0f) : 0.0f;
        // a new target in this slot (the page changed its group): what was collected for the old one is of no use. The baseline is the value the host is told for the new target; until the host was told (the poll's
        // report of the group), a write comes from the dial of the OLD target and is no turn of this one: it only tells where the host's dial is now
        const uint32_t gen = genOf (slot);
        if (gen != seenGen_[q].load (std::memory_order_relaxed))
        {
            const float now = valueOfCode (e, readNorm);
            acc_[q].store (0.0f, std::memory_order_relaxed);
            base_[q].store (now, std::memory_order_relaxed);
            rep_[q].store (now, std::memory_order_relaxed);
            seenGen_[q].store (gen, std::memory_order_relaxed);
        }
        if (toldGen_[q].load (std::memory_order_acquire) != gen)
        {
            base_[q].store (v, std::memory_order_relaxed);
            return Write::none;
        }
        // how far did the host move its own value with this write? Two bases are possible: the host's previous write (it keeps its own value while a knob is turned: the writes of a burst follow each other) and the value
        // we last REPORTED (the host takes the report as its value when the knob is touched again: the first write of a touch is the report plus an increment). A report that arrives in the middle of a burst must not
        // count: it differs from the host's own value by up to a whole step of the target, so measuring from it counted a write twice (a step after 3 increments instead of 6) or backwards (a list of 12 needed 11).
        // Measured on the Key 61 (host writes in the play log): a burst at 20 ms continues from the host's own value (increments of 1/128, now and then several in one write), one increment per touch starts at the
        // report. The plugin cannot see the touches, so it takes the distance that looks like an increment: the one of the two (from the previous write, from the last report) that is nearer to ONE increment; a write
        // that repeats the previous one exactly is the first write of a new touch (from the report). After a step the report has moved by the step of the target, so the first write of the next touch is that far from
        // the previous write and exactly one increment from the report: it is the report that counts then.
        const float base = base_[q].load (std::memory_order_relaxed);
        const float rep = rep_[q].load (std::memory_order_relaxed);
        repDue_[q].store (false, std::memory_order_relaxed);
        const float dBase = v - base, dRep = v - rep, aB = std::fabs (dBase), aR = std::fabs (dRep);
        float delta;
        if (aB <= 1.0e-7f)
            delta = dRep;
        else if (aR <= 1.0e-7f)
            delta = dBase;
        else
            delta = std::fabs (aR - kOneIncrement) < std::fabs (aB - kOneIncrement) ? dRep : dBase;
        dbgRaw_[q].store (v, std::memory_order_relaxed);
        dbgDelta_[q].store (delta, std::memory_order_relaxed);
        dbgSum_[q].store (dbgSum_[q].load (std::memory_order_relaxed) + delta, std::memory_order_relaxed);   // for the calibration of a quarter turn: the sum of every move the host made on this knob
        base_[q].store (v, std::memory_order_relaxed);
        float cur = readNorm (t);
        if (std::fabs (v - cur) <= kTolerance)   // an echo of what was reported
        {
            acc_[q].store (0.0f, std::memory_order_relaxed);
            return Write::echo;
        }
        if (restoring)
        {
            acc_[q].store (0.0f, std::memory_order_relaxed);   // the project is being restored: this write is the host putting a stored value back, no turn (the base was taken above)
            return Write::restoring;
        }
        const float travel = stepTravel();
        // a hand gives one increment per write (a burst or a coalesced write a few): a move of more than kMaxStepsPerWrite steps is the host jumping (an automation point, an adopted report that was far, a touch
        // screen drag of the host's own) and no travel: the value is the new baseline, nothing steps. So one host write makes at most kMaxStepsPerWrite steps, and writes the target ONCE (the entries in between are
        // no state anyone should hear: the audio thread may be the caller).
        if (std::fabs (delta) > kMaxStepsPerWrite * travel)
        {
            acc_[q].store (0.0f, std::memory_order_relaxed);
            last_[q].store (v, std::memory_order_relaxed);
            return Write::none;
        }
        float acc = acc_[q].load (std::memory_order_relaxed);
        if ((delta > 0.0f) != (acc > 0.0f) || acc == 0.0f)
            acc = 0.0f;   // another direction (or the first write): start again
        acc += delta;
        bool wrote = false;
        int steps = 0;
        while (std::fabs (acc) >= travel && steps < kMaxStepsPerWrite)
        {
            acc -= (acc > 0.0f ? 1.0f : -1.0f) * travel;
            ++steps;
        }
        if (std::fabs (acc) >= travel)
            acc = 0.0f;   // more travel than the bound allows is dropped (it cannot come from one write: the bound above is the same number)
        if (steps > 0)
        {
            const int dir = delta > 0.0f ? 1 : -1;
            const int idx = static_cast<int> (std::lround (cur * static_cast<float> (n - 1)));
            const int next = std::clamp (idx + dir * steps, 0, n - 1);
            if (next == idx)
                acc = 0.0f;   // the end of a list: further turning does nothing
            else
            {
                cur = static_cast<float> (next) / static_cast<float> (n - 1);
                write (t, cur);
                wrote = true;
            }
        }
        acc_[q].store (acc, std::memory_order_relaxed);
        last_[q].store (v, std::memory_order_relaxed);   // the host holds v: the poll tells it the real position (it differs from v), so the host's value follows the target
        if (wrote)
            realDue_[q].store (t, std::memory_order_release);
        return wrote ? Write::written : Write::none;
    }

    /** The host writes Q q. The write goes to the target through write (tableIndex, normalised) unless the target is none, the value is the target's own (echo) or the restore window is open. */
    template <class Read, class WriteFn>
    Write hostWrite (int q, float host, bool restoring, Read&& readNorm, WriteFn&& write) noexcept
    {
        return writeSlot (q, q >= 0 && q < kCount ? slot_[q].load (std::memory_order_relaxed) : pack (0u, kNone), host, restoring, readNorm, write);
    }
    /** The write of a Q whose slot word was read by the caller (once: the code does not change under the call, whatever the display thread does to the group). */
    template <class Read, class WriteFn>
    Write writeSlot (int q, uint64_t slot, float host, bool restoring, Read&& readNorm, WriteFn&& write) noexcept
    {
        writes_.fetch_add (1, std::memory_order_relaxed);   // every call counts, whatever came of it: the overlay learns that a hardware knob is being turned (the host's Q-Link panel is about to be drawn)
        const int e = codeOf (slot);
        if (const int vid = virtualIdOfCode (e); vid >= 0)
        {
            const float v = std::isfinite (host) ? (host < 0.0f ? 0.0f : (host > 1.0f ? 1.0f : host)) : 0.0f;
            if (std::fabs (v - vValue_[vid].load (std::memory_order_relaxed)) <= kTolerance)
                return Write::echo;
            if (restoring)
                return Write::restoring;
            vIn_[vid].store (v, std::memory_order_relaxed);
            vDue_[vid].store (true, std::memory_order_release);
            vValue_[vid].store (v, std::memory_order_relaxed);   // the host knows this value: it is the value of the target until the page says another
            last_[q].store (v, std::memory_order_relaxed);
            return Write::written;
        }
        const int t = targetOfCode (e);
        if (t == kNone)
            return Write::none;
        const float v = std::isfinite (host) ? (host < 0.0f ? 0.0f : (host > 1.0f ? 1.0f : host)) : 0.0f;
        const float cur = readNorm (t);
        const int m = modeOf (e);
        if (std::fabs (v - viewValue (m, cur)) <= kTolerance)
            return Write::echo;
        if (restoring)
            return Write::restoring;
        const float nv = viewWrite (m, v, cur);
        write (t, nv);
        last_[q].store (valueOfCode (e, readNorm), std::memory_order_relaxed);   // the host knows this value: the poll does not report it back
        realDue_[q].store (t, std::memory_order_release);
        return Write::written;
    }

    /** The display thread, every tick. reportQ (q, value): the host is told the value of Q q; reportReal (tableIndex): the host is told the real parameter changed. A group change reports all sixteen Q
        values once; otherwise a value is reported only when it differs from what the host last heard. */
    template <class Read, class ReportQ, class ReportReal>
    void poll (Read&& readNorm, ReportQ&& reportQ, ReportReal&& reportReal) noexcept
    {
        poll (readNorm, reportQ, reportReal, [] {});
    }
    /** As above; groupChanged () is called once, after the sixteen values of a changed group were reported: the names, units and value texts of the proxies are other now (the plugin tells the host). */
    template <class Read, class ReportQ, class ReportReal, class GroupChanged>
    void poll (Read&& readNorm, ReportQ&& reportQ, ReportReal&& reportReal, GroupChanged&& groupChanged) noexcept
    {
        if (! lastValid_)
        {
            for (int q = 0; q < kCount; ++q)
                last_[q].store (value (q, readNorm), std::memory_order_relaxed);
            lastValid_ = true;
        }
        if (groupDue_.exchange (false, std::memory_order_acq_rel))
        {
            for (int q = 0; q < kCount; ++q)
            {
                const uint64_t slot = slot_[q].load (std::memory_order_relaxed);
                const float v = valueOfCode (codeOf (slot), readNorm);
                last_[q].store (v, std::memory_order_relaxed);
                rep_[q].store (v, std::memory_order_relaxed);
                reportQ (q, v);
                toldGen_[q].store (genOf (slot), std::memory_order_release);   // the host holds this value for this target now: its next write is a turn of it
            }
            groupChanged();
        }
        for (int q = 0; q < kCount; ++q)
        {
            const float v = value (q, readNorm);
            if (std::fabs (v - last_[q].load (std::memory_order_relaxed)) > kTolerance)
            {
                last_[q].store (v, std::memory_order_relaxed);
                rep_[q].store (v, std::memory_order_relaxed);   // the host may take the reported value as its own: the next write's travel is measured from the nearer of this and its last write
                repDue_[q].store (true, std::memory_order_release);
                reportQ (q, v);
            }
            const int real = realDue_[q].exchange (kNone, std::memory_order_acq_rel);
            if (real != kNone)
                reportReal (real);
        }
    }

private:
    std::atomic<uint64_t> slot_[kCount];    // per slot: the generation of the target (high word) and its code (low word), see setGroup
    std::atomic<uint32_t> seenGen_[kCount] = {};   // the generation the stepping state (base_, rep_, acc_) belongs to (host thread)
    std::atomic<uint32_t> toldGen_[kCount] = {};   // the generation whose value the host was told (the poll's group report)
    std::atomic<float> base_[kCount] = {};  // the value the host holds for Q q (its last write or the last report): the travel of the next write is measured from it
    std::atomic<float> rep_[kCount] = {};   // the last value reported to the host
    std::atomic<bool> repDue_[kCount] = {};
    std::atomic<float> acc_[kCount] = {};   // the travel accumulated for the next step of a stepped target (signed)
    std::atomic<float> dbgRaw_[kCount] = {}, dbgDelta_[kCount] = {}, dbgSum_[kCount] = {};
    std::atomic<float> stepTravel_ { kStepTravelDefault };
    std::atomic<float> last_[kCount];       // the value of each Q as the host last heard it (a host write or a report)
    std::atomic<int> realDue_[kCount];      // the real parameter a host write of Q q changed: the poll tells the host (kNone: nothing due)
    std::atomic<float> vValue_[kNumVirtual] = {};   // the value of every virtual target (set by the page, or by the host's write)
    std::atomic<float> vIn_[kNumVirtual] = {};      // the last value the host wrote to a virtual target
    std::atomic<bool> vDue_[kNumVirtual] = {};      // ... and it was not taken by the page yet
    std::atomic<bool> groupDue_ { false };
    std::atomic<unsigned long> writes_ { 0 };
    bool lastValid_ = false;                // display thread only
};

} // namespace synthyboy_mpc
