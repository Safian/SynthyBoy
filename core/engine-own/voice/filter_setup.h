// SynthyBoy - own engine: the FilterSettings of a filter from the values of its parameter group (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The one place that turns the parameters of filter 1 / 2 (as the voice hears them) into FilterSettings: Voice::tick and the display's
// FilterResponse (core/src/filter_response.cpp) both call it, so the curve is the one of the filter that sounds.
#pragma once

#include <cmath>

#include "filter_unit.h"
#include "param_index.h"

namespace synthyboy
{
namespace own
{
/** Fills `s` from the parameters of filter group `fi`. P (index) is the value of a parameter in its plain unit for the left lane, PR (index) for the right lane (they
    differ only where a modulation slot has the stereo flag); cleanMode is the (unmodulated) clean mode switch, keyNum the key of the note (key tracking is relative to 60),
    vcCutSemis the cutoff offset of the voice control, fast the quality tier low flag. */
template <class GetL, class GetR>
inline void fillFilterSettings (FilterSettings& s, const FilterIdx& fi, GetL&& P, GetR&& PR, bool cleanMode, int keyNum, float vcCutSemis, bool fast) noexcept
{
    s.fast = fast;
    s.model = static_cast<int> (P (fi.model) + 0.5f);
    s.style = static_cast<int> (P (fi.style) + 0.5f);
    const float kt = semitonesToRatio ((static_cast<float> (keyNum) - 60.0f) * P (fi.keytrack) * 0.01f);
    const float vcCut = semitonesToRatio (vcCutSemis);
    const float panShift = fi.pan >= 0 ? P (fi.pan) * 0.01f : 0.0f; // +-100 % = +-12 semitones, opposite in the two channels (document 05, 1.2)
    s.cutoffHz = P (fi.cutoff) * kt * vcCut * std::exp2 (-panShift);
    s.cutoffHzR = PR (fi.cutoff) * kt * vcCut * std::exp2 (panShift);
    s.clean = cleanMode;
    s.resonance = P (fi.resonance) * 0.01f;
    s.drive = P (fi.drive) * (1.0f / 20.0f);
    s.driveDb = P (fi.drive);
    s.combOffset = P (fi.combOffset);
    s.formantX = P (fi.formantX) * 0.01f;
    s.formantY = P (fi.formantY) * 0.01f;
    s.formantRes = P (fi.formantRes) * 0.01f;
    s.formantSpread = P (fi.formantSpread) * 0.01f;
    s.formantTranspose = P (fi.formantTranspose);
    s.blend = P (fi.blend) * 0.01f;
    s.mix = P (fi.mix) * 0.01f;
    s.var = P (fi.var) * 0.01f;
}
} // namespace own
} // namespace synthyboy
