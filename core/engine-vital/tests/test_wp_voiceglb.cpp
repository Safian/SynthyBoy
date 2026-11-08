// SynthyBoy - engine-vital tests: block B15 voice-global (G12 same note / voice steal, G13 poly legato, G14 bend down, G15 stereo
// spread, G16 LFO swing, G18 4x oversampling and no smoothing).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Proves kCapVoiceSteal, kCapPolyLegato, kCapBendDownRange, kCapStereoSpread, kCapLfoSwing and kCapQualityOptions: every key of the six
// bits is rendered off and on and must make a measured difference (a pitch, which note survives, a level at a given instant, a
// stereo position, the shape of a modulation, the aliasing, the instant a parameter jump lands), and the default of every key must
// leave the version 2 sound alone (the explicit default renders sample identical to an engine that never saw the key).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/quality.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_bindings.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;
constexpr size_t kFft = 16384;

/** A settings list that renders on a fresh engine every time (two fresh engines with the same settings render sample identical). */
class Synth
{
public:
    Synth()
    {
        // one sine oscillator straight to the effects: the key under test is the only thing that shapes the sound
        set ("osc1.wavetable", sbtest::choiceValue ("osc1.wavetable", "Sine"));
        set ("osc1.level", 100.0f);
        set ("osc1.unison.voices", 1.0f);
        set ("osc1.random_phase", 0.0f);
        set ("osc1.destination", 3.0f);
        set ("filter1.on", 0.0f);
        set ("filter2.on", 0.0f);
        set ("master.volume", 0.0f);
    }

    void set (const char* key, float value) { settings.emplace_back (key, value); }

    std::unique_ptr<IEngine> build (const Quality& quality = Quality {}) const
    {
        auto e = createEngine (EngineKind::vital);
        e->setParam (kParamMasterPolyphony, 16.0f);
        e->prepare (kSr, 256, quality);
        for (const auto& s : settings)
            setByKey (*e, s.first.c_str(), s.second);
        if (e->needsPrepare()) // what a shell does when quality.oversample_4x changed
            e->prepare (kSr, 256, quality);
        return e;
    }

    sbtest::Rendered play (const std::vector<Event>& events, double seconds, const Quality& quality = Quality {}) const
    {
        auto e = build (quality);
        return sbtest::render (*e, events, static_cast<int> (seconds * kSr), 256);
    }

private:
    std::vector<std::pair<std::string, float>> settings;
};

std::size_t at (double seconds) { return static_cast<std::size_t> (seconds * kSr); }

double rms (const std::vector<float>& x, double t0, double t1) { return sbtest::rmsOf (x, at (t0), at (t1)); }

bool identical (const sbtest::Rendered& a, const sbtest::Rendered& b) { return a.left == b.left && a.right == b.right; }

double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0, double t1)
{
    double d = 0.0, n = 0.0;
    for (std::size_t i = at (t0); i < at (t1) && i < a.left.size() && i < b.left.size(); ++i)
    {
        const double x = static_cast<double> (a.left[i]) - b.left[i];
        d += x * x;
        n += static_cast<double> (a.left[i]) * a.left[i];
    }
    return n > 0.0 ? std::sqrt (d / n) : (d > 0.0 ? 1.0e9 : 0.0);
}

double noteHz (int key) { return 440.0 * std::pow (2.0, (key - 69) / 12.0); }

/** Power within +-3 bins of a frequency, measured over a window starting at `from` seconds. */
double powerAt (const std::vector<float>& x, double from, double hz)
{
    const auto p = sbtest::powerSpectrum (x, at (from), kFft, kFft, sbtest::Window::blackmanHarris);
    const long centre = std::lround (hz * static_cast<double> (kFft) / kSr);
    double sum = 0.0;
    for (long k = std::max (1L, centre - 3); k <= centre + 3 && k < static_cast<long> (p.size()); ++k)
        sum += p[static_cast<std::size_t> (k)];
    return sum;
}

double peakHz (const std::vector<float>& x, double from, double guess)
{
    const auto p = sbtest::powerSpectrum (x, at (from), kFft, kFft, sbtest::Window::blackmanHarris);
    return sbtest::peakFrequency (p, kFft, kSr, guess);
}

Event pitchBend (int frame, float value)
{
    Event e;
    e.type = Event::PitchBend;
    e.channel = 0;
    e.frame = frame;
    e.value = value;
    return e;
}

double sideRms (const sbtest::Rendered& r, double t0, double t1)
{
    double sum = 0.0;
    const std::size_t a = at (t0), b = std::min (at (t1), r.left.size());
    for (std::size_t i = a; i < b; ++i)
    {
        const double s = 0.5 * (static_cast<double> (r.left[i]) - r.right[i]);
        sum += s * s;
    }
    return std::sqrt (sum / static_cast<double> (b - a));
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// G12: same note and voice steal (kCapVoiceSteal)
// ---------------------------------------------------------------------------------------------------------------------

TEST (same_note_modes_steal_stack_ignore)
{
    // A4 = 440 Hz is exactly 110 cycles in 0.25 s at 48 kHz: a second voice that starts at 0.25 s is in phase with the first
    const std::vector<Event> twice { noteOn (0, 69, 0.8f), noteOn (static_cast<int> (0.25 * kSr), 69, 0.8f) };
    const std::vector<Event> once { noteOn (0, 69, 0.8f) };

    Synth steal;
    steal.set ("master.voice.same_note", 0.0f);
    const auto rSteal = steal.play (twice, 0.9);

    Synth stack;
    stack.set ("master.voice.same_note", 1.0f);
    const auto rStack = stack.play (twice, 0.9);

    Synth ignore;
    ignore.set ("master.voice.same_note", 2.0f);
    const auto rIgnore = ignore.play (twice, 0.9);
    const auto rOnce = Synth().play (once, 0.9);

    // Stack: two voices on the same key sound together: twice the level of Steal, which replaced the first voice
    const double levelSteal = rms (rSteal.left, 0.5, 0.8), levelStack = rms (rStack.left, 0.5, 0.8);
    std::printf ("    [SAME NOTE] level Steal %.4f Stack %.4f (ratio %.3f)\n", levelSteal, levelStack, levelStack / levelSteal);
    CHECK (levelStack / levelSteal > 1.8 && levelStack / levelSteal < 2.2);

    // Ignore: the second note on is dropped, so the render is the one of a single note on
    CHECK (identical (rIgnore, rOnce));
    CHECK (! identical (rSteal, rOnce));

    // Steal is the default and the version 2 behaviour: explicit Steal and no key at all render the same
    CHECK (identical (rSteal, Synth().play (twice, 0.9)));

    // with a slow attack the stolen note starts again from zero, the ignored one keeps its level
    Synth slowSteal, slowIgnore;
    for (Synth* s : { &slowSteal, &slowIgnore })
    {
        s->set ("env1.attack", 400.0f);
        s->set ("env1.sustain", 100.0f);
    }
    slowIgnore.set ("master.voice.same_note", 2.0f);
    const std::vector<Event> late { noteOn (0, 69, 0.8f), noteOn (static_cast<int> (0.8 * kSr), 69, 0.8f) };
    const auto rSlowSteal = slowSteal.play (late, 1.0);
    const auto rSlowIgnore = slowIgnore.play (late, 1.0);
    const double dip = rms (rSlowSteal.left, 0.82, 0.86) / rms (rSlowIgnore.left, 0.82, 0.86);
    std::printf ("    [SAME NOTE] level 20 ms after the retrigger, Steal / Ignore = %.3f\n", dip);
    CHECK (dip < 0.5);

    // a note off releases every stacked voice of the key
    Synth stackOff;
    stackOff.set ("master.voice.same_note", 1.0f);
    stackOff.set ("env1.release", 50.0f);
    const auto rOff = stackOff.play ({ noteOn (0, 69, 0.8f), noteOn (static_cast<int> (0.25 * kSr), 69, 0.8f), noteOff (static_cast<int> (0.5 * kSr), 69) }, 1.2);
    CHECK (rms (rOff.left, 0.45, 0.5) > 0.1);
    CHECK (rms (rOff.left, 0.9, 1.1) < 1.0e-4);
}

TEST (voice_steal_priority_picks_the_victim)
{
    // Serum 2 Voice Steal Priority names the voices that are NOT stolen. 3 voices, notes 60, 48, 72 in this order, then 67 comes in:
    // Newest keeps the newest, so the earliest (60) goes; Oldest keeps the oldest, so the most recent one (the new 67) goes; Highest
    // loses the lowest (48); Lowest loses the highest (72); Velocity loses the softest note (72 at velocity 0.3 here)
    auto notes = [] (float v60, float v48, float v72, float v67)
    {
        return std::vector<Event> { noteOn (0, 60, v60), noteOn (static_cast<int> (0.2 * kSr), 48, v48),
                                    noteOn (static_cast<int> (0.4 * kSr), 72, v72), noteOn (static_cast<int> (0.6 * kSr), 67, v67) };
    };
    struct Case { float priority; int victim; std::vector<Event> events; };
    const Case cases[] = { { 0.0f, 60, notes (0.8f, 0.8f, 0.8f, 0.8f) }, { 1.0f, 67, notes (0.8f, 0.8f, 0.8f, 0.8f) },
                           { 2.0f, 48, notes (0.8f, 0.8f, 0.8f, 0.8f) }, { 3.0f, 72, notes (0.8f, 0.8f, 0.8f, 0.8f) },
                           { 4.0f, 72, notes (0.9f, 0.8f, 0.3f, 0.9f) } };
    const int all[] = { 48, 60, 72, 67 };
    for (const Case& c : cases)
    {
        Synth s;
        s.set ("master.polyphony", 3.0f);
        s.set ("master.voice.steal_priority", c.priority);
        const auto r = s.play (c.events, 1.6);
        double victimPower = 0.0, minSurvivor = 1.0e30;
        for (int key : all)
        {
            const double p = powerAt (r.left, 1.0, noteHz (key));
            if (key == c.victim)
                victimPower = p;
            else
                minSurvivor = std::min (minSurvivor, p);
        }
        std::printf ("    [STEAL PRIORITY %.0f] victim %d power %.3e, weakest survivor %.3e\n", c.priority, c.victim, victimPower, minSurvivor);
        CHECK (victimPower * 1000.0 < minSurvivor);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// G13: poly legato (kCapPolyLegato)
// ---------------------------------------------------------------------------------------------------------------------

TEST (poly_legato_continues_the_envelope_of_a_held_voice)
{
    // 400 ms attack: note 60 is at full level (sustain 100 %) when note 64 comes in at 0.8 s with 60 still held
    const std::vector<Event> overlap { noteOn (0, 60, 0.8f), noteOn (static_cast<int> (0.8 * kSr), 64, 0.8f) };

    Synth off;
    off.set ("env1.attack", 400.0f);
    off.set ("env1.sustain", 100.0f);
    Synth on = off;
    on.set ("master.poly_legato", 1.0f);
    const auto rOff = off.play (overlap, 1.2), rOn = on.play (overlap, 1.2);

    // 40 ms after the second note on: without legato the new voice is still at the start of its attack (about 10 % of the level),
    // with legato it sounds at the level of the held voice: both notes at full level
    const double levelOff = rms (rOff.left, 0.84, 0.88), levelOn = rms (rOn.left, 0.84, 0.88);
    std::printf ("    [POLY LEGATO] level 40 ms after the overlapping note: off %.4f on %.4f (ratio %.3f)\n", levelOff, levelOn, levelOn / levelOff);
    CHECK (levelOn / levelOff > 1.25);
    // the power at the new pitch is there at once
    CHECK (powerAt (rOn.left, 0.82, noteHz (64)) > 2.5 * powerAt (rOff.left, 0.82, noteHz (64)));

    // before the overlap nothing differs
    CHECK (relativeDifference (rOff, rOn, 0.0, 0.79) < 1.0e-6);

    // no key held: the next note starts from zero as always, also after a legato phrase (no stale state remains)
    const std::vector<Event> separate { noteOn (0, 60, 0.8f), noteOff (static_cast<int> (0.6 * kSr), 60), noteOn (static_cast<int> (1.6 * kSr), 64, 0.8f) };
    const auto rSepOff = off.play (separate, 2.0), rSepOn = on.play (separate, 2.0);
    CHECK (identical (rSepOff, rSepOn));

    // the default is Off: explicit Off and no key at all render the same
    CHECK (identical (rOff, [&] { Synth plain; plain.set ("env1.attack", 400.0f); plain.set ("env1.sustain", 100.0f); return plain.play (overlap, 1.2); }()));
}

TEST (poly_legato_keeps_the_envelopes_apart)
{
    // env 2 (a filter envelope) must continue from env 2, not from env 1: the two are in different stages at the overlap.
    // env 1: instant attack, sustain 100 %. env 2: 600 ms attack, sustain 100 %, drives the volume of a second oscillator.
    Synth on;
    on.set ("env1.attack", 1.0f);
    on.set ("env1.sustain", 100.0f);
    on.set ("env2.attack", 600.0f);
    on.set ("env2.sustain", 100.0f);
    on.set ("osc1.level", 0.0f);
    on.set ("osc2.on", 1.0f);
    on.set ("osc2.wavetable", sbtest::choiceValue ("osc2.wavetable", "Sine"));
    on.set ("osc2.random_phase", 0.0f);
    on.set ("osc2.destination", 3.0f);
    on.set ("osc2.level", 100.0f);
    on.set ("mod01.source", sbtest::sourceChoice ("env2"));
    on.set ("mod01.destination", static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc2.level")->id)))));
    on.set ("mod01.amount", -100.0f);
    on.set ("master.poly_legato", 1.0f);
    Synth off = on;
    off.set ("master.poly_legato", 0.0f);

    // note 60 at 0 (env 2 is a third into its attack at 0.2 s), note 64 at 0.2 s with 60 still held
    const std::vector<Event> overlap { noteOn (0, 60, 0.8f), noteOn (static_cast<int> (0.2 * kSr), 64, 0.8f) };
    const auto rOn = on.play (overlap, 0.5), rOff = off.play (overlap, 0.5);
    // osc 2 is driven by env 2 only (its level falls as env 2 rises, amount -100 %). Over 85 ms from 220 ms the held voice (60) sits on
    // the env 2 curve. With legato the second voice (64) continues on that very curve, so both have about the same level; without
    // legato it starts at the beginning of its own attack, at the full level of osc 2. Had the second voice taken the state of env 1
    // (the old shared state), it would be at the end of an instant attack: the level of an env 2 that has fully risen.
    auto levelRatio = [&] (const sbtest::Rendered& r)
    {
        const auto p = sbtest::powerSpectrum (r.left, at (0.22), 4096, 4096, sbtest::Window::blackmanHarris);
        auto at4096 = [&] (double hz)
        {
            const long c = std::lround (hz * 4096.0 / kSr);
            double sum = 0.0;
            for (long k = c - 2; k <= c + 2; ++k)
                sum += p[static_cast<std::size_t> (k)];
            return sum;
        };
        return at4096 (noteHz (64)) / at4096 (noteHz (60));
    };
    const double withLegato = levelRatio (rOn), without = levelRatio (rOff);
    std::printf ("    [POLY LEGATO] power of the new voice / power of the held voice: legato %.3f, no legato %.3f\n", withLegato, without);
    CHECK (withLegato > 0.6 && withLegato < 1.7);
    CHECK (without > 5.0 * withLegato);
}

// ---------------------------------------------------------------------------------------------------------------------
// G14: separate pitch bend down range (kCapBendDownRange)
// ---------------------------------------------------------------------------------------------------------------------

TEST (bend_down_range_and_link)
{
    const int bendAt = static_cast<int> (0.1 * kSr);
    const double fNote = noteHz (60);
    auto bentPitch = [&] (float value, float downRange, float link, double expected)
    {
        Synth s;
        s.set ("master.bend_range", 2.0f);
        s.set ("master.bend_down_range", downRange);
        s.set ("master.bend_down_link", link);
        const auto r = s.play ({ noteOn (0, 60, 0.8f), pitchBend (bendAt, value) }, 0.6);
        return peakHz (r.left, 0.3, expected); // the search window is +-6 % around the expected pitch: a wrong range misses by more than the check allows
    };
    auto semis = [&] (double st) { return fNote * std::pow (2.0, st / 12.0); };

    // linked (the default): the down range is ignored, the wheel has the range of master.bend_range both ways
    const double linkedDown = bentPitch (-1.0f, 12.0f, 1.0f, semis (-2.0));
    CHECK (std::fabs (linkedDown - semis (-2.0)) < 1.0);
    // unlinked: down uses its own range, up still the master range
    const double freeDown = bentPitch (-1.0f, 12.0f, 0.0f, semis (-12.0));
    const double freeUp = bentPitch (1.0f, 12.0f, 0.0f, semis (2.0));
    std::printf ("    [BEND DOWN] linked %.2f Hz, unlinked down %.2f Hz, unlinked up %.2f Hz (note %.2f Hz)\n", linkedDown, freeDown, freeUp, fNote);
    CHECK (std::fabs (freeDown - semis (-12.0)) < 1.0);
    CHECK (std::fabs (freeUp - semis (2.0)) < 1.0);
    // half way down is half the range
    CHECK (std::fabs (bentPitch (-0.5f, 12.0f, 0.0f, semis (-6.0)) - semis (-6.0)) < 1.0);

    // a range of 0 keeps the pitch where it is
    CHECK (std::fabs (bentPitch (-1.0f, 0.0f, 0.0f, fNote) - fNote) < 1.0);

    // defaults (range 2, linked) and no keys render the same
    Synth plain;
    plain.set ("master.bend_range", 2.0f);
    Synth keys = plain;
    keys.set ("master.bend_down_range", 2.0f);
    keys.set ("master.bend_down_link", 1.0f);
    const std::vector<Event> ev { noteOn (0, 60, 0.8f), pitchBend (bendAt, -0.7f) };
    CHECK (identical (plain.play (ev, 0.4), keys.play (ev, 0.4)));
    // a linked range of 12 down does not matter
    Synth big = plain;
    big.set ("master.bend_down_range", 12.0f);
    CHECK (identical (plain.play (ev, 0.4), big.play (ev, 0.4)));
}

// ---------------------------------------------------------------------------------------------------------------------
// G15: stereo spread (kCapStereoSpread)
// ---------------------------------------------------------------------------------------------------------------------

TEST (stereo_spread_scales_the_side_signal)
{
    auto wideSynth = [] (float spread)
    {
        Synth s;
        s.set ("osc1.unison.voices", 4.0f);
        s.set ("osc1.unison.spread", 100.0f);
        s.set ("osc1.unison.detune", 40.0f);
        s.set ("master.stereo_spread", spread);
        return s;
    };
    const std::vector<Event> ev { noteOn (0, 60, 0.8f) };
    const auto r100 = wideSynth (100.0f).play (ev, 0.6), r50 = wideSynth (50.0f).play (ev, 0.6), r0 = wideSynth (0.0f).play (ev, 0.6);
    const double s100 = sideRms (r100, 0.2, 0.5), s50 = sideRms (r50, 0.2, 0.5), s0 = sideRms (r0, 0.2, 0.5);
    std::printf ("    [STEREO SPREAD] side RMS 100 %%: %.5f, 50 %%: %.5f, 0 %%: %.8f\n", s100, s50, s0);
    CHECK (s100 > 0.01);
    CHECK (std::fabs (s50 / s100 - 0.5) < 0.02); // mid / side: the side signal scales with the key
    CHECK (s0 == 0.0);                           // exactly mono
    // the mid signal is untouched: (L + R) / 2 is the same at every setting
    double dMid = 0.0;
    for (std::size_t i = at (0.2); i < at (0.5); ++i)
        dMid = std::max (dMid, std::fabs (0.5 * (static_cast<double> (r100.left[i]) + r100.right[i]) - 0.5 * (static_cast<double> (r0.left[i]) + r0.right[i])));
    CHECK (dMid < 1.0e-5);

    // 100 % is the version 2 sound
    CHECK (identical (r100, [&] { Synth s; s.set ("osc1.unison.voices", 4.0f); s.set ("osc1.unison.spread", 100.0f); s.set ("osc1.unison.detune", 40.0f); return s.play (ev, 0.6); }()));

    // the key is a modulation destination: a macro at 100 % that takes 100 % off the spread collapses the image
    Synth modulated = wideSynth (100.0f);
    modulated.set ("mod01.source", sbtest::sourceChoice ("macro1"));
    modulated.set ("mod01.destination", static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("master.stereo_spread")->id)))));
    modulated.set ("mod01.amount", -100.0f);
    modulated.set ("macro.1", 100.0f);
    const auto rMod = modulated.play (ev, 0.6);
    const double sMod = sideRms (rMod, 0.2, 0.5);
    std::printf ("    [STEREO SPREAD] side RMS with the macro taking the spread away: %.6f\n", sMod);
    CHECK (sMod < 0.05 * s100);
}

// ---------------------------------------------------------------------------------------------------------------------
// G16: LFO swing (kCapLfoSwing)
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
/** Sine carrier whose level follows LFO 1 (a rising saw at 2 Hz, restarted by the note) so that the level is the LFO value. */
Synth lfoLevelSynth (float swing, const char* lfoKey = "lfo1")
{
    Synth s;
    const std::string k (lfoKey);
    s.set ((k + ".shape").c_str(), sbtest::choiceValue ("lfo1.shape", "Saw Up"));
    s.set ((k + ".sync").c_str(), 0.0f);
    s.set ((k + ".rate").c_str(), 2.0f);
    s.set ("osc1.level", 0.0f);
    s.set ("osc2.on", 1.0f);
    s.set ("osc2.wavetable", sbtest::choiceValue ("osc2.wavetable", "Sine"));
    s.set ("osc2.random_phase", 0.0f);
    s.set ("osc2.destination", 3.0f);
    s.set ("osc2.level", 0.0f);
    s.set ("mod01.source", sbtest::sourceChoice (lfoKey));
    s.set ("mod01.destination", static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc2.level")->id)))));
    s.set ("mod01.amount", 100.0f);
    s.set ((k + ".swing").c_str(), swing);
    return s;
}

/** Peak amplitude of a carrier in a 4 ms window around t (seconds). */
double carrier (const sbtest::Rendered& r, double t)
{
    double peak = 0.0;
    for (std::size_t i = at (t - 0.002); i < at (t + 0.002); ++i)
        peak = std::max (peak, static_cast<double> (std::fabs (r.left[i])));
    return peak;
}
} // namespace

TEST (lfo_swing_delays_the_second_slot_of_every_pair)
{
    const std::vector<Event> ev { noteOn (0, 69, 0.8f) };
    const auto r0 = lfoLevelSynth (0.0f).play (ev, 1.6);
    const auto r100 = lfoLevelSynth (100.0f).play (ev, 1.6);

    // the LFO is a rising saw at 2 Hz (cycle 0.5 s) whose value sets the level of a sine; a pair is 1/8 of the cycle. With swing s the
    // second half of every pair starts at 1/2 + s / 4 of the pair instead of 1/2, so the swung level at t is the unswung level at the
    // warped instant (measured on the rising second half of the cycle, where the level is monotonic).
    const double period = 0.5, split = 0.75;
    auto warped = [&] (double t)
    {
        const double scaled = t / period * 4.0;
        const double pair = std::floor (scaled), within = scaled - pair;
        const double w = within < split ? within * 0.5 / split : 0.5 + (within - split) * 0.5 / (1.0 - split);
        return (pair + w) / 4.0 * period;
    };
    for (double t : { 0.30, 0.33, 0.43, 0.46 })
    {
        const double got = carrier (r100, t), expected = carrier (r0, warped (t));
        std::printf ("    [LFO SWING] t=%.2f swung level %.4f, unswung level at the warped instant %.4f (unswung at t: %.4f)\n", t, got, expected, carrier (r0, t));
        CHECK (std::fabs (got - expected) < 0.04 * expected);
        CHECK (std::fabs (carrier (r0, t) - expected) > 0.04 * expected); // the swing does move the level
    }

    // pair boundaries are fixed points: the level there is the same with or without swing
    for (double t : { 0.25, 0.375 })
        CHECK (std::fabs (carrier (r100, t) - carrier (r0, t)) < 0.06 * carrier (r0, t));

    // the cycle length does not change: the value one cycle later is the value one cycle earlier
    CHECK (std::fabs (carrier (r100, 0.35) - carrier (r100, 0.85)) < 0.03 * carrier (r100, 0.85));

    // 0 % is the version 2 sound, and any other LFO is as before
    CHECK (identical (r0, [&] { Synth s = lfoLevelSynth (0.0f); return s.play (ev, 1.6); }()));
    // an LFO without the swing key at its default stays identical when another LFO swings
    Synth other = lfoLevelSynth (0.0f);
    other.set ("lfo7.swing", 80.0f);
    CHECK (identical (r0, other.play (ev, 1.6)));

    // LFO 10 (a key of the LFO 9 / 10 block) swings as well
    const auto rA = lfoLevelSynth (0.0f, "lfo10").play (ev, 1.0), rB = lfoLevelSynth (100.0f, "lfo10").play (ev, 1.0);
    CHECK (! identical (rA, rB));

    // swing is a modulation destination
    Synth modulated = lfoLevelSynth (0.0f);
    modulated.set ("mod02.source", sbtest::sourceChoice ("macro1"));
    modulated.set ("mod02.destination", static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("lfo1.swing")->id)))));
    modulated.set ("mod02.amount", 100.0f);
    modulated.set ("macro.1", 100.0f);
    const auto rMod = modulated.play (ev, 1.6);
    CHECK (std::fabs (carrier (rMod, 0.30) - carrier (r100, 0.30)) < 0.04 * carrier (r100, 0.30));
}

// ---------------------------------------------------------------------------------------------------------------------
// G18: 4x oversampling and no smoothing (kCapQualityOptions)
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
/** Share of the power of a bright saw that is not on a harmonic of the note (aliasing + intermodulation). */
double inharmonicShare (const sbtest::Rendered& r, double f0)
{
    const auto p = sbtest::powerSpectrum (r.left, at (0.2), kFft, kFft, sbtest::Window::blackmanHarris);
    double harmonic = 0.0, total = 0.0;
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        total += p[k];
        const double hz = static_cast<double> (k) * kSr / static_cast<double> (kFft);
        const double h = hz / f0;
        if (std::fabs (h - std::round (h)) * f0 < 2.5 * kSr / static_cast<double> (kFft) && std::round (h) >= 1.0)
            harmonic += p[k];
    }
    return total > 0.0 ? (total - harmonic) / total : 0.0;
}

/** Osc 1 frequency modulated by osc 2 (silent, a ratio of about 3): the sidebands run far above the Nyquist frequency, which is what
    oversampling is for. */
Synth brightSaw()
{
    Synth s;
    s.set ("osc1.warp.type", sbtest::choiceValue ("osc1.warp.type", "FM from Osc 2"));
    s.set ("osc1.warp.amount", 100.0f);
    s.set ("osc2.on", 1.0f);
    s.set ("osc2.level", 0.0f);
    s.set ("osc2.transpose", 19.0f);
    return s;
}
} // namespace

TEST (oversample_4x_changes_the_aliasing)
{
    const std::vector<Event> ev { noteOn (0, 84, 0.9f) };
    const double f0 = noteHz (84);
    Quality high;       // the default tier: 2x
    Quality low;
    low.oversampling = 1;

    Synth off = brightSaw();
    Synth on = brightSaw();
    on.set ("quality.oversample_4x", 1.0f);
    const auto r1x = off.play (ev, 0.6, low), r2x = off.play (ev, 0.6, high);
    const auto r4x = on.play (ev, 0.6, high), r4xLow = on.play (ev, 0.6, low);

    const double a1 = inharmonicShare (r1x, f0), a2 = inharmonicShare (r2x, f0), a4 = inharmonicShare (r4x, f0);
    std::printf ("    [OVERSAMPLE] inharmonic share 1x %.3e, 2x %.3e, 4x %.3e; relative difference 2x/4x %.3e\n", a1, a2, a4,
                 relativeDifference (r2x, r4x, 0.2, 0.5));
    CHECK (relativeDifference (r2x, r4x, 0.2, 0.5) > 1.0e-3); // an audible difference between the tier and the option
    CHECK (a4 < a2);                                           // and it is the aliasing that falls
    CHECK (a2 < a1);
    // the option wins over the tier: 4x is 4x on the Low tier as well
    CHECK (relativeDifference (r4x, r4xLow, 0.2, 0.5) < 1.0e-3);
    CHECK (relativeDifference (r1x, r4xLow, 0.2, 0.5) > 1.0e-3);

    // the default (Off) is the tier, bit for bit
    CHECK (identical (r2x, [&] { Synth s = brightSaw(); s.set ("quality.oversample_4x", 0.0f); return s.play (ev, 0.6, high); }()));

    // the option is applied by prepare (): the engine says so, and does not allocate or change anything before that
    auto e = off.build (high);
    CHECK (! e->needsPrepare());
    setByKey (*e, "quality.oversample_4x", 1.0f);
    CHECK (e->needsPrepare());
    std::vector<float> l (256), r (256);
    const Event note = noteOn (0, 84, 0.9f);
    e->process (&note, 1, l.data(), r.data(), 256);
    CHECK (e->needsPrepare());
    CHECK (sbtest::allFinite (l) && sbtest::allFinite (r));
    e->prepare (kSr, 256, high);
    CHECK (! e->needsPrepare());
    setByKey (*e, "quality.oversample_4x", 0.0f);
    CHECK (e->needsPrepare());
    e->prepare (kSr, 256, high);
    CHECK (! e->needsPrepare());
    // and back to the tier, bit for bit
    const auto rBack = sbtest::render (*e, ev, static_cast<int> (0.6 * kSr), 256);
    CHECK (std::fabs (inharmonicShare (rBack, f0) / a2 - 1.0) < 0.01);
}

TEST (no_smoothing_makes_a_parameter_jump_instant)
{
    // osc 1 level drops from 100 % to 5 % between two blocks. With smoothing the gain glides down over some 100 ms (a 5 Hz one pole
    // filter); without it the first cycle after the jump is already at the new level.
    constexpr int kBlock = 2400, kTail = 19200;
    auto jump = [] (float noSmoothing)
    {
        Synth s;
        s.set ("quality.no_smoothing", noSmoothing);
        auto e = s.build();
        const Event on = noteOn (0, 69, 0.8f);
        std::vector<float> l (kBlock + kTail), r (kBlock + kTail);
        e->process (&on, 1, l.data(), r.data(), kBlock);
        setByKey (*e, "osc1.level", 5.0f);
        e->process (nullptr, 0, l.data() + kBlock, r.data() + kBlock, kTail);
        return l;
    };
    auto peak = [] (const std::vector<float>& x, int from, int to)
    {
        double p = 0.0;
        for (int i = from; i < to; ++i)
            p = std::max (p, static_cast<double> (std::fabs (x[static_cast<std::size_t> (i)])));
        return p;
    };
    const auto smooth = jump (0.0f), instant = jump (1.0f);
    const double before = peak (smooth, 2000, 2400);
    const double smoothFirst = peak (smooth, kBlock, kBlock + 110), instantFirst = peak (instant, kBlock, kBlock + 110);
    std::printf ("    [NO SMOOTHING] peak before the jump %.4f; first cycle after it: smoothed %.4f, instant %.4f\n", before, smoothFirst, instantFirst);
    CHECK (instantFirst < 0.15 * before);        // the new level at once
    CHECK (smoothFirst > 0.8 * before);          // the smoothed gain has hardly moved yet
    // both end at the same level
    const double endS = peak (smooth, kBlock + kTail - 400, kBlock + kTail), endI = peak (instant, kBlock + kTail - 400, kBlock + kTail);
    CHECK (std::fabs (endS - endI) < 0.02 * endI);

    // one engine with the option never changes another engine that is processed right after it on the same thread
    Synth plainSynth;
    auto a = Synth().build();
    Synth withOption;
    withOption.set ("quality.no_smoothing", 1.0f);
    auto b = withOption.build();
    const Event on = noteOn (0, 69, 0.8f);
    std::vector<float> la (kBlock + kTail), ra (kBlock + kTail), lb (kBlock + kTail), rb (kBlock + kTail);
    a->process (&on, 1, la.data(), ra.data(), kBlock);
    b->process (&on, 1, lb.data(), rb.data(), kBlock);
    setByKey (*a, "osc1.level", 5.0f);
    setByKey (*b, "osc1.level", 5.0f);
    b->process (nullptr, 0, lb.data() + kBlock, rb.data() + kBlock, kTail); // option on, then the engine without it
    a->process (nullptr, 0, la.data() + kBlock, ra.data() + kBlock, kTail);
    CHECK (la == smooth);                       // a still smooths, bit for bit as if b did not exist
    CHECK (lb == instant);
}

TEST (b15_defaults_leave_every_engine_render_alone)
{
    // every key of the block at its default, set explicitly, against an engine that never saw them
    const std::vector<Event> ev { noteOn (0, 60, 0.9f), noteOn (static_cast<int> (0.1 * kSr), 64, 0.9f), pitchBend (static_cast<int> (0.2 * kSr), -0.4f),
                                  noteOff (static_cast<int> (0.3 * kSr), 60), noteOn (static_cast<int> (0.35 * kSr), 64, 0.7f) };
    Synth plain;
    plain.set ("osc1.unison.voices", 3.0f);
    plain.set ("lfo1.rate", 3.0f);
    Synth keys = plain;
    keys.set ("master.voice.same_note", 0.0f);
    keys.set ("master.voice.steal_priority", 0.0f);
    keys.set ("master.poly_legato", 0.0f);
    keys.set ("master.bend_down_range", 2.0f);
    keys.set ("master.bend_down_link", 1.0f);
    keys.set ("master.stereo_spread", 100.0f);
    for (int n = 1; n <= 10; ++n)
        keys.set (("lfo" + std::to_string (n) + ".swing").c_str(), 0.0f);
    keys.set ("quality.oversample_4x", 0.0f);
    keys.set ("quality.no_smoothing", 0.0f);
    CHECK (identical (plain.play (ev, 0.6), keys.play (ev, 0.6)));
}
