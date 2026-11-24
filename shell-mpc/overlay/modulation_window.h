// SynthyBoy - visible routes for the modulation editor of one destination.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "../ui/panel.h"
#include "ui_bind.h"

namespace synthyboy_mpc::overlay::uibind
{
struct ModulationWindow
{
    ui::ModInput entries;
    int total = 0, page = 0, focusRow = -1;
};

inline ModulationWindow modulationWindow (const IInstanceView& inst, int parameter, int selectedSource,
                                          int requestedPage, ModRow* visible, int focusSlot = -1)
{
    constexpr int perPage = ui::ModulationsPage::kPerPage;
    ModulationWindow result;
    result.entries.selectedSource = selectedSource;
    std::fill_n (visible, perPage, ModRow {});
    const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (parameter));
    if (destination == 0)
        return result;

    std::array<ModRow, synthyboy::kNumModSlots> matches;
    for (int slot = 0; slot < synthyboy::kNumModSlots; ++slot)
    {
        const auto p = synthyboy::modSlotParams (slot);
        const int source = static_cast<int> (std::lround (inst.param (p.source)));
        if (source <= 0 || static_cast<size_t> (std::lround (inst.param (p.destination))) != destination)
            continue;
        if (slot == focusSlot)
        {
            requestedPage = result.total / perPage;
            result.focusRow = result.total % perPage;
        }
        matches[result.total++] = {slot, source};
    }

    result.page = std::clamp (requestedPage, 0, std::max (0, (result.total - 1) / perPage));
    const int first = result.page * perPage;
    const int count = std::min (perPage, result.total - first);
    for (int row = 0; row < count; ++row)
    {
        const ModRow binding = matches[first + row];
        const auto p = synthyboy::modSlotParams (binding.slot);
        char name[7];
        shortName (binding.source, name);
        result.entries.add (binding.source, name, inst.param (p.amount) / 100.0,
                            inst.param (p.bipolar) >= 0.5f, inst.param (p.bypass) >= 0.5f, isMacroSource (binding.source));
        visible[row] = binding;
    }
    return result;
}

inline bool modulationMatches (const IInstanceView& inst, int parameter, ModRow route) noexcept
{
    if (parameter < 0 || route.slot < 0 || route.source <= 0)
        return false;
    const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (parameter));
    const auto p = synthyboy::modSlotParams (route.slot);
    return destination != 0 && static_cast<int> (std::lround (inst.param (p.source))) == route.source
        && static_cast<size_t> (std::lround (inst.param (p.destination))) == destination;
}

/** The destination editor starts a newly selected source at zero depth. */
inline int ensureModulationSource (IInstanceView& inst, int parameter, int source)
{
    if (parameter < 0 || source <= 0)
        return -1;
    const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (parameter));
    if (destination == 0)
        return -1;
    const int existing = slotOf (inst, parameter, source);
    if (existing >= 0)
        return existing;
    const int slot = freeSlot (inst);
    if (slot < 0)
        return -1;
    const auto p = synthyboy::modSlotParams (slot);
    writePlain (inst, p.destination, static_cast<double> (destination));
    writePlain (inst, p.amount, 0.0);
    if (p.bypass >= 0)
        writePlain (inst, p.bypass, 0.0);
    if (p.bipolar >= 0)
        writePlain (inst, p.bipolar, info (parameter).min < 0.0f && info (parameter).max > 0.0f ? 1.0 : 0.0);
    writePlain (inst, p.source, source);
    return slot;
}
}
