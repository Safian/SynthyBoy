// SynthyBoy - MPC UI component library: QGroup, the four values of one Q-Link set (the frame around the group, the index at each member, the routing of the four hardware knobs, the adjacency check).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

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

    /** Puts a member into slot 0 .. 3 (null empties it). The frame of the group follows the members' rectangles when it is drawn. */
    bool set (int slot, Component* c)
    {
        if (slot < 0 || slot >= kSlots)
            return false;
        if (active_ && slot_[slot])
            slot_[slot]->setQ (false, 0);
        slot_[slot] = c;
        if (active_ && c)
            c->setQ (true, slot + 1);
        return true;
    }
    Component* member (int slot) const noexcept { return slot >= 0 && slot < kSlots ? slot_[slot] : nullptr; }
    int numMembers() const noexcept { int n = 0; for (Component* c : slot_) n += c != nullptr; return n; }
    bool active() const noexcept { return active_; }
    void setActive (bool a)
    {
        active_ = a;
        for (int i = 0; i < kSlots; ++i)
            if (slot_[i])
                slot_[i]->setQ (a, i + 1);
    }
    /** The union of the members' rectangles. */
    Rect rect() const noexcept
    {
        Rect u;
        for (Component* c : slot_)
            if (c)
                u = Rect::unite (u, c->rect());
        return u;
    }
    /** The one frame around the whole group (nothing when not active), inside the union, and the index of every member: a tab on the top edge of the frame above the member when no other member
        lies above it (a row of controls); inside the top right corner of the member otherwise (a column of fields, the second row of a pair). */
    void draw (gpu::SceneBuilder& sb) const
    {
        if (! active_)
            return;
        const Rect u = rect();
        if (u.empty())
            return;
        Pen pen (sb, true);
        pen.rrect (u, theme::L().rControl, pen.c (theme::L().q, 255), 2.0f);
        for (int i = 0; i < kSlots; ++i)
        {
            const Component* m = slot_[i];
            if (! m)
                continue;
            const Rect r = m->rect();
            bool above = false;
            for (int j = 0; j < kSlots && ! above; ++j)
            {
                const Component* o = slot_[j];
                if (o && o != m)
                {
                    const Rect s = o->rect();
                    above = s.bottom() <= r.y + 2 && s.right() > r.x && s.x < r.right();
                }
            }
            if (above)
                pen.qBadge (r, i + 1);
            else
                pen.qTab (static_cast<float> (r.x) + static_cast<float> (r.w) * 0.5f, u.y + 2, i + 1);
        }
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
        const Rect u = rect();
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
    Component* slot_[kSlots] = {};
    bool active_ = false;
};

}} // namespace synthyboy_mpc::ui
