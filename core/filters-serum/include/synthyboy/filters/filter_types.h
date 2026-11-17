// SynthyBoy - core/filters-serum: Filter type definitions.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy::filters
{

enum class FilterType : int
{
    // Normal (18)
    MGLow6 = 0,
    MGLow12,
    MGLow18,
    MGLow24,
    Low6,
    Low12,
    Low18,
    Low24,
    High6,
    High12,
    High18,
    High24,
    Band12,
    Band24,
    Peak12,
    Peak24,
    Notch12,
    Notch24,

    // Multi (16)
    LH,
    LB,
    LP,
    LN,
    HB,
    HP,
    HN,
    BP,
    BN,
    PP,
    PN,
    NN,
    LBH,
    LPH,
    LNH,
    BPN,

    // Flanges (9)
    CmbL,
    FlgL,
    PhsL,
    CmbH,
    FlgH,
    PhsH,
    CmbHL,
    FlgHL,
    PhsHL,

    // Misc (26)
    LowEQ6,
    LowEQ12,
    BandEQ6,
    BandEQ12,
    HighEQ6,
    HighEQ12,
    RingMod,
    RingModX2,
    SampHold,
    SampHoldMinus,
    Combs,
    Allpasses,
    Reverb,
    FrenchLP,
    GermanLP,
    AddBass,
    FormantI,
    FormantII,
    FormantIII,
    Bandreject,
    DistComb1LP,
    DistComb1BP,
    DistComb2LP,
    DistComb2BP,
    ScreamLP,
    ScreamBP,

    // New (11)
    NewWsp,
    DJMixerXfer,
    Diffusor,
    MGLadder,
    AcidLadder,
    EMSLadder,
    MGDirty,
    PZSVF,
    Comb2,
    ExpMM,
    ExpBPF,

    Count
};

constexpr int kFilterTypeCount = static_cast<int> (FilterType::Count);

/** Returns the exact manual name of the filter type. */
const char* getFilterTypeName (int type) noexcept;

/** Returns the family name of the filter type (Normal, Multi, Flanges, Misc, New). */
const char* getFilterFamilyName (int type) noexcept;

/** Returns the name of the Var parameter for this filter type (FAT, FREQ, MORPH, etc.). */
const char* getFilterVarName (int type) noexcept;

/** First and last model of the filter model list (filterN.model) that is rendered by a Serum type: 8 .. 26 (0 .. 7 are the classic models). */
constexpr int kFirstSerumModel = 8;
constexpr int kLastSerumModel = 26;

/** Number of styles of a model (kFirstSerumModel .. kLastSerumModel); 0 for any other model. */
int serumModelStyleCount (int model) noexcept;

/** The number of styles that filterN.model (0 .. 26) really has: the style slots from this count up to the twelve of filterN.style repeat another style of the model (the
    classic models: Analog, Dirty, Ladder, Digital 6, Diode 2, Formant 3, Comb 6, Phaser 2; the Serum models: serumModelStyleCount ()), so a UI offers only 0 .. count - 1.
    0 for a model outside 0 .. 26. The table of what is implemented and what is open: docs/engine-spec/05-filters.md section 13. */
int filterModelStyleCount (int model) noexcept;

/** The names of the styles of a model (0 .. filterModelStyleCount(model) - 1).
    Returns an array of filterModelStyleCount(model) C-string pointers, or nullptr for a model outside 0 .. 26. */
const char* const* filterModelStyleNames (int model) noexcept;

/** The name of a style (0 .. filterModelStyleCount(model) - 1) of model (0 .. 26).
    Empty string "" for a model or style outside its valid range. */
const char* filterModelStyleName (int model, int style) noexcept;

/** The FilterType a (model, style) pair of the table selects (the one place of the mapping, used by the engine and the UI). A style beyond the
    model's last clamps to the last, a model outside 8 .. 26 clamps to the nearest; the result is always a valid FilterType index. */
int serumFilterTypeOf (int model, int style) noexcept;

} // namespace synthyboy::filters
