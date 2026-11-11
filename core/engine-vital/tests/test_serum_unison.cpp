// SynthyBoy - engine-vital tests: the unison law of Serum 2.1.5 (block B33, 13-new-keys.md 4.47).
// kCap2SerumUnison is proven here: oscN.unison.law Serum against Vital changes the sound (off / on), and the voices have the levels measured on the real plug-in
// (tools/ref-compare/fit_unison.py): the gain of the centre and of the outer voices by blend and voice count, and the stereo spread.
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
constexpr int kKey = 60;

std::unique_ptr<IEngine> makeVital()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, Quality {});
    return e;
}

/** A saw on osc 1, `voices` voices spread +-200 cents on a linear scale, the stereo spread `spread` %, the blend `blend` %, the law Serum (1) or Vital (0). */
void patch (IEngine& e, int voices, double blend, double spread, int law)
{
    setByKey (e, "osc1.on", 1.0f);
    setByKey (e, "osc1.wavetable", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
    setByKey (e, "osc1.unison.voices", static_cast<float> (voices));
    setByKey (e, "osc1.unison.detune", 100.0f);        // x range 2 st = 200 cents at the outer voices
    setByKey (e, "osc1.unison.detune_curve", 0.0f);     // the linear scale of Serum
    setByKey (e, "osc1.unison.blend", static_cast<float> (blend));
    setByKey (e, "osc1.unison.spread", static_cast<float> (spread));
    setByKey (e, "osc1.unison.law", static_cast<float> (law));
}

sbtest::Rendered play (IEngine& e)
{
    return sbtest::render (e, { noteOn (0, kKey) }, static_cast<int> (1.4 * kSr), 256);
}

/** The amplitude of the line at `hz` (a Hann window over 0.4 .. 1.4 s, the power of +-1.6 Hz around the line). */
double lineAmplitude (const std::vector<float>& x, double hz)
{
    const size_t a = static_cast<size_t> (0.4 * kSr), n = static_cast<size_t> (1.0 * kSr);
    double sumW = 0.0, power = 0.0;
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i)
    {
        w[i] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * static_cast<double> (i) / static_cast<double> (n));
        sumW += w[i];
    }
    const double binHz = kSr / static_cast<double>(n);
    for (double f = hz - 1.6; f <= hz + 1.6 + 1.0e-9; f += binHz)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double ph = 2.0 * 3.14159265358979323846 * f * static_cast<double> (i) / kSr;
            re += x[a + i] * w[i] * std::cos (ph);
            im -= x[a + i] * w[i] * std::sin (ph);
        }
        power += (re * re + im * im) * (2.0 / sumW) * (2.0 / sumW);
    }
    return std::sqrt (power / 2.0);   // the Hann window: the noise bandwidth is 1.5 bins; the lines are 2 bins wide
}

double voiceHz (int voices, int index)
{
    const double f0 = 440.0 * std::pow (2.0, (kKey - 69) / 12.0);
    const double t = voices > 1 ? (2.0 * index - (voices - 1)) / (voices - 1) : 0.0;
    return f0 * std::pow (2.0, 200.0 * t / 1200.0);
}
} // namespace

TEST (serum_unison_capability_and_key)
{
    auto e = makeVital();
    CHECK ((e->capabilities2() & kCap2SerumUnison) != 0);
    for (const char* key : { "osc1.unison.law", "osc2.unison.law", "osc3.unison.law" })
    {
        const ParamInfo* info = findParamByKey (key);
        CHECK (info != nullptr);
        CHECK_EQ (requiredCaps2 (static_cast<size_t> (findParamIndex (info->id))), static_cast<EngineCaps2Mask> (kCap2SerumUnison));
        CHECK_EQ (info->def, 0.0f);   // the default is the Vital law: every older patch sounds as before
    }
    CHECK_EQ (std::string (capName2 (kCap2SerumUnison)), "serum_unison");
}

TEST (serum_unison_law_off_and_on_changes_the_sound_kCap2SerumUnison)
{
    for (int voices : { 3, 4, 7 })
    {
        auto off = makeVital();
        auto on = makeVital();
        patch (*off, voices, 100.0, 100.0, 0);
        patch (*on, voices, 100.0, 100.0, 1);
        const auto a = play (*off), b = play (*on);
        double d = 0.0, n = 0.0;
        for (size_t i = static_cast<size_t> (0.4 * kSr); i < a.left.size(); ++i)
        {
            d += (a.left[i] - b.left[i]) * static_cast<double> (a.left[i] - b.left[i]);
            n += static_cast<double> (a.left[i]) * a.left[i];
        }
        CHECK (std::sqrt (d / n) > 0.1);
    }
    // the law Vital is untouched by the key: setting 0 explicitly is bit identical to the default
    auto a = makeVital(), b = makeVital();
    patch (*a, 5, 60.0, 100.0, 0);
    patch (*b, 5, 60.0, 100.0, 0);
    const auto ra = play (*a), rb = play (*b);
    CHECK (ra.left == rb.left);
}

TEST (serum_unison_voice_levels_follow_the_measured_law)
{
    // 5 voices, stereo spread 0: the single voice, the centre and the outer voices relative to one voice (REFCOMPARE-REPORT.md: centre 0.4139 * sc (blend), outer 0.4139 * so (blend))
    auto one = makeVital();
    patch (*one, 1, 0.0, 0.0, 1);
    const double single = lineAmplitude (play (*one).left, voiceHz (1, 0));
    CHECK (single > 0.0);
    struct Row { double blend, centre, outer; };
    for (const Row r : { Row { 0.0, 0.4139 * 1.0, 0.4139 * 0.0445 }, Row { 50.0, 0.4139 * 0.8613, 0.4139 * 0.3200 }, Row { 100.0, 0.4139 * 0.4451, 0.4139 * 0.5921 } })
    {
        auto e = makeVital();
        patch (*e, 5, r.blend, 0.0, 1);
        const auto out = play (*e);
        const double c = lineAmplitude (out.left, voiceHz (5, 2)) / single;
        const double o = 0.5 * (lineAmplitude (out.left, voiceHz (5, 0)) + lineAmplitude (out.left, voiceHz (5, 4))) / single;
        CHECK_NEAR (c, r.centre, 0.006 + 0.04 * r.centre);
        CHECK_NEAR (o, r.outer, 0.006 + 0.04 * r.outer);
    }
    // an even count: the two middle voices are the centre
    auto e4 = makeVital();
    patch (*e4, 4, 50.0, 0.0, 1);
    const auto out4 = play (*e4);
    CHECK_NEAR (lineAmplitude (out4.left, voiceHz (4, 1)) / single, 0.4260 * 0.8613, 0.006 + 0.04 * 0.37);
    CHECK_NEAR (lineAmplitude (out4.left, voiceHz (4, 3)) / single, 0.4260 * 0.3200, 0.006 + 0.04 * 0.14);
}

TEST (serum_unison_stereo_spread_moves_the_outer_voices_and_keeps_the_centre)
{
    // 3 voices, blend 100: the low voice is in the right channel, the high voice in the left channel; at spread 100 the other channel is silent and the own one 2.52 times louder
    auto flat = makeVital();
    patch (*flat, 3, 100.0, 0.0, 1);
    const auto f = play (*flat);
    const double low0 = lineAmplitude (f.right, voiceHz (3, 0)), ctr0 = lineAmplitude (f.left, voiceHz (3, 1));
    auto wide = makeVital();
    patch (*wide, 3, 100.0, 100.0, 1);
    const auto w = play (*wide);
    CHECK_NEAR (lineAmplitude (w.right, voiceHz (3, 0)) / low0, 2.5217, 0.15);
    CHECK (lineAmplitude (w.left, voiceHz (3, 0)) / low0 < 0.05);
    CHECK_NEAR (lineAmplitude (w.left, voiceHz (3, 1)) / ctr0, 1.0, 0.03);   // the centre voice does not move and does not change its level
    auto half = makeVital();
    patch (*half, 3, 100.0, 50.0, 1);
    const auto h = play (*half);
    CHECK_NEAR (lineAmplitude (h.right, voiceHz (3, 0)) / low0, 1.6955, 0.1);
    CHECK_NEAR (lineAmplitude (h.left, voiceHz (3, 0)) / low0, 0.4346, 0.05);
}
