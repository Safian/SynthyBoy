// SynthyBoy - MPC UI component library: the FX SLOT EDITOR component (FxSlotEditor): the page of the selected effect. From the top: the power switch, the name of the effect, its rack (MAIN / BUS 1 / BUS 2),
// the page of the knobs, REMOVE; the display of the effect (the response, the transfer curve, the echoes ... fx_display.h) for the types that have one; the knobs of the parameters in rows of four, ONE Q group
// per row, every knob with the parameter's own name and value (the unit from the engine's descriptor). An effect with more parameters than a page holds (8 under a display, 12 without) has its pages: a
// stepper moves between them, a Q group on another page makes that page the one on screen. Nothing is left empty: without a display the knobs take its room.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <vector>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "fx_display.h"
#include "qgroup.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace ui {

class FxSlotEditor : public Composite
{
public:
    static constexpr int kMaxParams = synthyboy::kFxInstanceParams; // 24
    static constexpr int kMaxRows = 6;                              // 6 rows of 4 = 24 params
    static constexpr int kHeaderH = 56, kDisplayH = 188;

    ToggleIcon power;
    Stepper rack, page;
    MenuButton remove { "REMOVE", MenuButton::Icon::None };
    FxDisplay display;
    Knob knobs[kMaxParams];
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

    /** The effect on the page: the display kind, the knobs of its parameters (the owner labels them), the pages. `removable`: a slot of the pool (the modules of the fixed chain cannot be removed). */
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
            knobs[i].setEnabled (i < numParams_);
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
            knobs[i].setId (baseId + i);
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
        if (cap_)   // a tap on a knob makes its row the Q group
            for (int i = 0; i < numParams_; ++i)
                if (cap_ == &knobs[i])
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
    }

protected:
    /** The knobs of the page are shown, the others are not; the layout follows. */
    void syncPage()
    {
        page_ = std::clamp (page_, 0, numPages() - 1);
        const int first = firstRowOfPage() * 4, last = first + rowsPerPage() * 4;
        for (int i = 0; i < kMaxParams; ++i)
            show (knobs[i], i < numParams_ && i >= first && i < last);
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
        // the display, then the rows of knobs under it
        int y = hd.bottom() + m;
        if (display.kind != FxDisplay::Kind::None)
        {
            display.place ({ rect_.x + m, y, rect_.w - 2 * m, kDisplayH });
            y += kDisplayH + m;
        }
        knobArea_ = { rect_.x + m, y, rect_.w - 2 * m, std::max (0, rect_.bottom() - m - y) };
        const int rows = rowsPerPage(), rowH = knobArea_.h / std::max (1, rows);
        const int gapX = 8, cellW = (knobArea_.w - 3 * gapX) / 4;
        const int firstRow = firstRowOfPage();
        for (int r = 0; r < rows; ++r)
            for (int col = 0; col < 4; ++col)
            {
                const int idx = (firstRow + r) * 4 + col;
                if (idx >= kMaxParams)
                    continue;
                // a lane (kQLane) above every knob for the index chip of its Q group
                const int kx = knobArea_.x + col * (cellW + gapX), ky = knobArea_.y + r * rowH + kQLane;
                knobs[idx].place ({ kx, ky, col == 3 ? knobArea_.right() - kx : cellW, std::max (72, rowH - kQLane - 4) });
            }
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
    int activeRow_ = 0, page_ = 0;
    bool removable_ = false;
    char name_[24] = {};
    Rect knobArea_;
};

}} // namespace synthyboy_mpc::ui
