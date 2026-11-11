// SynthyBoy - engine-vital tests: block B17 (compressor limit mode and crossovers, delay HQ and offset, distortion types and key track and
// X-Shaper curves, phaser depth 2, reverb algorithms) of docs/ui-design/13-new-keys.md 4.17.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every capability bit of B17 is proven by rendering the keys off and on and asserting a measurable difference of the audio.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"
#include "compressor.h"
#include "futils.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;

std::unique_ptr<IEngine> makeVital()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, Quality {});
    (void) sbtest::render (*e, { noteOn (0, 60), noteOff (200, 60) }, 2048, 256); // the warm-up of the engine
    return e;
}

/** A plain saw, one voice, straight to the effects. */
void base (IEngine& e)
{
    setByKey (e, "osc1.wavetable", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

/** The second render after reset (the first one after prepare differs by the warm-up of the engine). */
sbtest::Rendered play (IEngine& e, double seconds = 0.8, int key = 57, bool release = false)
{
    std::vector<Event> events { noteOn (0, key) };
    if (release)
        events.push_back (noteOff (static_cast<int> (0.2 * kSr), key));
    e.reset();
    return sbtest::render (e, events, static_cast<int> (seconds * kSr), 256);
}

double rms (const std::vector<float>& v, double t0 = 0.3, double t1 = 0.7)
{
    return sbtest::rmsOf (v, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

double peak (const std::vector<float>& v, double t0 = 0.3, double t1 = 0.7)
{
    double p = 0.0;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < v.size(); ++i)
        p = std::max (p, static_cast<double> (std::fabs (v[i])));
    return p;
}

double mean (const std::vector<float>& v, double t0 = 0.3, double t1 = 0.7)
{
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < v.size(); ++i, ++n)
        sum += v[i];
    return n > 0 ? sum / static_cast<double> (n) : 0.0;
}

bool finite (const sbtest::Rendered& r)
{
    for (size_t i = 0; i < r.left.size(); ++i)
        if (! std::isfinite (r.left[i]) || ! std::isfinite (r.right[i]) || std::fabs (r.left[i]) > 100.0f || std::fabs (r.right[i]) > 100.0f)
            return false;
    return true;
}

/** sqrt (sum (a - b)^2 / sum a^2) of one channel over a window. */
double relativeDifference (const std::vector<float>& a, const std::vector<float>& b, double t0 = 0.3, double t1 = 0.7)
{
    double d = 0.0, n = 0.0;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < a.size() && i < b.size(); ++i)
    {
        const double x = static_cast<double> (a[i]) - b[i];
        d += x * x;
        n += static_cast<double> (a[i]) * a[i];
    }
    return n > 0.0 ? std::sqrt (d / n) : (d > 0.0 ? 1.0e9 : 0.0);
}

/** Energy above a frequency, as the energy of the first difference of the signal (a crude high pass). */
double highEnergy (const std::vector<float>& v, double t0 = 0.3, double t1 = 0.7)
{
    double e = 0.0;
    for (size_t i = static_cast<size_t> (t0 * kSr) + 1; i < static_cast<size_t> (t1 * kSr) && i < v.size(); ++i)
    {
        const double d = static_cast<double> (v[i]) - v[i - 1];
        e += d * d;
    }
    return e;
}

double sideRms (const sbtest::Rendered& r, double t0 = 0.3, double t1 = 0.7)
{
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < r.left.size(); ++i, ++count)
    {
        const double s = 0.5 * (static_cast<double> (r.left[i]) - r.right[i]);
        sum += s * s;
    }
    return count > 0 ? std::sqrt (sum / static_cast<double> (count)) : 0.0;
}
} // namespace

TEST (b17_capabilities_are_reported)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities() & kCapFxExtrasAll) == kCapFxExtrasAll);
    auto own = createEngine (EngineKind::own);
    CHECK ((own->capabilities() & kCapFxExtrasAll) == 0);
    for (const char* key : { "compressor.limit", "compressor.crossover_high", "delay.hq", "delay.offset", "distortion.filter_keytrack", "phaser.depth2",
                             "reverb.type", "reverb.room", "reverb.width", "reverb.er_size", "reverb.diff_a", "reverb.diff_b" })
    {
        const auto* info = findParamByKey (key);
        CHECK (info != nullptr);
        if (info != nullptr)
        {
            CHECK (e->supportsParam (info->id));
            CHECK (! own->supportsParam (info->id));
        }
    }
}

TEST (compressor_limit_mode_limits_the_peaks_and_reports_its_latency)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.unison.voices", 5.0f);
    setByKey (*e, "osc1.unison.detune", 20.0f);
    setByKey (*e, "compressor.on", 1.0f);
    setByKey (*e, "compressor.bands", 3.0f); // single band
    setByKey (*e, "compressor.mid.upper_ratio", 0.0f);
    setByKey (*e, "compressor.mid.lower_ratio", 0.0f);
    setByKey (*e, "compressor.mid.upper_threshold", -24.0f);
    setByKey (*e, "compressor.mid.lower_threshold", -80.0f);
    const auto off = play (*e);
    CHECK (finite (off));
    CHECK (e->latencySamples() == 0);

    setByKey (*e, "compressor.limit", 1.0f);
    const auto on = play (*e);
    CHECK (finite (on));
    // (the output stage of the engine has a fixed gain; the ceiling is found by the two thresholds: 12 dB are a factor of 3.98)
    setByKey (*e, "compressor.mid.upper_threshold", -12.0f);
    const auto higher = play (*e);
    CHECK (peak (off.left) > 8.0 * peak (on.left));                       // the unlimited signal is far above the ceiling
    CHECK_NEAR (peak (higher.left) / peak (on.left), 3.981, 0.04);        // the ceiling is the upper threshold
    CHECK_NEAR (peak (on.right) / peak (on.left), 1.0, 0.1);              // linked channels
    CHECK (e->latencySamples() > 0 && e->latencySamples() < 200);         // the look-ahead of 1.5 ms is reported
    CHECK (std::abs (e->latencySamples() - static_cast<int> (std::lround (0.0015 * kSr))) <= 2);

    setByKey (*e, "compressor.limit_latency_comp", 0.0f);
    (void) play (*e, 0.1);
    CHECK (e->latencySamples() == 0);                                     // not compensated: the chain does not report it
    setByKey (*e, "compressor.limit_latency_comp", 1.0f);
    setByKey (*e, "compressor.limit", 0.0f);
    (void) play (*e, 0.1);
    CHECK (e->latencySamples() == 0);

    // a signal below the ceiling passes unchanged (delayed by the look-ahead)
    setByKey (*e, "osc1.unison.voices", 1.0f);
    setByKey (*e, "osc1.level", 40.0f);
    setByKey (*e, "compressor.mid.upper_threshold", 0.0f);
    setByKey (*e, "compressor.limit", 1.0f);
    const auto passes = play (*e);
    const int lag = e->latencySamples();
    CHECK (lag > 0);
    setByKey (*e, "compressor.limit", 0.0f);
    const auto direct = play (*e);
    double d = 0.0, n = 0.0;
    for (size_t i = static_cast<size_t> (0.3 * kSr); i < static_cast<size_t> (0.7 * kSr); ++i)
    {
        const double x = static_cast<double> (passes.left[i + static_cast<size_t> (lag)]) - direct.left[i];
        d += x * x;
        n += static_cast<double> (direct.left[i]) * direct.left[i];
    }
    CHECK (std::sqrt (d / n) < 0.02);
}

TEST (lookahead_limiter_never_exceeds_its_ceiling)
{
    sbvital::LookaheadLimiter limiter;
    limiter.setSampleRate (48000);
    CHECK_EQ (limiter.lookahead(), 72);
    sbtest::Rng rng (5);
    const int n = 96000;
    std::vector<sbvital::poly_float> in (static_cast<size_t> (n)), out (static_cast<size_t> (n)), delayed (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        // a loud sine, noise bursts, single sample spikes and silence
        float v = 1.2f * std::sin (0.03f * static_cast<float> (i)) + (i % 20000 < 3000 ? 2.0f * static_cast<float> (rng.uniform() - 0.5) : 0.0f);
        if (i % 7001 == 0)
            v = 9.0f;
        if (i > 70000)
            v = 0.0f;
        in[static_cast<size_t> (i)] = sbvital::poly_float (v, -v * 0.5f, v * 0.25f, v);
    }
    const float ceiling = 0.2f;
    limiter.process (in.data(), n, sbvital::poly_float (ceiling), sbvital::poly_float (0.0005f), out.data(), delayed.data());
    float worst = 0.0f;
    for (int i = 0; i < n; ++i)
        for (size_t lane = 0; lane < 4; ++lane)
        {
            worst = std::max (worst, std::fabs (out[static_cast<size_t> (i)][lane]));
            CHECK (std::isfinite (out[static_cast<size_t> (i)][lane]));
        }
    CHECK (worst <= ceiling * 1.00001f);
    CHECK (worst > 0.9f * ceiling);
    // the delayed copy is the input, 72 samples late
    for (int i = 100; i < n; i += 997)
        CHECK_EQ (delayed[static_cast<size_t> (i)][0], in[static_cast<size_t> (i - 72)][0]);
    // after the silence the gain recovered (a fresh quiet tone passes at full level)
    CHECK (std::fabs (out[static_cast<size_t> (n - 1)][0]) < 1.0e-6f);
}

TEST (compressor_crossovers_move_the_bands)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.unison.voices", 3.0f);
    setByKey (*e, "compressor.on", 1.0f);
    setByKey (*e, "compressor.bands", 0.0f);
    // squash the low band only: the saw has energy in every band
    setByKey (*e, "compressor.low.upper_threshold", -60.0f);
    setByKey (*e, "compressor.low.upper_ratio", 100.0f);
    setByKey (*e, "compressor.mid.upper_threshold", 0.0f);
    setByKey (*e, "compressor.high.upper_threshold", 0.0f);
    const auto standard = play (*e, 0.8, 45);
    CHECK (finite (standard));
    setByKey (*e, "compressor.crossover_low", 600.0f);
    const auto wide = play (*e, 0.8, 45);
    CHECK (finite (wide));
    CHECK (relativeDifference (standard.left, wide.left) > 0.05);
    CHECK (rms (wide.left) < rms (standard.left)); // more of the spectrum is in the squashed low band
    // the explicit defaults are the engine's fixed ones
    setByKey (*e, "compressor.crossover_low", 120.0f);
    setByKey (*e, "compressor.crossover_high", 2500.0f);
    const auto again = play (*e, 0.8, 45);
    CHECK (relativeDifference (standard.left, again.left) < 5.0e-4); // (two renders of the engine differ by about 1e-4 by themselves)
    setByKey (*e, "compressor.crossover_high", 700.0f);
    CHECK (relativeDifference (standard.left, play (*e, 0.8, 45).left) > 0.0); // moves, and stays finite
}

TEST (delay_hq_and_offset)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "delay.on", 1.0f);
    setByKey (*e, "delay.mix", 100.0f);
    setByKey (*e, "delay.feedback", 0.0f);
    setByKey (*e, "delay.sync", 0.0f);
    setByKey (*e, "delay.style", 1.0f);
    setByKey (*e, "delay.time", 5.37f);   // a fractional number of samples: 257.76
    setByKey (*e, "delay.time2", 5.37f);
    setByKey (*e, "delay.filter_cutoff", 20000.0f);
    setByKey (*e, "delay.filter_spread", 0.0f);
    const auto normal = play (*e, 0.8, 76);
    setByKey (*e, "delay.hq", 1.0f);
    const auto hq = play (*e, 0.8, 76);
    CHECK (finite (hq));
    CHECK (relativeDifference (normal.left, hq.left) > 0.004);   // the interpolation of a high note differs
    CHECK (relativeDifference (normal.left, hq.left) < 0.5);     // and it is the same signal
    CHECK_NEAR (rms (hq.left), rms (normal.left), 0.1 * rms (normal.left));
    setByKey (*e, "delay.hq", 0.0f);

    // the offset multiplies the time of the right channel
    setByKey (*e, "delay.time", 203.0f);   // (not a multiple of the period of the note: 200 and 300 ms are whole periods of 220 Hz)
    setByKey (*e, "delay.time2", 203.0f);
    const auto level = play (*e, 1.0, 57);
    setByKey (*e, "delay.offset", 1.5f);
    const auto shifted = play (*e, 1.0, 57);
    CHECK (finite (shifted));
    CHECK (relativeDifference (level.left, shifted.left, 0.3, 0.9) < 1.0e-3);        // the left channel is untouched
    CHECK (relativeDifference (level.right, shifted.right, 0.3, 0.9) > 0.2);         // the right one moves
    CHECK_NEAR (rms (shifted.right, 0.3, 0.9), rms (level.right, 0.3, 0.9), 0.3 * rms (level.right, 0.3, 0.9) + 1.0e-4);
}

TEST (delay_tap_delay_mode_taps_once_then_repeats_on_the_right_time)
{
    // Serum 2 manual p.166: TAP-> DELAY sets both delays to mono and in series: the left signal fires once with no feedback (the tap), followed by
    // the right signal operating as a typical delay (feedback applied there)
    auto e = makeVital();
    base (*e);
    setByKey (*e, "delay.on", 1.0f);
    setByKey (*e, "delay.mix", 100.0f);
    setByKey (*e, "delay.feedback", 60.0f);
    setByKey (*e, "delay.sync", 0.0f);
    setByKey (*e, "delay.time", 100.0f);
    setByKey (*e, "delay.time2", 150.0f);
    setByKey (*e, "delay.filter_cutoff", 20000.0f);
    setByKey (*e, "delay.filter_spread", 0.0f);
    setByKey (*e, "delay.style", 4.0f);
    const auto render = [&] {
        e->reset();
        return sbtest::render (*e, { noteOn (0, 57), noteOff (static_cast<int> (0.03 * kSr), 57) }, static_cast<int> (1.2 * kSr), 256);
    };
    const auto r = render();
    CHECK (finite (r));
    CHECK (relativeDifference (r.left, r.right, 0.0, 1.2) < 1.0e-6); // mono
    const auto at = [&] (double from, double to) { return rms (r.left, from, to); };
    const double tap = at (0.105, 0.125);          // the tap at the left time
    const double none = at (0.04, 0.09);           // nothing before it (the dry signal is off)
    const double first = at (0.255, 0.275);        // the right delay: tap + 150 ms
    const double between = at (0.19, 0.24);        // the tap does not repeat at 200 ms
    const double second = at (0.405, 0.425);       // feedback: tap + 300 ms
    std::printf ("    tap delay: before %g, tap %g, between %g, repeat 1 %g, repeat 2 %g\n", none, tap, between, first, second);
    CHECK (tap > 20.0 * none && tap > 0.01);
    CHECK (first > 5.0 * between && first > 0.01);   // (the tail of the tap reaches into the gap)
    CHECK (second > 0.2 * first);                    // the right delay feeds itself back
    CHECK (second < first);
    // not the stereo style
    setByKey (*e, "delay.style", 1.0f);
    const auto stereo = render();
    CHECK (relativeDifference (r.left, stereo.left, 0.0, 1.2) > 0.3);
}

TEST (distortion_types_extras)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "distortion.on", 1.0f);
    setByKey (*e, "distortion.drive", 12.0f);
    setByKey (*e, "distortion.type", 0.0f);
    const auto reference = play (*e);
    CHECK (finite (reference));
    sbtest::Rendered previous = reference;
    for (int type = 6; type <= 13; ++type)
    {
        setByKey (*e, "distortion.type", static_cast<float> (type));
        const auto r = play (*e);
        if (! CHECK (finite (r)))
            std::printf ("    type %d\n", type);
        if (! CHECK (rms (r.left) > 0.01 && rms (r.left) < 4.0))
            std::printf ("    type %d rms %g\n", type, rms (r.left));
        if (! CHECK (relativeDifference (reference.left, r.left) > 0.05))
            std::printf ("    type %d is the soft clip\n", type);
        if (type == 7 || type == 8 || type == 9)
            CHECK (relativeDifference (previous.left, r.left) > 0.02);
        // the DC of the asymmetric types is taken out
        if (! CHECK (std::fabs (mean (r.left)) < 0.1 * rms (r.left) + 0.002))
            std::printf ("    type %d mean %g rms %g\n", type, mean (r.left), rms (r.left));
        previous = r;
    }
    // the type below the first new one is untouched by the extension
    setByKey (*e, "distortion.type", 5.0f);
    CHECK (finite (play (*e)));

    // the key track of the filter
    setByKey (*e, "distortion.type", 0.0f);
    setByKey (*e, "distortion.drive", 0.0f);
    setByKey (*e, "distortion.filter_order", 2.0f); // post
    setByKey (*e, "distortion.filter_cutoff", 900.0f);
    setByKey (*e, "distortion.filter_resonance", 20.0f);
    const auto none = play (*e, 0.8, 72);
    setByKey (*e, "distortion.filter_keytrack", 100.0f);
    const auto tracked = play (*e, 0.8, 72);
    CHECK (finite (tracked));
    CHECK (relativeDifference (none.left, tracked.left) > 0.05);
    CHECK (highEnergy (tracked.left) > 1.5 * highEnergy (none.left)); // the cutoff follows the key up (C5 is an octave above C4)
    const auto trackedLow = play (*e, 0.8, 48);
    setByKey (*e, "distortion.filter_keytrack", 0.0f);
    const auto noneLow = play (*e, 0.8, 48);
    CHECK (relativeDifference (noneLow.left, trackedLow.left) > 0.0 && finite (trackedLow));
}

TEST (distortion_xshaper_uses_the_drawn_curves)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "distortion.on", 1.0f);
    setByKey (*e, "distortion.drive", 0.0f);
    setByKey (*e, "master.volume", 0.0f);
    setByKey (*e, "osc1.level", 50.0f);
    setByKey (*e, "distortion.type", 12.0f); // X-Shaper
    const auto identity = play (*e);          // no curve: the identity
    CHECK (finite (identity));
    setByKey (*e, "distortion.type", 0.0f);
    setByKey (*e, "distortion.drive", -30.0f);
    const auto clipLinear = play (*e);        // soft clip at -30 dB drive is linear (times 0.03): only for the scale below
    (void) clipLinear;
    setByKey (*e, "distortion.drive", 0.0f);

    // X-Shaper (Serum 2 manual p.169): the DRIVE is the morph between the graphs A (-30 dB = 0 %) and B (+30 dB = 100 %), not a gain
    StateContent content;
    content.shaperA = { ContentPoint (0.0f, 0.0f), ContentPoint (1.0f, 0.25f) }; // B stays the identity
    e->setContent (content);
    setByKey (*e, "distortion.type", 12.0f);
    setByKey (*e, "distortion.drive", -30.0f);
    const auto quarter = play (*e);
    CHECK (finite (quarter));
    CHECK_NEAR (peak (quarter.left), 0.25 * peak (identity.left), 0.05 * peak (identity.left));
    CHECK_NEAR (rms (quarter.left), 0.25 * rms (identity.left), 0.05 * rms (identity.left));
    setByKey (*e, "distortion.drive", 30.0f);
    CHECK_NEAR (peak (play (*e).left), peak (identity.left), 0.02 * peak (identity.left));
    setByKey (*e, "distortion.drive", 0.0f); // half way: (0.25 + 1) / 2 of the identity
    CHECK_NEAR (peak (play (*e).left), 0.625 * peak (identity.left), 0.05 * peak (identity.left));

    // X-Shaper (Asym): the graph spans -1 .. 1 on both axes (the middle is silence); A takes the negative half down to a quarter, B is the identity.
    // (The asymmetric types pass a DC blocker, so the reference is the Asym type without any content.)
    const auto polarity = [] (const sbtest::Rendered& r)
    {
        double positive = 0.0, negative = 0.0;
        for (size_t i = static_cast<size_t> (0.3 * kSr); i < static_cast<size_t> (0.7 * kSr); ++i)
        {
            positive = std::max (positive, static_cast<double> (r.left[i]));
            negative = std::min (negative, static_cast<double> (r.left[i]));
        }
        return std::pair<double, double> { positive, -negative };
    };
    e->setContent (StateContent {});
    setByKey (*e, "distortion.type", 13.0f);
    setByKey (*e, "distortion.drive", -30.0f);
    const auto reference = polarity (play (*e));
    content.shaperA = { ContentPoint (0.0f, 0.375f), ContentPoint (0.5f, 0.5f), ContentPoint (1.0f, 1.0f) };
    content.shaperB = {};
    e->setContent (content);
    const auto asymA = play (*e);
    CHECK (finite (asymA));
    const auto cut = polarity (asymA);
    std::printf ("    asym: identity %g / %g, graph A %g / %g\n", reference.first, reference.second, cut.first, cut.second);
    CHECK (cut.second < 0.75 * reference.second);   // the negative half is shaped down
    CHECK (cut.first > 0.6 * reference.first);      // the positive half is still there
    setByKey (*e, "distortion.drive", 30.0f); // B: the identity brings the negative half back
    const auto back = polarity (play (*e));
    CHECK_NEAR (back.second, reference.second, 0.03 * reference.second);
    CHECK_NEAR (back.first, reference.first, 0.03 * reference.first);

    // removing the content restores the identity
    e->setContent (StateContent {});
    setByKey (*e, "distortion.type", 12.0f);
    CHECK_NEAR (peak (play (*e).left), peak (identity.left), 0.01 * peak (identity.left));
}

TEST (phaser_depth2_offsets_the_stages)
{
    // Serum 2 manual p.174: DEPTH 2 is "the offset between phaser stages": the all-pass stages are spread over depth 2 semitones around the
    // cutoff (0 = every stage at the same cutoff, as before). Both channels get the same spread.
    auto e = makeVital();
    base (*e);
    setByKey (*e, "phaser.on", 1.0f);
    setByKey (*e, "phaser.mix", 100.0f);
    setByKey (*e, "phaser.blend", 100.0f);
    setByKey (*e, "phaser.depth", 0.0f);
    setByKey (*e, "phaser.rate", 1.0f);
    setByKey (*e, "phaser.phase_offset", 0.0f);
    setByKey (*e, "phaser.feedback", 70.0f);
    const auto off = play (*e, 1.2, 50);
    CHECK (finite (off));
    CHECK (relativeDifference (off.left, off.right, 0.3, 1.1) < 1.0e-6);
    setByKey (*e, "phaser.depth2", 24.0f);
    const auto on = play (*e, 1.2, 50);
    CHECK (finite (on));
    CHECK (relativeDifference (off.left, on.left, 0.3, 1.1) > 0.05);
    CHECK (relativeDifference (off.right, on.right, 0.3, 1.1) > 0.05);
    CHECK (relativeDifference (on.left, on.right, 0.3, 1.1) < 1.0e-6); // no right-channel-only offset any more
    setByKey (*e, "phaser.depth2", -24.0f);
    const auto minus = play (*e, 1.2, 50);
    CHECK (finite (minus));
    CHECK (relativeDifference (off.left, minus.left, 0.3, 1.1) > 0.05);
}

TEST (reverb_algorithms)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "reverb.on", 1.0f);
    setByKey (*e, "reverb.mix", 100.0f);
    setByKey (*e, "reverb.decay", 1500.0f);
    setByKey (*e, "reverb.predelay", 0.0f);
    const auto render = [&] { return play (*e, 3.2, 57, true); };
    const auto vital = render();
    CHECK (finite (vital));
    const double reference = rms (vital.left, 0.5, 0.9);
    CHECK (reference > 1.0e-4);

    std::vector<sbtest::Rendered> types;
    types.push_back (vital);
    for (int type = 1; type <= 5; ++type)
    {
        setByKey (*e, "reverb.type", static_cast<float> (type));
        const auto r = render();
        types.push_back (r);
        if (! CHECK (finite (r)))
            std::printf ("    type %d\n", type);
        const double tail = rms (r.left, 0.5, 0.9);
        if (! CHECK (tail > 0.15 * reference && tail < 8.0 * reference))
            std::printf ("    type %d tail %g (vital %g)\n", type, tail, reference);
        // it decays: the late tail is below the early one, and it dies away after a stop
        if (! CHECK (rms (r.left, 2.6, 3.0) < 0.8 * rms (r.left, 0.4, 0.8)))
            std::printf ("    type %d does not decay\n", type);
        // a tail that is stereo
        CHECK (sideRms (r, 0.5, 0.9) > 0.05 * rms (r.left, 0.5, 0.9));
        for (int other = 0; other < type; ++other)
            if (! CHECK (relativeDifference (types[static_cast<size_t> (other)].left, r.left, 0.3, 0.9) > 0.3))
                std::printf ("    type %d is like type %d\n", type, other);
        e->reset();
    }

    // the decay time scales the tail
    setByKey (*e, "reverb.type", 2.0f);
    setByKey (*e, "reverb.decay", 600.0f);
    const auto shortHall = render();
    setByKey (*e, "reverb.decay", 6000.0f);
    const auto longHall = render();
    CHECK (rms (longHall.left, 2.4, 3.0) > 3.0 * rms (shortHall.left, 2.4, 3.0) + 1.0e-6);
    setByKey (*e, "reverb.decay", 1500.0f);

    // the MODE (room) of the Nitrous differs from room to room (Serum 2 manual p.177)
    setByKey (*e, "reverb.type", 4.0f);
    std::vector<sbtest::Rendered> rooms;
    for (int room = 0; room < 5; ++room)
    {
        setByKey (*e, "reverb.room", static_cast<float> (room));
        rooms.push_back (render());
        CHECK (finite (rooms.back()));
    }
    for (size_t a = 0; a < rooms.size(); ++a)
        for (size_t b = a + 1; b < rooms.size(); ++b)
            if (! CHECK (relativeDifference (rooms[a].left, rooms[b].left, 0.3, 0.9) > 0.2))
                std::printf ("    rooms %zu and %zu\n", a, b);
    // the room is inert for the other types
    setByKey (*e, "reverb.type", 2.0f);
    setByKey (*e, "reverb.room", 0.0f);
    const auto hallA = render();
    setByKey (*e, "reverb.room", 3.0f);
    CHECK (relativeDifference (hallA.left, render().left) < 1.0e-6);
    // ... and for the Basin
    setByKey (*e, "reverb.type", 5.0f);
    setByKey (*e, "reverb.room", 0.0f);
    const auto basinA = render();
    setByKey (*e, "reverb.room", 3.0f);
    CHECK (relativeDifference (basinA.left, render().left) < 1.0e-6);

    // ER size, the two diffusions and the width act on the algorithms
    const auto neutral = render();
    setByKey (*e, "reverb.er_size", 100.0f);
    CHECK (relativeDifference (neutral.left, render().left, 0.2, 0.9) > 0.03);
    setByKey (*e, "reverb.er_size", 50.0f);
    setByKey (*e, "reverb.diff_a", 0.0f);
    CHECK (relativeDifference (neutral.left, render().left, 0.2, 0.9) > 0.03);
    setByKey (*e, "reverb.diff_a", 70.0f);
    setByKey (*e, "reverb.diff_b", 100.0f);
    CHECK (relativeDifference (neutral.left, render().left, 0.2, 0.9) > 0.03);
    setByKey (*e, "reverb.diff_b", 60.0f);
    CHECK (relativeDifference (neutral.left, render().left) < 1.0e-6);
    setByKey (*e, "reverb.width", 0.0f);
    CHECK (sideRms (render(), 0.5, 0.9) < 0.01 * sideRms (neutral, 0.5, 0.9) + 1.0e-7);
    setByKey (*e, "reverb.width", 200.0f);
    CHECK (sideRms (render(), 0.5, 0.9) > 1.5 * sideRms (neutral, 0.5, 0.9));
}

TEST (reverb_width_acts_on_the_vital_reverb_too)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.unison.voices", 1.0f);
    setByKey (*e, "reverb.on", 1.0f);
    setByKey (*e, "reverb.mix", 100.0f);
    setByKey (*e, "reverb.chorus_amount", 0.0f); // the chorus of the network runs free: without it the renders are the same
    const auto neutral = play (*e, 1.5, 57, true);
    CHECK (sideRms (neutral, 0.4, 1.0) > 1.0e-5);
    setByKey (*e, "reverb.width", 0.0f);
    const auto mono = play (*e, 1.5, 57, true);
    CHECK (sideRms (mono, 0.4, 1.0) < 0.001 * sideRms (neutral, 0.4, 1.0));
    CHECK_NEAR (rms (mono.left, 0.4, 1.0), rms (neutral.left, 0.4, 1.0), 0.5 * rms (neutral.left, 0.4, 1.0));
    setByKey (*e, "reverb.width", 100.0f);
    // (the network keeps state between renders, so the neutral width is compared by its level and its side level)
    const auto again = play (*e, 1.5, 57, true);
    CHECK_NEAR (rms (again.left, 0.4, 1.0), rms (neutral.left, 0.4, 1.0), 0.15 * rms (neutral.left, 0.4, 1.0));
    CHECK_NEAR (sideRms (again, 0.4, 1.0), sideRms (neutral, 0.4, 1.0), 0.15 * sideRms (neutral, 0.4, 1.0));
    setByKey (*e, "reverb.width", 200.0f);
    CHECK (sideRms (play (*e, 1.5, 57, true), 0.4, 1.0) > 1.5 * sideRms (neutral, 0.4, 1.0));
}

TEST (b17_keys_survive_random_settings_and_type_changes)
{
    auto e = makeVital();
    sbtest::Rng rng (23);
    base (*e);
    setByKey (*e, "osc1.unison.voices", 3.0f);
    for (int round = 0; round < 24; ++round)
    {
        for (const char* module : { "compressor", "delay", "distortion", "phaser", "reverb" })
            setByKey (*e, (std::string (module) + ".on").c_str(), static_cast<float> (rng.u32() % 2));
        setByKey (*e, "compressor.limit", static_cast<float> (rng.u32() % 2));
        setByKey (*e, "compressor.limit_latency_comp", static_cast<float> (rng.u32() % 2));
        setByKey (*e, "compressor.bands", static_cast<float> (rng.u32() % 4));
        setByKey (*e, "compressor.crossover_low", 20.0f + static_cast<float> (rng.uniform() * 980.0));
        setByKey (*e, "compressor.crossover_high", 200.0f + static_cast<float> (rng.uniform() * 15800.0));
        setByKey (*e, "delay.hq", static_cast<float> (rng.u32() % 2));
        setByKey (*e, "delay.offset", 0.25f + static_cast<float> (rng.uniform() * 3.75));
        setByKey (*e, "delay.style", static_cast<float> (rng.u32() % 4));
        setByKey (*e, "distortion.type", static_cast<float> (rng.u32() % 14));
        setByKey (*e, "distortion.filter_keytrack", static_cast<float> (rng.uniform() * 200.0 - 100.0));
        setByKey (*e, "phaser.depth2", static_cast<float> (rng.uniform() * 96.0 - 48.0));
        setByKey (*e, "reverb.type", static_cast<float> (rng.u32() % 6));
        setByKey (*e, "reverb.room", static_cast<float> (rng.u32() % 5));
        setByKey (*e, "reverb.width", static_cast<float> (rng.uniform() * 200.0));
        setByKey (*e, "reverb.size", static_cast<float> (rng.uniform() * 100.0));
        setByKey (*e, "reverb.decay", 20.0f + static_cast<float> (rng.uniform() * 20000.0));
        setByKey (*e, "reverb.er_size", static_cast<float> (rng.uniform() * 100.0));
        setByKey (*e, "reverb.diff_a", static_cast<float> (rng.uniform() * 100.0));
        setByKey (*e, "reverb.diff_b", static_cast<float> (rng.uniform() * 100.0));
        setByKey (*e, "reverb.mix", 60.0f);
        const auto r = sbtest::render (*e, { noteOn (0, 40 + static_cast<int> (rng.u32() % 40)) }, static_cast<int> (0.5 * kSr), 256);
        if (! CHECK (finite (r)))
            std::printf ("    round %d\n", round);
    }
}




// REFCOMPARE round 3 (13-new-keys.md 4.48, A512): in limit mode the makeup gain comes AFTER the limiter, as in the real Serum 2.1.5 (measured: the output peaks of a limiter at -28 dB with a makeup of
// 18 dB are at -4 dB). GROUPA1 (A347) had read the manual as "the makeup drives into the limiter" (the peaks stayed at the ceiling); the limited signal now moves with the gain, peak and rms alike.
TEST (compressor_limit_mode_makeup_gain_comes_after_the_limiter)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.level", 40.0f);                                   // over the ceiling: the limiter works
    setByKey (*e, "compressor.on", 1.0f);
    setByKey (*e, "compressor.bands", 3.0f);
    setByKey (*e, "compressor.mid.upper_ratio", 0.0f);
    setByKey (*e, "compressor.mid.lower_ratio", 0.0f);
    setByKey (*e, "compressor.mid.upper_threshold", -30.0f);
    setByKey (*e, "compressor.mid.lower_threshold", -80.0f);
    setByKey (*e, "compressor.limit", 1.0f);
    setByKey (*e, "compressor.mid.gain", 0.0f);
    const auto flat = play (*e);
    setByKey (*e, "compressor.mid.gain", 12.0f);
    const auto driven = play (*e);
    setByKey (*e, "compressor.mid.gain", 36.0f);
    const auto slammed = play (*e);
    CHECK (finite (flat) && finite (driven) && finite (slammed));
    CHECK_NEAR (peak (driven.left) / peak (flat.left), 3.98, 0.4);        // +12 dB: the peaks sit 12 dB over the ceiling
    CHECK (peak (slammed.left) > 20.0 * peak (flat.left));                 // the range reaches +36 dB
    CHECK (rms (driven.left) > 3.0 * rms (flat.left));
}

// ---- GROUPA1 WP-10: delay.offset_left and reverb.feedback (13-new-keys.md 4.29) ----------------------------------------------------------------------

TEST (delay_offset_left_multiplies_the_time_of_the_left_channel)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "delay.on", 1.0f);
    setByKey (*e, "delay.mix", 100.0f);
    setByKey (*e, "delay.feedback", 0.0f);
    setByKey (*e, "delay.sync", 0.0f);
    setByKey (*e, "delay.style", 1.0f);
    setByKey (*e, "delay.time", 203.0f);   // not a multiple of the period of the note
    setByKey (*e, "delay.time2", 203.0f);
    setByKey (*e, "delay.filter_cutoff", 20000.0f);
    setByKey (*e, "delay.filter_spread", 0.0f);
    const auto level = play (*e, 1.0, 57);
    // the default is the old sound: left offset 1, bit for bit what it was
    setByKey (*e, "delay.offset_left", 1.0f);
    const auto same = play (*e, 1.0, 57);
    CHECK (relativeDifference (level.left, same.left, 0.3, 0.9) < 1.0e-3 && relativeDifference (level.right, same.right, 0.3, 0.9) < 1.0e-3);
    setByKey (*e, "delay.offset_left", 1.5f);
    (void) play (*e, 1.0, 57);   // (the delay time slews to its new value: the second render is the settled one)
    const auto shifted = play (*e, 1.0, 57);
    CHECK (finite (shifted));
    CHECK (relativeDifference (level.right, shifted.right, 0.3, 0.9) < 1.0e-3);       // the right channel is untouched
    CHECK (relativeDifference (level.left, shifted.left, 0.3, 0.9) > 0.2);            // the left one moves
    CHECK_NEAR (rms (shifted.left, 0.3, 0.9), rms (level.left, 0.3, 0.9), 0.3 * rms (level.left, 0.3, 0.9) + 1.0e-4);
    // the two offsets are independent: both at 1.5 moves both channels, and the left time is the time times the offset
    setByKey (*e, "delay.offset", 1.5f);
    (void) play (*e, 1.0, 57);
    const auto both = play (*e, 1.0, 57);
    CHECK (relativeDifference (level.right, both.right, 0.3, 0.9) > 0.2);
    CHECK (relativeDifference (shifted.left, both.left, 0.3, 0.9) < 1.0e-3);
    // the first echo comes at time x offset: an impulse like note (short) and the position of the left echo
    setByKey (*e, "delay.offset", 1.0f);
    setByKey (*e, "delay.offset_left", 2.0f);
    setByKey (*e, "osc1.unison.voices", 1.0f);
    e->reset();
    const auto click = sbtest::render (*e, { noteOn (0, 72), noteOff (static_cast<int> (0.01 * kSr), 72) }, static_cast<int> (0.8 * kSr), 256);
    const auto energy = [&] (const std::vector<float>& v, double from, double to) { return rms (v, from, to); };
    CHECK (energy (click.left, 0.40, 0.44) > 5.0 * energy (click.left, 0.20, 0.24) + 1.0e-6); // the left echo is at 406 ms, nothing at 203 ms
    CHECK (energy (click.right, 0.20, 0.24) > 5.0 * energy (click.right, 0.40, 0.44) + 1.0e-6); // the right one stays at 203 ms
}

TEST (reverb_feedback_scales_the_tail_of_nitrous_and_basin_only)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "reverb.on", 1.0f);
    setByKey (*e, "reverb.mix", 100.0f);
    setByKey (*e, "reverb.decay", 1500.0f);
    setByKey (*e, "reverb.predelay", 0.0f);
    const auto render = [&] { return play (*e, 3.2, 57, true); };
    for (const int type : { 4, 5 }) // Nitrous, Basin
    {
        setByKey (*e, "reverb.type", static_cast<float> (type));
        setByKey (*e, "reverb.feedback", 50.0f);
        (void) render();   // (a new type clears the tail: the first render after it is the warm-up one)
        const auto neutral = render();
        CHECK (finite (neutral));
        setByKey (*e, "reverb.feedback", 20.0f);
        const auto low = render();
        setByKey (*e, "reverb.feedback", 90.0f);
        const auto high = render();
        CHECK (finite (low) && finite (high));
        const double lateNeutral = rms (neutral.left, 2.0, 3.0), lateLow = rms (low.left, 2.0, 3.0), lateHigh = rms (high.left, 2.0, 3.0);
        if (! CHECK (lateLow < 0.6 * lateNeutral && lateHigh > 1.5 * lateNeutral))
            std::printf ("    type %d: late tail low %g neutral %g high %g\n", type, lateLow, lateNeutral, lateHigh);
        // 50 % is the old sound: the same as a patch that never had the key
        setByKey (*e, "reverb.feedback", 50.0f);
        CHECK (relativeDifference (neutral.left, render().left, 0.2, 3.0) < 5.0e-4);   // (two renders of the engine differ by about 1e-4 by themselves)
        e->reset();
    }
    // the other types do not have it
    for (const int type : { 1, 2, 3 }) // (the Vital reverb of type 0 modulates itself differently on every render)
    {
        setByKey (*e, "reverb.type", static_cast<float> (type));
        setByKey (*e, "reverb.feedback", 50.0f);
        const auto a = render();
        setByKey (*e, "reverb.feedback", 95.0f);
        CHECK (relativeDifference (a.left, render().left, 0.2, 3.0) < 5.0e-4);
        e->reset();
    }
}

// ---- numeric robustness of the effect chain (GROUPA1 item 0) -------------------------------------------------------------------------------
// A Serum 2 preset (compressor makeup +13.5 dB on three bands, two EQs with gain and a high Q, a Diode 1 distortion at +12.7 dB) went to NaN:
// the loud signal drove the exponent of futils::exp2 below -126, where the float exponent field wrapped (the chain case is
// fxi_loud_compressor_eq_diode_chain_stays_finite in test_fxinstances.cpp). The values here are synthetic (no preset file).

TEST (robust_exp2_stays_finite_and_monotone_for_any_exponent)
{
    using namespace sbvital;
    float previous = 0.0f;
    for (float x = -400.0f; x <= 400.0f; x += 0.37f)
    {
        const float y = futils::exp2 (poly_float (x))[0];
        CHECK (std::isfinite (y));
        CHECK (y >= 0.0f);
        CHECK (y >= previous * 0.999f); // never falls back (a wrapped exponent made garbage)
        previous = y;
    }
    // inside the old working range the value is the exact approximation of 2^x (relative error below 1e-5)
    for (float x = -100.0f; x <= 100.0f; x += 0.731f)
        CHECK (std::fabs (futils::exp2 (poly_float (x))[0] / std::exp2 (x) - 1.0f) < 1.0e-5f);
}

TEST (robust_extreme_legal_values_never_produce_nan)
{
    // every distortion type at the ends of the drive range behind the loudest compressor / EQ settings the ranges allow
    for (int type = 0; type < 14; ++type)
        for (const float drive : { -30.0f, 30.0f })
        {
            auto e = makeVital();
            base (*e);
            setByKey (*e, "compressor.on", 1.0f);
            for (const char* band : { "low", "mid", "high" })
                setByKey (*e, (std::string ("compressor.") + band + ".gain").c_str(), 30.0f);
            setByKey (*e, "eq.on", 1.0f);
            for (const char* band : { "low", "mid", "high" })
            {
                setByKey (*e, (std::string ("eq.") + band + ".gain").c_str(), 15.0f);
                setByKey (*e, (std::string ("eq.") + band + ".resonance").c_str(), 100.0f);
            }
            setByKey (*e, "distortion.on", 1.0f);
            setByKey (*e, "distortion.type", static_cast<float> (type));
            setByKey (*e, "distortion.drive", drive);
            const auto r = play (*e, 0.6, 40);
            if (! CHECK (finite (r)))
                std::printf ("    type %d drive %g\n", type, static_cast<double> (drive));
        }
}
