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
constexpr float kMinMag = 1.0e-6f;    // = kMinDb
constexpr size_t kProbe = 1024;       // samples of silence that decide whether the filter has an output offset
constexpr double kMinRate = 22050.0, kMaxRate = 192000.0; // the range filters::Filter::prepare runs in

double usableRate (double sr)
{
    return std::isfinite (sr) && sr > 0.0 ? std::clamp (sr, kMinRate, kMaxRate) : 48000.0;
}

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
    explicit Impl (double requested)
        : sampleRate (usableRate (requested)), n (lengthFor (sampleRate)), zeros (n, 0.0f), inL (n, 0.0f), inR (n, 0.0f), outL (n, 0.0f), outR (n, 0.0f), baseL (n, 0.0f), baseR (n, 0.0f),
          re (n, 0.0f), im (n, 0.0f), irL (n, 0.0f), irR (n, 0.0f), power (n / 2 + 1, 0.0f)
    {
        (void) own::paramIndex(); // built here, not inside the first compute ()
        unit.prepare (sampleRate);
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

    /** Runs the filter (state reset, settings as a voice that starts with the note applies them every control period) on `in` for `count` samples. */
    void run (const own::FilterSettings& s, const float* inLp, const float* inRp, float* oL, float* oR, size_t count) noexcept
    {
        unit.set (s);
        unit.reset();
        unit.set (s); // as a voice that starts with the note: the smoothers begin at the targets
        for (size_t t = 0; t < count; t += static_cast<size_t> (own::kControlPeriod))
        {
            unit.set (s); // every control period, as Voice::tick does
            unit.process (inLp + t, inRp + t, oL + t, oR + t, own::kControlPeriod, 0);
        }
    }

    /** The response to silence for as long as it still changes: an output offset settles within a few filter time constants, and what follows is held (this keeps
        a model with an offset at little more than the cost of one run). */
    void runSilenceUntilSettled (const own::FilterSettings& s) noexcept
    {
        const size_t period = static_cast<size_t> (own::kControlPeriod);
        unit.set (s);
        unit.reset();
        unit.set (s);
        size_t t = 0;
        for (; t < n; t += period)
        {
            unit.set (s);
            unit.process (zeros.data() + t, zeros.data() + t, baseL.data() + t, baseR.data() + t, own::kControlPeriod, 0);
            if (t >= 4 * period)
            {
                float change = 0.0f;
                for (size_t i = t; i < t + period; ++i)
                    change = std::max ({ change, std::fabs (baseL[i] - baseL[i - period]), std::fabs (baseR[i] - baseR[i - period]) });
                if (change <= 1.0e-8f)
                {
                    t += period;
                    break;
                }
            }
        }
        for (size_t i = t; i < n; ++i) // settled: hold the last value
        {
            baseL[i] = baseL[t - 1];
            baseR[i] = baseR[t - 1];
        }
    }

    /** The impulse response of both channels (the response to silence subtracted when the filter has an output offset), windowed, into irL / irR. */
    void measureImpulseResponse (const own::FilterSettings& s) noexcept
    {
        run (s, zeros.data(), zeros.data(), baseL.data(), baseR.data(), kProbe);
        float offset = 0.0f;
        for (size_t i = 0; i < kProbe; ++i)
            offset = std::max ({ offset, std::fabs (baseL[i]), std::fabs (baseR[i]) });
        const bool hasOffset = offset > kImpulse * 1.0e-3f;
        if (hasOffset)
            runSilenceUntilSettled (s);
        run (s, inL.data(), inR.data(), outL.data(), outR.data(), n);
        const size_t tail = n - taper.size();
        for (size_t i = 0; i < n; ++i)
        {
            const float w = i >= tail ? taper[i - tail] : 1.0f;
            const float vl = (outL[i] - (hasOffset ? baseL[i] : 0.0f)) * w;
            const float vr = (outR[i] - (hasOffset ? baseR[i] : 0.0f)) * w;
            irL[i] = std::isfinite (vl) ? vl : 0.0f;
            irR[i] = std::isfinite (vr) ? vr : 0.0f;
        }
    }

    /** Adds weight * |H|^2 of the impulse response `ir` to `power` (the bins 0 .. n / 2). */
    void addPower (const std::vector<float>& ir, float weight) noexcept
    {
        std::copy (ir.begin(), ir.end(), re.begin());
        std::fill (im.begin(), im.end(), 0.0f);
        wtedit::Radix2Fft::forward (re.data(), im.data(), n);
        const float norm = 1.0f / kImpulse;
        for (size_t k = 0; k <= n / 2; ++k)
        {
            const float a = re[k] * norm, b = im[k] * norm;
            power[k] += weight * (a * a + b * b);
        }
    }

    /** |H(f)|^2 of `ir` at exactly f (a DFT by a rotating phasor), for frequencies below the first bin. */
    double directPower (const std::vector<float>& ir, double f) const noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * f / sampleRate;
        const double cs = std::cos (w), sn = std::sin (w);
        double cr = 1.0, ci = 0.0, sr = 0.0, si = 0.0;
        for (size_t t = 0; t < n; ++t)
        {
            const double x = static_cast<double> (ir[t]);
            sr += x * cr;
            si -= x * ci;
            const double nr = cr * cs - ci * sn;
            ci = cr * sn + ci * cs;
            cr = nr;
        }
        const double norm = 1.0 / static_cast<double> (kImpulse);
        return (sr * sr + si * si) * norm * norm;
    }

    double sampleRate;
    size_t n;
    own::FilterUnit unit;
    std::vector<float> zeros, inL, inR, outL, outR, baseL, baseR, re, im, irL, irR, power, taper;
};

FilterResponse::FilterResponse (double sampleRate) : impl (new Impl (sampleRate)) {}
FilterResponse::~FilterResponse() = default;

double FilterResponse::sampleRate() const noexcept { return impl->sampleRate; }
int FilterResponse::impulseLength() const noexcept { return static_cast<int> (impl->n); }
double FilterResponse::binWidthHz() const noexcept { return impl->sampleRate / static_cast<double> (impl->n); }

void FilterResponse::readoutIndices (int filter, int out[2]) noexcept
{
    if (out == nullptr)
        return;
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
    if (r.filter < 1 || r.filter > 2 || !std::isfinite (r.fMin) || !std::isfinite (r.fMax) || !(r.fMin > 0.0f) || !(r.fMax >= r.fMin))
        return false;
    if ((r.cutoffHz && !(std::isfinite (*r.cutoffHz) && *r.cutoffHz > 0.0f)) || (r.resonancePercent && !std::isfinite (*r.resonancePercent)))
        return false;
    const own::FilterIdx& fi = own::paramIndex().filter[r.filter - 1];
    if (fi.on < 0 || fi.model < 0 || fi.style < 0 || fi.cutoff < 0 || fi.resonance < 0)
        return false;
    const float* v = values.plain;
    // every parameter of the group must be a number (a NaN would reach integer casts and delay line positions)
    for (const int idx : { fi.on, fi.model, fi.style, fi.cutoff, fi.resonance, fi.drive, fi.blend, fi.mix, fi.keytrack, fi.var, fi.pan, fi.combOffset, fi.formantX, fi.formantY, fi.formantRes,
                           fi.formantSpread, fi.formantTranspose, fi.cleanMode, r.includeLevel ? fi.level : -1 })
        if (idx >= 0 && !std::isfinite (v[idx]))
            return false;
    if (!(v[fi.on] >= 0.5f))
        return false;

    Impl& m = *impl;
    auto P = [&] (int i)
    {
        if (i < 0)
            return 0.0f;
        if (i == fi.cutoff && r.cutoffHz)
            return std::min (*r.cutoffHz, 1.0e6f);
        if (i == fi.resonance && r.resonancePercent)
            return std::clamp (*r.resonancePercent, -1.0e4f, 1.0e4f);
        const ParamInfo& info = paramTable()[static_cast<size_t> (i)];
        return std::clamp (v[i], info.min, info.max);
    };
    own::FilterSettings s;
    own::fillFilterSettings (s, fi, P, P, fi.cleanMode >= 0 && v[fi.cleanMode] >= 0.5f, std::clamp (r.referenceNote, 0, 127), 0.0f, false);

    // one run of the filter gives both channels; filter pan makes them differ: the display shows the power mean of the two
    const bool stereo = s.cutoffHzR != s.cutoffHz;
    const float wL = stereo ? 0.5f : 1.0f;
    m.measureImpulseResponse (s);
    std::fill (m.power.begin(), m.power.end(), 0.0f);
    m.addPower (m.irL, wL);
    if (stereo)
        m.addPower (m.irR, 0.5f);

    const float levelDb = r.includeLevel && fi.level >= 0 ? v[fi.level] : 0.0f;
    const double binHz = m.sampleRate / static_cast<double> (m.n);
    const double nyq = 0.5 * m.sampleRate;
    const double ratio = numPoints > 1 ? std::pow (static_cast<double> (r.fMax) / static_cast<double> (r.fMin), 1.0 / static_cast<double> (numPoints - 1)) : 1.0;
    double f = r.fMin;
    for (int i = 0; i < numPoints; ++i, f *= ratio)
    {
        const double fc = std::min (f, nyq);
        const double pos = fc / binHz;
        float mag;
        if (pos < 1.0)
        {
            // below the first bin: the response at exactly f, not an interpolation towards the DC bin
            double p = wL * m.directPower (m.irL, fc);
            if (stereo)
                p += 0.5 * m.directPower (m.irR, fc);
            mag = static_cast<float> (std::sqrt (p));
        }
        else
        {
            size_t k = static_cast<size_t> (pos);
            if (k >= m.n / 2)
                k = m.n / 2 - 1;
            const float frac = static_cast<float> (std::min (pos - static_cast<double> (k), 1.0));
            mag = (1.0f - frac) * std::sqrt (m.power[k]) + frac * std::sqrt (m.power[k + 1]);
        }
        const float db = 20.0f * std::log10 (std::max (mag, kMinMag)) + levelDb; // the floor first, then the level: a silent bin is not above a quiet one
        dbOut[i] = std::isfinite (db) ? std::clamp (db, kMinDb, kMaxDb) : 0.0f;
    }
    return true;
}
} // namespace synthyboy
