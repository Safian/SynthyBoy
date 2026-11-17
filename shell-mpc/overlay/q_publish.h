// SynthyBoy - MPC overlay: how a plugin-drawn page publishes its Q slots. A page knows its Q GROUPS in reading order (four values each) and which one is active; QRouter::mapGroups puts the active group on the Q proxies
// 1 .. 4 and the next groups of the page (wrapping, each once) on 5 .. 8, 9 .. 12 and 13 .. 16. The page asks for the slots of its groups with a callback and hands them over here; the router is told only when a slot changed.
// A page that only knows one group publishes it with groups = 1 (the slots 5 .. 16 have no target).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstring>

#include "../q_router.h"
#include "curve_source.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace overlay {

/** The groups of a screen that have members, in index order (a screen can have a group that is empty for the instance on it, like the FORMANT block of a filter without formants): ids[k] is the index of the k-th one, the
    result is how many there are; `activePos` gets the position of `active` among them (-1: the active group is none of them). */
template <class Group>
inline int usedGroups (const Group* groups, int n, int active, int* ids, int& activePos) noexcept
{
    int used = 0;
    activePos = -1;
    for (int g = 0; g < n; ++g)
        if (groups[g].numMembers() > 0)
        {
            if (g == active)
                activePos = used;
            ids[used++] = g;
        }
    return used;
}

/** Tells every group of a screen which block of knobs it has (QGroup::setBlock) once the page's `used` groups (ids as above) are mapped with the active position `activePos`; the others get none. */
template <class Group>
inline void setBlocks (Group* groups, int n, const int* ids, int used, int activePos) noexcept
{
    for (int g = 0; g < n; ++g)
        groups[g].setBlock (-1);
    for (int k = 0; k < used; ++k)
        groups[ids[k]].setBlock (QRouter::blockOfGroup (used, activePos, k));
}

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

/** The sixteen slots of a page and what was published of them. `Inst` is any instance interface with setQGroup (const int*, int): IInstanceView of SynthyBoy, mpckit::IInstance of the kit (migration
    step 1 of docs/ui-components/TEMPLATE.md; the callers are unchanged: the type is deduced). */
struct QSlots
{
    int target[QRouter::kCount];
    bool set = false;   // false: nothing published since the page was shown (the next publish always tells the instance)

    QSlots() noexcept { clear(); }
    void clear() noexcept { std::memset (target, 0xff, sizeof target); }   // -1 everywhere
    void forget() noexcept { set = false; }
    const int* data() const noexcept { return target; }

    /** Maps the page: nGroups groups, `active` the active one, targetsOf (group, int* four) the targets of a group; tells the instance when something changed. Returns the slot count in use. */
    template <class Inst, class TargetsOf>
    int publish (Inst& v, int nGroups, int active, TargetsOf&& targetsOf)
    {
        int t[QRouter::kCount];
        const int used = QRouter::mapGroups (nGroups, active, targetsOf, t);
        publishRaw (v, t);
        return used;
    }
    /** The slots are known already (kCount values). */
    template <class Inst>
    void publishRaw (Inst& v, const int* t)
    {
        if (! set || std::memcmp (t, target, sizeof target) != 0)
        {
            std::memcpy (target, t, sizeof target);
            v.setQGroup (t, QRouter::kCount);
            set = true;
        }
    }
};

}} // namespace synthyboy_mpc::overlay
