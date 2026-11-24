// SynthyBoy - MPC UI component library: VoiceGrid, the 8-step sequence and per-voice control grid (docs/MPC.md, ui-elem-mastervoice.md).
// Rows: 6 (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2); Columns: 8 steps.
// Eight existing bipolar knobs share one selected row. Native production drawing/input belongs to
// the host; this adapter retains step formatting, selection and live sounding-voice indicators.
// Relative drags never edit a different step. Parameter truth remains in the owner.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "base.h"
#include "voice_native_layout.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

class VoiceGrid : public Component
{
public:
    static constexpr int kNumRows = 6;
    static constexpr int kNumSteps = 8;
    static constexpr int kPad = voicelayout::kStepPad;
    static constexpr int kColGap = voicelayout::kStepGap;

    struct State
    {
        int length = 8;               // voicectl.length (1 .. 8)
        int activeRow = 0;            // 0..5 (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2)
        int selectedStep = 0;         // 0..7
        float values[kNumRows][kNumSteps] = {};     // -1.0 .. +1.0
        uint8_t voiceStepMask = 0;    // bit s is 1 if step s has an active sounding voice
    };
    State state;
    int baseId = 100;             // baseId + row * 8 + col

    static const char* rowName (int r) noexcept
    {
        static const char* const kNames[kNumRows] = { "PAN", "DETUNE", "CUTOFF", "ENV", "MOD 1", "MOD 2" };
        return r >= 0 && r < kNumRows ? kNames[r] : "";
    }

    VoiceGrid() = default;

    Size minSize() const override { return { 800, 236 }; }

    int stepId (int row, int step) const noexcept { return baseId + row * kNumSteps + step; }

    Rect gridArea() const noexcept
    {
        return { rect_.x + kPad, rect_.y + kPad, std::max (0, rect_.w - 2 * kPad), std::max (0, rect_.h - 2 * kPad) };
    }

    Rect columnRect (int s) const noexcept
    {
        if (s < 0 || s >= kNumSteps) return {};
        const Rect g = gridArea();
        const int totalGap = (kNumSteps - 1) * kColGap;
        const int colW = std::max (10, (g.w - totalGap) / kNumSteps);
        return { g.x + s * (colW + kColGap), g.y, colW, g.h };
    }

    /** For the owner: sets one value of `state` (-1 .. +1). */
    void setValue (int row, int step, float v) noexcept
    {
        if (row >= 0 && row < kNumRows && step >= 0 && step < kNumSteps)
            state.values[row][step] = std::clamp (v, -1.0f, 1.0f);
    }

    float value (int row, int step) const noexcept
    {
        return (row >= 0 && row < kNumRows && step >= 0 && step < kNumSteps) ? state.values[row][step] : 0.0f;
    }

    Rect knobRect (int step) const noexcept
    {
        const Rect col = columnRect (step);
        return { col.x + (col.w - voicelayout::kStepDiameter) / 2,
                 col.y + voicelayout::kStepTop, voicelayout::kStepDiameter, voicelayout::kStepDiameter };
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        dragging_ = false;
        if (! enabled_) return Change::none();
        for (int s = 0; s < kNumSteps; ++s)
            if (knobRect (s).contains (x, y))
            {
                dragging_ = true;
                gestureKnob_.place (knobRect (s));
                gestureKnob_.setId (stepId (state.activeRow, s));
                gestureKnob_.state.bipolar = true;
                gestureKnob_.state.value = (value (state.activeRow, s) + 1.0) * 0.5;
                gestureKnob_.touchDown (x, y, nowMs);
                return Change::normalised (stepId (state.activeRow, s), value (state.activeRow, s));
            }
        return Change::none();
    }

    Change touchMove (int x, int y) override
    {
        if (! enabled_ || ! dragging_) return Change::none();
        Change change = gestureKnob_.touchMove (x, y);
        if (change.kind == ChangeKind::Value) change.value = change.value * 2.0 - 1.0;
        return change;
    }

    Change touchUp (int x, int y) override
    {
        gestureKnob_.touchUp (x, y);
        dragging_ = false;
        return Change::none();
    }

    Change turn (double delta) override
    {
        if (! enabled_ || state.selectedStep < 0 || state.selectedStep >= kNumSteps || state.activeRow < 0 || state.activeRow >= kNumRows)
            return Change::none();
        const float cur = state.values[state.activeRow][state.selectedStep];
        float val = std::clamp (cur + static_cast<float> (delta * 0.05), -1.0f, 1.0f);
        if (std::fabs (val) < 0.02f)
            val = 0.0f;
        if (val == cur)   // at the end of the range, or a step that rounds back: nothing changes, nothing is reported
            return Change::none();
        return Change::normalised (stepId (state.activeRow, state.selectedStep), val);
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen p (sb, enabled_);
        Cell cell (sb, rect_);
        drawBody (p);
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        for (int step = 0; step < kNumSteps; ++step)
        {
            const Rect col = columnRect (step);
            const Rect dial = knobRect (step);
            const bool active = step < state.length;
            const bool selected = step == state.selectedStep;
            char label[8];
            std::snprintf (label, sizeof label, "%d", step + 1);
            p.textCentered ({ col.x, col.y + 4, col.w, 24 }, label, T.fLabel,
                            p.c (selected ? T.flow[0] : (active ? T.text : T.textDim), 255), T.fMin);
            if (state.voiceStepMask & (1 << step))
                p.disc (static_cast<float> (col.right() - 12), static_cast<float> (col.y + 14), 4.5f, p.c (T.flow[0], 255));
            char formatted[24];
            formatValue (state.activeRow, value (state.activeRow, step), formatted, sizeof formatted);
            p.textCentered ({ col.x, dial.bottom() + voicelayout::kStepValueGap, col.w, 24 }, formatted,
                            T.fLabel, p.c (selected ? T.flow[0] : (active ? T.text : T.textDim), 255), T.fMin);
        }
    }

private:
    bool dragging_ = false;
    Knob gestureKnob_;

public:
    static void formatValue (int row, float val, char* out, size_t maxLen) noexcept
    {
        if (std::fabs (val) < 0.005f)
        {
            if (row == 0) std::snprintf (out, maxLen, "MID");
            else if (row == 1) std::snprintf (out, maxLen, "0 c");
            else std::snprintf (out, maxLen, "0%%");
            return;
        }
        if (row == 0) // PAN: %
        {
            int p = static_cast<int> (std::lround (val * 100.0f));
            if (p > 0) std::snprintf (out, maxLen, "%dR", p);
            else std::snprintf (out, maxLen, "%dL", -p);
        }
        else if (row == 1) // DETUNE: cents
        {
            int c = static_cast<int> (std::lround (val * 100.0f));
            std::snprintf (out, maxLen, "%+d c", c);
        }
        else // CUTOFF, ENV, MOD1, MOD2: %
        {
            int p = static_cast<int> (std::lround (val * 100.0f));
            std::snprintf (out, maxLen, "%+d%%", p);
        }
    }
};

}} // namespace synthyboy_mpc::ui
