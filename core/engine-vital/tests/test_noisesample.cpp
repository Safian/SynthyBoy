// SynthyBoy - engine-vital tests: B23 (GROUPA1, WP-04): sub.phase / sub.phase_contiguous, noise.stereo / noise.filter (the colour samples),
// noise.type = Sample (content.samples[osc 4]) and noise.bounce (13-new-keys.md 4.27).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Proves kCapSubPhase, kCapNoiseColour and kCapNoiseSample: every key is rendered off and on and must make a measured difference, and the defaults
// leave the stock noise (left == right, no filter) exactly as it was.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
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
constexpr double kPi = 3.14159265358979323846;
constexpr size_t kFft = 16384;

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

std::vector<float> sine (double freq, double seconds, float amp = 0.8f)
{
    std::vector<float> v (static_cast<size_t> (seconds * kSr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = amp * static_cast<float> (std::sin (2.0 * kPi * freq * static_cast<double> (i) / kSr));
    return v;
}

/** A settings list that renders on a fresh engine every time (two fresh engines with the same settings are sample identical). */
class Synth
{
public:
    void set (const char* key, float value) { settings.emplace_back (key, value); }

    /** The sample of the noise source (content.samples[osc 4]). */
    void noiseSample (const std::vector<float>& audio)
    {
        store.add ("test", "noise.wav", wav16 (audio));
        ContentSample c;
        c.osc = kNoiseSampleOsc;
        c.ref.pack = "test";
        c.ref.file = "noise.wav";
        c.ref.name = "noise";
        c.sampleRate = 48000;
        c.frames = static_cast<int> (audio.size());
        c.channels = 1;
        content.samples.push_back (c);
    }

    std::unique_ptr<IEngine> build() const
    {
        auto e = createEngine (EngineKind::vital);
        e->setParam (kParamMasterPolyphony, 32.0f);
        e->prepare (kSr, 256, Quality {});
        if (! content.empty())
        {
            e->setContentStore (const_cast<MemoryContentStore*> (&store));
            e->setContent (content);
        }
        for (const auto& s : settings)
            setByKey (*e, s.first.c_str(), s.second);
        return e;
    }

    sbtest::Rendered play (double seconds = 0.6, int key = 48) const { return playEvents ({ noteOn (0, key) }, seconds); }

    sbtest::Rendered playEvents (const std::vector<Event>& events, double seconds) const
    {
        auto e = build();
        return sbtest::render (*e, events, static_cast<int> (seconds * kSr), 256);
    }

private:
    std::vector<std::pair<std::string, float>> settings;
    MemoryContentStore store;
    StateContent content;
};

void silenceMain (Synth& e)
{
    for (const char* key : { "osc1.on", "osc2.on", "osc3.on", "noise.on", "sub.on", "filter1.on", "filter2.on" })
        e.set (key, 0.0f);
    e.set ("master.volume", 0.0f);
}

void noiseOnly (Synth& e)
{
    silenceMain (e);
    e.set ("noise.on", 1.0f);
    e.set ("noise.level", 50.0f);
    e.set ("noise.destination", 3.0f);
    e.set ("noise.sample", 0.0f);
}

void subOnly (Synth& e)
{
    silenceMain (e);
    e.set ("sub.on", 1.0f);
    e.set ("sub.level", 100.0f);
    e.set ("sub.destination", 3.0f);
    e.set ("sub.octave", 0.0f);
    e.set ("sub.waveform", 0.0f); // sine
    e.set ("env1.attack", 0.0f);
}

bool identical (const sbtest::Rendered& a, const sbtest::Rendered& b) { return a.left == b.left && a.right == b.right; }

double rms (const std::vector<float>& v, double t0 = 0.15, double t1 = 0.55)
{
    return sbtest::rmsOf (v, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

double centroid (const sbtest::Rendered& r)
{
    return sbtest::centroidOf (r.left, static_cast<size_t> (0.15 * kSr), kFft, kSr, kFft);
}

/** Normalised correlation of two signals over a window (1 = the same, 0 = no relation, -1 = inverted). */
double correlation (const std::vector<float>& a, const std::vector<float>& b, double t0 = 0.15, double t1 = 0.55)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < a.size() && i < b.size(); ++i)
    {
        ab += static_cast<double> (a[i]) * b[i];
        aa += static_cast<double> (a[i]) * a[i];
        bb += static_cast<double> (b[i]) * b[i];
    }
    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
}

/** Zero crossings per second of the mean-free signal. */
double crossingsPerSecond (const std::vector<float>& v, double t0, double t1)
{
    double mean = 0.0;
    size_t n = 0;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < v.size(); ++i, ++n)
        mean += v[i];
    mean = n > 0 ? mean / static_cast<double> (n) : 0.0;
    int crossings = 0;
    bool previous = false;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < v.size(); ++i)
    {
        const bool positive = v[i] - mean > 0.0;
        if (i > static_cast<size_t> (t0 * kSr) && positive != previous)
            ++crossings;
        previous = positive;
    }
    return static_cast<double> (crossings) / (t1 - t0);
}
} // namespace

TEST (b23_capabilities_and_keys_exist)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities() & (kCapSubPhase | kCapNoiseColour | kCapNoiseSample)) == (kCapSubPhase | kCapNoiseColour | kCapNoiseSample));
    auto own = createEngine (EngineKind::own);
    for (size_t i = 0; i < paramCount(); ++i) // the own engine never claims more than its capability words say
        if (own->supportsParam (paramTable()[i].id))
            CHECK (capsCover (own->capabilities(), requiredCaps (i)) && capsCover2 (own->capabilities2(), requiredCaps2 (i)));
    for (const char* key : { "sub.phase", "sub.phase_contiguous", "noise.stereo", "noise.filter", "noise.type", "noise.bounce" })
        CHECK (findParamByKey (key) != nullptr);
}

TEST (noise_colour_defaults_are_the_stock_mono_noise)
{
    Synth e;
    noiseOnly (e);
    const auto reference = e.play();
    CHECK (rms (reference.left) > 0.01);
    CHECK (reference.left == reference.right); // STEREO 0: left == right, bit for bit, as before the colours had two channels
    e.set ("noise.stereo", 0.0f);
    e.set ("noise.filter", 0.0f);
    e.set ("noise.type", 0.0f);
    e.set ("noise.bounce", 0.0f);
    CHECK (identical (reference, e.play()));
    e.set ("sub.phase", 50.0f); // the sub is off
    CHECK (identical (reference, e.play()));
}

TEST (noise_stereo_decorrelates_the_colour_samples)
{
    for (const int colour : { 0, 1, 2, 3 }) // White, Pink, Brown, Geiger
    {
        Synth e;
        noiseOnly (e);
        e.set ("noise.sample", static_cast<float> (colour));
        const auto mono = e.play();
        CHECK (mono.left == mono.right);
        e.set ("noise.stereo", 100.0f);
        const auto wide = e.play();
        CHECK (rms (wide.left) > 0.0);
        const double c = correlation (wide.left, wide.right);
        if (! CHECK (std::fabs (c) < 0.2))
            std::printf ("    colour %d correlation %.3f\n", colour, c);
        // the left channel and the level are those of the mono noise
        CHECK_NEAR (rms (wide.left), rms (mono.left), 0.02 * rms (mono.left));
        CHECK_NEAR (rms (wide.right), rms (mono.left), 0.1 * rms (mono.left));
    }
    // half way: the correlation falls with the knob
    Synth e;
    noiseOnly (e);
    double previous = 1.1;
    for (const float stereo : { 0.0f, 30.0f, 60.0f, 100.0f })
    {
        e.set ("noise.stereo", stereo);
        const auto r = e.play();
        const double c = correlation (r.left, r.right);
        CHECK (c < previous + 1.0e-9);
        previous = c;
    }
    // the other generators and a content sample have no second channel: STEREO does nothing
    Synth t;
    noiseOnly (t);
    t.set ("noise.sample", 4.0f); // Tape Hiss
    t.set ("noise.stereo", 100.0f);
    const auto hiss = t.play();
    CHECK (hiss.left == hiss.right);
}

TEST (noise_filter_low_and_high_passes_the_colour_samples)
{
    Synth e;
    noiseOnly (e);
    const auto flat = e.play();
    e.set ("noise.filter", -60.0f);
    const auto lowPassed = e.play();
    e.set ("noise.filter", 60.0f);
    const auto highPassed = e.play();
    CHECK (centroid (lowPassed) < 0.5 * centroid (flat));
    CHECK (rms (lowPassed.left) < 0.7 * rms (flat.left));
    CHECK (centroid (highPassed) > 0.99 * centroid (flat));
    // white is bright already: the high pass shows on a dark colour (Brown) and on the level
    {
        Synth b;
        noiseOnly (b);
        b.set ("noise.sample", 2.0f);
        const auto brown = b.play();
        b.set ("noise.filter", 60.0f);
        const auto brownHigh = b.play();
        CHECK (centroid (brownHigh) > 1.2 * centroid (brown));
        CHECK (rms (brownHigh.left) < 0.6 * rms (brown.left));
    }
    // the harder the knob, the darker (brighter)
    e.set ("noise.filter", -100.0f);
    const auto darkest = e.play();
    CHECK (centroid (darkest) < centroid (lowPassed));
    e.set ("noise.filter", 100.0f);
    const auto brightest = e.play();
    CHECK (rms (brightest.left) < rms (highPassed.left));
    CHECK (std::isfinite (rms (brightest.left)) && std::isfinite (rms (darkest.left)));
    // 0 is off: the stock noise
    e.set ("noise.filter", 0.0f);
    CHECK (identical (flat, e.play()));
    // a generator that is not a colour is not filtered
    Synth t;
    noiseOnly (t);
    t.set ("noise.sample", 4.0f);
    const auto hiss = t.play();
    t.set ("noise.filter", -100.0f);
    CHECK (identical (hiss, t.play()));
}

TEST (noise_type_sample_plays_the_content_sample)
{
    const std::vector<float> tone = sine (440.0, 1.0);
    Synth e;
    noiseOnly (e);
    e.noiseSample (tone);
    e.set ("noise.keytrack", 0.0f);
    e.set ("noise.loop", 1.0f);

    const auto generated = e.play(); // noise.type is Noise: the generator plays
    e.set ("noise.type", 1.0f);
    const auto sampled = e.play();
    CHECK (rms (sampled.left) > 0.05);
    CHECK (centroid (sampled) < 0.3 * centroid (generated));
    // the sample sounds at its recorded pitch: 440 Hz, 880 Hz one octave up (noise.pitch)
    const auto freq = [] (const sbtest::Rendered& r) { return crossingsPerSecond (r.left, 0.2, 0.55) * 0.5; };
    CHECK_NEAR (freq (sampled), 440.0, 8.0);
    e.set ("noise.pitch", 12.0f);
    CHECK_NEAR (freq (e.play()), 880.0, 16.0);
    e.set ("noise.pitch", 0.0f);
    // keytrack: a key an octave above C3 doubles the pitch
    e.set ("noise.keytrack", 1.0f);
    CHECK_NEAR (freq (e.playEvents ({ noteOn (0, 72) }, 0.6)), 880.0, 16.0);
    e.set ("noise.keytrack", 0.0f);

    // STEREO and FILTER belong to the colour samples: a content sample is not touched by them
    e.set ("noise.stereo", 100.0f);
    e.set ("noise.filter", -100.0f);
    CHECK (identical (sampled, e.play()));
    e.set ("noise.stereo", 0.0f);
    e.set ("noise.filter", 0.0f);

    // without a content sample the generator plays (documented fallback)
    Synth none;
    noiseOnly (none);
    const auto reference = none.play();
    none.set ("noise.type", 1.0f);
    CHECK (identical (reference, none.play()));

    // switching back to the generator restores it exactly
    e.set ("noise.type", 0.0f);
    CHECK (identical (generated, e.play()));
}

TEST (noise_sample_loop_one_shot_and_bounce)
{
    // a short rising ramp: a looped ramp is a saw (one period per pass), a bounced one a triangle (two passes per period)
    std::vector<float> ramp (static_cast<size_t> (0.01 * kSr));
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = -0.8f + 1.6f * static_cast<float> (i) / static_cast<float> (ramp.size());
    Synth e;
    noiseOnly (e);
    e.noiseSample (ramp);
    e.set ("noise.type", 1.0f);
    e.set ("noise.keytrack", 0.0f);
    e.set ("noise.level", 80.0f);

    e.set ("noise.loop", 0.0f);
    const auto once = e.play (0.5);
    CHECK (rms (once.left, 0.2, 0.45) < 1.0e-4); // one shot: silent after 10 ms

    e.set ("noise.loop", 1.0f);
    const auto looped = e.play (0.5);
    CHECK (rms (looped.left, 0.2, 0.45) > 0.05);

    e.set ("noise.bounce", 1.0f);
    const auto bounced = e.play (0.5);
    CHECK (rms (bounced.left, 0.2, 0.45) > 0.05);
    const double sawRate = crossingsPerSecond (looped.left, 0.2, 0.45);
    const double triangleRate = crossingsPerSecond (bounced.left, 0.2, 0.45);
    if (! CHECK (triangleRate < 0.65 * sawRate))
        std::printf("    saw %.0f triangle %.0f crossings per second\n", sawRate, triangleRate);
    CHECK (sawRate > 150.0);
}

TEST (sub_phase_sets_the_start_phase)
{
    Synth e;
    subOnly (e);
    const auto zero = e.play();
    CHECK (rms (zero.left) > 0.05);
    e.set ("sub.phase", 50.0f); // half a cycle: the inverted sine
    const auto half = e.play();
    CHECK (correlation (zero.left, half.left) < -0.95);
    e.set ("sub.phase", 25.0f); // a quarter: in quadrature
    const auto quarter = e.play();
    CHECK (std::fabs (correlation (zero.left, quarter.left)) < 0.3);
    CHECK_NEAR (rms (quarter.left), rms (zero.left), 0.05 * rms (zero.left));
    // the default is the old sound
    e.set ("sub.phase", 0.0f);
    CHECK (identical (zero, e.play()));
}

TEST (sub_phase_contiguous_continues_with_the_phase_of_the_previous_note)
{
    // two notes of the same pitch sounding together (same note: Stack): the second starts in phase with the first when it is contiguous
    Synth e;
    subOnly (e);
    e.set ("master.voice.same_note", 1.0f);
    e.set ("env1.sustain", 100.0f);
    const std::vector<Event> events { noteOn (0, 48), noteOn (static_cast<int> (0.2013 * kSr), 48) };
    const auto separate = e.playEvents (events, 0.7);
    e.set ("sub.phase_contiguous", 1.0f);
    const auto contiguous = e.playEvents (events, 0.7);
    const auto peakAfter = [] (const sbtest::Rendered& r)
    {
        float p = 0.0f;
        for (size_t i = static_cast<size_t> (0.35 * kSr); i < static_cast<size_t> (0.65 * kSr); ++i)
            p = std::max (p, std::fabs (r.left[i]));
        return static_cast<double> (p);
    };
    CHECK (peakAfter (contiguous) > 1.08 * peakAfter (separate)); // in phase: twice the amplitude; at phase 0 the two are shifted by the 0.2013 s offset
    // before the second note nothing differs
    CHECK (std::equal (separate.left.begin(), separate.left.begin() + static_cast<long> (0.2 * kSr), contiguous.left.begin()));
}
