// SynthyBoy - own engine: building blocks of the effects (delay line, state variable filter, biquad) (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "../dsp/util.h"

namespace synthyboy
{
namespace own
{
/** Circular delay line with linear interpolation (the length is a power of two, set in prepare). */
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int n = 16;
        while (n < maxSamples + 4)
            n <<= 1;
        buf.assign (static_cast<size_t> (n), 0.0f);
        mask = n - 1;
        pos = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    void push (float x) noexcept { pos = (pos + 1) & mask; buf[static_cast<size_t> (pos)] = x; }
    /** The sample written `d` samples ago (d >= 0, fractional). */
    float read (float d) const noexcept
    {
        const float dd = std::max (d, 0.0f);
        const int i = static_cast<int> (dd);
        const float f = dd - static_cast<float> (i);
        const float a = buf[static_cast<size_t> ((pos - i) & mask)], b = buf[static_cast<size_t> ((pos - i - 1) & mask)];
        return a + (b - a) * f;
    }
    /** Catmull-Rom (cubic) interpolation, `d` samples back from the newest sample (d >= 1 for the full four points). */
    float readCubic (float d) const noexcept
    {
        const float dd = std::max (d, 1.0f);
        const int i = static_cast<int> (dd);
        const float t = dd - static_cast<float> (i);
        const float y0 = buf[static_cast<size_t> ((pos - i + 1) & mask)], y1 = buf[static_cast<size_t> ((pos - i) & mask)];
        const float y2 = buf[static_cast<size_t> ((pos - i - 1) & mask)], y3 = buf[static_cast<size_t> ((pos - i - 2) & mask)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
    /** 4 point (cubic) Lagrange interpolation, `d` samples back from the newest sample (d >= 1). */
    float readLagrange4 (float d) const noexcept
    {
        const float dd = std::max (d, 1.0f);
        const int i = static_cast<int> (dd);
        const float t = dd - static_cast<float> (i);
        const float y0 = buf[static_cast<size_t> ((pos - i + 1) & mask)], y1 = buf[static_cast<size_t> ((pos - i) & mask)];
        const float y2 = buf[static_cast<size_t> ((pos - i - 1) & mask)], y3 = buf[static_cast<size_t> ((pos - i - 2) & mask)];
        // nodes at -1, 0, 1, 2 around y1
        const float c0 = -t * (t - 1.0f) * (t - 2.0f) * (1.0f / 6.0f), c1 = (t + 1.0f) * (t - 1.0f) * (t - 2.0f) * 0.5f,
                    c2 = -(t + 1.0f) * t * (t - 2.0f) * 0.5f, c3 = (t + 1.0f) * t * (t - 1.0f) * (1.0f / 6.0f);
        return c0 * y0 + c1 * y1 + c2 * y2 + c3 * y3;
    }
    /** 8 point Lagrange interpolation (the high quality mode of the delay). */
    float readLagrange8 (float d) const noexcept
    {
        const float dd = std::max (d, 4.0f);
        const int i = static_cast<int> (dd);
        const float t = dd - static_cast<float> (i);
        float acc = 0.0f;
        for (int k = -3; k <= 4; ++k)
        {
            float w = 1.0f;
            for (int m = -3; m <= 4; ++m)
                if (m != k)
                    w *= (t - static_cast<float> (m)) / static_cast<float> (k - m);
            acc += w * buf[static_cast<size_t> ((pos - i - k) & mask)];
        }
        return acc;
    }
    int capacity() const noexcept { return mask; }

private:
    std::vector<float> buf;
    int mask = 15, pos = 0;
};

/** Zero delay feedback state variable filter (Cytomic style). */
struct Svf
{
    float s1 = 0.0f, s2 = 0.0f;
    float lp = 0.0f, bp = 0.0f, hp = 0.0f;

    void reset() noexcept { s1 = s2 = lp = bp = hp = 0.0f; }
    void process (float x, float g, float k) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = flushDenormal (2.0f * v1 - s1);
        s2 = flushDenormal (2.0f * v2 - s2);
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
    static float coefficient (float hz, double sr) noexcept
    {
        return std::tan (kPif * std::clamp (hz, 10.0f, static_cast<float> (sr) * 0.45f) / static_cast<float> (sr));
    }
};

/** Trapezoidal (zero delay) one pole low pass. */
struct TrapLp
{
    float s = 0.0f;
    void reset() noexcept { s = 0.0f; }
    static float coefficient (float hz, double sr) noexcept
    {
        const float g = std::tan (kPif * std::clamp (hz, 5.0f, static_cast<float> (sr) * 0.45f) / static_cast<float> (sr));
        return g / (1.0f + g);
    }
    float process (float x, float a) noexcept
    {
        const float v = (x - s) * a;
        const float lp = v + s;
        s = flushDenormal (lp + v);
        return lp;
    }
};

/** One pole low pass. */
struct OnePole
{
    float y = 0.0f;
    void reset() noexcept { y = 0.0f; }
    float process (float x, float a) noexcept { y += a * (x - y); y = flushDenormal (y); return y; }
    static float coefficient (float hz, double sr) noexcept { return 1.0f - std::exp (-kTwoPif * std::min (hz, static_cast<float> (sr) * 0.45f) / static_cast<float> (sr)); }
};

/** RBJ biquad (direct form 2 transposed). */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0.0f; }
    void copyCoefficients (const Biquad& o) noexcept { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = flushDenormal (b1 * x - a1 * y + z2);
        z2 = flushDenormal (b2 * x - a2 * y);
        return y;
    }
    void set (float nb0, float nb1, float nb2, float na0, float na1, float na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    enum Type { lowpass, highpass, peak, notch, lowShelf, highShelf };
    void design (Type type, float hz, float q, float gainDb, double sr, float maxFrac = 0.45f) noexcept
    {
        const float w0 = kTwoPif * std::clamp (hz, 10.0f, static_cast<float> (sr) * maxFrac) / static_cast<float> (sr);
        const float cs = std::cos (w0), sn = std::sin (w0);
        const float alpha = sn / (2.0f * std::max (q, 0.05f));
        const float A = std::pow (10.0f, gainDb / 40.0f);
        switch (type)
        {
            case lowpass: set ((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + alpha, -2 * cs, 1 - alpha); break;
            case highpass: set ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + alpha, -2 * cs, 1 - alpha); break;
            case peak: set (1 + alpha * A, -2 * cs, 1 - alpha * A, 1 + alpha / A, -2 * cs, 1 - alpha / A); break;
            case notch: set (1, -2 * cs, 1, 1 + alpha, -2 * cs, 1 - alpha); break;
            case lowShelf:
            {
                const float sq = 2.0f * std::sqrt (A) * alpha;
                set (A * ((A + 1) - (A - 1) * cs + sq), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sq),
                     (A + 1) + (A - 1) * cs + sq, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sq);
                break;
            }
            case highShelf:
            {
                const float sq = 2.0f * std::sqrt (A) * alpha;
                set (A * ((A + 1) + (A - 1) * cs + sq), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sq),
                     (A + 1) - (A - 1) * cs + sq, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sq);
                break;
            }
        }
    }
};

/** Slews a value to its target over one control period (the effects change their parameters once per period). */
struct Ramp
{
    float cur = 0.0f, step = 0.0f;
    int left = 0;
    void snap (float v) noexcept { cur = v; step = 0.0f; left = 0; }
    void target (float v, int n) noexcept
    {
        if (n <= 0) { snap (v); return; }
        step = (v - cur) / static_cast<float> (n);
        left = n;
    }
    float next() noexcept
    {
        if (left > 0) { cur += step; --left; }
        return cur;
    }
};
} // namespace own
} // namespace synthyboy
