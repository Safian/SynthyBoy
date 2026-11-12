// SynthyBoy - own engine: spectral morph of a wavetable frame (docs/engine-spec/02-oscillator.md section 7; original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "wavetable_bank.h"

namespace synthyboy
{
namespace own
{
/** Spectral morph types in the order of oscN.morph.type. */
enum MorphType : int
{
    kMorphNone = 0, kMorphVocode, kMorphFormantScale, kMorphHarmonicStretch, kMorphInharmonicStretch, kMorphSmear, kMorphRandomAmplitudes,
    kMorphLowPass, kMorphHighPass, kMorphPhaseDisperse, kMorphShepard, kMorphTimeSkew
};

/** Work arrays of one morph rebuild (kept by the oscillator: no allocation while playing). */
struct MorphScratch
{
    float re[Wavetable::kCycle], im[Wavetable::kCycle];
    float c[Wavetable::kHarmonics + 2], s[Wavetable::kHarmonics + 2];
};

/** Builds one band limited cycle (2048 samples, with the guard samples: out[-1] .. out[2050] are written) of the table at the frame position `framePos`
    (0 .. frames - 1; the two neighbouring frames are mixed when `interpolate`), changed by the spectral morph `type` with the amount `m` (0 .. 1), keeping
    only the harmonics up to `maxHarmonic` (the band limit of the voice). `phaseInc` = the phase increment of the voice (cycles per sample) and `hostRate`
    are used by Vocode. Real time safe. */
void buildMorphCycle (const Wavetable& wt, float framePos, bool interpolate, int type, float m, int maxHarmonic, float phaseInc, double hostRate, float* out,
                      MorphScratch& scratch) noexcept;
} // namespace own
} // namespace synthyboy
