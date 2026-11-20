// SynthyBoy - acceptance test of the output stage: make-up gain + look-ahead peak limiter (core/include/synthyboy/output_stage.h) and the MPC shell's setting around it (shell-mpc/output_stage.h).
// (a) the factory presets are in core/tests/test_output_stage_presets.cpp (they need the engine); here: (b) transparency, (c) attack, release, smoothness, THD, loudness order,
// (d) latency, reset and the live switch, the limits (non-finite input, overflow, sample rates, mono, block sizes), the chunk member and the environment override; the timing per block.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "output_stage.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using synthyboy::OutputStage;
using synthyboy_mpc::SwitchableOutputStage;

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }
static void checkf (bool ok, const char* fmt, double a, double b = 0.0, double c = 0.0)
{
    char buf[256];
    std::snprintf (buf, sizeof buf, fmt, a, b, c);
    check (ok, buf);
}

static const double kPi = 3.14159265358979323846;
static const float C = OutputStage::kCeiling;
static std::vector<float> sine (int n, float amp, double hz, double sr)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2.0 * kPi * hz * i / sr);
    return v;
}
static float peakOf (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
{
    float p = 0;
    for (size_t i = from; i < std::min (to, v.size()); ++i) p = std::max (p, std::fabs (v[i]));
    return p;
}
static void run (OutputStage& s, std::vector<float>& l, std::vector<float>& r, int block = 128)
{
    for (size_t o = 0; o < l.size(); o += (size_t) block)
        s.process (l.data() + o, r.data() + o, (int) std::min ((size_t) block, l.size() - o));
}
/** One sample per call: returns the limiter gain of every sample (the trace). */
static std::vector<float> runTrace (OutputStage& s, std::vector<float>& l, std::vector<float>& r)
{
    std::vector<float> g (l.size());
    for (size_t i = 0; i < l.size(); ++i) { s.process (&l[i], &r[i], 1); g[i] = s.lastGain(); }
    return g;
}
static OutputStage* make (double sr, float db, bool on)
{
    auto* s = new OutputStage();
    s->setLimiter (on);
    s->prepare (sr);
    s->setGainDb (db);
    s->reset();
    return s;
}
/** Total harmonic distortion of x (integer number of cycles of f0 over its length): harmonics 2 .. 10 against the fundamental, DFT at the exact bins. */
static double thd (const std::vector<float>& x, double f0, double sr)
{
    auto mag = [&] (double f) { double re = 0, im = 0; for (size_t i = 0; i < x.size(); ++i) { const double ph = 2.0 * kPi * f * (double) i / sr; re += x[i] * std::cos (ph); im -= x[i] * std::sin (ph); } return std::sqrt (re * re + im * im); };
    const double f = mag (f0);
    double h = 0;
    for (int k = 2; k <= 10 && k * f0 < 0.5 * sr; ++k) { const double m = mag (k * f0); h += m * m; }
    return std::sqrt (h) / f;
}

static_assert (noexcept (std::declval<OutputStage&>().process ((float*) nullptr, (float*) nullptr, 0)), "process must be noexcept");

int main()
{
    checkf (std::fabs (20.0 * std::log10 ((double) C) + 0.5) < 1e-5, "the ceiling is -0.5 dB full scale (%.6f)", (double) C);
    check (OutputStage::lookaheadFor (44100.0) == 132 && OutputStage::lookaheadFor (48000.0) == 144 && OutputStage::lookaheadFor (96000.0) == 288, "look-ahead 3 ms: 132 / 144 / 288 samples at 44.1 / 48 / 96 kHz");
    check (OutputStage::lookaheadFor (0.0) == 132 && OutputStage::lookaheadFor (std::numeric_limits<double>::quiet_NaN()) == 132, "an invalid rate counts as 44.1 kHz");
    check (synthyboy::kMakeupGainDb == 0.0f && OutputStage().gainDb() == synthyboy::kMakeupGainDb, "a new stage starts at kMakeupGainDb (0 dB: unity gain staging), the one value of every shell");
    check (! OutputStage().limiter() && OutputStage().latencySamples() == 0, "a new stage has the limiter off and reports no latency (the default sound: no dynamics, no delay)");
    {
        OutputStage s; s.prepare (48000.0); s.reset();
        auto in = sine (4800, 3.0f, 440.0, 48000.0); auto l = in, r = in; run (s, l, r);
        bool same = true; for (size_t i = 0; i < in.size(); ++i) same = same && l[i] == in[i] && r[i] == in[i];
        check (same, "a new stage passes every sample unchanged and undelayed (unity gain, limiter off), also far above full scale");
    }
    check (! synthyboy_mpc::kOutputLimiterDefaultOn, "MPC: the setting LIMIT is off by default (the user switches it on)");

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const int sec = (int) sr, D = OutputStage::lookaheadFor (sr);
        char tag[32];
        std::snprintf (tag, sizeof tag, "%.0f Hz", sr);
        std::printf ("---- %s (look-ahead %d samples)\n", tag, D);
        // ---------------- limiter OFF: the gain only, no delay ----------------
        for (float db : { 0.0f, 9.0f, 18.0f })
        {
            OutputStage* s = make (sr, db, false);
            const float lin = db == 0.0f ? 1.0f : std::pow (10.0f, db / 20.0f);
            bool exact = true;
            for (float amp : { 0.001f, 0.3f, 1.0f, 5.0f })
            {
                auto in = sine (sec / 8, amp, 330.0, sr); auto l = in, r = in; run (*s, l, r);
                for (size_t i = 0; i < in.size(); ++i) exact = exact && l[i] == in[i] * lin && r[i] == in[i] * lin;
            }
            check (exact && s->latencySamples() == 0 && s->gainReductionDb() == 0.0f, "OFF: output = input x gain to the bit, no delay, no reduction, latency 0 (also far above full scale)");
            delete s;
        }
        {
            OutputStage* s = make (sr, 6.0f, false);
            std::vector<float> l (128, std::numeric_limits<float>::quiet_NaN()), r (128, std::numeric_limits<float>::infinity()); run (*s, l, r);
            bool zero = true; for (size_t i = 0; i < l.size(); ++i) zero = zero && l[i] == 0.0f && r[i] == 0.0f;
            std::vector<float> l2 { 2.0e38f, 1.0e30f, -3.0e38f, 0.5f }, r2 = l2; s->process (l2.data(), r2.data(), 4);
            bool fin = true; for (size_t i = 0; i < 4; ++i) fin = fin && std::isfinite (l2[i]) && std::isfinite (r2[i]);
            check (zero && fin, "OFF: NaN and infinity are silence, an overflow of the gain is finite");
            delete s;
        }
        {   // make-up gain smoothing (both states)
            for (bool on : { false, true })
            {
                OutputStage* s = make (sr, 0.0f, on);
                std::vector<float> l ((size_t) sec / 2, 0.01f), r = l; run (*s, l, r);
                s->setGainDb (12.0f);
                std::vector<float> l2 ((size_t) sec / 2, 0.01f), r2 = l2; run (*s, l2, r2);
                const float fin = 0.01f * std::pow (10.0f, 0.6f);
                const size_t from = on ? (size_t) D : 0;
                bool mono = true; float maxStep = 0;
                for (size_t i = from + 1; i < l2.size(); ++i) { mono = mono && l2[i] >= l2[i - 1] - 1e-9f; maxStep = std::max (maxStep, l2[i] - l2[i - 1]); }
                check (mono && l2[from] < 0.6f * fin && maxStep < 0.02f * fin && std::fabs (l2[(size_t) (0.1 * sr)] - fin) < 0.01f * fin,
                       on ? "ON: a gain change is smoothed (monotone, no step above 2 %, at 1 % within 100 ms)" : "OFF: a gain change is smoothed (monotone, no step above 2 %, at 1 % within 100 ms)");
                delete s;
            }
        }
        // ---------------- (b) transparency: under the ceiling after the gain, the samples pass to the bit, delayed by D ----------------
        for (float db : { 0.0f, 9.0f })
        {
            OutputStage* s = make (sr, db, true);
            const float lin = db == 0.0f ? 1.0f : std::pow (10.0f, db / 20.0f);
            auto in = sine (sec, 0.9f * C / lin, 441.0, sr);
            auto inR = sine (sec, -0.5f * C / lin, 97.0, sr);
            auto l = in, r = inR; run (*s, l, r, 100);
            bool exact = true;
            for (size_t i = 0; i < in.size(); ++i)
            {
                const float el = i < (size_t) D ? 0.0f : in[i - (size_t) D] * lin, er = i < (size_t) D ? 0.0f : inR[i - (size_t) D] * lin;
                exact = exact && l[i] == el && r[i] == er;
            }
            check (exact && s->latencySamples() == D && s->gainReductionDb() == 0.0f,
                   db == 0.0f ? "ON, transparency: 0 dB under the ceiling: every sample bit-identical, delayed by exactly the look-ahead" : "ON, transparency: 9 dB, under the ceiling after the gain: every sample = input x gain to the bit, delayed by exactly the look-ahead");
            delete s;
        }
        {   // transparency survives a passage that was limited: back to bit-identical once the release is over
            OutputStage* s = make (sr, 0.0f, true);
            auto loud = sine (sec / 2, 2.0f, 220.0, sr), lr = loud; run (*s, loud, lr);
            auto in = sine (6 * sec, 0.5f, 330.0, sr); auto l = in, r = in; run (*s, l, r);
            bool exact = true;
            for (size_t i = (size_t) (5 * sr); i < in.size(); ++i) exact = exact && l[i] == in[i - (size_t) D];
            check (exact, "ON: after a limited passage and its release the samples pass to the bit again (idle)");
            delete s;
        }
        // ---------------- the ceiling ----------------
        {
            bool under = true; uint32_t clips = 0;
            for (float db : { 0.0f, 9.0f, 15.0f, 24.0f })
            {
                OutputStage* s = make (sr, db, true);
                // a sine burst train, a step from silence, white noise bursts, a single sample spike, alternating full scale
                std::vector<float> l ((size_t) (2 * sr), 0.0f);
                uint32_t rs = 12345u;
                for (size_t i = 0; i < l.size(); ++i)
                {
                    const double t = (double) i / sr;
                    float v = 0.0f;
                    if (std::fmod (t, 0.25) < 0.05) v = 1.3f * (float) std::sin (2.0 * kPi * 1000.0 * t);
                    else if (t > 1.0 && t < 1.2) { rs = rs * 1664525u + 1013904223u; v = ((float) (rs >> 8) / 16777216.0f - 0.5f) * 3.0f; }
                    else if (t > 1.5 && t < 1.6) v = (i % 2) ? 1.0f : -1.0f;
                    else if (i == (size_t) (1.7 * sr)) v = 4.0f;
                    else if (t > 1.8) v = 0.9f;
                    l[i] = v;
                }
                auto r = l; for (auto& v : r) v *= -0.7f;
                run (*s, l, r, 64);
                under = under && peakOf (l) <= C && peakOf (r) <= C;
                clips += s->safetyClips();
                delete s;
            }
            check (under, "ON: no output sample above the ceiling (bursts, steps, noise, Nyquist, a spike, DC; 0 / 9 / 15 / 24 dB)");
            checkf (clips == 0, "ON: the limiter holds the ceiling by itself: the safety clip acted %.0f times", clips);
        }
        {   // the first samples after a reset, mono, l == r
            OutputStage* s = make (sr, 24.0f, true);
            std::vector<float> l (512, 1.0f), r = l; run (*s, l, r);
            s->reset();
            std::vector<float> m (512, -1.0f); s->process (m.data(), nullptr, 512);
            std::vector<float> mr (512, 1.0f); s->process (nullptr, mr.data(), 512);
            std::vector<float> same (512, 0.8f); s->process (same.data(), same.data(), 512);
            check (peakOf (l) <= C && peakOf (m) <= C && peakOf (mr) <= C && peakOf (same) <= C && m[0] == 0.0f, "ON: within the ceiling from the first sample after a reset; left only, right only, l == r (one channel)");
            delete s;
        }
        {   // non-finite and huge input
            OutputStage* s = make (sr, 9.0f, true);
            std::vector<float> l (300, std::numeric_limits<float>::quiet_NaN()), r (300, -std::numeric_limits<float>::infinity());
            l[5] = 3.0e38f; r[7] = 1.0e30f; l[9] = 1.0e-40f;
            run (*s, l, r);
            std::vector<float> l2 (600, 0.1f), r2 = l2; run (*s, l2, r2);
            bool ok = true;
            for (size_t i = 0; i < l.size(); ++i) ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]) && std::fabs (l[i]) <= C && std::fabs (r[i]) <= C;
            for (size_t i = 0; i < l2.size(); ++i) ok = ok && std::isfinite (l2[i]) && std::fabs (l2[i]) <= C;
            check (ok && std::isfinite (s->gainReductionDb()), "ON: NaN, infinities and values up to 3e38 give finite output within the ceiling");
            delete s;
        }
        {   // block sizes
            OutputStage* s1 = make (sr, 9.0f, true), *s2 = make (sr, 9.0f, true);
            auto in = sine (sec, 0.7f, 440.0, sr); for (size_t i = 0; i < in.size(); ++i) in[i] *= (float) (0.3 + 0.7 * std::fabs (std::sin (3.0 * (double) i / sr)));
            auto l1 = in, r1 = in, l2 = in, r2 = in;
            s1->process (l1.data(), r1.data(), (int) l1.size());
            const int bs[] = { 1, 2, 7, 32, 64, 128, 17, 3, 500 };
            size_t pos = 0; int k = 0;
            while (pos < l2.size()) { const int n = (int) std::min ((size_t) bs[k++ % 9], l2.size() - pos); s2->process (l2.data() + pos, r2.data() + pos, n); pos += (size_t) n; }
            bool exact = true; for (size_t i = 0; i < l1.size(); ++i) exact = exact && l1[i] == l2[i] && r1[i] == r2[i];
            check (exact, "ON: any block sizes give the same samples to the bit");
            delete s1; delete s2;
        }
        // ---------------- (c) attack, release, smoothness ----------------
        {   // a single crest: the reduction reaches the target exactly at the peak
            OutputStage* s = make (sr, 0.0f, true);
            const size_t at = (size_t) (0.5 * sr);
            std::vector<float> l ((size_t) sr, 0.0f);
            for (size_t i = at - 200; i <= at + 200; ++i) l[i] = 2.0f * (float) std::pow (std::cos (kPi * ((double) i - (double) at) / 400.0), 2.0);
            auto r = l; auto g = runTrace (*s, l, r);
            const size_t out = at + (size_t) D;
            checkf (l[out] <= C && l[out] >= 0.9999f * C, "attack: the crest comes out at the ceiling (%.7f, ceiling %.7f), not below and not above", (double) l[out], (double) C);
            checkf (std::fabs (g[out] - C / 2.0f) < 2e-6f, "attack: the gain at the crest is the needed %.7f (got %.7f)", (double) (C / 2.0f), (double) g[out]);
            bool before = true; for (size_t i = out - (size_t) D; i < out; ++i) before = before && g[i] < 1.0f && g[i] >= g[out];
            check (before, "attack: the gain falls over the look-ahead before the crest (no step at the crest)");
            delete s;
        }
        {   // gain trace of a drum-like transient followed by a sustained chord: no sample above the ceiling, a smooth gain (bounded second difference), the two release stages
            OutputStage* s = make (sr, 0.0f, true);
            const size_t n = (size_t) (4.5 * sr);
            std::vector<float> l (n, 0.0f), r (n, 0.0f);
            for (size_t i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                double v = 0;
                if (t >= 0.2 && t < 0.4) { const double u = t - 0.2; v += 2.4 * std::exp (-u / 0.012) * std::sin (2.0 * kPi * (60.0 + 200.0 * std::exp (-u / 0.01)) * u); }   // a kick: 7 dB over
                if (t >= 1.0 && t < 3.0) for (double hz : { 110.0, 138.6, 164.8, 220.0 }) v += 0.42 * std::sin (2.0 * kPi * hz * t);                                              // a chord: up to 5 dB over
                if (t >= 3.0) v += 0.05 * std::sin (2.0 * kPi * 300.0 * t);
                l[i] = (float) v; r[i] = (float) (0.8 * v);
            }
            auto g = runTrace (*s, l, r);
            check (peakOf (l) <= C && peakOf (r) <= C && s->safetyClips() == 0, "transient + chord: never above the ceiling, the safety clip never acts");
            double d2 = 0, maxDrop = 0;
            for (size_t i = 1; i + 1 < n; ++i) d2 = std::max (d2, std::fabs ((double) g[i + 1] - 2.0 * g[i] + g[i - 1]));
            for (float v : g) maxDrop = std::max (maxDrop, 1.0 - v);
            const double bound = 4.0 * maxDrop * 4.0 / ((double) (D + 1) * (D + 1));
            checkf (d2 <= bound, "no zipper: largest second difference of the gain %.3g <= %.3g (4 x the S-ramp of the deepest drop %.3f over the look-ahead)", d2, bound, maxDrop);
            // the chord's gain does not ride on the waveform: in 5 ms steps over the chord (from its second 100 ms) the gain never rises by more than 0.3 dB (the analysis of the old limiter: 34 rises above 1 dB in 1.7 s)
            double up = 0; int bigUp = 0; const size_t st = (size_t) (0.005 * sr);
            for (size_t i = (size_t) (1.1 * sr) + (size_t) D; i + st < (size_t) (3.0 * sr); i += st) { const double d = 20.0 * std::log10 ((double) g[i + st] / (double) g[i]); up = std::max (up, d); bigUp += d > 1.0; }
            checkf (up < 0.3 && bigUp == 0, "sustained chord: the largest rise of the gain in 5 ms is %.3f dB (%.0f rises above 1 dB): no riding on the waveform", up, bigUp);
            delete s;
        }
        {   // the two release stages, measured from the gain trace: a 1 kHz sine 6 dB over the ceiling for 10 ms (a transient) and for 2 s (a sustained overload), then a quiet sine.
            // The time from the end of the hold (the last overloaded sample out of the delay line + the look-ahead window) until the reduction 1 - g has fallen to 1/e.
            double taus[2] = { 0, 0 };
            int k = 0;
            for (double len : { 0.010, 2.0 })
            {
                OutputStage* s = make (sr, 0.0f, true);
                const size_t n = (size_t) ((len + 2.5) * sr), end = (size_t) (len * sr);
                std::vector<float> l (n);
                for (size_t i = 0; i < n; ++i) l[i] = (i < end ? 2.0f * C : 0.05f) * (float) std::sin (2.0 * kPi * 1000.0 * (double) i / sr);
                auto r = l; auto g = runTrace (*s, l, r);
                const size_t i0 = end + 2 * (size_t) D + 1;
                const double g0 = 1.0 - g[i0];
                size_t i = i0; while (i < n && 1.0 - g[i] > g0 / std::exp (1.0)) ++i;
                taus[k++] = (double) (i - i0) / sr;
                delete s;
            }
            checkf (taus[0] > 0.025 && taus[0] < 0.060, "release, fast stage: after a 10 ms overload the reduction falls to 1/e in %.1f ms (design %.0f ms)", 1000.0 * taus[0], 1000.0 * OutputStage::kReleaseFastSeconds);
            checkf (taus[1] > 0.200 && taus[1] < 0.400, "release, slow stage: after a 2 s overload the reduction falls to 1/e in %.1f ms (design %.0f ms)", 1000.0 * taus[1], 1000.0 * OutputStage::kReleaseSlowSeconds);
        }
        {   // THD of a sustained sine 3 dB over the ceiling (after the release settled)
            for (double f0 : { 100.0, 1000.0 })
            {
                OutputStage* s = make (sr, 0.0f, true);
                auto l = sine (4 * sec, C * 1.4125375f, f0, sr), r = l; run (*s, l, r);
                std::vector<float> last (l.end() - sec, l.end());
                const double t = thd (last, f0, sr);
                checkf (t < (f0 < 500.0 ? 0.0005 : 0.0001), "THD of a sine 3 dB over the ceiling, %.0f Hz, the 4th second: %.4f %% (bound 0.05 %% at 100 Hz, 0.01 %% at 1 kHz)", f0, 100.0 * t);
                checkf (peakOf (last) <= C && peakOf (last) > 0.98f * C, "the limited sine sits just under the ceiling (%.5f)", (double) peakOf (last));
                delete s;
            }
        }
        {   // the loudness order: a louder input never comes out quieter (momentary max of 400 ms, unweighted) over make-up gains 0 .. 18 dB (historic test sweep)
            std::vector<float> in ((size_t) (3 * sr), 0.0f);
            for (size_t i = 0; i < in.size(); ++i)
            {
                const double t = (double) i / sr, u = std::fmod (t, 0.5);
                in[i] = (float) (0.5 * std::exp (-u / 0.03) * std::sin (2.0 * kPi * 70.0 * u) + 0.12 * (std::sin (2.0 * kPi * 220.0 * t) + std::sin (2.0 * kPi * 277.2 * t) + std::sin (2.0 * kPi * 329.6 * t)));
            }
            double prev = -1e9; bool mono = true;
            for (float db = 0.0f; db <= 18.0f; db += 1.0f)
            {
                OutputStage* s = make (sr, db, true);
                auto l = in, r = in; run (*s, l, r);
                const size_t win = (size_t) (0.4 * sr); double acc = 0, mx = 0;
                for (size_t i = 0; i < l.size(); ++i) { acc += (double) l[i] * l[i]; if (i >= win) acc -= (double) l[i - win] * l[i - win]; if (i + 1 >= win) mx = std::max (mx, acc); }
                const double lv = 10.0 * std::log10 (mx / (double) win);
                mono = mono && lv >= prev - 0.01;
                prev = lv;
                delete s;
            }
            check (mono, "loudness order: the momentary maximum never falls as the make-up gain rises 0 .. 18 dB (historic test sweep, no inversion by pumping)");
        }
        // ---------------- (d) latency, reset, the live switch ----------------
        {
            OutputStage* s = make (sr, 0.0f, true);
            std::vector<float> l (1024, 0.0f), r = l; l[0] = 0.25f; run (*s, l, r);
            bool ok = l[(size_t) D] == 0.25f; for (size_t i = 0; i < l.size(); ++i) if (i != (size_t) D) ok = ok && l[i] == 0.0f;
            check (ok && s->latencySamples() == D, "latency: an impulse comes out exactly look-ahead samples later, latencySamples () says so");
            s->setLimiter (false); s->reset();
            std::vector<float> l2 (64, 0.0f), r2 = l2; l2[0] = 0.25f; run (*s, l2, r2);
            check (l2[0] == 0.25f && s->latencySamples() == 0, "latency: off, the impulse comes out at once, latency 0");
            s->setLimiter (true); s->reset();
            std::vector<float> loud (2048, 3.0f), lr = loud; run (*s, loud, lr);
            check (s->gainReductionDb() > 9.0f, "reset: a 3.0 input is reported as about 10 dB of reduction");
            s->reset();
            std::vector<float> q (512, 0.1f), qr = q; run (*s, q, qr);
            bool clean = true; for (size_t i = 0; i < q.size(); ++i) clean = clean && q[i] == (i < (size_t) D ? 0.0f : 0.1f);
            check (clean && s->gainReductionDb() == 0.0f && s->lastGain() == 1.0f, "reset: the delay line is silent, no reduction is left");
            delete s;
        }
        for (bool toOn : { true, false })
        {   // switching while a sine plays: no click (no step larger than the sine's own plus the fade of the two paths), the new state exact after the fade
            OutputStage* s = make (sr, 0.0f, ! toOn);
            const float A = 0.5f; const double f = 330.0;
            auto in = sine (sec, A, f, sr); auto l = in, r = in;
            const size_t half = (size_t) sec / 2;
            s->process (l.data(), r.data(), (int) half);
            s->setLimiter (toOn);
            s->process (l.data() + half, r.data() + half, (int) (l.size() - half));
            double maxStep = 0; for (size_t i = half - 200; i < half + (size_t) (0.02 * sr); ++i) maxStep = std::max (maxStep, (double) std::fabs (l[i + 1] - l[i]));
            const double sineStep = 2.0 * kPi * f / sr * A, fadeStep = 2.0 * A / (OutputStage::kSwitchFadeSeconds * sr);
            bool exact = true; for (size_t i = half + (size_t) (0.02 * sr); i < l.size(); ++i) exact = exact && l[i] == (toOn ? in[i - (size_t) D] : in[i]);
            checkf (maxStep <= sineStep + fadeStep + 1e-6, toOn ? "switch on while playing: largest step %.5f <= sine %.5f + fade %.5f, then the delayed samples to the bit" : "switch off while playing: largest step %.5f <= sine %.5f + fade %.5f, then the undelayed samples to the bit", maxStep, sineStep, fadeStep);
            check (exact, toOn ? "switch on: exact limited (delayed) path after the fade" : "switch off: exact gained path (no delay) after the fade");
            delete s;
        }
        {   // switching on while far over the ceiling: within the ceiling from the switch
            OutputStage* s = make (sr, 18.0f, false);
            auto in = sine (2 * sec, 0.4f, 220.0, sr); auto l = in, r = in;
            const size_t half = (size_t) sec;
            s->process (l.data(), r.data(), (int) half);
            s->setLimiter (true);
            s->process (l.data() + half, r.data() + half, (int) half);
            check (peakOf (l, 0, half) > 2.0f && peakOf (l, half) <= C, "switch on with the signal far over full scale: from the switch no sample above the ceiling");
            delete s;
        }
    }
    // ---------------- the setting of the MPC shell: chunk member, environment ----------------
    {
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
        check (! synthyboy_mpc::outputLimiterFromChunk (base.data(), base.size(), keep) && keep, "chunk: an older chunk without the member leaves the setting as it is (the default: off)");
        keep = false;
        const std::string off = synthyboy_mpc::chunkWithOutputLimiter (base, false);
        bool v = true;
        check (synthyboy_mpc::outputLimiterFromChunk (off.data(), off.size(), v) && ! v, "chunk: a chunk that stores 0 stays off");
        const std::string bad = "{\"mpcProgram\":3,\"params\":{\"a\":1},\"mpcOutputLimiter\":7}";
        keep = true;
        check (! synthyboy_mpc::outputLimiterFromChunk (bad.data(), bad.size(), keep) && keep, "chunk: a damaged member is ignored");
        check (! synthyboy_mpc::outputLimiterFromChunk (nullptr, 0, keep), "chunk: no data");
        const std::string pre1 = "{\"format\":\"synthyboy-preset\",\"version\":1,\"meta\":{\"name\":\"X\"},\"outputLimiter\":1,\"state\":{\"params\":{\"a\":1}}}";
        const std::string pre0 = "{\"format\":\"synthyboy-preset\",\"outputLimiter\":false,\"state\":{}}";
        const std::string preNone = "{\"format\":\"synthyboy-preset\",\"state\":{},\"x\":{\"mpcOutputLimiter\":1}}";
        bool pv = false;
        check (synthyboy_mpc::outputLimiterFromPreset (pre1.data(), pre1.size(), pv) && pv, "preset: \"outputLimiter\":1 switches the limiter on");
        pv = true;
        check (synthyboy_mpc::outputLimiterFromPreset (pre0.data(), pre0.size(), pv) && ! pv, "preset: \"outputLimiter\":false switches it off");
        pv = false;
        check (! synthyboy_mpc::outputLimiterFromPreset (preNone.data(), preNone.size(), pv) && ! pv, "preset: without the member (the chunk's member is another key) nothing is set");
        check (! synthyboy_mpc::outputLimiterFromPreset (nullptr, 0, pv), "preset: no data");
        const std::string trim1 = "{\"format\":\"synthyboy-preset\",\"tierTrimDb\":1.5,\"state\":{}}";
        const std::string trimNeg = "{\"format\":\"synthyboy-preset\",\"tierTrimDb\":-2.4,\"state\":{}}";
        const std::string trimClamp = "{\"format\":\"synthyboy-preset\",\"tierTrimDb\":15.0,\"state\":{}}";
        float tv = 0.0f;
        check (synthyboy_mpc::tierTrimDbFromPreset (trim1.data(), trim1.size(), tv) && std::fabs (tv - 1.5f) < 1e-4f, "preset: \"tierTrimDb\":1.5 parses correctly");
        tv = 0.0f;
        check (synthyboy_mpc::tierTrimDbFromPreset (trimNeg.data(), trimNeg.size(), tv) && std::fabs (tv - (-2.4f)) < 1e-4f, "preset: \"tierTrimDb\":-2.4 parses correctly");
        tv = 0.0f;
        check (synthyboy_mpc::tierTrimDbFromPreset (trimClamp.data(), trimClamp.size(), tv) && std::fabs (tv - 9.0f) < 1e-4f, "preset: \"tierTrimDb\":15.0 clamped to +9 dB");
        tv = 42.0f;
        check (! synthyboy_mpc::tierTrimDbFromPreset (preNone.data(), preNone.size(), tv) && tv == 42.0f, "preset: missing tierTrimDb leaves value untouched");
        check (! synthyboy_mpc::tierTrimDbFromPreset (nullptr, 0, tv), "preset: null data");
    }
    {
        ::setenv ("SYNTHYBOY_MPC_OUTPUT_LIMITER", "0", 1);
        check (synthyboy_mpc::outputLimiterForced() == 0, "environment: SYNTHYBOY_MPC_OUTPUT_LIMITER=0 forces the limiter off");
        ::setenv ("SYNTHYBOY_MPC_OUTPUT_LIMITER", "1", 1);
        check (synthyboy_mpc::outputLimiterForced() == 1, "environment: SYNTHYBOY_MPC_OUTPUT_LIMITER=1 forces the limiter on");
        ::unsetenv ("SYNTHYBOY_MPC_OUTPUT_LIMITER");
        check (synthyboy_mpc::outputLimiterForced() == (SYNTHYBOY_MPC_OUTPUT_LIMITER != 0 ? 1 : -1), "environment: unset leaves it to the setting (a build with the limiter ON forces it on)");
    }
    // ---------------- timing: one 128 frame block at 44.1 kHz, idle (under the ceiling) and limiting (a chord 9 dB over) ----------------
    {
        const int blocks = 40000;
        auto time = [&] (float amp, bool on) {
            OutputStage s; s.setLimiter (on); s.prepare (44100.0); s.setGainDb (9.0f); s.reset();
            std::vector<float> src ((size_t) 128 * 64), l (128), r (128);
            for (size_t i = 0; i < src.size(); ++i) src[i] = amp * (float) (std::sin (0.0313 * (double) i) + 0.6 * std::sin (0.0471 * (double) i));
            double best = 1e9;
            for (int rep = 0; rep < 3; ++rep)
            {
                const auto t0 = std::chrono::steady_clock::now();
                for (int b = 0; b < blocks; ++b)
                {
                    const size_t o = (size_t) (b % 64) * 128;
                    std::copy (src.begin() + (long) o, src.begin() + (long) o + 128, l.begin());
                    std::copy (src.begin() + (long) o, src.begin() + (long) o + 128, r.begin());
                    s.process (l.data(), r.data(), 128);
                }
                best = std::min (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / blocks);
            }
            return best * 1e6;
        };
        const double idle = time (0.15f, true), lim = time (1.2f, true), off = time (1.2f, false);
        std::printf ("TIMING  per 128 frame block (44.1 kHz, 2.902 ms): idle %.3f us (%.4f %%), limiting %.3f us (%.4f %%), limiter off with the detector running %.3f us\n",
                     idle, idle / 2902.0 * 100.0, lim, lim / 2902.0 * 100.0, off);
    }
    std::printf ("%s  (%d failures)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
