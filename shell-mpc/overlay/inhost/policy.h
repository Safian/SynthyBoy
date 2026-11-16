// SynthyBoy - MPC in-host drawing (experimental): the small pure decisions around the hook, tested without a device: when the hook "misbehaves" (the fall back to the plane overlay), and the seqlock boxes
// that hand the hook's results to the other threads.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include "shadow.h"

namespace synthyboy_mpc { namespace overlay { namespace inhost {

struct FallbackLimits
{
    uint32_t slowUs = 8000;
    int slowPerWindow = 8;
    int window = 128;
    int unidentifiedRun = 40;
    int unparsedRun = 40;
};

/** When the in-host drawing gives up and the plane overlay takes over again (the verdicts are final for the process: no retry until the plugin is loaded again, or the kill-switch file is removed and a view shown anew):
      Slow          the hook took longer than `slowUs` in `slowPerWindow` of the last `window` flips (a hook that holds the host's main thread for long is worse than no picture)
      Unidentified  `unidentifiedRun` flips in a row whose buffer could not be named or mapped (the host's request or buffers are not what they were measured to be)
      Unparsed      `unparsedRun` commits with a page flip event in a row that carry no framebuffer for the primary plane (the request layout changed under us) */
class FallbackPolicy
{
public:
    using Limits = FallbackLimits;
    enum class Verdict { Ok, Slow, Unidentified, Unparsed };

    explicit FallbackPolicy (const Limits& l = Limits()) : lim_ (l) {}

    /** One flip that carried a framebuffer of the primary plane. `identified`: the buffer was found and mapped. */
    Verdict onFlip (uint32_t durUs, bool identified) noexcept
    {
        parseRun_ = 0;
        if (++inWindow_ > lim_.window)
        {
            inWindow_ = 1;
            slowInWindow_ = 0;
        }
        if (durUs > lim_.slowUs)
            ++slowInWindow_;
        idRun_ = identified ? 0 : idRun_ + 1;
        if (slowInWindow_ >= lim_.slowPerWindow)
            return Verdict::Slow;
        if (idRun_ >= lim_.unidentifiedRun)
            return Verdict::Unidentified;
        return Verdict::Ok;
    }
    /** A commit with the page flip event flag and no framebuffer item for the primary plane. */
    Verdict onUnparsed() noexcept { return ++parseRun_ >= lim_.unparsedRun ? Verdict::Unparsed : Verdict::Ok; }
    void reset() noexcept { inWindow_ = slowInWindow_ = idRun_ = parseRun_ = 0; }
    static const char* name (Verdict v) noexcept
    {
        switch (v)
        {
            case Verdict::Slow: return "the hook was slow too often";
            case Verdict::Unidentified: return "the buffer of the commit could not be identified";
            case Verdict::Unparsed: return "the commit request could not be read";
            default: return "ok";
        }
    }

private:
    Limits lim_;
    int inWindow_ = 0, slowInWindow_ = 0, idRun_ = 0, parseRun_ = 0;
};

/** What the hook decided at a flip, and what is on the screen now (the writer of the front buffer reads it). */
struct LookRecord
{
    Look look;
    uint32_t fb = 0;          // the host's framebuffer id the frame is in
    uint32_t flipSeq = 0;     // the number of the flip that put it on screen
    uint32_t drawnGen = 0;    // the generation of our picture the hook copied into it (0: none)
};

/** One writer, any number of readers, no lock: the reader repeats until it saw the same even sequence number before and after copying. The fields are relaxed atomics (no data race, and the sequence number orders them). */
class LookBox
{
public:
    void store (const LookRecord& r) noexcept
    {
        const uint32_t s = seq_.load (std::memory_order_relaxed);
        seq_.store (s + 1u, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        put (0, r.look.valid ? 1 : 0);
        put (1, r.look.view);
        put (2, r.look.slot);
        put (3, r.look.edge);
        put (4, static_cast<int32_t> (r.look.dim256));
        put (5, (r.look.dimmed ? 1 : 0) | (r.look.heldByPanel ? 2 : 0));
        put (6, static_cast<int32_t> (r.fb));
        put (7, static_cast<int32_t> (r.flipSeq));
        put (8, static_cast<int32_t> (r.drawnGen));
        std::atomic_thread_fence (std::memory_order_release);
        seq_.store (s + 2u, std::memory_order_release);
    }
    /** false: nothing was stored yet. */
    bool load (LookRecord& r) const noexcept
    {
        for (int tries = 0; tries < 16; ++tries)
        {
            const uint32_t a = seq_.load (std::memory_order_acquire);
            if (a == 0)
                return false;
            if (a & 1u)
                continue;
            LookRecord t;
            t.look.valid = get (0) != 0;
            t.look.view = get (1);
            t.look.slot = get (2);
            t.look.edge = get (3);
            t.look.dim256 = static_cast<unsigned> (get (4));
            t.look.dimmed = (get (5) & 1) != 0;
            t.look.heldByPanel = (get (5) & 2) != 0;
            t.fb = static_cast<uint32_t> (get (6));
            t.flipSeq = static_cast<uint32_t> (get (7));
            t.drawnGen = static_cast<uint32_t> (get (8));
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq_.load (std::memory_order_relaxed) == a)
            {
                r = t;
                return true;
            }
        }
        return false;
    }
    void clear() noexcept { seq_.store (0, std::memory_order_release); }

private:
    void put (int i, int32_t v) noexcept { f_[i].store (v, std::memory_order_relaxed); }
    int32_t get (int i) const noexcept { return f_[i].load (std::memory_order_relaxed); }
    std::atomic<uint32_t> seq_ { 0 };
    std::atomic<int32_t> f_[9] = {};
};

/** The same for the snapshot of the host's picture (the overlay thread's frame source reads it). */
class SnapshotBox
{
public:
    void store (const Snapshot& s, int rotation) noexcept
    {
        const uint32_t q = seq_.load (std::memory_order_relaxed);
        seq_.store (q + 1u, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        count_.store (s.count, std::memory_order_relaxed);
        rotation_.store (rotation, std::memory_order_relaxed);
        for (int i = 0; i < s.count && i < SamplePlan::kMax; ++i)
            val_[i].store (s.val[i], std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        seq_.store (q + 2u, std::memory_order_release);
    }
    bool load (Snapshot& s, int& rotation) const noexcept
    {
        for (int tries = 0; tries < 16; ++tries)
        {
            const uint32_t a = seq_.load (std::memory_order_acquire);
            if (a == 0)
                return false;
            if (a & 1u)
                continue;
            s.count = count_.load (std::memory_order_relaxed);
            rotation = rotation_.load (std::memory_order_relaxed);
            for (int i = 0; i < s.count && i < SamplePlan::kMax; ++i)
                s.val[i] = val_[i].load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq_.load (std::memory_order_relaxed) == a)
                return true;
        }
        return false;
    }
    void clear() noexcept { seq_.store (0, std::memory_order_release); }
    uint32_t sequence() const noexcept { return seq_.load (std::memory_order_relaxed); }

private:
    std::atomic<uint32_t> seq_ { 0 };
    std::atomic<int> count_ { 0 }, rotation_ { 90 };
    std::atomic<uint32_t> val_[SamplePlan::kMax] = {};
};

}}} // namespace synthyboy_mpc::overlay::inhost
