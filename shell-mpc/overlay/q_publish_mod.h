// SynthyBoy - MPC overlay: the Q slots of the rows of the modulation lists (ModulationsPage, ModTargetsPage): the three knobs of a row (modRowTargets) and the blocks the rows have (setRowBlocks). SynthyBoy specific: it names the
// parameters of a modulation slot (synthyboy/modulation.h); the generic part of the Q publishing is q_publish.h (migration step 1b of docs/ui-components/TEMPLATE.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../q_router.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace overlay {

/** The three knobs of a modulation row (the rows of the modulation lists and of the targets lists are groups of their own): amount, polarity, on / off of the slot; a row without a slot has none. `t` has four entries, all set. */
inline void modRowTargets (int slot, int* t) noexcept
{
    t[0] = t[1] = t[2] = t[3] = -1;
    if (slot >= 0)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        t[0] = sp.amount; t[1] = sp.bipolar; t[2] = sp.bypass;
    }
}

/** The rows of a list page (ModulationsPage, ModTargetsPage) are the groups: tells the page which block of knobs every row has (its idle chips show it). */
template <class RowsPage>
inline void setRowBlocks (RowsPage& page, int maxRows, int n, int active) noexcept
{
    for (int r = 0; r < maxRows; ++r)
        page.setRowBlock (r, r < n ? QRouter::blockOfGroup (n, active, r) : -1);
}

}} // namespace synthyboy_mpc::overlay
