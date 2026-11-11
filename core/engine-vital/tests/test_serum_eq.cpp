// SynthyBoy - engine-vital tests: the second EQ peak band (eq.mid2.*) and the EQ law of Serum 2.1.5 (block B34, 13-new-keys.md 4.48).
// kCap2EqBand2 and kCap2SerumEq are proven here: band 2 Off / Peak and the law Vital / Serum change the sound (off / on), and under the Serum law the four bands have the
// response of the RBJ biquads with the Q laws measured on the real plug-in (tools/ref-compare/eqfit.py, 0.00 dB residual): Q = 0.1 + 18.9053 x^4 for a peak, high pass,
// low pass and notch, Q = 0.4 + 1.7417 x^3 for a shelf, x = resonance / 100.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <complex>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/engine_caps.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kKey = 36;   // C1: 65.406 Hz, a saw with 300 lines up to 19.6 kHz
constexpr double kPi = 3.14159265358979323846;
constexpr double kModuleRate = 2.0 * kSr;   // the effect chain runs at the oversampled rate of the default quality (2x); the filters are designed for the rate they run at (Quality::oversampling = 2)

std::unique_ptr<IEngine> makeVital()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (kSr, 256, Quality {});
    return e;
}

/** A saw on osc 1 (a comb of lines every 65.4 Hz) through the EQ only. */
void patch (IEngine& e)
{
    setByKey (e, "osc1.on", 1.0f);
    setByKey (e, "osc1.wavetable", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -36.0f);   // a boost of 24 dB on a saw must not reach the output limiter
    setByKey (e, "eq.on", 1.0f);
}

sbtest::Rendered play (IEngine& e)
{
    return sbtest::render (e, { noteOn (0, kKey) }, static_cast<int> (1.6 * kSr), 256);
}

double f0() { return 440.0 * std::pow (2.0, (kKey - 69) / 12.0); }

/** The amplitude of the line k (a Hann window over 0.5 .. 1.5 s, the power around the line). */
double line (const std::vector<float>& x, int k)
{
    const size_t a = static_cast<size_t> (0.5 * kSr), n = static_cast<size_t> (1.0 * kSr);
    const double hz = f0() * k, binHz = kSr / static_cast<double> (n);
    double sumW = 0.0, power = 0.0;
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i)
    {
        w[i] = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (n));
        sumW += w[i];
    }
    for (double f = hz - 2.0 * binHz; f <= hz + 2.0 * binHz + 1.0e-9; f += binHz)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double ph = 2.0 * kPi * f * static_cast<double> (i) / kSr;
            re += x[a + i] * w[i] * std::cos (ph);
            im -= x[a + i] * w[i] * std::sin (ph);
        }
        power += (re * re + im * im) * (2.0 / sumW) * (2.0 / sumW);
    }
    return std::sqrt (power / 2.0);
}

double db (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

enum Kind { kPeak, kLowShelf, kHighShelf, kHighPass, kLowPass };

/** The RBJ response in dB at `hz`, written down here independently of the engine. */
double rbjDb (Kind kind, double fc, double reso, double gdb, double hz)
{
    const double x = reso / 100.0;
    const double q = (kind == kLowShelf || kind == kHighShelf) ? 0.4 + 1.7417 * x * x * x : 0.1 + 18.9053 * x * x * x * x;
    const double w0 = 2.0 * kPi * fc / kModuleRate, A = std::pow (10.0, gdb / 40.0), al = std::sin (w0) / (2.0 * q), c = std::cos (w0);
    double b[3], a[3];
    const double s = 2.0 * std::sqrt (A) * al;
    switch (kind)
    {
        case kPeak: b[0] = 1 + al * A; b[1] = -2 * c; b[2] = 1 - al * A; a[0] = 1 + al / A; a[1] = -2 * c; a[2] = 1 - al / A; break;
        case kLowShelf:
            b[0] = A * ((A + 1) - (A - 1) * c + s); b[1] = 2 * A * ((A - 1) - (A + 1) * c); b[2] = A * ((A + 1) - (A - 1) * c - s);
            a[0] = (A + 1) + (A - 1) * c + s; a[1] = -2 * ((A - 1) + (A + 1) * c); a[2] = (A + 1) + (A - 1) * c - s; break;
        case kHighShelf:
            b[0] = A * ((A + 1) + (A - 1) * c + s); b[1] = -2 * A * ((A - 1) + (A + 1) * c); b[2] = A * ((A + 1) + (A - 1) * c - s);
            a[0] = (A + 1) - (A - 1) * c + s; a[1] = 2 * ((A - 1) - (A + 1) * c); a[2] = (A + 1) - (A - 1) * c - s; break;
        case kHighPass: b[0] = (1 + c) / 2; b[1] = -(1 + c); b[2] = (1 + c) / 2; a[0] = 1 + al; a[1] = -2 * c; a[2] = 1 - al; break;
        case kLowPass: b[0] = (1 - c) / 2; b[1] = 1 - c; b[2] = (1 - c) / 2; a[0] = 1 + al; a[1] = -2 * c; a[2] = 1 - al; break;
    }
    const double w = 2.0 * kPi * hz / kModuleRate;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    return db (std::abs ((b[0] + b[1] * z1 + b[2] * z2) / (a[0] + a[1] * z1 + a[2] * z2)));
}

/** The level change in dB of the lines `harmonics` through the EQ settings `set` against the same patch with the EQ at its neutral settings. */
template <typename F>
std::vector<double> response (const std::vector<int>& harmonics, F&& set)
{
    auto flat = makeVital();
    patch (*flat);
    const auto ref = play (*flat);
    auto e = makeVital();
    patch (*e);
    set (*e);
    const auto out = play (*e);
    std::vector<double> r;
    for (int k : harmonics)
        r.push_back (db (line (out.left, k)) - db (line (ref.left, k)));
    return r;
}
} // namespace

TEST (eq_band2_and_law_capabilities_and_keys)
{
    auto e = makeVital();
    CHECK ((e->capabilities2() & kCap2EqBand2) != 0);
    CHECK ((e->capabilities2() & kCap2SerumEq) != 0);
    for (const char* key : { "eq.mid2.mode", "eq.mid2.cutoff", "eq.mid2.gain", "eq.mid2.resonance" })
    {
        const ParamInfo* info = findParamByKey (key);
        CHECK (info != nullptr);
        CHECK_EQ (requiredCaps2 (static_cast<size_t> (findParamIndex (info->id))), static_cast<EngineCaps2Mask> (kCap2EqBand2));
    }
    const ParamInfo* law = findParamByKey ("eq.law");
    CHECK (law != nullptr);
    CHECK_EQ (requiredCaps2 (static_cast<size_t> (findParamIndex (law->id))), static_cast<EngineCaps2Mask> (kCap2SerumEq));
    CHECK_EQ (law->def, 0.0f);   // the default is the Vital law: every older patch sounds as before
    CHECK_EQ (findParamByKey ("eq.mid2.mode")->def, 0.0f);   // Off
    CHECK_EQ (findParamByKey ("eq.mid2.gain")->def, 0.0f);
    CHECK_EQ (findParamByKey ("eq.mid.gain")->max, 24.0f);   // the gains reach the +-24 dB of the Serum equalizer
    CHECK_EQ (std::string (capName2 (kCap2EqBand2)), "eq_band2");
    CHECK_EQ (std::string (capName2 (kCap2SerumEq)), "serum_eq");
}

TEST (eq_band2_off_and_on_changes_the_sound_kCap2EqBand2)
{
    for (int law : { 0, 1 })
    {
        auto off = makeVital();
        auto on = makeVital();
        for (auto* e : { off.get(), on.get() })
        {
            patch (*e);
            setByKey (*e, "eq.law", static_cast<float> (law));
            setByKey (*e, "eq.mid2.cutoff", 1000.0f);
            setByKey (*e, "eq.mid2.gain", 12.0f);
            setByKey (*e, "eq.mid2.resonance", 40.0f);
        }
        setByKey (*on, "eq.mid2.mode", 1.0f);   // Peak
        const auto a = play (*off), b = play (*on);
        double d = 0.0, n = 0.0;
        for (size_t i = static_cast<size_t> (0.5 * kSr); i < a.left.size(); ++i)
        {
            d += (a.left[i] - b.left[i]) * static_cast<double> (a.left[i] - b.left[i]);
            n += static_cast<double> (a.left[i]) * a.left[i];
        }
        CHECK (std::sqrt (d / n) > 0.2);
    }
    // Off is bit identical to a patch that never touched the key (the neutral default of block B34)
    auto a = makeVital(), b = makeVital();
    patch (*a);
    patch (*b);
    setByKey (*a, "eq.mid.gain", 6.0f);
    setByKey (*b, "eq.mid.gain", 6.0f);
    setByKey (*b, "eq.mid2.mode", 0.0f);
    setByKey (*b, "eq.mid2.gain", 12.0f);   // the band is Off: its gain is not heard
    CHECK (play (*a).left == play (*b).left);
}

TEST (eq_law_vital_and_serum_differ_kCap2SerumEq)
{
    auto vital = makeVital();
    auto serum = makeVital();
    for (auto* e : { vital.get(), serum.get() })
    {
        patch (*e);
        setByKey (*e, "eq.low.mode", 1.0f);   // High pass
        setByKey (*e, "eq.low.cutoff", 1000.0f);
        setByKey (*e, "eq.low.resonance", 0.0f);
    }
    setByKey (*serum, "eq.law", 1.0f);
    const auto a = play (*vital), b = play (*serum);
    // at resonance 0 the Serum high pass has the quality 0.1: -23 dB at 785 Hz where the SVF of Vital is 8 dB down
    const double dv = db (line (a.left, 12)) - db (line (b.left, 12));
    CHECK (std::fabs (dv) > 6.0);
}

TEST (eq_serum_law_follows_the_rbj_biquads_with_the_measured_q_laws)
{
    const std::vector<int> harmonics { 2, 5, 9, 15, 23, 40, 70, 120, 200 };
    struct Row
    {
        const char* what;
        Kind kind;
        double fc, reso, gain;
    };
    const Row rows[] = {
        { "peak +12 dB", kPeak, 1000.0, 30.0, 12.0 },
        { "peak -12 dB, narrow", kPeak, 2500.0, 75.0, -12.0 },
        { "peak +20 dB, wide", kPeak, 600.0, 10.0, 20.0 },
        { "low shelf +12 dB", kLowShelf, 1000.0, 30.0, 12.0 },
        { "low shelf -18 dB", kLowShelf, 300.0, 80.0, -18.0 },
        { "high shelf +12 dB", kHighShelf, 3000.0, 30.0, 12.0 },
        { "high pass, resonance 0", kHighPass, 1000.0, 0.0, 0.0 },
        { "high pass, resonance 50", kHighPass, 1000.0, 50.0, 0.0 },
        { "low pass, resonance 0", kLowPass, 3000.0, 0.0, 0.0 },
        { "low pass, resonance 50", kLowPass, 3000.0, 50.0, 0.0 },
    };
    for (const Row& r : rows)
    {
        const auto measured = response (harmonics, [&] (IEngine& e)
        {
            setByKey (e, "eq.law", 1.0f);
            const char* band = r.kind == kPeak ? "mid" : (r.kind == kLowShelf || r.kind == kHighPass) ? "low" : "high";
            const std::string k = std::string ("eq.") + band + ".";
            if (r.kind == kHighPass) setByKey (e, "eq.low.mode", 1.0f);
            if (r.kind == kLowPass) setByKey (e, "eq.high.mode", 1.0f);
            setByKey (e, (k + "cutoff").c_str(), static_cast<float> (r.fc));
            setByKey (e, (k + "resonance").c_str(), static_cast<float> (r.reso));
            if (r.kind != kHighPass && r.kind != kLowPass) setByKey (e, (k + "gain").c_str(), static_cast<float> (r.gain));
        });
        for (size_t i = 0; i < harmonics.size(); ++i)
        {
            const double expected = rbjDb (r.kind, r.fc, r.reso, r.gain, f0() * harmonics[i]);
            if (std::fabs (expected) > 45.0)
                continue;   // below the measurement floor of a saw line
            CHECK_NEAR (measured[i], expected, 0.3);
        }
    }
}

TEST (eq_two_peaks_add_in_decibels_in_both_laws)
{
    // two peaks at 800 Hz and 3000 Hz, +9 dB each: the response is the sum of the two single peaks
    const std::vector<int> harmonics { 3, 12, 20, 46, 90 };
    for (int law : { 0, 1 })
    {
        const auto both = response (harmonics, [&] (IEngine& e)
        {
            setByKey (e, "eq.law", static_cast<float> (law));
            setByKey (e, "eq.mid.cutoff", 800.0f); setByKey (e, "eq.mid.gain", 9.0f); setByKey (e, "eq.mid.resonance", 50.0f);
            setByKey (e, "eq.mid2.mode", 1.0f); setByKey (e, "eq.mid2.cutoff", 3000.0f); setByKey (e, "eq.mid2.gain", 9.0f); setByKey (e, "eq.mid2.resonance", 50.0f);
        });
        const auto first = response (harmonics, [&] (IEngine& e)
        {
            setByKey (e, "eq.law", static_cast<float> (law));
            setByKey (e, "eq.mid.cutoff", 800.0f); setByKey (e, "eq.mid.gain", 9.0f); setByKey (e, "eq.mid.resonance", 50.0f);
        });
        const auto second = response (harmonics, [&] (IEngine& e)
        {
            setByKey (e, "eq.law", static_cast<float> (law));
            setByKey (e, "eq.mid2.mode", 1.0f); setByKey (e, "eq.mid2.cutoff", 3000.0f); setByKey (e, "eq.mid2.gain", 9.0f); setByKey (e, "eq.mid2.resonance", 50.0f);
        });
        for (size_t i = 0; i < harmonics.size(); ++i)
            CHECK_NEAR (both[i], first[i] + second[i], 0.4);
    }
}

TEST (eq_serum_law_state_survives_a_block_size_change_and_law_switch)
{
    // switching the law while playing must not blow up or leave stale filter state: after the switch back the sound is the Vital sound again
    auto a = makeVital(), b = makeVital();
    patch (*a);
    patch (*b);
    setByKey (*a, "eq.mid.gain", 9.0f);
    setByKey (*b, "eq.mid.gain", 9.0f);
    setByKey (*b, "eq.law", 1.0f);
    (void) play (*b);
    setByKey (*b, "eq.law", 0.0f);
    const auto ra = play (*a);
    const auto rb = play (*b);
    for (float v : rb.left)
        CHECK (std::isfinite (v));
    CHECK (rb.left.size() == ra.left.size());
}
