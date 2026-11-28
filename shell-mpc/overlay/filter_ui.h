// SynthyBoy - MPC overlay: the FILTER UI, the page of the ONE Filter tab: the parameter binding rows of the two filters (binding::Row, one table per filter), the Q-Link banks (binding::QTable), the geometry of the native
// cells of the host skin (filter_geometry.h), the native bridge, the response display (FilterDisplay: a display with its handle), the NOTE / VELO curve editor (ScalarCurveUi: canvas and data of its native cells), the PZ curve editor and the
// detail pages (the modulations of one control, the targets of NOTE / VELO: ModRowsPage data). The page holds no control (owner decision 2026-11-27: the C++ layer has no UI elements). One object per (page view, plugin instance).
// Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw (), with the instance as IInstanceView. Docs: docs/MPC.md "Filter UI".
// FOUR STATES (the chunk-only view state mpcTabView.filter, IInstanceView::tabView (kTabViewFilter)): 0 F1, 1 F2 (the controls edit that filter; the F1+F2 toggle is a VIEW only: both curves and the resultant are drawn),
// 2 NOTE, 3 VELO: the scalar curve editor of the source (ScalarCurveUi, ui/scalar_curve_editor.h) in the body under the top bar; its TARGETS cell opens the targets page of the source (23 velocity, 24 note).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementFilter, the pages above it are ours): the page is always open; detail pages above it: the modulations of one control (a long press on a dial), the
// targets of NOTE / VELO and the list that picks a new target (ListPopup: the host list), the PZ curve editor. LEVEL, PAN, DEST, BUS 1 and BUS 2 are read-only here (the Mixer is their home); BLEND and ROUTING are editable.
// The display shows the response the filter really has: shell-mpc/overlay/curve_source.h runs the engine's own filter on an impulse (once per change, with the live, modulated cutoff and resonance of the voice that is
// heard); F1 + F2 shows both and the resultant by the routing. What it does with a touch: the display gives a Change { id, kind, value } (the finger on the handle); a native cell of the skin writes its value through the native
// bridge (updateNativeControls) into the row it stands for (row.key, the engine table); the write goes through IInstanceView::writeParam (normalised; the plugin tells the host). Every update () reads every parameter back, so
// host automation, presets and the host's own widgets show on the next frame.
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
#include "../ui/binding.h"
#include "../ui/choice_names.h"
#include "../ui/filter_display.h"
#include "../ui/list.h"
#include "../ui/play_rows.h"
#include "../ui/pz_curve_editor.h"
#include "../ui/scalar_curve_editor.h"
#include "backgrounds_manifest.h"
#include "curve_source.h"
#include "filter_geometry.h"
#include "mod_rows_page.h"
#include "native_marker_covers.h"
#include "page_view.h"
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

class FilterUi
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

    // ---- the parameter rows: nineteen per filter, in the order of nativeui::FilterSlot (row index = kRowsPerFilter * (filter - 1) + slot)
    /** Q-Link: MAIN [CUTOFF, RESO, DRIVE, MIX], TRACK [KEYTRACK, ENV 1, VAR (the Formant model: its RESONANCE), COMB (Comb model only)], OUT [BLEND, ROUTING, MODEL, STYLE], FORMANT [X, Y, TRANSPOSE, SPREAD]; CLEAN and the
        power switch have no slot. ROUTING is the one shared parameter (`filter.routing`) of both filters. */
#define SB_FLT_C(ID, F, KEY, QG, QS) binding::Row { ID, "filter" #F "." KEY, binding::Kind::Continuous, 0, 0, 0, 0, binding::Format::Engine, nullptr, 0, QG, QS, false }
#define SB_FLT_T(ID, F, KEY, QG, QS) binding::Row { ID, "filter" #F "." KEY, binding::Kind::Toggle, 0, 0, 0, 0, binding::Format::Engine, nullptr, 0, QG, QS, false }
#define SB_FLT_H(ID, F, KEY, QG, QS) binding::Row { ID, "filter" #F "." KEY, binding::Kind::Choice, 0, 0, 0, 0, binding::Format::ChoiceName, nullptr, 0, QG, QS, false }
#define SB_FILTER(F) \
    SB_FLT_T (kPower, F, "on", -1, -1), SB_FLT_C (kCutoff, F, "cutoff", 0, 0), SB_FLT_C (kResonance, F, "resonance", 0, 1), SB_FLT_C (kDrive, F, "drive", 0, 2), SB_FLT_C (kMix, F, "mix", 0, 3), \
    SB_FLT_C (kKeytrack, F, "keytrack", 1, 0), SB_FLT_T (kEnv1, F, "env1", 1, 1), SB_FLT_C (kVar, F, "var", 1, 2), SB_FLT_C (kCombOffset, F, "comb_offset", 1, 3), SB_FLT_C (kBlend, F, "blend", 2, 0), \
    binding::Row { kRouting, "filter.routing", binding::Kind::Choice, 0, 0, 0, 0, binding::Format::ChoiceName, nullptr, 0, 2, 1, false }, \
    SB_FLT_C (kFormantX, F, "formant.x", 3, 0), SB_FLT_C (kFormantY, F, "formant.y", 3, 1), SB_FLT_C (kFormantTrans, F, "formant.transpose", 3, 2), SB_FLT_C (kFormantRes, F, "formant.resonance", 1, 2), \
    SB_FLT_C (kFormantSpread, F, "formant.spread", 3, 3), SB_FLT_T (kClean, F, "clean_mode", -1, -1), SB_FLT_H (kModel, F, "model", 2, 2), SB_FLT_H (kStyle, F, "style", 2, 3)
    static constexpr int kRowsPerFilter = nativeui::FilterPoint;
    static constexpr binding::Row kRows[2 * kRowsPerFilter] = { SB_FILTER (1), SB_FILTER (2) };
#undef SB_FILTER
#undef SB_FLT_H
#undef SB_FLT_T
#undef SB_FLT_C
    static constexpr int kGroups = 4;   // MAIN, TRACK, OUT, FORMANT
    /** The Q banks as slot numbers (add kRowsPerFilter * (filter - 1) for the row index); the TRACK bank swaps VAR for the formant RESONANCE with the Formant model and has COMB with the Comb model only (qMember). */
    static constexpr binding::QTable<kGroups> kQ { { { nativeui::FilterCutoff, nativeui::FilterResonance, nativeui::FilterDrive, nativeui::FilterMix },
                                                  { nativeui::FilterKeytrack, nativeui::FilterEnv1, nativeui::FilterVar, nativeui::FilterCombOffset },
                                                  { nativeui::FilterBlend, nativeui::FilterRouting, nativeui::FilterModel, nativeui::FilterStyle },
                                                  { nativeui::FilterFormantX, nativeui::FilterFormantY, nativeui::FilterFormantTranspose, nativeui::FilterFormantSpread } } };
    static constexpr const binding::Row& rowOf (int filter, int slot) noexcept { return kRows[(filter - 1) * kRowsPerFilter + slot]; }
    static const char* groupName (int g) noexcept { static const char* const n[kGroups] = { "MAIN", "TRACK", "OUT", "FORMANT" }; return g >= 0 && g < kGroups ? n[g] : ""; }
    /** The slot of a control id (-1: none); the ids of the display's changes are the dials they stand for. */
    static constexpr int slotOfId (int id) noexcept
    {
        switch (id)
        {
            case kPower: return nativeui::FilterPower;
            case kCutoff: case kDisplayCutoff: return nativeui::FilterCutoff;
            case kResonance: case kDisplayReso: return nativeui::FilterResonance;
            case kDrive: return nativeui::FilterDrive;
            case kMix: return nativeui::FilterMix;
            case kKeytrack: return nativeui::FilterKeytrack;
            case kEnv1: return nativeui::FilterEnv1;
            case kVar: return nativeui::FilterVar;
            case kCombOffset: return nativeui::FilterCombOffset;
            case kBlend: return nativeui::FilterBlend;
            case kRouting: return nativeui::FilterRouting;
            case kFormantX: case kDisplayFormant: return nativeui::FilterFormantX;
            case kFormantY: case kDisplayFormantY: return nativeui::FilterFormantY;
            case kFormantTrans: return nativeui::FilterFormantTranspose;
            case kFormantRes: return nativeui::FilterFormantResonance;
            case kFormantSpread: return nativeui::FilterFormantSpread;
            case kClean: return nativeui::FilterClean;
            case kModel: return nativeui::FilterModel;
            case kStyle: return nativeui::FilterStyle;
            default: return -1;
        }
    }
    static constexpr int idOfSlot (int slot) noexcept { return slot >= 0 && slot < kRowsPerFilter ? kRows[slot].id : 0; }

    using ModRow = uibind::ModRow;
    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kModelFormant = 5, kModelComb = 6, kModelPz = 21;   // the model indices shared by the engine and the patch format
    static constexpr double kModsRefreshMs = 100.0;
    static constexpr int kViewF1 = 0, kViewF2 = 1, kViewNote = 2, kViewVelo = 3;   // the states of the page (the selector's candidate id is the state + 1)

    explicit FilterUi (int standaloneFilter = -1)
        : standaloneFilter_ (standaloneFilter)
    {
        for (int f = 0; f < 2; ++f)
            for (int s = 0; s < kRowsPerFilter; ++s)
                params_[f][s] = kRows[f * kRowsPerFilter + s].hasParam() ? uibind::indexOfKey (kRows[f * kRowsPerFilter + s].key) : -1;
        curve_[0].configure (0);   // velocity (the state VELO)
        curve_[1].configure (1);   // note (the state NOTE)
        assignIds();
        geo_.setPageMode (true);
        setCanvas (kCanvasW, kCanvasH);
        if (standaloneFilter_ >= 1 && standaloneFilter_ <= 2)
            adoptSplitState (standaloneFilter_ - 1, scalarSel_, tabview::kFilterExpandedFilter);
        else
            syncLayoutMode();
    }

    int standaloneFilter() const noexcept { return standaloneFilter_; }
    bool isStandalone() const noexcept { return standaloneFilter_ >= 1 && standaloneFilter_ <= 2; }

    /** The page of a hardware sub-tab (one tab page = one screen): Filter 1, Filter 2, Note or Velo (the view's instance number 1 .. 4). The state of the page is the sub-tab's: nothing is read from or written to the
        chunk view state (`mpcTabView.filter`), there is no VIEW selector (F1 | F2 | NOTE | VELO), no 60/40 split and no expand toggle. The F1+F2 curve view toggle stays (a view of the page, not a choice of what it shows). */
    void setSubTab (bool on) noexcept
    {
        subTab_ = on;
        geo_.setSubTabPage (on);
        if (on)
            adoptSplitState (filterSel_, scalarSel_, tabview::kFilterExpandedFilter);
    }

    // Native controls are the only owner of the controls; there is no fallback switch.
    bool hasNativePower() const noexcept { return true; }
    void setHostBackground (bool on) noexcept { nativeGround_ = on; }
    bool hostBackground() const noexcept
    {
        return nativeGround_ && nativeControlsActive() && w_ == page::kW && h_ == page::kH
            && &ui::theme::L() == &ui::theme::kLook1;
    }
    ui::Rect nativePowerRect() const noexcept { return geo_.cell (nativeui::FilterPower); }

    int nativeViewFrame() const noexcept
    {
        if (! hasNativePower() || pageTop_ != 0)
            return 0;
        if (curveView())
            return scalarSel_ == 0 ? 3 : 4;
        return filterSel_ + 1;
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        geo_.place (tab);
        syncLayoutMode();
        destList_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        destList_.setPopup (tab);
        destList_.setId (kDestList);
        destList_.setBackdropId (kDestBackdrop);
        destList_.setHeaderId (kDestHeader);
        mods_.place (tab);
        targets_.place (tab);
        pzEditor_.place (tab);
        syncDisplayGeometry();
    }


    /** Reads the instance: the state of the page (F1, F2, NOTE, VELO: `mpcTabView.filter`), the real response curves, every value of the controls, the modulations of the knobs, the pages of the stack, the Q group.
        `in.targets` the live cutoff and resonance of both filters (cutoff 1, resonance 1, cutoff 2, resonance 2). */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
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
                while (stack.depth() > 1)
                    const_cast<UiStack&> (stack).back();
            }
            syncPages (stack);
        }
        const int rawView = inst.tabView (IInstanceView::kTabViewFilter);
        int targetFilter = 0, targetScalar = 0, targetExpand = tabview::kFilterSplit;
        tabview::decodeFilterSplit (rawView, targetFilter, targetScalar, targetExpand);
        if (standaloneFilter_ >= 1 && standaloneFilter_ <= 2)
        {
            targetFilter = standaloneFilter_ - 1;
            targetExpand = tabview::kFilterExpandedFilter;
        }
        else if (subTab_)
        {
            if (in.instance >= 3)   // Note (3) or Velo (4): the scalar curve editor of the source, in the expanded curve view
            {
                targetFilter = filterSel_;
                targetScalar = std::clamp (in.instance, 3, 4) - 3;
                targetExpand = tabview::kFilterExpandedCurve;
            }
            else   // Filter 1 (1) or Filter 2 (2)
            {
                targetFilter = std::clamp (in.instance, 1, 2) - 1;
                targetScalar = scalarSel_;
                targetExpand = tabview::kFilterExpandedFilter;
            }
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
                pickingSource_ = false;
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
            if (! geo_.expand().empty())
                pzOpen_.place ({ geo_.expand().x - 64, geo_.expand().y, 56, 56 });
            else
                pzOpen_.place (ui::playrows::fullRect (geo_.displayRect()));
        }
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }

        auto& mutableStack = const_cast<UiStack&> (stack);
        updateNativeControls (inst, mutableStack);
        nativeTargets_.update (inst, targets_, pageTop_ == kPageTargets, nativeui::targets::Filter,
                               curveSource(), targetRows_, [&] (ui::Change change) { handle (change, mutableStack, inst); });
        const auto destination = activeModParam_ >= 0
            ? synthyboy::modDestinationChoiceOf (static_cast<size_t> (activeModParam_)) : 0;
        const int pageBeforeMods = pageTop_;
        nativeModulations_.update (inst, mods_, ! pickingSource_ && modsContextValid (inst),
            nativeui::targets::Filter, static_cast<int> (destination), modRows_,
            [&] (ui::Change change) { handle (change, mutableStack, inst); }, nativeui::targets::Modulations);
        syncPages (stack);
        if (pageBeforeMods == kPageMods && pageTop_ == 0)
            updateNativeControls (inst, mutableStack);
        const bool pzNative = nativePzToolbar();
        pzEditor_.setNativeToolbar (pzNative);
        inst.setNativePzToolbar (pzNative ? pzFilter_ : 0, pzNative ? pzToolbarAvailability() : nativeui::pz::None);
        syncDisplayGeometry();
        inst.setUiFullOpen (nativeViewFrame() == 0);
        publishQGroup (inst);
    }

    int view() const noexcept { return view_; }                       // 0 F1, 1 F2, 2 NOTE, 3 VELO (expanded compat)

    bool isSplit() const noexcept { return ! isStandalone() && expandMode_ == tabview::kFilterSplit; }
    bool curveView() const noexcept { return ! isStandalone() && expandMode_ == tabview::kFilterExpandedCurve; }
    bool curveShown() const noexcept { return isSplit() || curveView(); }
    bool filterShown() const noexcept { return isSplit() || ! curveView(); }
    bool isDual() const noexcept { return dual_ && ! isSplit() && ! curveView(); }   // the F1+F2 view toggle
    /** The scalar curve editor of the selected scalar source (NOTE when scalarSel_ == 0, VELO when scalarSel_ == 1). */
    ScalarCurveUi& curveUi() noexcept { return curve_[scalarSel_ == 0 ? 1 : 0]; }
    const ScalarCurveUi& curveUi() const noexcept { return curve_[scalarSel_ == 0 ? 1 : 0]; }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    int topPage() const noexcept { return pageTop_; }
    ui::Rect displayRect() const noexcept { return geo_.displayRect(); }
    void leftTab (IInstanceView& inst) noexcept
    {
        nativeTargets_.hide (inst, targets_);
        nativeModulations_.hide (inst, mods_);
        pzOwner_ = nullptr;
        hideNativeControls (inst);
        inst.setNativePzToolbar (0, nativeui::pz::None);
    }

    // The Filter already owns its ground inside draw().
    void drawPageGround (gpu::SceneBuilder&) const noexcept {}

    /** The Filter foreground, including its opaque ground or full-screen detail. */
    void draw (gpu::SceneBuilder& sb) const
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
        if (pageTop_ == kPageMods)
        {
            mods_.draw (sb);
            if (pickingSource_)
                destList_.draw (sb);
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
        drawFilterPage (sb);
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
            if (pageTop_ == 0)
                drawCurvePageTitle (sb);   // above the opaque graph frame of the editor
            if (isSplit() && pageTop_ == 0)
            {
                const ui::Rect eb = ed.block (0);
                if (splitQBank_ == 3)
                    ed.groups[0].draw (sb, nativeControlsActive() ? &eb : nullptr);
            }
            if (pageTop_ == 0)
                ed.drawLive (sb);   // the live marker of the last note: the animated layer, nothing cached repaints with it
        }
    }

    bool covers (int x, int y) const noexcept
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
        return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y);
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
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
            {
                if (pageTop_ == kPageTargets || pageTop_ == kPageMods)
                {
                    // a touch on a row of the page makes it the active row (the Q group of the page); the cells themselves are the skin's
                    ModRowsPage& pg = pageTop_ == kPageMods ? mods_ : targets_;
                    if (const int row = pg.rowAt (x, y); row >= 0)
                        pg.selectRow (row);
                    publishQGroup (inst);
                    return true;
                }
                return false;
            }
            gesture_ = top;
            gestureDown_ = true;
            if (isSplit() && pageTop_ == 0 && top == &curveUi().screen)
            {
                if (curveUi().screen.block (0).contains (x, y) || curveUi().screen.canvas.rect().contains (x, y))
                    splitQBank_ = 3;
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

    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        nowMs_ = nowMs;
        if (curveShown() && pageTop_ == 0)
            curveUi().tick (inst, nowMs);   // the long press of the canvas, the throttled write that was left over
        if (gesture_ && gestureDown_)
            dispatch (gesture_->tick (nowMs), stack, inst);
        if (nativeTouchSlot_ >= 0 && nativeModControl_ >= 0 && nativeControlsActive() && nowMs - nativeTouchAt_ >= 500.0)
        {
            if (powerOn_)
            {
                blockedNativeSlot_ = nativeTouchSlot_;
                handle (ui::Change::modsRequest (nativeModControl_), stack, inst);
                hideNativeControls (inst);
            }
            nativeModControl_ = -1;
        }
        drainCurveRequests (stack, inst);
    }

    /** The hardware knob `slot` (0 .. 3) of the active group turned by `delta`: the dial / switch / choice of the member, as the group's row says (nothing while the filter is off; ROUTING while any filter is on). */
    void turnQ (int slot, double delta, UiStack& stack, IInstanceView& inst)
    {
        if (pageTop_ == kPagePz)
            return;
        if (pageTop_ == kPageMods || pageTop_ == kPageTargets)
        {
            // the three cells of the active row: depth, polarity, on / off
            ModRowsPage& pg = pageTop_ == kPageMods ? mods_ : targets_;
            const ModRow* rows = pageTop_ == kPageMods ? modRows_ : targetRows_;
            const int row = pg.activeRow();
            if (row < 0 || slot < 0 || slot > 2 || rows[row].slot < 0)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (rows[row].slot);
            if (slot == 0)
            {
                const double cur = uibind::normOf (sp.amount, inst.param (sp.amount));
                const double next = binding::turnNorm (cur, delta);
                if (std::fabs (next - cur) >= 1.0e-12)
                    handle (ui::Change::normalised (pageTop_ == kPageMods ? kModsRow + 4 * row : kTargetsRow + 4 * row, next), stack, inst);
            }
            else
            {
                const int id = (pageTop_ == kPageMods ? kModsRow : kTargetsRow) + 4 * row + slot;
                const int p = slot == 1 ? sp.bipolar : sp.bypass;
                const bool on = slot == 1 ? inst.param (p) >= 0.5f : inst.param (p) < 0.5f;   // the shown state (the switch says ON: the opposite of bypassed)
                const bool next = delta > 0.0;
                if (next != on)
                    handle (ui::Change::indexed (id, next ? 1 : 0, next ? 1.0 : 0.0), stack, inst);
            }
        }
        else if (curveView() || (isSplit() && splitQBank_ == 3))
        {
            ScalarCurveUi& cu = curveUi();
            const int g = isSplit() ? 0 : cu.screen.activeGroup();
            if (g >= 0 && g < ui::ScalarCurveEditor::kMaxGroups)
                cu.handle (cu.screen.groups[g].turn (slot, delta), inst, nowMs_);
            drainCurveRequests (stack, inst);
        }
        else
            turnMember (activeGroup_, slot, delta, inst);
    }

    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the display or the editor under the pointer takes it as steps of its own. false: nothing of this page lies there, or a finger is down on it. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        ui::Component* top = topComponent (x, y);
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

    bool fingerDown() const noexcept { return gestureDown_ || nativeTouchSlot_ >= 0; }

    void dropGesture() noexcept
    {
        if (gesture_ != nullptr)
            gesture_->touchUp (-1, -1);
        gesture_ = nullptr;
        gestureDown_ = false;
        nativeTouchSlot_ = nativeModControl_ = -1;
    }
    /** Another element's page was open: the Q group is published again at the next update. */
    void forgetQGroup() noexcept { qSlots_.forget(); }

    const FilterGeometry& geometry() const noexcept { return geo_; }
    ModRowsPage& modsPage() noexcept { return mods_; }
    ModRowsPage& targetsPage() noexcept { return targets_; }
    ui::ListPopup& destList() noexcept { return destList_; }
    ui::FilterDisplay& display() noexcept { return display_; }
    const CurveSource* curve (int filter) const noexcept { return filter == 2 ? curve2_.get() : curve1_.get(); }
    int paramOf (int id, int filter) const noexcept { return paramForId (id, filter); }
    bool formantMode() const noexcept { return geo_.formantMode(); }
    bool combMode() const noexcept { return geo_.combMode(); }
    int activeGroup() const noexcept { return activeGroup_; }

private:

    static void coverRootMarkers (gpu::SceneBuilder& sb) noexcept
    {
        drawNativeMarkerCovers (sb, "filter1");
    }

    bool nativePzOpen() const noexcept { return pzButtonShown_ && powerOn_ && nativeControlsActive(); }
    bool nativePzToolbar() const noexcept
    {
        return pzToolbarSupported_ && hasNativePower() && pageTop_ == kPagePz && pzOwner_ != nullptr
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
        inst.setNativeFilterControls (0, nullptr, nullptr, nullptr);
    }

    // ---- which native cells exist (the slots of the filter part, nativeui::FilterPower .. FilterStyle)
    bool hasNativeGeometry (int slot) const noexcept
    {
        using namespace nativeui;
        if (curveView())
            return false;
        switch (slot)
        {
            case FilterVar: return ! geo_.formantMode();
            case FilterClean: return ! geo_.formantMode() && ! geo_.combMode();
            case FilterCombOffset: return geo_.combMode();
            case FilterFormantX: case FilterFormantY: case FilterFormantTranspose: case FilterFormantSpread:
                return geo_.formantMode() && ! geo_.splitMode();
            case FilterFormantResonance: return geo_.formantMode();
            default: return true;
        }
    }
    bool hasNativeControl (int slot) const noexcept
    {
        return hasNativeGeometry (slot) && (slot != nativeui::FilterStyle || styleCount_ > 1)
            && (slot == nativeui::FilterPower || (slot == nativeui::FilterRouting ? anyOn_ : powerOn_));
    }
    std::array<ui::Rect, nativeui::FilterPoint> nativeRects (bool includePassive = false) const noexcept
    {
        std::array<ui::Rect, nativeui::FilterPoint> out {};
        for (int slot = 0; slot < nativeui::FilterPoint; ++slot)
            if (slot != nativeui::FilterRouting && (includePassive ? hasNativeGeometry (slot) : hasNativeControl (slot)))
                out[slot] = geo_.cell (slot);
        return out;
    }
    ui::Rect nativeSingleStyleRect() const noexcept
    {
        return ! curveView() && powerOn_ && styleCount_ == 1 ? geo_.cell (nativeui::FilterStyle) : ui::Rect {};
    }
    ui::Rect nativeOutReadoutRect() const noexcept
    {
        return ! curveView() && ! geo_.splitMode() ? geo_.outReadout() : ui::Rect {};
    }
    /** The member of Q group g at slot k (a nativeui::FilterSlot; -1: none): the TRACK bank holds the formant RESONANCE instead of VAR with the Formant model and COMB with the Comb model only. */
    int qMember (int g, int k) const noexcept
    {
        int s = kQ.row (g, k);
        if (g == 1 && k == 2 && geo_.formantMode())
            s = nativeui::FilterFormantResonance;
        if (g == 1 && k == 3 && ! geo_.combMode())
            s = -1;
        return s;
    }
    void setActiveGroup (int g) noexcept { activeGroup_ = g >= 0 && g < kGroups ? g : -1; }

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
        const auto areas = nativeRects();
        for (int s = 0; s < FilterPoint; ++s)
            if (areas[s].contains (x, y))
            {
                nativeTouchSlot_ = s == FilterModel || s == FilterStyle ? -1 : s;
                if (s != FilterPower && s != FilterClean && s != FilterModel && s != FilterStyle && s != FilterRouting && s != FilterEnv1)
                    nativeModControl_ = idOfSlot (s);
                for (int group = 0; group < kGroups; ++group)
                    for (int knob = 0; knob < binding::QTable<kGroups>::kSlots; ++knob)
                        if (qMember (group, knob) == s)
                        {
                            setActiveGroup (group);
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
            out[idx++] = geo_.view();
            if (isSplit())
            {
                out[idx++] = geo_.scalarView();
                out[idx++] = geo_.expand();
                out[idx++] = curveUi().screen.nativeExpandRect();
            }
            else
            {
                if (! curveView())
                    out[idx++] = geo_.both();
                out[idx++] = geo_.expand();
            }
            if (filterShown())
            {
                const auto areas = nativeRects (includeReadouts);
                for (const auto& r : areas)
                    if (! r.empty() && idx < static_cast<int> (out.size()))
                        out[idx++] = r;
                if (nativePzOpen() && idx < static_cast<int> (out.size()))
                    out[idx++] = pzOpen_.rect();
                if (includeReadouts)
                {
                    if (idx < static_cast<int> (out.size())) out[idx++] = nativeOutReadoutRect();
                    if (idx < static_cast<int> (out.size())) out[idx++] = nativeSingleStyleRect();
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
            out[0] = geo_.cell (nativeui::FilterPower);
        return out;
    }


    nativeui::FxCell nativeCell (int slot, IInstanceView& inst) const noexcept
    {
        using namespace nativeui;
        if (slot == FilterAction)
            return FxCell { 0, 0, kFxDial };
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
        if (slot < FilterPoint && filterShown() && hasNativeControl (slot))
        {
            const bool selOn = isSelectedFilterOn (inst);
            const bool allow = slot == FilterPower
                || (slot == FilterRouting ? anyFilterOn (inst) : selOn);
            if (! allow)
                return {};
            const int target = params_[editFilter_ - 1][slot];
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
        if (subTab_ && (action == FilterF1 || action == FilterF2 || action == FilterNote || action == FilterVelo
                        || action == FilterExpandFilter || action == FilterExpandCurve))
            return;   // the hardware sub-tab names the state of the page: no view choice, no split, no expand
        if (action == FilterF1 || action == FilterF2)
            chooseFilter (inst, action - FilterF1);
        else if (action == FilterNote || action == FilterVelo)
            chooseScalar (inst, action - FilterNote);
        else if (action == FilterBoth && ! isSplit() && ! curveView())
            handle (ui::Change::indexed (kBoth, ! dual_, 0.0), stack, inst);
        else if (action == FilterEditPz && filterShown())
            handlePz (kPzOpen, stack, inst);
        else if (action == FilterExpandFilter && ! isStandalone() && ! subTab_)
        {
            const int next = expandMode_ == tabview::kFilterSplit ? tabview::kFilterExpandedFilter : tabview::kFilterSplit;
            chooseSplitState (inst, filterSel_, scalarSel_, next);
        }
        else if (curveShown() && action >= FilterInit && action <= FilterTargets
                 && (! isSplit() || action == FilterTargets))
        {
            using Editor = ui::ScalarCurveEditor;
            const int ids[] = { Editor::kIdInit, Editor::kIdUndo, Editor::kIdDelete, Editor::kIdTargets };
            curveUi().handle (ui::Change::request (ids[action - FilterInit]), inst, nowMs_);
            drainCurveRequests (stack, inst);
        }
        else if (action == FilterExpandCurve && ! isStandalone() && ! subTab_)
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
        const bool powerBefore = powerOn_;
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
            nativeLastOn_ = powerOn_;
            nativeLastAnyOn_ = anyOn_;
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
            indicators[4] = powerOn_ ? 2 : (anyOn_ ? 1 : 0);
        else
            indicators[4] = (dual_ && ! curveView() ? 1 : 0) | ((filterShown() && powerOn_) ? 2 : 0);
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
            setActiveGroup (-1);
            curveUi().screen.setActiveGroup (0);
        }
        else
        {
            if (activeGroup_ != splitQBank_)
                setActiveGroup (splitQBank_);
            curveUi().screen.setActiveGroup (-1);
        }
    }

    /** The display follows the geometry (the page of a curve view has none). */
    void syncDisplayGeometry()
    {
        if (geo_.curveView())
            display_.place ({});
        else
            display_.placeGraph (geo_.displayRect(), geo_.cell (nativeui::FilterPower), geo_.expand());
    }

    void syncLayoutMode()
    {
        const bool sp = isSplit();
        const int fv = curveView() ? (scalarSel_ == 0 ? kViewNote : kViewVelo) : filterSel_;
        geo_.setCurveView (fv >= kViewNote);
        geo_.setSplitMode (sp);
        syncDisplayGeometry();
        ui::Rect compactRect {};
        if (sp)
        {
            const ui::Rect fr = geo_.rect();
            const int lx = fr.x + 8, lw = (fr.w - 28) * 60 / 100;
            const int rx = lx + lw + 12, rw = fr.right() - 8 - rx;
            compactRect = { rx, geo_.scalarView().y, rw, fr.bottom() - 8 - geo_.scalarView().y };
        }
        for (ScalarCurveUi& c : curve_)
        {
            c.screen.setCompact (sp);
            c.screen.setOwnQFrames (! sp);
            c.screen.place (sp ? compactRect : geo_.bodyRect());
        }
        view_ = fv;
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


    void assignIds()
    {
        pzOpen_.setId (kPzOpen);
        pzEditor_.back.setId (kPzBack);
        pzEditor_.reset.setId (kPzReset);
        pzEditor_.remove.setId (kPzRemove);
        pzEditor_.apply.setId (kPzApply);
        pzEditor_.canvas.setId (kPzCanvas);
        display_.assignIds (kDisplayCutoff, kDisplayReso, kDisplayFormant, kDisplayFormantY);
        mods_.back.setId (kModsBack);
        mods_.page.setId (kModsPage);
        mods_.add.setId (kModsAdd);
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ModRowsPage::kPerPage; ++r)
        {
            mods_.setRowIds (r, kModsRow + 4 * r, kModsRow + 4 * r + 1, kModsRow + 4 * r + 2, kModsRow + 4 * r + 3);
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
        }
    }

    /** The engine parameter behind a control id of filter `filterNum` (1 or 2; -1: none). */
    int paramForId (int id, int filterNum) const noexcept
    {
        const int s = slotOfId (id);
        return s >= 0 && filterNum >= 1 && filterNum <= 2 ? params_[filterNum - 1][s] : -1;
    }

    /** The part a touch at (x, y) starts on: a list above the tab, the PZ editor, in the NOTE / VELO states the editor under the top bar, else the response display (every other control is a native cell). */
    ui::Component* topComponent (int x, int y) noexcept
    {
        if (pageTop_ == kPageMods) return pickingSource_ ? &destList_ : nullptr;
        if (pageTop_ == kPageTargets) return nullptr;
        if (pageTop_ == kPageDest) return &destList_;
        if (pageTop_ == kPagePz) return &pzEditor_;
        if (isSplit() && geo_.scalarView().contains (x, y))
            return nullptr;
        if (curveShown() && curveUi().screen.rect().contains (x, y))
            return &curveUi().screen;
        if (curveView() && y >= geo_.bodyRect().y)
            return &curveUi().screen;
        return curveView() ? nullptr : &display_;
    }
    bool gestureOnCurve() noexcept { return curveShown() && pageTop_ == 0 && gesture_ == &curveUi().screen; }


    void syncPages (const UiStack& stack)
    {
        const int top = stack.depth() > 1 ? stack.top() : 0;
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
                if (! isStandalone() && ! subTab_ && pageTop_ == 0)
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
        while (gesture_ == &display_ && display_.takePending (extra))
            handle (extra, stack, inst);
    }

    void chooseSplitState (IInstanceView& inst, int f, int s, int e)
    {
        f = std::clamp (f, 0, 1);
        s = std::clamp (s, 0, 1);
        e = std::clamp (e, 0, 2);
        if (! isStandalone() && ! subTab_)
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
        if (v < kViewNote)
            chooseFilter (inst, v);
        else
            chooseSplitState (inst, filterSel_, v - kViewNote, tabview::kFilterExpandedCurve);
    }

    bool isPzModel (const IInstanceView& inst) const noexcept
    {
        return ! curveView() && inst.param (paramForId (kModel, editFilter_)) == kModelPz;
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
                pickingSource_ = false;
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
        if (c.id >= kModsRow && c.id < kModsRow + 4 * ModRowsPage::kPerPage)
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
        if (c.id >= kTargetsRow && c.id < kTargetsRow + 4 * ModRowsPage::kPerPage)
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
                    if (! isStandalone() && ! subTab_)
                    {
                        const int next = isSplit() ? tabview::kFilterExpandedFilter : tabview::kFilterSplit;
                        chooseSplitState (inst, filterSel_, scalarSel_, next);
                    }
                    break;
                case kModsBack:
                    if (pickingSource_)
                        pickingSource_ = false;
                    else
                        stack.back();
                    break;
                case kModsAdd:
                    if (modsContextValid (inst))
                        openSourceList();
                    break;
                case kDestBackdrop:
                    if (pickingSource_)
                    {
                        pickingSource_ = false;
                        break;
                    }
                    stack.back();
                    break;
                case kTargetsBack: stack.back(); break;
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
            if (pickingSource_)
            {
                if (c.index >= 0 && c.index < sourceCount_)
                    addModulation (inst, sourceIds_[c.index]);
                pickingSource_ = false;
                return;
            }
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

    /** The parameter of a control of the page (a dial, a choice, a switch). */
    int activeParamOfControl (int id) const noexcept { return paramForId (id, editFilter_); }

    /** One detent of the hardware knob `knob` of group `group`: the member's row decides (a dial steps 1 / 128, a switch goes on / off, a choice steps whole numbers); a filter that is off takes none, ROUTING needs any filter on. */
    void turnMember (int group, int knob, double delta, IInstanceView& inst)
    {
        if (group < 0 || curveView())
            return;
        const int slot = qMember (group, knob);
        if (slot < 0)
            return;
        const bool routing = slot == nativeui::FilterRouting;
        if (routing ? ! anyOn_ : ! powerOn_)
            return;
        const binding::Row& row = rowOf (editFilter_, slot);
        const int p = params_[editFilter_ - 1][slot];
        if (p < 0)
            return;
        switch (row.kind)
        {
            case binding::Kind::Continuous:
            {
                const double cur = uibind::normOf (p, inst.param (p));
                const double next = binding::turnNorm (cur, delta);
                if (std::fabs (next - cur) >= 1.0e-12)
                    uibind::writeNorm (inst, p, next);
                break;
            }
            case binding::Kind::Toggle:
            {
                const bool on = delta > 0.0;
                if (on != (inst.param (p) >= 0.5f))
                    uibind::writePlain (inst, p, on ? 1.0 : 0.0);
                break;
            }
            case binding::Kind::Choice:
            {
                const int n = info (p).numChoices;
                const int cur = static_cast<int> (std::lround (inst.param (p)));
                const int next = binding::turnChoice (cur, static_cast<int> (std::lround (delta)), n);
                if (next != cur)
                    writeChoice (inst, p, next);
                break;
            }
            default: break;
        }
    }

    // ---- the targets page of NOTE / VELO: the slots of the matrix whose source is the velocity (23) or the note (24)
    int curveSource() const noexcept { return scalarSel_ == 0 ? 24 : 23; }

    static void targetName (size_t destChoice, char* out, size_t cap)
    {
        const int p = synthyboy::modDestinationParamIndex (destChoice);
        std::snprintf (out, cap, "%s", p >= 0 ? uibind::info (p).name : "-");
    }
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        (void) fresh;
        constexpr int kPer = ModRowsPage::kPerPage;
        const int src = curveSource();
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, src, slots, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + kPer - 1) / kPer);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * kPer, n = std::clamp (total - first, 0, kPer);
        ModRowsPage::Entry e[kPer];
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[first + i]);
            targetName (static_cast<size_t> (std::lround (v.param (sp.destination))), e[i].name, sizeof e[i].name);
            targetRows_[i] = { slots[first + i], src };
        }
        for (int i = n; i < kPer; ++i)
            targetRows_[i] = {};
        targetTotal_ = total;
        targets_.load (e, n, src == 24 ? "NOTE" : "VELO", ui::theme::sourceColour (src == 24 ? "NOTE" : "VEL"), total, targetsPageIndex_);
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
        constexpr int rows = ModRowsPage::kPerPage;
        const int position = uibind::targetPosition (v, source, slot);
        targetsPageIndex_ = position / rows;
        loadTargets (v, true);
        targets_.selectRow (position % rows);
        modsDirty_ = true;
    }

    /** The source list of the modulations page (ADD): the selected source first, then the first envelopes and LFOs; the host list is the only list there is (the popup of the targets list). */
    void openSourceList()
    {
        int offer[8], n = 0;
        auto push = [&] (int s) { if (s == ui::kNoSource || n >= 8) return; for (int i = 0; i < n; ++i) if (offer[i] == s) return; offer[n++] = s; };
        push (selectedSource_);
        for (int s : { 7, 8, 1, 2, 9, 3, 19, 20 }) push (s);
        destNames_.clear();
        sourceText_.clear();
        sourceText_.reserve (8);
        for (int i = 0; i < n; ++i)
        {
            sourceIds_[i] = offer[i];
            sourceText_.push_back (uibind::sourceName (offer[i]));
        }
        for (const std::string& s : sourceText_)
            destNames_.push_back (s.c_str());
        sourceCount_ = n;
        destList_.state.header = "ADD SOURCE";
        destList_.state.headerBack = false;
        destList_.state.items = destNames_.data();
        destList_.state.count = n;
        destList_.state.current = -1;
        destList_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        destList_.showCurrent();
        pickingSource_ = true;
    }

    // ---- the modulations page of one control

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

    static const char* labelOfId (int id) noexcept
    {
        switch (id)
        {
            case kCutoff: return "CUTOFF";
            case kResonance: return "RESO";
            case kDrive: return "DRIVE";
            case kMix: return "MIX";
            case kKeytrack: return "KEYTRACK";
            case kVar: return "VAR";
            case kCombOffset: return "COMB";
            case kBlend: return "BLEND";
            case kFormantX: return "X";
            case kFormantY: return "Y";
            case kFormantTrans: return "TRANSPOSE";
            case kFormantRes: return "F RESO";
            case kFormantSpread: return "SPREAD";
            default: return nullptr;
        }
    }

    void loadMods (const IInstanceView& v, int focusSlot = -1)
    {
        char title[13] = "MODULATIONS";
        if (const char* label = labelOfId (modsTitle_))
            std::snprintf (title, sizeof title, "%.12s", label);
        const auto window = uibind::modulationWindow (v, activeModParam_, selectedSource_, modsPageIndex_, modRows_, focusSlot);
        modsPageIndex_ = window.page;
        ModRowsPage::Entry e[ModRowsPage::kPerPage];
        const int n = std::clamp (window.entries.numMods, 0, ModRowsPage::kPerPage);
        for (int i = 0; i < n; ++i)
        {
            std::snprintf (e[i].name, sizeof e[i].name, "%s", window.entries.mods[i].name);
            e[i].source = window.entries.mods[i].source;
            e[i].macro = window.entries.mods[i].macro;
        }
        mods_.load (e, n, title, 0xffffff, window.total, window.page);
        if (window.focusRow >= 0)
            mods_.selectRow (window.focusRow);
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
            pickingSource_ = false;
            loadMods (v, slot);
        }
    }
    void refreshMods (IInstanceView& inst)
    {
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
        if (pageTop_ == kPageTargets || pageTop_ == kPageDest)
            loadTargets (inst, false);
        if (pageTop_ == kPageMods && activeModParam_ >= 0)
            loadMods (inst);
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
        ui::FilterDisplay& d = display_;
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

    /** The state of the page from the parameters: the model (the Formant / Comb layout), the number of styles, the switches (a filter that is off dims the display and takes no edit). */
    void pullControls (IInstanceView& inst)
    {
        const int f = editFilter_;
        const int mIdx = paramForId (kModel, f);
        const int model = mIdx >= 0 ? static_cast<int> (std::lround (inst.param (mIdx))) : 0;
        const bool formant = model == kModelFormant;
        if (geo_.formantMode() != formant)
        {
            geo_.setFormantMode (formant);
            if (! formant && activeGroup_ == 3)
                setActiveGroup (0);
        }
        display_.state.isFormant = formant;
        geo_.setCombMode (model == kModelComb);   // COMB is shown for the Comb model only (hidden, not dimmed); VAR and CLEAN are hidden with the Formant model, CLEAN with the Comb model
        syncDisplayGeometry();
        styleCount_ = paramForId (kStyle, f) >= 0 ? synthyboy::filters::filterModelStyleCount (model) : 0;
        const int pIdx = paramForId (kPower, f);
        powerOn_ = pIdx >= 0 && inst.param (pIdx) >= 0.5f;
        anyOn_ = anyFilterOn (inst);
        display_.setEnabled (powerOn_);
        display_.setInactive (! powerOn_);
    }

    /** The picture of the filter page: the response display, the titles of the blocks, the frame of the active Q bank. */
    void drawFilterPage (gpu::SceneBuilder& sb) const
    {
        if (curveView())
            return;   // the NOTE / VELO page title is drawn after the curve editor frame (drawCurvePageTitle)
        display_.draw (sb);
        {
            ui::Pen p (sb, true, ! powerOn_);
            ui::Cell cell (sb, geo_.rect());
            drawLabels (p);
        }
        const bool formantBank = geo_.formantMode() && ! geo_.splitMode();
        const int maxG = formantBank ? kGroups : 3;
        if (ui::QGroup::wideDevice() || activeGroup_ < 0 || activeGroup_ >= maxG || activeGroup_ == 2)   // the OUT group is frameless: its members stand in two places
            return;
        const ui::Rect b = geo_.block (activeGroup_);
        if (b.empty())
            return;
        const ui::Rect frame = geo_.splitMode() ? b.inset (-1) : b;
        ui::Pen pen (sb, true, ! powerOn_);
        ui::Cell cell (sb, ui::Rect::unite (frame, ui::Pen::qMarkerRect (frame)));
        pen.qFocus (frame);
    }
    void drawLabels (ui::Pen& p) const
    {
        const ui::theme::Look& T = ui::theme::L();
        const ui::Rect g = geo_.displayRect();
        if (! g.empty())
        {
            const char* hdr = ! powerOn_
                ? (view_ == kViewF2 ? "FILTER 2 Â· OFF (BYPASSED)" : "FILTER 1 Â· OFF (BYPASSED)")
                : (geo_.formantMode() ? (view_ == kViewF2 ? "FILTER 2 Â· FORMANT XY" : "FILTER 1 Â· FORMANT XY")
                                      : (view_ == kViewF2 ? "FILTER 2 Â· RESPONSE" : "FILTER 1 Â· RESPONSE"));
            p.textLeft ({ g.x + 74, g.y + 4, std::max (0, g.w - 204), 56 },
                        hdr, ui::theme::kPageTitleSize, p.c (powerOn_ ? T.text : T.textFaint, 255), T.fMin);   // the page title: the size of every page (static-native-captions.json filter1 / filter2 draws the same rect on the host layer)
        }
        // "SET ON MIXER" above the OUT readouts is a host-layer caption (static-native-captions.json filter1 / filter2): one copy, no overlay twin.
        for (int i = 0; i < kGroups; ++i)
        {
            const ui::Rect b = geo_.block (i);
            if ((i != 3 || (geo_.formantMode() && ! geo_.splitMode())) && ! b.empty())
                p.textLeft ({ b.x + FilterGeometry::kBlockPad + 4, b.y + 3, std::max (0, b.w - 2 * FilterGeometry::kBlockPad - 4), 16 }, groupName (i), T.fLabel - 2.0f, p.c (activeGroup_ == i ? T.q : T.textDim, 255), T.fMin);
        }
    }
    /** The page title of the NOTE / VELO sub-tab (static-native-captions.json filternote / filtervelo draws the same rect on the host layer). It is drawn AFTER the curve editor: its graph frame is opaque and covers the top
        strip of the page, so a title drawn with the page never showed on the overlay plane. */
    void drawCurvePageTitle (gpu::SceneBuilder& sb) const
    {
        if (! curveView() || ! geo_.subTabPage() || geo_.rect().w <= 0)
            return;
        ui::Pen p (sb, true, false);
        p.textLeft ({ geo_.rect().x + 22, geo_.rect().y + 4, 400, 56 }, view_ == kViewNote ? "FILTER NOTE" : "FILTER VELO", ui::theme::kPageTitleSize, p.c (ui::theme::L().text, 255), ui::theme::L().fMin);
    }


    /** The groups of the page in reading order: the filter screen has MAIN, TRACK, OUT (and FORMANT with the Formant model); the modulation list one per row. The active group is on Q 1 .. 4, the
        next three (wrapping) on 5 .. 16. */
    void publishQGroup (IInstanceView& v)
    {
        const char* name = "";
        if (pageTop_ == kPageMods || pageTop_ == kPageTargets)
        {
            const bool md = pageTop_ == kPageMods;
            name = md ? "Mods" : "Targets";
            ModRowsPage& pg = md ? mods_ : targets_;
            const ModRow* rows = md ? modRows_ : targetRows_;
            qSlots_.publish (v, pg.numRows(), pg.activeRow(), [rows] (int row, int* t) { modRowTargets (row >= 0 && row < ModRowsPage::kPerPage ? rows[row].slot : -1, t); });
            setRowBlocks (pg, ModRowsPage::kPerPage, pg.numRows(), pg.activeRow());
        }
        else if (pageTop_ == 0 && curveView())
        {
            // NOTE / VELO: EDIT [POINT X Y CURVE] = virtual 0 .. 3, LEGATO AND GRID [LEGATO, GRID X, GRID Y, SNAP] = the legato parameter and virtual 4 .. 6 (the Filter page has no virtual target of its own)
            ScalarCurveUi& cu = curveUi();
            const int ag = std::clamp (cu.screen.activeGroup(), 0, ui::ScalarCurveEditor::kMaxGroups - 1);
            name = ui::ScalarCurveEditor::groupName (ag);
            qSlots_.publish (v, ui::ScalarCurveEditor::kMaxGroups, ag, [&cu] (int g, int* t) { cu.qTargets (g, t); });
        }
        else if (pageTop_ == 0 && isSplit())
        {
            const int pos = std::clamp (splitQBank_, 0, 3);
            name = pos == 3 ? ui::ScalarCurveEditor::groupName (0) : groupName (pos);
            ScalarCurveUi& cu = curveUi();
            qSlots_.publish (v, 4, pos, [&] (int k, int* t)
            {
                if (k == 3)
                {
                    cu.qTargets (0, t);
                    return;
                }
                memberTargets (k, t);
            });
            syncSplitQFocus();
        }
        else if (pageTop_ == 0)
        {
            const int ag = activeGroup_;
            name = groupName (ag);
            // When both filters are OFF, filter Q-Link exposes no targets. When only
            // one filter is OFF, shared filter.routing in OUT stays active/writable.
            if (! powerOn_ && ! anyOn_)
            {
                qSlots_.publish (v, 0, 0, [] (int, int*) {});
                ui::setText (qName_, sizeof qName_, name);
                return;
            }
            int ids[kGroups], pos = -1, used = 0;
            for (int g = 0; g < kGroups; ++g)
                if (g != 3 || geo_.formantMode())
                {
                    if (g == ag)
                        pos = used;
                    ids[used++] = g;
                }
            qSlots_.publish (v, used, pos, [&] (int k, int* t) { memberTargets (ids[k], t); });
        }
        else
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        ui::setText (qName_, sizeof qName_, name);
    }
    /** The four targets of Q group g of the filter page (a filter that is off has none; ROUTING needs any filter on). */
    void memberTargets (int g, int* t) const noexcept
    {
        for (int i = 0; i < 4; ++i)
        {
            const int slot = qMember (g, i);
            const bool allow = (g == 2 && i == 1) ? anyOn_ : powerOn_;
            t[i] = (allow && slot >= 0) ? params_[editFilter_ - 1][slot] : -1;
        }
    }

    int w_ = kCanvasW, h_ = kCanvasH;
    ui::ControlModel pzOpen_;
    ui::PzCurveEditor pzEditor_;
    const IInstanceView* pzOwner_ = nullptr;
    uint64_t pzGeneration_ = 0;
    int pzFilter_ = 1;
    bool pzButtonShown_ = false;
    bool pzToolbarSupported_ = false;
    int standaloneFilter_ = -1; // fixed filter instance when operating as a standalone tab (1 or 2)
    bool subTab_ = false;       // the page of a hardware sub-tab: Filter 1, Filter 2, Note or Velo (the view's instance number), see setSubTab
    bool nativeGround_ = false;
    int nativeLastView_ = -1, nativeLastScalar_ = -1, nativeLastModel_ = -1, nativeUsedMask_ = 0;
    bool nativeLastOn_ = false, nativeLastAnyOn_ = true, nativeLastSplit_ = false;
    int nativeTouchSlot_ = -1, blockedNativeSlot_ = -1, nativeModControl_ = -1, nativeTouchX_ = 0, nativeTouchY_ = 0;
    double nativeTouchAt_ = 0.0;
    int stackDepth_ = 0, pageTop_ = 0;
    int view_ = 0, filterSel_ = 0, scalarSel_ = 0, expandMode_ = tabview::kFilterSplit, splitQBank_ = 0, editFilter_ = 1, activeGroup_ = 0;
    bool dual_ = false;               // the F1+F2 view toggle (view only)
    bool started_ = false;
    bool powerOn_ = false, anyOn_ = true;   // the selected filter is on; any filter is on
    int styleCount_ = 0;                    // the styles of the model shown
    double nowMs_ = 0.0;
    int params_[2][kRowsPerFilter] = {};    // the engine parameter index of every row of filter 1 and 2 (-1: none)

    double sampleRate_ = 48000.0;
    std::unique_ptr<CurveSource> curve1_, curve2_;
    std::vector<float> curveResult_;

    FilterGeometry geo_;
    ui::FilterDisplay display_;
    ScalarCurveUi curve_[2];   // [0] velocity (VELO), [1] note (NOTE)
    ui::ListPopup destList_;
    ModRowsPage mods_ { ModRowsPage::Kind::Modulations };
    ModRowsPage targets_ { ModRowsPage::Kind::Targets };
    NativeModTargets nativeTargets_;
    ModRow targetRows_[ModRowsPage::kPerPage];
    int targetTotal_ = 0, targetsPageIndex_ = 0, destGroup_ = -1;
    std::vector<const char*> destNames_;
    std::vector<std::string> sourceText_;
    int sourceIds_[8] = {}, sourceCount_ = 0;
    bool pickingSource_ = false;      // the add-source choice of the modulations page is open (the host list over it)

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    int activeModParam_ = -1, modsTitle_ = 0;
    bool modsDirty_ = true;
    double modsAtMs_ = -1000.0;
    ModRow modRows_[ModRowsPage::kPerPage];
    NativeModTargets nativeModulations_;
    int modsPageIndex_ = 0, modsFilterModel_ = -1;
    uint64_t modsPreset_ = 0;

    int envSel_ = 1, lfoSel_ = 1, selectedSource_ = ui::kNoSource;

    QSlots qSlots_;
    char qName_[20] = {};
    uint64_t stateGeneration_ = 0;
};

}} // namespace synthyboy_mpc::overlay
