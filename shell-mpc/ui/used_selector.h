// SynthyBoy - MPC UI component library: UsedSelector, the selector of the merged Env / LFO tabs ("used + one free", docs/ui-map/env-filter-lfo-plan.md section (b)). One row of cells: every candidate that is USED
// and the SELECTED one (and a candidate marked `always`, the amplitude envelope), in the order the candidates were given (number order), then ONE "+" cell while at least one candidate is neither used, always nor
// selected. Same contract as every component (core.h): no parameter knowledge, the owner gives the candidates (id, label, used) and the selected id in `state` every update and writes nothing here.
// Out: an Index change { id, index = the id of the candidate, value } on a tap on a shown candidate other than the selected one; a tap on "+" gives an Index change with the free candidate when there is ONE choice
// (Env: the lowest free number) and a Request when there are more (the LFO: the lowest free standard LFO and the lowest free random LFO; the owner opens a ListPopup with `plusChoices`). Nothing is written by
// the selector: choosing "+" only selects (the owner shows it; it stays unwritten until the user edits something). turn steps the selection over the shown candidates (not "+"), without wrapping.
// Groups: a candidate has a `group` (0 standard, 1 random LFO); the "+" offers the lowest free candidate of each group (at most two rows in the LFO's list).
// Look: the cells of the library (control box, the selected one outlined in the accent colour), a small dot under the label of a used candidate (the selected unused one has none), "+" dim. Touch cells are 56 px
// wide at least: `minSize` is cells x 56, the owner gives at least that (15 cells in the worst case: 10 LFO + 4 random + "+", 840 px).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"

namespace synthyboy_mpc { namespace ui {

class UsedSelector : public Component
{
public:
    static constexpr int kMaxCandidates = 16, kMaxCells = kMaxCandidates + 1, kLabelLen = 8, kCellW = 56, kCellH = 56, kMaxChoices = 4;
    static constexpr int kPlus = -1;   // the value of a cell that is the "+" cell (`cells`)

    struct Candidate
    {
        int id = 0;                    // the number the owner uses (Env 1 .. 6; LFO 1 .. 10, random LFO 11 .. 14)
        char label[kLabelLen] = {};
        bool used = false;             // the owner's rule (a modulation source, an edited LFO ...): shown, and a dot
        bool always = false;           // shown whatever it is (ENV 1)
        int group = 0;                 // 0 standard, 1 random LFO
    };
    struct State
    {
        int count = 0;
        Candidate items[kMaxCandidates];
        int selected = 0;              // the id of the selected candidate (shown even when it is unused; 0 = none)
    };
    State state;

    void clear() noexcept { state.count = 0; }
    /** Appends a candidate (in number order). */
    void add (int id, const char* label, bool used, bool always = false, int group = 0)
    {
        if (state.count >= kMaxCandidates)
            return;
        Candidate& c = state.items[state.count++];
        c.id = id; c.used = used; c.always = always; c.group = group;
        setText (c.label, sizeof c.label, label);
    }
    void setSelected (int id) noexcept { state.selected = id; }

    // ---- the list (pure: the same input gives the same cells)
    /** Is candidate i shown: used, always or the selected one. */
    static bool isShown (const State& s, int i) noexcept { return i >= 0 && i < s.count && (s.items[i].used || s.items[i].always || s.items[i].id == s.selected); }
    /** The cells in order: the index of every shown candidate, then kPlus when a free candidate is left; returns the count (at most `max`). */
    static int cellsOf (const State& s, int* out, int max) noexcept
    {
        int n = 0, free = 0;
        for (int i = 0; i < std::min (s.count, kMaxCandidates); ++i)
            if (isShown (s, i))
            {
                if (n < max)
                    out[n++] = i;
            }
            else
                ++free;
        if (free > 0 && n < max)
            out[n++] = kPlus;
        return n;
    }
    /** What a tap on "+" offers: the index of the lowest free candidate of each group, groups in the order they first appear; returns the count (0 when nothing is free). */
    static int choicesOf (const State& s, int* out, int max) noexcept
    {
        int groups[kMaxChoices], ng = 0, n = 0;
        for (int i = 0; i < std::min (s.count, kMaxCandidates); ++i)
            if (! isShown (s, i))
            {
                bool seen = false;
                for (int g = 0; g < ng; ++g)
                    seen = seen || groups[g] == s.items[i].group;
                if (seen || ng >= kMaxChoices)
                    continue;
                groups[ng++] = s.items[i].group;
                if (n < max)
                    out[n++] = i;
            }
        return n;
    }
    int cells (int* out, int max) const noexcept { return cellsOf (state, out, max); }
    int plusChoices (int* out, int max) const noexcept { return choicesOf (state, out, max); }
    int numCells() const noexcept { int c[kMaxCells]; return cells (c, kMaxCells); }
    bool hasPlus() const noexcept { int c[kMaxChoices]; return plusChoices (c, kMaxChoices) > 0; }
    const char* labelOf (int candidate) const noexcept { return candidate >= 0 && candidate < state.count ? state.items[candidate].label : ""; }

    // ---- geometry
    /** The rectangle of cell k (the cells share the width evenly). */
    Rect cell (int k, int n) const noexcept
    {
        n = std::max (1, n);
        return { rect_.x + rect_.w * k / n, rect_.y, rect_.w * (k + 1) / n - rect_.w * k / n, rect_.h };
    }
    Size minSize() const override { return { std::max (1, numCells()) * kCellW, kCellH }; }
    int touchTargets (Rect* out, int max) const override
    {
        const int n = numCells();
        int k = 0;
        for (; k < n && k < max; ++k)
            out[k] = cell (k, n);
        return k;
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        int cs[kMaxCells];
        const int n = cells (cs, kMaxCells);
        for (int k = 0; k < n; ++k)
        {
            const Rect c = cell (k, n).inset (2);
            if (cs[k] == kPlus)
            {
                p.control (c, false, false);
                p.textCentered (c.inset (2), "+", T.fValue, p.c (T.textDim, 200), T.fMin);
                continue;
            }
            const Candidate& it = state.items[cs[k]];
            const bool cur = it.id == state.selected;
            p.control (c, false, cur);
            if (cur)
                p.rrect (c, T.rControl, p.c (T.flow[0], 255), 1.6f);
            p.textCentered ({ c.x + 2, c.y + 2, std::max (0, c.w - 4), std::max (0, c.h - 10) }, it.label, T.fLabel + 2.0f, p.c (cur ? T.text : T.textDim, 255), T.fMin);
            if (it.used)
                p.disc (static_cast<float> (c.x + c.w / 2), static_cast<float> (c.bottom() - 7), 2.5f, p.c (cur ? T.flow[0] : T.textDim, 255));
        }
    }
    int cellAt (int x, int y) const noexcept
    {
        const int n = numCells();
        for (int k = 0; k < n; ++k)
            if (cell (k, n).contains (x, y))
                return k;
        return -1;
    }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); downCell_ = cellAt (x, y); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int x, int y) override
    {
        const int down = downCell_;
        downCell_ = -1;
        if (tap_.up() == 0 || down < 0 || cellAt (x, y) != down)
            return Change::none();
        int cs[kMaxCells];
        const int n = cells (cs, kMaxCells);
        if (down >= n)
            return Change::none();
        if (cs[down] != kPlus)
        {
            const Candidate& it = state.items[cs[down]];
            return it.id == state.selected ? Change::none() : Change::indexed (id_, it.id, it.id);
        }
        int ch[kMaxChoices];
        const int nc = plusChoices (ch, kMaxChoices);
        if (nc == 1)
            return Change::indexed (id_, state.items[ch[0]].id, state.items[ch[0]].id);
        return nc > 1 ? Change::request (id_) : Change::none();
    }
    Change onTurn (double delta) override
    {
        long d = std::lround (delta);
        if (d == 0)
            return Change::none();
        const int dir = d > 0 ? 1 : -1;
        int shownIdx[kMaxCandidates], ns = 0, at = -1;
        for (int i = 0; i < std::min (state.count, kMaxCandidates); ++i)
            if (isShown (state, i))
            {
                if (state.items[i].id == state.selected)
                    at = ns;
                shownIdx[ns++] = i;
            }
        if (ns == 0)
            return Change::none();
        int to = at < 0 ? (dir > 0 ? 0 : ns - 1) : std::clamp (at + static_cast<int> (d), 0, ns - 1);
        if (to == at)
            return Change::none();
        const int id = state.items[shownIdx[to]].id;
        return Change::indexed (id_, id, id);
    }

    TapGesture tap_;
    int downCell_ = -1;
};

}} // namespace synthyboy_mpc::ui
