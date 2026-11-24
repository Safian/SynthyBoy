// SynthyBoy - core: the effect types of the instance pool and the descriptor table (docs/ui-design/18-FX-INSTANCES.md, 13-new-keys.md 4.23).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The 16 modules of the FX chain ("instance 0": chorus.*, delay.*, ...) stay as they are. On top of them there is a pool of 16 generic
// slots fxi01 .. fxi16; a slot has a type (the choice fxiNN.type), on / rack / order and kFxInstanceParams normalised parameters
// fxiNN.p01 .. p24. What parameter k means is defined here: the k-th key of the type's own key list (table order, without `.on` and
// `.rack`). Range, curve, unit, default, labels and choice names are the ones of that source key (one source of truth: no second table of
// ranges). The UI, Kick and the engine all use this table.
#pragma once

#include <cstddef>

#include "params.h"

namespace synthyboy
{
/** Number of slots of the instance pool (fxi01 .. fxi16). */
constexpr int kNumFxInstances = 16;
/** Parameters per slot (fxiNN.p01 .. p24). The compressor has 24, the reverb 19 and the FX filter 20 keys, so 16 did not fit (decision P = 24). */
constexpr int kFxInstanceParams = 24;

/** The choice fxiNN.type: 0 = none, then the module types that have parameters (the splitters are not in the pool: 18-FX-INSTANCES.md 7).
    Type t (>= 1) is the module t - 1 of the chain of instance 0 (ReorderableEffectChain::Module order, fx.order choices 0 .. 12). */
enum FxInstanceType : int
{
    kFxTypeNone = 0,
    kFxTypeChorus,
    kFxTypeCompressor,
    kFxTypeDelay,
    kFxTypeDistortion,
    kFxTypeEq,
    kFxTypeFilter,
    kFxTypeFlanger,
    kFxTypePhaser,
    kFxTypeReverb,
    kFxTypeHyperDimension,
    kFxTypeUtility,
    kFxTypeBode,
    kFxTypeConvolve,
    kNumFxInstanceTypes // 14
};

/** The order key of a pool slot lives on the same scale as the positions of the 16 modules of instance 0: the module at position q of fx.order sorts as
    q * kFxOrderStep + kFxOrderBase (fxModuleSortKey, fx_rack.h), a slot by fxiNN.order (0 .. kFxOrderMax) directly. The items of one rack are sorted by this
    value, a module of instance 0 before a slot on a tie, slots by number. The base leaves room in front of the first module. */
constexpr int kFxOrderStep = 64;
constexpr int kFxOrderBase = 32;
constexpr int kFxOrderMax = 1023;

struct FxTypeDescriptor
{
    const char* name;      // "Chorus"
    const char* shortName; // "Chor"
    const char* prefix;    // "chorus": the key prefix of instance 0
    int module;            // module index of instance 0 (0 .. 12); also the factory id of the engine
    int count;             // parameters that are mapped (<= kFxInstanceParams)
    int unfit;             // keys of the type that did not fit into kFxInstanceParams (0 for every type with P = 24)
    int source[kFxInstanceParams]; // table index of the source key of parameter k, -1 beyond `count`
    int onIndex, rackIndex;        // table index of "<prefix>.on" and "<prefix>.rack"
};

/** Descriptor of a type 1 .. kNumFxInstanceTypes - 1 (type 0 and out-of-range: a descriptor with count 0 and name "None"). Built once on first use. */
const FxTypeDescriptor& fxTypeDescriptor (int type) noexcept;

/** The table indices of the keys of one slot. */
struct FxInstanceKeys
{
    int type, on, rack, order;
    int p[kFxInstanceParams];
};
/** Keys of slot 0 .. kNumFxInstances - 1 (all -1 for an out-of-range slot). */
const FxInstanceKeys& fxInstanceKeys (int slot) noexcept;

/** The slot and the parameter number (0-based; -1 for type / on / rack / order) of a table index, or false if the key is not one of a slot. */
bool fxInstanceOfParam (size_t paramIndex, int& slot, int& number) noexcept;

/** Source parameter (the ParamInfo whose range, curve and names apply) of parameter `number` of the slot's type; nullptr beyond the type's count. */
const ParamInfo* fxInstanceSource (int type, int number) noexcept;

/** The normalised value of a plain value of the source key, and back (the conversion the engine and the UI both use: ParamInfo semantics). */
float fxInstanceNormalise (int type, int number, float plain) noexcept;
float fxInstancePlain (int type, int number, float normalised) noexcept;

/** Writes the normalised defaults of a type (the source keys' defaults) into out[kFxInstanceParams]; unused entries 0. */
void fxInstanceDefaults (int type, float* out) noexcept;

/** Keys of state version 3 (the table grew by block B34 after it: the keys from this index on are new in state version 4). */
constexpr size_t kNumKeysV3 = 2631;

/** State version 3 -> 4 for the effect instance pool (REFCOMPARE round 3, question 129). The plain ranges of the EQ gains (+-15 -> +-24 dB), of the compressor
    thresholds (-80 .. 0 -> -120 .. +12 dB) and gains (+-30 -> +-36 dB) grew and `compressor.limit` got a third value, so a stored normalised value fxiNN.pKK of an
    EQ or compressor slot meant another plain value: it is rescaled here so that the document sounds as before. The keys that are new in version 4 (kNumKeysV3 on,
    the second EQ band and the EQ law) are written at their defaults when the document does not have them, and the parameters of an EQ slot that now map to them are
    written at the default (an old document stored whatever the slot held there): the old sound has no second band and the Vital EQ law.
    value / has / derived are indexed by table index (the arrays of at least paramCount () entries: the loader's own, state.cpp, or a rack preset's, library_items.cpp);
    `derived` is set for what the migration adds (not counted as taken from the document). Documents of version 4 and later must not go through it. */
void fxPoolMigrateFromV3 (double* value, bool* has, bool* derived) noexcept;

/** The type picker names: type t as in fxiNN.type (0 = "None"). */
const char* fxTypeName (int type) noexcept;
} // namespace synthyboy
