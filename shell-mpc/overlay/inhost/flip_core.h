// SynthyBoy - MPC in-host drawing (experimental): what happens at one flip of the host, and between flips. Platform independent (memory only), so the tests drive it with a fake framebuffer; the runtime
// (runtime.h) connects it to the real commit hook, the real mappings and the threads.
//   onFlip ()     the hook, on the host's main thread, just before the host's commit: look at the buffer that is about to be shown (analysis: which of our pages, the pop-up edge, the standby dim), copy our latest
//                 complete picture into it (clipped left of the pop-up edge, darkened by the dim factor), write down what the frame is. Bounded work, no allocation, no lock held for long, never throws.
//   afterCommit() the hook, after the commit returned: the buffer is on screen now.
//   frontPass ()  the writer thread, between flips: our picture changed (the overlay thread published a new one) but the host has not flipped (it does not while it is idle): the changed rows are written into the
//                 buffer that is on screen, using what the hook found out about that frame.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "blit.h"
#include "policy.h"
#include "ring.h"
#include "shadow.h"

namespace synthyboy_mpc { namespace overlay { namespace inhost {

/** A mapped framebuffer of the host: XR24 / ARGB8888, `w` x `h` pixels, rows of `pitch` bytes. */
struct FbView
{
    uint8_t* map = nullptr;
    size_t pitch = 0;
    int w = 0, h = 0;
};

/** The framebuffers the hook can name (the host turns two). One writer (the thread that maps), the hook reads: an entry is complete before its id is stored. */
class FbTable
{
public:
    static constexpr int kMax = 4;
    /** The entry of framebuffer `id`, or null. */
    const FbView* find (uint32_t id) const noexcept
    {
        for (int i = 0; i < kMax; ++i)
            if (id != 0 && id_[i].load (std::memory_order_acquire) == id)
                return &view_[i];
        return nullptr;
    }
    bool add (uint32_t id, const FbView& v) noexcept
    {
        for (int i = 0; i < kMax; ++i)
            if (id_[i].load (std::memory_order_relaxed) == 0)
            {
                view_[i] = v;
                id_[i].store (id, std::memory_order_release);
                return true;
            }
        return false;
    }
    /** The id stops being served (the hook no longer uses the entry); the mapping stays until the end. */
    void drop (uint32_t id) noexcept
    {
        for (int i = 0; i < kMax; ++i)
            if (id_[i].load (std::memory_order_relaxed) == id)
                id_[i].store (0, std::memory_order_release);
    }
    void clear() noexcept
    {
        for (int i = 0; i < kMax; ++i)
            id_[i].store (0, std::memory_order_release);
    }
    uint32_t idAt (int i) const noexcept { return id_[i].load (std::memory_order_acquire); }
    const FbView& viewAt (int i) const noexcept { return view_[i]; }
    int count() const noexcept
    {
        int n = 0;
        for (int i = 0; i < kMax; ++i)
            n += id_[i].load (std::memory_order_relaxed) != 0 ? 1 : 0;
        return n;
    }

private:
    std::atomic<uint32_t> id_[kMax] = {};
    FbView view_[kMax];
};

struct Counters
{
    std::atomic<uint32_t> flips { 0 };            // commits that carried a framebuffer of the primary plane
    std::atomic<uint32_t> pageFlips { 0 };        // of them: one of our pages was on screen
    std::atomic<uint32_t> copied { 0 };           // of them: our picture was copied in
    std::atomic<uint32_t> missed { 0 };           // of them: our page was on screen but nothing was copied (no picture, another view's picture, no mapping)
    std::atomic<uint32_t> clipped { 0 };          // copies cut at the pop-up
    std::atomic<uint32_t> dimmedCopies { 0 };     // copies darkened (standby dim)
    std::atomic<uint32_t> unidentified { 0 };     // flips whose buffer could not be named or was not mapped
    std::atomic<uint32_t> noFbItem { 0 };         // commits with the page flip event flag and no framebuffer item of the primary plane
    std::atomic<uint32_t> copyUsSum { 0 }, copyUsMax { 0 };   // the blit alone
    std::atomic<uint32_t> hookUsSum { 0 }, hookUsMax { 0 };   // the whole hook before the commit (analysis + copy)
    std::atomic<uint32_t> readUsSum { 0 }, readUsMax { 0 };   // phase: reading the sample points + detector + pop-up scan + dim test
    std::atomic<uint32_t> aborted { 0 };          // copies stopped at the time budget (the writer thread finishes them after the commit)
    std::atomic<uint32_t> bgFills { 0 }, erased { 0 };   // frames of a page whose picture is not ready: the page's background instead of the host's skin; pictures taken out of an idle buffer
    std::atomic<uint32_t> vblankWaits { 0 };
    std::atomic<uint32_t> overLimit { 0 };        // hooks longer than the limit
    std::atomic<uint32_t> frontWrites { 0 }, frontRows { 0 }, frontUsSum { 0 }, frontUsMax { 0 }, frontRetries { 0 }, frontFull { 0 };
    std::atomic<uint32_t> busySkips { 0 };        // flips left alone because another thread held the hook's state
    std::atomic<uint32_t> fallbacks { 0 }, removals { 0 }, installs { 0 }, fbRemaps { 0 };
    static void bump (std::atomic<uint32_t>& a) noexcept { a.fetch_add (1, std::memory_order_relaxed); }
    static void add (std::atomic<uint32_t>& a, uint32_t v) noexcept { a.fetch_add (v, std::memory_order_relaxed); }
    static void peak (std::atomic<uint32_t>& a, uint32_t v) noexcept
    {
        uint32_t cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
};

class FlipCore
{
public:
    using ClockFn = uint32_t (*) () noexcept;

    struct FlipResult
    {
        bool hadFb = false;       // the request had a framebuffer item
        bool identified = false;  // ... and it is one of the mapped buffers
        bool drew = false;
        uint32_t hookUs = 0;
    };

    FlipCore (FbTable& fbs, FrameRing& ring, ClockFn clock) : fbs_ (fbs), ring_ (ring), clock_ (clock) {}

    Counters counters;
    LookBox look;                 // what the screen shows (from the hook's analysis of each flip, or the writer's own analysis of the front buffer)
    SnapshotBox snapshot;         // the host's pixels the page detector reads, of the last frame that was not dimmed
    std::atomic<bool> inCommit { false };
    std::atomic<uint32_t> flipSeq { 0 };
    std::atomic<int> edgeNow { PanelKey::kNoKey };   // the cut x of the frame on screen (the overlay thread's key follows it)
    std::atomic<bool> backlightDim { false };

    /** The hook, before the commit. `fbId`: the framebuffer the request puts on the primary plane (0: the request has none). */
    /** Waits for the next vblank (set by the runtime; null: no wait). The writer thread only. */
    void (*waitVblank) (void*) = nullptr;
    void* waitVblankCtx = nullptr;

    FlipResult onFlip (uint32_t fbId, const SamplePlan& plan) noexcept
    {
        FlipResult res;
        const uint32_t t0 = clock_();
        if (fbId == 0)
            return res;
        res.hadFb = true;
        Counters::bump (counters.flips);
        const FbView* fb = fbs_.find (fbId);
        inCommit.store (true, std::memory_order_seq_cst);   // from here until afterCommit: the front writer keeps its hands off
        pending_ = true;
        pendingLook_ = LookRecord();
        pendingLook_.fb = fbId;
        if (fb == nullptr || fb->map == nullptr || fb->w != plan.panelW() || fb->h != plan.panelH() || fb->pitch < static_cast<size_t> (fb->w) * 4u)
        {
            Counters::bump (counters.unidentified);
            return res;
        }
        res.identified = true;
        FrameRing::Hold hold;
        const bool have = ring_.acquire (hold);
        const int wantedView = have ? hold.meta.view : -1, wantedSlot = have ? hold.meta.slot : -1;
        const uint32_t r0 = clock_();
        Look lk = analyzer_.analyze (plan, fb->map, fb->pitch, snap_, wantedView, wantedSlot, backlightDim.load (std::memory_order_relaxed));
        const uint32_t r1 = clock_();
        Counters::add (counters.readUsSum, r1 - r0);
        Counters::peak (counters.readUsMax, r1 - r0);
        pendingLook_.look = lk;
        if (! lk.dimmed)
        {
            const PubGuard g (publishLock_);
            snapshot.store (snap_, plan.rotation());
        }
        if (lk.valid)
        {
            Counters::bump (counters.pageFlips);
            if (have && pictureFits (hold, lk, plan, *fb))
            {
                const uint32_t b0 = clock_();
                const Rect clip = clipToEdge (hold.pic.panel, plan.rotation(), lk.edge, plan.panelW(), plan.panelH());
                // a frame that goes out is complete: the whole page area is copied, whatever it costs. The far guard is the policy (policy.h): a hook over 12 ms is a fault, the plane overlay takes over.
                blitRows (fb->map, fb->pitch, fb->w, fb->h, hold.pic, clip, lk.dim256, 0, hold.pic.h);
                std::atomic_thread_fence (std::memory_order_seq_cst);   // the write-combined stores are out before the commit
                const uint32_t b1 = clock_();
                pendingLook_.drawnGen = hold.meta.gen;
                res.drew = true;
                Counters::bump (counters.copied);
                if (lk.edge < PanelKey::kNoKey)
                    Counters::bump (counters.clipped);
                if (lk.dimmed)
                    Counters::bump (counters.dimmedCopies);
                Counters::add (counters.copyUsSum, b1 - b0);
                Counters::peak (counters.copyUsMax, b1 - b0);
            }
            else
            {
                // our page is on screen but its picture is not ready (a tab was just changed, another instance's picture, nothing published yet): the host's skin of the page would show for these frames, so the
                // page's own dark background is put over its region instead (the picture follows within a few frames, the skin never shows)
                const Rect region = regionPanel (lk.view, plan);
                const Rect clip = clipToEdge (region, plan.rotation(), lk.edge, plan.panelW(), plan.panelH());
                fillRect (fb->map, fb->pitch, fb->w, fb->h, region, clip, layout::kBackgroundRgb, lk.dim256);
                std::atomic_thread_fence (std::memory_order_seq_cst);
                Counters::bump (counters.bgFills);
                Counters::bump (counters.missed);
            }
        }
        if (have)
            ring_.release (hold);
        res.hookUs = clock_() - t0;
        Counters::add (counters.hookUsSum, res.hookUs);
        Counters::peak (counters.hookUsMax, res.hookUs);
        return res;
    }

    /** The hook, after the commit returned `ret`. */
    void afterCommit (int ret) noexcept
    {
        if (pending_ && ret == 0)
        {
            const uint32_t seq = flipSeq.load (std::memory_order_relaxed) + 1u;
            pendingLook_.flipSeq = seq;
            const PubGuard g (publishLock_);
            look.store (pendingLook_);
            edgeNow.store (pendingLook_.look.valid ? pendingLook_.look.edge : PanelKey::kNoKey, std::memory_order_relaxed);
            flipSeq.store (seq, std::memory_order_release);
        }
        pending_ = false;
        inCommit.store (false, std::memory_order_seq_cst);
    }

    /** The writer thread: look at the buffer that is on screen when no look of it exists (nothing has flipped since the hook was armed, or the backlight changed): the buffer holds the host's pixels only (we never
        wrote into it). Stores the look when the host did not flip meanwhile. */
    bool analyseFront (uint32_t fbId, const SamplePlan& plan) noexcept
    {
        const FbView* fb = fbs_.find (fbId);
        if (fb == nullptr || fb->w != plan.panelW() || fb->h != plan.panelH() || inCommit.load())
            return false;
        if (! tryEnter())   // the hook's analyzer and snapshot: one user at a time (a flip that finds it taken draws nothing: counted as busy skip)
            return false;
        struct Leave { FlipCore& c; ~Leave() { c.leave(); } } leaveGuard { *this };
        const uint32_t seqBefore = flipSeq.load (std::memory_order_acquire);
        FrameRing::Hold hold;
        const bool have = ring_.acquire (hold);
        const Look lk = analyzer_.analyze (plan, fb->map, fb->pitch, snap_, have ? hold.meta.view : -1, have ? hold.meta.slot : -1, backlightDim.load (std::memory_order_relaxed));
        if (have)
            ring_.release (hold);
        if (inCommit.load() || flipSeq.load (std::memory_order_acquire) != seqBefore)
            return false;
        const PubGuard g (publishLock_);
        LookRecord r;
        r.look = lk;
        r.fb = fbId;
        r.flipSeq = seqBefore;
        look.store (r);
        edgeNow.store (lk.valid ? lk.edge : PanelKey::kNoKey, std::memory_order_relaxed);
        if (! lk.dimmed)
            snapshot.store (snap_, plan.rotation());
        return true;
    }

    /** The writer thread: the host is idle, our picture may have changed. Writes the changed rows into the buffer on screen. Returns true when something was written (or is known to be right). `needAgain` is set when
        a flip got in the way and the pass must be repeated soon. */
    bool frontPass (const SamplePlan& plan, bool& needAgain) noexcept
    {
        needAgain = false;
        if (inCommit.load (std::memory_order_seq_cst))
        {
            needAgain = true;
            return false;
        }
        LookRecord L;
        if (! look.load (L) || ! L.look.valid)
            return false;
        FrameRing::Hold hold;
        if (! ring_.acquire (hold))
            return false;
        struct Release { FrameRing& r; FrameRing::Hold& h; ~Release() { r.release (h); } } rel { ring_, hold };
        const FbView* fb = fbs_.find (L.fb);
        if (fb == nullptr || fb->map == nullptr || fb->w != plan.panelW() || fb->h != plan.panelH())
            return false;
        if (! pictureFits (hold, L.look, plan, *fb))
            return false;
        const bool sameFrame = belief_.valid && belief_.fb == L.fb && belief_.flipSeq == L.flipSeq && belief_.panel == hold.pic.panel && belief_.w == hold.pic.w && belief_.h == hold.pic.h;
        if (L.drawnGen == hold.meta.gen && L.drawnGen != 0)
        {
            // the hook put exactly this picture into this frame: remember it as what the buffer holds
            keep (hold, 0, hold.pic.h);
            belief_ = { true, L.fb, L.flipSeq, hold.meta.gen, hold.pic.panel, hold.pic.w, hold.pic.h };
            return true;
        }
        int r0 = 0, r1 = hold.pic.h;
        bool full = ! sameFrame;
        if (! full)
        {
            const size_t rowBytes = static_cast<size_t> (hold.pic.w) * 4u;
            r0 = hold.pic.h;
            r1 = 0;
            for (int r = 0; r < hold.pic.h; ++r)
                if (std::memcmp (hold.pic.pix + static_cast<size_t> (r) * hold.pic.w, mirror_.data() + static_cast<size_t> (r) * hold.pic.w, rowBytes) != 0 || hold.pic.kinds[r] != mirrorKinds_[static_cast<size_t> (r)])
                {
                    r0 = std::min (r0, r);
                    r1 = r + 1;
                }
            if (r1 <= r0)
            {
                belief_.gen = hold.meta.gen;
                return true;   // nothing differs from what the buffer holds
            }
        }
        if (waitVblank != nullptr)
        {
            waitVblank (waitVblankCtx);   // the writes follow the scan-out of the frame from its start (they are faster than the scan): no tear line inside the changed rows
            Counters::bump (counters.vblankWaits);
        }
        const uint32_t seqBefore = flipSeq.load (std::memory_order_acquire);
        if (inCommit.load (std::memory_order_seq_cst))
        {
            needAgain = true;
            return false;
        }
        const uint32_t t0 = clock_();
        const Rect clip = clipToEdge (hold.pic.panel, plan.rotation(), L.look.edge, plan.panelW(), plan.panelH());
        blitRows (fb->map, fb->pitch, fb->w, fb->h, hold.pic, clip, L.look.dim256, r0, r1);
        std::atomic_thread_fence (std::memory_order_seq_cst);
        const uint32_t dt = clock_() - t0;
        if (inCommit.load (std::memory_order_seq_cst) || flipSeq.load (std::memory_order_acquire) != seqBefore)
        {
            // a flip began while we wrote: the buffer we wrote into is the one that goes off screen (or the new one was copied from an older picture): start again on the new front
            belief_.valid = false;
            Counters::bump (counters.frontRetries);
            needAgain = true;
            return false;
        }
        keep (hold, r0, r1);
        belief_ = { true, L.fb, L.flipSeq, hold.meta.gen, hold.pic.panel, hold.pic.w, hold.pic.h };
        Counters::bump (counters.frontWrites);
        Counters::add (counters.frontRows, static_cast<uint32_t> (r1 - r0));
        if (full)
            Counters::bump (counters.frontFull);
        Counters::add (counters.frontUsSum, dt);
        Counters::peak (counters.frontUsMax, dt);
        return true;
    }

    /** The picture of the frame is gone (the overlay hid it): the belief about the buffer's content is dropped. */
    void forgetFront() noexcept { belief_.valid = false; }
    /** The overlay hid our picture while the host is idle (no flip will repaint the buffer): the page's region of the buffer on screen gets the skin's dark page background instead of a picture that is no longer
        ours to show. (The host's own pixels under it are not kept: reading them back from write-combined memory costs 14 ms; for the overlay pages they are this background anyway: the skin hides the host
        widgets there.) Writer thread. */
    void eraseFront (const SamplePlan& plan) noexcept
    {
        if (! belief_.valid || inCommit.load (std::memory_order_seq_cst))
            return;
        LookRecord L;
        const FbView* fb = look.load (L) ? fbs_.find (L.fb) : nullptr;
        const bool ours = fb != nullptr && L.look.valid && belief_.fb == L.fb && belief_.flipSeq == L.flipSeq && belief_.flipSeq == flipSeq.load (std::memory_order_acquire);
        if (ours)
        {
            const Rect region = regionPanel (L.look.view, plan);
            fillRect (fb->map, fb->pitch, fb->w, fb->h, region, clipToEdge (region, plan.rotation(), L.look.edge, plan.panelW(), plan.panelH()), layout::kBackgroundRgb, L.look.dim256);
            std::atomic_thread_fence (std::memory_order_seq_cst);
            Counters::bump (counters.erased);
        }
        belief_.valid = false;
    }
    void reset() noexcept
    {
        analyzer_.reset();
        belief_.valid = false;
        pending_ = false;
        look.clear();
        snapshot.clear();
        inCommit.store (false);
        edgeNow.store (PanelKey::kNoKey);
    }
    /** The hook's analyzer state is single-threaded (the host's commit thread); a second thread in the hook at the same time is left alone. */
    bool tryEnter() noexcept { return ! busy_.test_and_set (std::memory_order_acquire); }
    void leave() noexcept { busy_.clear (std::memory_order_release); }

private:
    /** The two threads that store into the boxes (the hook, the writer's own analysis of the front buffer) take turns; the holds are a few stores long. */
    struct PubGuard
    {
        explicit PubGuard (std::atomic_flag& f) : flag (f)
        {
            for (int spin = 0; spin < 20000 && flag.test_and_set (std::memory_order_acquire); ++spin) {}
        }
        ~PubGuard() { flag.clear (std::memory_order_release); }
        std::atomic_flag& flag;
    };

    struct Belief
    {
        bool valid = false;
        uint32_t fb = 0, flipSeq = 0, gen = 0;
        Rect panel;
        int w = 0, h = 0;
    };

    static Rect regionPanel (int view, const SamplePlan& plan) noexcept
    {
        const layout::View& v = layout::kViews[view >= 0 && view < layout::kNumViews ? view : 0];
        return uiRectToPanel (plan.rotation(), { v.region.x, v.region.y, v.region.w, v.region.h }, plan.panelW(), plan.panelH());
    }
    bool pictureFits (const FrameRing::Hold& hold, const Look& lk, const SamplePlan& plan, const FbView& fb) const noexcept
    {
        if (hold.meta.view != lk.view || hold.meta.rotation != plan.rotation())
            return false;
        if (lk.slot != kSlotUnknown && hold.meta.slot != lk.slot)
            return false;
        const Rect& p = hold.pic.panel;
        return p.w == hold.pic.w && p.h == hold.pic.h && p.x >= 0 && p.y >= 0 && p.x + p.w <= fb.w && p.y + p.h <= fb.h;
    }
    /** The rows [r0, r1) of the picture are what the buffer holds now (the next pass compares the picture with this copy). */
    void keep (const FrameRing::Hold& hold, int r0, int r1)
    {
        const size_t need = static_cast<size_t> (hold.pic.w) * static_cast<size_t> (hold.pic.h);
        if (mirror_.size() != need)
        {
            mirror_.assign (need, 0u);   // (the writer thread: allocation is allowed here)
            mirrorKinds_.assign (static_cast<size_t> (hold.pic.h), 0);
            r0 = 0;
            r1 = hold.pic.h;
        }
        std::memcpy (mirror_.data() + static_cast<size_t> (r0) * hold.pic.w, hold.pic.pix + static_cast<size_t> (r0) * hold.pic.w, static_cast<size_t> (r1 - r0) * hold.pic.w * 4u);
        std::memcpy (mirrorKinds_.data() + r0, hold.pic.kinds + r0, static_cast<size_t> (r1 - r0));
    }

    FbTable& fbs_;
    FrameRing& ring_;
    ClockFn clock_;
    Analyzer analyzer_;
    Snapshot snap_;
    LookRecord pendingLook_;
    bool pending_ = false;
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT, publishLock_ = ATOMIC_FLAG_INIT;
    Belief belief_;
    std::vector<uint32_t> mirror_;
    std::vector<uint8_t> mirrorKinds_;
};

}}} // namespace synthyboy_mpc::overlay::inhost
