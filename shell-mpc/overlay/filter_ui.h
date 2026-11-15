// SynthyBoy - MPC overlay: the FILTER UI of the Play page, the plugin-drawn pages built from the UI library
// (shell-mpc/ui/) and wired to the real engine parameters (docs/ui-elem-filter.md).
// One object per (Play view, plugin instance). Pure: no OS headers, no threads of its own;
// the overlay thread calls update () / touch () / tick () / describe (), with the instance as IInstanceView.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../q_router.h"
#include "../ui/ui.h"
#include "curve_source.h"
#include "touch_decoder.h"
#include "ui_stack.h"
#include "synthyboy/filter_response.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class FilterUi
{
public:
    // ---- the pages (UiStack ids)
    static constexpr int kElementFilter = 2; // the full screen of the filter
    enum Page { kPageRoute = 20, kPageList = 21, kPageMods = 22 };

    // ---- control IDs
    enum Id : int
    {
        kClose = 1, kPower, kRoute, kFullButton, kInstances, kModel,
        kCutoff = 100, kResonance, kDrive, kMix,
        kRouting = 110, kBlend, kLevel, kPan,
        kKeytrack = 120, kEnv1, kCleanMode, kVar, kCombOffset,
        kFormantX = 130, kFormantY, kFormantTrans, kFormantRes, kFormantSpread,
        kDisplayCutoff = 150, kDisplayFormant,
        kRouteDest = 200, kRouteBus1, kRouteBus2, kRouteLevel, kRoutePan, kRouteClose, kRouteBackdrop,
        kList = 300, kListBackdrop,
        kModsBack = 400, kModsRow = 410, kModsAddStep = 460, kModsAdd
    };

    struct ModRowInfo { int slot = -1; int source = 0; };

    FilterUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        assignIds();
    }

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        compact_.place ({ 8, kTabY + 4, 420, kTabH - 8 });
        full_.place (tab);

        popupBox_ = { w / 2 - 240, kTabY + kTabH / 2 - 110, 480, 220 };
        route_.place (popupBox_);
        route_.setPopup (true, tab);
        route_.setBackdropId (kRouteBackdrop);
        route_.close.setId (kRouteClose);

        list_.place ({ 340, kTabY + 20, 600, 8 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);

        mods_.place (tab);
    }

    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs, int selectedEnv, int selectedLfo)
    {
        highlight_ = highlight;
        stackDepth_ = stack.depth();
        syncPages (stack, inst);

        envSel_ = selectedEnv;
        lfoSel_ = selectedLfo;
        const int lastSel = inst.playLastSelector();
        selectedSource_ = lastSel == 1 ? envSource (selectedEnv) : lfoSource (selectedLfo);

        // Ensure curve sources exist
        if (! curve1_ || ! curve2_ || inst.sampleRate() != sampleRate_)
        {
            sampleRate_ = inst.sampleRate();
            CurveSource::Config c1 { sampleRate_, -1.0, 1 };
            CurveSource::Config c2 { sampleRate_, -1.0, 2 };
            curve1_ = std::make_unique<CurveSource> (c1);
            curve2_ = std::make_unique<CurveSource> (c2);
        }

        // Live readout
        float liveTargets1[2] = { -1.0f, -1.0f };
        float liveTargets2[2] = { -1.0f, -1.0f };
        synthyboy::LiveValues lv {};
        const bool hasLive = inst.live (lv);
        if (hasLive && lv.numTargets >= 4)
        {
            liveTargets1[0] = lv.target[0]; liveTargets1[1] = lv.target[1];
            liveTargets2[0] = lv.target[2]; liveTargets2[1] = lv.target[3];
        }

        curve1_->update (inst, hasLive ? liveTargets1 : nullptr, nowMs);
        curve2_->update (inst, hasLive ? liveTargets2 : nullptr, nowMs);

        // Build combined curve in dual mode
        if (sel_ == 3)
            computeCombinedCurve (inst);

        // Update display state
        updateDisplayData (inst, lv, hasLive);

        // Update UI controls
        pullControls (inst);

        // Modulations refresh
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }

        if (pageTop_ == kPageList && listParam_ >= 0)
            list_.state.current = static_cast<int> (std::lround (inst.param (listParam_)));

        inst.setUiFullOpen (! stack.isMain());
        publishQGroup (inst);
    }

    ui::Rect displayRect() const noexcept
    {
        if (stackDepth_ == 0)
            return compact_.displayRect();
        if (stackDepth_ == 1 || pageTop_ == kPageRoute || pageTop_ == kPageList)
            return full_.displayRect();
        return {};
    }

    bool isMain() const noexcept { return stackDepth_ == 0; }
    int selection() const noexcept { return sel_; } // 1: F1, 2: F2, 3: F1+F2
    int editFilter() const noexcept { return editFilter_; } // 1 or 2
    bool isDual() const noexcept { return sel_ == 3; }

    const int* qTargets() const noexcept { return qTargets_; }
    const char* qGroupName() const noexcept { return qName_; }

    void draw (gpu::SceneBuilder& sb) const
    {
        drawPage (sb);
        drawPopup (sb);
    }

    void drawPage (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0)
        {
            compact_.draw (sb);
            return;
        }
        if (pageTop_ == kPageMods)
        {
            mods_.draw (sb);
            return;
        }
        full_.draw (sb);
    }

    void drawPopup (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0 || pageTop_ == kPageMods)
            return;
        if (pageTop_ == kPageRoute)
            route_.draw (sb);
        else if (pageTop_ == kPageList)
            list_.draw (sb);
    }

    void drawOnDisplay (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0)
            compact_.full.draw (sb);
    }

    bool covers (int x, int y) const noexcept
    {
        return stackDepth_ > 0 ? ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y)
                               : ui::Rect { 8, kTabY + 4, 420, kTabH - 8 }.contains (x, y);
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        ui::Component* top = topComponent();
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            if (! covers (ev.x, ev.y) || ! top)
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

    void turnQ (int slot, double delta, UiStack& stack, IInstanceView& inst)
    {
        if (pageTop_ == kPageMods)
            handle (mods_.turn (delta), stack, inst);
        else if (stackDepth_ > 0)
            handle (full_.turnQ (slot, delta), stack, inst);
        else
            handle (compact_.qGroup().turn (slot, delta), stack, inst);
    }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }

    ui::FilterCompact& compact() noexcept { return compact_; }
    ui::FilterFullScreen& full() noexcept { return full_; }
    ui::FilterRoutePanel& routePanel() noexcept { return route_; }
    ui::ListPopup& list() noexcept { return list_; }
    ui::ModulationsPage& modsPage() noexcept { return mods_; }
    int topPage() const noexcept { return pageTop_; }

    static int envSource (int n) noexcept { return n >= 1 && n <= 6 ? n : ui::kNoSource; }
    static int lfoSource (int n) noexcept { return n >= 1 && n <= 8 ? 6 + n : (n == 9 ? 33 : (n == 10 ? 34 : ui::kNoSource)); }
    static constexpr double kModsRefreshMs = 100.0;

    static int indexOfKey (const std::string& key) noexcept
    {
        if (key.empty())
            return -1;
        const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key.c_str());
        return p != nullptr ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }

private:
    static const synthyboy::ParamInfo& info (int idx) noexcept { return synthyboy::paramTable()[idx]; }
    static double normOf (int idx, float plain) noexcept { return synthyboy::toNormalised (info (idx), plain); }
    static void writeNorm (IInstanceView& v, int idx, double norm) noexcept
    {
        if (idx >= 0)
            v.writeParam (idx, static_cast<float> (std::clamp (norm, 0.0, 1.0)));
    }
    static void writePlain (IInstanceView& v, int idx, double plain) noexcept
    {
        if (idx >= 0)
        {
            const synthyboy::ParamInfo& i = info (idx);
            v.writeParam (idx, static_cast<float> (synthyboy::toNormalised (i, synthyboy::constrainValue (i, static_cast<float> (plain)))));
        }
    }

    void assignIds()
    {
        compact_.instances.setId (kInstances);
        compact_.power.setId (kPower);
        compact_.model.setId (kModel);
        compact_.route.setId (kRoute);
        compact_.full.setId (kFullButton);
        compact_.display.assignIds (kDisplayCutoff, kResonance, kDisplayFormant, kFormantY);
        compact_.cutoff.setId (kCutoff);
        compact_.resonance.setId (kResonance);
        compact_.drive.setId (kDrive);
        compact_.mix.setId (kMix);

        full_.close.setId (kClose);
        full_.power.setId (kPower);
        full_.instances.setId (kInstances);
        full_.model.setId (kModel);
        full_.route.setId (kRoute);
        full_.display.assignIds (kDisplayCutoff, kResonance, kDisplayFormant, kFormantY);

        full_.routing.setId (kRouting);
        full_.blend.setId (kBlend);
        full_.level.setId (kLevel);
        full_.pan.setId (kPan);

        full_.drive.setId (kDrive);
        full_.var.setId (kVar);
        full_.combOffset.setId (kCombOffset);

        full_.formantX.setId (kFormantX);
        full_.formantY.setId (kFormantY);
        full_.formantTrans.setId (kFormantTrans);
        full_.formantRes.setId (kFormantRes);
        full_.formantSpread.setId (kFormantSpread);

        full_.keytrack.setId (kKeytrack);
        full_.env1.setId (kEnv1);
        full_.cleanMode.setId (kCleanMode);

        full_.cutoff.setId (kCutoff);
        full_.resonance.setId (kResonance);
        full_.mix.setId (kMix);

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
            return indexOfKey ("filter.routing");
        const char* s = nullptr;
        switch (id)
        {
            case kPower: s = "on"; break;
            case kModel: s = "model"; break;
            case kCutoff: case kDisplayCutoff: s = "cutoff"; break;
            case kResonance: s = "resonance"; break;
            case kDrive: s = "drive"; break;
            case kMix: s = "mix"; break;
            case kBlend: s = "blend"; break;
            case kLevel: case kRouteLevel: s = "level"; break;
            case kPan: case kRoutePan: s = "pan"; break;
            case kKeytrack: s = "keytrack"; break;
            case kEnv1: s = "env1"; break;
            case kCleanMode: s = "clean_mode"; break;
            case kVar: s = "var"; break;
            case kCombOffset: s = "comb_offset"; break;
            case kFormantX: case kDisplayFormant: s = "formant.x"; break;
            case kFormantY: s = "formant.y"; break;
            case kFormantTrans: s = "formant.transpose"; break;
            case kFormantRes: s = "formant.resonance"; break;
            case kFormantSpread: s = "formant.spread"; break;
            case kRouteDest: s = "destination"; break;
            case kRouteBus1: s = "bus1_send"; break;
            case kRouteBus2: s = "bus2_send"; break;
            default: return -1;
        }
        return indexOfKey ("filter" + std::to_string (filterNum) + "." + s);
    }

    ui::Component* topComponent() noexcept
    {
        if (stackDepth_ == 0) return &compact_;
        if (pageTop_ == kPageRoute) return &route_;
        if (pageTop_ == kPageList) return &list_;
        if (pageTop_ == kPageMods) return &mods_;
        return &full_;
    }

    void syncPages (const UiStack& stack, IInstanceView&)
    {
        pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
    }

    static ui::ModInput modsOf (const IInstanceView& v, int param, int selected, ModRowInfo* rows)
    {
        ui::ModInput mi;
        mi.selectedSource = selected;
        const size_t dest = synthyboy::modDestinationChoiceOf (static_cast<size_t> (param));
        if (dest == 0)
            return mi;
        int n = 0;
        for (int s = 0; s < synthyboy::kNumModSlots && n < ui::ModInput::kMaxMods; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source < 0 || sp.destination < 0 || sp.amount < 0)
                continue;
            const int src = static_cast<int> (std::lround (v.param (sp.source)));
            if (src <= 0 || static_cast<size_t> (std::lround (v.param (sp.destination))) != dest)
                continue;
            const double depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            const bool bi = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            const bool by = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            char name[7] = {};
            if (src >= 1 && src <= static_cast<int> (synthyboy::modSourceCount()))
                std::snprintf (name, sizeof name, "%.6s", synthyboy::modSourceTable()[src - 1].name);
            else
                std::snprintf (name, sizeof name, "SRC%d", src);
            mi.add (src, name, depth, bi, by);
            if (rows != nullptr)
            {
                rows[n].slot = s;
                rows[n].source = src;
            }
            ++n;
        }
        return mi;
    }

    void handle (ui::Change c, UiStack& stack, IInstanceView& inst)
    {
        if (! c)
            return;

        // Long press on a control asks for its modulations
        if (c.kind == ui::ChangeKind::ModsRequest)
        {
            const int p = paramForId (c.id, editFilter_);
            if (p >= 0)
            {
                activeModParam_ = p;
                modsDirty_ = true;
                refreshMods (inst);
                if (stack.depth() == 0)
                    stack.openFull (kElementFilter);
                stack.openDetail (kPageMods);
                syncPages (stack, inst);
            }
            return;
        }

        // Modulation matrix edits
        if (c.kind == ui::ChangeKind::Depth && c.id >= kModsRow && c.id < kModsRow + 4 * ui::ModulationsPage::kMaxRows)
        {
            const int row = (c.id - kModsRow) / 4;
            if (row >= 0 && row < ui::ModulationsPage::kMaxRows && modRows_[row].slot >= 0)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (modRows_[row].slot);
                writeNorm (inst, sp.amount, (c.value + 1.0) * 0.5);
                modsDirty_ = true;
            }
            return;
        }

        if (c.id == kModsBack)
        {
            stack.back();
            syncPages (stack, inst);
            return;
        }

        if (c.id == kModsAdd)
        {
            if (selectedSource_ != ui::kNoSource && activeModParam_ >= 0)
            {
                const size_t dest = synthyboy::modDestinationChoiceOf (static_cast<size_t> (activeModParam_));
                for (int s = 0; s < synthyboy::kNumModSlots; ++s)
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
                    if (sp.destination >= 0 && static_cast<int> (std::lround (inst.param (sp.destination))) == 0)
                    {
                        writePlain (inst, sp.source, selectedSource_);
                        writePlain (inst, sp.destination, static_cast<double> (dest));
                        writeNorm (inst, sp.amount, 0.75); // +50 % default
                        modsDirty_ = true;
                        break;
                    }
                }
            }
            return;
        }

        if (c.id >= kModsRow && c.id < kModsRow + 4 * ui::ModulationsPage::kMaxRows)
        {
            const int row = (c.id - kModsRow) / 4;
            const int part = (c.id - kModsRow) % 4;
            if (row >= 0 && row < ui::ModulationsPage::kMaxRows && modRows_[row].slot >= 0)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (modRows_[row].slot);
                if (part == 1) // polarity
                {
                    writeNorm (inst, sp.bipolar, c.index ? 1.0 : 0.0);
                    modsDirty_ = true;
                }
                else if (part == 2) // bypass
                {
                    writeNorm (inst, sp.bypass, c.index ? 1.0 : 0.0);
                    modsDirty_ = true;
                }
                else if (part == 3) // remove
                {
                    writePlain (inst, sp.destination, 0); // None
                    modsDirty_ = true;
                }
            }
            return;
        }

        // Navigation requests
        if (c.kind == ui::ChangeKind::Request)
        {
            if (c.id == kFullButton)
            {
                stack.openFull (kElementFilter);
                syncPages (stack, inst);
            }
            else if (c.id == kClose)
            {
                stack.back();
                syncPages (stack, inst);
            }
            else if (c.id == kRoute)
            {
                if (stack.depth() == 0)
                    stack.openFull (kElementFilter);
                stack.openDetail (kPageRoute);
                syncPages (stack, inst);
            }
            else if (c.id == kRouteClose || c.id == kRouteBackdrop)
            {
                stack.back();
                syncPages (stack, inst);
            }
            else if (c.id == kModel)
            {
                openModelList (inst, stack);
            }
            else if (c.id == kListBackdrop)
            {
                stack.back();
                syncPages (stack, inst);
            }
            return;
        }

        // Instance selection (F1 / F2 / F1+F2)
        if (c.id == kInstances)
        {
            sel_ = std::clamp (c.index + 1, 1, 3);
            if (sel_ <= 2)
                editFilter_ = sel_;
            compact_.instances.state.index = sel_ - 1;
            full_.instances.state.index = sel_ - 1;
            return;
        }

        // ListPopup selection
        if (c.id == kList && c.kind == ui::ChangeKind::Index)
        {
            if (listParam_ >= 0)
                writePlain (inst, listParam_, c.index);
            stack.back();
            syncPages (stack, inst);
            return;
        }

        // Interactive Display drag (Cutoff / Reso or Formant X / Y)
        ui::FilterDisplay& disp = (stackDepth_ > 0) ? full_.display : compact_.display;
        if (c.id == kDisplayCutoff)
        {
            const int cIdx = paramForId (kCutoff, editFilter_);
            const int rIdx = paramForId (kResonance, editFilter_);
            writeNorm (inst, cIdx, c.value);
            writeNorm (inst, rIdx, disp.resonanceNorm());
            return;
        }
        if (c.id == kDisplayFormant)
        {
            const int xIdx = paramForId (kFormantX, editFilter_);
            const int yIdx = paramForId (kFormantY, editFilter_);
            writeNorm (inst, xIdx, c.value);
            writeNorm (inst, yIdx, disp.formantYNorm());
            return;
        }

        // Normal parameter writes (value or index)
        const int p = paramForId (c.id, editFilter_);
        if (p < 0)
            return;

        if (c.kind == ui::ChangeKind::Value)
            writeNorm (inst, p, c.value);
        else if (c.kind == ui::ChangeKind::Index)
            writePlain (inst, p, c.index);
    }

    void openModelList (IInstanceView& inst, UiStack& stack)
    {
        listParam_ = paramForId (kModel, editFilter_);
        if (listParam_ < 0)
            return;
        const synthyboy::ParamInfo& pi = info (listParam_);
        list_.state.items = pi.choices;
        list_.state.count = pi.numChoices;
        list_.state.current = static_cast<int> (std::lround (inst.param (listParam_)));
        list_.showCurrent();
        if (stack.depth() == 0)
            stack.openFull (kElementFilter);
        stack.openDetail (kPageList);
        syncPages (stack, inst);
    }

    void computeCombinedCurve (IInstanceView& inst)
    {
        curveResult_.resize (CurveSource::kPoints, 0.0f);
        const float* c1 = curve1_->curve();
        const float* c2 = curve2_->curve();
        if (! c1 || ! c2)
            return;

        const int rIdx = indexOfKey ("filter.routing");
        const int routing = rIdx >= 0 ? static_cast<int> (std::lround (inst.param (rIdx))) : 0;

        for (int i = 0; i < CurveSource::kPoints; ++i)
        {
            if (routing == 0) // Parallel: add linear magnitudes
            {
                const double lin1 = std::pow (10.0, static_cast<double> (c1[i]) * 0.05);
                const double lin2 = std::pow (10.0, static_cast<double> (c2[i]) * 0.05);
                const double linSum = std::max (1.0e-6, lin1 + lin2);
                curveResult_[i] = static_cast<float> (std::clamp (20.0 * std::log10 (linSum), -120.0, 60.0));
            }
            else // Series 1>2 or Series 2>1: add dB
            {
                curveResult_[i] = static_cast<float> (std::clamp (static_cast<double> (c1[i]) + static_cast<double> (c2[i]), -120.0, 60.0));
            }
        }
    }

    void updateDisplayData (IInstanceView& inst, const synthyboy::LiveValues& lv, bool hasLive)
    {
        auto updateDisp = [&] (ui::FilterDisplay& d)
        {
            d.state.dual = (sel_ == 3);
            d.state.activeFilter = editFilter_;

            const int mIdx = paramForId (kModel, editFilter_);
            const int model = mIdx >= 0 ? static_cast<int> (std::lround (inst.param (mIdx))) : 0;
            d.state.isFormant = (model == 5);

            d.state.curve1 = curve1_->curve();
            d.state.numPoints1 = curve1_->points();
            d.state.curve2 = curve2_->curve();
            d.state.numPoints2 = curve2_->points();
            if (sel_ == 3)
            {
                d.state.curveResult = curveResult_.data();
                d.state.numPointsResult = static_cast<int> (curveResult_.size());
            }

            const int cutIdx = paramForId (kCutoff, editFilter_);
            const int resIdx = paramForId (kResonance, editFilter_);
            d.state.cutoffHz = cutIdx >= 0 ? inst.param (cutIdx) : 12000.0;
            d.state.resonancePct = resIdx >= 0 ? inst.param (resIdx) : 20.0;

            if (d.state.isFormant)
            {
                const int fxIdx = paramForId (kFormantX, editFilter_);
                const int fyIdx = paramForId (kFormantY, editFilter_);
                d.state.formantX = fxIdx >= 0 ? inst.param (fxIdx) : 50.0;
                d.state.formantY = fyIdx >= 0 ? inst.param (fyIdx) : 50.0;
            }

            // Live readout
            const CurveSource* activeCurve = (editFilter_ == 2 ? curve2_.get() : curve1_.get());
            d.state.liveCutoffHz = activeCurve ? activeCurve->stats().cutoffHz : -1.0;
            d.state.liveResoPct = activeCurve ? activeCurve->stats().resonancePercent : -1.0;

            // Voice markers (if voices are sounding)
            d.state.numVoices = 0;
            if (hasLive && lv.activeVoices > 0)
            {
                d.state.numVoices = std::min (lv.activeVoices, 8);
                const int ktIdx = paramForId (kKeytrack, editFilter_);
                const float kt = ktIdx >= 0 ? inst.param (ktIdx) * 0.01f : 0.0f;
                const float baseCutoff = cutIdx >= 0 ? inst.param (cutIdx) : 12000.0f;
                for (int v = 0; v < d.state.numVoices; ++v)
                {
                    // Synthesize voice cutoff from base cutoff and keytrack spread
                    const float noteOffset = static_cast<float> (v * 4 - 12);
                    d.state.voiceCutoffs[v] = std::clamp (baseCutoff * std::pow (2.0f, (noteOffset * kt) / 12.0f), 20.0f, 20000.0f);
                }
            }
        };

        updateDisp (compact_.display);
        updateDisp (full_.display);
    }

    void pullControls (IInstanceView& inst)
    {
        const int f = editFilter_;

        // Format helper
        char buf[32];
        auto formatVal = [&buf] (const char* fmt, double val)
        {
            std::snprintf (buf, sizeof buf, fmt, val);
            return buf;
        };

        // Model
        const int mIdx = paramForId (kModel, f);
        const int model = mIdx >= 0 ? static_cast<int> (std::lround (inst.param (mIdx))) : 0;
        const bool isFormant = (model == 5);
        if (full_.formantMode() != isFormant)
            full_.setFormantMode (isFormant);

        const synthyboy::ParamInfo* piModel = mIdx >= 0 ? &info (mIdx) : nullptr;
        const char* modelName = (piModel && model >= 0 && model < piModel->numChoices) ? piModel->choices[model] : "MODEL";
        compact_.model.setItem (modelName);
        compact_.model.state.count = piModel ? piModel->numChoices : 0;
        compact_.model.state.index = model;
        full_.model.setItem (modelName);
        full_.model.state.count = piModel ? piModel->numChoices : 0;
        full_.model.state.index = model;

        // Power
        const int powIdx = paramForId (kPower, f);
        const bool on = powIdx >= 0 && inst.param (powIdx) >= 0.5f;
        compact_.power.state.on = on;
        full_.power.state.on = on;

        // Cutoff
        const int cIdx = paramForId (kCutoff, f);
        if (cIdx >= 0)
        {
            const float cutHz = inst.param (cIdx);
            const double cutNorm = normOf (cIdx, cutHz);
            compact_.cutoff.state.value = compact_.cutoff.state.modValue = cutNorm;
            full_.cutoff.state.value = full_.cutoff.state.modValue = cutNorm;
            if (cutHz >= 1000.0f)
                formatVal ("%.1f kHz", cutHz / 1000.0f);
            else
                formatVal ("%.0f Hz", cutHz);
            ui::setText (compact_.cutoff.state.valueText, sizeof compact_.cutoff.state.valueText, buf);
            ui::setText (full_.cutoff.state.valueText, sizeof full_.cutoff.state.valueText, buf);
        }

        // Resonance
        const int rIdx = paramForId (kResonance, f);
        if (rIdx >= 0)
        {
            const float res = inst.param (rIdx);
            const double resNorm = normOf (rIdx, res);
            compact_.resonance.state.value = compact_.resonance.state.modValue = resNorm;
            full_.resonance.state.value = full_.resonance.state.modValue = resNorm;
            formatVal ("%.0f%%", res);
            ui::setText (compact_.resonance.state.valueText, sizeof compact_.resonance.state.valueText, buf);
            ui::setText (full_.resonance.state.valueText, sizeof full_.resonance.state.valueText, buf);
        }

        // Drive
        const int dIdx = paramForId (kDrive, f);
        if (dIdx >= 0)
        {
            const float drv = inst.param (dIdx);
            const double drvNorm = normOf (dIdx, drv);
            compact_.drive.state.value = compact_.drive.state.modValue = drvNorm;
            full_.drive.state.value = full_.drive.state.modValue = drvNorm;
            formatVal ("%.1f dB", drv);
            ui::setText (compact_.drive.state.valueText, sizeof compact_.drive.state.valueText, buf);
            ui::setText (full_.drive.state.valueText, sizeof full_.drive.state.valueText, buf);
        }

        // Mix
        const int mxIdx = paramForId (kMix, f);
        if (mxIdx >= 0)
        {
            const float mixVal = inst.param (mxIdx);
            const double mixNorm = normOf (mxIdx, mixVal);
            compact_.mix.state.value = compact_.mix.state.modValue = mixNorm;
            full_.mix.state.value = full_.mix.state.modValue = mixNorm;
            formatVal ("%.0f%%", mixVal);
            ui::setText (compact_.mix.state.valueText, sizeof compact_.mix.state.valueText, buf);
            ui::setText (full_.mix.state.valueText, sizeof full_.mix.state.valueText, buf);
        }

        // Routing block
        const int rtgIdx = indexOfKey ("filter.routing");
        if (rtgIdx >= 0)
        {
            const int rtg = static_cast<int> (std::lround (inst.param (rtgIdx)));
            static const char* const kRtgNames[] = { "PARALLEL", "SERIES 1>2", "SERIES 2>1" };
            full_.routing.setItem (rtg >= 0 && rtg < 3 ? kRtgNames[rtg] : "PARALLEL");
        }

        const int blIdx = paramForId (kBlend, f);
        if (blIdx >= 0)
        {
            full_.blend.state.value = full_.blend.state.modValue = normOf (blIdx, inst.param (blIdx));
            formatVal ("%.0f%%", inst.param (blIdx));
            ui::setText (full_.blend.state.valueText, sizeof full_.blend.state.valueText, buf);
        }

        const int lvlIdx = paramForId (kLevel, f);
        if (lvlIdx >= 0)
        {
            const float lvl = inst.param (lvlIdx);
            const double lvlNorm = normOf (lvlIdx, lvl);
            full_.level.state.value = full_.level.state.modValue = lvlNorm;
            route_.level.state.value = route_.level.state.modValue = lvlNorm;
            formatVal ("%+.1f dB", lvl);
            ui::setText (full_.level.state.valueText, sizeof full_.level.state.valueText, buf);
            ui::setText (route_.level.state.valueText, sizeof route_.level.state.valueText, buf);
        }

        const int panIdx = paramForId (kPan, f);
        if (panIdx >= 0)
        {
            const float p = inst.param (panIdx);
            const double panNorm = normOf (panIdx, p);
            full_.pan.state.value = full_.pan.state.modValue = panNorm;
            route_.pan.state.value = route_.pan.state.modValue = panNorm;
            if (std::fabs (p) < 1.0f)
                ui::setText (buf, sizeof buf, "C");
            else if (p < 0.0f)
                formatVal ("L %.0f%%", -p);
            else
                formatVal ("R %.0f%%", p);
            ui::setText (full_.pan.state.valueText, sizeof full_.pan.state.valueText, buf);
            ui::setText (route_.pan.state.valueText, sizeof route_.pan.state.valueText, buf);
        }

        // Tone block
        const int varIdx = paramForId (kVar, f);
        if (varIdx >= 0)
        {
            full_.var.state.value = full_.var.state.modValue = normOf (varIdx, inst.param (varIdx));
            formatVal ("%.0f%%", inst.param (varIdx));
            ui::setText (full_.var.state.valueText, sizeof full_.var.state.valueText, buf);
        }

        const int combIdx = paramForId (kCombOffset, f);
        if (combIdx >= 0)
        {
            full_.combOffset.state.value = full_.combOffset.state.modValue = normOf (combIdx, inst.param (combIdx));
            formatVal ("%.0f st", inst.param (combIdx));
            ui::setText (full_.combOffset.state.valueText, sizeof full_.combOffset.state.valueText, buf);
            full_.combOffset.setEnabled (model == 6); // enabled when Comb model
        }

        // Formant block
        const int fxIdx = paramForId (kFormantX, f);
        if (fxIdx >= 0)
        {
            full_.formantX.state.value = full_.formantX.state.modValue = normOf (fxIdx, inst.param (fxIdx));
            formatVal ("%.0f%%", inst.param (fxIdx));
            ui::setText (full_.formantX.state.valueText, sizeof full_.formantX.state.valueText, buf);
        }
        const int fyIdx = paramForId (kFormantY, f);
        if (fyIdx >= 0)
        {
            full_.formantY.state.value = full_.formantY.state.modValue = normOf (fyIdx, inst.param (fyIdx));
            formatVal ("%.0f%%", inst.param (fyIdx));
            ui::setText (full_.formantY.state.valueText, sizeof full_.formantY.state.valueText, buf);
        }
        const int ftrIdx = paramForId (kFormantTrans, f);
        if (ftrIdx >= 0)
        {
            full_.formantTrans.state.value = full_.formantTrans.state.modValue = normOf (ftrIdx, inst.param (ftrIdx));
            formatVal ("%+.0f st", inst.param (ftrIdx));
            ui::setText (full_.formantTrans.state.valueText, sizeof full_.formantTrans.state.valueText, buf);
        }
        const int fresIdx = paramForId (kFormantRes, f);
        if (fresIdx >= 0)
        {
            full_.formantRes.state.value = full_.formantRes.state.modValue = normOf (fresIdx, inst.param (fresIdx));
            formatVal ("%.0f%%", inst.param (fresIdx));
            ui::setText (full_.formantRes.state.valueText, sizeof full_.formantRes.state.valueText, buf);
        }
        const int fspIdx = paramForId (kFormantSpread, f);
        if (fspIdx >= 0)
        {
            full_.formantSpread.state.value = full_.formantSpread.state.modValue = normOf (fspIdx, inst.param (fspIdx));
            formatVal ("%+.0f%%", inst.param (fspIdx));
            ui::setText (full_.formantSpread.state.valueText, sizeof full_.formantSpread.state.valueText, buf);
        }

        // Track block
        const int ktIdx = paramForId (kKeytrack, f);
        if (ktIdx >= 0)
        {
            full_.keytrack.state.value = full_.keytrack.state.modValue = normOf (ktIdx, inst.param (ktIdx));
            formatVal ("%+.0f%%", inst.param (ktIdx));
            ui::setText (full_.keytrack.state.valueText, sizeof full_.keytrack.state.valueText, buf);
        }

        const int env1Idx = paramForId (kEnv1, f);
        if (env1Idx >= 0)
            full_.env1.state.on = inst.param (env1Idx) >= 0.5f;

        const int clnIdx = paramForId (kCleanMode, f);
        if (clnIdx >= 0)
            full_.cleanMode.state.on = inst.param (clnIdx) >= 0.5f;

        // Route panel destination and buses
        const int dstIdx = paramForId (kRouteDest, f);
        if (dstIdx >= 0)
        {
            const int dst = static_cast<int> (std::lround (inst.param (dstIdx)));
            static const char* const kDests[] = { "EFFECTS", "DIRECT", "NONE" };
            route_.destination.setItem (dst >= 0 && dst < 3 ? kDests[dst] : "EFFECTS");
            compact_.route.setTarget (dst == 1 ? "D" : (dst == 2 ? "OFF" : "M"));
            full_.route.setTarget (dst == 1 ? "D" : (dst == 2 ? "OFF" : "M"));
        }

        const int b1Idx = paramForId (kRouteBus1, f);
        if (b1Idx >= 0)
        {
            route_.bus1.state.value = route_.bus1.state.modValue = normOf (b1Idx, inst.param (b1Idx));
            formatVal ("%.0f%%", inst.param (b1Idx));
            ui::setText (route_.bus1.state.valueText, sizeof route_.bus1.state.valueText, buf);
        }

        const int b2Idx = paramForId (kRouteBus2, f);
        if (b2Idx >= 0)
        {
            route_.bus2.state.value = route_.bus2.state.modValue = normOf (b2Idx, inst.param (b2Idx));
            formatVal ("%.0f%%", inst.param (b2Idx));
            ui::setText (route_.bus2.state.valueText, sizeof route_.bus2.state.valueText, buf);
        }
    }

    void refreshMods (IInstanceView& inst)
    {
        if (activeModParam_ < 0)
            return;
        const ui::ModInput mi = modsOf (inst, activeModParam_, selectedSource_, modRows_);
        mods_.load (mi, "MODULATIONS");
        for (int i = 0; i < mods_.numRows(); ++i)
            mods_.setRowIds (i, kModsRow + 4 * i, kModsRow + 4 * i + 1, kModsRow + 4 * i + 2, kModsRow + 4 * i + 3);
    }

    void publishQGroup (IInstanceView& v)
    {
        int t[4] = { -1, -1, -1, -1 };
        const char* name = "";

        if (stackDepth_ == 0)
        {
            name = "Main";
            t[0] = paramForId (kCutoff, editFilter_);
            t[1] = paramForId (kResonance, editFilter_);
            t[2] = paramForId (kDrive, editFilter_);
            t[3] = paramForId (kMix, editFilter_);
        }
        else if (pageTop_ == kPageRoute)
        {
            name = "Route";
            t[0] = paramForId (kRouteDest, editFilter_);
            t[1] = paramForId (kRouteBus1, editFilter_);
            t[2] = paramForId (kRouteBus2, editFilter_);
            t[3] = paramForId (kRouteLevel, editFilter_);
        }
        else if (pageTop_ == kPageMods)
        {
            name = "Mods";
            const int row = mods_.activeRow();
            if (row >= 0 && row < ui::ModulationsPage::kMaxRows && modRows_[row].slot >= 0)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (modRows_[row].slot);
                t[0] = sp.amount; t[1] = sp.bipolar; t[2] = sp.bypass;
            }
        }
        else
        {
            const int ag = full_.activeGroup();
            name = full_.groupName (ag);
            if (ag == 0) // MAIN
            {
                t[0] = paramForId (kCutoff, editFilter_);
                t[1] = paramForId (kResonance, editFilter_);
                t[2] = full_.formantMode() ? paramForId (kFormantX, editFilter_) : paramForId (kDrive, editFilter_);
                t[3] = paramForId (kMix, editFilter_);
            }
            else if (ag == 1) // TRACK
            {
                t[0] = paramForId (kKeytrack, editFilter_);
                t[1] = paramForId (kEnv1, editFilter_);
                t[2] = paramForId (kVar, editFilter_);
                t[3] = paramForId (kCombOffset, editFilter_);
            }
            else if (ag == 2) // OUT
            {
                t[0] = paramForId (kLevel, editFilter_);
                t[1] = paramForId (kPan, editFilter_);
                t[2] = paramForId (kBlend, editFilter_);
                t[3] = indexOfKey ("filter.routing");
            }
            else if (ag == 3) // FORMANT
            {
                t[0] = paramForId (kFormantX, editFilter_);
                t[1] = paramForId (kFormantY, editFilter_);
                t[2] = paramForId (kFormantTrans, editFilter_);
                t[3] = paramForId (kFormantSpread, editFilter_);
            }
        }

        if (! qSet_ || std::memcmp (t, qTargets_, sizeof t) != 0)
        {
            std::memcpy (qTargets_, t, sizeof t);
            v.setQGroup (t, 4);
            qSet_ = true;
        }
        ui::setText (qName_, sizeof qName_, name);
    }

    int w_ = kCanvasW, h_ = kCanvasH;
    int stackDepth_ = 0, pageTop_ = 0;
    int highlight_ = 1;
    int sel_ = 1, editFilter_ = 1; // 1: F1, 2: F2, 3: F1+F2

    double sampleRate_ = 48000.0;
    std::unique_ptr<CurveSource> curve1_, curve2_;
    std::vector<float> curveResult_;

    ui::FilterCompact compact_;
    ui::FilterFullScreen full_;
    ui::FilterRoutePanel route_;
    ui::ListPopup list_;
    ui::ModulationsPage mods_;

    ui::Rect popupBox_;
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    int activeModParam_ = -1;
    bool modsDirty_ = false;
    double modsAtMs_ = 0.0;
    ModRowInfo modRows_[ui::ModulationsPage::kMaxRows];

    int envSel_ = 1, lfoSel_ = 1, selectedSource_ = ui::kNoSource;
    int listParam_ = -1;

    int qTargets_[4] = { -1, -1, -1, -1 };
    char qName_[20] = {};
    bool qSet_ = false;
};

}} // namespace synthyboy_mpc::overlay
