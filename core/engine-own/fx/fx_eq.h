// SynthyBoy - own engine: the equalizer with the two laws of document 06 section 5 (Vital: state variable shelves; Serum: the RBJ cookbook). Original code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "fx_dsp.h"

namespace synthyboy
{
namespace own
{
class Equalizer
{
public:
    /** mode: low 0 Shelf, 1 High pass; mid 0 Peak, 1 Notch; mid2 0 Off, 1 Peak, 2 Notch; high 0 Shelf, 1 Low pass. res is the knob 0 .. 1. */
    struct Band { int mode = 0; float hz = 1000.0f, gainDb = 0.0f, res = 0.3f; };
    struct Params { Band low, mid, mid2, high; int law = 0; };

    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept
    {
        for (auto& c : svf)
            for (auto& s : c)
                s = Sv();
        for (auto& c : bq)
            for (auto& b : c)
                b.reset();
        primed = false;
    }
    void set (const Params& np) noexcept
    {
        p = np;
        if (p.law == 0)
        {
            designSvf (0, p.low.mode == 1 ? kHighpass : kLowShelf, p.low);
            designSvf (1, p.mid.mode == 1 ? kNotch : kBell, p.mid);
            designSvf (2, p.mid2.mode == 2 ? kNotch : kBell, p.mid2);
            designSvf (3, p.high.mode == 1 ? kLowpass : kHighShelf, p.high);
        }
        else
        {
            for (int b = 0; b < 4; ++b)
                for (int k = 0; k < 5; ++k)
                    prevCo[b][k] = primed ? curCo[b][k] : 0.0f;
            designRbj (0, p.low.mode == 1 ? Biquad::highpass : Biquad::lowShelf, p.low, p.low.mode == 0);
            designRbj (1, p.mid.mode == 1 ? Biquad::notch : Biquad::peak, p.mid, false);
            designRbj (2, p.mid2.mode == 2 ? Biquad::notch : Biquad::peak, p.mid2, false);
            designRbj (3, p.high.mode == 1 ? Biquad::lowpass : Biquad::highShelf, p.high, p.high.mode == 0);
            if (!primed)
            {
                for (int b = 0; b < 4; ++b)
                    for (int k = 0; k < 5; ++k)
                        prevCo[b][k] = curCo[b][k];
                primed = true;
            }
        }
    }
    /** t0 / period: the position of the call in the control period (the coefficients of the Serum law are interpolated across it). */
    void process (float* l, float* r, int n, int t0, int period) noexcept
    {
        float* io[2] = { l, r };
        const bool mid2On = p.mid2.mode != 0;
        if (p.law == 0)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    float x = io[c][i];
                    x = svfProcess (svf[c][0], x);
                    x = svfProcess (svf[c][1], x);
                    if (mid2On)
                        x = svfProcess (svf[c][2], x);
                    io[c][i] = svfProcess (svf[c][3], x);
                }
            return;
        }
        const float invP = 1.0f / static_cast<float> (std::max (period, 1));
        for (int i = 0; i < n; ++i)
        {
            const float t = static_cast<float> (t0 + i + 1) * invP;
            for (int b = 0; b < 4; ++b)
            {
                if (b == 2 && !mid2On)
                    continue;
                float co[5];
                for (int k = 0; k < 5; ++k)
                    co[k] = prevCo[b][k] + (curCo[b][k] - prevCo[b][k]) * t;
                for (int c = 0; c < 2; ++c)
                {
                    Biquad& q = bq[c][b];
                    const float x = io[c][i];
                    const float y = co[0] * x + q.z1;
                    q.z1 = flushDenormal (co[1] * x - co[3] * y + q.z2);
                    q.z2 = flushDenormal (co[2] * x - co[4] * y);
                    io[c][i] = y;
                }
            }
        }
        if (t0 + n >= period)
            for (int b = 0; b < 4; ++b)
                for (int k = 0; k < 5; ++k)
                    prevCo[b][k] = curCo[b][k];
    }

private:
    enum Kind { kLowShelf, kHighShelf, kBell, kNotch, kHighpass, kLowpass };
    /** One section of the Digital state variable filter of document 05 section 2, in its simple form (no limiter, no drive compensation), with the output weights. */
    struct Sv
    {
        float g = 0.1f, R = 1.0f, wl = 1.0f, wb = 0.0f, wh = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
    };
    void designSvf (int band, Kind kind, const Band& b) noexcept
    {
        const float r = clamp01 (b.res);
        const float Q = 0.5f + 15.5f * r * r * r, R = 1.0f / Q;
        const float A = std::pow (10.0f, std::clamp (b.gainDb, -24.0f, 24.0f) * 0.05f);
        float hz = std::max (b.hz, 1.0f);
        float blend = 0.0f;
        float wl = 1.0f, wb = 0.0f, wh = 0.0f;
        switch (kind)
        {
            case kLowShelf: blend = -1.0f; break;
            case kHighShelf: blend = 1.0f; break;
            case kBell: blend = 0.0f; break;
            case kNotch: wl = 1.0f; wb = 0.0f; wh = 1.0f; break;
            case kHighpass: wl = 0.0f; wb = 0.0f; wh = 1.0f; break;
            case kLowpass: wl = 1.0f; wb = 0.0f; wh = 0.0f; break;
        }
        if (kind == kLowShelf || kind == kHighShelf || kind == kBell)
        {
            wl = lerp (A, 1.0f, std::clamp (blend + 1.0f, 0.0f, 1.0f));
            wh = lerp (1.0f, A, std::clamp (blend, 0.0f, 1.0f));
            wb = R * std::sqrt (A) * lerp (1.0f, std::sqrt (A), 1.0f - blend * blend);
            hz *= std::exp2 (3.0f * blend * std::log2 (A) / 12.0f);
        }
        Sv& s0 = svf[0][band];
        const float nyq = static_cast<float> (sampleRate);
        const float g = std::tan (kPif * std::min (hz / nyq, 0.499f));
        for (int c = 0; c < 2; ++c)
        {
            Sv& s = svf[c][band];
            s.g = g;
            s.R = R;
            s.wl = wl;
            s.wb = wb;
            s.wh = wh;
        }
        (void) s0;
    }
    static float svfProcess (Sv& s, float u) noexcept
    {
        const float v3 = u - s.ic2;
        const float v1 = (s.ic1 + s.g * v3) / (1.0f + s.g * s.R + s.g * s.g);
        const float v2 = s.ic2 + s.g * v1;
        s.ic1 = flushDenormal (2.0f * v1 - s.ic1);
        s.ic2 = flushDenormal (2.0f * v2 - s.ic2);
        const float hp = u - s.R * v1 - v2;
        return s.wh * hp + s.wb * v1 + s.wl * v2;
    }
    void designRbj (int band, Biquad::Type type, const Band& b, bool shelf) noexcept
    {
        const float r = clamp01 (b.res);
        const float q = shelf ? 0.4f + 1.7417f * r * r * r : 0.1f + 18.9053f * r * r * r * r;
        Biquad d;
        const float hz = std::clamp (b.hz, 10.0f, static_cast<float> (sampleRate) * 0.499f);
        d.design (type, hz, q, std::clamp (b.gainDb, -24.0f, 24.0f), sampleRate, 0.499f);
        curCo[band][0] = d.b0;
        curCo[band][1] = d.b1;
        curCo[band][2] = d.b2;
        curCo[band][3] = d.a1;
        curCo[band][4] = d.a2;
    }

    Params p;
    double sampleRate = 48000.0;
    Sv svf[2][4];
    Biquad bq[2][4];
    float prevCo[4][5] = {}, curCo[4][5] = {};
    bool primed = false;
};
} // namespace own
} // namespace synthyboy
