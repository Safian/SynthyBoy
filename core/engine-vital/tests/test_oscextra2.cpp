// SynthyBoy - engine-vital tests: oscillator extras of block B27 (GROUPB WP-05): Ratio SRC, smooth interpolation, single slice destination.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// kCapOscExtras2 is proven here: every key is rendered off and on and the audio must differ in the way the key says.

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

sbtest::Rendered play (IEngine& e, int key, double seconds)
{
    return sbtest::render (e, { noteOn (0, key) }, static_cast<int> (seconds * kSr), 256);
}

double pitchOf (const sbtest::Rendered& r, double guess, double from = 0.3)
{
    const auto spectrum = sbtest::powerSpectrum (r.left, static_cast<size_t> (from * kSr), 32768, 131072);
    return sbtest::peakFrequency (spectrum, 131072, kSr, guess);
}

double crossingFrequency (const sbtest::Rendered& r, double t0, double t1)
{
    int crossings = 0;
    for (size_t i = static_cast<size_t> (t0 * kSr) + 1; i < static_cast<size_t> (t1 * kSr) && i < r.left.size(); ++i)
        if (r.left[i - 1] < 0.0f && r.left[i] >= 0.0f)
            ++crossings;
    return crossings / (t1 - t0);
}

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

/** osc 1 silent but running (the source of a ratio), osc 2 a sine-ish saw in Ratio mode with ratio 2 that is the only thing heard. */
void ratioPatch (IEngine& e)
{
    setByKey (e, "osc1.on", 1.0f);
    setByKey (e, "osc1.level", 0.0f);
    setByKey (e, "osc1.wavetable", 0.0f); // the first table: a sine at frame 0
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc2.on", 1.0f);
    setByKey (e, "osc2.wavetable", 0.0f);
    setByKey (e, "osc2.level", 100.0f);
    setByKey (e, "osc2.destination", 3.0f);
    setByKey (e, "osc2.unison.voices", 1.0f);
    setByKey (e, "osc2.random_phase", 0.0f);
    setByKey (e, "osc2.pitch_mode", 2.0f); // Ratio
    setByKey (e, "osc2.pitch_coarse", 1.0f); // x 2
    setByKey (e, "osc3.on", 0.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
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

std::vector<float> sine (double freq, double seconds, float amp = 0.8f)
{
    std::vector<float> v (static_cast<size_t> (seconds * 48000));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = amp * static_cast<float> (std::sin (2.0 * kPi * freq * static_cast<double> (i) / 48000));
    return v;
}

/** Four tone sections (200, 300, 500, 800 Hz) separated by gaps: four auto slices. */
std::vector<float> fourTones()
{
    std::vector<float> audio;
    const double freqs[4] = { 200.0, 300.0, 500.0, 800.0 };
    for (double f : freqs)
    {
        if (f != freqs[0])
            audio.insert (audio.end(), 1920, 0.0f);
        const auto tone = sine (f, 0.21);
        audio.insert (audio.end(), tone.begin(), tone.end());
    }
    return audio;
}

struct Content
{
    MemoryContentStore store;
    StateContent state;
    void sample (const std::vector<float>& audio)
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
        state.samples.push_back (c);
    }
    void apply (IEngine& e)
    {
        e.setContentStore (&store);
        e.setContent (state);
    }
};

void slicePatch (IEngine& e, Content& c)
{
    c.apply (e);
    setByKey (e, "osc1.type", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.phase", 0.0f);
    setByKey (e, "osc1.sample.root", 69.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
    setByKey (e, "osc1.sample.loop_mode", 0.0f);
    setByKey (e, "env1.attack", 0.0f);
    setByKey (e, "osc1.sample.slice_mode", 1.0f);
    setByKey (e, "osc1.sample.slice_play", 1.0f);
    setByKey (e, "osc1.sample.slice_root", 36.0f);
    setByKey (e, "osc1.sample.slice_threshold", 90.0f);
}
} // namespace

TEST (oscextra2_capability_is_reported)
{
    auto e = makeVital();
    CHECK ((e->capabilities() & kCapOscExtras2) != 0);
}

TEST (ratio_source_tunes_an_oscillator_to_another_oscillator_or_the_sub)
{
    // key 57 = 220 Hz. Base note: osc 2 in Ratio mode x 2 = 440 Hz
    auto e = makeVital();
    ratioPatch (*e);
    CHECK_NEAR (pitchOf (play (*e, 57, 0.8), 440.0), 440.0, 3.0);

    // source Osc 1 transposed by +7 st (329.63 Hz): 659.26 Hz, and osc 2 follows a different key
    e = makeVital();
    ratioPatch (*e);
    setByKey (*e, "osc1.transpose", 7.0f);
    setByKey (*e, "osc2.ratio_src", 1.0f);
    CHECK_NEAR (pitchOf (play (*e, 57, 0.8), 659.26), 659.26, 4.0);
    e->reset();
    CHECK_NEAR (pitchOf (play (*e, 69, 0.8), 1318.5), 1318.5, 8.0);

    // the source in Ratio mode itself (x 3 of the note), osc 2 is x 2 of that: 220 x 3 x 2 = 1320 Hz
    e = makeVital();
    ratioPatch (*e);
    setByKey (*e, "osc1.pitch_mode", 2.0f);
    setByKey (*e, "osc1.pitch_coarse", 2.0f);
    setByKey (*e, "osc2.ratio_src", 1.0f);
    CHECK_NEAR (pitchOf (play (*e, 57, 0.8), 1320.0), 1320.0, 8.0);

    // source Sub (octave -1 = 110 Hz at key 57): x 2 = 220 Hz
    e = makeVital();
    ratioPatch (*e);
    setByKey (*e, "sub.on", 1.0f);
    setByKey (*e, "sub.level", 0.0f);
    setByKey (*e, "sub.octave", -1.0f);
    setByKey (*e, "osc2.ratio_src", 4.0f);
    CHECK_NEAR (pitchOf (play (*e, 57, 0.8), 220.0), 220.0, 2.0);

    // the source follows the source oscillator's pitch while it is changed
    e = makeVital();
    ratioPatch (*e);
    setByKey (*e, "osc2.ratio_src", 1.0f);
    const auto lowSource = play (*e, 57, 0.6);
    CHECK_NEAR (pitchOf (lowSource, 440.0), 440.0, 3.0);
    e->reset();
    setByKey (*e, "osc1.transpose", 12.0f);
    CHECK_NEAR (pitchOf (play (*e, 57, 0.6), 880.0), 880.0, 5.0);
}

TEST (ratio_source_outside_ratio_mode_a_switched_off_source_and_loops_use_the_note)
{
    // the source is only read in Ratio mode: Semitones mode with a source set plays the note
    auto e = makeVital();
    ratioPatch (*e);
    setByKey (*e, "osc1.transpose", 7.0f);
    setByKey (*e, "osc2.ratio_src", 1.0f);
    setByKey (*e, "osc2.pitch_mode", 0.0f);
    setByKey (*e, "osc2.pitch_coarse", 0.0f);
    CHECK_NEAR (pitchOf (play (*e, 57, 0.8), 220.0), 220.0, 2.0);

    // a source that is switched off does not tune anything
    e = makeVital();
    ratioPatch (*e);
    setByKey (*e, "osc1.transpose", 7.0f);
    setByKey (*e, "osc1.on", 0.0f);
    setByKey (*e, "osc2.ratio_src", 1.0f);
    CHECK_NEAR (pitchOf (play (*e, 57, 0.8), 440.0), 440.0, 3.0);

    // the oscillator itself as the source, and two oscillators tuned to each other: the note of the voice, finite audio
    e = makeVital();
    ratioPatch (*e);
    setByKey (*e, "osc2.ratio_src", 2.0f);
    CHECK_NEAR (pitchOf (play (*e, 57, 0.8), 440.0), 440.0, 3.0);
    e = makeVital();
    ratioPatch (*e);
    setByKey (*e, "osc1.pitch_mode", 2.0f);
    setByKey (*e, "osc1.ratio_src", 2.0f);
    setByKey (*e, "osc2.ratio_src", 1.0f);
    const auto loop = play (*e, 57, 0.8);
    CHECK (sbtest::allFinite (loop.left));
    CHECK_NEAR (pitchOf (loop, 440.0), 440.0, 3.0);
}

TEST (ratio_source_with_a_chord_tunes_every_voice_to_its_own_source)
{
    auto e = makeVital();
    ratioPatch (*e);
    setByKey (*e, "osc2.ratio_src", 1.0f);
    setByKey (*e, "osc1.transpose", 12.0f);
    const auto chord = sbtest::render (*e, { noteOn (0, 57), noteOn (0, 64) }, static_cast<int> (0.8 * kSr), 256);
    const auto spectrum = sbtest::powerSpectrum (chord.left, static_cast<size_t> (0.3 * kSr), 32768, 131072);
    const double fA = 220.0 * 2.0 * 2.0, fE = 329.63 * 2.0 * 2.0; // (key x 2 octave of osc 1) x ratio 2
    CHECK_NEAR (sbtest::peakFrequency (spectrum, 131072, kSr, fA), fA, 5.0);
    CHECK_NEAR (sbtest::peakFrequency (spectrum, 131072, kSr, fE), fE, 8.0);
}

TEST (smooth_interpolation_off_steps_between_frames_on_blends_them)
{
    // the first table with 64 morph frames: the position is a percentage of 63 steps
    const auto render = [&] (float frame, float smooth, bool modulated = false)
    {
        auto e = makeVital();
        setByKey (*e, "osc1.on", 1.0f);
        setByKey (*e, "osc1.wavetable", 8.0f);
        setByKey (*e, "osc1.frame", frame);
        setByKey (*e, "osc1.smooth_interp", smooth);
        setByKey (*e, "osc1.level", 100.0f);
        setByKey (*e, "osc1.unison.voices", 1.0f);
        setByKey (*e, "osc1.random_phase", 0.0f);
        setByKey (*e, "osc1.destination", 3.0f);
        setByKey (*e, "filter1.on", 0.0f);
        setByKey (*e, "master.volume", -6.0f);
        if (modulated)
        {
            const auto choice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc1.frame")->id)));
            setByKey (*e, "mod01.source", 19.0f); // Macro 1
            setByKey (*e, "mod01.destination", static_cast<float> (choice));
            setByKey (*e, "mod01.amount", 100.0f);
            setByKey (*e, "macro.1", 50.0f);
        }
        return play (*e, 48, 0.6);
    };
    // 40 % of 63 = 25.2: floor 25. 39.7 % = 25.01: floor 25. Off steps: the two renders are bit identical
    CHECK_EQ (relativeDifference (render (40.0f, 0.0f), render (39.7f, 0.0f), 0.1, 0.5), 0.0);
    CHECK (sbtest::rmsOf (render (40.0f, 0.0f).left, 24000, 28000) > 0.01);
    // On blends 20 % of the next frame in: it differs from Off and from On at the frame itself
    const auto off = render (40.0f, 0.0f), on = render (40.0f, 1.0f), onAtFrame = render (39.7f, 1.0f);
    std::printf ("    smooth off / on difference %g, on at the frame / on %g\n", relativeDifference (off, on, 0.1, 0.5), relativeDifference (onAtFrame, on, 0.1, 0.5));
    CHECK (relativeDifference (off, on, 0.1, 0.5) > 1.0e-3);
    CHECK (relativeDifference (onAtFrame, on, 0.1, 0.5) > 1.0e-3);
    CHECK (sbtest::allFinite (on.left));
    // exactly on a frame (fraction 0) both are the same sound
    CHECK_EQ (relativeDifference (render (0.0f, 0.0f), render (0.0f, 1.0f), 0.1, 0.5), 0.0);
    // at a modulated WT POS (0 % + 50 % of the macro): the same difference
    CHECK (relativeDifference (render (0.0f, 0.0f, true), render (0.0f, 1.0f, true), 0.1, 0.5) > 1.0e-3 ||
           relativeDifference (render (1.0f, 0.0f, true), render (1.0f, 1.0f, true), 0.1, 0.5) > 1.0e-3);
}

TEST (single_slice_destination_moves_the_played_slice_by_value_and_by_modulation)
{
    Content content;
    content.sample (fourTones());
    // key 36 = slice 0 (200 Hz); +33.3 % of the 3 steps = slice 1, +100 % = slice 3
    const auto frequencyWith = [&] (float slice)
    {
        auto e = makeVital();
        slicePatch (*e, content);
        setByKey (*e, "osc1.sample.slice", slice);
        return crossingFrequency (play (*e, 36, 0.3), 0.05, 0.15);
    };
    CHECK_NEAR (frequencyWith (0.0f), 200.0, 15.0);
    CHECK_NEAR (frequencyWith (33.34f), 300.0, 15.0);
    CHECK_NEAR (frequencyWith (66.67f), 500.0, 15.0);
    CHECK_NEAR (frequencyWith (100.0f), 800.0, 20.0);
    // negative values move down: key 39 is slice 3, -100 % is slice 0
    {
        auto e = makeVital();
        slicePatch (*e, content);
        setByKey (*e, "osc1.sample.slice", -100.0f);
        CHECK_NEAR (crossingFrequency (play (*e, 39, 0.3), 0.05, 0.15), 200.0, 15.0);
    }
    // not in Play Slice to End mode: the slice destination only acts on a single slice
    {
        auto e = makeVital();
        slicePatch (*e, content);
        setByKey (*e, "osc1.sample.slice_play", 0.0f);
        setByKey (*e, "osc1.sample.slice", 100.0f);
        CHECK_NEAR (crossingFrequency (play (*e, 36, 0.15), 0.03, 0.12), 200.0, 15.0);
    }
    // by modulation: Macro 1 -> the slice destination
    const auto choice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc1.sample.slice")->id)));
    CHECK (choice >= 909);
    const auto withMacro = [&] (float macro)
    {
        auto e = makeVital();
        slicePatch (*e, content);
        setByKey (*e, "mod01.source", 19.0f); // Macro 1
        setByKey (*e, "mod01.destination", static_cast<float> (choice));
        setByKey (*e, "mod01.amount", 100.0f);
        setByKey (*e, "macro.1", macro);
        return crossingFrequency (play (*e, 36, 0.3), 0.05, 0.15);
    };
    CHECK_NEAR (withMacro (0.0f), 200.0, 15.0);
    CHECK_NEAR (withMacro (100.0f), 800.0, 20.0);
}
