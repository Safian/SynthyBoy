// SynthyBoy - MPC UI component library: ListPopup, the list of choices as a popup (the source list of an oscillator, the warp types). Same contract as every component (core.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// In: the names (`items`, `count`: the owner's array, not copied: the engine's choice labels), the current index, the box (`place`) and the parent rectangle the dimmed backdrop covers (`setPopup`).
// Out: a tap on a row is an Index change { id, index = the row, value = the row }; a tap outside the box (on the backdrop) is a Request change with the id of `setBackdropId` (close requested);
// a vertical drag scrolls by whole rows (the rows drawn are always whole: nothing is cut at the box's edge); `turn (delta)` moves the current item (an Index change, the list follows it).
// Rows are kRowH pixels high (a touch target); the name is cut to 12 characters (the scene's text limit). A scroll zone (44 px) at the right edge, when the list is longer than the box: the thumb in it is dragged (it follows the finger), a touch on the track above or below it pages by one window.
#pragma once

#include "core.h"

namespace synthyboy_mpc { namespace ui {

class ListPopup : public Component
{
public:
    static constexpr int kRowH = 56, kBarW = 10, kPad = 4, kZoneW = 44;   // the scroll zone at the right edge: kZoneW wide (a touch target), the bar in it kBarW
    struct State
    {
        const char* const* items = nullptr;
        int count = 0, current = 0;
        const char* header = nullptr;   // a bar of its own above the rows (the content browser: the path); null = none
        bool headerBack = false;        // the header is also the way up: a back arrow at its left and a Request change with the id of `setHeaderId` on a tap
    };
    State state;

    Size minSize() const override { return { 240, 3 * kRowH + 2 * kPad }; }
    /** The dimmed backdrop covers `parent`; a tap on it closes (Request with the backdrop id). */
    void setPopup (const Rect& parent) { parent_ = parent; }
    void setBackdropId (int id) noexcept { backdropId_ = id; }
    void setHeaderId (int id) noexcept { headerId_ = id; }
    int headerH() const noexcept { return state.header != nullptr ? kRowH : 0; }
    Rect headerRect() const noexcept { return { rect_.x + kPad, rect_.y + kPad, rect_.w - 2 * kPad, headerH() }; }
    /** Rows that fit into the box. */
    int visibleRows() const noexcept { return std::max (1, (rect_.h - 2 * kPad - headerH()) / kRowH); }
    int firstRow() const noexcept { return std::clamp (first_, 0, std::max (0, state.count - visibleRows())); }
    /** Brings the current item into the window (the owner calls it when it opens the list). */
    void showCurrent() noexcept { first_ = std::clamp (state.current - visibleRows() / 2, 0, std::max (0, state.count - visibleRows())); }
    Rect rowRect (int row) const noexcept { return { rect_.x + kPad, rect_.y + kPad + headerH() + (row - firstRow()) * kRowH, rect_.w - 2 * kPad - (scrolls() ? kZoneW : 0), kRowH }; }
    bool scrolls() const noexcept { return state.count > visibleRows(); }
    /** The scroll zone (a touch there drags the thumb or pages) and the thumb. */
    Rect zoneRect() const noexcept { return { rect_.right() - kPad - kZoneW, rect_.y + kPad + headerH(), kZoneW, rect_.h - 2 * kPad - headerH() }; }
    Rect thumbRect() const noexcept
    {
        const int n = std::max (1, state.count), trackH = rect_.h - 2 * kPad - headerH(), thumbH = std::max (28, trackH * visibleRows() / n), thumbY = (trackH - thumbH) * firstRow() / std::max (1, n - visibleRows());
        return { rect_.right() - kPad - kZoneW / 2 - kBarW / 2, rect_.y + kPad + headerH() + thumbY, kBarW, thumbH };
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen pen (sb, enabled_);
        Cell cell (sb, parent_);
        pen.rect (parent_, colour::rgba (theme::L().bg1, 190));
        drawBody (pen);
    }
    Change touchDown (int x, int y, double nowMs) override
    {
        backdrop_ = false;
        captured_ = false;
        if (! enabled_)
            return Change::none();
        if (! rect_.contains (x, y))
        {
            if (! parent_.contains (x, y))
                return Change::none();
            backdrop_ = true;
            tap_.down (x, y, nowMs);
            return Change::none();
        }
        captured_ = true;
        return onDown (x, y, nowMs);
    }
    Change touchMove (int x, int y) override
    {
        if (backdrop_)
        {
            tap_.move (x, y);
            return Change::none();
        }
        return Component::touchMove (x, y);
    }
    Change touchUp (int x, int y) override
    {
        if (backdrop_)
        {
            backdrop_ = false;
            return tap_.up() != 0 ? Change::request (backdropId_) : Change::none();
        }
        return Component::touchUp (x, y);
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rect_, true);
        p.rrect (rect_, T.rBlock, p.c (T.flow[0], 255), 1.6f);
        if (state.header != nullptr)
        {
            const Rect h = headerRect();
            const float cy = static_cast<float> (h.y) + static_cast<float> (h.h) * 0.5f;
            if (state.headerBack)
            {
                p.style (1.0f, true);
                p.chevron (static_cast<float> (h.x + 28), cy, 16.0f, 0, 2.4f, p.c (T.flow[0], 255));
                p.plain();
            }
            p.textLeft ({ h.x + (state.headerBack ? 56 : 12), h.y + 4, std::max (0, h.w - (state.headerBack ? 64 : 20)), h.h - 8 }, state.header, T.fItem, p.c (T.text, 255), T.fMin);
            p.rect (h.x + 8, h.bottom() - 1, h.right() - 8, h.bottom(), p.c (T.keyline, 255));
        }
        const int n = std::max (0, state.count), f = firstRow(), last = std::min (n, f + visibleRows());
        for (int i = f; i < last; ++i)
        {
            const Rect r = rowRect (i);
            const bool cur = i == state.current;
            if (cur)
            {
                p.paintX2 (static_cast<float> (r.x), static_cast<float> (r.right()), T.flow[0], T.flow[2], 60);
                p.rrect (r.inset (1), T.rControl, 255);
                p.paintOff();
                p.paintX2 (static_cast<float> (r.x), static_cast<float> (r.right()), T.flow[0], T.flow[2], 255);
                p.rrect (r.inset (1), T.rControl, 255, 1.5f);
                p.plain();
            }
            const char* name = state.items != nullptr && state.items[i] != nullptr ? state.items[i] : "";
            p.textLeft ({ r.x + 12, r.y + 4, std::max (0, r.w - 20), r.h - 8 }, name, T.fItem, p.c (cur ? T.text : T.textDim, 255), T.fMin);
        }
        if (scrolls())
        {
            const Rect th = thumbRect();
            p.rrect ({ th.x, rect_.y + kPad + headerH(), kBarW, rect_.h - 2 * kPad - headerH() }, 5.0f, p.c (T.track, 255));
            p.paintY2 (static_cast<float> (th.y), static_cast<float> (th.bottom()), T.flow[0], T.flow[3], 255);
            p.rrect (th, 5.0f, 255);
            p.paintOff();
        }
    }
    Change onDown (int x, int y, double nowMs) override
    {
        tap_.down (x, y, nowMs);
        headerHit_ = state.header != nullptr && headerRect().contains (x, y);
        y0_ = y;
        first0_ = firstRow();
        zone_ = scrolls() && zoneRect().contains (x, y);
        grab_ = 0;
        if (zone_)
        {
            const Rect th = thumbRect();
            if (y >= th.y && y < th.bottom())
                grab_ = y - th.y + 1;                                      // the thumb is held: it follows the finger
            else
                first_ = std::clamp (firstRow() + (y < th.y ? -1 : 1) * std::max (1, visibleRows() - 1), 0, std::max (0, state.count - visibleRows()));   // the track: a page up / down
        }
        return Change::none();
    }
    Change onMove (int x, int y) override
    {
        if (zone_)
        {
            if (grab_ > 0)
            {
                const int trackH = rect_.h - 2 * kPad - headerH(), thumbH = thumbRect().h, room = std::max (1, trackH - thumbH), rows = std::max (0, state.count - visibleRows());
                const int ty = std::clamp (y - grab_ + 1 - (rect_.y + kPad + headerH()), 0, room);
                first_ = std::clamp ((ty * rows + room / 2) / room, 0, rows);
            }
            return Change::none();
        }
        if (tap_.move (x, y))
            first_ = std::clamp (first0_ + (y0_ - y) / kRowH, 0, std::max (0, state.count - visibleRows()));
        return Change::none();
    }
    Change onUp (int x, int y) override
    {
        const bool zone = zone_, header = headerHit_;
        zone_ = false;
        headerHit_ = false;
        if (header)
            return tap_.up() != 0 && state.headerBack ? Change::request (headerId_) : Change::none();
        if (zone || tap_.up() == 0)
            return Change::none();
        const int row = firstRow() + (y - rect_.y - kPad - headerH()) / kRowH;
        if (y < rect_.y + kPad + headerH() || row < 0 || row >= state.count || x >= rect_.x + rowRect (0).w + kPad || row >= firstRow() + visibleRows())
            return Change::none();
        return Change::indexed (id_, row, row);
    }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        const int n = std::clamp (state.current + static_cast<int> (d), 0, std::max (0, state.count - 1));
        if (d == 0 || n == state.current)
            return Change::none();
        first_ = std::clamp (first_, std::max (0, n - visibleRows() + 1), n);
        return Change::indexed (id_, n, n);
    }

    Rect parent_;
    int backdropId_ = 0, headerId_ = 0, y0_ = 0, first0_ = 0, first_ = 0, grab_ = 0;
    bool backdrop_ = false, zone_ = false, headerHit_ = false;
    TapGesture tap_;
};

}} // namespace synthyboy_mpc::ui
