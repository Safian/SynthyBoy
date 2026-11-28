// SynthyBoy - MPC overlay: the envelope UI of the Env tab and of the compact view of the Play page: the parameter binding rows of the six envelopes (binding::Row, one table per envelope), the Q-Link banks (binding::QTable), the geometry of
// the native cells of the host skin, the native bridge, the envelope graph (a display with its handles) and the detail pages (the targets of the envelope, the modulations of one control: ModRowsPage data). The page holds no
// control (owner decision 2026-11-27: the C++ layer has no UI elements); one object per (page view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw*,
// with the instance as IInstanceView.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementEnv, the pages above it are ours): main = the compact view in the middle third of the Play page (the graph; the four ADSR dials and the selector
// are native cells of the Play skin); full = the page of an Env tab (the graph; every control is a native cell); detail pages above it: the targets of the envelope (the modulation matrix seen from the source), the list that
// picks a new target (groups, then the parameters of a group; the native picker cells: native_list_cells.h), the modulations of one control (a long press on a dial).
// What it does with a touch: the graph gives a Change { id, kind, value } (a handle moved); a native cell of the skin writes its value through the native bridge (consumeNativeEditor) into the row it stands for (row.key, the
// engine table); the write goes through IInstanceView::writeParam (normalised; the plugin tells the host). Every update () reads every parameter back, so host automation, presets and the host's own widgets show on the next frame.
// Units: the time dials and the graph follow `envN.units`: milliseconds (envN.attack ..) or beats (envN.attack_beats ..); the sustain and the three curves have one parameter each.
// Live: the running dots come from LiveValues::envVoiceStage / envVoiceFraction (one per sounding voice, the newest first); an engine that fills only envFineStage / envFineFraction gives the one dot.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../native_ui_params.h"
#include "../q_router.h"
#include "../ui/binding.h"
#include "../ui/env_graph.h"
#include "../ui/native_apertures.h"
#include "../ui/play_rows.h"
#include "instance_view.h"
#include "mod_rows_page.h"
#include "modulation_window.h"
#include "native_list_cells.h"
#include "native_marker_covers.h"
#include "native_mod_targets_ui.h"
#include "page_layout.h"
#include "page_view.h"
#include "q_publish.h"
#include "q_publish_mod.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class EnvUi
{
public:
    // ---- the element and its pages (UiStack ids)
    static constexpr int kElementEnv = 2;
    static constexpr int kElementPage = kElementEnv;   // the page of an Env tab is the full screen of the element (page_view.h)
    enum Page { kPageTargets = 30, kPageList = 31, kPageMods = 32 };

    // ---- the id spaces (a Change comes out with the id the owner gave)
    enum Id : int
    {
        kGraph = 3, kTargets = 4,
        kDial = 100,                        // 100 + dial index: the dials (the compact view and the full screen share the ids)
        kBeats = 130, kLegato,
        kDialBeats = 140,                   // 140 + dial index: the rows of the time dials in beats
        kTargetsBack = 150, kTargetsPage, kTargetsAdd, kTargetsRow = 160,    // 160 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kModsBack = 600, kModsRow = 610, kModsAddStep = 660, kModsAdd, kModsPage,
        kList = 700, kListBackdrop, kListHeader
    };
    enum DialId : int { kAttack = 0, kDecay, kSustain, kRelease, kACurve, kDCurve, kRCurve, kDelay, kHold, kNumDials };

    // ---- the parameter rows: sixteen per envelope (row index = kRowsPerEnv * (envelope - 1) + slot)
    /** The slots of an envelope's rows: the nine dials (DialId), the units switch (BEATS), the retrigger switch (the engine's `legato_inverted`), then the beats versions of the five time dials. */
    enum Slot : int { kSlotUnits = kNumDials, kSlotLegato, kSlotAttackBeats, kSlotDecayBeats, kSlotReleaseBeats, kSlotDelayBeats, kSlotHoldBeats, kRowsPerEnv };
    static constexpr double kLongPressMs = 600.0;   // the long press of a native cell that opens the modulations of the control
    static constexpr int kEnvs = 6;
    /** Q-Link: three groups, ADSR [attack, decay, sustain, release], SHAPE [A curve, D curve, R curve], TIME [delay, hold, BEATS, RETRIG]; the beats rows share the slot of their dial (the engine parameter in force is chosen by the units). */
#define SB_ENV_C(ID, N, KEY, QG, QS) binding::Row { ID, "env" #N "." KEY, binding::Kind::Continuous, 0, 0, 0, 0, binding::Format::Engine, nullptr, 0, QG, QS, false }
#define SB_ENV_T(ID, N, KEY, QG, QS) binding::Row { ID, "env" #N "." KEY, binding::Kind::Toggle, 0, 0, 0, 0, binding::Format::Engine, nullptr, 0, QG, QS, false }
#define SB_ENV(N) \
    SB_ENV_C (100, N, "attack", 0, 0), SB_ENV_C (101, N, "decay", 0, 1), SB_ENV_C (102, N, "sustain", 0, 2), SB_ENV_C (103, N, "release", 0, 3), \
    SB_ENV_C (104, N, "attack_curve", 1, 0), SB_ENV_C (105, N, "decay_curve", 1, 1), SB_ENV_C (106, N, "release_curve", 1, 2), \
    SB_ENV_C (107, N, "delay", 2, 0), SB_ENV_C (108, N, "hold", 2, 1), SB_ENV_T (130, N, "units", 2, 2), SB_ENV_T (131, N, "legato_inverted", 2, 3), \
    SB_ENV_C (140, N, "attack_beats", 0, 0), SB_ENV_C (141, N, "decay_beats", 0, 1), SB_ENV_C (143, N, "release_beats", 0, 3), \
    SB_ENV_C (147, N, "delay_beats", 2, 0), SB_ENV_C (148, N, "hold_beats", 2, 1)
    static constexpr binding::Row kRows[kEnvs * kRowsPerEnv] = { SB_ENV (1), SB_ENV (2), SB_ENV (3), SB_ENV (4), SB_ENV (5), SB_ENV (6) };
#undef SB_ENV
#undef SB_ENV_T
#undef SB_ENV_C
    static constexpr int kGroups = 3;
    /** The Q banks as slot numbers of one envelope (add kRowsPerEnv * (envelope - 1) for the row index). */
    static constexpr binding::QTable<kGroups> kQ { { { kAttack, kDecay, kSustain, kRelease }, { kACurve, kDCurve, kRCurve, -1 }, { kDelay, kHold, kSlotUnits, kSlotLegato } } };
    static const char* groupName (int g) noexcept { static const char* const n[kGroups] = { "ADSR", "SHAPE", "TIME" }; return g >= 0 && g < kGroups ? n[g] : ""; }
    /** The row slot of the beats version of a time dial (-1: the dial has none). */
    static constexpr int beatsSlotOf (int slot) noexcept
    {
        return slot == kAttack ? kSlotAttackBeats : slot == kDecay ? kSlotDecayBeats : slot == kRelease ? kSlotReleaseBeats : slot == kDelay ? kSlotDelayBeats : slot == kHold ? kSlotHoldBeats : -1;
    }
    static constexpr const binding::Row& rowOf (int env, int slot) noexcept { return kRows[(env - 1) * kRowsPerEnv + slot]; }

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kCompactX = 440, kCompactW = 400;       // the middle third of the Play page
    static constexpr double kModsRefreshMs = 100.0;

    // ---- the geometry of the native cells
    static constexpr int kTopH = 72, kBottomH = 176, kBlockGap = 8, kBlockTitleH = 38, kBlockPad = 6, kKnobH = 84;
    /** The cells of the page of an Env tab: the nine dials, BEATS, RETRIG, the TARGETS cell, the graph, the blocks. */
    struct FullGeometry
    {
        ui::Rect knob[kNumDials], beats, legato, targets, graph, block[kGroups];
    };
    /** The compact view of the Play page: the graph and the four ADSR dial cells. */
    struct CompactView
    {
        ui::Rect graph, plot, knob[4];
    };

    EnvUi() { setCanvas (kCanvasW, kCanvasH); configure (1); }

    bool nativeTargets() const noexcept { return native(); }

    void setHostBackground (bool on) noexcept { nativeGround_ = on; }
    bool hostBackground() const noexcept
    {
        return nativeGround_ && page_ && stackDepth_ == 1 && pageTop_ == 0
            && w_ == page::kW && h_ == page::kH && &ui::theme::L() == &ui::theme::kLook1;
    }

    bool isNativeControl (int x, int y) const noexcept { return listShown() || nativeCellAt (x, y); }

    /** The envelope this UI shows: on the page of a tab (page_view.h) the one the hardware sub-tab names (Env 1 .. Env 6: the view's own instance number, no selector on the screen, nothing shared with the Play page); on the compact
        view of the Play page the shared Play selection (Play Env, chunk saved). */
    int shownEnv (const IInstanceView& inst) const noexcept { return pageEnv_ > 0 ? pageEnv_ : uibind::selectedEnv (inst); }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        layoutCompact();
        if (page_)
            layoutFull();
        targets_.place (tab);
        mods_.place (tab);
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        mods_.page.setId (kModsPage);
        mods_.back.setId (kModsBack);
        mods_.add.setId (kModsAdd);
        for (int r = 0; r < ModRowsPage::kPerPage; ++r)
        {
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
            mods_.setRowIds (r, kModsRow + 4 * r, kModsRow + 4 * r + 1, kModsRow + 4 * r + 2, kModsRow + 4 * r + 3);
        }
    }

    /** Reads the instance: the selected envelope, every value of the visible controls, the modulations of the dials, the pages of the stack, the dots of the voices, the Q group. */
    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs, const synthyboy::LiveValues* live)
    {
        highlight_ = highlight;
        const uint64_t generation = inst.stateGeneration();
        if (selectorInstance_ != &inst || selectorGeneration_ != generation)
        {
            selectorInstance_ = &inst;
            selectorGeneration_ = generation;
            modsDirty_ = true;
        }
        const int sel = shownEnv (inst);   // the page of a tab: its sub-tab's envelope; the compact view of the Play page: the Play selection (Play Env, chunk saved)
        if (sel != sel_ || ! configured_)
            configure (sel);
        selectedSource_ = uibind::envSource (sel_);
        const int unitsIdx = param_[kSlotUnits];
        beats_ = unitsIdx >= 0 && inst.param (unitsIdx) >= 0.5f;
        const bool open = stack.depth() > 0 && stack.element() == kElementEnv;
        if (open && stack.top() == kPageMods)
        {
            const int parameter = paramOfId (modsTarget_);
            const uint64_t preset = inst.stateGeneration();
            if (modsPreset_ != preset || modsTargetParam_ != parameter)
            {
                dropGesture();
                modsPreset_ = preset;
                modsDirty_ = true;
                pickingSource_ = false;
            }
        }
        if (open && stackDepth_ == 0)
            activeGroup_ = 0;
        stackDepth_ = open ? stack.depth() : 0;
        otherOpen_ = stack.depth() > 0 && ! open;
        syncPages (open ? stack : idle_, inst);
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        pullGraph (inst, live);
        if (open)
            inst.setUiFullOpen (stack.depth() > 1);
        else if (stack.isMain())
            inst.setUiFullOpen (false);
        publishQGroup (inst);
    }

    /** The page of the Env tab (page_view.h): the full screen of the envelope the hardware sub-tab names, nothing to close. */
    void update (IInstanceView& inst, UiStack& stack, double nowMs, const page::Input& in)
    {
        if (! page_)
        {
            page_ = true;
            layoutFull();
        }
        pageEnv_ = std::clamp (in.instance, 1, kEnvs);
        if (selectorInstance_ != &inst || selectorGeneration_ != inst.stateGeneration())
        {
            inst.setNativeEnvEditor (false, nullptr);
            nativeGestureBlocked_ = nativeGestureField_ >= 0;
            gesture_ = nullptr;
            gestureDown_ = false;
        }
        else if (stack.depth() == 1 && stack.element() == kElementEnv && shownEnv (inst) == sel_)
        {
            const auto& targets = nativeCells_[nativeui::env::Targets];
            float action = 0.0f;
            if (targets.target >= 0 && inst.takeNativeEnvField (nativeui::env::Targets, targets, action) && action >= 0.5f)
                stack.openDetail (kPageTargets);   // an opening detail owns the frame before the virtual selector is read
        }
        update (inst, 2, stack, nowMs, in.live);
        syncNativeTargets (inst, stack);
        publishNativeEditor (inst);
        if (consumeNativeEditor (inst, stack))
        {
            update (inst, 2, stack, nowMs, in.live);
            publishNativeEditor (inst);
        }
    }
    /** The whole picture of the page: its ground, the full screen or the page above it, the running dots, the popup. */
    void draw (gpu::SceneBuilder& sb) const
    {
        drawPage (sb);
        drawLive (sb);
    }

    bool isMain() const noexcept { return stackDepth_ == 0; }
    int selection() const noexcept { return sel_; }
    bool beats() const noexcept { return beats_; }
    int topPage() const noexcept { return pageTop_; }
    int activeQ() const noexcept { return activeGroup_; }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }

    // ---- the drawing (a picture is one scene of cached cells, then the live parts, then the popups)
    /** The compact view (main state): the graph and the Q focus of the bank (the host draws everything else of the Play page). */
    void drawCompact (gpu::SceneBuilder& sb) const
    {
        const ui::Rect atk = compactView_.knob[0];
        const ui::Rect dec = compactView_.knob[1];
        const ui::Rect sus = compactView_.knob[2];
        const ui::Rect rel = compactView_.knob[3];
        compactGraph_.draw (sb);
        if (highlight_ == 2 && ! ui::QGroup::wideDevice())
            drawBank (sb, ui::Rect::unite (ui::Rect::unite (atk, dec), ui::Rect::unite (sus, rel)));
    }
    /** The graph of the full screen, and the page of the stack above it (the targets, the modulations of a control; the cells of both and the lists are native). */
    void drawPage (gpu::SceneBuilder& sb) const
    {
        if (listShown())
        {
            if (page_)
                drawNativeMarkerCovers (sb, "env1");   // the native list cells fill the tab
            return;
        }
        if (hostBackground())
        {
            drawNativeMarkerCovers (sb, "env1");
            fullGraph_.draw (sb);
            return;
        }
        if (pageTop_ == 0)
        {
            ui::drawNativeCanvas (sb, ui::Rect { 0, kTabY, w_, kTabH }, nativeApertures());
            fullGraph_.draw (sb);
            return;
        }
        page::ground (sb);
        if (pageTop_ == kPageTargets)
            targets_.draw (sb);
        else if (pageTop_ == kPageMods)
            mods_.draw (sb);
    }
    /** The running dots (the graph that is on screen). */
    void drawLive (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0)
            compactGraph_.drawLive (sb);
        else if (pageTop_ == 0)
            fullGraph_.drawLive (sb);
    }
    /** What the current state draws, as one number (the picture changes when it does): a scene of everything, the live dots included. */
    uint32_t drawHash (gpu::SceneData& scratch) const
    {
        gpu::SceneBuilder sb (scratch);
        sb.begin (gpu::kKindPlay, 0, w_, h_);
        if (stackDepth_ == 0)
            drawCompact (sb);
        else
            drawPage (sb);
        drawLive (sb);
        return page::sceneHash (scratch) ^ static_cast<uint32_t> (stackDepth_ * 7 + pageTop_);
    }
    /** The touch hit this UI's drawing (the main state: the middle third; any other state: the whole tab). */
    bool covers (int x, int y) const noexcept
    {
        if (listShown())
            return false;   // the native list cells fill the tab
        if ((pageTop_ == kPageTargets && targets_.nativeHit (x, y))
            || (pageTop_ == kPageMods && mods_.nativeHit (x, y)))
            return false;
        if (stackDepth_ > 0)
        {
            if (pageTop_ == 0 && nativeCellAt (x, y))
                return false;
            return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y);
        }
        if (! ui::Rect { kCompactX, kTabY + 4, kCompactW, kTabH - 8 }.contains (x, y))
            return false;
        for (const ui::Rect& k : compactView_.knob)
            if (k.contains (x, y))
                return false;
        return true;
    }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** One touch event in UI pixels (the whole screen), `stack` is the instance's stack (the caller holds its lock). true: the touch belongs to the overlay pages. */
    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
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
            gesture_ = nullptr;
            gestureDown_ = false;
            if (page_ && stackDepth_ == 1 && pageTop_ == 0)
            {
                nativeGestureField_ = nativeFieldAt (x, y);
                nativeGestureTarget_ = nativeGestureField_ >= 0 ? nativeCells_[nativeGestureField_].target : -1;
                nativeGestureBlocked_ = false;
                nativeHold_ = nativeGestureField_ >= 0 && nativeGestureField_ < kNumDials;
                nativeDownX_ = x;
                nativeDownY_ = y;
                nativeDownMs_ = nowMs;
            }
            if (! covers (x, y))
            {
                if (pageTop_ == 0 && stackDepth_ > 0 && ! ui::QGroup::wideDevice())
                {
                    const int group = groupAt (x, y);
                    if (group >= 0)
                    {
                        activeGroup_ = group;
                        publishQGroup (inst);
                    }
                }
                return false;
            }
            if (pageTop_ == kPageTargets || pageTop_ == kPageMods)
            {
                // a touch on a row of the page makes it the active row (the Q group of the page); the cells themselves are the skin's
                ModRowsPage& pg = pageTop_ == kPageMods ? mods_ : targets_;
                if (const int row = pg.rowAt (x, y); row >= 0)
                    pg.selectRow (row);
                publishQGroup (inst);
                return true;
            }
            ui::Component* top = topComponent();
            if (! top)
                return false;
            gesture_ = top;
            gestureDown_ = true;
            handle (top->touchDown (x, y, nowMs), stack, inst);
            publishQGroup (inst);
            return true;
        }
        if (ev.kind == TouchEvent::Move && nativeHold_ &&
            (std::abs (x - nativeDownX_) > ui::TapGesture::kSlop || std::abs (y - nativeDownY_) > ui::TapGesture::kSlop))
            nativeHold_ = false;
        if (ev.kind == TouchEvent::Up && nativeGestureField_ >= 0)
        {
            nativeHold_ = false;
            nativeGestureField_ = -1;
            nativeGestureBlocked_ = false;
            publishNativeEditor (inst);
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
        if (nativeHold_ && ! nativeGestureBlocked_ && nowMs - nativeDownMs_ >= kLongPressMs)
        {
            const int field = nativeGestureField_;
            nativeHold_ = false;
            handle (ui::Change::modsRequest (kNativeScalarIds[field]), stack, inst);
            syncPages (stack, inst);
            publishNativeEditor (inst);
        }
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst);
    }
    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the graph or the list under the pointer takes it as steps of its own. false: nothing of
        this page lies there, or a finger is down on it. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        ui::Component* top = topComponent();
        if (gestureDown_)
            return false;
        if (! top || ! covers (x, y))
            return false;
        ui::Change ch;
        if (! top->wheelOver (x, y, notches, ch))
            return false;
        handle (ch, stack, inst);
        (void) nowMs;
        return true;
    }

    bool fingerDown() const noexcept { return gestureDown_ || (nativeHold_ && ! nativeGestureBlocked_); }
    /** Another element's full screen opened or closed: the Q group is published again at the next update (the router keeps what the last page set). */
    void forgetQGroup() noexcept { qSlots_.forget(); }
    void dropGesture() noexcept
    {
        gesture_ = nullptr;
        gestureDown_ = false;
        nativeGestureField_ = -1;
        nativeGestureBlocked_ = false;
        nativeHold_ = false;
    }
    /** The page went off the screen: nothing of this page stays open on the compact view (the list of the selector is the host's). */
    void closePopups() {}

    // ---- access
    CompactView& compact() noexcept { return compactView_; }
    const CompactView& compact() const noexcept { return compactView_; }
    ui::EnvGraph& graph() noexcept { return stackDepth_ == 0 ? compactGraph_ : fullGraph_; }
    void syncNativeTargets (IInstanceView& inst, UiStack& stack)
    {
        using namespace nativeui::targets;
        const auto before = stack.changes();
        nativeTargets_.update (inst, targets_, pageTop_ == kPageTargets, Env,
                               selectedSource_, targetRows_, [&] (ui::Change change) { handle (change, stack, inst); });
        const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (modsTargetParam_));
        nativeModulations_.update (inst, mods_, ! pickingSource_ && modsContextValid (inst),
            Env, static_cast<int> (destination), modRows_,
            [&] (ui::Change change) { handle (change, stack, inst); }, Modulations);
        publishNativeList (inst, stack);
        if (stack.changes() != before)
        {
            stackDepth_ = stack.depth();
            syncPages (stack, inst);
        }
        if (pageTop_ == kPageTargets || pageTop_ == kPageMods || stack.changes() != before)
            publishQGroup (inst);
    }
    void hideNativeTargets (IInstanceView& inst)
    {
        nativeTargets_.hide (inst, targets_);
        nativeModulations_.hide (inst, mods_);
        listCells_.hide (inst);
        publishNativeTags (inst, false);
        targets_.setNativeControls (false);
        mods_.setNativeControls (false);
    }
    ModRowsPage& targetsPage() noexcept { return targets_; }
    ModRowsPage& modsPage() noexcept { return mods_; }
    /** The engine parameter behind a slot of the shown envelope (the milliseconds / beats version chosen by the units; -1: none). */
    int paramOfSlot (int slot) const noexcept
    {
        if (slot < 0 || slot >= kSlotUnits + 2)
            return -1;
        const int b = beatsSlotOf (slot);
        return beats_ && b >= 0 && param_[b] >= 0 ? param_[b] : param_[slot];
    }
    int timeParam (int stage, bool beats) const noexcept
    {
        static const int kStage[5] = { kDelay, kAttack, kHold, kDecay, kRelease };
        if (stage < 0 || stage >= 5)
            return -1;
        const int s = kStage[stage], b = beatsSlotOf (s);
        return beats && b >= 0 && param_[b] >= 0 ? param_[b] : param_[s];
    }
    /** The number of the targets of the shown envelope. */
    int numTargets() const noexcept { return numTargets_; }
    /** The Env tab was left (page_view_base.h): a selected envelope that is neither used nor edited (a `+` pick the user did not touch) falls back to the last used one (uibind::fallbackSelection). */
    void leftTab (IInstanceView& v)
    {
        hideNativeTargets (v);
        selectorInstance_ = nullptr;
        if (page_)
            v.setNativeEnvEditor (false, nullptr);   // the sub-tab's envelope is its own: no shared selection to fall back (no leaveSelector)
        else
            uibind::leaveSelector (uibind::SelKind::Env, v);
    }
    /** The four targets of the ADSR group of the compact view (the Play view maps the groups of the three thirds together) and its block of knobs. */
    void compactTargets (int* t) const noexcept { for (int i = 0; i < 4; ++i) t[i] = paramOfSlot (i); }
    void setCompactBlock (int block) noexcept { compactBlock_ = block; }
    int compactBlock() const noexcept { return compactBlock_; }

private:
    using ModRow = uibind::ModRow;

    static constexpr int kNativeScalarIds[] {
        kDial + kAttack, kDial + kDecay, kDial + kSustain, kDial + kRelease,
        kDial + kACurve, kDial + kDCurve, kDial + kRCurve, kDial + kDelay, kDial + kHold, kBeats, kLegato,
    };
    static_assert (std::size (kNativeScalarIds) == nativeui::env::Selection);
    static constexpr int kGroundX0 = 434, kGroundX1 = 848;

    bool native() const noexcept { return page_; }
    /** The slot of a control id (-1: none): the dials 100 .. 108, BEATS 130, RETRIG 131. */
    static constexpr int slotOfId (int id) noexcept
    {
        return id >= kDial && id < kDial + kNumDials ? id - kDial : id == kBeats ? kSlotUnits : id == kLegato ? kSlotLegato : -1;
    }
    int paramOfId (int id) const noexcept { return paramOfSlot (slotOfId (id)); }
    static const char* labelOfSlot (int slot) noexcept
    {
        static const char* const n[kNumDials] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "A CURVE", "D CURVE", "R CURVE", "DELAY", "HOLD" };
        return slot >= 0 && slot < kNumDials ? n[slot] : "MODULATIONS";
    }
    /** The colour of envelope n (0 .. 5): the orange to red of the look, a step further along it for every envelope. */
    static uint32_t envColour (int n) noexcept
    {
        const ui::theme::Look& T = ui::theme::L();
        return ui::theme::mix (T.env[0], T.env[1], static_cast<float> (((n % kEnvs) + kEnvs) % kEnvs) / 5.0f);
    }

    // ---- the geometry
    void layoutCompact()
    {
        const ui::Rect third { kCompactX, kTabY + 4, kCompactW, kTabH - 8 };
        const int m = 6, W = third.w - 2 * m;
        const ui::Rect gr = ui::playrows::displayRect (third);
        compactGraph_.place (gr);
        compactGraph_.plotArea = ui::playrows::compactPlotRect (gr);   // Play phase 2: the display has no band; the header cells stand in the header band of the third
        compactGraph_.setId (kGraph);
        compactView_.graph = gr;
        compactView_.plot = compactGraph_.plotArea;
        const int gap = ui::playrows::kControlGap;
        const int by = ui::playrows::controlsLine (third), kh = std::max (kKnobH, (third.bottom() - m - by - gap) / 2);
        const int cw = (W - gap) / 2;
        compactView_.knob[0] = { third.x + m, by, cw, kh };
        compactView_.knob[1] = { third.x + m + cw + gap, by, W - cw - gap, kh };
        compactView_.knob[2] = { third.x + m, by + kh + gap, cw, kh };
        compactView_.knob[3] = { third.x + m + cw + gap, by + kh + gap, W - cw - gap, kh };
    }
    void layoutFull()
    {
        const ui::Rect tab { 0, kTabY, w_, kTabH };
        const int m = 8, W = tab.w - 2 * m;
        FullGeometry g;
        // ---- the top bar: the targets cell
        const ui::Rect top { tab.x + m, tab.y + m, W, kTopH - m };
        ui::Rect r[3];
        const int fixed[3] = { 0, 0, 200 }, weight[3] = { 0, 1, 0 };
        pagegeo::layoutRow (top, fixed, weight, 3, pagegeo::kGap, r);
        const int cy = top.y + (top.h - 56) / 2;
        g.targets = { r[2].x, cy, 160, 56 };
        // ---- the bottom band: three blocks (a Q group each), in proportion to the members
        const int bandY = tab.bottom() - m - kBottomH;
        const int unit = (W - 2 * kBlockGap) / 11;
        const int bw[3] = { 4 * unit, 3 * unit, W - 2 * kBlockGap - 7 * unit };
        int bx = tab.x + m;
        for (int i = 0; i < 3; ++i)
        {
            g.block[i] = { bx, bandY, bw[i], kBottomH };
            bx += bw[i] + kBlockGap;
        }
        ui::Rect c[4];
        pagegeo::blockCells (g.block[0], kBlockTitleH, kBlockPad, 4, pagegeo::kGap, c);
        for (int i = 0; i < 4; ++i)
            g.knob[kAttack + i] = c[i];
        pagegeo::blockCells (g.block[1], kBlockTitleH, kBlockPad, 3, pagegeo::kGap, c);
        for (int i = 0; i < 3; ++i)
            g.knob[kACurve + i] = c[i];
        pagegeo::blockCells (g.block[2], kBlockTitleH, kBlockPad, 4, pagegeo::kGap, c);
        g.knob[kDelay] = c[0];
        g.knob[kHold] = c[1];
        g.beats = c[2];
        g.legato = c[3];
        // ---- the middle: the graph (the whole width)
        const int midY = top.bottom() + m, midH = bandY - m - midY;
        g.graph = { tab.x + m, midY, W, std::max (0, midH) };
        geo_ = g;
        fullGraph_.place (g.graph);
        fullGraph_.setId (kGraph);
    }
    /** The rectangle of native field `field` (nativeui::env::Field): a dial, BEATS, RETRIG, the selector, TARGETS. */
    ui::Rect cellOf (int field) const noexcept
    {
        namespace n = nativeui::env;
        if (field >= 0 && field < kNumDials)
            return geo_.knob[field];
        switch (field)
        {
            case n::Beats: return geo_.beats;
            case n::Retrig: return geo_.legato;
            case n::Targets: return nativeTargets() ? geo_.targets : ui::Rect {};
            default: return {};
        }
    }
    bool nativeCellAt (int x, int y) const noexcept
    {
        if (! native())
            return false;
        for (int f = 0; f < nativeui::env::kFields; ++f)
            if (cellOf (f).contains (x, y))
                return true;
        return false;
    }
    /** The group whose native cells hold (x, y): ADSR 0, SHAPE 1, TIME 2 (-1: none). */
    int groupAt (int x, int y) const noexcept
    {
        for (int g = 0; g < kGroups; ++g)
            for (int k = 0; k < 4; ++k)
            {
                const int s = kQ.row (g, k);
                if (s >= 0 && slotCell (s).contains (x, y))
                    return g;
            }
        return -1;
    }
    ui::Rect slotCell (int slot) const noexcept { return slot == kSlotUnits ? geo_.beats : slot == kSlotLegato ? geo_.legato : slot >= 0 && slot < kNumDials ? geo_.knob[slot] : ui::Rect {}; }
    std::array<ui::Rect, nativeui::env::kFields + 1> nativeApertures() const noexcept
    {
        std::array<ui::Rect, nativeui::env::kFields + 1> areas {};
        if (! native())
            return areas;
        for (int f = 0; f < nativeui::env::kFields; ++f)
            areas[f] = cellOf (f);
        return areas;
    }
    static void drawBank (gpu::SceneBuilder& sb, const ui::Rect& frame)
    {
        if (frame.empty())
            return;
        ui::Pen pen (sb, true, false);
        ui::Cell cell (sb, ui::Rect::unite (frame, ui::Pen::qMarkerRect (frame)));
        pen.qFocus (frame);
    }

    // ---- the native bridge
    int nativeFieldAt (int x, int y) const noexcept
    {
        if (nativeTargets() && geo_.targets.contains (x, y))
            return nativeui::env::Targets;
        for (int field = 0; field < nativeui::env::Selection; ++field)
            if (cellOf (field).contains (x, y))
                return field;
        return -1;
    }

    void publishNativeEditor (IInstanceView& inst) noexcept
    {
        namespace n = nativeui::env;
        if (! page_)
            return;
        const bool visible = stackDepth_ == 1 && pageTop_ == 0;
        nativeCells_.fill ({});
        if (visible)
        {
            for (int field = 0; field < n::Selection; ++field)
            {
                const int slot = slotOfId (kNativeScalarIds[field]);
                const int target = paramOfSlot (slot);
                const int kind = rowOf (sel_, slot).kind == binding::Kind::Toggle ? nativeui::kFxToggle : nativeui::kFxDial;
                nativeCells_[field] = { target, target, kind };
            }
            // Actions identify the selected envelope via its sustain index.
            // They never write that engine parameter.
            if (nativeTargets())
                nativeCells_[n::Targets] = { param_[kSustain], param_[kSustain], nativeui::kFxToggle };
        }
        if (nativeGestureField_ >= 0)
        {
            auto& held = nativeCells_[nativeGestureField_];
            nativeGestureBlocked_ |= held.target != nativeGestureTarget_;
            if (nativeGestureBlocked_)
                held = {};
        }
        inst.setNativeEnvTargetCount (numTargets_);
        inst.setNativeEnvEditor (visible, nativeCells_.data());
        publishNativeTags (inst, visible);
    }
    /** What the envelope modulates, as the text lines of the tags in the top bar (nativeui::nativetext::EnvTag0 .., native Labels of the Env tabs): the first targets that fit (three) and "+n" for the rest. */
    void publishNativeTags (IInstanceView& inst, bool visible) const noexcept
    {
        namespace t = nativeui::nativetext;
        const int slots = 3;
        const int shown = visible ? std::min ({ numTargetLines_, numTargets_, slots }) : 0;
        for (int i = 0; i < 3; ++i)
            inst.setNativeText (t::EnvTag0 + i, i < shown ? targetLines_[i] : "");
        char rest[12] = {};
        if (visible && numTargets_ > shown)
            std::snprintf (rest, sizeof rest, "+%d", numTargets_ - shown);
        inst.setNativeText (t::EnvTagMore, rest);
    }

    bool consumeNativeEditor (IInstanceView& inst, UiStack& stack)
    {
        if (stackDepth_ != 1 || pageTop_ != 0)
            return false;
        namespace n = nativeui::env;
        auto take = [&] (int field, float& value)
        {
            const auto& cell = nativeCells_[field];
            return cell.target >= 0 && inst.takeNativeEnvField (field, cell, value);
        };
        float value = 0.0f;
        if (take (n::Targets, value) && value >= 0.5f)
        {
            stack.openDetail (kPageTargets);
            return true;
        }
        // The units switch takes precedence over queued edits in the old domain.
        if (take (n::Beats, value))
        {
            uibind::writeNorm (inst, nativeCells_[n::Beats].target, value);
            return true;
        }
        for (int field = 0; field < n::Selection; ++field)
            if (field != n::Beats && take (field, value))
                uibind::writeNorm (inst, nativeCells_[field].target, value);
        return false;
    }

    // ======================================================================================================================================================================
    // building: the selected envelope decides the parameters behind the rows
    void configure (int sel)
    {
        sel_ = sel;
        configured_ = true;
        for (int s = 0; s < kRowsPerEnv; ++s)
            param_[s] = rowOf (sel, s).hasParam() ? uibind::indexOfKey (rowOf (sel, s).key) : -1;
        pageTop_ = 0;
        underList_ = 0;
        modsDirty_ = true;
        qSlots_.clear();
        qSlots_.forget();
        gesture_ = nullptr; gestureDown_ = false;
        activeGroup_ = 0;
    }

    // ======================================================================================================================================================================
    // reading
    /** The shape of envelope `n` (1 .. 6) from the parameters: normalised times, the sustain level, the curves; false when the table has none of it. */
    bool shapeOf (const IInstanceView& v, int n, bool beats, ui::envgraph::Shape& s, ui::envgraph::Shape* def) const
    {
        const std::string pre = "env" + std::to_string (n) + ".";
        static const char* const kTime[5] = { "delay", "attack", "hold", "decay", "release" };
        const int units = uibind::indexOfKey (pre + "units");
        const bool b = units >= 0 ? v.param (units) >= 0.5f : beats;
        for (int i = 0; i < 5; ++i)
        {
            const int p = uibind::indexOfKey (pre + kTime[i] + (b ? "_beats" : ""));
            if (p < 0)
                return false;
            s.t[i] = uibind::normOf (p, v.param (p));
            s.timeMs[i] = static_cast<double> (v.param (p));
            if (def != nullptr)
            {
                def->t[i] = uibind::normOf (p, uibind::info (p).def);
                def->timeMs[i] = static_cast<double> (uibind::info (p).def);
            }
        }
        const int sp = uibind::indexOfKey (pre + "sustain");
        if (sp < 0)
            return false;
        s.sustain = std::clamp (static_cast<double> (v.param (sp)) / 100.0, 0.0, 1.0);
        if (def != nullptr)
            def->sustain = static_cast<double> (uibind::info (sp).def) / 100.0;
        static const char* const kCurve[3] = { "attack_curve", "decay_curve", "release_curve" };
        for (int i = 0; i < 3; ++i)
        {
            const int p = uibind::indexOfKey (pre + kCurve[i]);
            if (p < 0)
                return false;
            s.curve[i] = static_cast<double> (v.param (p));
            if (def != nullptr)
                def->curve[i] = static_cast<double> (uibind::info (p).def);
        }
        return true;
    }
    void pullGraph (IInstanceView& v, const synthyboy::LiveValues* live)
    {
        for (int gi = 0; gi < 2; ++gi)
        {
            ui::EnvGraph::State& st = (gi == 0 ? compactGraph_ : fullGraph_).state;
            st.selected = graphView_[gi].selected;   // the view state of the graph is the page's: the graph stores none of it
            st.locked = graphView_[gi].locked;
            st.zoom = graphView_[gi].zoom;
            ui::envgraph::Shape s, d;
            if (! shapeOf (v, sel_, beats_, s, &d))
                continue;
            st.shape = s;
            st.defaults = d;
            st.colour = envColour (sel_ - 1);
            st.beats = beats_;
            st.ghost = false;
            if (sel_ != 1)
            {
                ui::envgraph::Shape gs;
                const int unitsIdx = uibind::indexOfKey ("env1.units");
                const bool gb = unitsIdx >= 0 && v.param (unitsIdx) >= 0.5f;   // ENV 1 in beats or in ms: its own switch
                if (shapeOf (v, 1, gb, gs, nullptr))
                {
                    if (gb != beats_)
                    {
                        // on the axis of the envelope in front: the times are converted with the tempo (a beat is 60000 / bpm ms)
                        const double bpm = std::max (1.0, v.bpm());
                        for (int i = 0; i < 5; ++i)
                        {
                            const double u = ui::envgraph::timeFromNorm (i, gs.t[i], gb);
                            gs.t[i] = ui::envgraph::normFromTime (i, gb ? u * 60000.0 / bpm : u * bpm / 60000.0, beats_);
                        }
                    }
                    st.ghost = true;
                    st.ghostShape = gs;
                }
            }
            // the value of every handle as text (with its unit)
            for (int i = 0; i < 5; ++i)
            {
                const int p = timeParam (i, beats_);
                if (p >= 0)
                    uibind::formatValue (uibind::info (p), v.param (p), st.text[i], sizeof st.text[i]);
            }
            if (param_[kSustain] >= 0 && timeParam (3, beats_) >= 0)
            {
                char t[16], u[24];
                uibind::formatValue (uibind::info (timeParam (3, beats_)), v.param (timeParam (3, beats_)), t, sizeof t);
                std::snprintf (u, sizeof u, "%s %.0f%%", t, static_cast<double> (v.param (param_[kSustain])));
                std::snprintf (st.text[ui::envgraph::Decay], sizeof st.text[ui::envgraph::Decay], "%.15s", u);
                std::snprintf (st.decayTime, sizeof st.decayTime, "%.15s", t);   // under the axis: the time under DECAY, the level under SUSTAIN
                std::snprintf (st.sustainText, sizeof st.sustainText, "%.0f%%", static_cast<double> (v.param (param_[kSustain])));
            }
            for (int i = 0; i < 3; ++i)
                if (param_[kACurve + i] >= 0)
                    std::snprintf (st.text[ui::envgraph::CurveA + i], sizeof st.text[0], "%+.1f", static_cast<double> (v.param (param_[kACurve + i])));
            // the dots: one per sounding voice (newest first); an engine without them gives the one the fine read-out has
            st.numDots = 0;
            if (live != nullptr)
            {
                const int e = sel_ - 1;
                for (int i = 0; i < synthyboy::LiveValues::kReadVoices && st.numDots < ui::EnvGraph::kMaxDots; ++i)
                {
                    const int stage = static_cast<int> (live->envVoiceStage[e][i] + 0.5f);
                    if (stage >= 1 && stage <= 6)
                        st.dots[st.numDots++] = { stage, quantise (live->envVoiceFraction[e][i]) };
                }
                if (st.numDots == 0)
                {
                    const int stage = static_cast<int> (live->envFineStage[e] + 0.5f);
                    if (stage >= 1 && stage <= 6)
                        st.dots[st.numDots++] = { stage, quantise (live->envFineFraction[e]) };
                }
            }
        }
    }
    static float quantise (float f) noexcept { return std::round (std::clamp (f, 0.0f, 1.0f) * 256.0f) / 256.0f; }

    // ---- what the envelope drives and which envelopes have targets
    void refreshMods (IInstanceView& v)
    {
        // what the envelope drives: the slots of the matrix with this source
        int slots[uibind::kMaxTargetSlots];
        numTargets_ = uibind::slotsOfSource (v, selectedSource_, slots, uibind::kMaxTargetSlots);
        for (int i = 0; i < 3; ++i)
            targetLines_[i][0] = '\0';
        numTargetLines_ = std::min (3, numTargets_);
        for (int i = 0; i < numTargetLines_; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[i]);
            char name[40];
            targetName (static_cast<size_t> (std::lround (v.param (sp.destination))), name, sizeof name);
            std::snprintf (targetLines_[i], sizeof targetLines_[i], "%.28s  %+.0f%%", name, static_cast<double> (v.param (sp.amount)));
        }
        uint8_t mask = 0;
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source < 0 || sp.destination < 0)
                continue;
            if (std::lround (v.param (sp.destination)) > 0)
            {
                const int src = static_cast<int> (std::lround (v.param (sp.source)));
                if (src >= 1 && src <= kEnvs)
                    mask |= static_cast<uint8_t> (1u << (src - 1));
            }
        }
        targetMask_ = mask;
        if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
            loadTargets (v, false);
        else if (pageTop_ == kPageMods && modsTargetParam_ >= 0)
            loadMods (v);
    }
    static void targetName (size_t destChoice, char* out, size_t cap)
    {
        const int p = synthyboy::modDestinationParamIndex (destChoice);
        std::snprintf (out, cap, "%s", p >= 0 ? uibind::info (p).name : "-");
    }

    // ======================================================================================================================================================================
    // the pages above the full screen
    void syncPages (const UiStack& stack, IInstanceView& inst)
    {
        const int top = stack.depth() >= 2 ? stack.top() : 0;
        const int sig = top * 16 + (top == kPageMods ? modsTarget_ & 0xff : 0);
        if (top == pageTop_ && sig == pageSig_)
            return;
        if (gesture_ != nullptr && pageTop_ != top)
            dropGesture();
        if (top == kPageList && pageTop_ != kPageList)
            underList_ = pageTop_;
        const bool fromTargetsList = pageTop_ == kPageList && underList_ == kPageTargets;
        pageTop_ = top;
        pageSig_ = sig;
        modsDirty_ = true;
        qSlots_.forget();
        switch (top)
        {
            case kPageTargets: if (! fromTargetsList) targetsPageIndex_ = 0; loadTargets (inst, true); break;
            case kPageMods: loadMods (inst); break;
            case kPageList: openGroupList(); break;
            default: activeGroup_ = 0; break;
        }
    }

    // ---- the targets page: the rows of the matrix whose source is this envelope
    ModRow targetRows_[ModRowsPage::kPerPage];
    int targetTotal_ = 0;
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        (void) fresh;
        constexpr int kPer = ModRowsPage::kPerPage;
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, selectedSource_, slots, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + kPer - 1) / kPer);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * kPer, n = std::clamp (total - first, 0, kPer);
        ModRowsPage::Entry e[kPer];
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[first + i]);
            targetName (static_cast<size_t> (std::lround (v.param (sp.destination))), e[i].name, sizeof e[i].name);
            targetRows_[i] = { slots[first + i], selectedSource_ };
        }
        for (int i = n; i < kPer; ++i)
            targetRows_[i] = {};
        targetTotal_ = total;
        char title[16];
        std::snprintf (title, sizeof title, "ENV %d", sel_);
        targets_.load (e, n, title, ui::theme::sourceColour ("ENV"), total, targetsPageIndex_);   // the colour of the source on the depth arcs of the dials
    }

    // ---- the list that picks a new target: the groups of the destinations, then the parameters of a group (the host list, as it is)
    using DestIndex = uibind::DestIndex;
    std::vector<const char*> listNames_;
    std::vector<std::string> listText_;
    int listGroup_ = -1;   // -1: the list shows the groups; else the group whose parameters it shows
    void openGroupList()
    {
        listGroup_ = -1;
        fillList();
    }
    void fillList()
    {
        const DestIndex& d = DestIndex::get();
        listNames_.clear();
        if (listGroup_ < 0)
        {
            for (const std::string& g : d.groups)
                listNames_.push_back (g.c_str());
            listHeader_ = nullptr;
            listHeaderBack_ = false;
        }
        else
        {
            for (int c : d.choices[static_cast<size_t> (listGroup_)])
                listNames_.push_back (uibind::info (synthyboy::modDestinationParamIndex (static_cast<size_t> (c))).name);
            listHeader_ = d.groups[static_cast<size_t> (listGroup_)].c_str();
            listHeaderBack_ = true;
        }
        listCells_.restart();
    }
    /** The add-target / add-source list is on the screen as the native picker cells (nativeui::modboard; the skin instance `Osc Native List`). */
    bool listShown() const noexcept { return pageTop_ == kPageList || (pageTop_ == kPageMods && pickingSource_); }
    void publishNativeList (IInstanceView& inst, UiStack& stack)
    {
        if (! native() || ! listShown())
        {
            listCells_.hide (inst);
            return;
        }
        NativeListCells::Data data;
        data.header = listHeader_;
        data.fallbackTitle = pickingSource_ ? "ADD SOURCE" : "SELECT TARGET";
        data.items = listNames_.data();
        data.count = static_cast<int> (listNames_.size());
        data.headerBack = listHeaderBack_;
        listCells_.publish (inst, data,
            [&] (int index) { handle (ui::Change::indexed (kList, index, 1.0), stack, inst); },
            [&] { if (pickingSource_) pickingSource_ = false; else handle (ui::Change::request (kListBackdrop), stack, inst); },
            [&] { if (! pickingSource_) handle (ui::Change::request (kListHeader), stack, inst); });
    }

    // ---- the modulations page of one control (a long press on a dial or a switch)
    ModRow modRows_[ModRowsPage::kPerPage];
    NativeModTargets nativeModulations_;
    int modsPageIndex_ = 0;
    uint64_t modsPreset_ = 0;
    int modsTarget_ = 0, modsTargetParam_ = -1;
    bool pickingSource_ = false;          // the add-source choice of the modulations page is open (the plugin list of sources)
    bool modsContextValid (const IInstanceView& inst) const noexcept
    {
        if (pageTop_ != kPageMods || modsTargetParam_ < 0 || modsPreset_ != inst.stateGeneration() || shownEnv (inst) != sel_)
            return false;
        const int units = param_[kSlotUnits];
        const bool beats = units >= 0 && inst.param (units) >= 0.5f;
        const int slot = slotOfId (modsTarget_);
        const int b = beatsSlotOf (slot);
        const int p = slot < 0 ? -1 : (beats && b >= 0 && param_[b] >= 0 ? param_[b] : param_[slot]);
        return p == modsTargetParam_;
    }

    void loadMods (const IInstanceView& v, int focusSlot = -1)
    {
        const int target = paramOfId (modsTarget_);
        char title[13] = "MODULATIONS";
        if (modsTargetParam_ != target)
        {
            modsPageIndex_ = 0;
            modsTargetParam_ = target;
        }
        const int slotId = slotOfId (modsTarget_);
        if (modsTargetParam_ >= 0 && slotId >= 0 && slotId < kNumDials)
            std::snprintf (title, sizeof title, "%.12s", labelOfSlot (slotId));
        const auto window = uibind::modulationWindow (v, modsTargetParam_, selectedSource_, modsPageIndex_, modRows_, focusSlot);
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
        if (modsTargetParam_ >= 0)
            uibind::formatValue (uibind::info (modsTargetParam_), v.param (modsTargetParam_), tv, sizeof tv);
        mods_.setTargetValue (tv);
    }

    /** The sources the add-source list offers: this envelope first, then the other envelopes' first and the LFOs. */
    void openSourceList()
    {
        int offer[8], n = 0;
        auto push = [&] (int s) { if (s == ui::kNoSource || n >= 8) return; for (int i = 0; i < n; ++i) if (offer[i] == s) return; offer[n++] = s; };
        push (selectedSource_);
        for (int s : { 7, 8, 1, 2, 9, 3, 19, 20 }) push (s);
        listNames_.clear();
        listText_.clear();
        listText_.reserve (8);
        for (int i = 0; i < n; ++i)
        {
            sourceIds_[i] = offer[i];
            listText_.push_back (uibind::sourceName (offer[i]));
        }
        for (const std::string& s : listText_)
            listNames_.push_back (s.c_str());
        sourceCount_ = n;
        listHeader_ = "ADD SOURCE";
        listHeaderBack_ = false;
        listCells_.restart();
        pickingSource_ = true;
    }
    int sourceIds_[8] = {}, sourceCount_ = 0;

    // ======================================================================================================================================================================
    // the touch: the graph, the list; the cells of the skin are the host's
    ui::Component* topComponent() noexcept
    {
        if (stackDepth_ == 0)
            return &compactGraph_;
        if (listShown())
            return nullptr;
        if (pageTop_ == 0)
            return &fullGraph_;
        return nullptr;
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& v)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        const int id = c.id;
        if (c.kind == K::Request)
        {
            switch (id)
            {
                case kModsBack:
                    if (pickingSource_)
                        pickingSource_ = false;
                    else
                        stack.back();
                    return;
                case kModsAdd:
                    if (modsContextValid (v))
                        openSourceList();
                    return;
                case kTargetsBack: case kListBackdrop: pickingSource_ = false; stack.back(); return;
                case kTargets: stack.openDetail (kPageTargets); return;
                case kTargetsAdd: stack.openDetail (kPageList); return;
                case kListHeader: listGroup_ = -1; fillList(); return;
                case kGraph: applyViewEdit ((stackDepth_ == 0 ? compactGraph_ : fullGraph_).edit()); return;   // the graph asks: a handle was touched, LOCK was tapped, the ruler was dragged
                default: break;
            }
            if (id >= kTargetsRow && id < kTargetsRow + 4 * ModRowsPage::kPerPage && (id - kTargetsRow) % 4 == 3)
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
            if (id >= kModsRow && id < kModsRow + 4 * ModRowsPage::kPerPage && (id - kModsRow) % 4 == 3)
            {
                const auto row = modRows_[(id - kModsRow) / 4];
                if (modsContextValid (v) && uibind::modulationMatches (v, modsTargetParam_, row))
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (row.slot);
                    uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
                    loadMods (v);
                }
            }
            return;
        }
        if (c.kind == K::ModsRequest)
        {
            modsPageIndex_ = 0;
            modsPreset_ = v.stateGeneration();
            pickingSource_ = false;
            modsTarget_ = id;
            stack.openDetail (kPageMods);
            return;
        }
        if (id == kList && c.kind == K::Index)
        {
            if (pickingSource_)
            {
                if (c.index >= 0 && c.index < sourceCount_)
                    addModulation (v, sourceIds_[c.index]);
                pickingSource_ = false;
                return;
            }
            if (listGroup_ < 0)
            {
                listGroup_ = c.index;
                fillList();
            }
            else
            {
                const std::vector<int>& ch = DestIndex::get().choices[static_cast<size_t> (listGroup_)];
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
        if (id == kModsPage && c.kind == K::Index && modsContextValid (v))
        {
            modsPageIndex_ = c.index;
            loadMods (v);
            return;
        }
        // ---- the native cells of the targets page
        if (id >= kTargetsRow && id < kTargetsRow + 4 * ModRowsPage::kPerPage)
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
            return;
        }
        // ---- the native cells of the modulations page of a control
        if (id >= kModsRow && id < kModsRow + 4 * ModRowsPage::kPerPage)
        {
            const auto row = modRows_[(id - kModsRow) / 4];
            if (! modsContextValid (v) || ! uibind::modulationMatches (v, modsTargetParam_, row))
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (row.slot);
            switch ((id - kModsRow) % 4)
            {
                case 0: if (c.kind == K::Value) uibind::writeNorm (v, sp.amount, c.value); break;
                case 1: if (c.kind == K::Index) uibind::writePlain (v, sp.bipolar, c.index != 0 ? 1.0 : 0.0); break;
                case 2: if (c.kind == K::Index) uibind::writePlain (v, sp.bypass, c.index != 0 ? 0.0 : 1.0); break;
                default: break;
            }
            modsDirty_ = true;
            return;
        }
        // ---- the graph: the handle that was moved
        if (id == kGraph)
            applyEdit ((stackDepth_ == 0 ? compactGraph_ : fullGraph_).edit(), v);
    }

    /** The graph asked for a change of its view state (a Request of its id): the page keeps it and gives it back with the next update (pullGraph). */
    void applyViewEdit (const ui::EnvGraph::Edit& e) noexcept
    {
        GraphView& gv = graphView_[stackDepth_ == 0 ? 0 : 1];
        switch (e.handle)
        {
            case ui::EnvGraph::kSelect: gv.selected = e.touched; break;
            case ui::EnvGraph::kLock:
                gv.locked = e.level >= 0.5;
                if (! gv.locked)
                    gv.zoom = e.t;   // unlocked starts from the window the picture stands for now: it does not jump
                break;
            case ui::EnvGraph::kZoom: gv.zoom = e.level; break;
            default: break;
        }
    }
    /** A handle of the graph was moved: the parameters it stands for. */
    void applyEdit (const ui::EnvGraph::Edit& e, IInstanceView& v)
    {
        using namespace ui::envgraph;
        if (e.handle >= Delay && e.handle < kNumHandles)
            graphView_[stackDepth_ == 0 ? 0 : 1].selected = e.handle;   // a handle that is dragged (or reset) is the selected one
        if (e.handle >= Delay && e.handle <= Release)
        {
            const int p = timeParam (e.handle == Release ? 4 : e.handle, beats_);
            if (p >= 0)
                uibind::writeNorm (v, p, e.t);
            if (e.handle == Decay && param_[kSustain] >= 0)
                uibind::writeNorm (v, param_[kSustain], e.level);
        }
        else if (e.handle >= CurveA && e.handle <= CurveR)
        {
            const int p = param_[kACurve + (e.handle - CurveA)];
            if (p >= 0)
                uibind::writePlain (v, p, e.curve);
        }
    }
    void addTarget (IInstanceView& v, int destChoice)
    {
        const int source = selectedSource_;
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
    void addModulation (IInstanceView& v, int source)
    {
        if (! modsContextValid (v))
            return;
        if (const int slot = uibind::ensureModulationSource (v, modsTargetParam_, source); slot >= 0)
            loadMods (v, slot);
    }

    // ======================================================================================================================================================================
    // the Q group: the four targets the hardware knobs are mapped to
    /** The groups of the page in reading order, published to the sixteen Q slots (q_publish.h): the compact view has ADSR; the full screen ADSR, SHAPE, TIME (the active one on Q 1 .. 4, the next on 5 .. 16); the
        targets and modulation pages one group per row. */
    void publishQGroup (IInstanceView& v)
    {
        if (otherOpen_)
            return;   // the Q group belongs to the page of another element
        const char* name = "";
        if (stackDepth_ == 0)
        {
            name = groupName (0);
            return;   // the main state: the Play view publishes the three groups of the page together (PlayView::publishQ)
        }
        else if (pageTop_ == kPageTargets || pageTop_ == kPageMods)
        {
            const bool tg = pageTop_ == kPageTargets;
            name = tg ? "Targets" : "Mods";
            const ModRow* rows = tg ? targetRows_ : modRows_;
            ModRowsPage& page = tg ? targets_ : mods_;
            const int n = page.numRows(), act = page.activeRow();
            qSlots_.publish (v, n, act, [&] (int row, int* t) { modRowTargets (row >= 0 && row < ModRowsPage::kPerPage ? rows[row].slot : -1, t); });
            setRowBlocks (page, ModRowsPage::kPerPage, n, act);
        }
        else if (pageTop_ == kPageList)
        {
            name = "List";
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else
        {
            name = groupName (activeGroup_);
            qSlots_.publish (v, kGroups, activeGroup_, [&] (int g, int* t)
            {
                for (int k = 0; k < 4; ++k)
                    t[k] = kQ.row (g, k) >= 0 ? paramOfSlot (kQ.row (g, k)) : -1;
            });
        }
        ui::setText (qName_, sizeof qName_, name);
    }

    // ======================================================================================================================================================================
    ui::EnvGraph compactGraph_, fullGraph_;
    CompactView compactView_;
    FullGeometry geo_;
    ModRowsPage targets_ { ModRowsPage::Kind::Targets };
    NativeModTargets nativeTargets_;
    ModRowsPage mods_ { ModRowsPage::Kind::Modulations };
    NativeListCells listCells_;
    const char* listHeader_ = nullptr;
    bool listHeaderBack_ = false;
    UiStack idle_;                       // an empty stack: the pages are read from it while another element's page is open
    ui::Component* gesture_ = nullptr;
    std::array<nativeui::FxCell, nativeui::env::kFields> nativeCells_ {};
    int nativeGestureField_ = -1, nativeGestureTarget_ = -1;
    bool nativeGestureBlocked_ = false;
    bool nativeHold_ = false;
    int nativeDownX_ = 0, nativeDownY_ = 0;
    double nativeDownMs_ = 0.0;
    bool page_ = false;                  // the page of the Env tab
    const IInstanceView* selectorInstance_ = nullptr;
    uint64_t selectorGeneration_ = 0;
    int pageEnv_ = 0;                    // the page of a tab: the envelope of the sub-tab (1 .. 6); 0 in the compact view of the Play page
    bool gestureDown_ = false, otherOpen_ = false, configured_ = false, modsDirty_ = true, beats_ = false, nativeGround_ = false;
    int w_ = 1280, h_ = 800, sel_ = 1, highlight_ = 1, stackDepth_ = 0, pageTop_ = 0, pageSig_ = 0, underList_ = 0, selectedSource_ = ui::kNoSource, numTargets_ = 0, targetsPageIndex_ = 0, activeGroup_ = 0, compactBlock_ = -1;
    int param_[kRowsPerEnv] = {};        // the engine parameter index of every row of the selected envelope (-1: none)
    uint8_t targetMask_ = 0;
    char targetLines_[3][40] = {};
    int numTargetLines_ = 0;
    struct GraphView { int selected = ui::envgraph::None; bool locked = true; double zoom = 0.0; };   // what the graph shows besides the envelope: the handle whose value is written, LOCKED, the zoom of the unlocked ruler
    GraphView graphView_[2];             // the compact graph, the full screen graph
    double modsAtMs_ = -1000.0;
    char qName_[16] = {};
    QSlots qSlots_;
};

}} // namespace synthyboy_mpc::overlay
