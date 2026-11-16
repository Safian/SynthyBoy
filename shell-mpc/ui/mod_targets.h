// SynthyBoy - MPC UI component library: ModTargetsPage, the modulation page seen from the SOURCE: "where does this envelope / LFO go". The page of ModulationsPage (panel.h) is the view from a control (which
// sources move it); this one lists the targets of one source: a row per modulation slot that has the source, with the name of the target, the depth (an HBar, both ways from the middle), the polarity switch,
// the on / off switch and the remove button, and a button that asks for another target ("add target"). Same contract as every component (core.h): a state in (the rows), changes out with the ids the owner gave.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// More targets than fit are paged (kPerPage rows a page): the page stepper is a Stepper whose Index change is the new page; the owner loads the rows of that page. The rows of the active row form one Q group:
// [depth, polarity, on / off, -]; a touch on a row makes it the active one.
#pragma once

#include "panel.h"

namespace synthyboy_mpc { namespace ui {

class ModTargetsPage : public Composite
{
public:
    static constexpr int kPerPage = 6, kRowH = 72, kCtlH = 56, kBarH = 64, kNameW = 340;
    struct Entry
    {
        char name[40] = {};
        double depth = 0.0;                    // -1 .. +1 of the range of the target
        bool bidirectional = false, bypassed = false;
    };
    struct Row
    {
        HBar depth;
        ToggleIcon polarity, bypass;
        MenuButton remove { "X", MenuButton::Icon::None };
        char name[40] = {};
    };
    MenuButton back { "BACK", MenuButton::Icon::None };
    Stepper page;
    MenuButton add { "ADD TARGET", MenuButton::Icon::None };
    Row rows[kPerPage];
    QGroup group;

    ModTargetsPage()
    {
        addKid (back); addKid (page, false); addKid (add);
        for (Row& r : rows)
        {
            r.depth.state.bipolar = true;
            r.depth.state.defaultValue = 0.5;
            r.polarity.state.icon = ToggleIcon::Icon::Generic;
            r.bypass.state.icon = ToggleIcon::Icon::Power;
            setRowState (r, false, false);
            addKid (r.depth, false); addKid (r.polarity, false); addKid (r.bypass, false); addKid (r.remove, false);
        }
    }
    static void setRowState (Row& r, bool bidirectional, bool bypassed)
    {
        r.polarity.state.on = bidirectional;
        r.polarity.setLabel (bidirectional ? "BI" : "UNI");
        r.bypass.state.on = ! bypassed;
        r.bypass.setLabel (bypassed ? "OFF" : "ON");
    }
    /** The rows of the page shown now (`n` at most kPerPage entries), the title (the source: "ENV 3"), its colour, how many targets the source has in all and which page this is (0 ..). */
    void load (const Entry* e, int n, const char* title, uint32_t colour, int total, int pageIndex)
    {
        setText (title_, sizeof title_, title);
        colour_ = colour;
        total_ = total;
        nRows_ = std::clamp (n, 0, kPerPage);
        const int pages = std::max (1, (total + kPerPage - 1) / kPerPage);
        page.state.count = pages;
        page.state.index = std::clamp (pageIndex, 0, pages - 1);
        page.state.wrap = false;
        char t[32];
        std::snprintf (t, sizeof t, "%d / %d", page.state.index + 1, pages);
        page.setItem (t);
        show (page, pages > 1);
        for (int i = 0; i < kPerPage; ++i)
        {
            Row& r = rows[i];
            const bool on = i < nRows_;
            show (r.depth, on); show (r.polarity, on); show (r.bypass, on); show (r.remove, on);
            if (on)
            {
                setText (r.name, sizeof r.name, e[i].name);
                r.depth.state.value = r.depth.state.modValue = (std::clamp (e[i].depth, -1.0, 1.0) + 1.0) * 0.5;
                setRowState (r, e[i].bidirectional, e[i].bypassed);
            }
        }
        selectRow (nRows_ > 0 ? std::min (std::max (activeRow_, 0), nRows_ - 1) : -1);
        layout();
    }
    int numRows() const noexcept { return nRows_; }
    int activeRow() const noexcept { return activeRow_; }
    void selectRow (int i)
    {
        for (int k = 0; k < 4; ++k)
            group.set (k, nullptr);
        activeRow_ = i >= 0 && i < nRows_ ? i : -1;
        if (activeRow_ < 0)
        {
            group.setActive (false);
            return;
        }
        group.set (0, &rows[i].depth); group.set (1, &rows[i].polarity); group.set (2, &rows[i].bypass);
        group.setActive (true);
    }
    void setRowIds (int row, int depth, int polarity, int bypass, int remove)
    {
        if (row >= 0 && row < kPerPage)
        {
            rows[row].depth.setId (depth); rows[row].polarity.setId (polarity); rows[row].bypass.setId (bypass); rows[row].remove.setId (remove);
        }
    }
    int rowY (int i) const noexcept { return rect_.y + kBarH + 8 + i * kRowH; }
    Change turnQ (int knobIndex, double delta) { return group.turn (knobIndex, delta); }
    Size minSize() const override { return { kNameW + 200 + 2 * 72 + 64 + 4 * kGap + 8, kBarH + 8 + kPerPage * kRowH }; }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)
            for (int i = 0; i < nRows_; ++i)
                if (y >= rowY (i) && y < rowY (i) + kRowH)
                {
                    selectRow (i);
                    break;
                }
        return c;
    }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        group.draw (sb);
        for (int i = 0; i < nRows_ && i < kPerPage; ++i)
            if (i != activeRow_)
            {
                const Component* m[3] = { &rows[i].depth, &rows[i].polarity, &rows[i].bypass };
                QGroup::drawMappedMembers (sb, rowBlock_[i], m, 3);
            }
    }
    /** The block of knobs (QRouter::blockOfGroup) the row has while it is not the active one: its idle chips show it (-1: none). */
    void setRowBlock (int row, int block) noexcept { if (row >= 0 && row < kPerPage) rowBlock_[row] = block; }

protected:
    void layout() override
    {
        back.place ({ rect_.x + 4, rect_.y + 4, 120, kBarH - 8 });
        add.place ({ rect_.right() - 4 - 200, rect_.y + 4, 200, kBarH - 8 });
        page.place ({ rect_.right() - 4 - 200 - kGap - Stepper::kMinW, rect_.y + 4, Stepper::kMinW, kBarH - 8 });
        for (int i = 0; i < nRows_; ++i)
        {
            Row& r = rows[i];
            Rect k[4];
            const int fixed[4] = { 0, 72, 72, 64 }, weight[4] = { 1, 0, 0, 0 };
            layoutRow ({ rect_.x + 4 + kNameW, rowY (i) + kRowH - kCtlH, rect_.w - 8 - kNameW, kCtlH }, fixed, weight, 4, kGap, k);
            r.depth.place (k[0]); r.polarity.place (k[1]); r.bypass.place (k[2]); r.remove.place (k[3]);
        }
    }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.rect (rect_.x + 4, rect_.y + kBarH - 1, rect_.right() - 4, rect_.y + kBarH, p.c (T.keyline, 255));
        const float ts = T.fValue + 2.0f;
        const int tw = static_cast<int> (std::ceil (Pen::textWidth (title_, ts))) + 4;
        p.textLeft ({ rect_.x + 140, rect_.y + 4, std::max (0, std::min (tw, rect_.w - 150)), kBarH - 8 }, title_, ts, p.c (colour_, 255), T.fMin);
        char c[32];
        std::snprintf (c, sizeof c, total_ == 1 ? "%d TARGET" : "%d TARGETS", total_);
        p.textLeft ({ rect_.x + 140 + tw + 16, rect_.y + 4, 160, kBarH - 8 }, c, T.fValue, p.c (T.textDim, 255), T.fMin);
        for (int i = 0; i < nRows_; ++i)
            p.textLeft ({ rect_.x + 8, rowY (i) + kRowH - kCtlH, kNameW - 16, kCtlH }, rows[i].name, T.fValue, p.c (T.text, 255), T.fMin);
        if (nRows_ == 0)
            p.textLeft ({ rect_.x + 8, rect_.y + kBarH + 20, rect_.w - 16, 40 }, "THIS SOURCE MODULATES NOTHING YET: PRESS ADD TARGET", T.fLabel, p.c (T.textDim, 255), T.fMin);
    }
    char title_[24] = {};
    uint32_t colour_ = 0xffffff;
    int nRows_ = 0, activeRow_ = 0, total_ = 0;
    int rowBlock_[kPerPage] = {};
};

}} // namespace synthyboy_mpc::ui
