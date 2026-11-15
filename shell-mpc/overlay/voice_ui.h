// SynthyBoy - MPC overlay: the Voice UI (voicectl) of the Play page, wired to real engine parameters (docs/MPC.md, ui-elem-mastervoice.md).
// 8-step sequencer, per-voice random spread, time scaling, oscillator targeting.
// Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw ().
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/list.h"
#include "../ui/theme.h"
#include "../ui/voice_screen.h"
#include "curve_source.h"
#include "touch_decoder.h"
#include "ui_stack.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class VoiceUi
{
public:
    static constexpr int kElementVoice = 3; // UiStack element id for Voice full screen
    enum Page { kPageList = 15 };

    enum Id : int
    {
        kClose = 1,
        kLength = 2,
        kStepSelect = 3,
        kRowSelect = 4,

        // Step Grid: 6 rows * 8 steps = 48 values -> 100 .. 147
        kGridBase = 100,
        // Row tabs: 150 .. 155
        kRowTabBase = 150,

        // Block 1: Random
        kRandPan = 200, kRandDetune, kRandCutoff, kRandEnv, kRandDetune10x,
        // Block 2: Scaling
        kScaleEnv = 210, kScaleLfo, kScaleLfoMode,
        // Block 3: Osc Targets
        kOscA = 220, kOscB, kOscC, kOscSub, kOscNoise,

        // Popup
        kList = 700, kListBackdrop = 701
    };

    enum class T : uint8_t
    {
        Knob,
        Number,
        Toggle,
        Stepper,
        GridStep,
        Inert
    };

    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        T type = T::Inert;
        std::string key;
        int param = -1;
        bool alwaysOff = false;
        int row = -1;
        int step = -1;
    };

    VoiceUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        configure();
    }

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        full_.place (tab);
        list_.place ({ 340, kTabY + 20, 600, 8 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
    }

    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs)
    {
        (void) highlight; (void) nowMs;
        stackDepth_ = stack.depth();
        syncPages (stack, inst);

        // Pull values from engine into component states
        pull (inst);

        // Live voice step highlighting from engine readout
        synthyboy::LiveValues live;
        if (inst.live (live) && live.activeVoices > 0)
        {
            const int len = std::max (1, full_.grid.length);
            full_.grid.voiceStepMask = static_cast<uint8_t> (1u << (static_cast<unsigned> (live.activeVoices) % static_cast<unsigned> (len)));
        }
        else
        {
            full_.grid.voiceStepMask = 0;
        }

        // Format knob values
        formatKnobValues (inst);

        // Update step & row selectors in header
        char stepBuf[16];
        std::snprintf (stepBuf, sizeof stepBuf, "STEP %d", full_.grid.selectedStep + 1);
        full_.qStepSelect.state.count = 8;
        full_.qStepSelect.state.index = full_.grid.selectedStep;
        full_.qStepSelect.setItem (stepBuf);

        full_.qRowSelect.state.count = 6;
        full_.qRowSelect.state.index = full_.grid.activeRow;
        full_.qRowSelect.setItem (full_.grid.rowName (full_.grid.activeRow));

        // Publish active QGroup
        publishQGroup (inst);
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

    ui::Change turnQ (int slot, double delta, UiStack& stack, IInstanceView& inst)
    {
        const ui::Change ch = full_.turnQ (slot, delta);
        handle (ch, stack, inst);
        return ch;
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        full_.draw (sb);
        if (pageTop_ == kPageList)
            list_.draw (sb);
    }

    // Access for tests
    ui::VoiceScreen& screen() noexcept { return full_; }
    ui::VoiceGrid& grid() noexcept { return full_.grid; }
    ui::ListPopup& list() noexcept { return list_; }
    const std::vector<Item>& items() const noexcept { return items_; }
    const Item* item (int id) const noexcept
    {
        for (const Item& it : items_)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    int activeQ() const noexcept { return full_.activeGroup(); }
    const int* qTargets() const noexcept { return qTargets_; }

private:
    static int indexOfKey (const std::string& key) noexcept
    {
        if (key.empty()) return -1;
        const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key.c_str());
        return p != nullptr ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }

    static const synthyboy::ParamInfo& info (int idx) noexcept { return synthyboy::paramTable()[idx]; }
    static double normOf (int idx, float plain) noexcept { return synthyboy::toNormalised (info (idx), plain); }

    static void writePlain (IInstanceView& v, int idx, double plain) noexcept
    {
        const synthyboy::ParamInfo& i = info (idx);
        v.writeParam (idx, static_cast<float> (synthyboy::toNormalised (i, synthyboy::constrainValue (i, static_cast<float> (plain)))));
    }

    static void writeNorm (IInstanceView& v, int idx, double norm) noexcept
    {
        v.writeParam (idx, static_cast<float> (std::clamp (norm, 0.0, 1.0)));
    }

    void bind (int id, ui::Component* c, T type, const std::string& key, int row = -1, int step = -1)
    {
        Item it;
        it.id = id; it.c = c; it.type = type;
        it.key = key;
        it.param = indexOfKey (key);
        it.row = row; it.step = step;
        if (c != nullptr)
            c->setId (id);
        items_.push_back (it);
    }

    void configure()
    {
        items_.clear();

        // Header controls
        bind (kClose, &full_.close, T::Inert, "");
        bind (kLength, &full_.length, T::Number, "voicectl.length");
        bind (kStepSelect, &full_.qStepSelect, T::Inert, "");
        bind (kRowSelect, &full_.qRowSelect, T::Inert, "");

        // Step Grid parameters: 6 rows x 8 steps = 48 parameters
        static const char* const kRows[6] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
        full_.grid.baseId = kGridBase;
        full_.grid.rowTabBaseId = kRowTabBase;

        for (int r = 0; r < 6; ++r)
        {
            for (int s = 0; s < 8; ++s)
            {
                const int id = kGridBase + r * 8 + s;
                const std::string key = "voicectl.s" + std::to_string (s + 1) + "." + kRows[r];
                bind (id, &full_.grid, T::GridStep, key, r, s);
            }
        }

        full_.scaleLfoMode.listable = true;
        full_.qRowSelect.listable = true;
        full_.qStepSelect.listable = true;

        // Block 0: Random (voicectl.rand.*)
        bind (kRandPan, &full_.randPan, T::Knob, "voicectl.rand.pan");
        bind (kRandDetune, &full_.randDetune, T::Knob, "voicectl.rand.detune");
        bind (kRandCutoff, &full_.randCutoff, T::Knob, "voicectl.rand.cutoff");
        bind (kRandEnv, &full_.randEnv, T::Knob, "voicectl.rand.env");
        bind (kRandDetune10x, &full_.randDetune10x, T::Toggle, "voicectl.rand.detune10x");

        // Block 1: Scaling (voicectl.scale.*)
        bind (kScaleEnv, &full_.scaleEnv, T::Knob, "voicectl.scale.env");
        bind (kScaleLfo, &full_.scaleLfo, T::Knob, "voicectl.scale.lfo");
        bind (kScaleLfoMode, &full_.scaleLfoMode, T::Stepper, "voicectl.scale.lfo_mode");

        // Block 2: Osc Targets (voicectl.osc.*)
        bind (kOscA, &full_.oscA, T::Toggle, "voicectl.osc.a");
        bind (kOscB, &full_.oscB, T::Toggle, "voicectl.osc.b");
        bind (kOscC, &full_.oscC, T::Toggle, "voicectl.osc.c");
        bind (kOscSub, &full_.oscSub, T::Toggle, "voicectl.osc.sub");
        bind (kOscNoise, &full_.oscNoise, T::Toggle, "voicectl.osc.noise");
    }

    void pull (IInstanceView& inst)
    {
        for (const Item& it : items_)
        {
            if (it.param < 0 || ! it.c)
                continue;
            const float val = inst.param (it.param);
            const synthyboy::ParamInfo& p = info (it.param);

            switch (it.type)
            {
                case T::Knob:
                    if (auto* kn = dynamic_cast<ui::Knob*> (it.c))
                    {
                        kn->state.value = normOf (it.param, val);
                        kn->state.modValue = kn->state.value;
                    }
                    break;
                case T::Number:
                    if (auto* nf = dynamic_cast<ui::NumberField*> (it.c))
                    {
                        nf->state.value = val;
                        if (it.id == kLength)
                            full_.grid.length = static_cast<int> (std::lround (val));
                    }
                    break;
                case T::Toggle:
                    if (auto* tg = dynamic_cast<ui::ToggleIcon*> (it.c))
                        tg->state.on = (val > 0.5f);
                    break;
                case T::Stepper:
                    if (auto* st = dynamic_cast<ui::Stepper*> (it.c))
                    {
                        st->state.count = p.numChoices > 0 ? p.numChoices : 2;
                        st->state.index = static_cast<int> (std::lround (val));
                        if (p.numChoices > 0 && st->state.index >= 0 && st->state.index < p.numChoices)
                            st->setItem (p.choices[st->state.index]);
                    }
                    break;
                case T::GridStep:
                    if (it.row >= 0 && it.step >= 0)
                    {
                        // Convert -100 .. 100 plain value to -1.0 .. 1.0 grid value
                        full_.grid.setValue (it.row, it.step, val * 0.01f);
                    }
                    break;
                case T::Inert:
                    break;
            }
        }
    }

    void formatKnobValues (IInstanceView& inst)
    {
        auto fmtPct = [&] (ui::Knob& kn, const char* key) {
            int p = indexOfKey (key);
            if (p >= 0) {
                int v = static_cast<int> (std::lround (inst.param (p)));
                std::snprintf (kn.state.valueText, sizeof kn.state.valueText, "%d%%", v);
            }
        };

        fmtPct (full_.randPan, "voicectl.rand.pan");
        fmtPct (full_.randCutoff, "voicectl.rand.cutoff");
        fmtPct (full_.randEnv, "voicectl.rand.env");
        fmtPct (full_.scaleEnv, "voicectl.scale.env");
        fmtPct (full_.scaleLfo, "voicectl.scale.lfo");

        int p = indexOfKey ("voicectl.rand.detune");
        if (p >= 0) {
            float v = inst.param (p);
            std::snprintf (full_.randDetune.state.valueText, sizeof full_.randDetune.state.valueText, "%.1f c", v);
        }
    }

    void handle (const ui::Change& ch, UiStack& stack, IInstanceView& inst)
    {
        if (ch.kind == ui::ChangeKind::None)
            return;

        if (ch.id == kClose)
        {
            stack.back();
            return;
        }

        if (ch.id == kListBackdrop)
        {
            stack.back();
            return;
        }

        if (ch.id == kList && ch.kind == ui::ChangeKind::Index)
        {
            if (listParam_ >= 0)
            {
                writePlain (inst, listParam_, ch.index);
                pull (inst);
            }
            stack.back();
            return;
        }

        // Check if row tab was clicked on the grid
        if (ch.id >= kRowTabBase && ch.id < kRowTabBase + 6)
        {
            full_.grid.activeRow = ch.id - kRowTabBase;
            pull (inst);
            return;
        }

        // Check step / row selectors in header
        if (ch.id == kStepSelect && ch.kind == ui::ChangeKind::Index)
        {
            full_.grid.selectedStep = std::clamp (ch.index, 0, 7);
            return;
        }
        if (ch.id == kRowSelect && ch.kind == ui::ChangeKind::Index)
        {
            full_.grid.activeRow = std::clamp (ch.index, 0, 5);
            return;
        }

        const Item* it = item (ch.id);
        if (! it || it->param < 0)
            return;

        if (it->type == T::Stepper && ch.kind == ui::ChangeKind::Request)
        {
            const synthyboy::ParamInfo& pi = info (it->param);
            if (pi.numChoices > 0)
            {
                listParam_ = it->param;
                list_.state.items = pi.choices;
                list_.state.count = pi.numChoices;
                list_.state.current = static_cast<int> (std::lround (inst.param (it->param)));
                list_.showCurrent();
                stack.openDetail (kPageList);
                return;
            }
        }

        switch (ch.kind)
        {
            case ui::ChangeKind::Value:
                if (it->type == T::Knob)
                    writeNorm (inst, it->param, ch.value);
                else if (it->type == T::GridStep)
                {
                    // Convert -1.0 .. 1.0 to -100 .. 100
                    writePlain (inst, it->param, ch.value * 100.0f);
                }
                else
                    writePlain (inst, it->param, ch.value);
                break;
            case ui::ChangeKind::Index:
                writePlain (inst, it->param, ch.index);
                break;
            default:
                break;
        }
        pull (inst);
    }

    void publishQGroup (IInstanceView& inst)
    {
        const int g = full_.activeGroup();
        if (g == 0) // Step QGroup: [Active step value, Step select, Length, Row select]
        {
            static const char* const kRows[6] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
            const int s = full_.grid.selectedStep;
            const int r = full_.grid.activeRow;
            const std::string stepKey = "voicectl.s" + std::to_string (s + 1) + "." + kRows[r];
            qTargets_[0] = indexOfKey (stepKey);
            qTargets_[1] = -1; // hardware stepper
            qTargets_[2] = indexOfKey ("voicectl.length");
            qTargets_[3] = -1; // hardware stepper
        }
        else if (g == 1) // Random QGroup: [Rand Pan, Rand Detune, Rand Cutoff, Rand Env]
        {
            qTargets_[0] = indexOfKey ("voicectl.rand.pan");
            qTargets_[1] = indexOfKey ("voicectl.rand.detune");
            qTargets_[2] = indexOfKey ("voicectl.rand.cutoff");
            qTargets_[3] = indexOfKey ("voicectl.rand.env");
        }
        else if (g == 2) // Scaling QGroup: [Scale Env, Scale Lfo, Scale Mode, Detune 10x]
        {
            qTargets_[0] = indexOfKey ("voicectl.scale.env");
            qTargets_[1] = indexOfKey ("voicectl.scale.lfo");
            qTargets_[2] = indexOfKey ("voicectl.scale.lfo_mode");
            qTargets_[3] = indexOfKey ("voicectl.rand.detune10x");
        }
        else // Targets QGroup: [Osc A, Osc B, Osc C, Osc Sub]
        {
            qTargets_[0] = indexOfKey ("voicectl.osc.a");
            qTargets_[1] = indexOfKey ("voicectl.osc.b");
            qTargets_[2] = indexOfKey ("voicectl.osc.c");
            qTargets_[3] = indexOfKey ("voicectl.osc.sub");
        }
        inst.setQGroup (qTargets_, 4);
    }

    void syncPages (const UiStack& stack, IInstanceView& inst)
    {
        const int top = stack.top();
        pageTop_ = top;
        inst.setUiFullOpen (! stack.isMain());
    }

    ui::Component* topComponent() noexcept
    {
        return pageTop_ == kPageList ? static_cast<ui::Component*> (&list_) : static_cast<ui::Component*> (&full_);
    }

    ui::VoiceScreen full_;
    ui::ListPopup list_;
    std::vector<Item> items_;

    int w_ = kCanvasW, h_ = kCanvasH;
    int stackDepth_ = 0;
    int pageTop_ = -1;
    int listParam_ = -1;

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    int qTargets_[4] = { -1, -1, -1, -1 };
};

}} // namespace synthyboy_mpc::overlay
