// SynthyBoy - native control context for the Voice step sequence.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"

namespace synthyboy_mpc::nativeui::voice
{
enum Field { kRow, kStep, kValue, kCount };
static_assert (kCount == kVoiceSequenceCount);
constexpr int kRows = 6, kSteps = 8;
constexpr const char* kNames[kCount] { "Voice Row List", "Voice Step Select", "Voice Step Value" };
constexpr const char* kRowNames[kRows] { "PAN", "DETUNE", "CUTOFF", "ENV", "MOD 1", "MOD 2" };

struct ViewState
{
    bool active = false;
    int row = 0, step = 0, target = -1;
    uint64_t preset = 0;

    FxCell cell (int field) const noexcept
    {
        if (! active)
            return {};
        switch (field)
        {
            case kRow: return { row, step, kFxDial };
            case kStep: return { step, row, kFxDial };
            case kValue: return { target, row * kSteps + step, kFxBipolar };
            default: return {};
        }
    }
};

/** One editor per plugin instance. A native write belongs to the reported
    binding, including its row, step and preset, until the overlay takes it. */
class Bank : public NativeCellBank<kCount>
{
public:
    void publish (const ViewState& next) noexcept
    {
        if (next.preset != preset_)
            invalidate();
        preset_ = next.preset;
        const FxCell cells[kCount] { next.cell (kRow), next.cell (kStep), next.cell (kValue) };
        NativeCellBank<kCount>::publish (next.active ? 1 : 0, cells);
    }

    bool take (const ViewState& expected, int field, float& value) noexcept
    {
        const FxCell cell = expected.preset == preset_ ? expected.cell (field) : FxCell {};
        return NativeCellBank<kCount>::take (field, cell, value);
    }

private:
    uint64_t preset_ = 0;  // overlay thread only
};
}
