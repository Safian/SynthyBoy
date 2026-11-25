// SynthyBoy - MPC overlay: the MASTER UI, the page of the Master tab (the plugin draws the whole tab, page_view.h): the blocks of the master section (shell-mpc/ui/voice_master.h) wired to the engine's parameters
// master.*: OUTPUT (volume, stereo spread, routing, voice override, the meters), PITCH (tune, transpose, bend range), VOICES (polyphony, legato, same note, steal), GLIDE (portamento), and on the second side
// SCALE, TUNING and MPE. One object per (page view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw (), with the
// instance as IInstanceView. Docs: docs/MPC.md "Master and Voice UI".
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Native widgets own value entry and menus; the overlay retains parameter formatting and Q/state adapters.
// The meter is the engine's own master peak (LiveValues::stripPeak[9]: what leaves the engine), the voice bar the number of sounding voices. A touch in a block makes it the Q group.
#pragma once

#include "native_marker_covers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../native_master_controls.h"
#include "../ui/theme.h"
#include "../ui/voice_master.h"
#include "instance_view.h"
#include "page_view.h"
#include "q_publish.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class MasterUi
{
public:
    static constexpr int kElementPage = 9;           // the page of the Master tab
    static constexpr int kElementMaster = kElementPage;

    enum Id : int
    {
        // OUTPUT
        kVolume = 100, kSpread, kStereoRouting, kVoiceOverride,
        // PITCH
        kTune = 110, kTranspose, kBendUp, kBendDown, kBendLink,
        // VOICES
        kPolyphony = 120, kLegato, kPolyLegato, kPhaseLegato, kSameNote, kStealPriority, kVelocityTrack,
        // GLIDE
        kPortaTime = 130, kPortaSlope, kPortaAlways, kPortaScaled,
        // SCALE, TUNING
        kScaleType = 140, kScaleKey, kScaleMode, kTuningA4, kTuningScale, kMtsEsp, kMtsNoteOn,
        // MPE
        kMpeOn = 150, kMpeBend, kMpeMacro, kMpeYModwheel, kMpeBendToX, kMpeYBipolar,
        // the plugin-wide pad pressure layer (shell parameters of the plugin, not of the engine: no Q group)
        kPadPressure = 160, kPadAmount, kLimiter, kNoSmoothing, kOversample4x,
    };

    enum class T : uint8_t { Knob, Number, Toggle, Stepper, List, Inert };

    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        T type = T::Inert;
        std::string key;
        int param = -1;
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    MasterUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        configure();
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        full_.place (tab);
    }

    /** Reads the instance: every value, the meters, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        float panel;
        const auto view = nativeView();
        if (view != nativeui::master::Hidden && inst.takeNativeMasterPanel (view, panel))
        {
            full_.setSide (panel >= 0.75f ? ui::MasterScreen::SideTuning : ui::MasterScreen::SideMain);
            qSlots_.forget();
        }
        if (nativeView() == nativeui::master::Tuning && inst.masterScaleInputActive())
        {
            const int root = inst.takeMasterScaleRoot();
            if (root >= 0)
                uibind::writePlain (inst, item (kScaleKey)->param, static_cast<float> (root));
        }
        pull (inst);
        full_.setBendLinked (inst.param (bendLinkIdx_) >= 0.5f);   // what does not apply is hidden (not dimmed): BEND DOWN while LINK is on, MPE BEND and BEND TO X while MPE is off
        full_.setMpeOn (inst.param (mpeOnIdx_) >= 0.5f);
        full_.clip.state.active = inst.pollMasterClip (nowMs);
        full_.voiceMeter.activeVoices = in.live != nullptr ? in.live->activeVoices : 0;
        full_.voiceMeter.polyphony = std::max (1, static_cast<int> (std::lround (inst.param (polyphonyIdx_))));
        if (lastInstance_ != &inst)
        {
            full_.meters.holdL = full_.meters.holdR = 0.0f;
            lastMs_ = 0.0;
            lastInstance_ = &inst;
        }
        full_.meters.peakL = in.live != nullptr ? in.live->masterPeakL : 0.0f;
        full_.meters.peakR = in.live != nullptr ? in.live->masterPeakR : 0.0f;
        full_.meters.updateHold (lastMs_ > 0.0 ? static_cast<float> (nowMs - lastMs_) * 0.001f : 0.0f);
        lastMs_ = nowMs;
        {   // the output limiter of the plugin (docs/MPC.md "Output limiter"): a shell setting, shown on or off as the stage runs it
            bool on = false;
            const bool have = inst.outputLimiter (on);
            full_.outputLimiter.setEnabled (false);
            full_.outputLimiter.state.on = have && on;
        }
        {   // the pad pressure layer of the plugin (docs/MPC.md "Pad pressure"): off by default, the presets carry their own route
            bool on = false;
            float percent = 35.0f;
            const bool have = inst.padPressure (on, percent);
            full_.padPressure.setEnabled (false);
            full_.padAmount.setEnabled (false);
            full_.padPressure.state.on = on;
            full_.padAmount.state.min = 0.0; full_.padAmount.state.max = 100.0; full_.padAmount.state.step = 1.0; full_.padAmount.state.pxPerStep = 3;
            full_.padAmount.state.defaultValue = 35.0;
            full_.padAmount.state.value = std::round (percent);
            full_.setPadPressureOn (have && on);   // PAD % applies only while PAD PRESS is on: hidden (not dimmed) otherwise
        }
        full_.portaCurve.slope = inst.param (slopeIdx_);
        inst.setUiFullOpen (pageTop_ != 0);
        if (nativeView() == nativeui::master::Main)
            inst.setNativeMasterOutput ({ full_.meters.peakL, full_.meters.peakR, full_.meters.holdL, full_.meters.holdR,
                                          full_.voiceMeter.activeVoices, full_.voiceMeter.polyphony, full_.clip.state.active });
        publishNativeView (inst);
        publishQGroup (inst);
    }

    nativeui::master::View nativeView() const noexcept
    {
        using namespace nativeui::master;
        if (pageTop_ != 0)
            return Hidden;
        return full_.side() == ui::MasterScreen::SideMain ? Main : Tuning;
    }

    void leftTab (IInstanceView& inst) noexcept
    {
        inst.leaveNativeMasterView (full_.side() == ui::MasterScreen::SideMain ? nativeui::master::Main : nativeui::master::Tuning);
        lastInstance_ = nullptr;
    }

    bool covers (int x, int y) const noexcept
    {
        if (! ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y))
            return false;
        if (pageTop_ != 0)
            return true;
        if (full_.isNativeControl (x, y))
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
            gestureDown_ = false;
            if (nativeView() != nativeui::master::Hidden && full_.isNativeControl (x, y))
            {
                if (! ui::QGroup::wideDevice())
                {
                    full_.activateGroupAt (x, y);
                    publishQGroup (inst);
                }
                return false;
            }
            if (! covers (x, y) || ! top)
                return false;
            gesture_ = top;
            gestureDown_ = true;
            handle (top->touchDown (x, y, nowMs), stack, inst);
            syncEditorView (stack, inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (x, y, nowMs), stack, inst);
        else
        {
            handle (gesture_->touchUp (x, y), stack, inst);
            gestureDown_ = false;
            gesture_ = nullptr;
        }
        syncEditorView (stack, inst);
        return true;
    }

    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (gesture_ && gestureDown_)
        {
            handle (gesture_->tick (nowMs), stack, inst);
            syncEditorView (stack, inst);
        }
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
        syncEditorView (stack, inst);
        (void) nowMs;
        return true;
    }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    void forgetQGroup() noexcept
    {
        full_.setActiveGroup (full_.side() == ui::MasterScreen::SideMain ? 0 : ui::MasterScreen::kFirstTuningGroup);
        qSlots_.forget();
    }
    int topPage() const noexcept { return pageTop_; }

    ui::Change turnQ (int slot, double delta, UiStack& stack, IInstanceView& inst)
    {
        const ui::Change ch = full_.turnQ (slot, delta);
        handle (ch, stack, inst);
        syncEditorView (stack, inst);
        return ch;
    }

    void setHostBackground (bool on) noexcept { nativeGround_ = on; }
    bool hostBackground() const noexcept
    {
        return nativeGround_ && pageTop_ == 0 && w_ == page::kW && h_ == page::kH
            && &ui::theme::L() == &ui::theme::kLook1;
    }

    void drawPageGround (gpu::SceneBuilder& sb) const
    {
        if (hostBackground())
            return;
        if (pageTop_ == 0)
            full_.drawNativeGround (sb);
        else
            page::ground (sb);
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        drawNativeMarkerCovers (sb, "master");
        if (pageTop_ != 0)
            full_.drawStaticFurniture (sb);
        full_.drawPage (sb, pageTop_ == 0);
    }

    // ---- access for the tests
    ui::MasterScreen& screen() noexcept { return full_; }
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
    void bind (int id, ui::Component* c, T type, const std::string& key)
    {
        Item it;
        it.id = id; it.c = c; it.type = type; it.key = key;
        it.param = uibind::indexOfKey (key);
        if (c != nullptr)
            c->setId (id);
        items_.push_back (it);
    }

    void configure()
    {
        items_.clear();
        bind (kVolume, &full_.volume, T::Knob, "master.volume");
        bind (kSpread, &full_.spread, T::Knob, "master.stereo_spread");
        bind (kStereoRouting, &full_.stereoRouting, T::Knob, "master.stereo_routing");
        bind (kVoiceOverride, &full_.voiceOverride, T::List, "master.voice_override");
        bind (kTune, &full_.tune, T::Knob, "master.tune");
        bind (kTranspose, &full_.transpose, T::Number, "master.transpose");
        bind (kBendUp, &full_.bendUp, T::Number, "master.bend_range");
        bind (kBendDown, &full_.bendDown, T::Number, "master.bend_down_range");
        bind (kBendLink, &full_.bendLink, T::Toggle, "master.bend_down_link");
        bind (kPolyphony, &full_.polyphony, T::Number, "master.polyphony");
        bind (kLegato, &full_.legato, T::Toggle, "master.legato");
        bind (kPolyLegato, &full_.polyLegato, T::Toggle, "master.poly_legato");
        bind (kPhaseLegato, &full_.phaseLegato, T::Toggle, "voice.phase_legato");
        bind (kSameNote, &full_.sameNote, T::List, "master.voice.same_note");
        bind (kStealPriority, &full_.stealPriority, T::List, "master.voice.steal_priority");
        bind (kVelocityTrack, &full_.velocityTrack, T::Number, "master.velocity_track");
        bind (kPortaTime, &full_.portaTime, T::Knob, "master.portamento.time");
        bind (kPortaSlope, &full_.portaSlope, T::Knob, "master.portamento.slope");
        bind (kPortaAlways, &full_.portaAlways, T::Toggle, "master.portamento.always");
        bind (kPortaScaled, &full_.portaScaled, T::Toggle, "master.portamento.scaled");
        bind (kScaleType, &full_.scaleType, T::Stepper, "master.scale.type");
        bind (kScaleKey, &full_.scaleKey, T::Stepper, "master.scale.key");
        bind (kScaleMode, &full_.scaleMode, T::List, "master.scale.mode");
        bind (kTuningA4, &full_.tuningA4, T::Number, "master.tuning.a4");
        bind (kTuningScale, &full_.tuningScale, T::List, "master.tuning.scale");
        bind (kMtsEsp, &full_.mtsEsp, T::Toggle, "master.tuning.mts_esp");
        bind (kMtsNoteOn, &full_.mtsNoteOn, T::Toggle, "master.tuning.mts_note_on_only");
        bind (kMpeOn, &full_.mpeOn, T::Toggle, "master.mpe.on");
        bind (kMpeBend, &full_.mpeBend, T::Number, "master.mpe.bend_range");
        bind (kMpeMacro, &full_.mpeMacro, T::List, "master.mpe.xyz_macro");
        bind (kMpeYModwheel, &full_.mpeYModwheel, T::Toggle, "master.mpe.y_modwheel");
        bind (kMpeBendToX, &full_.mpeBendToX, T::Toggle, "master.mpe.bend_to_x");
        bind (kMpeYBipolar, &full_.mpeYBipolar, T::Toggle, "master.mpe.y_bipolar");
        bind (kLimiter, &full_.outputLimiter, T::Inert, "");
        bind (kNoSmoothing, &full_.noSmoothing, T::Toggle, "quality.no_smoothing");   // the QUALITY pair: engine parameters, touch only
        bind (kOversample4x, &full_.oversample4x, T::Toggle, "quality.oversample_4x");
        bind (kPadPressure, &full_.padPressure, T::Inert, "");
        bind (kPadAmount, &full_.padAmount, T::Inert, "");
        for (ui::Stepper* s : { &full_.scaleType, &full_.scaleKey })
            s->listable = true;   // a tap on the name opens the list of all the choices (the two long lists; the short ones are LED lists with every option visible)
        bendLinkIdx_ = uibind::indexOfKey ("master.bend_down_link");
        mpeOnIdx_ = uibind::indexOfKey ("master.mpe.on");
        polyphonyIdx_ = uibind::indexOfKey ("master.polyphony");
        slopeIdx_ = uibind::indexOfKey ("master.portamento.slope");
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
                    kn->state.isDb = (p.unit == synthyboy::Unit::dB);
                    if (kn->state.isDb)
                    {
                        kn->state.dbMin = p.min;
                        kn->state.dbMax = p.max;
                        kn->state.skew = p.skew;
                    }
                    break;
                }
                case T::Number:
                {
                    ui::NumberField* nf = static_cast<ui::NumberField*> (it.c);
                    nf->state.value = val;
                    nf->state.min = p.min;
                    nf->state.max = p.max;
                    nf->state.defaultValue = p.def;
                    nf->state.step = it.id == kTuningA4 ? 0.01 : 1.0;
                    nf->state.pxPerStep = it.id == kTuningA4 ? 1 : 6;
                    break;
                }
                case T::Toggle:
                    static_cast<ui::ToggleIcon*> (it.c)->state.on = val > 0.5f;
                    break;
                case T::Stepper:
                {
                    ui::Stepper* st = static_cast<ui::Stepper*> (it.c);
                    st->state.count = p.numChoices > 0 ? p.numChoices : 101;
                    st->state.index = std::clamp (static_cast<int> (std::lround (val)), 0, st->state.count - 1);
                    if (p.numChoices > 0)
                        st->setItem (p.choices[st->state.index]);
                    else
                    {
                        char buf[16];
                        uibind::formatValue (p, val, buf, sizeof buf);
                        st->setItem (buf);
                    }
                    break;
                }
                case T::List:
                {
                    ui::LedList* ll = static_cast<ui::LedList*> (it.c);
                    ll->setOptions (p.choices, p.numChoices > 0 ? p.numChoices : 0);
                    if (it.id == kMpeMacro && ll->state.count == 3)   // the long names of the engine ("XYZ -> Macro 1, 2, 3") do not fit three side by side: the title says what it is
                    {
                        ll->setOption (1, "XYZ");
                        ll->setOption (2, "YZ");
                    }
                    ll->state.index = std::clamp (static_cast<int> (std::lround (val)), 0, std::max (0, ll->state.count - 1));
                    break;
                }
                case T::Inert:
                    break;
            }
        }
    }

    void handle (const ui::Change& ch, UiStack&, IInstanceView& inst)
    {
        if (ch.kind == ui::ChangeKind::None)
            return;
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

    /** The groups of the page are the groups of the side that is up (Main: OUTPUT .. GLIDE, Tuning: SCALE .. MPE Y) in reading order; the active group is on Q 1 .. 4, the next three (wrapping inside the side) on 5 .. 16.
        The targets of a group are the parameters of its LIVE members (MasterScreen closes the groups up: a hidden control and the touch-only LINK are no member, so no slot is wasted on them). */
    int paramOf (const ui::Component* c) const noexcept
    {
        for (const Item& it : items_)
            if (it.c == c)
                return it.param;
        return -1;
    }

    void publishNativeView (IInstanceView& inst) const noexcept
    {
        inst.setNativeMasterView (nativeView());
    }

    void syncEditorView (const UiStack& stack, IInstanceView& inst) noexcept
    {
        pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        publishNativeView (inst);
    }

    void publishQGroup (IInstanceView& inst)
    {
        const int g = full_.activeGroup();
        const bool main = g < ui::MasterScreen::kFirstTuningGroup;
        const int first = main ? 0 : ui::MasterScreen::kFirstTuningGroup, count = main ? ui::MasterScreen::kFirstTuningGroup : ui::MasterScreen::kMaxGroups - ui::MasterScreen::kFirstTuningGroup;
        const bool list = pageTop_ != 0;
        // the groups with members only (VOICE OPTS has none: touch only, GLIDE takes its place on the Q-Links): MAIN is OUTPUT, PITCH, VOICES, GLIDE = four groups, all on the XL at once, the four banks of the Key 61
        int ids[ui::MasterScreen::kMaxGroups], pos = -1;
        const int used = usedGroups (full_.groups + first, count, g - first, ids, pos);
        const int active = pos < 0 ? 0 : pos;
        if (list)
            qSlots_.publish (inst, 0, 0, [] (int, int*) {});   // the list has no group: no slot has a target
        else
            qSlots_.publish (inst, used, active, [&] (int k, int* t)
            {
                for (int i = 0; i < 4; ++i)
                {
                    const ui::Component* m = full_.groups[first + ids[k]].member (i);
                    t[i] = m != nullptr ? paramOf (m) : -1;
                }
            });
        for (int i = 0; i < ui::MasterScreen::kMaxGroups; ++i)
            full_.groups[i].setBlock (-1);
        if (! list)
            for (int k = 0; k < used; ++k)
                full_.groups[first + ids[k]].setBlock (QRouter::blockOfGroup (used, active, k));
    }

    ui::Component* topComponent() noexcept { return &full_; }

    ui::MasterScreen full_;
    std::vector<Item> items_;

    bool nativeGround_ = false;
    int w_ = kCanvasW, h_ = kCanvasH;
    int pageTop_ = 0;
    const IInstanceView* lastInstance_ = nullptr;
    double lastMs_ = 0.0;
    int bendLinkIdx_ = -1, mpeOnIdx_ = -1, polyphonyIdx_ = -1, slopeIdx_ = -1;

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    QSlots qSlots_;
};

}} // namespace synthyboy_mpc::overlay
