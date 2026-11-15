// SynthyBoy - MPC overlay: the MIXER UI, the page of the Mixer tab (the plugin draws the whole tab, page_view.h): ten channel strips (Sub, A, B, C, Noise, Filter 1, Filter 2, Bus 1, Bus 2, Master) over a routing
// bar (shell-mpc/ui/mixer_screen.h, mixer_strip.h) wired to the engine's parameters: the switch, the destination, the pan, the two sends, the level (the fader with its meter). One object per (page view, plugin
// instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw (), with the instance as IInstanceView. Docs: docs/MPC.md "Mixer and FX UI".
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Values: every value is written with the parameter's own format (uibind::formatValue): the level of a strip in % or dB as the parameter is, the pan as C / L 35 / R 35, the sends in %. The meters are the
// engine's LiveValues::stripPeak (own engine: per strip, with a release of 300 ms), a hold line falls slowly from the highest level. The Q group is the strip you touched: LEVEL, PAN, B1, B2; a tap on the
// destination opens the list of its choices over the page.
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
#include "page_view.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class MixerUi
{
public:
    static constexpr int kElementPage = 5;          // the page of the Mixer tab (the oscillator is 1, the envelope 2, the LFO 3, the filter 4)
    static constexpr int kElementMixer = kElementPage;
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
    const StripBinding& binding (int strip) const noexcept { return bindings_[strip]; }
    int topPage() const noexcept { return listOpen_ ? kPageList : 0; }
    const int* qTargets() const noexcept { return qTargets_; }
    const char* qGroupName() const noexcept { return screen_.groupName (screen_.activeGroup()); }

    /** Reads the instance: every value of every strip, the meters, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        (void) nowMs;
        if (listOpen_ && ! (stack.depth() >= 2 && stack.top() == kPageList))
            listOpen_ = false;
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
        {
            const StripBinding& b = bindings_[s];
            ui::MixerStrip& strip = screen_.strips[s];

            if (b.powerParam >= 0)
            {
                const float pVal = inst.param (b.powerParam);
                strip.power.state.on = b.powerInverted ? (pVal < 0.5f) : (pVal >= 0.5f);
                strip.power.setEnabled (true);
            }
            else
            {
                strip.power.state.on = true;
                strip.power.setEnabled (false);
            }

            if (b.levelParam >= 0)
            {
                const synthyboy::ParamInfo& pi = uibind::info (b.levelParam);
                const float plain = inst.param (b.levelParam);
                strip.fader.state.value = strip.fader.state.modValue = uibind::normOf (b.levelParam, plain);
                strip.fader.state.defaultValue = uibind::normOf (b.levelParam, pi.def);
                uibind::formatValue (pi, plain, strip.fader.state.valueText, sizeof strip.fader.state.valueText);   // the parameter's own unit: % or dB
                setScale (strip.fader, pi);
                strip.fader.setEnabled (true);
            }
            else
                strip.fader.setEnabled (false);

            if (b.panParam >= 0)
            {
                const synthyboy::ParamInfo& pi = uibind::info (b.panParam);
                const float plain = inst.param (b.panParam);
                strip.pan.state.value = uibind::normOf (b.panParam, plain);
                strip.pan.state.defaultValue = uibind::normOf (b.panParam, pi.def);
                if (std::fabs (plain) < 0.5f)
                    ui::setText (strip.pan.state.valueText, sizeof strip.pan.state.valueText, "C");
                else
                    std::snprintf (strip.pan.state.valueText, sizeof strip.pan.state.valueText, plain < 0.0f ? "L %.0f" : "R %.0f", static_cast<double> (std::fabs (plain)));
                strip.pan.setEnabled (true);
            }
            else
            {
                strip.pan.state.value = 0.5;
                ui::setText (strip.pan.state.valueText, sizeof strip.pan.state.valueText, "-");
                strip.pan.setEnabled (false);
            }

            if (b.destParam >= 0)
            {
                const synthyboy::ParamInfo& pi = uibind::info (b.destParam);
                const int choice = std::clamp (static_cast<int> (inst.param (b.destParam) + 0.5f), 0, std::max (0, pi.numChoices - 1));
                screen_.routingBar.setDestination (s, choice);
                if (pi.choices != nullptr && choice < pi.numChoices)
                    strip.destButton.setLabel (pi.choices[choice]);
                strip.destButton.setEnabled (true);
            }
            else
            {
                strip.destButton.setLabel ("OUT");
                strip.destButton.setEnabled (false);
                screen_.routingBar.setDestination (s, 4);   // master out
            }

            auto send = [&] (ui::SliderBar& bar, int param)
            {
                if (param >= 0)
                {
                    const float plain = inst.param (param);
                    bar.state.value = uibind::normOf (param, plain);
                    bar.state.defaultValue = uibind::normOf (param, uibind::info (param).def);
                    uibind::formatValue (uibind::info (param), plain, bar.state.valueText, sizeof bar.state.valueText);
                    bar.setEnabled (true);
                }
                else
                {
                    bar.state.value = 0.0;
                    ui::setText (bar.state.valueText, sizeof bar.state.valueText, "-");
                    bar.setEnabled (false);
                }
            };
            send (strip.send1, b.send1Param);
            send (strip.send2, b.send2Param);
        }

        // the meters of the engine: the level and a hold line that falls slowly
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
        {
            ui::Fader& f = screen_.strips[s].fader;
            const float pk = in.live != nullptr ? in.live->stripPeak[s] : 0.0f;
            f.state.peak = pk;
            if (pk > f.state.peakHold)
                f.state.peakHold = pk;
            else
                f.state.peakHold = std::max (0.0f, f.state.peakHold - 0.01f);
        }
        inst.setUiFullOpen (true);
        publishQGroup (inst);
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        page::ground (sb);
        screen_.draw (sb);
        if (listOpen_)
            list_.draw (sb);
    }

    bool covers (int x, int y) const noexcept { return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y); }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        ui::Component* top = listOpen_ ? static_cast<ui::Component*> (&list_) : static_cast<ui::Component*> (&screen_);
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            if (! covers (ev.x, ev.y))
                return false;
            gesture_ = top;
            gestureDown_ = true;
            handle (top->touchDown (ev.x, ev.y, nowMs), stack, inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (ev.x, ev.y), stack, inst);
        else
        {
            handle (gesture_->touchUp (ev.x, ev.y), stack, inst);
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
    void forgetQGroup() noexcept { qSet_ = false; }
    void turnQ (int knobIndex, double delta, UiStack& stack, IInstanceView& inst) { handle (screen_.turnQ (knobIndex, delta), stack, inst); }
    bool listOpen() const noexcept { return listOpen_; }

private:
    int w_ = kCanvasW, h_ = kCanvasH;
    ui::MixerFullScreen screen_;
    ui::ListPopup list_;
    bool listOpen_ = false;
    int listDestStrip_ = -1;
    StripBinding bindings_[ui::MixerFullScreen::kStrips];
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;
    int qTargets_[4] = { -1, -1, -1, -1 };
    bool qSet_ = false;

    /** The scale beside a fader from the parameter's own range: in dB the marks 0, -12, -24, -48 that lie in the range (0 is the stronger one), else a mark every 25 %. */
    static void setScale (ui::Fader& f, const synthyboy::ParamInfo& pi)
    {
        f.state.numTicks = 0;
        auto add = [&] (double plain, const char* text)
        {
            if (f.state.numTicks >= ui::Fader::State::kMaxTicks || plain < static_cast<double> (pi.min) - 1.0e-6 || plain > static_cast<double> (pi.max) + 1.0e-6)
                return;
            f.state.tickPos[f.state.numTicks] = static_cast<float> (synthyboy::toNormalised (pi, static_cast<float> (plain)));
            ui::setText (f.state.tickText[f.state.numTicks], sizeof f.state.tickText[0], text);
            ++f.state.numTicks;
        };
        if (pi.unit == synthyboy::Unit::dB)
        {
            add (0.0, "0"); add (-12.0, "-12"); add (-24.0, "-24"); add (-48.0, "-48");
        }
        else
        {
            const double lo = static_cast<double> (pi.min), hi = static_cast<double> (pi.max);
            for (int k = 0; k <= 4; ++k)
            {
                char t[8];
                std::snprintf (t, sizeof t, "%.0f", lo + (hi - lo) * 0.25 * k);
                add (lo + (hi - lo) * 0.25 * k, t);
            }
        }
    }

    void initBindings()
    {
        auto idx = [] (const char* k) { return uibind::indexOfKey (k); };
        bindings_[0] = { idx ("sub.on"), idx ("sub.level"), idx ("sub.pan"), idx ("sub.destination"), idx ("sub.bus1_send"), idx ("sub.bus2_send"), false };
        bindings_[1] = { idx ("osc1.on"), idx ("osc1.level"), idx ("osc1.pan"), idx ("osc1.destination"), idx ("osc1.bus1_send"), idx ("osc1.bus2_send"), false };
        bindings_[2] = { idx ("osc2.on"), idx ("osc2.level"), idx ("osc2.pan"), idx ("osc2.destination"), idx ("osc2.bus1_send"), idx ("osc2.bus2_send"), false };
        bindings_[3] = { idx ("osc3.on"), idx ("osc3.level"), idx ("osc3.pan"), idx ("osc3.destination"), idx ("osc3.bus1_send"), idx ("osc3.bus2_send"), false };
        bindings_[4] = { idx ("noise.on"), idx ("noise.level"), idx ("noise.pan"), idx ("noise.destination"), idx ("noise.bus1_send"), idx ("noise.bus2_send"), false };
        bindings_[5] = { idx ("filter1.on"), idx ("filter1.level"), idx ("filter1.pan"), idx ("filter1.destination"), idx ("filter1.bus1_send"), idx ("filter1.bus2_send"), false };
        bindings_[6] = { idx ("filter2.on"), idx ("filter2.level"), idx ("filter2.pan"), idx ("filter2.destination"), idx ("filter2.bus1_send"), idx ("filter2.bus2_send"), false };
        bindings_[7] = { idx ("bus1.fx_bypass"), idx ("bus1.level"), -1, idx ("bus1.destination"), -1, -1, true };
        bindings_[8] = { idx ("bus2.fx_bypass"), idx ("bus2.level"), -1, idx ("bus2.destination"), -1, -1, true };
        bindings_[9] = { idx ("main.fx_bypass"), idx ("master.volume"), -1, -1, -1, -1, true };
    }

    void publishQGroup (IInstanceView& inst)
    {
        const int s = screen_.activeGroup();
        if (s < 0 || s >= ui::MixerFullScreen::kStrips)
            return;
        const StripBinding& b = bindings_[s];
        const int targets[4] = { b.levelParam, b.panParam, b.send1Param, b.send2Param };
        if (! qSet_ || std::memcmp (targets, qTargets_, sizeof targets) != 0)
        {
            std::memcpy (qTargets_, targets, sizeof targets);
            inst.setQGroup (targets, 4);
            qSet_ = true;
        }
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& inst)
    {
        if (! c)
            return;
        // the list of the destinations
        if (listOpen_)
        {
            if (c.id == kList && c.kind == ui::ChangeKind::Index && listDestStrip_ >= 0)
            {
                const int destParam = bindings_[listDestStrip_].destParam;
                if (destParam >= 0)
                    uibind::writePlain (inst, destParam, c.index);
                listOpen_ = false;
                stack.back();
            }
            else if (c.id == kListBackdrop)
            {
                listOpen_ = false;
                stack.back();
            }
            return;
        }
        const int s = (c.id - kStripBase) / 10, type = (c.id - kStripBase) % 10;
        if (c.id < kStripBase || s < 0 || s >= ui::MixerFullScreen::kStrips)
            return;
        const StripBinding& b = bindings_[s];
        if (type == 0 && c.kind == ui::ChangeKind::Index)   // the switch of the strip: the new state is the choice
        {
            if (b.powerParam >= 0)
                uibind::writePlain (inst, b.powerParam, (c.index != 0) != b.powerInverted ? 1.0 : 0.0);
        }
        else if (type == 1 && c.kind == ui::ChangeKind::Request)   // the destination: the list of its choices
        {
            if (b.destParam >= 0)
            {
                const synthyboy::ParamInfo& pi = uibind::info (b.destParam);
                if (pi.choices != nullptr && pi.numChoices > 0)
                {
                    list_.state.items = pi.choices;
                    list_.state.count = pi.numChoices;
                    list_.state.current = std::clamp (static_cast<int> (inst.param (b.destParam) + 0.5f), 0, pi.numChoices - 1);
                    const ui::Rect btn = screen_.strips[s].destButton.rect();
                    const int popW = 260, popH = std::min (6, static_cast<int> (pi.numChoices)) * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad;
                    const int popX = std::clamp (btn.x - (popW - btn.w) / 2, 10, w_ - popW - 10);
                    const int popY = std::clamp (btn.bottom() + 4, kTabY + 10, kTabY + kTabH - popH - 10);
                    list_.place ({ popX, popY, popW, popH });
                    list_.showCurrent();
                    listOpen_ = true;
                    listDestStrip_ = s;
                    stack.openDetail (kPageList);
                }
            }
        }
        else if (c.kind == ui::ChangeKind::Value)
        {
            const int p = type == 2 ? b.panParam : (type == 3 ? b.send1Param : (type == 4 ? b.send2Param : (type == 5 ? b.levelParam : -1)));
            if (p >= 0)
                uibind::writeNorm (inst, p, c.value);
        }
    }
};

}} // namespace synthyboy_mpc::overlay
