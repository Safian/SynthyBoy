// SynthyBoy - MPC overlay: Modulation Matrix UI (mod_ui.h).
// Connects the Mod Matrix and Slot Detail views to engine parameters via IInstanceView.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/mod_matrix.h"
#include "../ui/mod_detail.h"
#include "../ui/list.h"
#include "curve_source.h"
#include "touch_decoder.h"
#include "ui_stack.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class ModUi
{
public:
    static constexpr int kElementModMatrix = 2; // Full-screen Mod Matrix element
    enum Page { kPageSlotDetail = 20, kPageSourcePopup = 21, kPageDestPopup = 22 };

    enum Id : int
    {
        kClose = 10,
        kFilterAll = 20, kFilterSource = 21, kFilterDest = 22,
        kSourceFilterStep = 25, kDestFilterStep = 26,
        kAddSlot = 30,
        kDetailBack = 40, kDetailDelete = 41,
        kTabAux = 50, kTabShape = 51, kTabTime = 52,
        kListPopup = 60, kListPopupBackdrop = 61,
        // Per-row controls: kRowBase + windowRow * 10 + offset
        kRowBase = 100,
        kOffsetSource = 0,
        kOffsetDest = 1,
        kOffsetAmount = 2,
        kOffsetBi = 3,
        kOffsetBypass = 4,
        kOffsetDetail = 5,
        // Detail controls
        kDetailAuxSource = 800,
        kDetailAuxOutput = 801,
        kDetailAuxInv = 802,
        kDetailAuxPower = 803,
        kDetailPower = 804,
        kDetailStereo = 805,
        kDetailRise = 806,
        kDetailFall = 807,
        kDetailDelay = 808,
        kDetailDelaySync = 809,
        kDetailDelayBeats = 810
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    ModUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        assignIds();
        initSourceNames();
        initDestNames();
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        matrix_.place (tab);
        // Slot detail slides in on the right (680 px wide)
        slotDetail_.place ({ w - 680, kTabY, 680, kTabH });

        // Popups
        sourceList_.place ({ w / 2 - 250, kTabY + 40, 500, 8 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        sourceList_.setPopup (tab);
        sourceList_.setId (kListPopup);
        sourceList_.setBackdropId (kListPopupBackdrop);

        destList_.place ({ w / 2 - 270, kTabY + 40, 540, 8 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        destList_.setPopup (tab);
        destList_.setId (kListPopup);
        destList_.setBackdropId (kListPopupBackdrop);
    }

    void update (IInstanceView& inst, const UiStack& stack, double nowMs)
    {
        (void) nowMs;
        stackDepth_ = stack.depth();
        pageTop_ = stack.top();

        synthyboy::LiveValues liveVals;
        const bool hasLive = inst.live (liveVals);

        // Update all 64 slots
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source < 0 || sp.destination < 0) continue;

            const int srcVal = static_cast<int> (std::lround (inst.param (sp.source)));
            const int dstVal = static_cast<int> (std::lround (inst.param (sp.destination)));
            const float amtVal = inst.param (sp.amount); // -100 .. +100
            const bool biVal = inst.param (sp.bipolar) > 0.5f;
            const bool bypVal = inst.param (sp.bypass) > 0.5f;

            const char* sName = sourceNameOf (srcVal);
            const char* dName = destNameOf (dstVal);
            const int dModule = destModuleOf (dstVal);

            double liveSourceVal = 0.0;
            if (hasLive && srcVal >= 1 && srcVal <= synthyboy::LiveValues::kMaxSources)
                liveSourceVal = static_cast<double> (liveVals.source[srcVal - 1]);

            matrix_.setSlotData (s, sName, dName, static_cast<double> (amtVal) / 100.0,
                                 biVal, bypVal, srcVal, dModule, hasLive && srcVal > 0, liveSourceVal);
        }

        matrix_.rebuildVisibleList();

        // Update detail panel if open
        if (pageTop_ == kPageSlotDetail && openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (openSlot_);
            const int srcVal = static_cast<int> (std::lround (inst.param (sp.source)));
            const int dstVal = static_cast<int> (std::lround (inst.param (sp.destination)));
            slotDetail_.setSlot (openSlot_, sourceNameOf (srcVal), destNameOf (dstVal));

            const int auxSrc = static_cast<int> (std::lround (inst.param (sp.auxSource)));
            const float auxOut = inst.param (sp.auxOutput) / 100.0f; // 0..200% -> 0..2
            const bool auxInv = inst.param (sp.auxInv) > 0.5f;
            const float auxPwr = inst.param (sp.auxPower);
            slotDetail_.setAuxData (sourceNameOf (auxSrc), static_cast<double> (auxOut), auxInv, static_cast<double> (auxPwr));

            const float pwr = inst.param (sp.power);
            const bool st = inst.param (sp.stereo) > 0.5f;
            slotDetail_.setShapeData (static_cast<double> (pwr), st);

            const float r = inst.param (sp.rise);
            const float f = inst.param (sp.fall);
            const float d = inst.param (sp.delay);
            const bool sync = inst.param (sp.delaySync) > 0.5f;
            const float beats = inst.param (sp.delayBeats);
            slotDetail_.setTimeData (static_cast<double> (r), static_cast<double> (f), static_cast<double> (d), sync, static_cast<double> (beats));
        }

        publishQGroup (inst);
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        // Matrix view always drawn as base
        matrix_.draw (sb);

        // Slide-in slot detail panel if detail open
        if (pageTop_ == kPageSlotDetail)
        {
            slotDetail_.draw (sb);
        }
        else if (pageTop_ == kPageSourcePopup)
        {
            sourceList_.draw (sb);
        }
        else if (pageTop_ == kPageDestPopup)
        {
            destList_.draw (sb);
        }
    }

    bool covers (int x, int y) const noexcept
    {
        return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y);
    }

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

    ui::Change turnQ (int knobIndex, double delta, UiStack& stack, IInstanceView& inst)
    {
        ui::Change c;
        if (pageTop_ == kPageSlotDetail)
            c = slotDetail_.turnQ (knobIndex, delta);
        else
            c = matrix_.turnQ (knobIndex, delta);
        handle (c, stack, inst);
        return c;
    }

    const int* qTargets() const noexcept { return qTargets_; }
    const char* qGroupName() const noexcept { return qName_; }
    int topPage() const noexcept { return pageTop_; }

    ui::ModMatrixView& matrix() noexcept { return matrix_; }
    ui::ModSlotDetail& detail() noexcept { return slotDetail_; }

    void openSlotDetail (int slot, UiStack& stack)
    {
        openSlot_ = slot;
        stack.openDetail (kPageSlotDetail);
    }

    void setFilterSource (int srcChoice)
    {
        matrix_.setFilterMode (ui::ModMatrixView::FilterSource);
        matrix_.setSourceFilterChoice (srcChoice);
    }

    void setFilterDestModule (int modIdx)
    {
        matrix_.setFilterMode (ui::ModMatrixView::FilterDest);
        matrix_.setDestModuleFilter (modIdx);
    }

    void setFilterAll()
    {
        matrix_.setFilterMode (ui::ModMatrixView::FilterAll);
    }

private:
    int w_ = kCanvasW, h_ = kCanvasH;
    int stackDepth_ = 0;
    int pageTop_ = -1;
    int openSlot_ = -1;
    int popupSlot_ = -1;

    ui::ModMatrixView matrix_;
    ui::ModSlotDetail slotDetail_;
    ui::ListPopup sourceList_;
    ui::ListPopup destList_;

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    int qTargets_[4] = { -1, -1, -1, -1 };
    char qName_[32] = "MOD MATRIX";

    std::vector<std::string> sourceNames_;
    std::vector<const char*> sourceNamePtrs_;

    std::vector<std::string> destNames_;
    std::vector<const char*> destNamePtrs_;

    ui::Component* topComponent() noexcept
    {
        if (pageTop_ == kPageSourcePopup) return &sourceList_;
        if (pageTop_ == kPageDestPopup) return &destList_;
        if (pageTop_ == kPageSlotDetail) return &slotDetail_;
        return &matrix_;
    }

    void handle (ui::Change c, UiStack& stack, IInstanceView& inst)
    {
        if (! c) return;

        // Close / Back button
        if (c.id == kClose)
        {
            stack.back();
            return;
        }

        // Filter tabs
        if (c.id == kFilterAll) { setFilterAll(); return; }
        if (c.id == kFilterSource) { matrix_.setFilterMode (ui::ModMatrixView::FilterSource); return; }
        if (c.id == kFilterDest) { matrix_.setFilterMode (ui::ModMatrixView::FilterDest); return; }

        // Filter steppers
        if (c.id == kSourceFilterStep && c.kind == ui::ChangeKind::Index)
        {
            matrix_.setSourceFilterChoice (c.index);
            return;
        }
        if (c.id == kDestFilterStep && c.kind == ui::ChangeKind::Index)
        {
            matrix_.setDestModuleFilter (c.index - 1);
            return;
        }

        // Add Slot
        if (c.id == kAddSlot)
        {
            // Find first unassigned slot
            for (int s = 0; s < synthyboy::kNumModSlots; ++s)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
                const int src = static_cast<int> (std::lround (inst.param (sp.source)));
                const int dst = static_cast<int> (std::lround (inst.param (sp.destination)));
                if (src == 0 && dst == 0)
                {
                    // Assign default: Env 1 -> Cutoff (or first modulatable), +50%
                    writePlain (inst, sp.source, 1.0f); // Env 1
                    writePlain (inst, sp.destination, 1.0f);
                    writeNorm (inst, sp.amount, 0.75); // +50%
                    openSlotDetail (s, stack);
                    break;
                }
            }
            return;
        }

        // Slot Detail Back
        if (c.id == kDetailBack)
        {
            stack.back();
            return;
        }

        // Slot Detail Delete Confirmed
        if (c.id == kDetailDelete && openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (openSlot_);
            writePlain (inst, sp.source, 0.0f);
            writePlain (inst, sp.destination, 0.0f);
            writePlain (inst, sp.amount, 0.0f);
            writePlain (inst, sp.bipolar, 0.0f);
            writePlain (inst, sp.stereo, 0.0f);
            writePlain (inst, sp.bypass, 0.0f);
            writePlain (inst, sp.auxSource, 0.0f);
            stack.back();
            return;
        }

        // Popups
        if (c.id == kListPopupBackdrop)
        {
            stack.back();
            return;
        }

        if (c.id == kListPopup && c.kind == ui::ChangeKind::Index)
        {
            if (pageTop_ == kPageSourcePopup && popupSlot_ >= 0 && popupSlot_ < synthyboy::kNumModSlots)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (popupSlot_);
                writePlain (inst, sp.source, static_cast<float> (c.index));
                stack.back();
            }
            else if (pageTop_ == kPageDestPopup && popupSlot_ >= 0 && popupSlot_ < synthyboy::kNumModSlots)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (popupSlot_);
                writePlain (inst, sp.destination, static_cast<float> (c.index));
                stack.back();
            }
            return;
        }

        // Per-row controls (id = kRowBase + windowRow * 10 + offset)
        if (c.id >= kRowBase && c.id < kRowBase + ui::ModMatrixView::kWindowRows * 10)
        {
            const int wIdx = (c.id - kRowBase) / 10;
            const int offset = (c.id - kRowBase) % 10;
            const int slot = matrix_.slotAtWindowIndex (wIdx);

            if (slot == ui::ModMatrixView::kMaxSlots)
            {
                handle (ui::Change::request (kAddSlot), stack, inst);
                return;
            }

            if (slot < 0 || slot >= synthyboy::kNumModSlots) return;

            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);

            if (offset == kOffsetSource)
            {
                popupSlot_ = slot;
                sourceList_.state.current = static_cast<int> (std::lround (inst.param (sp.source)));
                sourceList_.showCurrent();
                stack.openDetail (kPageSourcePopup);
            }
            else if (offset == kOffsetDest)
            {
                popupSlot_ = slot;
                destList_.state.current = static_cast<int> (std::lround (inst.param (sp.destination)));
                destList_.showCurrent();
                stack.openDetail (kPageDestPopup);
            }
            else if (offset == kOffsetAmount && c.kind == ui::ChangeKind::Value)
            {
                writeNorm (inst, sp.amount, c.value);
            }
            else if (offset == kOffsetBi)
            {
                const bool cur = inst.param (sp.bipolar) > 0.5f;
                writePlain (inst, sp.bipolar, cur ? 0.0f : 1.0f);
            }
            else if (offset == kOffsetBypass)
            {
                const bool cur = inst.param (sp.bypass) > 0.5f;
                writePlain (inst, sp.bypass, cur ? 0.0f : 1.0f);
            }
            else if (offset == kOffsetDetail)
            {
                openSlotDetail (slot, stack);
            }
            return;
        }

        // Detail controls
        if (openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (openSlot_);
            if (c.id == kDetailAuxSource)
            {
                popupSlot_ = openSlot_;
                sourceList_.state.current = static_cast<int> (std::lround (inst.param (sp.auxSource)));
                sourceList_.showCurrent();
                stack.openDetail (kPageSourcePopup);
            }
            else if (c.id == kDetailAuxOutput && c.kind == ui::ChangeKind::Value)
            {
                writeNorm (inst, sp.auxOutput, c.value);
            }
            else if (c.id == kDetailAuxInv)
            {
                const bool cur = inst.param (sp.auxInv) > 0.5f;
                writePlain (inst, sp.auxInv, cur ? 0.0f : 1.0f);
            }
            else if (c.id == kDetailAuxPower && c.kind == ui::ChangeKind::Value)
            {
                writeNorm (inst, sp.auxPower, c.value);
            }
            else if (c.id == kDetailPower && c.kind == ui::ChangeKind::Value)
            {
                writeNorm (inst, sp.power, c.value);
            }
            else if (c.id == kDetailStereo)
            {
                const bool cur = inst.param (sp.stereo) > 0.5f;
                writePlain (inst, sp.stereo, cur ? 0.0f : 1.0f);
            }
            else if (c.id == kDetailRise && c.kind == ui::ChangeKind::Value)
            {
                writeNorm (inst, sp.rise, c.value);
            }
            else if (c.id == kDetailFall && c.kind == ui::ChangeKind::Value)
            {
                writeNorm (inst, sp.fall, c.value);
            }
            else if (c.id == kDetailDelay && c.kind == ui::ChangeKind::Value)
            {
                writeNorm (inst, sp.delay, c.value);
            }
            else if (c.id == kDetailDelaySync)
            {
                const bool cur = inst.param (sp.delaySync) > 0.5f;
                writePlain (inst, sp.delaySync, cur ? 0.0f : 1.0f);
            }
            else if (c.id == kDetailDelayBeats && c.kind == ui::ChangeKind::Value)
            {
                writeNorm (inst, sp.delayBeats, c.value);
            }
        }
    }

    void assignIds()
    {
        matrix_.close.setId (kClose);
        matrix_.tabAll.setId (kFilterAll);
        matrix_.tabSource.setId (kFilterSource);
        matrix_.tabDest.setId (kFilterDest);
        matrix_.sourceFilter.setId (kSourceFilterStep);
        matrix_.destFilter.setId (kDestFilterStep);

        for (int w = 0; w < ui::ModMatrixView::kWindowRows; ++w)
        {
            const int base = kRowBase + w * 10;
            matrix_.windowRows[w].sourceBtn.setId (base + kOffsetSource);
            matrix_.windowRows[w].destBtn.setId (base + kOffsetDest);
            matrix_.windowRows[w].amountBar.setId (base + kOffsetAmount);
            matrix_.windowRows[w].biToggle.setId (base + kOffsetBi);
            matrix_.windowRows[w].bypassToggle.setId (base + kOffsetBypass);
            matrix_.windowRows[w].detailBtn.setId (base + kOffsetDetail);
            matrix_.windowRows[w].addBtn.setId (base + kOffsetSource);
        }

        slotDetail_.back.setId (kDetailBack);
        slotDetail_.deleteBtn.setId (kDetailDelete);
        slotDetail_.auxSource.setId (kDetailAuxSource);
        slotDetail_.auxOutput.setId (kDetailAuxOutput);
        slotDetail_.auxInv.setId (kDetailAuxInv);
        slotDetail_.auxPower.setId (kDetailAuxPower);
        slotDetail_.power.setId (kDetailPower);
        slotDetail_.stereo.setId (kDetailStereo);
        slotDetail_.rise.setId (kDetailRise);
        slotDetail_.fall.setId (kDetailFall);
        slotDetail_.delay.setId (kDetailDelay);
        slotDetail_.delaySync.setId (kDetailDelaySync);
        slotDetail_.delayBeats.setId (kDetailDelayBeats);
    }

    void publishQGroup (IInstanceView& inst)
    {
        if (pageTop_ == kPageSlotDetail && openSlot_ >= 0 && openSlot_ < synthyboy::kNumModSlots)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (openSlot_);
            std::snprintf (qName_, sizeof qName_, "SLOT %02d DETAIL", openSlot_ + 1);
            if (slotDetail_.activeBlock() == ui::ModSlotDetail::BlockAux)
            {
                qTargets_[0] = sp.auxSource;
                qTargets_[1] = sp.auxOutput;
                qTargets_[2] = sp.auxInv;
                qTargets_[3] = sp.auxPower;
            }
            else if (slotDetail_.activeBlock() == ui::ModSlotDetail::BlockShape)
            {
                qTargets_[0] = sp.power;
                qTargets_[1] = sp.stereo;
                qTargets_[2] = -1;
                qTargets_[3] = -1;
            }
            else
            {
                qTargets_[0] = sp.rise;
                qTargets_[1] = sp.fall;
                qTargets_[2] = inst.param (sp.delaySync) > 0.5f ? sp.delayBeats : sp.delay;
                qTargets_[3] = sp.delaySync;
            }
        }
        else
        {
            const int sel = matrix_.selectedSlot();
            if (sel >= 0 && sel < synthyboy::kNumModSlots)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (sel);
                std::snprintf (qName_, sizeof qName_, "SLOT %02d", sel + 1);
                qTargets_[0] = sp.amount;
                qTargets_[1] = sp.source;
                qTargets_[2] = sp.destination;
                qTargets_[3] = -1; // row navigation
            }
            else
            {
                std::snprintf (qName_, sizeof qName_, "MOD MATRIX");
                for (int i = 0; i < 4; ++i) qTargets_[i] = -1;
            }
        }
        inst.setQGroup (qTargets_, 4);
    }

    void initSourceNames()
    {
        sourceNames_.clear();
        sourceNamePtrs_.clear();
        sourceNames_.push_back ("None");
        const synthyboy::ModSourceInfo* t = synthyboy::modSourceTable();
        const size_t n = synthyboy::modSourceCount();
        for (size_t i = 0; i < n; ++i)
            sourceNames_.push_back (t[i].name);

        for (const auto& s : sourceNames_)
            sourceNamePtrs_.push_back (s.c_str());

        sourceList_.state.items = sourceNamePtrs_.data();
        sourceList_.state.count = static_cast<int> (sourceNamePtrs_.size());
    }

    void initDestNames()
    {
        destNames_.clear();
        destNamePtrs_.clear();
        destNames_.push_back ("None");
        const size_t n = synthyboy::modDestinationCount();
        for (size_t i = 1; i <= n; ++i)
        {
            const int p = synthyboy::modDestinationParamIndex (i);
            if (p >= 0)
                destNames_.push_back (synthyboy::paramTable()[p].name);
            else
                destNames_.push_back ("-");
        }

        for (const auto& d : destNames_)
            destNamePtrs_.push_back (d.c_str());

        destList_.state.items = destNamePtrs_.data();
        destList_.state.count = static_cast<int> (destNamePtrs_.size());
    }

    const char* sourceNameOf (int choice) const noexcept
    {
        if (choice >= 0 && choice < static_cast<int> (sourceNamePtrs_.size()))
            return sourceNamePtrs_[choice];
        return "";
    }

    const char* destNameOf (int choice) const noexcept
    {
        if (choice >= 0 && choice < static_cast<int> (destNamePtrs_.size()))
            return destNamePtrs_[choice];
        return "";
    }

    int destModuleOf (int choice) const noexcept
    {
        if (choice <= 0) return -1;
        const int p = synthyboy::modDestinationParamIndex (choice);
        if (p < 0) return -1;
        const char* grp = synthyboy::paramTable()[p].group;
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
        return 9; // Master / Other
    }

    static void writeNorm (IInstanceView& v, int idx, double norm) noexcept
    {
        if (idx >= 0)
            v.writeParam (idx, static_cast<float> (std::clamp (norm, 0.0, 1.0)));
    }

    static void writePlain (IInstanceView& v, int idx, float plain) noexcept
    {
        if (idx >= 0)
        {
            const synthyboy::ParamInfo& i = synthyboy::paramTable()[idx];
            v.writeParam (idx, static_cast<float> (synthyboy::toNormalised (i, synthyboy::constrainValue (i, plain))));
        }
    }
};

}} // namespace synthyboy_mpc::overlay
