// SynthyBoy Master page as BINDING DATA (owner decision 2026-11-27: the C++ layer has no UI elements). Every control of both sides (MAIN: OUTPUT, PITCH, VOICES, GLIDE; TUNING & MPE: SCALE, TUNING, MPE) is a native cell of the
// host skin (components/native, scripts/native_master); this page holds the parameter rows (id, engine key, kind, range, format, Q slot), the nine Q groups as a table of row indices, the rectangles of the native cells (master_native_layout.h),
// the rules that hide what does not apply (BEND DOWN while LINK is on, MPE BEND / BEND TO X while MPE is off, PAD % while PAD PRESS is off), the native bridge (view, output meters, the scale root), and the live displays the
// overlay draws itself: the glide curve. The meter is the engine's own master peak, the voice bar the number of sounding voices. Pure: no OS headers, no threads of its own.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_marker_covers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../native_master_controls.h"
#include "../q_router.h"
#include "../ui/binding.h"
#include "../ui/master_native_layout.h"
#include "../ui/qgroup.h"
#include "../ui/theme.h"
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

    /** The page-local ids of the rows (the cell ids of the native bridge). */
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
        // the plugin-wide pad pressure layer, the output limiter (shell parameters of the plugin, not of the engine: no Q group) and the QUALITY pair (engine parameters, touch only)
        kPadPressure = 160, kPadAmount, kLimiter, kNoSmoothing, kOversample4x,
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kMaxGroups = 9, kFirstTuningGroup = 5, kBlocks = ui::masterlayout::kBlocks;
    enum Side : int { SideMain = 0, SideTuning = 1 };

    // ---- the parameter rows: X (name, key, kind, min, max, step, default, format, qGroup, qSlot); min == max: the engine's range. The row index of X (Name) is kRow##Name.
#define SB_MASTER_ROWS(X) \
    X (Volume, "master.volume", Continuous, 0, 0, 0, 0, Engine, 0, 0) \
    X (Spread, "master.stereo_spread", Continuous, 0, 0, 0, 0, Engine, 0, 1) \
    X (StereoRouting, "master.stereo_routing", Continuous, 0, 0, 0, 0, Engine, 0, 2) \
    X (VoiceOverride, "master.voice_override", Choice, 0, 0, 0, 0, ChoiceName, 0, 3) \
    X (Tune, "master.tune", Continuous, 0, 0, 0, 0, Engine, 1, 0) \
    X (Transpose, "master.transpose", Continuous, 0, 0, 1, 0, IntSigned, 1, 1) \
    X (BendUp, "master.bend_range", Continuous, 0, 0, 1, 0, IntPlain, 1, 2) \
    X (BendDown, "master.bend_down_range", Continuous, 0, 0, 1, 0, IntPlain, 1, 3) \
    X (BendLink, "master.bend_down_link", Toggle, 0, 0, 0, 0, Engine, -1, -1) \
    X (Polyphony, "master.polyphony", Continuous, 0, 0, 1, 0, IntPlain, 2, 0) \
    X (Legato, "master.legato", Toggle, 0, 0, 0, 0, Engine, 2, 1) \
    X (PolyLegato, "master.poly_legato", Toggle, 0, 0, 0, 0, Engine, 2, 2) \
    X (PhaseLegato, "voice.phase_legato", Toggle, 0, 0, 0, 0, Engine, 2, 3) \
    X (SameNote, "master.voice.same_note", Choice, 0, 0, 0, 0, ChoiceName, -1, -1) \
    X (StealPriority, "master.voice.steal_priority", Choice, 0, 0, 0, 0, ChoiceName, -1, -1) \
    X (VelocityTrack, "master.velocity_track", Continuous, 0, 0, 1, 0, IntSigned, -1, -1) \
    X (PortaTime, "master.portamento.time", Continuous, 0, 0, 0, 0, Engine, 4, 0) \
    X (PortaSlope, "master.portamento.slope", Continuous, 0, 0, 0, 0, Engine, 4, 1) \
    X (PortaAlways, "master.portamento.always", Toggle, 0, 0, 0, 0, Engine, 4, 2) \
    X (PortaScaled, "master.portamento.scaled", Toggle, 0, 0, 0, 0, Engine, 4, 3) \
    X (ScaleType, "master.scale.type", Choice, 0, 0, 0, 0, ChoiceName, 5, 0) \
    X (ScaleKey, "master.scale.key", Choice, 0, 0, 0, 0, ChoiceName, 5, 1) \
    X (ScaleMode, "master.scale.mode", Choice, 0, 0, 0, 0, ChoiceName, 5, 2) \
    X (TuningA4, "master.tuning.a4", Continuous, 0, 0, 0.01f, 0, Decimal2, 6, 0) \
    X (TuningScale, "master.tuning.scale", Choice, 0, 0, 0, 0, ChoiceName, 6, 1) \
    X (MtsEsp, "master.tuning.mts_esp", Toggle, 0, 0, 0, 0, Engine, 6, 2) \
    X (MtsNoteOn, "master.tuning.mts_note_on_only", Toggle, 0, 0, 0, 0, Engine, 6, 3) \
    X (MpeOn, "master.mpe.on", Toggle, 0, 0, 0, 0, Engine, 7, 0) \
    X (MpeBend, "master.mpe.bend_range", Continuous, 0, 0, 1, 0, IntPlain, 7, 1) \
    X (MpeMacro, "master.mpe.xyz_macro", Choice, 0, 0, 0, 0, ChoiceName, 7, 2) \
    X (MpeYModwheel, "master.mpe.y_modwheel", Toggle, 0, 0, 0, 0, Engine, 8, 0) \
    X (MpeBendToX, "master.mpe.bend_to_x", Toggle, 0, 0, 0, 0, Engine, 8, 1) \
    X (MpeYBipolar, "master.mpe.y_bipolar", Toggle, 0, 0, 0, 0, Engine, 8, 2) \
    X (PadPressure, "", Toggle, 0, 0, 0, 0, Engine, -1, -1) \
    X (PadAmount, "", Continuous, 0, 100, 1, 35, IntPlain, -1, -1) \
    X (Limiter, "", Toggle, 0, 0, 0, 0, Engine, -1, -1) \
    X (NoSmoothing, "quality.no_smoothing", Toggle, 0, 0, 0, 0, Engine, -1, -1) \
    X (Oversample4x, "quality.oversample_4x", Toggle, 0, 0, 0, 0, Engine, -1, -1) \

    enum RowIndex : int
    {
#define SB_MASTER_ENUM(N, ...) kRow##N,
        SB_MASTER_ROWS (SB_MASTER_ENUM)
#undef SB_MASTER_ENUM
        kNumRows
    };
#define SB_MASTER_ROW(N, KEY, KIND, MIN, MAX, STEP, DEF, FMT, QG, QS) binding::Row { k##N, KEY, binding::Kind::KIND, MIN, MAX, STEP, DEF, binding::Format::FMT, nullptr, 0, QG, QS, false },
    static constexpr binding::Row kRows[kNumRows] = { SB_MASTER_ROWS (SB_MASTER_ROW) };
#undef SB_MASTER_ROW
#undef SB_MASTER_ROWS

    /** The nine Q groups as row indices: 0 OUTPUT, 1 PITCH, 2 VOICES, 3 VOICE OPTS (no members: touch only), 4 GLIDE; 5 SCALE, 6 TUNING, 7 MPE, 8 MPE Y. The members that do not apply are left out by the page (the others close up). */
    static constexpr binding::QTable<kMaxGroups> kQ { { { kRowVolume, kRowSpread, kRowStereoRouting, kRowVoiceOverride },
                                                     { kRowTune, kRowTranspose, kRowBendUp, kRowBendDown },
                                                     { kRowPolyphony, kRowLegato, kRowPolyLegato, kRowPhaseLegato },
                                                     { -1, -1, -1, -1 },
                                                     { kRowPortaTime, kRowPortaSlope, kRowPortaAlways, kRowPortaScaled },
                                                     { kRowScaleType, kRowScaleKey, kRowScaleMode, -1 },
                                                     { kRowTuningA4, kRowTuningScale, kRowMtsEsp, kRowMtsNoteOn },
                                                     { kRowMpeOn, kRowMpeBend, kRowMpeMacro, -1 },
                                                     { kRowMpeYModwheel, kRowMpeBendToX, kRowMpeYBipolar, -1 } } };
    static const char* groupName (int g) noexcept
    {
        static const char* const kNames[kMaxGroups] = { "OUTPUT", "PITCH", "VOICES", "VOICE OPTS", "GLIDE", "SCALE", "TUNING", "MPE", "MPE Y" };
        return g >= 0 && g < kMaxGroups ? kNames[g] : "";
    }
    static const char* blockName (int b) noexcept { return b >= 0 && b < kBlocks ? ui::masterlayout::kBlockTitle[b] : ""; }
    static int sideOfGroup (int g) noexcept { return g < kFirstTuningGroup ? SideMain : SideTuning; }

    /** The row index of page id `id` (-1: none). */
    static int rowIndexOfId (int id) noexcept { return binding::indexOfId (kRows, kNumRows, id); }

    MasterUi()
    {
        for (int i = 0; i < kNumRows; ++i)
            params_[i] = kRows[i].hasParam() ? uibind::indexOfKey (kRows[i].key) : -1;
        setCanvas (kCanvasW, kCanvasH);
    }

    void setCanvas (int w, int h) { w_ = w; h_ = h; }

    /** Reads the instance: the side, the conditions, the meters, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        // the side is the page's: the hardware sub-tab Master (view index 1) is the MAIN side, the sub-tab Tuning (index 2) the TUNING & MPE side; there are no side buttons
        setSide (in.instance >= 2 ? SideTuning : SideMain);
        if (nativeView() == nativeui::master::Tuning && inst.masterScaleInputActive())
        {
            const int root = inst.takeMasterScaleRoot();
            if (root >= 0 && params_[kRowScaleKey] >= 0)
                uibind::writePlain (inst, params_[kRowScaleKey], static_cast<float> (root));
        }
        // what does not apply is hidden (not dimmed): BEND DOWN while LINK is on, MPE BEND and BEND TO X while MPE is off
        bendLinked_ = readOn (inst, kRowBendLink);
        mpeOn_ = readOn (inst, kRowMpeOn);
        clip_ = inst.pollMasterClip (nowMs);
        activeVoices_ = in.live != nullptr ? in.live->activeVoices : 0;
        polyphony_ = std::max (1, params_[kRowPolyphony] >= 0 ? static_cast<int> (std::lround (inst.param (params_[kRowPolyphony]))) : 16);
        if (lastInstance_ != &inst)
        {
            holdL_ = holdR_ = 0.0f;
            lastMs_ = 0.0;
            lastInstance_ = &inst;
        }
        peakL_ = in.live != nullptr ? in.live->masterPeakL : 0.0f;
        peakR_ = in.live != nullptr ? in.live->masterPeakR : 0.0f;
        updateHold (lastMs_ > 0.0 ? static_cast<float> (nowMs - lastMs_) * 0.001f : 0.0f);
        lastMs_ = nowMs;
        {   // the pad pressure layer of the plugin (docs/MPC.md "Pad pressure"): off by default, the presets carry their own route; PAD % applies only while PAD PRESS is on
            bool on = false;
            float percent = 35.0f;
            const bool have = inst.padPressure (on, percent);
            padOn_ = have && on;
        }
        slope_ = params_[kRowPortaSlope] >= 0 ? inst.param (params_[kRowPortaSlope]) : 0.0f;
        inst.setUiFullOpen (pageTop_ != 0);
        if (nativeView() == nativeui::master::Main)
            inst.setNativeMasterOutput ({ peakL_, peakR_, holdL_, holdR_, activeVoices_, polyphony_, clip_ });
        inst.setNativeMasterView (nativeView());
        publishQGroup (inst);
    }

    nativeui::master::View nativeView() const noexcept
    {
        using namespace nativeui::master;
        if (pageTop_ != 0)
            return Hidden;
        return side_ == SideMain ? Main : Tuning;
    }

    void leftTab (IInstanceView& inst) noexcept
    {
        inst.leaveNativeMasterView (side_ == SideMain ? nativeui::master::Main : nativeui::master::Tuning);
        lastInstance_ = nullptr;
    }

    // ---- the geometry of the native cells (master_native_layout.h, tab coordinates; the page is placed at kTabY) --------------------------------------------------------------------------------------------
    static ui::Rect at (const ui::Rect& r) noexcept { return { r.x, kTabY + r.y, r.w, r.h }; }
    /** The cell of row `id` on the page (the rectangle the skin places the native control at; BEND UP takes the wide cell while LINK is on, BEND DOWN and PAD % have none while they do not apply). */
    ui::Rect cellRect (int id) const noexcept
    {
        namespace L = ui::masterlayout;
        switch (id)
        {
            case kVolume: return at (L::kVolume);
            case kSpread: return at (L::kStereoSpread);
            case kStereoRouting: return at (L::kStereoRouting);
            case kVoiceOverride: return at (L::kVoiceOverride);
            case kLimiter: return at (L::kOutputLimiter);
            case kNoSmoothing: return at (L::kQualityNoSmoothing);
            case kOversample4x: return at (L::kQualityOversample4x);
            case kTune: return at (L::kTune);
            case kTranspose: return at (L::kTranspose);
            case kBendUp: return at (bendLinked_ ? L::kLinkedBend : L::kBendRange);
            case kBendDown: return bendLinked_ ? ui::Rect {} : at (L::kBendDownRange);
            case kBendLink: return at (L::kBendDownLink);
            case kPolyphony: return at (L::kPolyphony);
            case kLegato: return at (L::kLegato);
            case kPolyLegato: return at (L::kPolyLegato);
            case kPhaseLegato: return at (L::kVoicePhaseLegato);
            case kVelocityTrack: return at (L::kVelocityTrack);
            case kSameNote: return at (L::kVoiceSameNote);
            case kStealPriority: return at (L::kVoiceStealPriority);
            case kPadPressure: return at (L::kShellPadPressure);
            case kPadAmount: return padOn_ ? at (L::kShellPadPressureAmount) : ui::Rect {};
            case kPortaTime: return at (L::kPortamentoTime);
            case kPortaSlope: return at (L::kPortamentoSlope);
            case kPortaAlways: return at (L::kPortamentoAlways);
            case kPortaScaled: return at (L::kPortamentoScaled);
            case kScaleType: return at (L::kScaleType);
            case kScaleKey: return at (L::kScaleKey);
            case kScaleMode: return at (L::kScaleMode);
            case kTuningA4: return at (L::kTuningA4);
            case kTuningScale: return at (L::kTuningScale);
            case kMtsEsp: return at (L::kTuningMtsEsp);
            case kMtsNoteOn: return at (L::kTuningMtsNoteOnOnly);
            case kMpeOn: return at (L::kMpeOn);
            case kMpeBend: return at (L::kMpeBendRange);
            case kMpeMacro: return at (L::kMpeXyzMacro);
            case kMpeYModwheel: return at (L::kMpeYModwheel);
            case kMpeBendToX: return at (L::kMpeBendToX);
            case kMpeYBipolar: return at (L::kMpeYBipolar);
            default: return {};
        }
    }
    /** The rectangle of block b (0 .. 6: the titled panel the skin draws) on its side. */
    static ui::Rect blockRect (int b) noexcept { return b >= 0 && b < kBlocks ? at (ui::masterlayout::kBlock[b]) : ui::Rect {}; }
    bool blockVisible (int b) const noexcept { return b >= 0 && b < kBlocks && ui::masterlayout::kBlockOnTuning[b] == (side_ == SideTuning); }
    /** True if (x, y) lies in a native cell, readout or meter of the side that is up: the touch belongs to the host. */
    bool isNativeControl (int x, int y) const noexcept
    {
        namespace L = ui::masterlayout;
        if (side_ == SideMain)
        {
            static constexpr int kIds[] = { kVolume, kSpread, kStereoRouting, kVoiceOverride, kNoSmoothing, kOversample4x, kTune, kTranspose, kBendUp, kPolyphony, kLegato, kPolyLegato, kPhaseLegato, kSameNote,
                                            kStealPriority, kVelocityTrack, kBendLink, kPortaTime, kPortaSlope, kPortaAlways, kPortaScaled, kBendDown, kPadPressure, kPadAmount, kLimiter };
            for (const int id : kIds)
                if (cellRect (id).contains (x, y))
                    return true;
            return at (L::kOutputClip).contains (x, y) || at (L::kOutputMeters).contains (x, y) || at (L::kOutputVoiceMeter).contains (x, y);
        }
        static constexpr int kIds[] = { kScaleType, kScaleKey, kScaleMode, kTuningA4, kTuningScale, kMtsEsp, kMtsNoteOn, kMpeOn, kMpeMacro, kMpeYModwheel, kMpeYBipolar };
        for (const int id : kIds)
            if (cellRect (id).contains (x, y))
                return true;
        return at (L::kScaleInput).contains (x, y) || (mpeOn_ && (cellRect (kMpeBend).contains (x, y) || cellRect (kMpeBendToX).contains (x, y)));
    }

    bool covers (int x, int y) const noexcept
    {
        if (! ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y))
            return false;
        return pageTop_ != 0 || ! isNativeControl (x, y);
    }

    bool touch (const TouchEvent& ev, UiStack&, IInstanceView& inst, double)
    {
        const int x = ev.x, y = ev.y;
        if (ev.kind == TouchEvent::Down)
        {
            gestureDown_ = false;
            if (nativeView() != nativeui::master::Hidden && isNativeControl (x, y))
            {
                if (! ui::QGroup::wideDevice())
                {
                    activateGroupAt (x, y);
                    publishQGroup (inst);
                }
                return false;
            }
            if (! covers (x, y))
                return false;
            gestureDown_ = true;
            if (pageTop_ == 0)
                activateGroupAt (x, y);
            return true;
        }
        if (! gestureDown_)
            return false;
        if (ev.kind != TouchEvent::Move)
            gestureDown_ = false;
        return true;
    }

    void tick (double, UiStack&, IInstanceView&) {}
    bool wheel (int, int, int, UiStack&, IInstanceView&, double) { return false; }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gestureDown_ = false; }
    void forgetQGroup() noexcept
    {
        activeGroup_ = side_ == SideMain ? 0 : kFirstTuningGroup;
        qSlots_.forget();
    }
    int topPage() const noexcept { return pageTop_; }

    /** A touch selects the group of the control it lands on, or the group of the block it lies in (a control without a Q slot still selects its block's group). */
    void activateGroupAt (int x, int y) noexcept
    {
        for (int g = 0; g < kMaxGroups; ++g)
        {
            if (sideOfGroup (g) != side_)
                continue;
            for (int k = 0; k < binding::QTable<kMaxGroups>::kSlots; ++k)
            {
                const int r = liveRow (g, k);
                if (r >= 0 && cellRect (kRows[r].id).contains (x, y))
                {
                    setActiveGroup (g);
                    return;
                }
            }
        }
        constexpr int blockGroups[kBlocks] { 0, 1, 2, 4, 5, 6, 7 };
        for (int b = 0; b < kBlocks; ++b)
            if (blockVisible (b) && blockRect (b).contains (x, y))
            {
                setActiveGroup (b == 6 && y >= cellRect (kMpeYModwheel).y ? 8 : blockGroups[b]);
                return;
            }
    }

    /** The hardware knob `slot` (0 .. 3) of the active group turned by `delta` (normalised units): the live member of the slot takes it by its kind. */
    void turnQ (int slot, double delta, UiStack&, IInstanceView& inst)
    {
        const int r = liveRow (activeGroup_, slot);
        if (r < 0 || params_[r] < 0)
            return;
        const binding::Row& row = kRows[r];
        const int p = params_[r];
        const synthyboy::ParamInfo& pi = uibind::info (p);
        const long n = std::lround (delta / binding::kQStep);
        const int detents = n != 0 ? static_cast<int> (n) : (delta > 0.0 ? 1 : delta < 0.0 ? -1 : 0);
        switch (row.kind)
        {
            case binding::Kind::Toggle:
                uibind::writePlain (inst, p, binding::toggleStore (delta > 0.0, row.inverted));
                break;
            case binding::Kind::Choice:
                uibind::writePlain (inst, p, binding::turnChoice (static_cast<int> (std::lround (inst.param (p))), detents, pi.numChoices));
                break;
            case binding::Kind::Continuous:
                if (row.step > 0.0f)   // a number: one detent = one step
                    uibind::writePlain (inst, p, std::clamp (static_cast<double> (inst.param (p)) + detents * static_cast<double> (row.step), static_cast<double> (pi.min), static_cast<double> (pi.max)));
                else
                {
                    const double cur = uibind::normOf (p, inst.param (p)), next = binding::turnNorm (cur, delta);
                    if (std::fabs (next - cur) >= 1.0e-12)
                        uibind::writeNorm (inst, p, next);
                }
                break;
            case binding::Kind::Command:
                break;
        }
    }

    bool hostBackground() const noexcept { return true; }

    /** The picture: the marker covers of the host skin and the glide curve (a live display, MAIN side). */
    void draw (gpu::SceneBuilder& sb) const
    {
        drawNativeMarkerCovers (sb, "master");
        if (pageTop_ == 0 && side_ == SideMain)
            drawGlideCurve (sb, at (ui::masterlayout::kGlideCurve), slope_);
    }

    /** The glide slope curve (convex / linear / concave according to master.portamento.slope -8 .. +8): a display, no touch. */
    static void drawGlideCurve (gpu::SceneBuilder& sb, const ui::Rect& rect, float slope)
    {
        const ui::theme::Look& T = ui::theme::L();
        ui::Cell cell (sb, rect);
        ui::Pen p (sb, true);
        p.rrect (rect, T.rControl, p.c (T.panel1, 200));
        p.rrect (rect, T.rControl, p.c (T.keyline, 160), 1.0f);
        const int m = 6;
        const float x0 = static_cast<float> (rect.x + m), y0 = static_cast<float> (rect.bottom() - m);
        const float w = static_cast<float> (rect.w - 2 * m), h = static_cast<float> (rect.h - 2 * m);
        p.line (x0, y0 - h * 0.5f, x0 + w, y0 - h * 0.5f, 0.8f, p.c (T.track, 100));
        p.line (x0 + w * 0.5f, y0, x0 + w * 0.5f, y0 - h, 0.8f, p.c (T.track, 100));
        constexpr int kPoints = 32;
        float prevX = x0, prevY = y0;
        const float k = std::clamp (slope, -8.0f, 8.0f);
        const bool isLinear = std::fabs (k) < 0.05f;
        const float denom = isLinear ? 1.0f : (std::exp (k) - 1.0f);
        for (int i = 1; i <= kPoints; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (kPoints);
            const float yNorm = isLinear ? t : (std::exp (k * t) - 1.0f) / denom;
            const float px = x0 + t * w, py = y0 - yNorm * h;
            p.line (prevX, prevY, px, py, 1.8f, p.c (T.flow[0], 255));
            prevX = px;
            prevY = py;
        }
        p.textCentered ({ rect.x + 4, rect.y + 2, rect.w - 8, 18 }, "GLIDE CURVE", T.fSmall + 1.0f, p.c (T.textDim, 255), T.fMin);
    }

    // ---- access
    int side() const noexcept { return side_; }
    void setSide (int s)
    {
        const int next = s == SideTuning ? SideTuning : SideMain;
        if (next == side_)
            return;
        side_ = next;
        activeGroup_ = side_ == SideMain ? 0 : kFirstTuningGroup;
        qSlots_.forget();
    }
    int activeQ() const noexcept { return activeGroup_; }
    int activeGroup() const noexcept { return activeGroup_; }
    void setActiveGroup (int g) noexcept { activeGroup_ = std::clamp (g, 0, kMaxGroups - 1); }
    bool bendLinked() const noexcept { return bendLinked_; }
    bool mpeIsOn() const noexcept { return mpeOn_; }
    bool padPressureIsOn() const noexcept { return padOn_; }
    int paramOfRow (int rowIndex) const noexcept { return rowIndex >= 0 && rowIndex < kNumRows ? params_[rowIndex] : -1; }
    /** The row index in slot `slot` of group `g` after the members that do not apply are left out and the others closed up (-1: empty). */
    int liveRow (int g, int slot) const noexcept
    {
        int k = 0;
        for (int s = 0; s < binding::QTable<kMaxGroups>::kSlots; ++s)
        {
            const int r = kQ.row (g, s);
            if (r < 0 || hidden (kRows[r].id))
                continue;
            if (k++ == slot)
                return r;
        }
        return -1;
    }
    int liveMembers (int g) const noexcept
    {
        int n = 0;
        for (int s = 0; s < binding::QTable<kMaxGroups>::kSlots; ++s)
            n += liveRow (g, s) >= 0;
        return n;
    }
    /** True if the control of row `id` does not apply now and is hidden. */
    bool hidden (int id) const noexcept
    {
        return (id == kBendDown && bendLinked_) || ((id == kMpeBend || id == kMpeBendToX) && ! mpeOn_) || (id == kPadAmount && ! padOn_);
    }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    float peakL() const noexcept { return peakL_; }
    float peakR() const noexcept { return peakR_; }
    float holdL() const noexcept { return holdL_; }
    float holdR() const noexcept { return holdR_; }
    int activeVoices() const noexcept { return activeVoices_; }
    int polyphony() const noexcept { return polyphony_; }
    bool clipActive() const noexcept { return clip_; }

private:
    bool readOn (IInstanceView& inst, int rowIndex) const noexcept
    {
        return params_[rowIndex] >= 0 && inst.param (params_[rowIndex]) >= 0.5f;
    }

    void updateHold (float dtSeconds) noexcept
    {
        const float fall = 0.5f * std::clamp (dtSeconds, 0.0f, 0.2f);
        holdL_ = std::max (peakL_, holdL_ - fall);
        holdR_ = std::max (peakR_, holdR_ - fall);
    }

    /** The groups of the page are the groups of the side that is up (Main: OUTPUT .. GLIDE, Tuning: SCALE .. MPE Y) in reading order; the active group is on Q 1 .. 4, the next three (wrapping inside the side) on 5 .. 16.
        The targets of a group are the parameters of its LIVE members (a hidden control and the touch-only LINK are no member, so no slot is wasted on them); a group with no member (VOICE OPTS) is not mapped. */
    void publishQGroup (IInstanceView& inst)
    {
        const bool main = side_ == SideMain;
        const int first = main ? 0 : kFirstTuningGroup, count = main ? kFirstTuningGroup : kMaxGroups - kFirstTuningGroup;
        int ids[kMaxGroups], used = 0, pos = -1;
        for (int g = first; g < first + count; ++g)
            if (liveMembers (g) > 0)
            {
                if (g == activeGroup_)
                    pos = used;
                ids[used++] = g;
            }
        const int active = pos < 0 ? 0 : pos;
        if (pageTop_ != 0)
            qSlots_.publish (inst, 0, 0, [] (int, int*) {});   // a page above has no group: no slot has a target
        else
            qSlots_.publish (inst, used, active, [&] (int k, int* t)
            {
                for (int i = 0; i < 4; ++i)
                {
                    const int r = liveRow (ids[k], i);
                    t[i] = r >= 0 ? params_[r] : -1;
                }
            });
    }

    int params_[kNumRows] {};
    int side_ = SideMain, activeGroup_ = 0;
    bool bendLinked_ = false, mpeOn_ = true, padOn_ = true;   // what the page has read from the instance (the first picture of a bare page shows every cell)
    int w_ = kCanvasW, h_ = kCanvasH;
    int pageTop_ = 0;
    const IInstanceView* lastInstance_ = nullptr;
    double lastMs_ = 0.0;
    float peakL_ = 0.0f, peakR_ = 0.0f, holdL_ = 0.0f, holdR_ = 0.0f, slope_ = 0.0f;
    int activeVoices_ = 0, polyphony_ = 16;
    bool clip_ = false;
    bool gestureDown_ = false;

    QSlots qSlots_;
};

}} // namespace synthyboy_mpc::overlay
