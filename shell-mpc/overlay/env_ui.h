// SynthyBoy - MPC overlay: the envelope UI of the Play page, the plugin-drawn pages built from the UI library (shell-mpc/ui/env_*.h, mod_targets.h) and wired to the real engine parameters (envN.* of the table).
// One object per (Play view, plugin instance), like OscUi (osc_ui.h). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw*, with the instance as IInstanceView.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementEnv, the pages above it are ours): main = the compact view in the middle third of the Play page (EnvCompact); full = the full screen
// (EnvFullScreen) over the whole plugin area of the Play page; detail pages above it: the targets of the envelope (ModTargetsPage, the modulation matrix seen from the source), the list that picks a new target
// (groups, then the parameters of a group), the modulations of one control (ModulationsPage, a long press on a knob).
// What it does with a touch: the library component gives a Change { id, kind, value }; the id says which control it was, the binding of the id (a key of the engine table and a type) says which parameter and how
// the UI value converts; the write goes through IInstanceView::writeParam (normalised; the plugin tells the host). The graph's handles give an Edit (EnvGraph::edit) that is written the same way. Every update ()
// reads every parameter back, so host automation, presets and the host's own widgets show on the next frame.
// Units: the time knobs, the number fields and the graph follow `envN.units`: milliseconds (envN.attack ..) or beats (envN.attack_beats ..); the sustain and the three curves have one parameter each.
// Live: the running dots come from LiveValues::envVoiceStage / envVoiceFraction (one per sounding voice, the newest first); an engine that fills only envFineStage / envFineFraction gives the one dot.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/env_screen.h"
#include "../ui/list.h"
#include "../ui/mod_targets.h"
#include "../native_ui_params.h"
#include "../ui/native_apertures.h"
#include "../q_router.h"
#include "q_publish.h"
#include "q_publish_mod.h"
#include "instance_view.h"
#include "native_marker_covers.h"
#include "touch_decoder.h"
#include "page_view.h"
#include "ui_bind.h"
#include "native_mod_targets_ui.h"
#include "modulation_window.h"
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

    // ---- the id spaces of the controls (a Change comes out with the id the owner gave)
    enum Id : int
    {
        kClose = 1, kFullButton, kInstances, kGraph, kTargets, kCompactInstances,   // kInstances: the selector of the full screen / tab (Index = the envelope number); kCompactInstances: the carousel of the compact view (Index = the position)
        kKnob = 100,                        // 100 + Knob: the knobs (the compact view and the full screen share the ids)
        kBeats = 130, kLegato,
        kTargetsBack = 150, kTargetsPage, kTargetsAdd, kTargetsRow = 160,    // 160 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kModsBack = 600, kModsRow = 610, kModsAddStep = 660, kModsAdd, kModsPage,
        kList = 700, kListBackdrop, kListHeader
    };
    enum KnobId : int { kAttack = 0, kDecay, kSustain, kRelease, kACurve, kDCurve, kRCurve, kDelay, kHold, kNumKnobs };

    /** What a value is and how it converts to the engine's plain value. */
    enum class T : uint8_t { Knob, Toggle, Select, Pick, Graph, Inert };   // Select: the selector of the full screen (a UsedSelector, filled by pullSelector); Pick: the stepper with a list of the compact view

    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        T type = T::Inert;
        int param = -1;        // the table index (milliseconds / the only one)
        int paramBeats = -1;   // the table index in beats (-1: the value has no beats version)
        int slot = -1;         // Knob: kAttack ..
        int active (bool beats) const noexcept { return beats && paramBeats >= 0 ? paramBeats : param; }
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kCompactX = 440, kCompactW = 400;       // the middle third of the Play page
    static constexpr double kModsRefreshMs = 100.0;
    static constexpr int kVirtualSelector = 0;

    EnvUi() { setCanvas (kCanvasW, kCanvasH); compact_.setNativeInstanceSelector (false); compact_.setNativeDials(); configure (1); }

    void setNativeADSR (bool on) { full_.setNativeADSR (on); }
    bool nativeADSR() const noexcept { return full_.nativeADSR(); }
    bool nativeShape() const noexcept { return full_.nativeShape(); }
    bool nativeTime() const noexcept { return full_.nativeTime(); }
    bool nativeInstanceSelector() const noexcept { return full_.nativeInstanceSelector(); }
    bool nativeTargets() const noexcept { return full_.nativeTargets(); }

    void setHostBackground (bool on) noexcept { nativeGround_ = on; }
    bool hostBackground() const noexcept
    {
        return nativeGround_ && page_ && stackDepth_ == 1 && pageTop_ == 0
            && w_ == page::kW && h_ == page::kH && &ui::theme::L() == &ui::theme::kLook1;
    }

    void drawPageGround (gpu::SceneBuilder&) const noexcept {}
    bool isNativeControl (int x, int y) const noexcept { return full_.isNativeControl (x, y); }

    static constexpr int kEnvs = 6;

    // ---- standalone native instance choice and navigation helpers
    static const char* const* instanceNames() noexcept
    {
        static const char* const n[kEnvs] = { "ENV 1", "ENV 2", "ENV 3", "ENV 4", "ENV 5", "ENV 6" };
        return n;
    }
    static const char* instanceName (int sel) noexcept
    {
        return sel >= 1 && sel <= kEnvs ? instanceNames()[sel - 1] : "";
    }
    static std::string choiceLabel (const IInstanceView& inst, int sel)
    {
        const char* name = instanceName (sel);
        if (! name || name[0] == '\0')
            return "";
        (void) inst;
        return std::string (name);
    }
    std::string currentChoiceLabel (const IInstanceView& inst) const
    {
        return choiceLabel (inst, sel_);
    }
    void setSelection (IInstanceView& inst, int sel)
    {
        if (sel >= 1 && sel <= kEnvs)
            uibind::chooseEnv (inst, sel);
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        compact_.place ({ kCompactX, kTabY + 4, kCompactW, kTabH - 8 });
        full_.place (tab);
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
        list_.setHeaderId (kListHeader);
        targets_.place (tab);
        mods_.place (tab);
    }

    /** Reads the instance: the selected envelope, every value of the visible controls, the modulations of the knobs, the pages of the stack, the dots of the voices, the Q group. */
    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs, const synthyboy::LiveValues* live)
    {
        highlight_ = highlight;
        readSelectorQ (inst, stack);
        const uint64_t generation = inst.stateGeneration();
        if (selectorInstance_ != &inst || selectorGeneration_ != generation)
        {
            selectorInstance_ = &inst;
            selectorGeneration_ = generation;
            modsDirty_ = true;
        }
        const int sel = uibind::selectedEnv (inst);   // ONE selection for the Env tab and the Play page (Play Env, chunk saved): the tab and the Play page show the same envelope
        if (sel != sel_ || ! configured_)
            configure (sel);
        selectedSource_ = uibind::envSource (sel_);
        const int unitsIdx = uibind::indexOfKey (prefix_ + "units");
        beats_ = unitsIdx >= 0 && inst.param (unitsIdx) >= 0.5f;
        const bool open = stack.depth() > 0 && stack.element() == kElementEnv;
        if (open && stack.top() == kPageMods)
        {
            const auto* control = item (modsTarget_);
            const int parameter = control != nullptr ? control->active (beats_) : -1;
            const uint64_t preset = inst.stateGeneration();
            if (modsPreset_ != preset || modsTargetParam_ != parameter)
            {
                dropGesture();
                modsPreset_ = preset;
                modsDirty_ = true;
                mods_.setPickingSource (false);
            }
        }
        if (open && stackDepth_ == 0)
            full_.setActiveGroup (0);
        stackDepth_ = open ? stack.depth() : 0;
        if (stackDepth_ > 0 && instPopup_)
            closeInstanceList();
        otherOpen_ = stack.depth() > 0 && ! open;
        syncPages (open ? stack : idle_, inst);
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        pullSelector (inst);   // (before pull: the carousel of the compact view takes the cells of THIS selector)
        pull (inst, items_);
        pullGraph (inst, live);
        if (open)
            inst.setUiFullOpen (true);
        else if (stack.isMain())
            inst.setUiFullOpen (false);
        publishQGroup (inst);
    }

    /** The page of the Env tab (page_view.h): the full screen of the selected envelope (`Play Env`, shared with the Play page), nothing to close; the selector ("used + one free") chooses another one. */
    void update (IInstanceView& inst, UiStack& stack, double nowMs, const page::Input& in)
    {
        if (! page_)
        {
            page_ = true;
            full_.setPageMode (true);
        }
        if (selectorInstance_ != &inst || selectorGeneration_ != inst.stateGeneration())
        {
            inst.setNativeEnvEditor (false, nullptr);
            nativeGestureBlocked_ = nativeGestureField_ >= 0;
            gesture_ = nullptr;
            gestureDown_ = false;
        }
        else if (stack.depth() == 1 && stack.element() == kElementEnv && uibind::selectedEnv (inst) == sel_)
        {
            const auto& targets = nativeCells_[nativeui::env::Targets];
            float action = 0.0f;
            if (targets.target >= 0 && inst.takeNativeEnvField (nativeui::env::Targets, targets, action) && action >= 0.5f)
                stack.openDetail (kPageTargets);   // an opening detail owns the frame before the virtual selector is read
        }
        update (inst, 2, stack, nowMs, in.live);
        syncNativeTargets (inst, stack, false);
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
        drawPopup (sb);
    }

    bool isMain() const noexcept { return stackDepth_ == 0; }
    int selection() const noexcept { return sel_; }
    bool beats() const noexcept { return beats_; }
    int topPage() const noexcept { return pageTop_; }
    int activeQ() const noexcept { return full_.activeGroup(); }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }

    // ---- the drawing (a picture is one scene of cached cells, then the live parts, then the popups)
    /** The opaque ground and the compact view (main state). */
    void drawCompact (gpu::SceneBuilder& sb, bool nativeBackground = false) const
    {
        const ui::theme::Look& L = ui::theme::L();
        auto ground = [&] (int x0, int y0, int x1, int y1)
        {
            if (nativeBackground)
                return;   // Native Play background also owns the gaps between controls.
            if (x1 <= x0 || y1 <= y0)
                return;
            ui::Cell cell (sb, { x0, y0, x1 - x0, y1 - y0 }, 0);
            sb.gradientRect (static_cast<float> (x0), static_cast<float> (y0), static_cast<float> (x1), static_cast<float> (y1), ui::theme::rgba (L.bg0), ui::theme::rgba (L.bg1));
        };
        ground (kGroundX0, kTabY, kGroundX1, kTabY + 10);
        const ui::Rect atk = compact_.attack.rect();
        const ui::Rect dec = compact_.decay.rect();
        const ui::Rect sus = compact_.sustain.rect();
        const ui::Rect rel = compact_.release.rect();
        ground (kGroundX0, kTabY + 66, kGroundX1, atk.y);
        ground (kGroundX0, atk.y, atk.x, atk.bottom());
        ground (atk.right(), atk.y, dec.x, atk.bottom());
        ground (dec.right(), atk.y, kGroundX1, atk.bottom());
        ground (kGroundX0, atk.bottom(), kGroundX1, sus.y);
        ground (kGroundX0, sus.y, sus.x, sus.bottom());
        ground (sus.right(), sus.y, rel.x, sus.bottom());
        ground (rel.right(), sus.y, kGroundX1, sus.bottom());
        ground (kGroundX0, sus.bottom(), kGroundX1, kTabY + kTabH);
        compact_.draw (sb);
        if (instPopup_)
            list_.draw (sb);   // the list of the selector lies over the third (drawLive paints no dots then)
    }
    /** The opaque ground of the whole tab and the page of the stack (the full screen, the targets, the modulations of a control). */
    void drawPage (gpu::SceneBuilder& sb) const
    {
        if (hostBackground())
        {
            drawNativeMarkerCovers (sb, "env1");
            full_.drawPage (sb, ui::EnvFullScreen::Ground::Host);
            return;
        }
        if (pageTop_ == 0 && (page_ || nativePlayEditor_))
        {
            full_.drawNativeGround (sb);
            full_.drawPage (sb, ui::EnvFullScreen::Ground::Apertures);
            return;
        }
        page::ground (sb);
        if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
            targets_.draw (sb);
        else if (pageTop_ == kPageMods)
            mods_.draw (sb);
        else
            full_.draw (sb);
    }
    void drawPopup (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ > 0 && pageTop_ == kPageList)
            list_.draw (sb);
    }
    /** The running dots (the graph that is on screen). */
    void drawLive (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0)
        {
            if (! instPopup_)
                compact_.drawLive (sb);
        }
        else if (pageTop_ == 0)
            full_.drawLive (sb);
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
        drawPopup (sb);
        return page::sceneHash (scratch) ^ static_cast<uint32_t> (stackDepth_ * 7 + pageTop_);
    }
    /** The touch hit this UI's drawing (the main state: the middle third; any other state: the whole tab). */
    bool covers (int x, int y) const noexcept
    {
        if ((pageTop_ == kPageTargets && targets_.nativeHit (x, y))
            || (pageTop_ == kPageMods && mods_.nativeHit (x, y)))
            return false;
        if (stackDepth_ > 0)
        {
            if (pageTop_ == 0 && full_.isNativeControl (x, y))
                return false;
            return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y);
        }
        if (! ui::Rect { kCompactX, kTabY + 4, kCompactW, kTabH - 8 }.contains (x, y))
            return false;
        if (compact_.attack.rect().contains (x, y) || compact_.decay.rect().contains (x, y)
            || compact_.sustain.rect().contains (x, y) || compact_.release.rect().contains (x, y))
            return false;
        return true;
    }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** One touch event in UI pixels (the whole screen), `stack` is the instance's stack (the caller holds its lock). true: the touch belongs to the plugin-drawn pages. */
    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        ui::Component* top = topComponent();
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
            if ((page_ || nativePlayEditor_) && stackDepth_ == 1 && pageTop_ == 0)
            {
                nativeGestureField_ = nativeFieldAt (x, y);
                nativeGestureTarget_ = nativeGestureField_ >= 0 ? nativeCells_[nativeGestureField_].target : -1;
                nativeGestureBlocked_ = false;
                nativeHold_ = nativeGestureField_ >= 0 && nativeGestureField_ < kNumKnobs;
                nativeDownX_ = x;
                nativeDownY_ = y;
                nativeDownMs_ = nowMs;
            }
            if (! covers (x, y) || ! top)
            {
                if (pageTop_ == 0 && stackDepth_ > 0 && ! ui::QGroup::wideDevice())
                {
                    const int group = full_.isNativeADSR (x, y) ? 0
                                    : full_.isNativeShape (x, y) ? 1
                                    : full_.isNativeTime (x, y) ? 2 : -1;
                    if (group >= 0)
                    {
                        full_.setActiveGroup (group);
                        publishQGroup (inst);
                    }
                }
                return false;
            }
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
        if (nativeHold_ && ! nativeGestureBlocked_ && nowMs - nativeDownMs_ >= ui::Knob::kLongPressMs)
        {
            const int field = nativeGestureField_;
            nativeHold_ = false;
            handle (ui::Change::modsRequest (kNativeScalarIds[field]), stack, inst);
            syncPages (stack, inst);
            publishNativeSelector (inst);
            publishNativeEditor (inst);
        }
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst);
    }
    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the control of the page under the pointer takes it as steps of its own (a knob: one Q-Link step, a stepper, a list, a scrolled
        list; ui::Component::wheelOver). false: nothing of this page lies there, or a finger is down on it. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        ui::Component* top = topComponent();
        if (gestureDown_)
            return false;
        if (pageTop_ == 0 && stackDepth_ > 0 && full_.isNativeSelector (x, y))
        {
            handle (full_.selector.turn (notches), stack, inst);
            return true;
        }
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
    /** The page went off the screen: the open list of the compact view closes. */
    void closePopups() { if (instPopup_) closeInstanceList(); }

    // ---- access for the tests
    ui::EnvCompact& compact() noexcept { return compact_; }
    const ui::EnvCompact& compact() const noexcept { return compact_; }
    /** The handle of the graph shown as selected (the view state of the graph is this page's: the next update gives it to the graph; the full screen graph, or the compact one in the main state). */
    void selectGraphHandle (int handle) noexcept { graphView_[stackDepth_ == 0 ? 0 : 1].selected = handle; }
    ui::EnvFullScreen& full() noexcept { return full_; }
    void syncNativePlayEditor (IInstanceView& inst, UiStack& stack, int highlight, double nowMs,
                               const synthyboy::LiveValues* live, bool enable)
    {
        const bool active = enable && stack.depth() == 1 && stack.element() == kElementEnv && pageTop_ == 0;
        if (! active)
        {
            hideNativePlayEditor (inst);
            return;
        }
        const auto preset = inst.stateGeneration();
        if (nativePlayEditor_ && preset != nativePlayPreset_)
        {
            inst.setNativeEnvEditor (false, nullptr);
            nativeGestureBlocked_ = nativeGestureField_ >= 0;
        }
        nativePlayPreset_ = preset;
        nativePlayEditor_ = true;
        full_.setNativePlayEditor (true);
        publishNativeSelector (inst);
        publishNativeEditor (inst);
        if (consumeNativeEditor (inst, stack))
        {
            update (inst, highlight, stack, nowMs, live);
            if (stack.depth() != 1)
                hideNativePlayEditor (inst);
            else
            {
                publishNativeSelector (inst);
                publishNativeEditor (inst);
            }
        }
    }

    void hideNativePlayEditor (IInstanceView& inst) noexcept
    {
        if (! nativePlayEditor_)
            return;
        nativePlayEditor_ = false;
        full_.setNativePlayEditor (false);
        inst.setNativeEnvEditor (false, nullptr);
        const int frames[nativeui::kEnvSelectorCells] = {};
        inst.setNativeEnvSelector (frames);
        dropGesture();
    }

    void syncNativeTargets (IInstanceView& inst, UiStack& stack, bool fromPlay)
    {
        using namespace nativeui::targets;
        const auto before = stack.changes();
        nativeTargets_.update (inst, targets_, pageTop_ == kPageTargets, fromPlay ? PlayEnv : Env,
                               selectedSource_, targetRows_, [&] (ui::Change change) { handle (change, stack, inst); });
        const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (modsTargetParam_));
        nativeModulations_.update (inst, mods_, ! mods_.boardShown() && modsContextValid (inst),
            fromPlay ? PlayEnv : Env, static_cast<int> (destination), modRows_,
            [&] (ui::Change change) { handle (change, stack, inst); }, Modulations);
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
    }
    ui::ModTargetsPage& targetsPage() noexcept { return targets_; }
    ui::ModulationsPage& modsPage() noexcept { return mods_; }
    ui::ListPopup& list() noexcept { return list_; }
    const std::vector<Item>& items() const noexcept { return items_; }
    const Item* item (int id) const noexcept { for (const Item& it : items_) if (it.id == id) return &it; return nullptr; }
    int timeParam (int stage, bool beats) const noexcept { return stage >= 0 && stage < 5 ? (beats && timeBeats_[stage] >= 0 ? timeBeats_[stage] : timeMs_[stage]) : -1; }
    int sustainParam() const noexcept { return sustainP_; }
    int curveParam (int i) const noexcept { return i >= 0 && i < 3 ? curveP_[i] : -1; }
    /** The envelope that has a table index the transition of which the UI shows now (the number of the target rows etc.). */
    int numTargets() const noexcept { return numTargets_; }
    uint8_t targetMask() const noexcept { return compact_.instances.targetMask(); }
    /** The selector of the full screen (tests). */
    const ui::UsedSelector& selector() const noexcept { return full_.selector; }

private:
    using ModRow = uibind::ModRow;

    static constexpr int kNativeScalarIds[] {
        kKnob + kAttack, kKnob + kDecay, kKnob + kSustain, kKnob + kRelease,
        kKnob + kACurve, kKnob + kDCurve, kKnob + kRCurve, kKnob + kDelay, kKnob + kHold, kBeats, kLegato,
    };
    static_assert (std::size (kNativeScalarIds) == nativeui::env::Selection);

    int nativeFieldAt (int x, int y) const noexcept
    {
        if (full_.isNativeSelector (x, y))
            return nativeui::env::Selection;
        if (full_.isNativeTargets (x, y))
            return nativeui::env::Targets;
        for (int field = 0; field < nativeui::env::Selection; ++field)
            if (const auto* it = item (kNativeScalarIds[field]); it && full_.isNativeComponent (it->c) && it->c->rect().contains (x, y))
                return field;
        return -1;
    }

    void publishNativeEditor (IInstanceView& inst) noexcept
    {
        namespace n = nativeui::env;
        if (! page_ && ! nativePlayEditor_)
            return;
        const bool visible = stackDepth_ == 1 && pageTop_ == 0;
        nativeCells_.fill ({});
        if (visible)
        {
            for (int field = 0; field < n::Selection; ++field)
                if (const auto* it = item (kNativeScalarIds[field]); it && full_.isNativeComponent (it->c))
                {
                    const int target = it->active (beats_);
                    const int kind = it->type == T::Toggle ? nativeui::kFxToggle : nativeui::kFxDial;
                    nativeCells_[field] = { target, target, kind };
                }
            // Actions identify the selected envelope via its sustain index.
            // They never write that engine parameter.
            if (full_.nativeInstanceSelector())
                nativeCells_[n::Selection] = { sustainP_, sustainP_, nativeui::kFxToggle };
            if (full_.nativeTargets())
                nativeCells_[n::Targets] = { sustainP_, sustainP_, nativeui::kFxToggle };
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
        if (take (n::Selection, value))
        {
            const int choice = 1 + static_cast<int> (std::lround (value * (n::kInstances - 1)));
            for (int pos = 0; pos < playCells_.n; ++pos)
                if (uibind::cellChoice (full_.selector, playCells_, pos) == choice && choice != sel_)
                {
                    uibind::chooseEnv (inst, choice);
                    return true;
                }
        }
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
    static constexpr int kGroundX0 = 434, kGroundX1 = 848;
    // ======================================================================================================================================================================
    // building: the selected envelope decides the parameters behind the ids
    void bind (int id, ui::Component* c, T type, const std::string& suffix, const std::string& suffixBeats = std::string(), int slot = -1)
    {
        Item it;
        it.id = id; it.c = c; it.type = type; it.slot = slot;
        it.param = suffix.empty() ? -1 : uibind::indexOfKey (prefix_ + suffix);
        it.paramBeats = suffixBeats.empty() ? -1 : uibind::indexOfKey (prefix_ + suffixBeats);
        if (c != nullptr)
            c->setId (id);
        items_.push_back (it);
    }
    void configure (int sel)
    {
        sel_ = sel;
        configured_ = true;
        prefix_ = "env" + std::to_string (sel) + ".";
        items_.clear();
        static const char* const kTime[5] = { "delay", "attack", "hold", "decay", "release" };
        for (int i = 0; i < 5; ++i)
        {
            timeMs_[i] = uibind::indexOfKey (prefix_ + kTime[i]);
            timeBeats_[i] = uibind::indexOfKey (prefix_ + kTime[i] + "_beats");
        }
        sustainP_ = uibind::indexOfKey (prefix_ + "sustain");
        static const char* const kCurve[3] = { "attack_curve", "decay_curve", "release_curve" };
        for (int i = 0; i < 3; ++i)
            curveP_[i] = uibind::indexOfKey (prefix_ + kCurve[i]);
        bind (kClose, &full_.close, T::Inert, "");
        bind (kFullButton, &compact_.full, T::Inert, "");
        bind (kTargets, &full_.targets, T::Inert, "");
        bind (kCompactInstances, &compact_.instances, T::Pick, "");
        bind (kInstances, &full_.selector, T::Select, "");
        bind (kGraph, &compact_.graph, T::Graph, "");
        bind (kGraph, &full_.graph, T::Graph, "");
        struct K { int knob; const char* key; const char* beats; };
        const K ks[kNumKnobs] = { { kAttack, "attack", "attack_beats" }, { kDecay, "decay", "decay_beats" }, { kSustain, "sustain", "" }, { kRelease, "release", "release_beats" },
                                  { kACurve, "attack_curve", "" }, { kDCurve, "decay_curve", "" }, { kRCurve, "release_curve", "" }, { kDelay, "delay", "delay_beats" }, { kHold, "hold", "hold_beats" } };
        ui::Knob* fk[kNumKnobs] = { &full_.attack, &full_.decay, &full_.sustain, &full_.release, &full_.aCurve, &full_.dCurve, &full_.rCurve, &full_.delay, &full_.hold };
        ui::Knob* ck[kNumKnobs] = { &compact_.attack, &compact_.decay, &compact_.sustain, &compact_.release, nullptr, nullptr, nullptr, nullptr, nullptr };
        for (const K& k : ks)
        {
            bind (kKnob + k.knob, fk[k.knob], T::Knob, k.key, k.beats, k.knob);
            if (ck[k.knob] != nullptr)
                bind (kKnob + k.knob, ck[k.knob], T::Knob, k.key, k.beats, k.knob);
        }
        bind (kBeats, &full_.beats, T::Toggle, "units");
        bind (kLegato, &full_.legato, T::Toggle, "legato_inverted");
        pageTop_ = 0;
        underList_ = 0;
        modsDirty_ = true;
        qSlots_.clear();
        qSlots_.forget();
        gesture_ = nullptr; gestureDown_ = false;
        compact_.group.setActive (highlight_ == 2);
        full_.setActiveGroup (0);
    }

    // ======================================================================================================================================================================
    // reading: the states of the components from the parameters
    void pull (IInstanceView& v, std::vector<Item>& list)
    {
        for (Item& it : list)
        {
            if (it.c == nullptr)
                continue;
            if (it.type == T::Pick)
            {
                // the carousel of the compact view steps over the SAME cells as the tab's selector (used + the selected one + one "+": uibind::cellsFromSelector of the UsedSelector pullSelector filled), not over all six
                auto* st = static_cast<ui::EnvCarousel*> (it.c);
                st->state.count = playCells_.n; st->state.index = playCells_.selectedPos; st->state.wrap = true;
                st->state.names = playCells_.names;
                st->setItem (playCells_.n > 0 ? playCells_.names[playCells_.selectedPos] : kEnvNames[std::clamp (sel_, 1, kEnvs) - 1]);
                st->state.dotMask = cellDots_; st->state.dotColours = cellColours_;
                // Navigation is independent of the selected envelope's controls: a previous
                // page-level disable must never leave the Play selector dead or grey.
                it.c->setEnabled (true);
                it.c->setInactive (false);
                continue;
            }
            if (it.type == T::Select)
            {
                // The full-screen selector is navigation-only as well; it does not own a
                // parameter and must remain usable even when the selected envelope is empty.
                it.c->setEnabled (true);
                it.c->setInactive (false);
                continue;   // the selector: pullSelector
            }
            const int p = it.active (beats_);
            if (it.type == T::Inert || it.type == T::Graph)
            {
                if (it.id == kTargets)
                {
                    char t[8];
                    std::snprintf (t, sizeof t, "%d", numTargets_);
                    full_.targets.setValue (t);
                    it.c->setEnabled (true);
                    it.c->setInactive (false);
                }
                continue;
            }
            if (p < 0)
            {
                it.c->setEnabled (false);
                continue;
            }
            if (full_.isNativeComponent (it.c))
            {
                it.c->setEnabled (false);
                it.c->setInactive (true);
            }
            else
            {
                it.c->setEnabled (true);
                it.c->setInactive (false);
            }
            const float plain = v.param (p);
            const synthyboy::ParamInfo& pi = uibind::info (p);
            switch (it.type)
            {
                case T::Knob:
                {
                    auto* k = static_cast<ui::Knob*> (it.c);
                    k->state.value = k->state.modValue = uibind::normOf (p, plain);
                    k->state.defaultValue = uibind::normOf (p, pi.def);
                    k->state.bipolar = pi.min < 0.0f && pi.max > 0.0f;
                    valueText (pi, plain, k->state.valueText, sizeof k->state.valueText);
                    break;
                }
                case T::Toggle:
                    static_cast<ui::ToggleIcon*> (it.c)->state.on = plain >= 0.5f;
                    break;
                default: break;
            }
        }
    }
    /** The selector: ENV 1 .. 6, the used ones, the selected one and one free (`+`); the scan is the one of the last refreshMods (never per frame). */
    void pullSelector (IInstanceView& v)
    {
        uibind::fillSelector (uibind::SelKind::Env, scan_, v, full_.selector);
        full_.syncSelectorLayout();
        uibind::cellsFromSelector (uibind::SelKind::Env, full_.selector, playCells_);   // the Play page's carousel: the same list
        cellDots_ = 0;
        for (int k = 0; k < playCells_.n; ++k)
        {
            const int id = playCells_.id[k];
            cellColours_[k] = id > 0 ? ui::envColour (id - 1) : 0u;
            if (id > 0 && (compact_.instances.targetMask() & (1u << (id - 1))) != 0)
                cellDots_ |= 1u << k;   // the dot of an envelope that has targets
        }
        if (page_ && full_.nativeInstanceSelector())
            publishNativeSelector (v);
        if (page_ && stackDepth_ == 1 && pageTop_ == 0)
        {
            const int last = std::max (0, playCells_.n - 1);
            if (selectorQValue_ < 0.0f || std::lround (selectorQValue_ * last) != playCells_.selectedPos)
                selectorQValue_ = last > 0 ? static_cast<float> (playCells_.selectedPos) / last : 0.0f;
            v.setQVirtual (kVirtualSelector, selectorQValue_);
            selectorQOwner_ = &v;
        }
    }

    /** Q represents the previous published row, including its single free candidate. */
    void readSelectorQ (IInstanceView& v, const UiStack& stack) noexcept
    {
        if (! page_)
            return;
        const bool visible = stack.depth() == 1 && stack.element() == kElementEnv;
        const bool owned = selectorQOwner_ == &v;
        const bool same = owned && selectorGeneration_ == v.stateGeneration();
        float value;
        if ((visible || owned) && v.takeQVirtual (kVirtualSelector, value) && visible && same && playCells_.n > 0)
        {
            selectorQValue_ = std::clamp (value, 0.0f, 1.0f);
            const int pos = static_cast<int> (std::lround (selectorQValue_ * (playCells_.n - 1)));
            const int id = uibind::cellChoice (full_.selector, playCells_, pos);
            if (id > 0 && id != uibind::selectedEnv (v))
                uibind::chooseEnv (v, id);
        }
        // The id is shared with other pages; never apply a queued write when ownership changes.
        if (! visible || ! same)
        {
            selectorQOwner_ = nullptr;
            selectorQValue_ = -1.0f;
        }
    }

    /** The skin gates a native button on each candidate's five contiguous frames.
        Frame 0 is blank, then each candidate has plain, used, selected,
        selected-and-used and '+' frames in that order. */
    void publishNativeSelector (IInstanceView& v) const noexcept
    {
        constexpr int kVariants = 5;
        static_assert (nativeui::kEnvSelectorCells * kVariants == nativeui::kEnvSelectorLastFrame);
        int frames[nativeui::kEnvSelectorCells] = {};
        if (stackDepth_ == 1 && pageTop_ == 0)
        {
            const ui::UsedSelector& selector = full_.selector;
            int cells[nativeui::kEnvSelectorCells];
            const int n = selector.cells (cells, nativeui::kEnvSelectorCells);
            for (int slot = 0; slot < n; ++slot)
            {
                const int choice = uibind::cellChoice (selector, playCells_, slot);
                if (choice <= 0)
                    continue;
                const int index = cells[slot];
                const int variant = index == ui::UsedSelector::kPlus ? 4
                    : (selector.state.selected == choice ? 2 : 0) + (selector.state.items[index].used ? 1 : 0);
                frames[slot] = 1 + (choice - 1) * kVariants + variant;
            }
        }
        v.setNativeEnvSelector (frames);
    }
    uibind::SelectorCells playCells_;                  // the cells of the compact view's carousel and list (pullSelector)
    unsigned cellDots_ = 0;
    uint32_t cellColours_[uibind::SelectorCells::kMax] = {};
    /** A tap on cell `pos` of the compact view's carousel or list: select its envelope (the `+` cell: the lowest free one). View state only. */
    void chooseCell (IInstanceView& v, int pos) { const int id = uibind::cellChoice (full_.selector, playCells_, pos); if (id > 0) uibind::chooseEnv (v, id); }
public:
    const uibind::SelectorCells& playCells() const noexcept { return playCells_; }
    const ui::UsedSelector& tabSelector() const noexcept { return full_.selector; }
    /** The Env tab was left (page_view_base.h): a selected envelope that is neither used nor edited (a `+` pick the user did not touch) falls back to the last used one (uibind::fallbackSelection). */
    void leftTab (IInstanceView& v)
    {
        hideNativeTargets (v);
        selectorInstance_ = nullptr;
        if (selectorQOwner_ == &v)
        {
            float pending;
            v.takeQVirtual (kVirtualSelector, pending);
            selectorQOwner_ = nullptr;
            selectorQValue_ = -1.0f;
        }
        if (page_ && full_.nativeInstanceSelector())
        {
            const int frames[nativeui::kEnvSelectorCells] = {};
            v.setNativeEnvSelector (frames);
        }
        if (page_)
            v.setNativeEnvEditor (false, nullptr);
        uibind::leaveSelector (uibind::SelKind::Env, v);
    }
private:
    /** The text under a knob: the value with its unit. */
    static void valueText (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
    {
        if (pi.unit == synthyboy::Unit::none)
            std::snprintf (out, cap, "%+.1f", static_cast<double> (plain));
        else
            uibind::formatValue (pi, plain, out, cap);
    }
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
            ui::EnvGraph::State& st = (gi == 0 ? compact_.graph : full_.graph).state;
            st.selected = graphView_[gi].selected;   // the view state of the graph is the page's: the graph stores none of it
            st.locked = graphView_[gi].locked;
            st.zoom = graphView_[gi].zoom;
            ui::envgraph::Shape s, d;
            if (! shapeOf (v, sel_, beats_, s, &d))
                continue;
            st.shape = s;
            st.defaults = d;
            st.colour = ui::envColour (sel_ - 1);
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
            if (sustainP_ >= 0 && timeParam (3, beats_) >= 0)
            {
                char t[16], u[24];
                uibind::formatValue (uibind::info (timeParam (3, beats_)), v.param (timeParam (3, beats_)), t, sizeof t);
                std::snprintf (u, sizeof u, "%s %.0f%%", t, static_cast<double> (v.param (sustainP_)));
                std::snprintf (st.text[ui::envgraph::Decay], sizeof st.text[ui::envgraph::Decay], "%.15s", u);
                std::snprintf (st.decayTime, sizeof st.decayTime, "%.15s", t);   // under the axis: the time under DECAY, the level under SUSTAIN
                std::snprintf (st.sustainText, sizeof st.sustainText, "%.0f%%", static_cast<double> (v.param (sustainP_)));
            }
            for (int i = 0; i < 3; ++i)
                if (curveP_[i] >= 0)
                    std::snprintf (st.text[ui::envgraph::CurveA + i], sizeof st.text[0], "%+.1f", static_cast<double> (v.param (curveP_[i])));
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

    // ---- the modulations of the controls (the arcs of the knobs, the chips of the fields) and of the envelope itself
    void refreshMods (IInstanceView& v)
    {
        scan_ = uibind::scanUsed (v);   // which envelopes the selector shows
        for (Item& it : items_)
        {
            const int p = it.active (beats_);
            if (it.c == nullptr || p < 0 || it.type != T::Knob)
                continue;
            static_cast<ui::ModInput&> (static_cast<ui::Knob*> (it.c)->state) = uibind::modsOf (v, p, selectedSource_, nullptr);
        }
        // what the envelope drives: the slots of the matrix with this source
        int slots[uibind::kMaxTargetSlots];
        numTargets_ = uibind::slotsOfSource (v, selectedSource_, slots, uibind::kMaxTargetSlots);
        for (int i = 0; i < 3; ++i)
            full_.targetLines[i].text[0] = '\0';
        full_.setNumTargets (numTargets_);
        full_.numTargetLines = std::min (3, numTargets_);
        for (int i = 0; i < full_.numTargetLines; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[i]);
            char name[40];
            targetName (static_cast<size_t> (std::lround (v.param (sp.destination))), name, sizeof name);
            std::snprintf (full_.targetLines[i].text, sizeof full_.targetLines[i].text, "%.28s  %+.0f%%", name, static_cast<double> (v.param (sp.amount)));
        }
        uint8_t targetMask = 0;
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source < 0 || sp.destination < 0)
                continue;
            if (std::lround (v.param (sp.destination)) > 0)
            {
                const int src = static_cast<int> (std::lround (v.param (sp.source)));
                if (src >= 1 && src <= kEnvs)
                    targetMask |= static_cast<uint8_t> (1u << (src - 1));
            }
        }
        compact_.instances.setTargetMask (targetMask);
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
            default: full_.setActiveGroup (0); break;
        }
    }

    // ---- the targets page: the rows of the matrix whose source is this envelope
    ModRow targetRows_[ui::ModTargetsPage::kPerPage];
    int targetTotal_ = 0;
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, selectedSource_, slots, uibind::kMaxTargetSlots);
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
            targetRows_[i] = { slots[first + i], selectedSource_ };
        }
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
        char title[16];
        std::snprintf (title, sizeof title, "ENV %d", sel_);
        targets_.load (e, n, title, ui::theme::sourceColour ("ENV"), total, targetsPageIndex_);   // the colour of the source on the depth arcs of the knobs
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
    }

    // ---- the list that picks a new target: the groups of the destinations, then the parameters of a group
    using DestIndex = uibind::DestIndex;
    std::vector<const char*> listNames_;
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

    // ---- the list of the selector of the compact view: ENV 1 .. 6, a popup over the third (not a page of the stack), a dot on the rows of the envelopes that have targets
    static constexpr const char* kEnvNames[kEnvs] = { "ENV 1", "ENV 2", "ENV 3", "ENV 4", "ENV 5", "ENV 6" };
    void openInstanceList()
    {
        const ui::Rect third { kCompactX, kTabY + 4, kCompactW, kTabH - 8 };
        list_.state.header = nullptr;
        list_.state.headerBack = false;
        list_.state.items = playCells_.names;   // the same cells as the carousel
        list_.state.count = playCells_.n;
        list_.state.current = playCells_.selectedPos;
        list_.state.dotMask = cellDots_;
        list_.state.dotColours = cellColours_;
        list_.place ({ third.x + 6, third.y + 6 + ui::playrows::kSelectorH, third.w - 12, playCells_.n * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (third);
        list_.showCurrent();
        instPopup_ = true;
    }
    void closeInstanceList()
    {
        instPopup_ = false;
        list_.state.dotMask = 0;
        list_.state.dotColours = nullptr;
        list_.state.current = -1;
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup ({ 0, kTabY, w_, kTabH });
    }

    // ---- the modulations page of one control (a long press on a knob or a field)
    ModRow modRows_[ui::ModulationsPage::kPerPage];
    NativeModTargets nativeModulations_;
    int modsPageIndex_ = 0;
    uint64_t modsPreset_ = 0;
    int modsTarget_ = 0, modsTargetParam_ = -1, modsAddOffer_ = 0;
    bool modsContextValid (const IInstanceView& inst) const noexcept
    {
        if (pageTop_ != kPageMods || modsTargetParam_ < 0 || modsPreset_ != inst.stateGeneration() || uibind::selectedEnv (inst) != sel_)
            return false;
        const auto* control = item (modsTarget_);
        const int units = uibind::indexOfKey (prefix_ + "units");
        return control != nullptr && control->active (units >= 0 && inst.param (units) >= 0.5f) == modsTargetParam_;
    }

    void loadMods (const IInstanceView& v, int focusSlot = -1)
    {
        const Item* t = item (modsTarget_);
        char title[13] = "MODULATIONS";
        const int target = t != nullptr ? t->active (beats_) : -1;
        if (modsTargetParam_ != target)
        {
            modsPageIndex_ = 0;
            modsTargetParam_ = target;
        }
        if (t != nullptr && modsTargetParam_ >= 0)
        {
            if (t->c != nullptr && t->type == T::Knob)
                std::snprintf (title, sizeof title, "%.12s", static_cast<ui::Knob*> (t->c)->state.label);
        }
        const auto window = uibind::modulationWindow (v, modsTargetParam_, selectedSource_, modsPageIndex_, modRows_, focusSlot);
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
        // what "add source" offers: this envelope first, then the other envelopes' first and the LFOs
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
        if (modsTargetParam_ >= 0)
            uibind::formatValue (uibind::info (modsTargetParam_), v.param (modsTargetParam_), tv, sizeof tv);
        mods_.setTargetValue (tv);
    }

    // ======================================================================================================================================================================
    // writing: a Change of a component to a parameter
    ui::Component* topComponent() noexcept
    {
        if (stackDepth_ == 0)
            return instPopup_ ? static_cast<ui::Component*> (&list_) : &compact_;
        if (pageTop_ == kPageTargets)
            return &targets_;
        if (pageTop_ == kPageMods)
            return &mods_;
        if (pageTop_ == kPageList)
            return &list_;
        return &full_;
    }
    Item* find (int id) noexcept
    {
        for (Item& it : items_)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& v)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        const int id = c.id;
        if (instPopup_ && stackDepth_ == 0)   // the list of the selector of the compact view
        {
            if (id == kList && c.kind == K::Index)
            {
                chooseCell (v, c.index);
                closeInstanceList();
            }
            else if (id == kListBackdrop && c.kind == K::Request)
                closeInstanceList();
            return;
        }
        if (c.kind == K::Request)
        {
            switch (id)
            {
                case kCompactInstances: openInstanceList(); return;
                case kFullButton: stack.openFull (kElementEnv); return;
                case kModsBack:
                    if (mods_.pickingSource())
                        mods_.setPickingSource (false);
                    else
                        stack.back();
                    return;
                case kModsAdd:
                    if (modsContextValid (v))
                    {
                        uibind::fillSourceBoard (mods_.board);
                        mods_.setPickingSource (true);
                    }
                    return;
                case kClose: case kTargetsBack: case kListBackdrop: stack.back(); return;
                case kTargets: stack.openDetail (kPageTargets); return;
                case kTargetsAdd: stack.openDetail (kPageList); return;
                case kListHeader: listGroup_ = -1; fillList(); return;
                case kGraph: applyViewEdit ((stackDepth_ == 0 ? compact_.graph : full_.graph).edit()); return;   // the graph asks: a handle was touched, LOCK was tapped, the ruler was dragged
                default: break;
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
            if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kPerPage && (id - kModsRow) % 4 == 3)
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
            mods_.setPickingSource (false);
            modsTarget_ = id;
            stack.openDetail (kPageMods);
            return;
        }
        if (c.kind == K::AddRequest)
        {
            addModulation (v, c.index);
            return;
        }
        if (id == kList && c.kind == K::Index)
        {
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
        if (id == kInstances && c.kind == K::Index)
        {
            uibind::chooseEnv (v, c.index);   // view state only (Play Env, shared with the Play page): no parameter is written; the selector's own Index is the envelope number
            return;
        }
        if (id == kCompactInstances && c.kind == K::Index)
        {
            chooseCell (v, c.index);   // the carousel of the compact view steps (Index = the position among the cells)
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
        if (id == kModsAddStep && c.kind == K::Index)
        {
            const int i = std::clamp (c.index, 0, std::max (0, modsAddOffer_ - 1));
            mods_.addSource.state.index = i;
            mods_.addSource.setItem (uibind::sourceName (mods_.sourceIds[i]));
            return;
        }
        // ---- the rows of the targets page
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
            return;
        }
        // ---- the rows of the modulations page of a control
        if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kPerPage)
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
        {
            applyEdit ((stackDepth_ == 0 ? compact_.graph : full_.graph).edit(), v);
            return;
        }
        Item* it = find (id);
        if (it == nullptr || it->c == nullptr)
            return;
        const int p = it->active (beats_);
        if (p < 0)
            return;
        if (c.kind == K::Depth)
        {
            const int slot = uibind::slotOf (v, p, c.index);
            if (slot >= 0)
                uibind::writeNorm (v, synthyboy::modSlotParams (slot).amount, (c.value + 1.0) * 0.5);
            modsDirty_ = true;
            return;
        }
        switch (it->type)
        {
            case T::Knob: if (c.kind == K::Value) uibind::writeNorm (v, p, c.value); break;
            case T::Toggle: if (c.kind == K::Index) uibind::writePlain (v, p, c.index != 0 ? 1.0 : 0.0); break;
            default: break;
        }
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
            if (e.handle == Decay && sustainP_ >= 0)
                uibind::writeNorm (v, sustainP_, e.level);
        }
        else if (e.handle >= CurveA && e.handle <= CurveR)
        {
            const int p = curveP_[e.handle - CurveA];
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
        constexpr int rows = ui::ModTargetsPage::kPerPage;
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
        {
            mods_.setPickingSource (false);
            loadMods (v, slot);
        }
    }

    // ======================================================================================================================================================================
    // the Q group: the four targets the hardware knobs are mapped to
    /** The four targets of the ADSR group of the compact view and its block of knobs (the Play view maps the groups of the three thirds together). */
public:   // the Play view reads the targets of the compact group and sets its block
    void compactTargets (int* t) const noexcept { for (int i = 0; i < 4; ++i) t[i] = paramOfMember (compact_.group.member (i)); }
    void setCompactBlock (int block) noexcept { compact_.group.setBlock (block); }
private:
    int paramOfMember (const ui::Component* m) const noexcept
    {
        while (m != nullptr)
        {
            for (const Item& it : items_)
                if (it.c == m && (it.type == T::Knob || it.type == T::Toggle))
                    return it.active (beats_);
            m = m->primaryChild();
        }
        return -1;
    }
    /** The groups of the page in reading order, published to the sixteen Q slots (q_publish.h): the compact view has ADSR; the full screen ADSR, SHAPE, TIME (the active one on Q 1 .. 4, the next on 5 .. 16); the
        targets and modulation pages one group per row. */
    void publishQGroup (IInstanceView& v)
    {
        if (otherOpen_)
            return;   // the Q group belongs to the page of another element
        const char* name = "";
        auto members = [this] (const ui::QGroup& g, int* t) { for (int i = 0; i < 4; ++i) t[i] = paramOfMember (g.member (i)); };
        if (stackDepth_ == 0)
        {
            name = "ADSR";
            compact_.group.setActive (highlight_ == 2);
            return;   // the main state: the Play view publishes the three groups of the page together (PlayView::publishQ)
        }
        else if (pageTop_ == kPageTargets || pageTop_ == kPageMods)
        {
            const bool tg = pageTop_ == kPageTargets;
            name = tg ? "Targets" : "Mods";
            const ModRow* rows = tg ? targetRows_ : modRows_;
            const int n = tg ? targets_.numRows() : mods_.numRows(), act = tg ? targets_.activeRow() : mods_.activeRow();
            qSlots_.publish (v, n, act, [&] (int row, int* t) { modRowTargets (row >= 0 && row < ui::ModulationsPage::kPerPage ? rows[row].slot : -1, t); });
            if (tg)
                setRowBlocks (targets_, ui::ModTargetsPage::kPerPage, n, act);
            else
                setRowBlocks (mods_, ui::ModulationsPage::kPerPage, n, act);
        }
        else if (pageTop_ == kPageList)
        {
            name = "List";
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else
        {
            name = ui::EnvFullScreen::groupName (full_.activeGroup());
            int ids[ui::EnvFullScreen::kMaxGroups], pos = -1;
            const int used = usedGroups (full_.groups, ui::EnvFullScreen::kMaxGroups, full_.activeGroup(), ids, pos);
            qSlots_.publish (v, used, pos, [&] (int k, int* t)
            {
                members (full_.groups[ids[k]], t);
                if (page_ && ids[k] == 1 && t[3] < 0)
                    t[3] = QRouter::encode (kVirtualSelector, QRouter::kVirtual);   // spare slot in SHAPE
            });
            setBlocks (full_.groups, ui::EnvFullScreen::kMaxGroups, ids, used, pos);
        }
        ui::setText (qName_, sizeof qName_, name);
    }

    // ======================================================================================================================================================================
    ui::EnvCompact compact_;
    ui::EnvFullScreen full_;
    ui::ModTargetsPage targets_;
    NativeModTargets nativeTargets_;
    ui::ModulationsPage mods_;
    ui::ListPopup list_;
    UiStack idle_;                       // an empty stack: the pages are read from it while another element's page is open
    std::vector<Item> items_;
    ui::Component* gesture_ = nullptr;
    std::array<nativeui::FxCell, nativeui::env::kFields> nativeCells_ {};
    int nativeGestureField_ = -1, nativeGestureTarget_ = -1;
    bool nativeGestureBlocked_ = false;
    bool nativeHold_ = false;
    int nativeDownX_ = 0, nativeDownY_ = 0;
    double nativeDownMs_ = 0.0;
    bool instPopup_ = false;             // the list of the selector of the compact view is open (main state only)
    bool nativePlayEditor_ = false;
    uint64_t nativePlayPreset_ = 0;
    bool page_ = false;                  // the page of the Env tab
    uibind::UsedScan scan_;              // the last scan of the matrix and the parameters (selector candidates)
    const IInstanceView* selectorInstance_ = nullptr;
    uint64_t selectorGeneration_ = 0;
    const IInstanceView* selectorQOwner_ = nullptr;
    float selectorQValue_ = -1.0f;
    bool gestureDown_ = false, otherOpen_ = false, configured_ = false, modsDirty_ = true, beats_ = false, nativeGround_ = false;
    int w_ = 1280, h_ = 800, sel_ = 1, highlight_ = 1, stackDepth_ = 0, pageTop_ = 0, pageSig_ = 0, underList_ = 0, selectedSource_ = ui::kNoSource, numTargets_ = 0, targetsPageIndex_ = 0;
    int timeMs_[5] = { -1, -1, -1, -1, -1 }, timeBeats_[5] = { -1, -1, -1, -1, -1 }, sustainP_ = -1, curveP_[3] = { -1, -1, -1 };
    struct GraphView { int selected = ui::envgraph::None; bool locked = true; double zoom = 0.0; };   // what the graph shows besides the envelope: the handle whose value is written, LOCKED, the zoom of the unlocked ruler
    GraphView graphView_[2];             // the compact graph, the full screen graph
    double modsAtMs_ = -1000.0;
    std::string prefix_ = "env1.";
    char qName_[16] = {};
    QSlots qSlots_;
};

}} // namespace synthyboy_mpc::overlay
