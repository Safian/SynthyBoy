// SynthyBoy - MPC overlay: the MIXER UI, the page of the Mixer tab (the plugin draws the whole tab, page_view.h): ten channel strips (Sub, A, B, C, Noise, Filter 1, Filter 2, Bus 1, Bus 2, Master) over a routing
// bar (shell-mpc/ui/mixer_screen.h, mixer_strip.h) wired to the engine's parameters: the switch, the destination, the pan, the two sends, the level (the fader with its meter). One object per (page view, plugin
// instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw (), with the instance as IInstanceView. Docs: docs/MPC.md "Mixer and FX UI".
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Values: every value is written with the parameter's own format (uibind::formatValue): the level of a strip in % or dB as the parameter is, the pan as C / L 35 / R 35, the sends in %. The meters are the
// engine's LiveValues::stripPeak (own engine: per strip, with a release of 300 ms), a hold line falls slowly from the highest level. The Q groups are the LEVELS only ([SUB, A, B, C], [NOISE, F1, F2, MAIN], [BUS 1, BUS 2]; the strip you touched makes its group the active one, published at the touch; PAN and the sends are touch only); a member the engine has no parameter for
// (the buses and the master have a level only; the master has no destination either) is left out: not drawn, no chip, no touch (hidden, never a dimmed dummy). A tap on the destination opens the list of its choices over the page.
#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../native_ui_params.h"
#include "../ui/list.h"
#include "../ui/mixer_screen.h"
#include "../ui/native_apertures.h"
#include "../ui/ui.h"
#include "filter_ui.h"
#include "fx_ui.h"
#include "instance_view.h"
#include "native_marker_covers.h"
#include "page_view.h"
#include "q_publish.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/mixer_routing.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class MixerUi
{
public:
    static constexpr int kElementPage = 5;          // the page of the Mixer tab (the oscillator is 1, the envelope 2, the LFO 3, the filter 4)
    static constexpr int kElementMixer = kElementPage;
    static constexpr int kPageList = 25, kPagePeek = 26;

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kTabBarH = 64, kEditorH = kTabH - kTabBarH;   // 564 px docked editor above persistent 6-tab bar (y=566..622)

    enum IdBase : int
    {
        kStripBase = 1000,
        kList = 2000,
        kListBackdrop = 2001, kPeekTarget, kPeekClose
    };

    enum class DrawPass : uint8_t { All, LiveOnly };

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
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)   // a member the engine has no parameter for is left out of the strip and of its Q group
        {
            screen_.setStripMembers (s, bindings_[s].levelParam >= 0, bindings_[s].panParam >= 0, bindings_[s].send1Param >= 0, bindings_[s].send2Param >= 0);
            screen_.strips[s].setHasDestination (bindings_[s].destParam >= 0);   // the MASTER has none: no OUT button
            if (bindings_[s].powerInverted)
            {
                const char* const rackLabels[] = { "BUS 1 FX", "BUS 2 FX", "MAIN FX" };
                screen_.strips[s].power.setLabel (rackLabels[s - 7]);
            }
        }
        screen_.assignIds (kStripBase);
        screen_.routingBar.setId (kPeekTarget);
        peekClose_.setId (kPeekClose);
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
        placePeek();
    }

    ui::MixerFullScreen& screen() noexcept { return screen_; }
    const ui::MixerFullScreen& screen() const noexcept { return screen_; }
    ui::ListPopup& list() noexcept { return list_; }
    const ui::ListPopup& list() const noexcept { return list_; }
    const StripBinding& binding (int strip) const noexcept { return bindings_[strip]; }
    int activeTab() const noexcept { return activeTab_; }
    ui::Rect peekPanelRect() const noexcept
    {
        return nativePeek_ ? ui::Rect { 0, kTabY, w_, kEditorH }
                           : ui::Rect { 8, kTabY + 24, w_ - 16, kTabH - 48 };
    }
    ui::Rect peekCloseRect() const noexcept { return peekClose_.rect(); }
    ui::MenuButton& peekClose() noexcept { return peekClose_; }
    ui::Component* peekRoot() noexcept { return peek_ ? &peek_->peekRoot() : nullptr; }

    bool nativeWidgetsEnabled() const noexcept { return nativeWidgetsEnabled_; }
    void setNativeWidgetsEnabled (bool on) noexcept { nativeWidgetsEnabled_ = on; syncNativeGround(); }

    bool hostConfirmed() const noexcept
    {
        return &ui::theme::L() == &ui::theme::kLook1 && drawPass_ == DrawPass::LiveOnly;
    }

    bool nativeBase() const noexcept { return useNativeWidgets() && ! peek_ && ! listOpen_; }

    bool isNativeOwned (int strip, int controlType) const noexcept
    {
        if (! nativeBase() || strip < 0 || strip >= ui::MixerFullScreen::kStrips)
            return false;
        const auto& b = bindings_[strip];
        const int targets[] = { b.powerParam, b.destParam, b.panParam, b.send1Param, b.send2Param, b.levelParam };
        return controlType >= 0 && controlType < 6 && targets[controlType] >= 0
            && (controlType == 0 || b.powerInverted || screen_.strips[strip].power.state.on);
    }

    int topPage() const noexcept
    {
        if (peek_)
            return peek_->topPage() ? peek_->topPage() : kPagePeek;
        return listOpen_ ? kPageList : 0;
    }
    const int* qTargets() const noexcept { return peek_ ? peek_->qTargets() : qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return peek_ ? peek_->qGroupName() : screen_.groupName (screen_.activeGroup()); }

    void setHostBackground (bool on) noexcept
    {
        drawPass_ = on ? DrawPass::LiveOnly : DrawPass::All;
        syncNativeGround();
    }
    bool hostBackground() const noexcept { return hostConfirmed() && ! peek_ && ! listOpen_; }

    /** Reads the instance: every value of every strip, the meters, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, UiStack& stack, double nowMs, const page::Input& in)
    {
        const bool available = inst.nativeMixerWidgetsAvailable();
        if (nativeWidgetsAvailable_ != available)
        {
            nativeWidgetsAvailable_ = available;
            dropGesture();
        }
        if (peek_)
        {
            if (stack.id (1) == kPagePeek)
            {
                updatePeek (inst, stack, nowMs, in);
                return;
            }
            dismissPeek (inst); // hardware Back or a new host tab discarded the detail
        }
        inst.requestMeters();   // the source strips of this page: the engine measures them only while a page asks (every update of the page)
        if (listOpen_ && ! (stack.depth() >= 2 && stack.top() == kPageList))
        {
            dropGesture();
            listOpen_ = false;
            listDestStrip_ = -1;
        }
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
            const bool moduleOn = b.powerInverted || strip.power.state.on;
            strip.setInactive (! moduleOn);
            strip.power.setInactive (false);

            if (b.levelParam >= 0)
            {
                const synthyboy::ParamInfo& pi = uibind::info (b.levelParam);
                const float plain = inst.param (b.levelParam);
                strip.fader.state.value = strip.fader.state.modValue = uibind::normOf (b.levelParam, plain);
                strip.fader.state.defaultValue = uibind::normOf (b.levelParam, pi.def);
                uibind::formatValue (pi, plain, strip.fader.state.valueText, sizeof strip.fader.state.valueText);   // the parameter's own unit: % or dB
                setScale (strip.fader, pi, s >= 7);
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
            else   // the MASTER: no destination (its output is the plugin output): no button at all, not a greyed-out dummy
                screen_.routingBar.setDestination (s, 4);   // master out

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
            if (! moduleOn)
            {
                strip.fader.setEnabled (false);
                strip.pan.setEnabled (false);
                strip.destButton.setEnabled (false);
                strip.send1.setEnabled (false);
                strip.send2.setEnabled (false);
            }
        }

        // the meters of the engine: live level and peak hold for every channel strip (all 10 channels)
        const float dt = lastMs_ > 0.0 ? static_cast<float> (nowMs - lastMs_) * 0.001f : 0.0f;
        lastMs_ = nowMs;
        const float fall = 0.5f * std::clamp (dt, 0.0f, 0.2f);
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
        {
            ui::Fader& f = screen_.strips[s].fader;
            const float readout = in.live != nullptr ? in.live->stripPeak[s] : 0.0f;
            const float pk = std::isfinite (readout) ? readout : 0.0f;
            f.state.peak = pk;
            f.state.hasPeakHold = true;
            f.state.peakHold = std::max (pk, f.state.peakHold - fall);
            if (nativeBase())
            {
                inst.setNativeMixerMeter (s, pk, f.state.peakHold);
                float stereo[6] {};
                if (in.live != nullptr)
                {
                    stereo[0] = in.live->stripPeakL[s]; stereo[1] = in.live->stripPeakR[s];
                    stereo[2] = in.live->stripHoldL[s]; stereo[3] = in.live->stripHoldR[s];
                    stereo[4] = in.live->stripClipL[s]; stereo[5] = in.live->stripClipR[s];
                }
                inst.setNativeMixerStereoMeter (s, stereo);
            }
        }
        inst.setUiFullOpen (true);
        syncNativeGround();
        publishStripVisuals (inst);
        inst.setNativeMixerActiveTab (0);
        inst.setNativeMixerPeekEnabled (nativeBase() && ! listOpen_);
        if (nativeBase())
        {
            const int request = inst.takeNativeMixerPeek();
            if (request >= 0 && request < nativeui::kMixerTabs)
            {
                handleTabRequest (request, stack, inst);
                if (peek_)
                    return;
            }
        }
        publishQGroup (inst);
    }

    // The Mixer paints its own ground with native cut-outs or a PEEK backdrop.
    void drawPageGround (gpu::SceneBuilder&) const noexcept {}

    void draw (gpu::SceneBuilder& sb) const
    {
        if (peek_)
        {
            if (nativePeek_ && peek_->topPage() == 0)
            {
                const ui::Rect editor = peekPanelRect();
                ui::Cell clip (sb, editor);
                ui::Pen pen (sb, true);
                const ui::theme::Look& t = ui::theme::L();
                pen.rect (editor, pen.c (t.bg0, 255));
                peek_->clearNativePeekApertures (sb);
                if (peek_->peekCloseVisible())
                    peekClose_.draw (sb);
                peek_->draw (sb);
                return;
            }
            {
                const ui::Rect tab = screen_.rect();
                ui::Cell backdrop (sb, tab, 0);
                sb.rect (tab.x, tab.y, tab.right(), tab.bottom(), gpu::rgba8 (0, 0, 0, 255));
                sb.backdropBegin();
                page::ground (sb);
                screen_.draw (sb);
                sb.backdropEnd (tab.x, tab.y, tab.right(), tab.bottom());
            }
            // Keep the frozen backdrop in its own cache layer. Changes to the panel
            // then redraw its controls without repainting the Mixer or repeating the blur.
            ui::Pen pen (sb, true);
            const ui::theme::Look& t = ui::theme::L();
            pen.rect (peekPanelRect(), pen.c (t.bg0, 255));
            peek_->clearNativePeekApertures (sb);
            {
                ui::Cell frame (sb, peekPanelRect());
                pen.rrect (peekPanelRect(), t.rBlock, pen.c (t.keylineHi, 255), t.frame);
            }
            if (peek_->peekCloseVisible())
                peekClose_.draw (sb);
            peek_->draw (sb);
        }
        else
        {
            if (! nativeBase())
            {
                page::ground (sb);
                screen_.draw (sb);
            }
            else if (! hostBackground())
            {
                // A native channel strip includes its header, meters and controls.
                std::array<ui::Rect, ui::MixerFullScreen::kStrips + 1> holes;
                for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
                    holes[s] = screen_.strips[s].rect();
                holes.back() = { 10, kTabY + 566, kCanvasW - 20, 56 };
                ui::drawNativeCanvas (sb, screen_.rect(), holes);
            }
            if (nativeBase())
                drawNativeMarkerCovers (sb, "mixer");
            if (listOpen_)
                list_.draw (sb);
        }
    }

    /** Native strip widgets must be left to the host at the page router too.
        Returning true here used to let the overlay claim the whole Mixer tab;
        the direct touch method then yielded the event, but the host never got
        the event through the normal page-view path. Modal PEEK and destination
        popups reclaim the full tab so inputs never leak to underlying native controls. */
    bool covers (int x, int y) const noexcept
    {
        if (! ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y))
            return false;
        if (peek_)
        {
            if (nativePeek_ && peek_->topPage() == 0 && ! peekPanelRect().contains (x, y))
                return false;
            return ! nativePeek_ || ! peekPanelRect().contains (x, y) || (peek_->peekCloseVisible() && peekCloseRect().contains (x, y))
                || peek_->covers (x, y);
        }
        if (listOpen_)
            return true;
        if (nativeBase())
            return false;
        return ! nativeControlAt (x, y) && ! nativePeekAt (x, y);
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        if (peek_)
            return touchPeek (ev, stack, inst, nowMs);
        if (listOpen_)
        {
            ui::Component* top = &list_;
            if (ev.kind == TouchEvent::Down)
            {
                gesture_ = nullptr;
                gestureDown_ = false;
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

        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            gestureDown_ = false;
            // Keep the direct overlay API useful for selection bookkeeping on
            // a native rectangle, while the page router still sees covers()
            // false and dispatches the actual gesture to the host.
            if (! covers (ev.x, ev.y) && ! nativeControlAt (ev.x, ev.y))
                return false;

            // 1. Taps on the routing bar
            if (screen_.routingBar.rect().contains (ev.x, ev.y))
            {
                gesture_ = &screen_.routingBar;
                gestureDown_ = true;
                handle (screen_.routingBar.touchDown (ev.x, ev.y, nowMs), stack, inst);
                return true;
            }

            // 2. Taps on channel strips: selection and single input ownership
            for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
            {
                ui::MixerStrip& st = screen_.strips[s];
                if (st.rect().contains (ev.x, ev.y))
                {
                    if (screen_.selectedStrip() != s)
                    {
                        screen_.selectStrip (s);
                        publishQGroup (inst);
                    }

                    // Single input ownership: available strip bindings with real native widgets
                    // yield touch directly to the host native control.
                    if (nativeControlAt (ev.x, ev.y))
                        return false;

                    break;
                }
            }

            gesture_ = &screen_;
            gestureDown_ = true;
            handle (screen_.touchDown (ev.x, ev.y, nowMs), stack, inst);
            publishQGroup (inst);   // a tap on a strip made it the selected one: its four targets are the Q group at once, not at the next update
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

    bool nativeControlAt (int x, int y) const noexcept
    {
        bool found = false;
        visitNativeStripCells ([&] (ui::Rect cell) { found |= cell.contains (x, y); });
        return found;
    }

    bool nativePeekAt (int x, int y) const noexcept
    {
        if (nativeBase())
            for (int i = 0; i < nativeui::kMixerPeekTargets; ++i)
                if (screen_.routingBar.peekRect (i).contains (x, y))
                    return true;
        return false;
    }

    void leftTab (IInstanceView& inst) noexcept
    {
        (void) inst.takeNativeMixerPeek();
        activeTab_ = 0;
        inst.setNativeMixerActiveTab (0);
        inst.setNativeMixerPeekEnabled (false);
        inst.setNativeMixerPeekView (nativeui::MixerBase);
        if (peek_)
            peek_->leftTab (inst);
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
        {
            inst.setNativeMixerMeter (s, 0.0f, 0.0f);
            const float stereo[6] {};
            inst.setNativeMixerStereoMeter (s, stereo);
        }
    }

    void setNativeActionPopup (IInstanceView& inst, int action) noexcept
    {
        if (nativePeek_ && peek_)
            peek_->setNativeActionPopup (inst, action);
    }

    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (peek_)
        {
            peek_->tick (nowMs, stack, inst);
            if (! peek_->nativePeekReady())
                inst.setNativeMixerPeekView (nativeui::MixerBase);
            return;
        }
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst);
    }

    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the control of the page under the pointer takes it as steps of its own (a knob: one Q-Link step, a stepper, a list, a scrolled
        list; ui::Component::wheelOver). false: nothing of this page lies there, or a finger is down on it. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        if (peek_)
        {
            if (! covers (x, y) || fingerDown())
                return false;
            if (peek_->topPage() || ((! peek_->peekCloseVisible() || ! peekCloseRect().contains (x, y)) && peekPanelRect().contains (x, y)))
                peek_->wheel (x, y, notches, stack, inst, nowMs);
            return true; // the backdrop also consumes the wheel
        }
        ui::Component* top = listOpen_ ? static_cast<ui::Component*> (&list_) : static_cast<ui::Component*> (&screen_);
        if (gestureDown_ || ! top || ! covers (x, y))
            return false;

        ui::Change ch;
        if (! top->wheelOver (x, y, notches, ch))
            return listOpen_;
        handle (ch, stack, inst);
        (void) nowMs;
        return true;
    }

    bool fingerDown() const noexcept { return gestureDown_ || (peek_ && peek_->fingerDown()); }
    void dropGesture() noexcept
    {
        if (gesture_)
            gesture_->touchUp (-1, -1);
        gesture_ = nullptr;
        gestureDown_ = false;
        if (peek_)
            peek_->dropGesture();
    }
    void forgetQGroup() noexcept
    {
        qSlots_.forget();
        if (peek_)
            peek_->forgetQGroup();
    }
    void turnQ (int knobIndex, double delta, UiStack& stack, IInstanceView& inst)
    {
        if (peek_)
            peek_->turnQ (knobIndex, delta, stack, inst);
        else
            handle (screen_.turnQ (knobIndex, delta), stack, inst);
    }
    bool listOpen() const noexcept { return listOpen_; }

private:
    int w_ = kCanvasW, h_ = kCanvasH;
    bool nativeWidgetsEnabled_ = true;
    bool nativeWidgetsAvailable_ = false;
    ui::MixerFullScreen screen_;
    DrawPass drawPass_ = DrawPass::All;
    ui::ListPopup list_;
    bool listOpen_ = false;
    int listDestStrip_ = -1;
    StripBinding bindings_[ui::MixerFullScreen::kStrips];
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;
    QSlots qSlots_;
    double lastMs_ = 0.0;

    std::unique_ptr<PeekModule> peek_;
    int peekTarget_ = -1;
    int activeTab_ = 0;
    bool nativePeek_ = false;
    ui::TapGesture backdropTap_;
    ui::MenuButton peekClose_ { "CLOSE", ui::MenuButton::Icon::None };

    bool useNativeWidgets() const noexcept
    {
        return nativeWidgetsEnabled_ && w_ == page::kW && h_ == page::kH
            && (nativeWidgetsAvailable_ || hostConfirmed());
    }

    template <typename Visit>
    void visitNativeStripCells (Visit&& visit) const
    {
        if (! nativeBase())
            return;
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
        {
            const auto& strip = screen_.strips[s];
            const ui::Rect cells[] { strip.power.rect(), strip.destButton.rect(), strip.pan.rect(),
                                     strip.send1.rect(), strip.send2.rect(), strip.fader.rect() };
            for (int control = 0; control < 6; ++control)
                if (isNativeOwned (s, control))
                    visit (cells[control]);
        }
    }

    void syncNativeGround() noexcept
    {
        const bool hostBg = hostBackground();
        screen_.routingBar.nativeGround = hostBg || nativeBase();
        screen_.routingBar.nativePeek = nativeBase();
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
            screen_.strips[s].nativeGround = hostBg;
    }

    void placePeek()
    {
        const auto panel = peekPanelRect();
        if (peek_)
            peek_->placePeek (panel);
        const auto close = nativePeek_ && peek_ ? peek_->nativePeekCloseRect()
            : ui::Rect { panel.x + 8, panel.y + ui::kQLane, PeekModule::kCloseWidth, 56 };
        peekClose_.place (close);
    }

    void updatePeekTitle()
    {
        static const char* const titles[] { "FLT 1", "FLT 2", "MAIN FX", "BUS 1 FX", "BUS 2 FX" };
        peekClose_.setValue (nativePeek_ && peekTarget_ >= 2 ? "" : titles[peekTarget_]);
    }

    void publishStripVisuals (IInstanceView& inst) noexcept
    {
        uint32_t soloMask = 0, muteMask = 0;
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
        {
            if (inst.mixerSolo (s)) soloMask |= (1u << s);
            if (inst.mixerMute (s)) muteMask |= (1u << s);
        }
        const auto route = synthyboy::computeMixerRouteState (soloMask, muteMask, [&] (int idx) noexcept {
            return inst.param (idx);
        });
        for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
            inst.setNativeMixerStripVisuals (s, route.soloVisual[s], route.muteVisual[s]);
    }

    void handleTabRequest (int tab, UiStack& stack, IInstanceView& inst)
    {
        if (tab < 0 || tab >= nativeui::kMixerTabs || tab == activeTab_)
            return;
        if (tab == 0)
        {
            if (peek_)
            {
                if (stack.id (1) == kPagePeek)
                    stack.back();
                dismissPeek (inst);
            }
            else
            {
                activeTab_ = 0;
                inst.setNativeMixerActiveTab (0);
                inst.setNativeMixerPeekEnabled (nativeBase() && ! listOpen_);
            }
            publishQGroup (inst);
            return;
        }
        const int target = tab - 1;
        if (! peek_)
        {
            if (! stack.openDetail (kPagePeek))
                return;
        }
        else
        {
            peek_->leftTab (inst);
            qSlots_.forget();
        }
        peek_ = target >= 2 ? std::unique_ptr<PeekModule> (std::make_unique<FxUi> (target - 2))
                            : std::unique_ptr<PeekModule> (std::make_unique<FilterUi> (target));
        peekTarget_ = target;
        activeTab_ = tab;
        nativePeek_ = useNativeWidgets();
        peek_->setNativePeek (nativePeek_);
        updatePeekTitle();
        placePeek();
        syncNativeGround();
        inst.setNativeMixerActiveTab (activeTab_);
        inst.setNativeMixerPeekEnabled (nativePeek_ && peek_->topPage() == 0);
        updatePeek (inst, stack, lastMs_, page::Input{});
    }

    void updatePeek (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        publishStripVisuals (inst);
        const bool native = useNativeWidgets();
        const bool modeChanged = native != nativePeek_;
        if (modeChanged)
        {
            if (! native)
                peek_->leftTab (inst);
            nativePeek_ = native;
            peek_->setNativePeek (native);
            placePeek();
        }
        if (nativePeek_ && peek_->topPage() == 0)
        {
            const int request = inst.takeNativeMixerPeek();
            if (request >= 0 && request < nativeui::kMixerTabs && request != activeTab_)
            {
                handleTabRequest (request, const_cast<UiStack&> (stack), inst);
                return;
            }
        }
        peek_->update (inst, stack, nowMs, in);
        if (nativePeek_)
            peekClose_.place (peek_->nativePeekCloseRect());   // keep close aligned with FX chain bar across rack switches
        if (modeChanged)
            updatePeekTitle();
        inst.setNativeMixerActiveTab (activeTab_);
        inst.setNativeMixerPeekEnabled (nativePeek_ && peek_->topPage() == 0);
        inst.setNativeMixerPeekView (peek_->nativePeekReady()
            ? (peekTarget_ < 2 ? nativeui::MixerPeekFilter : nativeui::MixerPeekFx) : nativeui::MixerBase);
        inst.setUiFullOpen (true);
    }

    void dismissPeek (IInstanceView& inst)
    {
        (void) inst.takeNativeMixerPeek();
        inst.setNativeMixerPeekView (nativeui::MixerBase);
        peek_->leftTab (inst);
        peek_.reset();
        peekTarget_ = -1;
        activeTab_ = 0;
        nativePeek_ = false;
        backdropTap_.reset();
        gesture_ = nullptr;
        gestureDown_ = false;
        qSlots_.forget(); // another page published its own group
        syncNativeGround();
        inst.setNativeMixerActiveTab (0);
        inst.setNativeMixerPeekEnabled (nativeBase() && ! listOpen_);
    }

    bool touchPeek (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            gestureDown_ = false;
            if (! screen_.rect().contains (ev.x, ev.y))
                return false;
            if (nativePeek_ && ! peek_->topPage() && ! peekPanelRect().contains (ev.x, ev.y))
                return false;
            if (peek_->peekCloseVisible() && peekCloseRect().contains (ev.x, ev.y))
            {
                gesture_ = &peekClose_;
                gestureDown_ = true;
                peekClose_.touchDown (ev.x, ev.y, nowMs);
                return true;
            }
            if (! peek_->topPage() && ! peekPanelRect().contains (ev.x, ev.y))
            {
                backdropTap_.down (ev.x, ev.y, nowMs);
                gestureDown_ = true; // never pass the dismissing gesture to a Mixer control
                return true;
            }
        }
        if (gestureDown_)
        {
            if (ev.kind == TouchEvent::Move)
            {
                if (gesture_)
                    gesture_->touchMove (ev.x, ev.y);
                else
                    backdropTap_.move (ev.x, ev.y);
            }
            if (ev.kind == TouchEvent::Up)
            {
                if (! gesture_)
                    backdropTap_.move (ev.x, ev.y); // an input device may report a new coordinate only on Up
                const bool close = gesture_ ? gesture_->touchUp (ev.x, ev.y) && peekCloseRect().contains (ev.x, ev.y)
                                            : backdropTap_.up() && covers (ev.x, ev.y) && ! peekPanelRect().contains (ev.x, ev.y);
                gesture_ = nullptr;
                gestureDown_ = false;
                if (close)
                {
                    stack.back();
                    dismissPeek (inst);
                    publishQGroup (inst);
                }
            }
            return true;
        }
        // The child still observes a native Filter down for its modulation long-
        // press and generation guard. Empty or disabled areas remain modal.
        const bool owned = covers (ev.x, ev.y);
        return peek_->touch (ev, stack, inst, nowMs) || (ev.kind == TouchEvent::Down && owned);
    }

    /** The scale beside a fader from the parameter's own range: in dB the marks 0, -12, -24, -48 that lie in the range (0 is the stronger one), else a mark every 25 %.
        Full-height faders (fine = true) use a finer dB scale with marks every 6 dB down to -24 and -36, -48. */
    static void setScale (ui::Fader& f, const synthyboy::ParamInfo& pi, bool fine)
    {
        f.state.numTicks = 0;
        f.state.isFine = fine;
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
            if (fine)
            {
                add (+6.0, "+6"); add (0.0, "0"); add (-6.0, "-6"); add (-12.0, "-12"); add (-18.0, "-18"); add (-24.0, "-24"); add (-36.0, "-36"); add (-48.0, "-48");
            }
            else
            {
                add (0.0, "0"); add (-12.0, "-12"); add (-24.0, "-24"); add (-48.0, "-48");
            }
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

    /** The groups of the page are the LEVELS of the strips (ui/mixer_screen.h): [SUB, A, B, C], [NOISE, F1, F2, MAIN], [BUS 1, BUS 2]. The active group is on Q 1 .. 4 of the Key 61 (the banks follow in order); the XL keeps them on 1 .. 4, 5 .. 8, 9 .. 10.
        PAN, the sends and the destinations are touch only. */
    void publishQGroup (IInstanceView& inst)
    {
        screen_.syncDeviceLayout();
        const int a = screen_.activeGroup();
        if (a < 0 || a >= ui::MixerFullScreen::kMaxGroups)
            return;
        constexpr int kN = ui::MixerFullScreen::kMaxGroups;
        for (int g = 0; g < kN; ++g)
        {
            auto& group = screen_.groups[g];
            for (int k = 0; k < ui::QGroup::kSlots; ++k)
            {
                const int strip = ui::MixerFullScreen::stripOfSlot (g, k);
                auto* fader = strip >= 0 && screen_.strips[strip].fader.enabled() ? &screen_.strips[strip].fader : nullptr;
                if (group.member (k) != fader)
                    group.set (k, fader);
            }
            group.setBlock (QRouter::blockOfGroup (kN, a, g));
        }
        qSlots_.publish (inst, kN, a, [this] (int g, int* t)
        {
            for (int k = 0; k < ui::QGroup::kSlots; ++k)
            {
                const int strip = ui::MixerFullScreen::stripOfSlot (g, k);
                t[k] = screen_.groups[g].member (k) != nullptr ? bindings_[strip].levelParam : -1;
            }
        });
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& inst)
    {
        if (! c)
            return;
        if (c.id == kPeekTarget && c.kind == ui::ChangeKind::Index
            && c.index >= 0 && c.index < nativeui::kMixerPeekTargets)
        {
            handleTabRequest (c.index + 1, stack, inst);
            return;
        }
        // the list of the destinations
        if (listOpen_)
        {
            if (c.id == kList && c.kind == ui::ChangeKind::Index && listDestStrip_ >= 0)
            {
                const StripBinding& b = bindings_[listDestStrip_];
                if (b.destParam >= 0 && (b.powerInverted || inst.param (b.powerParam) >= 0.5f))
                    uibind::writePlain (inst, b.destParam, c.index);
                listOpen_ = false;
                listDestStrip_ = -1;
                stack.back();
            }
            else if (c.id == kListBackdrop)
            {
                listOpen_ = false;
                listDestStrip_ = -1;
                stack.back();
            }
            return;
        }
        const int s = (c.id - kStripBase) / 10, type = (c.id - kStripBase) % 10;
        if (c.id < kStripBase || s < 0 || s >= ui::MixerFullScreen::kStrips)
            return;
        const StripBinding& b = bindings_[s];
        if (type != 0 && ! b.powerInverted && inst.param (b.powerParam) < 0.5f)
            return;
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
