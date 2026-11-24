// SynthyBoy - presentation and Close actions for the Play page's full editors.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"

namespace synthyboy_mpc::nativeui::expandedplay
{
enum Frame { Hidden, OscRoot, OscDetail, EnvRoot, EnvDetail, LfoRoot, LfoDetail, LastFrame = LfoDetail };
enum Action { CloseOsc, CloseEnv, CloseLfo, Actions };
constexpr int kHandles = Actions + 1;
static_assert (kHandles == kPlayExpandedHandles);

constexpr int actionOf (Frame frame) noexcept
{
    switch (frame)
    {
        case OscRoot: return CloseOsc;
        case EnvRoot: return CloseEnv;
        case LfoRoot: return CloseLfo;
        default: return -1;
    }
}

struct State
{
    Frame frame = Hidden;
    int source = 0;
    uint64_t preset = 0;
};

class Bank : public NativeCellBank<Actions>
{
public:
    void publish (State next) noexcept
    {
        if (next.source != state_.source || next.preset != state_.preset)
            invalidate();
        state_ = next;
        FxCell cells[Actions] {};
        const int action = actionOf (next.frame);
        if (action >= 0)
            cells[action] = {next.source, next.source, kFxDial};
        NativeCellBank<Actions>::publish (next.frame, cells);
        preset_.store (next.preset, std::memory_order_release);
    }

    void post (int action, float value, uint64_t preset) noexcept
    {
        if (preset_.load (std::memory_order_acquire) == preset)
            NativeCellBank<Actions>::post (at (action), value);
    }

    bool take (State expected) noexcept
    {
        const int action = actionOf (expected.frame);
        float value;
        return action >= 0 && state_.frame == expected.frame && state_.source == expected.source && state_.preset == expected.preset
            && NativeCellBank<Actions>::take (action, {expected.source, expected.source, kFxDial}, value) && value >= 0.5f;
    }

private:
    State state_ {};
    std::atomic<uint64_t> preset_ {};
};
}
