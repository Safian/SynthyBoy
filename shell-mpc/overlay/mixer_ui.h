// SynthyBoy - MPC overlay: the MIXER UI, the page of the Mixer tab (the plugin draws the whole tab, page_view.h): ten channel strips (Sub, A, B, C, Noise, Filter 1, Filter 2, Bus 1, Bus 2, Master) over the whole
// tab (the native skin strips; this page holds only the parameter rows, the Q banks, the meters and the native bridge) wired to the engine's parameters: the switch, the destination, the pan, the two sends, the level (the fader with its meter). One object per (page view, plugin
// instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw (), with the instance as IInstanceView. Docs: docs/MPC.md "Mixer and FX UI".
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Values: every value is written with the parameter's own format (uibind::formatValue): the level of a strip in % or dB as the parameter is, the pan as C / L 35 / R 35, the sends in %. The meters are the
// engine's LiveValues::stripPeak (own engine: per strip, with a release of 300 ms), a hold line falls slowly from the highest level. The Q groups are the LEVELS only ([SUB, A, B, C], [NOISE, F1, F2, MAIN], [BUS 1, BUS 2]; the strip you touched makes its group the active one, published at the touch; PAN and the sends are touch only); a member the engine has no parameter for
// (the buses and the master have a level only; the master has no destination either) is left out: not drawn, no chip, no touch (hidden, never a dimmed dummy). The destination is a native dropdown (the host list); the page draws no control (owner decision 2026-11-27).
#pragma once

#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../native_ui_params.h"
#include "../ui/binding.h"
#include "../ui/native_apertures.h"
#include "../ui/qgroup.h"
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

/** The row index of control `type` (0 power, 1 destination, 2 pan, 3 send 1, 4 send 2, 5 level) of strip `strip`: six rows per strip. */
constexpr int mixerRow (int strip, int type) noexcept { return strip * 6 + type; }

class MixerUi
{
public:
    static constexpr int kElementPage = 5;          // the page of the Mixer tab (the oscillator is 1, the envelope 2, the LFO 3, the filter 4)
    static constexpr int kElementMixer = kElementPage;

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kStrips = 10, kMaxGroups = 3;

    enum IdBase : int { kStripBase = 1000 };
    /** The control types of a strip: the id of a row is kStripBase + 10 * strip + type. */
    enum Type : int { kPower = 0, kDest = 1, kPan = 2, kSend1 = 3, kSend2 = 4, kLevel = 5, kTypes = 6 };

    enum class DrawPass : uint8_t { All, LiveOnly };

    // ---- the parameter rows: ten strips x six controls (row index = 6 * strip + type) --------------------------------------------------------------------------------------------------------------
    /** Q-Link: only the LEVELS are slots (the strip's slot of its group, see kQ); PAN, the sends, the destination and the switch are touch only. */
#define SB_MIX_STRIP(S, PWR, PWRINV, DEST, PAN, B1, B2, LEVEL) \
    binding::Row { 1000 + 10 * (S) + 0, PWR,  binding::Kind::Toggle,     0, 0, 0, 0, binding::Format::Engine, nullptr, 0, -1, -1, PWRINV }, \
    binding::Row { 1000 + 10 * (S) + 1, DEST, binding::Kind::Choice,     0, 0, 0, 0, binding::Format::ChoiceName, nullptr, 0, -1, -1, false }, \
    binding::Row { 1000 + 10 * (S) + 2, PAN,  binding::Kind::Continuous, 0, 0, 0, 0, binding::Format::PanLR, nullptr, 0, -1, -1, false }, \
    binding::Row { 1000 + 10 * (S) + 3, B1,   binding::Kind::Continuous, 0, 0, 0, 0, binding::Format::Engine, nullptr, 0, -1, -1, false }, \
    binding::Row { 1000 + 10 * (S) + 4, B2,   binding::Kind::Continuous, 0, 0, 0, 0, binding::Format::Engine, nullptr, 0, -1, -1, false }, \
    binding::Row { 1000 + 10 * (S) + 5, LEVEL, binding::Kind::Continuous, 0, 0, 0, 0, binding::Format::Engine, nullptr, 0, -1, -1, false }
#define SB_MIX_SRC(S, P) SB_MIX_STRIP (S, P ".on", false, P ".destination", P ".pan", P ".bus1_send", P ".bus2_send", P ".level")
#define SB_MIX_BUS(S, P, LEVEL) SB_MIX_STRIP (S, P ".fx_bypass", true, P ".destination", "", "", "", LEVEL)
    static constexpr binding::Row kRows[kStrips * kTypes] = {
        SB_MIX_SRC (0, "sub"), SB_MIX_SRC (1, "osc1"), SB_MIX_SRC (2, "osc2"), SB_MIX_SRC (3, "osc3"), SB_MIX_SRC (4, "noise"),
        SB_MIX_SRC (5, "filter1"), SB_MIX_SRC (6, "filter2"),
        SB_MIX_BUS (7, "bus1", "bus1.level"), SB_MIX_BUS (8, "bus2", "bus2.level"),
        SB_MIX_STRIP (9, "main.fx_bypass", true, "", "", "", "", "master.volume")
    };
#undef SB_MIX_BUS
#undef SB_MIX_SRC
#undef SB_MIX_STRIP
    static constexpr int rowOf (int strip, int type) noexcept { return mixerRow (strip, type); }

    /** The Q banks as row indices (the LEVEL rows). Key 61: [SUB A B C] [NOISE F1 F2 MAIN] [BUS 1 BUS 2]; the XL (sixteen knobs): ten strips in order, four per bank. */
    static constexpr binding::QTable<kMaxGroups> kQ4 { { { mixerRow (0, 5), mixerRow (1, 5), mixerRow (2, 5), mixerRow (3, 5) },
                                                       { mixerRow (4, 5), mixerRow (5, 5), mixerRow (6, 5), mixerRow (9, 5) },
                                                       { mixerRow (7, 5), mixerRow (8, 5), -1, -1 } } };
    static constexpr binding::QTable<kMaxGroups> kQ16 { { { mixerRow (0, 5), mixerRow (1, 5), mixerRow (2, 5), mixerRow (3, 5) },
                                                        { mixerRow (4, 5), mixerRow (5, 5), mixerRow (6, 5), mixerRow (7, 5) },
                                                        { mixerRow (8, 5), mixerRow (9, 5), -1, -1 } } };
    static const binding::QTable<kMaxGroups>& qTable() noexcept { return ui::QGroup::wideDevice() ? kQ16 : kQ4; }
    static int stripOfSlot (int group, int slot) noexcept
    {
        const int r = qTable().row (group, slot);
        return r < 0 ? -1 : r / kTypes;
    }
    static int groupOfStrip (int strip) noexcept
    {
        int g = 0, s = 0;
        return qTable().find (rowOf (strip, kLevel), g, s) ? g : 0;
    }

    /** The engine parameter indices of one strip (resolved from the row keys; -1: the engine has no such parameter). */
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
        setCanvas (kCanvasW, kCanvasH);
        selectStrip (0);
    }

    void setCanvas (int w, int h) { w_ = w; h_ = h; }

    const StripBinding& binding (int strip) const noexcept { return bindings_[strip]; }
    int activeGroup() const noexcept { return activeGroup_; }
    void setActiveGroup (int g)
    {
        if (g < 0 || g >= kMaxGroups)
            return;
        selectStrip (groupOfStrip (selectedStrip_) == g ? selectedStrip_ : std::max (0, stripOfSlot (g, 0)));
    }
    /** A strip is selected: the group of its level is the active one. */
    void selectStrip (int strip) noexcept
    {
        if (strip < 0 || strip >= kStrips)
            return;
        selectedStrip_ = strip;
        activeGroup_ = groupOfStrip (strip);
    }

    // ---- the geometry of the native cells (the rectangles of the host skin the page leaves open) ------------------------------------------------------------------------------------------------
    static constexpr int kCellH = 56, kQLane = 16, kPad = 4;
    ui::Rect tabRect() const noexcept { return { 0, kTabY, w_, kTabH }; }
    ui::Rect stripRect (int s) const noexcept
    {
        const int pad = 6, topY = kTabY + 10, gap = 4;
        const int stripH = std::max (100, kTabH - 10 - 12);
        const int stripW = (w_ - 2 * pad - (kStrips - 1) * gap) / kStrips;
        return { pad + s * (stripW + gap), topY, stripW, stripH };
    }
    /** The cell of a control of strip s (an empty rectangle: the strip has no such control). */
    ui::Rect cellRect (int s, int type) const noexcept
    {
        const StripBinding& b = bindings_[s];
        const ui::Rect r = stripRect (s);
        const int innerW = r.w - 2 * kPad;
        int y = r.y + kPad;
        const ui::Rect power { r.x + kPad, y, innerW, kCellH };
        y += kCellH + kPad;
        const ui::Rect dest = b.destParam >= 0 ? ui::Rect { r.x + kPad, y, innerW, kCellH } : ui::Rect {};
        y += kCellH + kPad + kQLane;
        const bool lower = b.panParam >= 0 || b.send1Param >= 0 || b.send2Param >= 0;
        if (type == kPower) return power;
        if (type == kDest) return dest;
        if (! lower)
            return type == kLevel ? ui::Rect { r.x + kPad, y, innerW, std::max (96, r.bottom() - kPad - y) } : ui::Rect {};
        const int bottomY = r.bottom() - kPad - 3 * kCellH - 3 * kQLane;
        const ui::Rect fader { r.x + kPad, y, innerW, std::max (96, bottomY - y) };
        int ly = std::max (y + 96, bottomY) + kQLane;
        const ui::Rect pan { r.x + kPad, ly, innerW, kCellH };
        ly += kCellH + kQLane;
        const ui::Rect send1 { r.x + kPad, ly, innerW, kCellH };
        ly += kCellH + kQLane;
        const ui::Rect send2 { r.x + kPad, ly, innerW, kCellH };
        switch (type)
        {
            case kPan: return b.panParam >= 0 ? pan : ui::Rect {};
            case kSend1: return b.send1Param >= 0 ? send1 : ui::Rect {};
            case kSend2: return b.send2Param >= 0 ? send2 : ui::Rect {};
            case kLevel: return b.levelParam >= 0 ? fader : ui::Rect {};
            default: return {};
        }
    }


    bool hostConfirmed() const noexcept
    {
        return &ui::theme::L() == &ui::theme::kLook1 && drawPass_ == DrawPass::LiveOnly;
    }

    bool nativeBase() const noexcept { return useNativeCells(); }

    bool isNativeOwned (int strip, int controlType) const noexcept
    {
        if (! nativeBase() || strip < 0 || strip >= kStrips)
            return false;
        const auto& b = bindings_[strip];
        const int targets[] = { b.powerParam, b.destParam, b.panParam, b.send1Param, b.send2Param, b.levelParam };
        return controlType >= 0 && controlType < kTypes && targets[controlType] >= 0
            && (controlType == 0 || b.powerInverted || powerOn_[strip]);
    }

    int topPage() const noexcept { return 0; }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return groupName (activeGroup_); }
    static const char* groupName (int g) noexcept
    {
        static const char* const kNames[kMaxGroups] = { "LEVELS SUB A B C", "LEVELS NOISE F1 F2 MAIN", "LEVELS BUS 1 2" };
        static const char* const wideNames[kMaxGroups] = { "LEVELS SUB A B C", "LEVELS NOISE F1 F2 BUS1", "LEVELS BUS2 MAIN" };
        return g >= 0 && g < kMaxGroups ? (ui::QGroup::wideDevice() ? wideNames[g] : kNames[g]) : "";
    }

    void setHostBackground (bool on) noexcept { drawPass_ = on ? DrawPass::LiveOnly : DrawPass::All; }
    bool hostBackground() const noexcept { return hostConfirmed(); }

    /** The value of a row as text, by the row's own rule (the cell value of the native bridge, effGetParamDisplay). */
    static void formatRow (const binding::Row& row, int param, float plain, char* out, size_t cap)
    {
        if (param < 0)
        {
            std::snprintf (out, cap, "-");
            return;
        }
        if (! binding::formatPlain (row.format, plain, out, cap))
            uibind::formatValue (uibind::info (param), plain, out, cap);
    }
    /** The text of the level / pan / send of strip s as the page shows it in the Q display (the parameter's own format; the pan as C / L 35 / R 35). */
    void valueText (IInstanceView& inst, int s, int type, char* out, size_t cap) const
    {
        const int param = paramOf (s, type);
        formatRow (kRows[rowOf (s, type)], param, param >= 0 ? inst.param (param) : 0.0f, out, cap);
    }

    /** Reads the instance: the switches, the meters, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, UiStack& stack, double nowMs, const page::Input& in)
    {
        (void) stack;
        nativeCellsAvailable_ = inst.nativeMixerCellsAvailable();
        inst.requestMeters();   // the source strips of this page: the engine measures them only while a page asks (every update of the page)
        for (int s = 0; s < kStrips; ++s)
        {
            const StripBinding& b = bindings_[s];
            powerOn_[s] = b.powerParam < 0 || binding::toggleOn (inst.param (b.powerParam), b.powerInverted);
            levelEnabled_[s] = b.levelParam >= 0 && (b.powerInverted || powerOn_[s]);
        }

        // the meters of the engine: live level and peak hold for every channel strip (all 10 channels)
        const float dt = lastMs_ > 0.0 ? static_cast<float> (nowMs - lastMs_) * 0.001f : 0.0f;
        lastMs_ = nowMs;
        const float fall = 0.5f * std::clamp (dt, 0.0f, 0.2f);
        for (int s = 0; s < kStrips; ++s)
        {
            const float readout = in.live != nullptr ? in.live->stripPeak[s] : 0.0f;
            const float pk = std::isfinite (readout) ? readout : 0.0f;
            peak_[s] = pk;
            peakHold_[s] = std::max (pk, peakHold_[s] - fall);
            if (nativeBase())
            {
                inst.setNativeMixerMeter (s, pk, peakHold_[s]);
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
        publishStripVisuals (inst);
        inst.setNativeMixerActive (nativeBase());
        publishQGroup (inst);
    }

    // The Mixer paints its own ground with native cut-outs.
    void drawPageGround (gpu::SceneBuilder&) const noexcept {}

    void draw (gpu::SceneBuilder& sb) const
    {
        if (! nativeBase())
            page::ground (sb);
        else if (! hostBackground())
        {
            // A native channel strip includes its header, meters and controls.
            std::array<ui::Rect, kStrips> holes;
            for (int s = 0; s < kStrips; ++s)
                holes[s] = stripRect (s);
            ui::drawNativeCanvas (sb, tabRect(), holes);
        }
        if (nativeBase())
            drawNativeMarkerCovers (sb, "mixer");
    }

    /** Every control of the page is a native cell of the host skin: the overlay claims no touch of the tab. */
    bool covers (int, int) const noexcept { return false; }

    /** A touch on a strip only selects it (its group becomes the active Q bank); the gesture itself belongs to the host's native control. */
    bool touch (const TouchEvent& ev, UiStack&, IInstanceView& inst, double)
    {
        if (ev.kind != TouchEvent::Down || ! tabRect().contains (ev.x, ev.y))
            return false;
        for (int s = 0; s < kStrips; ++s)
            if (stripRect (s).contains (ev.x, ev.y))
            {
                if (selectedStrip_ != s)
                {
                    selectStrip (s);
                    publishQGroup (inst);
                }
                break;
            }
        return false;
    }


    void leftTab (IInstanceView& inst) noexcept
    {
        inst.setNativeMixerActive (false);
        for (int s = 0; s < kStrips; ++s)
        {
            inst.setNativeMixerMeter (s, 0.0f, 0.0f);
            const float stereo[6] {};
            inst.setNativeMixerStereoMeter (s, stereo);
        }
    }

    void tick (double, UiStack&, IInstanceView&) {}
    bool wheel (int, int, int, UiStack&, IInstanceView&, double) { return false; }
    bool fingerDown() const noexcept { return false; }
    void dropGesture() noexcept {}
    void forgetQGroup() noexcept { qSlots_.forget(); }

    /** The hardware knob `knobIndex` (0 .. 3) of the active group turned by `delta` (normalised units): the LEVEL of the strip in that slot; nothing when the strip is switched off or has no level. */
    void turnQ (int knobIndex, double delta, UiStack&, IInstanceView& inst)
    {
        const int s = stripOfSlot (activeGroup_, knobIndex);
        if (s < 0 || ! levelEnabled_[s])
            return;
        const int p = bindings_[s].levelParam;
        const double cur = uibind::normOf (p, inst.param (p));
        const double next = binding::turnNorm (cur, delta);
        if (std::fabs (next - cur) >= 1.0e-12)
            uibind::writeNorm (inst, p, next);
    }

    /** The native bridge: a cell of the skin wrote `value` (normalised for a slider / fader, 0 / 1 for the switch, the choice index for the destination) into the row with page id `id`. */
    void writeCell (int id, double value, IInstanceView& inst) const
    {
        const int s = (id - kStripBase) / 10, type = (id - kStripBase) % 10;
        if (id < kStripBase || s < 0 || s >= kStrips || type < 0 || type >= kTypes)
            return;
        const StripBinding& b = bindings_[s];
        if (type != kPower && ! b.powerInverted && b.powerParam >= 0 && inst.param (b.powerParam) < 0.5f)
            return;   // a switched-off strip takes no edit
        const int p = paramOf (s, type);
        if (p < 0)
            return;
        if (type == kPower)
            uibind::writePlain (inst, p, binding::toggleStore (value >= 0.5, b.powerInverted));
        else if (type == kDest)
            uibind::writePlain (inst, p, value);
        else
            uibind::writeNorm (inst, p, value);
    }
    bool listOpen() const noexcept { return false; }

private:
    int w_ = kCanvasW, h_ = kCanvasH;
    bool nativeCellsEnabled_ = true;
    bool nativeCellsAvailable_ = false;
    DrawPass drawPass_ = DrawPass::All;
    StripBinding bindings_[kStrips];
    bool powerOn_[kStrips] = {}, levelEnabled_[kStrips] = {};
    float peak_[kStrips] = {}, peakHold_[kStrips] = {};
    int selectedStrip_ = 0, activeGroup_ = 0;
    QSlots qSlots_;
    double lastMs_ = 0.0;

    int paramOf (int s, int type) const noexcept
    {
        const StripBinding& b = bindings_[s];
        const int p[] = { b.powerParam, b.destParam, b.panParam, b.send1Param, b.send2Param, b.levelParam };
        return type >= 0 && type < kTypes ? p[type] : -1;
    }

    bool useNativeCells() const noexcept
    {
        return nativeCellsEnabled_ && w_ == page::kW && h_ == page::kH
            && (nativeCellsAvailable_ || hostConfirmed());
    }

    void publishStripVisuals (IInstanceView& inst) noexcept
    {
        uint32_t soloMask = 0, muteMask = 0;
        for (int s = 0; s < kStrips; ++s)
        {
            if (inst.mixerSolo (s)) soloMask |= (1u << s);
            if (inst.mixerMute (s)) muteMask |= (1u << s);
        }
        const auto route = synthyboy::computeMixerRouteState (soloMask, muteMask, [&] (int idx) noexcept {
            return inst.param (idx);
        });
        for (int s = 0; s < kStrips; ++s)
            inst.setNativeMixerStripVisuals (s, route.soloVisual[s], route.muteVisual[s]);
    }

    void initBindings()
    {
        for (int s = 0; s < kStrips; ++s)
        {
            auto idx = [&] (int type) { return kRows[rowOf (s, type)].hasParam() ? uibind::indexOfKey (kRows[rowOf (s, type)].key) : -1; };
            bindings_[s] = { idx (kPower), idx (kLevel), idx (kPan), idx (kDest), idx (kSend1), idx (kSend2), kRows[rowOf (s, kPower)].inverted };
        }
    }

    /** The groups of the page are the LEVELS of the strips: the active group is on Q 1 .. 4 of the Key 61 (the banks follow in order); the XL keeps them on 1 .. 4, 5 .. 8, 9 .. 10.
        PAN, the sends and the destinations are touch only. */
    void publishQGroup (IInstanceView& inst)
    {
        selectStrip (selectedStrip_);   // the device rule may have changed since the last call: the group of the selected strip follows it
        const int a = activeGroup_;
        constexpr int kN = kMaxGroups;
        qSlots_.publish (inst, kN, a, [this] (int g, int* t)
        {
            for (int k = 0; k < binding::QTable<kMaxGroups>::kSlots; ++k)
            {
                const int strip = stripOfSlot (g, k);
                t[k] = strip >= 0 && levelEnabled_[strip] ? bindings_[strip].levelParam : -1;
            }
        });
    }
};

}} // namespace synthyboy_mpc::overlay
