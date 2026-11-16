// SynthyBoy - MPC UI component library: LedList, a radio list of 2 .. 8 options: a row per option (a small flat dot and the name), ALL options visible, one lit. Same contract as every component (core.h).
// In: the options (names of 23 characters), their number, the index of the lit one, an optional bit mask of disabled options (not valid in the current state: drawn dim, no reaction, skipped by steps),
// an optional small title above. Out: an Index change { id, index } (the option) on a tap on an enabled row (made on the touch up); turn (delta) / the wheel move the selection by whole steps over the
// enabled options WITHOUT wrapping (a step beyond the ends does nothing). The wheel up goes up the list (like ListPopup).
// Geometry (the component decides from its rectangle, `columnsFor`): one column when the rows are at least 56 px high there, else two, else three columns (filled row by row); a row is never lower
// than 56 px where the rectangle allows it and never narrower than its text (dot, name). Where nothing fits the choice with the highest rows is used and the name is cut with an ellipsis.
// Look: the palette and fonts of the library; a flat dot (lit = accent colour filled, unlit = a thin dim ring), no box, no frame, no fill: the lit dot and the brighter name are the selection; no glow. The active Q member: the title and the lit name take the
// Q colour (the chip comes from the group).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "led_toggle.h"

namespace synthyboy_mpc { namespace ui {

class LedList : public Component
{
public:
    static constexpr int kMaxOptions = 8, kNameLen = 24, kTitleH = 24, kMaxColumns = 3;
    struct State
    {
        int count = 0, index = 0;
        uint8_t disabledMask = 0;   // bit i set: option i is not valid now
        char names[kMaxOptions][kNameLen] = {};
        char title[kNameLen] = {};
        bool titleInLane = false;   // the title is drawn in the lane of the Q index chip above the rectangle (right of the chip) and takes no room of the list
    };
    State state;

    void setTitle (const char* s) { setText (state.title, sizeof state.title, s); }
    void setOption (int i, const char* name) { if (i >= 0 && i < kMaxOptions) setText (state.names[i], kNameLen, name); }
    /** Sets all options at once from an array of names (the first `n`, at most 8). */
    void setOptions (const char* const* names, int n)
    {
        state.count = std::clamp (n, 0, kMaxOptions);
        for (int i = 0; i < state.count; ++i)
            setOption (i, names[i]);
    }
    bool optionEnabled (int i) const noexcept { return i >= 0 && i < state.count && (state.disabledMask & (1u << i)) == 0; }
    int titleH() const noexcept { return state.title[0] != '\0' && ! state.titleInLane ? kTitleH : 0; }

    /** The width the widest name of the list needs in one column. */
    int columnNeed() const noexcept
    {
        int w = 0;
        for (int i = 0; i < std::min (state.count, kMaxOptions); ++i)
            w = std::max (w, led::widthFor (state.names[i]));
        return std::max (w, 2 * led::kPadR + 40);
    }
    /** The number of columns (1 .. 3) for a list of n options in a w x h rectangle: the fewest where the rows are at least 56 px high and the columns wide enough for the text; where no choice satisfies both, the one with the
        highest rows (ties: the fewer columns). */
    /** Does a list of n options whose widest name needs `columnNeed` fit a w x h rectangle (some column count has rows >= 56 px and columns wide enough)? */
    static bool fitsIn (int n, int w, int h, int titleH, int columnNeed) noexcept
    {
        n = std::clamp (n, 1, kMaxOptions);
        for (int c = 1; c <= std::min (kMaxColumns, n); ++c)
            if (w / c >= columnNeed && (h - titleH) / ((n + c - 1) / c) >= led::kRowH)
                return true;
        return false;
    }
    static int columnsFor (int n, int w, int h, int titleH, int columnNeed) noexcept
    {
        n = std::clamp (n, 1, kMaxOptions);
        int best = 1, bestRow = -1;
        for (int c = 1; c <= std::min (kMaxColumns, n); ++c)
        {
            const int rows = (n + c - 1) / c, rowH = (h - titleH) / rows;
            if (rowH >= led::kRowH && w / c >= columnNeed)
                return c;
            if (rowH > bestRow)
            {
                bestRow = rowH;
                best = c;
            }
        }
        return best;
    }
    int columns() const noexcept { return columnsFor (state.count, rect_.w, rect_.h, titleH(), columnNeed()); }
    int rows() const noexcept { const int c = columns(); return (std::max (1, state.count) + c - 1) / c; }
    /** The smallest size at which the list is legible: the arrangement of the least area (one column of rows 56 px high, or two or three columns). */
    Size minSize() const override
    {
        const int n = std::clamp (state.count, 1, kMaxOptions), need = columnNeed();
        Size best { need, led::kRowH * n + titleH() };
        for (int c = 2; c <= std::min (kMaxColumns, n); ++c)
        {
            const Size s { c * need, led::kRowH * ((n + c - 1) / c) + titleH() };
            if (s.w * s.h < best.w * best.h)
                best = s;
        }
        return best;
    }
    /** Legible when ANY arrangement (one, two or three columns) fits the rectangle: its rows at least 56 px high, its columns as wide as the widest name needs. */
    bool sizeOk() const noexcept override
    {
        const int n = std::clamp (state.count, 1, kMaxOptions), need = columnNeed();
        for (int c = 1; c <= std::min (kMaxColumns, n); ++c)
            if (rect_.w >= c * need && rect_.h >= led::kRowH * ((n + c - 1) / c) + titleH())
                return true;
        return false;
    }
    double wheelUnit() const noexcept override { return -1.0; }   // the wheel up goes up the list

    /** The touch rectangle of option i (the rows tile the rectangle below the title, the options are placed in reading order: along the row, then the next row). */
    Rect rowRect (int i) const noexcept
    {
        const int c = columns(), r = rows(), col = i % c, row = i / c, top = rect_.y + titleH(), bodyH = rect_.h - titleH();
        const int x0 = rect_.x + col * rect_.w / c, x1 = rect_.x + (col + 1) * rect_.w / c;
        return { x0, top + row * bodyH / r, x1 - x0, top + (row + 1) * bodyH / r - (top + row * bodyH / r) };
    }
    int touchTargets (Rect* out, int max) const override
    {
        int n = 0;
        for (int i = 0; i < std::min (state.count, kMaxOptions) && n < max; ++i)
            out[n++] = rowRect (i);
        return n;
    }
    /** The option under the point (-1: none). */
    int optionAt (int x, int y) const noexcept
    {
        for (int i = 0; i < std::min (state.count, kMaxOptions); ++i)
            if (rowRect (i).contains (x, y))
                return i;
        return -1;
    }

public:
    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen pen (sb, enabled_);
        Cell cell (sb, rect_, state.titleInLane ? 22 : 2);
        drawBody (pen);
    }
protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (state.title[0] != '\0' && state.titleInLane)
            p.textLeft ({ rect_.x + 30, rect_.y - 16, std::max (0, rect_.w - 32), 16 }, state.title, T.fLabel - 3.0f, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
        if (state.title[0] != '\0')
            p.textLeft ({ rect_.x + 4, rect_.y + 4, std::max (0, rect_.w - 8), kTitleH - 6 }, state.title, T.fLabel - 2.0f, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
        const int n = std::min (state.count, kMaxOptions);
        for (int i = 0; i < n; ++i)
        {
            const Rect r = rowRect (i);
            const bool lit = i == state.index, on = optionEnabled (i);
            const float cy = static_cast<float> (r.y) + static_cast<float> (r.h) * 0.5f;
            led::dot (p, static_cast<float> (r.x + led::kDotX), cy, lit, on ? 255 : 80, qFrame_);
            const uint32_t col = qFrame_ && lit ? p.c (T.q, on ? 255 : 90) : p.c (lit ? T.text : T.textDim, on ? (lit ? 255 : 200) : 80);
            p.textLeft ({ r.x + led::kTextX, r.y + 2, std::max (0, r.w - led::kTextX - led::kPadR), std::max (0, r.h - 4) }, state.names[i], led::nameSize(), col, T.fMin);
        }
    }
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        downRow_ = optionAt (x, y);
        return Change::none();
    }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override
    {
        if (tap_.up() == 0 || ! optionEnabled (downRow_) || downRow_ == state.index)
            return Change::none();
        return Change::indexed (id_, downRow_, downRow_);
    }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        return d == 0 ? Change::none() : step (static_cast<int> (std::clamp<long> (d, -100000, 100000)));
    }

private:
    /** `d` steps over the enabled options, no wrap: stops at the last enabled option in that direction. */
    Change step (int d) const
    {
        const int n = std::min (state.count, kMaxOptions), dir = d < 0 ? -1 : 1;
        int cur = std::clamp (state.index, -1, n - 1);
        for (int k = 0; k < std::abs (d); ++k)
        {
            int j = cur + dir;
            while (j >= 0 && j < n && ! optionEnabled (j))
                j += dir;
            if (j < 0 || j >= n)
                break;
            cur = j;
        }
        return cur == state.index || cur < 0 ? Change::none() : Change::indexed (id_, cur, cur);
    }
    TapGesture tap_;
    int downRow_ = -1;
};

}} // namespace synthyboy_mpc::ui
