// Read-only cached settings cell bank of the standalone Osc root panels.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"
#include "native_osc_controls.h"

namespace synthyboy_mpc::nativeui::osc
{
/** Read-only cached settings cell bank for the standalone Osc root panels. */
class SettingsBank : public NativeCellBank<kSettingsAll>
{
public:
    void suspend() noexcept
    {
        suspended_.store (true, std::memory_order_release);
        renewInputRevisions();
    }

    void publish (int view, const FxCell* fields, bool interactive = true) noexcept
    {
        if (! interactive)
            suspended_.store (true, std::memory_order_release);
        if (suspended_.load (std::memory_order_relaxed))
            renewInputRevisions();
        NativeCellBank<kSettingsAll>::publish (view, fields);
        suspended_.store (! interactive, std::memory_order_release);
    }

    void post (Snapshot current, float value) noexcept
    {
        if (suspended_.load (std::memory_order_acquire))
            return;
        NativeCellBank<kSettingsAll>::post (current, value);
    }

    bool take (int slot, const FxCell& expected, float& value) noexcept
    {
        if (suspended_.load (std::memory_order_relaxed))
            return false;
        return NativeCellBank<kSettingsAll>::take (slot, expected, value);
    }

    bool suspended() const noexcept { return suspended_.load (std::memory_order_acquire); }

private:
    std::atomic<bool> suspended_ { true };
};
} // namespace synthyboy_mpc::nativeui::osc
