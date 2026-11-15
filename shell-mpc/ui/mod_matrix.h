// SynthyBoy - MPC UI: Modulation Matrix View
// Full-screen scrollable matrix view (1280x628) showing modulation slots.
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
#include "qgroup.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** Two-way horizontal amount bar with center 0, static deflection bar, thin live needle, and numeric text readout.
    Touch/drag adjusts amount (-1.0 .. +1.0); double tap resets to 0.0; turn (delta) turns Q-Link knob. */
class ModAmountBar : public Component
{
public:
    struct State
    {
        double amount = 0.0;     // -1.0 .. +1.0
        double liveAmount = 0.0; // static amount * source live value (-1.0 .. +1.0)
        bool hasLive = false;
        char valueText[16] = {}; // e.g. "+50%", "-100%"
    };
    State state;

    ModAmountBar() { formatValue(); }

    Size minSize() const override { return { 160, 48 }; }

    void setAmount (double a)
    {
        state.amount = std::clamp (a, -1.0, 1.0);
        formatValue();
    }

    void setLive (double sVal)
    {
        state.hasLive = true;
        state.liveAmount = std::clamp (state.amount * sVal, -1.0, 1.0);
    }

    void clearLive() noexcept { state.hasLive = false; }

    void formatValue()
    {
        const int pct = static_cast<int> (std::lround (state.amount * 100.0));
        if (pct > 0)
            std::snprintf (state.valueText, sizeof state.valueText, "+%d%%", pct);
        else if (pct < 0)
            std::snprintf (state.valueText, sizeof state.valueText, "%d%%", pct);
        else
            std::snprintf (state.valueText, sizeof state.valueText, "0%%");
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (! enabled_) return Change::none();
        tap_.down (x, y, nowMs);
        dragStartX_ = x;
        startAmount_ = state.amount;
        return Change::none();
    }

    Change touchMove (int x, int y) override
    {
        if (! enabled_) return Change::none();
        tap_.move (x, y);
        // 160 px drag = 1.0 range change
        const double delta = static_cast<double> (x - dragStartX_) / 160.0;
        const double newAmount = std::clamp (startAmount_ + delta, -1.0, 1.0);
        if (std::fabs (newAmount - state.amount) > 0.002)
        {
            state.amount = newAmount;
            formatValue();
            return Change::normalised (id_, (state.amount + 1.0) * 0.5);
        }
        return Change::none();
    }

    Change touchUp (int x, int y) override
    {
        (void) x; (void) y;
        if (! enabled_) return Change::none();
        if (tap_.up() == 2)
        {
            state.amount = 0.0;
            formatValue();
            return Change::normalised (id_, 0.5);
        }
        return Change::none();
    }

    Change turn (double delta) override
    {
        if (! enabled_) return Change::none();
        const double newAmount = std::clamp (state.amount + delta * 0.02, -1.0, 1.0);
        state.amount = newAmount;
        formatValue();
        return Change::normalised (id_, (state.amount + 1.0) * 0.5);
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.control (rect_, qFrame_);

        const Rect inner = rect_.inset (4);
        if (inner.empty()) return;

        const int cx = inner.x + inner.w / 2;
        const int cy = inner.y + inner.h / 2;

        // Background track line
        p.line (static_cast<float> (inner.x + 4), static_cast<float> (cy),
                static_cast<float> (inner.right() - 4), static_cast<float> (cy),
                1.5f, p.c (T.track, 180));

        // Center line
        p.line (static_cast<float> (cx), static_cast<float> (inner.y + 4),
                static_cast<float> (cx), static_cast<float> (inner.bottom() - 4),
                1.5f, p.c (T.keylineHi, 220));

        // Deflection bar
        if (std::fabs (state.amount) > 0.005)
        {
            const int barW = static_cast<int> ((inner.w / 2 - 8) * std::fabs (state.amount));
            const int x0 = state.amount > 0 ? cx : cx - barW;
            const uint32_t col = state.amount > 0 ? T.flow[0] : T.negative;
            p.rrect ({ x0, inner.y + 6, std::max (2, barW), std::max (4, inner.h - 12) }, 2.0f, p.c (col, 200));
        }

        // Live needle marker
        if (state.hasLive)
        {
            const int lx = cx + static_cast<int> ((inner.w / 2 - 8) * state.liveAmount);
            p.line (static_cast<float> (lx), static_cast<float> (inner.y + 2),
                    static_cast<float> (lx), static_cast<float> (inner.bottom() - 2),
                    2.0f, p.c (T.text, 255));
        }

        // Value text
        p.textCentered (inner, state.valueText, T.fLabel, p.c (T.text, 255), T.fMin);
    }

private:
    TapGesture tap_;
    int dragStartX_ = 0;
    double startAmount_ = 0.0;
};


// ===============================================================================================================================================================
/** Row selection / stepping helper component for Q4 knob in matrix view. */
class ModRowSelector : public Component
{
public:
    int selected = 0;
    int maxRows = 0;

    ModRowSelector() = default;
    Size minSize() const override { return { 40, 48 }; }

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
        (void) p; // Visuals handled by row number
    }
};


// ===============================================================================================================================================================
/** Single modulation slot row (height 64 px):
    # | SOURCE | -> | DESTINATION | AMOUNT | UNI/BI | ON/OFF | ... (details)
    or dimmed "+ ADD" row. */
class ModRow : public Composite
{
public:
    static constexpr int kRowH = 64;

    int slotIndex = 0;       // 0 .. 63
    bool isAddRow = false;
    bool selected = false;
    bool active = false;

    char slotText[8] = {};
    char sourceName[32] = {};
    char destName[32] = {};

    MenuButton sourceBtn { "---", MenuButton::Icon::Down };
    MenuButton destBtn { "---", MenuButton::Icon::Down };
    ModAmountBar amountBar;
    MenuButton biToggle { "UNI", MenuButton::Icon::None };
    MenuButton bypassToggle { "ON", MenuButton::Icon::None };
    MenuButton detailBtn { "...", MenuButton::Icon::None };
    MenuButton addBtn { "+ ADD MODULATION", MenuButton::Icon::None };

    ModRow()
    {
        addKid (sourceBtn);
        addKid (destBtn);
        addKid (amountBar);
        addKid (biToggle);
        addKid (bypassToggle);
        addKid (detailBtn);
        addKid (addBtn, false);
    }

    Size minSize() const override { return { 600, kRowH }; }

    void setSlot (int idx, bool addRow)
    {
        slotIndex = idx;
        isAddRow = addRow;
        std::snprintf (slotText, sizeof slotText, "%02d", idx + 1);

        show (sourceBtn, ! isAddRow);
        show (destBtn, ! isAddRow);
        show (amountBar, ! isAddRow);
        show (biToggle, ! isAddRow);
        show (bypassToggle, ! isAddRow);
        show (detailBtn, ! isAddRow);
        show (addBtn, isAddRow);
        layout();
    }

    void updateData (const char* src, const char* dst, double amount, bool bi, bool bypassed, bool hasLiveVal = false, double liveVal = 0.0)
    {
        setText (sourceName, sizeof sourceName, src);
        setText (destName, sizeof destName, dst);
        sourceBtn.setLabel (src[0] != '\0' ? src : "---");
        destBtn.setLabel (dst[0] != '\0' ? dst : "---");
        amountBar.setAmount (amount);
        if (hasLiveVal)
            amountBar.setLive (liveVal);
        else
            amountBar.clearLive();
        biToggle.setLabel (bi ? "BI" : "UNI");
        bypassToggle.setLabel (bypassed ? "OFF" : "ON");
        active = (src[0] != '\0' && dst[0] != '\0');
    }

protected:
    void layout() override
    {
        if (isAddRow)
        {
            addBtn.place ({ rect_.x + 48, rect_.y + 4, rect_.w - 96, kRowH - 8 });
            return;
        }

        // Layout across available width (touch targets >= 56 px)
        const int y = rect_.y + 4;
        const int h = kRowH - 8; // 56 px high
        int x = rect_.x + 48;

        sourceBtn.place ({ x, y, 170, h });
        x += 170 + 30; // 30 px for arrow ->
        destBtn.place ({ x, y, 230, h });
        x += 230 + 12;
        amountBar.place ({ x, y, 260, h });
        x += 260 + 12;
        biToggle.place ({ x, y, 64, h });
        x += 64 + 10;
        bypassToggle.place ({ x, y, 64, h });
        x += 64 + 10;
        detailBtn.place ({ x, y, 56, h });
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (isAddRow) return;

        // Slot number #
        p.textCentered ({ rect_.x + 4, rect_.y + 4, 40, kRowH - 8 }, slotText, T.fLabel, p.c (T.textDim, 200), T.fMin);

        // Arrow between source and dest
        const int ax = rect_.x + 48 + 170 + 4;
        p.line (static_cast<float> (ax), static_cast<float> (rect_.y + kRowH / 2),
                static_cast<float> (ax + 18), static_cast<float> (rect_.y + kRowH / 2),
                1.5f, p.c (T.keylineHi, 220));
        p.line (static_cast<float> (ax + 13), static_cast<float> (rect_.y + kRowH / 2 - 4),
                static_cast<float> (ax + 18), static_cast<float> (rect_.y + kRowH / 2),
                1.5f, p.c (T.keylineHi, 220));
        p.line (static_cast<float> (ax + 13), static_cast<float> (rect_.y + kRowH / 2 + 4),
                static_cast<float> (ax + 18), static_cast<float> (rect_.y + kRowH / 2),
                1.5f, p.c (T.keylineHi, 220));

        // Subtle row bottom divider
        p.line (static_cast<float> (rect_.x + 12), static_cast<float> (rect_.bottom() - 1),
                static_cast<float> (rect_.right() - 12), static_cast<float> (rect_.bottom() - 1),
                1.0f, p.c (T.keyline, 80));
    }
};


// ===============================================================================================================================================================
/** Full-screen Modulation Matrix View (1280x628) with filter tabs, inertia-free scrollable slot list, and Q-Link group. */
class ModMatrixView : public Composite
{
public:
    static constexpr int kMaxSlots = 64;
    static constexpr int kWindowRows = 10;
    static constexpr int kBarH = 56;
    static constexpr int kRowH = ModRow::kRowH; // 64
    static constexpr int kZoneW = 44;

    enum FilterMode : uint8_t { FilterAll = 0, FilterSource, FilterDest };

    struct SlotEntry
    {
        char src[32] = {};
        char dst[32] = {};
        double amount = 0.0;
        bool bi = false;
        bool bypass = false;
        int srcChoice = 0;
        int destModule = -1;
        bool hasLive = false;
        double liveVal = 0.0;
        bool active = false;
    };

    MenuButton close { "CLOSE", MenuButton::Icon::None };
    MenuButton tabAll { "ALL", MenuButton::Icon::None };
    MenuButton tabSource { "BY SOURCE", MenuButton::Icon::None };
    MenuButton tabDest { "BY DEST", MenuButton::Icon::None };

    Stepper sourceFilter;
    Stepper destFilter;

    ModRow windowRows[kWindowRows];
    ModRowSelector rowSelector;
    QGroup qgroup;

    ModMatrixView()
    {
        addKid (close);
        addKid (tabAll);
        addKid (tabSource);
        addKid (tabDest);
        addKid (sourceFilter);
        addKid (destFilter);
        addKid (rowSelector);

        for (int i = 0; i < kWindowRows; ++i)
        {
            addKid (windowRows[i]);
        }

        setFilterMode (FilterAll);
        initFilters();
    }

    Size minSize() const override { return { 800, 400 }; }

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
        for (int i = 0; i < kMaxSlots; ++i)
        {
            bool match = false;
            if (filterMode_ == FilterAll)
            {
                match = slots_[i].active;
            }
            else if (filterMode_ == FilterSource)
            {
                match = (sourceFilterId_ <= 0) || (slots_[i].srcChoice == sourceFilterId_);
            }
            else if (filterMode_ == FilterDest)
            {
                match = (destModuleFilterId_ < 0) || (slots_[i].destModule == destModuleFilterId_);
            }

            if (match)
            {
                visibleSlots_[visibleSlotCount_++] = i;
            }
        }

        // Add the "+ ADD" row at the end if room
        if (filterMode_ == FilterAll && visibleSlotCount_ < kMaxSlots)
        {
            visibleSlots_[visibleSlotCount_++] = kMaxSlots; // add row
        }

        rowSelector.maxRows = visibleSlotCount_;
        if (selectedRow_ >= visibleSlotCount_)
            selectedRow_ = std::max (0, visibleSlotCount_ - 1);
        rowSelector.selected = selectedRow_;

        syncWindow();
        updateQGroup();
        layout();
    }

    void selectRow (int idx)
    {
        if (visibleSlotCount_ <= 0)
        {
            selectedRow_ = -1;
            qgroup.setActive (false);
            return;
        }
        selectedRow_ = std::clamp (idx, 0, visibleSlotCount_ - 1);
        rowSelector.selected = selectedRow_;

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

    void updateQGroup()
    {
        for (int k = 0; k < 4; ++k)
            qgroup.set (k, nullptr);

        if (selectedRow_ < 0 || selectedRow_ >= visibleSlotCount_)
        {
            qgroup.setActive (false);
            return;
        }

        const int wIdx = selectedRow_ - firstRow_;
        if (wIdx >= 0 && wIdx < kWindowRows && visibleSlots_[selectedRow_] < kMaxSlots)
        {
            qgroup.set (0, &windowRows[wIdx].amountBar);
            qgroup.set (1, &windowRows[wIdx].sourceBtn);
            qgroup.set (2, &windowRows[wIdx].destBtn);
            qgroup.set (3, &rowSelector);
            qgroup.setActive (true);
        }
        else
        {
            qgroup.setActive (false);
        }
    }

    void setSlotData (int slot, const char* src, const char* dst, double amount, bool bi, bool bypassed, int srcChoice, int destModule, bool hasLive = false, double liveVal = 0.0)
    {
        if (slot < 0 || slot >= kMaxSlots) return;
        SlotEntry& e = slots_[slot];
        setText (e.src, sizeof e.src, src);
        setText (e.dst, sizeof e.dst, dst);
        e.amount = amount;
        e.bi = bi;
        e.bypass = bypassed;
        e.srcChoice = srcChoice;
        e.destModule = destModule;
        e.hasLive = hasLive;
        e.liveVal = liveVal;
        e.active = (src[0] != '\0' && dst[0] != '\0');
    }

    const SlotEntry& slotData (int slot) const noexcept { return slots_[std::clamp (slot, 0, kMaxSlots - 1)]; }

    int visibleSlotCount() const noexcept { return visibleSlotCount_; }

    int slotAtWindowIndex (int wIdx) const noexcept
    {
        const int vi = firstRow_ + wIdx;
        return (vi >= 0 && vi < visibleSlotCount_) ? visibleSlots_[vi] : -1;
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        const int listY = rect_.y + kBarH;
        if (y >= listY && y < rect_.bottom() && x < rect_.right() - kZoneW)
        {
            const int clickedRow = firstRow_ + (y - listY) / kRowH;
            if (clickedRow >= 0 && clickedRow < visibleSlotCount_)
            {
                selectRow (clickedRow);
            }
            dragStartY_ = y;
            startFirstRow_ = firstRow_;
            isDraggingList_ = true;
        }
        else if (scrolls() && x >= rect_.right() - kZoneW)
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
                layout();
            }
        }

        return Composite::touchDown (x, y, nowMs);
    }

    Change touchMove (int x, int y) override
    {
        if (isDraggingList_)
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
            const int trackH = rect_.h - kBarH - 8;
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

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        qgroup.draw (sb);

        if (scrolls())
        {
            Pen p (sb, true);
            const Rect z = zoneRect();
            p.rect (z.x + z.w / 2 - 1, z.y, z.x + z.w / 2 + 1, z.bottom(), p.c (theme::L().track, 120));
            p.rrect (thumbRect(), 3.0f, p.c (theme::L().keylineHi, 220));
        }
    }

    Change turnQ (int knobIndex, double delta)
    {
        const Change c = qgroup.turn (knobIndex, delta);
        if (knobIndex == 3 && c.kind == ChangeKind::Index)
        {
            selectRow (c.index);
        }
        return c;
    }

    int visibleRows() const noexcept { return std::min (kWindowRows, std::max (1, (rect_.h - kBarH - 8) / kRowH)); }
    int maxScroll() const noexcept { return std::max (0, visibleSlotCount_ - visibleRows()); }
    bool scrolls() const noexcept { return visibleSlotCount_ > visibleRows(); }

    Rect zoneRect() const noexcept
    {
        return { rect_.right() - kZoneW, rect_.y + kBarH + 4, kZoneW, rect_.h - kBarH - 8 };
    }

    Rect thumbRect() const noexcept
    {
        const int trackH = rect_.h - kBarH - 8;
        const int n = std::max (1, visibleSlotCount_);
        const int thumbH = std::max (28, trackH * visibleRows() / n);
        const int thumbY = (trackH - thumbH) * firstRow_ / std::max (1, n - visibleRows());
        return { rect_.right() - kZoneW / 2 - 5, rect_.y + kBarH + 4 + thumbY, 10, thumbH };
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
                if (s == kMaxSlots)
                {
                    windowRows[i].setSlot (s, true);
                }
                else
                {
                    windowRows[i].setSlot (s, false);
                    const SlotEntry& e = slots_[s];
                    windowRows[i].updateData (e.src, e.dst, e.amount, e.bi, e.bypass, e.hasLive, e.liveVal);
                }
                show (windowRows[i], true);
            }
            else
            {
                show (windowRows[i], false);
            }
        }
    }

protected:
    void layout() override
    {
        // Top bar
        close.place ({ rect_.x + 12, rect_.y + 4, 100, kBarH - 8 });
        tabAll.place ({ rect_.x + 280, rect_.y + 4, 80, kBarH - 8 });
        tabSource.place ({ rect_.x + 368, rect_.y + 4, 130, kBarH - 8 });
        tabDest.place ({ rect_.x + 506, rect_.y + 4, 120, kBarH - 8 });

        if (filterMode_ == FilterSource)
            sourceFilter.place ({ rect_.x + 638, rect_.y + 4, 200, kBarH - 8 });
        else if (filterMode_ == FilterDest)
            destFilter.place ({ rect_.x + 638, rect_.y + 4, 200, kBarH - 8 });

        const int vRows = visibleRows();
        const int listY = rect_.y + kBarH + 4;
        const int rowW = rect_.w - (scrolls() ? kZoneW : 12);

        for (int i = 0; i < kWindowRows; ++i)
        {
            if (i < vRows && firstRow_ + i < visibleSlotCount_)
            {
                const int y = listY + i * kRowH;
                windowRows[i].place ({ rect_.x, y, rowW, kRowH });
            }
        }
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        // Top bar title
        p.textLeft ({ rect_.x + 124, rect_.y + 4, 140, kBarH - 8 }, "MOD MATRIX", T.fTitle, p.c (T.text, 255), T.fMin);

        // Highlight active filter tab
        const MenuButton* activeTab = (filterMode_ == FilterAll ? &tabAll : (filterMode_ == FilterSource ? &tabSource : &tabDest));
        p.rrect (activeTab->rect(), T.rControl, p.c (T.flow[0], 255), 1.6f);

        // Header separator line
        p.line (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.y + kBarH - 1),
                static_cast<float> (rect_.right() - 8), static_cast<float> (rect_.y + kBarH - 1),
                1.5f, p.c (T.keyline, 255));
    }

private:
    FilterMode filterMode_ = FilterAll;
    int sourceFilterId_ = 0;
    int destModuleFilterId_ = -1;

    SlotEntry slots_[kMaxSlots];
    int visibleSlots_[kMaxSlots + 1] = {};
    int visibleSlotCount_ = 0;
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
