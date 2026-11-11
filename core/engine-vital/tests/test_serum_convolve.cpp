// SynthyBoy - engine-vital tests: the convolution of Serum 2.1.5 (convolve.law = Serum, block B35, 13-new-keys.md 4.49).
// kCap2SerumConvolve is proven here: the same impulse file under the law Vital and under the law Serum renders another sound (off / on), and the laws measured on the real plug-in hold: the impulse is
// normalised to the peak 1 and then multiplied by IR GAIN (the level of the file does not matter), DECAY is the T60 of an exponential envelope, SIZE stretches the impulse by (SIZE / 100) ^ 0.7 (the key
// reaches 0.2 .. 5), TONE is a first order low pass (below 0) or high pass (above 0) at the measured corner. The Vital law is untouched.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "groupc_util.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
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

/** An impulse of white noise (a fixed generator) times the envelope `shape (t)`, `seconds` long. */
template <typename Shape>
std::vector<float> noiseImpulse (double seconds, float amplitude, Shape shape)
{
    std::vector<float> ir (static_cast<size_t> (seconds * kSr));
    uint32_t state = 12345u;
    for (size_t i = 0; i < ir.size(); ++i)
    {
        state = state * 1664525u + 1013904223u;
        const float noise = static_cast<float> (state >> 8) / static_cast<float> (1u << 24) * 2.0f - 1.0f;
        ir[i] = amplitude * noise * shape (static_cast<double> (i) / kSr);
    }
    return ir;
}

struct Rig
{
    MemoryContentStore store;
    std::unique_ptr<IEngine> engine;
};

/** An engine whose convolve holds `ir` (full wet) behind a short saw note; the law and the settings in `settings` are written before the note. */
std::unique_ptr<Rig> makeRig (const std::vector<float>& ir, int law, const std::vector<std::pair<const char*, float>>& settings = {})
{
    auto rig = std::make_unique<Rig>();
    rig->store.add ("test", "ir.wav", groupc::wav16 (ir));
    StateContent content;
    ContentFileRef ref;
    ref.pack = "test";
    ref.file = "ir.wav";
    ref.name = "ir";
    content.impulses.push_back (ref);
    rig->engine = createEngine (EngineKind::vital);
    rig->engine->setParam (kParamMasterPolyphony, 8.0f);
    rig->engine->prepare (kSr, 256, Quality {});
    IEngine& e = *rig->engine;
    e.setContentStore (&rig->store);
    e.setContent (content);
    setByKey (e, "osc1.wavetable", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -12.0f);
    setByKey (e, "convolve.on", 1.0f);
    setByKey (e, "convolve.ir", 1.0f);
    setByKey (e, "convolve.mix", 100.0f);
    setByKey (e, "convolve.law", static_cast<float> (law));
    for (const auto& s : settings)
        setByKey (e, s.first, s.second);
    // the builder thread partitions the impulse next to the rendering: let it finish
    sbtest::render (e, {}, 4096, 256);
    std::this_thread::sleep_for (std::chrono::milliseconds (700));
    sbtest::render (e, {}, 4096, 256);
    return rig;
}

/** A click of 5 ms (a short note) through the rig. */
sbtest::Rendered click (IEngine& e, double seconds = 2.5)
{
    setByKey (e, "env1.release", 2.0f);
    return sbtest::render (e, { sbtest::noteOn (0, 57), sbtest::noteOff (static_cast<int> (0.005 * kSr), 57) }, static_cast<int> (seconds * kSr), 256);
}

double rmsDb (const std::vector<float>& x, double t0, double t1)
{
    return 20.0 * std::log10 (std::max (1.0e-9, sbtest::rmsOf (x, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr))));
}
} // namespace

TEST (serum_convolve_capability_key_and_ranges)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities2() & kCap2SerumConvolve) != 0);
    const ParamInfo* law = findParamByKey ("convolve.law");
    CHECK (law != nullptr);
    CHECK_EQ (law->numChoices, static_cast<uint16_t> (2));
    CHECK_EQ (law->def, 0.0f); // Vital: every older patch sounds as before
    CHECK_EQ (requiredCaps2 (static_cast<size_t> (findParamIndex (law->id))), static_cast<EngineCaps2Mask> (kCap2SerumConvolve));
    CHECK_EQ (std::string (capName2 (kCap2SerumConvolve)), "serum_convolve");
    const ParamInfo* gain = findParamByKey ("convolve.ir_gain");
    CHECK (gain != nullptr);
    CHECK_EQ (gain->min, -40.0f); // the IR gain knob of Serum reaches -34 dB
}

TEST (serum_convolve_law_off_and_on_changes_the_sound_kCap2SerumConvolve)
{
    // a quiet impulse file (peak 0.02): the Vital law convolves the file as it is, the Serum law normalises it to the peak 1 first (and applies the IR gain 0 dB here)
    const auto ir = noiseImpulse (0.4, 0.02f, [] (double t) { return static_cast<float> (std::exp (-t * 8.0)); });
    auto vital = makeRig (ir, 0), serum = makeRig (ir, 1);
    const auto a = click (*vital->engine), b = click (*serum->engine);
    CHECK (rmsDb (b.left, 0.0, 0.3) > rmsDb (a.left, 0.0, 0.3) + 20.0);   // 34 dB louder: the peak of the file was 0.02
}

TEST (serum_convolve_level_does_not_depend_on_the_level_of_the_file_and_follows_ir_gain)
{
    const auto shape = [] (double t) { return static_cast<float> (std::exp (-t * 8.0)); };
    auto loud = makeRig (noiseImpulse (0.4, 0.8f, shape), 1), quiet = makeRig (noiseImpulse (0.4, 0.05f, shape), 1);
    const double a = rmsDb (click (*loud->engine).left, 0.0, 0.3), b = rmsDb (click (*quiet->engine).left, 0.0, 0.3);
    CHECK_NEAR (a, b, 0.3);
    auto minus12 = makeRig (noiseImpulse (0.4, 0.8f, shape), 1, { { "convolve.ir_gain", -12.0f } });
    CHECK_NEAR (rmsDb (click (*minus12->engine).left, 0.0, 0.3), a - 12.0, 0.4);
    auto minus36 = makeRig (noiseImpulse (0.4, 0.8f, shape), 1, { { "convolve.ir_gain", -36.0f } });   // below the former range of -24 dB
    CHECK_NEAR (rmsDb (click (*minus36->engine).left, 0.0, 0.3), a - 36.0, 0.6);
}

TEST (serum_convolve_decay_is_a_t60_in_seconds)
{
    // an impulse of constant noise, 2 s long; DECAY 1.25 % = 0.5 s: the wet signal falls by 60 dB per 0.5 s (a click: 5 ms)
    const auto ir = noiseImpulse (2.0, 0.5f, [] (double) { return 1.0f; });
    auto rig = makeRig (ir, 1, { { "convolve.decay", 1.25f } });
    const auto out = click (*rig->engine, 1.0);
    const double early = rmsDb (out.left, 0.05, 0.10), late = rmsDb (out.left, 0.30, 0.35);
    CHECK_NEAR (early - late, 60.0 * 0.25 / 0.5, 4.0);   // the windows are 0.25 s apart: 30 dB
    // 100 % = no envelope
    auto full = makeRig (ir, 1, { { "convolve.decay", 100.0f } });
    const auto flat = click (*full->engine, 1.0);
    CHECK_NEAR (rmsDb (flat.left, 0.05, 0.10) - rmsDb (flat.left, 0.30, 0.35), 0.0, 1.5);
}

TEST (serum_convolve_size_stretches_by_the_size_to_the_power_0_7)
{
    // a click in the impulse at 0.1 s; the knob 71.53 % is the factor 2 (0.2 * 25 ^ 0.7153), 50 % the original, 28.47 % the factor 0.5
    std::vector<float> ir (static_cast<size_t> (0.3 * kSr), 0.0f);
    ir[static_cast<size_t> (0.1 * kSr)] = 0.9f;
    const auto peakAt = [&] (float knob)
    {
        auto rig = makeRig (ir, 1, { { "convolve.size", knob } });
        const auto out = click (*rig->engine, 0.8);
        size_t at = 0;
        float best = 0.0f;
        for (size_t i = static_cast<size_t> (0.02 * kSr); i < out.left.size(); ++i)
            if (std::fabs (out.left[i]) > best)
            {
                best = std::fabs (out.left[i]);
                at = i;
            }
        return static_cast<double> (at) / kSr;
    };
    CHECK_NEAR (peakAt (50.0f), 0.1, 0.012);
    CHECK_NEAR (peakAt (71.53f), 0.2, 0.015);
    CHECK_NEAR (peakAt (28.47f), 0.05, 0.012);
}

TEST (serum_convolve_tone_is_a_first_order_filter_at_the_measured_corner)
{
    const auto ir = noiseImpulse (1.0, 0.5f, [] (double t) { return static_cast<float> (std::exp (-t * 6.0)); });
    const auto band = [&] (const std::vector<float>& x, double lo, double hi)
    {
        const auto sp = sbtest::powerSpectrum (x, static_cast<size_t> (0.02 * kSr), 32768, 65536);
        double sum = 0.0;
        for (size_t k = static_cast<size_t> (lo / kSr * 65536.0); k < static_cast<size_t> (hi / kSr * 65536.0); ++k)
            sum += sp[k];
        return 10.0 * std::log10 (std::max (1.0e-30, sum));
    };
    auto flat = makeRig (ir, 1), low = makeRig (ir, 1, { { "convolve.tone", -60.0f } }), high = makeRig (ir, 1, { { "convolve.tone", 60.0f } });
    const auto f = click (*flat->engine, 1.0), l = click (*low->engine, 1.0), h = click (*high->engine, 1.0);
    // TONE -60: a low pass at 1044 Hz: 6 dB at 1 kHz (|1 + j|) is -3 dB, at 8 .. 9 kHz (the ratio 8.1) -18.2 dB
    CHECK_NEAR (band (l.left, 8000.0, 9000.0) - band (f.left, 8000.0, 9000.0), -18.2, 2.5);
    CHECK_NEAR (band (l.left, 200.0, 300.0) - band (f.left, 200.0, 300.0), -0.2, 1.0);
    // TONE +60: a high pass at 626 Hz: at 100 .. 150 Hz (the ratio 0.2) -14 dB, at 8 .. 9 kHz nothing
    CHECK_NEAR (band (h.left, 100.0, 150.0) - band (f.left, 100.0, 150.0), -14.0, 3.0);
    CHECK_NEAR (band (h.left, 8000.0, 9000.0) - band (f.left, 8000.0, 9000.0), 0.0, 0.8);
}
