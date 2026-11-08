// SynthyBoy - core/filters-serum: Filter type definitions and metadata.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/filters/filter_types.h"

namespace synthyboy::filters
{
namespace
{

struct FilterMeta
{
    const char* name;
    const char* family;
    const char* varName;
};

constexpr FilterMeta kFilterMetadata[kFilterTypeCount] = {
    // Normal (18)
    { "MG Low 6", "Normal", "FAT" },
    { "MG Low 12", "Normal", "FAT" },
    { "MG Low 18", "Normal", "FAT" },
    { "MG Low 24", "Normal", "FAT" },
    { "Low 6", "Normal", "FAT" },
    { "Low 12", "Normal", "FAT" },
    { "Low 18", "Normal", "FAT" },
    { "Low 24", "Normal", "FAT" },
    { "High 6", "Normal", "FAT" },
    { "High 12", "Normal", "FAT" },
    { "High 18", "Normal", "FAT" },
    { "High 24", "Normal", "FAT" },
    { "Band 12", "Normal", "FAT" },
    { "Band 24", "Normal", "FAT" },
    { "Peak 12", "Normal", "FAT" },
    { "Peak 24", "Normal", "FAT" },
    { "Notch 12", "Normal", "FAT" },
    { "Notch 24", "Normal", "FAT" },

    // Multi (16)
    { "LH", "Multi", "FREQ" },
    { "LB", "Multi", "FREQ" },
    { "LP", "Multi", "FREQ" },
    { "LN", "Multi", "FREQ" },
    { "HB", "Multi", "FREQ" },
    { "HP", "Multi", "FREQ" },
    { "HN", "Multi", "FREQ" },
    { "BP", "Multi", "FREQ" },
    { "BN", "Multi", "FREQ" },
    { "PP", "Multi", "FREQ" },
    { "PN", "Multi", "FREQ" },
    { "NN", "Multi", "FREQ" },
    { "LBH", "Multi", "MORPH" },
    { "LPH", "Multi", "MORPH" },
    { "LNH", "Multi", "MORPH" },
    { "BPN", "Multi", "MORPH" },

    // Flanges (9)
    { "Cmb L", "Flanges", "LP FREQ" },
    { "Flg L", "Flanges", "LP FREQ" },
    { "Phs L", "Flanges", "LP FREQ" },
    { "Cmb H", "Flanges", "HP FREQ" },
    { "Flg H", "Flanges", "HP FREQ" },
    { "Phs H", "Flanges", "HP FREQ" },
    { "Cmb HL", "Flanges", "HL WID" },
    { "Flg HL", "Flanges", "HL WID" },
    { "Phs HL", "Flanges", "HL WID" },

    // Misc (26)
    { "Low EQ 6", "Misc", "DB" },
    { "Low EQ 12", "Misc", "DB" },
    { "Band EQ 6", "Misc", "DB" },
    { "Band EQ 12", "Misc", "DB" },
    { "High EQ 6", "Misc", "DB" },
    { "High EQ 12", "Misc", "DB" },
    { "Ring Mod", "Misc", "N/A" },
    { "Ring Mod x2", "Misc", "SPREAD" },
    { "SampHold", "Misc", "N/A" },
    { "SampHold-", "Misc", "N/A" },
    { "Combs", "Misc", "DAMP" },
    { "Allpasses", "Misc", "DAMP" },
    { "Reverb", "Misc", "DAMP" },
    { "French LP", "Misc", "BOEUF" },
    { "German LP", "Misc", "N/A" },
    { "Add Bass", "Misc", "THRU" },
    { "Formant-I", "Misc", "FORMNT" },
    { "Formant-II", "Misc", "FORMNT" },
    { "Formant-III", "Misc", "FORMNT" },
    { "Bandreject", "Misc", "WIDTH" },
    { "Dist.Comb 1 LP", "Misc", "COMBFRQ" },
    { "Dist.Comb 1 BP", "Misc", "COMBFRQ" },
    { "Dist.Comb 2 LP", "Misc", "COMBFRQ" },
    { "Dist.Comb 2 BP", "Misc", "COMBFRQ" },
    { "Scream LP", "Misc", "SCREAM" },
    { "Scream BP", "Misc", "SCREAM" },

    // New (11)
    { "New Wsp", "New", "MORPH" },
    { "DJ Mixer Xfer", "New", "N/A" },
    { "Diffusor", "New", "STAGES" },
    { "MG Ladder", "New", "SMOOTH" },
    { "Acid Ladder", "New", "SMOOTH" },
    { "EMS Ladder", "New", "SMOOTH" },
    { "MG Dirty", "New", "PAIN" },
    { "PZ SVF", "New", "SMOOTH" },
    { "Comb 2", "New", "FRQ2" },
    { "Exp MM", "New", "MIX" },
    { "Exp BPF", "New", "N/A" }
};

} // namespace

const char* getFilterTypeName (int type) noexcept
{
    if (type >= 0 && type < kFilterTypeCount)
        return kFilterMetadata[type].name;
    return "Unknown";
}

const char* getFilterFamilyName (int type) noexcept
{
    if (type >= 0 && type < kFilterTypeCount)
        return kFilterMetadata[type].family;
    return "Unknown";
}

const char* getFilterVarName (int type) noexcept
{
    if (type >= 0 && type < kFilterTypeCount)
        return kFilterMetadata[type].varName;
    return "N/A";
}

} // namespace synthyboy::filters
