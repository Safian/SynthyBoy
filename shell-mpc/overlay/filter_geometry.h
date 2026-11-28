// SynthyBoy - MPC overlay: the GEOMETRY of the native cells of the Filter page (the tab is 1280 x 628): where the host skin draws the controls (the power switch, the nine dials, ENV 1, ROUTING, MODEL, STYLE, CLEAN, the four
// FORMANT dials, the VIEW / NOTE | VELO selectors, F1+F2, EXPAND), where the response display lies and the blocks of the Q groups (MAIN, TRACK, OUT, FORMANT). Pure rectangles computed from the flags of the page (split or
// full width, filter or curve view, Formant / Comb model): no control, no drawing, no touch (owner decision 2026-11-27: the C++ layer has no UI elements). The cells are indexed by nativeui::FilterSlot.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "../native_filter_controls.h"
#include "../ui/core.h"
#include "../ui/model.h"
#include "page_layout.h"

namespace synthyboy_mpc { namespace overlay {

class FilterGeometry
{
public:
    static constexpr int kMaxGroups = 4;   // MAIN, TRACK, OUT, FORMANT
    static constexpr int kRoutingW = 150;  // the ROUTING cell: dot, "Series 1 to 2", padding
    static constexpr int kTopH = 56, kTopBarH = ui::kQLane + kTopH, kMarkerBandH = 8, kBandH = 214, kBlockTitleH = 34, kBlockPad = 2, kBlockGap = 4, kSlotGap = 0, kBandMargin = 4;
    static constexpr int kViewCellW = 72, kCurveSideW = 400;   // the width of one cell of the VIEW selector; the side column of the NOTE / VELO editor (ScalarCurveEditor::kSideW)
    static constexpr int kToggleBandW = 100;                   // the width of a toggle that sits in the slot of a band of knobs
    static constexpr int kSlots = nativeui::FilterPoint;       // the cells of the filter part: FilterPower .. FilterStyle

    // ---- the state the layout depends on
    void place (const ui::Rect& r)
    {
        rect_ = r;
        layout();
    }
    void setCurveView (bool curve) { if (curveView_ != curve) { curveView_ = curve; relayout(); } }
    void setSubTabPage (bool on) { if (subTabPage_ != on) { subTabPage_ = on; relayout(); } }
    void setPageMode (bool on) { if (pageMode_ != on) { pageMode_ = on; relayout(); } }
    void setFormantMode (bool on) { if (formant_ != on) { formant_ = on; if (on) comb_ = false; relayout(); } }
    void setCombMode (bool on) { const bool eff = on && ! formant_; if (comb_ != eff) { comb_ = eff; relayout(); } }
    bool curveView() const noexcept { return curveView_; }
    bool subTabPage() const noexcept { return subTabPage_; }
    bool pageMode() const noexcept { return pageMode_; }
    bool formantMode() const noexcept { return formant_; }
    bool combMode() const noexcept { return comb_; }

    // ---- the rectangles
    ui::Rect rect() const noexcept { return rect_; }
    /** The cell of a filter control (nativeui::FilterPower .. FilterStyle). */
    ui::Rect cell (int slot) const noexcept { return slot >= 0 && slot < kSlots ? cell_[slot] : ui::Rect {}; }
    ui::Rect displayRect() const noexcept { return graph_; }
    ui::Rect expand() const noexcept { return expand_; }
    ui::Rect both() const noexcept { return both_; }
    ui::Rect view() const noexcept { return view_; }
    ui::Rect outReadout() const noexcept { return outReadout_; }
    ui::Rect block (int i) const noexcept { return i == 0 ? main_ : i == 1 ? track_ : i == 2 ? out_ : formantBlock_; }
    /** What the body of NOTE and VELO is drawn in: everything below the top bar (1280 x 556 on the page). On the page of a hardware sub-tab the top bar has nothing to hold (no selector: the sub-tab names the
        state, the chain, MODEL and STYLE belong to the filter pages), so the body is the page under the strip of the three page markers (kMarkerBandH: the markers of the detector are at the top edge, no
        control may touch them). */
    ui::Rect bodyRect() const noexcept
    {
        if (subTabPage_)
            return { rect_.x, rect_.y + kMarkerBandH, rect_.w, std::max (0, rect_.h - kMarkerBandH) };
        return { rect_.x, rect_.y + kTopBarH, rect_.w, std::max (0, rect_.h - kTopBarH) };
    }

    /** The width a toggle with this name needs in a row (the dot, the gap, the text, the padding; at least 96 px). */
    static int toggleWidthFor (const char* name) noexcept
    {
        const int w = 25 + 6 + 2 + static_cast<int> (std::ceil (ui::Pen::textWidth (name, ui::theme::L().fLabel + 3.0f)));
        return std::max (96, w);
    }

private:
    void relayout() { if (rect_.w > 0) layout(); }

    void clearCells()
    {
        cell_.fill ({});
        both_ = view_ = outReadout_ = expand_ = graph_ = main_ = track_ = out_ = formantBlock_ = {};
    }

    void layout()
    {
        using namespace nativeui;
        clearCells();
        const int m = 8, W = rect_.w;
        const bool isExpCurve = curveView_;
        const ui::Rect top { rect_.x + m, rect_.y + ui::kQLane, W - 2 * m - (isExpCurve ? 62 : 0), kTopH };
        ui::Rect r[7];
        const int fixed[7] = { pageMode_ ? 0 : 84, isExpCurve ? 0 : 56, 4 * kViewCellW, toggleWidthFor ("F1+F2"), 0, 0, toggleWidthFor ("CLEAN") }, weight[7] = { 0, 0, 0, 0, 5, 4, 0 };
        pagegeo::layoutRow (top, fixed, weight, 7, pagegeo::kGap, r);
        view_ = r[2];
        both_ = r[3];
        cell_[FilterModel] = r[4];
        cell_[FilterStyle] = r[5];
        cell_[FilterClean] = r[6];
        if (isExpCurve)
        {
            const int graphRight = rect_.right() - 2 * m - kCurveSideW;
            expand_ = { graphRight - 8 - 56, top.bottom() + m + 4, 56, 56 };
            cell_[FilterModel] = cell_[FilterStyle] = cell_[FilterClean] = {};
            return;
        }

        // 2. the bottom band: three blocks, one Q group each (a slot is a dial or a switch of the same size)
        const int bandY = rect_.bottom() - kBandMargin - kBandH;
        // the blocks are as wide as their slots need: a dial slot, the ENV 1 toggle (a toggle of its band width) and the ROUTING cell (three rows of 56 px, the longest name in one column)
        const int pads = 2 * kBlockPad + 3 * kSlotGap, togW = kToggleBandW, listW = kRoutingW;
        const int avail = W - 2 * kBandMargin - 2 * kBlockGap, unit = (avail - 3 * pads - togW - listW) / 10;
        const int wMain = 4 * unit + pads, wTrack = 3 * unit + togW + pads;
        main_ = { rect_.x + kBandMargin, bandY, wMain, kBandH };
        track_ = { main_.right() + kBlockGap, bandY, wTrack, kBandH };
        out_ = { track_.right() + kBlockGap, bandY, rect_.right() - kBandMargin - (track_.right() + kBlockGap), kBandH };
        auto inner = [&] (const ui::Rect& b) { return ui::Rect { b.x + kBlockPad, b.y + kBlockTitleH, std::max (0, b.w - 2 * kBlockPad), std::max (0, b.h - kBlockTitleH - kBlockPad) }; };
        // four slots side by side; `fixedW[i]` > 0: that slot has exactly that width, the others share the rest equally
        auto fourSlots = [&] (const ui::Rect& b, const int* slots, const int* fixedW)
        {
            const ui::Rect in = inner (b);
            int fixedSum = 0, nFree = 0;
            for (int i = 0; i < 4; ++i)
            {
                if (fixedW[i] > 0)
                    fixedSum += fixedW[i];
                else
                    ++nFree;
            }
            const int cw = nFree > 0 ? (in.w - 3 * kSlotGap - fixedSum) / nFree : 0;
            int x = in.x;
            for (int i = 0; i < 4; ++i)
            {
                const int w = i == 3 ? in.right() - x - (fixedW[3] > 0 ? 6 : 0) : (fixedW[i] > 0 ? fixedW[i] : cw);
                cell_[slots[i]] = { x, in.y, w, in.h };
                x += w + kSlotGap;
            }
        };
        const int noFixed[4] = { 0, 0, 0, 0 }, trackFixed[4] = { 0, togW, 0, 0 };
        const int mainSlots[4] = { FilterCutoff, FilterResonance, FilterDrive, FilterMix };
        const int trackSlots[4] = { FilterKeytrack, FilterEnv1, formant_ ? FilterFormantResonance : FilterVar, FilterCombOffset };
        fourSlots (main_, mainSlots, noFixed);
        fourSlots (track_, trackSlots, trackFixed);
        {
            const ui::Rect third = cell_[formant_ ? FilterFormantResonance : FilterVar];   // VAR and the formant RESONANCE share the third slot (only one of them is shown)
            cell_[FilterVar] = third;
            cell_[FilterFormantResonance] = third;
        }
        {
            const ui::Rect in = inner (out_);
            cell_[FilterBlend] = { in.x, in.y, unit, in.h };
            cell_[FilterRouting] = { cell_[FilterBlend].right() + kSlotGap, in.y, listW, in.h };
            const int rx = cell_[FilterRouting].right() + 8;
            outReadout_ = { rx, in.y + 6, std::max (0, in.right() - 6 - rx), std::max (0, in.h - 12) };
        }

        // 3. the display row: the display, and in the Formant model the XY pad with its block at the right
        const int midY = top.bottom() + 4, midH = std::max (60, bandY - m - midY);
        const int fw = formant_ ? std::min (440, (W - 2 * m) * 36 / 100) : 0;
        const int gw = formant_ ? (W - 2 * m - fw - kBlockGap) : (W - 2 * m);
        graph_ = { rect_.x + m, midY, gw, midH };
        cell_[FilterPower] = { graph_.x + 8, graph_.y + 4, 56, 56 };
        expand_ = { graph_.right() - 8 - 56, graph_.y + 4, 56, 56 };
        if (formant_)
        {
            formantBlock_ = { rect_.right() - m - fw, midY, fw, midH };
            const ui::Rect in = inner (formantBlock_);
            const int cw = (in.w - pagegeo::kGap) / 2, ch = (in.h - ui::kQLane - pagegeo::kGap) / 2;   // X, Y / TRANSPOSE, SPREAD in a 2 x 2 grid (the Q group)
            cell_[FilterFormantX] = { in.x, in.y, cw, ch };
            cell_[FilterFormantY] = { in.x + cw + pagegeo::kGap, in.y, in.w - cw - pagegeo::kGap, ch };
            cell_[FilterFormantTranspose] = { in.x, in.y + ch + pagegeo::kGap + ui::kQLane, cw, ch };
            cell_[FilterFormantSpread] = { in.x + cw + pagegeo::kGap, in.y + ch + pagegeo::kGap + ui::kQLane, in.w - cw - pagegeo::kGap, ch };
        }
    }

    ui::Rect rect_;
    std::array<ui::Rect, nativeui::FilterPoint> cell_ {};
    ui::Rect both_, view_, outReadout_, expand_, graph_, main_, track_, out_, formantBlock_;
    bool curveView_ = false, subTabPage_ = false, pageMode_ = false, formant_ = false, comb_ = false;
};

}} // namespace synthyboy_mpc::overlay
