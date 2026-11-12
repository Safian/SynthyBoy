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

Event expression (double t, int key, Event::Expression ex, float value, int channel = 0)
{
    Event e = sbtest::noteOn (static_cast<int> (t * kSr), key, 0.0f, channel);
    e.type = Event::NoteExpression;
    e.expression = ex;
    e.value = value;
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

// ------------------------------------------------------------------------------------------------------------------------------------
// LFO sync types, swing, stereo, slot rise / fall / delay, amount destinations, stereo source (document 03 and 04)

namespace
{
/** LFO 1 on the pan of the sine oscillator (+25 %, document 03 L1): the left peak gives the pan control, 2 * pan = the LFO value. */
struct PanProbe
{
    sbtest::Rendered out;
    static double panOfGain (double g) { return 1.0 - 4.0 / M_PI * std::asin(std::min(g / 1.41421356, 1.0)); }
    double peakAt (const std::vector<float>& x, double t) const
    {
        const size_t a = static_cast<size_t> ((t - 0.002) * kSr), b = static_cast<size_t> ((t + 0.002) * kSr);
        float p = 0.0f;
        for (size_t i = a; i < b && i < x.size(); ++i)
            p = std::max (p, std::fabs (x[i]));
        return p;
    }
    double left (double t) const { return panOfGain (peakAt (out.left, t)); }
    double right (double t) const { return (4.0 / M_PI) * std::asin (std::min (peakAt (out.right, t) / 1.41421356, 1.0)) - 1.0; }
    /** The pan from the level ratio of the channels (independent of the amplitude envelope), both lanes alike. */
    double both (double t) const
    {
        const double r = peakAt (out.left, t) / std::max (peakAt (out.right, t), 1.0e-9);
        double lo = -1.0, hi = 1.0;
        for (int i = 0; i < 40; ++i)
        {
            const double p = 0.5 * (lo + hi);
            const double ratio = std::sin (M_PI / 4.0 * (1.0 - p)) / std::max (std::sin (M_PI / 4.0 * (1.0 + p)), 1.0e-9);
            (ratio > r ? lo : hi) = p;
        }
        return 0.5 * (lo + hi);
    }
};

std::unique_ptr<IEngine> lfoPan (int shape, float rate)
{
    auto e = make();
    sinePatch (*e);
    set (*e, "env1.release", 3000);
    set (*e, "lfo1.shape", static_cast<float> (shape));
    set (*e, "lfo1.rate", rate);
    set (*e, "lfo1.smooth", 0);
    set (*e, "mod01.source", srcChoice ("lfo1"));
    set (*e, "mod01.destination", dstChoice ("osc1.pan"));
    set (*e, "mod01.amount", 25);
    return e;
}
} // namespace

TEST (ownv_lfo_swing_warps_the_shape_position)
{
    for (float swing : { 0.0f, 100.0f })
    {
        auto e = lfoPan (2, 1.0f); // saw up, 1 Hz
        set (*e, "lfo1.swing", swing);
        PanProbe p;
        p.out = run (*e, { sbtest::noteOn (0, 69) }, 1.1);
        const double sw = swing * 0.01;
        auto expect = [&] (double u)
        {
            const double x = u * 4.0, k = std::floor (x), w = x - k, b = 0.5 + 0.25 * sw;
            const double w2 = w < b ? 0.5 * w / b : 0.5 + 0.5 * (w - b) / (1.0 - b);
            return (k + w2) * 0.25;
        };
        for (double u : { 0.125, 0.25, 0.375, 0.6 })
        {
            const double s = 2.0 * p.left (0.0 + u);
            if (!CHECK (std::fabs (s - expect (u)) < 0.03))
                std::printf ("    swing %.0f u %.3f: %.3f expected %.3f\n", swing, u, s, expect (u));
        }
    }
}

TEST (ownv_lfo_sync_types_envelope_sustain_loop_and_hold)
{
    // 2: the envelope plays once and holds the end value
    {
        auto e = lfoPan (2, 2.0f);
        set (*e, "lfo1.sync_type", 2);
        PanProbe p;
        p.out = run (*e, { sbtest::noteOn (0, 69) }, 1.6);
        CHECK (std::fabs (2.0 * p.left (0.25) - 0.5) < 0.04);
        CHECK (std::fabs (2.0 * p.left (0.9) - 1.0) < 0.04);
        CHECK (std::fabs (2.0 * p.left (1.4) - 1.0) < 0.04);
    }
    // 3: the sustain envelope waits at the phase knob while the key is down and plays on after the release
    {
        auto e = lfoPan (2, 1.0f);
        set (*e, "lfo1.sync_type", 3);
        set (*e, "lfo1.phase", 90);
        PanProbe p;
        p.out = run (*e, { sbtest::noteOn (0, 69), sbtest::noteOff (static_cast<int> (1.0 * kSr), 69) }, 1.8);
        CHECK (std::fabs (2.0 * p.left (0.6) - 0.25) < 0.04);
        CHECK (std::fabs (2.0 * p.left (0.9) - 0.25) < 0.04);
        CHECK (std::fabs (2.0 * p.both (1.5) - 0.75) < 0.05);
    }
    // 4: loop at point: after the end the position returns to the phase knob
    {
        auto e = lfoPan (2, 1.0f);
        set (*e, "lfo1.sync_type", 4);
        set (*e, "lfo1.phase", 180);
        PanProbe p;
        p.out = run (*e, { sbtest::noteOn (0, 69) }, 1.8);
        CHECK (std::fabs (2.0 * p.left (0.5) - 0.5) < 0.04);
        CHECK (std::fabs (2.0 * p.left (1.25) - 0.75) < 0.05);
    }
    // 5: loop and hold: the position loops over [0, knob) while the key is down, then plays to the end and holds
    {
        auto e = lfoPan (2, 1.0f);
        set (*e, "lfo1.sync_type", 5);
        set (*e, "lfo1.phase", 180);
        PanProbe p;
        p.out = run (*e, { sbtest::noteOn (0, 69), sbtest::noteOff (static_cast<int> (1.2 * kSr), 69) }, 2.4);
        CHECK (std::fabs (2.0 * p.left (0.75) - 0.25) < 0.05);
        CHECK (std::fabs (2.0 * p.both (2.2) - 1.0) < 0.05);
    }
}

TEST (ownv_lfo_stereo_offset_puts_the_lanes_half_a_cycle_apart)
{
    auto e = lfoPan (2, 1.0f);
    set (*e, "lfo1.stereo", 180);
    PanProbe p;
    p.out = run (*e, { sbtest::noteOn (0, 69) }, 1.1);
    // left u + 0.25, right u - 0.25 (pan of the right lane is read from its gain)
    CHECK (std::fabs (2.0 * p.left (0.5) - 0.75) < 0.05);
    CHECK (std::fabs (2.0 * p.right (0.5) - 0.25) < 0.05);
}

TEST (ownv_slot_rise_fall_and_delay)
{
    auto base = [&] (float rise, float delay)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "env1.release", 3000);
        set (*e, "mod01.source", srcChoice ("mod_wheel"));
        set (*e, "mod01.destination", dstChoice ("osc1.pan"));
        set (*e, "mod01.amount", 25);
        set (*e, "mod01.rise", rise);
        set (*e, "mod01.delay", delay);
        Event mw = sbtest::noteOn (static_cast<int> (0.3 * kSr), 0, 1.0f, 0);
        mw.type = Event::ModWheel;
        PanProbe p;
        p.out = run (*e, { sbtest::noteOn (0, 69), mw }, 1.0);
        return p;
    };
    // without a slew the control jumps to 0.5 at 0.3 s (pan control = 2 * 0.25 * x), with a rise of 200 ms it covers 90 % of the step after 200 ms
    const PanProbe plain = base (0, 0), slow = base (200, 0), late = base (0, 400);
    CHECK (std::fabs (plain.left (0.35) - 0.5) < 0.03);
    CHECK (std::fabs (slow.left (0.5) - 0.45) < 0.04);
    CHECK (std::fabs (slow.left (0.35) - 0.5) > 0.1);
    CHECK (std::fabs (late.left (0.35)) < 0.03 && std::fabs (late.left (0.35)) < std::fabs (late.left (0.6))); // the connection starts 400 ms after the note on
    CHECK (std::fabs (late.left (0.6) - 0.5) < 0.04);
}

TEST (ownv_slot_amount_destination_stereo_source_and_loops)
{
    // mod 2: macro 1 -> amount of mod 1 (width 2 = a full -100 .. 100 %): macro 100 % brings the amount from 0 to 50 % x 2
    auto pan = [&] (bool amountSlot, bool loop)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "macro.1", 100);
        set (*e, "macro.2", 100);
        set (*e, "mod01.source", srcChoice ("macro2"));
        set (*e, "mod01.destination", dstChoice ("osc1.pan"));
        set (*e, "mod01.amount", 0);
        if (amountSlot)
        {
            set (*e, "mod02.source", srcChoice ("macro1"));
            set (*e, "mod02.destination", dstChoice ("mod01.amount"));
            set (*e, "mod02.amount", 25); // +0.25 * width 2 = +0.5 -> the amount of slot 1 is 50 %
        }
        if (loop) // slot 3 would feed lfo1 from lfo1: not wired
        {
            set (*e, "mod03.source", srcChoice ("lfo1"));
            set (*e, "mod03.destination", dstChoice ("lfo1.rate"));
            set (*e, "mod03.amount", 100);
        }
        PanProbe p;
        p.out = run (*e, { sbtest::noteOn (0, 69) }, 0.5);
        return p;
    };
    CHECK (std::fabs (pan (false, false).left (0.3)) < 0.02);
    CHECK (std::fabs (pan (true, false).left (0.3) - 1.0) < 0.05); // pan control = 0.5 x 2
    // the stereo source is (1, 0): pan control +1 in the left lane, 0 in the right
    auto e = make();
    sinePatch (*e);
    set (*e, "mod01.source", srcChoice ("stereo"));
    set (*e, "mod01.destination", dstChoice ("osc1.pan"));
    set (*e, "mod01.amount", 50);
    PanProbe p;
    p.out = run (*e, { sbtest::noteOn (0, 69) }, 0.5);
    CHECK (std::fabs (p.left (0.3) - 1.0) < 0.05);
    CHECK (std::fabs (p.right (0.3)) < 0.05);
}

// ------------------------------------------------------------------------------------------------------------------------------------
// voice control

namespace
{
double lineAt (const std::vector<float>& x, double t0, double hz, size_t n = 4800)
{
    const size_t a = static_cast<size_t> (t0 * kSr);
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i)
    {
        re += x[a + i] * std::cos (2.0 * M_PI * hz * static_cast<double> (i) / kSr);
        im += x[a + i] * std::sin (2.0 * M_PI * hz * static_cast<double> (i) / kSr);
    }
    return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (n);
}
} // namespace

TEST (ownv_voice_control_steps_pan_and_detune_alternate_per_note)
{
    auto e = make();
    sinePatch (*e);
    set (*e, "voicectl.length", 2);
    set (*e, "voicectl.s1.pan", 100);
    set (*e, "voicectl.s2.pan", -100);
    set (*e, "voicectl.s2.detune", 100);
    const auto o = run (*e, { sbtest::noteOn (0, 60), sbtest::noteOn (static_cast<int> (0.1 * kSr), 72) }, 0.5);
    const double f1 = 261.6256, f2 = 523.2511 * std::pow (2.0, 1.0 / 12.0);
    CHECK (lineAt (o.left, 0.3, f1) < 0.05 && lineAt (o.right, 0.3, f1) > 1.0);   // step 1 pans right
    CHECK (lineAt (o.right, 0.3, f2) < 0.05 && lineAt (o.left, 0.3, f2) > 1.0);   // step 2 pans left and detunes by a semitone
    // the mask: a target that is off ignores the step values
    auto e2 = make();
    sinePatch (*e2);
    set (*e2, "voicectl.s1.pan", 100);
    set (*e2, "voicectl.osc.a", 0);
    const auto o2 = run (*e2, { sbtest::noteOn (0, 60) }, 0.4);
    CHECK (std::fabs (lineAt (o2.left, 0.3, f1) - lineAt (o2.right, 0.3, f1)) < 0.02);
}

TEST (ownv_voice_control_envelope_scaling_random_and_sources)
{
    // the envelope time scaling stretches the attack: at 400 % a 100 ms attack takes 400 ms
    auto rise = [&] (float scale)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "env1.attack", 100);
        set (*e, "voicectl.scale.env", scale);
        const auto o = run (*e, { sbtest::noteOn (0, 69) }, 0.6);
        return lineAt (o.left, 0.15, 440.0, 2400);
    };
    CHECK (rise (100) > 0.9);
    CHECK (rise (400) < 0.5);
    // the random pan is the same in every render of the same note counter
    auto pan = [&]
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "voicectl.rand.pan", 100);
        const auto o = run (*e, { sbtest::noteOn (0, 69) }, 0.3);
        return lineAt (o.left, 0.2, 440.0, 2400);
    };
    CHECK (pan() == pan());
    CHECK (std::fabs (pan() - 1.0) > 0.01);
    // Voice Mod 1 is a source: step value 100 % moves the level
    auto level = [&] (float mod1)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "osc1.level", 50);
        set (*e, "voicectl.s1.mod1", mod1);
        set (*e, "mod01.source", srcChoice ("voice_mod1"));
        set (*e, "mod01.destination", dstChoice ("osc1.level"));
        set (*e, "mod01.amount", 50);
        const auto o = run (*e, { sbtest::noteOn (0, 69) }, 0.4);
        return lineAt (o.left, 0.3, 440.0, 2400);
    };
    CHECK (level (100) > 1.3 * level (0));
}

// ------------------------------------------------------------------------------------------------------------------------------------
// filters: the vectors F1 .. F7 of document 05, clean mode, the shelving style, the formant tract style

namespace
{
/** Gain of noise through the own engine relative to the same patch with `refOff` keys switched off: 125 .. 16k Hz, bins +-6 of 8192 point Welch averages. */
std::vector<double> engineResponse (const oracle::Overrides& ov, const oracle::Overrides& refOv, bool rightChannel = false, int note = 69)
{
    oracle::Note n;
    n.key = note;
    n.length = 3.5;
    const auto a = oracle::render (EngineKind::own, ov, { n }, 3.6, kSr);
    const auto b = oracle::render (EngineKind::own, refOv, { n }, 3.6, kSr);
    const size_t N = 8192;
    auto avg = [&] (const std::vector<float>& x)
    {
        std::vector<double> acc (N / 2, 0.0);
        for (size_t pos = static_cast<size_t> (0.3 * kSr); pos + N <= x.size(); pos += N / 2)
        {
            const auto ps = oracle::powerSpectrum (x, pos, N);
            for (size_t i = 0; i < N / 2; ++i)
                acc[i] += ps[i];
        }
        return acc;
    };
    const auto pa = avg (rightChannel ? a.r : a.l), pb = avg (rightChannel ? b.r : b.l);
    std::vector<double> g;
    for (double f : { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
    {
        const size_t c = static_cast<size_t> (f / (kSr / static_cast<double> (N)) + 0.5);
        double x = 0, y = 0;
        for (size_t i = c - 6; i <= c + 6; ++i)
        {
            x += pa[i];
            y += pb[i];
        }
        g.push_back (10.0 * std::log10 (x / y));
    }
    return g;
}

oracle::Overrides noisePatch (std::vector<std::pair<std::string, float>> extra = {})
{
    oracle::Overrides ov = { { "osc1.on", 0 }, { "noise.on", 1 }, { "noise.level", 100 }, { "filter1.on", 1 }, { "filter1.model", 3 }, { "filter1.style", 0 }, { "filter1.cutoff", 1000 },
                             { "filter1.resonance", 0 }, { "filter1.keytrack", 0 }, { "filter1.drive", 0 }, { "filter1.blend", 0 }, { "filter1.mix", 100 }, { "filter1.level", 0 },
                             { "master.volume", -6 }, { "env1.attack", 0 }, { "env1.release", 50 } };
    for (auto& e : extra)
        ov.push_back (e);
    return ov;
}

void expectRow (const std::vector<double>& got, const double (&want)[8], double tol, const char* what)
{
    for (int i = 0; i < 8; ++i)
    {
        if (want[i] < -40.0)
        {
            CHECK (got[static_cast<size_t> (i)] < -40.0 + 4.0);
            continue;
        }
        // deep in the stopband a stronger attenuation than the reference is better (the reference's decimator and tables leave a floor)
        const bool ok = want[i] < -20.0 ? got[static_cast<size_t> (i)] <= want[i] + tol : std::fabs (got[static_cast<size_t> (i)] - want[i]) <= tol;
        if (!CHECK (ok))
            std::printf ("    %s bin %d: %.1f dB, spec %.1f dB\n", what, i, got[static_cast<size_t> (i)], want[i]);
    }
}
} // namespace

TEST (ownv_filter_vectors_f1_f4_f6_f7)
{
    const oracle::Overrides ref = noisePatch ({ { "filter1.on", 0 } });
    // F1: blend 0 / 50 / 100 % = LP / BP / HP
    expectRow (engineResponse (noisePatch (), ref), { -0.1, -0.5, -1.9, -6.0, -14.0, -24.7, -36.6, -49.9 }, 1.5, "F1 LP");
    expectRow (engineResponse (noisePatch ({ { "filter1.blend", 50 } }), ref), { -18.6, -12.4, -8.0, -6.0, -8.0, -12.6, -18.4, -25.0 }, 2.0, "F1 BP");
    expectRow (engineResponse (noisePatch ({ { "filter1.blend", 100 } }), ref), { -27.8, -23.9, -14.5, -6.5, -2.2, -0.7, -0.4, -0.2 }, 6.0, "F1 HP");
    // F2: mix 50 %: the dry signal comes through at -6 dB
    expectRow (engineResponse (noisePatch ({ { "filter1.mix", 50 } }), ref), { -0.1, -0.5, -1.9, -5.1, -7.0, -6.5, -6.1, -6.0 }, 1.5, "F2 mix");
    // F3: level -6 dB
    expectRow (engineResponse (noisePatch ({ { "filter1.level", -6 } }), ref), { -6.1, -6.5, -7.9, -12.0, -20.0, -30.7, -42.6, -55.9 }, 1.5, "F3 level");
    // F4: keytrack 100 % at note 72 doubles the corner
    expectRow (engineResponse (noisePatch ({ { "filter1.keytrack", 100 } }), ref, false, 72), { -0.0, -0.1, -0.5, -1.9, -6.0, -14.0, -25.0, -37.9 }, 1.5, "F4 keytrack");
    // F5: drive 12 dB adds about 5.6 dB in the passband; clean mode keeps it (5.9)
    expectRow (engineResponse (noisePatch ({ { "filter1.drive", 12 } }), ref), { 5.6, 5.3, 3.8, -0.2, -8.0, -18.7, -30.7, -44.2 }, 2.5, "F5 drive");
    expectRow (engineResponse (noisePatch ({ { "filter1.drive", 12 }, { "filter1.clean_mode", 1 } }), ref), { 5.9, 5.5, 4.1, -0.0, -8.0, -18.7, -30.6, -43.9 }, 2.5, "F5 clean");
    // F6: pan +100 %: the left corner 12 st down (500 Hz), the right one 12 st up (2 kHz)
    expectRow (engineResponse (noisePatch ({ { "filter1.pan", 100 } }), ref, false), { -0.5, -2.0, -6.0, -14.0, -24.6, -36.3, -48.6, -61.9 }, 1.5, "F6 pan left");
    expectRow (engineResponse (noisePatch ({ { "filter1.pan", 100 } }), ref, true), { -0.0, -0.1, -0.5, -1.9, -6.0, -14.0, -25.0, -37.9 }, 1.5, "F6 pan right");
    // F7: both filters low pass 1 kHz, the source in filter 1 only, series 1 to 2: two 12 dB stages
    oracle::Overrides f7 = noisePatch ({ { "filter2.on", 1 }, { "filter2.model", 3 }, { "filter2.cutoff", 1000 }, { "filter2.resonance", 0 }, { "filter.routing", 1 }, { "noise.destination", 0 } });
    expectRow (engineResponse (f7, ref), { -0.2, -1.1, -3.9, -12.1, -27.9, -49.4, -73.3, -99.8 }, 2.0, "F7 series");
}

TEST (ownv_shelving_style_is_the_identity_and_formant_tract_style_sounds)
{
    const oracle::Overrides ref = noisePatch ({ { "filter1.on", 0 } });
    const auto g = engineResponse (noisePatch ({ { "filter1.style", 5 } }), ref);
    for (double x : g)
        CHECK (std::fabs (x) < 0.6);
    const auto f2 = engineResponse (noisePatch ({ { "filter1.model", 5 }, { "filter1.style", 2 } }), ref);
    double sum = 0;
    for (double x : f2)
        sum += std::fabs (x);
    CHECK (sum > 20.0); // not a pass-through: four resonances
}
