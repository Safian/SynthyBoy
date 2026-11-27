// SynthyBoy - selected FX editor. The chain and header stay fixed; functional
// panels select data-driven rows of four controls. Small effects have one MAIN
// panel. Reverb uses MAIN/TONE/MOD; Compressor uses MAIN/BANDS/UPWARD.
// Conditional controls are hidden and rows collapse. Every applicable control
// belongs to exactly one panel, and every row retains its physical Q group.
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
#include "../native_fx_cells.h"
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
    Stepper rack;
    LedList tabs;   // view-local functional panel selection, no engine parameter
    MenuButton remove { "REMOVE", MenuButton::Icon::None };
    FxDisplay display;
    FxBandBoard bands;   // the racks of the bands of a splitter, in the place of the display
    Knob knobs[kMaxParams];
    Stepper steppers[kMaxParams];
    LedList lists[kMaxParams];
    LedToggle toggles[kMaxParams];
    NumberField numbers[kMaxParams];
    LedList bandSel;   // the VIEW-LOCAL BAND selector of the compressor (LOW / MID / HIGH): not a parameter, no Q member (fx_layout.h `kViewBand`)
    static constexpr int kSel = kMaxParams;   // the control number of the selector in the cells (after the parameters' numbers)
    QGroup groups[kMaxRows];   // by row on the page order: the visible rows (a row that is gone has none)

    FxSlotEditor()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        power.state.negativeOff = true;
        rack.setCaption ("RACK");
        rack.state.count = 3;
        addKid (power);
        addKid (rack);
        tabs.state.titleInLane = false;
        addKid (tabs, false);
        bandSel.state.titleInLane = true;
        bandSel.setTitle ("BAND");
        static const char* const kBandNames[3] = { "LOW", "MID", "HIGH" };
        bandSel.setOptions (kBandNames, 3);
        addKid (bandSel, false);
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

    /** Disable effect parameter editing while retaining POWER and panel navigation. */
    void setModuleEnabled (bool on)
    {
        const bool changed = on != moduleEnabled_;
        moduleEnabled_ = on;
        if (changed && nativeControls_ && rect_.w > 0)
            layout();
        rack.setEnabled (on); remove.setEnabled (on); display.setEnabled (on); bands.setEnabled (on);
        bandSel.setEnabled (on);
        for (int i = 0; i < kMaxParams; ++i)
        {
            const bool en = on && isLive (i);
            knobs[i].setEnabled (en); steppers[i].setEnabled (en); lists[i].setEnabled (en);
            toggles[i].setEnabled (en); numbers[i].setEnabled (en);
        }
        power.setEnabled (true); tabs.setEnabled (true);
        setInactive (! on);
        power.setInactive (false); tabs.setInactive (false);
        for (QGroup& g : groups)
            g.setInactive (! on);
    }

    void setNativeControls (bool native) noexcept
    {
        if (nativeControls_ == native)
            return;
        nativeControls_ = native;
        if (rect_.w > 0)
            layout();
    }
    bool nativeControls() const noexcept { return nativeControls_; }
    void setChainInset (int inset) noexcept { chainInset_ = inset; }

    Rect knobArea() const noexcept { return knobArea_; }
    Rect nativeControlsArea() const noexcept { return nativeLayout() != 0 ? knobArea_ : Rect {}; }
    const char* title() const noexcept { return name_; }
    Rect pathRect() const noexcept
    {
        const int x = power.rect().right() + 14;
        return { x, power.rect().y, std::max (0, rack.rect().x - x - 8), 18 };
    }
    Rect nameRect() const noexcept
    {
        const auto path = pathRect();
        return { path.x, path.y + 14, path.w, kHeaderH - 14 };
    }
    Rect noticeRect() const noexcept
    {
        const auto path = pathRect();
        const int x = path.x + static_cast<int> (std::ceil (Pen::textWidth (name_, theme::L().fTitle))) + 24;
        return { x, path.y, std::max (0, rack.rect().x - 8 - x), kHeaderH };
    }

    template <typename Visit>
    void visitNativeControls (Visit&& visit) const
    {
        if (! nativeControls_)
            return;
        visit (power.rect());
        visit (nameRect());
        visit (pathRect());
        visit (noticeRect());
        if (moduleEnabled_)
        {
            visit (rack.rect());
            if (removable_)
                visit (remove.rect());
        }
        if (numPages() > 1)
            visit (tabs.rect());
        if (nativeBandShown())
            visit (nativeBandRect());
        for (int r = 0; r < rowsOnPage(); ++r)
            for (int c = 0; c < 4; ++c)
                if (nativeKindOf (controlInCell (firstRowOfPage() + r, c)) != nativeui::kFxHidden)
                    visit (cellRect (r, c));
    }

    bool isNativeControl (int x, int y) const noexcept
    {
        bool found = false;
        visitNativeControls ([&] (Rect area) { found = found || area.contains (x, y); });
        return found;
    }

    int nativeLayout() const noexcept
    {
        if (! nativeControls_)
            return 0;
        const int top = knobArea_.y - 110;
        const bool side = sideMode();
        for (int i = 0; i < nativeui::kFxLayoutLast; ++i)
        {
            const nativeui::FxLayout& g = nativeui::kFxLayouts[i];
            if (g.top != top || (g.side != 0) != side)
                continue;
            if (! side)
            {
                if (g.rows == rowsPerPage())
                    return i + 1;
                continue;
            }
            // a side layout is named by the lines of the rows that are on the panel now (a row that is gone moves the ones below it up)
            bool same = g.rows == rowsOnPage();
            for (int r = 0; same && r < rowsOnPage(); ++r)
                same = r < 4 && g.lines[r] == sideLines (firstRowOfPage() + r);
            if (same)
                return i + 1;
        }
        return 0;
    }

    int nativeKindOf (int control) const noexcept
    {
        if (! isLive (control) || nativeLayout() == 0)
            return nativeui::kFxHidden;
        const auto& pi = *infoOf (control);
        switch (baseKind (control))
        {
            case Kind::Knob: return pi.min < 0.0f && pi.max > 0.0f ? nativeui::kFxBipolar : nativeui::kFxDial;
            case Kind::Toggle: return nativeui::kFxToggle;
            default: return nativeui::fxDomainKind (pi, styleModel_);
        }
    }

    /** DISPLAY LEFT, CONTROLS RIGHT (the table's `side` flag): only when the rows that are on the panel now fit the height at the right (otherwise the
        stacked layout applies, as for the FX Filter's TONE panel of the Formant model: three rows of 2 + 2 + 1 lines do not fit under the tabs). */
    bool sideMode() const noexcept
    {
        return lay_->side && lay_->displayH > 0 && display.kind != FxDisplay::Kind::None && sideFits();
    }
    /** The height the rows on the panel take in the side layout: a Q lane and the lines of every row. */
    int sideHeight() const noexcept
    {
        int h = 0;
        for (int r = 0; r < rowsOnPage(); ++r)
            h += kQLane + nativeui::kFxSideLine * sideLines (firstRowOfPage() + r);
        return h;
    }
    bool sideFits() const noexcept
    {
        const int top = rect_.y + 8 + kHeaderH + 8 + (numPages() > 1 ? 64 : 0);   // under the header and the tabs (layout())
        return rect_.h > 0 && rowsOnPage() > 0 && sideHeight() <= rect_.bottom() - 8 - top;
    }
    /** The lines of a row of a side page: two cells to a line, by the cells of its table row (stable while controls hide). */
    int sideLines (int visRow) const noexcept
    {
        if (visRow < 0 || visRow >= numVis_)
            return 1;
        int n = 0;
        for (const fxl::Cell& c : lay_->row[vis_[visRow]].cell)
            n += c.alt[0].key != nullptr ? 1 : 0;
        return std::max (1, (n + 1) / 2);
    }
    Rect cellRect (int row, int col) const noexcept
    {
        if (sideMode())
        {
            const int first = firstRowOfPage();
            int y = knobArea_.y;
            for (int r = 0; r < row; ++r)
                y += kQLane + nativeui::kFxSideLine * sideLines (first + r);
            return { knobArea_.x + (col % 2) * nativeui::kFxSideStride, y + kQLane + (col / 2) * nativeui::kFxSideLine,
                     nativeui::kFxSideCellW, nativeui::kFxSideLine };
        }
        const int gapX = 8, cellW = (knobArea_.w - 3 * gapX) / 4;
        const int rowH = knobArea_.h / std::max (1, rowsPerPage());
        const int x = knobArea_.x + col * (cellW + gapX);
        return { x, knobArea_.y + row * rowH + kQLane, col == 3 ? knobArea_.right() - x : cellW, nativeui::fxStackedCellH (rowH) };
    }

    Size minSize() const override { return { 1200, 400 }; }

    int currentType() const noexcept { return kind_; }
    /** The number of the controls of the page (the highest control number + 1: the controls of the type's own parameters come first, in the order of the engine's list, then the ones the table adds). */
    int numParams() const noexcept { return numParams_; }
    /** The rows that are on the pages now (a row with nothing live is not). */
    int numRows() const noexcept { return numVis_; }
    /** Stable row capacity of the selected functional panel. */
    int rowsPerPage() const noexcept
    {
        int rows = 0;
        const auto panels = fxl::panelsOf (kind_);
        for (int r = 0; r < lay_->numRows; ++r)
            rows += panels.rows[r] == page_;
        return std::clamp (rows, 1, fxl::kMaxPageRows);
    }
    /** One MAIN panel for small effects, otherwise two or three named panels. */
    int numPages() const noexcept { return fxl::panelsOf (kind_).count; }
    const char* panelName (int p) const noexcept { return fxl::panelsOf (kind_).names[std::clamp (p, 0, numPages() - 1)]; }
    int pageIndex() const noexcept { return page_; }
    bool isMoreView() const noexcept { return page_ != 0; } // historical query: any secondary panel
    int firstRowOfPage() const noexcept { return panelStart_[page_]; }
    /** The rows of the view on screen. */
    int rowsOnPage() const noexcept { return panelStart_[page_ + 1] - panelStart_[page_]; }
    /** The rows of the default view that are on now (a row with nothing live is not). */
    int numDefaultRows() const noexcept { return numDefaultVis_; }
    /** Is there a display area on the view on screen (the picture of the effect, or the racks of the bands of a splitter in its place)? */
    bool hasDisplay() const noexcept { return hasBands() || (display.kind != FxDisplay::Kind::None && (sideMode() || displayShown())); }
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
    int memberControl (int row, int col) const noexcept { const int p = controlInCell (row, col); return p >= 0 && p < kMaxParams && ctl_[p].live ? p : -1; }   // the selector is a cell, no Q member
    /** The BAND selector of the compressor: the selected band (0 low, 1 mid, 2 high) as the mode of the bands allows it (Low band: no high; High band: no low; Single band: the mid band only). */
    bool hasBandSelector() const noexcept { return hasBandSel_; }
    bool nativeBandShown() const noexcept { return nativeControls_ && moduleEnabled_ && hasBandSel_ && bandsMode_ != 3 && ! isMoreView(); }
    Rect nativeBandRect() const noexcept
    {
        const int row = nativeBandShown() ? rowOfControl (kSel) : -1;
        // The band stays on the right when LIMIT removes the preceding Ratio.
        return row >= 0 ? cellRect (row, 3) : Rect {};
    }
    int effectiveBand() const noexcept { return effBand_; }
    int bandSelection() const noexcept { return bandSel_; }
    void setBand (int sel)
    {
        sel = std::clamp (sel, 0, 2);
        if (sel == bandSel_ && flowed_)
            return;
        bandSel_ = sel;
        if (hasBandSel_ && flowed_)
            recomputeLive();
    }
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
    ControlModel* memberOf (int p) noexcept
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
        if (! changed)
        {
            updateHeaderVisibility();
            return;
        }
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
        styleModel_ = kind_ == synthyboy::kFxTypeFilter ? static_cast<int> (std::lround (valueOf ("fxfilter.model"))) : 0;
        for (int p = 0; p <= kSel; ++p)   // the values the conditions name, kept for the next change of the BAND selector
        {
            const fxl::Live& lv = ctl_[p].alt != nullptr ? ctl_[p].alt->live : fxl::Live {};
            snap_[p][0] = lv.key != nullptr && ! fxl::isView (lv.key) ? static_cast<float> (valueOf (lv.key)) : 0.0f;
            snap_[p][1] = lv.key2 != nullptr && ! fxl::isView (lv.key2) ? static_cast<float> (valueOf (lv.key2)) : 0.0f;
        }
        bandsMode_ = hasBandSel_ ? static_cast<int> (std::lround (valueOf ("compressor.bands"))) : 0;
        return recomputeLive();
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
        if (r < firstRowOfPage() || r >= firstRowOfPage() + rowsOnPage())
        {
            for (int panel = 0; panel < numPages(); ++panel)
                if (r >= panelStart_[panel] && r < panelStart_[panel + 1])
                    page_ = panel;
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
    void assignIds (int baseId, int powerId, int rackId, int tabsId, int removeId)
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
        tabs.setId (tabsId);
        bandSel.setId (baseId + kSel);
        remove.setId (removeId);
    }
    Rect displayRect() const noexcept { return hasBands() ? bands.rect() : display.rect(); }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (isNativeControl (x, y))
            return Change::none();
        return Composite::touchDown (x, y, nowMs);
    }

    Change turnQ (int knobIndex, double delta) { return numVis_ > 0 ? groups[activeRow()].turn (knobIndex, delta) : Change::none(); }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        {
            // Native controls still share the Gen1 bank frame. QGroup compiles
            // this drawing out in Gen2, without changing physical routing.
            const int act = activeRow();
            if (act >= 0 && act < numVis_)
                groups[act].draw (sb);
            // the other rows of the page are a quad too: their chips stand there quietly, a touch on them makes it the active one (its values are then on the Q-Links)
            for (int r = firstRowOfPage(); r < firstRowOfPage() + rowsOnPage() && r < numVis_; ++r)
                if (r != act && groups[r].numMembers() > 0)
                    groups[r].drawIdle (sb);
        }
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
        rack.place ({ right - 230, hd.y, 230, kHeaderH });
        updateHeaderVisibility();
        // the display (the height the table says), then the rows of cells under it: every row has the STANDARD height (the room of the page shared by the rows a page can hold, whether it has them or not), every cell is the
        // same rectangle whatever it holds (a knob, a stepper, a LED list, a LED toggle, a number field drawn in the middle of it), so the index chips of a row stand in one line. A list that does not fit its cell is a stepper.
        int y = hd.bottom() + m;
        if (numPages() > 1)
        {
            tabs.place ({ hd.x, y, std::min (600, hd.w), 56 });
            y += 64;
        }
        show (display, display.kind != FxDisplay::Kind::None && (sideMode() || displayShown()));
        if (sideMode())
        {
            // the display at the left over the whole height, the cells at the right (cellRect)
            const int bottom = rect_.bottom() - m;
            display.place ({ rect_.x + m, y, nativeui::kFxSideX - 2 * m,std::max (0, bottom - y) });
            knobArea_ = { rect_.x + nativeui::kFxSideX, y, rect_.right() - m - (rect_.x + nativeui::kFxSideX), std::max (0, bottom - y) };
            layoutCells();
            return;
        }
        const int displayH = lay_->displayH > 0 ? std::max (0, lay_->displayH - chainInset_) : 0;
        if (displayH > 0 && display.kind != FxDisplay::Kind::None && displayShown())
        {
            display.place ({ rect_.x + m, y, rect_.w - 2 * m, displayH });
            y += displayH + m;
        }
        else if (displayH > 0 && hasBands())
        {
            bands.place ({ rect_.x + m, y, rect_.w - 2 * m, displayH });
            y += displayH + m;
        }
        knobArea_ = { rect_.x + m, y, rect_.w - 2 * m, std::max (0, rect_.bottom() - m - y) };
        layoutCells();
    }

    /** The cells of the page in their rectangles (cellRect), the unused controls hidden. */
    void layoutCells()
    {
        const int rows = rowsPerPage(), first = firstRowOfPage();
        for (int p = 0; p < kMaxParams; ++p)
        {
            ctl_[p].kind = baseKind (p);
            knobs[p].setEnabled (true);
            steppers[p].setEnabled (true);
            lists[p].setEnabled (true);
            toggles[p].setEnabled (true);
            numbers[p].setEnabled (true);
        }
        bool onPage[kMaxParams + 1] = {};
        for (int r = 0; r < rows && r < rowsOnPage(); ++r)
            for (int col = 0; col < 4; ++col)
            {
                const int v = first + r;
                const int p = controlInCell (v, col);
                if (p < 0)
                    continue;
                // a lane (kQLane) above every cell for the index chip of its Q group
                const Rect bounds = cellRect (r, col);
                // both controls of a cell (the one shown and the one that waits for the other sync) lie in the same rectangle
                for (int a = 0; a < fxl::kAlts; ++a)
                {
                    const int q = cellCtl_[vis_[v]][shownCol_[vis_[v]][col]][a];
                    if (q < 0)
                        continue;
                    if (q == kSel)
                    {
                        bandSel.place (bounds);
                        continue;
                    }
                    if (ctl_[q].kind == Kind::List && ! listFits (q, bounds.w, bounds.h))
                        ctl_[q].kind = Kind::Stepper;
                    knobs[q].place (bounds);
                    steppers[q].place (bounds);
                    lists[q].place (bounds);
                    toggles[q].place (bounds);
                    numbers[q].place (bounds);
                }
                onPage[p] = true;
            }
        show (bandSel, false);
        bandSel.setEnabled (moduleEnabled_);
        bandSel.state.index = effBand_;
        bandSel.state.disabledMask = static_cast<uint8_t> ((bandsMode_ == 2 ? 1u : 0u) | (bandsMode_ == 1 ? 4u : 0u));   // High band: no low; Low band: no high
        for (int p = 0; p < kMaxParams; ++p)
        {
            show (knobs[p], false);
            show (steppers[p], false);
            show (lists[p], false);
            show (toggles[p], false);
            show (numbers[p], false);
            const bool live = moduleEnabled_ && ctl_[p].live;
            knobs[p].setEnabled (live);
            steppers[p].setEnabled (live);
            lists[p].setEnabled (live);
            toggles[p].setEnabled (live);
            numbers[p].setEnabled (live);
        }
        updateGroups();
    }

    void drawUnder (Pen&) const override {}

    void drawExtra (Pen&) const override {}

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
        bandSel_ = 1;   // the middle band: the one every band mode has
        for (auto& r : cellCtl_)
            for (auto& c : r)
                for (int& a : c)
                    a = -1;
        hasBandSel_ = false;
        const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (kind_ < synthyboy::kNumFxInstanceTypes ? kind_ : 0);
        int nextExtra = desc.count;
        numParams_ = 0;
        for (int r = 0; r < lay_->numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (int a = 0; a < fxl::kAlts; ++a)
                {
                    const fxl::Alt& alt = lay_->row[r].cell[c].alt[a];
                    if (alt.key == nullptr)
                        continue;
                    if (fxl::isView (alt.key))   // the view-local selector: a cell, not an engine parameter
                    {
                        ctl_[kSel].alt = &alt;
                        ctl_[kSel].row = r;
                        ctl_[kSel].col = c;
                        ctl_[kSel].slot = a;
                        cellCtl_[r][c][a] = kSel;
                        hasBandSel_ = true;
                        continue;
                    }
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

    /** The live state of every control from the values of the last `setState` and the BAND selector; reflows when something changed. */
    bool recomputeLive()
    {
        bool changed = ! flowed_;
        // the effective band: the selection as the mode of the bands allows it (Low band: low + mid; High band: mid + high; Single band: mid only)
        const int eff = bandsMode_ == 3 ? 1 : (bandsMode_ == 1 && bandSel_ == 2 ? 1 : (bandsMode_ == 2 && bandSel_ == 0 ? 1 : bandSel_));
        changed = changed || eff != effBand_;
        effBand_ = eff;
        for (int p = 0; p <= kSel; ++p)
        {
            if (p < kMaxParams ? ctl_[p].info == nullptr : ctl_[p].alt == nullptr)
                continue;
            const fxl::Live& lv = ctl_[p].alt->live;
            auto value = [&] (const char* key, int which) { return fxl::isView (key) ? static_cast<float> (effBand_) : snap_[p][which]; };
            const bool live = lv.always() || (fxl::Live::in (lv.modes, value (lv.key, 0)) && (lv.key2 == nullptr || fxl::Live::in (lv.modes2, value (lv.key2, 1))));
            changed = changed || live != ctl_[p].live;
            ctl_[p].live = live;
        }
        if (changed)
            reflow();
        return changed;
    }

    /** The rows that have something live, the control every cell shows (the live one: a control that is not live is not shown; the live ones of a row stand together from the left) and the Q groups; then the views and the layout follow. */
    void reflow()
    {
        numVis_ = 0;
        numDefaultVis_ = 0;
        for (auto& r : shown_)
            for (int& c : r)
                c = -1;
        const auto panels = fxl::panelsOf (kind_);
        for (int panel = 0; panel < panels.count; ++panel)
        {
        panelStart_[panel] = numVis_;
        for (int r = 0; r < lay_->numRows; ++r)
        {
            if (panels.rows[r] != panel)
                continue;
            int n = 0;
            for (int c = 0; c < 4; ++c)
                for (int a = 0; a < fxl::kAlts; ++a)
                {
                    const int p = cellCtl_[r][c][a];
                    if (p >= 0 && ctl_[p].live)
                    {
                        shown_[r][n] = p;
                        shownCol_[r][n] = c;
                        ++n;
                        break;
                    }
                }
            if (n == 0)
                continue;   // nothing live: the row is gone
            numDefaultVis_ += panel == 0 ? 1 : 0;
            vis_[numVis_++] = r;
        }
        }
        panelStart_[panels.count] = numVis_;
        flowed_ = true;
        page_ = std::clamp (page_, 0, numPages() - 1);
        const int act = activeRow();
        const bool known = numVis_ > 0 && vis_[act] == activeTable_;
        if (! known)
            activeTable_ = numVis_ > 0 ? vis_[std::min (std::max (0, firstRowOfPage()), numVis_ - 1)] : -1;
        applyPage();
    }

    /** Native header visibility and functional panel selection. */
    void updateHeaderVisibility()
    {
        show (power, false);
        show (rack, false);
        show (remove, false);
        const auto panels = fxl::panelsOf (kind_);
        tabs.setOptions (panels.names, panels.count);
        tabs.state.index = page_;
        show (tabs, false);
    }

    void applyPage()
    {
        page_ = std::clamp (page_, 0, numPages() - 1);
        tabs.state.index = page_;
        const int act = activeRow();
        if (numVis_ > 0 && (act < firstRowOfPage() || act >= firstRowOfPage() + rowsOnPage()))
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

    /** Keep the live display when two control rows fit below it. */
    bool displayShown() const noexcept { return rowsPerPage() <= 2 || numPages() == 1; }

    int kind_ = -1;
    const fxl::Layout* lay_ = &fxl::layoutOf (0);
    bool resolved_ = false, flowed_ = false;
    int numParams_ = 0;
    Ctl ctl_[kMaxParams + 1];   // the parameters' controls, then the selector (kSel)
    int cellCtl_[kMaxRows][4][fxl::kAlts] = {};
    float snap_[kMaxParams + 1][2] = {};   // the values the conditions of the controls name, as of the last `setState`
    bool hasBandSel_ = false;
    int bandSel_ = 1, effBand_ = 1, bandsMode_ = 0;
    int shown_[kMaxRows][4] = {};
    int shownCol_[kMaxRows][4] = {};   // the column of the table a shown cell is the (compacted) cell of
    int vis_[kMaxRows] = {};
    int numVis_ = 0, numDefaultVis_ = 0;
    int panelStart_[4] = {};
    int activeTable_ = -1, page_ = 0;
    bool removable_ = false;
    char name_[24] = {}, path_[72] = {};
    Rect knobArea_;
    bool nativeControls_ = true, moduleEnabled_ = true;
    int chainInset_ = 0;
    int styleModel_ = 0;
};

}} // namespace synthyboy_mpc::ui
