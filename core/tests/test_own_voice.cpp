// SynthyBoy - own engine tests, voice side: tuning, scale, MPE, voice override (kill), recall, poly legato, modulation extras, mixer, filters, effects.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "own_oracle.h"
#include "synthyboy/engine.h"
#include "synthyboy/content_state.h"
#include "synthyboy/modulation.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;

std::unique_ptr<IEngine> make()
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, qualityForTier (Tier::high));
    return e;
}

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

float srcChoice (const char* key)
{
    for (size_t i = 0; i < modSourceCount(); ++i)
        if (std::string (modSourceTable()[i].key) == key)
            return static_cast<float> (i + 1);
    return 0.0f;
}

float dstChoice (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (p->id)))) : 0.0f;
}

/** A pure sine patch: the first oscillator on a sine, no filter, a short attack. */
void sinePatch (IEngine& e)
{
    set (e, "osc1.wavetable", 0);
    set (e, "osc1.level", 100);
    set (e, "filter1.on", 0);
    set (e, "master.volume", 0);
    set (e, "env1.attack", 1);
    set (e, "env1.release", 50);
}

/** The dominant frequency of x in [from, from + n) (parabolic interpolation of the spectrum peak). */
double freqOf (const std::vector<float>& x, size_t from, size_t n = 16384)
{
    const auto ps = oracle::powerSpectrum (x, from, n);
    const auto peaks = oracle::topPeaks (ps, kSr, n, 1);
    return peaks.empty() ? 0.0 : peaks[0].hz;
}

double ampOf (const std::vector<float>& x, size_t from, size_t n)
{
    return oracle::rms (x, from, from + n) * 1.41421356;
}

sbtest::Rendered run (IEngine& e, const std::vector<Event>& ev, double seconds)
{
    return sbtest::render (e, ev, static_cast<int> (seconds * kSr), 256);
}

Event frameAt (double t, Event e)
{
    e.frame = static_cast<int32_t> (t * kSr);
    return e;
}

Event expression (double t, int key, Event::Expression ex, float value, int channel = 0)
{
    Event e = sbtest::noteOn (static_cast<int> (t * kSr), key, 0.0f, channel);
    e.type = Event::NoteExpression;
    e.expression = ex;
    e.value = value;
    return e;
}

Event aftertouchEvent (double t, float value, int channel)
{
    Event e = sbtest::noteOn (static_cast<int> (t * kSr), 0, value, channel);
    e.type = Event::Aftertouch;
    return e;
}
} // namespace

// ------------------------------------------------------------------------------------------------------------------------------------
// tuning, scale, A4, MPE

TEST (ownv_concert_pitch_and_tuning_scales)
{
    struct Case { float a4; int scale; int key; double hz; };
    const Case cases[] = { { 440, 0, 69, 440.0 }, { 432, 0, 69, 432.0 }, { 466.16f, 0, 69, 466.16 }, { 440, 1, 60, 440.0 * 16.0 / 27.0 }, { 440, 2, 60, 264.0 },
                           { 440, 2, 64, 330.0 }, { 440, 1, 69, 440.0 } };
    for (const Case& c : cases)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "master.tuning.a4", c.a4);
        set (*e, "master.tuning.scale", static_cast<float> (c.scale));
        const auto o = run (*e, { sbtest::noteOn (0, c.key) }, 0.6);
        const double f = freqOf (o.left, 8000);
        if (!CHECK (std::fabs (f - c.hz) < 0.6))
            std::printf ("    a4 %.2f scale %d key %d: %.2f Hz, expected %.2f\n", c.a4, c.scale, c.key, f, c.hz);
    }
}

TEST (ownv_user_tuning_file_and_missing_keys)
{
    auto e = make();
    sinePatch (*e);
    StateContent c;
    c.tuning.present = true;
    c.tuning.scl = "! test\ntwo notes\n 2\n 700.0\n 2/1\n";
    e->setContent (c);
    set (*e, "master.tuning.scale", 3);
    const auto o = run (*e, { sbtest::noteOn (0, 62) }, 0.6); // degree 2 of the 2 note scale = the octave above the middle note 60: 523.25 * ...
    const double f = freqOf (o.left, 8000);
    CHECK (std::fabs (f - 36.69) < 1.0); // the mapping of Scala: degree 2 of the two note scale, A4 = 440 Hz at degree 9
}

TEST (ownv_scale_conforming_moves_the_key_down_on_a_tie)
{
    auto e = make();
    sinePatch (*e);
    set (*e, "master.scale.key", 0);  // C
    set (*e, "master.scale.type", 1); // Major
    const auto o = run (*e, { sbtest::noteOn (0, 61) }, 0.6); // C# -> C (the lower one of the equally near C and D)
    CHECK (std::fabs (freqOf (o.left, 8000) - 261.63) < 0.6);
    auto e2 = make();
    sinePatch (*e2);
    set (*e2, "master.scale.type", 1);
    set (*e2, "master.scale.mode", 1); // mark only: nothing changes
    const auto o2 = run (*e2, { sbtest::noteOn (0, 61) }, 0.6);
    CHECK (std::fabs (freqOf (o2.left, 8000) - 277.18) < 0.6);
}

TEST (ownv_mpe_member_channel_bend)
{
    auto e = make();
    sinePatch (*e);
    set (*e, "master.mpe.on", 1);
    set (*e, "master.mpe.bend_range", 48);
    set (*e, "master.bend_range", 2);
    // a note on member channel 1 bent by a quarter of the 48 st range = 12 st; the manager channel bends every note by its own range
    auto b = sbtest::pitchBend (100, 0.25f, 1);
    const auto o = run (*e, { sbtest::noteOn (0, 57, 1.0f, 1), b }, 0.6);
    CHECK (std::fabs (freqOf (o.left, 8000) - 440.0) < 0.8);
    auto e2 = make();
    sinePatch (*e2);
    set (*e2, "master.mpe.on", 0); // without MPE the member channel uses the global range of 2 st
    const auto o2 = run (*e2, { sbtest::noteOn (0, 57, 1.0f, 1), sbtest::pitchBend (100, 0.25f, 1) }, 0.6);
    CHECK (std::fabs (freqOf (o2.left, 8000) - 220.0 * std::pow (2.0, 0.5 / 12.0)) < 0.8);
    // the manager channel adds to the member channel
    auto e3 = make();
    sinePatch (*e3);
    set (*e3, "master.mpe.on", 1);
    set (*e3, "master.bend_range", 12);
    const auto o3 = run (*e3, { sbtest::noteOn (0, 57, 1.0f, 1), sbtest::pitchBend (50, 1.0f, 0), sbtest::pitchBend (100, 0.25f, 1) }, 0.6);
    CHECK (std::fabs (freqOf (o3.left, 8000) - 880.0) < 1.5);
}

TEST (ownv_mpe_axes_drive_macros_and_modwheel)
{
    // Y (slide) to macro 1 and the matrix macro1 -> level: the level follows the note expression
    auto render = [&] (int mode, float y)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "osc1.level", 50);
        set (*e, "mod01.source", srcChoice ("macro1"));
        set (*e, "mod01.destination", dstChoice ("osc1.level"));
        set (*e, "mod01.amount", -50);
        set (*e, "master.mpe.xyz_macro", static_cast<float> (mode));
        const auto o = run (*e, { sbtest::noteOn (0, 69, 1.0f, 1), expression (0.05, 69, Event::Brightness, y, 1) }, 0.5);
        return ampOf (o.left, 12000, 4800);
    };
    const double off = render (0, 1.0f), on = render (2, 1.0f), on0 = render (2, 0.0f);
    CHECK (std::fabs (off - on0) < 0.02 * off + 1.0e-4); // the axis at 0 equals the knob at 0
    CHECK (on < 0.8 * off);                             // the axis at 1 is macro 1 = 100 %: -50 % amount lowers the level
}

// ------------------------------------------------------------------------------------------------------------------------------------
// voice override (kill), recall, poly legato

TEST (ownv_kill_mode_fades_the_excess_voice_in_50_ms)
{
    auto e = make();
    sinePatch (*e);
    set (*e, "master.polyphony", 1);
    set (*e, "master.voice_override", 0); // Kill
    set (*e, "env1.release", 2000);
    set (*e, "env1.attack", 0);
    const auto o = run (*e, { sbtest::noteOn (0, 60), sbtest::noteOn (static_cast<int> (0.2 * kSr), 72) }, 0.5);
    // 261 Hz is the first note: at 0.2 s it is alive, 20 ms later still partly (the fade), 100 ms later gone
    auto line = [&] (double t0, double hz)
    {
        const size_t a = static_cast<size_t> (t0 * kSr), n = 4096;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            re += o.left[a + i] * std::cos (2.0 * M_PI * hz * static_cast<double> (i) / kSr);
            im += o.left[a + i] * std::sin (2.0 * M_PI * hz * static_cast<double> (i) / kSr);
        }
        return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (n);
    };
    const double before = line (0.1, 261.6256), during = line (0.2, 261.6256), after = line (0.35, 261.6256);
    CHECK (before > 0.5);
    CHECK (during < before && during > 0.05 * before); // still fading in the first part of the 50 ms window
    CHECK (after < 0.02 * before);
    CHECK (line (0.35, 523.2511) > 0.5);
}

TEST (ownv_note_recall_on_release_with_one_voice)
{
    for (int override = 0; override < 2; ++override)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "master.polyphony", 1);
        set (*e, "master.voice_override", static_cast<float> (override));
        set (*e, "env1.release", 20);
        const auto o = run (*e, { sbtest::noteOn (0, 60), sbtest::noteOn (static_cast<int> (0.3 * kSr), 64), sbtest::noteOff (static_cast<int> (0.6 * kSr), 64) }, 1.0);
        const double f1 = freqOf (o.left, static_cast<size_t> (0.4 * kSr), 8192), f2 = freqOf (o.left, static_cast<size_t> (0.8 * kSr));
        CHECK (std::fabs (f1 - 329.63) < 1.0);
        CHECK (std::fabs (f2 - 261.63) < 1.0); // the held key 60 sounds again
    }
}

TEST (ownv_poly_legato_continues_the_envelope)
{
    auto level = [&] (float polyLegato)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "env1.attack", 1000);
        set (*e, "master.poly_legato", polyLegato);
        const auto o = run (*e, { sbtest::noteOn (0, 60), sbtest::noteOn (static_cast<int> (0.5 * kSr), 72) }, 0.6);
        const size_t a = static_cast<size_t> (0.52 * kSr), n = 2400;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            re += o.left[a + i] * std::cos (2.0 * M_PI * 523.2511 * static_cast<double> (i) / kSr);
            im += o.left[a + i] * std::sin (2.0 * M_PI * 523.2511 * static_cast<double> (i) / kSr);
        }
        return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (n);
    };
    CHECK (level (1.0f) > 8.0 * level (0.0f)); // the new voice starts half way up the attack instead of at zero
}
