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

struct ModelTypes {
  int count;
  FilterType types[12];
};

// docs/ui-design/13-new-keys.md 4.11 & 4.21: the model of the table (8 + index) -> the types of its styles
const ModelTypes kModels[19] = {
  { 12, { FilterType::LH, FilterType::LB, FilterType::LP, FilterType::LN, FilterType::HB, FilterType::HP, FilterType::HN, FilterType::BP,
          FilterType::BN, FilterType::PP, FilterType::PN, FilterType::NN } },                                          // 8  Multi SVF
  { 6, { FilterType::MGLow6, FilterType::MGLow18, FilterType::Low6, FilterType::Low18, FilterType::High6, FilterType::High18 } }, // 9  Slope
  { 3, { FilterType::LowEQ6, FilterType::BandEQ6, FilterType::HighEQ6 } },                                              // 10 EQ 6 dB
  { 2, { FilterType::RingMod, FilterType::RingModX2 } },                                                                // 11 Ring Mod
  { 2, { FilterType::SampHold, FilterType::SampHoldMinus } },                                                           // 12 Sample Hold
  { 3, { FilterType::Combs, FilterType::Allpasses, FilterType::Reverb } },                                              // 13 Smear
  { 1, { FilterType::FrenchLP } },                                                                                      // 14 French LP
  { 1, { FilterType::AddBass } },                                                                                       // 15 Add Bass
  { 2, { FilterType::ScreamLP, FilterType::ScreamBP } },                                                                // 16 Scream
  { 1, { FilterType::NewWsp } },                                                                                        // 17 New Wsp
  { 1, { FilterType::DJMixerXfer } },                                                                                   // 18 DJ Mixer
  { 1, { FilterType::Diffusor } },                                                                                      // 19 Diffusor
  { 1, { FilterType::EMSLadder } },                                                                                     // 20 EMS Ladder
  { 1, { FilterType::PZSVF } },                                                                                         // 21 PZ SVF
  { 12, { FilterType::MGLow12, FilterType::MGLow24, FilterType::Low12, FilterType::Low24, FilterType::High12, FilterType::High24,
          FilterType::Band12, FilterType::Band24, FilterType::Peak12, FilterType::Peak24, FilterType::Notch12, FilterType::Notch24 } }, // 22 Normal
  { 4, { FilterType::LBH, FilterType::LPH, FilterType::LNH, FilterType::BPN } },                                       // 23 Multi Morph
  { 9, { FilterType::CmbL, FilterType::FlgL, FilterType::PhsL, FilterType::CmbH, FilterType::FlgH, FilterType::PhsH,
         FilterType::CmbHL, FilterType::FlgHL, FilterType::PhsHL } },                                                  // 24 Flanges
  { 12, { FilterType::LowEQ12, FilterType::BandEQ12, FilterType::HighEQ12, FilterType::GermanLP,
          FilterType::FormantI, FilterType::FormantII, FilterType::FormantIII, FilterType::Bandreject,
          FilterType::DistComb1LP, FilterType::DistComb1BP, FilterType::DistComb2LP, FilterType::DistComb2BP } },       // 25 Misc
  { 6, { FilterType::MGLadder, FilterType::AcidLadder, FilterType::MGDirty, FilterType::Comb2, FilterType::ExpMM, FilterType::ExpBPF } }, // 26 New
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

int serumModelStyleCount (int model) noexcept
{
    return model >= kFirstSerumModel && model <= kLastSerumModel ? kModels[model - kFirstSerumModel].count : 0;
}

int serumFilterTypeOf (int model, int style) noexcept
{
    const int index = model < kFirstSerumModel ? 0 : (model > kLastSerumModel ? kLastSerumModel - kFirstSerumModel : model - kFirstSerumModel);
    const ModelTypes& m = kModels[index];
    const int s = style < 0 ? 0 : (style > m.count - 1 ? m.count - 1 : style);
    return static_cast<int> (m.types[s]);
}

} // namespace synthyboy::filters
