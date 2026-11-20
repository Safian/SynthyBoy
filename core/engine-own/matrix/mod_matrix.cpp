// SynthyBoy - own engine: the modulation matrix.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include "mod_matrix.h"
#include "../dsp/fastmath.h"

#include <cstring>
#include <vector>

#include "../dsp/util.h"
#include "synthyboy/shapes/shape.h"

namespace synthyboy
{
namespace own
{
namespace
{
bool startsWith (const char* s, const char* prefix) noexcept
{
    return std::strncmp (s, prefix, std::strlen (prefix)) == 0;
}

/** Per voice parameters: everything the voice reads; the rest (master, effects) is modulated by the global sources only. */
bool isVoiceParam (const ParamInfo& info) noexcept
{
    static const char* const prefixes[] = { "osc", "sub.", "noise.", "filter1.", "filter2.", "env", "lfo", "rlfo", "mod" };
    for (const char* p : prefixes)
        if (startsWith (info.key, p))
            return true;
    return false;
}
} // namespace

namespace
{
bool endsWith (const char* s, const char* suffix) noexcept
{
    const size_t n = std::strlen (s), m = std::strlen (suffix);
    return n >= m && std::strcmp (s + n - m, suffix) == 0;
}

DestLaw computeLaw (const ParamInfo& info) noexcept
{
    DestLaw law;
    const char* k = info.key;
    const float range = info.max - info.min;
    if (std::strcmp (k, "master.volume") == 0)
    {
        law.kind = DestKind::volume;
        law.width = 7399.44f;
        return law;
    }
    // envelope times: fourth root of seconds
    if (startsWith (k, "env") && (info.unit == Unit::ms || endsWith (k, "_beats")))
    {
        law.kind = DestKind::quart;
        law.scale = info.unit == Unit::ms ? 0.001f : 1.0f; // seconds or beats
        law.width = (endsWith (k, "delay") || endsWith (k, "hold") || endsWith (k, "delay_beats") || endsWith (k, "hold_beats")) ? 1.41421f : 2.37842f;
        return law;
    }
    if (std::strcmp (k, "master.portamento.time") == 0 || endsWith (k, ".smooth_time"))
    {
        law.kind = DestKind::log2Seconds;
        law.width = std::strcmp (k, "master.portamento.time") == 0 ? 14.0f : 14.0f;
        return law;
    }
    if (info.unit == Unit::Hz && (endsWith (k, ".rate")))
    {
        law.kind = DestKind::log2Hz;
        law.width = std::log2 (info.max / info.min);
        return law;
    }
    if (info.unit == Unit::Hz && (endsWith (k, "cutoff") || endsWith (k, ".center")))
    {
        law.kind = DestKind::note;
        law.width = 128.0f;
        return law;
    }
    if ((endsWith (k, ".level") && info.unit == Unit::percent) || endsWith (k, "unison.detune") || (endsWith (k, ".resonance") && startsWith (k, "eq.")))
    {
        law.kind = DestKind::sqrtLevel;
        law.scale = 0.01f;
        law.width = endsWith (k, "unison.detune") ? 10.0f : 1.0f;
        return law;
    }
    law.kind = DestKind::lin;
    switch (info.unit)
    {
        case Unit::percent: law.scale = 0.01f; break;
        case Unit::degrees: law.scale = 1.0f / 360.0f; break;
        case Unit::cents: law.scale = 0.01f; break;
        default: law.scale = 1.0f; break;
    }
    law.width = range * law.scale;
    if (endsWith (k, ".frame") && startsWith (k, "osc"))
    {
        law.scale = 0.63f;
        law.width = 63.0f;
    }
    else if (endsWith (k, ".frame_spread"))
    {
        law.scale = 0.63f;
        law.width = 126.0f;
    }
    return law;
}
} // namespace

const DestLaw& destLawOf (size_t paramIndex) noexcept
{
    static const std::vector<DestLaw> laws = []
    {
        std::vector<DestLaw> v (paramCount());
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = computeLaw (paramTable()[i]);
        return v;
    }();
    return laws[paramIndex < laws.size() ? paramIndex : 0];
}

float modulatedValue (const ParamInfo& info, const DestLaw& law, float base, float delta, bool fast) noexcept
{
    // tier low (fast): log2 / exp2 by the polynomials of dsp/fastmath.h (absolute error below 6e-8 in log2, below 1e-8 relative in exp2)
    auto lg2 = [fast] (float x) noexcept { return fast ? fastmath::log2 (x) : std::log2 (x); };
    auto ex2 = [fast] (float x) noexcept { return fast ? fastmath::exp2 (x) : std::exp2 (x); };
    switch (law.kind)
    {
        case DestKind::volume:
        {
            const float db0 = std::min (base, 6.0f);
            const float c = (db0 <= -60.0f ? 0.0f : (db0 + 80.0f) * (db0 + 80.0f)) + delta;
            return c <= 0.0f ? -120.0f : std::sqrt (c) - 80.0f;
        }
        case DestKind::sqrtLevel:
        {
            const float c = std::sqrt (std::max (base * 0.01f * (law.width > 1.5f ? 100.0f : 1.0f), 0.0f)) + delta; // detune: sqrt of the percent
            const float v = c > 0.0f ? c * c : 0.0f;
            return law.width > 1.5f ? v : v * 100.0f;
        }
        case DestKind::quart:
        {
            const float c = std::sqrt (std::sqrt (std::max (base, 0.0f) * law.scale)) + delta;
            const float c2 = c > 0.0f ? c * c : 0.0f;
            return c2 * c2 / law.scale;
        }
        case DestKind::log2Seconds:
        {
            const float c = lg2 (std::max (base, 1.0e-3f) * 0.001f) + delta;
            return ex2 (std::clamp (c, -30.0f, 10.0f)) * 1000.0f;
        }
        case DestKind::log2Hz:
        {
            const float c = lg2 (std::max (base, 1.0e-4f)) + delta;
            return ex2 (std::clamp (c, -14.0f, 14.0f));
        }
        case DestKind::note:
        {
            const float c = 12.0f * lg2 (std::max (base, 1.0f) * (1.0f / 8.1757989156f)) + delta;
            return 8.1757989156f * ex2 (std::clamp (c, 0.0f, 160.0f) * (1.0f / 12.0f));
        }
        default:
            return base + delta / law.scale;
    }
    (void) info;
}

namespace
{
/** The node of the dependency graph that owns a destination key (a modulator or the amount of a slot), -1 if the key is not a modulator. */
int ownerNode (const ParamInfo& info) noexcept
{
    const char* k = info.key;
    auto num = [&] (const char* prefix, int digits) -> int
    {
        const size_t n = std::strlen (prefix);
        if (std::strncmp (k, prefix, n) != 0)
            return -1;
        int v = 0, d = 0;
        for (const char* q = k + n; *q >= '0' && *q <= '9'; ++q, ++d)
            v = v * 10 + (*q - '0');
        return d > 0 && d <= digits ? v : -1;
    };
    int n;
    if (std::strncmp (k, "rlfo", 4) == 0 && (n = num ("rlfo", 1)) >= 1 && n <= 4)
        return srcRlfo (n - 1);
    if (std::strncmp (k, "lfo", 3) == 0 && (n = num ("lfo", 2)) >= 1 && n <= 10)
        return srcLfo (n - 1);
    if (std::strncmp (k, "env", 3) == 0 && (n = num ("env", 1)) >= 1 && n <= 6)
        return srcEnv (n - 1);
    if (std::strncmp (k, "macro.", 6) == 0 && (n = num ("macro.", 1)) >= 1 && n <= 8)
        return srcMacro (n - 1);
    if (std::strncmp (k, "mod", 3) == 0 && endsWith (k, ".amount") && (n = num ("mod", 2)) >= 1 && n <= 64)
        return 100 + n - 1;
    return -1;
}

/** The dependency graph of the modulators as a plain edge list (no allocation: compile () runs on the audio thread). */
struct EdgeList
{
    int from[256], to[256];
    int n = 0;
    void add (int a, int b) noexcept
    {
        if (n < 256)
        {
            from[n] = a;
            to[n] = b;
            ++n;
        }
    }
    /** True if `target` can be reached from `start` along the edges (source -> dependent). */
    bool reaches (int start, int target) const noexcept
    {
        if (start == target)
            return true;
        bool seen[200] = {};
        int stack[200], sp = 0;
        stack[sp++] = start;
        seen[start] = true;
        while (sp > 0)
        {
            const int u = stack[--sp];
            for (int e = 0; e < n; ++e)
                if (from[e] == u && !seen[to[e]])
                {
                    if (to[e] == target)
                        return true;
                    seen[to[e]] = true;
                    stack[sp++] = to[e];
                }
        }
        return false;
    }
};
} // namespace

void ModMatrix::compile (const float* base) noexcept
{
    const ParamIndex& pi = paramIndex();
    const ParamInfo* table = paramTable();
    count = 0;
    srcMask = globalSrcMask = 0;
    hasPolyAftertouchToCutoff = false;
    const int cutIdx = pi.filter[0].cutoff;
    EdgeList adj;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const ModSlotParams& sp = pi.slot[s];
        if (sp.source < 0 || sp.destination < 0 || sp.amount < 0)
            continue;
        const int src = static_cast<int> (base[sp.source] + 0.5f);
        const int dstChoice = static_cast<int> (base[sp.destination] + 0.5f);
        if (src <= 0 || src >= kNumSourceIds || dstChoice <= 0 || (sp.bypass >= 0 && base[sp.bypass] >= 0.5f))
            continue;
        const int dest = modDestinationParamIndex (static_cast<size_t> (dstChoice));
        if (dest < 0)
            continue;
        const int aux = sp.auxSource >= 0 ? std::clamp (static_cast<int> (base[sp.auxSource] + 0.5f), 0, kNumSourceIds - 1) : 0;
        if (dest == cutIdx && (src == kSrcPolyAftertouch || aux == kSrcPolyAftertouch))
            hasPolyAftertouchToCutoff = true;
        // a connection that would close a loop is not wired (document 04, 6)
        const int owner = ownerNode (table[dest]);
        if (owner >= 0)
        {
            const int me = 100 + s;
            if (adj.reaches (owner, src) || (aux != 0 && adj.reaches (owner, aux)) || owner == me || adj.reaches (owner, me))
                continue;
            adj.add (src, me);
            if (aux != 0)
                adj.add (aux, me);
            adj.add (me, owner);
        }
        CompiledSlot& c = slots[count++];
        c = CompiledSlot();
        c.index = s;
        c.source = src;
        c.dest = dest;
        c.amount = std::clamp (base[sp.amount] * 0.01f, -1.0f, 1.0f);
        c.power = sp.power >= 0 ? base[sp.power] : 0.0f;
        c.bipolar = sp.bipolar >= 0 && base[sp.bipolar] >= 0.5f;
        c.stereo = sp.stereo >= 0 && base[sp.stereo] >= 0.5f;
        c.aux = aux;
        c.auxInv = sp.auxInv >= 0 && base[sp.auxInv] >= 0.5f;
        c.auxPower = sp.auxPower >= 0 ? base[sp.auxPower] : 0.0f;
        c.auxOut = sp.auxOutput >= 0 ? base[sp.auxOutput] * 0.01f : 1.0f;
        c.global = !isVoiceParam (table[dest]);
        c.width = destLawOf (static_cast<size_t> (dest)).width;
        c.riseMs = sp.rise >= 0 ? base[sp.rise] : 0.0f;
        c.fallMs = sp.fall >= 0 ? base[sp.fall] : 0.0f;
        c.delayMs = sp.delay >= 0 ? base[sp.delay] : 0.0f;
        c.delayBeats = sp.delayBeats >= 0 ? base[sp.delayBeats] : 0.0f;
        c.delaySync = sp.delaySync >= 0 && base[sp.delaySync] >= 0.5f;
        if (owner >= 100 && owner < 164)
        {
            c.amountDest = true;
            c.amountSlot = owner - 100;
        }
        c.curve = (content != nullptr && content->modCurve[static_cast<size_t> (s)].present) ? &content->modCurve[static_cast<size_t> (s)] : nullptr;
        c.hasPower = !(std::fabs (-c.power) < 1.0e-4f);
        c.powerDen = c.hasPower ? std::expm1 (-c.power) : 1.0f;
        c.hasAuxPower = c.auxPower != 0.0f && !(std::fabs (-c.auxPower) < 1.0e-4f);
        c.auxPowerDen = c.hasAuxPower ? std::expm1 (-c.auxPower) : 1.0f;
        c.slewCached = false;
        // V4: with a source x <= 0 (+-0, negative, -inf; not NaN) in a lane, one () computes clamp01 (x) = +-0 (no curve, no slew: the test of one () itself), law = copysign
        // (powerShape (|+-0|, -power), +-0): +0 for |power| < 1e-4, else expm1 (+-0) / expm1 (-power) = +-0 for every finite power (expm1 of a finite value is finite, -1 or +inf, and not 0
        // for |power| >= 1e-4); not bipolar (2x - 1 would be -1). Then +-0 * amount * width * aux * gate is +-0 when the other factors are finite: the amount (checked per evaluation:
        // not NaN, it is clamped), the width, the gate (0 or 1) and the aux factor: clamp01 (a) of an aux value a that is not NaN is in [0, 1] (or -0), powerShape (v, -auxPower) of it
        // is finite for a finite |auxPower| <= 80 (expm1 (p v) <= expm1 (80) for v <= 1, and the denominator is not 0), 1 - v is finite, and so is v * auxOut for a finite auxOut. The
        // output +-0 of both lanes is then skipped by the tests after one () anyway (dl == 0 && dr == 0; in the amount loop a +-0 added to a sum that started at +0 changes nothing).
        c.zeroSkip = c.curve == nullptr && !c.bipolar && !(c.riseMs > 0.0f || c.fallMs > 0.0f) && std::isfinite (c.power) && std::isfinite (c.width)
                     && (aux == 0 || (std::isfinite (c.auxPower) && std::fabs (c.auxPower) <= 80.0f && std::isfinite (c.auxOut)));
        const uint64_t bits = (uint64_t (1) << src) | (aux != 0 ? uint64_t (1) << aux : uint64_t (0));
        srcMask |= bits;
        if (c.global)
            globalSrcMask |= bits;
    }
    // the points of a custom LFO shape that follow a source read that source too
    if (content != nullptr)
        for (const CompiledShape& cs : content->lfo)
            if (cs.present && cs.anyMod)
                for (int i = 0; i < cs.num; ++i)
                {
                    const CompiledPoint& pt = cs.points[static_cast<size_t> (i)];
                    for (const int source : { pt.x.source, pt.y.source, pt.curve.source })
                        if (source > 0 && source < kNumSourceIds)
                            srcMask |= uint64_t (1) << source;
                }
}

void ModMatrix::evaluate (const EvalContext& ctx) const noexcept
{
    // the amounts of the slots that are modulated by other slots (a slot on `modNN.amount`): first, then everything else
    float amountDeltaL[kNumModSlots], amountDeltaR[kNumModSlots];
    bool anyAmountDest = false;
    for (int i = 0; i < count; ++i)
        anyAmountDest = anyAmountDest || slots[i].amountDest;
    if (anyAmountDest)
        for (int i = 0; i < kNumModSlots; ++i)
            amountDeltaL[i] = amountDeltaR[i] = 0.0f;

    const float* srcR = ctx.srcR != nullptr ? ctx.srcR : ctx.src;
    auto allowed = [&] (const CompiledSlot& c)
    {
        const uint64_t bits = (uint64_t (1) << c.source) | (c.aux != 0 ? uint64_t (1) << c.aux : uint64_t (0));
        return (bits & ~ctx.sourceMask) == 0;
    };
    // V4: a slot whose output is +-0 in both lanes (CompiledSlot::zeroSkip, the source <= 0 in both lanes, finite amounts, an aux source that is not NaN) adds nothing: not evaluated
    auto zeroOut = [&] (const CompiledSlot& c, float amL, float amR)
    {
        return !ctx.reference && c.zeroSkip && ctx.src[c.source] <= 0.0f && srcR[c.source] <= 0.0f && amL == amL && amR == amR
               && (c.aux == 0 || (ctx.src[c.aux] == ctx.src[c.aux] && srcR[c.aux] == srcR[c.aux]));
    };
    auto bitsOf = [] (float x) noexcept
    {
        uint32_t u;
        std::memcpy (&u, &x, sizeof u);
        return u;
    };
    auto one = [&] (const CompiledSlot& c, float amountL, float amountR, float& outL, float& outR)
    {
        float xl = clamp01 (ctx.src[c.source]), xr = clamp01 (srcR[c.source]);
        // V9: a lane whose inputs have the same bits as the left one's gives the same bits (the same expressions); the reference computes it all the same
        const bool sameSource = !ctx.reference && bitsOf (xl) == bitsOf (xr);
        if (c.curve != nullptr)
        {
            auto curved = [&] (float x) { return std::clamp (shapes::ShapeData::evaluateTable (c.curve->table.data(), c.curve->table.size(), x), -1.0f, 1.0f); };
            xl = curved (xl);
            xr = sameSource ? xl : curved (xr);
        }
        if (ctx.state != nullptr && (c.riseMs > 0.0f || c.fallMs > 0.0f))
        {
            SlotState& st = ctx.state[c.index];
            if (!st.init)
            {
                st.heldL = xl;
                st.heldR = xr;
                st.init = true;
            }
            if (!ctx.reference)
            {
                const uint32_t dtBits = bitsOf (ctx.dt);
                if (!c.slewCached || dtBits != c.slewDtBits)
                {
                    c.riseAlpha = c.riseMs > 0.0f ? (1.0f - std::exp (-ctx.dt * 2.302585f / (c.riseMs * 0.001f))) : 0.0f;
                    c.fallAlpha = c.fallMs > 0.0f ? (1.0f - std::exp (-ctx.dt * 2.302585f / (c.fallMs * 0.001f))) : 0.0f;
                    c.slewDtBits = dtBits;
                    c.slewCached = true;
                }
                auto slew = [&] (float held, float x)
                {
                    if (x > held)
                        return c.riseMs > 0.0f ? held + (x - held) * c.riseAlpha : x;
                    return c.fallMs > 0.0f ? held + (x - held) * c.fallAlpha : x;
                };
                st.heldL = slew (st.heldL, xl);
                st.heldR = slew (st.heldR, xr);
            }
            else
            {
                auto slew = [&] (float held, float x)
                {
                    const float t = x > held ? c.riseMs : c.fallMs;
                    if (!(t > 0.0f))
                        return x;
                    return held + (x - held) * (1.0f - std::exp (-ctx.dt * 2.302585f / (t * 0.001f)));
                };
                st.heldL = slew (st.heldL, xl);
                st.heldR = slew (st.heldR, xr);
            }
            xl = st.heldL;
            xr = st.heldR;
        }
        auto law = [&] (float x)
        {
            if (ctx.reference)
            {
                if (c.bipolar)
                {
                    const float y = 2.0f * x - 1.0f;
                    return 0.5f * std::copysign (powerShape (std::fabs (y), -c.power), y); // swings +-0.5 of the full range
                }
                return std::copysign (powerShape (std::fabs (x), -c.power), x);
            }
            auto shaped = [&] (float u) noexcept { return c.hasPower ? std::expm1 (-c.power * u) / c.powerDen : u; };
            if (c.bipolar)
            {
                const float y = 2.0f * x - 1.0f;
                return 0.5f * std::copysign (shaped (std::fabs (y)), y);
            }
            return std::copysign (shaped (std::fabs (x)), x);
        };
        float gate = 1.0f;
        const float delay = c.delaySync ? c.delayBeats * static_cast<float> (60.0 / std::max (ctx.bpm, 1.0)) : c.delayMs * 0.001f;
        if (ctx.state != nullptr && delay > 0.0f && ctx.voiceAge < delay)
            gate = 0.0f;
        float auxL = 1.0f, auxR = 1.0f;
        auto a = [&] (const float* s)
        {
            float v = clamp01 (s[c.aux]);
            if (ctx.reference)
            {
                if (c.auxPower != 0.0f)
                    v = powerShape (v, -c.auxPower);
            }
            else
            {
                if (c.hasAuxPower)
                    v = std::expm1 (-c.auxPower * v) / c.auxPowerDen;
            }
            if (c.auxInv)
                v = 1.0f - v;
            return v * c.auxOut;
        };
        if (!ctx.reference && bitsOf (xl) == bitsOf (xr) && bitsOf (amountL) == bitsOf (amountR) && (c.aux == 0 || bitsOf (ctx.src[c.aux]) == bitsOf (srcR[c.aux])))
        {
            if (c.aux != 0)
                auxL = a (ctx.src);
            outL = law (xl) * amountL * c.width * auxL * gate;
            outR = outL;
        }
        else
        {
            if (c.aux != 0)
            {
                auxL = a (ctx.src);
                auxR = a (srcR);
            }
            outL = law (xl) * amountL * c.width * auxL * gate;
            outR = law (xr) * amountR * c.width * auxR * gate;
        }
        if (c.stereo)
            outR = -outR; // the stereo flag turns the right lane around (document 04, 3.2)
    };
    if (anyAmountDest)
        for (int i = 0; i < count; ++i)
        {
            const CompiledSlot& c = slots[i];
            if (!c.amountDest || c.amount == 0.0f || !allowed (c) || zeroOut (c, c.amount, c.amount))
                continue;
            float dl, dr;
            one (c, c.amount, c.amount, dl, dr);
            // the control domain of an amount is the fraction (width 2): the sum is added to the amount of the target
            amountDeltaL[c.amountSlot] += dl;
            amountDeltaR[c.amountSlot] += dr;
        }
    for (int i = 0; i < count; ++i)
    {
        const CompiledSlot& c = slots[i];
        if (c.amountDest)
            continue;
        if (c.global != ctx.global || !allowed (c))
            continue;
        float amL = c.amount, amR = c.amount;
        if (anyAmountDest)
        {
            amL = std::clamp (amL + amountDeltaL[c.index], -1.0f, 1.0f);
            amR = std::clamp (amR + amountDeltaR[c.index], -1.0f, 1.0f);
        }
        if ((amL == 0.0f && amR == 0.0f) || zeroOut (c, amL, amR))
            continue;
        float dl, dr;
        one (c, amL, amR, dl, dr);
        if (dl == 0.0f && dr == 0.0f)
            continue;
        if (ctx.delta[c.dest] == 0.0f && (ctx.deltaRight == nullptr || ctx.deltaRight[c.dest] == 0.0f) && *ctx.numTouched < ctx.maxTouched)
            ctx.touched[(*ctx.numTouched)++] = c.dest;
        ctx.delta[c.dest] += dl;
        if (ctx.deltaRight != nullptr)
            ctx.deltaRight[c.dest] += dr;
    }
}
} // namespace own
} // namespace synthyboy
