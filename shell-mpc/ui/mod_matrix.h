// SynthyBoy - MPC UI: Modulation Matrix View
// The page of the modulation matrix (1280 x 628): the slots as rows, scrolled, filtered by source or by destination module. A row is: the number (it selects the row, the first Q-Link of the row), the SOURCE and the
// TARGET (buttons that open lists), the AMOUNT (the library's HBar: both ways from the middle, the value at its right end, a live marker), UNI / BI, ON / OFF and MORE (the details of the slot). The selected row is
// one Q group: row number (Q1), SOURCE (Q2), TARGET (Q3), and AMOUNT (Q4).
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
#include "list.h"
#include "mod_board.h"
#include "qgroup.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** The number of a row as the first member of its Q group: it steps the selected row (turn: an Index change with the new row), and a tap on it is the row selection of the view. */
class ModRowSelector : public Component
{
public:
    int selected = 0;
    int maxRows = 0;
    char text[8] = {};

    ModRowSelector() = default;
    Size minSize() const override { return { 56, 56 }; }

    Change turn (double delta) override
    {
        if (maxRows <= 0) return Change::none();
        int step = delta > 0.0 ? 1 : (delta < 0.0 ? -1 : 0);
        if (step != 0)
        {
            selected = std::clamp (selected + step, 0, maxRows - 1);
            return Change::indexed (id_, selected, static_cast<double> (selected));
        }
        return Change::none();
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textCentered (rect_, text, T.fLabel, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
    }
};

// ===============================================================================================================================================================
/** Single modulation slot row (kRowH pixels: a lane of kQLane for the Q index chips above the 56 px controls):
    # | SOURCE | -> | TARGET | AMOUNT (HBar) | UNI / BI | ON / OFF | MORE */
class ModRow : public Composite
{
public:
    static constexpr int kRowH = 72, kCtlH = 56;

    int slotIndex = 0;       // 0 .. 63
    bool selected = false;
    bool active = false;

    char sourceName[32] = {};
    char destName[40] = {};

    ModRowSelector number;
    MenuButton sourceBtn { "---", MenuButton::Icon::Down };
    MenuButton destBtn { "---", MenuButton::Icon::Down };
    HBar amountBar;
    ToggleIcon biToggle, bypassToggle;
    MenuButton detailBtn { "MORE", MenuButton::Icon::None };

    ModRow()
    {
        amountBar.state.bipolar = true;
        amountBar.state.defaultValue = 0.5;
        biToggle.state.icon = ToggleIcon::Icon::Generic;
        bypassToggle.state.icon = ToggleIcon::Icon::Power;
        addKid (number);
        addKid (sourceBtn);
        addKid (destBtn);
        addKid (amountBar);
        addKid (biToggle);
        addKid (bypassToggle);
        addKid (detailBtn);
    }

    Size minSize() const override { return { 900, kRowH }; }
    /** The MORE button is for the matrix; the details page shows the row without it. */
    void showDetailButton (bool shown) { show (detailBtn, shown); }

    void setSlot (int idx)
    {
        slotIndex = idx;
        std::snprintf (number.text, sizeof number.text, "%02d", std::clamp (idx + 1, 0, 99));
        layout();
    }

    /** `amount` -1 .. +1, `liveAmount` the live position of the modulation (-1 .. +1) when `hasLive`. */
    void updateData (const char* src, const char* dst, double amount, bool bi, bool bypassed, bool hasLiveVal = false, double liveAmount = 0.0)
    {
        setText (sourceName, sizeof sourceName, src);
        setText (destName, sizeof destName, dst);
        sourceBtn.setLabel (src[0] != '\0' ? src : "---");
        destBtn.setLabel (dst[0] != '\0' ? dst : "---");
        amountBar.state.value = (std::clamp (amount, -1.0, 1.0) + 1.0) * 0.5;
        amountBar.state.modValue = hasLiveVal ? (std::clamp (liveAmount, -1.0, 1.0) + 1.0) * 0.5 : amountBar.state.value;
        std::snprintf (amountBar.state.text, sizeof amountBar.state.text, "%+d%%", static_cast<int> (std::lround (amount * 100.0)));
        biToggle.state.on = bi;
        biToggle.setLabel (bi ? "BI" : "UNI");
        bypassToggle.state.on = ! bypassed;
        bypassToggle.setLabel (bypassed ? "OFF" : "ON");
        active = src[0] != '\0' && dst[0] != '\0';
    }

protected:
    void layout() override
    {
        const int y = rect_.y + kQLane, h = kCtlH, gap = 6;
        int x = rect_.x + 4;
        number.place ({ x, y, 56, h });
        x += 56 + gap;
        // the AMOUNT bar is the widest element of the row (the fields of the source and the target are only as wide as their names need; the bar gets the rest, at least 380 pixels)
        sourceBtn.place ({ x, y, 176, h });
        x += 176 + 26;
        destBtn.place ({ x, y, 244, h });
        x += 244 + gap;
        const int rightW = 72 + gap + 72 + gap + 76;
        const int barW = std::max (160, rect_.right() - 4 - rightW - gap - x);
        amountBar.place ({ x, y, barW, h });
        x += barW + gap;
        biToggle.place ({ x, y, 72, h });
        x += 72 + gap;
        bypassToggle.place ({ x, y, 72, h });
        x += 72 + gap;
        detailBtn.place ({ x, y, std::max (56, rect_.right() - 4 - x), h });
    }

    void drawUnder (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        // the arrow between the source and the target
        const float ax = static_cast<float> (sourceBtn.rect().right() + 4), ay = static_cast<float> (sourceBtn.rect().y + kCtlH / 2);
        p.line (ax, ay, ax + 16.0f, ay, 1.5f, p.c (T.keylineHi, 220));
        p.line (ax + 11.0f, ay - 4.0f, ax + 16.0f, ay, 1.5f, p.c (T.keylineHi, 220));
        p.line (ax + 11.0f, ay + 4.0f, ax + 16.0f, ay, 1.5f, p.c (T.keylineHi, 220));
        // a faint divider under the row
        p.line (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.bottom() - 1), static_cast<float> (rect_.right() - 8), static_cast<float> (rect_.bottom() - 1), 1.0f, p.c (T.keyline, 80));
    }
};


// ===============================================================================================================================================================
/** The page of the Modulation Matrix (1280x628) with filter tabs, a scrollable slot list and the Q group of the selected row. */
class ModMatrixView : public Composite
{
public:
    static constexpr int kMaxSlots = 64;
    static constexpr int kWindowRows = 8;
    static constexpr int kBarH = 64, kHeaderH = 22;
    static constexpr int kRowH = ModRow::kRowH; // 72
    static constexpr int kZoneW = 44, kThumbW = 24;

    enum FilterMode : uint8_t { FilterAll = 0, FilterSource, FilterDest };

    struct SlotEntry
    {
        char src[32] = {};
        char dst[40] = {};
        double amount = 0.0;
        bool bi = false;
        bool bypass = false;
        int srcGroup = 0;      // the group of the source (the owner's numbering: 0 none)
        int destModule = -1;
        bool hasLive = false;
        double liveVal = 0.0;
        bool active = false;
    };

    MenuButton tabAll { "ALL", MenuButton::Icon::None };
    MenuButton tabSource { "BY SOURCE", MenuButton::Icon::None };
    MenuButton tabDest { "BY DEST", MenuButton::Icon::None };
    MenuButton add { "+ ADD", MenuButton::Icon::None };

    Stepper sourceFilter;
    Stepper destFilter;

    ModRow windowRows[kWindowRows];
    ModSourceBoard board;   // the quick-add board: it takes the list's place while no slot is in use (the owner fills it with the sources)
    QGroup qgroup;

    ModMatrixView()
    {
        addKid (tabAll);
        addKid (tabSource);
        addKid (tabDest);
        addKid (add);
        addKid (sourceFilter);
        addKid (destFilter);
        for (int i = 0; i < kWindowRows; ++i)
            addKid (windowRows[i]);
        addKid (board, false);

        setFilterMode (FilterAll);
        initFilters();
    }

    Size minSize() const override { return { 1280, 628 }; }

    void setFilterMode (FilterMode m)
    {
        filterMode_ = m;
        show (sourceFilter, filterMode_ == FilterSource);
        show (destFilter, filterMode_ == FilterDest);
        rebuildVisibleList();
    }

    FilterMode filterMode() const noexcept { return filterMode_; }

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
                match = slots_[i].active && ((sourceFilterId_ <= 0) || (slots_[i].srcGroup == sourceFilterId_));
            else if (filterMode_ == FilterDest)
                match = slots_[i].active && ((destModuleFilterId_ < 0) || (slots_[i].destModule == destModuleFilterId_));
            if (match)
                visibleSlots_[visibleSlotCount_++] = i;
        }
        if (selectedRow_ >= visibleSlotCount_)
            selectedRow_ = std::max (0, visibleSlotCount_ - 1);
        firstRow_ = std::clamp (firstRow_, 0, maxScroll());

        show (board, totalActive_ == 0 && board.numSources() > 0);
        syncWindow();
        updateQGroup();
        layout();
    }

    /** True while the board of the sources is what the page shows: no slot is in use at all (with one in use the list is the page, whatever the filter says). */
    bool boardShown() const noexcept { return totalActive_ == 0 && board.numSources() > 0; }

    void selectRow (int idx)
    {
        if (visibleSlotCount_ <= 0)
        {
            selectedRow_ = -1;
            qgroup.setActive (false);
            return;
        }
        selectedRow_ = std::clamp (idx, 0, visibleSlotCount_ - 1);

        // Scroll into view if needed
        const int vRows = visibleRows();
        if (selectedRow_ < firstRow_)
            firstRow_ = selectedRow_;
        else if (selectedRow_ >= firstRow_ + vRows)
            firstRow_ = selectedRow_ - vRows + 1;

        syncWindow();
        updateQGroup();
        layout();
    }

    int selectedSlot() const noexcept
    {
        if (selectedRow_ >= 0 && selectedRow_ < visibleSlotCount_)
            return visibleSlots_[selectedRow_];
        return -1;
    }
    int selectedRow() const noexcept { return selectedRow_; }
    int firstRow() const noexcept { return firstRow_; }
    int activeSlots() const noexcept { return totalActive_; }

    void updateQGroup()
    {
        for (int k = 0; k < 4; ++k)
            qgroup.set (k, nullptr);

        const int wIdx = selectedRow_ - firstRow_;
        if (selectedRow_ < 0 || selectedRow_ >= visibleSlotCount_ || wIdx < 0 || wIdx >= visibleRows())
        {
            qgroup.setActive (false);
            return;
        }
        // the number of the selected row is the first member of the group: its chip is the first index, and the first Q-Link steps the row
        ModRowSelector& num = windowRows[wIdx].number;
        num.maxRows = visibleSlotCount_;
        num.selected = selectedRow_;
        qgroup.set (0, &num);
        qgroup.set (1, &windowRows[wIdx].sourceBtn);
        qgroup.set (2, &windowRows[wIdx].destBtn);
        qgroup.set (3, &windowRows[wIdx].amountBar);
        qgroup.setActive (true);
    }

    /** The data of slot `slot` (the owner calls it for all 64; an empty name is a slot that is not used): amount -1 .. +1, `srcGroup` the group of the source and `destModule` the module of the target (the filters), `liveVal` the live value of its source. */
    void setSlotData (int slot, const char* src, const char* dst, double amount, bool bi, bool bypassed, int srcGroup, int destModule, bool hasLive = false, double liveVal = 0.0)
    {
        if (slot < 0 || slot >= kMaxSlots) return;
        SlotEntry& e = slots_[slot];
        setText (e.src, sizeof e.src, src);
        setText (e.dst, sizeof e.dst, dst);
        e.amount = amount;
        e.bi = bi;
        e.bypass = bypassed;
        e.srcGroup = srcGroup;
        e.destModule = destModule;
        e.hasLive = hasLive;
        e.liveVal = liveVal;
        e.active = (src[0] != '\0' && dst[0] != '\0');
    }
    /** The owner is done with a round of setSlotData: the rows follow the data (the filter and the window), the list is rebuilt only when what it lists changed. */
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
        {
            syncWindow();
            updateQGroup();
        }
    }

    const SlotEntry& slotData (int slot) const noexcept { return slots_[std::clamp (slot, 0, kMaxSlots - 1)]; }

    int visibleSlotCount() const noexcept { return visibleSlotCount_; }

    int slotAtWindowIndex (int wIdx) const noexcept
    {
        const int vi = firstRow_ + wIdx;
        return (vi >= 0 && vi < visibleSlotCount_) ? visibleSlots_[vi] : -1;
    }

    Rect listRect() const noexcept { return { rect_.x, rect_.y + kBarH + kHeaderH, rect_.w, rect_.h - kBarH - kHeaderH - 4 }; }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (boardShown())
            return Composite::touchDown (x, y, nowMs);

        const Rect lr = listRect();
        const int listY = lr.y;
        if (y >= listY && y < lr.bottom() && x < rect_.right() - (scrolls() ? kZoneW : 0))
        {
            const int clickedRow = firstRow_ + (y - listY) / kRowH;
            if (clickedRow >= 0 && clickedRow < visibleSlotCount_)
                selectRow (clickedRow);
            dragStartY_ = y;
            startFirstRow_ = firstRow_;
            isDraggingList_ = true;
        }
        else if (scrolls() && x >= rect_.right() - kZoneW && y >= listY)
        {
            const Rect th = thumbRect();
            if (th.contains (x, y))
            {
                isDraggingThumb_ = true;
                dragStartY_ = y;
                startFirstRow_ = firstRow_;
            }
            else
            {
                if (y < th.y)
                    firstRow_ = std::max (0, firstRow_ - visibleRows());
                else
                    firstRow_ = std::min (maxScroll(), firstRow_ + visibleRows());
                syncWindow();
                updateQGroup();
                layout();
            }
        }

        return Composite::touchDown (x, y, nowMs);
    }

    Change touchMove (int x, int y) override
    {
        if (isDraggingList_ && std::abs (y - dragStartY_) > TapGesture::kSlop)
        {
            const int dy = y - dragStartY_;
            const int dRows = -dy / kRowH;
            const int newFirst = std::clamp (startFirstRow_ + dRows, 0, maxScroll());
            if (newFirst != firstRow_)
            {
                firstRow_ = newFirst;
                syncWindow();
                updateQGroup();
                layout();
            }
        }
        else if (isDraggingThumb_)
        {
            const int trackH = listRect().h - 8;
            const int dy = y - dragStartY_;
            const int n = std::max (1, visibleSlotCount_ - visibleRows());
            const int dRows = dy * n / std::max (1, trackH);
            const int newFirst = std::clamp (startFirstRow_ + dRows, 0, maxScroll());
            if (newFirst != firstRow_)
            {
                firstRow_ = newFirst;
                syncWindow();
                updateQGroup();
                layout();
            }
        }
        return Composite::touchMove (x, y);
    }

    Change touchUp (int x, int y) override
    {
        isDraggingList_ = false;
        isDraggingThumb_ = false;
        return Composite::touchUp (x, y);
    }

    /** The mouse wheel: a control of a row takes it as it is (a knob, a stepper); over the rest of the list it scrolls, one row a notch (the wheel up shows the earlier rows). */
    bool wheelOver (int x, int y, int notches, Change& out) override
    {
        if (Composite::wheelOver (x, y, notches, out))
            return true;
        if (! enabled_ || ! scrolls() || ! listRect().contains (x, y))
            return false;
        const int newFirst = std::clamp (firstRow_ - notches, 0, maxScroll());
        if (newFirst != firstRow_)
        {
            firstRow_ = newFirst;
            syncWindow();
            updateQGroup();
            layout();
        }
        out = Change::none();
        return true;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        qgroup.draw (sb);

        if (scrolls())
        {
            Pen p (sb, true);
            Cell cell (sb, zoneRect());
            const Rect z = zoneRect();
            p.rrect ({ z.x + z.w / 2 - kThumbW / 2, z.y, kThumbW, z.h }, 6.0f, p.c (theme::L().track, 120));   // the track and the thumb are as wide as a finger needs (24 px)
            p.rrect (thumbRect(), 6.0f, p.c (theme::L().keylineHi, 235));
        }
    }

    Change turnQ (int knobIndex, double delta)
    {
        const Change c = qgroup.turn (knobIndex, delta);
        if (knobIndex == 0 && c.kind == ChangeKind::Index)
            selectRow (c.index);
        return c;
    }

    int visibleRows() const noexcept { return std::min (kWindowRows, std::max (1, (listRect().h - 4) / kRowH)); }
    int maxScroll() const noexcept { return std::max (0, visibleSlotCount_ - visibleRows()); }
    bool scrolls() const noexcept { return visibleSlotCount_ > visibleRows(); }

    Rect zoneRect() const noexcept
    {
        const Rect lr = listRect();
        return { rect_.right() - kZoneW, lr.y + 4, kZoneW, lr.h - 8 };
    }

    Rect thumbRect() const noexcept
    {
        const Rect z = zoneRect();
        const int trackH = z.h;
        const int n = std::max (1, visibleSlotCount_);
        const int thumbH = std::max (28, trackH * visibleRows() / n);
        const int thumbY = (trackH - thumbH) * firstRow_ / std::max (1, n - visibleRows());
        return { z.x + kZoneW / 2 - kThumbW / 2, z.y + thumbY, kThumbW, thumbH };
    }

    void setSourceFilterChoice (int choice)
    {
        sourceFilterId_ = choice;
        rebuildVisibleList();
    }

    void setDestModuleFilter (int modId)
    {
        destModuleFilterId_ = modId;
        rebuildVisibleList();
    }

    void syncWindow()
    {
        const int vRows = visibleRows();
        for (int i = 0; i < kWindowRows; ++i)
        {
            const int vi = firstRow_ + i;
            if (i < vRows && vi < visibleSlotCount_)
            {
                const int s = visibleSlots_[vi];
                windowRows[i].setSlot (s);
                const SlotEntry& e = slots_[s];
                windowRows[i].updateData (e.src, e.dst, e.amount, e.bi, e.bypass, e.hasLive, e.hasLive ? e.amount * e.liveVal : 0.0);
                show (windowRows[i], true);
            }
            else
                show (windowRows[i], false);
        }
    }

protected:
    void layout() override
    {
        // top bar: the title, the filter tabs and the filter choice, the slot count and + ADD
        const int by = rect_.y + 4, bh = 56;
        tabAll.place ({ rect_.x + 280, by, 80, bh });
        tabSource.place ({ rect_.x + 368, by, 140, bh });
        tabDest.place ({ rect_.x + 516, by, 120, bh });
        if (filterMode_ == FilterSource)
            sourceFilter.place ({ rect_.x + 648, by, 260, bh });
        else if (filterMode_ == FilterDest)
            destFilter.place ({ rect_.x + 648, by, 260, bh });
        add.place ({ rect_.right() - 12 - 120, by, 120, bh });

        if (! (board.rect() == listRect()))
            board.place (listRect());
        const int vRows = visibleRows();
        const int listY = listRect().y;
        const int rowW = rect_.w - (scrolls() ? kZoneW : 12);
        for (int i = 0; i < kWindowRows; ++i)
            if (i < vRows && firstRow_ + i < visibleSlotCount_)
                windowRows[i].place ({ rect_.x, listY + i * kRowH, rowW, kRowH });
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        // the title, the slot count
        p.textLeft ({ rect_.x + 14, rect_.y + 4, 250, 56 }, "MOD MATRIX", T.fTitle, p.c (T.text, 255), T.fMin);
        char buf[32];
        std::snprintf (buf, sizeof buf, "%d / %d SLOTS", totalActive_, kMaxSlots);
        p.textRight ({ add.rect().x - 190, rect_.y + 4, 180, 56 }, buf, T.fLabel, p.c (T.textDim, 255), T.fMin);

        // the active filter tab
        const MenuButton* activeTab = (filterMode_ == FilterAll ? &tabAll : (filterMode_ == FilterSource ? &tabSource : &tabDest));
        p.rrect (activeTab->rect(), T.rControl, p.c (T.flow[0], 255), 1.6f);

        // the line under the bar and the names of the columns
        p.line (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.y + kBarH), static_cast<float> (rect_.right() - 8), static_cast<float> (rect_.y + kBarH), 1.5f, p.c (T.keyline, 255));
        if (visibleSlotCount_ > 0)
        {
            const ModRow& r = windowRows[0];
            const int hy = rect_.y + kBarH + 3, hh = kHeaderH - 4;
            p.textLeft ({ r.sourceBtn.rect().x + 4, hy, 160, hh }, "SOURCE", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
            p.textLeft ({ r.destBtn.rect().x + 4, hy, 160, hh }, "TARGET", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
            p.textLeft ({ r.amountBar.rect().x + 4, hy, 160, hh }, "AMOUNT", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
            p.textCentered ({ r.biToggle.rect().x, hy, r.biToggle.rect().w, hh }, "POLARITY", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
            p.textCentered ({ r.bypassToggle.rect().x, hy, r.bypassToggle.rect().w, hh }, "STATE", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }
        else if (totalActive_ > 0)   // slots exist, the filter hides them all (with none at all the board of the sources is the page)
        {
            const Rect lr = listRect();
            p.textCentered ({ lr.x, lr.y + lr.h / 2 - 40, lr.w, 40 }, "NO SLOT FOR THIS FILTER", T.fTitle, p.c (T.textDim, 255), T.fMin);
            p.textCentered ({ lr.x, lr.y + lr.h / 2 + 4, lr.w, 28 }, "TAP + ADD TO MAKE ONE", T.fLabel, p.c (T.textFaint, 255), T.fMin);
        }
    }

private:
    FilterMode filterMode_ = FilterAll;
    int sourceFilterId_ = 0;
    int destModuleFilterId_ = -1;

    SlotEntry slots_[kMaxSlots];
    int visibleSlots_[kMaxSlots + 1] = {};
    int visibleSlotCount_ = 0;
    int totalActive_ = 0;
    int listSig_ = -1;
    int firstRow_ = 0;
    int selectedRow_ = 0;

    bool isDraggingList_ = false;
    bool isDraggingThumb_ = false;
    int dragStartY_ = 0;
    int startFirstRow_ = 0;

    void initFilters()
    {
        sourceFilter.state.count = 7;
        sourceFilter.state.index = 0;
        sourceFilter.setItem ("All Sources");

        destFilter.state.count = 11;
        destFilter.state.index = 0;
        destFilter.setItem ("All Modules");
    }
};

}} // namespace synthyboy_mpc::ui
