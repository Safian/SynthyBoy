// SynthyBoy - core: the racks of the effect chain as lists of modules, and the edits of those lists (docs/ui-design/18-FX-INSTANCES.md section 2.8).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pure functions on the plain values of the parameters (indexed like paramTable()): they read the racks as the engine builds them (the modules of instance 0
// by their position in fx.order, the slots of the pool by fxiNN.order) and turn an edit (add, remove, duplicate, move, bypass) into the list of parameter
// writes that makes it. The UI applies the writes through the host parameters, the tests apply them to a ParamValues; both use the same code.
#pragma once

#include <array>
#include <vector>

#include "fx_types.h"

namespace synthyboy
{
/** Number of racks of the chain: Main, Bus 1, Bus 2, then the bands of the three splitters (modules of instance 0 only). */
constexpr int kNumFxRacks = 10;
/** Racks a pool slot can be in (fxiNN.rack). */
constexpr int kNumPoolRacks = 3;

/** The order key of the module of instance 0 at position q of fx.order (the slots sort by fxiNN.order on the same scale). The offset leaves room for
    slots in front of the first module. */
constexpr int fxModuleSortKey (int position) noexcept { return position * kFxOrderStep + kFxOrderBase; }

/** One module of a rack: a module of instance 0 (pool == false, index 0 .. 15 = the fx.order choice) or a slot of the pool (index 0 .. 15). */
struct FxRackItem
{
    bool pool = false;
    int index = 0;
    bool operator== (const FxRackItem& o) const noexcept { return pool == o.pool && index == o.index; }
    bool operator!= (const FxRackItem& o) const noexcept { return ! (*this == o); }
};

/** The modules of every rack in processing order. Instance 0 is always listed (all 16 modules, on or off); a slot only while its type is not none. */
struct FxRacks
{
    std::array<std::vector<FxRackItem>, kNumFxRacks> rack;
    /** Position (0 .. 15) of each module of instance 0 in the chain order (fx.order repaired to a permutation as the engine does). */
    std::array<int, kNumFxSlots> position {};

    /** The rack and index in it of an item, or false. */
    bool find (FxRackItem item, int& rackOut, int& indexOut) const noexcept;
};

/** plain: the values of all parameters, indexed like paramTable() (ParamValues::plain). */
FxRacks fxRacks (const float* plain) noexcept;

/** Rack (0 .. 9) an item is in. A module of instance 0 in an invalid rack counts as Main. */
int fxItemRack (const float* plain, FxRackItem item) noexcept;
/** The power of the item (its on switch). */
bool fxItemOn (const float* plain, FxRackItem item) noexcept;
/** The type of the item: kFxType* of the module (instance 0 included: the module index + 1); none for a slot without a module. */
int fxItemType (const float* plain, FxRackItem item) noexcept;
/** "Chorus", "Reverb" ... the name of the type; the number of an instance is not part of it. */
const char* fxItemName (const float* plain, FxRackItem item) noexcept;

/** A parameter write: table index and plain value. */
struct FxWrite
{
    int param;
    float value;
};

/** The first slot without a module, or -1 if the pool is full. */
int fxFirstFreeSlot (const float* plain) noexcept;

/** Adds a module of `type` to `rack` (0 .. 2) at the end of it, in the first free slot; fills the writes of the slot (type, on, rack, order and the default
    parameters of the type). Returns the slot, or -1 (pool full, type not allowed: nothing is written). */
int fxAddModule (const float* plain, int type, int rack, std::vector<FxWrite>& writes);
/** Removes the module of a slot: the slot is back at its defaults. False for a slot that has no module. */
bool fxRemoveSlot (const float* plain, int slot, std::vector<FxWrite>& writes);
/** Copies the module (the parameters, the power, the rack) into the first free slot, right behind the original. Returns the slot, or -1 (pool full, or a
    splitter, which has no pool type). */
int fxDuplicate (const float* plain, FxRackItem item, std::vector<FxWrite>& writes);
/** Moves an item to `rack` (0 .. 2 for a slot, 0 .. 2 for the modules of instance 0 as well) at index `index` of that rack's list (counted without the item;
    clamped). Renumbers fx.order and the orders of the slots so that the lists come out as asked. False if nothing changes. */
bool fxMove (const float* plain, FxRackItem item, int rack, int index, std::vector<FxWrite>& writes);
/** The power of an item. */
void fxSetOn (const float* plain, FxRackItem item, bool on, std::vector<FxWrite>& writes);
/** The parameters of a slot back to the defaults of its type (type, power, rack and order stay): the Init of the module sheet. */
bool fxInitSlot (const float* plain, int slot, std::vector<FxWrite>& writes);

/** Gives the slots of every rack distinct orders in the order the engine would process them (after a paste or a swap of slots, which copy the order of
    the other slot): nothing changes in what is played. */
void fxNormalise (const float* plain, std::vector<FxWrite>& writes);

/** Applies writes to a plain value array (the tests, and the UI for its model). */
void fxApply (float* plain, const std::vector<FxWrite>& writes) noexcept;
} // namespace synthyboy
