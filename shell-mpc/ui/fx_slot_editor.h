// SynthyBoy - MPC UI component library: the FX SLOT EDITOR component (FxSlotEditor): the page of the selected effect. From the top: the power switch, the name of the effect, its rack (MAIN / BUS 1 / BUS 2),
// the page of the controls, REMOVE; the display of the effect (the response, the transfer curve, the echoes, the bands ... fx_display.h) for the effects that have one; the controls in rows of four.
// WHAT STANDS WHERE comes from ONE table per effect (fx_layout.h): a row is a block of the effect and ONE Q group, a cell holds one control or two that exclude each other (RATE / DIVISION: the one that is live in
// the sync of the effect), and one rule says what happens to a control that does nothing in the state of the effect: it is hidden when no live control follows it in its row (the row closes up, a row with nothing live
// is gone and the rows below move up), else it is drawn dim in its place. The pages are made of the rows that are left. The owner (fx_ui.h) says the state of the effect with `setState`.
// A control is shown by its kind: a continuous value is a knob, a whole number (voices) a NUMBER FIELD, an on / off one a LED toggle, one with 3 to 8 values (or two named ones) a LED list with ALL values visible, a
// longer one (or one whose list does not fit its cell) the stepper with its popup. A list's name stands in the lane of the index chip; every row of a page has the standard height and every cell the same rectangle.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "fx_band_board.h"
#include "fx_display.h"
#include "fx_layout.h"
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
    static constexpr int kMaxRows = fxl::kMaxRows;                  // rows (blocks) of a table
    static constexpr int kHeaderH = 56, kDisplayH = 176;
    char notice[128] = {};   // a line in the header right of the effect's name ("content missing: <name>"); empty = none. The owner sets it at every update.
    /** How the control of a cell is shown. */
    enum class Kind : uint8_t { Knob, Stepper, List, Toggle, Number };

    ToggleIcon power;
    Stepper rack, page;
    MenuButton remove { "REMOVE", MenuButton::Icon::None };
    FxDisplay display;
    FxBandBoard bands;   // the racks of the bands of a splitter, in the place of the display
    Knob knobs[kMaxParams];
    Stepper steppers[kMaxParams];
    LedList lists[kMaxParams];
    LedToggle toggles[kMaxParams];
    NumberField numbers[kMaxParams];
    QGroup groups[kMaxRows];   // by row on the page order: the visible rows (a row that is gone has none)

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
        addKid (bands, false);
        for (int i = 0; i < kMaxParams; ++i)
        {
            knobs[i].state.side = true;   // the dial at the left, the name and the value beside it: the cells are wide
            addKid (knobs[i], false);
            steppers[i].listable = true;
            steppers[i].minWidth = 100;
            addKid (steppers[i], false);
            lists[i].state.titleInLane = true;
            toggles[i].centre = 0.5f;
            numbers[i].state.format = NumberField::Format::IntPlain;
            numbers[i].maxBox = { 200, 84 };
            addKid (lists[i], false);
            addKid (toggles[i], false);
            addKid (numbers[i], false);
        }
        setType (synthyboy::kFxTypeChorus, true);
    }

    Size minSize() const override { return { 1200, 400 }; }

    int currentType() const noexcept { return kind_; }
    /** The number of the controls of the page (the highest control number + 1: the controls of the type's own parameters come first, in the order of the engine's list, then the ones the table adds). */
    int numParams() const noexcept { return numParams_; }
    /** The rows that are on the pages now (a row with nothing live is not). */
    int numRows() const noexcept { return numVis_; }
    int rowsPerPage() const noexcept { return lay_->rowsPerPage; }
    int numPages() const noexcept { return std::max (1, (numVis_ + rowsPerPage() - 1) / rowsPerPage()); }
    int pageIndex() const noexcept { return page_; }
    int firstRowOfPage() const noexcept { return page_ * rowsPerPage(); }
    /** Is there a display area (the picture of the effect, or the racks of the bands of a splitter in its place)? */
    bool hasDisplay() const noexcept { return display.kind != FxDisplay::Kind::None || hasBands(); }
    /** The page of a splitter shows the racks of its bands (the band board) where other effects have their display. */
    bool hasBands() const noexcept { return kind_ >= fxl::kKindSplitterLh; }
    /** The place of the effect in the chain ("MAIN > SPLITTER L/M/H > MID BAND"), the small line over its name. */
    void setPath (const char* path) { setText (path_, sizeof path_, path != nullptr ? path : ""); }
    const char* path() const noexcept { return path_; }
    int displayHeight() const noexcept { return lay_->displayH; }
    const fxl::Layout& layoutTable() const noexcept { return *lay_; }

    /** The engine's parameter behind control p (null: no such control), the control of an engine key (-1: the effect has none), and the table row (block) of a row on the pages. */
    const synthyboy::ParamInfo* infoOf (int p) const noexcept { return p >= 0 && p < kMaxParams ? ctl_[p].info : nullptr; }
    const fxl::Alt* altOf (int p) const noexcept { return p >= 0 && p < kMaxParams ? ctl_[p].alt : nullptr; }
    int controlOfKey (const char* key) const noexcept
    {
        for (int p = 0; p < kMaxParams; ++p)
            if (ctl_[p].info != nullptr && std::strcmp (ctl_[p].info->key, key) == 0)
                return p;
        return -1;
    }
    /** The name written on control p (capitals; the table's label, the law's label or the engine's name without the name of its effect). */
    void nameOf (int p, char* out, size_t cap) const
    {
        if (infoOf (p) == nullptr)
        {
            std::snprintf (out, cap, "%s", "");
            return;
        }
        fxl::controlName (*ctl_[p].alt, *ctl_[p].info, lay_->name, out, cap);
    }
    /** Is the control live in the state of the effect (last `setState`)? */
    bool isLive (int p) const noexcept { return p >= 0 && p < kMaxParams && ctl_[p].info != nullptr && ctl_[p].live; }
    /** The control that stands in cell `col` of row `row` (a row on the pages, counted from 0) now, live or dim: -1 where the cell shows nothing. */
    int controlInCell (int row, int col) const noexcept { return row >= 0 && row < numVis_ && col >= 0 && col < 4 ? shown_[vis_[row]][col] : -1; }
    /** The same for the live ones only: the members of the Q group of the row. */
    int memberControl (int row, int col) const noexcept { const int p = controlInCell (row, col); return p >= 0 && ctl_[p].live ? p : -1; }
    const char* rowTitle (int row) const noexcept { return row >= 0 && row < numVis_ ? lay_->row[vis_[row]].title : ""; }
    /** The row on the pages (counted from 0) a control stands in; -1 where it is not on any. */
    int rowOfControl (int p) const noexcept
    {
        for (int v = 0; v < numVis_; ++v)
            for (int c = 0; c < 4; ++c)
                if (shown_[vis_[v]][c] == p)
                    return v;
        return -1;
    }

    bool isChoice (int p) const noexcept
    {
        const synthyboy::ParamInfo* pi = infoOf (p);
        return pi != nullptr && pi->numChoices > 0 && pi->choices != nullptr;
    }
    /** The kind a parameter is shown as by its values alone: a knob, a number field (whole numbers), an on / off toggle, a list of 3 .. 8 values (or two named ones), a stepper for more than 8. */
    Kind baseKind (int p) const noexcept
    {
        const synthyboy::ParamInfo* pi = infoOf (p);
        if (pi == nullptr)
            return Kind::Knob;
        if (pi->numChoices <= 0 || pi->choices == nullptr)
            return pi->interval == 1.0f && pi->max - pi->min <= 100.0f ? Kind::Number : Kind::Knob;
        if (pi->numChoices > LedList::kMaxOptions)
            return Kind::Stepper;
        if (pi->numChoices == 2 && std::strcmp (pi->choices[0], "Off") == 0 && std::strcmp (pi->choices[1], "On") == 0)
            return Kind::Toggle;
        return Kind::List;
    }
    /** The kind of the cell now (a list that does not fit its cell is a stepper: decided by the layout). */
    Kind kindOf (int p) const noexcept { return p >= 0 && p < kMaxParams ? ctl_[p].kind : Kind::Knob; }
    Component* memberOf (int p) noexcept
    {
        switch (kindOf (p))
        {
            case Kind::Stepper: return &steppers[p];
            case Kind::List: return &lists[p];
            case Kind::Toggle: return &toggles[p];
            case Kind::Number: return &numbers[p];
            default: return &knobs[p];
        }
    }

    /** The effect on the page: `kind` is a type of the pool (1 .. 13) or a splitter (fxl::kKindSplitter*), `removable` a slot of the pool (the modules of the fixed chain cannot be removed). The controls are the ones of
        the effect's table (fx_layout.h). */
    void setType (int kind, bool removable)
    {
        kind = std::clamp (kind, 0, fxl::kNumKinds - 1);
        const bool changed = kind != kind_ || ! resolved_;
        if (! changed && removable == removable_)
            return;
        removable_ = removable;
        show (remove, removable);
        if (! changed)
            return;
        kind_ = kind;
        resolved_ = true;
        lay_ = &fxl::layoutOf (kind);
        display.setKindFromType (kind);
        setText (name_, sizeof name_, lay_->name);
        show (display, display.kind != FxDisplay::Kind::None);
        show (bands, hasBands());
        resolve();
        page_ = 0;
        activeTable_ = -1;
        reflow();
        setActiveRow (0);
    }

    /** The state of the effect: `valueOf (key)` is the plain value of the engine key `key` of the open effect (the keys the conditions of the table name). Controls become live or not; the rows that are gone close up.
        Returns true when something changed on the page. */
    template <class ValueOf>
    bool setState (ValueOf&& valueOf)
    {
        bool changed = ! flowed_;
        for (int p = 0; p < kMaxParams; ++p)
        {
            if (ctl_[p].info == nullptr)
                continue;
            const fxl::Live& lv = ctl_[p].alt->live;
            const bool live = lv.always() || lv.holds (static_cast<float> (valueOf (lv.key)));
            changed = changed || live != ctl_[p].live;
            ctl_[p].live = live;
        }
        if (changed)
            reflow();
        return changed;
    }

    int activeRow() const noexcept
    {
        for (int v = 0; v < numVis_; ++v)
            if (vis_[v] == activeTable_)
                return v;
        return 0;
    }
    void setActiveRow (int r)
    {
        if (numVis_ <= 0)
        {
            activeTable_ = -1;
            return;
        }
        r = std::clamp (r, 0, numVis_ - 1);
        activeTable_ = vis_[r];
        if (r < firstRowOfPage() || r >= firstRowOfPage() + rowsPerPage())
        {
            page_ = r / rowsPerPage();
            applyPage();
        }
        updateGroups();
    }
    void setPage (int p)
    {
        page_ = std::clamp (p, 0, numPages() - 1);
        applyPage();
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
            numbers[i].setId (baseId + i);
        }
        power.setId (powerId);
        rack.setId (rackId);
        page.setId (pageId);
        remove.setId (removeId);
    }
    Rect displayRect() const noexcept { return hasBands() ? bands.rect() : display.rect(); }
    Rect knobArea() const noexcept { return knobArea_; }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)   // a tap on a control makes its row the Q group
            for (int i = 0; i < kMaxParams; ++i)
                if (cap_ == &knobs[i] || cap_ == &steppers[i] || cap_ == &lists[i] || cap_ == &toggles[i] || cap_ == &numbers[i])
                {
                    const int v = rowOfControl (i);
                    if (v >= 0)
                        setActiveRow (v);
                    break;
                }
        return c;
    }

    Change turnQ (int knobIndex, double delta) { return numVis_ > 0 ? groups[activeRow()].turn (knobIndex, delta) : Change::none(); }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        const int act = activeRow();
        if (act >= 0 && act < numVis_)
            groups[act].draw (sb);
        // the other rows of the page are a quad too: their chips stand there quietly, a touch on them makes it the active one (its values are then on the Q-Links)
        for (int r = firstRowOfPage(); r < firstRowOfPage() + rowsPerPage() && r < numVis_; ++r)
            if (r != act && groups[r].numMembers() > 0)
                groups[r].drawIdle (sb);
    }

public:
    /** Does the LED list of control `p` fit a cell of this size (some column count with rows of at least 56 px and columns wide enough for the longest name)? */
    bool listFits (int p, int w, int h) const
    {
        const synthyboy::ParamInfo* pi = infoOf (p);
        if (pi == nullptr || pi->choices == nullptr)
            return false;
        int need = 0;
        for (int i = 0; i < pi->numChoices; ++i)
            need = std::max (need, led::widthFor (shownChoice (pi->key, i, pi->choices[i])));
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
        // the display (the height the table says), then the rows of cells under it: every row has the STANDARD height (the room of the page shared by the rows a page can hold, whether it has them or not), every cell is the
        // same rectangle whatever it holds (a knob, a stepper, a LED list, a LED toggle, a number field drawn in the middle of it), so the index chips of a row stand in one line. A list that does not fit its cell is a stepper.
        int y = hd.bottom() + m;
        if (lay_->displayH > 0 && display.kind != FxDisplay::Kind::None)
        {
            display.place ({ rect_.x + m, y, rect_.w - 2 * m, lay_->displayH });
            y += lay_->displayH + m;
        }
        else if (lay_->displayH > 0 && hasBands())
        {
            bands.place ({ rect_.x + m, y, rect_.w - 2 * m, lay_->displayH });
            y += lay_->displayH + m;
        }
        knobArea_ = { rect_.x + m, y, rect_.w - 2 * m, std::max (0, rect_.bottom() - m - y) };
        const int rows = rowsPerPage(), rowH = knobArea_.h / std::max (1, rows), gapX = 8, cellW = (knobArea_.w - 3 * gapX) / 4, first = firstRowOfPage();
        for (int p = 0; p < kMaxParams; ++p)
        {
            ctl_[p].kind = baseKind (p);
            knobs[p].setEnabled (true);
            steppers[p].setEnabled (true);
            lists[p].setEnabled (true);
            toggles[p].setEnabled (true);
            numbers[p].setEnabled (true);
        }
        bool onPage[kMaxParams] = {};
        for (int r = 0; r < rows; ++r)
            for (int col = 0; col < 4; ++col)
            {
                const int v = first + r;
                const int p = controlInCell (v, col);
                if (p < 0)
                    continue;
                // a lane (kQLane) above every cell for the index chip of its Q group
                const int kx = knobArea_.x + col * (cellW + gapX), ky = knobArea_.y + r * rowH + kQLane;
                const Rect cellRect { kx, ky, col == 3 ? knobArea_.right() - kx : cellW, std::max (72, rowH - kQLane) };
                // both controls of a cell (the one shown and the one that waits for the other sync) lie in the same rectangle
                for (int a = 0; a < 2; ++a)
                {
                    const int q = cellCtl_[vis_[v]][col][a];
                    if (q < 0)
                        continue;
                    if (ctl_[q].kind == Kind::List && ! listFits (q, cellRect.w, cellRect.h))
                        ctl_[q].kind = Kind::Stepper;
                    knobs[q].place (cellRect);
                    steppers[q].place (cellRect);
                    lists[q].place (cellRect);
                    toggles[q].place (cellRect);
                    numbers[q].place (cellRect);
                }
                onPage[p] = true;
            }
        for (int p = 0; p < kMaxParams; ++p)
        {
            const bool on = onPage[p] && ctl_[p].info != nullptr;
            const bool live = ctl_[p].live;
            show (knobs[p], on && ctl_[p].kind == Kind::Knob);
            show (steppers[p], on && ctl_[p].kind == Kind::Stepper);
            show (lists[p], on && ctl_[p].kind == Kind::List);
            show (toggles[p], on && ctl_[p].kind == Kind::Toggle);
            show (numbers[p], on && ctl_[p].kind == Kind::Number);
            // what does nothing is dim and takes no touch
            knobs[p].setEnabled (live);
            steppers[p].setEnabled (live);
            lists[p].setEnabled (live);
            toggles[p].setEnabled (live);
            numbers[p].setEnabled (live);
        }
        updateGroups();
    }

    void drawUnder (Pen& p) const override
    {
        // the controls lie on a block of their own (the page of the effect)
        p.block ({ knobArea_.x - 2, knobArea_.y - 2, knobArea_.w + 4, knobArea_.h + 4 }, false);
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const int tx = rect_.x + 8 + 56 + 14, tw = std::max (0, rack.rect().x - tx - 8);
        if (path_[0] != '\0')
        {
            p.textLeft ({ tx, rect_.y + 8, tw, 18 }, path_, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
            p.textLeft ({ tx, rect_.y + 8 + 14, tw, kHeaderH - 14 }, name_, T.fTitle, p.c (T.text, 255), T.fMin);
        }
        else
            p.textLeft ({ tx, rect_.y + 8, tw, kHeaderH }, name_, T.fTitle, p.c (T.text, 255), T.fMin);
        if (notice[0] != '\0')   // a notice in the header, right of the name (the missing impulse of Convolve): the name of a file loses its middle when it does not fit (content_name.h)
        {
            const int nx = tx + static_cast<int>(std::ceil (Pen::textWidth (name_, T.fTitle))) + 24;
            const Rect box { nx, rect_.y + 8, std::max (0, rack.rect().x - 8 - nx), kHeaderH };
            const float size = Pen::fitSize (notice, static_cast<float> (box.w), static_cast<float> (box.h), T.fLabel + 2.0f, T.fMin);
            if (size > 0.0f)
            {
                char fitted[160];
                contentname::middleFit (notice, size, static_cast<float> (box.w), fitted, static_cast<int> (sizeof fitted));
                p.textRight (box, fitted, size, p.c (T.negative, 255), T.fMin);   // at the size the middle cut was made for (a larger maximum would let the pen cut the end of the cut text again)
            }
        }
    }

private:
    struct Ctl
    {
        const synthyboy::ParamInfo* info = nullptr;
        const fxl::Alt* alt = nullptr;
        int row = -1, col = -1, slot = -1;
        bool live = true;
        Kind kind = Kind::Knob;
    };

    /** The controls of the table: the engine's parameter of every key, numbered like the parameters of the type's own list (so that control p of a type of the pool IS its parameter p), the ones the type's list does not
        have (the splitters, the force enable of the reverb) after them. */
    void resolve()
    {
        for (Ctl& c : ctl_)
            c = Ctl {};
        for (auto& r : cellCtl_)
            for (auto& c : r)
                c[0] = c[1] = -1;
        const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (kind_ < synthyboy::kNumFxInstanceTypes ? kind_ : 0);
        int nextExtra = desc.count;
        numParams_ = 0;
        for (int r = 0; r < lay_->numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (int a = 0; a < 2; ++a)
                {
                    const fxl::Alt& alt = lay_->row[r].cell[c].alt[a];
                    if (alt.key == nullptr)
                        continue;
                    const synthyboy::ParamInfo* pi = synthyboy::findParamByKey (alt.key);
                    if (pi == nullptr)
                        continue;
                    const int tableIdx = static_cast<int> (pi - synthyboy::paramTable());
                    int p = -1;
                    for (int q = 0; q < desc.count; ++q)
                        if (desc.source[q] == tableIdx)
                            p = q;
                    if (p < 0)
                        p = nextExtra++;
                    if (p >= kMaxParams)
                        continue;
                    ctl_[p].info = pi;
                    ctl_[p].alt = &alt;
                    ctl_[p].row = r;
                    ctl_[p].col = c;
                    ctl_[p].slot = a;
                    cellCtl_[r][c][a] = p;
                    numParams_ = std::max (numParams_, p + 1);
                }
        for (int p = 0; p < kMaxParams; ++p)
            ctl_[p].kind = baseKind (p);
        flowed_ = false;
    }

    /** The rows that have something live, the control every cell shows (the live one; a dead one with a live one after it in its row, dim) and the Q groups; then the pages and the layout follow. */
    void reflow()
    {
        numVis_ = 0;
        for (auto& r : shown_)
            for (int& c : r)
                c = -1;
        for (int r = 0; r < lay_->numRows; ++r)
        {
            int pick[4], deadFirst[4], lastLive = -1;
            for (int c = 0; c < 4; ++c)
            {
                pick[c] = -1;
                deadFirst[c] = cellCtl_[r][c][0] >= 0 ? cellCtl_[r][c][0] : cellCtl_[r][c][1];
                for (int a = 0; a < 2 && pick[c] < 0; ++a)
                {
                    const int p = cellCtl_[r][c][a];
                    if (p >= 0 && ctl_[p].live)
                        pick[c] = p;
                }
                if (pick[c] >= 0)
                    lastLive = c;
            }
            if (lastLive < 0)
                continue;   // nothing live: the row is gone
            for (int c = 0; c < 4; ++c)
                shown_[r][c] = pick[c] >= 0 ? pick[c] : (c < lastLive ? deadFirst[c] : -1);
            vis_[numVis_++] = r;
        }
        flowed_ = true;
        page_ = std::clamp (page_, 0, numPages() - 1);
        const int act = activeRow();
        const bool known = numVis_ > 0 && vis_[act] == activeTable_;
        if (! known)
            activeTable_ = numVis_ > 0 ? vis_[std::min (std::max (0, firstRowOfPage()), numVis_ - 1)] : -1;
        applyPage();
    }

    /** The page of the stepper, the layout of the page and the Q groups. */
    void applyPage()
    {
        page_ = std::clamp (page_, 0, numPages() - 1);
        show (page, numPages() > 1);
        page.state.count = numPages();
        page.state.index = page_;
        char t[32];
        std::snprintf (t, sizeof t, "PAGE %d / %d", page_ + 1, numPages());
        page.setItem (t);
        const int act = activeRow();
        if (numVis_ > 0 && (act < firstRowOfPage() || act >= firstRowOfPage() + rowsPerPage()))
            activeTable_ = vis_[std::min (firstRowOfPage(), numVis_ - 1)];
        if (rect_.w > 0)
            layout();
        else
            updateGroups();
    }

    /** The members of the Q groups are the components the live cells have now (a knob, a stepper, a LED list, a LED toggle or a number field): the same slot of the same row, whatever it looks like. */
    void updateGroups()
    {
        const int act = numVis_ > 0 ? activeRow() : -1;
        for (int v = 0; v < kMaxRows; ++v)
        {
            for (int c = 0; c < 4; ++c)
            {
                const int p = v < numVis_ ? memberControl (v, c) : -1;
                groups[v].set (c, p >= 0 ? memberOf (p) : nullptr);
            }
            groups[v].setActive (v == act);
        }
    }

    int kind_ = -1;
    const fxl::Layout* lay_ = &fxl::layoutOf (0);
    bool resolved_ = false, flowed_ = false;
    int numParams_ = 0;
    Ctl ctl_[kMaxParams];
    int cellCtl_[kMaxRows][4][2] = {};
    int shown_[kMaxRows][4] = {};
    int vis_[kMaxRows] = {};
    int numVis_ = 0;
    int activeTable_ = -1, page_ = 0;
    bool removable_ = false;
    char name_[24] = {}, path_[72] = {};
    Rect knobArea_;
};

}} // namespace synthyboy_mpc::ui
