// SynthyBoy - MPC in-host drawing (experimental): what the hook learns about the host's picture, from the buffer that is about to be shown. Our own picture is drawn INTO that buffer, over the two marker
// squares of the page, so the overlay's usual look at the screen (page_detector.h, the host's primary framebuffer) would see our own picture instead of the host's. The hook therefore reads the pixels the
// detector and the pop-up edge need from the host's fresh frame, BEFORE it draws (a few hundred reads of write-combined memory: the SamplePlan), keeps them as a snapshot, runs the SAME PageDetector over the
// snapshot and hands the snapshot on: the overlay thread's frame source (InHostFrameSource) answers its reads from it. One frame, one decision.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../drm_interfaces.h"
#include "../geometry.h"
#include "../overlay_layout.h"
#include "../page_detector.h"
#include "../panel_key.h"

namespace synthyboy_mpc { namespace overlay { namespace inhost {

/** The panel pixels the page detector reads (the markers and the instance picture of every view, the signature of the host's Q-Link panel) under one rotation, sorted by position. Built outside the hook. */
class SamplePlan
{
public:
    static constexpr int kMax = 640;

    void build (int rotation, int panelW, int panelH) noexcept
    {
        rotation_ = normaliseRotation (rotation);
        panelW_ = panelW;
        panelH_ = panelH;
        count_ = 0;
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            const layout::View& v = layout::kViews[i];
            for (int m = 0; m < 2; ++m)
                addRect (v.markers[m].rect.x, v.markers[m].rect.y, v.markers[m].rect.w, v.markers[m].rect.h);
            addRect (v.instance.x, v.instance.y, v.instance.w, v.instance.h);
        }
        for (int y : PageDetector::kPanelRows)
        {
            const int xs[6] = { PageDetector::kPanelLineX - 2, PageDetector::kPanelLineX, PageDetector::kPanelLineX + 1, PageDetector::kPanelLineX + 6, PageDetector::kPanelLineX + 60, 1270 };
            for (int x : xs)
                add (x, y);
        }
        std::sort (key_, key_ + count_);
        int n = 0;
        for (int i = 0; i < count_; ++i)
            if (n == 0 || key_[i] != key_[n - 1])
                key_[n++] = key_[i];
        count_ = n;
    }
    int count() const noexcept { return count_; }
    int rotation() const noexcept { return rotation_; }
    int panelW() const noexcept { return panelW_; }
    int panelH() const noexcept { return panelH_; }
    int px (int i) const noexcept { return static_cast<int> (key_[i] % static_cast<uint32_t> (panelW_)); }
    int py (int i) const noexcept { return static_cast<int> (key_[i] / static_cast<uint32_t> (panelW_)); }
    /** Index of the point (px, py), -1 when it is not in the plan. */
    int find (int x, int y) const noexcept
    {
        if (x < 0 || y < 0 || x >= panelW_ || y >= panelH_)
            return -1;
        const uint32_t k = static_cast<uint32_t> (y) * static_cast<uint32_t> (panelW_) + static_cast<uint32_t> (x);
        const uint32_t* it = std::lower_bound (key_, key_ + count_, k);
        return it != key_ + count_ && *it == k ? static_cast<int> (it - key_) : -1;
    }

private:
    // the detector's five points of a rectangle (the four corners of the inset square and the centre: page_detector.h check () / readInstance ())
    void addRect (int x, int y, int w, int h) noexcept
    {
        const int ix = w > 3 ? 1 : 0, iy = h > 3 ? 1 : 0;
        const int xs[5] = { x + ix, x + w - 1 - ix, x + ix, x + w - 1 - ix, x + w / 2 };
        const int ys[5] = { y + iy, y + iy, y + h - 1 - iy, y + h - 1 - iy, y + h / 2 };
        for (int k = 0; k < 5; ++k)
            add (xs[k], ys[k]);
    }
    void add (int x, int y) noexcept
    {
        int px, py;
        uiPointToPanel (rotation_, x, y, panelW_, panelH_, px, py);
        if (px < 0 || py < 0 || px >= panelW_ || py >= panelH_ || count_ >= kMax)
            return;
        key_[count_++] = static_cast<uint32_t> (py) * static_cast<uint32_t> (panelW_) + static_cast<uint32_t> (px);
    }

    uint32_t key_[kMax] = {};
    int count_ = 0, rotation_ = 90, panelW_ = 800, panelH_ = 1280;
};

/** The values of a plan's points in one frame of the host. */
struct Snapshot
{
    uint32_t val[SamplePlan::kMax] = {};
    int count = 0;
};

/** Reads the plan's points from a mapped buffer (XR24 / ARGB, rows of pitchBytes, panelW x panelH pixels). Volatile reads: write-combined memory, each one is a bus read. */
inline void fillSnapshot (const SamplePlan& plan, const uint8_t* buf, size_t pitchBytes, Snapshot& out) noexcept
{
    out.count = plan.count();
    for (int i = 0; i < out.count; ++i)
        out.val[i] = *reinterpret_cast<const volatile uint32_t*> (buf + static_cast<size_t> (plan.py (i)) * pitchBytes + static_cast<size_t> (plan.px (i)) * 4u) & 0xffffffu;
}

/** An IFrameSource over a snapshot: a read of a point that is not in the plan fails (and is counted: the plan must hold everything the detector reads). */
class SnapshotSource final : public IFrameSource
{
public:
    SnapshotSource (const SamplePlan& plan, const Snapshot& snap) : plan_ (plan), snap_ (snap) {}
    bool begin (int& w, int& h) override { w = plan_.panelW(); h = plan_.panelH(); return snap_.count == plan_.count(); }
    bool pixel (int px, int py, uint32_t& rgb) noexcept override
    {
        const int i = plan_.find (px, py);
        if (i < 0)
        {
            ++misses_;
            return false;
        }
        rgb = snap_.val[i];
        return true;
    }
    void end() noexcept override {}
    int misses() const noexcept { return misses_; }

private:
    const SamplePlan& plan_;
    const Snapshot& snap_;
    int misses_ = 0;
};

/** The sample pixels of the table the detector's views are built from (the same arrays the controller makes). */
struct ViewTables
{
    MarkerSpec markers[Detection::kMaxViews * 2];
    Rect instances[Detection::kMaxViews];
    Rect regions[Detection::kMaxViews];
    int count = 0;
    ViewTables() noexcept
    {
        count = std::min (layout::kNumViews, static_cast<int> (Detection::kMaxViews));
        for (int i = 0; i < count; ++i)
        {
            const layout::View& v = layout::kViews[i];
            for (int m = 0; m < 2; ++m)
                markers[i * 2 + m] = { { v.markers[m].rect.x, v.markers[m].rect.y, v.markers[m].rect.w, v.markers[m].rect.h }, v.markers[m].rgb };
            instances[i] = { v.instance.x, v.instance.y, v.instance.w, v.instance.h };
            regions[i] = { v.region.x, v.region.y, v.region.w, v.region.h };
        }
    }
};

/** What the hook decided about one frame of the host. */
struct Look
{
    bool valid = false;               // one of our pages is on screen in this frame (a picture of it may be drawn)
    int view = -1;                    // the view (table index)
    int slot = kSlotUnknown;          // the instance read from the instance picture (kSlotUnknown: not told: any instance's picture of that view may be drawn)
    int edge = PanelKey::kNoKey;      // the picture is shown left of this UI x (the host's Q-Link status pop-up is right of it)
    unsigned dim256 = 256;            // 256: as it is; less: the host's standby dim of the frame, our picture is darkened by dim256 / 256
    bool dimmed = false;
    bool heldByPanel = false;         // the right marker is under the host's pop-up
};

class Analyzer
{
public:
    static constexpr int kRefPoints = 12;

    Analyzer() = default;

    void reset() noexcept { prevView_ = -1; haveRef_ = false; }
    int lastView() const noexcept { return prevView_; }

    /** Reads the plan's points of `buf` into `snap`, decides what is on screen. `wantedView` / `wantedSlot`: the picture the overlay has ready (-1: none), used to hold a page under the pop-up (its right marker is covered).
        `backlightDim`: the backlight was lowered (the standby dim; a modal scrim would not lower it). No allocation, no system call. */
    Look analyze (const SamplePlan& plan, const uint8_t* buf, size_t pitchBytes, Snapshot& snap, int wantedView, int wantedSlot, bool backlightDim) noexcept
    {
        Look out;
        fillSnapshot (plan, buf, pitchBytes, snap);
        SnapshotSource src (plan, snap);
        PageDetector det (src, tables_.markers, tables_.count, tables_.instances);
        const Detection d = det.check (plan.rotation());
        lastMisses_ = src.misses();
        const int W = plan.panelW(), H = plan.panelH();
        const bool candidate = d.view >= 0 || d.partialView >= 0;
        int edge = PanelKey::kNoKey;
        if (candidate)
            edge = scanEdge (buf, pitchBytes, plan.rotation(), W, H);
        if (d.view >= 0)
        {
            out.valid = true;
            out.view = d.view;
            out.slot = d.slot == kSlotNone ? kSlotUnknown : d.slot;
            out.edge = edge;
            out.heldByPanel = d.qlinkPanel;
            remember (buf, pitchBytes, plan.rotation(), W, H, d.view, out.slot);
            return out;
        }
        if (d.partialView >= 0 && d.partialView == wantedView && edge < PanelKey::kNoKey)
        {
            out.valid = true;
            out.view = d.partialView;
            out.slot = wantedSlot;
            out.edge = edge;
            out.heldByPanel = true;
            return out;
        }
        if (backlightDim && prevView_ >= 0 && haveRef_)
        {
            unsigned f = 0;
            if (dimConsistent (buf, pitchBytes, plan.rotation(), W, H, f))
            {
                out.valid = true;
                out.view = prevView_;
                out.slot = prevSlot_;
                out.dimmed = true;
                out.dim256 = f;
                return out;
            }
        }
        return out;
    }

    /** The cut of the picture at the host's Q-Link status pop-up: the black line (two columns, kX0 .. kX1, in most of the 16 sample rows) of the band the host draws. The same rule as PanelKey::findLine, with the middle
        row tried first (about 220 reads when there is no pop-up). PanelKey::kNoKey: none. */
    static int scanEdge (const uint8_t* buf, size_t pitchBytes, int rotation, int W, int H) noexcept
    {
        const int mid = PanelKey::kRows / 2;
        for (int c = 0; c < PanelKey::kCols; ++c)
        {
            if (! black (buf, pitchBytes, rotation, W, H, c, mid))
                continue;
            int n = 0;
            for (int r = 0; r < PanelKey::kRows; ++r)
                if (r == mid || black (buf, pitchBytes, rotation, W, H, c, r))
                    ++n;
            if (n >= PanelKey::kBandRows)
                return std::max (PanelKey::kX0, PanelKey::kX0 + c * PanelKey::kStep - PanelKey::kGrow);
        }
        return PanelKey::kNoKey;
    }

    int lastMisses() const noexcept { return lastMisses_; }

private:
    static bool black (const uint8_t* buf, size_t pitchBytes, int rotation, int W, int H, int c, int r) noexcept
    {
        int px, py;
        uiPointToPanel (rotation, PanelKey::kX0 + c * PanelKey::kStep, PanelKey::rowY (r), W, H, px, py);
        if (px < 0 || py < 0 || px >= W || py >= H)
            return false;
        const uint32_t v = *reinterpret_cast<const volatile uint32_t*> (buf + static_cast<size_t> (py) * pitchBytes + static_cast<size_t> (px) * 4u);
        return (v & 0xffu) <= static_cast<uint32_t> (PanelKey::kTolerance) && ((v >> 8) & 0xffu) <= static_cast<uint32_t> (PanelKey::kTolerance) && ((v >> 16) & 0xffu) <= static_cast<uint32_t> (PanelKey::kTolerance);
    }

    /** The twelve reference points of a view (inside its region, away from the squares): the host's picture under our page is static, so they are what the dimmed frame is compared with. */
    static void refPoint (const Rect& region, int k, int& x, int& y) noexcept
    {
        static const int fx[3] = { 20, 50, 80 }, fy[4] = { 28, 44, 62, 78 };
        x = region.x + region.w * fx[k % 3] / 100;
        y = region.y + region.h * fy[k / 3] / 100;
    }
    static uint32_t readUi (const uint8_t* buf, size_t pitchBytes, int rotation, int W, int H, int x, int y) noexcept
    {
        int px, py;
        uiPointToPanel (rotation, x, y, W, H, px, py);
        if (px < 0 || py < 0 || px >= W || py >= H)
            return 0;
        return *reinterpret_cast<const volatile uint32_t*> (buf + static_cast<size_t> (py) * pitchBytes + static_cast<size_t> (px) * 4u) & 0xffffffu;
    }
    void remember (const uint8_t* buf, size_t pitchBytes, int rotation, int W, int H, int view, int slot) noexcept
    {
        prevView_ = view;
        prevSlot_ = slot;
        for (int k = 0; k < kRefPoints; ++k)
        {
            int x, y;
            refPoint (tables_.regions[view], k, x, y);
            ref_[k] = readUi (buf, pitchBytes, rotation, W, H, x, y);
        }
        haveRef_ = true;
    }
    bool dimConsistent (const uint8_t* buf, size_t pitchBytes, int rotation, int W, int H, unsigned& f256) const noexcept
    {
        uint32_t cur[kRefPoints];
        unsigned long sumRef = 0, sumCur = 0;
        int usable = 0;
        for (int k = 0; k < kRefPoints; ++k)
        {
            int x, y;
            refPoint (tables_.regions[prevView_], k, x, y);
            cur[k] = readUi (buf, pitchBytes, rotation, W, H, x, y);
            const unsigned r = (ref_[k] & 255u) + ((ref_[k] >> 8) & 255u) + ((ref_[k] >> 16) & 255u);
            if (r >= 60u)
            {
                sumRef += r;
                sumCur += (cur[k] & 255u) + ((cur[k] >> 8) & 255u) + ((cur[k] >> 16) & 255u);
                ++usable;
            }
        }
        if (usable < 8 || sumRef == 0)
            return false;
        const unsigned f = static_cast<unsigned> (sumCur * 256ul / sumRef);
        if (f < 64u || f > 230u)
            return false;
        int good = 0;
        for (int k = 0; k < kRefPoints; ++k)
        {
            bool ok = true;
            for (int shift = 0; shift <= 16; shift += 8)
            {
                const int want = static_cast<int> (((ref_[k] >> shift) & 255u) * f / 256u), got = static_cast<int> ((cur[k] >> shift) & 255u);
                if (std::abs (want - got) > 4)
                    ok = false;
            }
            good += ok ? 1 : 0;
        }
        if (good * 4 < kRefPoints * 3)
            return false;
        f256 = f;
        return true;
    }

    ViewTables tables_;
    int prevView_ = -1, prevSlot_ = kSlotUnknown, lastMisses_ = 0;
    bool haveRef_ = false;
    uint32_t ref_[kRefPoints] = {};
};

}}} // namespace synthyboy_mpc::overlay::inhost
