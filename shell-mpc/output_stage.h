// SynthyBoy - MPC shell: output stage. Make-up gain (smoothed over kGainSmoothingSeconds) followed by an optional stereo-linked peak limiter
// (SYNTHYBOY_MPC_OUTPUT_LIMITER, default OFF). When the limiter is OFF, samples pass with make-up gain only and may exceed 0 dBFS.
// When ON, the gain reduction falls at once to what keeps the louder channel at kCeiling (no overshoot, no look-ahead, no latency) and recovers
// with a one-pole of kReleaseSeconds. At 0 dB and with peaks under the ceiling the samples pass bit-identical. Non-finite input samples are
// treated as silence. No allocation, no lock: safe on the audio thread.
// Tested by shell-mpc/tests/output_stage_host.cpp (tests/run_output_stage_test.sh).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

// 32 bit ARM and AArch64 Linux (the MPC builds); SB_TEST_NEON builds the same intrinsics on any ARM (the host test); SB_NO_NEON forces the scalar loop.
#if (((defined(__arm__) || (defined(__aarch64__) && defined(__linux__))) && defined(__ARM_NEON)) || defined(SB_TEST_NEON)) && ! defined(SB_NO_NEON)
 #include <arm_neon.h>
 #define SYNTHYBOY_OUT_NEON 1
#else
 #define SYNTHYBOY_OUT_NEON 0
#endif

#ifndef SYNTHYBOY_MPC_OUTPUT_LIMITER
#define SYNTHYBOY_MPC_OUTPUT_LIMITER 0
#endif

namespace synthyboy_mpc {

template <bool LimiterEnabled = (SYNTHYBOY_MPC_OUTPUT_LIMITER != 0)>
class BasicOutputStage {
public:
    static constexpr bool kLimiterEnabled = LimiterEnabled;
    static constexpr float kCeiling = 0.891251f, kReleaseSeconds = 0.120f, kGainSmoothingSeconds = 0.020f;
    BasicOutputStage() noexcept { prepare (44100.0); }
    void prepare (double sr) noexcept {
        const double safeSr = (std::isfinite (sr) && sr > 0.0) ? sr : 44100.0;
        relCoef = std::exp (-1.0f / (kReleaseSeconds * (float) safeSr));
        smCoef = std::exp (-1.0f / (kGainSmoothingSeconds * (float) safeSr));
        reset();
    }
    void reset() noexcept { env = 1.0f; curGain = targetGain; }
    void setGainDb (float db) noexcept { if (! std::isfinite (db)) db = 0.0f; gDb = std::clamp (db, -24.0f, 24.0f); targetGain = gDb == 0.0f ? 1.0f : std::pow (10.0f, gDb / 20.0f); }
    float gainDb() const noexcept { return gDb; }
    float gainReductionDb() const noexcept {
        if constexpr (! LimiterEnabled) {
            return 0.0f;
        } else {
            if (env >= 1.0f) return 0.0f;
            const float safeEnv = std::max (env, 1e-6f);
            return -20.0f * std::log10 (safeEnv);
        }
    }
    /** true: the scalar per sample loop for every block (the reference the NEON form is tested against, tests/output_stage_host.cpp). */
    bool scalarOnly = false;
    void process (float* l, float* r, int n) noexcept {
        if (n <= 0 || (l == nullptr && r == nullptr)) return;
        int i = 0;
#if SYNTHYBOY_OUT_NEON
        if (! scalarOnly) i = processLanes (l, r, n);
#endif
        for (; i < n; ++i)
            sampleScalar (l, r, i);
    }
private:
    /** One sample of the stage (the loop body of the plain version). Also what the NEON form falls back to wherever its conditions do not hold. */
    void sampleScalar (float* l, float* r, int i) noexcept {
        if (curGain != targetGain) {
            curGain = targetGain + (curGain - targetGain) * smCoef;
            if (std::fabs (curGain - targetGain) < 1e-6f * targetGain)
                curGain = targetGain;
        }
        float a = (l != nullptr) ? l[i] : 0.0f;
        float b = (r != nullptr) ? r[i] : 0.0f;
        if (! std::isfinite (a)) a = 0.0f;
        if (! std::isfinite (b)) b = 0.0f;
        a *= curGain;
        b *= curGain;
        if (! std::isfinite (a)) a = 0.0f;
        if (! std::isfinite (b)) b = 0.0f;

        if constexpr (! LimiterEnabled) {
            if (l != nullptr) l[i] = a;
            if (r != nullptr) r[i] = b;
        } else {
            const float pk = std::max (std::fabs (a), std::fabs (b));
            const float tgt = pk > kCeiling ? kCeiling / pk : 1.0f;
            if (tgt < env)
                env = tgt;
            else {
                env = 1.0f + (env - 1.0f) * relCoef;
                if (env > 0.99999f) env = 1.0f;
                if (env > tgt) env = tgt;
            }
            if (l != nullptr) {
                float outL = env == 1.0f ? a : a * env;
                if (! std::isfinite (outL)) outL = 0.0f;
                l[i] = outL;
            }
            if (r != nullptr) {
                float outR = env == 1.0f ? b : b * env;
                if (! std::isfinite (outR)) outR = 0.0f;
                r[i] = outR;
            }
        }
    }
#if SYNTHYBOY_OUT_NEON
    /** x with a non finite lane (NaN, infinity: the magnitude bits are not below those of infinity) turned into +0, multiplied by g, and a result that overflowed to infinity turned into +0:
        the two isfinite tests and the multiply of sampleScalar, four samples at a time. The multiply is always made (also by 1): in flush to zero mode it flushes a subnormal like the scalar one. */
    static float32x4_t finiteMul (float32x4_t x, float32x4_t g) noexcept {
        const uint32x4_t inf = vdupq_n_u32 (0x7f800000u), absMask = vdupq_n_u32 (0x7fffffffu);
        const uint32x4_t fin = vcltq_u32 (vandq_u32 (vreinterpretq_u32_f32 (x), absMask), inf);
        const float32x4_t y = vmulq_f32 (vreinterpretq_f32_u32 (vandq_u32 (vreinterpretq_u32_f32 (x), fin)), g);
        const uint32x4_t fin2 = vcltq_u32 (vandq_u32 (vreinterpretq_u32_f32 (y), absMask), inf);
        return vreinterpretq_f32_u32 (vandq_u32 (vreinterpretq_u32_f32 (y), fin2));
    }
    static uint32_t bitsOf (float c) noexcept { uint32_t u; std::memcpy (&u, &c, 4); return u; }
    /** The blocks of four samples where the stage is in its steady state: the gain has settled (the smoothing runs sample by sample first), and for the limiter version the reduction is off (env == 1) and no
        sample after the gain is above the ceiling, so every output sample is the gained one and env stays 1; any other group of four goes through sampleScalar. Returns the number of samples done. */
    int processLanes (float* l, float* r, int n) noexcept {
        int i = 0;
        while (i < n && curGain != targetGain) {
            sampleScalar (l, r, i);
            ++i;
        }
        const float32x4_t g = vdupq_n_f32 (curGain), zero = vdupq_n_f32 (0.0f);
        for (; i + 4 <= n; i += 4) {
            const float32x4_t a = l != nullptr ? finiteMul (vld1q_f32 (l + i), g) : finiteMul (zero, g);
            const float32x4_t b = r != nullptr ? finiteMul (vld1q_f32 (r + i), g) : finiteMul (zero, g);
            if constexpr (LimiterEnabled) {
                const uint32x4_t absMask = vdupq_n_u32 (0x7fffffffu), c = vdupq_n_u32 (bitsOf (kCeiling));
                const uint32x4_t under = vandq_u32 (vcleq_u32 (vandq_u32 (vreinterpretq_u32_f32 (a), absMask), c), vcleq_u32 (vandq_u32 (vreinterpretq_u32_f32 (b), absMask), c));
                const uint32x2_t h = vand_u32 (vget_low_u32 (under), vget_high_u32 (under));
                if (env != 1.0f || (vget_lane_u32 (h, 0) & vget_lane_u32 (h, 1)) == 0u) {
                    for (int k = 0; k < 4; ++k)
                        sampleScalar (l, r, i + k);
                    continue;
                }
            }
            if (l != nullptr) vst1q_f32 (l + i, a);
            if (r != nullptr) vst1q_f32 (r + i, b);
        }
        return i;
    }
#endif
    float gDb = 0.0f, targetGain = 1.0f, curGain = 1.0f, env = 1.0f;
    float relCoef = 0.99981105f, smCoef = 0.99886686f;
};

using OutputStage = BasicOutputStage<>;

// ---- the limiter as a plugin setting (docs/MPC.md "Output limiter") ----
/** The default of the setting "LIMIT" on the Master page (OUTPUT block): the one constant to flip. A project saved before the setting existed keeps this default. */
constexpr bool kOutputLimiterDefaultOn = false;

/** SYNTHYBOY_MPC_OUTPUT_LIMITER in the environment forces the limiter for tests: "1" on, "0" off, anything else (or unset) leaves it to the setting. A build with
    -DSYNTHYBOY_MPC_OUTPUT_LIMITER=1 forces it on as before (the value 0 of the default build forces nothing). Returns -1 (not forced), 0 (off) or 1 (on). */
inline int outputLimiterForced() noexcept
{
    constexpr bool kBuildForcesOn = (SYNTHYBOY_MPC_OUTPUT_LIMITER != 0);
    const char* e = std::getenv ("SYNTHYBOY_MPC_OUTPUT_LIMITER");
    if (e != nullptr && e[0] == '0' && e[1] == '\0') return 0;
    if (kBuildForcesOn || (e != nullptr && e[0] == '1' && e[1] == '\0')) return 1;
    return -1;
}

/** The output stage with the limiter switchable while running: two stages (make-up gain only, and make-up gain + limiter) of which the audio thread runs the one that is wanted.
    setLimiter () is lock free from any thread; the audio thread takes the change over at the start of its next process () call and restarts the stage it switches to (gain at
    its target, no reduction), so a switch is at most a step of the make-up gain's smoothing. Everything else is the BasicOutputStage the tests know. */
class SwitchableOutputStage {
public:
    void prepare (double sr) noexcept { plain.prepare (sr); limited.prepare (sr); }
    void reset() noexcept { plain.reset(); limited.reset(); }
    void setGainDb (float db) noexcept { plain.setGainDb (db); limited.setGainDb (db); }
    float gainDb() const noexcept { return plain.gainDb(); }
    void setLimiter (bool on) noexcept { wanted.store (on, std::memory_order_relaxed); }
    bool limiter() const noexcept { return wanted.load (std::memory_order_relaxed); }
    float gainReductionDb() const noexcept { return running.load (std::memory_order_relaxed) ? limited.gainReductionDb() : 0.0f; }
    void process (float* l, float* r, int n) noexcept {
        const bool on = wanted.load (std::memory_order_relaxed);
        if (on != running.load (std::memory_order_relaxed)) {
            if (on) limited.reset(); else plain.reset();
            running.store (on, std::memory_order_relaxed);
        }
        if (on) limited.process (l, r, n); else plain.process (l, r, n);
    }
private:
    BasicOutputStage<false> plain;
    BasicOutputStage<true> limited;
    std::atomic<bool> wanted { kOutputLimiterDefaultOn }, running { false };
};

/** The chunk with the member "mpcOutputLimiter":0 / 1 before the last closing brace (the members of the pad pressure follow it: they are read from the very end). */
inline std::string chunkWithOutputLimiter (std::string chunk, bool on)
{
    const size_t close = chunk.rfind ('}');
    if (close == std::string::npos)
        return chunk;
    chunk.insert (close, on ? ",\"mpcOutputLimiter\":1" : ",\"mpcOutputLimiter\":0");
    return chunk;
}

/** Reads the member from the last 240 bytes of a chunk. True when it is there (then `on` is set); false leaves `on` as it is. Never reads outside `size`. */
inline bool outputLimiterFromChunk (const void* data, size_t size, bool& on) noexcept
{
    if (data == nullptr || size < 24)
        return false;
    const size_t tailLen = std::min<size_t> (size, 240);
    const std::string tail (static_cast<const char*> (data) + (size - tailLen), tailLen);
    static const char kKey[] = "\"mpcOutputLimiter\":";
    const size_t a = tail.rfind (kKey);
    if (a == std::string::npos)
        return false;
    const size_t p = a + sizeof (kKey) - 1;
    if (p + 1 >= tail.size() || (tail[p] != '0' && tail[p] != '1') || (tail[p + 1] != ',' && tail[p + 1] != '}'))
        return false;
    on = tail[p] == '1';
    return true;
}

} // namespace synthyboy_mpc
