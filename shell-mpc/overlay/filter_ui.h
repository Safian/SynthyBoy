// SynthyBoy - MPC overlay: the FILTER UI, the page of the Filter tabs (the plugin draws the whole tab, page_view.h): the plugin-drawn pages built from the UI library (shell-mpc/ui/filter_display.h, panel.h, list.h)
// and wired to the real engine parameters (filterN.* of the table and filter.routing). One object per (page view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () /
// touch () / tick () / draw (), with the instance as IInstanceView. Docs: docs/MPC.md "Filter UI".
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementFilter, the pages above it are ours): the page (FilterFullScreen) is always open; detail pages above it: the OUTPUT popup (destination, bus
// sends, level, pan), the list of the models or of the styles (ListPopup), the modulations of one control (ModulationsPage, a long press on a knob).
// The display shows the response the filter really has: shell-mpc/overlay/curve_source.h runs the engine's own filter on an impulse (once per change, with the live, modulated cutoff and resonance of the voice that is
// heard); F1 + F2 shows both and the resultant by the routing. What it does with a touch: the library component gives a Change { id, kind, value }; the id says which control it was, the binding of the id (a key of the
// engine table) says which parameter; the write goes through IInstanceView::writeParam (normalised; the plugin tells the host). Every update () reads every parameter back, so host automation, presets and the host's
// own widgets show on the next frame.
// Q groups (one block each, q_router.h): MAIN CUTOFF RESO DRIVE MIX; TRACK KEYTRACK ENV 1 VAR COMB; OUT LEVEL PAN BLEND ROUTING; FORMANT X Y TRANSPOSE SPREAD (the Formant model). A tap on a member selects its block.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../q_router.h"
#include "../ui/choice_names.h"
#include "../ui/ui.h"
#include "curve_source.h"
#include "page_view.h"
#include "q_publish.h"
#include "touch_decoder.h"
#include "ui_bind.h"
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
    enum Page { kPageRoute = 20, kPageList = 21, kPageMods = 22 };

    // ---- control IDs
    enum Id : int
    {
        kClose = 1, kPower, kRoute, kInstances, kModel, kModelList, kStyle, kClean,
        kCutoff = 100, kResonance, kDrive, kMix,
        kRouting = 110, kBlend, kLevel, kPan,
        kKeytrack = 120, kEnv1, kVar, kCombOffset,
        kFormantX = 130, kFormantY, kFormantTrans, kFormantRes, kFormantSpread,
        kDisplayCutoff = 150, kDisplayFormant, kDisplayReso, kDisplayFormantY,   // the ids of the display's changes: the cutoff (x) and the resonance (y) of the finger, the formant X and Y
        kRouteDest = 200, kRouteBus1, kRouteBus2, kRouteLevel, kRoutePan, kRouteClose, kRouteBackdrop,
        kList = 300, kListBackdrop,
        kModsBack = 400, kModsRow = 410, kModsAddStep = 460, kModsAdd
    };

    using ModRow = uibind::ModRow;
    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kModelFormant = 5, kModelComb = 6;   // the models of the table (Analog, Dirty, Ladder, Digital, Diode, Formant, Comb ...)
    static constexpr double kModsRefreshMs = 100.0;

    FilterUi() { setCanvas (kCanvasW, kCanvasH); assignIds(); full_.setPageMode (true); }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        full_.place (tab);
        popupBox_ = { w / 2 - 260, kTabY + kTabH / 2 - 120, 520, 240 };
        route_.place (popupBox_);
        route_.setPopup (true, tab);
        route_.setBackdropId (kRouteBackdrop);
        route_.close.setId (kRouteClose);
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
        mods_.place (tab);
    }

    /** Reads the instance: the filter the page shows (F1, F2 or both), the real response curves, every value of the controls, the modulations of the knobs, the pages of the stack, the Q group.
        `in.instance` is the filter the tab names (Filter 1 / Filter 2); `in.targets` the live cutoff and resonance of both filters (cutoff 1, resonance 1, cutoff 2, resonance 2). */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        if (! started_)
        {
            started_ = true;
            sel_ = editFilter_ = std::clamp (in.instance, 1, 2);
        }
        full_.instances.state.index = sel_ - 1;   // the selector follows the filter the page shows (the Filter 2 tab opens on F2), not only a tap on it
        stackDepth_ = stack.depth();
        syncPages (stack);
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
        const bool hasTargets = in.targets != nullptr;
        curve1_->update (inst, hasTargets ? in.targets : nullptr, nowMs);
        curve2_->update (inst, hasTargets ? in.targets + 2 : nullptr, nowMs);
        if (sel_ == 3)
            computeCombinedCurve (inst);
        updateDisplayData (inst, in.live);
        pullControls (inst);
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        if (pageTop_ == kPageList && listParam_ >= 0)
        {
            if (listParam_ == paramForId (kStyle, editFilter_))
            {
                const int mIdx = paramForId (kModel, editFilter_);
                const int model = mIdx >= 0 ? static_cast<int> (std::lround (inst.param (mIdx))) : 0;
                const int count = synthyboy::filters::filterModelStyleCount (model);
                list_.state.items = ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
                list_.state.count = count;
                list_.state.current = std::clamp (static_cast<int> (std::lround (inst.param (listParam_))), 0, std::max (0, count - 1));
                placeList (true);
            }
            else
                list_.state.current = static_cast<int> (std::lround (inst.param (listParam_)));
        }
        inst.setUiFullOpen (true);
        publishQGroup (inst);
    }

    int selection() const noexcept { return sel_; }          // 1: F1, 2: F2, 3: F1+F2
    int editFilter() const noexcept { return editFilter_; }  // 1 or 2: the filter the knobs belong to
    bool isDual() const noexcept { return sel_ == 3; }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    int topPage() const noexcept { return pageTop_; }
    ui::Rect displayRect() const noexcept { return full_.displayRect(); }

    /** The whole picture: the opaque ground, the page (or the modulations page above it), the popup. */
    void draw (gpu::SceneBuilder& sb) const
    {
        page::ground (sb);
        if (pageTop_ == kPageMods)
        {
            mods_.draw (sb);
            return;
        }
        full_.draw (sb);
        if (pageTop_ == kPageRoute)
            route_.draw (sb);
        else if (pageTop_ == kPageList)
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
            handlePending (stack, inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
        {
            handle (gesture_->touchMove (x, y), stack, inst);
            handlePending (stack, inst);
        }
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

    void turnQ (int slot, double delta, UiStack& stack, IInstanceView& inst)
    {
        if (pageTop_ == kPageMods)
            handle (mods_.turn (delta), stack, inst);
        else
            handle (full_.turnQ (slot, delta), stack, inst);
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
    void dropGesture() noexcept { if (gesture_ != nullptr) gesture_->touchUp (-1, -1); gesture_ = nullptr; gestureDown_ = false; }   // (a finger that is gone: the display stops drawing the dragged values)
    /** Another element's page was open: the Q group is published again at the next update. */
    void forgetQGroup() noexcept { qSlots_.forget(); }

    ui::FilterFullScreen& full() noexcept { return full_; }
    ui::FilterRoutePanel& routePanel() noexcept { return route_; }
    ui::ListPopup& list() noexcept { return list_; }
    ui::ModulationsPage& modsPage() noexcept { return mods_; }
    const CurveSource* curve (int filter) const noexcept { return filter == 2 ? curve2_.get() : curve1_.get(); }
    int paramOf (int id, int filter) const noexcept { return paramForId (id, filter); }
    const ModRow* modRows() const noexcept { return modRows_; }

private:
    static const synthyboy::ParamInfo& info (int idx) noexcept { return uibind::info (idx); }

    void assignIds()
    {
        full_.close.setId (kClose);
        full_.power.setId (kPower);
        full_.instances.setId (kInstances);
        full_.model.setId (kModel);
        full_.modelList.setId (kModelList);
        full_.style.setId (kStyle);
        full_.clean.setId (kClean);
        full_.route.setId (kRoute);
        full_.display.assignIds (kDisplayCutoff, kDisplayReso, kDisplayFormant, kDisplayFormantY);

        full_.cutoff.setId (kCutoff);
        full_.resonance.setId (kResonance);
        full_.drive.setId (kDrive);
        full_.mix.setId (kMix);

        full_.keytrack.setId (kKeytrack);
        full_.env1.setId (kEnv1);
        full_.var.setId (kVar);
        full_.combOffset.setId (kCombOffset);

        full_.level.setId (kLevel);
        full_.pan.setId (kPan);
        full_.blend.setId (kBlend);
        full_.routing.setId (kRouting);

        full_.formantX.setId (kFormantX);
        full_.formantY.setId (kFormantY);
        full_.formantTrans.setId (kFormantTrans);
        full_.formantRes.setId (kFormantRes);
        full_.formantSpread.setId (kFormantSpread);

        route_.destination.setId (kRouteDest);
        route_.bus1.setId (kRouteBus1);
        route_.bus2.setId (kRouteBus2);
        route_.level.setId (kRouteLevel);
        route_.pan.setId (kRoutePan);
        route_.close.setId (kRouteClose);

        mods_.back.setId (kModsBack);
        mods_.add.setId (kModsAdd);
    }

    int paramForId (int id, int filterNum) const noexcept
    {
        if (id == kRouting)
            return uibind::indexOfKey ("filter.routing");
        const char* s = nullptr;
        switch (id)
        {
            case kPower: s = "on"; break;
            case kModel: case kModelList: s = "model"; break;
            case kStyle: s = "style"; break;
            case kClean: s = "clean_mode"; break;
            case kCutoff: case kDisplayCutoff: s = "cutoff"; break;
            case kResonance: case kDisplayReso: s = "resonance"; break;
            case kDrive: s = "drive"; break;
            case kMix: s = "mix"; break;
            case kBlend: s = "blend"; break;
            case kLevel: case kRouteLevel: s = "level"; break;
            case kPan: case kRoutePan: s = "pan"; break;
            case kKeytrack: s = "keytrack"; break;
            case kEnv1: s = "env1"; break;
            case kVar: s = "var"; break;
            case kCombOffset: s = "comb_offset"; break;
            case kFormantX: case kDisplayFormant: s = "formant.x"; break;
            case kFormantY: case kDisplayFormantY: s = "formant.y"; break;
            case kFormantTrans: s = "formant.transpose"; break;
            case kFormantRes: s = "formant.resonance"; break;
            case kFormantSpread: s = "formant.spread"; break;
            case kRouteDest: s = "destination"; break;
            case kRouteBus1: s = "bus1_send"; break;
            case kRouteBus2: s = "bus2_send"; break;
            default: return -1;
        }
        return uibind::indexOfKey ("filter" + std::to_string (filterNum) + "." + s);
    }

    ui::Component* topComponent() noexcept
    {
        if (pageTop_ == kPageRoute) return &route_;
        if (pageTop_ == kPageList) return &list_;
        if (pageTop_ == kPageMods) return &mods_;
        return &full_;
    }

    void syncPages (const UiStack& stack)
    {
        const int top = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        if (top != pageTop_ && gesture_ != nullptr)
            dropGesture();
        pageTop_ = top;
    }

    /** The second change of a touch on the display (the finger's Y: resonance or formant Y). */
    void handlePending (UiStack& stack, IInstanceView& inst)
    {
        ui::Change extra;
        while (gesture_ == &full_ && full_.display.takePending (extra))
            handle (extra, stack, inst);
    }

    void handle (ui::Change c, UiStack& stack, IInstanceView& inst)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;

        // a long press on a control asks for its modulations
        if (c.kind == K::ModsRequest)
        {
            const int p = paramForId (c.id, editFilter_);
            if (p >= 0)
            {
                activeModParam_ = p;
                modsTitle_ = c.id;
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
        if (c.id >= kModsRow && c.id < kModsRow + 4 * ui::ModulationsPage::kMaxRows)
        {
            const int row = (c.id - kModsRow) / 4, part = (c.id - kModsRow) % 4;
            if (modRows_[row].slot >= 0)
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
        if (c.kind == K::Depth)
        {
            const int p = activeParamOfControl (c.id);
            const int slot = p >= 0 ? uibind::slotOf (inst, p, c.index) : -1;
            if (slot >= 0)
                uibind::writeNorm (inst, synthyboy::modSlotParams (slot).amount, (c.value + 1.0) * 0.5);
            modsDirty_ = true;
            return;
        }
        if (c.id == kModsAddStep && c.kind == K::Index)
        {
            const int i = std::clamp (c.index, 0, std::max (0, modsAddOffer_ - 1));
            mods_.addSource.state.index = i;
            mods_.addSource.setItem (uibind::sourceName (mods_.sourceIds[i]));
            return;
        }

        // navigation requests
        if (c.kind == K::Request)
        {
            switch (c.id)
            {
                case kRoute: stack.openDetail (kPageRoute); break;
                case kRouteClose: case kRouteBackdrop: case kModsBack: case kListBackdrop: stack.back(); break;
                case kModel: case kModelList: openList (inst, stack, kModel); break;
                case kStyle: openList (inst, stack, kStyle); break;
                default: break;
            }
            syncPages (stack);
            return;
        }

        // F1 | F2 | F1+F2
        if (c.id == kInstances)
        {
            sel_ = std::clamp (c.index + 1, 1, 3);
            if (sel_ <= 2)
                editFilter_ = sel_;
            full_.instances.state.index = sel_ - 1;
            qSlots_.forget();
            return;
        }

        // a choice of the list popup
        if (c.id == kList && c.kind == K::Index)
        {
            if (listParam_ >= 0)
            {
                uibind::writePlain (inst, listParam_, c.index);
                if (listParam_ == paramForId (kModel, editFilter_))
                {
                    const int newModel = c.index;
                    const int styleCount = synthyboy::filters::filterModelStyleCount (newModel);
                    const int sIdx = paramForId (kStyle, editFilter_);
                    if (sIdx >= 0 && styleCount > 0)
                    {
                        const int curStyle = static_cast<int> (std::lround (inst.param (sIdx)));
                        if (curStyle >= styleCount)
                            uibind::writePlain (inst, sIdx, styleCount - 1);
                    }
                    lastModel_[editFilter_] = newModel;
                }
            }
            stack.back();
            syncPages (stack);
            return;
        }

        // the finger on the display: its X and Y come as two changes (ui::FilterDisplay: the cutoff on the log axis and the resonance, or the formant X and Y). In the dual mode `index` is the filter whose handle the finger holds.
        if (c.id >= kDisplayCutoff && c.id <= kDisplayFormantY && c.kind == K::Value)
        {
            const bool cutoffOrReso = c.id == kDisplayCutoff || c.id == kDisplayReso;
            const int targetFilter = cutoffOrReso && sel_ == 3 && (c.index == 1 || c.index == 2) ? c.index : editFilter_;
            if (targetFilter != editFilter_)
                editFilter_ = targetFilter;   // the handle the finger holds is the filter being edited (the selector follows on the next update)
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

        const int p = paramForId (c.id, editFilter_);
        if (p < 0)
            return;
        if (c.kind == K::Value)
            uibind::writeNorm (inst, p, c.value);
        else if (c.kind == K::Index)
        {
            uibind::writePlain (inst, p, c.index);
            if (p == paramForId (kModel, editFilter_))
            {
                const int newModel = c.index;
                const int styleCount = synthyboy::filters::filterModelStyleCount (newModel);
                const int sIdx = paramForId (kStyle, editFilter_);
                if (sIdx >= 0 && styleCount > 0)
                {
                    const int curStyle = static_cast<int> (std::lround (inst.param (sIdx)));
                    if (curStyle >= styleCount)
                        uibind::writePlain (inst, sIdx, styleCount - 1);
                }
                lastModel_[editFilter_] = newModel;
            }
        }
    }

    /** The parameter of a control of the page (a knob, a stepper, a switch). */
    int activeParamOfControl (int id) const noexcept { return paramForId (id, editFilter_); }

    /** The box of the list popup: the standard one (600 px wide, nine rows) or, for the style list, the size of its content: as wide as the longest name needs (and at least 280 px), as tall as its rows, centred. */
    void placeList (bool fitContent)
    {
        if (! fitContent)
        {
            list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
            return;
        }
        int widest = 0;
        for (int i = 0; i < list_.state.count && list_.state.items != nullptr; ++i)
            if (list_.state.items[i] != nullptr)
                widest = std::max (widest, static_cast<int> (std::ceil (ui::Pen::textWidth (list_.state.items[i], ui::theme::L().fItem))));
        const int w = std::clamp (widest + 12 + 20 + 2 * ui::ListPopup::kPad, 280, 600), rows = std::clamp (list_.state.count, 1, 9);
        list_.place ({ (kCanvasW - w) / 2, kTabY + 20, w, rows * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
    }

    void openList (IInstanceView& inst, UiStack& stack, int id)
    {
        listParam_ = paramForId (id, editFilter_);
        if (listParam_ < 0)
            return;
        if (id == kStyle)
        {
            const int mIdx = paramForId (kModel, editFilter_);
            const int model = mIdx >= 0 ? static_cast<int> (std::lround (inst.param (mIdx))) : 0;
            const int count = synthyboy::filters::filterModelStyleCount (model);
            // One-style rule: a model with one style has no list popup
            if (count <= 1)
                return;
            list_.state.items = ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
            list_.state.count = count;
            list_.state.current = std::clamp (static_cast<int> (std::lround (inst.param (listParam_))), 0, std::max (0, count - 1));
            placeList (true);
            list_.showCurrent();
            stack.openDetail (kPageList);
            return;
        }
        const synthyboy::ParamInfo& pi = info (listParam_);
        list_.state.items = pi.choices;
        list_.state.count = pi.numChoices;
        list_.state.current = static_cast<int> (std::lround (inst.param (listParam_)));
        placeList (false);
        list_.showCurrent();
        stack.openDetail (kPageList);
    }

    // ---- the modulations page of one control
    void loadMods (const IInstanceView& v)
    {
        char title[13] = "MODULATIONS";
        const ui::Knob* knob = knobOf (modsTitle_);
        if (knob != nullptr)
            std::snprintf (title, sizeof title, "%.12s", knob->state.label);
        ui::ModInput mi;
        if (activeModParam_ >= 0)
            mi = uibind::modsOf (v, activeModParam_, selectedSource_, modRows_);
        else
            for (ModRow& r : modRows_) r = {};
        if (mi.numMods == 0)   // nothing modulates the control yet: the page is the board of every source (ui/mod_board.h), a tap on a chip makes the modulation
            uibind::fillSourceBoard (mods_.board);
        mods_.load (mi, title);
        mods_.back.setId (kModsBack);
        mods_.addSource.setId (kModsAddStep);
        mods_.add.setId (kModsAdd);
        for (int r = 0; r < ui::ModulationsPage::kMaxRows; ++r)
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
        if (source == ui::kNoSource || activeModParam_ < 0 || uibind::slotOf (v, activeModParam_, source) >= 0)
            return;
        const int slot = uibind::freeSlot (v);
        const size_t dest = synthyboy::modDestinationChoiceOf (static_cast<size_t> (activeModParam_));
        if (slot < 0 || dest == 0)
            return;
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        uibind::writePlain (v, sp.destination, static_cast<double> (dest));
        uibind::writePlain (v, sp.amount, 0.0);
        if (sp.bypass >= 0) uibind::writePlain (v, sp.bypass, 0.0);
        if (sp.bipolar >= 0) uibind::writePlain (v, sp.bipolar, info (activeModParam_).min < 0.0f && info (activeModParam_).max > 0.0f ? 1.0 : 0.0);
        uibind::writePlain (v, sp.source, source);
        loadMods (v);
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
        if (pageTop_ != kPageMods || activeModParam_ < 0)
            return;
        const ui::ModInput mi = uibind::modsOf (inst, activeModParam_, selectedSource_, modRows_);
        if (mi.numMods != mods_.numRows())
        {
            loadMods (inst);
            return;
        }
        for (int i = 0; i < mi.numMods; ++i)
        {
            ui::ModulationsPage::Row& r = mods_.rows[i];
            r.depth.state.value = r.depth.state.modValue = (mi.mods[i].depth + 1.0) * 0.5;
            ui::ModulationsPage::setRowState (r, mi.mods[i].bidirectional, mi.mods[i].bypassed);
        }
    }
    std::vector<ui::Knob*> knobsOfPage()
    {
        return { &full_.cutoff, &full_.resonance, &full_.drive, &full_.mix, &full_.keytrack, &full_.var, &full_.combOffset, &full_.level, &full_.pan, &full_.blend,
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
        d.state.dual = sel_ == 3;
        d.state.activeFilter = editFilter_;
        const int mIdx = paramForId (kModel, editFilter_);
        const int model = mIdx >= 0 ? static_cast<int> (std::lround (inst.param (mIdx))) : 0;
        d.state.isFormant = model == kModelFormant;
        d.state.curve1 = curve1_->curve();
        d.state.numPoints1 = curve1_->points();
        d.state.curve2 = curve2_->curve();
        d.state.numPoints2 = curve2_->points();
        if (sel_ == 3)
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
        if (pi.unit == synthyboy::Unit::percent && pi.min < 0.0f && (id == kPan || id == kRoutePan))
        {
            if (std::fabs (plain) < 1.0f)
                ui::setText (k.state.valueText, sizeof k.state.valueText, "C");
            else
                std::snprintf (k.state.valueText, sizeof k.state.valueText, plain < 0.0f ? "L %.0f%%" : "R %.0f%%", static_cast<double> (std::fabs (plain)));
        }
        else if (pi.numChoices > 0)   // a knob over a choice (ROUTING): the name of the choice
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
        pullStepper (inst, full_.model, kModel);

        const int sIdx = paramForId (kStyle, f);
        const int styleCount = synthyboy::filters::filterModelStyleCount (model);
        const char* const* styleNames = ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
        if (sIdx >= 0)
        {
            int curStyle = static_cast<int> (std::lround (inst.param (sIdx)));
            if (model != lastModel_[f])
            {
                lastModel_[f] = model;
                // when the model changes and current style index is above the new count,
                // clamp and write the parameter once through the same path a user change uses
                if (styleCount > 0 && curStyle >= styleCount)
                {
                    curStyle = styleCount - 1;
                    uibind::writePlain (inst, sIdx, curStyle);
                }
            }
            else if (styleCount > 0 && curStyle >= styleCount)
            {
                curStyle = styleCount - 1;
                uibind::writePlain (inst, sIdx, curStyle);
            }
            const int n = std::clamp (curStyle, 0, std::max (0, styleCount - 1));
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
                                 std::make_pair (&full_.combOffset, static_cast<int> (kCombOffset)), std::make_pair (&full_.level, static_cast<int> (kLevel)), std::make_pair (&full_.pan, static_cast<int> (kPan)),
                                 std::make_pair (&full_.blend, static_cast<int> (kBlend)), std::make_pair (&full_.formantX, static_cast<int> (kFormantX)), std::make_pair (&full_.formantY, static_cast<int> (kFormantY)),
                                 std::make_pair (&full_.formantTrans, static_cast<int> (kFormantTrans)), std::make_pair (&full_.formantRes, static_cast<int> (kFormantRes)),
                                 std::make_pair (&full_.formantSpread, static_cast<int> (kFormantSpread)) })
            pullKnob (inst, *kid.first, kid.second);
        full_.combOffset.setEnabled (model == kModelComb);   // the comb offset has a meaning in the Comb model only
        full_.var.setEnabled (! formant);
        full_.clean.setEnabled (model != kModelComb && ! formant);
        // OUTPUT: where the filter sends its signal (M: the effects, D: direct out, -: nowhere)
        const int dstIdx = paramForId (kRouteDest, f);
        if (dstIdx >= 0)
        {
            const int dst = static_cast<int> (std::lround (inst.param (dstIdx)));
            const synthyboy::ParamInfo& pi = info (dstIdx);
            route_.destination.setOptions (pi.choices, pi.numChoices);
            route_.destination.state.index = std::clamp (dst, 0, std::max (0, route_.destination.state.count - 1));
            full_.route.setTarget (dst == 1 ? "D" : (dst == 2 ? "-" : "M"));
            full_.route.state.segment[0] = full_.route.state.segment[1] = dst == 0;
        }
        pullKnob (inst, route_.bus1, kRouteBus1);
        pullKnob (inst, route_.bus2, kRouteBus2);
        pullKnob (inst, route_.level, kRouteLevel);
        pullKnob (inst, route_.pan, kRoutePan);
    }

    /** The groups of the page in reading order: the filter screen has MAIN, TRACK, OUT (and FORMANT with the Formant model); the output panel has one, the modulation list one per row. The active group is on Q 1 .. 4, the
        next three (wrapping) on 5 .. 16. */
    void publishQGroup (IInstanceView& v)
    {
        const char* name = "";
        if (pageTop_ == kPageRoute)
        {
            name = "Output";
            qSlots_.publish (v, 1, 0, [this] (int, int* t)
            {
                t[0] = paramForId (kRouteDest, editFilter_);
                t[1] = paramForId (kRouteBus1, editFilter_);
                t[2] = paramForId (kRouteBus2, editFilter_);
                t[3] = paramForId (kRouteLevel, editFilter_);
            });
        }
        else if (pageTop_ == kPageMods)
        {
            name = "Mods";
            qSlots_.publish (v, mods_.numRows(), mods_.activeRow(), [this] (int row, int* t) { modRowTargets (row < ui::ModulationsPage::kMaxRows ? modRows_[row].slot : -1, t); });
            setRowBlocks (mods_, ui::ModulationsPage::kMaxRows, mods_.numRows(), mods_.activeRow());
        }
        else if (pageTop_ == 0)
        {
            const int ag = full_.activeGroup();
            name = ui::FilterFullScreen::groupName (ag);
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
                    t[i] = m != nullptr ? paramOfMember (m) : -1;
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
    int stackDepth_ = 0, pageTop_ = 0;
    int sel_ = 1, editFilter_ = 1; // 1: F1, 2: F2, 3: F1+F2
    bool started_ = false;

    double sampleRate_ = 48000.0;
    std::unique_ptr<CurveSource> curve1_, curve2_;
    std::vector<float> curveResult_;

    ui::FilterFullScreen full_;
    ui::FilterRoutePanel route_;
    ui::ListPopup list_;
    ui::ModulationsPage mods_;

    ui::Rect popupBox_;
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    int activeModParam_ = -1, modsTitle_ = 0, modsAddOffer_ = 0;
    bool modsDirty_ = true;
    double modsAtMs_ = -1000.0;
    ModRow modRows_[ui::ModulationsPage::kMaxRows];

    int envSel_ = 1, lfoSel_ = 1, selectedSource_ = ui::kNoSource;
    int listParam_ = -1;
    int lastModel_[3] = { -1, -1, -1 };

    QSlots qSlots_;
    char qName_[20] = {};
};

}} // namespace synthyboy_mpc::overlay
