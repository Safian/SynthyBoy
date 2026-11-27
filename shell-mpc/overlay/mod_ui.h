// SynthyBoy - MPC overlay: the MODULATION MATRIX page of the Mod tab (the plugin owns the page logic, the controls are native cells of the host skin, owner decision 2026-11-27: the C++ layer has no UI elements): the 64 slots of
// the matrix, the page of one slot and the targets page of a source wired to the engine parameters (modNN.* of the table, docs/MPC.md "Modulation UI"). The plain data of the page (the slots with their filter and scroll window,
// the rectangles of the native cells, the Q members) is mod_model.h; the binding of the native cells is publishNative*. One object per (page view, plugin instance). Pure: no OS headers, no threads of its own; the overlay
// thread calls update () / touch () / tick () / draw (), with the instance as IInstanceView.
// While no slot is in use the matrix has no rows to show: the page is the quick-add BOARD instead (native, published by publishNativeBoard): every source the engine has. A tap on a chip opens the targets page of that source
// (the page of the envelope, the LFO and the macros: "where does this source go") with the grouped list of the targets; the target that is picked makes the slot, and the page shows it. BACK returns to the matrix, which lists
// the slot (the board is gone as soon as one slot is in use).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementPage): the matrix is the page; above it the details of a slot (a page over the whole tab, BACK returns) and the native list that picks a source, a
// target or the auxiliary source of a slot. The lists are grouped like the ADD TARGET list of the envelope and the LFO (one source of the groups: ui_bind.h DestIndex): the first level has the groups, a tap on a group shows its
// entries (the header of the list is the way back). The Q group of the matrix is the selected row: ROW, SOURCE, TARGET and AMOUNT (the fourth Q-Link steps through the rows: a virtual Q target, q_router.h).
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../native_mod_controls.h"
#include "../q_router.h"
#include "../ui/native_apertures.h"
#include "instance_view.h"
#include "mod_model.h"
#include "mod_targets_native.h"
#include "page_view.h"
#include "native_marker_covers.h"
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
    enum class ListKind : uint8_t { Source, Dest, Aux, NewTarget, BoardSource };   // NewTarget: the target of a new slot of the source on the targets page

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kVirtualRow = 0;   // the virtual Q target that steps through the rows of the matrix

    ModUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        setNativeControls (true);
        matrix_.setBoardSources (static_cast<int> (synthyboy::modSourceCount()));
        matrix_.rebuildVisibleList();
    }

    void setNativeControls (bool on) { nativeControls_ = on; }
    bool nativeControls() const noexcept { return nativeControls_; }
    void setHostBackground (bool present) noexcept { nativeGround_ = present; }
    bool hostBackground() const noexcept
    {
        return nativeGround_ && nativeControls_ && (pageTop_ == 0 || pageTop_ == kPageDetail || pageTop_ == kPageTargets || pageTop_ == kPageList) && w_ == kCanvasW && h_ == kCanvasH
            && &ui::theme::L() == &ui::theme::kLook1;
    }
    // draw() owns the ground because slot-detail widgets need individual apertures.
    void drawPageGround (gpu::SceneBuilder&) const noexcept {}


    nativeui::mod::View nativeView() const noexcept
    {
        using namespace nativeui::mod;
        if (! nativeControls_)
            return Hidden;
        if (pageTop_ == kPageDetail && openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots)
            return detail_.delaySyncOn() ? DetailBeats : DetailDelay;
        if (pageTop_ == 0 && ! matrix_.boardShown() && matrix_.visibleSlotCount() > 0)
            return matrix_.scrolls() ? MatrixScroll : Matrix;
        return Hidden;
    }

    void leftTab (IInstanceView& inst) noexcept
    {
        nativeTargets_.hide (inst);
        inst.setNativeModMatrix (nativeui::mod::Hidden, nullptr, nullptr);
        inst.setNativeModNavigation (0, 0, 0, nullptr);
        inst.setNativeModDetailNavigation (nullptr);
        inst.setNativeModBoard (0, nullptr);
    }

    bool isNativeControl (int x, int y) const noexcept
    {
        if (! nativeControls_)
            return false;
        if (pageTop_ == kPageList)
            return true;
        if (pageTop_ == kPageTargets)
            return targets_.nativeHit (x, y);
        if (pageTop_ == kPageDetail)
            return detail_.isNativeControl (x, y);
        if (pageTop_ == 0)
        {
            if (matrix_.isNativeNavigation (x, y))
                return true;
            if (matrix_.boardShown())
                return hostBackground() && (y >= kTabY + 44 && y <= kTabY + kTabH);
            return matrix_.isNativeControl (x, y);
        }
        return false;
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        matrix_.place (tab);
        detail_.place (tab);
        targets_.place (tab);
    }

    /** Reads the instance: the 64 slots, the page of the open slot, the live values of the sources, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, UiStack& stack, double nowMs, const page::Input& in)
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
            matrix_.rebuildVisibleList();
        }
        const uint64_t gen = inst.stateGeneration();
        if (gen != stateGeneration_)
        {
            stateGeneration_ = gen;
            if (nativeControls_)
                leftTab (inst);
            if (stack.depth() >= 2 && stack.top() == kPageList)
            {
                stack.back();
                listParam_ = listSlot_ = -1;
                gestureDown_ = false;
            }
            nativeGestureBlocked_ = nativeGestureField_ >= 0;
            if (stack.depth() <= 1)
                openSlot_ = -1;
            matrix_.rebuildVisibleList();
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
        if (pageTop_ == kPageTargets && targetsSource_ <= 0)
            targetsSource_ = 1;
        if (targetsShown())
            loadTargets (inst, false);
        nativeTargets_.update (inst, targets_, nativeControls_ && pageTop_ == kPageTargets, nativeui::targets::Mod, targetsSource_,
                               targetRows_, ModTargetsNative::Ids { kTargetsBack, kTargetsAdd, kTargetsPage, kTargetsRow },
                               [&] (ui::Change change) { handle (change, stack, inst); });

        // the virtual Q target of the rows: a write of the host moves the selected row
        float x;
        if (pageTop_ == 0 && inst.takeQVirtual (kVirtualRow, x) && matrix_.visibleSlotCount() > 0)
            matrix_.selectRow (static_cast<int> (std::lround (x * static_cast<float> (std::max (1, matrix_.visibleSlotCount() - 1)))));
        inst.setQVirtual (kVirtualRow, matrix_.visibleSlotCount() > 1 ? static_cast<float> (matrix_.selectedRow()) / static_cast<float> (matrix_.visibleSlotCount() - 1) : 0.0f);

        publishNativeNavigation (inst, stack, true);
        publishNativeMatrix (inst, true);
        publishNativeBoard (inst, stack, true);
        const bool full = nativeView() == nativeui::mod::Hidden && (! nativeControls_ || (pageTop_ != 0 && pageTop_ != kPageTargets && pageTop_ != kPageList));
        inst.setUiFullOpen (full);
        publishQGroup (inst);
    }

    /** The whole picture: the ground with the cut-outs of the native cells (the cells themselves are the host's), the marker covers. */
    void draw (gpu::SceneBuilder& sb) const
    {
        if (hostBackground())
        {
            drawNativeMarkerCovers (sb, "mod");
            return;
        }

        const ui::Rect tab { 0, kTabY, w_, kTabH };
        auto ground = [&] (ui::Rect r)
        {
            ui::Cell cell (sb, r, 0);
            gpu::ScopedClip clip (sb, r.x, r.y, r.right(), r.bottom());
            if (!clip) return;
            sb.gradientRect (tab.x, tab.y, tab.right(), tab.bottom(),
                             ui::theme::rgba (ui::theme::L().bg0), ui::theme::rgba (ui::theme::L().bg1));
        };
        if (nativeControls_ && pageTop_ == 0)
        {
            const auto nav = matrix_.nativeNavigationApertures();
            const auto values = matrix_.nativeApertures();
            std::array<ui::Rect, nav.size() + values.size()> controls;
            std::copy (nav.begin(), nav.end(), controls.begin());
            std::copy (values.begin(), values.end(), controls.begin() + nav.size());
            ui::outsideNativeCells (tab, controls, ground);
        }
        else if (nativeView() != nativeui::mod::Hidden && pageTop_ == kPageDetail)
            ui::outsideNativeCells (tab, detail_.nativeApertures(), ground);
        else
            page::ground (sb);
        drawNativeMarkerCovers (sb, "mod");
    }

    /** The cells of the tab are the host's; the overlay claims the rest of the page (the gestures of the list: a tap on a row, a drag, the scroll zone, a tap on a block of the page of a slot). */
    bool covers (int x, int y) const noexcept
    {
        if (! ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y))
            return false;
        if (pageTop_ == kPageList)
            return false;
        if (pageTop_ == kPageTargets)
            return ! (nativeControls_ && targets_.nativeHit (x, y));
        if (nativeControls_ && isNativeControl (x, y))
            return false;
        return true;
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double)
    {
        const int x = ev.x, y = ev.y;
        (void) stack;
        if (pageTop_ == kPageList && inst.stateGeneration() != stateGeneration_)
        {
            dropGesture();
            return true;
        }
        if (ev.kind == TouchEvent::Down)
        {
            int targetRow = -1;
            if (pageTop_ == kPageTargets && targets_.nativeHit (x, y, &targetRow))
            {
                dropGesture();
                if (targetRow >= 0)
                {
                    targets_.selectRow (targetRow);
                    publishQGroup (inst);
                }
                return false;
            }
            nativeGestureField_ = nativeFieldAt (x, y);
            nativeGestureBlocked_ = false;
            nativeGestureTarget_ = nativeGestureField_ >= 0 ? nativeCells_[nativeGestureField_].target : -1;
            gestureDown_ = false;
            if (nativeGestureField_ >= 0)
            {
                if (pageTop_ == 0)
                    matrix_.selectRow (matrix_.firstRow() + nativeGestureField_ / nativeui::mod::kRowFields);
                else if (! ui::QGroup::wideDevice())
                    for (int b = 0; b < 3; ++b)
                        if (detail_.block (b).contains (x, y))
                            detail_.setActiveBlock (static_cast<modmodel::Detail::Block> (b));
                publishQGroup (inst);
                return false;
            }
            if (! covers (x, y))
                return false;
            if (pageTop_ == 0)
            {
                gestureDown_ = matrix_.scrollDown (x, y);
                if (gestureDown_)
                    publishQGroup (inst);
            }
            else if (pageTop_ == kPageDetail)
            {
                for (int b = 0; b < 3; ++b)
                    if (detail_.block (b).contains (x, y))
                    {
                        detail_.setActiveBlock (static_cast<modmodel::Detail::Block> (b));
                        publishQGroup (inst);
                        break;
                    }
                gestureDown_ = true;
            }
            else if (pageTop_ == kPageTargets)
            {
                for (int i = 0; i < targets_.numRows(); ++i)
                    if (y >= targets_.rowY (i) && y < targets_.rowY (i) + modmodel::Targets::kRowH)
                    {
                        targets_.selectRow (i);
                        publishQGroup (inst);
                        break;
                    }
                gestureDown_ = true;
            }
            publishNativeNavigation (inst, stack, false);
            publishNativeMatrix (inst, false);
            publishNativeBoard (inst, stack, false);
            return true;
        }
        if (ev.kind == TouchEvent::Up && nativeGestureField_ >= 0)
        {
            nativeGestureField_ = -1;
            nativeGestureBlocked_ = false;
            publishNativeMatrix (inst, false);
        }
        if (! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
        {
            if (pageTop_ == 0)
            {
                matrix_.scrollMove (x, y);
                publishQGroup (inst);
            }
        }
        else
        {
            matrix_.scrollUp();
            gestureDown_ = false;
        }
        publishNativeNavigation (inst, stack, false);
        publishNativeMatrix (inst, false);
        publishNativeBoard (inst, stack, false);
        return true;
    }

    void tick (double, UiStack&, IInstanceView&) {}

    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the list of the matrix scrolls a row a notch. false: nothing of this page lies there, or a finger is down on it. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double)
    {
        if (gestureDown_ || pageTop_ != 0 || ! covers (x, y) || ! matrix_.wheel (x, y, notches))
            return false;
        publishQGroup (inst);
        publishNativeNavigation (inst, stack, false);
        publishNativeMatrix (inst, false);
        publishNativeBoard (inst, stack, false);
        return true;
    }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { matrix_.scrollUp(); gestureDown_ = false; nativeGestureField_ = -1; nativeGestureBlocked_ = false; }
    void forgetQGroup() noexcept { qSlots_.forget(); }

    void turnQ (int knobIndex, double delta, UiStack& stack, IInstanceView& inst)
    {
        handle (turnChange (knobIndex, delta), stack, inst);
        publishNativeMatrix (inst, false);
    }

    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    int topPage() const noexcept { return pageTop_; }
    int openSlot() const noexcept { return openSlot_; }

    modmodel::Matrix& matrix() noexcept { return matrix_; }
    modmodel::Detail& detail() noexcept { return detail_; }
    modmodel::Targets& targetsPage() noexcept { return targets_; }
    /** The rows of the open list (the names the host list shows) and the choice value of each (0 NONE, a negative number a group). */
    const std::vector<const char*>& listNames() const noexcept { return listNames_; }
    const std::vector<int>& listChoices() const noexcept { return listChoices_; }
    /** The source (the choice value of modNN.source) whose targets page is or was open last (0: none yet). */
    int targetsSource() const noexcept { return targetsSource_; }

    void openSlotDetail (int slot, UiStack& stack)
    {
        openSlot_ = slot;
        detail_.cancelDeleteConfirmation();
        stack.openDetail (kPageDetail);
        pageTop_ = kPageDetail;
    }
    ListKind listKind() const noexcept { return listKind_; }
    int listSlot() const noexcept { return listSlot_; }

    /** Names. The matrix shows "" for a slot that has no source or no target. */
    /** The name of a source as the matrix rows and the detail page write it: the engine's name, except "Note-On Rand (Discrete)" (choice 54), which does not fit a row ("Note-On Rand (Discret..." was cut with an ellipsis, a layout-lint finding) and is written "Note-On Rand Disc.". */
    static const char* sourceName (int choice) noexcept
    {
        if (choice < 1)
            return "";
        const char* n = uibind::sourceName (choice);
        return std::strcmp (n, "Note-On Rand (Discrete)") == 0 ? "Note-On Rand Disc." : n;
    }
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
    int nativeFieldAt (int x, int y) const noexcept
    {
        namespace n = nativeui::mod;
        const auto view = nativeView();
        if (view == n::DetailDelay || view == n::DetailBeats)
        {
            const auto areas = detail_.nativeApertures();
            for (int field = 0; field < n::kDetailFields; ++field)
                if (areas[field].contains (x, y))
                    return n::kDetail + field;
        }
        else if (view != n::Hidden)
        {
            const int row = matrix_.nativeWindowRowAt (x, y);
            if (row >= 0 && row < n::kRows)
            {
                const auto areas = matrix_.cells (row).values();
                for (int field = 0; field < n::kRowFields; ++field)
                    if (areas[field].contains (x, y))
                        return row * n::kRowFields + field;
            }
        }
        return -1;
    }

    void publishNativeNavigation (IInstanceView& inst, UiStack& stack, bool consume)
    {
        namespace n = nativeui::mod;
        namespace nd = nativeui::moddetail;
        if (nativeControls_ && pageTop_ == kPageDetail && openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots)
        {
            inst.setNativeModNavigation (0, 0, 0, nullptr);
            std::array<nativeui::FxCell, nd::kHandles> detailFields {};
            auto linkDetail = [&] (int field, int target = 0, int source = 0)
            {
                detailFields[field] = { target, source, nativeui::kFxToggle };
            };
            const bool armed = detail_.isDeleteConfirmed();
            linkDetail (nd::Back);
            linkDetail (nd::Delete, 0, armed ? 1 : 0);
            const auto sp = synthyboy::modSlotParams (openSlot_);
            linkDetail (nd::Source, sp.source);
            linkDetail (nd::Target, sp.destination);
            linkDetail (nd::AuxSource, sp.auxSource);
            linkDetail (nd::Confirm, 0, armed ? 1 : 0);

            inst.setNativeModDetailNavigation (detailFields.data());
            if (! consume)
                return;
            float value = 0.0f;
            auto take = [&] (int field) { return inst.takeNativeModDetailNavigation (field, detailFields[field], value); };
            if (take (nd::Back) && value >= 0.5f)
            {
                detail_.cancelDeleteConfirmation();
                handle (ui::Change::request (kDetailBack), stack, inst);
                inst.setNativeModDetailNavigation (nullptr);
                return;
            }
            if (take (nd::Delete) && value >= 0.5f)
            {
                if (! detail_.isDeleteConfirmed())
                {
                    detail_.confirmDelete();
                    detailFields[nd::Delete].source = 1;
                    detailFields[nd::Confirm].source = 1;
                    inst.setNativeModDetailNavigation (detailFields.data());
                }
                else
                {
                    detail_.cancelDeleteConfirmation();
                    handle (ui::Change::request (kDetailDelete), stack, inst);
                    inst.setNativeModDetailNavigation (nullptr);
                }
                return;
            }
            if (take (nd::Source) && value >= 0.5f)
            {
                detail_.cancelDeleteConfirmation();
                handle (ui::Change::request (kDetailRowBase + kOffsetSource), stack, inst);
                inst.setNativeModDetailNavigation (nullptr);
                return;
            }
            if (take (nd::Target) && value >= 0.5f)
            {
                detail_.cancelDeleteConfirmation();
                handle (ui::Change::request (kDetailRowBase + kOffsetDest), stack, inst);
                inst.setNativeModDetailNavigation (nullptr);
                return;
            }
            if (take (nd::AuxSource) && value >= 0.5f)
            {
                detail_.cancelDeleteConfirmation();
                handle (ui::Change::request (kDetailAuxSource), stack, inst);
                inst.setNativeModDetailNavigation (nullptr);
                return;
            }
            return;
        }

        inst.setNativeModDetailNavigation (nullptr);

        if (! nativeControls_ || pageTop_ != 0)
        {
            inst.setNativeModNavigation (0, 0, 0, nullptr);
            return;
        }

        std::array<nativeui::FxCell, n::kNavigation> fields {};
        const int mode = static_cast<int> (matrix_.filterMode()) + 1;
        auto link = [&] (int field, int target = 0)
        {
            fields[field] = {target, target, nativeui::kFxToggle};
        };
        link (n::Mode);
        if (matrix_.filterMode() == modmodel::Matrix::FilterSource)
            link (n::SourceGroup);
        if (matrix_.filterMode() == modmodel::Matrix::FilterDest)
            link (n::DestinationGroup);
        if (firstUnusedSlot (inst) >= 0)
            link (n::Add);
        for (int row = 0; row < n::kRows; ++row)
            if (const int slot = matrix_.slotAtWindowIndex (row); slot >= 0)
            {
                const auto sp = synthyboy::modSlotParams (slot);
                link (n::navigationField (row, n::OpenSource), sp.source);
                link (n::navigationField (row, n::OpenDestination), sp.destination);
                link (n::navigationField (row, n::OpenMore), sp.amount);
            }

        inst.setNativeModNavigation (mode, srcFilter_, dstFilter_, fields.data());
        if (! consume)
            return;
        float value = 0.0f;
        auto take = [&] (int field) { return inst.takeNativeModNavigation (field, fields[field], value); };
        if (take (n::Mode))
        {
            const int requested = static_cast<int> (std::lround (value * n::kLastMode));
            const int ids[] { kFilterAll, kFilterSource, kFilterDest };
            if (requested >= 1 && requested <= n::kLastMode)
            {
                handle (ui::Change::request (ids[requested - 1]), stack, inst);
                publishNativeNavigation (inst, stack, false);
            }
            return;
        }
        if (take (n::SourceGroup))
        {
            handle (ui::Change::indexed (kSourceFilterStep, static_cast<int> (std::lround (value * n::kLastSourceGroup)), value), stack, inst);
            publishNativeNavigation (inst, stack, false);
            return;
        }
        if (take (n::DestinationGroup))
        {
            handle (ui::Change::indexed (kDestFilterStep, static_cast<int> (std::lround (value * n::kLastDestinationGroup)), value), stack, inst);
            publishNativeNavigation (inst, stack, false);
            return;
        }
        if (take (n::Add) && value >= 0.5f)
        {
            handle (ui::Change::request (kAdd), stack, inst);
            inst.setNativeModNavigation (0, 0, 0, nullptr);
            return;
        }
        const int offsets[] { kOffsetSource, kOffsetDest, kOffsetDetail };
        for (int row = 0; row < n::kRows; ++row)
            for (int action = 0; action < n::kRowNavigation; ++action)
            {
                const int field = n::kHeaderNavigation + row * n::kRowNavigation + action;
                if (take (field) && value >= 0.5f)
                {
                    matrix_.selectRow (matrix_.firstRow() + row);
                    handle (ui::Change::request (kRowBase + 10 * row + offsets[action]), stack, inst);
                    inst.setNativeModNavigation (0, 0, 0, nullptr);
                    return;
                }
            }
    }

    void publishNativeMatrix (IInstanceView& inst, bool consume)
    {
        namespace n = nativeui::mod;
        const auto view = nativeView();
        int rows[n::kRows] {};
        nativeCells_.fill ({});
        auto link = [&] (int field, int target, int kind)
        {
            nativeCells_[field] = { target, target, kind };
        };
        auto linkRow = [&] (int base, const synthyboy::ModSlotParams& sp)
        {
            link (base + n::Amount, sp.amount, nativeui::kFxBipolar);
            link (base + n::Polarity, sp.bipolar, nativeui::kFxToggle);
            link (base + n::Bypass, sp.bypass, nativeui::kFxToggle);
        };
        if (view == n::Matrix || view == n::MatrixScroll)
        {
            for (int row = 0; row < n::kRows; ++row)
            {
                const int slot = matrix_.slotAtWindowIndex (row);
                if (slot < 0)
                    continue;
                rows[row] = slot + 1;
                linkRow (row * n::kRowFields, synthyboy::modSlotParams (slot));
            }
        }
        else if ((view == n::DetailDelay || view == n::DetailBeats) && openSlot_ >= 0)
        {
            const auto sp = synthyboy::modSlotParams (openSlot_);
            linkRow (n::kDetail, sp);
            link (n::kDetail + n::AuxScale, sp.auxOutput, nativeui::kFxDial);
            link (n::kDetail + n::AuxInvert, sp.auxInv, nativeui::kFxToggle);
            link (n::kDetail + n::AuxCurve, sp.auxPower, nativeui::kFxBipolar);
            link (n::kDetail + n::ShapeCurve, sp.power, nativeui::kFxBipolar);
            link (n::kDetail + n::Stereo, sp.stereo, nativeui::kFxToggle);
            link (n::kDetail + n::Rise, sp.rise, nativeui::kFxDial);
            link (n::kDetail + n::Fall, sp.fall, nativeui::kFxDial);
            link (n::kDetail + n::Delay, view == n::DetailBeats ? sp.delayBeats : sp.delay, nativeui::kFxDial);
            link (n::kDetail + n::DelaySync, sp.delaySync, nativeui::kFxToggle);
        }
        if (nativeGestureField_ >= 0)
        {
            auto& held = nativeCells_[nativeGestureField_];
            nativeGestureBlocked_ |= nativeGestureTarget_ != held.target;
            if (nativeGestureBlocked_)
                held = {};
        }
        inst.setNativeModMatrix (view, nativeCells_.data(), rows);
        if (! consume || view == n::Hidden)
            return;

        auto apply = [&] (int field)
        {
            const auto cell = nativeCells_[field];
            float value = 0.0f;
            if (cell.target < 0 || ! inst.takeNativeModField (field, cell, value))
                return false;
            uibind::writeNorm (inst, cell.target, value);
            return true;
        };
        const int sync = n::kDetail + n::DelaySync;
        const bool delayChanged = apply (sync);
        for (int field = 0; field < n::kFields; ++field)
            if (field != sync && (field != n::kDetail + n::Delay || ! delayChanged))
                apply (field);
    }

    void publishNativeBoard (IInstanceView& inst, UiStack& stack, bool consume)
    {
        namespace nb = nativeui::modboard;
        if (nativeControls_ && pageTop_ == kPageList)
        {
            std::array<nativeui::FxCell, nb::kHandles> fields {};
            const int count = static_cast<int> (listChoices_.size());
            nativeListFirst_ = std::clamp (nativeListFirst_, 0, std::max (0, count - 1));
            const int identity = listParam_ >= 0 ? listParam_ + 1 : targetsSource_ + 4000;
            for (int row = 0; row < nb::kListRows; ++row)
            {
                const int index = nativeListFirst_ + row;
                if (index >= count) break;
                fields[nb::ListRow + row] = { nb::kChoiceBias + listChoices_[index], identity,
                                            destList() ? nativeui::kFxToggle : nativeui::kFxDial };
                fields[nb::ListRowGate + row] = { 1, 1, nativeui::kFxToggle };
            }
            for (int field : { nb::ListBack, nb::ListPrev, nb::ListNext, nb::ListGroups, nb::ListGate, nb::ListTitle })
                fields[field] = { listGroup_ + 1, identity + nativeListFirst_, nativeui::kFxToggle };
            inst.setNativeModBoard (2, fields.data());
            if (! consume) return;
            for (int field = nb::ListRow; field < nb::ListGate; ++field)
            {
                float value = 0.0f;
                if (! inst.takeNativeModBoard (field, fields[field], value) || value < 0.5f) continue;
                if (field < nb::ListBack)
                    handle (ui::Change::indexed (kListPopup, nativeListFirst_ + field - nb::ListRow, value), stack, inst);
                else if (field == nb::ListBack)
                    handle (ui::Change::request (kListBackdrop), stack, inst);
                else if (field == nb::ListGroups)
                    handle (ui::Change::request (kListHeader), stack, inst);
                else
                    nativeListFirst_ = std::clamp (nativeListFirst_ + (field == nb::ListPrev ? -nb::kListRows : nb::kListRows),
                                                  0, std::max (0, ((count - 1) / nb::kListRows) * nb::kListRows));
                inst.setNativeModBoard (0, nullptr);
                return;
            }
            return;
        }
        if (! nativeControls_ || pageTop_ != 0 || ! matrix_.boardShown())
        {
            inst.setNativeModBoard (0, nullptr);
            return;
        }

        std::array<nativeui::FxCell, nb::kHandles> fields {};
        fields[nb::Source] = { 0, 0, nativeui::kFxDial };
        fields[nb::Group] = { 0, srcFilter_, nativeui::kFxDial };
        fields[nb::Gate] = { 1, 1, nativeui::kFxToggle };
        inst.setNativeModBoard (1, fields.data());
        if (! consume)
            return;
        float allSources = 0.0f;
        if (inst.takeNativeModBoard (nb::Group, fields[nb::Group], allSources) && allSources >= 0.5f)
        {
            listKind_ = ListKind::BoardSource;
            listSlot_ = listParam_ = listGroup_ = -1;
            fillList (0);
            if (stack.openDetail (kPageList)) pageTop_ = kPageList;
            inst.setNativeModBoard (0, nullptr);
            return;
        }
        float value = 0.0f;
        if (inst.takeNativeModBoard (nb::Source, fields[nb::Source], value))
        {
            const int totalSources = static_cast<int> (synthyboy::modSourceCount());
            int sourceId = static_cast<int> (std::lround (value * static_cast<float> (totalSources)));
            if (sourceId < 1)
                sourceId = static_cast<int> (std::lround (value));
            if (sourceId >= 1 && sourceId <= totalSources)
            {
                beginTargets (inst, stack, sourceId);
                inst.setNativeModBoard (0, nullptr);
            }
        }
    }

    // ---- the quick-add board: every source the engine has, in groups (ui_bind.h sourceGroup), in the order of the engine's table
    /** The text of a chip and the name that picks its colour: the same functions as the board of the page of one control (ui_bind.h). */
    static void boardLabel (int choice, char* out, size_t cap) { uibind::boardLabel (choice, out, cap); }
    static void boardColourName (int choice, char* out, size_t cap) { uibind::boardColourName (choice, out, cap); }

    // ---- the targets page of one source (the board's tap, ADD TARGET): the slots whose source it is
    bool targetsShown() const noexcept { return pageTop_ == kPageTargets || (pageTop_ == kPageList && listKind_ == ListKind::NewTarget); }
    void syncTop (const UiStack& stack) noexcept
    {
        pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        if (pageTop_ == kPageTargets && targetsSource_ <= 0)
            targetsSource_ = 1;
    }
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        constexpr int kPer = modmodel::Targets::kPerPage;
        const int total = uibind::slotsOfSource (v, targetsSource_, scratchSlots_, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + kPer - 1) / kPer);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * kPer, n = std::clamp (total - first, 0, kPer);
        modmodel::Targets::Entry e[kPer];
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
            same = targets_.sameRow (i, e[i]);
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
    void addTargetSlot (IInstanceView& inst, int source, int destChoice)
    {
        const int slot = uibind::addTargetSlot (inst, source, destChoice);
        if (slot < 0)
            return;
        constexpr int rows = modmodel::Targets::kPerPage;
        const int position = uibind::targetPosition (inst, source, slot);
        targetsPageIndex_ = position / rows;
        loadTargets (inst, true);
        targets_.selectRow (position % rows);
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
        detail_.setSlot (openSlot_);
        (void) srcVal; (void) dstVal; (void) live;
        modmodel::Detail::Values& v = detail_.values;
        v.auxOutput = norm (inst, sp.auxOutput);
        v.auxPower = norm (inst, sp.auxPower);
        v.auxInv = inst.param (sp.auxInv) > 0.5f;
        v.power = norm (inst, sp.power);
        v.stereo = inst.param (sp.stereo) > 0.5f;
        v.rise = norm (inst, sp.rise);
        v.fall = norm (inst, sp.fall);
        v.delay = norm (inst, sp.delay);
        v.delayBeats = norm (inst, sp.delayBeats);
        detail_.setDelaySync (inst.param (sp.delaySync) > 0.5f);
    }
    /** The normalised value of parameter p (0 when the engine has none). */
    static double norm (const IInstanceView& v, int p) { return p < 0 ? 0.0 : uibind::normOf (p, v.param (p)); }

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
            }
            else
            {
                for (int c = 1; c <= static_cast<int> (synthyboy::modSourceCount()); ++c)
                    if (uibind::sourceGroup (c) == listGroup_ + 1)
                    {
                        listNames_.push_back (uibind::sourceName (c));
                        listChoices_.push_back (c);
                    }
            }
        }
        nativeListFirst_ = 0;
        (void) current;
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
        if (c.id == kFilterAll) { matrix_.setFilterMode (modmodel::Matrix::FilterAll); return; }
        if (c.id == kFilterSource) { matrix_.setFilterMode (modmodel::Matrix::FilterSource); stepSourceFilter (0); return; }
        if (c.id == kFilterDest) { matrix_.setFilterMode (modmodel::Matrix::FilterDest); stepDestFilter (0); return; }
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
        if (c.id >= kTargetsRow && c.id < kTargetsRow + 4 * modmodel::Targets::kPerPage)
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
                pullDetail (inst, nullptr);
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
            if (listKind_ == ListKind::BoardSource)
            {
                if (sel <= 0) return;
                stack.back();
                syncTop (stack);
                beginTargets (inst, stack, sel);
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
        if (c.id >= kRowBase && c.id < kRowBase + modmodel::Matrix::kWindowRows * 10)
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
            {
                openSlotDetail (slot, stack);
                pullDetail (inst, nullptr);
            }
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
        matrix_.setSourceFilterChoice (srcFilter_);
    }
    void stepDestFilter (int)
    {
        dstFilter_ = std::clamp (dstFilter_, 0, 10);
        matrix_.setDestModuleFilter (dstFilter_ - 1);
    }

    /** The change of the Q-Link `knobIndex` on the page shown: the matrix steps the row (knob 1) and writes the AMOUNT of the selected row (knob 4), the page of a slot and the targets page write their active block / row. */
    ui::Change turnChange (int knobIndex, double delta)
    {
        if (pageTop_ == kPageDetail)
            return detail_.turnQ (knobIndex, delta, { kDetailAuxOutput, kDetailAuxInv, kDetailAuxPower, kDetailPower, kDetailStereo, kDetailRise, kDetailFall, kDetailDelay, kDetailDelayBeats, kDetailDelaySync });
        if (pageTop_ == kPageTargets)
            return targets_.turnQ (knobIndex, delta, kTargetsRow);
        if (pageTop_ != 0)
            return {};
        const int w = matrix_.selectedWindowRow();
        if (knobIndex == 0)
        {
            ui::Change c = matrix_.turnRow (delta);
            if (c)
                matrix_.selectRow (c.index);
            return {};
        }
        if (knobIndex == 3 && w >= 0)
        {
            const double cur = matrix_.amountNorm (w), v = std::clamp (cur + delta, 0.0, 1.0);
            return std::fabs (v - cur) < 1.0e-12 ? ui::Change {} : ui::Change::normalised (kRowBase + w * 10 + kOffsetAmount, v);
        }
        return {};
    }

    /** The groups of the page in reading order: a targets list has one group per row, the slot detail its three blocks (AUXILIARY SOURCE, SHAPE, TIME), the matrix the selected slot. The active group is on Q 1 .. 4, the
        next three (wrapping) on 5 .. 16. */
    void publishQGroup (IInstanceView& inst)
    {
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
            qSlots_.publish (inst, targets_.numRows(), targets_.activeRow(), [this] (int row, int* t) { modRowTargets (row < modmodel::Targets::kPerPage ? targetRows_[row].slot : -1, t); });
        }
        else if (pageTop_ == kPageDetail && openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (openSlot_);
            std::snprintf (qName_, sizeof qName_, "SLOT %02d %s", openSlot_ + 1, modmodel::Detail::blockName (detail_.activeBlock()));
            const int active = static_cast<int> (detail_.activeBlock());
            qSlots_.publish (inst, 3, active, [&] (int block, int* t)
            {
                if (block == modmodel::Detail::BlockAux)
                {
                    t[0] = sp.auxSource; t[1] = sp.auxOutput; t[2] = sp.auxInv; t[3] = sp.auxPower;
                }
                else if (block == modmodel::Detail::BlockShape)
                {
                    t[0] = sp.power; t[1] = sp.stereo;
                }
                else
                {
                    t[0] = sp.rise; t[1] = sp.fall; t[2] = detail_.delaySyncOn() ? sp.delayBeats : sp.delay; t[3] = sp.delaySync;
                }
            });
        }
        else
        {
            const int sel = matrix_.selectedSlot();
            if (sel >= 0 && sel < synthyboy::kNumModSlots)
            {
                std::snprintf (qName_, sizeof qName_, "SLOT %02d", sel + 1);
                const int row = matrix_.selectedRow();
                const int s1 = matrix_.slotOfVisibleRow (row + 1), s2 = matrix_.slotOfVisibleRow (row + 2);
                qSlots_.publish (inst, 4, 0, [&] (int g, int* t) { matrixGroupTargets (g, sel, s1, s2, kVirtualRow, t); });
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

    modmodel::Matrix matrix_;
    modmodel::Detail detail_;
    modmodel::Targets targets_;
    int nativeListFirst_ = 0;
    ModTargetsNative nativeTargets_;

    // the targets page of a source: which source, the page of its rows, the slot of every row shown
    int targetsSource_ = 0, targetsLoaded_ = 0, targetsPageIndex_ = 0, targetTotal_ = -1;
    uibind::ModRow targetRows_[modmodel::Targets::kPerPage];
    int scratchSlots_[uibind::kMaxTargetSlots] = {};

    ListKind listKind_ = ListKind::Source;
    int listSlot_ = -1, listParam_ = -1, listGroup_ = -1;
    std::vector<const char*> listNames_;
    std::vector<int> listChoices_;   // per row: the choice value, 0 NONE, a negative number a group

    bool gestureDown_ = false;

    QSlots qSlots_;
    char qName_[40] = "MOD MATRIX";
    bool nativeControls_ = false;
    bool nativeGround_ = false;
    std::array<nativeui::FxCell, nativeui::mod::kFields> nativeCells_ {};
    int nativeGestureField_ = -1, nativeGestureTarget_ = -1;
    bool nativeGestureBlocked_ = false;
    uint64_t stateGeneration_ = 0;
};

}} // namespace synthyboy_mpc::overlay
