// SynthyBoy - core/filters-serum: DSP building blocks.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace synthyboy::filters
{

#ifndef M_PI
constexpr float kPi = 3.14159265358979323846f;
#else
constexpr float kPi = static_cast<float> (M_PI);
#endif

inline float sanitize (float x) noexcept
{
    if (std::isnan (x) || std::isinf (x))
        return 0.0f;
    if (std::abs (x) < 1e-18f)
        return 0.0f;
    return x;
}

inline float softClip (float x) noexcept
{
    // Fast polynomial tanh approximation for small inputs, clamp at extremes
    if (x <= -3.0f)
        return -1.0f;
    if (x >= 3.0f)
        return 1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** TPT 1-pole stage (trapezoidal integration). */
struct OnePoleTPT
{
    float s = 0.0f;

    void reset() noexcept { s = 0.0f; }

    void processLP (float in, float g, float& lp, float& hp) noexcept
    {
        s = sanitize (s);
        const float G = g / (1.0f + g);
        const float v = G * (in - s);
        lp = v + s;
        s = sanitize (v + lp);
        hp = in - lp;
    }

    float processLPOnly (float in, float g) noexcept
    {
        float lp = 0.0f;
        float hp = 0.0f;
        processLP (in, g, lp, hp);
        return lp;
    }

    float processHPOnly (float in, float g) noexcept
    {
        float lp = 0.0f;
        float hp = 0.0f;
        processLP (in, g, lp, hp);
        return hp;
    }
};

/** TPT 2-pole State-Variable Filter (SVF) with feedback saturation (FAT). */
struct SVFTPT
{
    float s1 = 0.0f;
    float s2 = 0.0f;

    void reset() noexcept
    {
        s1 = 0.0f;
        s2 = 0.0f;
    }

    void process (float in, float g, float R, float fat,
                  float& lp, float& bp, float& hp, float& notch, float& peak) noexcept
    {
        s1 = sanitize (s1);
        s2 = sanitize (s2);

        // Clamping g to avoid extreme numerical issues near Nyquist
        const float gSafe = std::max (1e-5f, std::min (g, 50.0f));
        const float d = 1.0f / (1.0f + 2.0f * R * gSafe + gSafe * gSafe);
        // FAT: the damping grows with the band-pass amplitude (a dissipative nonlinearity in the feedback path), so the resonance saturates instead
        // of growing; the earlier version clipped the integrator input, which broke the trapezoidal relation and ran away at high resonance
        const float damping = 2.0f * R * (1.0f + (fat > 0.001f ? std::min (4.0f * fat * s1 * s1, 30.0f) : 0.0f));
        const float dEff = 1.0f / (1.0f + damping * gSafe + gSafe * gSafe); // the same linear filter with the momentary damping: stable for any damping > 0
        const float hp_val = (in - (damping + gSafe) * s1 - s2) * dEff;
        (void) d;
        const float v1 = gSafe * hp_val;
        float bp_val = v1 + s1;
        s1 = sanitize (v1 + bp_val);

        float v2 = gSafe * bp_val;
        float lp_val = v2 + s2;
        s2 = sanitize (v2 + lp_val);

        lp = lp_val;
        bp = bp_val;
        hp = hp_val;
        notch = hp_val + lp_val;
        peak = lp_val - hp_val;
    }
};

/** Moog 4-pole transistor ladder model. */
struct Ladder4Pole
{
    float s[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    void reset() noexcept
    {
        for (int i = 0; i < 4; ++i)
            s[i] = 0.0f;
    }

    void process (float in, float g, float res, float fat, float drive,
                  float& y1, float& y2, float& y3, float& y4) noexcept
    {
        for (int i = 0; i < 4; ++i)
            s[i] = sanitize (s[i]);

        const float gSafe = std::max (1e-5f, std::min (g, 0.95f));
        const float G = gSafe / (1.0f + gSafe);
        const float G0 = 1.0f / (1.0f + gSafe);
        const float G2 = G * G;
        const float G3 = G2 * G;
        const float G4 = G3 * G;

        // Exact ZDF feedback prediction
        const float S = G0 * (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]);

        // Feedback coefficient: k safely below runaway threshold (k < 4.0)
        const float k = 3.85f * res * (1.0f - 0.05f * res);
        const float driveGain = 1.0f + 2.0f * drive;

        // Feedback saturation (FAT tames large resonance excursions without inflating small-signal gain)
        float fb = softClip (S);
        if (fat > 0.001f)
            fb = fb / (1.0f + 2.0f * fat * (S * S));

        const float u = softClip ((driveGain * in - k * fb) / (1.0f + k * G4));

        // Stage 1
        float v0 = G * (u - s[0]);
        y1 = v0 + s[0];
        s[0] = sanitize (v0 + y1);

        // Stage 2
        float v1 = G * (y1 - s[1]);
        y2 = v1 + s[1];
        s[1] = sanitize (v1 + y2);

        // Stage 3
        float v2 = G * (y2 - s[2]);
        y3 = v2 + s[2];
        s[2] = sanitize (v2 + y3);

        // Stage 4
        float v3 = G * (y3 - s[3]);
        y4 = v3 + s[3];
        s[3] = sanitize (v3 + y4);
    }
};

/** TB-303 diode ladder model with highpass in resonance feedback. */
struct DiodeLadder
{
    float s[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float hpState = 0.0f;

    void reset() noexcept
    {
        for (int i = 0; i < 4; ++i)
            s[i] = 0.0f;
        hpState = 0.0f;
    }

    float process (float in, float g, float res, float drive, float sampleRate) noexcept
    {
        for (int i = 0; i < 4; ++i)
            s[i] = sanitize (s[i]);
        hpState = sanitize (hpState);

        const float gSafe = std::max (1e-5f, std::min (g, 0.95f));
        const float G = gSafe / (1.0f + gSafe);
        const float k = 3.82f * res;

        // 303 feedback high-pass around 30 Hz
        const float gHp = std::tan (kPi * 30.0f / static_cast<float> (sampleRate));
        const float gHpSafe = std::max (1e-5f, std::min (gHp, 0.5f));
        const float GHp = gHpSafe / (1.0f + gHpSafe);

        const float fbLP = GHp * (s[3] - hpState) + hpState;
        hpState = sanitize (GHp * (s[3] - hpState) + fbLP);
        const float fbHP = s[3] - fbLP;

        const float driveGain = 1.0f + 4.0f * drive;
        const float u = softClip (driveGain * in - k * softClip (fbHP));

        float y = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = G * (y - s[i]);
            y = v + s[i];
            s[i] = sanitize (v + y);
        }
        return y;
    }
};

/** Standard biquad filter section (Audio EQ Cookbook). */
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;

    void reset() noexcept
    {
        x1 = 0.0f;
        x2 = 0.0f;
        y1 = 0.0f;
        y2 = 0.0f;
    }

    float process (float in) noexcept
    {
        x1 = sanitize (x1);
        x2 = sanitize (x2);
        y1 = sanitize (y1);
        y2 = sanitize (y2);

        const float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = sanitize (out);
        return y1;
    }

    void setLowShelf (float fc, float sampleRate, float gainDb, float Q) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * std::max (20.0f, std::min (fc, sampleRate * 0.49f)) / sampleRate;
        const float cosw0 = std::cos (w0);
        const float sinw0 = std::sin (w0);
        const float alpha = sinw0 / (2.0f * std::max (0.1f, Q));
        const float twoSqrtAAlpha = 2.0f * std::sqrt (A) * alpha;

        const float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + twoSqrtAAlpha;
        b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + twoSqrtAAlpha)) / a0;
        b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - twoSqrtAAlpha)) / a0;
        a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - twoSqrtAAlpha) / a0;
    }

    void setPeakingEQ (float fc, float sampleRate, float gainDb, float Q) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * std::max (20.0f, std::min (fc, sampleRate * 0.49f)) / sampleRate;
        const float cosw0 = std::cos (w0);
        const float sinw0 = std::sin (w0);
        const float alpha = sinw0 / (2.0f * std::max (0.1f, Q));

        const float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosw0) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    void setHighShelf (float fc, float sampleRate, float gainDb, float Q) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * std::max (20.0f, std::min (fc, sampleRate * 0.49f)) / sampleRate;
        const float cosw0 = std::cos (w0);
        const float sinw0 = std::sin (w0);
        const float alpha = sinw0 / (2.0f * std::max (0.1f, Q));
        const float twoSqrtAAlpha = 2.0f * std::sqrt (A) * alpha;

        const float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + twoSqrtAAlpha;
        b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 + twoSqrtAAlpha)) / a0;
        b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 - twoSqrtAAlpha)) / a0;
        a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - twoSqrtAAlpha) / a0;
    }
};

/** Circular delay line with fractional interpolation. */
class DelayLine
{
public:
    void prepare (size_t maxSamples)
    {
        buffer_.assign (maxSamples + 4, 0.0f);
        writeIndex_ = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer_.begin(), buffer_.end(), 0.0f);
        writeIndex_ = 0;
    }

    void write (float in) noexcept
    {
        if (buffer_.empty())
            return;
        buffer_[writeIndex_] = sanitize (in);
        writeIndex_ = (writeIndex_ + 1) % buffer_.size();
    }

    float read (float delaySamples) const noexcept
    {
        if (buffer_.empty())
            return 0.0f;

        const float maxD = static_cast<float> (buffer_.size() - 2);
        const float d = std::max (1.0f, std::min (delaySamples, maxD));
        const auto intD = static_cast<size_t> (d);
        const float frac = d - static_cast<float> (intD);

        const size_t size = buffer_.size();
        const size_t i1 = (writeIndex_ + size - intD) % size;
        const size_t i2 = (writeIndex_ + size - intD - 1) % size;

        return buffer_[i1] * (1.0f - frac) + buffer_[i2] * frac;
    }

private:
    std::vector<float> buffer_;
    size_t writeIndex_ = 0;
};

/** Allpass filter diffuser stage with feedback. */
struct AllpassDiffuser
{
    DelayLine delay;
    float gain = 0.5f;

    void prepare (size_t samples, float g)
    {
        delay.prepare (samples);
        gain = g;
    }

    void reset() noexcept
    {
        delay.reset();
    }

    float process (float in) noexcept
    {
        const float delayed = delay.read (1.0f);
        const float x = in - gain * delayed;
        const float out = delayed + gain * x;
        delay.write (x);
        return sanitize (out);
    }
};

} // namespace synthyboy::filters
