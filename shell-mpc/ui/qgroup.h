// SynthyBoy - MPC UI component library: QGroup, the four values of one Q-Link set (the frame around the group, the index at each member, the routing of the four hardware knobs, the adjacency check).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>

#include "core.h"

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

    /** The device has 16 Q-Link knobs at once (the MPC XL): the idle chips of the groups that sit on the other three rows of the knobs (setBlock 1 .. 3) show the knob numbers 5 .. 16, and a group that is not mapped shows
        no chip. On a device with four knobs (the Key 61: the bank button steps through the rows) the idle chips stay 1 .. 4, relative to the bank. A process wide setting, made once by the overlay at start. */
    static void setWideDevice (bool wide) noexcept { wideFlag().store (wide, std::memory_order_relaxed); }
    static bool wideDevice() noexcept { return wideFlag().load (std::memory_order_relaxed); }
    /** Which block of four knobs the group has while it is not the active one (QRouter::blockOfGroup): 0 the active group, 1 .. 3 the next groups of the page, -1 no knobs (the default). The page sets it for every group it publishes. */
    void setBlock (int block) noexcept { block_ = block; }
    /** The inactive look (core.h Pen): the frame and the chips are drawn grey too (the group of a switched-off oscillator). */
    void setInactive (bool i) noexcept { inactive_ = i; }
    bool inactive() const noexcept { return inactive_; }
    int block() const noexcept { return block_; }

    /** Puts a member into slot 0 .. 3 (null empties it). The frame of the group follows the members' rectangles when it is drawn. */
    bool set (int slot, Component* c)
    {
        if (slot < 0 || slot >= kSlots)
            return false;
        if (active_ && slot_[slot])
            slot_[slot]->setQ (false, 0);
        slot_[slot] = c;
        if (active_ && c)
            c->setQ (! wideDevice(), slot + 1);
        return true;
    }
    Component* member (int slot) const noexcept { return slot >= 0 && slot < kSlots ? slot_[slot] : nullptr; }
    int numMembers() const noexcept { int n = 0; for (Component* c : slot_) n += c != nullptr; return n; }
    bool active() const noexcept { return active_; }
    void setActive (bool a)
    {
        active_ = a;
        const bool mark = a && ! wideDevice();   // the 16 knob device: no active group, nobody's labels in the Q colour
        for (int i = 0; i < kSlots; ++i)
            if (slot_[i])
                slot_[i]->setQ (mark, i + 1);
    }
    void setFrameRect (const Rect& r) noexcept { frameRect_ = r; }
    /** A group whose members lie APART in the layout (the shape grid and the RATE block of the LFO page): no frame (a frame around the union would take in everything between them), the four active chips and the
        members' Q colour mark the quad; the adjacency check does not apply. */
    void setFrameless (bool f) noexcept { frameless_ = f; }
    bool frameless() const noexcept { return frameless_; }
    Rect frameRect() const noexcept { return frameRect_; }

    /** The union of the members' rectangles. */
    Rect rect() const noexcept
    {
        Rect u;
        for (Component* c : slot_)
            if (c)
                u = Rect::unite (u, c->rect());
        return u;
    }
    /** The one frame around the whole group (nothing when not active), inside the union, and the index of every member as a chip on the member's top left corner (Pen::qIndex): the lane above each
        member (kQLane) is left free by the layouts, so the chip covers no content. */
    void draw (gpu::SceneBuilder& sb, const Rect* around = nullptr) const
    {
        if (wideDevice())   // the 16 knob device (the XL): no frame and no active group; every member of a group that is on the knobs carries its slot number 1 .. 16, all chips in the style of the active ones
        {
            drawWide (sb);
            return;
        }
        if (! active_)
            return;
        const Rect m = frameRect_.empty() ? rect() : frameRect_;
        if (m.empty())
            return;
        // `around`: the rectangle of the frame when the members lie inside cards (a macro card, a block): the ONE frame then runs around the cards, never through them; the chips stay at the members
        const Rect u = around && ! around->empty() ? Rect::unite (*around, m) : m;
        Pen pen (sb, true, inactive_);
        if (! frameless_)
        {
            Cell cell (sb, u);   // the frame lies inside the rectangle of the frame: the cell is repainted when the group becomes active / idle or moves, nothing else is
            pen.rrect (u, theme::L().rControl, pen.c (theme::L().q, 255), 2.0f);
        }
        for (int i = 0; i < kSlots; ++i)
            if (const Component* c = slot_[i]; c && ! c->rect().empty())
                chip (sb, pen, c->rect(), i + 1, false);
    }
    /** The rectangle of the index chip of a member (the same as Pen::qIndex draws): the cell of the chip. */
    static Rect chipRect (const Rect& member, int index) noexcept { return { member.x + 4, std::max (0, member.y - 15), index > 9 ? 26 : 18, 17 }; }
    /** ONE chip in a cell of its own (the helper repaints a cell by its rectangle: the chips of a group in a column or in two rows are far apart, a cell over the union of the members would be big and a cell over the
        first member would not hold the others). */
    static void chip (gpu::SceneBuilder& sb, Pen& pen, const Rect& member, int index, bool idle)
    {
        if (index < 1 || index > 16)
            return;
        Cell cell (sb, chipRect (member, index));
        pen.qIndex (member, index, idle);
    }
    /** The chips of a group on the 16 knob device: the slot number block * 4 + i + 1 on every member, in the one style (the yellow Q chip: legible on every ground, the colour that means "a knob is here"); a group with no
        block (it is not on the knobs) draws nothing; the active group of a page that did not set blocks is block 0. No frame: the frame cell is not emitted. */
    void drawWide (gpu::SceneBuilder& sb) const
    {
        const int block = block_ >= 0 ? block_ : (active_ ? 0 : -1);
        if (block < 0)
            return;
        Pen pen (sb, true, inactive_);
        for (int i = 0; i < kSlots; ++i)
            if (const Component* m = slot_[i]; m && ! m->rect().empty())
                chip (sb, pen, m->rect(), block * kSlots + i + 1, false);
    }
    /** The quiet index chips of a group that is NOT the active one (a bank that a touch on one of its members, or the page stepper, makes the active one): no frame, the four chips in the keyline colours. */
    void drawIdle (gpu::SceneBuilder& sb) const
    {
        if (wideDevice())
        {
            drawWide (sb);
            return;
        }
        if (active_)
            return;
        const Rect u = rect();
        const bool wide = wideDevice();
        if (u.empty() || (wide && block_ < 1))
            return;
        Pen pen (sb, true, inactive_);
        for (int i = 0; i < kSlots; ++i)
            if (const Component* m = slot_[i]; m && ! m->rect().empty())
                chip (sb, pen, m->rect(), wide ? block_ * kSlots + i + 1 : i + 1, true);
    }
    /** The idle chips of a group that sits on one of the other three blocks of knobs (setBlock 1 .. 3) and nothing for the others: the page shows where the next groups are (the XL: the knobs 5 .. 16, the Key 61: the banks 2 .. 4 as 1 .. 4). */
    void drawMapped (gpu::SceneBuilder& sb) const
    {
        if (block_ >= 1 || (wideDevice() && block_ >= 0))
            drawIdle (sb);
    }
    /** The same chips for a group that is not a QGroup of its own: a row of a list whose members are the first `n` (at most 4) of `members` (null: skipped); `block` as setBlock (nothing for a block below 1). */
    static void drawMappedMembers (gpu::SceneBuilder& sb, int block, const Component* const* members, int n)
    {
        if (block < 1 && ! (wideDevice() && block == 0))
            return;
        const bool wide = wideDevice();
        Rect u;
        for (int i = 0; i < n; ++i)
            if (members[i])
                u = Rect::unite (u, members[i]->rect());
        if (u.empty())
            return;
        Pen pen (sb, true, inactive_);
        for (int i = 0; i < n && i < kSlots; ++i)
            if (members[i] && ! members[i]->rect().empty())
                chip (sb, pen, members[i]->rect(), wide ? block * kSlots + i + 1 : i + 1, ! wide);
    }
    /** The hardware knob `knobIndex` (0 .. 3) turned by delta: routed to the member of that slot; nothing when not active or the slot is empty. */
    Change turn (int knobIndex, double delta)
    {
        Component* c = member (knobIndex);
        return active_ && c ? c->turn (delta) : Change::none();
    }
    /** The adjacency check: how many components of `all` (the leaves of the layout) that are not members, and not parts of a member, overlap the union; `first` gets the first such one. */
    int check (Component* const* all, int n, const Component** first = nullptr) const
    {
        if (frameless_)
        {
            if (first)
                *first = nullptr;
            return 0;
        }
        const Rect u = frameRect_.empty() ? rect() : frameRect_;
        Component* own[kSlots * kMaxCheck];
        int nOwn = 0;
        for (Component* c : slot_)
            if (c)
                nOwn = c->collectLeaves (own, nOwn, kSlots * kMaxCheck);
        int bad = 0;
        if (first)
            *first = nullptr;
        for (int i = 0; i < n; ++i)
        {
            const Component* c = all[i];
            bool mine = false;
            for (int j = 0; j < nOwn && ! mine; ++j)
                mine = own[j] == c;
            for (Component* m : slot_)
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

private:
    static std::atomic<bool>& wideFlag() noexcept { static std::atomic<bool> f { false }; return f; }
    Rect frameRect_ = {};
    int block_ = -1;
    bool frameless_ = false, inactive_ = false;
    Component* slot_[kSlots] = {};
    bool active_ = false;
};

}} // namespace synthyboy_mpc::ui
