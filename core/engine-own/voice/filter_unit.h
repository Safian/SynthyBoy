// SynthyBoy - own engine: a stereo voice filter on top of the Serum filter collection (core/filters-serum).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../dsp/util.h"
#include "synthyboy/filters/filter.h"

namespace synthyboy
{
namespace own
{
struct FilterSettings
{
    int model = 3, style = 0;
    float cutoffHz = 12000.0f, resonance = 0.2f, drive = 0.0f, blend = 0.0f, mix = 1.0f, var = 0.5f;
};

class FilterUnit
{
public:
    void prepare (double sampleRate)
    {
        left.prepare (sampleRate);
        right.prepare (sampleRate);
    }

    void reset() noexcept
    {
        left.reset();
        right.reset();
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
        left.setParams (p);
        right.setParams (p);
    }

    /** Filters a stereo block. Some of the collection's types can run away at extreme settings (resonance and drive at the maximum on loud input): a
        runaway (non finite, or beyond 200) resets the filter and plays silence for that block, everything else is limited to +-16 (+24 dB). */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        left.process (inL, outL, n);
        right.process (inR, outR, n);
        bool runaway = false;
        for (int k = 0; k < n; ++k)
            runaway = runaway || !(std::fabs (outL[k]) < 200.0f) || !(std::fabs (outR[k]) < 200.0f);
        if (runaway)
        {
            left.reset();
            right.reset();
            std::fill (outL, outL + n, 0.0f);
            std::fill (outR, outR + n, 0.0f);
            return;
        }
        for (int k = 0; k < n; ++k)
        {
            outL[k] = std::clamp (outL[k], -16.0f, 16.0f);
            outR[k] = std::clamp (outR[k], -16.0f, 16.0f);
        }
    }

private:
    filters::Filter left, right;
};
} // namespace own
} // namespace synthyboy
