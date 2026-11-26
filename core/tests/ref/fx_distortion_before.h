// SynthyBoy - core tests: the FROZEN REFERENCE of the distortion effect and of its ADAA laws before the CPU review item 7 (2026-11-26): Distortion and adaa:: as they were at 391febd
// (a switch on the type per sample, the state variable filter's coefficients made every sample, ln cosh computed with its exp and log everywhere). Test only; never edit it
// (test_fxopt_cpu_distortion.cpp compares the Distortion with it). Namespace synthyboy::own::cpuref.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "../../engine-own/dsp/fastmath.h"
#include "../../engine-own/dsp/util.h"
#include "../../engine-own/fx/fx_dsp.h"
#include "../../engine-own/fx/fx_oversampler.h"
#include "synthyboy/aa_switch.h"

namespace synthyboy
{
namespace own
{
namespace cpuref
{
namespace adaa
{
/** Below this distance of two input samples the output is the law at their midpoint. */
constexpr float kEps = 1.0f / 1024.0f;

/** The types (bit n = distortion type n) that run through the ADAA. Types 4 and 5 are lo-fi effects on purpose and never join. */
constexpr uint32_t kConverted = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11);

/** x = the last raw input (before the drive), g = the gain b was computed for, b = the bounded part of F at x * g. */
struct State
{
    float x = 0.0f, g = -1.0f, b = 0.0f;
};

/** tanh (0.3), the constant the Tube law (type 6) subtracts. */
constexpr float kTanh03 = 0.29131261f;
constexpr float kLn2 = 0.69314718f;

/** ln cosh (t) - |t| = L (|t|) - ln 2 with L (s) = log1p (exp (-2 s)): bounded, between -0.7 and 0. */
inline float lncoshBnd (float t, bool fast) noexcept
{
    const float e = fast ? fastmath::exp (-2.0f * std::fabs (t)) : std::exp (-2.0f * std::fabs (t));
    const float l = fast ? kLn2 * fastmath::log2 (1.0f + e) : std::log1p (e);
    return l - kLn2;
}

/** E (s) = 1 - exp (-s), s >= 0. */
inline float oneMinusExp (float s, bool fast) noexcept
{
    return 1.0f - (fast ? fastmath::exp (-s) : std::exp (-s));
}

/** The periodic integral of the Linear Fold triangle (0 .. 0.5), w = fract (v / 4 + 3/4): Fp (w) = 4 w (1 - 2 w) below 1/2, 4 (2 w - 1) (w - 1) above. */
inline float foldIntegral (float v) noexcept
{
    const float t = 0.25f * v + 0.75f;
    const float w = t - ffloor (t);
    return w < 0.5f ? 4.0f * w * (1.0f - 2.0f * w) : 4.0f * (2.0f * w - 1.0f) * (w - 1.0f);
}

/** The piecewise linear part of the antiderivative at t, the argument of the closed form (t = v; type 9: 32 v; type 6: v + 0.3). */
inline float lin (int type, float t) noexcept
{
    switch (type)
    {
        case 0: case 6: case 9: case 1: return std::fabs (t);
        case 11: return t;
        case 10: case 7: return t >= 0.0f ? t : 0.5f * -t;
        case 8: return std::fabs (t);
        default: return 0.0f;
    }
}

/** The bounded part of the antiderivative at t: F = lin + bnd. */
inline float bnd (int type, float t, bool fast) noexcept
{
    const float s = std::fabs (t);
    switch (type)
    {
        case 0: case 6: case 9: return lncoshBnd (t, fast);
        case 1: return s < 1.0f ? -0.5f + 0.5f * (1.0f - s) * (1.0f - s) : -0.5f;
        case 11:
        {
            const float h = s < 1.0f ? 0.5f * s * s : s - 0.5f; // H (s), the integral of min (s, 1)
            return (t < 0.0f ? -h : h) - t;
        }
        case 10: return t >= 0.0f ? lncoshBnd (t, fast) : (s >= 0.5f ? -0.125f : 0.5f * t * t - 0.5f * s);
        case 7: return t >= 0.0f ? -oneMinusExp (s, fast) : -0.25f * oneMinusExp (2.0f * s, fast); // F = v + e^-v - 1 above zero, -v / 2 + e^2v / 4 - 1/4 below
        case 8: return -oneMinusExp (s, fast);
        case 2: return foldIntegral (t);
        default: return 0.0f;
    }
}

/** Today's law (docs/engine-spec/06-effects.md section 4) of a converted type at t (the argument of the closed form). */
inline float naive (int type, float t, bool fast) noexcept
{
    auto th = [fast] (float a) noexcept { return fast ? fastmath::tanh (a) : std::tanh (a); };
    switch (type)
    {
        case 0: case 9: return th (t);
        // Exactly 0 at x = 0 (t == 0.3f). A bare th (t) - th (0.3f) is not: the compiler folds the constant th (0.3f) with a correctly rounded tanh while th (t) runs the
        // library's tanhf, and on GCC / glibc x86-64 the two differ by one ulp (-3e-8, which the DC blocker then keeps as a slow decay). The equality test makes the zero a property of the law.
        case 6: { const float a = th (t); return t == 0.3f ? 0.0f : a - th (0.3f); }
        case 10: return th (std::max (t, 0.0f)) + std::max (std::min (t, 0.0f), -0.5f);
        case 7:
        {
            auto ex = [fast] (float a) noexcept { return fast ? fastmath::exp (a) : std::exp (a); };
            return (1.0f - ex (-std::max (t, 0.0f))) - 0.5f * (1.0f - ex (2.0f * std::min (t, 0.0f)));
        }
        case 8:
        {
            auto ex = [fast] (float a) noexcept { return fast ? fastmath::exp (a) : std::exp (a); };
            return ex (std::min (t, 0.0f)) - ex (-std::max (t, 0.0f));
        }
        case 2: { const float u = 0.25f * t + 0.75f; return std::fabs (-4.0f * (u - ffloor (u)) + 2.0f) - 1.0f; }
        case 3: return fast ? fastmath::sinCycles (0.25f * t) : std::sin (0.5f * kPif * t);
        case 1: return std::clamp (t, -1.0f, 1.0f);
        case 11: return std::fabs (std::clamp (t, -1.0f, 1.0f));
        default: return t;
    }
}

/** One sample of the converted type `type`: x = the raw input, g = the drive gain. The result has no DC blocker applied. */
inline float step (int type, State& s, float x, float g, bool fast) noexcept
{
    const float ge = type == 9 ? 32.0f * g : g; // Zero-Square is tanh (32 v)
    const float off = type == 6 ? 0.3f : 0.0f;  // Tube is tanh (v + 0.3) - tanh (0.3)
    const float sub = type == 6 ? kTanh03 : 0.0f;
    const float t = x * ge + off;
    if (!(std::fabs (t) < 1.0e6f)) // too large or not finite: reset the pair and return the law of the clamped value
    {
        const float c = t != t ? 0.0f : (t > 0.0f ? 1.0e6f : -1.0e6f);
        s.x = 0.0f;
        s.g = ge;
        s.b = bnd (type, off, fast);
        return naive (type, c, fast);
    }
    const float tp = s.x * ge + off;
    if (type == 3) // Sine Fold: the closed form has no quotient, sin (pi m / 2) * sinc (dv / 4) with m the midpoint, dv the difference (the cosine identity of the plan 3.2)
    {
        const float z = 0.25f * (t - tp);
        const float pz = kPif * z, pz2 = pz * pz;
        const float sinc = std::fabs (z) < (fast ? 0.05f : 0.01f) ? 1.0f - pz2 * (1.0f / 6.0f - pz2 * (1.0f / 120.0f)) : (fast ? fastmath::sinCycles (0.5f * z) : std::sin (pz)) / pz;
        const float ys = (fast ? fastmath::sinCycles (0.125f * (t + tp)) : std::sin (0.25f * kPif * (t + tp))) * sinc;
        s.x = flushDenormal (x);
        s.g = ge;
        return ys;
    }
    if (ge != s.g) // the gain changed: the stored bounded part belongs to the old gain
    {
        s.b = bnd (type, tp, fast);
        s.g = ge;
    }
    const float bt = bnd (type, t, fast);
    const float dt = t - tp;
    float y;
    if (std::fabs (dt) < kEps)
        y = naive (type, 0.5f * (t + tp), fast);
    else
        y = ((lin (type, t) - lin (type, tp)) + (bt - s.b)) / dt - sub;
    s.x = flushDenormal (x);
    s.b = flushDenormal (bt);
    return y;
}

/** The 512-segment interpolation of a transfer curve (y == nullptr: identity clamp (u, 0, 1)). */
inline float shaperC (const float* y, float u) noexcept
{
    if (u <= 0.0f) return y ? y[0] : 0.0f;
    if (u >= 1.0f) return y ? y[512] : 1.0f;
    if (!y) return u;
    const float s = 512.0f * u;
    const int i = std::min (static_cast<int> (s), 511);
    const float t = s - static_cast<float> (i);
    return y[i] + (y[i + 1] - y[i]) * t;
}

/** The 512-segment interpolation of the curve's cumulative integral (null pair: identity u^2 / 2 for u in [0, 1]). */
inline float shaperI (const float* y, const float* I, float u) noexcept
{
    if (!y || !I)
    {
        if (u <= 0.0f) return 0.0f;
        if (u >= 1.0f) return u - 0.5f;
        return 0.5f * u * u;
    }
    if (u <= 0.0f) return y[0] * u;
    if (u >= 1.0f) return I[512] + y[512] * (u - 1.0f);
    constexpr float h = 1.0f / 512.0f;
    const float s = 512.0f * u;
    const int i = std::min (static_cast<int> (s), 511);
    const float t = s - static_cast<float> (i);
    return I[i] + h * (y[i] * t + 0.5f * (y[i + 1] - y[i]) * t * t);
}

/** One sample of the X-Shaper (types 12 and 13): x = raw input, m = morph in [0, 1], curves A and B with their cumulative integrals. */
inline float stepShaper (int type, State& s, float x, float m, const float* yA, const float* IA, const float* yB, const float* IB) noexcept
{
    auto evalNaive = [type, m, yA, yB] (float v) noexcept -> float
    {
        if (type == 13)
        {
            const float u = 0.5f * (v + 1.0f);
            const float c = (1.0f - m) * shaperC (yA, u) + m * shaperC (yB, u);
            return 2.0f * c - 1.0f;
        }
        else
        {
            if (v == 0.0f)
                return 0.0f;
            const float u = std::min (std::fabs (v), 1.0f);
            const float c = (1.0f - m) * shaperC (yA, u) + m * shaperC (yB, u);
            return v > 0.0f ? c : -c;
        }
    };

    auto evalF = [type, m, yA, IA, yB, IB] (float v) noexcept -> float
    {
        if (type == 13)
        {
            const float u = 0.5f * (v + 1.0f);
            const float C = (1.0f - m) * shaperI (yA, IA, u) + m * shaperI (yB, IB, u);
            return 4.0f * C - 2.0f * u;
        }
        else
        {
            const float u = std::fabs (v);
            return (1.0f - m) * shaperI (yA, IA, u) + m * shaperI (yB, IB, u);
        }
    };

    if (!(std::fabs (x) < 1.0e6f)) // too large or not finite: reset the pair and return the naive law
    {
        const float c = x != x ? 0.0f : (x > 0.0f ? 1.0e6f : -1.0e6f);
        s.x = 0.0f;
        s.g = m;
        s.b = evalF (0.0f);
        return evalNaive (c);
    }
    if (s.g != m) // morph changed: recompute stored integral at previous x
    {
        s.b = evalF (s.x);
        s.g = m;
    }
    const float Fx = evalF (x);
    const float dx = x - s.x;
    float y;
    if (std::fabs (dx) < kEps)
        y = evalNaive (0.5f * (x + s.x));
    else
        y = (Fx - s.b) / dx;
    s.x = flushDenormal (x);
    s.b = flushDenormal (Fx);
    return y;
}
} // namespace adaa

class DistortionBefore
{
public:
    static constexpr int kLatency = HalfBand2x::kLatency;
    bool fast = false; // tier low: tanh, sin, exp, exp2 and pow by the cheaper forms of dsp/fastmath.h
    struct Params { int type = 0; float driveDb = 0.0f, mix = 1.0f, cutoff = 4000.0f, resonance = 0.5f, blend = 0.0f; int order = 0; bool os2 = false; };
    int latencySamples() const noexcept { return p.os2 ? kLatency : 0; }
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept
    {
        for (auto& s : svf) s.reset();
        held[0] = held[1] = 0.0f;
        holdPhase[0] = holdPhase[1] = 0.0f;
        dc[0] = dc[1] = 0.0f;
        adaaState[0] = adaaState[1] = adaa::State();
        for (auto& o : oversampler) o.reset();
        for (auto& d : dryDelay) d.reset();
        for (auto& d : lofiDelay) d.reset();
        counter = 0;
        primed = false;
    }
    /** The drive of the new control period: it is reached at the end of the period (process (): linear across it, nothing is stepped, docs/engine-spec/06-effects.md section 1), the
        period before it is where the ramp starts. The first set () after a reset () has no period before: no ramp. */
    void set (const Params& np) noexcept
    {
        if (np.type != p.type) // switching the type clears the state (docs/engine-spec/06-effects.md section 4)
            adaaState[0] = adaaState[1] = adaa::State();
        if (np.os2 != p.os2)
        {
            adaaState[0] = adaaState[1] = adaa::State();
            for (auto& o : oversampler) o.reset();
            for (auto& d : dryDelay) d.reset();
            for (auto& d : lofiDelay) d.reset();
        }
        p = np;
        const DriveLaw now = driveLaw (p.driveDb);
        prevLaw = primed ? curLaw : now;
        curLaw = now;
        primed = true;
    }
    void setShaper (const float* yA, const float* IA, const float* yB, const float* IB) noexcept
    {
        shaperA = yA;
        shaperIntA = IA;
        shaperB = yB;
        shaperIntB = IB;
    }
    /** t0 / period: the position of the call in the control period (the gain, the morph and the divisor of the drive are interpolated across it from the period before to this one,
        like the Serum law of the EQ); period 0 (a caller without periods): the drive of the last set () at once. */
    void process (float* l, float* r, int n, int t0 = 0, int period = 0) noexcept
    {
        aaOn = (synthyboy::aa::off() & synthyboy::aa::kDistortion) == 0; // the test and benchmark switch, read once per call
        const float cg = Svf::coefficient (p.cutoff, sampleRate);
        const float k = 2.0f - 1.9f * clamp01 (p.resonance);
        const bool ramp = period > 0 && (prevLaw.g != curLaw.g || prevLaw.u != curLaw.u || prevLaw.d != curLaw.d); // a still drive: the same values as without the ramp, bit for bit
        const float invP = 1.0f / static_cast<float> (std::max (period, 1));
        const float dcA = 1.0f - (fast ? fastmath::exp (-kTwoPif * 10.0f / static_cast<float> (sampleRate)) : std::exp (-kTwoPif * 10.0f / static_cast<float> (sampleRate)));
        float* io[2] = { l, r };
        if (!p.os2)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float dry = io[c][i];
                    float x = dry;
                    float g = curLaw.g, u = curLaw.u, d = curLaw.d;
                    if (ramp)
                    {
                        const float t = std::min (static_cast<float> (t0 + i + 1) * invP, 1.0f);
                        g = prevLaw.g + (curLaw.g - prevLaw.g) * t;
                        u = prevLaw.u + (curLaw.u - prevLaw.u) * t;
                        d = prevLaw.d + (curLaw.d - prevLaw.d) * t;
                    }
                    if (p.order == 1) x = filt (c, x, cg, k);
                    x = applyDc (c, shape (c, x, g, u, d), dcA);
                    if (p.order == 2) x = filt (c, x, cg, k);
                    io[c][i] = dry * (1.0f - p.mix) + x * p.mix;
                }
            return;
        }
        const bool lofi = p.type == 4 || p.type == 5; // Bit Crush and Down Sample are lo-fi effects on purpose and are delayed without oversampling
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float dry = io[c][i];
                const float dryDelayed = dryDelay[c].pushPop (dry);
                float x = dry;
                if (p.order == 1) x = filt (c, x, cg, k);
                if (lofi)
                {
                    float g = curLaw.g, u = curLaw.u, d = curLaw.d;
                    if (ramp)
                    {
                        const float t = std::min (static_cast<float> (t0 + i + 1) * invP, 1.0f);
                        g = prevLaw.g + (curLaw.g - prevLaw.g) * t;
                        u = prevLaw.u + (curLaw.u - prevLaw.u) * t;
                        d = prevLaw.d + (curLaw.d - prevLaw.d) * t;
                    }
                    x = lofiDelay[c].pushPop (shape (c, x, g, u, d));
                }
                else
                {
                    const auto up2 = oversampler[c].up (x);
                    float g0 = curLaw.g, u0 = curLaw.u, d0 = curLaw.d;
                    float g1 = curLaw.g, u1 = curLaw.u, d1 = curLaw.d;
                    if (ramp)
                    {
                        const float t05 = std::min ((static_cast<float> (t0 + i) + 0.5f) * invP, 1.0f);
                        const float t1 = std::min (static_cast<float> (t0 + i + 1) * invP, 1.0f);
                        g0 = prevLaw.g + (curLaw.g - prevLaw.g) * t05;
                        u0 = prevLaw.u + (curLaw.u - prevLaw.u) * t05;
                        d0 = prevLaw.d + (curLaw.d - prevLaw.d) * t05;
                        g1 = prevLaw.g + (curLaw.g - prevLaw.g) * t1;
                        u1 = prevLaw.u + (curLaw.u - prevLaw.u) * t1;
                        d1 = prevLaw.d + (curLaw.d - prevLaw.d) * t1;
                    }
                    const float y0 = shape (c, up2[0], g0, u0, d0);
                    const float y1 = shape (c, up2[1], g1, u1, d1);
                    x = applyDc (c, oversampler[c].down (y0, y1), dcA);
                }
                if (p.order == 2) x = filt (c, x, cg, k);
                io[c][i] = dryDelayed * (1.0f - p.mix) + x * p.mix;
            }
    }

private:
    struct Delay15
    {
        std::array<float, 16> buf {};
        int pos = 0;
        void reset() noexcept { buf.fill (0.0f); pos = 0; }
        float pushPop (float x) noexcept
        {
            buf[static_cast<size_t> (pos)] = x;
            const float out = buf[static_cast<size_t> ((pos - kLatency) & 15)];
            pos = (pos + 1) & 15;
            return out;
        }
    };
    /** What the drive in dB makes of the shaper: the gain, the morph / bit depth position and the divisor of the sample rate and bit laws. */
    struct DriveLaw { float g = 1.0f, u = 0.5f, d = 1.0f; };
    DriveLaw driveLaw (float driveDb) const noexcept
    {
        DriveLaw w;
        w.g = std::pow (10.0f, driveDb * 0.05f);
        w.u = clamp01 ((driveDb + 30.0f) / 60.0f);
        const float dExp = std::clamp (driveDb, -30.0f, 30.0f) * 0.05f;
        w.d = fast ? fastmath::pow10 (dExp) : std::pow (10.0f, dExp);
        return w;
    }
    float filt (int c, float x, float g, float k) noexcept
    {
        svf[c].process (x, g, k);
        // blend 0 .. 2 : low pass, band pass, high pass
        const float b = std::clamp (p.blend, 0.0f, 2.0f);
        return b < 1.0f ? lerp (svf[c].lp, svf[c].bp * k, b) : lerp (svf[c].bp * k, svf[c].hp, b - 1.0f);
    }
    float shape (int c, float x, float g, float u, float d) noexcept
    {
        // v = x * drive; the laws of docs/engine-spec/06-effects.md section 4
        const float v = x * g;
        auto th = [this] (float a) { return fast ? fastmath::tanh (a) : std::tanh (a); };
        auto ex = [this] (float a) { return fast ? fastmath::exp (a) : std::exp (a); };
        float y = std::clamp (v, -1.0f, 1.0f);
        if (aaOn && p.type >= 0 && p.type < 32 && ((adaa::kConverted >> p.type) & 1u) != 0u)
            y = adaa::step (p.type, adaaState[c], x, g, fast); // first-order ADAA (fx_adaa.h)
        else
        switch (p.type)
        {
            case 0: y = th (v); break;
            case 1: y = std::clamp (v, -1.0f, 1.0f); break;
            case 2: { const float t = 0.25f * v + 0.75f; y = std::fabs (-4.0f * (t - ffloor (t)) + 2.0f) - 1.0f; break; }
            case 3: { const float t = 0.5f - 0.25f * v; y = fast ? fastmath::sinCycles (t) : std::sin (kTwoPif * (t - ffloor (t))); break; }
            case 4:
            {
                const float qx = d != 0.0f ? v / d : v;
                const float q = std::clamp (u * u, 1.5e-8f, 1.0f);
                y = std::round (qx / q) * q;
                break;
            }
            case 5:
            {
                const float dx = d != 0.0f ? v / d : v;
                const float period = std::max (static_cast<float> (sampleRate) * std::max (0.99f / std::max ((1.0f - u) * (1.0f - u), 1.0e-4f), 1.0f) / 88200.0f, 1.0f);
                holdPhase[c] += 1.0f;
                if (holdPhase[c] >= period) { holdPhase[c] -= period; held[c] = dx; }
                y = held[c];
                break;
            }
            case 6: { const float t = v + 0.3f, a = th (t); y = t == 0.3f ? 0.0f : a - th (0.3f); break; } // exactly 0 at v = 0 (see adaa::naive: a folded th (0.3f) is one ulp off the runtime th (t))
            case 7: { const float vp = std::max (v, 0.0f), vn = std::min (v, 0.0f); y = (1.0f - ex (-vp)) - 0.5f * (1.0f - ex (2.0f * vn)); break; }
            case 8: y = ex (std::min (v, 0.0f)) - ex (-std::max (v, 0.0f)); break;
            case 9: y = th (32.0f * v); break;
            case 10: y = th (std::max (v, 0.0f)) + std::max (std::min (v, 0.0f), -0.5f); break;
            case 11: y = std::fabs (std::clamp (v, -1.0f, 1.0f)); break;
            case 12:
            case 13:
                y = aaOn ? adaa::stepShaper (p.type, adaaState[c], x, u, shaperA, shaperIntA, shaperB, shaperIntB) : std::clamp (v, -1.0f, 1.0f);
                break;
        }
        return y;
    }
    float applyDc (int c, float y, float dcA) noexcept
    {
        const bool asym = p.type == 6 || p.type == 7 || p.type == 10 || p.type == 11 || p.type == 13;
        if (asym) // 10 Hz DC blocker
        {
            dc[c] += dcA * (y - dc[c]);
            dc[c] = flushDenormal (dc[c]);
            y -= dc[c];
        }
        return y;
    }
    Params p;
    DriveLaw prevLaw, curLaw;
    bool primed = false;
    double sampleRate = 48000.0;
    Svf svf[2];
    HalfBand2x oversampler[2];
    Delay15 dryDelay[2];
    Delay15 lofiDelay[2];
    float held[2] = {}, holdPhase[2] = {}, dc[2] = {};
    adaa::State adaaState[2];
    const float* shaperA = nullptr;
    const float* shaperIntA = nullptr;
    const float* shaperB = nullptr;
    const float* shaperIntB = nullptr;
    bool aaOn = true;
    unsigned counter = 0;
};

} // namespace cpuref
} // namespace own
} // namespace synthyboy
