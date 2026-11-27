// SynthyBoy - MPC overlay: the DATA and the native bridge of the two row pages of the LFO: TARGETS (the modulation slots whose source is this LFO) and MODULATIONS (the sources that move one control). Owner decision
// 2026-11-27: the C++ layer has no UI elements. The back, add, page controls and the depth, polarity, on / off and remove controls of the rows are native cells of the host skin (nativeui::targets); this header holds the rows
// as plain data (name, depth, polarity, bypass), the rectangles the skin lays them out in (so that the overlay leaves them open and knows a touch on one), the texts the plugin-drawn ground of the page carries (the title,
// the names of the rows) and the bridge that publishes the cells and hands the native edits back as commands. Ported from the deleted ui::ModTargetsPage / ui::ModulationsPage and overlay/native_mod_targets_ui.h
// (git show d8cfc25f~1:shell-mpc/ui/mod_targets.h, panel.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../native_mod_targets.h"
#include "../ui/core.h"
#include "../ui/native_apertures.h"
#include "instance_view.h"
#include "ui_bind.h"
#include "synthyboy/modulation.h"

namespace synthyboy_mpc { namespace overlay {

/** The rows of a TARGETS or MODULATIONS page: at most kPerPage rows a page. */
class LfoModRows
{
public:
    static constexpr int kPerPage = nativeui::targets::kRows, kRowH = 72, kCtlH = 56, kBarH = 92, kNameW = 340, kToolbarY = 32, kToolbarAddW = 200, kPageW = 208, kGap = 4;
    static_assert (kPerPage == 6);
    struct Entry
    {
        char name[40] = {};
        double depth = 0.0;           // -1 .. +1 of the range of the target
        bool bidirectional = false, bypassed = false, macro = false;
    };

    void place (const ui::Rect& r) noexcept { rect_ = r; }
    ui::Rect rect() const noexcept { return rect_; }

    /** The visible window: the rows (at most kPerPage), the title (the source or the control), its colour, how many entries there are in all and which page this is (0 ..). */
    void load (const Entry* e, int n, const char* title, uint32_t colour, int total, int pageIndex)
    {
        ui::setText (title_, sizeof title_, title);
        colour_ = colour;
        total_ = total;
        nRows_ = std::clamp (n, 0, kPerPage);
        for (int i = 0; i < nRows_; ++i)
            rows_[i] = e[i];
        pages_ = std::max (1, (total + kPerPage - 1) / kPerPage);
        page_ = std::clamp (pageIndex, 0, pages_ - 1);
        selectRow (nRows_ > 0 ? std::min (std::max (activeRow_, 0), nRows_ - 1) : -1);
    }
    void setTargetValue (const char* s) { ui::setText (target_, sizeof target_, s); }
    int numRows() const noexcept { return nRows_; }
    int activeRow() const noexcept { return activeRow_; }
    int pageIndex() const noexcept { return page_; }
    int pageCount() const noexcept { return pages_; }
    int total() const noexcept { return total_; }
    const Entry& row (int i) const noexcept { return rows_[std::clamp (i, 0, kPerPage - 1)]; }
    void selectRow (int i) noexcept { activeRow_ = i >= 0 && i < nRows_ ? i : -1; }
    void setTitleColour (uint32_t c) noexcept { colour_ = c; }

    // ---- geometry: the cells of the skin -------------------------------------------------------------------------------------------------------------------------------------------------------------------
    int rowY (int i) const noexcept { return rect_.y + kBarH + 8 + i * kRowH; }
    ui::Rect back() const noexcept { return { rect_.x + 4, rect_.y + kToolbarY, 120, kCtlH }; }
    ui::Rect add() const noexcept { return { rect_.right() - 4 - kToolbarAddW, rect_.y + kToolbarY, kToolbarAddW, kCtlH }; }
    ui::Rect page() const noexcept { return { rect_.right() - 4 - kToolbarAddW - kGap - kPageW, rect_.y + kToolbarY, kPageW, kCtlH }; }
    ui::Rect nameRect (int i) const noexcept { return { rect_.x + 8, rowY (i) + kRowH - kCtlH, kNameW - 16, kCtlH }; }
    /** The four cells of a row: the depth (the free width), polarity, on / off and remove. */
    void rowCells (int i, ui::Rect out[4]) const noexcept
    {
        const ui::Rect area { rect_.x + 4 + kNameW, rowY (i) + kRowH - kCtlH, rect_.w - 8 - kNameW, kCtlH };
        const int fixed[3] = { 72, 72, 64 };
        const int freeW = std::max (0, area.w - 208 - 3 * kGap);
        int x = area.x;
        out[0] = { x, area.y, std::max (0, std::min (freeW, area.right() - x)), area.h };
        x += freeW + kGap;
        for (int k = 0; k < 3; ++k)
        {
            out[k + 1] = { x, area.y, std::max (0, std::min (fixed[k], area.right() - x)), area.h };
            x += fixed[k] + kGap;
        }
    }
    /** Is (x, y) on a native cell of the page (`row`: its row, -1 for the toolbar)? */
    bool nativeHit (int x, int y, int* row = nullptr) const noexcept
    {
        if (back().contains (x, y) || add().contains (x, y) || (pages_ > 1 && page().contains (x, y)))
        {
            if (row != nullptr)
                *row = -1;
            return true;
        }
        for (int i = 0; i < nRows_; ++i)
        {
            ui::Rect c[4];
            rowCells (i, c);
            for (const ui::Rect& r : c)
                if (r.contains (x, y))
                {
                    if (row != nullptr)
                        *row = i;
                    return true;
                }
        }
        return false;
    }
    /** The ground of the page: the cut-outs of the native cells and the texts the host does not draw (the title, the names of the rows). */
    void draw (gpu::SceneBuilder& sb, bool modulations) const
    {
        ui::clearNativeCell (sb, back());
        ui::clearNativeCell (sb, add());
        if (pages_ > 1)
            ui::clearNativeCell (sb, page());
        for (int i = 0; i < nRows_; ++i)
        {
            ui::Rect c[4];
            rowCells (i, c);
            for (const ui::Rect& r : { nameRect (i), c[0], c[1], c[2], c[3] })
                ui::clearNativeCell (sb, r);
        }
        ui::Pen p (sb, true);
        ui::Cell cell (sb, rect_);
        const ui::theme::Look& T = ui::theme::L();
        p.rect (rect_.x + 4, rect_.y + kBarH - 1, rect_.right() - 4, rect_.y + kBarH, p.c (T.keyline, 255));
        const float ts = T.fValue + 2.0f;
        const int tw = static_cast<int> (std::ceil (ui::Pen::textWidth (title_, ts))) + 4;
        if (modulations)
        {
            const int right = pages_ > 1 ? page().x : add().x;
            const int w = std::max (0, std::min (right - (rect_.x + 140), tw));
            p.textLeft ({ rect_.x + 140, rect_.y + kToolbarY, w, kCtlH }, title_, ts, p.c (T.text, 255), T.fMin);
            if (target_[0] != '\0')
                p.textLeft ({ rect_.x + 140 + w + 16, rect_.y + kToolbarY, std::max (0, right - (rect_.x + 140) - w - 16), kCtlH }, target_, T.fValue, p.c (T.textDim, 255), T.fMin);
            for (int i = 0; i < nRows_; ++i)
                p.textLeft ({ rect_.x + 8, rowY (i) + kRowH - kCtlH, kNameW - 8, kCtlH }, rows_[i].name, T.fValue, p.c (ui::theme::sourceColour (rows_[i].macro ? "MAC" : rows_[i].name), 255), T.fMin);
            if (nRows_ == 0)
                p.textLeft ({ rect_.x + 8, rect_.y + kBarH + 20, rect_.w - 16, 40 }, "NOTHING MODULATES THIS VALUE YET: PRESS ADD", T.fLabel, p.c (T.textDim, 255), T.fMin);
            return;
        }
        p.textLeft ({ rect_.x + 140, rect_.y + kToolbarY, std::max (0, std::min (tw, rect_.w - 150)), kCtlH }, title_, ts, p.c (colour_, 255), T.fMin);
        char c[32];
        std::snprintf (c, sizeof c, total_ == 1 ? "%d TARGET" : "%d TARGETS", total_);
        p.textLeft ({ rect_.x + 140 + tw + 16, rect_.y + kToolbarY, 160, kCtlH }, c, T.fValue, p.c (T.textDim, 255), T.fMin);
        for (int i = 0; i < nRows_; ++i)
            p.textLeft (nameRect (i), rows_[i].name, T.fValue, p.c (T.text, 255), T.fMin);
        if (nRows_ == 0)
            p.textLeft ({ rect_.x + 8, rect_.y + kBarH + 20, rect_.w - 16, 40 }, "THIS SOURCE MODULATES NOTHING YET: PRESS ADD TARGET", T.fLabel, p.c (T.textDim, 255), T.fMin);
    }

private:
    ui::Rect rect_;
    char title_[32] = {}, target_[24] = {};
    uint32_t colour_ = 0xffffff;
    int nRows_ = 0, activeRow_ = -1, total_ = 0, page_ = 0, pages_ = 1;
    Entry rows_[kPerPage];
};

/** The commands the native cells of a row page report (the page maps them to its own actions). */
enum class ModRowsCmd : uint8_t { Back, Add, Page, Remove, Depth, Polarity, Bypass };

/** The host control ownership and the edit routing of a row page (native cells: nativeui::targets): publishes the bindings of the rows and takes the native edits. */
class LfoNativeModRows
{
public:
    void hide (IInstanceView& inst)
    {
        if (! active_)
            return;
        active_ = false;
        inst.hideNativeModTargets (this);
    }

    /** `routes[row].slot` is the matrix slot of a row; `apply (cmd, row, value)` is called for every native edit (value: the new normalised depth, the switch state 0 / 1, the page offset of +1 / -1). */
    template<class Row, class Apply>
    void update (IInstanceView& inst, const LfoModRows& screen, bool shown, nativeui::targets::Owner owner, int subject, const Row* routes, Apply&& apply,
                 nativeui::targets::Mode mode = nativeui::targets::Targets)
    {
        namespace n = nativeui::targets;
        if (! shown || subject <= 0 || screen.rect().w != 1280 || screen.rect().h != 628 || ! inst.nativeModTargetsSupported())
        {
            hide (inst);
            return;
        }
        active_ = true;
        const auto bind = [&]
        {
            n::State state;
            state.owner = owner;
            state.mode = mode;
            state.presenter = this;
            state.subject = subject;
            state.page = screen.pageIndex();
            state.preset = inst.stateGeneration();
            state.bindings[n::Back] = { 0, 0, nativeui::kFxDial };
            state.bindings[n::Add] = { 0, 0, nativeui::kFxDial };
            if (screen.pageCount() > 1)
                state.bindings[n::Page] = { screen.pageCount(), state.page, nativeui::kFxDial };
            for (int row = 0; row < screen.numRows(); ++row)
            {
                const int slot = routes[row].slot;
                if (slot < 0)
                    continue;
                const auto sp = synthyboy::modSlotParams (slot);
                if (sp.source < 0 || sp.destination < 0)
                    continue;
                const int src = static_cast<int> (std::lround (inst.param (sp.source)));
                const int dest = static_cast<int> (std::lround (inst.param (sp.destination)));
                const int route = mode == n::Targets && src == subject ? dest : mode == n::Modulations && dest == subject ? src : 0;
                if (route <= 0)
                    continue;
                state.bindings[n::field (row, n::Amount)] = { sp.amount, route, nativeui::kFxDial };
                state.bindings[n::field (row, n::Polarity)] = { sp.bipolar, route, nativeui::kFxToggle };
                state.bindings[n::field (row, n::Active)] = { sp.bypass, route, nativeui::kFxToggle };
                state.bindings[n::field (row, n::Remove)] = { slot, route, nativeui::kFxDial };
            }
            inst.publishNativeModTargets (state);
            return state;
        };
        const auto state = bind();
        const auto take = [&] (int field, float& value)
        {
            return state.bindings[field].target >= 0 && inst.stateGeneration() == state.preset && inst.takeNativeModTarget (state, field, value);
        };
        float value;
        for (const auto cmd : { ModRowsCmd::Back, ModRowsCmd::Add })
            if (take (cmd == ModRowsCmd::Back ? n::Back : n::Add, value))
            {
                hide (inst);
                apply (cmd, -1, 0.0);
                return;
            }
        if (take (n::Page, value))
        {
            const int index = state.page + (value > 0.5f ? 1 : -1);
            if (index >= 0 && index < screen.pageCount())
            {
                inst.hideNativeModTargets (this);
                apply (ModRowsCmd::Page, -1, index);
                bind();
                return;
            }
        }
        for (int row = 0; row < screen.numRows(); ++row)
            if (take (n::field (row, n::Remove), value))
            {
                inst.hideNativeModTargets (this);
                apply (ModRowsCmd::Remove, row, 0.0);
                if (mode == n::Modulations && screen.numRows() == 0)
                    hide (inst);
                else
                    bind();
                return;
            }
        for (int row = 0; row < screen.numRows(); ++row)
        {
            if (take (n::field (row, n::Amount), value))
                apply (ModRowsCmd::Depth, row, value);
            if (take (n::field (row, n::Polarity), value))
                apply (ModRowsCmd::Polarity, row, value >= 0.5f ? 1.0 : 0.0);
            if (take (n::field (row, n::Active), value))
                apply (ModRowsCmd::Bypass, row, value >= 0.5f ? 1.0 : 0.0);
        }
    }

private:
    bool active_ = false;
};


// ---- the visible routes of the modulation editor of one destination (the same functions as overlay/modulation_window.h, here without the deleted ui::ModulationsPage) ---------------------------------------------------
namespace lfomods
{
using namespace uibind;
struct ModulationWindow
{
    ui::ModInput entries;
    int total = 0, page = 0, focusRow = -1;
};

/** The visible window of the modulations of one destination (see overlay/modulation_window.h). */
inline ModulationWindow modulationWindow (const IInstanceView& inst, int parameter, int selectedSource,
                                          int requestedPage, ModRow* visible, int focusSlot = -1)
{
    constexpr int perPage = LfoModRows::kPerPage;
    ModulationWindow result;
    result.entries.selectedSource = selectedSource;
    std::fill_n (visible, perPage, ModRow {});
    const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (parameter));
    if (destination == 0)
        return result;

    std::array<ModRow, synthyboy::kNumModSlots> matches;
    for (int slot = 0; slot < synthyboy::kNumModSlots; ++slot)
    {
        const auto p = synthyboy::modSlotParams (slot);
        const int source = static_cast<int> (std::lround (inst.param (p.source)));
        if (source <= 0 || static_cast<size_t> (std::lround (inst.param (p.destination))) != destination)
            continue;
        if (slot == focusSlot)
        {
            requestedPage = result.total / perPage;
            result.focusRow = result.total % perPage;
        }
        matches[result.total++] = {slot, source};
    }

    result.page = std::clamp (requestedPage, 0, std::max (0, (result.total - 1) / perPage));
    const int first = result.page * perPage;
    const int count = std::min (perPage, result.total - first);
    for (int row = 0; row < count; ++row)
    {
        const ModRow binding = matches[first + row];
        const auto p = synthyboy::modSlotParams (binding.slot);
        char name[7];
        shortName (binding.source, name);
        result.entries.add (binding.source, name, inst.param (p.amount) / 100.0,
                            inst.param (p.bipolar) >= 0.5f, inst.param (p.bypass) >= 0.5f, isMacroSource (binding.source));
        visible[row] = binding;
    }
    return result;
}

inline bool modulationMatches (const IInstanceView& inst, int parameter, ModRow route) noexcept
{
    if (parameter < 0 || route.slot < 0 || route.source <= 0)
        return false;
    const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (parameter));
    const auto p = synthyboy::modSlotParams (route.slot);
    return destination != 0 && static_cast<int> (std::lround (inst.param (p.source))) == route.source
        && static_cast<size_t> (std::lround (inst.param (p.destination))) == destination;
}

/** The destination editor starts a newly selected source at zero depth. */
inline int ensureModulationSource (IInstanceView& inst, int parameter, int source)
{
    if (parameter < 0 || source <= 0)
        return -1;
    const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (parameter));
    if (destination == 0)
        return -1;
    const int existing = slotOf (inst, parameter, source);
    if (existing >= 0)
        return existing;
    const int slot = freeSlot (inst);
    if (slot < 0)
        return -1;
    const auto p = synthyboy::modSlotParams (slot);
    writePlain (inst, p.destination, static_cast<double> (destination));
    writePlain (inst, p.amount, 0.0);
    if (p.bypass >= 0)
        writePlain (inst, p.bypass, 0.0);
    if (p.bipolar >= 0)
        writePlain (inst, p.bipolar, info (parameter).min < 0.0f && info (parameter).max > 0.0f ? 1.0 : 0.0);
    writePlain (inst, p.source, source);
    return slot;
}
}

}} // namespace synthyboy_mpc::overlay
