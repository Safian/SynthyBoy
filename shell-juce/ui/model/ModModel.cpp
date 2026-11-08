// SynthyBoy - UI view-model: modulation assign flow, mod snapshot, A/B compare.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModModel.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::ui
{
namespace
{
/** The slot parameter indices, looked up once (modSlotParams searches the table by key). */
const std::array<ModSlotParams, kNumModSlots>& slotTable()
{
    static const std::array<ModSlotParams, kNumModSlots> table = []
    {
        std::array<ModSlotParams, kNumModSlots> t {};
        for (int i = 0; i < kNumModSlots; ++i)
            t[static_cast<size_t> (i)] = modSlotParams (i);
        return t;
    }();
    return table;
}
} // namespace

ModSnapshot ModSnapshot::build (const ModMatrixAccess& access)
{
    ModSnapshot s;
    s.sourceCounts.assign (static_cast<size_t> (kNumModSources) + 1, 0);
    const auto& slots = slotTable();
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const ModSlotParams& p = slots[static_cast<size_t> (i)];
        if (p.source < 0 || ! slotInUse (access, p))
            continue;

        ModEntry e;
        e.slot = i;
        e.source = static_cast<int> (std::lround (access.get (p.source)));
        e.amount = access.get (p.amount);
        e.bipolar = access.get (p.bipolar) >= 0.5f;
        e.bypass = access.get (p.bypass) >= 0.5f;
        const int dest = modDestinationParamIndex (static_cast<size_t> (std::lround (access.get (p.destination))));
        if (dest < 0)
            continue;

        s.byDestination[dest].push_back (e);
        ++s.used;
        if (e.source >= 1 && e.source <= kNumModSources)
            ++s.sourceCounts[static_cast<size_t> (e.source)];
    }
    return s;
}

const std::vector<ModEntry>& ModSnapshot::entriesFor (int destParamIndex) const
{
    static const std::vector<ModEntry> none;
    const auto it = byDestination.find (destParamIndex);
    return it == byDestination.end() ? none : it->second;
}

std::vector<std::pair<int, ModEntry>> ModSnapshot::entriesForSource (int sourceChoice) const
{
    std::vector<std::pair<int, ModEntry>> out;
    for (const auto& kv : byDestination)
        for (const auto& e : kv.second)
            if (e.source == sourceChoice)
                out.emplace_back (kv.first, e);
    std::sort (out.begin(), out.end(), [] (const auto& x, const auto& y) { return x.second.slot < y.second.slot; });
    return out;
}

int ModSnapshot::destinationCount (int sourceChoice) const
{
    return sourceChoice >= 0 && static_cast<size_t> (sourceChoice) < sourceCounts.size() ? sourceCounts[static_cast<size_t> (sourceChoice)] : 0;
}

bool ModSnapshot::operator== (const ModSnapshot& other) const noexcept
{
    if (used != other.used || byDestination.size() != other.byDestination.size())
        return false;
    for (const auto& kv : byDestination)
    {
        const auto it = other.byDestination.find (kv.first);
        if (it == other.byDestination.end() || it->second.size() != kv.second.size())
            return false;
        for (size_t i = 0; i < kv.second.size(); ++i)
        {
            const ModEntry &x = kv.second[i], &y = it->second[i];
            if (x.slot != y.slot || x.source != y.source || x.amount != y.amount || x.bipolar != y.bipolar || x.bypass != y.bypass)
                return false;
        }
    }
    return true;
}

// ---- AssignFlow -----------------------------------------------------------------------------------------------------------------

AssignFlow::Phase AssignFlow::tapSource (int sourceChoice)
{
    if (state == Phase::dragging || sourceChoice < 1 || sourceChoice > kNumModSources)
        return state;

    if (state == Phase::armed && source == sourceChoice)
    {
        state = Phase::idle;
        source = 0;
    }
    else
    {
        state = Phase::armed;
        source = sourceChoice;
    }
    return state;
}

void AssignFlow::cancel()
{
    if (state == Phase::dragging)
        endDrag();
    state = Phase::idle;
    source = 0;
}

bool AssignFlow::isTarget (int paramIndex) const
{
    return state != Phase::idle && paramIndex >= 0 && modDestinationChoiceOf (static_cast<size_t> (paramIndex)) > 0;
}

bool AssignFlow::defaultBipolar (const ParamInfo& d) noexcept
{
    return d.scale != Scale::stepped && d.min < 0.0f && d.max > 0.0f;
}

int AssignFlow::findSlot (int sourceChoice, int destinationChoice) const
{
    const auto& slots = slotTable();
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const ModSlotParams& p = slots[static_cast<size_t> (i)];
        if (p.source >= 0 && std::lround (matrix.get (p.source)) == sourceChoice && std::lround (matrix.get (p.destination)) == destinationChoice)
            return i;
    }
    return -1;
}

int AssignFlow::findFreeSlot() const
{
    const auto& slots = slotTable();
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const ModSlotParams& p = slots[static_cast<size_t> (i)];
        if (p.source >= 0 && ! slotInUse (matrix, p))
            return i;
    }
    return -1;
}

AssignFlow::BeginResult AssignFlow::beginDrag (int paramIndex)
{
    if (state != Phase::armed)
        return BeginResult::notArmed;
    if (! isTarget (paramIndex))
        return BeginResult::notModulatable;

    const int destChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (paramIndex)));
    int s = findSlot (source, destChoice);
    const bool isNew = s < 0;
    if (isNew)
    {
        s = findFreeSlot();
        if (s < 0)
            return BeginResult::matrixFull;
    }

    const ModSlotParams& p = slotTable()[static_cast<size_t> (s)];
    if (isNew)
    {
        // source last: a half written slot is not in use (destination or source 0) and so is inaudible
        matrix.set (p.amount, 0.0f);
        matrix.set (p.bipolar, defaultBipolar (paramTable()[static_cast<size_t> (paramIndex)]) ? 1.0f : 0.0f);
        matrix.set (p.bypass, 0.0f);
        matrix.set (p.destination, static_cast<float> (destChoice));
        matrix.set (p.source, static_cast<float> (source));
    }

    slot = s;
    destination = paramIndex;
    state = Phase::dragging;
    matrix.beginGesture (p.amount);
    return BeginResult::ok;
}

float AssignFlow::amountNormalised() const
{
    if (state != Phase::dragging)
        return 0.5f;
    const ParamInfo& info = paramTable()[static_cast<size_t> (slotTable()[static_cast<size_t> (slot)].amount)];
    return toNormalised (info, matrix.get (slotTable()[static_cast<size_t> (slot)].amount));
}

void AssignFlow::dragBy (float dyUpPx, FineZone zone)
{
    if (state != Phase::dragging)
        return;
    const int idx = slotTable()[static_cast<size_t> (slot)].amount;
    const ParamInfo& info = paramTable()[static_cast<size_t> (idx)];
    const float n = applyDragDelta (toNormalised (info, matrix.get (idx)), dyUpPx, zone);
    matrix.set (idx, fromNormalised (info, n));
}

void AssignFlow::setAmount (float percent)
{
    if (state != Phase::dragging)
        return;
    const int idx = slotTable()[static_cast<size_t> (slot)].amount;
    matrix.set (idx, constrainValue (paramTable()[static_cast<size_t> (idx)], percent));
}

void AssignFlow::endDrag()
{
    if (state != Phase::dragging)
        return;
    matrix.endGesture (slotTable()[static_cast<size_t> (slot)].amount);
    slot = -1;
    destination = -1;
    state = Phase::armed;
}

void clearSlot (ModMatrixAccess& matrix, int slot)
{
    if (slot < 0 || slot >= kNumModSlots)
        return;
    const ModSlotParams& p = slotTable()[static_cast<size_t> (slot)];
    const int all[] = { p.source, p.destination, p.amount, p.power, p.bipolar, p.stereo, p.bypass };
    // source first: the slot is out of use before the rest changes
    for (const int idx : all)
        if (idx >= 0)
        {
            matrix.beginGesture (idx);
            matrix.set (idx, paramTable()[static_cast<size_t> (idx)].def);
            matrix.endGesture (idx);
        }
}

void setSlotBypass (ModMatrixAccess& matrix, int slot, bool bypass)
{
    if (slot < 0 || slot >= kNumModSlots)
        return;
    const int idx = slotTable()[static_cast<size_t> (slot)].bypass;
    if (idx < 0)
        return;
    matrix.beginGesture (idx);
    matrix.set (idx, bypass ? 1.0f : 0.0f);
    matrix.endGesture (idx);
}

// ---- AbCompare ------------------------------------------------------------------------------------------------------------------

const ParamValues& AbCompare::toggle (const ParamValues& live)
{
    if (current == Slot::a)
    {
        a = live;
        storedA = true;
        if (! storedB)
        {
            b = live;
            storedB = true;
        }
        current = Slot::b;
        return b;
    }
    b = live;
    storedB = true;
    if (! storedA)
    {
        a = live;
        storedA = true;
    }
    current = Slot::a;
    return a;
}

void AbCompare::copyActiveToOther (const ParamValues& live)
{
    a = live; // the live values are the active slot; the other slot becomes a copy of it
    b = live;
    storedA = storedB = true;
}
} // namespace synthyboy::ui
