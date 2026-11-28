// SynthyBoy - MPC overlay: how a plugin-drawn page publishes its Q slots. A page knows its Q GROUPS in reading order (four values each) and which one is active; QRouter::mapGroups puts the active group on the Q proxies
// 1 .. 4 and the next groups of the page (wrapping, each once) on 5 .. 8, 9 .. 12 and 13 .. 16. The page asks for the slots of its groups with a callback and hands them over here; the router is told only when a slot changed.
// A page that only knows one group publishes it with groups = 1 (the slots 5 .. 16 have no target). This header knows no engine: the helpers for the modulation rows (modRowTargets, setRowBlocks) are in q_publish_mod.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstring>

#include "../q_router.h"
#include "../ui/qgroup.h"

namespace synthyboy_mpc { namespace overlay {

/** Connects the router's device rule to the UI library's device flag (ui::QGroup::setWideDevice, set once by the overlay at start): one switch for the frames, the chips and the slot order. */
inline const bool kQDeviceRule = (QRouter::wideProbe() = &ui::QGroup::wideDevice, true);



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
