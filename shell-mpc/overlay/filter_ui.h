// SynthyBoy - MPC overlay: the FILTER UI, the page of the ONE Filter tab (native controls and live overlay, page_view.h): the plugin-drawn pages built from the UI library (shell-mpc/ui/filter_display.h, panel.h, list.h)
// and wired to the real engine parameters (filterN.* of the table and filter.routing). One object per (page view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () /
// touch () / tick () / draw (), with the instance as IInstanceView. Docs: docs/MPC.md "Filter UI".
// FOUR STATES (the chunk-only view state mpcTabView.filter, IInstanceView::tabView (kTabViewFilter)): 0 F1, 1 F2 (the controls edit that filter; the F1+F2 toggle is a VIEW only: both curves and the resultant are drawn),
// 2 NOTE, 3 VELO: the scalar curve editor of the source (ScalarCurveUi, ui/scalar_curve_editor.h) in the body under the top bar; its TARGETS button opens the ModTargetsPage of the source (23 velocity, 24 note).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementFilter, the pages above it are ours): the page (FilterFullScreen) is always open; detail pages above it: the list of the models or of the styles
// (ListPopup), the modulations of one control (ModulationsPage, a long press on a knob), the targets of NOTE / VELO (ModTargetsPage) and the list that picks a new target. LEVEL, PAN, DEST, BUS 1 and BUS 2 are read-only
// here (the Mixer is their home); BLEND and ROUTING are editable.
// The display shows the response the filter really has: shell-mpc/overlay/curve_source.h runs the engine's own filter on an impulse (once per change, with the live, modulated cutoff and resonance of the voice that is
// heard); F1 + F2 shows both and the resultant by the routing. What it does with a touch: the library component gives a Change { id, kind, value }; the id says which control it was, the binding of the id (a key of the
// engine table) says which parameter; the write goes through IInstanceView::writeParam (normalised; the plugin tells the host). Every update () reads every parameter back, so host automation, presets and the host's
// own widgets show on the next frame.
// Q groups (one block each, q_router.h): MAIN CUTOFF RESO DRIVE MIX; TRACK KEYTRACK ENV 1 VAR (Formant: RESONANCE) COMB (Comb only); OUT BLEND ROUTING MODEL STYLE (frameless: MODEL and STYLE are in the top bar);
// FORMANT X Y TRANSPOSE SPREAD (the Formant model). NOTE / VELO: EDIT [POINT X Y CURVE], LEGATO AND GRID [LEGATO GRID X GRID Y SNAP]. A tap on a member selects its block.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../q_router.h"
#include "../tab_view.h"
#include "../ui/choice_names.h"
#include "../ui/mod_targets.h"
#include "../ui/play_rows.h"
#include "../ui/pz_curve_editor.h"
#include "../ui/ui.h"
#include "backgrounds_manifest.h"
#include "curve_source.h"
#include "native_marker_covers.h"
#include "page_view.h"
#include "peek_module.h"
#include "q_publish.h"
#include "q_publish_mod.h"
#include "scalar_curve_ui.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "native_mod_targets_ui.h"
#include "modulation_window.h"
#include "ui_stack.h"
#include "synthyboy/filter_response.h"
#include "synthyboy/filters/filter_types.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class FilterUi : public PeekModule
{
public:
    // ---- the pages (UiStack ids)
    static constexpr int kElementFilter = 4;      // the page of a filter tab (the oscillator is 1, the envelope 2, the LFO 3)
    static constexpr int kElementPage = kElementFilter;
    enum Page { kPageMods = 22, kPageTargets = 23, kPageDest = 24, kPagePz = 25 };

    // ---- control IDs (kView: Index = the state + 1, the candidate id of the selector)
    enum Id : int
    {
        kClose = 40, kPower, kView, kBoth, kModel, kStyle, kClean, kExpand, kScalarView,
        kCutoff = 100, kResonance, kDrive, kMix,
        kRouting = 110, kBlend,
        kKeytrack = 120, kEnv1, kVar, kCombOffset,
        kFormantX = 130, kFormantY, kFormantTrans, kFormantRes, kFormantSpread,
        kDisplayCutoff = 150, kDisplayFormant, kDisplayReso, kDisplayFormantY,   // the ids of the display's changes: the cutoff (x) and the resonance (y) of the finger, the formant X and Y
        kDestList = 310, kDestBackdrop, kDestHeader,
        kModsBack = 400, kModsRow = 410, kModsAddStep = 460, kModsAdd, kModsPage,
        kTargetsBack = 500, kTargetsPage, kTargetsAdd, kTargetsRow = 510,   // 510 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kPzOpen = 600, kPzBack, kPzReset, kPzRemove, kPzApply, kPzCanvas
    };

    using ModRow = uibind::ModRow;
    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kModelFormant = 5, kModelComb = 6, kModelPz = 21;   // the model indices shared by the engine and the patch format
    static constexpr double kModsRefreshMs = 100.0;

    explicit FilterUi (int peekFilter = -1, int standaloneFilter = -1)
        : peekFilter_ (peekFilter), standaloneFilter_ (standaloneFilter)
    {
        curve_[0].configure (0);   // velocity (the state VELO)
        curve_[1].configure (1);   // note (the state NOTE)
        setCanvas (kCanvasW, kCanvasH);
        assignIds();
        full_.setPageMode (true);
        if (peekFilter_ >= 0)
            full_.setPeekHeader (kCloseWidth);
        if (standaloneFilter_ >= 1 && standaloneFilter_ <= 2)
            adoptSplitState (standaloneFilter_ - 1, scalarSel_, tabview::kFilterExpandedFilter);
        else
            syncLayoutMode();
    }

    void setStandaloneFilter (int filterNum) noexcept
    {
        standaloneFilter_ = filterNum;
        if (standaloneFilter_ >= 1 && standaloneFilter_ <= 2)
            adoptSplitState (standaloneFilter_ - 1, scalarSel_, tabview::kFilterExpandedFilter);
    }
    int standaloneFilter() const noexcept { return standaloneFilter_; }
    bool isStandalone() const noexcept { return standaloneFilter_ >= 1 && standaloneFilter_ <= 2; }

    // Native controls are the only base-control owner; there is no fallback switch.
    bool hasNativePower() const noexcept { return true; }
    void setHostBackground (bool on) noexcept { nativeGround_ = on; }
    bool hostBackground() const noexcept
    {
        return nativeGround_ && nativeControlsActive() && w_ == page::kW && h_ == page::kH
            && &ui::theme::L() == &ui::theme::kLook1;
    }
    ui::Rect nativePowerRect() const noexcept { return full_.power.rect(); }
    int nativeViewFrame() const noexcept
    {
        if (! hasNativePower() || pageTop_ != 0)
            return 0;
        if (curveView())
            return scalarSel_ == 0 ? 3 : 4;
        return filterSel_ + 1;
    }

    ui::Component& peekRoot() noexcept override { return full_; }
    void placePeek (ui::Rect panel) override
    {
        full_.place (panel);
        mods_.place (nativePeek_ ? ui::Rect { 0, kTabY, w_, kTabH } : panel);
    }
    void setNativePeek (bool enabled) noexcept override
    {
        nativePeek_ = enabled;
        full_.setNativePeekGeometry (enabled);
    }
    bool nativePeekReady() const noexcept override { return nativePeek_ && nativeControlsActive(); }
    ui::Rect nativePeekCloseRect() const noexcept override
    {
        const auto view = full_.view.rect();
        return { view.x, view.y, kCloseWidth, view.h };
    }
    void clearNativePeekApertures (gpu::SceneBuilder& sb) const override
    {
        if (nativePeekReady())
            for (const auto area : nativeApertures (true))
                ui::clearNativeCell (sb, area);
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        full_.place (tab);
        syncLayoutMode();
        destList_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        destList_.setPopup (tab);
        destList_.setId (kDestList);
        destList_.setBackdropId (kDestBackdrop);
        destList_.setHeaderId (kDestHeader);
        mods_.place (tab);
        targets_.place (tab);
        pzEditor_.place (tab);
    }

    /** Reads the instance: the state of the page (F1, F2, NOTE, VELO: `mpcTabView.filter`), the real response curves, every value of the controls, the modulations of the knobs, the pages of the stack, the Q group.
        `in.targets` the live cutoff and resonance of both filters (cutoff 1, resonance 1, cutoff 2, resonance 2). */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in) override
    {
        pzToolbarSupported_ = inst.nativePzToolbarSupported();
        nowMs_ = nowMs;
        const uint64_t generation = inst.stateGeneration();
        if (generation != stateGeneration_)
        {
            blockedNativeSlot_ = nativeTouchSlot_;
            hideNativeControls (inst);
            stateGeneration_ = generation;
            dropGesture();
            qSlots_.forget();
            modsDirty_ = true;

            if (pageTop_ == kPageTargets || pageTop_ == kPageDest)
            {
                activeModParam_ = -1;
                while (stack.depth() > (peekFilter_ >= 0 ? 2 : 1))
                    const_cast<UiStack&> (stack).back();
            }
            syncPages (stack);
        }
        const int rawView = inst.tabView (IInstanceView::kTabViewFilter);
        int targetFilter = 0, targetScalar = 0, targetExpand = tabview::kFilterSplit;
        tabview::decodeFilterSplit (rawView, targetFilter, targetScalar, targetExpand);
        if (peekFilter_ >= 0)
        {
            targetFilter = peekFilter_;
            targetExpand = tabview::kFilterExpandedFilter;
        }
        else if (standaloneFilter_ >= 1 && standaloneFilter_ <= 2)
        {
            targetFilter = standaloneFilter_ - 1;
            targetExpand = tabview::kFilterExpandedFilter;
        }
        else if (in.instance == 2)
        {
            targetFilter = 1;
        }
        adoptSplitState (targetFilter, targetScalar, targetExpand);
        if (stack.top() == kPageMods)
        {
            if (! modsDestinationValid (inst))
                const_cast<UiStack&> (stack).back();
            else if (modsPreset_ != generation)
            {
                modsPreset_ = generation;
                mods_.setPickingSource (false);
                loadMods (inst);
            }
        }
        stackDepth_ = stack.depth();
        syncPages (stack);
        if (pageTop_ == kPagePz)
        {
            if (! pzTargetAvailable (inst))
                closePz (const_cast<UiStack&> (stack));
            else if (pzGeneration_ != generation)
                reloadPz (inst);
            else if (nativePzToolbar())
                takeNativePzToolbar (inst, const_cast<UiStack&> (stack));
        }
        envSel_ = inst.playSelection (1);
        lfoSel_ = inst.playSelection (2);
        const int lastSel = inst.playLastSelector();
        selectedSource_ = lastSel == 1 ? uibind::envSource (envSel_) : uibind::lfoSource (lfoSel_);

        if (! curve1_ || ! curve2_ || inst.sampleRate() != sampleRate_)
        {
            sampleRate_ = inst.sampleRate();
            CurveSource::Config c1;
            c1.sampleRate = sampleRate_;
            c1.filter = 1;
            CurveSource::Config c2 = c1;
            c2.filter = 2;
            curve1_ = std::make_unique<CurveSource> (c1);
            curve2_ = std::make_unique<CurveSource> (c2);
        }
        if (filterShown())
        {
            const bool hasTargets = in.targets != nullptr;
            curve1_->update (inst, hasTargets ? in.targets : nullptr, nowMs);
            curve2_->update (inst, hasTargets ? in.targets + 2 : nullptr, nowMs);
            if (isDual())
                computeCombinedCurve (inst);
            updateDisplayData (inst, in.live);
            pullControls (inst);
        }
        if (curveShown())
        {
            ScalarCurveUi& cu = curveUi();
            cu.pull (inst, nowMs);
            cu.updateVirtuals (inst, nowMs);
        }
        pzButtonShown_ = isPzModel (inst);
        pzOpen_.setEnabled (pzButtonShown_ && pzPowered (inst));
        if (pzButtonShown_)
        {
            if (! full_.expand.rect().empty())
                pzOpen_.place ({ full_.expand.rect().x - 64, full_.expand.rect().y, 56, 56 });
            else
                pzOpen_.place (ui::playrows::fullRect (full_.displayRect()));
        }
        full_.both.state.on = dual_;
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }

        auto& mutableStack = const_cast<UiStack&> (stack);
        if (peekFilter_ < 0 || nativePeek_)
            updateNativeControls (inst, mutableStack);
        nativeTargets_.update (inst, targets_, peekFilter_ < 0 && pageTop_ == kPageTargets, nativeui::targets::Filter,
                               curveSource(), targetRows_, [&] (ui::Change change) { handle (change, mutableStack, inst); });
        const auto destination = activeModParam_ >= 0
            ? synthyboy::modDestinationChoiceOf (static_cast<size_t> (activeModParam_)) : 0;
        const int pageBeforeMods = pageTop_;
        nativeModulations_.update (inst, mods_, (peekFilter_ < 0 || nativePeek_) && ! mods_.boardShown() && modsContextValid (inst),
            peekFilter_ >= 0 ? nativeui::targets::Mixer : nativeui::targets::Filter, static_cast<int> (destination), modRows_,
            [&] (ui::Change change) { handle (change, mutableStack, inst); }, nativeui::targets::Modulations);
        syncPages (stack);
        if (pageBeforeMods == kPageMods && pageTop_ == 0 && (peekFilter_ < 0 || nativePeek_))
            updateNativeControls (inst, mutableStack);
        const bool pzNative = nativePzToolbar();
        pzEditor_.setNativeToolbar (pzNative);
        inst.setNativePzToolbar (pzNative ? pzFilter_ : 0, pzNative ? pzToolbarAvailability() : nativeui::pz::None);
        // The Mixer owns the host widget gate while the Filter is embedded.
        if (peekFilter_ < 0)
            inst.setUiFullOpen (nativeViewFrame() == 0);
        publishQGroup (inst);
    }

    int view() const noexcept { return view_; }                       // 0 F1, 1 F2, 2 NOTE, 3 VELO (expanded compat)
    int filterSelection() const noexcept { return filterSel_; }
    int scalarSelection() const noexcept { return scalarSel_; }
    int expandMode() const noexcept { return expandMode_; }
    bool isSplit() const noexcept { return peekFilter_ < 0 && ! isStandalone() && expandMode_ == tabview::kFilterSplit; }
    bool curveView() const noexcept { return peekFilter_ < 0 && ! isStandalone() && expandMode_ == tabview::kFilterExpandedCurve; }
    bool curveShown() const noexcept { return isSplit() || curveView(); }
    bool filterShown() const noexcept { return isSplit() || ! curveView(); }
    int editFilter() const noexcept { return editFilter_; }           // 1 or 2: the filter the knobs belong to
    bool isDual() const noexcept { return dual_ && ! isSplit() && ! curveView(); }   // the F1+F2 view toggle
    /** The scalar curve editor of the selected scalar source (NOTE when scalarSel_ == 0, VELO when scalarSel_ == 1). */
    ScalarCurveUi& curveUi() noexcept { return curve_[scalarSel_ == 0 ? 1 : 0]; }
    const ScalarCurveUi& curveUi() const noexcept { return curve_[scalarSel_ == 0 ? 1 : 0]; }
    ScalarCurveUi& curveUiOf (int which) noexcept { return curve_[which == 1 ? 1 : 0]; }
    const int* qTargets() const noexcept override { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept override { return qName_; }
    int topPage() const noexcept override { return pageTop_; }
    ui::Rect displayRect() const noexcept { return full_.displayRect(); }
    void leftTab (IInstanceView& inst) noexcept override
    {
        nativeTargets_.hide (inst, targets_);
        nativeModulations_.hide (inst, mods_);
        pzOwner_ = nullptr;
        hideNativeControls (inst);
        inst.setNativePzToolbar (0, nativeui::pz::None);
    }

    // The Filter and its PEEK variant already own their ground inside draw().
    void drawPageGround (gpu::SceneBuilder&) const noexcept {}

    /** The Filter foreground, including its opaque fallback or full-screen detail. */
    void draw (gpu::SceneBuilder& sb) const override
    {
        if (peekFilter_ < 0)
        {
            if (hostBackground())
                coverRootMarkers (sb);
            else if (nativePowerVisible() || nativeControlsActive())
            {
                const auto& look = ui::theme::L();
                ui::Pen p (sb, true, false);
                ui::outsideNativeCells ({ 0, kTabY, w_, kTabH }, nativeApertures (true), [&] (ui::Rect r)
                {
                    ui::Cell cell (sb, r, 0);
                    p.paintY2 (kTabY, kTabY + kTabH, look.bg0, look.bg1, 255);
                    p.rect (r, p.c (look.bg1, 255));
                    p.paintOff();
                });
            }
            else
                page::ground (sb);
        }
        if (pageTop_ == kPageMods)
        {
            mods_.draw (sb);
            return;
        }
        if (pageTop_ == kPagePz)
        {
            pzEditor_.draw (sb);
            if (nativePzToolbar())
                for (const auto area : pzEditor_.toolbarRects())
                    ui::clearNativeCell (sb, area);
            return;
        }
        if (pageTop_ == kPageTargets || pageTop_ == kPageDest)
        {
            targets_.draw (sb);
            if (pageTop_ == kPageDest)
                destList_.draw (sb);
            return;
        }
        full_.draw (sb);
        if (pageTop_ == 0 && pzButtonShown_)
        {
            if (nativePzOpen())
            {
                const auto r = pzOpen_.rect();
                const gpu::ScopedClip clip (sb, r.x, r.y, r.right(), r.bottom());
                if (clip)
                    sb.clear (0u);
            }
        }
        if (curveShown())
        {
            const ui::ScalarCurveEditor& ed = curveUi().screen;
            ed.draw (sb);
            if (isSplit() && pageTop_ == 0)
            {
                const ui::Rect eb = ed.block (0);
                if (splitQBank_ == 3)
                    ed.groups[0].draw (sb, nativeControlsActive() ? &eb : nullptr);
                else
                    ed.groups[0].drawIdle (sb);
            }
            if (pageTop_ == 0)
                ed.drawLive (sb);   // the live marker of the last note: the animated layer, nothing cached repaints with it
        }
    }

    bool covers (int x, int y) const noexcept override
    {
        if ((pageTop_ == kPageTargets && targets_.nativeHit (x, y))
            || (pageTop_ == kPageMods && mods_.nativeHit (x, y)))
            return false;
        if (nativePzToolbar())
            for (const auto& area : pzEditor_.toolbarRects())
                if (area.contains (x, y))
                    return false;
        for (const auto& area : nativeApertures())
            if (area.contains (x, y))
                return false;
        if (nativePeek_ && pageTop_ == 0)
            return full_.rect().contains (x, y);
        return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y);
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs) override
    {
        nowMs_ = nowMs;
        const int x = ev.x, y = ev.y;
        if (ev.kind == TouchEvent::Down)
        {
            int targetRow = -1;
            const bool modulations = pageTop_ == kPageMods;
            if (modulations ? mods_.nativeHit (x, y, &targetRow)
                            : pageTop_ == kPageTargets && targets_.nativeHit (x, y, &targetRow))
            {
                dropGesture();
                if (targetRow >= 0)
                {
                    if (modulations)
                        mods_.selectRow (targetRow);
                    else
                        targets_.selectRow (targetRow);
                    publishQGroup (inst);
                }
                return false;
            }
            blockedNativeSlot_ = -1;
            gesture_ = nullptr;
            gestureDown_ = false;
            ui::Component* top = topComponent (x, y);
            if (! covers (x, y))
            {
                observeNativeTouch (x, y, nowMs);
                return false;
            }
            if (! top)
                return false;
            gesture_ = top;
            gestureDown_ = true;
            if (isSplit() && pageTop_ == 0)
            {
                if (top == &curveUi().screen)
                {
                    if (curveUi().screen.block (0).contains (x, y) || curveUi().screen.canvas.rect().contains (x, y))
                        splitQBank_ = 3;
                }
                else if (top == &full_)
                {
                    const int beforeG = full_.activeGroup();
                    const ui::Change ch = top->touchDown (x, y, nowMs);
                    if (full_.activeGroup() != beforeG && full_.activeGroup() >= 0 && full_.activeGroup() < 3)
                        splitQBank_ = full_.activeGroup();
                    dispatch (ch, stack, inst);
                    handlePending (stack, inst);
                    syncSplitQFocus();
                    return true;
                }
            }
            dispatch (top->touchDown (x, y, nowMs), stack, inst);
            handlePending (stack, inst);
            syncSplitQFocus();
            return true;
        }
        if (nativeTouchSlot_ >= 0 || blockedNativeSlot_ >= 0)
        {
            if (ev.kind == TouchEvent::Up)
            {
                float stale;
                if (blockedNativeSlot_ >= 0)
                    inst.takeNativeFilterSlot (blockedNativeSlot_, {}, stale);
                nativeTouchSlot_ = blockedNativeSlot_ = nativeModControl_ = -1;
            }
            else if (std::abs (x - nativeTouchX_) + std::abs (y - nativeTouchY_) > 6)
                nativeModControl_ = -1;
            return false;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
        {
            dispatch (gesture_->touchMove (x, y), stack, inst);
            handlePending (stack, inst);
        }
        else
        {
            dispatch (gesture_->touchUp (x, y), stack, inst);
            gestureDown_ = false;
            gesture_ = nullptr;
        }
        return true;
    }

    void tick (double nowMs, UiStack& stack, IInstanceView& inst) override
    {
        nowMs_ = nowMs;
        if (curveShown() && pageTop_ == 0)
            curveUi().tick (inst, nowMs);   // the long press of the canvas, the throttled write that was left over
        if (gesture_ && gestureDown_)
            dispatch (gesture_->tick (nowMs), stack, inst);
        if (nativeTouchSlot_ >= 0 && nativeModControl_ >= 0 && nativeControlsActive() && nowMs - nativeTouchAt_ >= 500.0)
        {
            if (full_.power.state.on)
            {
                blockedNativeSlot_ = nativeTouchSlot_;
                handle (ui::Change::modsRequest (nativeModControl_), stack, inst);
                hideNativeControls (inst);
            }
            nativeModControl_ = -1;
        }
        drainCurveRequests (stack, inst);
    }

    void turnQ (int slot, double delta, UiStack& stack, IInstanceView& inst) override
    {
        if (pageTop_ == kPagePz)
            return;
        if (pageTop_ == kPageMods)
            handle (mods_.turn (delta), stack, inst);
        else if (pageTop_ == kPageTargets)
            handle (targets_.turnQ (slot, delta), stack, inst);
        else if (curveView() || (isSplit() && splitQBank_ == 3))
        {
            ScalarCurveUi& cu = curveUi();
            const int g = isSplit() ? 0 : cu.screen.activeGroup();
            if (g >= 0 && g < ui::ScalarCurveEditor::kMaxGroups)
                cu.handle (cu.screen.groups[g].turn (slot, delta), inst, nowMs_);
            drainCurveRequests (stack, inst);
        }
        else
            handle (full_.turnQ (slot, delta), stack, inst);
    }

    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the control of the page under the pointer takes it as steps of its own (a knob: one Q-Link step, a stepper, a list, a scrolled
        list; ui::Component::wheelOver). false: nothing of this page lies there, or a finger is down on it. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs) override
    {
        ui::Component* top = topComponent (x, y);
        if (! full_.power.state.on && filterShown() && pageTop_ == 0 && top == &full_)
        {
            const bool allowRouting = full_.anyFilterOn() && full_.routing.rect().contains (x, y);
            if (! full_.power.rect().contains (x, y) && ! full_.view.rect().contains (x, y)
                && ! full_.both.rect().contains (x, y) && ! full_.close.rect().contains (x, y)
                && ! full_.expand.rect().contains (x, y) && ! full_.scalarView.rect().contains (x, y)
                && ! allowRouting)
                return false;
        }
        if (gestureDown_ || ! top || ! covers (x, y))
            return false;
        ui::Change ch;
        if (! top->wheelOver (x, y, notches, ch))
            return false;
        nowMs_ = nowMs;
        if (top == &curveUi().screen && curveShown())
        {
            curveUi().handle (ch, inst, nowMs);
            drainCurveRequests (stack, inst);
        }
        else
            handle (ch, stack, inst);
        return true;
    }

    bool fingerDown() const noexcept override { return gestureDown_ || nativeTouchSlot_ >= 0; }
    void dropGesture() noexcept override
    {
        if (gesture_ != nullptr)
            gesture_->touchUp (-1, -1);
        gesture_ = nullptr;
        gestureDown_ = false;
        nativeTouchSlot_ = nativeModControl_ = -1;
    }
    /** Another element's page was open: the Q group is published again at the next update. */
    void forgetQGroup() noexcept override { qSlots_.forget(); }

    ui::FilterFullScreen& full() noexcept { return full_; }
    ui::ModulationsPage& modsPage() noexcept { return mods_; }
    ui::ModTargetsPage& targetsPage() noexcept { return targets_; }
    ui::ListPopup& destList() noexcept { return destList_; }
    const CurveSource* curve (int filter) const noexcept { return filter == 2 ? curve2_.get() : curve1_.get(); }
    const ui::MenuButton& pzEditorButton() const noexcept { return pzOpen_; }
    const ui::PzCurveEditor& pzEditor() const noexcept { return pzEditor_; }
    int paramOf (int id, int filter) const noexcept { return paramForId (id, filter); }
    const ModRow* modRows() const noexcept { return modRows_; }

private:
    /** Restore the scanout colours behind the marker pixels. The Filter asset
        has the factory gradient and one deliberately different sentinel patch. */
    static void coverRootMarkers (gpu::SceneBuilder& sb) noexcept
    {
        drawNativeMarkerCovers (sb, "filter1", 0x0b1026, 0x0b1026);
        if (const auto* bg = backgroundOf ("filter1"))
        {
            const int x = bg->sentinelX, y = kTabY + bg->sentinelY;
            sb.rect (x, y, x + 5, y + 1, gpu::rgba8FromRgb (0x0a0f23, 255));
            sb.rect (x, y + 1, x + 5, y + 5, gpu::rgba8FromRgb (0x0a0f22, 255));
        }
    }

    bool nativePzOpen() const noexcept { return pzButtonShown_ && full_.power.state.on && nativeControlsActive(); }
    bool nativePzToolbar() const noexcept
    {
        return pzToolbarSupported_ && peekFilter_ < 0 && hasNativePower() && pageTop_ == kPagePz && pzOwner_ != nullptr
            && w_ == page::kW && h_ == page::kH;
    }

    int pzToolbarAvailability() const noexcept
    {
        namespace pz = nativeui::pz;
        const bool remove = pzEditor_.remove.enabled(), apply = pzEditor_.apply.enabled();
        return remove ? (apply ? pz::Both : pz::RemoveOnly) : (apply ? pz::ApplyOnly : pz::None);
    }

    void takeNativePzToolbar (IInstanceView& inst, UiStack& stack)
    {
        namespace pz = nativeui::pz;
        float value;
        if (! inst.takeNativePzAction (pzFilter_, value))
            return;
        const int action = static_cast<int> (std::lround (value * pz::Apply));
        constexpr int ids[] {0, kPzBack, kPzReset, kPzRemove, kPzApply};
        if (action < pz::Back || action > pz::Apply || (action == pz::Remove && ! pzEditor_.remove.enabled())
            || (action == pz::Apply && ! pzEditor_.apply.enabled()))
            return;
        dropGesture();
        handlePz (ids[action], stack, inst);
    }

    bool nativePowerVisible() const noexcept { return nativeViewFrame() == 1 || nativeViewFrame() == 2; }
    bool nativeControlsActive() const noexcept { return nativeViewFrame() != 0; }

    void hideNativeControls (IInstanceView& inst) noexcept
    {
        if (peekFilter_ < 0 || nativePeek_)
            inst.setNativeFilterControls (0, nullptr, nullptr, nullptr);
    }

    void observeNativeTouch (int x, int y, double nowMs) noexcept
    {
        using namespace nativeui;
        nativeTouchSlot_ = nativeModControl_ = -1;
        if (! nativeControlsActive())
            return;
        nativeTouchX_ = x;
        nativeTouchY_ = y;
        nativeTouchAt_ = nowMs;
        if (curveShown())
        {
            const auto areas = curveUi().screen.nativeRects();
            for (int s = 0; s < ui::ScalarCurveEditor::kNativeFields; ++s)
                if (areas[s].contains (x, y))
                {
                    nativeTouchSlot_ = FilterPoint + s;
                    if (isSplit())
                    {
                        if (s < 4)
                            splitQBank_ = 3;
                    }
                    else
                        curveUi().screen.setActiveGroup (s < 4 ? 0 : 1);
                    syncSplitQFocus();
                    return;
                }
            if (curveView())
                return;
        }
        const auto areas = full_.nativeRects();
        const auto controls = full_.nativeControls();
        for (int s = 0; s < FilterPoint; ++s)
            if (areas[s].contains (x, y))
            {
                nativeTouchSlot_ = s == FilterModel || s == FilterStyle ? -1 : s;
                if (s != FilterPower && s != FilterClean && s != FilterModel && s != FilterStyle && s != FilterRouting && s != FilterEnv1)
                    nativeModControl_ = controls[s]->id();
                for (int group = 0; group < full_.kMaxGroups; ++group)
                    for (int knob = 0; knob < ui::QGroup::kSlots; ++knob)
                        if (full_.groups[group].member (knob) == controls[s])
                        {
                            full_.setActiveGroup (group);
                            if (isSplit() && group < 3)
                                splitQBank_ = group;
                        }
                syncSplitQFocus();
                break;
            }
    }

    std::array<ui::Rect, nativeui::FilterSlots + 20> nativeApertures (bool includeReadouts = false) const noexcept
    {
        std::array<ui::Rect, nativeui::FilterSlots + 20> out {};
        if (nativeControlsActive())
        {
            int idx = 0;
            if (peekFilter_ < 0)
                out[idx++] = full_.view.rect();
            if (isSplit())
            {
                out[idx++] = full_.scalarView.rect();
                out[idx++] = full_.nativeExpandFilterRect();
                out[idx++] = curveUi().screen.nativeExpandRect();
            }
            else if (peekFilter_ < 0)
            {
                if (! curveView())
                    out[idx++] = full_.both.rect();
                out[idx++] = full_.nativeExpandFilterRect();
            }
            if (filterShown())
            {
                const auto areas = full_.nativeRects (includeReadouts);
                for (const auto& r : areas)
                    if (! r.empty() && idx < static_cast<int> (out.size()))
                        out[idx++] = r;
                if (nativePzOpen() && idx < static_cast<int> (out.size()))
                    out[idx++] = pzOpen_.rect();
                if (includeReadouts)
                {
                    if (idx < static_cast<int> (out.size())) out[idx++] = full_.nativeOutReadoutRect();
                    if (idx < static_cast<int> (out.size())) out[idx++] = full_.nativeSingleStyleRect();
                }
            }
            if (curveShown())
            {
                const auto& editor = curveUi().screen;
                const auto areas = editor.nativeRects();
                for (const auto& r : areas)
                    if (! r.empty() && idx < static_cast<int> (out.size()))
                        out[idx++] = r;
                if (includeReadouts && idx < static_cast<int> (out.size()))
                    out[idx++] = editor.nativeGlideRect();
            }
        }
        else if (nativePowerVisible())
            out[0] = full_.power.rect();
        return out;
    }

    nativeui::FxCell nativeCell (int slot, IInstanceView& inst) const noexcept
    {
        using namespace nativeui;
        if (slot == FilterAction)
            return peekFilter_ < 0 ? FxCell { 0, 0, kFxDial } : FxCell {};
        if (slot >= FilterPoint && slot <= FilterLegato)
        {
            if (! curveShown())
                return {};
            if (slot == FilterLegato)
            {
                const int target = curveUi().legatoParam();
                return { target, target, kFxToggle };
            }
            return { 0, 0, kFxDial };
        }
        if (slot < FilterPoint && filterShown() && full_.hasNativeControl (slot))
        {
            const bool selOn = isSelectedFilterOn (inst);
            const bool allow = slot == FilterPower
                || (slot == FilterRouting ? anyFilterOn (inst) : selOn);
            if (! allow)
                return {};
            const int target = paramForId (full_.nativeControls()[slot]->id(), editFilter_);
            const bool toggle = slot == FilterPower || slot == FilterEnv1 || slot == FilterClean;
            int kind = toggle ? kFxToggle : kFxDial;
            if (slot == FilterModel)
                kind = kFxFirstDomain + kFxModel;
            else if (slot == FilterStyle)
            {
                const int model = paramForId (kModel, editFilter_);
                kind = kFxFirstDomain + kFxStyleBase + static_cast<int> (std::lround (inst.param (model)));
            }
            return { target, target, kind };
        }
        return {};
    }

    void nativeAction (int action, IInstanceView& inst, UiStack& stack)
    {
        using namespace nativeui;
        if (action == FilterF1 || action == FilterF2)
            chooseFilter (inst, action - FilterF1);
        else if (action == FilterNote || action == FilterVelo)
            chooseScalar (inst, action - FilterNote);
        else if (action == FilterBoth && ! isSplit() && ! curveView())
            handle (ui::Change::indexed (kBoth, ! dual_, 0.0), stack, inst);
        else if (action == FilterEditPz && filterShown())
            handlePz (kPzOpen, stack, inst);
        else if (action == FilterExpandFilter && peekFilter_ < 0 && ! isStandalone())
        {
            const int next = expandMode_ == tabview::kFilterSplit ? tabview::kFilterExpandedFilter : tabview::kFilterSplit;
            chooseSplitState (inst, filterSel_, scalarSel_, next);
        }
        else if (curveShown() && action >= FilterInit && action <= FilterTargets)
        {
            using Editor = ui::ScalarCurveEditor;
            const int ids[] = { Editor::kIdInit, Editor::kIdUndo, Editor::kIdDelete, Editor::kIdTargets };
            curveUi().handle (ui::Change::request (ids[action - FilterInit]), inst, nowMs_);
            drainCurveRequests (stack, inst);
        }
        else if (action == FilterExpandCurve && peekFilter_ < 0 && ! isStandalone())
        {
            const int next = expandMode_ == tabview::kFilterExpandedCurve ? tabview::kFilterSplit : tabview::kFilterExpandedCurve;
            chooseSplitState (inst, filterSel_, scalarSel_, next);
        }
    }

    void updateNativeControls (IInstanceView& inst, UiStack& stack)
    {
        using namespace nativeui;
        bool active = nativeControlsActive();
        if (! active)
        {
            hideNativeControls (inst);
            return;
        }

        const int modelParam = filterShown() ? paramForId (kModel, editFilter_) : -1;
        const int model = modelParam >= 0 ? static_cast<int> (std::lround (inst.param (modelParam))) : -1;
        const bool on = filterShown() && isSelectedFilterOn (inst);
        const bool anyOn = filterShown() && anyFilterOn (inst);
        const bool sp = isSplit();
        if (view_ != nativeLastView_ || sp != nativeLastSplit_ || scalarSel_ != nativeLastScalar_
            || model != nativeLastModel_ || on != nativeLastOn_ || anyOn != nativeLastAnyOn_)
        {
            if (nativeTouchSlot_ >= 0)
                blockedNativeSlot_ = nativeTouchSlot_;
            hideNativeControls (inst);
            nativeLastView_ = view_;
            nativeLastSplit_ = sp;
            nativeLastScalar_ = scalarSel_;
            nativeLastModel_ = model;
            nativeLastOn_ = on;
            nativeLastAnyOn_ = anyOn;
        }
        float value;
        if (inst.takeNativeFilterSlot (FilterAction, nativeCell (FilterAction, inst), value))
        {
            const int beforeV = view_, beforeS = scalarSel_, beforeE = expandMode_;
            nativeAction (static_cast<int> (std::lround (value * FilterLastAction)), inst, stack);
            active = nativeControlsActive();
            if (view_ != beforeV || scalarSel_ != beforeS || expandMode_ != beforeE || ! active)
            {
                hideNativeControls (inst);
                return;
            }
        }
        const bool powerBefore = full_.power.state.on;
        for (int slot = 0; slot < FilterAction; ++slot)
        {
            if (slot == blockedNativeSlot_)
            {
                inst.takeNativeFilterSlot (slot, {}, value);
                continue;
            }
            const auto cell = nativeCell (slot, inst);
            if (cell.target < 0 || ! inst.takeNativeFilterSlot (slot, cell, value))
                continue;
            if (slot >= FilterPoint && slot < FilterLegato)
                curveUi().applyVirtual (slot - FilterPoint, value, inst, nowMs_);
            else if (slot == FilterModel)
            {
                const auto& source = info (cell.source);
                const int newModel = static_cast<int> (std::lround (synthyboy::fromNormalised (source, value)));
                if (newModel != static_cast<int> (std::lround (inst.param (cell.target))))
                {
                    writeChoice (inst, cell.target, newModel);
                    pullControls (inst);
                    nativeLastModel_ = newModel;
                }
            }
            else
            {
                const float norm = slot == FilterStyle ? fxEngineValue (cell, value) : value;
                if (std::fabs (uibind::normOf (cell.target, inst.param (cell.target)) - norm) > 1.0e-6f)
                    uibind::writeNorm (inst, cell.target, norm);
            }
        }
        if (filterShown() && powerBefore != isSelectedFilterOn (inst))
        {
            pullControls (inst);
            nativeLastOn_ = full_.power.state.on;
            nativeLastAnyOn_ = full_.anyFilterOn();
        }
        FxCell cells[FilterSlots];
        float values[2 * kFilterCurveFields] {};
        for (int slot = 0; slot < FilterSlots; ++slot)
            cells[slot] = nativeCell (slot, inst);
        if (curveShown())
        {
            for (int i = 0; i < ScalarCurveUi::kVirtualCount; ++i)
                values[i] = curveUi().virtualValue (i);
            const auto& ed = curveUi().screen;
            const float plain[] = { static_cast<float> (ed.point.state.value), static_cast<float> (ed.xField.state.value),
                static_cast<float> (ed.yField.state.value), static_cast<float> (ed.curveField.state.value), static_cast<float> (ed.gridX.state.value),
                static_cast<float> (ed.gridY.state.value), ed.snap.state.on ? 1.0f : 0.0f };
            std::copy (plain, plain + kFilterCurveFields, values + kFilterCurveFields);
        }
        int indicators[kFilterIndicators] {};
        indicators[0] = (filterSel_ == 0 ? 2 : 0) | ((nativeUsedMask_ >> 0) & 1);
        indicators[1] = (filterSel_ == 1 ? 2 : 0) | ((nativeUsedMask_ >> 1) & 1);
        const bool showScalarSel = isSplit() || curveView();
        indicators[2] = (showScalarSel && scalarSel_ == 0 ? 2 : 0) | ((nativeUsedMask_ >> 2) & 1);
        indicators[3] = (showScalarSel && scalarSel_ == 1 ? 2 : 0) | ((nativeUsedMask_ >> 3) & 1);
        if (isSplit())
            indicators[4] = full_.power.state.on ? 2 : (full_.anyFilterOn() ? 1 : 0);
        else
            indicators[4] = (dual_ && ! curveView() ? 1 : 0) | ((filterShown() && full_.power.state.on) ? 2 : 0);
        inst.setNativeFilterControls (nativeViewFrame(), cells, values, indicators);
    }
    static const synthyboy::ParamInfo& info (int idx) noexcept { return uibind::info (idx); }

    bool isSelectedFilterOn (const IInstanceView& inst) const noexcept
    {
        const int p = paramForId (kPower, editFilter_);
        return p >= 0 && inst.param (p) >= 0.5f;
    }
    bool anyFilterOn (const IInstanceView& inst) const noexcept
    {
        const int p1 = paramForId (kPower, 1), p2 = paramForId (kPower, 2);
        return (p1 >= 0 && inst.param (p1) >= 0.5f) || (p2 >= 0 && inst.param (p2) >= 0.5f);
    }

    void syncSplitQFocus() noexcept
    {
        if (! isSplit())
            return;
        if (splitQBank_ == 3)
        {
            full_.setActiveGroup (-1);
            curveUi().screen.setActiveGroup (0);
        }
        else
        {
            if (full_.activeGroup() != splitQBank_)
                full_.setActiveGroup (splitQBank_);
            curveUi().screen.setActiveGroup (-1);
        }
    }

    void syncLayoutMode()
    {
        const bool sp = isSplit();
        full_.setSplitMode (sp);
        ui::Rect compactRect {};
        if (sp)
        {
            const ui::Rect fr = full_.rect();
            const int lx = fr.x + 8, lw = (fr.w - 28) * 60 / 100;
            const int rx = lx + lw + 12, rw = fr.right() - 8 - rx;
            compactRect = { rx, fr.y + ui::kQLane, rw, std::max (0, fr.h - ui::kQLane - 8) };
        }
        for (ScalarCurveUi& c : curve_)
        {
            c.screen.setCompact (sp);
            c.screen.setOwnQFrames (! sp);
            c.screen.place (sp ? compactRect : full_.bodyRect());
        }
        full_.setScalarView (scalarSel_);
        const int fv = curveView() ? (scalarSel_ == 0 ? ui::FilterFullScreen::kViewNote : ui::FilterFullScreen::kViewVelo) : filterSel_;
        view_ = fv;
        full_.setView (fv);
        full_.setViewCells ((nativeUsedMask_ & 1) != 0, (nativeUsedMask_ & 2) != 0, (nativeUsedMask_ & 4) != 0, (nativeUsedMask_ & 8) != 0);
        full_.setScalarCells ((nativeUsedMask_ & 4) != 0, (nativeUsedMask_ & 8) != 0);
        if (sp)
            syncSplitQFocus();
        else
            curve_[0].screen.setOwnQFrames (true), curve_[1].screen.setOwnQFrames (true);
    }

    void adoptSplitState (int f, int s, int e)
    {
        f = std::clamp (f, 0, 1);
        s = std::clamp (s, 0, 1);
        e = std::clamp (e, 0, 2);
        const bool changed = (f != filterSel_ || s != scalarSel_ || e != expandMode_ || ! started_);
        if (changed)
        {
            started_ = true;
            if (gesture_ != nullptr && (e != expandMode_ || s != scalarSel_))
                dropGesture();
            qSlots_.forget();
            filterSel_ = f;
            scalarSel_ = s;
            expandMode_ = e;
            editFilter_ = filterSel_ + 1;
            modsDirty_ = true;
            syncLayoutMode();
        }
        else
        {
            editFilter_ = filterSel_ + 1;
        }
    }

    /** Compatibility entry for single-index callers (`0 F1, 1 F2, 2 NOTE, 3 VELO`). */
    void adoptView (int v)
    {
        int f = filterSel_, s = scalarSel_, e = expandMode_;
        tabview::decodeFilterSplit (std::clamp (v, 0, 3), f, s, e);
        adoptSplitState (f, s, e);
    }

    void assignIds()
    {
        pzOpen_.setId (kPzOpen);
        pzEditor_.back.setId (kPzBack);
        pzEditor_.reset.setId (kPzReset);
        pzEditor_.remove.setId (kPzRemove);
        pzEditor_.apply.setId (kPzApply);
        pzEditor_.canvas.setId (kPzCanvas);
        full_.close.setId (kClose);
        full_.expand.setId (kExpand);
        full_.power.setId (kPower);
        full_.view.setId (kView);
        full_.scalarView.setId (kScalarView);
        full_.both.setId (kBoth);
        full_.model.setId (kModel);
        full_.style.setId (kStyle);
        full_.clean.setId (kClean);
        full_.display.assignIds (kDisplayCutoff, kDisplayReso, kDisplayFormant, kDisplayFormantY);

        full_.cutoff.setId (kCutoff);
        full_.resonance.setId (kResonance);
        full_.drive.setId (kDrive);
        full_.mix.setId (kMix);

        full_.keytrack.setId (kKeytrack);
        full_.env1.setId (kEnv1);
        full_.var.setId (kVar);
        full_.combOffset.setId (kCombOffset);

        full_.blend.setId (kBlend);
        full_.routing.setId (kRouting);

        full_.formantX.setId (kFormantX);
        full_.formantY.setId (kFormantY);
        full_.formantTrans.setId (kFormantTrans);
        full_.formantRes.setId (kFormantRes);
        full_.formantSpread.setId (kFormantSpread);

        mods_.back.setId (kModsBack);
        mods_.add.setId (kModsAdd);
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
    }

    int paramForId (int id, int filterNum) const noexcept
    {
        if (id == kRouting)
            return uibind::indexOfKey ("filter.routing");
        const char* s = nullptr;
        switch (id)
        {
            case kPower: s = "on"; break;
            case kModel: s = "model"; break;
            case kStyle: s = "style"; break;
            case kClean: s = "clean_mode"; break;
            case kCutoff: case kDisplayCutoff: s = "cutoff"; break;
            case kResonance: case kDisplayReso: s = "resonance"; break;
            case kDrive: s = "drive"; break;
            case kMix: s = "mix"; break;
            case kBlend: s = "blend"; break;
            case kKeytrack: s = "keytrack"; break;
            case kEnv1: s = "env1"; break;
            case kVar: s = "var"; break;
            case kCombOffset: s = "comb_offset"; break;
            case kFormantX: case kDisplayFormant: s = "formant.x"; break;
            case kFormantY: case kDisplayFormantY: s = "formant.y"; break;
            case kFormantTrans: s = "formant.transpose"; break;
            case kFormantRes: s = "formant.resonance"; break;
            case kFormantSpread: s = "formant.spread"; break;
            default: return -1;
        }
        return uibind::indexOfKey ("filter" + std::to_string (filterNum) + "." + s);
    }

    /** The component a touch at height y starts on: a page above the tab, else in the NOTE / VELO states the top bar (the selector) or the editor under it, else the filter page. */
    ui::Component* topComponent (int x, int y) noexcept
    {
        if (pageTop_ == kPageMods) return &mods_;
        if (pageTop_ == kPageTargets) return &targets_;
        if (pageTop_ == kPageDest) return &destList_;
        if (pageTop_ == kPagePz) return &pzEditor_;
        if (isSplit() && full_.scalarView.rect().contains (x, y))
            return &full_;
        if (curveShown() && curveUi().screen.rect().contains (x, y))
            return &curveUi().screen;
        if (curveView() && y >= full_.bodyRect().y)
            return &curveUi().screen;
        return &full_;
    }
    bool gestureOnCurve() noexcept { return curveShown() && pageTop_ == 0 && gesture_ == &curveUi().screen; }

    void syncPages (const UiStack& stack)
    {
        const int rootDepth = peekFilter_ >= 0 ? 2 : 1;
        const int top = stack.depth() > rootDepth ? stack.top() : 0;
        if (top != pageTop_ && gesture_ != nullptr)
            dropGesture();
        pageTop_ = top;
    }

    /** A change of the part that holds the finger: of the editor (NOTE / VELO) or of the page. */
    void dispatch (ui::Change c, UiStack& stack, IInstanceView& inst)
    {
        if (! c)
            return;
        if (gestureOnCurve())
        {
            curveUi().handle (c, inst, nowMs_);
            drainCurveRequests (stack, inst);
            return;
        }
        handle (c, stack, inst);
    }
    void drainCurveRequests (UiStack& stack, IInstanceView& inst)
    {
        for (ScalarCurveUi& cu : curve_)
        {
            if (cu.expandRequested)
            {
                cu.expandRequested = false;
                if (peekFilter_ < 0 && ! isStandalone() && pageTop_ == 0)
                {
                    const int next = expandMode_ == tabview::kFilterExpandedCurve ? tabview::kFilterSplit : tabview::kFilterExpandedCurve;
                    chooseSplitState (inst, filterSel_, scalarSel_, next);
                }
            }
        }
        openTargetsIfAsked (stack, inst);
    }
    /** TARGETS of the editor was tapped: the ModTargetsPage of the source (23 velocity, 24 note) opens. */
    void openTargetsIfAsked (UiStack& stack, IInstanceView& inst)
    {
        for (ScalarCurveUi& cu : curve_)
            if (cu.targetsRequested)
            {
                cu.targetsRequested = false;
                if (curveShown() && pageTop_ == 0)
                {
                    targetsPageIndex_ = 0;
                    stack.openDetail (kPageTargets);
                    syncPages (stack);
                    loadTargets (inst, true);
                }
            }
    }

    /** The second change of a touch on the display (the finger's Y: resonance or formant Y). */
    void handlePending (UiStack& stack, IInstanceView& inst)
    {
        ui::Change extra;
        while (gesture_ == &full_ && full_.display.takePending (extra))
            handle (extra, stack, inst);
    }

    void chooseSplitState (IInstanceView& inst, int f, int s, int e)
    {
        f = std::clamp (f, 0, 1);
        s = std::clamp (s, 0, 1);
        e = std::clamp (e, 0, 2);
        if (peekFilter_ < 0 && ! isStandalone())
            inst.setTabView (IInstanceView::kTabViewFilter, tabview::encodeFilterSplit (f, s, e));
        adoptSplitState (f, s, e);
        qSlots_.forget();
    }
    void chooseFilter (IInstanceView& inst, int f)
    {
        f = std::clamp (f, 0, 1);
        const int e = expandMode_ == tabview::kFilterExpandedCurve ? tabview::kFilterExpandedFilter : expandMode_;
        chooseSplitState (inst, f, scalarSel_, e);
    }
    void chooseScalar (IInstanceView& inst, int s)
    {
        s = std::clamp (s, 0, 1);
        chooseSplitState (inst, filterSel_, s, expandMode_ == tabview::kFilterExpandedFilter ? tabview::kFilterExpandedCurve : expandMode_);
    }
    /** Compatibility entry for single-index view transitions (`0 F1, 1 F2, 2 NOTE, 3 VELO`). */
    void chooseView (IInstanceView& inst, int v)
    {
        v = std::clamp (v, 0, 3);
        if (v < ui::FilterFullScreen::kViewNote)
            chooseFilter (inst, v);
        else
            chooseSplitState (inst, filterSel_, v - ui::FilterFullScreen::kViewNote, tabview::kFilterExpandedCurve);
    }

    bool isPzModel (const IInstanceView& inst) const noexcept
    {
        return peekFilter_ < 0 && ! curveView() && inst.param (paramForId (kModel, editFilter_)) == kModelPz;
    }

    bool pzPowered (const IInstanceView& inst) const noexcept
    {
        return inst.param (paramForId (kPower, editFilter_)) >= 0.5f;
    }

    bool pzTargetAvailable (const IInstanceView& inst) const noexcept
    {
        return pzOwner_ == &inst && pzFilter_ == editFilter_ && isPzModel (inst) && pzPowered (inst);
    }

    void reloadPz (IInstanceView& inst)
    {
        pzGeneration_ = inst.stateGeneration();
        const std::vector<ui::PzCurveCanvas::Point>* points = nullptr;
        const auto content = inst.content();
        if (content != nullptr)
            for (const auto& curve : content->filterCurves)
                if (curve.filter == (pzFilter_ == 2 ? "filter2" : "filter1"))
                {
                    points = &curve.points;
                    break;
                }
        pzEditor_.showFilter (pzFilter_, points);
    }

    void closePz (UiStack& stack)
    {
        pzOwner_ = nullptr;
        stack.back();
        syncPages (stack);
    }

    void handlePz (int id, UiStack& stack, IInstanceView& inst)
    {
        if (id == kPzOpen)
        {
            if (pageTop_ != 0 || ! isPzModel (inst) || ! pzPowered (inst) || ! stack.openDetail (kPagePz))
                return;
            pzOwner_ = &inst;
            pzFilter_ = editFilter_;
            reloadPz (inst);
            syncPages (stack);
            hideNativeControls (inst);
            return;
        }
        if (pageTop_ != kPagePz)
            return;
        if (id == kPzBack || ! pzTargetAvailable (inst))
        {
            closePz (stack);
            return;
        }
        if (pzGeneration_ != inst.stateGeneration())
        {
            // A release may be delivered before the first update of the new preset.
            // Discard the previous draft and the action that targeted it.
            dropGesture();
            reloadPz (inst);
            return;
        }
        if (id == kPzApply)
        {
            if (pzEditor_.apply.enabled())
                inst.setFilterCurve (pzFilter_, pzEditor_.canvas.points(), pzGeneration_);
            closePz (stack);
            return;
        }
        if (id == kPzReset)
            pzEditor_.canvas.reset();
        if (id == kPzRemove)
            pzEditor_.canvas.removeSelected();
        pzEditor_.refresh();
    }

    void handle (ui::Change c, UiStack& stack, IInstanceView& inst)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        if (c.kind == K::Request && c.id >= kPzOpen && c.id <= kPzCanvas)
        {
            handlePz (c.id, stack, inst);
            return;
        }

        // a long press on a control asks for its modulations
        if (c.kind == K::ModsRequest)
        {
            if (! isSelectedFilterOn (inst))
                return;
            const int p = paramForId (c.id, editFilter_);
            if (p >= 0)
            {
                activeModParam_ = p;
                modsTitle_ = c.id;
                modsPreset_ = inst.stateGeneration();
                modsPageIndex_ = 0;
                modsFilterModel_ = static_cast<int> (std::lround (inst.param (paramForId (kModel, editFilter_))));
                mods_.setPickingSource (false);
                modsDirty_ = true;
                loadMods (inst);
                stack.openDetail (kPageMods);
                syncPages (stack);
            }
            return;
        }
        if (c.kind == K::AddRequest)
        {
            addModulation (inst, c.index);
            return;
        }
        // the rows of the modulations page
        if (c.id >= kModsRow && c.id < kModsRow + 4 * ui::ModulationsPage::kPerPage)
        {
            const int row = (c.id - kModsRow) / 4, part = (c.id - kModsRow) % 4;
            if (modsContextValid (inst) && uibind::modulationMatches (inst, activeModParam_, modRows_[row]))
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (modRows_[row].slot);
                if (c.kind == K::Value && part == 0)
                    uibind::writeNorm (inst, sp.amount, c.value);
                else if (c.kind == K::Index && part == 1)
                    uibind::writePlain (inst, sp.bipolar, c.index != 0 ? 1.0 : 0.0);
                else if (c.kind == K::Index && part == 2)
                    uibind::writePlain (inst, sp.bypass, c.index != 0 ? 0.0 : 1.0);   // the switch says ON (active): bypassed is its opposite
                else if (c.kind == K::Request && part == 3)
                {
                    uibind::writePlain (inst, sp.source, 0); uibind::writePlain (inst, sp.destination, 0); uibind::writePlain (inst, sp.amount, 0);
                    loadMods (inst);
                    return;
                }
                modsDirty_ = true;
            }
            return;
        }
        // the rows of the targets page of NOTE / VELO
        if (c.id >= kTargetsRow && c.id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage)
        {
            const int row = (c.id - kTargetsRow) / 4, part = (c.id - kTargetsRow) % 4;
            const int slot = targetRows_[row].slot;
            if (slot < 0)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
            if (c.kind == K::Value && part == 0)
                uibind::writeNorm (inst, sp.amount, c.value);
            else if (c.kind == K::Index && part == 1)
                uibind::writePlain (inst, sp.bipolar, c.index != 0 ? 1.0 : 0.0);
            else if (c.kind == K::Index && part == 2)
                uibind::writePlain (inst, sp.bypass, c.index != 0 ? 0.0 : 1.0);
            else if (c.kind == K::Request && part == 3)
            {
                uibind::writePlain (inst, sp.source, 0); uibind::writePlain (inst, sp.destination, 0); uibind::writePlain (inst, sp.amount, 0);
                modsDirty_ = true;
                loadTargets (inst, true);
                return;
            }
            modsDirty_ = true;
            return;
        }
        if (c.kind == K::Depth)
        {
            if (! isSelectedFilterOn (inst))
                return;
            const int p = activeParamOfControl (c.id);
            const int slot = p >= 0 ? uibind::slotOf (inst, p, c.index) : -1;
            if (slot >= 0)
                uibind::writeNorm (inst, synthyboy::modSlotParams (slot).amount, (c.value + 1.0) * 0.5);
            modsDirty_ = true;
            return;
        }
        if (c.id == kModsPage && c.kind == K::Index && modsContextValid (inst))
        {
            modsPageIndex_ = c.index;
            loadMods (inst);
            return;
        }
        if (c.id == kModsAddStep && c.kind == K::Index)
        {
            const int i = std::clamp (c.index, 0, std::max (0, modsAddOffer_ - 1));
            mods_.addSource.state.index = i;
            mods_.addSource.setItem (uibind::sourceName (mods_.sourceIds[i]));
            return;
        }
        if (c.id == kTargetsPage && c.kind == K::Index)
        {
            targetsPageIndex_ = c.index;
            loadTargets (inst, true);
            return;
        }

        // navigation requests
        if (c.kind == K::Request)
        {
            switch (c.id)
            {
                case kExpand:
                    if (peekFilter_ < 0 && ! isStandalone())
                    {
                        const int next = isSplit() ? tabview::kFilterExpandedFilter : tabview::kFilterSplit;
                        chooseSplitState (inst, filterSel_, scalarSel_, next);
                    }
                    break;
                case kModsBack:
                    if (mods_.pickingSource())
                        mods_.setPickingSource (false);
                    else
                        stack.back();
                    break;
                case kModsAdd:
                    if (modsContextValid (inst))
                    {
                        uibind::fillSourceBoard (mods_.board);
                        mods_.setPickingSource (true);
                    }
                    break;
                case kTargetsBack: case kDestBackdrop: stack.back(); break;
                case kTargetsAdd: openDestList (stack); break;
                case kDestHeader: destGroup_ = -1; fillDestList(); break;
                default: break;
            }
            syncPages (stack);
            return;
        }

        // the list that picks a new target of NOTE / VELO: the groups of the destinations, then the parameters of a group
        if (c.id == kDestList && c.kind == K::Index)
        {
            if (destGroup_ < 0)
            {
                destGroup_ = c.index;
                fillDestList();
            }
            else
            {
                const std::vector<int>& ch = uibind::DestIndex::get().choices[static_cast<size_t> (destGroup_)];
                if (c.index >= 0 && c.index < static_cast<int> (ch.size()))
                    addTarget (inst, ch[static_cast<size_t> (c.index)]);
                stack.back();
                syncPages (stack);
            }
            return;
        }

        // F1 | F2 | NOTE | VELO: left filter or full-screen selector; scalarView: right NOTE | VELO selector in Split mode
        if (c.id == kView && c.kind == K::Index)
        {
            if (isSplit())
                chooseFilter (inst, c.index - 1);
            else
                chooseView (inst, c.index - 1);
            return;
        }
        if (c.id == kScalarView && c.kind == K::Index)
        {
            chooseScalar (inst, c.index - 1);
            return;
        }
        // F1+F2: a view only (the curves of both filters and the resultant): nothing is written
        if (c.id == kBoth && c.kind == K::Index)
        {
            dual_ = c.index != 0;
            return;
        }

        // a choice of the list popup


        // the finger on the display: its X and Y come as two changes (ui::FilterDisplay: the cutoff on the log axis and the resonance, or the formant X and Y). In the dual view `index` is the filter whose handle the finger holds.
        if (c.id >= kDisplayCutoff && c.id <= kDisplayFormantY && c.kind == K::Value)
        {
            const bool cutoffOrReso = c.id == kDisplayCutoff || c.id == kDisplayReso;
            const int targetFilter = cutoffOrReso && isDual() && (c.index == 1 || c.index == 2) ? c.index : editFilter_;
            const int targetOn = paramForId (kPower, targetFilter);
            if (targetOn < 0 || inst.param (targetOn) < 0.5f)
                return;
            if (targetFilter != editFilter_)
            {
                editFilter_ = targetFilter;   // the handle the finger holds is the filter being edited: the selector follows (F1 or F2)
                chooseFilter (inst, targetFilter - 1);
            }
            const int pd = paramForId (c.id, targetFilter);
            if (pd >= 0)
            {
                if (c.id == kDisplayCutoff)
                    uibind::writeNorm (inst, pd, static_cast<float> (uibind::normOf (pd, static_cast<float> (ui::FilterDisplay::hzOfNorm (c.value)))));
                else
                    uibind::writeNorm (inst, pd, static_cast<float> (c.value));
            }
            return;
        }

        if (c.id == kRouting)
        {
            if (! anyFilterOn (inst))
                return;
        }
        else if (c.id != kPower && ! isSelectedFilterOn (inst))
        {
            return;
        }

        const int p = paramForId (c.id, editFilter_);
        if (p < 0)
            return;
        if (c.kind == K::Value)
            uibind::writeNorm (inst, p, c.value);
        else if (c.kind == K::Index)
            writeChoice (inst, p, c.index);
    }

    /** Both the stepper arrows and the list change the model and limit its style in the same edit. */
    void writeChoice (IInstanceView& inst, int param, int choice)
    {
        uibind::writePlain (inst, param, choice);
        if (param != paramForId (kModel, editFilter_))
            return;
        const int count = synthyboy::filters::filterModelStyleCount (choice);
        const int style = paramForId (kStyle, editFilter_);
        if (style >= 0 && count > 0 && std::lround (inst.param (style)) >= count)
            uibind::writePlain (inst, style, count - 1);
    }

    /** The parameter of a control of the page (a knob, a stepper, a switch). */
    int activeParamOfControl (int id) const noexcept { return paramForId (id, editFilter_); }

    /** The box of the list popup: the standard one (600 px wide, nine rows) or, for the style list, the size of its content: as wide as the longest name needs (and at least 280 px), as tall as its rows, centred. */




    // ---- the targets page of NOTE / VELO: the slots of the matrix whose source is the velocity (23) or the note (24)
    int curveSource() const noexcept { return scalarSel_ == 0 ? 24 : 23; }
    static void targetName (size_t destChoice, char* out, size_t cap)
    {
        const int p = synthyboy::modDestinationParamIndex (destChoice);
        std::snprintf (out, cap, "%s", p >= 0 ? uibind::info (p).name : "-");
    }
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        const int src = curveSource();
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, src, slots, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + ui::ModTargetsPage::kPerPage - 1) / ui::ModTargetsPage::kPerPage);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * ui::ModTargetsPage::kPerPage, n = std::clamp (total - first, 0, ui::ModTargetsPage::kPerPage);
        ui::ModTargetsPage::Entry e[ui::ModTargetsPage::kPerPage];
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[first + i]);
            targetName (static_cast<size_t> (std::lround (v.param (sp.destination))), e[i].name, sizeof e[i].name);
            e[i].depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            e[i].bidirectional = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            e[i].bypassed = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            targetRows_[i] = { slots[first + i], src };
        }
        for (int i = n; i < ui::ModTargetsPage::kPerPage; ++i)
            targetRows_[i] = {};
        // only a change of the rows is loaded again (a reload resets the touch of a row that is being dragged)
        bool same = ! fresh && n == targets_.numRows() && total == targetTotal_;
        for (int i = 0; same && i < n; ++i)
        {
            const ui::ModTargetsPage::Row& r = targets_.rows[i];
            same = std::strcmp (r.name, e[i].name) == 0 && std::fabs (r.depth.state.value - (e[i].depth + 1.0) * 0.5) < 1.0e-9 && r.polarity.state.on == e[i].bidirectional && r.bypass.state.on == ! e[i].bypassed;
        }
        if (same)
            return;
        targetTotal_ = total;
        targets_.load (e, n, src == 24 ? "NOTE" : "VELO", ui::theme::sourceColour (src == 24 ? "NOTE" : "VEL"), total, targetsPageIndex_);
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
    }
    void openDestList (UiStack& stack)
    {
        destGroup_ = -1;
        fillDestList();
        stack.openDetail (kPageDest);
    }
    /** The list that picks a new target: the groups of the destinations, then the parameters of a group. */
    void fillDestList()
    {
        const uibind::DestIndex& d = uibind::DestIndex::get();
        destNames_.clear();
        if (destGroup_ < 0)
        {
            for (const std::string& g : d.groups)
                destNames_.push_back (g.c_str());
            destList_.state.header = nullptr;
            destList_.state.headerBack = false;
        }
        else
        {
            for (int c : d.choices[static_cast<size_t> (destGroup_)])
                destNames_.push_back (uibind::info (synthyboy::modDestinationParamIndex (static_cast<size_t> (c))).name);
            destList_.state.header = d.groups[static_cast<size_t> (destGroup_)].c_str();
            destList_.state.headerBack = true;
        }
        destList_.state.items = destNames_.data();
        destList_.state.count = static_cast<int> (destNames_.size());
        destList_.state.current = -1;
        destList_.showCurrent();
    }
    void addTarget (IInstanceView& v, int destChoice)
    {
        const int source = curveSource();
        const int slot = uibind::addTargetSlot (v, source, destChoice);
        if (slot < 0)
            return;
        constexpr int rows = ui::ModTargetsPage::kPerPage;
        const int position = uibind::targetPosition (v, source, slot);
        targetsPageIndex_ = position / rows;
        loadTargets (v, true);
        targets_.selectRow (position % rows);
        modsDirty_ = true;
    }

    // ---- the modulations page of one control
    bool modsDestinationValid (const IInstanceView& inst) const noexcept
    {
        const int on = paramForId (kPower, editFilter_);
        const int model = paramForId (kModel, editFilter_);
        return ! curveView() && activeModParam_ >= 0 && paramForId (modsTitle_, editFilter_) == activeModParam_
            && on >= 0 && inst.param (on) >= 0.5f
            && model >= 0 && static_cast<int> (std::lround (inst.param (model))) == modsFilterModel_;
    }
    bool modsContextValid (const IInstanceView& inst) const noexcept
    {
        return pageTop_ == kPageMods && modsPreset_ == inst.stateGeneration() && modsDestinationValid (inst);
    }

    void loadMods (const IInstanceView& v, int focusSlot = -1)
    {
        char title[13] = "MODULATIONS";
        const ui::Knob* knob = knobOf (modsTitle_);
        if (knob != nullptr)
            std::snprintf (title, sizeof title, "%.12s", knob->state.label);
        const auto window = uibind::modulationWindow (v, activeModParam_, selectedSource_, modsPageIndex_, modRows_, focusSlot);
        modsPageIndex_ = window.page;
        if (window.total == 0)
            uibind::fillSourceBoard (mods_.board);
        mods_.load (window.entries, title, window.total, window.page);
        if (window.focusRow >= 0)
            mods_.selectRow (window.focusRow);
        mods_.page.setId (kModsPage);
        mods_.back.setId (kModsBack);
        mods_.addSource.setId (kModsAddStep);
        mods_.add.setId (kModsAdd);
        for (int r = 0; r < ui::ModulationsPage::kPerPage; ++r)
            mods_.setRowIds (r, kModsRow + 4 * r, kModsRow + 4 * r + 1, kModsRow + 4 * r + 2, kModsRow + 4 * r + 3);
        // what "add source" offers: the selected source first, then the first envelopes and LFOs
        int offer[8], n = 0;
        auto push = [&] (int s) { if (s == ui::kNoSource || n >= 8) return; for (int i = 0; i < n; ++i) if (offer[i] == s) return; offer[n++] = s; };
        push (selectedSource_);
        for (int s : { 7, 8, 1, 2, 9, 3, 19, 20 }) push (s);
        for (int i = 0; i < n; ++i)
            mods_.sourceIds[i] = offer[i];
        mods_.addSource.state.count = n;
        mods_.addSource.state.index = std::clamp (mods_.addSource.state.index, 0, std::max (0, n - 1));
        mods_.addSource.setItem (uibind::sourceName (offer[std::clamp (mods_.addSource.state.index, 0, std::max (0, n - 1))]));
        modsAddOffer_ = n;
        char tv[16] = "";
        if (activeModParam_ >= 0)
            uibind::formatValue (info (activeModParam_), v.param (activeModParam_), tv, sizeof tv);
        mods_.setTargetValue (tv);
    }
    void addModulation (IInstanceView& v, int source)
    {
        if (! modsContextValid (v))
            return;
        if (const int slot = uibind::ensureModulationSource (v, activeModParam_, source); slot >= 0)
        {
            mods_.setPickingSource (false);
            loadMods (v, slot);
        }
    }
    void refreshMods (IInstanceView& inst)
    {
        // the arcs and the tabs of the knobs: the modulations of every knob of the page
        for (ui::Knob* k : knobsOfPage())
        {
            const int p = paramForId (k->id(), editFilter_);
            if (p >= 0)
                static_cast<ui::ModInput&> (k->state) = uibind::modsOf (inst, p, selectedSource_, nullptr);
        }
        // the dots of the selector: F1 / F2 are on, NOTE / VELO are used by a modulation slot
        ui::ScalarCurveEditor::UsedBy used;
        ScalarCurveUi::scanUsedBy (inst, 24, used);
        const bool noteUsed = used.total > 0;
        ScalarCurveUi::scanUsedBy (inst, 23, used);
        const int on1 = paramForId (kPower, 1), on2 = paramForId (kPower, 2);
        const bool f1 = on1 >= 0 && inst.param (on1) >= 0.5f;
        const bool f2 = on2 >= 0 && inst.param (on2) >= 0.5f;
        const bool velo = used.total > 0;
        nativeUsedMask_ = static_cast<int> (f1) | (f2 << 1) | (noteUsed << 2) | (velo << 3);
        full_.setViewCells (f1, f2, noteUsed, velo);
        full_.setScalarCells (noteUsed, velo);
        if (pageTop_ == kPageTargets || pageTop_ == kPageDest)
            loadTargets (inst, false);
        if (pageTop_ == kPageMods && activeModParam_ >= 0)
            loadMods (inst);
    }
    std::vector<ui::Knob*> knobsOfPage()
    {
        return { &full_.cutoff, &full_.resonance, &full_.drive, &full_.mix, &full_.keytrack, &full_.var, &full_.combOffset, &full_.blend,
                 &full_.formantX, &full_.formantY, &full_.formantTrans, &full_.formantRes, &full_.formantSpread };
    }
    const ui::Knob* knobOf (int id)
    {
        for (const ui::Knob* k : knobsOfPage())
            if (k->id() == id)
                return k;
        return nullptr;
    }

    void computeCombinedCurve (IInstanceView& inst)
    {
        curveResult_.assign (CurveSource::kPoints, 0.0f);
        const float* c1 = curve1_->curve();
        const float* c2 = curve2_->curve();
        if (! c1 || ! c2)
            return;
        const int on1Idx = paramForId (kPower, 1);
        const int on2Idx = paramForId (kPower, 2);
        const bool on1 = on1Idx >= 0 && inst.param (on1Idx) >= 0.5f;
        const bool on2 = on2Idx >= 0 && inst.param (on2Idx) >= 0.5f;

        if (! on1 && ! on2)
            return;
        if (on1 && ! on2)
        {
            std::copy (c1, c1 + CurveSource::kPoints, curveResult_.data());
            return;
        }
        if (! on1 && on2)
        {
            std::copy (c2, c2 + CurveSource::kPoints, curveResult_.data());
            return;
        }

        const int rIdx = uibind::indexOfKey ("filter.routing");
        const int routing = rIdx >= 0 ? static_cast<int> (std::lround (inst.param (rIdx))) : 0;
        for (int i = 0; i < CurveSource::kPoints; ++i)
        {
            if (routing == 0)   // parallel: the linear magnitudes add
            {
                const double lin = std::pow (10.0, static_cast<double> (c1[i]) * 0.05) + std::pow (10.0, static_cast<double> (c2[i]) * 0.05);
                curveResult_[static_cast<size_t> (i)] = static_cast<float> (std::clamp (20.0 * std::log10 (std::max (1.0e-6, lin)), -120.0, 60.0));
            }
            else   // in series: the dB add
                curveResult_[static_cast<size_t> (i)] = static_cast<float> (std::clamp (static_cast<double> (c1[i]) + static_cast<double> (c2[i]), -120.0, 60.0));
        }
    }

    void updateDisplayData (IInstanceView& inst, const synthyboy::LiveValues* live)
    {
        ui::FilterDisplay& d = full_.display;
        d.state.dual = isDual();
        d.state.activeFilter = editFilter_;
        const int mIdx = paramForId (kModel, editFilter_);
        const int model = mIdx >= 0 ? static_cast<int> (std::lround (inst.param (mIdx))) : 0;
        d.state.isFormant = model == kModelFormant;
        d.state.curve1 = curve1_->curve();
        d.state.numPoints1 = curve1_->points();
        d.state.curve2 = curve2_->curve();
        d.state.numPoints2 = curve2_->points();
        if (isDual())
        {
            d.state.curveResult = curveResult_.data();
            d.state.numPointsResult = static_cast<int> (curveResult_.size());
        }
        else
        {
            d.state.curveResult = nullptr;
            d.state.numPointsResult = 0;
        }
        const int cut1Idx = paramForId (kCutoff, 1), res1Idx = paramForId (kResonance, 1);
        const int cut2Idx = paramForId (kCutoff, 2), res2Idx = paramForId (kResonance, 2);
        const int on1Idx = paramForId (kPower, 1), on2Idx = paramForId (kPower, 2);
        d.state.filter1On = on1Idx >= 0 && inst.param (on1Idx) >= 0.5f;
        d.state.filter2On = on2Idx >= 0 && inst.param (on2Idx) >= 0.5f;

        // every value comes from the parameters on every update, also under a finger: the display stores nothing, the point follows the parameter the finger wrote
        d.state.cutoffHz1 = cut1Idx >= 0 ? inst.param (cut1Idx) : 12000.0;
        d.state.resonancePct1 = res1Idx >= 0 ? inst.param (res1Idx) : 20.0;
        d.state.cutoffHz2 = cut2Idx >= 0 ? inst.param (cut2Idx) : 12000.0;
        d.state.resonancePct2 = res2Idx >= 0 ? inst.param (res2Idx) : 20.0;
        d.state.cutoffHz = editFilter_ == 2 ? d.state.cutoffHz2 : d.state.cutoffHz1;
        d.state.resonancePct = editFilter_ == 2 ? d.state.resonancePct2 : d.state.resonancePct1;
        if (d.state.isFormant)
        {
            const int fxIdx = paramForId (kFormantX, editFilter_), fyIdx = paramForId (kFormantY, editFilter_);
            d.state.formantX = fxIdx >= 0 ? inst.param (fxIdx) : 50.0;
            d.state.formantY = fyIdx >= 0 ? inst.param (fyIdx) : 50.0;
        }
        const CurveSource* active = editFilter_ == 2 ? curve2_.get() : curve1_.get();
        d.state.liveCutoffHz = active != nullptr ? active->stats().cutoffHz : -1.0;
        d.state.liveResoPct = active != nullptr ? active->stats().resonancePercent : -1.0;
        // the voices: where the key tracking puts the cutoff of each sounding voice
        d.state.numVoices = 0;
        if (live != nullptr && live->activeVoices > 0)
        {
            d.state.numVoices = std::min (live->activeVoices, 8);
            const int ktIdx = paramForId (kKeytrack, editFilter_);
            const int cutIdx = editFilter_ == 2 ? cut2Idx : cut1Idx;
            const float kt = ktIdx >= 0 ? inst.param (ktIdx) * 0.01f : 0.0f;
            const float baseCutoff = cutIdx >= 0 ? inst.param (cutIdx) : 12000.0f;
            for (int v = 0; v < d.state.numVoices; ++v)
            {
                const float noteOffset = static_cast<float> (v * 4 - 12);
                d.state.voiceCutoffs[v] = std::clamp (baseCutoff * std::pow (2.0f, (noteOffset * kt) / 12.0f), 20.0f, 20000.0f);
            }
        }
    }

    /** A knob from its parameter: the value, the text (the parameter's own format), the default and the bipolar mark. */
    void pullKnob (IInstanceView& v, ui::Knob& k, int id)
    {
        const int p = paramForId (id, editFilter_);
        if (p < 0)
        {
            k.setEnabled (false);
            return;
        }
        const synthyboy::ParamInfo& pi = info (p);
        const float plain = v.param (p);
        k.state.value = k.state.modValue = uibind::normOf (p, plain);
        k.state.defaultValue = uibind::normOf (p, pi.def);
        k.state.bipolar = pi.min < 0.0f && pi.max > 0.0f;
        if (pi.numChoices > 0)   // a knob over a choice (ROUTING): the name of the choice
        {
            const int n = std::clamp (static_cast<int> (std::lround (plain)), 0, pi.numChoices - 1);
            ui::setText (k.state.valueText, sizeof k.state.valueText, pi.choices[n]);
            k.state.bipolar = false;
        }
        else
            uibind::formatValue (pi, plain, k.state.valueText, sizeof k.state.valueText);
    }
    void pullToggle (IInstanceView& v, ui::ToggleIcon& t, int id)
    {
        const int p = paramForId (id, editFilter_);
        t.setEnabled (p >= 0);
        if (p >= 0)
            t.state.on = v.param (p) >= 0.5f;
    }
    void pullList (IInstanceView& v, ui::LedList& l, int id)
    {
        const int p = paramForId (id, editFilter_);
        l.setEnabled (p >= 0);
        if (p < 0)
            return;
        const synthyboy::ParamInfo& pi = info (p);
        l.setOptions (pi.choices, pi.numChoices);
        l.state.index = std::clamp (static_cast<int> (std::lround (v.param (p))), 0, std::max (0, l.state.count - 1));
    }
    void pullStepper (IInstanceView& v, ui::Stepper& s, int id)
    {
        const int p = paramForId (id, editFilter_);
        if (p < 0)
        {
            s.setEnabled (false);
            return;
        }
        const synthyboy::ParamInfo& pi = info (p);
        const int n = std::clamp (static_cast<int> (std::lround (v.param (p))), 0, std::max (0, pi.numChoices - 1));
        s.state.count = pi.numChoices;
        s.state.index = n;
        s.setItem (pi.numChoices > 0 ? pi.choices[n] : "");
    }

    void pullControls (IInstanceView& inst)
    {
        const int f = editFilter_;
        const int mIdx = paramForId (kModel, f);
        const int model = mIdx >= 0 ? static_cast<int> (std::lround (inst.param (mIdx))) : 0;
        const bool formant = model == kModelFormant;
        if (full_.formantMode() != formant)
            full_.setFormantMode (formant);
        full_.setCombMode (model == kModelComb);   // COMB is shown for the Comb model only (hidden, not dimmed); VAR and CLEAN are hidden with the Formant model, CLEAN with the Comb model
        pullStepper (inst, full_.model, kModel);

        const int sIdx = paramForId (kStyle, f);
        const int styleCount = synthyboy::filters::filterModelStyleCount (model);
        const char* const* styleNames = ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
        if (sIdx >= 0)
        {
            const int curStyle = static_cast<int> (std::lround (inst.param (sIdx)));
            const int n = std::clamp (curStyle, 0, std::max (0, styleCount - 1));
            // Pulling a preset is read-only.  An out-of-range legacy style is
            // displayed clamped; writeChoice() performs the documented clamp
            // only when the user actually changes MODEL.
            full_.style.setEnabled (true);
            full_.style.state.count = styleCount;
            full_.style.state.index = n;
            full_.style.setItem (styleNames != nullptr && n < styleCount ? styleNames[n] : "");

            // One-style rule: a model with one style shows the style control dim / as a plain label
            if (styleCount <= 1)
            {
                full_.style.state.dim = true;
                full_.style.listable = false;
            }
            else
            {
                full_.style.state.dim = false;
                full_.style.listable = true;
            }
        }
        else
        {
            full_.style.setEnabled (false);
        }
        pullToggle (inst, full_.power, kPower);
        pullToggle (inst, full_.clean, kClean);
        pullToggle (inst, full_.env1, kEnv1);
        pullList (inst, full_.routing, kRouting);
        for (const auto& kid : { std::make_pair (&full_.cutoff, static_cast<int> (kCutoff)), std::make_pair (&full_.resonance, static_cast<int> (kResonance)), std::make_pair (&full_.drive, static_cast<int> (kDrive)),
                                 std::make_pair (&full_.mix, static_cast<int> (kMix)), std::make_pair (&full_.keytrack, static_cast<int> (kKeytrack)), std::make_pair (&full_.var, static_cast<int> (kVar)),
                                 std::make_pair (&full_.combOffset, static_cast<int> (kCombOffset)),
                                 std::make_pair (&full_.blend, static_cast<int> (kBlend)), std::make_pair (&full_.formantX, static_cast<int> (kFormantX)), std::make_pair (&full_.formantY, static_cast<int> (kFormantY)),
                                 std::make_pair (&full_.formantTrans, static_cast<int> (kFormantTrans)), std::make_pair (&full_.formantRes, static_cast<int> (kFormantRes)),
                                 std::make_pair (&full_.formantSpread, static_cast<int> (kFormantSpread)) })
            pullKnob (inst, *kid.first, kid.second);
        full_.setModuleEnabled (full_.power.state.on, anyFilterOn (inst));
    }

    /** The read-only part of the OUT block: where the filter sends its signal and how much (the Mixer strip F1 / F2 is the place to edit it). The texts are the ones of the Mixer strip: the parameter's own format. */

    static int paramForKey (int f, const char* key) { return uibind::indexOfKey ("filter" + std::to_string (f) + "." + key); }

    /** The groups of the page in reading order: the filter screen has MAIN, TRACK, OUT (and FORMANT with the Formant model); the output panel has one, the modulation list one per row. The active group is on Q 1 .. 4, the
        next three (wrapping) on 5 .. 16. */
    void publishQGroup (IInstanceView& v)
    {
        const char* name = "";
        if (pageTop_ == kPageMods)
        {
            name = "Mods";
            qSlots_.publish (v, mods_.numRows(), mods_.activeRow(), [this] (int row, int* t) { modRowTargets (row < ui::ModulationsPage::kPerPage ? modRows_[row].slot : -1, t); });
            setRowBlocks (mods_, ui::ModulationsPage::kPerPage, mods_.numRows(), mods_.activeRow());
        }
        else if (pageTop_ == kPageTargets)
        {
            name = "Targets";
            qSlots_.publish (v, targets_.numRows(), targets_.activeRow(), [this] (int row, int* t) { modRowTargets (row >= 0 && row < ui::ModTargetsPage::kPerPage ? targetRows_[row].slot : -1, t); });
            setRowBlocks (targets_, ui::ModTargetsPage::kPerPage, targets_.numRows(), targets_.activeRow());
        }
        else if (pageTop_ == 0 && curveView())
        {
            // NOTE / VELO: EDIT [POINT X Y CURVE] = virtual 0 .. 3, LEGATO AND GRID [LEGATO, GRID X, GRID Y, SNAP] = the legato parameter and virtual 4 .. 6 (the Filter page has no virtual target of its own)
            ScalarCurveUi& cu = curveUi();
            const int ag = std::clamp (cu.screen.activeGroup(), 0, ui::ScalarCurveEditor::kMaxGroups - 1);
            name = ui::ScalarCurveEditor::groupName (ag);
            qSlots_.publish (v, ui::ScalarCurveEditor::kMaxGroups, ag, [&cu] (int g, int* t) { cu.qTargets (g, t); });
            const int ids[ui::ScalarCurveEditor::kMaxGroups] = { 0, 1 };
            setBlocks (cu.screen.groups, ui::ScalarCurveEditor::kMaxGroups, ids, ui::ScalarCurveEditor::kMaxGroups, ag);
        }
        else if (pageTop_ == 0 && isSplit())
        {
            const bool selOn = full_.power.state.on;
            const bool anyOn = full_.anyFilterOn();
            const int pos = std::clamp (splitQBank_, 0, 3);
            name = pos == 3 ? ui::ScalarCurveEditor::groupName (0) : ui::FilterFullScreen::groupName (pos);
            ScalarCurveUi& cu = curveUi();
            qSlots_.publish (v, 4, pos, [&] (int k, int* t)
            {
                if (k == 3)
                {
                    cu.qTargets (0, t);
                    return;
                }
                for (int i = 0; i < 4; ++i)
                {
                    const ui::Component* m = full_.groups[k].member (i);
                    const bool allow = (k == 2 && i == 1) ? anyOn : selOn;
                    t[i] = (allow && m != nullptr) ? paramOfMember (m) : -1;
                }
            });
            for (int g = 0; g < 3; ++g)
                full_.groups[g].setBlock ((g - pos + 4) % 4);
            full_.groups[3].setBlock (-1);
            cu.screen.groups[0].setBlock ((3 - pos + 4) % 4);
            syncSplitQFocus();
        }
        else if (pageTop_ == 0)
        {
            const int ag = full_.activeGroup();
            name = ui::FilterFullScreen::groupName (ag);
            const bool selOn = full_.power.state.on;
            const bool anyOn = full_.anyFilterOn();
            // When both filters are OFF, filter Q-Link exposes no targets. When only
            // one filter is OFF, shared filter.routing in OUT stays active/writable.
            if (! selOn && ! anyOn)
            {
                qSlots_.publish (v, 0, 0, [] (int, int*) {});
                for (ui::QGroup& g : full_.groups)
                    g.setBlock (-1);
                ui::setText (qName_, sizeof qName_, name);
                return;
            }
            int ids[ui::FilterFullScreen::kMaxGroups], pos = -1;
            int used = 0;
            for (int g = 0; g < ui::FilterFullScreen::kMaxGroups; ++g)
                if (g != 3 || full_.formantMode())
                {
                    if (g == ag)
                        pos = used;
                    ids[used++] = g;
                }
            qSlots_.publish (v, used, pos, [&] (int k, int* t)
            {
                for (int i = 0; i < 4; ++i)
                {
                    const ui::Component* m = full_.groups[ids[k]].member (i);
                    const bool allow = (ids[k] == 2 && i == 1) ? anyOn : selOn;
                    t[i] = (allow && m != nullptr) ? paramOfMember (m) : -1;
                }
            });
            setBlocks (full_.groups, ui::FilterFullScreen::kMaxGroups, ids, used, pos);
        }
        else
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        ui::setText (qName_, sizeof qName_, name);
    }
    int paramOfMember (const ui::Component* m) const noexcept
    {
        while (m != nullptr)
        {
            if (m->id() > 0)
                return paramForId (m->id(), editFilter_);
            m = m->primaryChild();
        }
        return -1;
    }

    int w_ = kCanvasW, h_ = kCanvasH;
    ui::MenuButton pzOpen_ { "", ui::MenuButton::Icon::Expand };
    ui::PzCurveEditor pzEditor_;
    const IInstanceView* pzOwner_ = nullptr;
    uint64_t pzGeneration_ = 0;
    int pzFilter_ = 1;
    bool pzButtonShown_ = false;
    bool pzToolbarSupported_ = false;
    bool nativePeek_ = false;
    int peekFilter_ = -1; // fixed filter while embedded in the Mixer; the native tab keeps its own view
    int standaloneFilter_ = -1; // fixed filter instance when operating as a standalone tab (1 or 2)
    bool nativeGround_ = false;
    int nativeLastView_ = -1, nativeLastScalar_ = -1, nativeLastModel_ = -1, nativeUsedMask_ = 0;
    bool nativeLastOn_ = false, nativeLastAnyOn_ = true, nativeLastSplit_ = false;
    int nativeTouchSlot_ = -1, blockedNativeSlot_ = -1, nativeModControl_ = -1, nativeTouchX_ = 0, nativeTouchY_ = 0;
    double nativeTouchAt_ = 0.0;
    int stackDepth_ = 0, pageTop_ = 0;
    int view_ = 0, filterSel_ = 0, scalarSel_ = 0, expandMode_ = tabview::kFilterSplit, splitQBank_ = 0, editFilter_ = 1;
    bool dual_ = false;               // the F1+F2 view toggle (view only)
    bool started_ = false;
    double nowMs_ = 0.0;

    double sampleRate_ = 48000.0;
    std::unique_ptr<CurveSource> curve1_, curve2_;
    std::vector<float> curveResult_;

    ui::FilterFullScreen full_;
    ScalarCurveUi curve_[2];   // [0] velocity (VELO), [1] note (NOTE)
    ui::ListPopup destList_;
    ui::ModulationsPage mods_;
    ui::ModTargetsPage targets_;
    NativeModTargets nativeTargets_;
    ModRow targetRows_[ui::ModTargetsPage::kPerPage];
    int targetTotal_ = 0, targetsPageIndex_ = 0, destGroup_ = -1;
    std::vector<const char*> destNames_;

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    int activeModParam_ = -1, modsTitle_ = 0, modsAddOffer_ = 0;
    bool modsDirty_ = true;
    double modsAtMs_ = -1000.0;
    ModRow modRows_[ui::ModulationsPage::kPerPage];
    NativeModTargets nativeModulations_;
    int modsPageIndex_ = 0, modsFilterModel_ = -1;
    uint64_t modsPreset_ = 0;

    int envSel_ = 1, lfoSel_ = 1, selectedSource_ = ui::kNoSource;

    QSlots qSlots_;
    char qName_[20] = {};
    uint64_t stateGeneration_ = 0;
};

}} // namespace synthyboy_mpc::overlay
