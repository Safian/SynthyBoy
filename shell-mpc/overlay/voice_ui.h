// SynthyBoy - MPC overlay: the VOICE UI (the voice control), the page of the Voice tab (the plugin draws the whole tab, page_view.h): the 8 step sequence with its six rows, the random spreads, the scaling and the
// targets (shell-mpc/ui/voice_screen.h, voice_grid.h) wired to the engine's parameters voicectl.*. One object per (page view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls
// update () / touch () / tick () / draw (), with the instance as IInstanceView. Docs: docs/MPC.md "Master and Voice UI".
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The grid: a finger draws the bars of the active row; a stroke over several columns sets every column it passes (the grid hands the skipped columns over: VoiceGrid::takePending). The column of the step a voice plays
// is lit (LiveValues::voiceStep: each of the newest voices plays the step (note number - 1) modulo the length), the selected step has its frame, and under the grid every row's value of the selected step is written.
// Q groups: STEP (the step, the row (both virtual targets of the router), the length and the value of the selected step), RANDOM, SCALING, TARGETS.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../q_router.h"
#include "../ui/list.h"
#include "../ui/theme.h"
#include "../ui/voice_screen.h"
#include "curve_source.h"
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
    enum Page { kPageList = 15 };
    static constexpr int kVirtualStep = 0, kVirtualRow = 1;   // the virtual Q targets of the group STEP

    enum Id : int
    {
        kLength = 2, kStepSelect = 3, kRowSelect = 4, kStepValue = 5,
        // Step Grid: 6 rows * 8 steps = 48 values -> 100 .. 147
        kGridBase = 100,
        // Row tabs: 150 .. 155
        kRowTabBase = 150,
        kRandPan = 200, kRandDetune, kRandCutoff, kRandEnv, kRandDetune10x,
        kScaleEnv = 210, kScaleLfo, kScaleLfoMode,
        kOscA = 220, kOscB, kOscC, kOscSub, kOscNoise,
        kList = 700, kListBackdrop = 701
    };

    enum class T : uint8_t { Knob, Number, Toggle, Stepper, GridStep, Inert };

    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        T type = T::Inert;
        std::string key;
        int param = -1;
        int row = -1;
        int step = -1;
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    VoiceUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        configure();
    }

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

    /** Reads the instance: every value, the step a voice plays, the virtual Q targets (step, row), the pages of the stack, the Q group. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        (void) nowMs;
        pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        // the host writes the virtual targets: the Q-Link of the step and of the row
        float x;
        if (inst.takeQVirtual (kVirtualStep, x))
            full_.grid.selectedStep = std::clamp (static_cast<int> (std::lround (x * 7.0f)), 0, 7);
        if (inst.takeQVirtual (kVirtualRow, x))
            full_.grid.activeRow = std::clamp (static_cast<int> (std::lround (x * 5.0f)), 0, 5);
        pull (inst);

        // the steps the newest voices play, lit in the grid
        uint8_t mask = 0;
        if (in.live != nullptr)
            for (int v = 0; v < synthyboy::LiveValues::kReadVoices; ++v)
            {
                const int s = static_cast<int> (std::lround (in.live->voiceStep[v]));
                if (in.live->voiceStep[v] >= 0.0f && s < 8 && s < std::max (1, full_.grid.length))
                    mask = static_cast<uint8_t> (mask | (1u << s));
            }
        full_.grid.voiceStepMask = mask;

        // the step and the row of the header: the selectors, the value of the step
        char stepBuf[32];
        std::snprintf (stepBuf, sizeof stepBuf, "STEP %d", full_.grid.selectedStep + 1);
        full_.qStepSelect.state.count = 8;
        full_.qStepSelect.state.index = full_.grid.selectedStep;
        full_.qStepSelect.setItem (stepBuf);
        full_.qRowSelect.state.count = 6;
        full_.qRowSelect.state.index = full_.grid.activeRow;
        full_.qRowSelect.setItem (ui::VoiceGrid::rowName (full_.grid.activeRow));
        const float sv = full_.grid.value (full_.grid.activeRow, full_.grid.selectedStep) * 100.0f;
        full_.stepValue.state.value = std::round (sv);
        full_.stepValue.setLabel (ui::VoiceGrid::rowName (full_.grid.activeRow));
        inst.setQVirtual (kVirtualStep, static_cast<float> (full_.grid.selectedStep) / 7.0f);
        inst.setQVirtual (kVirtualRow, static_cast<float> (full_.grid.activeRow) / 5.0f);

        inst.setUiFullOpen (true);
        publishQGroup (inst);
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
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
        {
            handle (gesture_->touchMove (x, y), stack, inst);
            ui::Change extra;
            while (gesture_ == &full_ && full_.grid.takePending (extra))   // the columns a fast stroke passed over
                handle (extra, stack, inst);
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

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    void forgetQGroup() noexcept
    {
        full_.setActiveGroup (0);
        qSlots_.forget();
    }
    int topPage() const noexcept { return pageTop_; }

    ui::Change turnQ (int slot, double delta, UiStack& stack, IInstanceView& inst)
    {
        const ui::Change ch = full_.turnQ (slot, delta);
        handle (ch, stack, inst);
        return ch;
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        page::ground (sb);
        full_.draw (sb);
        if (pageTop_ == kPageList)
            list_.draw (sb);
    }

    // ---- access for the tests
    ui::VoiceScreen& screen() noexcept { return full_; }
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
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }

private:
    void bind (int id, ui::Component* c, T type, const std::string& key, int row = -1, int step = -1)
    {
        Item it;
        it.id = id; it.c = c; it.type = type;
        it.key = key;
        it.param = uibind::indexOfKey (key);
        it.row = row; it.step = step;
        if (c != nullptr)
            c->setId (id);
        items_.push_back (it);
    }

    void configure()
    {
        items_.clear();
        bind (kLength, &full_.length, T::Number, "voicectl.length");
        bind (kStepSelect, &full_.qStepSelect, T::Inert, "");
        bind (kRowSelect, &full_.qRowSelect, T::Inert, "");
        bind (kStepValue, &full_.stepValue, T::Inert, "");

        static const char* const kRows[6] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
        full_.grid.baseId = kGridBase;
        full_.grid.rowTabBaseId = kRowTabBase;
        for (int r = 0; r < 6; ++r)
            for (int s = 0; s < 8; ++s)
                bind (kGridBase + r * 8 + s, &full_.grid, T::GridStep, "voicectl.s" + std::to_string (s + 1) + "." + kRows[r], r, s);

        full_.scaleLfoMode.listable = true;
        bind (kRandPan, &full_.randPan, T::Knob, "voicectl.rand.pan");
        bind (kRandDetune, &full_.randDetune, T::Knob, "voicectl.rand.detune");
        bind (kRandCutoff, &full_.randCutoff, T::Knob, "voicectl.rand.cutoff");
        bind (kRandEnv, &full_.randEnv, T::Knob, "voicectl.rand.env");
        bind (kRandDetune10x, &full_.randDetune10x, T::Toggle, "voicectl.rand.detune10x");
        bind (kScaleEnv, &full_.scaleEnv, T::Knob, "voicectl.scale.env");
        bind (kScaleLfo, &full_.scaleLfo, T::Knob, "voicectl.scale.lfo");
        bind (kScaleLfoMode, &full_.scaleLfoMode, T::Stepper, "voicectl.scale.lfo_mode");
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
            const synthyboy::ParamInfo& p = uibind::info (it.param);
            switch (it.type)
            {
                case T::Knob:
                {
                    ui::Knob* kn = static_cast<ui::Knob*> (it.c);
                    kn->state.value = kn->state.modValue = uibind::normOf (it.param, val);
                    kn->state.defaultValue = uibind::normOf (it.param, p.def);
                    uibind::formatValue (p, val, kn->state.valueText, sizeof kn->state.valueText);
                    break;
                }
                case T::Number:
                {
                    ui::NumberField* nf = static_cast<ui::NumberField*> (it.c);
                    nf->state.value = val;
                    nf->state.min = p.min; nf->state.max = p.max; nf->state.step = 1.0; nf->state.defaultValue = p.def; nf->state.pxPerStep = 8;
                    if (it.id == kLength)
                        full_.grid.length = static_cast<int> (std::lround (val));
                    break;
                }
                case T::Toggle:
                    static_cast<ui::ToggleIcon*> (it.c)->state.on = val > 0.5f;
                    break;
                case T::Stepper:
                {
                    ui::Stepper* st = static_cast<ui::Stepper*> (it.c);
                    st->state.count = p.numChoices > 0 ? p.numChoices : 2;
                    st->state.index = std::clamp (static_cast<int> (std::lround (val)), 0, st->state.count - 1);
                    if (p.numChoices > 0)
                        st->setItem (p.choices[st->state.index]);
                    break;
                }
                case T::GridStep:
                    if (it.row >= 0 && it.step >= 0 && ! (gestureDown_ && gesture_ == &full_ && it.row == full_.grid.activeRow))   // a bar under the finger shows what the finger draws
                        full_.grid.setValue (it.row, it.step, val * 0.01f);
                    break;
                case T::Inert:
                    break;
            }
        }
    }

    void handle (const ui::Change& ch, UiStack& stack, IInstanceView& inst)
    {
        if (ch.kind == ui::ChangeKind::None)
            return;
        if (ch.id == kListBackdrop)
        {
            stack.back();
            return;
        }
        if (ch.id == kList && ch.kind == ui::ChangeKind::Index)
        {
            if (listParam_ >= 0)
                uibind::writePlain (inst, listParam_, ch.index);
            stack.back();
            return;
        }
        if (ch.id >= kRowTabBase && ch.id < kRowTabBase + 6)   // a row tab of the grid
        {
            full_.grid.activeRow = ch.id - kRowTabBase;
            return;
        }
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
        if (ch.id == kStepValue && ch.kind == ui::ChangeKind::Index)   // the value of the selected step as a number
        {
            const int idx = stepParam (full_.grid.activeRow, full_.grid.selectedStep);
            if (idx >= 0)
                uibind::writePlain (inst, idx, std::clamp (ch.value, -100.0, 100.0));
            return;
        }
        const Item* it = item (ch.id);
        if (! it || it->param < 0)
            return;
        if (it->type == T::Stepper && ch.kind == ui::ChangeKind::Request)
        {
            const synthyboy::ParamInfo& pi = uibind::info (it->param);
            if (pi.numChoices > 0)
            {
                listParam_ = it->param;
                list_.state.items = pi.choices;
                list_.state.count = pi.numChoices;
                list_.state.current = static_cast<int> (std::lround (inst.param (it->param)));
                list_.showCurrent();
                stack.openDetail (kPageList);
                pageTop_ = kPageList;
            }
            return;
        }
        switch (ch.kind)
        {
            case ui::ChangeKind::Value:
                if (it->type == T::Knob)
                    uibind::writeNorm (inst, it->param, ch.value);
                else if (it->type == T::GridStep)
                    uibind::writePlain (inst, it->param, ch.value * 100.0);   // -1 .. +1 of the grid is -100 .. +100 of the parameter
                else
                    uibind::writePlain (inst, it->param, ch.value);
                break;
            case ui::ChangeKind::Index:
                uibind::writePlain (inst, it->param, ch.index);
                break;
            default:
                break;
        }
    }

    static int stepParam (int row, int step)
    {
        static const char* const kRows[6] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
        return uibind::indexOfKey ("voicectl.s" + std::to_string (std::clamp (step, 0, 7) + 1) + "." + kRows[std::clamp (row, 0, 5)]);
    }

    /** The groups of the page are its six blocks in reading order (STEP, RANDOM, RAND 10x, SCALING, TARGETS, NOISE): the active one is on Q 1 .. 4, the next three (wrapping) on 5 .. 16. */
    void publishQGroup (IInstanceView& inst)
    {
        const int g = full_.activeGroup();
        constexpr int kN = ui::VoiceScreen::kMaxGroups;
        const bool list = pageTop_ == kPageList;
        qSlots_.publish (inst, list ? 0 : kN, g, [this] (int k, int* t)
        {
            if (k == 0)   // STEP: the step and the row (virtual targets), the length, the value of the selected step
            {
                t[0] = QRouter::encode (kVirtualStep, QRouter::kVirtual);
                t[1] = QRouter::encode (kVirtualRow, QRouter::kVirtual);
                t[2] = uibind::indexOfKey ("voicectl.length");
                t[3] = stepParam (full_.grid.activeRow, full_.grid.selectedStep);
            }
            else if (k == 1)
            {
                t[0] = uibind::indexOfKey ("voicectl.rand.pan"); t[1] = uibind::indexOfKey ("voicectl.rand.detune");
                t[2] = uibind::indexOfKey ("voicectl.rand.cutoff"); t[3] = uibind::indexOfKey ("voicectl.rand.env");
            }
            else if (k == 2)
                t[0] = uibind::indexOfKey ("voicectl.rand.detune10x");
            else if (k == 3)
            {
                t[0] = uibind::indexOfKey ("voicectl.scale.env"); t[1] = uibind::indexOfKey ("voicectl.scale.lfo"); t[2] = uibind::indexOfKey ("voicectl.scale.lfo_mode");
            }
            else if (k == 4)
            {
                t[0] = uibind::indexOfKey ("voicectl.osc.a"); t[1] = uibind::indexOfKey ("voicectl.osc.b");
                t[2] = uibind::indexOfKey ("voicectl.osc.c"); t[3] = uibind::indexOfKey ("voicectl.osc.sub");
            }
            else if (k == 5)
                t[0] = uibind::indexOfKey ("voicectl.osc.noise");
        });
        for (int i = 0; i < kN; ++i)
            full_.groups[i].setBlock (list ? -1 : QRouter::blockOfGroup (kN, g, i));
    }

    ui::Component* topComponent() noexcept
    {
        return pageTop_ == kPageList ? static_cast<ui::Component*> (&list_) : static_cast<ui::Component*> (&full_);
    }

    ui::VoiceScreen full_;
    ui::ListPopup list_;
    std::vector<Item> items_;

    int w_ = kCanvasW, h_ = kCanvasH;
    int pageTop_ = 0, listParam_ = -1;

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    QSlots qSlots_;
};

}} // namespace synthyboy_mpc::overlay
