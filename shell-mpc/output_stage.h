// SynthyBoy - MPC shell: output stage. Make-up gain (smoothed over kGainSmoothingSeconds) followed by a stereo-linked peak limiter: the gain
// reduction falls at once to what keeps the louder channel at kCeiling (no overshoot, no look-ahead, no latency) and recovers with a one-pole of
// kReleaseSeconds. At 0 dB and with peaks under the ceiling the samples pass bit-identical. Non-finite input samples are treated as silence.
// No allocation, no lock: safe on the audio thread. Tested by shell-mpc/tests/output_stage_host.cpp (tests/run_output_stage_test.sh).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
namespace synthyboy_mpc {
class OutputStage {
public:
    static constexpr float kCeiling = 0.891251f, kReleaseSeconds = 0.120f, kGainSmoothingSeconds = 0.020f;
    OutputStage() noexcept { prepare (44100.0); }
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
        if (env >= 1.0f) return 0.0f;
        const float safeEnv = std::max (env, 1e-6f);
        return -20.0f * std::log10 (safeEnv);
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
private:
    float gDb = 0.0f, targetGain = 1.0f, curGain = 1.0f, env = 1.0f;
    float relCoef = 0.99981105f, smCoef = 0.99886686f;
};
}
