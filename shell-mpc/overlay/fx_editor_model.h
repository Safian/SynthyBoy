// SynthyBoy - MPC overlay: the DATA of the effect page of the FX tab (owner decision 2026-11-27: the C++ layer has no UI elements). The page of the selected effect is a table of rows of four cells (ui/fx_layout.h);
// every control of it is a native cell of the host skin. This header holds what the plugin still has to know about that page: which control stands in which cell (the table, the conditions under which a control is live,
// the functional panels, the rows that close up), the engine parameter behind each control, how the skin lays the cells out (nativeLayout, the rectangles the page leaves open), the header facts (title, place, notice,
// power, rack), the compressor's view-local band choice, the lanes of a splitter (data only) and the display (ui::FxDisplay, a graph the host cannot draw). No touch, no drawing of a control.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../native_fx_cells.h"
#include "../ui/fx_display.h"
#include "../ui/fx_layout.h"
#include "../ui/qgroup.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace overlay {

/** The lanes of the page of a splitter as plain data: per band the facts of the tile and the cards of its rack (the native band lanes draw them, nativeui::fxbands). */
struct FxBandData
{
    static constexpr int kMaxBands = 3, kMaxCards = 13;
    struct Card { int item = -1, type = 0; bool on = false; };
    struct Lane
    {
        int kind = 0, band = 0, colour = 0, numCards = 0;
        float low = 0.0f, high = 0.0f;   // the crossover frequencies in Hz
        bool bypassed = false, idle = false, addEnabled = true;
        Card cards[kMaxCards];
    };
    int numBands = 0;
    Lane lane[kMaxBands];
};

class FxEditorModel
{
public:
    static constexpr int kMaxParams = synthyboy::kFxInstanceParams;   // 24
    static constexpr int kMaxRows = ui::fxl::kMaxRows;                 // rows (blocks) of a table
    static constexpr int kSel = kMaxParams;                            // the control number of the band selector in the cells (after the parameters' numbers)
    static constexpr int kMaxListOptions = 8;                          // a choice with more values is a dropdown (the stepper of the old editor)
    // the geometry of the page (1280 x 628 tab; the chain bar of the native chain is 120 px high, the editor lies under it)
    static constexpr int kTabY = 110, kTabH = 628, kHeaderH = 56, kQLane = 16, kMargin = 8, kChainBarH = 120;
    static constexpr int kEditorY = kTabY + kMargin + kChainBarH + kMargin;

    // ---- the header of the page: the facts the native header publishes -----------------------------------------------------------------------------------------------------------------------------------------
    bool powerOn = true;
    int rackIndex = 0, rackCount = 3;
    char notice[128] = {};                       // a line right of the effect's name ("content missing: <name>"); empty = none
    ui::FxDisplay display;                       // the graph of the effect (EQ, compressor, delay, filter, distortion)
    FxBandData bands;                            // the lanes of a splitter

    FxEditorModel() { setType (synthyboy::kFxTypeChorus, true); }

    void setCanvas (int w) noexcept { w_ = w; layout(); }

    // ---- geometry -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    ui::Rect editorRect() const noexcept { return { 0, kEditorY, w_, std::max (100, kTabY + kTabH - kEditorY) }; }
    ui::Rect headerRect() const noexcept { return { kMargin, kEditorY + kMargin, w_ - 2 * kMargin, kHeaderH }; }
    ui::Rect powerRect() const noexcept { const auto hd = headerRect(); return { hd.x, hd.y, 56, kHeaderH }; }
    ui::Rect removeRect() const noexcept { const auto hd = headerRect(); return { hd.right() - 130, hd.y, 130, kHeaderH }; }
    ui::Rect rackRect() const noexcept { const auto hd = headerRect(); return { hd.right() - 130 - 6 - 230, hd.y, 230, kHeaderH }; }
    ui::Rect tabsRect() const noexcept { const auto hd = headerRect(); return { hd.x, hd.bottom() + kMargin, std::min (600, hd.w), 56 }; }
    ui::Rect pathRect() const noexcept
    {
        const int x = powerRect().right() + 14;
        return { x, powerRect().y, std::max (0, rackRect().x - x - 8), 18 };
    }
    ui::Rect nameRect() const noexcept { const auto path = pathRect(); return { path.x, path.y + 14, path.w, kHeaderH - 14 }; }
    ui::Rect noticeRect() const noexcept
    {
        const auto path = pathRect();
        const int x = path.x + static_cast<int> (std::ceil (ui::Pen::textWidth (name_, ui::theme::L().fTitle))) + 24;
        return { x, path.y, std::max (0, rackRect().x - 8 - x), kHeaderH };
    }
    ui::Rect displayRect() const noexcept { return displayRect_; }

    /** Calls `visit` with the rectangle of every native control of the page (they stay open in the overlay's ground). */
    template <typename Visit>
    void visitNativeControls (Visit&& visit) const
    {
        visit (powerRect());
        visit (nameRect());
        visit (pathRect());
        visit (noticeRect());
        if (moduleEnabled_)
        {
            visit (rackRect());
            if (removable_)
                visit (removeRect());
        }
        if (numPages() > 1)
            visit (tabsRect());
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
        visitNativeControls ([&] (ui::Rect area) { found = found || area.contains (x, y); });
        return found;
    }

    /** The native layout of the page (1 .. kFxLayoutLast, 0 = none): by the top of the cells and the rows of the panel (or, side by side with the display, the lines of the rows). */
    int nativeLayout() const noexcept
    {
        const int top = knobArea_.y - kTabY;
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
            bool same = g.rows == rowsOnPage();   // a side layout is named by the lines of the rows that are on the panel now
            for (int r = 0; same && r < rowsOnPage(); ++r)
                same = r < 4 && g.lines[r] == sideLines (firstRowOfPage() + r);
            if (same)
                return i + 1;
        }
        return 0;
    }
    /** The kind of native cell a control is (nativeui::kFxHidden: none now). */
    int nativeKindOf (int control) const noexcept
    {
        if (! isLive (control) || nativeLayout() == 0)
            return nativeui::kFxHidden;
        const auto& pi = *infoOf (control);
        switch (baseKind (control))
        {
            case Kind::Dial: return pi.min < 0.0f && pi.max > 0.0f ? nativeui::kFxBipolar : nativeui::kFxDial;
            case Kind::Toggle: return nativeui::kFxToggle;
            default: return nativeui::fxDomainKind (pi, styleModel_);
        }
    }
    /** DISPLAY LEFT, CONTROLS RIGHT (the table's `side` flag): only when the rows of the panel fit the height at the right. */
    bool sideMode() const noexcept { return lay_->side && lay_->displayH > 0 && display.kind != ui::FxDisplay::Kind::None && sideFits(); }
    int sideHeight() const noexcept
    {
        int h = 0;
        for (int r = 0; r < rowsOnPage(); ++r)
            h += kQLane + nativeui::kFxSideLine * sideLines (firstRowOfPage() + r);
        return h;
    }
    bool sideFits() const noexcept
    {
        const int top = kEditorY + kMargin + kHeaderH + kMargin + (numPages() > 1 ? 64 : 0);
        return rowsOnPage() > 0 && sideHeight() <= editorRect().bottom() - kMargin - top;
    }
    /** The lines of a row of a side page: two cells to a line, by the cells of its table row (stable while controls hide). */
    int sideLines (int visRow) const noexcept
    {
        if (visRow < 0 || visRow >= numVis_)
            return 1;
        int n = 0;
        for (const ui::fxl::Cell& c : lay_->row[vis_[visRow]].cell)
            n += c.alt[0].key != nullptr ? 1 : 0;
        return std::max (1, (n + 1) / 2);
    }
    ui::Rect cellRect (int row, int col) const noexcept
    {
        if (sideMode())
        {
            const int first = firstRowOfPage();
            int y = knobArea_.y;
            for (int r = 0; r < row; ++r)
                y += kQLane + nativeui::kFxSideLine * sideLines (first + r);
            return { knobArea_.x + (col % 2) * nativeui::kFxSideStride, y + kQLane + (col / 2) * nativeui::kFxSideLine, nativeui::kFxSideCellW, nativeui::kFxSideLine };
        }
        const int gapX = 8, cellW = (knobArea_.w - 3 * gapX) / 4;
        const int rowH = knobArea_.h / std::max (1, rowsPerPage());
        const int x = knobArea_.x + col * (cellW + gapX);
        return { x, knobArea_.y + row * rowH + kQLane, col == 3 ? knobArea_.right() - x : cellW, nativeui::fxStackedCellH (rowH) };
    }

    // ---- the effect on the page ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** How a parameter is shown by its values alone. */
    enum class Kind : uint8_t { Dial, Choice, List, Toggle, Number };

    int currentType() const noexcept { return kind_; }
    int numParams() const noexcept { return numParams_; }
    int numRows() const noexcept { return numVis_; }
    int rowsPerPage() const noexcept
    {
        int rows = 0;
        const auto panels = ui::fxl::panelsOf (kind_);
        for (int r = 0; r < lay_->numRows; ++r)
            rows += panels.rows[r] == page_;
        return std::clamp (rows, 1, ui::fxl::kMaxPageRows);
    }
    int numPages() const noexcept { return ui::fxl::panelsOf (kind_).count; }
    int pageIndex() const noexcept { return page_; }
    bool isMoreView() const noexcept { return page_ != 0; }
    int firstRowOfPage() const noexcept { return panelStart_[page_]; }
    int rowsOnPage() const noexcept { return panelStart_[page_ + 1] - panelStart_[page_]; }
    bool hasDisplay() const noexcept { return hasBands() || (display.kind != ui::FxDisplay::Kind::None && (sideMode() || displayShown())); }
    bool hasBands() const noexcept { return kind_ >= ui::fxl::kKindSplitterLh; }
    void setPath (const char* path) { ui::setText (path_, sizeof path_, path != nullptr ? path : ""); }
    const char* path() const noexcept { return path_; }
    const char* title() const noexcept { return name_; }
    bool removable() const noexcept { return removable_; }

    const synthyboy::ParamInfo* infoOf (int p) const noexcept { return p >= 0 && p < kMaxParams ? ctl_[p].info : nullptr; }
    int controlOfKey (const char* key) const noexcept
    {
        for (int p = 0; p < kMaxParams; ++p)
            if (ctl_[p].info != nullptr && std::strcmp (ctl_[p].info->key, key) == 0)
                return p;
        return -1;
    }
    void nameOf (int p, char* out, size_t cap) const
    {
        if (infoOf (p) == nullptr)
        {
            std::snprintf (out, cap, "%s", "");
            return;
        }
        ui::fxl::controlName (*ctl_[p].alt, *ctl_[p].info, lay_->name, out, cap);
    }
    bool isLive (int p) const noexcept { return p >= 0 && p < kMaxParams && ctl_[p].info != nullptr && ctl_[p].live; }
    int controlInCell (int row, int col) const noexcept { return row >= 0 && row < numVis_ && col >= 0 && col < 4 ? shown_[vis_[row]][col] : -1; }
    /** The live control in a cell: the members of the Q group of the row. */
    int memberControl (int row, int col) const noexcept { const int p = controlInCell (row, col); return p >= 0 && p < kMaxParams && ctl_[p].live ? p : -1; }
    bool nativeBandShown() const noexcept { return moduleEnabled_ && hasBandSel_ && bandsMode_ != 3 && ! isMoreView(); }
    ui::Rect nativeBandRect() const noexcept
    {
        const int row = nativeBandShown() ? rowOfControl (kSel) : -1;
        return row >= 0 ? cellRect (row, 3) : ui::Rect {};
    }
    /** The disabled bands of the selector as the native header publishes them: 4 (high) in Low band mode, 1 (low) in High band mode, else 0. */
    int bandDisabledMask() const noexcept { return (bandsMode_ == 2 ? 1 : 0) | (bandsMode_ == 1 ? 4 : 0); }
    int effectiveBand() const noexcept { return effBand_; }
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
    /** The kind a parameter is shown as by its values alone: a knob, a number (whole numbers), an on / off toggle, a list of 3 .. 8 values (or two named ones), a dropdown for more than 8. */
    Kind baseKind (int p) const noexcept
    {
        const synthyboy::ParamInfo* pi = infoOf (p);
        if (pi == nullptr)
            return Kind::Dial;
        if (pi->numChoices <= 0 || pi->choices == nullptr)
            return pi->interval == 1.0f && pi->max - pi->min <= 100.0f ? Kind::Number : Kind::Dial;
        if (pi->numChoices > kMaxListOptions)
            return Kind::Choice;
        if (pi->numChoices == 2 && std::strcmp (pi->choices[0], "Off") == 0 && std::strcmp (pi->choices[1], "On") == 0)
            return Kind::Toggle;
        return Kind::List;
    }

    /** Disables the editing of the effect's parameters while POWER and the panel navigation stay (the module is off). */
    void setModuleEnabled (bool on)
    {
        const bool changed = on != moduleEnabled_;
        moduleEnabled_ = on;
        if (changed)
            layout();
    }

    /** The effect on the page: `kind` is a type of the pool (1 .. 13) or a splitter (ui::fxl::kKindSplitter*), `removable` a slot of the pool. */
    void setType (int kind, bool removable)
    {
        kind = std::clamp (kind, 0, ui::fxl::kNumKinds - 1);
        const bool changed = kind != kind_ || ! resolved_;
        if (! changed && removable == removable_)
            return;
        removable_ = removable;
        if (! changed)
            return;
        kind_ = kind;
        resolved_ = true;
        lay_ = &ui::fxl::layoutOf (kind);
        display.setKindFromType (kind);
        ui::setText (name_, sizeof name_, lay_->name);
        resolve();
        page_ = 0;
        activeTable_ = -1;
        reflow();
        setActiveRow (0);
    }

    /** The state of the effect: `valueOf (key)` is the plain value of the engine key of the open effect (the keys the conditions of the table name). Returns true when something changed on the page. */
    template <class ValueOf>
    bool setState (ValueOf&& valueOf)
    {
        styleModel_ = kind_ == synthyboy::kFxTypeFilter ? static_cast<int> (std::lround (valueOf ("fxfilter.model"))) : 0;
        for (int p = 0; p <= kSel; ++p)   // the values the conditions name, kept for the next change of the BAND selector
        {
            const ui::fxl::Live& lv = ctl_[p].alt != nullptr ? ctl_[p].alt->live : ui::fxl::Live {};
            snap_[p][0] = lv.key != nullptr && ! ui::fxl::isView (lv.key) ? static_cast<float> (valueOf (lv.key)) : 0.0f;
            snap_[p][1] = lv.key2 != nullptr && ! ui::fxl::isView (lv.key2) ? static_cast<float> (valueOf (lv.key2)) : 0.0f;
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
    }
    void setPage (int p)
    {
        page_ = std::clamp (p, 0, numPages() - 1);
        applyPage();
        setActiveRow (firstRowOfPage());
    }

private:
    struct Ctl
    {
        const synthyboy::ParamInfo* info = nullptr;
        const ui::fxl::Alt* alt = nullptr;
        int row = -1, col = -1, slot = -1;
        bool live = true;
        Kind kind = Kind::Dial;
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
                for (int a = 0; a < ui::fxl::kAlts; ++a)
                {
                    const ui::fxl::Alt& alt = lay_->row[r].cell[c].alt[a];
                    if (alt.key == nullptr)
                        continue;
                    if (ui::fxl::isView (alt.key))   // the view-local selector: a cell, not an engine parameter
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
            const ui::fxl::Live& lv = ctl_[p].alt->live;
            auto value = [&] (const char* key, int which) { return ui::fxl::isView (key) ? static_cast<float> (effBand_) : snap_[p][which]; };
            const bool live = lv.always() || (ui::fxl::Live::in (lv.modes, value (lv.key, 0)) && (lv.key2 == nullptr || ui::fxl::Live::in (lv.modes2, value (lv.key2, 1))));
            changed = changed || live != ctl_[p].live;
            ctl_[p].live = live;
        }
        if (changed)
            reflow();
        return changed;
    }

    /** The rows that have something live, the control every cell shows (the live one: a control that is not live is not shown; the live ones of a row stand together from the left); then the layout follows. */
    void reflow()
    {
        numVis_ = 0;
        for (auto& r : shown_)
            for (int& c : r)
                c = -1;
        const auto panels = ui::fxl::panelsOf (kind_);
        for (int panel = 0; panel < panels.count; ++panel)
        {
            panelStart_[panel] = numVis_;
            for (int r = 0; r < lay_->numRows; ++r)
            {
                if (panels.rows[r] != panel)
                    continue;
                int n = 0;
                for (int c = 0; c < 4; ++c)
                    for (int a = 0; a < ui::fxl::kAlts; ++a)
                    {
                        const int p = cellCtl_[r][c][a];
                        if (p >= 0 && ctl_[p].live)
                        {
                            shown_[r][n] = p;
                            ++n;
                            break;
                        }
                    }
                if (n == 0)
                    continue;   // nothing live: the row is gone
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

    void applyPage()
    {
        page_ = std::clamp (page_, 0, numPages() - 1);
        const int act = activeRow();
        if (numVis_ > 0 && (act < firstRowOfPage() || act >= firstRowOfPage() + rowsOnPage()))
            activeTable_ = vis_[std::min (firstRowOfPage(), numVis_ - 1)];
        layout();
    }

    /** Keep the live display when two control rows fit below it. */
    bool displayShown() const noexcept { return rowsPerPage() <= 2 || numPages() == 1; }

    /** The rectangles of the display and of the cells (the layout of the deleted editor, without any component). */
    void layout()
    {
        const auto ed = editorRect();
        int y = ed.y + kMargin + kHeaderH + kMargin;
        if (numPages() > 1)
            y += 64;
        displayRect_ = {};
        if (sideMode())
        {
            const int bottom = ed.bottom() - kMargin;
            displayRect_ = { ed.x + kMargin, y, nativeui::kFxSideX - 2 * kMargin, std::max (0, bottom - y) };
            knobArea_ = { ed.x + nativeui::kFxSideX, y, ed.right() - kMargin - (ed.x + nativeui::kFxSideX), std::max (0, bottom - y) };
            return;
        }
        const int displayH = lay_->displayH > 0 ? std::max (0, lay_->displayH - (kChainBarH - 88)) : 0;
        if (displayH > 0 && display.kind != ui::FxDisplay::Kind::None && displayShown())
        {
            displayRect_ = { ed.x + kMargin, y, ed.w - 2 * kMargin, displayH };
            y += displayH + kMargin;
        }
        else if (displayH > 0 && hasBands())
        {
            displayRect_ = { ed.x + kMargin, y, ed.w - 2 * kMargin, displayH };
            y += displayH + kMargin;
        }
        knobArea_ = { ed.x + kMargin, y, ed.w - 2 * kMargin, std::max (0, ed.bottom() - kMargin - y) };
        display.place (displayRect_);
    }

    int w_ = 1280;
    int kind_ = -1;
    const ui::fxl::Layout* lay_ = &ui::fxl::layoutOf (0);
    bool resolved_ = false, flowed_ = false;
    int numParams_ = 0;
    Ctl ctl_[kMaxParams + 1];   // the parameters' controls, then the selector (kSel)
    int cellCtl_[kMaxRows][4][ui::fxl::kAlts] = {};
    float snap_[kMaxParams + 1][2] = {};   // the values the conditions of the controls name, as of the last `setState`
    bool hasBandSel_ = false;
    int bandSel_ = 1, effBand_ = 1, bandsMode_ = 0;
    int shown_[kMaxRows][4] = {};
    int vis_[kMaxRows] = {};
    int numVis_ = 0;
    int panelStart_[4] = {};
    int activeTable_ = -1, page_ = 0;
    bool removable_ = false, moduleEnabled_ = true;
    char name_[24] = {}, path_[72] = {};
    ui::Rect knobArea_, displayRect_;
    int styleModel_ = 0;
};

}} // namespace synthyboy_mpc::overlay
