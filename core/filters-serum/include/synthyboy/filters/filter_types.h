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

} // namespace synthyboy::filters
