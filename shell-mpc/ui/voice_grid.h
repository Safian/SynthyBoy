// Voice sequence sounding-step indicators and per-row display formatting.
// The host skin owns the step selectors, dials and readouts.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core.h"
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
        uint8_t voiceStepMask = 0;    // bit s is 1 if step s has an active sounding voice
    };
    State state;
    static const char* rowName (int r) noexcept
    {
        static const char* const kNames[kNumRows] = { "PAN", "DETUNE", "CUTOFF", "ENV", "MOD 1", "MOD 2" };
        return r >= 0 && r < kNumRows ? kNames[r] : "";
    }

    VoiceGrid() = default;

    Size minSize() const override { return { 800, 200 }; }

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

    Rect knobRect (int step) const noexcept
    {
        const Rect col = columnRect (step);
        return { col.x + (col.w - voicelayout::kStepDiameter) / 2,
                 col.y + voicelayout::kStepTop, voicelayout::kStepDiameter, voicelayout::kStepDiameter };
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        for (int step = 0; step < kNumSteps; ++step)
        {
            const Rect col = columnRect (step);
            if (state.voiceStepMask & (1 << step))
                p.disc (static_cast<float> (col.right() - 12), static_cast<float> (col.y + 14), 4.5f, p.c (T.flow[0], 255));
        }
    }

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
