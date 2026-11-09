// SynthyBoy - engine-vital tests: helpers of the GROUPC oscillator tests (granular, spectral, multisample): a Vital engine at 48 kHz, synthetic samples as
// WAV files in a MemoryContentStore, off / on render comparisons.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
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

namespace groupc
{
using namespace synthyboy;
using sbtest::noteOn;
using sbtest::setByKey;

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

inline std::unique_ptr<IEngine> makeVital (const Quality& q = Quality {})
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, q);
    return e;
}

inline sbtest::Rendered play (IEngine& e, int key, double seconds, float velocity = 1.0f)
{
    return sbtest::render (e, { noteOn (0, key, velocity) }, static_cast<int> (seconds * kSr), 256);
}

inline double crossingFrequency (const sbtest::Rendered& r, double t0, double t1)
{
    int crossings = 0;
    for (size_t i = static_cast<size_t> (t0 * kSr) + 1; i < static_cast<size_t> (t1 * kSr) && i < r.left.size(); ++i)
        if (r.left[i - 1] < 0.0f && r.left[i] >= 0.0f)
            ++crossings;
    return crossings / (t1 - t0);
}

inline double rmsBetween (const sbtest::Rendered& r, double t0, double t1)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), std::min (r.left.size(), static_cast<size_t> (t1 * kSr)));
}

inline double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0, double t1)
{
    double d = 0.0, n = 0.0;
    const size_t from = static_cast<size_t> (t0 * kSr), to = std::min (std::min (a.left.size(), b.left.size()), static_cast<size_t> (t1 * kSr));
    for (size_t i = from; i < to; ++i)
    {
        const double x = static_cast<double> (a.left[i]) - b.left[i];
        d += x * x;
        n += static_cast<double> (a.left[i]) * a.left[i] + 1.0e-12;
    }
    return n > 0.0 ? std::sqrt (d / n) : 0.0;
}

inline bool identical (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    return a.left == b.left && a.right == b.right;
}

inline std::vector<uint8_t> wav16 (const std::vector<float>& samples, int sampleRate = 48000, int channels = 1)
{
    std::vector<uint8_t> b;
    const auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (v >> (8 * i))); };
    const auto put16 = [&] (uint16_t v) { b.push_back (static_cast<uint8_t> (v)); b.push_back (static_cast<uint8_t> (v >> 8)); };
    const auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (t[i])); };
    tag ("RIFF"); put32 (static_cast<uint32_t> (36 + samples.size() * 2)); tag ("WAVE");
    tag ("fmt "); put32 (16); put16 (1); put16 (static_cast<uint16_t> (channels)); put32 (static_cast<uint32_t> (sampleRate));
    put32 (static_cast<uint32_t> (sampleRate * 2 * channels)); put16 (static_cast<uint16_t> (2 * channels)); put16 (16);
    tag ("data"); put32 (static_cast<uint32_t> (samples.size() * 2));
    for (float s : samples)
        put16 (static_cast<uint16_t> (static_cast<int16_t> (std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767.0f))));
    return b;
}

inline std::vector<float> sine (double freq, double seconds, float amp = 0.8f)
{
    std::vector<float> v (static_cast<size_t> (seconds * 48000));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = amp * static_cast<float> (std::sin (2.0 * kPi * freq * static_cast<double> (i) / 48000));
    return v;
}

/** `first` Hz for the first half, `second` Hz for the second half (the position / scan of the oscillators moves between them). */
inline std::vector<float> twoTones (double first, double second, double seconds, float amp = 0.8f)
{
    auto a = sine (first, seconds * 0.5, amp);
    const auto b = sine (second, seconds * 0.5, amp);
    a.insert (a.end(), b.begin(), b.end());
    return a;
}

/** A harmonically rich tone (a saw built from 20 partials), for the spectral oscillator. */
inline std::vector<float> richTone (double freq, double seconds, float amp = 0.5f)
{
    std::vector<float> v (static_cast<size_t> (seconds * 48000), 0.0f);
    for (int h = 1; h <= 20; ++h)
        for (size_t i = 0; i < v.size(); ++i)
            v[i] += amp * (1.0f / static_cast<float> (h)) * static_cast<float> (std::sin (2.0 * kPi * freq * h * static_cast<double> (i) / 48000));
    float p = 0.0f;
    for (float s : v) p = std::max (p, std::fabs (s));
    for (float& s : v) s *= 0.8f / p;
    return v;
}

struct Content
{
    MemoryContentStore store;
    StateContent state;
    void sample (const std::vector<float>& audio, int osc = 1, const char* file = "sample.wav")
    {
        store.add ("test", file, wav16 (audio));
        ContentSample c;
        c.osc = osc;
        c.ref.pack = "test";
        c.ref.file = file;
        c.ref.name = "sample";
        c.sampleRate = 48000;
        c.frames = static_cast<int> (audio.size());
        c.channels = 1;
        state.samples.push_back (c);
    }
    /** An instrument for oscillator `osc`: the SFZ text and its samples (name -> audio) in the pack "test", the folder "inst/". */
    void instrument (const std::string& sfz, const std::map<std::string, std::vector<float>>& samples, int osc = 1)
    {
        store.add ("test", "inst/instrument.sfz", std::vector<uint8_t> (sfz.begin(), sfz.end()));
        for (const auto& s : samples)
            store.add ("test", "inst/" + s.first, wav16 (s.second));
        ContentInstrument c;
        c.osc = osc;
        c.ref.pack = "test";
        c.ref.file = "inst/instrument.sfz";
        c.ref.name = "instrument";
        state.instruments.push_back (c);
    }
    void apply (IEngine& e)
    {
        e.setContentStore (&store);
        e.setContent (state);
    }
};

/** osc 1 alone, plain: no filter, no random phase, one voice, the sample root at A3 (key 69 = 440 Hz at the recorded pitch). */
inline void basePatch (IEngine& e, Content& c, int type)
{
    c.apply (e);
    setByKey (e, "osc1.type", static_cast<float> (type));
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.phase", 0.0f);
    setByKey (e, "osc1.sample.root", 69.0f);
    setByKey (e, "osc2.on", 0.0f);
    setByKey (e, "osc3.on", 0.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
    setByKey (e, "env1.attack", 0.0f);
    setByKey (e, "env1.sustain", 100.0f);
    setByKey (e, "env1.release", 50.0f);
}
} // namespace groupc
