// SynthyBoy Voice page as BINDING DATA (owner decision 2026-11-27: the C++ layer has no UI elements). Every control (the step selector, the row list, the step grid dials, the value, the random / scaling knobs, the oscillator targets)
// is a native cell of the host skin (components/native, scripts/native_voice); this page holds the parameter rows (id, engine key, kind, range, format, Q slot), the four Q banks, the rectangles of the native cells, the sequence
// selection that the native bridge reports (takeNativeVoice) and the live sounding-step indicators (a display, drawn by the overlay). Pure: no OS headers, no threads of its own.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../native_voice_controls.h"
#include "native_marker_covers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../q_router.h"
#include "../ui/binding.h"
#include "../ui/qgroup.h"
#include "../ui/theme.h"
#include "../ui/voice_native_layout.h"
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
    static constexpr int kVirtualStep = 0, kVirtualRow = 1;   // the virtual Q targets of the group STEP

    static constexpr int kNumRows = 6, kNumSteps = 8, kMaxGroups = 4;
    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    /** The page-local ids of the rows (the cell ids of the native bridge). */
    enum Id : int
    {
        kLength = 2, kStepSelect = 3, kRowSelect = 4, kStepValue = 5,
        // Step Grid: 6 rows * 8 steps = 48 values -> 100 .. 147 (id = kGridBase + row * 8 + step)
        kGridBase = 100,
        kRandPan = 200, kRandDetune, kRandCutoff, kRandEnv, kRandDetune10x,
        kScaleEnv = 210, kScaleLfo, kScaleLfoMode,
        kOscA = 220, kOscB, kOscC, kOscSub, kOscNoise
    };

    static constexpr const char* const kRowNames[kNumRows] = { "PAN", "DETUNE", "CUTOFF", "ENV", "MOD 1", "MOD 2" };

    // ---- the parameter rows (row index = position in this table). The first four are the header; the 48 grid rows follow STEP-major (index = kGridFirst + 6 * step + row); then the blocks RANDOM, SCALING, TARGETS.
    //                                          id, key, kind, min, max, step, def, format, choices, numChoices, qGroup, qSlot, inverted
#define SB_VOICE_ROW(ID, KEY, KIND, MIN, MAX, STEP, FMT, QG, QS) binding::Row { ID, KEY, binding::Kind::KIND, MIN, MAX, STEP, 0, binding::Format::FMT, nullptr, 0, QG, QS, false }
#define SB_VOICE_STEP(N) \
    SB_VOICE_ROW (100 + 0 * 8 + (N) - 1, "voicectl.s" #N ".pan",    Continuous, -100, 100, 1, IntSigned, -1, -1), \
    SB_VOICE_ROW (100 + 1 * 8 + (N) - 1, "voicectl.s" #N ".detune", Continuous, -100, 100, 1, IntSigned, -1, -1), \
    SB_VOICE_ROW (100 + 2 * 8 + (N) - 1, "voicectl.s" #N ".cutoff", Continuous, -100, 100, 1, IntSigned, -1, -1), \
    SB_VOICE_ROW (100 + 3 * 8 + (N) - 1, "voicectl.s" #N ".env",    Continuous, -100, 100, 1, IntSigned, -1, -1), \
    SB_VOICE_ROW (100 + 4 * 8 + (N) - 1, "voicectl.s" #N ".mod1",   Continuous, -100, 100, 1, IntSigned, -1, -1), \
    SB_VOICE_ROW (100 + 5 * 8 + (N) - 1, "voicectl.s" #N ".mod2",   Continuous, -100, 100, 1, IntSigned, -1, -1)
    static constexpr int kGridFirst = 4;
    static constexpr int kNumRowsTotal = 4 + kNumRows * kNumSteps + 5 + 3 + 5;
    static constexpr binding::Row kRows[kNumRowsTotal] = {
        SB_VOICE_ROW (kLength, "voicectl.length", Continuous, 0, 0, 1, IntPlain, 0, 2),
        binding::Row { kStepSelect, "", binding::Kind::Choice, 0, kNumSteps - 1, 1, 0, binding::Format::IntPlain, nullptr, kNumSteps, 0, 0, false },      // virtual Q target (kVirtualStep)
        binding::Row { kRowSelect, "", binding::Kind::Choice, 0, kNumRows - 1, 1, 0, binding::Format::ChoiceName, kRowNames, kNumRows, 0, 1, false },     // virtual Q target (kVirtualRow)
        SB_VOICE_ROW (kStepValue, "", Continuous, -100, 100, 1, IntSigned, 0, 3),                                                                        // the value of the selected step: the target follows the selection
        SB_VOICE_STEP (1), SB_VOICE_STEP (2), SB_VOICE_STEP (3), SB_VOICE_STEP (4), SB_VOICE_STEP (5), SB_VOICE_STEP (6), SB_VOICE_STEP (7), SB_VOICE_STEP (8),
        SB_VOICE_ROW (kRandPan, "voicectl.rand.pan", Continuous, 0, 0, 0, Engine, 1, 0),
        SB_VOICE_ROW (kRandDetune, "voicectl.rand.detune", Continuous, 0, 0, 0, Engine, 1, 1),
        SB_VOICE_ROW (kRandCutoff, "voicectl.rand.cutoff", Continuous, 0, 0, 0, Engine, 1, 2),
        SB_VOICE_ROW (kRandEnv, "voicectl.rand.env", Continuous, 0, 0, 0, Engine, 1, 3),
        SB_VOICE_ROW (kRandDetune10x, "voicectl.rand.detune10x", Toggle, 0, 0, 0, Engine, 2, 0),
        SB_VOICE_ROW (kScaleEnv, "voicectl.scale.env", Continuous, 0, 0, 0, Engine, 2, 1),
        SB_VOICE_ROW (kScaleLfo, "voicectl.scale.lfo", Continuous, 0, 0, 0, Engine, 2, 2),
        binding::Row { kScaleLfoMode, "voicectl.scale.lfo_mode", binding::Kind::Choice, 0, 0, 0, 0, binding::Format::ChoiceName, nullptr, 0, 2, 3, false },
        SB_VOICE_ROW (kOscA, "voicectl.osc.a", Toggle, 0, 0, 0, Engine, 3, 0),
        SB_VOICE_ROW (kOscB, "voicectl.osc.b", Toggle, 0, 0, 0, Engine, 3, 1),
        SB_VOICE_ROW (kOscC, "voicectl.osc.c", Toggle, 0, 0, 0, Engine, 3, 2),
        SB_VOICE_ROW (kOscSub, "voicectl.osc.sub", Toggle, 0, 0, 0, Engine, 3, 3),
        SB_VOICE_ROW (kOscNoise, "voicectl.osc.noise", Toggle, 0, 0, 0, Engine, -1, -1)   // touch only (the 17th value)
    };
#undef SB_VOICE_STEP
#undef SB_VOICE_ROW
    static constexpr int gridRowIndex (int row, int step) noexcept { return kGridFirst + step * kNumRows + row; }

    /** The four Q banks as row indices, in reading order: STEP, RANDOM, SCALING (the 10x switch of RANDOM and the SCALING block), TARGETS. NOISE (the 17th value) is touch only. */
    enum RowIndex : int { kRowLength = 0, kRowStepSelect = 1, kRowRowSelect = 2, kRowStepValue = 3, kRowRandPan = kGridFirst + kNumRows * kNumSteps, kRowRandDetune, kRowRandCutoff, kRowRandEnv, kRowRandDetune10x,
                          kRowScaleEnv, kRowScaleLfo, kRowScaleLfoMode, kRowOscA, kRowOscB, kRowOscC, kRowOscSub, kRowOscNoise };
    static_assert (kRowOscNoise == kNumRowsTotal - 1, "the row table and the row indices agree");
    static constexpr binding::QTable<kMaxGroups> kQ { { { kRowStepSelect, kRowRowSelect, kRowLength, kRowStepValue },
                                                     { kRowRandPan, kRowRandDetune, kRowRandCutoff, kRowRandEnv },
                                                     { kRowRandDetune10x, kRowScaleEnv, kRowScaleLfo, kRowScaleLfoMode },
                                                     { kRowOscA, kRowOscB, kRowOscC, kRowOscSub } } };

    // ---- the geometry of the native cells (voice_native_layout.h, tab coordinates; the page is placed at kTabY) --------------------------------------------------------------------------------------------
    static ui::Rect at (const ui::Rect& r) noexcept { return { r.x, kTabY + r.y, r.w, r.h }; }
    static ui::Rect sequenceGrid() noexcept { return at (ui::voicelayout::kSequenceGrid); }
    static ui::Rect blockSequence() noexcept { return at (ui::voicelayout::kBlockSequence); }
    static ui::Rect blockRect (int b) noexcept
    {
        return at (b == 0 ? ui::voicelayout::kBlockRandom : b == 1 ? ui::voicelayout::kBlockScaling : ui::voicelayout::kBlockTargets);
    }
    /** The rectangles of the native cells (everything the skin owns on the tab: the touch passes through). */
    static std::array<ui::Rect, 18> nativeCells() noexcept
    {
        namespace L = ui::voicelayout;
        return { at (L::kSequenceGrid), at (L::kRowList), at (L::kStepSelect), at (L::kStepValue), at (L::kLength),
                 at (L::kRandPan), at (L::kRandDetune), at (L::kRandCutoff), at (L::kRandEnv), at (L::kRandDetune10x),
                 at (L::kScaleEnv), at (L::kScaleLFO), at (L::kScaleLFOMode),
                 at (L::kOscA), at (L::kOscB), at (L::kOscC), at (L::kOscSub), at (L::kOscNoise) };
    }
    static bool isNativeControl (int x, int y) noexcept
    {
        for (const ui::Rect& r : nativeCells())
            if (r.contains (x, y))
                return true;
        return false;
    }
    /** The column of step s of the grid (the sounding-step dot lies in its upper right corner). */
    static ui::Rect stepColumn (int s) noexcept
    {
        if (s < 0 || s >= kNumSteps)
            return {};
        const ui::Rect area { sequenceGrid().x + ui::voicelayout::kStepPad, sequenceGrid().y + ui::voicelayout::kStepPad,
                              std::max (0, sequenceGrid().w - 2 * ui::voicelayout::kStepPad), std::max (0, sequenceGrid().h - 2 * ui::voicelayout::kStepPad) };
        const int colW = std::max (10, (area.w - (kNumSteps - 1) * ui::voicelayout::kStepGap) / kNumSteps);
        return { area.x + s * (colW + ui::voicelayout::kStepGap), area.y, colW, area.h };
    }

    VoiceUi()
    {
        for (int i = 0; i < kNumRowsTotal; ++i)
            params_[i] = kRows[i].hasParam() ? uibind::indexOfKey (kRows[i].key) : -1;
        setCanvas (kCanvasW, kCanvasH);
    }

    void setCanvas (int w, int) { w_ = w; }

    /** Reads the instance: the step a voice plays, the virtual Q targets (step, row), the pages of the stack, the Q group. */
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
            selectStep (std::lround (x * (kNumSteps - 1)));
        if (inst.takeQVirtual (kVirtualRow, x))
            selectRow (std::lround (x * (kNumRows - 1)));
        if (native)
            takeNativeSequence (inst);
        length_ = std::clamp (static_cast<int> (std::lround (params_[0] >= 0 ? inst.param (params_[0]) : 8.0f)), 1, kNumSteps);

        // the steps the newest voices play, lit in the grid
        uint8_t mask = 0;
        if (in.live != nullptr)
            for (int v = 0; v < synthyboy::LiveValues::kReadVoices; ++v)
            {
                const int s = static_cast<int> (std::lround (in.live->voiceStep[v]));
                if (in.live->voiceStep[v] >= 0.0f && s < kNumSteps && s < std::max (1, length_))
                    mask = static_cast<uint8_t> (mask | (1u << s));
            }
        voiceStepMask_ = mask;

        inst.setQVirtual (kVirtualStep, static_cast<float> (selectedStep_) / static_cast<float> (kNumSteps - 1));
        inst.setQVirtual (kVirtualRow, static_cast<float> (activeRow_) / static_cast<float> (kNumRows - 1));
        inst.setNativeVoiceState (sequenceState (inst));
        inst.setUiFullOpen (pageTop_ != 0);
        publishQGroup (inst);
    }

    /** The overlay claims the tab outside the native cells (a touch there selects the block's Q group). */
    bool covers (int x, int y) const noexcept
    {
        if (! ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y))
            return false;
        return pageTop_ != 0 || ! isNativeControl (x, y);
    }

    bool touch (const TouchEvent& ev, UiStack&, IInstanceView& inst, double)
    {
        if (ev.kind == TouchEvent::Down)
        {
            gestureDown_ = false;
            if (! covers (ev.x, ev.y))
                return false;
            gestureDown_ = true;
            gesturePreset_ = inst.stateGeneration();
            if (pageTop_ == 0)
                activateGroupAt (ev.x, ev.y);
            inst.setNativeVoiceState (sequenceState (inst));
            return true;
        }
        if (! gestureDown_)
            return false;
        if (gesturePreset_ != inst.stateGeneration())
        {
            dropGesture();
            return true;
        }
        if (ev.kind != TouchEvent::Move)
            gestureDown_ = false;
        inst.setNativeVoiceState (sequenceState (inst));
        return true;
    }

    void tick (double, UiStack&, IInstanceView&) {}
    bool wheel (int, int, int, UiStack&, IInstanceView&, double) { return false; }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gestureDown_ = false; }
    void forgetQGroup() noexcept
    {
        activeGroup_ = 0;
        qSlots_.forget();
    }
    int topPage() const noexcept { return pageTop_; }
    void leftTab (IInstanceView& inst) noexcept { inst.setNativeVoiceState ({}); }

    /** A touch on the page outside the native cells selects the group of the block it lies in. */
    void activateGroupAt (int x, int y) noexcept
    {
        if (blockSequence().contains (x, y))
            setActiveGroup (0);
        else if (blockRect (0).contains (x, y))
            setActiveGroup (y >= at (ui::voicelayout::kRandDetune10x).y - 4 ? 2 : 1);   // the 10x switch under the knobs is the first member of SCALING's group
        else if (blockRect (1).contains (x, y))
            setActiveGroup (2);
        else if (blockRect (2).contains (x, y))
            setActiveGroup (3);
    }

    /** The hardware knob `slot` (0 .. 3) of the active group turned by `delta` (normalised units): the row in that slot takes it by its kind (the step and row selectors are virtual targets, the value is the selected step's). */
    void turnQ (int slot, double delta, UiStack&, IInstanceView& inst)
    {
        if (pageTop_ != 0)
            return;
        const int r = kQ.row (activeGroup_, slot);
        if (r < 0)
            return;
        const binding::Row& row = kRows[r];
        const int n = detents (delta);
        if (row.id == kStepSelect)
            selectStep (binding::turnChoice (selectedStep_, n, kNumSteps));
        else if (row.id == kRowSelect)
            selectRow (binding::turnChoice (activeRow_, n, kNumRows));
        else
        {
            const int p = row.id == kStepValue ? stepParam (activeRow_, selectedStep_) : params_[r];
            if (p < 0)
                return;
            switch (row.kind)
            {
                case binding::Kind::Toggle: uibind::writePlain (inst, p, delta > 0.0 ? 1.0 : 0.0); break;
                case binding::Kind::Choice:
                {
                    const synthyboy::ParamInfo& pi = uibind::info (p);
                    uibind::writePlain (inst, p, binding::turnChoice (static_cast<int> (std::lround (inst.param (p))), n, pi.numChoices));
                    break;
                }
                default:
                    if (row.step > 0.0f)   // a whole-number value (the length, the value of a step): one detent = one step
                        uibind::writePlain (inst, p, std::clamp (static_cast<double> (inst.param (p)) + n * row.step,
                                                                   static_cast<double> (uibind::info (p).min), static_cast<double> (uibind::info (p).max)));
                    else
                        uibind::writeNorm (inst, p, binding::turnNorm (uibind::normOf (p, inst.param (p)), delta));
                    break;
            }
        }
        inst.setNativeVoiceState (sequenceState (inst));
    }

    void setHostBackground (bool) noexcept {}
    bool hostBackground() const noexcept { return true; }

    /** The cover of the skin's marker squares and instance picture (page_view_base.h drawGround; no drawing of the page: they lie under the transparent plane). */
    void drawPageGround (gpu::SceneBuilder& sb) const
    {
        if (pageTop_ == 0)
            drawNativeMarkerCovers (sb, "voice");
    }

    /** The picture: the sounding-step dots in the grid (a live display). */
    void draw (gpu::SceneBuilder& sb) const
    {
        if (pageTop_ != 0)
            return;
        const ui::theme::Look& T = ui::theme::L();
        ui::Cell cell (sb, sequenceGrid(), 0);
        ui::Pen p (sb, true);
        for (int s = 0; s < kNumSteps; ++s)
            if (voiceStepMask_ & (1 << s))
            {
                const ui::Rect col = stepColumn (s);
                p.disc (static_cast<float> (col.right() - 12), static_cast<float> (col.y + 14), 4.5f, p.c (T.flow[0], 255));
            }
    }

    // ---- access
    void setActiveGroup (int g) noexcept { activeGroup_ = std::clamp (g, 0, kMaxGroups - 1); }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }

    /** The engine parameter of the grid value (row 0 .. 5, step 0 .. 7). */
    int stepParam (int row, int step) const noexcept
    {
        return params_[gridRowIndex (std::clamp (row, 0, kNumRows - 1), std::clamp (step, 0, kNumSteps - 1))];
    }

private:
    /** Whole detents of a Q-Link turn (delta in normalised units, one detent = binding::kQStep); a turn that is not zero is at least one. */
    static int detents (double delta) noexcept
    {
        const long n = std::lround (delta / binding::kQStep);
        return n != 0 ? static_cast<int> (n) : (delta > 0.0 ? 1 : delta < 0.0 ? -1 : 0);
    }

    void selectRow (long row) { activeRow_ = static_cast<int> (std::clamp (row, 0L, static_cast<long> (kNumRows - 1))); }
    void selectStep (long step) { selectedStep_ = static_cast<int> (std::clamp (step, 0L, static_cast<long> (kNumSteps - 1))); }

    nativeui::voice::ViewState sequenceState (const IInstanceView& inst) const
    {
        if (! sequenceAvailable_ || pageTop_ != 0)
            return {};
        return { true, activeRow_, selectedStep_, stepParam (activeRow_, selectedStep_), inst.stateGeneration() };
    }

    void takeNativeSequence (IInstanceView& inst)
    {
        namespace n = nativeui::voice;
        static_assert (n::kRows == kNumRows && n::kSteps == kNumSteps);
        const auto before = sequenceState (inst);
        float value;
        if (inst.takeNativeVoice (before, n::kRow, value))
        {
            selectRow (std::lround (value * (n::kRows - 1)));
            setActiveGroup (0);
        }
        if (inst.takeNativeVoice (before, n::kStep, value))
        {
            selectStep (std::lround (value * (n::kSteps - 1)));
            setActiveGroup (0);
        }
        // Selection wins over a write queued for the previously displayed cell.
        const auto current = sequenceState (inst);
        if (inst.takeNativeVoice (current, n::kValue, value) && current.target >= 0 && current.preset == inst.stateGeneration())
        {
            setActiveGroup (0);
            const float plain = static_cast<float> (std::lround (synthyboy::fromNormalised (uibind::info (current.target), value)));
            if (inst.param (current.target) != plain)
                uibind::writePlain (inst, current.target, plain);
        }
    }

    /** The Q target of slot `slot` of group `group`: the engine parameter of the row, the virtual targets of STEP and ROW, the selected step's value (-1: empty). */
    int groupTarget (int group, int slot) const noexcept
    {
        const int r = kQ.row (group, slot);
        if (r < 0)
            return -1;
        switch (kRows[r].id)
        {
            case kStepSelect: return QRouter::encode (kVirtualStep, QRouter::kVirtual);
            case kRowSelect: return QRouter::encode (kVirtualRow, QRouter::kVirtual);
            case kStepValue: return stepParam (activeRow_, selectedStep_);
            default: return params_[r];
        }
    }

    /** The groups of the page are four, in reading order (STEP, RANDOM, SCALING with the 10x switch, TARGETS): the active one is on Q 1 .. 4, the next three (wrapping) on 5 .. 16. */
    void publishQGroup (IInstanceView& inst)
    {
        const bool list = pageTop_ != 0;
        qSlots_.publish (inst, list ? 0 : kMaxGroups, activeGroup_, [this] (int k, int* t)
        {
            for (int s = 0; s < binding::QTable<kMaxGroups>::kSlots; ++s)
                t[s] = groupTarget (k, s);
        });
    }

    int params_[kNumRowsTotal] {};
    bool sequenceAvailable_ = false;
    int activeGroup_ = 0, activeRow_ = 0, selectedStep_ = 0, length_ = 8;
    uint8_t voiceStepMask_ = 0;

    uint64_t gesturePreset_ = 0;
    int w_ = kCanvasW;
    int pageTop_ = 0;
    bool gestureDown_ = false;

    QSlots qSlots_;
};

}} // namespace synthyboy_mpc::overlay
