// SynthyBoy - own engine: a stereo voice filter on top of the Serum filter collection (core/filters-serum).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include "../dsp/util.h"
#include "../dsp/simd.h"
#include "classic_filters.h"
#include "synthyboy/filters/filter.h"

namespace synthyboy
{
namespace own
{
struct FilterSettings
{
    int model = 3, style = 0;
    float cutoffHz = 12000.0f, resonance = 0.2f, drive = 0.0f, blend = 0.0f, mix = 1.0f, var = 0.5f;
    bool clean = false;
    bool noSmoothing = false;             // quality.no_smoothing: the Serum models' cutoff / resonance smoothing is skipped
    bool fast = false;                    // tier low: cheaper tanh in the classic models
    bool scalarOnly = false;              // measurements (setOwnSavingsOff): the two sides of a Serum model run one after the other also where a pair kernel exists
    float cutoffHzR = -1.0f, level = 1.0f, driveDb = 0.0f, combOffset = 42.0f, formantX = 0.5f, formantY = 0.5f, formantRes = 0.85f, formantSpread = 0.0f, formantTranspose = 0.0f;
};

class FilterUnit
{
public:
    void prepare (double sampleRate)
    {
        left.prepare (sampleRate);
        right.prepare (sampleRate);
        cl[0].prepare (sampleRate);
        cl[1].prepare (sampleRate);
    }

    void reset() noexcept
    {
        left.reset();
        right.reset();
        cl[0].reset();
        cl[1].reset();
        prevP = curP;
        prevPR = curPR;
    }

    /** The FilterType of a (model, style) pair; the classic models 0 .. 7 map onto the closest types of the collection. */
    static int typeFor (int model, int style, float blend, float& morph) noexcept
    {
        using filters::FilterType;
        morph = -1.0f;
        if (model >= filters::kFirstSerumModel)
            return filters::serumFilterTypeOf (model, style);
        auto morphing = [&] (FilterType plain12, FilterType plain24)
        {
            if (style == 1)
                return static_cast<int> (plain24);
            if (style <= 0 && blend < 0.01f)
                return static_cast<int> (plain12);
            morph = blend;
            switch (style)
            {
                case 2: return static_cast<int> (FilterType::LNH);
                case 3: return static_cast<int> (FilterType::LPH);
                case 4: return static_cast<int> (FilterType::BPN);
                default: return static_cast<int> (FilterType::LBH);
            }
        };
        switch (model)
        {
            case 0: return morphing (FilterType::MGLow12, FilterType::MGLow24);    // Analog
            case 1: return static_cast<int> (FilterType::MGDirty);                 // Dirty
            case 2: return static_cast<int> (style == 1 ? FilterType::MGLow24 : FilterType::MGLadder); // Ladder
            case 3: return morphing (FilterType::Low12, FilterType::Low24);        // Digital
            case 4: return static_cast<int> (FilterType::AcidLadder);              // Diode
            case 5: return static_cast<int> (style <= 0 ? FilterType::FormantI : (style == 1 ? FilterType::FormantII : FilterType::FormantIII));
            case 6: return static_cast<int> (style <= 0 ? FilterType::CmbL : (style == 1 ? FilterType::CmbH : FilterType::CmbHL));
            case 7: return static_cast<int> (style <= 0 ? FilterType::PhsL : (style == 1 ? FilterType::PhsH : FilterType::PhsHL));
            default: return static_cast<int> (FilterType::Low12);
        }
    }

    void set (const FilterSettings& s) noexcept
    {
        prevP = curP;
        prevPR = curPR;
        curP.model = s.model;
        curP.style = s.style;
        curP.cutoffHz = s.cutoffHz;
        curP.res = s.resonance;
        curP.driveDb = s.driveDb;
        curP.blendRaw = s.blend * 2.0f;
        curP.combOffset = s.combOffset;
        curP.fx = s.formantX;
        curP.fy = s.formantY;
        curP.fres = s.formantRes;
        curP.fspread = s.formantSpread;
        curP.ftranspose = s.formantTranspose;
        curP.fast = s.fast;
        curPR = curP;
        if (s.cutoffHzR > 0.0f)
            curPR.cutoffHz = s.cutoffHzR;
        classic = s.model < filters::kFirstSerumModel;
        scalarOnly = s.scalarOnly;
        cl[0].scalarLoops = cl[1].scalarLoops = s.scalarOnly;
        prevMix = curMix;
        curMix = clamp01 (s.mix);
        prevClean = curClean;
        curClean = (s.clean && s.model != 5 && s.model != 6) ? 1.0f : 0.0f; // not for the Formant and Comb models (no drive stage)
        if (classic)
            return;
        float morph = -1.0f;
        const int type = typeFor (s.model, s.style, s.blend, morph);
        if (type != left.getType())
        {
            left.setType (type);
            right.setType (type);
        }
        filters::FilterParams p;
        p.cutoff = std::clamp (s.cutoffHz, 20.0f, 20000.0f);
        p.resonance = clamp01 (s.resonance);
        p.drive = clamp01 (s.drive);
        p.var = clamp01 (morph >= 0.0f ? morph : s.var);
        p.blend = clamp01 (s.blend);
        p.mix = clamp01 (s.mix);
        p.jump = s.noSmoothing;
        p.coefHold = s.fast ? 4 : 1; // tier low: the coefficients follow the smoothed cutoff every fourth sample (the memo of the transcendental functions then hits)
        left.setParams (p);
        if (s.cutoffHzR > 0.0f)
            p.cutoff = std::clamp (s.cutoffHzR, 20.0f, 20000.0f);
        right.setParams (p);
        // loudness of the classic models matched to the reference engine's by measurement (tools/own-oracle), dB
        static const float kTrimDb[8] = { 1.5f, -11.5f, 3.5f, 0.0f, 4.0f, 2.0f, 0.5f, 1.0f };
        trim = 1.0f;
        (void) kTrimDb;
    }

    /** Filters a stereo block. Some of the collection's types can run away at extreme settings (resonance and drive at the maximum on loud input): a
        runaway (non finite, or beyond 200) resets the filter and plays silence for that block, everything else is limited to +-16 (+24 dB). */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, int t0 = 0) noexcept
    {
        if (classic)
        {
            // clean mode: the model sees its input at -24 dB and the +24 dB follow it (the drive saturates less), ramped across the block
            const float invP = 1.0f / static_cast<float> (kControlPeriod);
            float tmpL[kControlPeriod], tmpR[kControlPeriod];
            const bool cleaning = curClean > 0.5f || prevClean > 0.5f;
            const float* srcL = inL;
            const float* srcR = inR;
            if (cleaning)
            {
                for (int k = 0; k < n; ++k)
                {
                    const float c = lerp (prevClean, curClean, static_cast<float> (t0 + k + 1) * invP);
                    const float g = dbToGain (-24.0f * c);
                    tmpL[k] = inL[k] * g;
                    tmpR[k] = inR[k] * g;
                }
                srcL = tmpL;
                srcR = tmpR;
            }
            if (!ClassicFilter::processPair (cl[0], cl[1], srcL, outL, srcR, outR, n, prevP, curP, prevPR, curPR))
            {
                const bool sameLR = ClassicFilter::sameParams (prevP, prevPR) && ClassicFilter::sameParams (curP, curPR);
                if (sameLR && ClassicFilter::processRampPair (cl[0], cl[1], srcL, outL, srcR, outR, n, prevP, curP, t0))
                {
                    // done: the two channels ran in one loop with one setup
                }
                else if (sameLR && !scalarOnly)
                {
                    // both channels with the same parameters: one setup per piece for the two (scalarOnly keeps the separate passes below, for the measurements and the tests)
                    ClassicFilter::processTwin (cl[0], cl[1], srcL, outL, srcR, outR, n, prevP, curP, t0);
                }
                else
                {
                cl[0].process (srcL, outL, n, prevP, curP, t0);
                // both channels with the same parameters (the pan of the filter is 0): the right one finds the setup the left one just computed (tier low ramps)
                if (ClassicFilter::sameParams (prevP, prevPR) && ClassicFilter::sameParams (curP, curPR))
                    cl[1].adoptRamp (cl[0]);
                cl[1].process (srcR, outR, n, prevPR, curPR, t0);
                }
            }
            if (!cleaning && prevMix == 1.0f && curMix == 1.0f)
            {
                // no clean gain, no dry mix: the output is only limited (x * 1 = x; lerp (1, 1, t) = 1 so the mix branch is not taken)
                simd::clampTo (outL, -16.0f, 16.0f, n);
                simd::clampTo (outR, -16.0f, 16.0f, n);
                return;
            }
            for (int k = 0; k < n; ++k)
            {
                const float t = static_cast<float> (t0 + k + 1) * invP;
                const float c = cleaning ? dbToGain (24.0f * lerp (prevClean, curClean, t)) : 1.0f;
                const float m = lerp (prevMix, curMix, t);
                outL[k] = std::clamp (outL[k] * c, -16.0f, 16.0f);
                outR[k] = std::clamp (outR[k] * c, -16.0f, 16.0f);
                if (m < 1.0f)
                {
                    outL[k] = inL[k] + (outL[k] - inL[k]) * m;
                    outR[k] = inR[k] + (outR[k] - inR[k]) * m;
                }
            }
            return;
        }
        {
            const bool cleaning = curClean > 0.5f || prevClean > 0.5f;
            const float invP = 1.0f / static_cast<float> (kControlPeriod);
            float tmpL[kControlPeriod], tmpR[kControlPeriod];
            if (cleaning)
            {
                for (int k = 0; k < n; ++k)
                {
                    const float g = dbToGain (-24.0f * lerp (prevClean, curClean, static_cast<float> (t0 + k + 1) * invP));
                    tmpL[k] = inL[k] * g;
                    tmpR[k] = inR[k] * g;
                }
                if (scalarOnly || !filters::Filter::processPair (left, right, tmpL, outL, tmpR, outR, n))
                {
                    left.process (tmpL, outL, n);
                    right.process (tmpR, outR, n);
                }
                for (int k = 0; k < n; ++k)
                {
                    const float g = dbToGain (24.0f * lerp (prevClean, curClean, static_cast<float> (t0 + k + 1) * invP));
                    outL[k] *= g;
                    outR[k] *= g;
                }
            }
            else
            {
                // the two sides in one pass where the collection has a pair kernel for the type (the same samples), else one after the other
                if (scalarOnly || !filters::Filter::processPair (left, right, inL, outL, inR, outR, n))
                {
                    left.process (inL, outL, n);
                    right.process (inR, outR, n);
                }
            }
        }
        if (scalarOnly || trim != 1.0f)
        {
            // the loops as they were (measurements: setOwnSavingsOff, kOwnSavingVectorFilters), and the general form for a trim other than 1
            bool runaway = false;
            for (int k = 0; k < n; ++k)
                runaway = runaway || !(std::fabs (outL[k]) < 200.0f) || !(std::fabs (outR[k]) < 200.0f);
            if (runaway)
            {
                runaway_ (outL, outR, n);
                return;
            }
            for (int k = 0; k < n; ++k)
            {
                outL[k] = std::clamp (outL[k] * trim, -16.0f, 16.0f);
                outR[k] = std::clamp (outR[k] * trim, -16.0f, 16.0f);
            }
            return;
        }
        // the same in vector form (CPU review F6): the runaway test on the bit patterns (the decision of the loop above for every input), then the limit. After the
        // test every value is finite and below 200 in magnitude, where vmax / vmin give what std::clamp gives (signed zero included), and x * 1 is x.
        if (!simd::allAbsBelow (outL, outR, n, 200.0f))
        {
            runaway_ (outL, outR, n);
            return;
        }
        simd::clampTo (outL, -16.0f, 16.0f, n);
        simd::clampTo (outR, -16.0f, 16.0f, n);
    }

private:
    /** A runaway (a value not finite or beyond 200): the filters are reset and the block is silence. */
    void runaway_ (float* outL, float* outR, int n) noexcept
    {
        left.reset();
        right.reset();
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);
    }

    filters::Filter left, right;
    ClassicFilter cl[2];
    ClassicParams prevP, curP, prevPR, curPR;
    filters::FilterParams paramsR;
    bool classic = false, scalarOnly = false;
    float trim = 1.0f;
    float prevMix = 1.0f, curMix = 1.0f, prevClean = 0.0f, curClean = 0.0f;
};
} // namespace own
} // namespace synthyboy
