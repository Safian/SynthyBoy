// SynthyBoy - MPC overlay: Macros UI (macro_ui.h).
// Connects the Macros view (8 macro cards) to engine parameters macro.1 .. macro.8 via IInstanceView.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/mod_macros.h"
#include "curve_source.h"
#include "touch_decoder.h"
#include "ui_stack.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class MacroUi
{
public:
    static constexpr int kElementMacros = 3; // Full-screen Macros element

    enum Id : int
    {
        kClose = 10,
        kBank1 = 20, kBank2 = 21,
        kMacroKnobBase = 100, // 100 .. 107
        kMacroCardBase = 200  // 200 .. 207: request opening mod matrix filtered to macro
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    MacroUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        assignIds();
        initParamIndices();
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        view_.place ({ 0, kTabY, w, kTabH });
    }

    void update (IInstanceView& inst, const UiStack& stack, double nowMs)
    {
        (void) nowMs;
        stackDepth_ = stack.depth();

        // 1. Read macro values from engine
        for (int m = 0; m < 8; ++m)
        {
            if (macroParams_[m] >= 0)
            {
                const float val = inst.param (macroParams_[m]); // 0.0 .. 100.0%
                view_.setMacroValue (m, static_cast<double> (val) / 100.0);
            }
        }

        // 2. Resolve destination chips for each macro from the 64 matrix slots
        for (int m = 0; m < 8; ++m)
        {
            const int choice = macroSourceChoice (m);
            std::vector<std::string> dests;

            for (int s = 0; s < synthyboy::kNumModSlots; ++s)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
                if (sp.source < 0 || sp.destination < 0) continue;

                const int src = static_cast<int> (std::lround (inst.param (sp.source)));
                const int dst = static_cast<int> (std::lround (inst.param (sp.destination)));
                const bool byp = inst.param (sp.bypass) > 0.5f;

                if (src == choice && dst > 0 && ! byp)
                {
                    const int dp = synthyboy::modDestinationParamIndex (static_cast<size_t> (dst));
                    if (dp >= 0)
                    {
                        const char* n = synthyboy::paramTable()[dp].name;
                        dests.push_back (cleanDestName (n));
                    }
                }
            }

            view_.setMacroDestinations (m, dests);
        }

        publishQGroup (inst);
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        view_.draw (sb);
    }

    bool covers (int x, int y) const noexcept
    {
        return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y);
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            if (! covers (x, y))
                return false;
            gesture_ = &view_;
            gestureDown_ = true;
            handle (view_.touchDown (x, y, nowMs), stack, inst);
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
        ui::Change c = view_.turnQ (knobIndex, delta);
        handle (c, stack, inst);
        return c;
    }

    const int* qTargets() const noexcept { return qTargets_; }
    const char* qGroupName() const noexcept { return qName_; }
    int requestedMacro() const noexcept { return requestedMacroFilter_; }
    void clearRequestedMacro() noexcept { requestedMacroFilter_ = -1; }

    ui::MacroView& view() noexcept { return view_; }
    int bank() const noexcept { return view_.bank(); }
    void setBank (int b) { view_.setBank (b); }

    static int macroSourceChoice (int macroIdx) noexcept
    {
        if (macroIdx >= 0 && macroIdx < 4) return 19 + macroIdx; // Macro 1..4 (choices 19..22)
        if (macroIdx >= 4 && macroIdx < 8) return 29 + (macroIdx - 4); // Macro 5..8 (choices 29..32)
        return 0;
    }

private:
    int w_ = kCanvasW, h_ = kCanvasH;
    int stackDepth_ = 0;
    int macroParams_[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    int requestedMacroFilter_ = -1;

    ui::MacroView view_;
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    int qTargets_[4] = { -1, -1, -1, -1 };
    char qName_[32] = "MACROS 1-4";

    void assignIds()
    {
        view_.close.setId (kClose);
        view_.bank1Btn.setId (kBank1);
        view_.bank2Btn.setId (kBank2);

        for (int i = 0; i < 8; ++i)
        {
            view_.cards[i].knob.setId (kMacroKnobBase + i);
            view_.cards[i].setId (kMacroCardBase + i);
        }
    }

    void initParamIndices()
    {
        for (int i = 0; i < 8; ++i)
        {
            char key[16];
            std::snprintf (key, sizeof key, "macro.%d", i + 1);
            const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key);
            if (p != nullptr)
                macroParams_[i] = synthyboy::findParamIndex (p->id);
            else
                macroParams_[i] = -1;
        }
    }

    void publishQGroup (IInstanceView& inst)
    {
        const int b = view_.bank();
        std::snprintf (qName_, sizeof qName_, b == 0 ? "MACROS 1-4" : "MACROS 5-8");
        const int offset = b * 4;
        for (int k = 0; k < 4; ++k)
            qTargets_[k] = macroParams_[offset + k];
        inst.setQGroup (qTargets_, 4);
    }

    void handle (ui::Change c, UiStack& stack, IInstanceView& inst)
    {
        if (! c) return;

        if (c.id == kClose)
        {
            stack.back();
            return;
        }

        if (c.id == kBank1)
        {
            view_.setBank (0);
            publishQGroup (inst);
            return;
        }

        if (c.id == kBank2)
        {
            view_.setBank (1);
            publishQGroup (inst);
            return;
        }

        // Macro Knob drag
        if (c.id >= kMacroKnobBase && c.id < kMacroKnobBase + 8)
        {
            const int idx = c.id - kMacroKnobBase;
            if (idx >= 0 && idx < 8 && macroParams_[idx] >= 0)
            {
                if (c.kind == ui::ChangeKind::Value)
                {
                    inst.writeParam (macroParams_[idx], static_cast<float> (c.value));
                }
            }
            return;
        }

        // Tap card to open Mod Matrix filtered to this macro
        if (c.id >= kMacroCardBase && c.id < kMacroCardBase + 8)
        {
            const int idx = c.id - kMacroCardBase;
            requestedMacroFilter_ = idx;
            return;
        }
    }

    static std::string cleanDestName (const char* full)
    {
        if (! full || ! full[0]) return "";
        // Extract concise name: e.g. "Osc 1 Level" -> "Osc 1 Lvl", "Filter 1 Cutoff" -> "Flt 1 Cut"
        std::string s = full;
        if (s.rfind ("Osc 1 ", 0) == 0) s = "A:" + s.substr (6);
        else if (s.rfind ("Osc 2 ", 0) == 0) s = "B:" + s.substr (6);
        else if (s.rfind ("Osc 3 ", 0) == 0) s = "C:" + s.substr (6);
        else if (s.rfind ("Filter 1 ", 0) == 0) s = "F1:" + s.substr (9);
        else if (s.rfind ("Filter 2 ", 0) == 0) s = "F2:" + s.substr (9);
        if (s.length() > 10) s = s.substr (0, 10);
        return s;
    }
};

}} // namespace synthyboy_mpc::overlay
