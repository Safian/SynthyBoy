// SynthyBoy - MPC shell: acceptance test of the output stage (make-up gain + peak limiter), shell-mpc/output_stage.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "output_stage.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using synthyboy_mpc::OutputStage;

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }

static const float kPi = 3.14159265358979f;
static std::vector<float> sine (int n, float amp, float hz, double sr) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = amp * std::sin (2.0f * kPi * hz * (float) i / (float) sr); return v; }
static float peakOf (const std::vector<float>& v, int from) { float p = 0; for (size_t i = (size_t) from; i < v.size(); ++i) p = std::max (p, std::fabs (v[i])); return p; }
static void run (OutputStage& s, std::vector<float>& l, std::vector<float>& r, int block = 128) { for (size_t o = 0; o < l.size(); o += (size_t) block) s.process (l.data() + o, r.data() + o, (int) std::min ((size_t) block, l.size() - o)); }

static_assert (noexcept (std::declval<OutputStage&>().process ((float*) nullptr, (float*) nullptr, 0)), "process must be noexcept");

int main()
{
    const float T = OutputStage::kCeiling; // linear, -1 dBFS
    check (std::fabs (T - 0.891251f) < 1e-5f, "kCeiling is -1 dBFS (0.891251)");
    check (std::fabs (OutputStage::kReleaseSeconds - 0.120f) < 1e-6f, "kReleaseSeconds is 0.120");
    check (std::fabs (OutputStage::kGainSmoothingSeconds - 0.020f) < 1e-6f, "kGainSmoothingSeconds is 0.020");

    for (double sr : { 44100.0, 48000.0 })
    {
        const int sec = (int) sr;
        { // 1 unity: 0 dB and peaks under the ceiling leave the samples bit-identical
            OutputStage s; s.prepare (sr); s.setGainDb (0.0f);
            auto in = sine (sec / 2, 0.5f, 220.0f, sr); auto l = in, r = in; run (s, l, r);
            bool same = true; for (size_t i = 0; i < in.size(); ++i) same = same && l[i] == in[i] && r[i] == in[i];
            check (same, "unity gain below the ceiling is bit-identical");
            check (s.gainReductionDb() == 0.0f, "no reduction reported at unity");
        }
        { // 2 plain gain
            OutputStage s; s.prepare (sr); s.setGainDb (6.0206f);
            auto l = sine (sec / 2, 0.2f, 220.0f, sr), r = l; run (s, l, r);
            const float p = peakOf (l, (int) (0.2 * sr));
            check (std::fabs (p - 0.4f) < 0.002f, "+6.02 dB doubles a signal that stays under the ceiling");
            check (s.gainReductionDb() < 0.01f, "no reduction while under the ceiling");
        }
        { // 3 limiting: never above the ceiling, and not pushed far below it
            OutputStage s; s.prepare (sr); s.setGainDb (12.0f);
            auto l = sine (sec, 0.5f, 220.0f, sr), r = l; run (s, l, r);
            float mx = 0; for (float v : l) mx = std::max (mx, std::fabs (v));
            check (mx <= T + 1e-6f, "output never exceeds the ceiling");
            check (peakOf (l, (int) (0.5 * sr)) >= 0.97f * T, "a limited sine still reaches the ceiling");
            check (s.gainReductionDb() > 6.0f && s.gainReductionDb() < 9.0f, "reduction of about 7 dB reported (2.0 -> 0.891)");
        }
        { // 4 a step from silence does not overshoot
            OutputStage s; s.prepare (sr); s.setGainDb (0.0f);
            std::vector<float> l (2000, 0.0f), r (2000, 0.0f); for (size_t i = 1000; i < 2000; ++i) { l[i] = 1.0f; r[i] = -1.0f; }
            run (s, l, r);
            float mx = 0; for (size_t i = 0; i < l.size(); ++i) mx = std::max (mx, std::max (std::fabs (l[i]), std::fabs (r[i])));
            check (mx <= T + 1e-6f, "the first sample of a full-scale step is already at the ceiling");
        }
        { // 5 release: slow enough not to pump at once, complete within a second
            OutputStage s; s.prepare (sr); s.setGainDb (0.0f);
            const int burst = (int) (0.05 * sr), tail = (int) (1.2 * sr);
            std::vector<float> l ((size_t) (burst + tail)), r;
            for (int i = 0; i < burst + tail; ++i) l[(size_t) i] = (i < burst ? 3.55f : 0.01f) * std::sin (2.0f * kPi * 1000.0f * (float) i / (float) sr); // 3.55 = +11 dB over full scale -> 12 dB of reduction
            r = l; auto in = l; run (s, l, r, 64);
            auto redDbAt = [&] (double t) { const int a = burst + (int) (t * sr); float pi = 0, po = 0; for (int i = a; i < a + (int) (0.004 * sr); ++i) { pi = std::max (pi, std::fabs (in[(size_t) i])); po = std::max (po, std::fabs (l[(size_t) i])); } return 20.0f * std::log10 (pi / po); };
            check (redDbAt (0.020) > 3.0f, "20 ms after a burst the gain is still reduced by more than 3 dB");
            check (redDbAt (0.120) < redDbAt (0.020), "the reduction falls with time");
            check (redDbAt (1.0) < 0.1f, "one second after the burst the gain is back (less than 0.1 dB)");
        }
        { // 6 stereo link
            OutputStage s; s.prepare (sr); s.setGainDb (6.0f);
            auto l = sine (sec / 2, 0.9f, 300.0f, sr), r = sine (sec / 2, 0.09f, 300.0f, sr); auto il = l, ir = r; run (s, l, r);
            bool linked = true; for (size_t i = (size_t) (0.2 * sr); i < l.size(); ++i) if (std::fabs (il[i]) > 0.05f) linked = linked && std::fabs (l[i] / il[i] - r[i] / ir[i]) < 1e-3f;
            check (linked, "both channels get the same gain (stereo linked)");
        }
        { // 7 gain changes are smoothed
            OutputStage s; s.prepare (sr); s.setGainDb (0.0f);
            std::vector<float> l ((size_t) (sec / 2), 0.01f), r = l; run (s, l, r); // settle at unity
            s.setGainDb (12.0f); std::vector<float> l2 ((size_t) (sec / 2), 0.01f), r2 = l2; run (s, l2, r2);
            const float fin = 0.01f * std::pow (10.0f, 12.0f / 20.0f); bool mono = true; float maxStep = 0;
            for (size_t i = 1; i < l2.size(); ++i) { mono = mono && l2[i] >= l2[i - 1] - 1e-9f; maxStep = std::max (maxStep, l2[i] - l2[i - 1]); }
            check (mono, "after a gain change the output moves monotonically");
            check (l2[0] < 0.6f * fin, "the new gain is not applied at once");
            check (maxStep < 0.02f * fin, "no step larger than 2 % of the final value between two samples");
            check (std::fabs (l2[(size_t) (0.1 * sr)] - fin) < 0.01f * fin, "the new gain is reached within 100 ms (1 %)");
        }
        { // 8 bad input does not poison the state
            OutputStage s; s.prepare (sr); s.setGainDb (0.0f);
            std::vector<float> l (256, std::numeric_limits<float>::quiet_NaN()), r (256, std::numeric_limits<float>::infinity()); run (s, l, r);
            bool finite = true; for (size_t i = 0; i < l.size(); ++i) finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]) && std::fabs (l[i]) <= T + 1e-6f && std::fabs (r[i]) <= T + 1e-6f;
            check (finite, "NaN and infinity give finite output within the ceiling");
            auto in = sine (sec, 0.5f, 220.0f, sr); auto l2 = in, r2 = in; run (s, l2, r2);
            bool same = true; for (size_t i = (size_t) (0.9 * sr); i < in.size(); ++i) same = same && l2[i] == in[i];
            check (same, "after bad input the stage returns to exact unity");
        }
        { // 9 reset, 10 range of the gain
            OutputStage s; s.prepare (sr); s.setGainDb (0.0f);
            std::vector<float> l (512, 3.0f), r = l; run (s, l, r); check (s.gainReductionDb() > 9.0f, "reduction after a 3.0 input");
            s.reset(); check (s.gainReductionDb() == 0.0f, "reset() clears the reduction");
            s.setGainDb (100.0f); check (s.gainDb() == 24.0f, "the gain is clamped to +24 dB");
            s.setGainDb (-100.0f); check (s.gainDb() == -24.0f, "the gain is clamped to -24 dB");
            s.setGainDb (std::numeric_limits<float>::quiet_NaN()); check (s.gainDb() == 0.0f, "a NaN gain becomes 0 dB");
        }
    }
    std::printf ("%s  (%d failures)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
