// SynthyBoy - MPC UI component library: the FX SLOT EDITOR component (FxSlotEditor): the page of the selected effect. From the top: the power switch, the name of the effect, its rack (MAIN / BUS 1 / BUS 2),
// the page of the knobs, REMOVE; the display of the effect (the response, the transfer curve, the echoes ... fx_display.h) for the types that have one; the knobs of the parameters in rows of four, ONE Q group
// per row, every knob with the parameter's own name and value (the unit from the engine's descriptor). An enumerated parameter is shown by its size: an on / off one is a LED toggle, one with 3 to 8 values (or
// two named ones) a LED list with ALL values visible, a longer one (or one whose list does not fit its cell) the stepper with its popup. The rows have the height their cells need (a list of five values is taller than a
// knob); the display gives up height before a list is turned into a stepper. An effect with more parameters than a page holds (8 under a display, 12 without) has its pages: a stepper moves between them, a Q group
// on another page makes that page the one on screen. Nothing is left empty: without a display the knobs take its room.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "fx_display.h"
#include "led_list.h"
#include "led_toggle.h"
#include "qgroup.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace ui {

class FxSlotEditor : public Composite
{
public:
    static constexpr int kMaxParams = synthyboy::kFxInstanceParams; // 24
    static constexpr int kMaxRows = 6;                              // 6 rows of 4 = 24 params
    static constexpr int kHeaderH = 56, kDisplayH = 188, kMinDisplayH = 112, kKnobWant = 96;
    /** How the parameter of a cell is shown. */
    enum class Kind : uint8_t { Knob, Stepper, List, Toggle };

    ToggleIcon power;
    Stepper rack, page;
    MenuButton remove { "REMOVE", MenuButton::Icon::None };
    FxDisplay display;
    Knob knobs[kMaxParams];
    Stepper steppers[kMaxParams];
    LedList lists[kMaxParams];
    LedToggle toggles[kMaxParams];
    QGroup groups[kMaxRows];

    FxSlotEditor()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        rack.setCaption ("RACK");
        rack.state.count = 3;
        page.state.wrap = true;
        addKid (power);
        addKid (rack);
        addKid (page, false);
        addKid (remove);
        addKid (display, false);
        for (int i = 0; i < kMaxParams; ++i)
        {
            knobs[i].state.side = true;   // the dial at the left, the name and the value beside it: the cells are wide
            addKid (knobs[i], false);
            steppers[i].listable = true;
            steppers[i].minWidth = 100;
            addKid (steppers[i], false);
            addKid (lists[i], false);
            addKid (toggles[i], false);
        }
        for (int r = 0; r < kMaxRows; ++r)
            for (int c = 0; c < 4; ++c)
                groups[r].set (c, &knobs[r * 4 + c]);
        setType (synthyboy::kFxTypeChorus, true);
    }

    Size minSize() const override { return { 1200, 400 }; }

    int currentType() const noexcept { return currentType_; }
    int numParams() const noexcept { return numParams_; }
    int numRows() const noexcept { return (numParams_ + 3) / 4; }
    int rowsPerPage() const noexcept { return display.kind != FxDisplay::Kind::None ? 2 : 3; }
    int numPages() const noexcept { return std::max (1, (numRows() + rowsPerPage() - 1) / rowsPerPage()); }
    int pageIndex() const noexcept { return page_; }
    int firstRowOfPage() const noexcept { return page_ * rowsPerPage(); }
    bool hasDisplay() const noexcept { return display.kind != FxDisplay::Kind::None; }

    bool isChoice (int p) const noexcept
    {
        const synthyboy::ParamInfo* pi = synthyboy::fxInstanceSource (currentType_, p);
        return pi != nullptr && pi->numChoices > 0 && pi->choices != nullptr;
    }
    /** The kind a parameter is shown as by its values alone: a knob, an on / off toggle, a list of 3 .. 8 values (or two named ones), a stepper for more than 8. */
    Kind baseKind (int p) const noexcept
    {
        const synthyboy::ParamInfo* pi = synthyboy::fxInstanceSource (currentType_, p);
        if (pi == nullptr || pi->numChoices <= 0 || pi->choices == nullptr)
            return Kind::Knob;
        if (pi->numChoices > LedList::kMaxOptions)
            return Kind::Stepper;
        if (pi->numChoices == 2 && std::strcmp (pi->choices[0], "Off") == 0 && std::strcmp (pi->choices[1], "On") == 0)
            return Kind::Toggle;
        return Kind::List;
    }
    /** The kind of the cell now (a list that does not fit its cell is a stepper: decided by the layout). */
    Kind kindOf (int p) const noexcept { return p >= 0 && p < kMaxParams ? kind_[p] : Kind::Knob; }
    Component* memberOf (int p) noexcept
    {
        switch (kindOf (p))
        {
            case Kind::Stepper: return &steppers[p];
            case Kind::List: return &lists[p];
            case Kind::Toggle: return &toggles[p];
            default: return &knobs[p];
        }
    }

    /** The effect on the page: the display kind, the knobs or steppers of its parameters (the owner labels them), the pages. `removable`: a slot of the pool (the modules of the fixed chain cannot be removed). */
    void setType (int type, bool removable)
    {
        const bool changed = type != currentType_;
        currentType_ = std::clamp (type, 0, static_cast<int> (synthyboy::kNumFxInstanceTypes - 1));
        const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (currentType_);
        display.setKindFromType (currentType_);
        setText (name_, sizeof name_, desc.name);
        numParams_ = std::min (desc.count, kMaxParams);
        show (display, display.kind != FxDisplay::Kind::None);
        show (remove, removable);
        removable_ = removable;
        if (changed)
        {
            page_ = 0;
            activeRow_ = 0;
        }
        for (int i = 0; i < kMaxParams; ++i)
        {
            kind_[i] = i < numParams_ ? baseKind (i) : Kind::Knob;
            knobs[i].setEnabled (i < numParams_ && kind_[i] == Kind::Knob);
            steppers[i].setEnabled (i < numParams_ && kind_[i] == Kind::Stepper);
            lists[i].setEnabled (i < numParams_ && kind_[i] == Kind::List);
            toggles[i].setEnabled (i < numParams_ && kind_[i] == Kind::Toggle);
        }
        syncPage();
    }

    int activeRow() const noexcept { return activeRow_; }
    void setActiveRow (int r)
    {
        if (r < 0 || r >= kMaxRows)
            return;
        activeRow_ = r;
        if (r < numRows() && (r < firstRowOfPage() || r >= firstRowOfPage() + rowsPerPage()))
        {
            page_ = r / rowsPerPage();
            syncPage();
        }
        for (int i = 0; i < kMaxRows; ++i)
            groups[i].setActive (i == activeRow_ && i < numRows());
    }
    void setPage (int p)
    {
        page_ = std::clamp (p, 0, numPages() - 1);
        syncPage();
        setActiveRow (firstRowOfPage());
    }
    void assignIds (int baseId, int powerId, int rackId, int pageId, int removeId)
    {
        for (int i = 0; i < kMaxParams; ++i)
        {
            knobs[i].setId (baseId + i);
            steppers[i].setId (baseId + i);
            lists[i].setId (baseId + i);
            toggles[i].setId (baseId + i);
        }
        power.setId (powerId);
        rack.setId (rackId);
        page.setId (pageId);
        remove.setId (removeId);
    }
    Rect displayRect() const noexcept { return display.rect(); }
    Rect knobArea() const noexcept { return knobArea_; }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)   // a tap on a knob or stepper makes its row the Q group
            for (int i = 0; i < numParams_; ++i)
                if (cap_ == &knobs[i] || cap_ == &steppers[i] || cap_ == &lists[i] || cap_ == &toggles[i])
                {
                    setActiveRow (i / 4);
                    break;
                }
        return c;
    }

    Change turnQ (int knobIndex, double delta) { return activeRow_ < numRows() ? groups[activeRow_].turn (knobIndex, delta) : Change::none(); }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        if (activeRow_ >= 0 && activeRow_ < numRows())
            groups[activeRow_].draw (sb);
        // the other row on the page is a quad too: its chips stand there quietly, a touch on it makes it the active one (its four values are then on the Q-Links)
        for (int r = firstRowOfPage(); r < firstRowOfPage() + rowsPerPage() && r < numRows(); ++r)
            if (r != activeRow_ && r * 4 + 3 < numParams_)
                groups[r].drawIdle (sb);
    }

protected:
    /** The knobs and steppers of the page are shown, the others are not; the layout follows. */
    void syncPage()
    {
        page_ = std::clamp (page_, 0, numPages() - 1);
        const int first = firstRowOfPage() * 4, last = first + rowsPerPage() * 4;
        for (int i = 0; i < kMaxParams; ++i)
        {
            const bool onPage = i < numParams_ && i >= first && i < last;
            show (knobs[i], onPage && kind_[i] == Kind::Knob);
            show (steppers[i], onPage && kind_[i] == Kind::Stepper);
            show (lists[i], onPage && kind_[i] == Kind::List);
            show (toggles[i], onPage && kind_[i] == Kind::Toggle);
        }
        syncGroups();
        show (page, numPages() > 1);
        page.state.count = numPages();
        page.state.index = page_;
        char t[32];
        std::snprintf (t, sizeof t, "PAGE %d / %d", page_ + 1, numPages());
        page.setItem (t);
        if (activeRow_ < firstRowOfPage() || activeRow_ >= firstRowOfPage() + rowsPerPage())
            activeRow_ = std::min (firstRowOfPage(), std::max (0, numRows() - 1));
        for (int i = 0; i < kMaxRows; ++i)
            groups[i].setActive (i == activeRow_ && i < numRows());
        if (rect_.w > 0)
            layout();
    }

    /** The members of the Q groups are the components the cells have now (a knob, a stepper, a LED list or a LED toggle): the same slot of the same row, whatever it looks like. */
    void syncGroups()
    {
        for (int r = 0; r < kMaxRows; ++r)
            for (int c = 0; c < 4; ++c)
            {
                const int idx = r * 4 + c;
                groups[r].set (c, idx < kMaxParams ? memberOf (idx) : nullptr);
            }
    }
    /** The height a LED list of parameter `p` needs in a cell `w` wide (its fewest rows of 56 px and its title where the names fit the columns); 0: no arrangement fits the width. */
    int listNeed (int p, int w) const
    {
        const synthyboy::ParamInfo* pi = synthyboy::fxInstanceSource (currentType_, p);
        if (pi == nullptr || pi->choices == nullptr)
            return 0;
        int need = 0;
        for (int i = 0; i < pi->numChoices; ++i)
            need = std::max (need, led::widthFor (pi->choices[i]));
        int best = 0;
        for (int c = 1; c <= LedList::kMaxColumns && c <= pi->numChoices; ++c)
            if (w >= c * need)
            {
                const int h = led::kRowH * ((pi->numChoices + c - 1) / c) + LedList::kTitleH;
                best = best == 0 ? h : std::min (best, h);
            }
        return best;
    }
    int cellNeed (int p, int w) const
    {
        switch (kind_[p])
        {
            case Kind::Toggle: return led::kRowH;
            case Kind::List: return listNeed (p, w);
            default: return 72;
        }
    }

    void layout() override
    {
        const int m = 8, gap = 6;
        // the header: power, name (drawn), rack, page, remove
        const Rect hd { rect_.x + m, rect_.y + m, rect_.w - 2 * m, kHeaderH };
        power.place ({ hd.x, hd.y, 56, kHeaderH });
        remove.place ({ hd.right() - 130, hd.y, 130, kHeaderH });
        int right = hd.right() - 130 - gap;
        if (numPages() > 1)
        {
            right -= Stepper::kMinW;
            page.place ({ right, hd.y, Stepper::kMinW, kHeaderH });
            right -= gap;
        }
        rack.place ({ right - 230, hd.y, 230, kHeaderH });
        // the room under the header holds the display and the rows of cells; a cell is a knob, a stepper, a LED toggle or a LED list: the rows are as high as their tallest cell needs, the display gives up
        // height (down to kMinDisplayH) before a list is turned into a stepper, what is left is shared by the rows
        const int y0 = hd.bottom() + m, room = std::max (0, rect_.bottom() - m - y0);
        const int firstRow = firstRowOfPage(), rows = std::max (1, std::min (rowsPerPage(), numRows() - firstRow)), gapX = 8, cellW = (rect_.w - 2 * m - 3 * gapX) / 4;
        const bool hasDisplay = display.kind != FxDisplay::Kind::None;
        int need[kMaxRows] = {}, want[kMaxRows] = {};
        int dispH = hasDisplay ? kDisplayH : 0;
        for (int i = 0; i < kMaxParams; ++i)
            kind_[i] = i < numParams_ ? baseKind (i) : Kind::Knob;   // every layout starts from what the values say; a list that does not fit becomes a stepper below
        auto cellW_of = [&] (int col) { return col == 3 ? rect_.w - 2 * m - 3 * (cellW + gapX) : cellW; };
        for (;;)
        {
            int sumNeed = 0, sumWant = 0;
            for (int r = 0; r < rows; ++r)
            {
                int cn = 0, cw = 0;   // the tallest cell: the least it needs, what it likes (a knob likes 96 px, the rest its need)
                for (int col = 0; col < 4; ++col)
                {
                    const int idx = (firstRow + r) * 4 + col;
                    if (idx >= kMaxParams || idx >= numParams_)
                        continue;
                    if (kind_[idx] == Kind::List && listNeed (idx, cellW_of (col)) == 0)
                        kind_[idx] = Kind::Stepper;   // no arrangement of its values fits the width of the cell
                    const int n = cellNeed (idx, cellW_of (col));
                    cn = std::max (cn, n);
                    cw = std::max (cw, kind_[idx] == Kind::Knob || kind_[idx] == Kind::Stepper ? kKnobWant : n);
                }
                need[r] = kQLane + std::max (cn, led::kRowH) + 4;
                want[r] = kQLane + std::max (cw, led::kRowH) + 4;
                sumNeed += need[r];
                sumWant += want[r];
            }
            // the display takes what the rows leave (at most kDisplayH; the rows are at their wish while the display keeps 140 px, below that the rows give up height down to their need, then the display down to kMinDisplayH)
            bool fits = true;
            if (! hasDisplay)
                fits = room >= sumNeed;
            else
            {
                int d = std::clamp (room - m - sumWant, 140, kDisplayH);
                if (room - (d + m) < sumNeed)
                {
                    d = room - m - sumNeed;
                    fits = d >= kMinDisplayH;
                }
                dispH = fits ? d : kMinDisplayH;
            }
            if (fits)
                break;
            // not even the smallest display and the rows fit: the tallest list of the page becomes a stepper (a list is never squeezed below its 56 px rows), then the rows are counted again
            int worst = -1, worstNeed = 0;
            for (int r = 0; r < rows; ++r)
                for (int col = 0; col < 4; ++col)
                {
                    const int idx = (firstRow + r) * 4 + col;
                    if (idx < kMaxParams && idx < numParams_ && kind_[idx] == Kind::List && listNeed (idx, cellW_of (col)) > worstNeed)
                    {
                        worstNeed = listNeed (idx, cellW_of (col));
                        worst = idx;
                    }
                }
            if (worst < 0)
                break;
            kind_[worst] = Kind::Stepper;
        }
        int y = y0;
        if (hasDisplay)
        {
            display.place ({ rect_.x + m, y, rect_.w - 2 * m, dispH });
            y += dispH + m;
        }
        knobArea_ = { rect_.x + m, y, rect_.w - 2 * m, std::max (0, rect_.bottom() - m - y) };
        // the rows: at least their need, the room shared out evenly (a row that needs more than its share keeps its need, the others share the rest)
        int rowH[kMaxRows] = {}, total = knobArea_.h, freeRows = rows;
        bool fixedRow[kMaxRows] = {};
        for (bool again = true; again && freeRows > 0;)
        {
            again = false;
            for (int r = 0; r < rows; ++r)
                if (! fixedRow[r] && need[r] > total / freeRows)
                {
                    fixedRow[r] = true;
                    rowH[r] = need[r];
                    total -= need[r];
                    --freeRows;
                    again = true;
                    break;
                }
        }
        for (int r = 0, left = freeRows; r < rows; ++r)
            if (! fixedRow[r])
            {
                rowH[r] = total / std::max (1, left);
                total -= rowH[r];
                --left;
            }
        int rowY = knobArea_.y;
        for (int r = 0; r < rows; ++r)
        {
            for (int col = 0; col < 4; ++col)
            {
                const int idx = (firstRow + r) * 4 + col;
                if (idx >= kMaxParams)
                    continue;
                // a lane (kQLane) above every cell for the index chip of its Q group
                const int kx = knobArea_.x + col * (cellW + gapX), ky = rowY + kQLane;
                const Rect cellRect { kx, ky, col == 3 ? knobArea_.right() - kx : cellW, std::max (56, rowH[r] - kQLane - 4) };
                knobs[idx].place (cellRect);
                steppers[idx].place (cellRect);
                lists[idx].place (cellRect);
                // a toggle is one row of 56 px (at most 80), in the middle of its cell
                const int th = std::clamp (cellRect.h, led::kRowH, 80);
                toggles[idx].place ({ cellRect.x, cellRect.y + (cellRect.h - th) / 2, cellRect.w, th });
            }
            rowY += rowH[r];
        }
        for (int i = 0; i < kMaxParams; ++i)
        {
            const bool onPage = i < numParams_ && i >= firstRow * 4 && i < (firstRow + rowsPerPage()) * 4;
            show (knobs[i], onPage && kind_[i] == Kind::Knob);
            show (steppers[i], onPage && kind_[i] == Kind::Stepper);
            show (lists[i], onPage && kind_[i] == Kind::List);
            show (toggles[i], onPage && kind_[i] == Kind::Toggle);
        }
        syncGroups();
    }

    void drawUnder (Pen& p) const override
    {
        // the knobs lie on a block of their own (the page of the effect)
        p.block ({ knobArea_.x - 2, knobArea_.y - 2, knobArea_.w + 4, knobArea_.h + 4 }, false);
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft ({ rect_.x + 8 + 56 + 14, rect_.y + 8, std::max (0, rack.rect().x - (rect_.x + 8 + 56 + 14) - 8), kHeaderH }, name_, T.fTitle, p.c (T.text, 255), T.fMin);
    }

private:
    int currentType_ = synthyboy::kFxTypeChorus;
    int numParams_ = 0;
    Kind kind_[kMaxParams] = {};
    int activeRow_ = 0, page_ = 0;
    bool removable_ = false;
    char name_[24] = {};
    Rect knobArea_;
};

}} // namespace synthyboy_mpc::ui
