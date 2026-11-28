// SynthyBoy - MPC UI component library: QGroup, the four values of one Q-Link set (the frame around the group, the index at each member, the routing of the four hardware knobs, the adjacency check).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>

#include "core.h"
#include "model.h"

namespace synthyboy_mpc { namespace ui {

/** The four values of one Q-Link set (the four hardware knobs Q-Link 1 .. 4 control them at a time). Four slots, each a component (a leaf, or a composite whose turn goes to its primary
    child); the rectangle of the group is the union of the members' rectangles. Active: the members get the accent frame colour and their index 1 .. 4 (setQ), the group draws ONE
    frame around the union, and turn (knobIndex, delta) is routed to the member. Members must be adjacent in the layout: `check` reports every component of the layout that is not a
    member (or a part of one) but lies in the union. */
class QGroup
{
public:
    static constexpr int kSlots = 4;
    static constexpr int kMaxCheck = 64;

    /** The sixteen-knob device maps all four banks at once. The four-knob device has one active bank. */
    static void setWideDevice (bool wide) noexcept { wideFlag().store (wide, std::memory_order_relaxed); }
    static bool wideDevice() noexcept { return wideFlag().load (std::memory_order_relaxed); }
    /** Which block of four knobs the group has while it is not the active one (QRouter::blockOfGroup): 0 the active group, 1 .. 3 the next groups of the page, -1 no knobs (the default). The page sets it for every group it publishes. */
    void setBlock (int block) noexcept { block_ = block; wideOffset_ = 0; }
    /** The frame and marker follow the inactive look of a switched-off oscillator. */
    void setInactive (bool i) noexcept { inactive_ = i; }
    bool inactive() const noexcept { return inactive_; }
    int block() const noexcept { return block_; }

    /** Puts a member into slot 0 .. 3 (null empties it). The frame of the group follows the members' rectangles when it is drawn. */
    bool set (int slot, ControlModel* c)
    {
        if (slot < 0 || slot >= kSlots)
            return false;
        if (active_ && slot_[slot])
            slot_[slot]->setQ (false, 0);
        slot_[slot] = c;
        if (active_ && c)
        {
#if SYNTHYBOY_MPC_QFRAME_ENABLED
            c->setQ (! wideDevice(), slot + 1);
#else
            c->setQ (false, slot + 1);
#endif
        }
        return true;
    }
    ControlModel* member (int slot) const noexcept { return slot >= 0 && slot < kSlots ? slot_[slot] : nullptr; }
    int numMembers() const noexcept { int n = 0; for (ControlModel* c : slot_) n += c != nullptr; return n; }
    bool active() const noexcept { return active_; }
    void setActive (bool a)
    {
        active_ = a;
#if SYNTHYBOY_MPC_QFRAME_ENABLED
        const bool mark = a && ! wideDevice();   // the 16 knob device: no active group, nobody's labels in the Q colour
#else
        const bool mark = false;
#endif
        for (int i = 0; i < kSlots; ++i)
            if (slot_[i])
                slot_[i]->setQ (mark, i + 1);
    }
    void setFrameRect (const Rect& r) noexcept { frameRect_ = r; }
    bool frameless() const noexcept { return frameless_; }

    /** The union of the members' rectangles. */
    Rect rect() const noexcept
    {
        Rect u;
        for (ControlModel* c : slot_)
            if (c)
                u = Rect::unite (u, c->rect());
        return u;
    }
    /** The one frame around the whole group (nothing when not active), inside the union, and a single unnumbered
        Q marker in its top-left corner (Pen::qFocus). Zero visible Q-Link serial numbers or individual numbered chips.
        In Gen 2 builds, the frame and marker drawing is completely compiled out. */
    void draw (gpu::SceneBuilder& sb, const Rect* around = nullptr) const
    {
#if SYNTHYBOY_MPC_QFRAME_ENABLED
        if (wideDevice())   // the 16 knob device (the XL): all knobs are mapped, no active group frame
            return;
        if (! active_)
            return;
        const Rect m = frameRect_.empty() ? rect() : frameRect_;
        if (m.empty())
            return;
        // Keep the frame outside any cards surrounding the members.
        const Rect u = around && ! around->empty() ? Rect::unite (*around, m) : m;
        Pen pen (sb, true, inactive_);
        if (! frameless_)
        {
            Cell cell (sb, Rect::unite (u, Pen::qMarkerRect (u)));
            pen.qFocus (u);
        }
#else
        (void) sb;
        (void) around;
#endif
    }
    static void drawMappedMembers (gpu::SceneBuilder&, int, const ControlModel* const*, int) {}
    static void drawMappedMembers (gpu::SceneBuilder&, int, const Component* const*, int) {}

    /** Route a hardware turn to the active group's member at the same index. */
    Change turn (int knobIndex, double delta)
    {
        ControlModel* c = member (knobIndex);
        return active_ && c ? c->turn (delta) : Change::none();
    }
    /** The adjacency check: how many models/components of `all` (the leaves of the layout) that are not members, and not parts of a member, overlap the union; `first` gets the first such one. */
    int check (ControlModel* const* all, int n, const ControlModel** first = nullptr) const
    {
        if (frameless_)
        {
            if (first)
                *first = nullptr;
            return 0;
        }
        const Rect u = frameRect_.empty() ? rect() : frameRect_;
        ControlModel* own[kSlots * kMaxCheck];
        int nOwn = 0;
        for (ControlModel* c : slot_)
            if (c)
                nOwn = c->collectModelLeaves (own, nOwn, kSlots * kMaxCheck);
        int bad = 0;
        if (first)
            *first = nullptr;
        for (int i = 0; i < n; ++i)
        {
            const ControlModel* c = all[i];
            bool mine = false;
            for (int j = 0; j < nOwn && ! mine; ++j)
                mine = own[j] == c;
            for (ControlModel* m : slot_)
                mine = mine || m == c;
            if (! mine && c->rect().overlaps (u))
            {
                if (bad == 0 && first)
                    *first = c;
                ++bad;
            }
        }
        return bad;
    }
    int check (Component* const* all, int n, const Component** first = nullptr) const
    {
        if (frameless_)
        {
            if (first)
                *first = nullptr;
            return 0;
        }
        const Rect u = frameRect_.empty() ? rect() : frameRect_;
        ControlModel* own[kSlots * kMaxCheck];
        int nOwn = 0;
        for (ControlModel* c : slot_)
            if (c)
                nOwn = c->collectModelLeaves (own, nOwn, kSlots * kMaxCheck);
        int bad = 0;
        if (first)
            *first = nullptr;
        for (int i = 0; i < n; ++i)
        {
            const Component* c = all[i];
            const ControlModel* cm = c;
            bool mine = false;
            for (int j = 0; j < nOwn && ! mine; ++j)
                mine = own[j] == cm;
            for (ControlModel* m : slot_)
                mine = mine || m == cm;
            if (! mine && c->rect().overlaps (u))
            {
                if (bad == 0 && first)
                    *first = c;
                ++bad;
            }
        }
        return bad;
    }

private:
    static std::atomic<bool>& wideFlag() noexcept { static std::atomic<bool> f { false }; return f; }
    Rect frameRect_ = {};
    int block_ = -1, wideOffset_ = 0;
    bool frameless_ = false, inactive_ = false;
    ControlModel* slot_[kSlots] = {};
    bool active_ = false;
};

}} // namespace synthyboy_mpc::ui
