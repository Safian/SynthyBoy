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
// Classic filter style names (models 0 .. 7)
static constexpr const char* const kClassicAnalogStyles[6] = {
    "Style A (12 dB)", "Style B (24 dB)", "Style C (notch swap)",
    "Style D (dual notch)", "Style E (band peak notch)", "Style F (shelf)"
};
static constexpr const char* const kClassicDiodeStyles[2] = {
    "Style A (12 dB)", "Style B (24 dB)"
};
static constexpr const char* const kClassicFormantStyles[3] = {
    "A-O-I-E", "A-I-U-O", "Vocal Tract"
};
// the six styles of the Comb (docs/engine-spec/05-filters.md 8.1): feedback_style = style mod 3 (comb, positive flange, negative flange), filter_style = style div 3 (blend filter, band filter); style 0 is the plain comb of old
static constexpr const char* const kClassicCombStyles[6] = {
    "Comb", "Positive Flange", "Negative Flange", "Comb Band", "Positive Flange Band", "Negative Flange Band"
};
static constexpr const char* const kClassicPhaserStyles[2] = {
    "Positive Feedback", "Negative Feedback"
};

// Serum filter model style names (models 8 .. 26)
static constexpr const char* const kMultiSvfStyles[12] = {
    "LH", "LB", "LP", "LN", "HB", "HP", "HN", "BP", "BN", "PP", "PN", "NN"
};
static constexpr const char* const kSlopeStyles[6] = {
    "MG Low 6", "MG Low 18", "Low 6", "Low 18", "High 6", "High 18"
};
static constexpr const char* const kEq6Styles[3] = {
    "Low EQ 6", "Band EQ 6", "High EQ 6"
};
static constexpr const char* const kRingModStyles[2] = {
    "Ring Mod", "Ring Mod x2"
};
static constexpr const char* const kSampleHoldStyles[2] = {
    "SampHold", "SampHold-"
};
static constexpr const char* const kSmearStyles[3] = {
    "Combs", "Allpasses", "Reverb"
};
static constexpr const char* const kFrenchLpStyles[1] = {
    "French LP"
};
static constexpr const char* const kAddBassStyles[1] = {
    "Add Bass"
};
static constexpr const char* const kScreamStyles[2] = {
    "Scream LP", "Scream BP"
};
static constexpr const char* const kNewWspStyles[1] = {
    "New Wsp"
};
static constexpr const char* const kDjMixerStyles[1] = {
    "DJ Mixer Xfer"
};
static constexpr const char* const kDiffusorStyles[1] = {
    "Diffusor"
};
static constexpr const char* const kEmsLadderStyles[1] = {
    "EMS Ladder"
};
static constexpr const char* const kPzSvfStyles[1] = {
    "PZ SVF"
};
static constexpr const char* const kNormalStyles[12] = {
    "MG Low 12", "MG Low 24", "Low 12", "Low 24", "High 12", "High 24",
    "Band 12", "Band 24", "Peak 12", "Peak 24", "Notch 12", "Notch 24"
};
static constexpr const char* const kMultiMorphStyles[4] = {
    "LBH", "LPH", "LNH", "BPN"
};
static constexpr const char* const kFlangesStyles[9] = {
    "Cmb L", "Flg L", "Phs L", "Cmb H", "Flg H", "Phs H", "Cmb HL", "Flg HL", "Phs HL"
};
static constexpr const char* const kMiscStyles[12] = {
    "Low EQ 12", "Band EQ 12", "High EQ 12", "German LP",
    "Formant-I", "Formant-II", "Formant-III", "Bandreject",
    "Dist.Comb 1 LP", "Dist.Comb 1 BP", "Dist.Comb 2 LP", "Dist.Comb 2 BP"
};
static constexpr const char* const kNewStyles[6] = {
    "MG Ladder", "Acid Ladder", "MG Dirty", "Comb 2", "Exp MM", "Exp BPF"
};

static constexpr const char* const* kModelStyles[27] = {
    kClassicAnalogStyles,  // 0 Analog
    kClassicAnalogStyles,  // 1 Dirty
    kClassicAnalogStyles,  // 2 Ladder
    kClassicAnalogStyles,  // 3 Digital
    kClassicDiodeStyles,   // 4 Diode
    kClassicFormantStyles, // 5 Formant
    kClassicCombStyles,    // 6 Comb
    kClassicPhaserStyles,  // 7 Phaser
    kMultiSvfStyles,       // 8 Multi SVF
    kSlopeStyles,          // 9 Slope
    kEq6Styles,            // 10 EQ 6 dB
    kRingModStyles,        // 11 Ring Mod
    kSampleHoldStyles,     // 12 Sample Hold
    kSmearStyles,          // 13 Smear
    kFrenchLpStyles,       // 14 French LP
    kAddBassStyles,        // 15 Add Bass
    kScreamStyles,         // 16 Scream
    kNewWspStyles,         // 17 New Wsp
    kDjMixerStyles,        // 18 DJ Mixer
    kDiffusorStyles,       // 19 Diffusor
    kEmsLadderStyles,      // 20 EMS Ladder
    kPzSvfStyles,          // 21 PZ SVF
    kNormalStyles,         // 22 Normal
    kMultiMorphStyles,     // 23 Multi Morph
    kFlangesStyles,        // 24 Flanges
    kMiscStyles,           // 25 Misc
    kNewStyles             // 26 New
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

int filterModelStyleCount (int model) noexcept
{
    static constexpr int kClassic[kFirstSerumModel] = { 6, 6, 6, 6, 2, 3, 6, 2 }; // Analog, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser
    if (model >= 0 && model < kFirstSerumModel)
        return kClassic[model];
    return serumModelStyleCount (model);
}

const char* const* filterModelStyleNames (int model) noexcept
{
    if (model >= 0 && model <= kLastSerumModel)
        return kModelStyles[model];
    return nullptr;
}

const char* filterModelStyleName (int model, int style) noexcept
{
    const int count = filterModelStyleCount (model);
    if (model < 0 || model > kLastSerumModel || style < 0 || style >= count)
        return "";
    const char* const* names = filterModelStyleNames (model);
    return names != nullptr && names[style] != nullptr ? names[style] : "";
}

int serumFilterTypeOf (int model, int style) noexcept
{
    const int index = model < kFirstSerumModel ? 0 : (model > kLastSerumModel ? kLastSerumModel - kFirstSerumModel : model - kFirstSerumModel);
    const ModelTypes& m = kModels[index];
    const int s = style < 0 ? 0 : (style > m.count - 1 ? m.count - 1 : style);
    return static_cast<int> (m.types[s]);
}

} // namespace synthyboy::filters
