// SynthyBoy - own engine tests: the keys that pages show and that the engine now reads (sub.phase_contiguous, quality.no_smoothing, curve.*.legato, fxfilter.*,
// hyperdim.retrig, bode.retrig, master.tuning.mts_note_on_only). Each key: the default (written out) renders what the engine rendered without it; a non default value
// changes the sound in the way the document says.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>
#include <atomic>
#include <memory>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/tuning.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;

struct Setting { const char* key; float v; };

std::unique_ptr<IEngine> make (const std::vector<Setting>& s)
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 16.0f);
    e->prepare (kSr, 256, qualityForTier (Tier::high));
    for (const Setting& x : s)
    {
        const ParamInfo* p = findParamByKey (x.key);
        CHECK (p != nullptr);
        if (p != nullptr)
            e->setParam (p->id, x.v);
    }
    return e;
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

/** A plain patch: one oscillator (wavetable 1, the saw, or 0, the sine), no voice filter, a short attack and release. */
std::vector<Setting> base (float wavetable)
{
    return { { "osc1.wavetable", wavetable }, { "osc1.level", 100 }, { "filter1.on", 0 }, { "master.volume", -6 }, { "env1.attack", 1 }, { "env1.release", 5 } };
}

std::vector<Setting> plus (std::vector<Setting> a, const std::vector<Setting>& b)
{
    a.insert (a.end(), b.begin(), b.end());
    return a;
}

sbtest::Rendered play (const std::vector<Setting>& s, const std::vector<Event>& ev, double seconds)
{
    auto e = make (s);
    return sbtest::render (*e, ev, static_cast<int> (seconds * kSr), 256);
}

bool identical (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    return a.left == b.left && a.right == b.right;
}

double rms (const std::vector<float>& x, double fromSec, double toSec)
{
    const size_t a = static_cast<size_t> (fromSec * kSr), b = std::min (x.size(), static_cast<size_t> (toSec * kSr));
    double acc = 0.0;
    for (size_t i = a; i < b; ++i)
        acc += static_cast<double> (x[i]) * x[i];
    return std::sqrt (acc / std::max<size_t> (b - a, 1));
}

/** The energy of the first difference over the energy: how bright the signal is. */
double brightness (const std::vector<float>& x, double fromSec, double toSec)
{
    const size_t a = static_cast<size_t> (fromSec * kSr), b = std::min (x.size(), static_cast<size_t> (toSec * kSr));
    double d = 0.0, e = 1.0e-12;
    for (size_t i = a + 1; i < b; ++i)
    {
        const double dx = static_cast<double> (x[i]) - x[i - 1];
        d += dx * dx;
        e += static_cast<double> (x[i]) * x[i];
    }
    return d / e;
}

double maxDiff (const std::vector<float>& a, const std::vector<float>& b, double fromSec)
{
    double m = 0.0;
    for (size_t i = static_cast<size_t> (fromSec * kSr); i < a.size() && i < b.size(); ++i)
        m = std::max (m, static_cast<double> (std::fabs (a[i] - b[i])));
    return m;
}

double peakOf (const std::vector<float>& x, double fromSec)
{
    double m = 1.0e-9;
    for (size_t i = static_cast<size_t> (fromSec * kSr); i < x.size(); ++i)
        m = std::max (m, static_cast<double> (std::fabs (x[i])));
    return m;
}

double line (const std::vector<float>& x, double fromSec, double toSec, double hz)
{
    const size_t a = static_cast<size_t> (fromSec * kSr), b = std::min (x.size(), static_cast<size_t> (toSec * kSr));
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * static_cast<double> (i - a) / static_cast<double> (b - a));
        const double ph = 2.0 * 3.14159265358979323846 * hz * static_cast<double> (i) / kSr;
        re += w * x[i] * std::cos (ph);
        im -= w * x[i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

const std::vector<Setting> kFxFilter = { { "fxfilter.on", 1 }, { "fxfilter.model", 3 }, { "fxfilter.cutoff", 500 }, { "fxfilter.resonance", 10 } };
} // namespace

TEST (own_fxfilter_keys_default_is_unchanged)
{
    const std::vector<Event> ev = { sbtest::noteOn (0, 69) };
    const auto plain = play (plus (base (1), kFxFilter), ev, 0.5);
    const auto written = play (plus (plus (base (1), kFxFilter), { { "fxfilter.keytrack", 0 }, { "fxfilter.pan", 0 }, { "fxfilter.clean_mode", 0 }, { "fxfilter.comb_offset", 42 },
                                                                  { "fxfilter.formant.x", 50 }, { "fxfilter.formant.y", 50 }, { "fxfilter.formant.resonance", 85 },
                                                                  { "fxfilter.formant.spread", 0 }, { "fxfilter.formant.transpose", 0 } }), ev, 0.5);
    CHECK (identical (plain, written));
}

TEST (own_fxfilter_keytrack_follows_the_last_note)
{
    const std::vector<Event> ev = { sbtest::noteOn (0, 84) }; // two octaves above C4: cutoff x4 at 100 %
    const auto off = play (plus (base (1), kFxFilter), ev, 0.5);
    const auto on = play (plus (plus (base (1), kFxFilter), { { "fxfilter.keytrack", 100 } }), ev, 0.5);
    const double b0 = brightness (off.left, 0.2, 0.45), b1 = brightness (on.left, 0.2, 0.45);
    std::printf ("    fxfilter keytrack: brightness %.5f -> %.5f\n", b0, b1);
    CHECK (b1 > 1.3 * b0);
    // note 60 is the reference: no change
    const std::vector<Event> ref = { sbtest::noteOn (0, 60) };
    CHECK (identical (play (plus (base (1), kFxFilter), ref, 0.3), play (plus (plus (base (1), kFxFilter), { { "fxfilter.keytrack", 100 } }), ref, 0.3)));
}

TEST (own_fxfilter_pan_moves_the_two_cutoffs_apart)
{
    const std::vector<Event> ev = { sbtest::noteOn (0, 60) };
    const auto flat = play (plus (base (1), kFxFilter), ev, 0.5);
    const auto panned = play (plus (plus (base (1), kFxFilter), { { "fxfilter.pan", 100 } }), ev, 0.5);
    const double l = brightness (panned.left, 0.2, 0.45), r = brightness (panned.right, 0.2, 0.45), f = brightness (flat.left, 0.2, 0.45);
    std::printf ("    fxfilter pan 100: left %.5f, right %.5f, centre %.5f\n", l, r, f);
    CHECK (r > 1.5 * l); // left 12 semitones down, right 12 up
    CHECK (std::fabs (brightness (flat.left, 0.2, 0.45) - brightness (flat.right, 0.2, 0.45)) < 1.0e-6 * f + 1.0e-9);
}

TEST (own_fxfilter_clean_mode_and_model_settings_change_the_sound)
{
    const std::vector<Event> ev = { sbtest::noteOn (0, 57) };
    // clean mode: the model's input is 24 dB lower and the output 24 dB higher: a driven filter saturates less
    const std::vector<Setting> driven = { { "fxfilter.on", 1 }, { "fxfilter.model", 0 }, { "fxfilter.cutoff", 2000 }, { "fxfilter.resonance", 60 }, { "fxfilter.drive", 20 } };
    const auto a = play (plus (base (1), driven), ev, 0.5);
    const auto b = play (plus (plus (base (1), driven), { { "fxfilter.clean_mode", 1 } }), ev, 0.5);
    CHECK (maxDiff (a.left, b.left, 0.2) > 0.01 * peakOf (a.left, 0.2));
    // comb offset (Comb model), formant X (Formant model)
    const std::vector<Setting> comb = { { "fxfilter.on", 1 }, { "fxfilter.model", 6 }, { "fxfilter.cutoff", 220 }, { "fxfilter.resonance", 80 } };
    const auto c0 = play (plus (plus (base (1), comb), { { "fxfilter.comb_offset", 0 } }), ev, 0.5);
    const auto c1 = play (plus (plus (base (1), comb), { { "fxfilter.comb_offset", 84 } }), ev, 0.5);
    CHECK (maxDiff (c0.left, c1.left, 0.2) > 0.01 * peakOf (c0.left, 0.2));
    const std::vector<Setting> fmt = { { "fxfilter.on", 1 }, { "fxfilter.model", 5 } };
    const auto f0 = play (plus (plus (base (1), fmt), { { "fxfilter.formant.x", 0 } }), ev, 0.5);
    const auto f1 = play (plus (plus (base (1), fmt), { { "fxfilter.formant.x", 100 } }), ev, 0.5);
    CHECK (maxDiff (f0.left, f1.left, 0.2) > 0.01 * peakOf (f0.left, 0.2));
    const auto f2 = play (plus (plus (base (1), fmt), { { "fxfilter.formant.transpose", 7 } }), ev, 0.5);
    CHECK (maxDiff (f0.left, f2.left, 0.2) > 0.01 * peakOf (f0.left, 0.2) || maxDiff (f1.left, f2.left, 0.2) > 0.01 * peakOf (f1.left, 0.2));
}

TEST (own_hyper_and_bode_retrig_restart_on_a_note_on)
{
    const std::vector<Event> two = { sbtest::noteOn (0, 57), sbtest::noteOn (static_cast<int> (0.37 * kSr), 64) };
    const std::vector<Setting> hyper = { { "hyperdim.on", 1 }, { "hyperdim.mix", 100 }, { "hyperdim.unison", 4 }, { "hyperdim.rate", 1.0f }, { "hyperdim.dim_mix", 0 } };
    const auto h0 = play (plus (base (0), hyper), two, 1.0);
    const auto h0b = play (plus (plus (base (0), hyper), { { "hyperdim.retrig", 0 } }), two, 1.0);
    CHECK (identical (h0, h0b)); // the default
    const auto h1 = play (plus (plus (base (0), hyper), { { "hyperdim.retrig", 1 } }), two, 1.0);
    CHECK (maxDiff (h0.left, h1.left, 0.40) > 0.005 * peakOf (h0.left, 0.40)); // the second note restarts the voices' LFOs
    const std::vector<Setting> bode = { { "bode.on", 1 }, { "bode.mix", 100 }, { "bode.shift", 100 }, { "bode.range", 300 } };
    const auto b0 = play (plus (base (0), bode), two, 1.0);
    const auto b0b = play (plus (plus (base (0), bode), { { "bode.retrig", 0 } }), two, 1.0);
    CHECK (identical (b0, b0b));
    const auto b1 = play (plus (plus (base (0), bode), { { "bode.retrig", 1 } }), two, 1.0);
    CHECK (maxDiff (b0.left, b1.left, 0.40) > 0.005 * peakOf (b0.left, 0.40));
}

TEST (own_sub_phase_contiguous_continues_the_phase)
{
    const std::vector<Setting> sub = { { "osc1.on", 0 }, { "sub.on", 1 }, { "sub.octave", 0 }, { "sub.level", 100 }, { "sub.waveform", 0 }, { "env1.attack", 1 }, { "master.volume", -6 } };
    const std::vector<Event> two = { sbtest::noteOn (0, 57), sbtest::noteOn (static_cast<int> (0.1234 * kSr), 69) };
    const auto off = play (sub, two, 0.4);
    const auto off2 = play (plus (sub, { { "sub.phase_contiguous", 0 } }), two, 0.4);
    CHECK (identical (off, off2));
    const auto on = play (plus (sub, { { "sub.phase_contiguous", 1 } }), two, 0.4);
    // before the second note nothing differs, after it the second voice has another start phase
    CHECK (maxDiff (off.left, on.left, 0.0) > 0.01);
    for (size_t i = 0; i < static_cast<size_t> (0.1234 * kSr); ++i)
        CHECK (off.left[i] == on.left[i]);
}

TEST (own_quality_no_smoothing_jumps_the_master_volume)
{
    auto firstBlockLevel = [] (bool noSmoothing)
    {
        auto e = make (plus (base (0), { { "master.volume", 0 }, { "quality.no_smoothing", noSmoothing ? 1.0f : 0.0f } }));
        std::vector<float> l (256), r (256);
        Event ev[1] = { sbtest::noteOn (0, 69) };
        e->process (ev, 1, l.data(), r.data(), 256);
        for (int k = 0; k < 8; ++k)
            e->process (nullptr, 0, l.data(), r.data(), 256);
        const ParamInfo* p = findParamByKey ("master.volume");
        e->setParam (p->id, -30.0f);
        e->process (nullptr, 0, l.data(), r.data(), 256);
        double acc = 0.0;
        for (int i = 0; i < 32; ++i)
            acc += static_cast<double> (l[static_cast<size_t> (i)]) * l[static_cast<size_t> (i)];
        return std::sqrt (acc / 32.0);
    };
    const double smooth = firstBlockLevel (false), jump = firstBlockLevel (true);
    std::printf ("    master volume 0 -> -30 dB, first 32 samples: smoothing %.4f, no smoothing %.4f\n", smooth, jump);
    CHECK (jump < 0.2 * smooth); // with smoothing the gain is still at the old level (a 20 ms ramp), without it the new level is there at once
}

TEST (own_curve_legato_glides_the_curved_velocity)
{
    // velocity -> master volume through an inverted velocity curve; a legato second note moves the curved value: at once, or with master.portamento.time
    auto run = [] (bool glide)
    {
        auto e = make (plus (base (0), { { "master.legato", 1 }, { "master.portamento.time", 300 }, { "master.velocity_track", 0 }, { "curve.velocity.legato", glide ? 1.0f : 0.0f },
                                         { "mod01.source", srcChoice ("velocity") }, { "mod01.destination", dstChoice ("master.volume") }, { "mod01.amount", -20 } }));
        StateContent content;
        content.velocityCurve = { ContentPoint (0.0f, 1.0f), ContentPoint (1.0f, 0.0f) };
        e->setContent (content);
        const auto r = sbtest::render (*e, { sbtest::noteOn (0, 60, 1.0f), sbtest::noteOn (static_cast<int> (0.3 * kSr), 67, 0.25f) }, static_cast<int> (1.6 * kSr), 256);
        return r.left;
    };
    const auto jump = run (false), glide = run (true);
    // after the second note (0.3 s): the level of the first 30 ms against the level a second later
    const double jumpRatio = rms (jump, 0.31, 0.34) / rms (jump, 1.4, 1.5), glideRatio = rms (glide, 0.31, 0.34) / rms (glide, 1.4, 1.5);
    std::printf ("    curve legato: early / late level without glide %.3f, with glide %.3f\n", jumpRatio, glideRatio);
    CHECK (std::fabs (std::log (jumpRatio)) < 0.12);
    CHECK (std::fabs (std::log (glideRatio)) > 0.15);
}

TEST (own_mts_note_on_only_keeps_the_started_tuning)
{
    struct Master final : tuning::ITuningSource
    {
        mutable std::atomic<float> cents { 0.0f };
        bool isConnected() const noexcept override { return true; }
        double frequencyForKey (int key) const noexcept override { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0 + static_cast<double> (cents.load()) / 1200.0); }
        bool query128Keys (std::array<double, tuning::kNumKeys>& out) const noexcept override
        {
            for (int k = 0; k < tuning::kNumKeys; ++k)
                out[static_cast<size_t> (k)] = frequencyForKey (k);
            return true;
        }
    };
    auto run = [] (bool noteOnOnly)
    {
        Master master;
        auto e = make (plus (base (0), { { "master.tuning.mts_esp", 1 }, { "master.tuning.mts_note_on_only", noteOnOnly ? 1.0f : 0.0f } }));
        e->setTuningSource (&master);
        std::vector<float> all;
        std::vector<float> l (256), r (256);
        Event ev[1] = { sbtest::noteOn (0, 69) };
        e->process (ev, 1, l.data(), r.data(), 256);
        all.insert (all.end(), l.begin(), l.end());
        for (int k = 0; k < 60; ++k)
        {
            if (k == 20)
                master.cents = 100.0f; // the master retunes by a semitone while the note sounds
            e->process (nullptr, 0, l.data(), r.data(), 256);
            all.insert (all.end(), l.begin(), l.end());
        }
        e->setTuningSource (nullptr);
        return all;
    };
    const auto live = run (false), held = run (true);
    const double f0 = 440.0, f1 = 440.0 * std::pow (2.0, 1.0 / 12.0);
    // the last 0.15 s: the live note sounds at 466 Hz, the note-on-only one stays at 440 Hz
    CHECK (line (live, 0.25, 0.31, f1) > 3.0 * line (live, 0.25, 0.31, f0));
    CHECK (line (held, 0.25, 0.31, f0) > 3.0 * line (held, 0.25, 0.31, f1));
}
