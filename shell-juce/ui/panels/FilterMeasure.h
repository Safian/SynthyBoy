// SynthyBoy - UI: the displayed response of the Serum filter types of models 22 .. 26 (WP-03 FILTERS80), measured from the real DSP.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

#include "synthyboy/filters/filter.h"
#include "synthyboy/filters/filter_types.h"

namespace synthyboy::ui
{

/**
 * The magnitude / phase of one Serum filter type at the current settings, taken from the same core/filters-serum code that renders the sound:
 * a small impulse goes through a private Filter instance and the response is the DFT of what comes out, on 257 log-spaced frequencies
 * (20 Hz .. 20 kHz). The result is cached, so a repaint only measures again when the model, style, cutoff, resonance, blend or var changed.
 * Owned by one view (no shared state); message thread only.
 */
class FilterResponseMeasure
{
public:
    static constexpr int kPoints = 257;
    static constexpr int kLength = 8192;
    static constexpr double kRate = 48000.0;

    struct Value
    {
        float magnitudeDb = 0.0f;
        float phaseRad = 0.0f;
    };

    /** True if the display of this model comes from the DSP (the new Serum models 22 .. 26). */
    static bool handles (int model) noexcept { return model >= 22 && model <= synthyboy::filters::kLastSerumModel; }

    /** Measures (if the settings changed since the last call) and returns the response at f Hz. Percent values are the plain 0 .. 100 of the keys. */
    Value at (float f, int model, int style, float cutoffHz, float resPercent, float blendPercent, float varPercent)
    {
        const Key key { model, style, cutoffHz, resPercent, blendPercent, varPercent };
        if (! valid || ! (key == last))
        {
            measure (key);
            last = key;
            valid = true;
        }
        const float norm = std::clamp (std::log10 (std::clamp (f, 20.0f, 20000.0f) / 20.0f) / 3.0f, 0.0f, 1.0f) * static_cast<float> (kPoints - 1);
        const int i0 = std::min (static_cast<int> (norm), kPoints - 2);
        const float t = norm - static_cast<float> (i0);
        Value v;
        v.magnitudeDb = (1.0f - t) * table[static_cast<size_t> (i0)].magnitudeDb + t * table[static_cast<size_t> (i0 + 1)].magnitudeDb;
        v.phaseRad = (1.0f - t) * table[static_cast<size_t> (i0)].phaseRad + t * table[static_cast<size_t> (i0 + 1)].phaseRad;
        return v;
    }

    /** The measured table itself (257 points), after at() was called. For tests. */
    const std::array<Value, kPoints>& points() const noexcept { return table; }
    int measureCount() const noexcept { return measures; }

private:
    struct Key
    {
        int model = 0, style = 0;
        float cutoff = 0.0f, res = 0.0f, blend = 0.0f, var = 0.0f;
        static bool same (float a, float b) noexcept { return ! (a < b) && ! (b < a); }
        bool operator== (const Key& o) const noexcept
        {
            return model == o.model && style == o.style && same (cutoff, o.cutoff) && same (res, o.res) && same (blend, o.blend) && same (var, o.var);
        }
    };

    void measure (const Key& key)
    {
        ++measures;
        if (filter == nullptr)
        {
            filter = std::make_unique<synthyboy::filters::Filter>();
            filter->prepare (kRate);
            impulse.assign (kLength, 0.0f);
            response.assign (kLength, 0.0f);
        }
        synthyboy::filters::FilterParams p;
        p.cutoff = std::clamp (key.cutoff, 20.0f, static_cast<float> (0.49 * kRate));
        p.resonance = std::clamp (key.res / 100.0f, 0.0f, 1.0f);
        p.blend = std::clamp (key.blend / 100.0f, 0.0f, 1.0f);
        p.var = std::clamp (key.var / 100.0f, 0.0f, 1.0f);
        p.drive = 0.0f;
        p.mix = 1.0f;
        filter->setType (synthyboy::filters::serumFilterTypeOf (key.model, key.style));
        filter->setParams (p);
        filter->reset();

        constexpr float kAmplitude = 0.01f; // small enough for the soft clippers to be linear
        std::fill (impulse.begin(), impulse.end(), 0.0f);
        impulse[0] = kAmplitude;
        filter->process (impulse.data(), response.data(), kLength, nullptr, nullptr);

        // the last quarter fades out, so a long ring (high resonance) does not leave a ripple from the truncation
        const int fadeStart = kLength - kLength / 4;
        for (int n = fadeStart; n < kLength; ++n)
        {
            const double x = static_cast<double> (n - fadeStart) / static_cast<double> (kLength - fadeStart);
            response[static_cast<size_t> (n)] *= static_cast<float> (0.5 + 0.5 * std::cos (3.14159265358979 * x));
        }
        for (int k = 0; k < kPoints; ++k)
        {
            const double hz = 20.0 * std::pow (1000.0, static_cast<double> (k) / static_cast<double> (kPoints - 1));
            const double w = 2.0 * 3.14159265358979 * hz / kRate;
            // Goertzel-free direct sum with a rotating phasor (8192 multiplies per point)
            std::complex<double> sum (0.0, 0.0), phasor (1.0, 0.0);
            const std::complex<double> step = std::polar (1.0, -w);
            for (int n = 0; n < kLength; ++n)
            {
                sum += static_cast<double> (response[static_cast<size_t> (n)]) * phasor;
                phasor *= step;
            }
            sum /= static_cast<double> (kAmplitude);
            const double mag = std::abs (sum);
            Value& v = table[static_cast<size_t> (k)];
            v.magnitudeDb = std::isfinite (mag) ? static_cast<float> (20.0 * std::log10 (std::max (1.0e-6, mag))) : -120.0f;
            v.phaseRad = std::isfinite (mag) ? static_cast<float> (std::arg (sum)) : 0.0f;
        }
    }

    std::unique_ptr<synthyboy::filters::Filter> filter;
    std::vector<float> impulse, response;
    std::array<Value, kPoints> table {};
    Key last;
    bool valid = false;
    int measures = 0;
};

} // namespace synthyboy::ui
