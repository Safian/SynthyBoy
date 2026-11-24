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

/** The matrix page, four groups in reading order: [ROW (the virtual target `virtualRow`), SOURCE, TARGET, AMOUNT] of the selected slot, [UNI / BI, ON / OFF, AUX SOURCE, AUX SCALE] of the same slot, then the two next visible rows as
    [SOURCE, TARGET, AMOUNT, UNI / BI]; `slot1`, `slot2` the slots of those rows (-1: no such row, the group is empty). `t` has four entries per call. */
inline void matrixGroupTargets (int group, int selected, int slot1, int slot2, int virtualRow, int* t) noexcept
{
    t[0] = t[1] = t[2] = t[3] = -1;
    if (group == 0 || group == 1)
    {
        if (selected < 0)
            return;
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (selected);
        if (group == 0)
        {
            t[0] = QRouter::encode (virtualRow, QRouter::kVirtual); t[1] = sp.source; t[2] = sp.destination; t[3] = sp.amount;
        }
        else
        {
            t[0] = sp.bipolar; t[1] = sp.bypass; t[2] = sp.auxSource; t[3] = sp.auxOutput;
        }
        return;
    }
    const int slot = group == 2 ? slot1 : slot2;
    if (slot >= 0)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        t[0] = sp.source; t[1] = sp.destination; t[2] = sp.amount; t[3] = sp.bipolar;
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
