// SynthyBoy - own engine: the FilterSettings of a filter from the values of its parameter group (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The one place that turns the parameters of filter 1 / 2 (as the voice hears them) into FilterSettings: Voice::tick and the display's
// FilterResponse (core/src/filter_response.cpp) both call it, so the curve is the one of the filter that sounds.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "filter_unit.h"
#include "param_index.h"

namespace synthyboy
{
namespace own
{
/** The last results of the exp2 calls of fillFilterSettings () with the bit patterns of their arguments (a voice keeps one per filter): the key track, the voice control cutoff and the pan
    shift are the same period after period while the patch and the note stay, so the calls are made again only when an argument changed. An argument of exactly +-0 gives exp2 (+-0) = 1
    without a call. Each result is the function of its argument alone (the same libm in the same mode), so a hit returns the bits a call would. */
struct FilterSetupMemo
{
    uint32_t ktKey = 0, vcKey = 0, panKey = 0;   // +0.0f: the arguments the results below belong to (exp2 (0) = 1)
    float kt = 1.0f, vcCut = 1.0f, panMinus = 1.0f, panPlus = 1.0f;
};

namespace detail
{
/** semitonesToRatio (st) with the result kept under the bit pattern of st. */
inline float memoRatio (float st, uint32_t& key, float& value) noexcept
{
    uint32_t bits;
    std::memcpy (&bits, &st, sizeof bits);
    if ((bits << 1) == 0u) // +-0: exp2 (+-0 * 1/12) = 1
        return 1.0f;
    if (bits != key)
    {
        value = semitonesToRatio (st);
        key = bits;
    }
    return value;
}
} // namespace detail

/** Fills `s` from the parameters of filter group `fi`. P (index) is the value of a parameter in its plain unit for the left lane, PR (index) for the right lane (they
    differ only where a modulation slot has the stereo flag); cleanMode is the (unmodulated) clean mode switch, keyNum the key of the note (key tracking is relative to 60),
    vcCutSemis the cutoff offset of the voice control, fast the quality tier low flag; memo (null: every call is made) keeps the results of the exp2 calls of the voice between the periods. */
template <class GetL, class GetR>
inline void fillFilterSettings (FilterSettings& s, const FilterIdx& fi, GetL&& P, GetR&& PR, bool cleanMode, int keyNum, float vcCutSemis, bool fast, FilterSetupMemo* memo = nullptr) noexcept
{
    s.fast = fast;
    s.model = static_cast<int> (P (fi.model) + 0.5f);
    s.style = static_cast<int> (P (fi.style) + 0.5f);
    const float ktArg = (static_cast<float> (keyNum) - 60.0f) * P (fi.keytrack) * 0.01f;
    const float panShift = fi.pan >= 0 ? P (fi.pan) * 0.01f : 0.0f; // +-100 % = +-12 semitones, opposite in the two channels (document 05, 1.2)
    float kt, vcCut, panMinus, panPlus;
    if (memo != nullptr)
    {
        kt = detail::memoRatio (ktArg, memo->ktKey, memo->kt);
        vcCut = detail::memoRatio (vcCutSemis, memo->vcKey, memo->vcCut);
        uint32_t panBits;
        std::memcpy (&panBits, &panShift, sizeof panBits);
        if ((panBits << 1) == 0u) // exp2 (-0) = exp2 (+0) = 1
            panMinus = panPlus = 1.0f;
        else
        {
            if (panBits != memo->panKey)
            {
                memo->panMinus = std::exp2 (-panShift);
                memo->panPlus = std::exp2 (panShift);
                memo->panKey = panBits;
            }
            panMinus = memo->panMinus;
            panPlus = memo->panPlus;
        }
    }
    else
    {
        kt = semitonesToRatio (ktArg);
        vcCut = semitonesToRatio (vcCutSemis);
        panMinus = std::exp2 (-panShift);
        panPlus = std::exp2 (panShift);
    }
    s.cutoffHz = P (fi.cutoff) * kt * vcCut * panMinus;
    s.cutoffHzR = PR (fi.cutoff) * kt * vcCut * panPlus;
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
