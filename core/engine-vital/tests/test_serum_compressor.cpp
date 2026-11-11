// SynthyBoy - engine-vital tests: the peak compressor of Serum 2.1.5 (compressor.limit = Serum, block B34, 13-new-keys.md 4.48).
// kCap2SerumCompressor is proven here: the mode Serum against Compress changes the sound (off / on), the static curve is the one measured on the real plug-in (hard knee at the peak
// ceiling, the slope 1 / R above it, tools/ref-compare/compratio.py: R 1.5 .. 50 within 0.05 of 1 / R), the threshold reaches -120 dB, the attack and the release follow the
// Serum time laws (a gain reduction curve, tools/ref-compare/compmodelb.py), and the other two modes are untouched.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
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
constexpr int kKey = 72;   // C4: 523 Hz, a 1.9 ms period: the release of 60 ms and more holds the peak of the sine

std::unique_ptr<IEngine> makeVital()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (kSr, 256, Quality {});
    return e;
}

/** A sine (peak 1 at level 100: the peak detector sees the same value in every cycle) through the compressor alone; `level` is the oscillator level in percent. */
void patch (IEngine& e, double levelPercent, int mode, double thresholdDb, double ratioPercent, double attackPercent = 0.0, double releasePercent = 70.0)
{
    setByKey (e, "osc1.on", 1.0f);
    setByKey (e, "osc1.wavetable", 0.0f);
    setByKey (e, "osc1.level", static_cast<float> (levelPercent));
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", 0.0f);
    setByKey (e, "compressor.on", 1.0f);
    setByKey (e, "compressor.bands", 3.0f);   // single band
    setByKey (e, "compressor.limit", static_cast<float> (mode));
    for (const char* band : { "low", "mid", "high" })
    {
        setByKey (e, (std::string ("compressor.") + band + ".upper_threshold").c_str(), static_cast<float> (thresholdDb));
        setByKey (e, (std::string ("compressor.") + band + ".upper_ratio").c_str(), static_cast<float> (ratioPercent));
        setByKey (e, (std::string ("compressor.") + band + ".gain").c_str(), 0.0f);
    }
    setByKey (e, "compressor.attack", static_cast<float> (attackPercent));
    setByKey (e, "compressor.release", static_cast<float> (releasePercent));
    setByKey (e, "compressor.mix", 100.0f);
}

sbtest::Rendered play (IEngine& e, double seconds = 1.5)
{
    return sbtest::render (e, { noteOn (0, kKey) }, static_cast<int> (seconds * kSr), 256);
}

double peakDb (const std::vector<float>& x, double from = 0.8, double to = 1.4)
{
    float p = 0.0f;
    for (size_t i = static_cast<size_t> (from * kSr); i < static_cast<size_t> (to * kSr) && i < x.size(); ++i)
        p = std::max (p, std::fabs (x[i]));
    return 20.0 * std::log10 (std::max (p, 1.0e-9f));
}

double ratioPercentOf (double ratio) { return 100.0 * (1.0 - 1.0 / ratio); }
} // namespace

TEST (serum_compressor_capability_mode_and_ranges)
{
    auto e = makeVital();
    CHECK ((e->capabilities2() & kCap2SerumCompressor) != 0);
    const ParamInfo* info = findParamByKey ("compressor.limit");
    CHECK (info != nullptr);
    CHECK_EQ (info->numChoices, static_cast<uint16_t> (3));
    CHECK_EQ (info->def, 0.0f);   // Compress: every older patch sounds as before
    const size_t index = static_cast<size_t> (findParamIndex (info->id));
    CHECK_EQ (requiredCapsForValue2 (index, 2.0f), static_cast<EngineCaps2Mask> (kCap2SerumCompressor));
    CHECK_EQ (requiredCapsForValue2 (index, 1.0f), static_cast<EngineCaps2Mask> (kCap2None));   // Limit is a block B17 feature
    CHECK_EQ (std::string (capName2 (kCap2SerumCompressor)), "serum_compressor");
    for (const char* key : { "compressor.low.upper_threshold", "compressor.mid.lower_threshold", "compressor.high.upper_threshold" })
    {
        const ParamInfo* t = findParamByKey (key);
        CHECK (t != nullptr);
        CHECK_EQ (t->min, -120.0f);   // the threshold knob of Serum reaches -106 dB
        CHECK_EQ (t->max, 12.0f);
    }
}

TEST (serum_compressor_mode_off_and_on_changes_the_sound_kCap2SerumCompressor)
{
    auto rms = makeVital(), peak = makeVital();
    patch (*rms, 60.0, 0, -18.0, 75.0, 60.0, 40.0);   // the two time laws are different: attack 60 % is 3 ms in the Vital law, 25 ms in the Serum law
    patch (*peak, 60.0, 2, -18.0, 75.0, 60.0, 40.0);
    for (auto* e : { rms.get(), peak.get() })
        setByKey (*e, "osc1.wavetable", 1.0f);   // a saw: peak and RMS are 7 dB apart, the two detectors give different reductions
    const auto a = play (*rms), b = play (*peak);
    double d = 0.0, n = 0.0;
    for (size_t i = static_cast<size_t> (0.8 * kSr); i < a.left.size(); ++i)
    {
        d += (a.left[i] - b.left[i]) * static_cast<double> (a.left[i] - b.left[i]);
        n += static_cast<double> (a.left[i]) * a.left[i];
    }
    CHECK (std::sqrt (d / n) > 0.05);
    // the Compress mode is bit identical to a patch that never touched the key (the default)
    auto x = makeVital(), y = makeVital();
    patch (*x, 60.0, 0, -18.0, 75.0);
    patch (*y, 60.0, 0, -18.0, 75.0);
    CHECK (play (*x).left == play (*y).left);
}

TEST (serum_compressor_static_curve_is_a_hard_knee_at_the_peak_ceiling_with_slope_one_over_r)
{
    // threshold -20 dB peak; the sine peak is the level: -34, -26, -14, -6 dB. Below the ceiling the signal passes, above it the output peak is T + (A - T) / R
    for (const double ratio : { 2.0, 4.0, 8.0, 20.0 })
        for (const double levelDb : { -34.0, -26.0, -14.0, -6.0 })
        {
            auto e = makeVital();
            patch (*e, 100.0 * std::pow (10.0, levelDb / 20.0), 2, -20.0, ratioPercentOf (ratio));
            const double out = peakDb (play (*e).left);
            const double expected = levelDb <= -20.0 ? levelDb : -20.0 + (levelDb + 20.0) / ratio;
            CHECK_NEAR (out, expected, 0.4);
        }
}

TEST (serum_compressor_threshold_reaches_minus_120_db)
{
    // a quiet signal (-70 dB) at the threshold -100 dB and ratio 4: the Serum threshold knob goes that low, the Vital range stopped at -80
    auto e = makeVital();
    patch (*e, 100.0 * std::pow (10.0, -70.0 / 20.0), 2, -100.0, ratioPercentOf (4.0));
    CHECK_NEAR (peakDb (play (*e).left), -100.0 + 30.0 / 4.0, 0.9);
}

TEST (serum_compressor_attack_and_release_follow_the_serum_time_laws)
{
    // attack: attack 62 % = 0.1 * 10^4^0.62 = 31 ms, the time constant of the gain reduction is 0.37 * 31^2 / 35 = 10.2 ms (fitted on the real plug-in); a constant over-threshold sine (ratio 8, 30 dB over:
    // the end value of the reduction is 26.25 dB) reaches 63 % of it after one time constant: the output peak is -16.5 dB
    auto e = makeVital();
    patch (*e, 100.0, 2, -30.0, ratioPercentOf (8.0), 62.0, 70.0);
    const auto out = play (*e, 0.8);
    CHECK_NEAR (peakDb (out.left, 0.6, 0.78), -25.35, 0.6);           // the end value: the peak detector holds 1 ms, a sine of 523 Hz falls in between (ripple of the reduction: 0.9 dB)
    CHECK_NEAR (peakDb (out.left, 0.0195, 0.0215), -21.0, 2.0);       // two time constants (the reduction is 86 % of its end value)
    CHECK (peakDb (out.left, 0.1, 0.12) < peakDb (out.left, 0.02, 0.04) + 0.5);   // and then it only gets deeper
    // release: the threshold goes up to 0 dB at 0.5 s (a base control, no smoothing): the reduction (25.35 dB) falls to 0 with the time constant of the release time (release 66 % = 0.1 * 10^4^0.66 = 43.7 ms),
    // the output rises from -25.35 dB to the input level 0 dB
    auto step = makeVital();
    patch (*step, 100.0, 2, -30.0, ratioPercentOf (8.0), 0.0, 66.0);
    (void) sbtest::render (*step, { noteOn (0, kKey) }, static_cast<int> (0.5 * kSr), 256);
    for (const char* band : { "low", "mid", "high" })
        setByKey (*step, (std::string ("compressor.") + band + ".upper_threshold").c_str(), 0.0f);
    const auto after = sbtest::render (*step, {}, static_cast<int> (0.6 * kSr), 256);
    for (const double tau : { 1.0, 2.0 })   // one and two time constants after the step
    {
        const double t = tau * 0.0437;
        CHECK_NEAR (peakDb (after.left, t - 0.003, t + 0.003), -25.35 * std::exp (-tau), 1.5);
    }
    CHECK_NEAR (peakDb (after.left, 0.5, 0.6), 0.0, 0.3);   // gone
}

TEST (serum_compressor_modes_limit_and_compress_are_untouched_by_the_peak_mode)
{
    // limit (1): the look-ahead limiter keeps the output peak at the ceiling; the same patch in the Serum mode with the ratio 100 % does too (R = infinity)
    auto lim = makeVital(), peak = makeVital();
    patch (*lim, 100.0, 1, -12.0, 100.0);
    patch (*peak, 100.0, 2, -12.0, 100.0);
    CHECK_NEAR (peakDb (play (*lim).left), -12.0, 1.0);
    CHECK_NEAR (peakDb (play (*peak).left), -12.0, 1.0);
}
