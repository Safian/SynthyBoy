// SynthyBoy - UI: the edits of the effect racks on the live parameters.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxRackOps.h"

namespace synthyboy::ui::fx_ops
{
namespace
{
bool commit (UiContext& ctx, ParamValues& live, const std::vector<FxWrite>& writes)
{
    if (writes.empty())
        return false;
    fxApply (live.plain, writes);
    applyValues (ctx, live);
    ctx.assign.sendChangeMessage();
    return true;
}
} // namespace

ParamValues values (const UiContext& ctx)
{
    return readLiveValues (ctx);
}

FxRacks racks (const UiContext& ctx)
{
    return fxRacks (readLiveValues (ctx).plain);
}

bool setOn (UiContext& ctx, FxRackItem item, bool on)
{
    auto live = values (ctx);
    std::vector<FxWrite> writes;
    fxSetOn (live.plain, item, on, writes);
    return commit (ctx, live, writes);
}

bool toggleOn (UiContext& ctx, FxRackItem item)
{
    return setOn (ctx, item, ! fxItemOn (values (ctx).plain, item));
}

int addModule (UiContext& ctx, int type, int rack)
{
    auto live = values (ctx);
    std::vector<FxWrite> writes;
    const int slot = fxAddModule (live.plain, type, rack, writes);
    if (slot >= 0)
        commit (ctx, live, writes);
    return slot;
}

bool removeSlot (UiContext& ctx, int slot)
{
    auto live = values (ctx);
    std::vector<FxWrite> writes;
    return fxRemoveSlot (live.plain, slot, writes) && commit (ctx, live, writes);
}

int duplicate (UiContext& ctx, FxRackItem item)
{
    auto live = values (ctx);
    std::vector<FxWrite> writes;
    const int slot = fxDuplicate (live.plain, item, writes);
    if (slot >= 0)
        commit (ctx, live, writes);
    return slot;
}

bool move (UiContext& ctx, FxRackItem item, int rack, int index)
{
    auto live = values (ctx);
    std::vector<FxWrite> writes;
    return fxMove (live.plain, item, rack, index, writes) && commit (ctx, live, writes);
}

void bypassAll (UiContext& ctx, uint32_t& savedMask)
{
    auto live = values (ctx);
    std::vector<FxWrite> writes;
    uint32_t on = 0;
    for (int m = 0; m < kNumFxSlots; ++m)
        on |= fxItemOn (live.plain, { false, m }) ? (1u << m) : 0u;
    for (int s = 0; s < kNumFxInstances; ++s)
        if (fxItemType (live.plain, { true, s }) != kFxTypeNone && fxItemOn (live.plain, { true, s }))
            on |= 1u << (16 + s);

    if (on != 0)
    {
        savedMask = on;
        for (int m = 0; m < kNumFxSlots; ++m)
            fxSetOn (live.plain, { false, m }, false, writes);
        for (int s = 0; s < kNumFxInstances; ++s)
            fxSetOn (live.plain, { true, s }, false, writes);
    }
    else
    {
        const uint32_t mask = savedMask != 0 ? savedMask : 0xFFFFu;
        for (int m = 0; m < kNumFxSlots; ++m)
            if ((mask & (1u << m)) != 0)
                fxSetOn (live.plain, { false, m }, true, writes);
        for (int s = 0; s < kNumFxInstances; ++s)
            if ((mask & (1u << (16 + s))) != 0 && fxItemType (live.plain, { true, s }) != kFxTypeNone)
                fxSetOn (live.plain, { true, s }, true, writes);
    }
    commit (ctx, live, writes);
}
} // namespace synthyboy::ui::fx_ops
