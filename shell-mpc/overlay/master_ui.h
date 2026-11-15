// SynthyBoy - MPC overlay: the Master UI of the Play page, wired to real engine parameters (docs/MPC.md, ui-elem-mastervoice.md).
// One object per (Play view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw ().
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../spectrum.h"
#include "../ui/list.h"
#include "../ui/theme.h"
#include "../ui/voice_master.h"
#include "curve_source.h"
#include "touch_decoder.h"
#include "ui_stack.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class MasterUi
{
public:
    static constexpr int kElementMaster = 2; // UiStack element id for Master full screen
    enum Page { kPageList = 15 };

    enum Id : int
    {
        kClose = 1,
        // Block 0: Output
        kVolume = 100, kSpread, kStereoRouting, kVoiceOverride,
        // Block 1: Pitch
        kTune = 110, kTranspose, kBendUp, kBendDown, kBendLink,
        // Block 2: Voices
        kPolyphony = 120, kLegato, kPolyLegato, kPhaseLegato, kSameNote, kStealPriority,
        // Block 3: Glide
        kPortaTime = 130, kPortaSlope, kPortaAlways, kPortaScaled,
        // Block 4: Tuning / Scale
        kScaleType = 140, kScaleKey, kScaleMode, kTuningA4, kTuningScale, kMtsEsp, kMtsNoteOn,
        // Block 5: MPE
        kMpeOn = 150, kMpeBend, kMpeMacro, kMpeYModwheel, kMpeBendToX, kMpeYBipolar,
        // Popup
        kList = 700, kListBackdrop = 701
    };

    enum class T : uint8_t
    {
        Knob,
        Number,
        Toggle,
        Stepper,
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
    };

    MasterUi()
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

        // Update live readouts
        synthyboy::LiveValues live;
        if (inst.live (live))
        {
            full_.voiceMeter.activeVoices = live.activeVoices;
            full_.meters.peakL = std::clamp (static_cast<float> (live.activeVoices) * 0.15f, 0.0f, 1.2f);
            full_.meters.peakR = full_.meters.peakL;
        }

        // Pull audio peaks if spectrum ring is present
        const SpectrumRing* ring = inst.spectrumRing();
        if (ring != nullptr)
        {
            float samples[256];
            if (ring->latest (samples, 256))
            {
                float peak = 0.0f;
                for (int i = 0; i < 256; ++i)
                    peak = std::max (peak, std::fabs (samples[i]));
                full_.meters.peakL = peak;
                full_.meters.peakR = peak;
            }
        }

        // Update glide curve display slope
        full_.portaCurve.slope = inst.param (indexOfKey ("master.portamento.slope"));

        // Update scale display
        full_.keyboard.scaleType = static_cast<int> (std::lround (inst.param (indexOfKey ("master.scale.type"))));
        full_.keyboard.scaleKey = static_cast<int> (std::lround (inst.param (indexOfKey ("master.scale.key"))));
        full_.keyboard.scaleMode = static_cast<int> (std::lround (inst.param (indexOfKey ("master.scale.mode"))));

        // Format value labels on all knobs
        formatKnobValues (inst);

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
    ui::MasterScreen& screen() noexcept { return full_; }
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

    void bind (int id, ui::Component* c, T type, const std::string& key, bool alwaysOff = false)
    {
        Item it;
        it.id = id; it.c = c; it.type = type; it.alwaysOff = alwaysOff;
        it.key = key;
        it.param = indexOfKey (key);
        if (c != nullptr)
            c->setId (id);
        items_.push_back (it);
    }

    void configure()
    {
        items_.clear();

        // Header
        bind (kClose, &full_.close, T::Inert, "");

        // Block 0: Output
        bind (kVolume, &full_.volume, T::Knob, "master.volume");
        bind (kSpread, &full_.spread, T::Knob, "master.stereo_spread");
        bind (kStereoRouting, &full_.stereoRouting, T::Stepper, "master.stereo_routing");
        bind (kVoiceOverride, &full_.voiceOverride, T::Stepper, "master.voice_override");

        // Block 1: Pitch
        bind (kTune, &full_.tune, T::Knob, "master.tune");
        bind (kTranspose, &full_.transpose, T::Number, "master.transpose");
        bind (kBendUp, &full_.bendUp, T::Number, "master.bend_range");
        bind (kBendDown, &full_.bendDown, T::Number, "master.bend_down_range");
        bind (kBendLink, &full_.bendLink, T::Toggle, "master.bend_down_link");

        // Block 2: Voices
        bind (kPolyphony, &full_.polyphony, T::Number, "master.polyphony");
        bind (kLegato, &full_.legato, T::Toggle, "master.legato");
        bind (kPolyLegato, &full_.polyLegato, T::Toggle, "master.poly_legato");
        bind (kPhaseLegato, &full_.phaseLegato, T::Toggle, "voice.phase_legato");
        bind (kSameNote, &full_.sameNote, T::Stepper, "master.voice.same_note");
        bind (kStealPriority, &full_.stealPriority, T::Stepper, "master.voice.steal_priority");

        // Block 3: Glide
        bind (kPortaTime, &full_.portaTime, T::Knob, "master.portamento.time");
        bind (kPortaSlope, &full_.portaSlope, T::Knob, "master.portamento.slope");
        bind (kPortaAlways, &full_.portaAlways, T::Toggle, "master.portamento.always");
        bind (kPortaScaled, &full_.portaScaled, T::Toggle, "master.portamento.scaled");

        // Block 4: Tuning / Scale
        full_.scaleType.listable = true;
        full_.tuningScale.listable = true;
        full_.sameNote.listable = true;
        full_.stealPriority.listable = true;
        full_.voiceOverride.listable = true;
        full_.scaleKey.listable = true;
        full_.scaleMode.listable = true;
        full_.mpeMacro.listable = true;

        bind (kScaleType, &full_.scaleType, T::Stepper, "master.scale.type");
        bind (kScaleKey, &full_.scaleKey, T::Stepper, "master.scale.key");
        bind (kScaleMode, &full_.scaleMode, T::Stepper, "master.scale.mode");
        bind (kTuningA4, &full_.tuningA4, T::Number, "master.tuning.a4");
        bind (kTuningScale, &full_.tuningScale, T::Stepper, "master.tuning.scale");
        bind (kMtsEsp, &full_.mtsEsp, T::Toggle, "master.tuning.mts_esp");
        bind (kMtsNoteOn, &full_.mtsNoteOn, T::Toggle, "master.tuning.mts_note_on_only");

        // Block 5: MPE
        bind (kMpeOn, &full_.mpeOn, T::Toggle, "master.mpe.on");
        bind (kMpeBend, &full_.mpeBend, T::Number, "master.mpe.bend_range");
        bind (kMpeMacro, &full_.mpeMacro, T::Stepper, "master.mpe.xyz_macro");
        bind (kMpeYModwheel, &full_.mpeYModwheel, T::Toggle, "master.mpe.y_modwheel");
        bind (kMpeBendToX, &full_.mpeBendToX, T::Toggle, "master.mpe.bend_to_x");
        bind (kMpeYBipolar, &full_.mpeYBipolar, T::Toggle, "master.mpe.y_bipolar");
    }

    void pull (IInstanceView& inst)
    {
        for (const Item& it : items_)
        {
            if (it.param < 0 || it.alwaysOff || ! it.c)
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
                        nf->state.value = val;
                    break;
                case T::Toggle:
                    if (auto* tg = dynamic_cast<ui::ToggleIcon*> (it.c))
                        tg->state.on = (val > 0.5f);
                    break;
                case T::Stepper:
                    if (auto* st = dynamic_cast<ui::Stepper*> (it.c))
                    {
                        st->state.count = p.numChoices > 0 ? p.numChoices : 101;
                        st->state.index = static_cast<int> (std::lround (val));
                        if (p.numChoices > 0 && st->state.index >= 0 && st->state.index < p.numChoices)
                            st->setItem (p.choices[st->state.index]);
                        else
                        {
                            char buf[16];
                            std::snprintf (buf, sizeof buf, "%d%%", static_cast<int> (std::lround (val)));
                            st->setItem (buf);
                        }
                    }
                    break;
                case T::Inert:
                    break;
            }
        }
    }

    void formatKnobValues (IInstanceView& inst)
    {
        // Volume: dB
        int p = indexOfKey ("master.volume");
        if (p >= 0)
        {
            float v = inst.param (p);
            std::snprintf (full_.volume.state.valueText, sizeof full_.volume.state.valueText, "%.1f dB", v);
        }

        // Spread: %
        p = indexOfKey ("master.stereo_spread");
        if (p >= 0)
        {
            int v = static_cast<int> (std::lround (inst.param (p)));
            std::snprintf (full_.spread.state.valueText, sizeof full_.spread.state.valueText, "%d%%", v);
        }

        // Tune: cents
        p = indexOfKey ("master.tune");
        if (p >= 0)
        {
            float v = inst.param (p);
            std::snprintf (full_.tune.state.valueText, sizeof full_.tune.state.valueText, "%+.1f c", v);
        }

        // Porta Time: ms or s
        p = indexOfKey ("master.portamento.time");
        if (p >= 0)
        {
            float v = inst.param (p);
            if (v >= 1000.0f)
                std::snprintf (full_.portaTime.state.valueText, sizeof full_.portaTime.state.valueText, "%.2f s", v * 0.001f);
            else
                std::snprintf (full_.portaTime.state.valueText, sizeof full_.portaTime.state.valueText, "%d ms", static_cast<int> (std::lround (v)));
        }

        // Porta Slope: -8 .. +8
        p = indexOfKey ("master.portamento.slope");
        if (p >= 0)
        {
            float v = inst.param (p);
            std::snprintf (full_.portaSlope.state.valueText, sizeof full_.portaSlope.state.valueText, "%+.1f", v);
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

        const Item* it = item (ch.id);
        if (! it || it->param < 0)
            return;

        if (it->type == T::Stepper && ch.kind == ui::ChangeKind::Request)
        {
            // Open ListPopup for stepper
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
        static const char* const kGroupKeys[6][4] = {
            { "master.volume", "master.stereo_spread", "master.stereo_routing", "master.voice_override" },
            { "master.tune", "master.transpose", "master.bend_range", "master.bend_down_range" },
            { "master.polyphony", "master.legato", "master.poly_legato", "master.voice.same_note" },
            { "master.portamento.time", "master.portamento.slope", "master.portamento.always", "master.portamento.scaled" },
            { "master.scale.type", "master.scale.key", "master.scale.mode", "master.tuning.a4" },
            { "master.mpe.on", "master.mpe.bend_range", "master.mpe.xyz_macro", "master.mpe.y_modwheel" }
        };

        for (int i = 0; i < 4; ++i)
            qTargets_[i] = indexOfKey (kGroupKeys[g][i]);
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

    ui::MasterScreen full_;
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
