// SynthyBoy - MPC overlay: the MACROS UI, the page of the Macros tab (the plugin draws the whole tab, page_view.h): the eight macros as cards (shell-mpc/ui/mod_macros.h) wired to the parameters macro.1 .. macro.8,
// the chips of what each drives (the modulation slots of the matrix whose source is that macro), the targets page of a macro (the same ModTargetsPage the envelope and the LFO have: depth, polarity, on / off, remove,
// ADD TARGET with the grouped list of ui_bind.h) and under the cards the live spectrum of the output (the curve of spectrum_curve.h: the strip the 32 meters of the host skin were).
// One object per (page view, plugin instance). Pure: no OS headers, no threads of its own.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Q groups: BANK 1 = the macros 1 .. 4 (the top row of cards), BANK 2 = the macros 5 .. 8; a tap on a card makes its row the group. On the targets page: the depth, the polarity and the switch of the active row.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../q_router.h"
#include "../ui/list.h"
#include "../ui/mod_macros.h"
#include "../ui/mod_targets.h"
#include "curve_source.h"
#include "page_view.h"
#include "q_publish.h"
#include "spectrum_curve.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class MacroUi
{
public:
    static constexpr int kElementPage = 8;           // the page of the Macros tab
    static constexpr int kElementMacros = kElementPage;
    enum Page { kPageTargets = 50, kPageList = 51 };

    enum Id : int
    {
        kBank1 = 20, kBank2 = 21,
        kMacroKnobBase = 100,    // 100 .. 107
        kMacroCardBase = 200,    // 200 .. 207: the chips of a card ask for the targets of the macro
        kTargetsBack = 300, kTargetsPage, kTargetsAdd, kTargetsRow = 310,   // 310 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kList = 400, kListBackdrop, kListHeader
    };
    using ModRow = uibind::ModRow;

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr double kModsRefreshMs = 100.0;

    MacroUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        assignIds();
        for (int i = 0; i < 8; ++i)
        {
            char key[16];
            std::snprintf (key, sizeof key, "macro.%d", i + 1);
            macroParams_[i] = uibind::indexOfKey (key);
            macroSource_[i] = uibind::macroSource (i + 1);
        }
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        view_.place (tab);
        targets_.place (tab);
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
        list_.setHeaderId (kListHeader);
    }

    /** Reads the instance: the eight macros, what each drives, the spectrum of the output, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input&)
    {
        const int top = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        if (top != pageTop_)
        {
            if (gesture_ != nullptr)
                dropGesture();
            if (top == kPageTargets)
                targetsPageIndex_ = 0;
            else if (top == kPageList)
                listGroup_ = -1;
            pageTop_ = top;
            qSlots_.forget();
            modsDirty_ = true;
            if (top == kPageList)
                fillList();
        }
        for (int m = 0; m < 8; ++m)
        {
            if (macroParams_[m] < 0)
                continue;
            const float v = inst.param (macroParams_[m]);
            char text[16];
            uibind::formatValue (uibind::info (macroParams_[m]), v, text, sizeof text);
            view_.cards[m].setValue (uibind::normOf (macroParams_[m], v), text);
            char name[16];
            std::snprintf (name, sizeof name, "MACRO %d", m + 1);
            view_.cards[m].setMacro (m, name);
        }
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshChips (inst);
            if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
                loadTargets (inst, false);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        updateSpectrum (inst, nowMs);
        inst.setUiFullOpen (true);
        publishQGroup (inst);
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        page::ground (sb);
        if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
            targets_.draw (sb);
        else
            view_.draw (sb);
        if (pageTop_ == kPageList)
            list_.draw (sb);
    }

    bool covers (int x, int y) const noexcept { return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y); }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        ui::Component* top = topComponent();
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            if (! covers (x, y) || ! top)
                return false;
            gesture_ = top;
            gestureDown_ = true;
            handle (top->touchDown (x, y, nowMs), stack, inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (x, y), stack, inst);
        else
        {
            handle (gesture_->touchUp (x, y), stack, inst);
            gestureDown_ = false;
            gesture_ = nullptr;
        }
        return true;
    }

    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst);
    }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    /** The page comes on screen again (another view was shown): it starts on BANK 1 (macros 1 .. 4) every time, so what the page shows and what the knobs write on arrival never depend on a bank left from an earlier visit; the group is published again. */
    void forgetQGroup() noexcept { view_.setBank (0); qSlots_.forget(); }

    void turnQ (int knobIndex, double delta, UiStack& stack, IInstanceView& inst)
    {
        handle (pageTop_ == kPageTargets ? targets_.turnQ (knobIndex, delta) : view_.turnQ (knobIndex, delta), stack, inst);
    }

    const int* qTargets() const noexcept { return qSlots_.data(); }
    const char* qGroupName() const noexcept { return qName_; }
    int topPage() const noexcept { return pageTop_; }
    /** The macro whose targets page is open (0 .. 7), -1 none. */
    int targetsOf() const noexcept { return pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets) ? targetMacro_ : -1; }

    ui::MacroView& view() noexcept { return view_; }
    ui::ModTargetsPage& targetsPage() noexcept { return targets_; }
    ui::ListPopup& list() noexcept { return list_; }
    int bank() const noexcept { return view_.bank(); }
    void setBank (int b) { view_.setBank (b); }
    int macroParam (int m) const noexcept { return m >= 0 && m < 8 ? macroParams_[m] : -1; }
    const SpectrumCurve* spectrum() const noexcept { return curve_.get(); }

private:
    ui::Component* topComponent() noexcept
    {
        if (pageTop_ == kPageList) return &list_;
        if (pageTop_ == kPageTargets) return &targets_;
        return &view_;
    }

    void assignIds()
    {
        view_.bank1Btn.setId (kBank1);
        view_.bank2Btn.setId (kBank2);
        for (int i = 0; i < 8; ++i)
        {
            view_.cards[i].knob.setId (kMacroKnobBase + i);
            view_.cards[i].setId (kMacroCardBase + i);
        }
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
    }

    /** The chips of the cards: the targets of the slots whose source is the macro (a slot that is switched off does not count). */
    void refreshChips (IInstanceView& inst)
    {
        for (int m = 0; m < 8; ++m)
        {
            int slots[uibind::kMaxTargetSlots];
            const int total = uibind::slotsOfSource (inst, macroSource_[m], slots, uibind::kMaxTargetSlots);
            const char* names[ui::MacroCard::kMaxChips];
            char text[ui::MacroCard::kMaxChips][20];
            int shown = 0, count = 0;
            for (int i = 0; i < total; ++i)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[i]);
                if (sp.bypass >= 0 && inst.param (sp.bypass) > 0.5f)
                    continue;
                ++count;
                const int p = synthyboy::modDestinationParamIndex (static_cast<size_t> (std::lround (inst.param (sp.destination))));
                if (p >= 0 && shown < ui::MacroCard::kMaxChips)
                {
                    names[shown] = chipName (p, text[shown]);
                    ++shown;
                }
            }
            view_.cards[m].setDestinations (names, shown, count);
        }
    }
    /** A short name of a target for a chip: the parameter's name without the words that every name of its group starts with ("Filter 1 Cutoff" -> "F1 Cutoff"). */
    static const char* chipName (int p, char* out)
    {
        std::string s = uibind::info (p).name;
        auto strip = [&] (const char* from, const char* to) { const size_t n = std::strlen (from); if (s.compare (0, n, from) == 0) s = std::string (to) + s.substr (n); };
        strip ("Filter 1 ", "F1 "); strip ("Filter 2 ", "F2 "); strip ("Osc 1 ", "A "); strip ("Osc 2 ", "B "); strip ("Osc 3 ", "C ");
        std::snprintf (out, 20, "%.19s", s.c_str());
        return out;
    }

    void updateSpectrum (IInstanceView& inst, double nowMs)
    {
        const SpectrumRing* ring = inst.spectrumRing();
        if (ring == nullptr)
        {
            view_.spectrum.levels = nullptr;
            return;
        }
        if (! curve_)
            curve_ = std::make_unique<SpectrumCurve> (inst.sampleRate());
        const double dt = lastSpectrumMs_ < 0.0 ? 33.3 : nowMs - lastSpectrumMs_;
        if (lastSpectrumMs_ >= 0.0 && dt < kSpectrumMinStepMs)
            return;
        if (lastSpectrumMs_ >= 0.0 && dt > 600.0)
            curve_->reset();
        lastSpectrumMs_ = nowMs;
        curve_->update (*ring, inst.sampleRate(), dt);
        view_.spectrum.levels = curve_->level();
    }

    // ---- the targets of one macro (the same page as the envelope's and the LFO's)
    ModRow targetRows_[ui::ModTargetsPage::kPerPage];
    int targetTotal_ = 0;
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        const int source = macroSource_[std::clamp (targetMacro_, 0, 7)];
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, source, slots, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + ui::ModTargetsPage::kPerPage - 1) / ui::ModTargetsPage::kPerPage);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * ui::ModTargetsPage::kPerPage, n = std::clamp (total - first, 0, ui::ModTargetsPage::kPerPage);
        ui::ModTargetsPage::Entry e[ui::ModTargetsPage::kPerPage];
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[first + i]);
            const int p = synthyboy::modDestinationParamIndex (static_cast<size_t> (std::lround (v.param (sp.destination))));
            std::snprintf (e[i].name, sizeof e[i].name, "%.39s", p >= 0 ? uibind::info (p).name : "-");
            e[i].depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            e[i].bidirectional = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            e[i].bypassed = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            targetRows_[i] = { slots[first + i], source };
        }
        bool same = ! fresh && n == targets_.numRows() && total == targetTotal_;
        for (int i = 0; same && i < n; ++i)
        {
            const ui::ModTargetsPage::Row& r = targets_.rows[i];
            same = std::strcmp (r.name, e[i].name) == 0 && std::fabs (r.depth.state.value - (e[i].depth + 1.0) * 0.5) < 1.0e-9 && r.polarity.state.on == e[i].bidirectional && r.bypass.state.on == ! e[i].bypassed;
        }
        if (same)
            return;
        targetTotal_ = total;
        char title[32];
        std::snprintf (title, sizeof title, "MACRO %d", targetMacro_ + 1);
        targets_.load (e, n, title, ui::theme::sourceColour ("MAC"), total, targetsPageIndex_);
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
    }

    // ---- the list that picks a new target: the groups of the destinations, then the parameters of a group
    void fillList()
    {
        const uibind::DestIndex& d = uibind::DestIndex::get();
        listNames_.clear();
        if (listGroup_ < 0)
        {
            for (const std::string& g : d.groups)
                listNames_.push_back (g.c_str());
            list_.state.header = nullptr;
            list_.state.headerBack = false;
        }
        else
        {
            for (int c : d.choices[static_cast<size_t> (listGroup_)])
                listNames_.push_back (uibind::info (synthyboy::modDestinationParamIndex (static_cast<size_t> (c))).name);
            list_.state.header = d.groups[static_cast<size_t> (listGroup_)].c_str();
            list_.state.headerBack = true;
        }
        list_.state.items = listNames_.data();
        list_.state.count = static_cast<int> (listNames_.size());
        list_.state.current = -1;
        list_.showCurrent();
    }
    void addTarget (IInstanceView& v, int destChoice)
    {
        const int source = macroSource_[std::clamp (targetMacro_, 0, 7)];
        if (destChoice <= 0 || source <= 0 || uibind::slotOfPair (v, source, destChoice) >= 0)
            return;   // that target is on the list already
        const int slot = uibind::freeSlot (v);
        if (slot < 0)
            return;
        const int p = synthyboy::modDestinationParamIndex (static_cast<size_t> (destChoice));
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        uibind::writePlain (v, sp.destination, destChoice);
        uibind::writePlain (v, sp.amount, 50.0);
        if (sp.bypass >= 0) uibind::writePlain (v, sp.bypass, 0.0);
        if (sp.bipolar >= 0) uibind::writePlain (v, sp.bipolar, p >= 0 && uibind::info (p).min < 0.0f && uibind::info (p).max > 0.0f ? 1.0 : 0.0);
        uibind::writePlain (v, sp.source, source);
        modsDirty_ = true;
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, source, slots, uibind::kMaxTargetSlots);
        targetsPageIndex_ = std::max (0, (total - 1) / ui::ModTargetsPage::kPerPage);   // the page that shows the new row
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& v)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        const int id = c.id;
        if (c.kind == K::Request)
        {
            if (id == kTargetsBack || id == kListBackdrop)
            {
                stack.back();
                return;
            }
            if (id == kTargetsAdd)
            {
                underList_ = kPageTargets;
                stack.openDetail (kPageList);
                return;
            }
            if (id == kListHeader)
            {
                listGroup_ = -1;
                fillList();
                return;
            }
            if (id >= kMacroCardBase && id < kMacroCardBase + 8)   // the chips of a card: the targets of the macro
            {
                targetMacro_ = id - kMacroCardBase;
                stack.openDetail (kPageTargets);
                return;
            }
            if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage && (id - kTargetsRow) % 4 == 3)
            {
                const int slot = targetRows_[(id - kTargetsRow) / 4].slot;
                if (slot >= 0)
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
                    uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
                    modsDirty_ = true;
                    loadTargets (v, true);
                }
                return;
            }
            return;
        }
        if (id == kBank1) { view_.setBank (0); qSlots_.forget(); return; }
        if (id == kBank2) { view_.setBank (1); qSlots_.forget(); return; }
        if (id == kList && c.kind == K::Index)
        {
            if (listGroup_ < 0)
            {
                listGroup_ = c.index;
                fillList();
            }
            else
            {
                const std::vector<int>& ch = uibind::DestIndex::get().choices[static_cast<size_t> (listGroup_)];
                if (c.index >= 0 && c.index < static_cast<int> (ch.size()))
                    addTarget (v, ch[static_cast<size_t> (c.index)]);
                stack.back();
            }
            return;
        }
        if (id == kTargetsPage && c.kind == K::Index)
        {
            targetsPageIndex_ = c.index;
            loadTargets (v, true);
            return;
        }
        if (id >= kMacroKnobBase && id < kMacroKnobBase + 8)
        {
            const int m = id - kMacroKnobBase;
            if (c.kind == K::Value && macroParams_[m] >= 0)
                uibind::writeNorm (v, macroParams_[m], c.value);
            return;
        }
        if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage)
        {
            const int slot = targetRows_[(id - kTargetsRow) / 4].slot;
            if (slot < 0)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
            switch ((id - kTargetsRow) % 4)
            {
                case 0: if (c.kind == K::Value) uibind::writeNorm (v, sp.amount, c.value); break;
                case 1: if (c.kind == K::Index) uibind::writePlain (v, sp.bipolar, c.index != 0 ? 1.0 : 0.0); break;
                case 2: if (c.kind == K::Index) uibind::writePlain (v, sp.bypass, c.index != 0 ? 0.0 : 1.0); break;   // the switch says ON (active): bypassed is its opposite
                default: break;
            }
            modsDirty_ = true;
        }
    }

    /** The groups of the page: the two banks of four macros (MACROS 1-4, MACROS 5-8) in reading order, the shown bank on Q 1 .. 4 and the other one on 5 .. 8; a targets list has one group per row. */
    void publishQGroup (IInstanceView& v)
    {
        if (pageTop_ == kPageTargets)
        {
            std::snprintf (qName_, sizeof qName_, "MACRO %d TARGETS", targetMacro_ + 1);
            qSlots_.publish (v, targets_.numRows(), targets_.activeRow(), [this] (int row, int* t) { modRowTargets (row < ui::ModTargetsPage::kPerPage ? targetRows_[row].slot : -1, t); });
            setRowBlocks (targets_, ui::ModTargetsPage::kPerPage, targets_.numRows(), targets_.activeRow());
        }
        else if (pageTop_ == kPageList)
        {
            std::snprintf (qName_, sizeof qName_, "LIST");
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else
        {
            std::snprintf (qName_, sizeof qName_, view_.bank() == 0 ? "MACROS 1-4" : "MACROS 5-8");
            qSlots_.publish (v, 2, view_.bank(), [this] (int bank, int* t) { for (int k = 0; k < 4; ++k) t[k] = macroParams_[bank * 4 + k]; });
        }
    }

    static constexpr double kSpectrumMinStepMs = 16.0;
    int w_ = kCanvasW, h_ = kCanvasH;
    int pageTop_ = 0, underList_ = 0, listGroup_ = -1, targetMacro_ = 0, targetsPageIndex_ = 0;
    int macroParams_[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    int macroSource_[8] = {};
    std::vector<const char*> listNames_;

    ui::MacroView view_;
    ui::ModTargetsPage targets_;
    ui::ListPopup list_;
    std::unique_ptr<SpectrumCurve> curve_;
    double lastSpectrumMs_ = -1.0, modsAtMs_ = -1000.0;
    bool modsDirty_ = true;

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    QSlots qSlots_;
    char qName_[32] = "MACROS 1-4";
};

}} // namespace synthyboy_mpc::overlay
