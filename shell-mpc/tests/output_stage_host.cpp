// SynthyBoy - MPC shell: acceptance test of the output stage (make-up gain + peak limiter), shell-mpc/output_stage.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "output_stage.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using synthyboy_mpc::BasicOutputStage;
using synthyboy_mpc::OutputStage;

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }

static const float kPi = 3.14159265358979f;
static std::vector<float> sine (int n, float amp, float hz, double sr) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = amp * std::sin (2.0f * kPi * hz * (float) i / (float) sr); return v; }
static float peakOf (const std::vector<float>& v, int from) { float p = 0; for (size_t i = (size_t) from; i < v.size(); ++i) p = std::max (p, std::fabs (v[i])); return p; }

template <typename Stage>
static void run (Stage& s, std::vector<float>& l, std::vector<float>& r, int block = 128)
{
    for (size_t o = 0; o < l.size(); o += (size_t) block)
        s.process (l.data() + o, r.data() + o, (int) std::min ((size_t) block, l.size() - o));
}

static_assert (noexcept (std::declval<BasicOutputStage<false>&>().process ((float*) nullptr, (float*) nullptr, 0)), "process must be noexcept (limiter OFF)");
static_assert (noexcept (std::declval<BasicOutputStage<true>&>().process ((float*) nullptr, (float*) nullptr, 0)), "process must be noexcept (limiter ON)");
static_assert (! BasicOutputStage<false>::kLimiterEnabled, "kLimiterEnabled false for BasicOutputStage<false>");
static_assert (BasicOutputStage<true>::kLimiterEnabled, "kLimiterEnabled true for BasicOutputStage<true>");
static_assert (OutputStage::kLimiterEnabled == (SYNTHYBOY_MPC_OUTPUT_LIMITER != 0), "OutputStage matches SYNTHYBOY_MPC_OUTPUT_LIMITER");

int main()
{
    const float T = OutputStage::kCeiling; // linear, -1 dBFS
    check (std::fabs (T - 0.891251f) < 1e-5f, "kCeiling is -1 dBFS (0.891251)");
    check (std::fabs (OutputStage::kReleaseSeconds - 0.120f) < 1e-6f, "kReleaseSeconds is 0.120");
    check (std::fabs (OutputStage::kGainSmoothingSeconds - 0.020f) < 1e-6f, "kGainSmoothingSeconds is 0.020");

    for (double sr : { 44100.0, 48000.0 })
    {
        const int sec = (int) sr;

        // =========================================================================
        // PART 1: Limiter OFF (BasicOutputStage<false>, default MPC shell behaviour)
        // =========================================================================
        {
            // OFF-1 unity: 0 dB leaves samples bit-identical across all levels, no limiter reduction
            BasicOutputStage<false> s; s.prepare (sr); s.setGainDb (0.0f);
            for (float amp : { 0.1f, 0.5f, 1.0f, 2.0f })
            {
                auto in = sine (sec / 4, amp, 220.0f, sr); auto l = in, r = in; run (s, l, r);
                bool same = true; for (size_t i = 0; i < in.size(); ++i) same = same && l[i] == in[i] && r[i] == in[i];
                check (same, "limiter OFF: unity gain is bit-identical (including peaks over ceiling)");
            }
            check (s.gainReductionDb() == 0.0f, "limiter OFF: no reduction reported at unity");
        }
        {
            // OFF-2 linear gain: output = input * gain bit-identically after smoothing settles / reset
            for (float g : { 6.0f, 12.0f, 18.0f })
            {
                BasicOutputStage<false> s; s.prepare (sr); s.setGainDb (g); s.reset();
                const float lin = std::pow (10.0f, g / 20.0f);
                for (float amp : { 0.001f, 0.05f, 0.2f, 0.5f, 0.891f, 1.0f, 2.0f, 5.0f, 10.0f })
                {
                    auto in = sine (sec / 8, amp, 330.0f, sr); auto l = in, r = in;
                    s.process (l.data(), r.data(), (int) l.size());
                    bool exact = true;
                    for (size_t i = 0; i < in.size(); ++i)
                    {
                        const float expected = in[i] * lin;
                        exact = exact && (l[i] == expected) && (r[i] == expected);
                    }
                    check (exact, "limiter OFF: output = input * gain bit-identically at all levels");
                }
                check (s.gainReductionDb() == 0.0f, "limiter OFF: gainReductionDb() is always 0.0");
            }
        }
        {
            // OFF-3 peak can exceed 0 dBFS (no limiter, nothing stops it)
            BasicOutputStage<false> s; s.prepare (sr); s.setGainDb (18.0f); s.reset();
            auto l = sine (sec / 4, 0.5f, 220.0f, sr), r = l;
            s.process (l.data(), r.data(), (int) l.size());
            const float pk = peakOf (l, 0);
            check (pk > 1.0f && pk > 3.5f, "limiter OFF: peak exceeds 0 dBFS (linear gain 18 dB on 0.5 amp sine gives ~3.97)");
            check (s.gainReductionDb() == 0.0f, "limiter OFF: no gain reduction reported when exceeding 0 dBFS");
        }
        {
            // OFF-4 bad input (NaN/inf) treated as silence, valid audio returns to exact linear gain
            BasicOutputStage<false> s; s.prepare (sr); s.setGainDb (6.0f); s.reset();
            std::vector<float> badL (128, std::numeric_limits<float>::quiet_NaN()), badR (128, std::numeric_limits<float>::infinity());
            s.process (badL.data(), badR.data(), (int) badL.size());
            bool zero = true;
            for (size_t i = 0; i < badL.size(); ++i)
                zero = zero && (badL[i] == 0.0f) && (badR[i] == 0.0f);
            check (zero, "limiter OFF: non-finite inputs produce silence (0.0f)");

            auto in = sine (sec / 4, 0.4f, 220.0f, sr); auto l = in, r = in;
            s.process (l.data(), r.data(), (int) l.size());
            const float lin = std::pow (10.0f, 6.0f / 20.0f);
            bool exact = true;
            for (size_t i = 0; i < in.size(); ++i)
                exact = exact && (l[i] == in[i] * lin) && (r[i] == in[i] * lin);
            check (exact, "limiter OFF: valid audio after bad inputs is exact input * gain");
        }
        {
            // OFF-5 large finite inputs multiplying to infinity do not produce NaN or inf
            BasicOutputStage<false> s; s.prepare (sr); s.setGainDb (6.0f); s.reset();
            std::vector<float> l { 2.0e38f, std::numeric_limits<float>::max() / 1.5f, 1.0e30f, 0.5f },
                               r { 0.5f, 2.0e38f, -std::numeric_limits<float>::max() / 1.5f, 0.5f };
            s.process (l.data(), r.data(), (int) l.size());
            bool ok = true;
            for (size_t i = 0; i < l.size(); ++i)
                ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]) && ! std::isnan (l[i]) && ! std::isnan (r[i]);
            check (ok, "limiter OFF: overflow multiplication produces finite output (silenced)");
            check (s.gainReductionDb() == 0.0f, "limiter OFF: gainReductionDb() is 0.0 after extreme input");
        }
        {
            // OFF-6 gain smoothing
            BasicOutputStage<false> s; s.prepare (sr); s.setGainDb (0.0f);
            std::vector<float> l ((size_t) (sec / 2), 0.01f), r = l; run (s, l, r);
            s.setGainDb (12.0f); std::vector<float> l2 ((size_t) (sec / 2), 0.01f), r2 = l2; run (s, l2, r2);
            const float fin = 0.01f * std::pow (10.0f, 12.0f / 20.0f); bool mono = true; float maxStep = 0;
            for (size_t i = 1; i < l2.size(); ++i) { mono = mono && l2[i] >= l2[i - 1] - 1e-9f; maxStep = std::max (maxStep, l2[i] - l2[i - 1]); }
            check (mono, "limiter OFF: after a gain change the output moves monotonically");
            check (l2[0] < 0.6f * fin, "limiter OFF: the new gain is not applied at once");
            check (maxStep < 0.02f * fin, "limiter OFF: no step larger than 2 % between two samples");
            check (std::fabs (l2[(size_t) (0.1 * sr)] - fin) < 0.01f * fin, "limiter OFF: the new gain is reached within 100 ms (1 %)");
        }
        {
            // OFF-7 invalid sample rates fall back safely
            for (double badSr : { 0.0, -44100.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
            {
                BasicOutputStage<false> s; s.prepare (badSr); s.setGainDb (6.0f);
                std::vector<float> l (128, 0.5f), r = l;
                s.process (l.data(), r.data(), (int) l.size());
                bool finite = true;
                for (size_t i = 0; i < l.size(); ++i)
                    finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                check (finite, "limiter OFF: invalid sample rate does not blow up process()");
            }
        }
        {
            // OFF-8 null buffers and negative n
            BasicOutputStage<false> s; s.prepare (sr); s.setGainDb (6.0f);
            s.process (nullptr, nullptr, 128);
            s.process (nullptr, nullptr, 0);
            s.process (nullptr, nullptr, -10);
            std::vector<float> mono (128, 0.8f);
            s.process (mono.data(), nullptr, (int) mono.size());
            bool okL = true;
            for (float v : mono) okL = okL && std::isfinite (v);
            check (okL, "limiter OFF: mono left channel (r == nullptr) processed safely");
            std::vector<float> monoR (128, 0.8f);
            s.process (nullptr, monoR.data(), (int) monoR.size());
            bool okR = true;
            for (float v : monoR) okR = okR && std::isfinite (v);
            check (okR, "limiter OFF: mono right channel (l == nullptr) processed safely");
        }
        {
            // OFF-9 block size invariance
            BasicOutputStage<false> s1; s1.prepare (sr); s1.setGainDb (6.0f);
            BasicOutputStage<false> s2; s2.prepare (sr); s2.setGainDb (6.0f);
            auto in = sine (sec / 2, 0.7f, 440.0f, sr);
            auto l1 = in, r1 = in, l2 = in, r2 = in;
            s1.process (l1.data(), r1.data(), (int) l1.size());
            const int blockSizes[] = { 1, 2, 7, 32, 64, 128, 17 };
            size_t pos = 0; int bsIdx = 0;
            while (pos < l2.size())
            {
                int b = blockSizes[bsIdx % 7];
                int n = (int) std::min ((size_t) b, l2.size() - pos);
                s2.process (l2.data() + pos, r2.data() + pos, n);
                pos += (size_t) n;
                ++bsIdx;
            }
            bool exact = true;
            for (size_t i = 0; i < l1.size(); ++i)
                exact = exact && (l1[i] == l2[i]) && (r1[i] == r2[i]);
            check (exact, "limiter OFF: varying block sizes (1, 2, 7, 32...) give bit-identical output");
        }

        // =========================================================================
        // PART 2: Limiter ON (BasicOutputStage<true>, optional peak limiter mode)
        // =========================================================================
        { // ON-1 unity: 0 dB and peaks under the ceiling leave the samples bit-identical
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (0.0f);
            auto in = sine (sec / 2, 0.5f, 220.0f, sr); auto l = in, r = in; run (s, l, r);
            bool same = true; for (size_t i = 0; i < in.size(); ++i) same = same && l[i] == in[i] && r[i] == in[i];
            check (same, "limiter ON: unity gain below the ceiling is bit-identical");
            check (s.gainReductionDb() == 0.0f, "limiter ON: no reduction reported at unity");
        }
        { // ON-2 plain gain
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (6.0206f);
            auto l = sine (sec / 2, 0.2f, 220.0f, sr), r = l; run (s, l, r);
            const float p = peakOf (l, (int) (0.2 * sr));
            check (std::fabs (p - 0.4f) < 0.002f, "limiter ON: +6.02 dB doubles a signal that stays under the ceiling");
            check (s.gainReductionDb() < 0.01f, "limiter ON: no reduction while under the ceiling");
        }
        { // ON-3 limiting: never above the ceiling, and not pushed far below it
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (12.0f);
            auto l = sine (sec, 0.5f, 220.0f, sr), r = l; run (s, l, r);
            float mx = 0; for (float v : l) mx = std::max (mx, std::fabs (v));
            check (mx <= T + 1e-6f, "limiter ON: output never exceeds the ceiling");
            check (peakOf (l, (int) (0.5 * sr)) >= 0.97f * T, "limiter ON: a limited sine still reaches the ceiling");
            check (s.gainReductionDb() > 6.0f && s.gainReductionDb() < 9.0f, "limiter ON: reduction of about 7 dB reported (2.0 -> 0.891)");
        }
        { // ON-4 a step from silence does not overshoot
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (0.0f);
            std::vector<float> l (2000, 0.0f), r (2000, 0.0f); for (size_t i = 1000; i < 2000; ++i) { l[i] = 1.0f; r[i] = -1.0f; }
            run (s, l, r);
            float mx = 0; for (size_t i = 0; i < l.size(); ++i) mx = std::max (mx, std::max (std::fabs (l[i]), std::fabs (r[i])));
            check (mx <= T + 1e-6f, "limiter ON: the first sample of a full-scale step is already at the ceiling");
        }
        { // ON-5 release: slow enough not to pump at once, complete within a second
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (0.0f);
            const int burst = (int) (0.05 * sr), tail = (int) (1.2 * sr);
            std::vector<float> l ((size_t) (burst + tail)), r;
            for (int i = 0; i < burst + tail; ++i) l[(size_t) i] = (i < burst ? 3.55f : 0.01f) * std::sin (2.0f * kPi * 1000.0f * (float) i / (float) sr);
            r = l; auto in = l; run (s, l, r, 64);
            auto redDbAt = [&] (double t) { const int a = burst + (int) (t * sr); float pi = 0, po = 0; for (int i = a; i < a + (int) (0.004 * sr); ++i) { pi = std::max (pi, std::fabs (in[(size_t) i])); po = std::max (po, std::fabs (l[(size_t) i])); } return 20.0f * std::log10 (pi / po); };
            check (redDbAt (0.020) > 3.0f, "limiter ON: 20 ms after a burst the gain is still reduced by more than 3 dB");
            check (redDbAt (0.120) < redDbAt (0.020), "limiter ON: the reduction falls with time");
            check (redDbAt (1.0) < 0.1f, "limiter ON: one second after the burst the gain is back (less than 0.1 dB)");
        }
        { // ON-6 stereo link
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (6.0f);
            auto l = sine (sec / 2, 0.9f, 300.0f, sr), r = sine (sec / 2, 0.09f, 300.0f, sr); auto il = l, ir = r; run (s, l, r);
            bool linked = true; for (size_t i = (size_t) (0.2 * sr); i < l.size(); ++i) if (std::fabs (il[i]) > 0.05f) linked = linked && std::fabs (l[i] / il[i] - r[i] / ir[i]) < 1e-3f;
            check (linked, "limiter ON: both channels get the same gain (stereo linked)");
        }
        { // ON-7 gain changes are smoothed
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (0.0f);
            std::vector<float> l ((size_t) (sec / 2), 0.01f), r = l; run (s, l, r);
            s.setGainDb (12.0f); std::vector<float> l2 ((size_t) (sec / 2), 0.01f), r2 = l2; run (s, l2, r2);
            const float fin = 0.01f * std::pow (10.0f, 12.0f / 20.0f); bool mono = true; float maxStep = 0;
            for (size_t i = 1; i < l2.size(); ++i) { mono = mono && l2[i] >= l2[i - 1] - 1e-9f; maxStep = std::max (maxStep, l2[i] - l2[i - 1]); }
            check (mono, "limiter ON: after a gain change the output moves monotonically");
            check (l2[0] < 0.6f * fin, "limiter ON: the new gain is not applied at once");
            check (maxStep < 0.02f * fin, "limiter ON: no step larger than 2 % of the final value between two samples");
            check (std::fabs (l2[(size_t) (0.1 * sr)] - fin) < 0.01f * fin, "limiter ON: the new gain is reached within 100 ms (1 %)");
        }
        { // ON-8 bad input does not poison the state
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (0.0f);
            std::vector<float> l (256, std::numeric_limits<float>::quiet_NaN()), r (256, std::numeric_limits<float>::infinity()); run (s, l, r);
            bool finite = true; for (size_t i = 0; i < l.size(); ++i) finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]) && std::fabs (l[i]) <= T + 1e-6f && std::fabs (r[i]) <= T + 1e-6f;
            check (finite, "limiter ON: NaN and infinity give finite output within the ceiling");
            auto in = sine (sec, 0.5f, 220.0f, sr); auto l2 = in, r2 = in; run (s, l2, r2);
            bool same = true; for (size_t i = (size_t) (0.9 * sr); i < in.size(); ++i) same = same && l2[i] == in[i];
            check (same, "limiter ON: after bad input the stage returns to exact unity");
        }
        { // ON-9 reset, ON-10 range of the gain
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (0.0f);
            std::vector<float> l (512, 3.0f), r = l; run (s, l, r); check (s.gainReductionDb() > 9.0f, "limiter ON: reduction after a 3.0 input");
            s.reset(); check (s.gainReductionDb() == 0.0f, "limiter ON: reset() clears the reduction");
            s.setGainDb (100.0f); check (s.gainDb() == 24.0f, "limiter ON: the gain is clamped to +24 dB");
            s.setGainDb (-100.0f); check (s.gainDb() == -24.0f, "limiter ON: the gain is clamped to -24 dB");
            s.setGainDb (std::numeric_limits<float>::quiet_NaN()); check (s.gainDb() == 0.0f, "limiter ON: a NaN gain becomes 0 dB");
        }
        { // ON-11 [KRITIKUS-1] large finite input + make-up gain does not produce NaN/inf
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (6.0f); s.reset();
            std::vector<float> l { 2.0e38f, std::numeric_limits<float>::max() / 1.5f, 1.0e30f, 0.5f },
                               r { 0.5f, 2.0e38f, -std::numeric_limits<float>::max() / 1.5f, 0.5f };
            s.process (l.data(), r.data(), (int) l.size());
            bool ok = true;
            for (size_t i = 0; i < l.size(); ++i)
                ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]) && ! std::isnan (l[i]) && ! std::isnan (r[i])
                        && std::fabs (l[i]) <= T + 1e-6f && std::fabs (r[i]) <= T + 1e-6f;
            check (ok, "limiter ON: large finite inputs with positive make-up gain do not produce NaN or inf");
            check (std::isfinite (s.gainReductionDb()), "limiter ON: gain reduction remains finite after huge inputs");
        }
        { // ON-12 [MAGAS-1] invalid sample rates in prepare() fall back to safe behaviour
            for (double badSr : { 0.0, -44100.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
            {
                BasicOutputStage<true> s; s.prepare (badSr); s.setGainDb (6.0f);
                std::vector<float> l (128, 0.5f), r = l;
                s.process (l.data(), r.data(), (int) l.size());
                bool finite = true;
                for (size_t i = 0; i < l.size(); ++i)
                    finite = finite && std::isfinite (l[i]) && std::isfinite (r[i])
                                    && std::fabs (l[i]) <= T + 1e-6f && std::fabs (r[i]) <= T + 1e-6f;
                check (finite, "limiter ON: invalid sample rate does not blow up process()");
            }
        }
        { // ON-13 [KÖZEPES-1] null or partial channel pointers and n <= 0 do not crash
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (6.0f);
            s.process (nullptr, nullptr, 128);
            s.process (nullptr, nullptr, 0);
            s.process (nullptr, nullptr, -10);
            std::vector<float> mono (128, 0.8f);
            s.process (mono.data(), nullptr, (int) mono.size());
            bool okL = true;
            for (float v : mono) okL = okL && std::isfinite (v) && std::fabs (v) <= T + 1e-6f;
            check (okL, "limiter ON: mono left channel (r == nullptr) is limited safely");

            std::vector<float> monoR (128, 0.8f);
            s.process (nullptr, monoR.data(), (int) monoR.size());
            bool okR = true;
            for (float v : monoR) okR = okR && std::isfinite (v) && std::fabs (v) <= T + 1e-6f;
            check (okR, "limiter ON: mono right channel (l == nullptr) is limited safely");
        }
        { // ON-14 [KÖZEPES-2] process() before prepare() uses safe default smoothing
            BasicOutputStage<true> s;
            s.setGainDb (12.0f);
            std::vector<float> l ((size_t) 128, 0.01f), r = l;
            s.process (l.data(), r.data(), (int) l.size());
            const float fin = 0.01f * std::pow (10.0f, 12.0f / 20.0f);
            check (l[0] < 0.6f * fin, "limiter ON: process before prepare does not jump immediately to target gain");
        }
        { // ON-15 [KÖZEPES-3] gainReductionDb() is finite even under extreme reduction
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (24.0f);
            std::vector<float> l (64, 1e20f), r = l;
            s.process (l.data(), r.data(), (int) l.size());
            const float gr = s.gainReductionDb();
            check (std::isfinite (gr) && gr >= 0.0f && gr <= 200.0f, "limiter ON: gainReductionDb() stays finite under extreme reduction");
        }
        { // ON-16 stereo link works with right louder than left
            BasicOutputStage<true> s; s.prepare (sr); s.setGainDb (6.0f);
            auto l = sine (sec / 2, 0.09f, 300.0f, sr), r = sine (sec / 2, 0.9f, 300.0f, sr); auto il = l, ir = r; run (s, l, r);
            bool linked = true;
            for (size_t i = (size_t) (0.2 * sr); i < l.size(); ++i)
                if (std::fabs (ir[i]) > 0.05f) linked = linked && std::fabs (l[i] / il[i] - r[i] / ir[i]) < 1e-3f;
            check (linked, "limiter ON: stereo link reduces left channel when right channel is louder");
        }
        { // ON-17 block size invariance
            BasicOutputStage<true> s1; s1.prepare (sr); s1.setGainDb (6.0f);
            BasicOutputStage<true> s2; s2.prepare (sr); s2.setGainDb (6.0f);
            auto in = sine (sec / 2, 0.7f, 440.0f, sr);
            auto l1 = in, r1 = in, l2 = in, r2 = in;
            s1.process (l1.data(), r1.data(), (int) l1.size());
            const int blockSizes[] = { 1, 2, 7, 32, 64, 128, 17 };
            size_t pos = 0; int bsIdx = 0;
            while (pos < l2.size())
            {
                int b = blockSizes[bsIdx % 7];
                int n = (int) std::min ((size_t) b, l2.size() - pos);
                s2.process (l2.data() + pos, r2.data() + pos, n);
                pos += (size_t) n;
                ++bsIdx;
            }
            bool exact = true;
            for (size_t i = 0; i < l1.size(); ++i)
                exact = exact && (l1[i] == l2[i]) && (r1[i] == r2[i]);
            check (exact, "limiter ON: varying block sizes (1, 2, 7, 32...) give bit-identical output");
        }
    }
    // =========================================================================
    // PART 3: the limiter as a plugin setting (SwitchableOutputStage, the chunk member, the environment override)
    // =========================================================================
    using synthyboy_mpc::SwitchableOutputStage;
    check (SwitchableOutputStage().limiter() == synthyboy_mpc::kOutputLimiterDefaultOn, "setting: a new stage has the default of kOutputLimiterDefaultOn");
    static_assert (noexcept (std::declval<SwitchableOutputStage&>().process ((float*) nullptr, (float*) nullptr, 0)), "process must be noexcept (switchable)");
    for (double sr : { 44100.0, 48000.0 })
    {
        const int sec = (int) sr;
        // a full chord (six partials, about 1.5 peak) at the 18 dB make-up gain of the MPC shell
        auto chord = [&] (int n) {
            std::vector<float> v ((size_t) n, 0.0f);
            for (float hz : { 110.0f, 165.0f, 220.0f, 277.2f, 330.0f, 440.0f })
                for (int i = 0; i < n; ++i) v[(size_t) i] += 0.25f * std::sin (2.0f * kPi * hz * (float) i / (float) sr);
            return v;
        };
        {
            SwitchableOutputStage s; s.prepare (sr); s.setGainDb (18.0f); s.setLimiter (false);
            auto in = chord (sec); auto l = in, r = in; run (s, l, r);
            check (peakOf (l, sec / 4) > 2.0f, "switchable, limiter off: the full chord peaks far over full scale (what the setting is for)");
            check (s.gainReductionDb() == 0.0f, "switchable, limiter off: no reduction reported");
        }
        {
            SwitchableOutputStage s; s.prepare (sr); s.setGainDb (18.0f); s.setLimiter (true);
            auto in = chord (sec); auto l = in, r = in; run (s, l, r);
            check (peakOf (l, 0) <= OutputStage::kCeiling + 1e-6f && peakOf (r, 0) <= OutputStage::kCeiling + 1e-6f, "switchable, limiter on: the full chord never passes the -1 dBFS ceiling (from the first sample)");
            check (peakOf (l, sec / 2) > 0.5f, "switchable, limiter on: a sustained chord keeps its level (no pumping down to silence)");
            check (s.gainReductionDb() > 6.0f, "switchable, limiter on: the reduction is reported");
        }
        {
            // below the threshold the limiter passes the samples bit-identical (gain 0 dB)
            SwitchableOutputStage s; s.prepare (sr); s.setGainDb (0.0f); s.setLimiter (true);
            auto in = sine (sec / 2, 0.3f, 330.0f, sr); auto l = in, r = in; run (s, l, r);
            bool same = true; for (size_t i = 0; i < in.size(); ++i) same = same && l[i] == in[i] && r[i] == in[i];
            check (same, "switchable, limiter on: peaks under the ceiling pass bit-identical at 0 dB");
        }
        {
            // switched while running: off for a second, then on: from the switch the ceiling holds
            SwitchableOutputStage s; s.prepare (sr); s.setGainDb (18.0f); s.setLimiter (false);
            auto in = chord (2 * sec); std::vector<float> l (in.begin(), in.begin() + sec), r = l;
            run (s, l, r);
            s.setLimiter (true);
            std::vector<float> l2 (in.begin() + sec, in.end()), r2 = l2;
            run (s, l2, r2);
            check (peakOf (l, sec / 4) > 2.0f && peakOf (l2, 0) <= OutputStage::kCeiling + 1e-6f, "switchable: switching the limiter on while running holds the ceiling from the next block");
        }
    }
    {   // the chunk member: round trip, position before the pad pressure members, an older chunk, a damaged member
        const std::string base = "{\"mpcProgram\":3,\"mpcProgramName\":\"A\",\"version\":4,\"params\":{\"a\":1}}";
        for (bool v : { true, false })
        {
            std::string c = synthyboy_mpc::chunkWithOutputLimiter (base, v);
            bool got = ! v;
            check (synthyboy_mpc::outputLimiterFromChunk (c.data(), c.size(), got) && got == v, "chunk: the limiter setting survives a round trip");
            const std::string withPad = c.substr (0, c.size() - 1) + ",\"mpcPadPressure\":1,\"mpcPadPressureAmount\":35.00}";
            bool got2 = ! v;
            check (synthyboy_mpc::outputLimiterFromChunk (withPad.data(), withPad.size(), got2) && got2 == v, "chunk: the member is found before the pad pressure members");
        }
        bool keep = true;
        check (! synthyboy_mpc::outputLimiterFromChunk (base.data(), base.size(), keep) && keep, "chunk: an older chunk without the member leaves the setting as it is");
        const std::string bad = "{\"mpcProgram\":3,\"params\":{\"a\":1},\"mpcOutputLimiter\":7}";
        keep = true;
        check (! synthyboy_mpc::outputLimiterFromChunk (bad.data(), bad.size(), keep) && keep, "chunk: a damaged member is ignored");
        check (! synthyboy_mpc::outputLimiterFromChunk (nullptr, 0, keep), "chunk: no data");
    }
    {   // the environment forces the state for tests
        ::setenv ("SYNTHYBOY_MPC_OUTPUT_LIMITER", "0", 1);
        check (synthyboy_mpc::outputLimiterForced() == 0, "environment: SYNTHYBOY_MPC_OUTPUT_LIMITER=0 forces the limiter off");
        ::setenv ("SYNTHYBOY_MPC_OUTPUT_LIMITER", "1", 1);
        check (synthyboy_mpc::outputLimiterForced() == 1, "environment: SYNTHYBOY_MPC_OUTPUT_LIMITER=1 forces the limiter on");
        ::unsetenv ("SYNTHYBOY_MPC_OUTPUT_LIMITER");
        check (synthyboy_mpc::outputLimiterForced() == (SYNTHYBOY_MPC_OUTPUT_LIMITER != 0 ? 1 : -1), "environment: unset leaves it to the setting (a build with the limiter ON forces it on)");
    }
    std::printf ("%s  (%d failures)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
