// SynthyBoy - MPC overlay: the MODULATION MATRIX UI, the page of the Mod tab (the plugin draws the whole tab, page_view.h): the 64 slots of the matrix (shell-mpc/ui/mod_matrix.h) and the page of one slot
// (mod_detail.h) wired to the engine parameters (modNN.* of the table, docs/MPC.md "Modulation UI"). One object per (page view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls
// update () / touch () / tick () / draw (), with the instance as IInstanceView.
// While no slot is in use the matrix has no rows to show: the page is the quick-add BOARD instead (ui/mod_board.h): every source the engine has, as chips in groups. A tap on a chip opens the targets page of
// that source (ui/mod_targets.h, the page of the envelope, the LFO and the macros: "where does this source go") with the grouped list of the targets on top of it; the target that is picked makes the slot, and
// the page shows it. BACK returns to the matrix, which lists the slot (the board is gone as soon as one slot is in use).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementPage): the matrix is the page; above it the details of a slot (a page over the whole tab, BACK returns) and the list popup that picks a
// source, a target or the auxiliary source of a slot. The lists are grouped like the ADD TARGET list of the envelope and the LFO (one source of the groups: ui_bind.h DestIndex): the first level has the
// groups, a tap on a group shows its entries (the header of the list is the way back). The Q group of the matrix is the selected row: AMOUNT, SOURCE, TARGET and the row (the fourth Q-Link steps through the rows:
// a virtual Q target, q_router.h).
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../q_router.h"
#include "../ui/list.h"
#include "../ui/mod_detail.h"
#include "../ui/mod_matrix.h"
#include "../ui/mod_targets.h"
#include "instance_view.h"
#include "page_view.h"
#include "q_publish.h"
#include "q_publish_mod.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class ModUi
{
public:
    static constexpr int kElementPage = 7;          // the page of the Mod tab (the oscillator is 1, the envelope 2, the LFO 3, the filter 4)
    static constexpr int kElementModMatrix = kElementPage;
    enum Page { kPageDetail = 20, kPageList = 21, kPageTargets = 22 };

    enum Id : int
    {
        kFilterAll = 20, kFilterSource = 21, kFilterDest = 22,
        kSourceFilterStep = 25, kDestFilterStep = 26,
        kAdd = 30, kBoard = 35,
        kDetailBack = 40, kDetailDelete = 41,
        kListPopup = 60, kListBackdrop = 61, kListHeader = 62,
        // the rows of the matrix: kRowBase + window row * 10 + offset; the row of the details page: kDetailRowBase + offset
        kRowBase = 100,
        kOffsetNumber = 0, kOffsetSource = 1, kOffsetDest = 2, kOffsetAmount = 3, kOffsetBi = 4, kOffsetBypass = 5, kOffsetDetail = 6,
        kDetailRowBase = 300,
        kTargetsBack = 330, kTargetsPage, kTargetsAdd, kTargetsRow = 340,   // the targets page of a source: kTargetsRow + 4 * row + (0 depth, 1 polarity, 2 on / off, 3 remove)
        kDetailAuxSource = 800, kDetailAuxOutput, kDetailAuxInv, kDetailAuxPower, kDetailPower, kDetailStereo, kDetailRise, kDetailFall, kDetailDelay, kDetailDelaySync, kDetailDelayBeats
    };
    enum class ListKind : uint8_t { Source, Dest, Aux, NewTarget };   // NewTarget: the target of a new slot of the source on the targets page

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kVirtualRow = 0;   // the virtual Q target that steps through the rows of the matrix

    ModUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        assignIds();
        for (int g = 0; g <= uibind::kSourceGroups; ++g)
            srcGroupNames_[g] = uibind::sourceGroupName (g);
        fillBoard();
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        matrix_.place (tab);
        detail_.place (tab);
        targets_.place (tab);
        list_.place ({ w / 2 - 260, kTabY + 20, 520, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kListPopup);
        list_.setBackdropId (kListBackdrop);
        list_.setHeaderId (kListHeader);
    }

    /** Reads the instance: the 64 slots, the page of the open slot, the live values of the sources, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        (void) nowMs;
        {
            const auto content = inst.content();   // the names of the macros: in the rows, the board and the pickers
            if (macroNames_.load (content.get()))
                namesDirty_ = true;
        }
        const NamesScope namesScope (macroNames_);
        if (namesDirty_)
        {
            namesDirty_ = false;
            fillBoard();
        }
        pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        if (pageTop_ != kPageDetail && pageTop_ != kPageList && pageTop_ != kPageTargets)
            pageTop_ = 0;
        if (pageTop_ == 0 && openSlot_ >= 0 && ! gestureDown_)
            openSlot_ = -1;

        const synthyboy::LiveValues* live = in.live;
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source < 0 || sp.destination < 0)
                continue;
            const int srcVal = static_cast<int> (std::lround (inst.param (sp.source)));
            const int dstVal = static_cast<int> (std::lround (inst.param (sp.destination)));
            const double amount = static_cast<double> (inst.param (sp.amount)) / 100.0;
            const bool bi = sp.bipolar >= 0 && inst.param (sp.bipolar) > 0.5f, byp = sp.bypass >= 0 && inst.param (sp.bypass) > 0.5f;
            double liveSource = 0.0;
            if (live != nullptr && srcVal >= 1 && srcVal <= synthyboy::LiveValues::kMaxSources)
                liveSource = static_cast<double> (live->source[srcVal - 1]);
            matrix_.setSlotData (s, sourceName (srcVal), destName (dstVal), amount, bi, byp, uibind::sourceGroup (srcVal), destModuleOf (dstVal), live != nullptr && srcVal > 0, liveSource);
        }
        matrix_.refresh();

        if (openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots && (pageTop_ == kPageDetail || (pageTop_ == kPageList && listKind_ != ListKind::NewTarget)))
            pullDetail (inst, live);
        if (targetsShown())
            loadTargets (inst, false);

        // the virtual Q target of the rows: a write of the host moves the selected row
        float x;
        if (pageTop_ == 0 && inst.takeQVirtual (kVirtualRow, x) && matrix_.visibleSlotCount() > 0)
            matrix_.selectRow (static_cast<int> (std::lround (x * static_cast<float> (std::max (1, matrix_.visibleSlotCount() - 1)))));
        inst.setQVirtual (kVirtualRow, matrix_.visibleSlotCount() > 1 ? static_cast<float> (matrix_.selectedRow()) / static_cast<float> (matrix_.visibleSlotCount() - 1) : 0.0f);

        inst.setUiFullOpen (true);
        publishQGroup (inst);
    }

    /** The whole picture: the page and what lies over it. */
    void draw (gpu::SceneBuilder& sb) const
    {
        page::ground (sb);
        if (targetsShown())
            targets_.draw (sb);
        else if (pageTop_ == kPageDetail || (pageTop_ == kPageList && openSlot_ >= 0))
            detail_.draw (sb);
        else
            matrix_.draw (sb);
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

    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the control of the page under the pointer takes it as steps of its own (a knob: one Q-Link step, a stepper, a list, a scrolled
        list; ui::Component::wheelOver). false: nothing of this page lies there, or a finger is down on it. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        ui::Component* top = topComponent();
        if (gestureDown_ || ! top || ! covers (x, y))
            return false;
        ui::Change ch;
        if (! top->wheelOver (x, y, notches, ch))
            return false;
        handle (ch, stack, inst);
        (void) nowMs;
        return true;
    }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    void forgetQGroup() noexcept { qSlots_.forget(); }

    void turnQ (int knobIndex, double delta, UiStack& stack, IInstanceView& inst)
    {
        handle (pageTop_ == kPageDetail ? detail_.turnQ (knobIndex, delta) : (pageTop_ == kPageTargets ? targets_.turnQ (knobIndex, delta) : matrix_.turnQ (knobIndex, delta)), stack, inst);
    }

    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    int topPage() const noexcept { return pageTop_; }
    int openSlot() const noexcept { return openSlot_; }

    ui::ModMatrixView& matrix() noexcept { return matrix_; }
    ui::ModSlotDetail& detail() noexcept { return detail_; }
    ui::ListPopup& list() noexcept { return list_; }
    ui::ModTargetsPage& targetsPage() noexcept { return targets_; }
    /** The source (the choice value of modNN.source) whose targets page is or was open last (0: none yet). */
    int targetsSource() const noexcept { return targetsSource_; }

    void openSlotDetail (int slot, UiStack& stack)
    {
        openSlot_ = slot;
        stack.openDetail (kPageDetail);
        pageTop_ = kPageDetail;
    }

    /** Names. The matrix shows "" for a slot that has no source or no target. */
    static const char* sourceName (int choice) noexcept { return choice >= 1 ? uibind::sourceName (choice) : ""; }
    static const char* destName (int choice) noexcept
    {
        if (choice <= 0)
            return "";
        const int p = synthyboy::modDestinationParamIndex (static_cast<size_t> (choice));
        return p >= 0 ? uibind::info (p).name : "";
    }
    static int destModuleOf (int choice) noexcept
    {
        if (choice <= 0) return -1;
        const int p = synthyboy::modDestinationParamIndex (static_cast<size_t> (choice));
        if (p < 0) return -1;
        const char* grp = uibind::info (p).group;
        if (! grp) return -1;
        if (std::strstr (grp, "Osc 1")) return 0;
        if (std::strstr (grp, "Osc 2")) return 1;
        if (std::strstr (grp, "Osc 3")) return 2;
        if (std::strstr (grp, "Sub") || std::strstr (grp, "Noise")) return 3;
        if (std::strstr (grp, "Filter 1")) return 4;
        if (std::strstr (grp, "Filter 2")) return 5;
        if (std::strstr (grp, "Env")) return 6;
        if (std::strstr (grp, "LFO")) return 7;
        if (std::strstr (grp, "FX")) return 8;
        return 9;   // master and the others
    }
    static const char* destModuleName (int m) noexcept
    {
        static const char* const n[11] = { "ALL MODULES", "OSC A", "OSC B", "OSC C", "SUB / NOISE", "FILTER 1", "FILTER 2", "ENVELOPES", "LFOS", "FX", "MASTER" };
        return m >= 0 && m < 11 ? n[m] : "";
    }

private:
    ui::Component* topComponent() noexcept
    {
        if (pageTop_ == kPageList) return &list_;
        if (pageTop_ == kPageDetail) return &detail_;
        if (pageTop_ == kPageTargets) return &targets_;
        return &matrix_;
    }

    // ---- the quick-add board: every source the engine has, in groups (ui_bind.h sourceGroup), in the order of the engine's table
    /** The text of a chip and the name that picks its colour: the same functions as the board of the page of one control (ui_bind.h). */
    static void boardLabel (int choice, char* out, size_t cap) { uibind::boardLabel (choice, out, cap); }
    static void boardColourName (int choice, char* out, size_t cap) { uibind::boardColourName (choice, out, cap); }
    void fillBoard()
    {
        uibind::fillSourceBoard (matrix_.board);
        matrix_.rebuildVisibleList();
    }

    // ---- the targets page of one source (the board's tap, ADD TARGET): the slots whose source it is
    bool targetsShown() const noexcept { return pageTop_ == kPageTargets || (pageTop_ == kPageList && listKind_ == ListKind::NewTarget); }
    void syncTop (const UiStack& stack) noexcept { pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0; }
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        constexpr int kPer = ui::ModTargetsPage::kPerPage;
        const int total = uibind::slotsOfSource (v, targetsSource_, scratchSlots_, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + kPer - 1) / kPer);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * kPer, n = std::clamp (total - first, 0, kPer);
        ui::ModTargetsPage::Entry e[kPer];
        for (int i = 0; i < kPer; ++i)
            targetRows_[i] = {};
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (scratchSlots_[first + i]);
            ui::setText (e[i].name, sizeof e[i].name, destName (static_cast<int> (std::lround (v.param (sp.destination)))));
            e[i].depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            e[i].bidirectional = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            e[i].bypassed = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            targetRows_[i] = { scratchSlots_[first + i], targetsSource_ };
        }
        // only a change of the rows is loaded again (a reload resets the touch of a row that is being dragged)
        bool same = ! fresh && targetsLoaded_ == targetsSource_ && n == targets_.numRows() && total == targetTotal_;
        for (int i = 0; same && i < n; ++i)
        {
            const ui::ModTargetsPage::Row& r = targets_.rows[i];
            same = std::strcmp (r.name, e[i].name) == 0 && std::fabs (r.depth.state.value - (e[i].depth + 1.0) * 0.5) < 1.0e-9 && r.polarity.state.on == e[i].bidirectional && r.bypass.state.on == ! e[i].bypassed;
        }
        if (same)
            return;
        targetTotal_ = total;
        targetsLoaded_ = targetsSource_;
        char title[28];
        boardLabel (targetsSource_, title, sizeof title);
        char colour[12];
        boardColourName (targetsSource_, colour, sizeof colour);
        targets_.load (e, n, title, ui::theme::sourceColour (colour), total, targetsPageIndex_);
    }
    /** The board's tap: the targets page of the source, and over it the list that picks the first target. */
    void beginTargets (IInstanceView& inst, UiStack& stack, int source)
    {
        if (source < 1 || source > static_cast<int> (synthyboy::modSourceCount()) || ! stack.openDetail (kPageTargets))
            return;
        targetsSource_ = source;
        targetsPageIndex_ = 0;
        openSlot_ = -1;
        pageTop_ = kPageTargets;
        loadTargets (inst, true);
        openNewTargetList (inst, stack);
    }
    void openNewTargetList (IInstanceView& inst, UiStack& stack)
    {
        listKind_ = ListKind::NewTarget;
        listSlot_ = -1;
        listParam_ = -1;
        listGroup_ = -1;
        fillList (0);
        if (stack.openDetail (kPageList))
            pageTop_ = kPageList;
        (void) inst;
    }
    /** A new slot of `source` with the target `destChoice`: the first free slot, +50 %, unipolar unless the target swings around zero, on; the source is written last so that the slot appears whole. */
    bool addTargetSlot (IInstanceView& inst, int source, int destChoice)
    {
        if (source < 1 || destChoice <= 0 || uibind::slotOfPair (inst, source, destChoice) >= 0)
            return false;   // that target is on the list already
        const int slot = uibind::freeSlot (inst);
        if (slot < 0)
            return false;
        const int p = synthyboy::modDestinationParamIndex (static_cast<size_t> (destChoice));
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        uibind::writePlain (inst, sp.destination, destChoice);
        uibind::writePlain (inst, sp.amount, 50.0);
        if (sp.bypass >= 0) uibind::writePlain (inst, sp.bypass, 0.0);
        if (sp.bipolar >= 0) uibind::writePlain (inst, sp.bipolar, p >= 0 && uibind::info (p).min < 0.0f && uibind::info (p).max > 0.0f ? 1.0 : 0.0);
        uibind::writePlain (inst, sp.source, source);
        const int total = uibind::slotsOfSource (inst, source, scratchSlots_, uibind::kMaxTargetSlots);
        targetsPageIndex_ = std::max (0, (total - 1) / ui::ModTargetsPage::kPerPage);   // the page that shows the new row
        return true;
    }
    static void clearSlot (IInstanceView& inst, int slot)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        for (int p : { sp.source, sp.destination, sp.amount, sp.bipolar, sp.stereo, sp.bypass, sp.auxSource })
            if (p >= 0)
                uibind::writePlain (inst, p, 0.0);
    }
    /** The choice that is set now in the parameter the open list edits (0 when the list edits none: the target of a new slot). */
    int listCurrent (const IInstanceView& inst) const noexcept { return listParam_ >= 0 ? static_cast<int> (std::lround (inst.param (listParam_))) : 0; }
    bool destList() const noexcept { return listKind_ == ListKind::Dest || listKind_ == ListKind::NewTarget; }

    // ---- the page of one slot
    void pullDetail (IInstanceView& inst, const synthyboy::LiveValues* live)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (openSlot_);
        const int srcVal = static_cast<int> (std::lround (inst.param (sp.source))), dstVal = static_cast<int> (std::lround (inst.param (sp.destination)));
        detail_.setTitle (openSlot_, sourceName (srcVal), destName (dstVal));
        const double amount = static_cast<double> (inst.param (sp.amount)) / 100.0;
        double liveSource = 0.0;
        if (live != nullptr && srcVal >= 1 && srcVal <= synthyboy::LiveValues::kMaxSources)
            liveSource = static_cast<double> (live->source[srcVal - 1]);
        detail_.summary.setSlot (openSlot_);
        detail_.summary.updateData (sourceName (srcVal), destName (dstVal), amount, inst.param (sp.bipolar) > 0.5f, inst.param (sp.bypass) > 0.5f, live != nullptr && srcVal > 0, amount * liveSource);

        const int auxSrc = static_cast<int> (std::lround (inst.param (sp.auxSource)));
        detail_.auxSource.setValue (auxSrc > 0 ? sourceName (auxSrc) : "NONE");
        pullKnob (inst, detail_.auxOutput, sp.auxOutput);
        pullKnob (inst, detail_.auxPower, sp.auxPower);
        detail_.auxInv.state.on = inst.param (sp.auxInv) > 0.5f;
        pullKnob (inst, detail_.power, sp.power);
        detail_.stereo.state.on = inst.param (sp.stereo) > 0.5f;
        pullKnob (inst, detail_.rise, sp.rise);
        pullKnob (inst, detail_.fall, sp.fall);
        pullKnob (inst, detail_.delay, sp.delay);
        pullKnob (inst, detail_.delayBeats, sp.delayBeats);
        detail_.delaySync.state.on = inst.param (sp.delaySync) > 0.5f;
        detail_.setDelaySync (detail_.delaySync.state.on);
    }
    static void pullKnob (const IInstanceView& v, ui::Knob& k, int p)
    {
        if (p < 0)
        {
            k.setEnabled (false);
            return;
        }
        const synthyboy::ParamInfo& pi = uibind::info (p);
        const float plain = v.param (p);
        k.state.value = k.state.modValue = uibind::normOf (p, plain);
        k.state.defaultValue = uibind::normOf (p, pi.def);
        k.state.bipolar = pi.min < 0.0f && pi.max > 0.0f;
        if (pi.unit == synthyboy::Unit::none)
            std::snprintf (k.state.valueText, sizeof k.state.valueText, "%+.1f", static_cast<double> (plain));   // the curves: -10 .. +10
        else
            uibind::formatValue (pi, plain, k.state.valueText, sizeof k.state.valueText);
    }

    // ---- the grouped lists (the sources, the targets)
    void openList (IInstanceView& inst, UiStack& stack, ListKind kind, int slot)
    {
        listKind_ = kind;
        listSlot_ = slot;
        listGroup_ = -1;
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        listParam_ = kind == ListKind::Source ? sp.source : (kind == ListKind::Dest ? sp.destination : sp.auxSource);
        const int current = static_cast<int> (std::lround (inst.param (listParam_)));
        fillList (current);
        stack.openDetail (kPageList);
        pageTop_ = kPageList;
    }
    /** The first level: NONE and the groups; the second level: the entries of one group. `current` is the choice that is set now (its entry is the one shown first). */
    void fillList (int current)
    {
        listNames_.clear();
        listChoices_.clear();
        const bool dest = destList();
        if (listGroup_ < 0)
        {
            if (listKind_ != ListKind::NewTarget)   // a new slot has to have a target: no NONE in that list
            {
                listNames_.push_back ("NONE");
                listChoices_.push_back (0);
            }
            if (dest)
            {
                const uibind::DestIndex& d = uibind::DestIndex::get();
                for (size_t g = 0; g < d.groups.size(); ++g)
                {
                    listNames_.push_back (d.groups[g].c_str());
                    listChoices_.push_back (-1 - static_cast<int> (g));
                }
            }
            else
                for (int g = 1; g <= uibind::kSourceGroups; ++g)
                {
                    listNames_.push_back (uibind::sourceGroupName (g));
                    listChoices_.push_back (-g);
                }
            list_.state.header = nullptr;
            list_.state.headerBack = false;
        }
        else
        {
            if (dest)
            {
                const uibind::DestIndex& d = uibind::DestIndex::get();
                for (int c : d.choices[static_cast<size_t> (listGroup_)])
                {
                    listNames_.push_back (destName (c));
                    listChoices_.push_back (c);
                }
                list_.state.header = d.groups[static_cast<size_t> (listGroup_)].c_str();
            }
            else
            {
                for (int c = 1; c <= static_cast<int> (synthyboy::modSourceCount()); ++c)
                    if (uibind::sourceGroup (c) == listGroup_ + 1)
                    {
                        listNames_.push_back (uibind::sourceName (c));
                        listChoices_.push_back (c);
                    }
                list_.state.header = uibind::sourceGroupName (listGroup_ + 1);
            }
            list_.state.headerBack = true;
        }
        list_.state.items = listNames_.data();
        list_.state.count = static_cast<int> (listNames_.size());
        list_.state.current = -1;
        for (size_t i = 0; i < listChoices_.size(); ++i)
            if (listChoices_[i] == current && current > 0)
                list_.state.current = static_cast<int> (i);
        list_.showCurrent();
    }
    int firstUnusedSlot (const IInstanceView& inst) const noexcept
    {
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source < 0 || sp.destination < 0)
                continue;
            if (static_cast<int> (std::lround (inst.param (sp.source))) == 0 && static_cast<int> (std::lround (inst.param (sp.destination))) == 0)
                return s;
        }
        return -1;
    }

    void handle (ui::Change c, UiStack& stack, IInstanceView& inst)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;

        // the filter tabs and the filter choice
        if (c.id == kFilterAll) { matrix_.setFilterMode (ui::ModMatrixView::FilterAll); return; }
        if (c.id == kFilterSource) { matrix_.setFilterMode (ui::ModMatrixView::FilterSource); stepSourceFilter (0); return; }
        if (c.id == kFilterDest) { matrix_.setFilterMode (ui::ModMatrixView::FilterDest); stepDestFilter (0); return; }
        if (c.id == kSourceFilterStep && c.kind == K::Index) { srcFilter_ = c.index; stepSourceFilter (0); return; }
        if (c.id == kDestFilterStep && c.kind == K::Index) { dstFilter_ = c.index; stepDestFilter (0); return; }

        // the board of the sources: a tap opens the targets page of the source and the list of its first target
        if (c.id == kBoard && c.kind == K::Index)
        {
            beginTargets (inst, stack, c.index);
            return;
        }

        // the targets page of a source
        if (c.id == kTargetsBack && c.kind == K::Request) { stack.back(); syncTop (stack); return; }
        if (c.id == kTargetsAdd && c.kind == K::Request) { openNewTargetList (inst, stack); return; }
        if (c.id == kTargetsPage && c.kind == K::Index)
        {
            targetsPageIndex_ = c.index;
            loadTargets (inst, true);
            return;
        }
        if (c.id >= kTargetsRow && c.id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage)
        {
            const int slot = targetRows_[(c.id - kTargetsRow) / 4].slot, what = (c.id - kTargetsRow) % 4;
            if (slot < 0)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
            if (what == 0 && c.kind == K::Value)
                uibind::writeNorm (inst, sp.amount, c.value);
            else if (what == 1 && c.kind == K::Index)
                uibind::writePlain (inst, sp.bipolar, c.index != 0 ? 1.0 : 0.0);
            else if (what == 2 && c.kind == K::Index)
                uibind::writePlain (inst, sp.bypass, c.index != 0 ? 0.0 : 1.0);   // the switch says ON (active): bypassed is its opposite
            else if (what == 3 && c.kind == K::Request)
            {
                clearSlot (inst, slot);
                loadTargets (inst, true);
            }
            return;
        }

        // a new slot: the first one that is not used (ENV 1 to the first target of the table, +50 %), its page opens
        if (c.id == kAdd)
        {
            const int s = firstUnusedSlot (inst);
            if (s >= 0)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
                uibind::writePlain (inst, sp.source, 1.0);
                uibind::writePlain (inst, sp.destination, 1.0);
                uibind::writeNorm (inst, sp.amount, 0.75);
                if (sp.bypass >= 0) uibind::writePlain (inst, sp.bypass, 0.0);
                openSlotDetail (s, stack);
            }
            return;
        }

        // the page of a slot
        if (c.id == kDetailBack) { stack.back(); pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0; return; }
        if (c.id == kDetailDelete && openSlot_ >= 0)
        {
            clearSlot (inst, openSlot_);
            stack.back();
            pageTop_ = 0;
            return;
        }

        // the list
        if (c.id == kListBackdrop) { stack.back(); pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0; return; }
        if (c.id == kListHeader && c.kind == K::Request)
        {
            listGroup_ = -1;
            fillList (listCurrent (inst));
            return;
        }
        if (c.id == kListPopup && c.kind == K::Index)
        {
            const int sel = c.index >= 0 && c.index < static_cast<int> (listChoices_.size()) ? listChoices_[static_cast<size_t> (c.index)] : 0;
            if (sel < 0)   // a group: its entries
            {
                listGroup_ = destList() ? -1 - sel : -sel - 1;
                fillList (listCurrent (inst));
                return;
            }
            if (listKind_ == ListKind::NewTarget)
            {
                addTargetSlot (inst, targetsSource_, sel);
                loadTargets (inst, true);
            }
            else if (listParam_ >= 0)
                uibind::writePlain (inst, listParam_, sel);
            stack.back();
            syncTop (stack);
            return;
        }

        // the rows of the matrix (id = kRowBase + window row * 10 + offset) and the row of the page of a slot
        int slot = -1, offset = -1;
        if (c.id >= kRowBase && c.id < kRowBase + ui::ModMatrixView::kWindowRows * 10)
        {
            slot = matrix_.slotAtWindowIndex ((c.id - kRowBase) / 10);
            offset = (c.id - kRowBase) % 10;
        }
        else if (c.id >= kDetailRowBase && c.id < kDetailRowBase + 10)
        {
            slot = openSlot_;
            offset = c.id - kDetailRowBase;
        }
        if (offset >= 0)
        {
            if (slot < 0 || slot >= synthyboy::kNumModSlots)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
            if (offset == kOffsetSource)
                openList (inst, stack, ListKind::Source, slot);
            else if (offset == kOffsetDest)
                openList (inst, stack, ListKind::Dest, slot);
            else if (offset == kOffsetAmount && c.kind == K::Value)
                uibind::writeNorm (inst, sp.amount, c.value);
            else if (offset == kOffsetBi && c.kind == K::Index)
                uibind::writePlain (inst, sp.bipolar, c.index != 0 ? 1.0 : 0.0);
            else if (offset == kOffsetBypass && c.kind == K::Index)
                uibind::writePlain (inst, sp.bypass, c.index != 0 ? 0.0 : 1.0);   // the switch says ON (active): bypassed is its opposite
            else if (offset == kOffsetDetail)
                openSlotDetail (slot, stack);
            return;
        }

        // the controls of the page of a slot
        if (openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (openSlot_);
            switch (c.id)
            {
                case kDetailAuxSource: openList (inst, stack, ListKind::Aux, openSlot_); return;
                case kDetailAuxOutput: if (c.kind == K::Value) uibind::writeNorm (inst, sp.auxOutput, c.value); return;
                case kDetailAuxInv: if (c.kind == K::Index) uibind::writePlain (inst, sp.auxInv, c.index != 0 ? 1.0 : 0.0); return;
                case kDetailAuxPower: if (c.kind == K::Value) uibind::writeNorm (inst, sp.auxPower, c.value); return;
                case kDetailPower: if (c.kind == K::Value) uibind::writeNorm (inst, sp.power, c.value); return;
                case kDetailStereo: if (c.kind == K::Index) uibind::writePlain (inst, sp.stereo, c.index != 0 ? 1.0 : 0.0); return;
                case kDetailRise: if (c.kind == K::Value) uibind::writeNorm (inst, sp.rise, c.value); return;
                case kDetailFall: if (c.kind == K::Value) uibind::writeNorm (inst, sp.fall, c.value); return;
                case kDetailDelay: if (c.kind == K::Value) uibind::writeNorm (inst, sp.delay, c.value); return;
                case kDetailDelayBeats: if (c.kind == K::Value) uibind::writeNorm (inst, sp.delayBeats, c.value); return;
                case kDetailDelaySync: if (c.kind == K::Index) uibind::writePlain (inst, sp.delaySync, c.index != 0 ? 1.0 : 0.0); return;
                default: break;
            }
        }
    }

    void stepSourceFilter (int)
    {
        srcFilter_ = std::clamp (srcFilter_, 0, uibind::kSourceGroups);
        matrix_.sourceFilter.state.count = uibind::kSourceGroups + 1;
        matrix_.sourceFilter.state.index = srcFilter_;
        matrix_.sourceFilter.setItem (srcFilter_ == 0 ? "ALL SOURCES" : uibind::sourceGroupName (srcFilter_));
        matrix_.setSourceFilterChoice (srcFilter_);
    }
    void stepDestFilter (int)
    {
        dstFilter_ = std::clamp (dstFilter_, 0, 10);
        matrix_.destFilter.state.count = 11;
        matrix_.destFilter.state.index = dstFilter_;
        matrix_.destFilter.setItem (destModuleName (dstFilter_));
        matrix_.setDestModuleFilter (dstFilter_ - 1);
    }

    void assignIds()
    {
        matrix_.tabAll.setId (kFilterAll);
        matrix_.tabSource.setId (kFilterSource);
        matrix_.tabDest.setId (kFilterDest);
        matrix_.add.setId (kAdd);
        matrix_.board.setId (kBoard);
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
        matrix_.sourceFilter.setId (kSourceFilterStep);
        matrix_.destFilter.setId (kDestFilterStep);
        stepSourceFilter (0);
        stepDestFilter (0);
        auto rowIds = [] (ui::ModRow& r, int base)
        {
            r.number.setId (base + kOffsetNumber);
            r.sourceBtn.setId (base + kOffsetSource);
            r.destBtn.setId (base + kOffsetDest);
            r.amountBar.setId (base + kOffsetAmount);
            r.biToggle.setId (base + kOffsetBi);
            r.bypassToggle.setId (base + kOffsetBypass);
            r.detailBtn.setId (base + kOffsetDetail);
        };
        for (int w = 0; w < ui::ModMatrixView::kWindowRows; ++w)
            rowIds (matrix_.windowRows[w], kRowBase + w * 10);
        rowIds (detail_.summary, kDetailRowBase);

        detail_.back.setId (kDetailBack);
        detail_.deleteBtn.setId (kDetailDelete);
        detail_.auxSource.setId (kDetailAuxSource);
        detail_.auxOutput.setId (kDetailAuxOutput);
        detail_.auxInv.setId (kDetailAuxInv);
        detail_.auxPower.setId (kDetailAuxPower);
        detail_.power.setId (kDetailPower);
        detail_.stereo.setId (kDetailStereo);
        detail_.rise.setId (kDetailRise);
        detail_.fall.setId (kDetailFall);
        detail_.delay.setId (kDetailDelay);
        detail_.delayBeats.setId (kDetailDelayBeats);
        detail_.delaySync.setId (kDetailDelaySync);
    }

    /** The groups of the page in reading order: a targets list has one group per row, the slot detail its three blocks (AUXILIARY SOURCE, SHAPE, TIME), the matrix the selected slot. The active group is on Q 1 .. 4, the
        next three (wrapping) on 5 .. 16. */
    void publishQGroup (IInstanceView& inst)
    {
        for (int b = 0; b < 3; ++b)
            detail_.groups[b].setBlock (-1);
        if (pageTop_ == kPageList)
        {
            std::snprintf (qName_, sizeof qName_, "LIST");
            qSlots_.publish (inst, 0, 0, [] (int, int*) {});
        }
        else if (pageTop_ == kPageTargets)
        {
            char title[28];
            boardLabel (targetsSource_, title, sizeof title);
            std::snprintf (qName_, sizeof qName_, "%s TARGETS", title);
            qSlots_.publish (inst, targets_.numRows(), targets_.activeRow(), [this] (int row, int* t) { modRowTargets (row < ui::ModTargetsPage::kPerPage ? targetRows_[row].slot : -1, t); });
            setRowBlocks (targets_, ui::ModTargetsPage::kPerPage, targets_.numRows(), targets_.activeRow());
        }
        else if (pageTop_ == kPageDetail && openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (openSlot_);
            std::snprintf (qName_, sizeof qName_, "SLOT %02d %s", openSlot_ + 1, ui::ModSlotDetail::blockName (detail_.activeBlock()));
            const int active = static_cast<int> (detail_.activeBlock());
            qSlots_.publish (inst, 3, active, [&] (int block, int* t)
            {
                if (block == ui::ModSlotDetail::BlockAux)
                {
                    t[0] = sp.auxSource; t[1] = sp.auxOutput; t[2] = sp.auxInv; t[3] = sp.auxPower;
                }
                else if (block == ui::ModSlotDetail::BlockShape)
                {
                    t[0] = sp.power; t[1] = sp.stereo;
                }
                else
                {
                    t[0] = sp.rise; t[1] = sp.fall; t[2] = detail_.delaySyncOn() ? sp.delayBeats : sp.delay; t[3] = sp.delaySync;
                }
            });
            for (int b = 0; b < 3; ++b)
                detail_.groups[b].setBlock (QRouter::blockOfGroup (3, active, b));
        }
        else
        {
            const int sel = matrix_.selectedSlot();
            if (sel >= 0 && sel < synthyboy::kNumModSlots)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (sel);
                std::snprintf (qName_, sizeof qName_, "SLOT %02d", sel + 1);
                qSlots_.publish (inst, 1, 0, [&] (int, int* t)
                {
                    t[0] = QRouter::encode (kVirtualRow, QRouter::kVirtual);   // Q1 = row select
                    t[1] = sp.source;                                          // Q2 = SOURCE
                    t[2] = sp.destination;                                     // Q3 = TARGET
                    t[3] = sp.amount;                                          // Q4 = AMOUNT
                });
            }
            else
            {
                std::snprintf (qName_, sizeof qName_, "MOD MATRIX");
                qSlots_.publish (inst, 0, 0, [] (int, int*) {});
            }
        }
    }

    MacroNames macroNames_;   // the macro names of the instance (loaded in update ()); the board is filled again when they change
    bool namesDirty_ = false;
    int w_ = kCanvasW, h_ = kCanvasH;
    int pageTop_ = 0, openSlot_ = -1;
    int srcFilter_ = 0, dstFilter_ = 0;

    ui::ModMatrixView matrix_;
    ui::ModSlotDetail detail_;
    ui::ListPopup list_;
    ui::ModTargetsPage targets_;

    // the targets page of a source: which source, the page of its rows, the slot of every row shown
    int targetsSource_ = 0, targetsLoaded_ = 0, targetsPageIndex_ = 0, targetTotal_ = -1;
    uibind::ModRow targetRows_[ui::ModTargetsPage::kPerPage];
    int scratchSlots_[uibind::kMaxTargetSlots] = {};

    ListKind listKind_ = ListKind::Source;
    int listSlot_ = -1, listParam_ = -1, listGroup_ = -1;
    std::vector<const char*> listNames_;
    std::vector<int> listChoices_;   // per row: the choice value, 0 NONE, a negative number a group
    const char* srcGroupNames_[uibind::kSourceGroups + 1] = {};

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    QSlots qSlots_;
    char qName_[40] = "MOD MATRIX";
};

}} // namespace synthyboy_mpc::overlay
