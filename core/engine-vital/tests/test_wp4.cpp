// SynthyBoy - engine-vital tests: WP4 (content store, sample oscillator, user wavetable, warp slot 2) of docs/ui-design/13-new-keys.md.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every capability bit WP4 sets (kCapSampleOsc, kCapUserWavetable, kCapWarp2) is proven here by rendering the key off and on and asserting a
// measurable difference of the audio. The thresholds are ratios between two renderings, not absolute levels.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
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
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::unique_ptr<IEngine> makeVital (const Quality& q = Quality {})
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, q);
    return e;
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

std::vector<float> sine (double freq, double seconds, float amp = 0.8f, int sr = 48000)
{
    std::vector<float> v (static_cast<size_t> (seconds * sr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = amp * static_cast<float> (std::sin (2.0 * kPi * freq * static_cast<double> (i) / sr));
    return v;
}

/** A store with one sample and/or one table for oscillator 1. */
struct Content
{
    MemoryContentStore store;
    StateContent state;

    void sample (const std::vector<float>& audio, const std::vector<float>& slices = {})
    {
        store.add ("test", "sample.wav", wav16 (audio));
        ContentSample c;
        c.osc = 1;
        c.ref.pack = "test";
        c.ref.file = "sample.wav";
        c.ref.name = "sample";
        c.sampleRate = 48000;
        c.frames = static_cast<int> (audio.size());
        c.channels = 1;
        c.slices = slices;
        state.samples.push_back (c);
    }

    void table (const std::vector<float>& frames)
    {
        store.add ("test", "table.wav", wav16 (frames));
        ContentWavetable w;
        w.osc = 1;
        w.ref.pack = "test";
        w.ref.file = "table.wav";
        w.ref.name = "table";
        w.frames = static_cast<int> (frames.size() / 2048);
        state.wavetables.push_back (w);
    }

    void apply (IEngine& e)
    {
        e.setContentStore (&store);
        e.setContent (state);
    }
};

void sampleOsc (IEngine& e, float root = 69.0f)
{
    setByKey (e, "osc1.type", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.phase", 0.0f);
    setByKey (e, "osc1.sample.root", root);
    setByKey (e, "osc1.sample.loop_mode", 1.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

sbtest::Rendered play (IEngine& e, int key, double seconds, std::vector<Event> extra = {})
{
    std::vector<Event> events { noteOn (0, key) };
    events.insert (events.end(), extra.begin(), extra.end());
    return sbtest::render (e, events, static_cast<int> (seconds * kSr), 256);
}

double pitchOf (const sbtest::Rendered& r, double guess, double from = 0.3)
{
    const auto spectrum = sbtest::powerSpectrum (r.left, static_cast<size_t> (from * kSr), 32768, 131072);
    return sbtest::peakFrequency (spectrum, 131072, kSr, guess);
}

double bandPower (const std::vector<double>& power, size_t fftSize, double sampleRate, double lo, double hi)
{
    double sum = 0.0;
    for (size_t k = static_cast<size_t> (lo / sampleRate * static_cast<double> (fftSize)); k <= static_cast<size_t> (hi / sampleRate * static_cast<double> (fftSize)) && k < power.size(); ++k)
        sum += power[k];
    return sum;
}

/** The frequency of a clean tone by its positive-going zero crossings between t0 and t1. */
double crossingFrequency (const sbtest::Rendered& r, double t0, double t1)
{
    int crossings = 0;
    for (size_t i = static_cast<size_t> (t0 * kSr) + 1; i < static_cast<size_t> (t1 * kSr) && i < r.left.size(); ++i)
        if (r.left[i - 1] < 0.0f && r.left[i] >= 0.0f)
            ++crossings;
    return crossings / (t1 - t0);
}

double rms (const sbtest::Rendered& r, double t0, double t1)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

/** RMS of the difference of two renderings relative to the RMS of the first. */
double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0, double t1)
{
    double d = 0.0, n = 0.0;
    const size_t from = static_cast<size_t> (t0 * kSr), to = std::min (std::min (a.left.size(), b.left.size()), static_cast<size_t> (t1 * kSr));
    for (size_t i = from; i < to; ++i)
    {
        const double x = static_cast<double> (a.left[i]) - b.left[i];
        d += x * x;
        n += static_cast<double> (a.left[i]) * a.left[i];
    }
    return n > 0.0 ? std::sqrt (d / n) : (d > 0.0 ? 1.0e9 : 0.0);
}
} // namespace

TEST (wp4_capabilities_are_reported)
{
    auto e = createEngine (EngineKind::vital);
    const auto caps = e->capabilities();
    CHECK ((caps & kCapSampleOsc) != 0);
    CHECK ((caps & kCapUserWavetable) != 0);
    CHECK ((caps & kCapWarp2) != 0);
    auto own = createEngine (EngineKind::own);
    CHECK ((own->capabilities() & (kCapSampleOsc | kCapUserWavetable | kCapWarp2)) == 0); // the iOS engine sets none of the heavy bits
}

TEST (sample_oscillator_type_plays_the_content_sample_at_the_key_pitch)
{
    Content c;
    c.sample (sine (440.0, 1.0));
    auto e = makeVital();
    c.apply (*e);
    sampleOsc (*e);
    setByKey (*e, "osc1.type", 0.0f);
    const auto wavetable = play (*e, 69, 1.0); // off: the wavetable oscillator (saw)
    e->reset();
    setByKey (*e, "osc1.type", 1.0f);
    const auto sample = play (*e, 69, 1.0);
    CHECK (rms (sample, 0.3, 0.9) > 0.05);
    CHECK (relativeDifference (wavetable, sample, 0.3, 0.9) > 0.5);
    CHECK_NEAR (pitchOf (sample, 440.0), 440.0, 2.0); // the sine at its root key
    e->reset();
    CHECK_NEAR (pitchOf (play (*e, 81, 1.0), 880.0), 880.0, 4.0); // key tracking: an octave up
    e->reset();
    setByKey (*e, "osc1.sample.keytrack", 0.0f);
    CHECK_NEAR (pitchOf (play (*e, 81, 1.0), 440.0), 440.0, 2.0); // switched off: the recorded pitch
    // without the content the type Sample is a silent oscillator
    auto bare = makeVital();
    sampleOsc (*bare);
    CHECK (rms (play (*bare, 69, 0.8), 0.3, 0.7) < 1.0e-4);
}

TEST (sample_loop_modes_start_end_and_reverse)
{
    Content c;
    std::vector<float> burst = sine (440.0, 0.2); // 200 ms
    c.sample (burst);
    auto e = makeVital();
    c.apply (*e);
    sampleOsc (*e);
    setByKey (*e, "env1.sustain", 100.0f);
    setByKey (*e, "env1.release", 20.0f);
    setByKey (*e, "osc1.sample.loop_mode", 0.0f);
    const auto oneShot = play (*e, 69, 1.0);
    CHECK (rms (oneShot, 0.05, 0.15) > 0.05);
    CHECK (rms (oneShot, 0.5, 0.9) < 1.0e-3); // played to its end
    e->reset();
    for (float mode : { 1.0f, 2.0f, 3.0f, 4.0f })
    {
        setByKey (*e, "osc1.sample.loop_mode", mode);
        const auto loop = play (*e, 69, 1.0);
        CHECK (rms (loop, 0.5, 0.9) > 0.05); // every loop mode keeps sounding
        e->reset();
    }
    // the region: start at 50 % plays the second half only
    setByKey (*e, "osc1.sample.loop_mode", 0.0f);
    setByKey (*e, "osc1.sample.start", 50.0f);
    const auto half = play (*e, 69, 1.0);
    CHECK (rms (half, 0.02, 0.08) > 0.05);
    CHECK (rms (half, 0.12, 0.5) < 1.0e-3); // half as long
    e->reset();
    // reverse: a decaying sample plays from its quiet end first
    std::vector<float> decay = sine (440.0, 0.4);
    for (size_t i = 0; i < decay.size(); ++i)
        decay[i] *= std::exp (-6.0f * static_cast<float> (i) / static_cast<float> (decay.size()));
    Content d;
    d.sample (decay);
    d.apply (*e);
    setByKey (*e, "osc1.sample.start", 0.0f);
    const auto forward = play (*e, 69, 0.6);
    e->reset();
    setByKey (*e, "osc1.sample.reverse", 1.0f);
    const auto reversed = play (*e, 69, 0.6);
    CHECK (rms (forward, 0.02, 0.1) > 3.0 * rms (forward, 0.3, 0.38));
    CHECK (rms (reversed, 0.3, 0.38) > 3.0 * rms (reversed, 0.02, 0.1));
}

TEST (sample_scan_normalize_fades_slices_and_unison_start)
{
    Content c;
    c.sample (sine (440.0, 1.5, 0.1f));
    auto e = makeVital();
    c.apply (*e);
    sampleOsc (*e);
    const auto base = play (*e, 69, 0.8);
    // scan: 200 % of the range (+-200 %) doubles the speed, so the pitch
    e->reset();
    setByKey (*e, "osc1.sample.scan_range", 1.0f);
    CHECK_NEAR (pitchOf (play (*e, 69, 1.0), 880.0), 880.0, 4.0);
    e->reset();
    setByKey (*e, "osc1.sample.scan_range", 0.0f);
    setByKey (*e, "osc1.sample.scan_reverse", 1.0f); // backwards: the same pitch, the head runs the other way
    CHECK_NEAR (pitchOf (play (*e, 69, 1.0), 440.0), 440.0, 4.0);
    e->reset();
    setByKey (*e, "osc1.sample.scan_reverse", 0.0f);
    // normalize: the file peaks at 0.1, normalised it is about 8 times louder
    setByKey (*e, "osc1.sample.normalize", 1.0f);
    const auto normalized = play (*e, 69, 0.8);
    CHECK (rms (normalized, 0.3, 0.7) > 5.0 * rms (base, 0.3, 0.7));
    e->reset();
    setByKey (*e, "osc1.sample.normalize", 0.0f);
    // fade edges on a one-shot: the start is faded in over 128 ms
    setByKey (*e, "osc1.sample.loop_mode", 0.0f);
    setByKey (*e, "env1.attack", 0.0f);
    const auto hard = play (*e, 69, 0.4);
    e->reset();
    setByKey (*e, "osc1.sample.fade_edges", 8.0f);
    const auto faded = play (*e, 69, 0.4);
    CHECK (rms (faded, 0.0, 0.02) < 0.3 * rms (hard, 0.0, 0.02));
    e->reset();
    setByKey (*e, "osc1.sample.fade_edges", 0.0f);
    // unison with the same pitch: a start span spreads the voices over the file, so the sum changes
    setByKey (*e, "osc1.sample.loop_mode", 1.0f);
    setByKey (*e, "osc1.unison.voices", 4.0f);
    setByKey (*e, "osc1.unison.detune", 0.0f);
    const auto together = play (*e, 69, 0.8);
    e->reset();
    setByKey (*e, "osc1.sample.uni_span", 37.0f);
    const auto spread = play (*e, 69, 0.8);
    CHECK (relativeDifference (together, spread, 0.3, 0.7) > 0.3);
}

TEST (sample_slices_auto_and_manual_pick_the_slice_by_key)
{
    // four tone sections of a quarter second, each after a 40 ms gap: 200, 300, 500, 800 Hz
    std::vector<float> audio;
    const double freqs[4] = { 200.0, 300.0, 500.0, 800.0 };
    for (double f : freqs)
    {
        if (f != freqs[0])
            audio.insert (audio.end(), 1920, 0.0f);
        const auto tone = sine (f, 0.21);
        audio.insert (audio.end(), tone.begin(), tone.end());
    }
    Content c;
    c.sample (audio);
    auto e = makeVital();
    c.apply (*e);
    sampleOsc (*e);
    setByKey (*e, "osc1.sample.loop_mode", 0.0f);
    setByKey (*e, "env1.attack", 0.0f);
    setByKey (*e, "osc1.sample.slice_mode", 1.0f);
    setByKey (*e, "osc1.sample.slice_play", 1.0f); // a single slice
    setByKey (*e, "osc1.sample.slice_root", 36.0f);
    setByKey (*e, "osc1.sample.slice_threshold", 90.0f);
    const auto slice2 = play (*e, 38, 0.3); // the third slice: 500 Hz
    CHECK_NEAR (crossingFrequency (slice2, 0.05, 0.15), 500.0, 15.0);
    e->reset();
    const auto slice0 = play (*e, 36, 0.3);
    CHECK_NEAR (crossingFrequency (slice0, 0.05, 0.15), 200.0, 15.0);
    // manual: the positions of the content (a quarter of the file each)
    e->reset();
    Content m;
    m.sample (audio, { 0.25f, 0.5f, 0.75f });
    m.apply (*e);
    setByKey (*e, "osc1.sample.slice_mode", 2.0f);
    const auto manual = play (*e, 39, 0.3);
    CHECK_NEAR (crossingFrequency (manual, 0.05, 0.15), 800.0, 20.0);
}

TEST (user_wavetable_preset_table_is_read_from_the_content)
{
    // one frame, a square wave: only odd harmonics; the fallback without content is the saw
    std::vector<float> square (2048);
    for (size_t i = 0; i < square.size(); ++i)
        square[i] = i < 1024 ? 0.7f : -0.7f;
    auto harmonic = [&] (const sbtest::Rendered& r, int k)
    {
        const auto sp = sbtest::powerSpectrum (r.left, static_cast<size_t> (0.3 * kSr), 32768, 131072);
        return bandPower (sp, 131072, kSr, 220.0 * k - 8.0, 220.0 * k + 8.0);
    };
    Content c;
    c.table (square);
    auto without = makeVital();
    setByKey (*without, "osc1.wavetable", 13.0f);
    setByKey (*without, "osc1.unison.voices", 1.0f);
    setByKey (*without, "osc1.random_phase", 0.0f);
    setByKey (*without, "filter1.on", 0.0f);
    const auto saw = play (*without, 57, 1.0); // 220 Hz
    auto with = makeVital();
    c.apply (*with);
    setByKey (*with, "osc1.wavetable", 13.0f);
    setByKey (*with, "osc1.unison.voices", 1.0f);
    setByKey (*with, "osc1.random_phase", 0.0f);
    setByKey (*with, "filter1.on", 0.0f);
    const auto sq = play (*with, 57, 1.0);
    CHECK (harmonic (saw, 2) > 0.05 * harmonic (saw, 1));  // a saw has the second harmonic
    CHECK (harmonic (sq, 2) < 0.002 * harmonic (sq, 1));   // a square has none
    CHECK (harmonic (sq, 3) > 0.05 * harmonic (sq, 1));
    // replacing the content while it plays swaps the table without a crash and without a stale pointer
    std::vector<float> two (4096);
    for (size_t i = 0; i < 2048; ++i)
    {
        two[i] = 0.8f * static_cast<float> (std::sin (2.0 * kPi * static_cast<double> (i) / 2048.0));
        two[2048 + i] = square[i];
    }
    Content c2;
    c2.table (two);
    c2.apply (*with);
    setByKey (*with, "osc1.frame", 100.0f); // 100 % of the 2 frames: the square
    const auto morphed = play (*with, 57, 1.0);
    CHECK (harmonic (morphed, 3) > 0.05 * harmonic (morphed, 1));
}

TEST (warp_slot_2_changes_the_oscillator_and_chains_after_slot_1)
{
    auto e = makeVital();
    const auto setup = [&]
    {
        setByKey (*e, "osc1.wavetable", 1.0f); // saw
        setByKey (*e, "osc1.unison.voices", 1.0f);
        setByKey (*e, "osc1.random_phase", 0.0f);
        setByKey (*e, "filter1.on", 0.0f);
        setByKey (*e, "osc1.warp.type", 0.0f);
        setByKey (*e, "osc1.warp2.type", 0.0f);
    };
    setup();
    const auto none = play (*e, 57, 0.7);
    e->reset();
    setByKey (*e, "osc1.warp2.type", 4.0f); // Bend
    setByKey (*e, "osc1.warp2.amount", 80.0f);
    const auto second = play (*e, 57, 0.7);
    CHECK (relativeDifference (none, second, 0.3, 0.6) > 0.3);
    e->reset();
    // the same effect in slot 1 gives the same sound as in slot 2 (with slot 1 empty)
    setByKey (*e, "osc1.warp2.type", 0.0f);
    setByKey (*e, "osc1.warp.type", 4.0f);
    setByKey (*e, "osc1.warp.amount", 80.0f);
    const auto first = play (*e, 57, 0.7);
    CHECK (relativeDifference (first, second, 0.3, 0.6) < 0.05);
    e->reset();
    // both slots: differs from either alone
    setByKey (*e, "osc1.warp2.type", 3.0f); // Quantize
    setByKey (*e, "osc1.warp2.amount", 60.0f);
    const auto both = play (*e, 57, 0.7);
    CHECK (relativeDifference (first, both, 0.3, 0.6) > 0.1);
    e->reset();
    // modulation of the slot: an LFO-free check - the amount key is a modulation destination
    const ParamInfo* amount = findParamByKey ("osc1.warp2.amount");
    CHECK (amount != nullptr && (amount->flags & kParamModulatable) != 0);
    // slot 2 on the sample oscillator: Quantize reduces the resolution
    Content c;
    c.sample (sine (440.0, 1.0));
    c.apply (*e);
    sampleOsc (*e);
    setByKey (*e, "osc1.warp.type", 0.0f);
    setByKey (*e, "osc1.warp2.type", 0.0f);
    const auto clean = play (*e, 69, 0.8);
    e->reset();
    setByKey (*e, "osc1.warp2.type", 3.0f);
    setByKey (*e, "osc1.warp2.amount", 100.0f);
    const auto crushed = play (*e, 69, 0.8);
    CHECK (relativeDifference (clean, crushed, 0.3, 0.7) > 0.2);
}

TEST (sample_oscillator_survives_content_replacement_and_extreme_settings)
{
    auto e = makeVital();
    Content c;
    c.sample (sine (330.0, 0.5));
    c.apply (*e);
    sampleOsc (*e);
    setByKey (*e, "osc1.unison.voices", 16.0f);
    setByKey (*e, "osc1.unison.detune", 100.0f);
    std::vector<Event> events { noteOn (0, 50), noteOn (0, 62), noteOn (0, 74) };
    auto out = sbtest::render (*e, events, static_cast<int> (0.3 * kSr), 128);
    Content d;
    d.sample (sine (660.0, 0.5));
    d.apply (*e); // replaced while the voices play
    setByKey (*e, "osc1.sample.scan", -100.0f);
    setByKey (*e, "osc1.sample.scan_range", 3.0f);
    setByKey (*e, "osc1.sample.slice_mode", 1.0f);
    setByKey (*e, "osc1.sample.loop_start", 90.0f);
    setByKey (*e, "osc1.sample.loop_end", 10.0f);
    setByKey (*e, "osc1.sample.loop_xfade", 1000.0f);
    auto more = sbtest::render (*e, { noteOn (0, 48), noteOn (0, 60) }, static_cast<int> (0.5 * kSr), 97);
    CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (more.left) && sbtest::allFinite (more.right));
    CHECK (sbtest::peak (more.left) < 4.0f);
}

TEST (oscillator_type_change_under_a_sounding_note_crossfades_in_10_ms)
{
    // a smooth sine table (user wavetable) held at 110 Hz; the type switches to Sample (and back) between two blocks. A hard switch cuts
    // the sine at its current value (a step of up to the peak); the 10 ms crossfade leaves no step larger than the fade slope.
    std::vector<float> table (2048);
    for (size_t i = 0; i < table.size(); ++i)
        table[i] = 0.8f * static_cast<float> (std::sin (2.0 * kPi * static_cast<double> (i) / 2048.0));
    Content c;
    c.table (table);
    c.sample (sine (330.0, 1.0));

    const auto maxStep = [] (const std::vector<float>& v, size_t from, size_t to)
    {
        float m = 0.0f;
        for (size_t i = from + 1; i < to; ++i)
            m = std::max (m, std::fabs (v[i] - v[i - 1]));
        return m;
    };
    for (const bool toSample : { true, false })
    {
        auto e = makeVital();
        c.apply (*e);
        sampleOsc (*e);
        setByKey (*e, "osc1.wavetable", 13.0f);
        setByKey (*e, "osc1.type", toSample ? 0.0f : 1.0f);
        setByKey (*e, "env1.attack", 0.0f);
        setByKey (*e, "env1.release", 500.0f);
        std::vector<float> all;
        const auto append = [&] (const sbtest::Rendered& r) { all.insert (all.end(), r.left.begin(), r.left.end()); };
        append (sbtest::render (*e, { noteOn (0, 45) }, 9984, 256));
        const size_t switchAt = all.size();
        setByKey (*e, "osc1.type", toSample ? 1.0f : 0.0f);
        append (sbtest::render (*e, {}, 4096, 256));
        const float peak = sbtest::peak (all);
        const float slope = maxStep (all, 2000, 4000); // steepest step of the plain sine
        const float stepAfter = maxStep (all, switchAt - 1, switchAt + 2048);
        std::printf ("    %s: peak %.3f, plain step %.5f, step around the switch %.5f, value at the switch %.3f\n", toSample ? "wavetable -> sample" : "sample -> wavetable",
                     static_cast<double> (peak), static_cast<double> (slope), static_cast<double> (stepAfter), static_cast<double> (all[switchAt - 1]));
        CHECK (peak > 0.05f);
        CHECK (stepAfter < 2.5f * slope + peak / 200.0f);
    }
}
