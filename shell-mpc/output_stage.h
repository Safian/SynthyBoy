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
    void process (float* l, float* r, int n) noexcept {
        if (n <= 0 || (l == nullptr && r == nullptr)) return;
        for (int i = 0; i < n; ++i) {
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
    }
private:
    float gDb = 0.0f, targetGain = 1.0f, curGain = 1.0f, env = 1.0f;
    float relCoef = 0.99981105f, smCoef = 0.99886686f;
};

using OutputStage = BasicOutputStage<>;

// ---- the limiter as a plugin setting (docs/MPC.md "Output limiter") ----
/** The default of the setting "LIMIT" on the Master page (OUTPUT block): the one constant to flip. A project saved before the setting existed keeps this default. */
constexpr bool kOutputLimiterDefaultOn = true;

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
