// SynthyBoy - DSP utility functions and primitives for Serum FX modules.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace synthyboy::fx
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kDenormalThreshold = 1.0e-15f;

/** Flushes subnormal / denormal floats to clean zero to prevent CPU stalls. */
inline float flushDenormal (float val) noexcept
{
    return (std::fabs (val) < kDenormalThreshold) ? 0.0f : val;
}

/** Decibels to linear gain. Values <= -60 dB clamp to 0. */
inline float dbToGain (float db) noexcept
{
    if (db <= -59.99f)
        return 0.0f;
    return std::pow (10.0f, db * 0.05f);
}

/** Linear parameter smoother over an n-sample block. */
class ParamSmoother
{
public:
    void reset (float value) noexcept
    {
        current_ = value;
        target_ = value;
    }

    void setTarget (float target) noexcept
    {
        target_ = target;
    }

    float current() const noexcept { return current_; }
    float target() const noexcept { return target_; }

    /** Computes step per sample for an n-sample block. */
    float computeStep (int n) const noexcept
    {
        if (n <= 0)
            return 0.0f;
        return (target_ - current_) / static_cast<float> (n);
    }

    void advance (int n) noexcept
    {
        (void) n;
        current_ = target_;
    }

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
};

/** Delay line with power-of-two capacity and cubic Hermite interpolation. */
class DelayLine
{
public:
    void prepare (int minCapacity)
    {
        int cap = 16;
        while (cap < minCapacity + 16)
            cap <<= 1;
        buffer_.assign (static_cast<size_t> (cap), 0.0f);
        mask_ = cap - 1;
        writeIndex_ = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer_.begin(), buffer_.end(), 0.0f);
        writeIndex_ = 0;
    }

    void write (float sample) noexcept
    {
        buffer_[static_cast<size_t> (writeIndex_)] = flushDenormal (sample);
        writeIndex_ = (writeIndex_ + 1) & mask_;
    }

    float readLinear (float delaySamples) const noexcept
    {
        if (delaySamples < 0.0f)
            delaySamples = 0.0f;
        const int dInt = static_cast<int> (delaySamples);
        const float frac = delaySamples - static_cast<float> (dInt);
        const int i0 = (writeIndex_ - 1 - dInt) & mask_;
        const int i1 = (i0 - 1) & mask_;
        const float s0 = buffer_[static_cast<size_t> (i0)];
        const float s1 = buffer_[static_cast<size_t> (i1)];
        return s0 + frac * (s1 - s0);
    }

    float readCubic (float delaySamples) const noexcept
    {
        if (delaySamples < 1.0f)
            delaySamples = 1.0f;
        const int dInt = static_cast<int> (delaySamples);
        const float frac = delaySamples - static_cast<float> (dInt);
        const int i0 = (writeIndex_ - 1 - dInt) & mask_;
        const int im1 = (i0 + 1) & mask_;
        const int i1 = (i0 - 1) & mask_;
        const int i2 = (i0 - 2) & mask_;

        const float y_m1 = buffer_[static_cast<size_t> (im1)];
        const float y_0  = buffer_[static_cast<size_t> (i0)];
        const float y_1  = buffer_[static_cast<size_t> (i1)];
        const float y_2  = buffer_[static_cast<size_t> (i2)];

        const float c0 = y_0;
        const float c1 = 0.5f * (y_1 - y_m1);
        const float c2 = y_m1 - 2.5f * y_0 + 2.0f * y_1 - 0.5f * y_2;
        const float c3 = 0.5f * (y_2 - y_m1) + 1.5f * (y_0 - y_1);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

private:
    std::vector<float> buffer_;
    int mask_ = 0;
    int writeIndex_ = 0;
};

/** Second-order Direct-Form II Transposed Biquad Filter. */
class Biquad
{
public:
    void reset() noexcept
    {
        s1_ = 0.0f;
        s2_ = 0.0f;
    }

    void setCoefficients (float b0, float b1, float b2, float a1, float a2) noexcept
    {
        b0_ = b0;
        b1_ = b1;
        b2_ = b2;
        a1_ = a1;
        a2_ = a2;
    }

    /** 2nd-order Butterworth Low-Pass (Q = 1 / sqrt(2)). */
    void setButterworthLP (float cutoffHz, double sampleRate) noexcept
    {
        const float nyquist = static_cast<float> (sampleRate * 0.499);
        const float fc = std::clamp (cutoffHz, 10.0f, nyquist);
        const float omega = std::tan (kPi * fc / static_cast<float> (sampleRate));
        const float omegaSq = omega * omega;
        const float sqrt2Omega = 1.41421356237f * omega;
        const float norm = 1.0f / (1.0f + sqrt2Omega + omegaSq);

        b0_ = omegaSq * norm;
        b1_ = 2.0f * b0_;
        b2_ = b0_;
        a1_ = 2.0f * (omegaSq - 1.0f) * norm;
        a2_ = (1.0f - sqrt2Omega + omegaSq) * norm;
    }

    /** 2nd-order Butterworth High-Pass (Q = 1 / sqrt(2)). */
    void setButterworthHP (float cutoffHz, double sampleRate) noexcept
    {
        const float nyquist = static_cast<float> (sampleRate * 0.499);
        const float fc = std::clamp (cutoffHz, 10.0f, nyquist);
        const float omega = std::tan (kPi * fc / static_cast<float> (sampleRate));
        const float omegaSq = omega * omega;
        const float sqrt2Omega = 1.41421356237f * omega;
        const float norm = 1.0f / (1.0f + sqrt2Omega + omegaSq);

        b0_ = norm;
        b1_ = -2.0f * b0_;
        b2_ = b0_;
        a1_ = 2.0f * (omegaSq - 1.0f) * norm;
        a2_ = (1.0f - sqrt2Omega + omegaSq) * norm;
    }

    /** 2nd-order All-Pass filter with Butterworth pole locations (for 3-band alignment). */
    void setButterworthAP (float cutoffHz, double sampleRate) noexcept
    {
        const float nyquist = static_cast<float> (sampleRate * 0.499);
        const float fc = std::clamp (cutoffHz, 10.0f, nyquist);
        const float omega = std::tan (kPi * fc / static_cast<float> (sampleRate));
        const float omegaSq = omega * omega;
        const float sqrt2Omega = 1.41421356237f * omega;
        const float norm = 1.0f / (1.0f + sqrt2Omega + omegaSq);

        const float a1 = 2.0f * (omegaSq - 1.0f) * norm;
        const float a2 = (1.0f - sqrt2Omega + omegaSq) * norm;

        b0_ = a2;
        b1_ = a1;
        b2_ = 1.0f;
        a1_ = a1;
        a2_ = a2;
    }

    float process (float in) noexcept
    {
        const float out = b0_ * in + s1_;
        s1_ = b1_ * in - a1_ * out + s2_;
        s2_ = b2_ * in - a2_ * out;
        s1_ = flushDenormal (s1_);
        s2_ = flushDenormal (s2_);
        return flushDenormal (out);
    }

private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float s1_ = 0.0f, s2_ = 0.0f;
};

/** 4th-Order Linkwitz-Riley (LR4) Low-Pass Filter: two cascaded 2nd-order Butterworth LP biquads. */
class LinkwitzRiley4LP
{
public:
    void reset() noexcept
    {
        stage1_.reset();
        stage2_.reset();
    }

    void setCutoff (float cutoffHz, double sampleRate) noexcept
    {
        stage1_.setButterworthLP (cutoffHz, sampleRate);
        stage2_.setButterworthLP (cutoffHz, sampleRate);
    }

    float process (float in) noexcept
    {
        return stage2_.process (stage1_.process (in));
    }

private:
    Biquad stage1_;
    Biquad stage2_;
};

/** 4th-Order Linkwitz-Riley (LR4) High-Pass Filter: two cascaded 2nd-order Butterworth HP biquads. */
class LinkwitzRiley4HP
{
public:
    void reset() noexcept
    {
        stage1_.reset();
        stage2_.reset();
    }

    void setCutoff (float cutoffHz, double sampleRate) noexcept
    {
        stage1_.setButterworthHP (cutoffHz, sampleRate);
        stage2_.setButterworthHP (cutoffHz, sampleRate);
    }

    float process (float in) noexcept
    {
        return stage2_.process (stage1_.process (in));
    }

private:
    Biquad stage1_;
    Biquad stage2_;
};

/** 4th-Order All-Pass Filter: two cascaded 2nd-order Butterworth AP biquads. */
class LinkwitzRiley4AP
{
public:
    void reset() noexcept
    {
        stage1_.reset();
        stage2_.reset();
    }

    void setCutoff (float cutoffHz, double sampleRate) noexcept
    {
        stage1_.setButterworthAP (cutoffHz, sampleRate);
        stage2_.setButterworthAP (cutoffHz, sampleRate);
    }

    float process (float in) noexcept
    {
        return stage2_.process (stage1_.process (in));
    }

private:
    Biquad stage1_;
    Biquad stage2_;
};

/** 1st-order DC Blocker: y[n] = x[n] - x[n-1] + R * y[n-1] (cutoff ~10 Hz). */
class DcBlocker
{
public:
    void prepare (double sampleRate) noexcept
    {
        const float fc = 10.0f;
        r_ = 1.0f - (kTwoPi * fc / static_cast<float> (sampleRate));
        if (r_ < 0.9f)
            r_ = 0.9f;
        else if (r_ > 0.9999f)
            r_ = 0.9999f;
        reset();
    }

    void reset() noexcept
    {
        x1_ = 0.0f;
        y1_ = 0.0f;
    }

    float process (float in) noexcept
    {
        const float out = in - x1_ + r_ * y1_;
        x1_ = flushDenormal (in);
        y1_ = flushDenormal (out);
        return y1_;
    }

private:
    float r_ = 0.999f;
    float x1_ = 0.0f;
    float y1_ = 0.0f;
};

} // namespace synthyboy::fx
