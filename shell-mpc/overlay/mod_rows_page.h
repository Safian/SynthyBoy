// SynthyBoy - MPC overlay: the ONE data model of the two detail pages "targets of a source" and "modulations of a control" (Env, LFO, Filter NOTE / VELO, Mod, Osc, ...): the rows (name, source, depth, polarity, on / off),
// the page index, the title and the rectangles of the native cells (the geometry of scripts/mpc-skin-extras/mod-targets-native-contract.json)
// the host skin draws (BACK, ADD, the page stepper and, per row, the depth, the polarity, the on / off and the remove cell). A model, not a control (owner decision 2026-11-27: the C++ layer has no UI elements): it
// takes no touch and draws no control; the overlay paints only what the host skin does not (the title, the count, the names, the empty hint) and cuts the native cells out of the ground. The native bridge
// (native_mod_targets_ui.h) publishes the rows and reads the gestures of the skin through the same ids the page gave (`setRowIds`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../ui/core.h"
#include "../ui/model.h"
#include "../ui/native_apertures.h"
#include "../ui/qgroup.h"
#include "../ui/theme.h"

namespace synthyboy_mpc { namespace overlay {

class ModRowsPage
{
public:
    /** Targets: the rows of the matrix whose source is one envelope / LFO / filter source (the names are the host's in the native mode). Modulations: the rows that modulate one control (the names are the sources, in their colour). */
    enum class Kind : uint8_t { Targets, Modulations };

    static constexpr int kPerPage = 6, kRowH = 72, kCtlH = 56, kBarH = 92, kNameW = 340;
    static constexpr int kToolbarY = 32, kToolbarAddW = 200, kPageW = 208, kGap = 4, kNameLen = 40;

    /** The id and the rectangle of one native cell. */
    struct Cell
    {
        int id() const noexcept { return id_; }
        void setId (int id) noexcept { id_ = id; }
        const ui::Rect& rect() const noexcept { return rect_; }
        int id_ = 0;
        ui::Rect rect_;
    };
    struct PageCell : Cell
    {
        struct { int index = 0, count = 1; } state;   // the page stepper: the shown page and the number of pages
    };
    struct Row
    {
        Cell depth, polarity, bypass, remove;
        char name[kNameLen] = {};
        int source = 0;
        bool macro = false;
        double depthNorm = 0.5;                // 0 .. 1 (the depth cell)
        bool bidirectional = false, on = true; // on = the modulation is active
    };
    struct Entry
    {
        char name[kNameLen] = {};
        int source = 0;
        bool macro = false;
        double depth = 0.0;                    // -1 .. +1 of the range of the target
        bool bidirectional = false, bypassed = false;
    };

    Cell back, add;
    PageCell page;
    Row rows[kPerPage];

    explicit ModRowsPage (Kind kind = Kind::Targets) : kind_ (kind) {}

    Kind kind() const noexcept { return kind_; }
    ui::Rect rect() const noexcept { return rect_; }
    void place (const ui::Rect& r)
    {
        rect_ = r;
        layout();
    }

    /** The rows of the page shown now (`n` at most kPerPage entries), the title (the source or the control), its colour, how many rows there are in all and which page this is (0 ..). */
    void load (const Entry* e, int n, const char* title, uint32_t colour, int total, int pageIndex)
    {
        ui::setText (title_, sizeof title_, title);
        colour_ = colour;
        total_ = total;
        nRows_ = std::clamp (n, 0, kPerPage);
        page.state.count = std::max (1, (total + kPerPage - 1) / kPerPage);
        page.state.index = std::clamp (pageIndex, 0, page.state.count - 1);
        for (int i = 0; i < nRows_; ++i)
        {
            ui::setText (rows[i].name, sizeof rows[i].name, e[i].name);
            rows[i].source = e[i].source;
            rows[i].macro = e[i].macro;
            rows[i].depthNorm = (std::clamp (e[i].depth, -1.0, 1.0) + 1.0) * 0.5;
            rows[i].bidirectional = e[i].bidirectional;
            rows[i].on = ! e[i].bypassed;
        }
        selectRow (nRows_ > 0 ? std::min (std::max (activeRow_, 0), nRows_ - 1) : -1);
        layout();
    }
    /** The value of the control a modulations page is about, shown beside the title (empty = none). */
    void setTargetValue (const char* s) { ui::setText (target_, sizeof target_, s); }
    /** The host skin shows the cells (true) or the host has none (false: the page shows the names only). */
    void setNativeControls (bool active)
    {
        nativeControls_ = active;
        layout();
    }
    bool nativeControls() const noexcept { return nativeControls_; }

    int numRows() const noexcept { return nRows_; }
    int activeRow() const noexcept { return activeRow_; }
    int pageIndex() const noexcept { return page.state.index; }
    int pageCount() const noexcept { return page.state.count; }
    int total() const noexcept { return total_; }
    const char* title() const noexcept { return title_; }
    uint32_t colour() const noexcept { return colour_; }
    /** True when row `i` of the page shows exactly this data (a reload resets the touch of a row that is being dragged). */
    bool sameRow (int i, const Entry& e) const noexcept
    {
        const Row& r = rows[i];
        return std::strcmp (r.name, e.name) == 0 && std::fabs (r.depthNorm - (e.depth + 1.0) * 0.5) < 1.0e-9 && r.bidirectional == e.bidirectional && r.on == ! e.bypassed;
    }
    /** The ids of the cells the page reports changes with (the owner's numbering): BACK, ADD, the page cell, and per row `rowBase + 4 * row` + 0 depth, 1 polarity, 2 on / off, 3 remove. */
    void setIds (int backId, int addId, int pageId, int rowBase)
    {
        back.setId (backId);
        add.setId (addId);
        page.setId (pageId);
        for (int row = 0; row < kPerPage; ++row)
            setRowIds (row, rowBase + 4 * row, rowBase + 4 * row + 1, rowBase + 4 * row + 2, rowBase + 4 * row + 3);
    }
    /** The Q-Link `knobIndex` of the active row: 0 depth, 1 polarity, 2 on / off; the ids are those of `setIds`. */
    ui::Change turnQ (int knobIndex, double delta) const noexcept
    {
        if (activeRow_ < 0 || delta == 0.0)
            return ui::Change::none();
        const Row& r = rows[activeRow_];
        if (knobIndex == 0)
        {
            const double v = std::clamp (r.depthNorm + delta, 0.0, 1.0);
            return std::fabs (v - std::clamp (r.depthNorm, 0.0, 1.0)) < 1.0e-12 ? ui::Change::none() : ui::Change::normalised (r.depth.id(), v);
        }
        const bool cur = knobIndex == 1 ? r.bidirectional : r.on, next = delta > 0.0;
        if ((knobIndex != 1 && knobIndex != 2) || next == cur)
            return ui::Change::none();
        return ui::Change::indexed (knobIndex == 1 ? r.polarity.id() : r.bypass.id(), next ? 1 : 0, next ? 1.0 : 0.0);
    }
    void selectRow (int i) noexcept { activeRow_ = i >= 0 && i < nRows_ ? i : -1; }
    /** The page id of the cells of a row (the ids the native bridge reports). */
    void setRowIds (int row, int depth, int polarity, int bypass, int remove)
    {
        if (row < 0 || row >= kPerPage)
            return;
        rows[row].depth.setId (depth);
        rows[row].polarity.setId (polarity);
        rows[row].bypass.setId (bypass);
        rows[row].remove.setId (remove);
    }
    /** The block of knobs (QRouter::blockOfGroup) the row has while it is not the active one (-1: none). */
    void setRowBlock (int row, int block) noexcept { if (row >= 0 && row < kPerPage) rowBlock_[row] = block; }
    int rowBlock (int row) const noexcept { return row >= 0 && row < kPerPage ? rowBlock_[row] : -1; }

    int rowY (int i) const noexcept { return rect_.y + kBarH + 8 + i * kRowH; }
    ui::Rect nameRect (int row) const noexcept { return { rect_.x + 8, rowY (row) + kRowH - kCtlH, kNameW - 16, kCtlH }; }
    /** The row whose lane contains y (-1: none). */
    int rowAt (int x, int y) const noexcept
    {
        if (! rect_.contains (x, y))
            return -1;
        for (int i = 0; i < nRows_; ++i)
            if (y >= rowY (i) && y < rowY (i) + kRowH)
                return i;
        return -1;
    }
    /** The touch is on a native cell (the skin owns it); `row` receives the row of a row cell (-1 for BACK / ADD / the page stepper). */
    bool nativeHit (int x, int y, int* row = nullptr) const noexcept
    {
        if (! nativeControls_)
            return false;
        if (row != nullptr)
            *row = -1;
        if (back.rect().contains (x, y) || add.rect().contains (x, y) || (page.state.count > 1 && page.rect().contains (x, y)))
            return true;
        for (int i = 0; i < nRows_; ++i)
        {
            const Row& r = rows[i];
            if (r.depth.rect().contains (x, y) || r.polarity.rect().contains (x, y) || r.bypass.rect().contains (x, y) || r.remove.rect().contains (x, y))
            {
                if (row != nullptr)
                    *row = i;
                return true;
            }
        }
        return false;
    }
    /** The frame of the Q group of the active row: its depth, polarity and on / off cells. */
    ui::Rect activeRowFrame() const noexcept
    {
        if (activeRow_ < 0)
            return {};
        const Row& r = rows[activeRow_];
        return ui::Rect::unite (ui::Rect::unite (r.depth.rect(), r.polarity.rect()), r.bypass.rect());
    }

    /** The overlay's part of the page: the cut-outs of the native cells, the title and the count, the names (the host draws the target names itself in the native mode), the empty hint, the Q frame of the active row. */
    void draw (gpu::SceneBuilder& sb) const
    {
        if (nativeControls_)
        {
            ui::clearNativeCell (sb, back.rect());
            ui::clearNativeCell (sb, add.rect());
            if (page.state.count > 1)
                ui::clearNativeCell (sb, page.rect());
            for (int i = 0; i < nRows_; ++i)
            {
                const Row& r = rows[i];
                const ui::Rect named = kind_ == Kind::Targets ? nameRect (i) : ui::Rect {};
                for (const ui::Rect& area : { named, r.depth.rect(), r.polarity.rect(), r.bypass.rect(), r.remove.rect() })
                    ui::clearNativeCell (sb, area);
            }
        }
        {
            ui::Pen p (sb, true, false);
            ui::Cell cell (sb, rect_);
            drawLabels (p);
        }
        if (! ui::QGroup::wideDevice() && activeRow_ >= 0 && nativeControls_)
        {
            const ui::Rect frame = activeRowFrame();
            ui::Pen pen (sb, true, false);
            ui::Cell cell (sb, ui::Rect::unite (frame, ui::Pen::qMarkerRect (frame)));
            pen.qFocus (frame);
        }
    }

private:
    void layout()
    {
        const int toolbarY = rect_.y + kToolbarY;
        back.rect_ = { rect_.x + 4, toolbarY, 120, kCtlH };
        add.rect_ = { rect_.right() - 4 - kToolbarAddW, toolbarY, kToolbarAddW, kCtlH };
        page.rect_ = { add.rect_.x - kGap - kPageW, toolbarY, kPageW, kCtlH };
        for (int i = 0; i < kPerPage; ++i)
        {
            Row& r = rows[i];
            const ui::Rect lane { rect_.x + 4 + kNameW, rowY (i) + kRowH - kCtlH, rect_.w - 8 - kNameW, kCtlH };
            const int polW = 72, byW = 72, remW = 64;
            const int depthW = std::max (0, lane.w - polW - byW - remW - 3 * kGap);
            int x = lane.x;
            r.depth.rect_ = { x, lane.y, depthW, lane.h };
            x += depthW + kGap;
            r.polarity.rect_ = { x, lane.y, polW, lane.h };
            x += polW + kGap;
            r.bypass.rect_ = { x, lane.y, byW, lane.h };
            x += byW + kGap;
            r.remove.rect_ = { x, lane.y, remW, lane.h };
        }
    }

    void drawLabels (ui::Pen& p) const
    {
        const ui::theme::Look& T = ui::theme::L();
        p.rect (rect_.x + 4, rect_.y + kBarH - 1, rect_.right() - 4, rect_.y + kBarH, p.c (T.keyline, 255));
        const float ts = T.fValue + 2.0f;
        const int titleX = rect_.x + 140;
        const int tw = std::max (0, std::min (static_cast<int> (std::ceil (ui::Pen::textWidth (title_, ts))) + 4, rect_.w - 150));
        p.textLeft ({ titleX, rect_.y + kToolbarY, tw, kCtlH }, title_, ts, p.c (kind_ == Kind::Targets ? colour_ : T.text, 255), T.fMin);
        if (kind_ == Kind::Targets)
        {
            char c[32];
            std::snprintf (c, sizeof c, total_ == 1 ? "%d TARGET" : "%d TARGETS", total_);
            p.textLeft ({ titleX + tw + 16, rect_.y + kToolbarY, 160, kCtlH }, c, T.fValue, p.c (T.textDim, 255), T.fMin);
        }
        else if (target_[0] != '\0')
        {
            const int right = page.state.count > 1 ? page.rect().x : add.rect().x;
            p.textLeft ({ titleX + tw + 16, rect_.y + kToolbarY, std::max (0, right - titleX - tw - 16), kCtlH }, target_, T.fValue, p.c (T.textDim, 255), T.fMin);
        }
        for (int i = 0; i < nRows_; ++i)
        {
            if (kind_ == Kind::Targets)
            {
                if (! nativeControls_)
                    p.textLeft (nameRect (i), rows[i].name, T.fValue, p.c (T.text, 255), T.fMin);
            }
            else
                p.textLeft ({ rect_.x + 8, rowY (i) + kRowH - kCtlH, kNameW - 8, kCtlH }, rows[i].name, T.fValue, p.c (ui::theme::sourceColour (rows[i].macro ? "MAC" : rows[i].name), 255), T.fMin);
        }
        if (nRows_ == 0)
            p.textLeft ({ rect_.x + 8, rect_.y + kBarH + 20, rect_.w - 16, 40 },
                        kind_ == Kind::Targets ? "THIS SOURCE MODULATES NOTHING YET: PRESS ADD TARGET" : "NOTHING MODULATES THIS VALUE YET: PRESS ADD AND CHOOSE A SOURCE",
                        T.fLabel, p.c (T.textDim, 255), T.fMin);
    }

    Kind kind_ = Kind::Targets;
    ui::Rect rect_;
    char title_[32] = {}, target_[24] = {};
    uint32_t colour_ = 0xffffff;
    int nRows_ = 0, activeRow_ = -1, total_ = 0;
    int rowBlock_[kPerPage] = {};
    bool nativeControls_ = false;
};

}} // namespace synthyboy_mpc::overlay
