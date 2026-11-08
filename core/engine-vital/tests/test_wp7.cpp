// SynthyBoy - engine-vital tests: WP7 (Utility, Hyper / Dimension, Bode, Convolve, the three splitters) and WP8 (reverb force enable) of
// docs/ui-design/13-new-keys.md.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every capability bit WP7 and WP8 set is proven by rendering the keys off and on and asserting a measurable difference of the audio.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;

std::unique_ptr<IEngine> makeVital (const Quality& q = Quality {})
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, q);
    return e;
}

/** A wide stereo saw (7 unison voices) to effects only. */
void base (IEngine& e)
{
    setByKey (e, "osc1.wavetable", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 7.0f);
    setByKey (e, "osc1.unison.detune", 30.0f);
    setByKey (e, "osc1.unison.spread", 100.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

sbtest::Rendered play (IEngine& e, double seconds = 0.8, int key = 57)
{
    return sbtest::render (e, { noteOn (0, key) }, static_cast<int> (seconds * kSr), 256);
}

double rms (const std::vector<float>& v, double t0 = 0.3, double t1 = 0.7)
{
    return sbtest::rmsOf (v, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0 = 0.3, double t1 = 0.7)
{
    double d = 0.0, n = 0.0;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < a.left.size() && i < b.left.size(); ++i)
    {
        const double x = static_cast<double> (a.left[i]) - b.left[i];
        d += x * x;
        n += static_cast<double> (a.left[i]) * a.left[i];
    }
    return n > 0.0 ? std::sqrt (d / n) : (d > 0.0 ? 1.0e9 : 0.0);
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

std::vector<uint8_t> wav16 (const std::vector<float>& samples, int sampleRate = 48000)
{
    std::vector<uint8_t> b;
    const auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (v >> (8 * i))); };
    const auto put16 = [&] (uint16_t v) { b.push_back (static_cast<uint8_t> (v)); b.push_back (static_cast<uint8_t> (v >> 8)); };
    const auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (t[i])); };
    tag ("RIFF"); put32 (static_cast<uint32_t> (36 + samples.size() * 2)); tag ("WAVE");
    tag ("fmt "); put32 (16); put16 (1); put16 (1); put32 (static_cast<uint32_t> (sampleRate)); put32 (static_cast<uint32_t> (sampleRate * 2)); put16 (2); put16 (16);
    tag ("data"); put32 (static_cast<uint32_t> (samples.size() * 2));
    for (float s : samples)
        put16 (static_cast<uint16_t> (static_cast<int16_t> (std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767.0f))));
    return b;
}

/** The fully rendered transparent signal level of an effect that does nothing: the splitter with no effect in its bands. */
double dbOf (double ratio)
{
    return 20.0 * std::log10 (ratio);
}
} // namespace

TEST (wp7_wp8_capabilities_are_reported)
{
    auto e = createEngine (EngineKind::vital);
    for (EngineCapsMask bit : { kCapFxUtility, kCapFxHyperDimension, kCapFxBode, kCapFxConvolve, kCapFxSplitters, kCapReverbForce })
        CHECK ((e->capabilities() & bit) != 0);
    auto own = createEngine (EngineKind::own);
    CHECK ((own->capabilities() & (kCapFxUtility | kCapFxHyperDimension | kCapFxBode | kCapFxConvolve | kCapFxSplitters | kCapReverbForce)) == 0);
}

TEST (utility_polarity_filters_width_pan_mix_and_level)
{
    auto e = makeVital();
    base (*e);
    const auto off = play (*e);
    e->reset();
    setByKey (*e, "utility.on", 1.0f);
    const auto neutral = play (*e);
    CHECK (relativeDifference (off, neutral) < 0.02); // everything at its neutral position: the signal passes
    e->reset();
    setByKey (*e, "utility.level", -12.0f);
    CHECK_NEAR (rms (play (*e).left) / rms (off.left), std::pow (10.0, -12.0 / 20.0), 0.03);
    e->reset();
    setByKey (*e, "utility.level", 0.0f);
    setByKey (*e, "utility.width", 0.0f); // mono
    const auto mono = play (*e);
    CHECK (sideRms (mono) < 0.01 * sideRms (off));
    e->reset();
    setByKey (*e, "utility.width", 200.0f);
    CHECK (sideRms (play (*e)) > 1.5 * sideRms (off));
    e->reset();
    setByKey (*e, "utility.width", 100.0f);
    setByKey (*e, "utility.pan", 100.0f);
    const auto panned = play (*e);
    CHECK (rms (panned.right) > 3.0 * rms (panned.left));
    e->reset();
    setByKey (*e, "utility.pan", 0.0f);
    setByKey (*e, "utility.polarity_l", 1.0f);
    const auto inverted = play (*e);
    CHECK_NEAR (rms (inverted.left), rms (off.left), 0.1 * rms (off.left));
    double sum = 0.0, diff = 0.0;
    for (size_t i = static_cast<size_t> (0.3 * kSr); i < static_cast<size_t> (0.7 * kSr); ++i)
    {
        sum += std::fabs (off.left[i] + inverted.left[i]);
        diff += std::fabs (off.left[i] - inverted.left[i]);
    }
    CHECK (sum < 0.05 * diff); // the left channel is the negative of the plain one
    e->reset();
    setByKey (*e, "utility.polarity_l", 0.0f);
    setByKey (*e, "utility.lpf", 300.0f);
    const auto lowPassed = play (*e);
    CHECK (relativeDifference (off, lowPassed) > 0.3);
    e->reset();
    setByKey (*e, "utility.lpf", 20000.0f);
    setByKey (*e, "utility.hpf", 3000.0f);
    CHECK (rms (play (*e).left) < 0.5 * rms (off.left));
    e->reset();
    setByKey (*e, "utility.hpf", 20.0f);
    setByKey (*e, "utility.width", 0.0f);
    setByKey (*e, "utility.mix", 0.0f); // dry
    CHECK (relativeDifference (off, play (*e)) < 0.02);
}

TEST (hyper_dimension_changes_the_sound_and_its_controls_work)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.unison.voices", 1.0f);
    const auto off = play (*e);
    e->reset();
    setByKey (*e, "hyperdim.on", 1.0f);
    setByKey (*e, "hyperdim.mix", 100.0f);
    setByKey (*e, "hyperdim.unison", 5.0f);
    setByKey (*e, "hyperdim.detune", 80.0f);
    const auto hyper = play (*e);
    CHECK (relativeDifference (off, hyper) > 0.3);
    CHECK (sideRms (hyper) > 5.0 * sideRms (off) + 1.0e-4); // the hyper voices are spread over the stereo field
    e->reset();
    setByKey (*e, "hyperdim.mix", 0.0f);
    setByKey (*e, "hyperdim.dim_mix", 100.0f);
    setByKey (*e, "hyperdim.dim_size", 100.0f);
    const auto dimension = play (*e);
    CHECK (relativeDifference (off, dimension) > 0.1);
    CHECK (sideRms (dimension) > sideRms (off) + 1.0e-4);
    e->reset();
    setByKey (*e, "hyperdim.dim_mix", 0.0f);
    setByKey (*e, "hyperdim.level", -12.0f);
    CHECK (rms (play (*e).left) < 0.5 * rms (off.left)); // level
}

TEST (bode_shifts_the_frequency_up_and_down)
{
    auto e = makeVital();
    setByKey (*e, "osc1.wavetable", 0.0f); // sine
    setByKey (*e, "osc1.level", 100.0f);
    setByKey (*e, "osc1.unison.voices", 1.0f);
    setByKey (*e, "osc1.random_phase", 0.0f);
    setByKey (*e, "osc1.destination", 3.0f);
    setByKey (*e, "filter1.on", 0.0f);
    setByKey (*e, "master.volume", -6.0f);
    setByKey (*e, "bode.on", 1.0f);
    setByKey (*e, "bode.mix", 100.0f);
    setByKey (*e, "bode.range", 100.0f);
    setByKey (*e, "bode.shift", 100.0f); // 100 % of the 100 Hz range
    setByKey (*e, "bode.dir", 100.0f);
    const auto spectrumPeak = [&] (const sbtest::Rendered& r, double guess)
    {
        const auto sp = sbtest::powerSpectrum (r.left, static_cast<size_t> (0.3 * kSr), 32768, 131072);
        return sbtest::peakFrequency (sp, 131072, kSr, guess);
    };
    const auto up = play (*e, 1.0, 69); // 440 Hz
    CHECK_NEAR (spectrumPeak (up, 540.0), 540.0, 5.0);
    e->reset();
    setByKey (*e, "bode.dir", -100.0f);
    CHECK_NEAR (spectrumPeak (play (*e, 1.0, 69), 340.0), 340.0, 5.0);
    e->reset();
    setByKey (*e, "bode.shift", 0.0f);
    CHECK_NEAR (spectrumPeak (play (*e, 1.0, 69), 440.0), 440.0, 3.0); // no shift: the sine stays where it is
}

TEST (convolve_uses_the_impulse_of_the_content_and_rebuilds_when_the_size_changes)
{
    // the impulse: a single click 150 ms after the start
    std::vector<float> ir (static_cast<size_t> (0.151 * kSr), 0.0f);
    ir[static_cast<size_t> (0.15 * kSr)] = 0.9f;
    MemoryContentStore store;
    store.add ("test", "ir.wav", wav16 (ir));
    StateContent content;
    ContentFileRef ref;
    ref.pack = "test";
    ref.file = "ir.wav";
    ref.name = "click";
    content.impulses.push_back (ref);

    auto e = makeVital();
    e->setContentStore (&store);
    e->setContent (content);
    base (*e);
    setByKey (*e, "osc1.unison.voices", 1.0f);
    setByKey (*e, "convolve.on", 1.0f);
    setByKey (*e, "convolve.ir", 1.0f);
    setByKey (*e, "convolve.mix", 100.0f);
    setByKey (*e, "convolve.size", 50.0f); // the original length
    // let the builder thread partition the impulse (it runs in real time next to the rendering)
    sbtest::render (*e, {}, 4096, 256);
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    sbtest::render (*e, {}, 4096, 256);
    std::vector<Event> events { noteOn (0, 69), noteOff (static_cast<int> (0.05 * kSr), 69) };
    setByKey (*e, "env1.release", 5.0f);
    const auto wet = sbtest::render (*e, events, static_cast<int> (0.6 * kSr), 256);
    // the click of the impulse delays the 50 ms note by 150 ms: sound between 0.15 and 0.22 s, little before 0.12 s
    CHECK (rms (wet.left, 0.16, 0.2) > 0.01);
    CHECK (rms (wet.left, 0.3, 0.5) < 0.1 * rms (wet.left, 0.16, 0.2));
    // without the impulse (None) the module passes the dry signal
    e->reset();
    setByKey (*e, "convolve.ir", 0.0f);
    sbtest::render (*e, {}, 4096, 256); // posts the new setting to the builder thread
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    sbtest::render (*e, {}, 4096, 256);
    const auto dry = sbtest::render (*e, events, static_cast<int> (0.6 * kSr), 256);
    CHECK (rms (dry.left, 0.0, 0.04) > 0.01);
    CHECK (rms (dry.left, 0.16, 0.2) < 0.1 * rms (dry.left, 0.0, 0.04));
    // size 100 % = four times longer: the click moves from 150 ms to 600 ms
    e->reset();
    setByKey (*e, "convolve.ir", 1.0f);
    setByKey (*e, "convolve.size", 100.0f);
    sbtest::render (*e, {}, 4096, 256);
    std::this_thread::sleep_for (std::chrono::milliseconds (600));
    sbtest::render (*e, {}, 4096, 256);
    const auto stretched = sbtest::render (*e, events, static_cast<int> (1.0 * kSr), 256);
    CHECK (rms (stretched.left, 0.16, 0.2) < 0.2 * rms (wet.left, 0.16, 0.2));
    CHECK (rms (stretched.left, 0.6, 0.7) > 0.005);
}

TEST (convolve_rebuild_is_deterministic_offline_and_does_not_wait_in_realtime_mode)
{
    std::vector<float> ir (static_cast<size_t> (0.151 * kSr), 0.0f);
    ir[static_cast<size_t> (0.05 * kSr)] = 0.9f;
    ir[static_cast<size_t> (0.15 * kSr)] = 0.5f;
    MemoryContentStore store;
    store.add ("test", "ir.wav", wav16 (ir));
    StateContent content;
    ContentFileRef ref;
    ref.pack = "test";
    ref.file = "ir.wav";
    ref.name = "click";
    content.impulses.push_back (ref);

    // the size changes between blocks (a rebuild each time) with no pause for the builder thread: two runs must be bit identical
    const auto run = [&]
    {
        auto e = makeVital();
        e->setContentStore (&store);
        e->setContent (content);
        base (*e);
        setByKey (*e, "convolve.on", 1.0f);
        setByKey (*e, "convolve.ir", 1.0f);
        setByKey (*e, "convolve.mix", 100.0f);
        sbtest::Rendered all;
        for (int part = 0; part < 6; ++part)
        {
            setByKey (*e, "convolve.size", 10.0f + 15.0f * static_cast<float> (part));
            const auto r = sbtest::render (*e, part == 0 ? std::vector<Event> { noteOn (0, 57) } : std::vector<Event> {}, 4800, 256);
            all.left.insert (all.left.end(), r.left.begin(), r.left.end());
            all.right.insert (all.right.end(), r.right.begin(), r.right.end());
        }
        return all;
    };
    const auto a = run(), b = run();
    CHECK (a.left == b.left && a.right == b.right);
    CHECK (rms (a.left, 0.0, 0.1) > 0.001);

    // realtime mode: the audio thread must not wait for the builder (a block with a pending rebuild returns at once)
    Quality live;
    live.realtime = true;
    auto e = makeVital (live);
    e->setContentStore (&store);
    e->setContent (content);
    base (*e);
    setByKey (*e, "convolve.on", 1.0f);
    setByKey (*e, "convolve.ir", 1.0f);
    sbtest::render (*e, {}, 256, 256);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i)
    {
        setByKey (*e, "convolve.size", 10.0f + 4.0f * static_cast<float> (i));
        sbtest::render (*e, { noteOn (0, 57) }, 256, 256);
    }
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("    realtime mode: 20 blocks with a rebuild each took %.1f ms\n", ms);
    CHECK (ms < 1000.0);
}

TEST (bode_reports_its_latency_and_every_other_path_is_delayed_to_match)
{
    // Bode's Hilbert filter delays the path it sits in by 64 samples of the chain's rate. The engine reports it (latencySamples) and delays
    // the paths that do not pass the effect (the other bus, the voices that bypass the rack, the Direct output), so with mix 0 the whole
    // output is the output without Bode, late by exactly the reported latency.
    for (const int oversampling : { 1, 2 })
    {
        for (const char* where : { "main", "bus1", "band" })
        {
            const auto run = [&] (bool bodeOn, int& latency)
            {
                Quality q;
                q.oversampling = oversampling;
                auto e = makeVital (q);
                setByKey (*e, "osc1.wavetable", 1.0f);
                setByKey (*e, "osc1.unison.voices", 1.0f);
                setByKey (*e, "osc1.random_phase", 0.0f);
                setByKey (*e, "osc1.level", 70.0f);
                setByKey (*e, "osc2.on", 1.0f);
                setByKey (*e, "osc2.wavetable", 2.0f);
                setByKey (*e, "osc2.unison.voices", 1.0f);
                setByKey (*e, "osc2.random_phase", 0.0f);
                setByKey (*e, "osc2.transpose", 7.0f);
                setByKey (*e, "osc2.level", 50.0f);
                setByKey (*e, "filter1.on", 0.0f);
                setByKey (*e, "filter2.on", 0.0f);
                setByKey (*e, "master.volume", -6.0f);
                setByKey (*e, "osc1.destination", 6.0f);   // no direct audio: osc 1 reaches the effects through the bus 1 send
                setByKey (*e, "osc1.bus1_send", 100.0f);
                setByKey (*e, "osc2.destination", 3.0f);   // osc 2: the main bus (a path that does not pass the bus rack)
                setByKey (*e, "bode.mix", 0.0f);
                setByKey (*e, "bode.on", bodeOn ? 1.0f : 0.0f);
                const std::string w = where;
                if (w == "bus1")
                    setByKey (*e, "bode.rack", 1.0f);
                else if (w == "band")
                {
                    setByKey (*e, "splitter_lh.on", 1.0f);
                    setByKey (*e, "bode.rack", 3.0f);   // the low band of the L/H splitter in the Main rack
                }
                const auto r = sbtest::render (*e, { noteOn (0, 57) }, 20000, 256);
                latency = e->latencySamples();
                return r;
            };
            int off = -1, on = -1;
            const auto ref = run (false, off);
            const auto test = run (true, on);
            const int expected = 64 / oversampling;
            std::printf ("    oversampling %d, Bode in %s: latency %d (expected %d)\n", oversampling, where, on, expected);
            CHECK_EQ (off, 0);
            CHECK_EQ (on, expected);
            double worst = 0.0, peak = 0.0;
            for (size_t i = 4000; i + static_cast<size_t> (on) < ref.left.size() && i < 16000; ++i)
            {
                worst = std::max (worst, static_cast<double> (std::fabs (test.left[i + static_cast<size_t> (on)] - ref.left[i])));
                peak = std::max (peak, static_cast<double> (std::fabs (ref.left[i])));
            }
            std::printf ("      shifted output differs by %.2e (peak %.3f)\n", worst, peak);
            CHECK (peak > 0.05);
            CHECK (worst < 0.01 * peak);
        }
    }
}

TEST (splitters_are_transparent_and_host_the_effects_of_their_bands)
{
    auto e = makeVital();
    base (*e);
    const auto off = play (*e);
    for (const char* name : { "splitter_lh", "splitter_lmh", "splitter_ms" })
    {
        e->reset();
        setByKey (*e, (std::string (name) + ".on").c_str(), 1.0f);
        const auto on = play (*e);
        const double db = dbOf (rms (on.left) / rms (off.left));
        std::printf ("    %s: %.3f dB\n", name, db);
        CHECK (std::fabs (db) < 0.1);   // A24: summing all bands with no effects is transparent within 0.1 dB
        setByKey (*e, (std::string (name) + ".on").c_str(), 0.0f);
    }

    // an effect in a band rack only works while its splitter is on
    e->reset();
    setByKey (*e, "distortion.on", 1.0f);
    setByKey (*e, "distortion.drive", 24.0f);
    setByKey (*e, "distortion.mix", 100.0f);
    setByKey (*e, "distortion.rack", 4.0f); // Splitter L/H high
    const auto inertWithoutSplitter = play (*e);
    CHECK (relativeDifference (off, inertWithoutSplitter) < 0.01);
    e->reset();
    setByKey (*e, "splitter_lh.on", 1.0f);
    setByKey (*e, "splitter_lh.freq", 800.0f);
    const auto highDistorted = play (*e);
    CHECK (relativeDifference (off, highDistorted) > 0.15);
    e->reset();
    setByKey (*e, "distortion.rack", 3.0f); // the low band
    const auto lowDistorted = play (*e);
    CHECK (relativeDifference (highDistorted, lowDistorted) > 0.1); // the other band
    // a bypassed band is removed from the sum
    e->reset();
    setByKey (*e, "distortion.on", 0.0f);
    setByKey (*e, "splitter_lh.low_bypass", 1.0f);
    CHECK (rms (play (*e).left) < 0.8 * rms (off.left));
    e->reset();
    setByKey (*e, "splitter_lh.low_bypass", 0.0f);
    setByKey (*e, "splitter_lh.on", 0.0f);
    // M/S: a centre signal is all mid
    setByKey (*e, "osc1.unison.voices", 1.0f);
    const auto centre = play (*e);
    e->reset();
    setByKey (*e, "splitter_ms.on", 1.0f);
    setByKey (*e, "splitter_ms.mid_bypass", 1.0f);
    CHECK (rms (play (*e).left) < 0.05 * rms (centre.left));
    e->reset();
    setByKey (*e, "splitter_ms.mid_bypass", 0.0f);
    setByKey (*e, "splitter_ms.side_bypass", 1.0f);
    CHECK (rms (play (*e).left) > 0.8 * rms (centre.left));
    // L/M/H: the mid band only
    e->reset();
    setByKey (*e, "splitter_ms.on", 0.0f);
    setByKey (*e, "splitter_lmh.on", 1.0f);
    setByKey (*e, "splitter_lmh.freq_low", 300.0f);
    setByKey (*e, "splitter_lmh.freq_high", 1500.0f);
    setByKey (*e, "splitter_lmh.mid_bypass", 1.0f);
    const auto noMid = play (*e);
    CHECK (rms (noMid.left) < 0.95 * rms (centre.left));
    CHECK (relativeDifference (centre, noMid) > 0.1);
}

TEST (heavy_serum_modules_are_bypassed_on_the_low_tier_and_run_on_the_others)
{
    auto high = makeVital (qualityForTier (Tier::high));
    auto low = makeVital (qualityForTier (Tier::low));
    for (IEngine* e : { high.get(), low.get() })
    {
        base (*e);
        setByKey (*e, "osc1.unison.voices", 1.0f);
    }
    const auto highOff = play (*high);
    const auto lowOff = play (*low);
    setByKey (*high, "hyperdim.on", 1.0f);
    setByKey (*high, "hyperdim.mix", 100.0f);
    setByKey (*low, "hyperdim.on", 1.0f);
    setByKey (*low, "hyperdim.mix", 100.0f);
    high->reset();
    low->reset();
    CHECK (relativeDifference (highOff, play (*high)) > 0.3);
    CHECK (relativeDifference (lowOff, play (*low)) < 0.02); // masked by the tier
    setByKey (*high, "hyperdim.on", 0.0f);
    setByKey (*low, "hyperdim.on", 0.0f);
    setByKey (*high, "utility.on", 1.0f);
    setByKey (*high, "utility.width", 0.0f);
    setByKey (*low, "utility.on", 1.0f);
    setByKey (*low, "utility.level", -12.0f);
    high->reset();
    low->reset();
    CHECK (relativeDifference (highOff, play (*high)) < 0.5);                           // utility runs on both tiers
    CHECK (rms (play (*low).left) < 0.5 * rms (lowOff.left));
}

TEST (reverb_force_enable_overrides_the_tier_mask)
{
    // the tier "low" masks the reverb out; force enable keeps it
    auto low = makeVital (qualityForTier (Tier::low));
    base (*low);
    setByKey (*low, "osc1.unison.voices", 1.0f);
    setByKey (*low, "reverb.mix", 100.0f);
    setByKey (*low, "reverb.decay", 3.0f);
    const auto clean = sbtest::render (*low, { noteOn (0, 57), noteOff (static_cast<int> (0.2 * kSr), 57) }, static_cast<int> (1.5 * kSr), 256);
    low->reset();
    // (the first render after prepare () differs from the later ones by the warm-up of the engine: the reference is the second)
    const auto clean2 = sbtest::render (*low, { noteOn (0, 57), noteOff (static_cast<int> (0.2 * kSr), 57) }, static_cast<int> (1.5 * kSr), 256);
    low->reset();
    setByKey (*low, "reverb.on", 1.0f);
    const auto masked = sbtest::render (*low, { noteOn (0, 57), noteOff (static_cast<int> (0.2 * kSr), 57) }, static_cast<int> (1.5 * kSr), 256);
    CHECK (relativeDifference (clean2, masked, 0.0, 1.4) < 0.01); // masked: no reverb on the low tier
    low->reset();
    setByKey (*low, "reverb.force_enable", 1.0f);
    const auto forced = sbtest::render (*low, { noteOn (0, 57), noteOff (static_cast<int> (0.2 * kSr), 57) }, static_cast<int> (1.5 * kSr), 256);
    CHECK (rms (forced.left, 0.6, 1.2) > 20.0 * rms (clean2.left, 0.6, 1.2) + 1.0e-5); // the reverb tail is there
    // the mask helper of quality.h
    CHECK ((effectiveFxMask (qualityForTier (Tier::low).fxMask, false) & kFxReverb) == 0);
    CHECK ((effectiveFxMask (qualityForTier (Tier::low).fxMask, true) & kFxReverb) != 0);
    CHECK ((effectiveFxMask (qualityForTier (Tier::low).fxMask, true) & kFxFlanger) == 0); // only the reverb
    // switching it off again takes the reverb away
    setByKey (*low, "reverb.force_enable", 0.0f);
    low->reset();
    const auto offAgain = sbtest::render (*low, { noteOn (0, 57), noteOff (static_cast<int> (0.2 * kSr), 57) }, static_cast<int> (1.5 * kSr), 256);
    CHECK (relativeDifference (clean2, offAgain, 0.0, 1.4) < 0.01);
}

TEST (serum_modules_in_all_racks_survive_random_settings)
{
    auto e = makeVital();
    sbtest::Rng rng (17);
    base (*e);
    for (int round = 0; round < 16; ++round)
    {
        for (const char* module : { "hyperdim", "utility", "bode", "convolve", "splitter_lh", "splitter_lmh", "splitter_ms", "chorus", "delay", "reverb" })
        {
            setByKey (*e, (std::string (module) + ".on").c_str(), static_cast<float> (rng.u32() % 2));
            setByKey (*e, (std::string (module) + ".rack").c_str(), static_cast<float> (rng.u32() % 10));
        }
        for (const char* key : { "hyperdim.rate", "hyperdim.detune", "utility.width", "utility.pan", "utility.lpf", "utility.hpf", "bode.shift", "bode.range",
                                 "bode.dir", "bode.feed", "bode.mix", "convolve.size", "convolve.tone", "convolve.mix", "splitter_lh.freq",
                                 "splitter_lmh.freq_low", "splitter_lmh.freq_high" })
        {
            const ParamInfo* info = findParamByKey (key);
            CHECK (info != nullptr);
            if (info != nullptr)
                setByKey (*e, key, info->min + static_cast<float> (rng.uniform()) * (info->max - info->min));
        }
        for (int position = 1; position <= 16; ++position)
        {
            char key[16];
            std::snprintf (key, sizeof key, "fx.order.%d", position);
            setByKey (*e, key, static_cast<float> (rng.u32() % 16));
        }
        setByKey (*e, "osc1.destination", static_cast<float> (rng.u32() % 4));
        setByKey (*e, "osc1.bus1_send", static_cast<float> (rng.u32() % 101));
        auto out = sbtest::render (*e, { noteOn (0, 40 + static_cast<int> (rng.u32() % 30)), noteOn (0, 60) }, 20000, 256);
        CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (out.right));
        CHECK (sbtest::peak (out.left) < 8.0f);
    }
}
