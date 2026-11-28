// SynthyBoy - MPC overlay: the PANEL PRESETS of the oscillator pages as plain DATA (owner decision 2026-11-27: the C++ layer has no UI elements): the slots of the parts, the grid cells and blocks of every full screen and detail
// page of an oscillator, and the Q groups (four slots each). Nothing here draws or handles a control.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace synthyboy_mpc { namespace overlay { namespace oscmodel {

/** The place of a part in a preset: the owner finds a child by it (`KnobPanel::part (slot)`). */
enum class PanelSlot : uint8_t { WtPos, Unison, Detune, Blend, Warp, Pan, Level, WarpMenu, Scan, Dens, Length, Offset, Dir, Pitch, Filter,
                                 Mode, Stack, Width, Range, WtSpread, Warp1Spread, Warp2Spread, Amount, AmountRand, Skew, SkewRand, Shape, Timbre, Start, Delay, Attack, Hold, Decay, Sustain, Release, Override,
                                 NoiseType, NoiseStart, NoiseRand, NoiseLoop, NoiseBounce,
                                 NoiseStereo, NoiseEnv1,
                                 SubCrs, SubEnv1, SubMono, SubPhase, SubContig, SubBlend,
                                 SampleScan, SpecPos, Span,
                                 Fin, NoisePitch, NoiseFine, NoiseKey,
                                 Count };
enum class PartKind : uint8_t { Dial, RandPair, Unison, WarpPair, Menu, Choice, Toggle, Number };   // Number: a whole-number field (FIN, the noise's PITCH and FINE)

/** One part of a preset: what, which slot, its label, and the cells it covers in the grid. */
struct PanelItem
{
    PartKind kind;
    PanelSlot slot;
    const char* label;
    uint8_t col, row, colSpan, rowSpan;
    bool bipolar;
    uint8_t block = 0;   // in a preset with blocks: the block the cells belong to (the cells are those of the block's inner grid)
};

/** A titled, bordered block of a preset (one Q group): its cells in the outer grid of the preset and its own inner grid for the items. */
struct PanelBlock
{
    const char* title;
    uint8_t col, row, colSpan, rowSpan;
    uint8_t innerCols, innerRows;
    uint8_t firstRowH = 0;   // > 0: the first row of the block's inner grid is this many pixels high (a row of steppers), the other rows share the rest
};

/** A preset: the grid, the column before which the separator stands (-1: none; the column after it is the separated one), and the parts. */
/** The four values of one Q group by slot (Count = an empty place). */
struct QSpec { PanelSlot slot[4]; };

struct PanelPreset
{
    const char* name;
    int cols, rows, separatorBefore;
    const PanelItem* items;
    int numItems;
    const QSpec* q = nullptr;   // the Q groups of the preset (a DetailPage activates them one at a time)
    int numQ = 0;
    int displayH = 0;           // a DetailPage: the height of the display area above the grid (0: none)
    const PanelBlock* blocks = nullptr;   // the full screen's bottom band: blocks side by side, each with a title and a thin outline (the grid of the preset is the grid of the blocks)
    int numBlocks = 0;
};

namespace presets
{
constexpr int kUnisonDisplayH = 170;   // the picture above the unison settings (the fan of the voices)
// DetailPage presets: titled blocks (one Q group each), the same rules as the full screen. The unison settings: the MODE and STACK steppers (a row of their own, 64 px, with their names) over the WIDTH and
// RANGE knobs, and the block of the three spreads over the voices (what WT POS and the two warps do from voice to voice). Knobs are one size on a page (a block's columns are as wide as the others').
constexpr PanelItem kUnisonDetailItems[] = {
    { PartKind::Choice, PanelSlot::Mode, "MODE", 0, 0, 1, 1, false, 0 },
    { PartKind::Choice, PanelSlot::Stack, "STACK", 1, 0, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Width, "WIDTH", 0, 1, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Range, "RANGE", 1, 1, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::WtSpread, "WT POS", 0, 0, 1, 1, false, 1 },
    { PartKind::Dial, PanelSlot::Warp1Spread, "WARP 1", 1, 0, 1, 1, false, 1 },
    { PartKind::Dial, PanelSlot::Warp2Spread, "WARP 2", 2, 0, 1, 1, false, 1 },
};
constexpr PanelBlock kUnisonDetailBlocks[] = { { "UNISON", 0, 0, 2, 1, 2, 2, 64 }, { "VOICE SPREAD", 2, 0, 3, 1, 3, 1 } };
constexpr QSpec kUnisonDetailQ[] = { { { PanelSlot::Mode, PanelSlot::Stack, PanelSlot::Width, PanelSlot::Range } },
                                     { { PanelSlot::WtSpread, PanelSlot::Warp1Spread, PanelSlot::Warp2Spread, PanelSlot::Count } } };
constexpr PanelItem kGrainDetailItems[] = {
    { PartKind::Choice, PanelSlot::Shape, "SHAPE", 0, 0, 4, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Amount, "AMOUNT", 0, 1, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::AmountRand, "RAND", 1, 1, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Skew, "SKEW", 2, 1, 1, 1, true, 0 },
    { PartKind::Dial, PanelSlot::SkewRand, "RAND", 3, 1, 1, 1, false, 0 },
};
constexpr PanelBlock kGrainDetailBlocks[] = { { "WINDOW", 0, 0, 1, 1, 4, 2, 64 } };
constexpr QSpec kGrainDetailQ[] = { { { PanelSlot::Amount, PanelSlot::AmountRand, PanelSlot::Skew, PanelSlot::SkewRand } },
                                    { { PanelSlot::Shape, PanelSlot::Count, PanelSlot::Count, PanelSlot::Count } } };
// The bottom band of the full screen of an oscillator (screen.h): the blocks UNISON (the unison box, DETUNE and FIN) and WARP at the right (wavetable: both warps; multisample, sample, spectral: warp 1 only, the screen says `setSingle`; WARP 2 and the PHASE knobs are behind MORE).
// The first Q group (Basic: TYPE, SOURCE, the position knob, OCT) is the top row (OscTopRow).
constexpr PanelBlock kWavetableBlocks[] = { { "UNISON", 0, 0, 1, 1, 3, 2 }, { "WARP", 1, 0, 1, 1, 1, 1 } };
constexpr PanelItem kWavetableFullItems[] = {
    { PartKind::Unison, PanelSlot::Unison, "", 0, 0, 1, 2, false, 0 },
    { PartKind::Dial, PanelSlot::Detune, "DETUNE", 1, 0, 1, 2, false, 0 },
    { PartKind::Number, PanelSlot::Fin, "FIN", 2, 0, 1, 1, false, 0 },
    { PartKind::WarpPair, PanelSlot::Warp, "", 0, 0, 1, 1, false, 1 },
};
constexpr QSpec kWavetableFullQ[] = { { { PanelSlot::Unison, PanelSlot::Detune, PanelSlot::Fin, PanelSlot::Count } } };
// the spectral type has no FIN in the default view (it is behind MORE): its block has two cells
constexpr PanelBlock kSpectralBlocks[] = { { "UNISON", 0, 0, 1, 1, 2, 2 }, { "WARP", 1, 0, 1, 1, 1, 1 } };
constexpr PanelItem kSpectralFullItems[] = {
    { PartKind::Unison, PanelSlot::Unison, "", 0, 0, 1, 2, false, 0 },
    { PartKind::Dial, PanelSlot::Detune, "DETUNE", 1, 0, 1, 2, false, 0 },
    { PartKind::WarpPair, PanelSlot::Warp, "", 0, 0, 1, 1, false, 1 },
};
constexpr QSpec kSpectralFullQ[] = { { { PanelSlot::Unison, PanelSlot::Detune, PanelSlot::Count, PanelSlot::Count } } };
// granular: GRAIN (DENS, LENGTH, WARP type) and PLAYBACK (OFFSET, DIR, PITCH, FIN); the first Q group is the top row (TYPE, SOURCE, SCAN, OCT)
constexpr PanelBlock kGranularBlocks[] = { { "GRAIN", 0, 0, 3, 1, 3, 1 }, { "PLAYBACK", 3, 0, 4, 1, 4, 1 } };
constexpr PanelItem kGranularFullItems[] = {
    { PartKind::Dial, PanelSlot::Dens, "DENS", 0, 0, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Length, "LENGTH", 1, 0, 1, 1, false, 0 },
    { PartKind::Menu, PanelSlot::WarpMenu, "WARP", 2, 0, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Offset, "OFFSET", 0, 0, 1, 1, false, 1 },
    { PartKind::Dial, PanelSlot::Dir, "DIR", 1, 0, 1, 1, false, 1 },
    { PartKind::Dial, PanelSlot::Pitch, "PITCH", 2, 0, 1, 1, true, 1 },
    { PartKind::Number, PanelSlot::Fin, "FIN", 3, 0, 1, 1, false, 1 },
};
constexpr QSpec kGranularFullQ[] = { { { PanelSlot::Dens, PanelSlot::Length, PanelSlot::WarpMenu, PanelSlot::Count } },
                                     { { PanelSlot::Offset, PanelSlot::Dir, PanelSlot::Pitch, PanelSlot::Fin } } };
// ---- THE DEFAULT VIEWS of the bold cut: the bottom band of the full screen of an oscillator instance. The top band (OscTopRow, screen.h) holds the first Q group (Basic); the blocks
// here are the other groups, in reading order. Level, pan and destination are not here (read-only indicator in the top band: their home is the Mixer); everything that is not in the default view is behind the ONE MORE button.
// Sub: tuning and behaviour below the waveform and octave selectors.
constexpr PanelItem kSubFullItems[] = {
    { PartKind::Dial, PanelSlot::SubCrs, "CRS", 0, 0, 1, 1, true, 0 },
    { PartKind::Dial, PanelSlot::SubPhase, "PHASE", 1, 0, 1, 1, false, 0 },
    { PartKind::Toggle, PanelSlot::SubEnv1, "ENV 1", 0, 0, 1, 1, false, 1 },
    { PartKind::Toggle, PanelSlot::SubMono, "MONO", 1, 0, 1, 1, false, 1 },
    { PartKind::Toggle, PanelSlot::SubContig, "FREE RUN", 2, 0, 1, 1, false, 1 },
};
constexpr PanelBlock kSubBlocks[] = { { "TUNE", 0, 0, 2, 1, 2, 1 }, { "BEHAVIOUR", 2, 0, 3, 1, 3, 1 } };
constexpr QSpec kSubFullQ[] = { { { PanelSlot::SubCrs, PanelSlot::SubPhase, PanelSlot::Count, PanelSlot::Count } },
    { { PanelSlot::SubEnv1, PanelSlot::SubMono, PanelSlot::SubContig, PanelSlot::Count } } };
constexpr PanelPreset kSubFull { "sub (full screen)", 5, 1, -1, kSubFullItems, 5, kSubFullQ, 2, 0, kSubBlocks, 2 };

// Noise: compact tuning and character groups; sample mode adds playback.
constexpr PanelItem kNoiseGenItems[] = {
    { PartKind::Number, PanelSlot::NoisePitch, "PITCH", 0, 0, 1, 1, false, 0 },
    { PartKind::Number, PanelSlot::NoiseFine, "FINE", 1, 0, 1, 1, false, 0 },
    { PartKind::Toggle, PanelSlot::NoiseKey, "KEY TRACK", 2, 0, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Filter, "FILTER", 0, 0, 1, 1, true, 1 },
    { PartKind::Dial, PanelSlot::NoiseStereo, "STEREO", 1, 0, 1, 1, false, 1 },
    { PartKind::Toggle, PanelSlot::NoiseEnv1, "ENV 1", 2, 0, 1, 1, false, 1 },
    { PartKind::Dial, PanelSlot::SubBlend, "BLEND", 3, 0, 1, 1, false, 1 },
};
constexpr PanelBlock kNoiseGenBlocks[] = { { "TUNE", 0, 0, 3, 1, 3, 1 }, { "CHARACTER", 3, 0, 4, 1, 4, 1 } };
constexpr QSpec kNoiseGenQ[] = { { { PanelSlot::NoisePitch, PanelSlot::NoiseFine, PanelSlot::NoiseKey, PanelSlot::Count } },
    { { PanelSlot::Filter, PanelSlot::NoiseStereo, PanelSlot::NoiseEnv1, PanelSlot::SubBlend } } };
constexpr PanelPreset kNoiseGenFull { "noise (full screen, generated)", 7, 1, -1, kNoiseGenItems, 7, kNoiseGenQ, 2, 0, kNoiseGenBlocks, 2 };
constexpr PanelItem kNoiseSampleItems[] = {
    kNoiseGenItems[0], kNoiseGenItems[1], kNoiseGenItems[2], kNoiseGenItems[3],
    kNoiseGenItems[4], kNoiseGenItems[5], kNoiseGenItems[6],
    { PartKind::Dial, PanelSlot::NoiseStart, "START", 0, 0, 1, 1, false, 2 },
    { PartKind::Toggle, PanelSlot::NoiseRand, "RANDOM", 1, 0, 1, 1, false, 2 },
    { PartKind::Toggle, PanelSlot::NoiseLoop, "LOOP", 2, 0, 1, 1, false, 2 },
    { PartKind::Toggle, PanelSlot::NoiseBounce, "BOUNCE", 3, 0, 1, 1, false, 2 },
};
constexpr PanelBlock kNoiseSampleBlocks[] = { { "TUNE", 0, 0, 3, 1, 3, 1 }, { "CHARACTER", 3, 0, 4, 1, 4, 1 }, { "PLAYBACK", 7, 0, 4, 1, 4, 1 } };
constexpr QSpec kNoiseSampleQ[] = { kNoiseGenQ[0], kNoiseGenQ[1],
    { { PanelSlot::NoiseStart, PanelSlot::NoiseRand, PanelSlot::NoiseLoop, PanelSlot::NoiseBounce } } };
constexpr PanelPreset kNoiseSampleFull { "noise (full screen, sample)", 11, 1, -1, kNoiseSampleItems, 11, kNoiseSampleQ, 3, 0, kNoiseSampleBlocks, 3 };

constexpr PanelPreset kWavetableFull { "wavetable (full screen)", 2, 1, -1, kWavetableFullItems, 4, kWavetableFullQ, 1, 0, kWavetableBlocks, 2 };
constexpr PanelPreset kGranularFull { "granular (full screen)", 7, 1, -1, kGranularFullItems, 7, kGranularFullQ, 2, 0, kGranularBlocks, 2 };
constexpr PanelPreset kMultisampleFull { "multisample (full screen)", 2, 1, -1, kWavetableFullItems, 4, kWavetableFullQ, 1, 0, kWavetableBlocks, 2 };
// the DetailPages of the unison settings of the three types, and the envelope of the multisample oscillator
constexpr PanelItem kUnisonMsItems[] = {
    { PartKind::Choice, PanelSlot::Mode, "MODE", 0, 0, 1, 1, false, 0 }, { PartKind::Choice, PanelSlot::Stack, "STACK", 1, 0, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Width, "WIDTH", 0, 1, 1, 1, false, 0 }, { PartKind::Dial, PanelSlot::Range, "RANGE", 1, 1, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Warp1Spread, "WARP 1", 0, 0, 1, 1, false, 1 }, { PartKind::Dial, PanelSlot::Warp2Spread, "WARP 2", 1, 0, 1, 1, false, 1 },
};
constexpr PanelBlock kUnisonMsBlocks[] = { { "UNISON", 0, 0, 2, 1, 2, 2, 64 }, { "VOICE SPREAD", 2, 0, 2, 1, 2, 1 } };
constexpr QSpec kUnisonMsQ[] = { { { PanelSlot::Mode, PanelSlot::Stack, PanelSlot::Width, PanelSlot::Range } }, { { PanelSlot::Warp1Spread, PanelSlot::Warp2Spread, PanelSlot::Count, PanelSlot::Count } } };
constexpr PanelItem kUnisonGrItems[] = {
    { PartKind::Choice, PanelSlot::Mode, "MODE", 0, 0, 1, 1, false, 0 }, { PartKind::Choice, PanelSlot::Stack, "STACK", 1, 0, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Width, "WIDTH", 0, 1, 1, 1, false, 0 }, { PartKind::Dial, PanelSlot::Range, "RANGE", 1, 1, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Start, "START", 0, 0, 1, 1, false, 1 },
    { PartKind::Dial, PanelSlot::Span, "SPAN", 1, 0, 1, 1, false, 1 },
};
constexpr PanelBlock kUnisonGrBlocks[] = { { "UNISON", 0, 0, 2, 1, 2, 2, 64 }, { "VOICE SPREAD", 2, 0, 2, 1, 2, 1 } };
constexpr QSpec kUnisonGrQ[] = { { { PanelSlot::Mode, PanelSlot::Stack, PanelSlot::Width, PanelSlot::Range } }, { { PanelSlot::Start, PanelSlot::Span, PanelSlot::Count, PanelSlot::Count } } };
constexpr PanelItem kEnvelopeItems[] = {
    { PartKind::Dial, PanelSlot::Delay, "DELAY", 0, 0, 1, 1, false, 0 }, { PartKind::Dial, PanelSlot::Attack, "ATTACK", 1, 0, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Hold, "HOLD", 2, 0, 1, 1, false, 0 }, { PartKind::Dial, PanelSlot::Decay, "DECAY", 3, 0, 1, 1, false, 0 },
    { PartKind::Dial, PanelSlot::Sustain, "SUSTAIN", 0, 0, 1, 1, false, 1 }, { PartKind::Dial, PanelSlot::Release, "RELEASE", 1, 0, 1, 1, false, 1 },
    { PartKind::Toggle, PanelSlot::Override, "OVERRIDE", 2, 0, 2, 1, false, 1 },
};
constexpr PanelBlock kEnvelopeBlocks[] = { { "TIMES", 0, 0, 1, 1, 4, 1 }, { "SUSTAIN AND RELEASE", 0, 1, 1, 1, 4, 1 } };
constexpr QSpec kEnvelopeQ[] = { { { PanelSlot::Delay, PanelSlot::Attack, PanelSlot::Hold, PanelSlot::Decay } }, { { PanelSlot::Sustain, PanelSlot::Release, PanelSlot::Override, PanelSlot::Count } } };
constexpr PanelPreset kUnisonDetailMultisample { "unison details (multisample)", 4, 1, -1, kUnisonMsItems, 6, kUnisonMsQ, 2, kUnisonDisplayH, kUnisonMsBlocks, 2 };
constexpr PanelPreset kUnisonDetailGranular { "unison details (granular)", 4, 1, -1, kUnisonGrItems, 6, kUnisonGrQ, 2, kUnisonDisplayH, kUnisonGrBlocks, 2 };
constexpr PanelPreset kSampleFull { "sample (full screen)", 2, 1, -1, kWavetableFullItems, 4, kWavetableFullQ, 1, 0, kWavetableBlocks, 2 };
constexpr PanelPreset kSpectralFull { "spectral (full screen)", 2, 1, -1, kSpectralFullItems, 3, kSpectralFullQ, 1, 0, kSpectralBlocks, 2 };
constexpr PanelPreset kEnvelopeDetail { "multisample envelope", 1, 2, -1, kEnvelopeItems, 7, kEnvelopeQ, 2, 140, kEnvelopeBlocks, 2 };
constexpr PanelPreset kUnisonDetail { "unison details", 5, 1, -1, kUnisonDetailItems, 7, kUnisonDetailQ, 2, kUnisonDisplayH, kUnisonDetailBlocks, 2 };
constexpr PanelPreset kGrainDetail { "grain window", 1, 1, -1, kGrainDetailItems, 5, kGrainDetailQ, 2, 140, kGrainDetailBlocks, 1 };
}

}}} // namespace synthyboy_mpc::overlay::oscmodel
