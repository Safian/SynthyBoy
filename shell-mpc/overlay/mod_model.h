// SynthyBoy - MPC overlay: the PLAIN DATA of the Mod tab (owner decision 2026-11-27: the C++ layer has no UI elements). The Mod tab is made of native host cells; this header holds what the plugin still has to know about them:
// the slots of the matrix with their filter, scroll window and selected row, the rectangles of the native cells of a row / of the page of one slot (the targets page of a source is ModRowsPage, mod_rows_page.h), and the Q-Link value rules of those cells.
// No drawing, no touch widgets, no virtuals: every type is a struct of numbers and rectangles, every function a pure function of them. The gestures that are no control (a tap on a row selects it, a drag on the list scrolls it)
// are the three small functions of `Matrix` (scrollDown / scrollMove / scrollUp).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../ui/binding.h"
#include "../ui/core.h"
#include "../ui/model.h"

namespace synthyboy_mpc { namespace overlay { namespace modmodel {

constexpr int kRowH = 72, kCtlH = 56, kGap = 4;
constexpr int kQLane = ui::kQLane;

/** Splits `r` into a row of n rectangles: an entry with fixed[i] > 0 has exactly that width, the others share the rest in proportion to weight[i]; `gap` pixels between them. */
inline void layoutRow (const ui::Rect& r, const int* fixed, const int* weight, int n, int gap, ui::Rect* out) noexcept
{
    int fixedSum = 0, weightSum = 0;
    for (int i = 0; i < n; ++i)
    {
        fixedSum += fixed[i] > 0 ? fixed[i] : 0;
        weightSum += fixed[i] > 0 ? 0 : weight[i];
    }
    const int free = std::max (0, r.w - fixedSum - gap * (n - 1));
    int x = r.x, usedWeight = 0;
    for (int i = 0; i < n; ++i)
    {
        int w;
        if (fixed[i] > 0)
            w = fixed[i];
        else
        {
            const int before = weightSum > 0 ? free * usedWeight / weightSum : 0;
            usedWeight += weight[i];
            w = (weightSum > 0 ? free * usedWeight / weightSum : 0) - before;
        }
        out[i] = { x, r.y, std::max (0, std::min (w, r.right() - x)), r.h };
        x += w + gap;
    }
}

// =============================================================================================================================================================
/** The cells of one row of the matrix (and of the summary row of the page of a slot): number, SOURCE, TARGET, AMOUNT, polarity, on / off, MORE. `r` is the row (kRowH high, the cells 56 px under the lane of the Q chips). */
struct RowCells
{
    ui::Rect number, source, dest, amount, polarity, bypass, detail;
    std::array<ui::Rect, 3> navigation() const noexcept { return { source, dest, detail }; }
    std::array<ui::Rect, 3> values() const noexcept { return { amount, polarity, bypass }; }
};
inline RowCells rowCells (const ui::Rect& r) noexcept
{
    RowCells c;
    const int y = r.y + kQLane, h = kCtlH, gap = 6;
    int x = r.x + 4;
    c.number = { x, y, 56, h };
    x += 56 + gap;
    c.source = { x, y, 176, h };
    x += 176 + 26;
    c.dest = { x, y, 244, h };
    x += 244 + gap;
    const int rightW = 72 + gap + 72 + gap + 76;
    const int barW = std::max (160, r.right() - 4 - rightW - gap - x);
    c.amount = { x, y, barW, h };
    x += barW + gap;
    c.polarity = { x, y, 72, h };
    x += 72 + gap;
    c.bypass = { x, y, 72, h };
    x += 72 + gap;
    c.detail = { x, y, std::max (56, r.right() - 4 - x), h };
    return c;
}

/** The label the polarity / state cells carry for a value (the native cell shows it: the plugin only names it). */
inline const char* polarityLabel (bool bipolar) noexcept { return bipolar ? "BI" : "UNI"; }
inline const char* stateLabel (bool bypassed) noexcept { return bypassed ? "OFF" : "ON"; }

// =============================================================================================================================================================
/** The matrix of the 64 slots: the filter, the visible list, the scroll window and the selected row. */
class Matrix
{
public:
    static constexpr int kMaxSlots = 64, kWindowRows = 8;
    static constexpr int kBarH = 64, kHeaderH = 22;
    static constexpr int kZoneW = 56, kThumbW = 24;
    enum FilterMode : uint8_t { FilterAll = 0, FilterSource, FilterDest };

    struct SlotEntry
    {
        char src[32] = {};
        char dst[40] = {};
        double amount = 0.0;
        bool bi = false, bypass = false;
        int srcGroup = 0;      // the group of the source (0: none)
        int destModule = -1;
        bool hasLive = false;
        double liveVal = 0.0;
        bool active = false;
    };

    void place (const ui::Rect& r) { rect_ = r; syncWindow(); }
    ui::Rect rect() const noexcept { return rect_; }

    // ---- header cells (mode tabs, the filter choice, ADD) ----------------------------------------------------------------------------------------------------
    ui::Rect tabAll() const noexcept { return { rect_.x + 280, rect_.y + 4, 80, 56 }; }
    ui::Rect tabSource() const noexcept { return { rect_.x + 368, rect_.y + 4, 140, 56 }; }
    ui::Rect tabDest() const noexcept { return { rect_.x + 516, rect_.y + 4, 120, 56 }; }
    ui::Rect filterCell() const noexcept { return { rect_.x + 648, rect_.y + 4, 260, 56 }; }
    ui::Rect addCell() const noexcept { return { rect_.right() - 40 - 120, rect_.y + 4, 120, 56 }; }
    /** All three mode cells, ADD, and the current mode's dependent choice. */
    std::array<ui::Rect, 6> headerNavigation() const noexcept
    {
        return { tabAll(), tabSource(), tabDest(), addCell(),
                 filterMode_ == FilterSource ? filterCell() : ui::Rect {}, filterMode_ == FilterDest ? filterCell() : ui::Rect {} };
    }

    // ---- rows ---------------------------------------------------------------------------------------------------------------------------------------------
    ui::Rect listRect() const noexcept { return { rect_.x, rect_.y + kBarH + kHeaderH, rect_.w, rect_.h - kBarH - kHeaderH - 4 }; }
    int visibleRows() const noexcept { return std::min (kWindowRows, std::max (1, (listRect().h - 4) / kRowH)); }
    int maxScroll() const noexcept { return std::max (0, visibleSlotCount_ - visibleRows()); }
    bool scrolls() const noexcept { return visibleSlotCount_ > visibleRows(); }
    ui::Rect rowRect (int wIdx) const noexcept
    {
        return { rect_.x, listRect().y + wIdx * kRowH, rect_.w - (scrolls() ? kZoneW : 12), kRowH };
    }
    RowCells cells (int wIdx) const noexcept { return rowCells (rowRect (wIdx)); }

    std::array<ui::Rect, 6 + kWindowRows * 3> nativeNavigationApertures() const noexcept
    {
        std::array<ui::Rect, 6 + kWindowRows * 3> areas {};
        const auto header = headerNavigation();
        std::copy (header.begin(), header.end(), areas.begin());
        for (int i = 0; i < visibleRows(); ++i)
            if (slotAtWindowIndex (i) >= 0)
            {
                const auto row = cells (i).navigation();
                std::copy (row.begin(), row.end(), areas.begin() + header.size() + i * row.size());
            }
        return areas;
    }
    std::array<ui::Rect, kWindowRows * 3> nativeApertures() const noexcept
    {
        std::array<ui::Rect, kWindowRows * 3> areas {};
        for (int i = 0; i < visibleRows(); ++i)
            if (slotAtWindowIndex (i) >= 0)
            {
                const auto row = cells (i).values();
                std::copy (row.begin(), row.end(), areas.begin() + i * row.size());
            }
        return areas;
    }
    bool isNativeNavigation (int x, int y) const noexcept
    {
        for (const ui::Rect& area : headerNavigation())
            if (area.contains (x, y))
                return true;
        for (int i = 0; i < visibleRows(); ++i)
            if (slotAtWindowIndex (i) >= 0)
                for (const ui::Rect& area : cells (i).navigation())
                    if (area.contains (x, y))
                        return true;
        return false;
    }
    /** The window row whose native value cell (amount, polarity, state) lies at (x, y); -1: none (and while the board of the sources is the page). */
    int nativeWindowRowAt (int x, int y) const noexcept
    {
        if (boardShown())
            return -1;
        for (int i = 0; i < visibleRows(); ++i)
            if (slotAtWindowIndex (i) >= 0)
                for (const ui::Rect& r : cells (i).values())
                    if (r.contains (x, y))
                        return i;
        return -1;
    }
    bool isNativeControl (int x, int y) const noexcept { return nativeWindowRowAt (x, y) >= 0; }

    // ---- the filter, the list -----------------------------------------------------------------------------------------------------------------------------
    void setFilterMode (FilterMode m)
    {
        filterMode_ = m;
        rebuildVisibleList();
    }
    FilterMode filterMode() const noexcept { return filterMode_; }
    void setSourceFilterChoice (int choice) { sourceFilterId_ = choice; rebuildVisibleList(); }
    void setDestModuleFilter (int modId) { destModuleFilterId_ = modId; rebuildVisibleList(); }

    /** How many sources the quick-add board offers (the engine's table; the owner sets it once). */
    void setBoardSources (int n) noexcept { boardSources_ = n; }
    /** True while the board of the sources is what the page shows: no slot is in use at all (with one in use the list is the page, whatever the filter says). */
    bool boardShown() const noexcept { return totalActive_ == 0 && boardSources_ > 0; }

    void rebuildVisibleList()
    {
        visibleSlotCount_ = 0;
        totalActive_ = 0;
        for (int i = 0; i < kMaxSlots; ++i)
        {
            totalActive_ += slots_[i].active ? 1 : 0;
            bool match = false;
            if (filterMode_ == FilterAll)
                match = slots_[i].active;
            else if (filterMode_ == FilterSource)
                match = slots_[i].active && (sourceFilterId_ <= 0 || slots_[i].srcGroup == sourceFilterId_);
            else if (filterMode_ == FilterDest)
                match = slots_[i].active && (destModuleFilterId_ < 0 || slots_[i].destModule == destModuleFilterId_);
            if (match)
                visibleSlots_[visibleSlotCount_++] = i;
        }
        if (selectedRow_ >= visibleSlotCount_)
            selectedRow_ = std::max (0, visibleSlotCount_ - 1);
        firstRow_ = std::clamp (firstRow_, 0, maxScroll());
        syncWindow();
    }

    void selectRow (int idx)
    {
        if (visibleSlotCount_ <= 0)
        {
            selectedRow_ = -1;
            return;
        }
        selectedRow_ = std::clamp (idx, 0, visibleSlotCount_ - 1);
        const int vRows = visibleRows();
        if (selectedRow_ < firstRow_)
            firstRow_ = selectedRow_;
        else if (selectedRow_ >= firstRow_ + vRows)
            firstRow_ = selectedRow_ - vRows + 1;
        syncWindow();
    }
    int selectedSlot() const noexcept { return selectedRow_ >= 0 && selectedRow_ < visibleSlotCount_ ? visibleSlots_[selectedRow_] : -1; }
    int selectedRow() const noexcept { return selectedRow_; }
    int firstRow() const noexcept { return firstRow_; }
    void setFirstRow (int r) noexcept { firstRow_ = std::clamp (r, 0, maxScroll()); syncWindow(); }
    void scrollByRows (int rows) noexcept { setFirstRow (firstRow_ + rows); }
    int activeSlots() const noexcept { return totalActive_; }

    /** The data of slot `slot` (the owner calls it for all 64; an empty name is a slot that is not used): amount -1 .. +1, `srcGroup` the group of the source and `destModule` the module of the target (the filters). */
    void setSlotData (int slot, const char* src, const char* dst, double amount, bool bi, bool bypassed, int srcGroup, int destModule, bool hasLive = false, double liveVal = 0.0)
    {
        if (slot < 0 || slot >= kMaxSlots)
            return;
        SlotEntry& e = slots_[slot];
        ui::setText (e.src, sizeof e.src, src);
        ui::setText (e.dst, sizeof e.dst, dst);
        e.amount = amount;
        e.bi = bi;
        e.bypass = bypassed;
        e.srcGroup = srcGroup;
        e.destModule = destModule;
        e.hasLive = hasLive;
        e.liveVal = liveVal;
        e.active = src[0] != '\0' && dst[0] != '\0';
    }
    /** The owner is done with a round of setSlotData: the list is rebuilt only when what it lists changed. */
    void refresh()
    {
        int sig = 0;
        for (int i = 0; i < kMaxSlots; ++i)
            sig = static_cast<int> (static_cast<unsigned> (sig) * 31u + (slots_[i].active ? static_cast<unsigned> (1 + slots_[i].srcGroup * 3 + (slots_[i].destModule + 2) * 7) : 0u));
        if (sig != listSig_)
        {
            listSig_ = sig;
            rebuildVisibleList();
        }
        else
            syncWindow();
    }
    const SlotEntry& slotData (int slot) const noexcept { return slots_[std::clamp (slot, 0, kMaxSlots - 1)]; }
    int visibleSlotCount() const noexcept { return visibleSlotCount_; }
    int slotOfVisibleRow (int row) const noexcept { return row >= 0 && row < visibleSlotCount_ ? visibleSlots_[row] : -1; }
    int slotAtWindowIndex (int wIdx) const noexcept
    {
        const int vi = firstRow_ + wIdx;
        return wIdx >= 0 && wIdx < visibleRows() && vi < visibleSlotCount_ ? visibleSlots_[vi] : -1;
    }
    /** The window row of the selected row (-1: not shown). */
    int selectedWindowRow() const noexcept
    {
        const int w = selectedRow_ - firstRow_;
        return selectedRow_ >= 0 && selectedRow_ < visibleSlotCount_ && w >= 0 && w < visibleRows() ? w : -1;
    }
    /** The amount of the window row as the normalised value of its AMOUNT cell (0 .. 1). */
    double amountNorm (int wIdx) const noexcept
    {
        const int s = slotAtWindowIndex (wIdx);
        return s >= 0 ? (std::clamp (slots_[s].amount, -1.0, 1.0) + 1.0) * 0.5 : 0.5;
    }

    // ---- the scroll zone and the gestures that are no control (row tap, list drag, thumb) ---------------------------------------------------------------------------
    ui::Rect zoneRect() const noexcept { const ui::Rect lr = listRect(); return { rect_.right() - kZoneW, lr.y + 4, kZoneW, lr.h - 8 }; }
    ui::Rect thumbRect() const noexcept
    {
        const ui::Rect z = zoneRect();
        const int trackH = z.h;
        const int n = std::max (1, visibleSlotCount_);
        const int thumbH = std::max (28, trackH * visibleRows() / n);
        const int thumbY = (trackH - thumbH) * firstRow_ / std::max (1, n - visibleRows());
        return { z.x + kZoneW / 2 - kThumbW / 2, z.y + thumbY, kThumbW, thumbH };
    }
    /** A finger down on the list or the scroll zone: selects the row, starts a drag. true: the touch is the matrix's (not a native cell). */
    bool scrollDown (int x, int y)
    {
        if (boardShown())
            return false;
        const ui::Rect lr = listRect();
        if (y >= lr.y && y < lr.bottom() && x < rect_.right() - (scrolls() ? kZoneW : 0))
        {
            const int clicked = firstRow_ + (y - lr.y) / kRowH;
            if (clicked >= 0 && clicked < visibleSlotCount_)
                selectRow (clicked);
            dragStartY_ = y;
            startFirstRow_ = firstRow_;
            dragList_ = true;
            return true;
        }
        if (scrolls() && x >= rect_.right() - kZoneW && y >= lr.y)
        {
            const ui::Rect th = thumbRect();
            if (th.contains (x, y))
            {
                dragThumb_ = true;
                dragStartY_ = y;
                startFirstRow_ = firstRow_;
            }
            else
                setFirstRow (y < th.y ? firstRow_ - visibleRows() : firstRow_ + visibleRows());
            return true;
        }
        return false;
    }
    void scrollMove (int, int y)
    {
        if (dragList_ && std::abs (y - dragStartY_) > ui::TapGesture::kSlop)
            setFirstRow (std::clamp (startFirstRow_ + -(y - dragStartY_) / kRowH, 0, maxScroll()));
        else if (dragThumb_)
        {
            const int trackH = listRect().h - 8;
            const int n = std::max (1, visibleSlotCount_ - visibleRows());
            setFirstRow (std::clamp (startFirstRow_ + (y - dragStartY_) * n / std::max (1, trackH), 0, maxScroll()));
        }
    }
    void scrollUp() noexcept { dragList_ = dragThumb_ = false; }
    /** The mouse wheel over the list: one row a notch (the wheel up shows the earlier rows). true: taken. */
    bool wheel (int x, int y, int notches)
    {
        if (! scrolls() || ! listRect().contains (x, y))
            return false;
        setFirstRow (firstRow_ - notches);
        return true;
    }

    /** One Q-Link detent of the AMOUNT of the selected row / one step of the row number: the change the page writes. `baseId` is the id of the window row's first cell. */
    ui::Change turnRow (double delta) const noexcept
    {
        if (visibleSlotCount_ <= 0 || selectedRow_ < 0)
            return ui::Change::none();
        const int step = delta > 0.0 ? 1 : (delta < 0.0 ? -1 : 0);
        const int next = std::clamp (selectedRow_ + step, 0, visibleSlotCount_ - 1);
        if (step == 0 || next == selectedRow_)
            return ui::Change::none();
        return ui::Change::indexed (0, next, static_cast<double> (next));
    }

private:
    ui::Rect rect_ {};
    FilterMode filterMode_ = FilterAll;
    int sourceFilterId_ = 0, destModuleFilterId_ = -1;
    SlotEntry slots_[kMaxSlots];
    int visibleSlots_[kMaxSlots + 1] = {};
    int visibleSlotCount_ = 0, totalActive_ = 0, listSig_ = -1;
    int firstRow_ = 0, selectedRow_ = 0, boardSources_ = 0;
    bool dragList_ = false, dragThumb_ = false;
    int dragStartY_ = 0, startFirstRow_ = 0;

    void syncWindow() noexcept
    {
        firstRow_ = std::clamp (firstRow_, 0, maxScroll());
    }
};

// =============================================================================================================================================================
/** The page of one slot: the title, the three blocks (AUXILIARY SOURCE, SHAPE, TIME) with their cells, the delete confirmation and the values of the Q members. */
class Detail
{
public:
    static constexpr int kBarH = 64, kBlockTitleH = 34, kBlockPad = 6, kBlockGap = 8, kLedRowH = 56;
    enum Block : uint8_t { BlockAux = 0, BlockShape, BlockTime };

    /** The normalised values of the knob cells (0 .. 1) as the owner read them from the instance. */
    struct Values
    {
        double auxOutput = 0.5, auxPower = 0.5, power = 0.5, rise = 0, fall = 0, delay = 0, delayBeats = 0;
        bool auxInv = false, stereo = false;
    };
    Values values;

    void place (const ui::Rect& r)
    {
        rect_ = r;
        const int m = 8;
        back_ = { r.x + 66, r.y + 4, 100, 56 };
        delete_ = { r.right() - 40 - 120, r.y + 4, 120, 56 };
        summary_ = rowCells ({ r.x, r.y + kBarH, r.w, kRowH });
        const int y0 = r.y + kBarH + kRowH + 6, h = r.bottom() - m - y0;
        const int avail = r.w - 2 * m - 2 * kBlockGap;
        const int wAux = avail * 36 / 100, wShape = avail * 26 / 100, wTime = avail - wAux - wShape;
        blocks_[BlockAux] = { r.x + m, y0, wAux, h };
        blocks_[BlockShape] = { blocks_[BlockAux].right() + kBlockGap, y0, wShape, h };
        blocks_[BlockTime] = { blocks_[BlockShape].right() + kBlockGap, y0, wTime, h };
        auto inner = [&] (const ui::Rect& b) { return ui::Rect { b.x + kBlockPad, b.y + kBlockTitleH, std::max (0, b.w - 2 * kBlockPad), std::max (0, b.h - kBlockTitleH - kBlockPad) }; };
        // a 2 x 2 grid: the cells of the second row start a lane lower (the Q marks of the members lie above them)
        auto grid = [&] (const ui::Rect& b, ui::Rect* out, bool auxSourceFirst)
        {
            const ui::Rect in = inner (b);
            const int cw = (in.w - kGap) / 2, rh = (in.h - kQLane - kGap) / 2;
            for (int i = 0; i < 4; ++i)
            {
                const int col = i % 2, row = i / 2;
                ui::Rect cell { in.x + col * (cw + kGap), in.y + row * (rh + kGap + kQLane), col == 1 ? in.right() - (in.x + cw + kGap) : cw, rh };
                if (auxSourceFirst && i == 0)
                    cell = { cell.x, cell.y + (cell.h - std::min (cell.h, 110)) / 2, cell.w, std::min (cell.h, 110) };
                out[i] = cell;
            }
        };
        ui::Rect a[4], t[4];
        grid (blocks_[BlockAux], a, true);
        auxSource_ = a[0]; auxOutput_ = a[1]; auxInv_ = a[2]; auxPower_ = a[3];
        {
            const ui::Rect in = inner (blocks_[BlockShape]);
            const int rh = in.h - kQLane - kGap - kLedRowH;   // STEREO is one row; the room it leaves goes to the CURVE cell
            power_ = { in.x, in.y, in.w, rh };
            stereo_ = { in.x, in.y + rh + kGap + kQLane, in.w, kLedRowH };
        }
        grid (blocks_[BlockTime], t, false);
        rise_ = t[0]; fall_ = t[1]; delay_ = t[2]; delaySync_ = t[3];
    }

    ui::Rect rect() const noexcept { return rect_; }
    ui::Rect block (int b) const noexcept { return b >= 0 && b < 3 ? blocks_[b] : ui::Rect {}; }
    const RowCells& summary() const noexcept { return summary_; }
    /** The twelve native value cells (the slot's row, then the cells of the blocks), the DELAY cell being the BEATS cell while SYNC is on. */
    std::array<ui::Rect, 12> nativeApertures() const noexcept
    {
        const auto r = summary_.values();
        return { r[0], r[1], r[2], auxOutput_, auxInv_, auxPower_, power_, stereo_, rise_, fall_, delay_, delaySync_ };
    }
    /** BACK, DELETE, SOURCE, TARGET, AUX SOURCE. */
    std::array<ui::Rect, 5> nativeNavigationApertures() const noexcept { return { back_, delete_, summary_.source, summary_.dest, auxSource_ }; }
    bool isNativeControl (int x, int y) const noexcept
    {
        for (const ui::Rect& r : nativeApertures())
            if (r.contains (x, y))
                return true;
        return false;
    }

    // ---- the delete confirmation ------------------------------------------------------------------------------------------------------------------------
    void confirmDelete() noexcept { confirmDelete_ = true; }
    void cancelDeleteConfirmation() noexcept { confirmDelete_ = false; }
    bool isDeleteConfirmed() const noexcept { return confirmDelete_; }
    /** The slot the page shows: a new slot cancels a pending confirmation. */
    void setSlot (int idx) noexcept
    {
        if (slotIndex_ != idx)
        {
            slotIndex_ = idx;
            confirmDelete_ = false;
        }
    }
    int slot() const noexcept { return slotIndex_; }

    /** The DELAY is in milliseconds or in beats (the SYNC switch): the member of the TIME group follows it. */
    void setDelaySync (bool sync) noexcept { delaySyncOn_ = sync; }
    bool delaySyncOn() const noexcept { return delaySyncOn_; }

    void setActiveBlock (Block b) noexcept { activeBlock_ = b; }
    Block activeBlock() const noexcept { return activeBlock_; }
    static const char* blockName (int b) noexcept { static const char* const n[3] = { "AUXILIARY SOURCE", "SHAPE", "TIME" }; return b >= 0 && b < 3 ? n[b] : ""; }

    /** The change of the Q-Link `knobIndex` of the active block: the page writes it. `baseId` of the page ids is given by the owner (ids are the owner's, see ModUi::Id). */
    struct Ids { int auxOutput, auxInv, auxPower, power, stereo, rise, fall, delay, delayBeats, delaySync; };
    ui::Change turnQ (int knobIndex, double delta, const Ids& id) const noexcept
    {
        auto knob = [&] (int cid, double v) {
            const double n = std::clamp (v + delta, 0.0, 1.0);
            return std::fabs (n - std::clamp (v, 0.0, 1.0)) < 1.0e-12 ? ui::Change::none() : ui::Change::normalised (cid, n);
        };
        auto toggle = [&] (int cid, bool on) {
            if (delta == 0.0) return ui::Change::none();
            const bool next = delta > 0.0;
            return next == on ? ui::Change::none() : ui::Change::indexed (cid, next ? 1 : 0, next ? 1.0 : 0.0);
        };
        switch (activeBlock_)
        {
            case BlockAux:
                return knobIndex == 1 ? knob (id.auxOutput, values.auxOutput) : knobIndex == 2 ? toggle (id.auxInv, values.auxInv) : knobIndex == 3 ? knob (id.auxPower, values.auxPower) : ui::Change::none();
            case BlockShape:
                return knobIndex == 0 ? knob (id.power, values.power) : knobIndex == 1 ? toggle (id.stereo, values.stereo) : ui::Change::none();
            case BlockTime:
                return knobIndex == 0 ? knob (id.rise, values.rise) : knobIndex == 1 ? knob (id.fall, values.fall)
                     : knobIndex == 2 ? (delaySyncOn_ ? knob (id.delayBeats, values.delayBeats) : knob (id.delay, values.delay))
                     : toggle (id.delaySync, delaySyncOn_);
        }
        return ui::Change::none();
    }

private:
    ui::Rect rect_ {}, back_ {}, delete_ {}, blocks_[3] {}, auxSource_ {}, auxOutput_ {}, auxInv_ {}, auxPower_ {}, power_ {}, stereo_ {}, rise_ {}, fall_ {}, delay_ {}, delaySync_ {};
    RowCells summary_ {};
    Block activeBlock_ = BlockAux;
    bool confirmDelete_ = false, delaySyncOn_ = false;
    int slotIndex_ = -1;
};

}}} // namespace synthyboy_mpc::overlay::modmodel
