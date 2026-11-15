// SynthyBoy - MPC overlay: the MIXER UI.
// Connects the MixerFullScreen component to the engine parameter table, the live readout peaks,
// the Q-group routing, and destination popup menus.
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
#include "../ui/mixer_screen.h"
#include "../ui/ui.h"
#include "curve_source.h"
#include "touch_decoder.h"
#include "ui_stack.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class MixerUi
{
public:
    static constexpr int kElementMixer = 2;
    static constexpr int kPageList = 25;

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    enum IdBase : int
    {
        kStripBase = 1000,
        kList = 2000,
        kListBackdrop = 2001
    };

    struct StripBinding
    {
        int powerParam = -1;
        int levelParam = -1;
        int panParam = -1;
        int destParam = -1;
        int send1Param = -1;
        int send2Param = -1;
        bool powerInverted = false; // true for fx_bypass
    };

    MixerUi()
    {
        initBindings();
        screen_.assignIds (kStripBase);
        setCanvas (kCanvasW, kCanvasH);
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        screen_.place (tab);
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
    }

    ui::MixerFullScreen& screen() noexcept { return screen_; }
    const ui::MixerFullScreen& screen() const noexcept { return screen_; }
    ui::ListPopup& list() noexcept { return list_; }
    const ui::ListPopup& list() const noexcept { return list_; }

    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs)
    {
        (void) highlight;
        (void) stack;
        (void) nowMs;

        // 1. Pull parameter values into components
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
        {
            const StripBinding& b = bindings_[s];
            ui::MixerStrip& strip = screen_.strips[s];

            // Power
            if (b.powerParam >= 0)
            {
                const float pVal = inst.param (b.powerParam);
                const bool on = b.powerInverted ? (pVal < 0.5f) : (pVal >= 0.5f);
                strip.power.state.on = on;
                strip.power.setEnabled (true);
            }
            else
            {
                strip.power.state.on = true;
                strip.power.setEnabled (false);
            }

            // Level / Fader
            if (b.levelParam >= 0)
            {
                const synthyboy::ParamInfo& pi = synthyboy::paramTable()[b.levelParam];
                const float plain = inst.param (b.levelParam);
                const float norm = synthyboy::toNormalised (pi, plain);
                strip.fader.state.value = norm;
                strip.fader.state.defaultValue = synthyboy::toNormalised (pi, pi.def);
                char buf[16];
                synthyboy::valueToText (pi, plain, buf, sizeof buf);
                ui::setText (strip.fader.state.valueText, sizeof strip.fader.state.valueText, buf);
                strip.fader.setEnabled (true);
            }
            else
            {
                strip.fader.setEnabled (false);
            }

            // Pan
            if (b.panParam >= 0)
            {
                const synthyboy::ParamInfo& pi = synthyboy::paramTable()[b.panParam];
                const float plain = inst.param (b.panParam);
                strip.pan.state.value = synthyboy::toNormalised (pi, plain);
                strip.pan.state.defaultValue = synthyboy::toNormalised (pi, pi.def);
                if (std::fabs (plain) < 0.5f)
                    ui::setText (strip.pan.state.valueText, sizeof strip.pan.state.valueText, "C");
                else if (plain < 0.0f)
                    std::snprintf (strip.pan.state.valueText, sizeof strip.pan.state.valueText, "L%.0f", std::fabs (plain));
                else
                    std::snprintf (strip.pan.state.valueText, sizeof strip.pan.state.valueText, "R%.0f", plain);
                strip.pan.setEnabled (true);
            }
            else
            {
                strip.pan.state.value = 0.5;
                ui::setText (strip.pan.state.valueText, sizeof strip.pan.state.valueText, "-");
                strip.pan.setEnabled (false);
            }

            // Destination
            if (b.destParam >= 0)
            {
                const synthyboy::ParamInfo& pi = synthyboy::paramTable()[b.destParam];
                const int choice = std::clamp (static_cast<int> (inst.param (b.destParam) + 0.5f), 0, pi.numChoices - 1);
                screen_.routingBar.setDestination (s, choice);
                if (pi.choices != nullptr && choice < pi.numChoices)
                    strip.destButton.setLabel (pi.choices[choice]);
                strip.destButton.setEnabled (true);
            }
            else
            {
                strip.destButton.setLabel ("OUT");
                strip.destButton.setEnabled (false);
                screen_.routingBar.setDestination (s, 4); // master out
            }

            // Send 1
            if (b.send1Param >= 0)
            {
                const synthyboy::ParamInfo& pi = synthyboy::paramTable()[b.send1Param];
                const float plain = inst.param (b.send1Param);
                strip.send1.state.value = synthyboy::toNormalised (pi, plain);
                char buf[12];
                std::snprintf (buf, sizeof buf, "%.0f%%", plain);
                ui::setText (strip.send1.state.valueText, sizeof strip.send1.state.valueText, buf);
                strip.send1.setEnabled (true);
            }
            else
            {
                strip.send1.state.value = 0.0;
                ui::setText (strip.send1.state.valueText, sizeof strip.send1.state.valueText, "-");
                strip.send1.setEnabled (false);
            }

            // Send 2
            if (b.send2Param >= 0)
            {
                const synthyboy::ParamInfo& pi = synthyboy::paramTable()[b.send2Param];
                const float plain = inst.param (b.send2Param);
                strip.send2.state.value = synthyboy::toNormalised (pi, plain);
                char buf[12];
                std::snprintf (buf, sizeof buf, "%.0f%%", plain);
                ui::setText (strip.send2.state.valueText, sizeof strip.send2.state.valueText, buf);
                strip.send2.setEnabled (true);
            }
            else
            {
                strip.send2.state.value = 0.0;
                ui::setText (strip.send2.state.valueText, sizeof strip.send2.state.valueText, "-");
                strip.send2.setEnabled (false);
            }
        }

        // 2. Live peak meters from LiveValues
        synthyboy::LiveValues lv;
        if (inst.live (lv))
        {
            for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
            {
                const float pk = lv.stripPeak[s];
                screen_.strips[s].fader.state.peak = pk;
                if (pk > screen_.strips[s].fader.state.peakHold)
                    screen_.strips[s].fader.state.peakHold = pk;
                else
                    screen_.strips[s].fader.state.peakHold = std::max (0.0f, screen_.strips[s].fader.state.peakHold - 0.02f);
            }
        }

        // 3. Publish active Q-Group
        publishQGroup (inst);
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        // 1. If list popup is active
        if (listOpen_)
        {
            ui::Change c;
            if (ev.kind == TouchEvent::Down) c = list_.touchDown (ev.x, ev.y, nowMs);
            else if (ev.kind == TouchEvent::Move) c = list_.touchMove (ev.x, ev.y);
            else if (ev.kind == TouchEvent::Up) c = list_.touchUp (ev.x, ev.y);

            if (c)
            {
                if (c.kind == ui::ChangeKind::Index && listDestStrip_ >= 0)
                {
                    const int destParam = bindings_[listDestStrip_].destParam;
                    if (destParam >= 0)
                    {
                        const synthyboy::ParamInfo& pi = synthyboy::paramTable()[destParam];
                        inst.writeParam (destParam, synthyboy::toNormalised (pi, static_cast<float> (c.index)));
                    }
                    listOpen_ = false;
                    stack.back();
                    return true;
                }
                else if (c.kind == ui::ChangeKind::Request) // close backdrop
                {
                    listOpen_ = false;
                    stack.back();
                    return true;
                }
            }
            return true;
        }

        // 2. Normal screen touch
        ui::Change c;
        if (ev.kind == TouchEvent::Down) c = screen_.touchDown (ev.x, ev.y, nowMs);
        else if (ev.kind == TouchEvent::Move) c = screen_.touchMove (ev.x, ev.y);
        else if (ev.kind == TouchEvent::Up) c = screen_.touchUp (ev.x, ev.y);

        if (c)
        {
            handleScreenChange (c, stack, inst);
            return true;
        }
        return false;
    }

    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        (void) nowMs; (void) stack; (void) inst;
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        screen_.draw (sb);
        if (listOpen_)
            list_.draw (sb);
    }

    bool listOpen() const noexcept { return listOpen_; }

private:
    int w_ = kCanvasW, h_ = kCanvasH;
    ui::MixerFullScreen screen_;
    ui::ListPopup list_;
    bool listOpen_ = false;
    int listDestStrip_ = -1;
    StripBinding bindings_[ui::MixerFullScreen::kStrips];

    void initBindings()
    {
        auto idx = [] (const char* k) -> int {
            const auto* p = synthyboy::findParamByKey (k);
            return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
        };

        // 0: SUB
        bindings_[0] = { idx ("sub.on"), idx ("sub.level"), idx ("sub.pan"), idx ("sub.destination"), idx ("sub.bus1_send"), idx ("sub.bus2_send"), false };
        // 1: A (Osc 1)
        bindings_[1] = { idx ("osc1.on"), idx ("osc1.level"), idx ("osc1.pan"), idx ("osc1.destination"), idx ("osc1.bus1_send"), idx ("osc1.bus2_send"), false };
        // 2: B (Osc 2)
        bindings_[2] = { idx ("osc2.on"), idx ("osc2.level"), idx ("osc2.pan"), idx ("osc2.destination"), idx ("osc2.bus1_send"), idx ("osc2.bus2_send"), false };
        // 3: C (Osc 3)
        bindings_[3] = { idx ("osc3.on"), idx ("osc3.level"), idx ("osc3.pan"), idx ("osc3.destination"), idx ("osc3.bus1_send"), idx ("osc3.bus2_send"), false };
        // 4: NOISE
        bindings_[4] = { idx ("noise.on"), idx ("noise.level"), idx ("noise.pan"), idx ("noise.destination"), idx ("noise.bus1_send"), idx ("noise.bus2_send"), false };
        // 5: F1 (Filter 1)
        bindings_[5] = { idx ("filter1.on"), idx ("filter1.level"), idx ("filter1.pan"), idx ("filter1.destination"), idx ("filter1.bus1_send"), idx ("filter1.bus2_send"), false };
        // 6: F2 (Filter 2)
        bindings_[6] = { idx ("filter2.on"), idx ("filter2.level"), idx ("filter2.pan"), idx ("filter2.destination"), idx ("filter2.bus1_send"), idx ("filter2.bus2_send"), false };
        // 7: BUS 1
        bindings_[7] = { idx ("bus1.fx_bypass"), idx ("bus1.level"), -1, idx ("bus1.destination"), -1, -1, true };
        // 8: BUS 2
        bindings_[8] = { idx ("bus2.fx_bypass"), idx ("bus2.level"), -1, idx ("bus2.destination"), -1, -1, true };
        // 9: MASTER
        bindings_[9] = { idx ("main.fx_bypass"), idx ("master.volume"), -1, -1, -1, -1, true };
    }

    void publishQGroup (IInstanceView& inst)
    {
        const int s = screen_.activeGroup();
        if (s < 0 || s >= ui::MixerFullScreen::kStrips)
            return;
        const StripBinding& b = bindings_[s];
        const int targets[4] = { b.levelParam, b.panParam, b.send1Param, b.send2Param };
        inst.setQGroup (targets, 4);
    }

    void handleScreenChange (const ui::Change& c, UiStack& stack, IInstanceView& inst)
    {
        const int s = (c.id - kStripBase) / 10;
        const int type = (c.id - kStripBase) % 10;
        if (s < 0 || s >= ui::MixerFullScreen::kStrips)
            return;

        const StripBinding& b = bindings_[s];

        if (type == 0) // Power toggle
        {
            if (b.powerParam >= 0)
            {
                const bool newOn = screen_.strips[s].power.state.on;
                const float norm = b.powerInverted ? (newOn ? 0.0f : 1.0f) : (newOn ? 1.0f : 0.0f);
                inst.writeParam (b.powerParam, norm);
            }
        }
        else if (type == 1) // Destination button tapped -> open popup
        {
            if (b.destParam >= 0)
            {
                const synthyboy::ParamInfo& pi = synthyboy::paramTable()[b.destParam];
                if (pi.choices != nullptr && pi.numChoices > 0)
                {
                    list_.state.items = pi.choices;
                    list_.state.count = pi.numChoices;
                    list_.state.current = std::clamp (static_cast<int> (inst.param (b.destParam) + 0.5f), 0, pi.numChoices - 1);
                    const ui::Rect btnRect = screen_.strips[s].destButton.rect();
                    const int popW = 240;
                    const int popH = std::min (6, static_cast<int> (pi.numChoices)) * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad;
                    const int popX = std::clamp (btnRect.x - (popW - btnRect.w) / 2, 10, w_ - popW - 10);
                    const int popY = std::clamp (btnRect.bottom() + 4, kTabY + 10, kTabY + kTabH - popH - 10);
                    list_.place ({ popX, popY, popW, popH });
                    list_.showCurrent();
                    listOpen_ = true;
                    listDestStrip_ = s;
                    stack.openDetail (kPageList);
                }
            }
        }
        else if (type == 2) // Pan knob
        {
            if (b.panParam >= 0)
                inst.writeParam (b.panParam, static_cast<float> (c.value));
        }
        else if (type == 3) // Send 1 bar
        {
            if (b.send1Param >= 0)
                inst.writeParam (b.send1Param, static_cast<float> (c.value));
        }
        else if (type == 4) // Send 2 bar
        {
            if (b.send2Param >= 0)
                inst.writeParam (b.send2Param, static_cast<float> (c.value));
        }
        else if (type == 5) // Fader
        {
            if (b.levelParam >= 0)
                inst.writeParam (b.levelParam, static_cast<float> (c.value));
        }
    }
};

}} // namespace synthyboy_mpc::overlay
