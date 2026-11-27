// SynthyBoy Voice page. The host owns the native controls and readouts.
// The overlay publishes Q-Link bindings and draws live sounding-step indicators.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../native_voice_controls.h"
#include "native_marker_covers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "../q_router.h"
#include "../ui/theme.h"
#include "../ui/voice_screen.h"
#include "instance_view.h"
#include "page_view.h"
#include "q_publish.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class VoiceUi
{
public:
    static constexpr int kElementPage = 10;          // the page of the Voice tab
    static constexpr int kElementVoice = kElementPage;
    static constexpr int kVirtualStep = 0, kVirtualRow = 1;   // the virtual Q targets of the group STEP

    enum Id : int
    {
        kLength = 2, kStepSelect = 3, kRowSelect = 4, kStepValue = 5,
        // Step Grid: 6 rows * 8 steps = 48 values -> 100 .. 147
        kGridBase = 100,
        kRandPan = 200, kRandDetune, kRandCutoff, kRandEnv, kRandDetune10x,
        kScaleEnv = 210, kScaleLfo, kScaleLfoMode,
        kOscA = 220, kOscB, kOscC, kOscSub, kOscNoise
    };

    enum class T : uint8_t { Knob, Number, Toggle, List, GridStep, Inert };

    struct Item
    {
        int id = 0;
        ui::ControlModel* c = nullptr;
        T type = T::Inert;
        std::string key;
        int param = -1;
        float presentedValue = std::numeric_limits<float>::quiet_NaN();
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    VoiceUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        configure();
    }

    void setCanvas (int w, int)
    {
        w_ = w;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        full_.place (tab);
    }

    /** Reads the instance: every value, the step a voice plays, the virtual Q targets (step, row), the pages of the stack, the Q group. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        (void) nowMs;
        pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        const bool native = inst.nativeVoiceSequenceControls() && pageTop_ == 0;
        sequenceAvailable_ = native;
        const uint64_t preset = inst.stateGeneration();
        if (gestureDown_ && gesturePreset_ != preset)
            dropGesture();

        // the host writes the virtual targets: the Q-Link of the step and of the row
        float x;
        if (inst.takeQVirtual (kVirtualStep, x))
            selectStep (std::lround (x * (ui::VoiceGrid::kNumSteps - 1)));
        if (inst.takeQVirtual (kVirtualRow, x))
            selectRow (std::lround (x * (ui::VoiceGrid::kNumRows - 1)));
        if (native)
            takeNativeSequence (inst);
        pull (inst);

        // the steps the newest voices play, lit in the grid
        uint8_t mask = 0;
        if (in.live != nullptr)
            for (int v = 0; v < synthyboy::LiveValues::kReadVoices; ++v)
            {
                const int s = static_cast<int> (std::lround (in.live->voiceStep[v]));
                if (in.live->voiceStep[v] >= 0.0f && s < 8 && s < std::max (1, full_.grid.state.length))
                    mask = static_cast<uint8_t> (mask | (1u << s));
            }
        full_.grid.state.voiceStepMask = mask;

        // the step and the row of the header: the selectors, the value of the step
        const int step = full_.grid.state.selectedStep, row = full_.grid.state.activeRow;
        full_.qStepSelect.state.count = ui::VoiceGrid::kNumSteps;
        full_.qStepSelect.state.index = step;
        full_.rowList.state.index = row;
        full_.stepValue.state.value = inst.param (stepParam (row, step));
        inst.setQVirtual (kVirtualStep, static_cast<float> (full_.grid.state.selectedStep) / 7.0f);
        inst.setQVirtual (kVirtualRow, static_cast<float> (full_.grid.state.activeRow) / 5.0f);

        inst.setNativeVoiceState (sequenceState (inst));
        inst.setUiFullOpen (pageTop_ != 0);
        publishQGroup (inst);
    }

    bool covers (int x, int y) const noexcept
    {
        if (! ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y))
            return false;
        if (pageTop_ == 0 && full_.isNativeControl (x, y))
            return false;
        return true;
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
            gesturePreset_ = inst.stateGeneration();
            handle (top->touchDown (x, y, nowMs), stack, inst);
            inst.setNativeVoiceState (sequenceState (inst));
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (gesturePreset_ != inst.stateGeneration())
        {
            dropGesture();
            return true;
        }
        if (ev.kind == TouchEvent::Move)
        {
            const ui::Change moved = gesture_->touchMove (x, y);
            handle (moved, stack, inst);
        }
        else
        {
            handle (gesture_->touchUp (x, y), stack, inst);
            gestureDown_ = false;
            gesture_ = nullptr;
        }
        inst.setNativeVoiceState (sequenceState (inst));
        return true;
    }

    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst);
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
        inst.setNativeVoiceState (sequenceState (inst));
        (void) nowMs;
        return true;
    }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    void forgetQGroup() noexcept
    {
        full_.setActiveGroup (0);
        qSlots_.forget();
    }
    int topPage() const noexcept { return pageTop_; }
    void leftTab (IInstanceView& inst) noexcept { inst.setNativeVoiceState ({}); }

    ui::Change turnQ (int slot, double delta, UiStack& stack, IInstanceView& inst)
    {
        if (pageTop_ != 0)
            return ui::Change::none();
        const ui::Change ch = full_.turnQ (slot, delta);
        handle (ch, stack, inst);
        inst.setNativeVoiceState (sequenceState (inst));
        return ch;
    }

    void setHostBackground (bool) noexcept {}
    bool hostBackground() const noexcept { return true; }

    void draw (gpu::SceneBuilder& sb) const
    {
        if (pageTop_ == 0)
        {
            drawNativeMarkerCovers (sb, "voice");
            full_.draw (sb);
        }
    }

    // ---- access for the tests
    ui::VoiceScreen& screen() noexcept { return full_; }
    const std::vector<Item>& items() const noexcept { return items_; }
    const Item* item (int id) const noexcept
    {
        for (const Item& it : items_)
            if (it.id == id)
                return &it;
        return nullptr;
    }
    int activeQ() const noexcept { return full_.activeGroup(); }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }

private:
    void bind (int id, ui::ControlModel* c, T type, const std::string& key)
    {
        Item it;
        it.id = id; it.c = c; it.type = type;
        it.key = key;
        it.param = uibind::indexOfKey (key);
        if (c != nullptr)
        {
            c->setId (id);
            c->setParam (it.param);
        }
        items_.push_back (it);
    }

    void configure()
    {
        items_.clear();
        bind (kLength, &full_.length, T::Number, "voicectl.length");
        bind (kStepSelect, &full_.qStepSelect, T::Inert, "");
        bind (kRowSelect, &full_.rowList, T::Inert, "");
        bind (kStepValue, &full_.stepValue, T::Inert, "");

        static const char* const kRows[6] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
        for (int row = 0; row < ui::VoiceGrid::kNumRows; ++row)
            for (int step = 0; step < ui::VoiceGrid::kNumSteps; ++step)
            {
                bind (kGridBase + row * ui::VoiceGrid::kNumSteps + step, nullptr, T::GridStep,
                      "voicectl.s" + std::to_string (step + 1) + "." + kRows[row]);
                gridParams_[row][step] = items_.back().param;
            }

        bind (kRandPan, &full_.randPan, T::Knob, "voicectl.rand.pan");
        bind (kRandDetune, &full_.randDetune, T::Knob, "voicectl.rand.detune");
        bind (kRandCutoff, &full_.randCutoff, T::Knob, "voicectl.rand.cutoff");
        bind (kRandEnv, &full_.randEnv, T::Knob, "voicectl.rand.env");
        bind (kRandDetune10x, &full_.randDetune10x, T::Toggle, "voicectl.rand.detune10x");
        bind (kScaleEnv, &full_.scaleEnv, T::Knob, "voicectl.scale.env");
        bind (kScaleLfo, &full_.scaleLfo, T::Knob, "voicectl.scale.lfo");
        bind (kScaleLfoMode, &full_.scaleLfoMode, T::List, "voicectl.scale.lfo_mode");
        bind (kOscA, &full_.oscA, T::Toggle, "voicectl.osc.a");
        bind (kOscB, &full_.oscB, T::Toggle, "voicectl.osc.b");
        bind (kOscC, &full_.oscC, T::Toggle, "voicectl.osc.c");
        bind (kOscSub, &full_.oscSub, T::Toggle, "voicectl.osc.sub");
        bind (kOscNoise, &full_.oscNoise, T::Toggle, "voicectl.osc.noise");

        for (int group = 0; group < ui::VoiceScreen::kMaxGroups; ++group)
            for (int slot = 0; slot < ui::QGroup::kSlots; ++slot)
            {
                const auto* member = full_.groups[group].member (slot);
                const auto* binding = member ? item (member->id()) : nullptr;
                groupTargets_[group][slot] = binding ? binding->param : -1;
            }
        groupTargets_[0][0] = QRouter::encode (kVirtualStep, QRouter::kVirtual);
        groupTargets_[0][1] = QRouter::encode (kVirtualRow, QRouter::kVirtual);
    }

    void pull (IInstanceView& inst)
    {
        for (Item& it : items_)
        {
            if (it.param < 0 || ! it.c)
                continue;
            const float val = inst.param (it.param);
            if (! resyncAfterInput_ && it.presentedValue == val)
                continue;
            it.presentedValue = val;
            const synthyboy::ParamInfo& p = uibind::info (it.param);
            switch (it.type)
            {
                case T::Knob:
                {
                    ui::Knob* kn = static_cast<ui::Knob*> (it.c);
                    kn->state.value = kn->state.modValue = uibind::normOf (it.param, val);
                    kn->state.defaultValue = uibind::normOf (it.param, p.def);
                    break;
                }
                case T::Number:
                {
                    ui::NumberField* nf = static_cast<ui::NumberField*> (it.c);
                    nf->state.value = val;
                    nf->state.min = p.min; nf->state.max = p.max; nf->state.step = 1.0; nf->state.defaultValue = p.def; nf->state.pxPerStep = 8;
                    if (it.id == kLength)
                        full_.grid.state.length = static_cast<int> (std::lround (val));
                    break;
                }
                case T::Toggle:
                    static_cast<ui::ToggleIcon*> (it.c)->state.on = val > 0.5f;
                    break;
                case T::List:
                {
                    ui::LedList* ll = static_cast<ui::LedList*> (it.c);
                    ll->setOptions (p.choices, p.numChoices > 0 ? p.numChoices : 0);
                    ll->state.index = std::clamp (static_cast<int> (std::lround (val)), 0, std::max (0, ll->state.count - 1));
                    break;
                }
                case T::GridStep:
                case T::Inert:
                    break;
            }
        }
        resyncAfterInput_ = false;
    }

    void handle (const ui::Change& ch, UiStack&, IInstanceView& inst)
    {
        // Local controls can edit their own presentation before the host confirms a write.
        resyncAfterInput_ = true;
        if (ch.kind == ui::ChangeKind::None)
            return;
        if (ch.id == kStepSelect && ch.kind == ui::ChangeKind::Index)
        {
            selectStep (ch.index);
            return;
        }
        if (ch.id == kRowSelect && ch.kind == ui::ChangeKind::Index)
        {
            selectRow (ch.index);
            return;
        }
        if (ch.id == kStepValue && ch.kind == ui::ChangeKind::Index)   // the value of the selected step as a number
        {
            const int idx = stepParam (full_.grid.state.activeRow, full_.grid.state.selectedStep);
            if (idx >= 0)
                uibind::writePlain (inst, idx, std::clamp (ch.value, -100.0, 100.0));
            return;
        }
        const Item* it = item (ch.id);
        if (! it || it->param < 0)
            return;
        switch (ch.kind)
        {
            case ui::ChangeKind::Value:
                if (it->type == T::Knob)
                    uibind::writeNorm (inst, it->param, ch.value);
                else
                    uibind::writePlain (inst, it->param, ch.value);
                break;
            case ui::ChangeKind::Index:
                uibind::writePlain (inst, it->param, it->type == T::Number ? ch.value : ch.index);
                break;
            default:
                break;
        }
    }

    void selectRow (int row) { full_.grid.state.activeRow = std::clamp (row, 0, ui::VoiceGrid::kNumRows - 1); }
    void selectStep (int step) { full_.grid.state.selectedStep = std::clamp (step, 0, ui::VoiceGrid::kNumSteps - 1); }

    nativeui::voice::ViewState sequenceState (const IInstanceView& inst) const
    {
        if (! sequenceAvailable_ || pageTop_ != 0)
            return {};
        const int row = full_.grid.state.activeRow, step = full_.grid.state.selectedStep;
        return { true, row, step, stepParam (row, step), inst.stateGeneration() };
    }

    void takeNativeSequence (IInstanceView& inst)
    {
        namespace n = nativeui::voice;
        static_assert (n::kRows == ui::VoiceGrid::kNumRows && n::kSteps == ui::VoiceGrid::kNumSteps);
        const auto before = sequenceState (inst);
        float value;
        if (inst.takeNativeVoice (before, n::kRow, value))
        {
            selectRow (std::lround (value * (n::kRows - 1)));
            full_.setActiveGroup (0);
        }
        if (inst.takeNativeVoice (before, n::kStep, value))
        {
            selectStep (std::lround (value * (n::kSteps - 1)));
            full_.setActiveGroup (0);
        }
        // Selection wins over a write queued for the previously displayed cell.
        const auto current = sequenceState (inst);
        if (inst.takeNativeVoice (current, n::kValue, value) && current.target >= 0 && current.preset == inst.stateGeneration())
        {
            full_.setActiveGroup (0);
            const float plain = static_cast<float> (std::lround (synthyboy::fromNormalised (uibind::info (current.target), value)));
            if (inst.param (current.target) != plain)
                uibind::writePlain (inst, current.target, plain);
        }
    }

    int stepParam (int row, int step) const noexcept
    {
        return gridParams_[std::clamp (row, 0, ui::VoiceGrid::kNumRows - 1)][std::clamp (step, 0, ui::VoiceGrid::kNumSteps - 1)];
    }

    /** The groups of the page are four, in reading order (STEP, RANDOM, SCALING with the 10x switch, TARGETS): the active one is on Q 1 .. 4, the next three (wrapping) on 5 .. 16. */
    void publishQGroup (IInstanceView& inst)
    {
        const int g = full_.activeGroup();
        constexpr int kN = ui::VoiceScreen::kMaxGroups;
        const bool list = pageTop_ != 0;
        qSlots_.publish (inst, list ? 0 : kN, g, [this] (int k, int* t)
        {
            std::copy_n (groupTargets_[k], ui::QGroup::kSlots, t);
            if (k == 0)
                t[3] = stepParam (full_.grid.state.activeRow, full_.grid.state.selectedStep);
        });
        for (int i = 0; i < kN; ++i)
            full_.groups[i].setBlock (list ? -1 : QRouter::blockOfGroup (kN, g, i));
    }

    ui::Component* topComponent() noexcept
    {
        return pageTop_ == 0 ? &full_ : nullptr;
    }

    ui::VoiceScreen full_;
    bool sequenceAvailable_ = false;
    std::vector<Item> items_;
    bool resyncAfterInput_ = false;
    int groupTargets_[ui::VoiceScreen::kMaxGroups][ui::QGroup::kSlots] {};
    int gridParams_[ui::VoiceGrid::kNumRows][ui::VoiceGrid::kNumSteps] {};

    uint64_t gesturePreset_ = 0;
    int w_ = kCanvasW;
    int pageTop_ = 0;

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    QSlots qSlots_;
};

}} // namespace synthyboy_mpc::overlay
