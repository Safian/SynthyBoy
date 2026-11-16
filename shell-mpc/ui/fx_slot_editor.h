// SynthyBoy - MPC UI component library: the FX SLOT EDITOR component (FxSlotEditor): the page of the selected effect. From the top: the power switch, the name of the effect, its rack (MAIN / BUS 1 / BUS 2),
// the page of the knobs, REMOVE; the display of the effect (the response, the transfer curve, the echoes ... fx_display.h) for the types that have one; the knobs of the parameters in rows of four, ONE Q group
// per row, every knob with the parameter's own name and value (the unit from the engine's descriptor). An enumerated parameter is shown by its size: an on / off one is a LED toggle, one with 3 to 8 values (or
// two named ones) a LED list with ALL values visible, a longer one (or one whose list does not fit its cell) the stepper with its popup. A list's name stands in the lane of the index chip; every row has the standard height and every cell the same rectangle; a list that does not fit its cell is a stepper. An effect with more parameters than a page holds (8 under a display, 12 without) has its pages: a stepper moves between them, a Q group
// on another page makes that page the one on screen. The display keeps its height, a page with fewer rows than it can hold keeps the standard row height.
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
    static constexpr int kHeaderH = 56, kDisplayH = 176;
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
            lists[i].state.titleInLane = true;
            toggles[i].centre = 0.5f;
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
public:
    /** Does the LED list of parameter `p` fit a cell of this size (some column count with rows of at least 56 px and columns wide enough for the longest name)? */
    bool listFits (int p, int w, int h) const
    {
        const synthyboy::ParamInfo* pi = synthyboy::fxInstanceSource (currentType_, p);
        if (pi == nullptr || pi->choices == nullptr)
            return false;
        int need = 0;
        for (int i = 0; i < pi->numChoices; ++i)
            need = std::max (need, led::widthFor (pi->choices[i]));
        return LedList::fitsIn (pi->numChoices, w, h, 0, need);
    }

protected:

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
        // the display (always kDisplayH), then the rows of cells under it: every row has the STANDARD height (the room of the page shared by the rows a page can hold, whether it has them or not), every cell is the
        // same rectangle whatever it holds (a knob, a stepper, a LED list, a LED toggle drawn in the middle of it), so the index chips of a row stand in one line. A list that does not fit its cell is a stepper.
        int y = hd.bottom() + m;
        if (display.kind != FxDisplay::Kind::None)
        {
            display.place ({ rect_.x + m, y, rect_.w - 2 * m, kDisplayH });
            y += kDisplayH + m;
        }
        knobArea_ = { rect_.x + m, y, rect_.w - 2 * m, std::max (0, rect_.bottom() - m - y) };
        const int rows = rowsPerPage(), rowH = knobArea_.h / std::max (1, rows), firstRow = firstRowOfPage();
        const int gapX = 8, cellW = (knobArea_.w - 3 * gapX) / 4;
        for (int i = 0; i < kMaxParams; ++i)
            kind_[i] = i < numParams_ ? baseKind (i) : Kind::Knob;
        for (int r = 0; r < rows; ++r)
            for (int col = 0; col < 4; ++col)
            {
                const int idx = (firstRow + r) * 4 + col;
                if (idx >= kMaxParams)
                    continue;
                // a lane (kQLane) above every cell for the index chip of its Q group
                const int kx = knobArea_.x + col * (cellW + gapX), ky = knobArea_.y + r * rowH + kQLane;
                const Rect cellRect { kx, ky, col == 3 ? knobArea_.right() - kx : cellW, std::max (72, rowH - kQLane) };
                if (idx < numParams_ && kind_[idx] == Kind::List && ! listFits (idx, cellRect.w, cellRect.h))
                    kind_[idx] = Kind::Stepper;
                knobs[idx].place (cellRect);
                steppers[idx].place (cellRect);
                lists[idx].place (cellRect);
                toggles[idx].place (cellRect);
            }
        for (int i = 0; i < kMaxParams; ++i)
        {
            const bool onPage = i < numParams_ && i >= firstRow * 4 && i < (firstRow + rows) * 4;
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
