// SynthyBoy - core: the racks of the effect chain as lists of modules, and the edits of those lists (see fx_rack.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/fx_rack.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace synthyboy
{
namespace
{
constexpr const char* kModulePrefix[kNumFxSlots] = { "chorus", "compressor", "delay", "distortion", "eq", "fxfilter", "flanger", "phaser", "reverb",
                                                      "hyperdim", "utility", "bode", "convolve", "splitter_lh", "splitter_lmh", "splitter_ms" };
constexpr const char* kModuleName[kNumFxSlots] = { "Chorus", "Compressor", "Delay", "Distortion", "EQ", "FX Filter", "Flanger", "Phaser", "Reverb",
                                                    "Hyper/Dimension", "Utility", "Bode", "Convolve", "Splitter L/H", "Splitter L/M/H", "Splitter M/S" };

struct Tables
{
    int on[kNumFxSlots];
    int rack[kNumFxSlots];
    int order[kNumFxSlots];   // fx.order.1 .. 16
    Tables()
    {
        const auto find = [] (const std::string& key) { const ParamInfo* info = findParamByKey (key.c_str()); return info != nullptr ? findParamIndex (info->id) : -1; };
        for (int m = 0; m < kNumFxSlots; ++m)
        {
            on[m] = find (std::string (kModulePrefix[m]) + ".on");
            rack[m] = find (std::string (kModulePrefix[m]) + ".rack");
            order[m] = find ("fx.order." + std::to_string (m + 1));
        }
    }
};

const Tables& tables() noexcept
{
    static const Tables t;
    return t;
}

int moduleRack (const float* plain, int module) noexcept
{
    int rack = static_cast<int> (std::lround (plain[tables().rack[module]]));
    rack = std::clamp (rack, 0, kNumFxRacks - 1);
    if (module >= 13 && rack >= 3)
        rack = 0; // a splitter cannot sit in the band of another splitter (the engine does the same)
    return rack;
}

/** fx.order repaired to a permutation as the engine does it (resolveFxOrder): a position whose module is already placed takes the lowest free one. */
void repairedOrder (const float* plain, int (&at)[kNumFxSlots]) noexcept
{
    bool used[kNumFxSlots] = {};
    for (int position = 0; position < kNumFxSlots; ++position)
    {
        const float f = plain[tables().order[position]];
        int module = f >= 0.0f && f < static_cast<float> (kNumFxSlots) ? static_cast<int> (f + 0.5f) : -1;
        if (module < 0 || module >= kNumFxSlots || used[module])
        {
            module = 0;
            while (used[module])
                ++module;
        }
        used[module] = true;
        at[position] = module;
    }
}

struct Entry
{
    int key, kind, index;
    bool operator< (const Entry& o) const noexcept
    {
        if (key != o.key)
            return key < o.key;
        if (kind != o.kind)
            return kind < o.kind; // a module of instance 0 before a slot on a tie
        return index < o.index;
    }
};

void push (std::vector<FxWrite>& writes, const float* plain, int param, float value)
{
    if (param >= 0 && plain[param] != value)
        writes.push_back ({ param, value });
}

/** Writes the final order of one rack list: the positions of the modules of instance 0 in it (a permutation of the positions they already hold) and the
    orders of the slots between them. */
void placeList (const float* plain, const std::vector<FxRackItem>& list, std::vector<FxWrite>& writes)
{
    int at[kNumFxSlots];
    repairedOrder (plain, at);
    int position[kNumFxSlots];
    for (int q = 0; q < kNumFxSlots; ++q)
        position[at[q]] = q;

    std::vector<int> modules, held;
    for (const FxRackItem& item : list)
        if (! item.pool)
        {
            modules.push_back (item.index);
            held.push_back (position[item.index]);
        }
    std::sort (held.begin(), held.end());
    int newAt[kNumFxSlots];
    std::copy (at, at + kNumFxSlots, newAt);
    int newPosition[kNumFxSlots];
    std::copy (position, position + kNumFxSlots, newPosition);
    for (size_t i = 0; i < modules.size(); ++i)
    {
        newAt[held[i]] = modules[i];
        newPosition[modules[i]] = held[i];
    }
    for (int q = 0; q < kNumFxSlots; ++q)
        push (writes, plain, tables().order[q], static_cast<float> (newAt[q]));

    // the slots: a run of slots between two modules of instance 0 takes the keys right behind the first one
    size_t i = 0;
    int previousKey = -1;
    while (i < list.size())
    {
        if (! list[i].pool)
        {
            previousKey = fxModuleSortKey (newPosition[list[i].index]);
            ++i;
            continue;
        }
        int key = previousKey;
        while (i < list.size() && list[i].pool)
        {
            key = std::min (kFxOrderMax, key + 1);
            push (writes, plain, fxInstanceKeys (list[i].index).order, static_cast<float> (key));
            ++i;
        }
        previousKey = key;
    }
}
} // namespace

bool FxRacks::find (FxRackItem item, int& rackOut, int& indexOut) const noexcept
{
    for (int r = 0; r < kNumFxRacks; ++r)
        for (size_t i = 0; i < rack[static_cast<size_t> (r)].size(); ++i)
            if (rack[static_cast<size_t> (r)][i] == item)
            {
                rackOut = r;
                indexOut = static_cast<int> (i);
                return true;
            }
    return false;
}

FxRacks fxRacks (const float* plain) noexcept
{
    FxRacks out;
    int at[kNumFxSlots];
    repairedOrder (plain, at);
    for (int q = 0; q < kNumFxSlots; ++q)
        out.position[static_cast<size_t> (at[q])] = q;

    std::array<std::vector<Entry>, kNumFxRacks> entries;
    for (int m = 0; m < kNumFxSlots; ++m)
        entries[static_cast<size_t> (moduleRack (plain, m))].push_back ({ fxModuleSortKey (out.position[static_cast<size_t> (m)]), 0, m });
    for (int s = 0; s < kNumFxInstances; ++s)
    {
        const FxInstanceKeys& k = fxInstanceKeys (s);
        if (static_cast<int> (std::lround (plain[k.type])) == kFxTypeNone)
            continue;
        const int rack = std::clamp (static_cast<int> (std::lround (plain[k.rack])), 0, kNumPoolRacks - 1);
        entries[static_cast<size_t> (rack)].push_back ({ static_cast<int> (std::lround (plain[k.order])), 1, s });
    }
    for (int r = 0; r < kNumFxRacks; ++r)
    {
        auto& list = entries[static_cast<size_t> (r)];
        std::sort (list.begin(), list.end());
        for (const Entry& e : list)
            out.rack[static_cast<size_t> (r)].push_back ({ e.kind == 1, e.index });
    }
    return out;
}

int fxItemRack (const float* plain, FxRackItem item) noexcept
{
    if (item.pool)
        return std::clamp (static_cast<int> (std::lround (plain[fxInstanceKeys (item.index).rack])), 0, kNumPoolRacks - 1);
    return moduleRack (plain, item.index);
}

bool fxItemOn (const float* plain, FxRackItem item) noexcept
{
    return plain[item.pool ? fxInstanceKeys (item.index).on : tables().on[item.index]] > 0.5f;
}

int fxItemType (const float* plain, FxRackItem item) noexcept
{
    if (item.pool)
        return std::clamp (static_cast<int> (std::lround (plain[fxInstanceKeys (item.index).type])), 0, kNumFxInstanceTypes - 1);
    return item.index < 13 ? item.index + 1 : kFxTypeNone; // the splitters have no type of the pool
}

const char* fxItemName (const float* plain, FxRackItem item) noexcept
{
    return item.pool ? fxTypeName (fxItemType (plain, item)) : kModuleName[item.index];
}

int fxFirstFreeSlot (const float* plain) noexcept
{
    for (int s = 0; s < kNumFxInstances; ++s)
        if (static_cast<int> (std::lround (plain[fxInstanceKeys (s).type])) == kFxTypeNone)
            return s;
    return -1;
}

int fxAddModule (const float* plain, int type, int rack, std::vector<FxWrite>& writes)
{
    if (type <= kFxTypeNone || type >= kNumFxInstanceTypes)
        return -1;
    const int slot = fxFirstFreeSlot (plain);
    if (slot < 0)
        return -1;
    rack = std::clamp (rack, 0, kNumPoolRacks - 1);
    const FxInstanceKeys& k = fxInstanceKeys (slot);
    push (writes, plain, k.type, static_cast<float> (type));
    push (writes, plain, k.on, 1.0f);
    push (writes, plain, k.rack, static_cast<float> (rack));
    float defaults[kFxInstanceParams];
    fxInstanceDefaults (type, defaults);
    for (int p = 0; p < kFxInstanceParams; ++p)
        push (writes, plain, k.p[p], defaults[p]);
    auto list = fxRacks (plain).rack[static_cast<size_t> (rack)];
    list.push_back ({ true, slot });
    placeList (plain, list, writes);
    return slot;
}

bool fxRemoveSlot (const float* plain, int slot, std::vector<FxWrite>& writes)
{
    if (slot < 0 || slot >= kNumFxInstances)
        return false;
    const FxInstanceKeys& k = fxInstanceKeys (slot);
    if (static_cast<int> (std::lround (plain[k.type])) == kFxTypeNone)
        return false;
    for (const int index : { k.type, k.on, k.rack, k.order })
        push (writes, plain, index, paramTable()[index].def);
    for (int p = 0; p < kFxInstanceParams; ++p)
        push (writes, plain, k.p[p], paramTable()[k.p[p]].def);
    return true;
}

int fxDuplicate (const float* plain, FxRackItem item, std::vector<FxWrite>& writes)
{
    const int type = fxItemType (plain, item);
    if (type == kFxTypeNone)
        return -1;
    const int slot = fxFirstFreeSlot (plain);
    if (slot < 0)
        return -1;
    const FxInstanceKeys& k = fxInstanceKeys (slot);
    const int itemRack = fxItemRack (plain, item);
    const int rack = itemRack < kNumPoolRacks ? itemRack : 0; // the band of a splitter has no pool slot: the copy goes to Main
    push (writes, plain, k.type, static_cast<float> (type));
    push (writes, plain, k.on, fxItemOn (plain, item) ? 1.0f : 0.0f);
    push (writes, plain, k.rack, static_cast<float> (rack));
    for (int p = 0; p < kFxInstanceParams; ++p)
    {
        float value = 0.0f;
        if (item.pool)
            value = plain[fxInstanceKeys (item.index).p[p]];
        else if (const ParamInfo* src = fxInstanceSource (type, p))
            value = toNormalised (*src, plain[fxTypeDescriptor (type).source[p]]);
        push (writes, plain, k.p[p], value);
    }
    // right behind the original when it is in the same rack, else at the end of the rack
    auto list = fxRacks (plain).rack[static_cast<size_t> (rack)];
    const auto at = std::find (list.begin(), list.end(), item);
    list.insert (at != list.end() ? at + 1 : list.end(), FxRackItem { true, slot });
    placeList (plain, list, writes);
    return slot;
}

bool fxMove (const float* plain, FxRackItem item, int rack, int index, std::vector<FxWrite>& writes)
{
    rack = std::clamp (rack, 0, kNumPoolRacks - 1);
    const FxRacks racks = fxRacks (plain);
    int fromRack = 0, fromIndex = 0;
    if (! racks.find (item, fromRack, fromIndex))
        return false;
    auto list = racks.rack[static_cast<size_t> (rack)];
    if (fromRack == rack)
        list.erase (list.begin() + fromIndex);
    index = std::clamp (index, 0, static_cast<int> (list.size()));
    if (fromRack == rack && index == fromIndex)
        return false;
    list.insert (list.begin() + index, item);

    push (writes, plain, item.pool ? fxInstanceKeys (item.index).rack : tables().rack[item.index], static_cast<float> (rack));
    placeList (plain, list, writes);
    return ! writes.empty();
}

void fxSetOn (const float* plain, FxRackItem item, bool on, std::vector<FxWrite>& writes)
{
    push (writes, plain, item.pool ? fxInstanceKeys (item.index).on : tables().on[item.index], on ? 1.0f : 0.0f);
}

bool fxInitSlot (const float* plain, int slot, std::vector<FxWrite>& writes)
{
    if (slot < 0 || slot >= kNumFxInstances)
        return false;
    const FxInstanceKeys& k = fxInstanceKeys (slot);
    const int type = static_cast<int> (std::lround (plain[k.type]));
    if (type == kFxTypeNone)
        return false;
    float defaults[kFxInstanceParams];
    fxInstanceDefaults (type, defaults);
    for (int p = 0; p < kFxInstanceParams; ++p)
        push (writes, plain, k.p[p], defaults[p]);
    return true;
}

void fxNormalise (const float* plain, std::vector<FxWrite>& writes)
{
    const FxRacks racks = fxRacks (plain);
    for (int r = 0; r < kNumPoolRacks; ++r)
        placeList (plain, racks.rack[static_cast<size_t> (r)], writes);
}

FxRackClip fxCopyRack (const float* plain, int rack)
{
    FxRackClip clip;
    if (rack < 0 || rack >= kNumPoolRacks)
        return clip;
    const FxRacks racks = fxRacks (plain);
    for (const FxRackItem& item : racks.rack[static_cast<size_t> (rack)])
    {
        const int type = fxItemType (plain, item);
        if (type <= kFxTypeNone || type >= kNumFxInstanceTypes)
            continue; // an empty slot, or a splitter
        if (! item.pool && ! fxItemOn (plain, item))
            continue; // a bypassed module of instance 0 is not in the rack
        FxRackClip::Entry e;
        e.type = type;
        e.on = fxItemOn (plain, item);
        for (int p = 0; p < kFxInstanceParams; ++p)
        {
            if (item.pool)
                e.p[p] = plain[fxInstanceKeys (item.index).p[p]];
            else if (const ParamInfo* src = fxInstanceSource (type, p))
                e.p[p] = toNormalised (*src, plain[fxTypeDescriptor (type).source[p]]);
        }
        clip.entries.push_back (e);
    }
    return clip;
}

int fxClearRack (const float* plain, int rack, std::vector<FxWrite>& writes)
{
    if (rack < 0 || rack >= kNumPoolRacks)
        return 0;
    int removed = 0;
    const FxRacks racks = fxRacks (plain);
    for (const FxRackItem& item : racks.rack[static_cast<size_t> (rack)])
    {
        if (item.pool)
            removed += fxRemoveSlot (plain, item.index, writes) ? 1 : 0;
        else if (fxItemType (plain, item) != kFxTypeNone && fxItemOn (plain, item)) // (a splitter stays)
        {
            fxSetOn (plain, item, false, writes);
            ++removed;
        }
    }
    return removed;
}

int fxPasteRack (const float* plain, const FxRackClip& clip, int rack, std::vector<FxWrite>& writes)
{
    if (rack < 0 || rack >= kNumPoolRacks)
        return 0;
    // every module is added to the state the earlier ones left: the next free slot, the end of the rack
    std::vector<float> state (plain, plain + paramCount());
    int pasted = 0;
    for (const FxRackClip::Entry& e : clip.entries)
    {
        std::vector<FxWrite> step;
        const int slot = fxAddModule (state.data(), e.type, rack, step);
        if (slot < 0)
            break; // the pool is full
        fxApply (state.data(), step);
        const FxInstanceKeys& k = fxInstanceKeys (slot);
        step.clear();
        push (step, state.data(), k.on, e.on ? 1.0f : 0.0f);
        for (int p = 0; p < kFxInstanceParams; ++p)
            push (step, state.data(), k.p[p], e.p[p]);
        fxApply (state.data(), step);
        ++pasted;
    }
    // the writes that make `plain` into `state`
    for (size_t i = 0; i < state.size(); ++i)
        if (state[i] != plain[i])
            writes.push_back ({ static_cast<int> (i), state[i] });
    return pasted;
}

void fxApply (float* plain, const std::vector<FxWrite>& writes) noexcept
{
    for (const FxWrite& w : writes)
        plain[w.param] = w.value;
}
} // namespace synthyboy
