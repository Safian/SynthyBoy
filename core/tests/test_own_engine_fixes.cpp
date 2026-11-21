// SynthyBoy - own engine tests: Direct out and the master volume, the Sub MONO switch, the keys the engine claims, and the voice source of a global (effect) modulation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
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

/** A pure sine patch: the first oscillator on a sine, no filter, a short attack and release. */
void sinePatch (IEngine& e)
{
    set (e, "osc1.wavetable", 0);
    set (e, "osc1.level", 100);
    set (e, "filter1.on", 0);
    set (e, "master.volume", 0);
    set (e, "env1.attack", 1);
    set (e, "env1.release", 5);
}

double rms (const std::vector<float>& x, double fromSec, double toSec)
{
    const size_t a = static_cast<size_t> (fromSec * kSr), b = std::min (x.size(), static_cast<size_t> (toSec * kSr));
    double acc = 0.0;
    for (size_t i = a; i < b; ++i)
        acc += static_cast<double> (x[i]) * x[i];
    return std::sqrt (acc / std::max<size_t> (b - a, 1));
}

/** Amplitude of the spectral line at hz (Goertzel over [fromSec, toSec), Hann window). */
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

double db (double a, double b) { return 20.0 * std::log10 ((a + 1.0e-12) / (b + 1.0e-12)); }
} // namespace

// document 06 section 10 / 13-new-keys A16: Direct out skips the racks, but the master volume still applies to it
TEST (own_direct_follows_master_volume)
{
    auto level = [] (float masterDb)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "osc1.destination", 4); // Direct out
        set (*e, "master.volume", masterDb);
        const auto r = sbtest::render (*e, { sbtest::noteOn (0, 69) }, static_cast<int> (0.8 * kSr), 256);
        return rms (r.left, 0.5, 0.75);
    };
    const double l0 = level (0.0f), l12 = level (-12.0f), l30 = level (-30.0f);
    std::printf ("    direct out: master 0 dB rms %.4f, -12 dB %.4f (%.2f dB), -30 dB %.4f (%.2f dB)\n", l0, l12, db (l12, l0), l30, db (l30, l0));
    CHECK (l0 > 0.01);
    CHECK_NEAR (db (l12, l0), -12.0, 0.5);
    CHECK_NEAR (db (l30, l0), -30.0, 0.8);
    // direct.level still scales it on its own
    auto e = make();
    sinePatch (*e);
    set (*e, "osc1.destination", 4);
    set (*e, "direct.level", -6);
    const auto r = sbtest::render (*e, { sbtest::noteOn (0, 69) }, static_cast<int> (0.8 * kSr), 256);
    CHECK_NEAR (db (rms (r.left, 0.5, 0.75), l0), -6.0, 0.5);
}

// the Sub MONO switch: only the voice with the lowest note carries the sub
TEST (own_sub_mono_keeps_only_the_lowest_note)
{
    auto run = [] (bool mono, const std::vector<Event>& events)
    {
        auto e = make();
        set (*e, "osc1.on", 0);
        set (*e, "filter1.on", 0);
        set (*e, "master.volume", 0);
        set (*e, "env1.attack", 1);
        set (*e, "env1.release", 5);
        set (*e, "sub.on", 1);
        set (*e, "sub.waveform", 0);
        set (*e, "sub.octave", 0);
        set (*e, "sub.level", 30); // two subs at 100 % reach 2.8 and the output stage's soft limit (linear to 1.8) would bend the lines; at 30 % both stay linear
        set (*e, "sub.mono", mono ? 1.0f : 0.0f);
        return sbtest::render (*e, events, static_cast<int> (0.7 * kSr), 256);
    };
    const std::vector<Event> chord = { sbtest::noteOn (0, 57), sbtest::noteOn (0, 69) }; // 220 Hz and 440 Hz
    const auto off = run (false, chord), on = run (true, chord);
    const double offLow = line (off.left, 0.2, 0.6, 220.0), offHigh = line (off.left, 0.2, 0.6, 440.0);
    const double onLow = line (on.left, 0.2, 0.6, 220.0), onHigh = line (on.left, 0.2, 0.6, 440.0);
    std::printf ("    sub.mono off: 220 Hz %.3f, 440 Hz %.3f | on: 220 Hz %.3f, 440 Hz %.3f\n", offLow, offHigh, onLow, onHigh);
    CHECK (offLow > 0.05 && offHigh > 0.05); // both notes carry the sub
    CHECK (onHigh < 0.02 * onLow);           // the higher note is gated away (ramped, so the line is far below, not exactly zero)
    CHECK_NEAR (onLow, offLow, 0.05 * offLow);

    // the note that is the lowest again takes the sub back: the lower note ends, the upper one carries it
    const std::vector<Event> handover = { sbtest::noteOn (0, 57), sbtest::noteOn (0, 69), sbtest::noteOff (static_cast<int> (0.3 * kSr), 57) };
    const auto h = run (true, handover);
    CHECK (line (h.left, 0.5, 0.65, 440.0) > 0.05);
    CHECK (line (h.left, 0.5, 0.65, 220.0) < 0.02);
    CHECK (sbtest::allFinite (h.left));

    // one note, or the lowest of several: mono On is the same samples as Off (the gate is 1.0)
    const std::vector<Event> single = { sbtest::noteOn (0, 60) };
    const auto a = run (false, single), b = run (true, single);
    bool same = a.left.size() == b.left.size();
    for (size_t i = 0; same && i < a.left.size(); ++i)
        same = a.left[i] == b.left[i] && a.right[i] == b.right[i];
    CHECK (same);
}

// supportsParam follows what the engine reads
TEST (own_claims_the_keys_the_voice_reads)
{
    auto e = make();
    for (const char* key : { "osc1.unison.stack", "osc2.unison.stack_repeat", "osc3.unison.blend_shape", "osc1.map.key_fold", "osc2.map.key_warp", "osc1.map.key_low", "osc3.transpose_snap",
                             "osc1.phase_memory", "osc2.phase_legato", "osc3.fixed_pitch", "voice.phase_legato", "sub.mono", "sub.phase", "quality.oversample_4x", "osc1.pitch_mode",
                             "osc1.unison.law" })
    {
        const ParamInfo* p = findParamByKey (key);
        CHECK (p != nullptr);
        if (p != nullptr && !e->supportsParam (p->id))
            std::printf ("    not supported: %s\n", key);
        CHECK (p != nullptr && e->supportsParam (p->id));
    }
    // every key of the table that the own engine reads is claimed (test_own_keys_claimed.cpp measures what each of them does)
    for (const char* key : { "sub.phase_contiguous", "quality.no_smoothing", "curve.note.legato", "curve.velocity.legato", "fxfilter.keytrack", "fxfilter.pan", "fxfilter.clean_mode",
                             "fxfilter.comb_offset", "fxfilter.formant.x", "hyperdim.retrig", "bode.retrig", "master.tuning.mts_note_on_only" })
    {
        const ParamInfo* p = findParamByKey (key);
        CHECK (p != nullptr);
        CHECK (p != nullptr && e->supportsParam (p->id));
    }
}

// document 04, 3.1: a voice source on an effect parameter follows the newest note; with no voice the connection keeps its last value
TEST (own_global_voice_source_follows_the_newest_note)
{
    auto run = [] (float velA, float velB)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "env1.release", 300);
        set (*e, "master.velocity_track", 0);
        set (*e, "mod01.source", srcChoice ("velocity"));
        set (*e, "mod01.destination", dstChoice ("master.volume"));
        set (*e, "mod01.amount", -20);
        const auto r = sbtest::render (*e, { sbtest::noteOn (0, 60, velA), sbtest::noteOn (static_cast<int> (0.3 * kSr), 67, velB) }, static_cast<int> (1.0 * kSr), 256);
        return rms (r.left, 0.7, 0.95);
    };
    const double xA1 = run (1.0f, 0.2f), xA2 = run (0.5f, 0.2f), y = run (1.0f, 1.0f);
    std::printf ("    newest note: (1.0, 0.2) %.4f, (0.5, 0.2) %.4f, (1.0, 1.0) %.4f\n", xA1, xA2, y);
    CHECK (std::fabs (db (xA1, xA2)) < 0.3); // the first note's velocity does not matter once the second one is the newest
    CHECK (std::fabs (db (xA1, y)) > 1.0);   // the second note's velocity does

    // no voice left: the last value stays (an envelope that moves the master volume keeps its last value while the delay tail rings)
    auto tail = [] (float amount)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "env1.release", 5);
        set (*e, "env2.attack", 0);
        set (*e, "env2.sustain", 100);
        set (*e, "env2.release", 10000);
        set (*e, "delay.on", 1);
        set (*e, "delay.sync", 0);
        set (*e, "delay.time", 100);
        set (*e, "delay.feedback", 85);
        set (*e, "delay.mix", 100);
        set (*e, "mod01.source", srcChoice ("env2"));
        set (*e, "mod01.destination", dstChoice ("master.volume"));
        set (*e, "mod01.amount", amount);
        const auto r = sbtest::render (*e, { sbtest::noteOn (0, 69), sbtest::noteOff (static_cast<int> (0.2 * kSr), 69) }, static_cast<int> (1.4 * kSr), 256);
        return rms (r.left, 0.7, 1.3);
    };
    const double plain = tail (0.0f), held = tail (-20.0f);
    std::printf ("    held envelope value over the tail: without the route %.4f, with it %.4f (%.2f dB)\n", plain, held, db (held, plain));
    CHECK (plain > 1.0e-3);
    CHECK (std::fabs (db (held, plain)) > 1.0); // before the fix the route fell back to zero as soon as the last voice ended
}

// named existing test: own_direct_follows_master_volume (line 90)
// document 06 section 11: splitter bands delay-align unequal band latencies before merge
TEST (own_splitter_bands_compensate_latency_before_merge)
{
    // 1. Splitter M/S (splitter_ms.on = 1, Limiter in Mid rack compressor.rack = 8, Side rack 9 empty)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "osc1.pan", -100);
        set (*e, "env1.attack", 0);
        set (*e, "splitter_ms.on", 1);
        set (*e, "compressor.on", 1);
        set (*e, "compressor.limit", 1);
        set (*e, "compressor.bands", 3);
        set (*e, "compressor.mid.upper_threshold", 12);
        set (*e, "compressor.mix", 100);
        set (*e, "compressor.rack", 8);

        const auto r = sbtest::render (*e, { sbtest::noteOn (0, 64) }, static_cast<int> (0.4 * kSr), 256);
        const double rmsL = rms (r.left, 0.15, 0.35);
        const double rmsR = rms (r.right, 0.15, 0.35);
        std::printf ("    splitter_ms: rmsL=%.4f, rmsR=%.4f (ratio=%.4f)\n", rmsL, rmsR, rmsR / std::max (rmsL, 1e-9));
        CHECK (rmsL > 0.4);
        CHECK (rmsR < 0.01 * rmsL);
    }

    // 2. Splitter L/H (splitter_lh.on = 1, splitter_lh.freq = 1000, Limiter in Low rack compressor.rack = 3, High rack 4 empty)
    {
        auto setupLh = [] (bool withLimiter)
        {
            auto e = make();
            sinePatch (*e);
            set (*e, "env1.attack", 0);
            set (*e, "osc1.tune", 21.31f);
            set (*e, "splitter_lh.on", 1);
            set (*e, "splitter_lh.freq", 1000);
            if (withLimiter)
            {
                set (*e, "compressor.on", 1);
                set (*e, "compressor.limit", 1);
                set (*e, "compressor.bands", 3);
                set (*e, "compressor.mid.upper_threshold", 12);
                set (*e, "compressor.mix", 100);
                set (*e, "compressor.rack", 3);
            }
            return e;
        };
        const auto ref = sbtest::render (*setupLh (false), { sbtest::noteOn (0, 83) }, static_cast<int> (0.4 * kSr), 256);
        const double refRms = rms (ref.left, 0.15, 0.35);
        const auto rLh = sbtest::render (*setupLh (true), { sbtest::noteOn (0, 83) }, static_cast<int> (0.4 * kSr), 256);
        const double rmsLh = rms (rLh.left, 0.15, 0.35);
        std::printf ("    splitter_lh: refRms=%.4f, rmsLh=%.4f (ratio=%.4f)\n", refRms, rmsLh, rmsLh / std::max (refRms, 1e-9));
        CHECK (refRms > 0.4);
        CHECK (rmsLh > 0.90 * refRms);
    }

    // 3. Splitter L/M/H (splitter_lmh.on = 1, splitter_lmh.freq_low = 300, splitter_lmh.freq_high = 1000, Limiter in High rack compressor.rack = 7, Low/Mid racks 5/6 empty)
    {
        auto setupLmh = [] (bool withLimiter)
        {
            auto e = make();
            sinePatch (*e);
            set (*e, "env1.attack", 0);
            set (*e, "osc1.tune", 21.31f);
            set (*e, "splitter_lmh.on", 1);
            set (*e, "splitter_lmh.freq_low", 300);
            set (*e, "splitter_lmh.freq_high", 1000);
            if (withLimiter)
            {
                set (*e, "compressor.on", 1);
                set (*e, "compressor.limit", 1);
                set (*e, "compressor.bands", 3);
                set (*e, "compressor.mid.upper_threshold", 12);
                set (*e, "compressor.mix", 100);
                set (*e, "compressor.rack", 7);
            }
            return e;
        };
        const auto refLmh = sbtest::render (*setupLmh (false), { sbtest::noteOn (0, 83) }, static_cast<int> (0.4 * kSr), 256);
        const double refRmsLmh = rms (refLmh.left, 0.15, 0.35);
        const auto rLmh = sbtest::render (*setupLmh (true), { sbtest::noteOn (0, 83) }, static_cast<int> (0.4 * kSr), 256);
        const double rmsLmh = rms (rLmh.left, 0.15, 0.35);
        std::printf ("    splitter_lmh: refRms=%.4f, rmsLmh=%.4f (ratio=%.4f)\n", refRmsLmh, rmsLmh, rmsLmh / std::max (refRmsLmh, 1e-9));
        CHECK (refRmsLmh > 0.4);
        CHECK (rmsLmh > 0.90 * refRmsLmh);
    }

    // 4. Equal/zero-latency bit-identity checksum: render Splitter L/H with zero-latency effects
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "env1.attack", 0);
        set (*e, "splitter_lh.on", 1);
        set (*e, "splitter_lh.freq", 800);
        set (*e, "eq.on", 0);
        set (*e, "compressor.on", 0);
        const auto r = sbtest::render (*e, { sbtest::noteOn (0, 69) }, static_cast<int> (0.2 * kSr), 256);
        CHECK (sbtest::allFinite (r.left));
        CHECK (r.left.size() >= 8);
        double sumAbs = 0.0;
        for (float s : r.left)
            sumAbs += std::fabs (static_cast<double> (s));
        CHECK (sumAbs > 0.0);
        const size_t sz = r.left.size();
        std::printf ("SPLITTERLAT-CHECKSUM zero_lat sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     sumAbs,
                     static_cast<double> (r.left[sz - 8]),
                     static_cast<double> (r.left[sz - 7]),
                     static_cast<double> (r.left[sz - 6]),
                     static_cast<double> (r.left[sz - 5]),
                     static_cast<double> (r.left[sz - 4]),
                     static_cast<double> (r.left[sz - 3]),
                     static_cast<double> (r.left[sz - 2]),
                     static_cast<double> (r.left[sz - 1]));
    }
}


// docs/engine-spec/01:176, 04:191: master.tune is a modulation destination. A slot (macro 1 at 100 %, amount +-50 % of the -100 .. +100 cent range = +-100 cents) moves the pitch of the voice;
// before the fix the voice read the knob (g.base) and the slot did nothing. 440 Hz note: +100 cents = 466.164 Hz, -100 cents = 415.305 Hz. Goertzel lines over 0.3 .. 0.8 s, 5 Hz apart from the
// unmodulated line (a 0.5 s Hann window resolves ~3 Hz), the wanted line must be 10x the line of the unmodulated pitch.
TEST (own_master_tune_is_a_modulation_destination)
{
    auto lines = [] (float amount, double hzA, double hzB)
    {
        auto e = make();
        sinePatch (*e);
        set (*e, "macro.1", 100);
        set (*e, "mod01.source", srcChoice ("macro1"));
        set (*e, "mod01.destination", dstChoice ("master.tune"));
        set (*e, "mod01.amount", amount);
        const auto r = sbtest::render (*e, { sbtest::noteOn (0, 69) }, static_cast<int> (1.0 * kSr), 256);
        return std::pair<double, double> { line (r.left, 0.3, 0.8, hzA), line (r.left, 0.3, 0.8, hzB) };
    };
    CHECK (dstChoice ("master.tune") > 0.0f);
    const double up = 440.0 * std::pow (2.0, 1.0 / 12.0), down = 440.0 * std::pow (2.0, -1.0 / 12.0);
    const auto u = lines (50.0f, up, 440.0);
    CHECK (u.first > 10.0 * u.second);
    const auto d = lines (-50.0f, down, 440.0);
    CHECK (d.first > 10.0 * d.second);
    const auto z = lines (0.0f, 440.0, up);
    CHECK (z.first > 10.0 * z.second);
}
