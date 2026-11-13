// SynthyBoy - own engine: the eight classic filter models (Analog, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser), written from
// docs/engine-spec/05-filters.md and its measured responses (original code). Models 8 and up are the filters-serum collection.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../dsp/util.h"
#include "../dsp/fastmath.h"
#include "../fx/fx_dsp.h"

#ifndef SB_MID_SAT
#define SB_MID_SAT 1.0f
#endif
#ifndef SB_DIRTY_Q1
#define SB_DIRTY_Q1 0.556f
#endif
#ifndef SB_DIRTY_SAT
#define SB_DIRTY_SAT 0.5f
#endif
#ifndef SB_DIODE_CORNER
#define SB_DIODE_CORNER 0.765f
#endif
#ifndef SB_DIODE_GAIN
#define SB_DIODE_GAIN 9.8f
#endif
#ifndef SB_DIODE_FB
#define SB_DIODE_FB 0.28f
#endif

namespace synthyboy
{
namespace own
{
struct ClassicParams
{
    int model = 3, style = 0;
    float cutoffHz = 1000.0f, res = 0.2f, driveDb = 0.0f, blendRaw = 0.0f; // blendRaw 0 .. 2
    float combOffset = 42.0f, fx = 0.5f, fy = 0.5f, fres = 0.85f, fspread = 0.0f, ftranspose = 0.0f;
    bool fast = false; // tier low: tanh by a rational approximation (error < 1e-4)
};

/** One channel of a classic model. process () takes the block's start and end parameters and interpolates the coefficients every 8 samples. */
class ClassicFilter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        memoValid = false;
        rampValid = false;
        svfRampValid = false;
        comb.prepare (static_cast<int> (sampleRate * 0.1));
        reset();
    }

    void reset() noexcept
    {
        s[0] = s[1] = s[2] = s[3] = s[4] = s[5] = 0.0f;
        for (auto& x : dv) x = 0.0f;
        dHp[0] = dHp[1] = dFb = dU = dPre = tilt = 0.0f;
        for (auto& f : fm) f.reset();
        if (combUsed) // the comb line (32 KB) is cleared only if the Comb model has written into it
        {
            comb.reset();
            combUsed = false;
        }
        for (auto& z : ap) z = 0.0f;
        fbState = 0.0f;
        lpA = lpB = 0.0f;
    }

    /** The coefficients change on the absolute 8 sample grid of the control period (t0 = position in the period), so the result does not depend on how the
        host cuts the blocks. */
    void process (const float* in, float* out, int n, const ClassicParams& a, const ClassicParams& b, int t0) noexcept
    {
        if (sameParams (a, b) && a.cutoffHz >= 1.0f)
        {
            // nothing moves across the period: every 8 sample piece would get exactly b (pow (1, t) = 1, lerp (x, x, t) = x), and the sample loops
            // carry their state from piece to piece, so one call gives the same samples
            block (in, out, n, b);
            return;
        }
        if (b.fast && a.fast && a.model == b.model && a.style == b.style)
        {
            if (b.model == 2)
            {
                ladderRamp (in, out, n, a, b, t0);
                return;
            }
            if (b.model == 0 || b.model == 1 || b.model == 3)
            {
                svfRamp (in, out, n, a, b, t0);
                return;
            }
        }
        for (int i = 0; i < n;)
        {
            const int pos = t0 + i;
            const int m = std::min (8 - (pos % 8), n - i);
            const float t = static_cast<float> ((pos / 8) * 8 + 8) / static_cast<float> (kControlPeriod);
            ClassicParams p = b;
            p.cutoffHz = a.cutoffHz * std::pow (b.cutoffHz / std::max (a.cutoffHz, 1.0f), t);
            p.res = lerp (a.res, b.res, t);
            p.driveDb = lerp (a.driveDb, b.driveDb, t);
            p.blendRaw = lerp (a.blendRaw, b.blendRaw, t);
            block (in + i, out + i, m, p);
            i += m;
        }
    }

private:
    static bool sameParams (const ClassicParams& a, const ClassicParams& b) noexcept
    {
        return a.model == b.model && a.style == b.style && a.cutoffHz == b.cutoffHz && a.res == b.res && a.driveDb == b.driveDb && a.blendRaw == b.blendRaw
            && a.combOffset == b.combOffset && a.fx == b.fx && a.fy == b.fy && a.fres == b.fres && a.fspread == b.fspread && a.ftranspose == b.ftranspose && a.fast == b.fast;
    }
    static float sat (float v) noexcept { return v - 0.9f * v * v * v / (v * v + 3.0f); }
    /** tanh: the library function, or at tier low a [7/6] Pade approximant (absolute error below 1e-4, clamped to +-1 beyond |x| = 4.972 where it reaches 1). */
    float th (float x) const noexcept
    {
        return fastMath ? fastmath::tanh (x) : std::tanh (x);
    }
    float hardtanh (float v) const noexcept
    {
        const float a = std::fabs (v);
        return a <= 0.66f ? v : std::copysign (0.66f + 0.34f * th ((a - 0.66f) / 0.34f), v);
    }
    bool fastMath = false;
    /** The values derived from the parameters (several pow / sqrt / tan) are kept while the parameters stay the same, as they do for most control periods. */
    void remember (const ClassicParams& p) noexcept
    {
        if (memoValid && sameParams (p, memoP))
            return;
        memoP = p;
        fastMath = p.fast;
        memoValid = true;
        svfValid = false;
        ladValid = false;
        mf = std::clamp (p.cutoffHz, 8.0f, static_cast<float> (sr) * 0.45f);
        md = std::pow (10.0f, p.driveDb * 0.05f);
        mr = clamp01 (p.res);
        mbraw = std::clamp (p.blendRaw, 0.0f, 2.0f);
    }
    /** The setup of the state variable models (0 Analog, 1 Dirty, 3 Digital) for the parameters last passed to remember (). */
    void ensureSvf (const ClassicParams& p) noexcept
    {
        if (svfValid)
            return;
        const float f = mf, d = md, r = mr;
        switch (p.model)
        {
            case 3: svf = svfSetup (f, 0.5f + 15.5f * r * r * r, 1.0f, d, d / (2.0f * r * r * r + 1.0f), 1.0f / std::sqrt (d), 3, p.style, p.blendRaw, r); break;
            case 0: svf = svfSetup (f * kAn[0], kAn[1] + kAn[2] * std::pow (2.0f * r, 5.1f), kAn[4], d, d / (2.0f * r + 1.0f), 1.0f / std::sqrt (d) * (p.style != 0 ? kAn[5] : kAn[3]), 0, p.style, p.blendRaw, r); break;
            default: svf = svfSetup (f, 1.0f / std::max (1.8f - 1.87f * std::sqrt (r), 0.03f), kDirtyQ1, 2.0f * (d - 1.0f) + 1.0f, (2.0f * (d - 1.0f) + 1.0f) / (r + 1.0f), 1.0f / std::sqrt (d) * 1.05f, 1, p.style, p.blendRaw, r); break;
        }
        svfValid = true;
    }

    void block (const float* in, float* out, int n, const ClassicParams& p) noexcept
    {
        remember (p);
        const float f = mf, d = md, r = mr, braw = mbraw;
        switch (p.model)
        {
            case 3: satLoop = false; ensureSvf (p); svfModel (in, out, n, 3, true); break;
            case 0: satLoop = false; ensureSvf (p); svfModel (in, out, n, 0, false); break;
            case 1: satLoop = true; ensureSvf (p); svfModel (in, out, n, 1, false); break;
            case 2: ladder (in, out, n, f, r, d, p.style, braw - 1.0f); break;
            case 4: diode (in, out, n, f, r, d, p); break;
            case 5: formant (in, out, n, p); break;
            case 6: combFilter (in, out, n, f, r, p); break;
            case 7: phaserFilter (in, out, n, f, r, d, p); break;
            default: std::copy (in, in + n, out); break;
        }
    }

public:
    /** Two channels of the state variable models with parameters that stay put across the period, in one loop: the two recurrences are independent, so they overlap
        instead of running one after the other. The samples are those of two separate process () calls. False (nothing done) if the case does not apply. */
    static bool processPair (ClassicFilter& A, ClassicFilter& B, const float* inA, float* outA, const float* inB, float* outB, int n, const ClassicParams& aA, const ClassicParams& bA,
                             const ClassicParams& aB, const ClassicParams& bB) noexcept
    {
        if (!(sameParams (aA, bA) && aA.cutoffHz >= 1.0f && sameParams (aB, bB) && aB.cutoffHz >= 1.0f))
            return false;
        const int model = bA.model;
        if (bB.model != model || bB.style != bA.style || !(model == 0 || model == 1 || model == 2 || model == 3))
            return false;
        if (model == 2)
            return processPairLadder (A, B, inA, outA, inB, outB, n, bA, bB);
        A.remember (bA);
        B.remember (bB);
        A.ensureSvf (bA);
        B.ensureSvf (bB);
        if (A.svf.shelf || B.svf.shelf || A.svf.two != B.svf.two)
            return false;
        A.satLoop = B.satLoop = model == 1;
        const bool linearHard = model == 3;
        const SvfSetup& za = A.svf;
        const SvfSetup& zb = B.svf;
        const bool two = za.two;
        const bool sat = A.satLoop;
        for (int i = 0; i < n; ++i)
        {
            float ua = inA[i] * za.dEff, ub = inB[i] * zb.dEff;
            if (two)
            {
                float lpa, bpa, hpa, lpb, bpb, hpb;
                A.tpt (0, ua, za.g, za.R1, model == 3 ? za.R : za.R1, lpa, bpa, hpa, false);
                B.tpt (0, ub, zb.g, zb.R1, model == 3 ? zb.R : zb.R1, lpb, bpb, hpb, false);
                const float y1a = za.wa.lp * lpa + za.wa.bp * bpa + za.wa.hp * hpa;
                const float y1b = zb.wa.lp * lpb + zb.wa.bp * bpb + zb.wa.hp * hpb;
                ua = linearHard ? A.hardtanh (y1a) : (model == 1 ? y1a : A.th (y1a * kMidSat) / kMidSat);
                ub = linearHard ? B.hardtanh (y1b) : (model == 1 ? y1b : B.th (y1b * kMidSat) / kMidSat);
            }
            float lpa, bpa, hpa, lpb, bpb, hpb;
            A.tpt (two ? 3 : 0, ua, za.g, za.R, za.R, lpa, bpa, hpa, sat);
            B.tpt (two ? 3 : 0, ub, zb.g, zb.R, zb.R, lpb, bpb, hpb, sat);
            float ya = za.wb.lp * lpa + za.wb.bp * bpa + za.wb.hp * hpa;
            float yb = zb.wb.lp * lpb + zb.wb.bp * bpb + zb.wb.hp * hpb;
            ya = linearHard ? A.hardtanh (ya) : ya;
            yb = linearHard ? B.hardtanh (yb) : yb;
            outA[i] = ya * za.post;
            outB[i] = yb * zb.post;
        }
        return true;
    }

private:
    ClassicParams memoP;
    bool memoValid = false;
    float mf = 0.0f, md = 1.0f, mr = 0.0f, mbraw = 0.0f;

    /** The weights of the sections for a style and the blend `b` in -1 .. 1 (document 05, 2.1). */
    struct Wt { float lp, bp, hp; };
    static void styleWeights (int style, float b, Wt& A, Wt& B) noexcept
    {
        const float t = 0.5f * (b + 1.0f), pn = std::min (b, 0.0f), m = std::sqrt (0.5f * std::max (b, 0.0f) * std::max (b, 0.0f) + 0.5f);
        switch (style)
        {
            case 2: A = B = { std::min (1.0f - b, 1.0f), 0.0f, std::min (1.0f + b, 1.0f) }; break;
            case 3: A = { t, 0.0f, 1.0f }; B = { 1.0f, 0.0f, t }; break;
            case 4: A = B = { m * (1.0f + pn), 2.0f * m * (1.0f + pn - b), m * (1.0f + pn) }; break;
            default: A = B = { std::max (-b, 0.0f), std::sqrt (std::max (0.0f, 1.0f - b * b)), std::max (b, 0.0f) }; break;
        }
    }

    // one or two (24 dB) state variable sections with the weights of the style (document 05, 2 and 2.1); the saturation of the Dirty model sits in the loop of the section
    // `dRaw` is the drive as set, `dComp` the drive compensated by the resonance of the model; the style blends them
    /** Everything an svfModel call derives from its arguments (tan, divisions, weights, square roots): computed when the parameters change. */
    struct SvfSetup
    {
        float g = 0.0f, R = 0.0f, R1 = 0.0f, dEff = 1.0f, post = 1.0f;
        Wt wa {}, wb {};
        bool two = false, shelf = false;
    };
    SvfSetup svf;
    bool svfValid = false;

    void svfModel (const float* in, float* out, int n, int model, bool linearHard) noexcept
    {
        svfRun (svf, in, out, n, model, linearHard);
    }
    void svfRun (const SvfSetup& z, const float* in, float* out, int n, int model, bool linearHard) noexcept
    {
        if (z.shelf)
        {
            std::copy (in, in + n, out); // the shelving mode at 0 dB: LP + HP + R BP = u exactly (document 05, 2.1)
            return;
        }
        const float g = z.g, R = z.R, R1 = z.R1, dEff = z.dEff, post = z.post;
        const Wt wa = z.wa, wb = z.wb;
        const bool two = z.two;
        for (int i = 0; i < n; ++i)
        {
            float u = in[i] * dEff;
            if (two)
            {
                // the first section: R = 1 in the loop, the weights with the true R inside the high pass
                float lp, bp, hp;
                tpt (0, u, g, R1, model == 3 ? R : R1, lp, bp, hp, false);
                float y1 = wa.lp * lp + wa.bp * bp + wa.hp * hp;
                u = linearHard ? hardtanh (y1) : (model == 1 ? y1 : th (y1 * kMidSat) / kMidSat);
            }
            float lp, bp, hp;
            tpt (two ? 3 : 0, u, g, R, R, lp, bp, hp, satLoop);
            float y = wb.lp * lp + wb.bp * bp + wb.hp * hp;
            y = linearHard ? hardtanh (y) : y;
            out[i] = y * post;
        }
    }
    /** The setup of the state variable models for parameters p, without touching the memo of block (). */
    SvfSetup svfSetupFor (const ClassicParams& p) const noexcept
    {
        const float f = std::clamp (p.cutoffHz, 8.0f, static_cast<float> (sr) * 0.45f);
        const float d = p.fast ? fastmath::pow10 (p.driveDb * 0.05f) : std::pow (10.0f, p.driveDb * 0.05f);
        const float r = clamp01 (p.res);
        switch (p.model)
        {
            case 3: return svfSetup (f, 0.5f + 15.5f * r * r * r, 1.0f, d, d / (2.0f * r * r * r + 1.0f), 1.0f / std::sqrt (d), 3, p.style, p.blendRaw, r);
            case 0: return svfSetup (f * kAn[0], kAn[1] + kAn[2] * std::pow (2.0f * r, 5.1f), kAn[4], d, d / (2.0f * r + 1.0f), 1.0f / std::sqrt (d) * (p.style != 0 ? kAn[5] : kAn[3]), 0, p.style, p.blendRaw, r);
            default: return svfSetup (f, 1.0f / std::max (1.8f - 1.87f * std::sqrt (r), 0.03f), kDirtyQ1, 2.0f * (d - 1.0f) + 1.0f, (2.0f * (d - 1.0f) + 1.0f) / (r + 1.0f), 1.0f / std::sqrt (d) * 1.05f, 1, p.style, p.blendRaw, r);
        }
    }
    /** Tier low, state variable models with moving parameters: the setup (tan, pow, sqrt, divisions) is computed at the two ends of the period, its coefficients are
        interpolated linearly over the four pieces of 8 samples (see ladderRamp). */
    void svfRamp (const float* in, float* out, int n, const ClassicParams& a, const ClassicParams& b, int t0) noexcept
    {
        fastMath = true;
        satLoop = b.model == 1;
        if (!svfRampValid || !sameParams (svfRampP, a))
            svfRampA = svfSetupFor (a);
        else
            svfRampA = svfRampB;
        svfRampB = svfSetupFor (b);
        svfRampP = b;
        svfRampValid = true;
        const bool linearHard = b.model == 3;
        for (int i = 0; i < n;)
        {
            const int pos = t0 + i;
            const int m = std::min (8 - (pos % 8), n - i);
            const float t = static_cast<float> ((pos / 8) * 8 + 8) / static_cast<float> (kControlPeriod);
            SvfSetup z = svfRampB; // the flags (two, shelf) are those of the end of the period
            z.g = lerp (svfRampA.g, svfRampB.g, t);
            z.R = lerp (svfRampA.R, svfRampB.R, t);
            z.R1 = lerp (svfRampA.R1, svfRampB.R1, t);
            z.dEff = lerp (svfRampA.dEff, svfRampB.dEff, t);
            z.post = lerp (svfRampA.post, svfRampB.post, t);
            z.wa = { lerp (svfRampA.wa.lp, svfRampB.wa.lp, t), lerp (svfRampA.wa.bp, svfRampB.wa.bp, t), lerp (svfRampA.wa.hp, svfRampB.wa.hp, t) };
            z.wb = { lerp (svfRampA.wb.lp, svfRampB.wb.lp, t), lerp (svfRampA.wb.bp, svfRampB.wb.bp, t), lerp (svfRampA.wb.hp, svfRampB.wb.hp, t) };
            svfRun (z, in + i, out + i, m, b.model, linearHard);
            i += m;
        }
    }
    SvfSetup svfRampA, svfRampB;
    ClassicParams svfRampP;
    bool svfRampValid = false;
    SvfSetup svfSetup (float f, float Q, float Q1, float dRaw, float dComp, float postBase, int model, int style, float blendRaw, float resKnob) const noexcept
    {
        SvfSetup z;
        z.g = std::tan (kPif * std::min (f / static_cast<float> (sr), 0.499f));
        z.R = 1.0f / std::max (Q, 0.3f);
        z.R1 = 1.0f / Q1;
        const float b = std::clamp (blendRaw - 1.0f, -1.0f, 1.0f);
        if (model == 3 && style == 5)
        {
            z.shelf = true;
            return z;
        }
        styleWeights (style == 5 ? 1 : style, b, z.wa, z.wb);
        // the sections: the Digital model runs one for the styles 0 and 5, two for 1 .. 4; Analog and Dirty one for style 0, two for the others (style 3 the dual pair)
        z.two = style != 0;
        float w = 1.0f, dEff;
        switch (style)
        {
            case 2: w = std::fabs (b); break;
            case 4: w = std::min (1.0f - b, 1.0f); break;
            default: w = 1.0f; break;
        }
        const float t = 0.5f * (b + 1.0f);
        if (style == 3)
            dEff = lerp (dRaw, dComp * (2.0f - t), std::min (1.0f - b, 1.0f));
        else
            dEff = lerp (dRaw, dComp, w);
        float post;
        if (model == 3)
            post = postBase; // 1 / sqrt (d) with the raw drive
        else if (model == 0)
            post = postBase * std::sqrt ((2.0f * resKnob + 1.0f) * dComp) / std::sqrt (std::max ((2.0f * resKnob + 1.0f) * dEff, 1.0e-6f)); // 1 / sqrt ((2 r + 1) d_eff)
        else
            post = postBase * std::sqrt (std::max (dComp, 1.0e-6f) / std::max (dEff, 1.0e-6f)) * (style >= 2 ? 1.0f - 0.2f * z.wb.hp : 1.0f); // (measured: the high pass share of the Dirty styles sits about 2 dB low)
        z.dEff = dEff;
        z.post = post;
        return z;
    }
    static inline float kMidSat = SB_MID_SAT;
    static inline float kDirtyQ1 = SB_DIRTY_Q1, kDirtySat = SB_DIRTY_SAT;
    bool satLoop = false;
    void tpt (int k, float u, float g, float Rloop, float Rhp, float& lp, float& bp, float& hp, bool saturate) noexcept
    {
        float& ic1 = s[k];
        float& ic2 = s[k + 1];
        const float v3 = u - ic2;
        const float v1 = (ic1 + g * v3) / (1.0f + g * Rloop + g * g);
        const float v2 = ic2 + g * v1;
        ic1 = flushDenormal (2.0f * (saturate ? th (v1 * kDirtySat) / kDirtySat : v1) - ic1);
        ic2 = flushDenormal (2.0f * v2 - ic2);
        lp = v2;
        bp = v1;
        hp = u - Rhp * v1 - v2;
    }

    // four saturating one pole stages with global feedback (zero delay feedback solved linearly); the output is a weighted sum of the input and the stage outputs (document 05, 2.1)
    struct LadderSetup
    {
        float G = 0.0f, k = 0.0f, pre = 1.0f, post = 1.0f, invDen = 1.0f, c[5] = {};
    };
    LadderSetup lad;
    bool ladValid = false;

    LadderSetup ladderSetup (float f, float r, float d, int style, float bRaw) const noexcept
    {
        LadderSetup z;
        const float g = std::tan (kPif * std::min (f / static_cast<float> (sr), 0.499f));
        z.G = g / (1.0f + g);
        const float b = std::clamp (bRaw, -1.0f, 1.0f);
        const float Lw = std::max (-b, 0.0f), Bw = std::sqrt (std::max (0.0f, 1.0f - b * b)), Hw = std::max (b, 0.0f);
        const float H = std::min (1.0f + b, 1.0f), L = std::min (1.0f - b, 1.0f);
        const float pn = std::min (b, 0.0f), m = std::sqrt (0.5f * Hw * Hw + 0.5f), q = m * (1.0f + pn), band = 2.0f * m * (1.0f + pn - b);
        const float ab = 1.0f - std::fabs (b);
        float* c = z.c;
        switch (style)
        {
            case 1: c[0] = Hw; c[1] = -4.0f * Hw; c[2] = 6.0f * Hw - ab; c[3] = -4.0f * Hw + 2.0f * ab; c[4] = Lw + Hw - ab; break;
            case 2: c[0] = H * H; c[1] = -4.0f * H * H; c[2] = 6.0f * H * H + 2.0f * H * L; c[3] = -4.0f * H * H - 4.0f * H * L; c[4] = H * H + L * L + 2.0f * H * L; break;
            case 3: c[0] = H; c[1] = -4.0f * H; c[2] = 8.0f * H + 4.0f * L; c[3] = -8.0f * H - 8.0f * L; c[4] = 4.0f * H + 4.0f * L; break;
            case 4: c[0] = q; c[1] = band - 2.0f * q; c[2] = 2.0f * q - band; c[3] = 0.0f; c[4] = 0.0f; break;
            default: c[0] = Hw; c[1] = Bw - 2.0f * Hw; c[2] = Lw - Bw + Hw; c[3] = 0.0f; c[4] = 0.0f; break; // style 0 and 5
        }
        const int st5 = style == 5 ? 0 : style;
        // drive: d (1 + r') faded to d as the blend goes to high pass; style 3 uses the raw drive, style 4 fades between d and that
        const float rp = st5 == 0 ? r : std::sin (r);
        const float dL = d * lerp (1.0f + rp, 1.0f, Hw);
        z.pre = st5 == 3 ? d : (st5 == 4 ? lerp (d, dL, std::min (1.0f - b, 1.0f)) : dL);
        z.post = 1.0f / std::sqrt ((d - 1.0f) * (st5 == 1 ? 0.25f : 0.5f) + 1.0f);
        const float rr = st5 == 0 ? r : std::sin (kPif * 0.5f * r);
        z.k = lerp (0.001f, 4.1f, rr);
        z.invDen = 1.0f / (1.0f + z.k * z.G * z.G * z.G * z.G);
        return z;
    }
    void ladder (const float* in, float* out, int n, float f, float r, float d, int style, float bRaw) noexcept
    {
        if (!ladValid)
        {
            lad = ladderSetup (f, r, d, style, bRaw);
            ladValid = true;
        }
        ladderRun (lad, in, out, n);
    }
    void ladderRun (const LadderSetup& z, const float* in, float* out, int n) noexcept
    {
        const float G = z.G, k = z.k, pre = z.pre, post = z.post;
        const float c0 = z.c[0], c1 = z.c[1], c2 = z.c[2], c3 = z.c[3], c4 = z.c[4];
        for (int i = 0; i < n; ++i)
        {
            const float S = G * G * G * s[0] + G * G * s[1] + G * s[2] + s[3];
            const float u = th ((fastMath ? (pre * in[i] - k * S) * z.invDen : (pre * in[i] - k * S) / (1.0f + k * G * G * G * G)) * 0.9f) * 1.1f;
            float x = u, st[4];
            for (int j = 0; j < 4; ++j)
            {
                const float v = (x - s[j]) * G;
                const float y = v + s[j];
                s[j] = flushDenormal (y + v);
                st[j] = y;
                x = sat (y);
            }
            out[i] = (c0 * u + c1 * st[0] + c2 * st[1] + c3 * st[2] + c4 * st[3]) * post;
        }
    }
    /** The Ladder setup of parameters p, without touching the memo of block (). */
    LadderSetup ladderSetupFor (const ClassicParams& p) const noexcept
    {
        const float f = std::clamp (p.cutoffHz, 8.0f, static_cast<float> (sr) * 0.45f);
        const float d = p.fast ? fastmath::pow10 (p.driveDb * 0.05f) : std::pow (10.0f, p.driveDb * 0.05f);
        return ladderSetup (f, clamp01 (p.res), d, p.style, std::clamp (p.blendRaw, 0.0f, 2.0f) - 1.0f);
    }
    /** Tier low, Ladder with moving parameters: the setup (tan, sin, sqrt) is computed at the two ends of the period and its coefficients are interpolated linearly over the
        four pieces of 8 samples, instead of the cutoff by a power and the whole setup again for every piece. The end of one period is the start of the next. */
    void ladderRamp (const float* in, float* out, int n, const ClassicParams& a, const ClassicParams& b, int t0) noexcept
    {
        fastMath = true;
        if (!rampValid || !sameParams (rampP, a))
            rampA = ladderSetupFor (a);
        else
            rampA = rampB;
        rampB = ladderSetupFor (b);
        rampP = b;
        rampValid = true;
        for (int i = 0; i < n;)
        {
            const int pos = t0 + i;
            const int m = std::min (8 - (pos % 8), n - i);
            const float t = static_cast<float> ((pos / 8) * 8 + 8) / static_cast<float> (kControlPeriod);
            LadderSetup z;
            z.G = lerp (rampA.G, rampB.G, t);
            z.k = lerp (rampA.k, rampB.k, t);
            z.pre = lerp (rampA.pre, rampB.pre, t);
            z.post = lerp (rampA.post, rampB.post, t);
            z.invDen = lerp (rampA.invDen, rampB.invDen, t);
            for (int j = 0; j < 5; ++j)
                z.c[j] = lerp (rampA.c[j], rampB.c[j], t);
            ladderRun (z, in + i, out + i, m);
            i += m;
        }
    }
    LadderSetup rampA, rampB;
    ClassicParams rampP;
    bool rampValid = false;
public:
    /** Two channels of the Ladder model in one loop (see processPair). */
    static bool processPairLadder (ClassicFilter& A, ClassicFilter& B, const float* inA, float* outA, const float* inB, float* outB, int n, const ClassicParams& bA, const ClassicParams& bB) noexcept
    {
        A.remember (bA);
        B.remember (bB);
        if (!A.ladValid)
        {
            A.lad = A.ladderSetup (A.mf, A.mr, A.md, bA.style, A.mbraw - 1.0f);
            A.ladValid = true;
        }
        if (!B.ladValid)
        {
            B.lad = B.ladderSetup (B.mf, B.mr, B.md, bB.style, B.mbraw - 1.0f);
            B.ladValid = true;
        }
        const LadderSetup za = A.lad, zb = B.lad;
        for (int i = 0; i < n; ++i)
        {
            const float Sa = za.G * za.G * za.G * A.s[0] + za.G * za.G * A.s[1] + za.G * A.s[2] + A.s[3];
            const float Sb = zb.G * zb.G * zb.G * B.s[0] + zb.G * zb.G * B.s[1] + zb.G * B.s[2] + B.s[3];
            const float ua = A.th ((A.fastMath ? (za.pre * inA[i] - za.k * Sa) * za.invDen : (za.pre * inA[i] - za.k * Sa) / (1.0f + za.k * za.G * za.G * za.G * za.G)) * 0.9f) * 1.1f;
            const float ub = B.th ((B.fastMath ? (zb.pre * inB[i] - zb.k * Sb) * zb.invDen : (zb.pre * inB[i] - zb.k * Sb) / (1.0f + zb.k * zb.G * zb.G * zb.G * zb.G)) * 0.9f) * 1.1f;
            float xa = ua, xb = ub, sta[4], stb[4];
            for (int j = 0; j < 4; ++j)
            {
                const float va = (xa - A.s[j]) * za.G, vb = (xb - B.s[j]) * zb.G;
                const float ya = va + A.s[j], yb = vb + B.s[j];
                A.s[j] = flushDenormal (ya + va);
                B.s[j] = flushDenormal (yb + vb);
                sta[j] = ya;
                stb[j] = yb;
                xa = sat (ya);
                xb = sat (yb);
            }
            outA[i] = (za.c[0] * ua + za.c[1] * sta[0] + za.c[2] * sta[1] + za.c[3] * sta[2] + za.c[4] * sta[3]) * za.post;
            outB[i] = (zb.c[0] * ub + zb.c[1] * stb[0] + zb.c[2] * stb[1] + zb.c[3] * stb[2] + zb.c[4] * stb[3]) * zb.post;
        }
        return true;
    }
private:

    /** The diode ladder (document 05, 6): a two pole high pass in front (cutoff by the blend), four stages that average with their neighbours, a feedback of the
        fourth stage through a fixed 20 Hz high pass, the input stage tanh (0.5 (d' hp - k s4)). The stage dynamics are solved with the trapezoidal rule as a tridiagonal
        system (zero delay feedback inside the ladder); the saturators act on the old states. The corner is on the scale of the measured responses (kDiodeCorner). */
    static inline float kDiodeCorner = SB_DIODE_CORNER, kDiodeGain = SB_DIODE_GAIN, kDiodePre = 100.0f, kDiodeFb = SB_DIODE_FB, kDiodeW = 1.0f;
    void diode (const float* in, float* out, int n, float f, float r, float d, const ClassicParams& p) noexcept
    {
        const float k = 0.7f + 16.3f * r * r * r;
        const float dIn = (0.5f * k + 1.0f) * d, post = 1.0f / std::sqrt (d);
        const float fc = std::clamp (f * kDiodeCorner, 5.0f, static_cast<float> (sr) * 0.45f);
        const float g = std::tan (kPif * std::min (fc / static_cast<float> (sr), 0.499f));
        const float braw = clamp01 (p.blendRaw * 0.5f); // 0 .. 1
        // the high pass in front: 24 dB style: the corner moves from cutoff 2^-9 to 2^-1 with the blend; 12 dB style: fixed 2^-1, the amount is blend^2 / 4
        const bool style12 = p.style == 0;
        const float hpHz = f * std::exp2 (style12 ? -1.0f : -9.0f + 8.0f * braw);
        const float amount = style12 ? braw * braw * 0.25f : 1.0f;
        const float gh = std::tan (kPif * std::clamp (hpHz, 1.0f, static_cast<float> (sr) * 0.45f) / static_cast<float> (sr));
        const float gf = std::tan (kPif * 20.0f / static_cast<float> (sr));
        const float gp = std::tan (kPif * std::clamp (f * kDiodePre, 5.0f, static_cast<float> (sr) * 0.45f) / static_cast<float> (sr));
        const float inv = 1.0f / (1.0f + g);
        // tridiagonal matrix (constant per block): stage i takes w/2 of each neighbour and (1 - w) of the previous stage; w = 1 averages the neighbours
        const float w = kDiodeW, up = 0.5f * w, lo = 1.0f - 0.5f * w;
        const float bb = 1.0f + g;
        float cp[4];
        cp[0] = -g * up / bb;
        for (int j = 1; j < 4; ++j)
            cp[j] = (j < 3 ? -g * up : 0.0f) / (bb + g * lo * cp[j - 1]);
        (void) inv;
        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            // two pole high pass (two cascaded one pole trapezoidal sections)
            float h = x;
            for (int j = 0; j < 2; ++j)
            {
                const float v = (h - dHp[j]) * gh / (1.0f + gh);
                const float lp = v + dHp[j];
                dHp[j] = flushDenormal (lp + v);
                h = h - lp;
            }
            const float xin = lerp (x, h, amount);
            // 20 Hz high pass of the fed back fourth stage
            const float vf = (dv[3] - dFb) * gf / (1.0f + gf);
            const float lpf = vf + dFb;
            dFb = flushDenormal (lpf + vf);
            const float fb = dv[3] - lpf;
            float xp = xin;
            if (kDiodePre < 50.0f) // optional extra pole in front (calibration of the measured response)
            {
                dPre += gp / (1.0f + gp) * (xin - dPre);
                xp = dPre;
            }
            const float u = th (0.5f * (dIn * xp - k * kDiodeFb * fb)) * kDiodeGain;
            // right hand side with the old states (the neighbours through the algebraic saturator)
            const float s0 = sat (dv[0]), s1 = sat (dv[1]), s2 = sat (dv[2]), s3 = sat (dv[3]);
            const float in0 = up * (dU + s1) + (1.0f - w) * dU, in1 = up * (s0 + s2) + (1.0f - w) * s0, in2 = up * (s1 + s3) + (1.0f - w) * s1,
                        in3 = up * 0.0f + lo * s2;
            float rhs[4] = { (1.0f - g) * dv[0] + g * in0 + g * lo * u, (1.0f - g) * dv[1] + g * in1, (1.0f - g) * dv[2] + g * in2, (1.0f - g) * dv[3] + g * in3 };
            dU = u;
            float dp[4];
            dp[0] = rhs[0] / bb;
            for (int j = 1; j < 4; ++j)
                dp[j] = (rhs[j] + g * lo * dp[j - 1]) / (bb + g * lo * cp[j - 1]);
            float v[4];
            v[3] = dp[3];
            for (int j = 2; j >= 0; --j)
                v[j] = dp[j] - cp[j] * v[j + 1];
            for (int j = 0; j < 4; ++j)
                dv[j] = flushDenormal (v[j]);
            out[i] = dv[3] * post;
        }
    }
    float dv[4] = {}, dHp[2] = {}, dFb = 0.0f, dU = 0.0f, dPre = 0.0f, tilt = 0.0f;

    void formant (const float* in, float* out, int n, const ClassicParams& p) noexcept
    {
        // vowels: gain dB, resonance share, centre note per formant (document 05, section 9)
        static const float kV[5][4][3] = {
            { { -2, 0.66f, 75.755f }, { -8, 0.75f, 84.545f }, { -9, 1.0f, 100.085f }, { -10, 1.0f, 101.646f } },   // A
            { { 0, 0.66f, 67.350f }, { -14, 0.75f, 92.400f }, { -4, 1.0f, 99.755f }, { -14, 1.0f, 103.350f } },    // E
            { { 0, 0.80f, 61.783f }, { -15, 0.75f, 94.050f }, { -17, 1.0f, 101.038f }, { -20, 1.0f, 103.618f } },  // I
            { { -2, 0.70f, 67.350f }, { -6, 0.75f, 79.350f }, { -14, 1.0f, 99.755f }, { -14, 1.0f, 101.038f } },   // O
            { { 0, 0.70f, 65.038f }, { -20, 0.75f, 74.370f }, { -17, 1.0f, 100.409f }, { -14, 1.0f, 101.646f } } };// U
        static const int kStyle0[4] = { 0, 2, 4, 3 }, kStyle1[4] = { 0, 3, 2, 1 }, kStyle2[4] = { 1, 4, 3, 0 }; // corners (0,0) (1,0) (0,1) (1,1): A I U O / A O I E / E U O A
        const int* c = p.style == 0 ? kStyle0 : (p.style == 1 ? kStyle1 : kStyle2);
        const float tract = p.style >= 2 ? 1.25f : 1.0f; // the vocal tract style: the same four resonators, narrower, with a gentle spectral tilt (the glottal source)
        float gain[4], note[4], q[4];
        for (int k = 0; k < 4; ++k)
        {
            auto mixv = [&] (int comp)
            {
                const float b0 = lerp (kV[c[0]][k][comp], kV[c[1]][k][comp], p.fx), b1 = lerp (kV[c[2]][k][comp], kV[c[3]][k][comp], p.fx);
                return lerp (b0, b1, p.fy);
            };
            gain[k] = std::pow (10.0f, mixv (0) * 0.05f);
            float nt = mixv (2) + p.ftranspose;
            nt = lerp (nt, 80.0f, std::clamp (p.fspread, -1.0f, 1.0f));
            note[k] = nt;
            const float rr = clamp01 (p.fres * mixv (1));
            q[k] = (4.0f + 26.0f * rr * rr * rr) * tract;
        }
        // the four resonators keep their frequency and Q for the whole block: the coefficients (an exp2, a tan, a division each) are computed once, not for every sample
        Svf::Coef cf[4];
        float kq[4], bpGain[4];
        for (int k = 0; k < 4; ++k)
        {
            const float hz = 8.1757989f * std::exp2 (note[k] / 12.0f);
            kq[k] = 1.0f / q[k];
            cf[k] = Svf::coefficients (Svf::coefficient (hz, sr), kq[k]);
            bpGain[k] = 1.0f / q[k];
        }
        for (int i = 0; i < n; ++i)
        {
            float y = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                fm[k].step (in[i], cf[k], kq[k]);
                y += fm[k].bp * gain[k] * bpGain[k] * 8.0f;
            }
            if (p.style >= 2)
            {
                tilt += 0.35f * (y - tilt); // a one pole low pass mixed in: -6 dB per octave above 3 kHz like a glottal source
                y = 0.6f * y + 0.4f * tilt;
            }
            out[i] = y;
        }
    }

    void combFilter (const float* in, float* out, int n, float f, float r, const ClassicParams& p) noexcept
    {
        const float T = std::clamp (static_cast<float> (sr) / std::max (f, 20.0f), 2.0f, static_cast<float> (sr) * 0.09f);
        float fb = lerp (-1.0f, 1.0f, r);
        fb = fb / std::sqrt (std::fabs (fb) + 1.0e-5f);
        fb = std::clamp (fb, -0.98f, 0.98f);
        const float scale = 1.0f - 0.5f * fb * fb;
        const float loopHz = std::clamp (f * std::exp2 (p.combOffset / 12.0f), 20.0f, static_cast<float> (sr) * 0.45f);
        const float a = OnePole::coefficient (loopHz, sr);
        combUsed = true;
        for (int i = 0; i < n; ++i)
        {
            const float v = scale * in[i] + fb * comb.read (T - 1.0f);
            lpA += a * (v - lpA);
            lpA = flushDenormal (lpA);
            const float w = hardtanh (lpA);
            comb.push (w);
            out[i] = w;
        }
    }

    void phaserFilter (const float* in, float* out, int n, float f, float r, float d, const ClassicParams& p) noexcept
    {
        const float g = phaserStageG (f, sr);
        const float a = g / (1.0f + g);
        const float b = clamp01 (p.blendRaw);
        const float braw = std::clamp (p.blendRaw, 0.0f, 2.0f);
        const float p1 = clamp01 (1.0f - braw), p5 = clamp01 (braw - 1.0f), p3 = 1.0f - p1 - p5;
        const float inv = p.style == 1 ? -1.0f : 1.0f;
        (void) b;
        const float c1 = OnePole::coefficient (f / 20.0f, sr), c2 = OnePole::coefficient (f * 20.0f, sr);
        for (int i = 0; i < n; ++i)
        {
            float x = hardtanh (d * (1.0f + 0.5f * r) * in[i] + inv * fbState);
            float tap[3] = { 0, 0, 0 };
            for (int st = 0; st < 12; ++st)
            {
                const float v = (x - ap[st]) * a;
                const float lp = v + ap[st];
                ap[st] = flushDenormal (lp + v);
                x = x - 2.0f * lp;
                if (st == 3) tap[0] = x;
                if (st == 7) tap[1] = x;
                if (st == 11) tap[2] = x;
            }
            const float outAp = p1 * tap[0] + p3 * tap[1] + p5 * tap[2];
            lpA += c2 * (outAp - lpA);
            lpB += c1 * (lpA - lpB);
            fbState = flushDenormal (r * (lpA - lpB));
            out[i] = 0.5f * (in[i] + inv * outAp);
        }
    }

    double sr = 48000.0;
    static inline float kAn[6] = { 1.0f, 0.462f, 1.088f, 0.915f, 1.249f, 0.97f };
    float s[6] = {};
    Svf fm[4];
    DelayLine comb;
    bool combUsed = false;
    float ap[12] = {};
    float fbState = 0.0f, lpA = 0.0f, lpB = 0.0f;
};
} // namespace own
} // namespace synthyboy
