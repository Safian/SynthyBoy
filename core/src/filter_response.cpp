// SynthyBoy - core: the magnitude response of filter 1 / 2 of the own engine (see filter_response.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synthyboy/filter_response.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "synthyboy/params.h"
#include "synthyboy/wavetable_edit.h"
#include "voice/filter_setup.h"

namespace synthyboy
{
namespace
{
constexpr float kImpulse = 1.0e-3f;   // small signal: the nonlinear models stay linear
constexpr float kMinDb = -120.0f;
constexpr float kMaxDb = 60.0f;

size_t lengthFor (double sampleRate)
{
    size_t n = 8192;
    while (static_cast<double> (n) < sampleRate / 3.0 && n < 65536)
        n <<= 1;
    return n;
}
} // namespace

struct FilterResponse::Impl
{
    explicit Impl (double sr) : sampleRate (sr), n (lengthFor (sr)), inL (n, 0.0f), inR (n, 0.0f), outL (n, 0.0f), outR (n, 0.0f), re (n, 0.0f), im (n, 0.0f), power (n / 2 + 1, 0.0f)
    {
        unit.prepare (sr);
        inL[0] = kImpulse;
        inR[0] = kImpulse;
        // the taper of the last quarter of the response
        taper.resize (n / 4);
        for (size_t i = 0; i < taper.size(); ++i)
        {
            const double c = std::cos (0.5 * 3.14159265358979323846 * static_cast<double> (i) / static_cast<double> (taper.size()));
            taper[i] = static_cast<float> (c * c);
        }
    }

    /** Runs the filter on the impulse and adds |H|^2 of the channel to `power` (weight 1 or 0.5). */
    void measure (const own::FilterSettings& s, bool right, float weight) noexcept
    {
        unit.set (s);
        unit.reset();
        unit.set (s); // as a voice that starts with the note: the smoothers begin at the targets
        for (size_t t = 0; t < n; t += static_cast<size_t> (own::kControlPeriod))
        {
            unit.set (s); // every control period, as Voice::tick does
            unit.process (inL.data() + t, inR.data() + t, outL.data() + t, outR.data() + t, own::kControlPeriod, 0);
        }
        const float* out = right ? outR.data() : outL.data();
        const size_t tail = n - taper.size();
        for (size_t i = 0; i < n; ++i)
        {
            const float w = i >= tail ? taper[i - tail] : 1.0f;
            const float v = out[i] * w;
            re[i] = std::isfinite (v) ? v : 0.0f;
            im[i] = 0.0f;
        }
        wtedit::Radix2Fft::forward (re.data(), im.data(), n);
        const float norm = 1.0f / kImpulse;
        for (size_t k = 0; k <= n / 2; ++k)
        {
            const float a = re[k] * norm, b = im[k] * norm;
            power[k] += weight * (a * a + b * b);
        }
    }

    double sampleRate;
    size_t n;
    own::FilterUnit unit;
    std::vector<float> inL, inR, outL, outR, re, im, power, taper;
};

FilterResponse::FilterResponse (double sampleRate) : impl (new Impl (sampleRate)) {}
FilterResponse::~FilterResponse() = default;

double FilterResponse::sampleRate() const noexcept { return impl->sampleRate; }
int FilterResponse::impulseLength() const noexcept { return static_cast<int> (impl->n); }
double FilterResponse::binWidthHz() const noexcept { return impl->sampleRate / static_cast<double> (impl->n); }

void FilterResponse::readoutIndices (int filter, int out[2]) noexcept
{
    out[0] = out[1] = -1;
    if (filter < 1 || filter > 2)
        return;
    const own::FilterIdx& fi = own::paramIndex().filter[filter - 1];
    out[0] = fi.cutoff;
    out[1] = fi.resonance;
}

bool FilterResponse::compute (const ParamValues& values, const Request& r, float* dbOut, int numPoints) noexcept
{
    if (dbOut == nullptr || numPoints < 1)
        return false;
    std::fill (dbOut, dbOut + numPoints, 0.0f);
    if (r.filter < 1 || r.filter > 2 || !(r.fMin > 0.0f) || !(r.fMax >= r.fMin))
        return false;
    const own::FilterIdx& fi = own::paramIndex().filter[r.filter - 1];
    if (fi.on < 0 || fi.model < 0 || fi.style < 0 || fi.cutoff < 0 || fi.resonance < 0)
        return false;
    if (values.plain[fi.on] < 0.5f)
        return false;

    Impl& m = *impl;
    const float* v = values.plain;
    const bool hasCut = r.cutoffHz > 0.0f, hasRes = r.resonancePercent >= 0.0f;
    auto P = [&] (int i)
    {
        if (i == fi.cutoff && hasCut)
            return r.cutoffHz;
        if (i == fi.resonance && hasRes)
            return r.resonancePercent;
        return v[i];
    };
    own::FilterSettings s;
    own::fillFilterSettings (s, fi, P, P, fi.cleanMode >= 0 && v[fi.cleanMode] >= 0.5f, r.referenceNote, 0.0f, false);

    std::fill (m.power.begin(), m.power.end(), 0.0f);
    if (s.cutoffHzR != s.cutoffHz) // filter pan: the two channels differ, the display shows the power mean
    {
        m.measure (s, false, 0.5f);
        m.measure (s, true, 0.5f);
    }
    else
        m.measure (s, false, 1.0f);

    const float levelDb = r.includeLevel && fi.level >= 0 ? v[fi.level] : 0.0f;
    const double binHz = m.sampleRate / static_cast<double> (m.n);
    const double nyq = 0.5 * m.sampleRate;
    const double ratio = numPoints > 1 ? std::pow (static_cast<double> (r.fMax) / static_cast<double> (r.fMin), 1.0 / static_cast<double> (numPoints - 1)) : 1.0;
    double f = r.fMin;
    for (int i = 0; i < numPoints; ++i, f *= ratio)
    {
        const double pos = std::min (f, nyq) / binHz;
        size_t k = static_cast<size_t> (pos);
        if (k >= m.n / 2)
            k = m.n / 2 - 1;
        const float frac = static_cast<float> (std::min (pos - static_cast<double> (k), 1.0));
        const float mag = (1.0f - frac) * std::sqrt (m.power[k]) + frac * std::sqrt (m.power[k + 1]);
        float db = mag > 1.0e-9f ? 20.0f * std::log10 (mag) : kMinDb;
        db += levelDb;
        dbOut[i] = std::isfinite (db) ? std::clamp (db, kMinDb, kMaxDb) : 0.0f;
    }
    return true;
}
} // namespace synthyboy
