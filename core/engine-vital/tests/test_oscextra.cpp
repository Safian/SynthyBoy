// SynthyBoy - engine-vital tests: B13 osc-extras (G23 sub oscillator, G38 noise colours & extras)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Proves kCapSubOsc and kCapNoiseExtras by rendering with parameters on/off and asserting measurable audio differences.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_bindings.h"
#include "vital_engine.h"

using namespace synthyboy;
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
    return e;
}

/** Silence oscillators 1..3 and noise, master volume unity. */
void silenceMain (IEngine& e)
{
    setByKey (e, "osc1.on", 0.0f);
    setByKey (e, "osc2.on", 0.0f);
    setByKey (e, "osc3.on", 0.0f);
    setByKey (e, "noise.on", 0.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", 0.0f);
}

sbtest::Rendered play (IEngine& e, double seconds = 0.5, int key = 48)
{
    return sbtest::render (e, { noteOn (0, key) }, static_cast<int> (seconds * kSr), 256);
}

double rms (const sbtest::Rendered& r, double t0 = 0.1, double t1 = 0.4)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0 = 0.1, double t1 = 0.4)
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
} // namespace

TEST (oscextra_capabilities_are_reported)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities() & kCapSubOsc) != 0);
    CHECK ((e->capabilities() & kCapNoiseExtras) != 0);
    auto own = createEngine (EngineKind::own);
    CHECK ((own->capabilities() & (kCapSubOsc | kCapNoiseExtras)) == 0);
}

TEST (sub_osc_on_off_and_level)
{
    auto e = makeVital();
    silenceMain (*e);

    // sub off -> silence
    setByKey (*e, "sub.on", 0.0f);
    auto silent = play (*e);
    CHECK (rms (silent) < 1.0e-4);

    // sub on -> audible tone
    setByKey (*e, "sub.on", 1.0f);
    setByKey (*e, "sub.level", 80.0f);
    setByKey (*e, "sub.destination", 3.0f); // Effects (unfiltered)
    auto sound = play (*e);
    CHECK (rms (sound) > 0.05);

    // sub level 0 -> silent again
    setByKey (*e, "sub.level", 0.0f);
    auto quiet = play (*e);
    CHECK (rms (quiet) < 1.0e-4);
}

TEST (sub_osc_waveforms_differ)
{
    auto e = makeVital();
    silenceMain (*e);
    setByKey (*e, "sub.on", 1.0f);
    setByKey (*e, "sub.level", 80.0f);
    setByKey (*e, "sub.destination", 3.0f);

    std::vector<sbtest::Rendered> renders;
    for (int w = 0; w < 6; ++w)
    {
        setByKey (*e, "sub.waveform", static_cast<float> (w));
        renders.push_back (play (*e));
        CHECK (rms (renders.back()) > 0.03);
    }

    // Every waveform renders a measurably distinct shape
    for (size_t i = 0; i < renders.size(); ++i)
        for (size_t j = i + 1; j < renders.size(); ++j)
        {
            double diff = relativeDifference (renders[i], renders[j]);
            CHECK (diff > 0.05);
        }
}

TEST (sub_osc_octave_and_tune)
{
    auto e = makeVital();
    silenceMain (*e);
    setByKey (*e, "sub.on", 1.0f);
    setByKey (*e, "sub.level", 80.0f);
    setByKey (*e, "sub.destination", 3.0f);

    setByKey (*e, "sub.octave", -1.0f);
    auto rOctMinus1 = play (*e);

    setByKey (*e, "sub.octave", 0.0f);
    auto rOct0 = play (*e);

    CHECK (relativeDifference (rOctMinus1, rOct0) > 0.2);

    setByKey (*e, "sub.tune", 50.0f);
    auto rTune50 = play (*e);
    CHECK (relativeDifference (rOct0, rTune50) > 0.1);
}

TEST (sub_osc_pan_and_destination)
{
    auto e = makeVital();
    silenceMain (*e);
    setByKey (*e, "sub.on", 1.0f);
    setByKey (*e, "sub.level", 80.0f);

    // Left pan vs Right pan
    setByKey (*e, "sub.pan", -100.0f);
    setByKey (*e, "sub.destination", 4.0f); // Direct out
    auto leftPan = play (*e);
    double lRms = sbtest::rmsOf (leftPan.left, static_cast<size_t> (0.1 * kSr), static_cast<size_t> (0.4 * kSr));
    double rRms = sbtest::rmsOf (leftPan.right, static_cast<size_t> (0.1 * kSr), static_cast<size_t> (0.4 * kSr));
    CHECK (lRms > 0.05);
    CHECK (rRms < 1.0e-3);

    setByKey (*e, "sub.pan", 100.0f);
    auto rightPan = play (*e);
    lRms = sbtest::rmsOf (rightPan.left, static_cast<size_t> (0.1 * kSr), static_cast<size_t> (0.4 * kSr));
    rRms = sbtest::rmsOf (rightPan.right, static_cast<size_t> (0.1 * kSr), static_cast<size_t> (0.4 * kSr));
    CHECK (lRms < 1.0e-3);
    CHECK (rRms > 0.05);
}

TEST (sub_osc_mono_lowest_note)
{
    auto e = makeVital();
    silenceMain (*e);
    setByKey (*e, "sub.on", 1.0f);
    setByKey (*e, "sub.level", 80.0f);
    setByKey (*e, "sub.destination", 3.0f);

    // Mono mode = On vs Off when playing two notes
    setByKey (*e, "sub.mono", 1.0f);
    auto rMono = sbtest::render (*e, { noteOn (0, 36), noteOn (0, 48) }, static_cast<int> (0.5 * kSr), 256);

    setByKey (*e, "sub.mono", 0.0f);
    auto rPoly = sbtest::render (*e, { noteOn (0, 36), noteOn (0, 48) }, static_cast<int> (0.5 * kSr), 256);

    CHECK (rms (rMono) > 0.03);
    CHECK (rms (rPoly) > 0.03);
    CHECK (relativeDifference (rMono, rPoly) > 0.1);
}

TEST (noise_sample_selection_and_audio)
{
    auto e = makeVital();
    silenceMain (*e);
    setByKey (*e, "noise.on", 1.0f);
    setByKey (*e, "noise.level", 50.0f);
    setByKey (*e, "noise.destination", 3.0f);

    std::vector<sbtest::Rendered> noiseRenders;
    for (int s = 0; s < 9; ++s)
    {
        setByKey (*e, "noise.sample", static_cast<float> (s));
        noiseRenders.push_back (play (*e));
        CHECK (rms (noiseRenders.back()) > 0.01);
    }

    // White (0), Pink (1), and Brown (2) noise have distinctly colored spectra
    double whitePinkDiff = relativeDifference (noiseRenders[0], noiseRenders[1]);
    double whiteBrownDiff = relativeDifference (noiseRenders[0], noiseRenders[2]);
    CHECK (whitePinkDiff > 0.1);
    CHECK (whiteBrownDiff > 0.15);
}

TEST (noise_pitch_and_keytrack)
{
    auto e = makeVital();
    silenceMain (*e);
    setByKey (*e, "noise.on", 1.0f);
    setByKey (*e, "noise.level", 50.0f);
    setByKey (*e, "noise.destination", 3.0f);
    setByKey (*e, "noise.sample", 1.0f); // Pink noise

    setByKey (*e, "noise.pitch", 0.0f);
    auto p0 = play (*e);

    setByKey (*e, "noise.pitch", 24.0f);
    auto p24 = play (*e);

    CHECK (relativeDifference (p0, p24) > 0.1);

    // Keytracking
    setByKey (*e, "noise.pitch", 0.0f);
    setByKey (*e, "noise.keytrack", 1.0f);
    auto k36 = play (*e, 0.5, 36);
    auto k72 = play (*e, 0.5, 72);
    CHECK (relativeDifference (k36, k72) > 0.15);
}
