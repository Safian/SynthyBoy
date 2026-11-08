// SynthyBoy - engine-vital tests: WP2 (modulation core) and WP3 (voice / global) of docs/ui-design/13-new-keys.md.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every capability bit engine-vital reports has tests here (or in core/tests/test_events.cpp for the event contract) that render the key
// off and on and assert a measurable difference of the audio. The thresholds are ratios between the two renderings (the key's effect is
// large by construction: a modulation that opens a level which is closed otherwise), not absolute levels.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/tuning.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::choiceValue;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;

std::unique_ptr<IEngine> makeVital()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    return e;
}

/** Osc 1 as a plain sine at `level` percent, nothing else in the signal path. */
void sine (IEngine& e, float level)
{
    setByKey (e, "osc1.wavetable", 0.0f);
    setByKey (e, "osc1.level", level);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

void route (IEngine& e, int slot, const char* source, const char* destinationKey, float amountPercent)
{
    char key[32];
    std::snprintf (key, sizeof key, "mod%02d.source", slot);
    setByKey (e, key, sbtest::sourceChoice (source));
    std::snprintf (key, sizeof key, "mod%02d.destination", slot);
    const ParamInfo* destination = findParamByKey (destinationKey);
    CHECK (destination != nullptr);
    setByKey (e, key, destination != nullptr ? static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (destination->id)))) : 0.0f);
    std::snprintf (key, sizeof key, "mod%02d.amount", slot);
    setByKey (e, key, amountPercent);
}

void prepare (IEngine& e)
{
    e.prepare (kSr, 256, Quality {});
}

/** Renders with the host transport advancing from `transport` (if given), in blocks of 256. */
sbtest::Rendered renderWith (IEngine& e, const std::vector<Event>& events, int frames, const TransportInfo* transport = nullptr)
{
    sbtest::Rendered out;
    out.left.assign (static_cast<size_t> (frames), 0.0f);
    out.right.assign (static_cast<size_t> (frames), 0.0f);
    std::vector<Event> local;
    size_t idx = 0;
    for (int start = 0; start < frames; start += 256)
    {
        const int n = std::min (256, frames - start);
        local.clear();
        while (idx < events.size() && events[idx].frame < start + n)
        {
            auto ev = events[idx++];
            ev.frame -= start;
            local.push_back (ev);
        }
        if (transport != nullptr)
        {
            TransportInfo t = *transport;
            t.songPosBeats += static_cast<double> (start) / kSr * t.bpm / 60.0;
            e.setTransport (t);
        }
        e.process (local.data(), static_cast<int> (local.size()), out.left.data() + start, out.right.data() + start, n);
    }
    return out;
}

double rmsBetween (const sbtest::Rendered& r, double t0, double t1)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

int frames (double seconds)
{
    return static_cast<int> (seconds * kSr);
}

double pitchOf (const sbtest::Rendered& r, double guess, double from = 0.3)
{
    const auto spectrum = sbtest::powerSpectrum (r.left, static_cast<size_t> (from * kSr), 32768, 131072);
    return sbtest::peakFrequency (spectrum, 131072, kSr, guess);
}

Event event (Event::Type type, int frame, int channel, int key, float value, Event::Expression expression = Event::Tuning)
{
    Event e = noteOn (frame, key, value, channel);
    e.type = type;
    e.expression = expression;
    return e;
}

/** One quiet and one loud rendering: the key `on` is the thing that opens the sound. */
void expectOpens (double quiet, double loud, const char* what)
{
    std::printf ("    %s: rms %.5f -> %.5f\n", what, quiet, loud);
    CHECK (quiet < 0.004);
    CHECK (loud > 0.02);
    CHECK (loud > 8.0 * quiet);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// WP2
// ---------------------------------------------------------------------------------------------------------------------------------
TEST (macros_5_to_8_are_sources_and_destinations)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapMacros58));
    for (int n = 5; n <= 8; ++n)
    {
        const std::string macro = "macro" + std::to_string (n), key = "macro." + std::to_string (n);
        auto closed = makeVital(), open = makeVital();
        for (auto* e : { closed.get(), open.get() })
        {
            sine (*e, 0.0f);
            route (*e, 1, macro.c_str(), "osc1.level", 100.0f); // macro N opens the oscillator level
            prepare (*e);
        }
        setByKey (*open, key.c_str(), 100.0f);
        open->reset();
        closed->reset();
        const auto a = renderWith (*closed, { noteOn (0, 69) }, frames (0.4)), b = renderWith (*open, { noteOn (0, 69) }, frames (0.4));
        expectOpens (rmsBetween (a, 0.1, 0.4), rmsBetween (b, 0.1, 0.4), macro.c_str());
    }

    // as a destination: the mod wheel moves macro 5, macro 5 opens the level
    auto e = makeVital();
    sine (*e, 0.0f);
    route (*e, 1, "macro5", "osc1.level", 100.0f);
    route (*e, 2, "mod_wheel", "macro.5", 100.0f);
    prepare (*e);
    const auto wheel = renderWith (*e, { noteOn (0, 69), event (Event::ModWheel, frames (0.3), 0, 0, 1.0f) }, frames (0.7));
    expectOpens (rmsBetween (wheel, 0.1, 0.3), rmsBetween (wheel, 0.45, 0.7), "mod wheel -> macro 5 -> level");
}

TEST (lfo_9_and_10_run_and_modulate)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapLfo910));
    for (int n = 9; n <= 10; ++n)
    {
        const std::string lfo = "lfo" + std::to_string (n);
        auto none = makeVital(), routed = makeVital();
        for (auto* e : { none.get(), routed.get() })
        {
            sine (*e, 0.0f);
            setByKey (*e, (lfo + ".shape").c_str(), choiceValue ((lfo + ".shape").c_str(), "Saw Up"));
            setByKey (*e, (lfo + ".rate").c_str(), 4.0f);
            prepare (*e);
        }
        route (*routed, 1, lfo.c_str(), "osc1.level", 100.0f);
        routed->reset();
        const auto a = renderWith (*none, { noteOn (0, 69) }, frames (0.8)), b = renderWith (*routed, { noteOn (0, 69) }, frames (0.8));
        expectOpens (rmsBetween (a, 0.1, 0.8), rmsBetween (b, 0.1, 0.8), lfo.c_str());

        // the rate key reaches the LFO: a saw of 4 Hz peaks every 250 ms, one of 8 Hz every 125 ms (the last 40 ms of each cycle are loud)
        setByKey (*routed, (lfo + ".rate").c_str(), 16.0f);
        routed->reset();
        const auto fast = renderWith (*routed, { noteOn (0, 69) }, frames (0.8));
        double slowSpread = 0.0, fastSpread = 0.0;
        for (double t = 0.1; t < 0.7; t += 0.02)
        {
            slowSpread += std::fabs (rmsBetween (b, t, t + 0.02) - rmsBetween (b, t + 0.02, t + 0.04));
            fastSpread += std::fabs (rmsBetween (fast, t, t + 0.02) - rmsBetween (fast, t + 0.02, t + 0.04));
        }
        std::printf ("    %s: movement of the level at 4 Hz %.3f, at 16 Hz %.3f\n", lfo.c_str(), slowSpread, fastSpread);
        CHECK (fastSpread > 1.5 * slowSpread);
    }
}

TEST (new_mod_sources_lift_slide_oct_note_poly_aftertouch)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapModSourcesExtra));
    const auto fresh = [] (const char* source)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        route (*e, 1, source, "osc1.level", 100.0f);
        prepare (*e);
        return e;
    };

    // oct note: (note mod 12) / 12 opens the level more for a higher note in the octave
    {
        double c = 0.0, fSharp = 0.0, b = 0.0;
        for (const int key : { 60, 66, 71 })
        {
            auto e = fresh ("oct_note");
            const double v = rmsBetween (renderWith (*e, { noteOn (0, key) }, frames (0.4)), 0.1, 0.4);
            (key == 60 ? c : (key == 66 ? fSharp : b)) = v;
        }
        std::printf ("    oct_note: C %.5f, F# %.5f, B %.5f\n", c, fSharp, b);
        CHECK (c < 0.004);
        CHECK (fSharp > 0.01);
        CHECK (b > 1.5 * fSharp);
    }

    // lift: the release velocity, 0 before the note off; it opens the level in the release tail only if the host sent one
    {
        double plain = 0.0, lifted = 0.0;
        for (const float velocity : { 0.0f, 1.0f })
        {
            auto e = fresh ("lift");
            setByKey (*e, "env1.release", 600.0f);
            Event off = noteOff (frames (0.2), 69);
            off.value = velocity;
            const auto out = renderWith (*e, { noteOn (0, 69), off }, frames (0.6));
            CHECK (rmsBetween (out, 0.05, 0.19) < 0.004); // held: lift is 0
            (velocity == 0.0f ? plain : lifted) = rmsBetween (out, 0.22, 0.4);
        }
        expectOpens (plain, lifted, "lift (release velocity 0 / 1, in the tail)");
    }

    // slide: per-note brightness (CLAP BRIGHTNESS or MIDI CC 74); poly aftertouch: per-note pressure
    for (const char* source : { "slide", "poly_aftertouch" })
    {
        const Event::Expression kind = std::string (source) == "slide" ? Event::Brightness : Event::Pressure;
        auto e = fresh (source);
        const auto out = renderWith (*e, { noteOn (0, 69, 1.0f, 0, 3), event (Event::NoteExpression, frames (0.3), 0, -1, 1.0f, kind) }, frames (0.7));
        expectOpens (rmsBetween (out, 0.1, 0.29), rmsBetween (out, 0.4, 0.7), source);
    }

    // channel pressure is "aftertouch", not "poly aftertouch" (MPE is off)
    {
        auto poly = fresh ("poly_aftertouch"), channel = fresh ("aftertouch");
        const std::vector<Event> events = { noteOn (0, 69), event (Event::Aftertouch, frames (0.3), 0, 0, 1.0f) };
        const auto a = renderWith (*poly, events, frames (0.7)), b = renderWith (*channel, events, frames (0.7));
        CHECK (rmsBetween (a, 0.4, 0.7) < 0.004);
        CHECK (rmsBetween (b, 0.4, 0.7) > 0.02);
    }
}

TEST (aux_source_multiplies_the_slot)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapAuxSource));
    const auto level = [] (float aux, bool invert, float output, float power)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        route (*e, 1, "macro1", "osc1.level", 100.0f);
        setByKey (*e, "macro.1", 100.0f);
        setByKey (*e, "mod01.aux_source", sbtest::sourceChoice ("macro2"));
        setByKey (*e, "macro.2", aux);
        setByKey (*e, "mod01.aux_inv", invert ? 1.0f : 0.0f);
        setByKey (*e, "mod01.aux_output", output);
        setByKey (*e, "mod01.aux_power", power);
        prepare (*e);
        return rmsBetween (renderWith (*e, { noteOn (0, 69) }, frames (0.4)), 0.1, 0.4);
    };

    const double zero = level (0.0f, false, 100.0f, 0.0f), half = level (50.0f, false, 100.0f, 0.0f), full = level (100.0f, false, 100.0f, 0.0f);
    std::printf("    aux macro 0 / 50 / 100 %%: rms %.5f / %.5f / %.5f\n", zero, half, full);
    CHECK (zero < 0.004);
    CHECK (half > 0.01 && half < 0.8 * full);
    CHECK (full > 0.02);

    const double inverted = level (100.0f, true, 100.0f, 0.0f), invertedZero = level (0.0f, true, 100.0f, 0.0f);
    std::printf("    aux inverted: macro 100 -> %.5f, macro 0 -> %.5f\n", inverted, invertedZero);
    CHECK (inverted < 0.004);
    CHECK (invertedZero > 0.8 * full);

    const double scaled = level (100.0f, false, 50.0f, 0.0f), boosted = level (100.0f, false, 200.0f, 0.0f);
    std::printf("    aux output 50 / 100 / 200 %%: %.5f / %.5f / %.5f\n", scaled, full, boosted);
    CHECK (scaled < 0.7 * full);
    CHECK (boosted > 1.05 * full);

    const double curved = level (50.0f, false, 100.0f, 8.0f);
    std::printf("    aux curve +8 at 50 %%: %.5f (linear %.5f)\n", curved, half);
    CHECK (std::fabs (curved - half) > 0.15 * half);

    // the slot's source stays in charge: the aux alone does nothing
    auto e = makeVital();
    sine (*e, 0.0f);
    route (*e, 1, "macro1", "osc1.level", 100.0f);
    setByKey (*e, "mod01.aux_source", sbtest::sourceChoice ("macro2"));
    setByKey (*e, "macro.2", 100.0f);
    prepare (*e);
    CHECK (rmsBetween (renderWith (*e, { noteOn (0, 69) }, frames (0.4)), 0.1, 0.4) < 0.004);
}

TEST (lfo_direction_reverse_and_ping_pong)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapLfoOptions));
    double at[3][2] = {};
    for (int direction = 0; direction < 3; ++direction)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "lfo1.shape", choiceValue ("lfo1.shape", "Saw Up"));
        setByKey (*e, "lfo1.rate", 2.0f);
        setByKey (*e, "lfo1.smooth", 0.0f);
        setByKey (*e, "lfo1.direction", static_cast<float> (direction));
        route (*e, 1, "lfo1", "osc1.level", 100.0f);
        prepare (*e);
        const auto out = renderWith (*e, { noteOn (0, 69) }, frames (0.5));
        at[direction][0] = rmsBetween (out, 0.02, 0.06); // phase 0.04 .. 0.12 of a 2 Hz cycle
        at[direction][1] = rmsBetween (out, 0.42, 0.46); // phase 0.84 .. 0.92
    }
    std::printf ("    early: forward %.5f reverse %.5f ping pong %.5f; late: forward %.5f reverse %.5f ping pong %.5f\n", at[0][0], at[1][0], at[2][0],
                 at[0][1], at[1][1], at[2][1]);
    CHECK (at[1][0] > 5.0 * at[0][0]);   // reverse starts high where forward starts low
    CHECK (at[0][1] > 5.0 * at[1][1]);   // and ends low where forward ends high
    CHECK (at[0][1] > 3.0 * at[2][1]);   // ping pong has come back down by phase 0.88 (shape position 0.24)
    CHECK (at[2][0] < 0.5 * at[0][1]);
}

TEST (lfo_mono_keeps_the_phase_across_notes)
{
    double off = 0.0, on = 0.0;
    for (const bool mono : { false, true })
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "lfo1.shape", choiceValue ("lfo1.shape", "Saw Up"));
        setByKey (*e, "lfo1.rate", 1.0f);
        setByKey (*e, "lfo1.smooth", 0.0f);
        setByKey (*e, "lfo1.mono", mono ? 1.0f : 0.0f);
        route (*e, 1, "lfo1", "osc1.level", 100.0f);
        prepare (*e);
        // the note starts 0.6 s into the run: a polyphonic LFO starts at phase 0 there, a mono one is at phase 0.6
        const auto out = renderWith (*e, { noteOn (frames (0.6), 69) }, frames (0.75));
        (mono ? on : off) = rmsBetween (out, 0.61, 0.65);
    }
    std::printf ("    LFO at the start of a note 0.6 s into the run: polyphonic %.5f, mono %.5f\n", off, on);
    CHECK (off < 0.01);
    CHECK (on > 0.05);
}

TEST (lfo_host_locks_the_phase_to_the_transport)
{
    // free rate 1 Hz at 60 BPM: one beat is one second, the host phase is fract (song position in beats)
    const auto start = [] (bool host, double songPosBeats)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "lfo1.shape", choiceValue ("lfo1.shape", "Saw Up"));
        setByKey (*e, "lfo1.rate", 1.0f);
        setByKey (*e, "lfo1.smooth", 0.0f);
        setByKey (*e, "lfo1.host", host ? 1.0f : 0.0f);
        route (*e, 1, "lfo1", "osc1.level", 100.0f);
        prepare (*e);
        TransportInfo transport;
        transport.bpm = 60.0;
        transport.songPosBeats = songPosBeats;
        transport.playing = true;
        return rmsBetween (renderWith (*e, { noteOn (0, 69) }, frames (0.3), &transport), 0.02, 0.1);
    };
    const double hostStart = start (true, 0.0), hostLater = start (true, 0.6), free = start (false, 0.6), hostNextCycle = start (true, 3.6);
    std::printf ("    LFO at the start: host song position 0 -> %.5f, 0.6 -> %.5f, 3.6 -> %.5f; no host lock at 0.6 -> %.5f\n", hostStart, hostLater, hostNextCycle,
                 free);
    CHECK (hostStart < 0.01);
    CHECK (hostLater > 0.05);
    CHECK_NEAR (hostNextCycle, hostLater, 0.1 * hostLater); // the same phase one cycle (3 beats) later
    CHECK (free < 0.01);

    // a stopped transport lets the LFO run free
    auto e = makeVital();
    sine (*e, 0.0f);
    setByKey (*e, "lfo1.shape", choiceValue ("lfo1.shape", "Saw Up"));
    setByKey (*e, "lfo1.rate", 1.0f);
    setByKey (*e, "lfo1.smooth", 0.0f);
    setByKey (*e, "lfo1.host", 1.0f);
    route (*e, 1, "lfo1", "osc1.level", 100.0f);
    prepare (*e);
    TransportInfo stopped;
    stopped.playing = false;
    stopped.songPosBeats = 7.6;
    CHECK (rmsBetween (renderWith (*e, { noteOn (0, 69) }, frames (0.3), &stopped), 0.02, 0.1) < 0.01);
}

TEST (random_lfo_rossler_attractor_style)
{
    const auto run = [] (const char* style)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "rlfo1.style", choiceValue ("rlfo1.style", style));
        setByKey (*e, "rlfo1.rate", 2.0f);
        route (*e, 1, "rlfo1", "osc1.level", 100.0f);
        prepare (*e);
        return renderWith (*e, { noteOn (0, 69) }, frames (2.0));
    };
    const auto rossler = run ("Rossler attractor"), perlin = run ("Perlin");
    CHECK (sbtest::allFinite (rossler.left));
    double lo = 1e9, hi = 0.0, difference = 0.0;
    for (double t = 0.1; t < 1.9; t += 0.05)
    {
        const double r = rmsBetween (rossler, t, t + 0.05);
        lo = std::min (lo, r);
        hi = std::max (hi, r);
        difference += std::fabs (r - rmsBetween (perlin, t, t + 0.05));
    }
    std::printf ("    Rossler random LFO as the level: windows from %.5f to %.5f; distance to Perlin %.3f\n", lo, hi, difference);
    CHECK (hi > 0.03);        // the attractor drives the level
    CHECK (hi > 2.0 * lo);    // and moves
    CHECK (difference > 0.1); // it is a different signal than the other styles
}

TEST (envelope_beat_units_follow_the_host_tempo)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapEnvBeats));
    const auto attack = [] (bool beats, double bpm)
    {
        auto e = makeVital();
        sine (*e, 60.0f);
        setByKey (*e, "env1.attack", 5.0f);        // the millisecond key: 5 ms
        setByKey (*e, "env1.attack_beats", 2.0f);  // the beat key: 2 beats
        setByKey (*e, "env1.units", beats ? 1.0f : 0.0f);
        prepare (*e);
        TransportInfo transport;
        transport.bpm = bpm;
        transport.playing = true;
        return renderWith (*e, { noteOn (0, 69) }, frames (1.5), &transport);
    };
    const auto ms = attack (false, 60.0), slow = attack (true, 60.0), fast = attack (true, 240.0);
    const double msStart = rmsBetween (ms, 0.0, 0.1), slowStart = rmsBetween (slow, 0.0, 0.1), fastStart = rmsBetween (fast, 0.0, 0.1);
    std::printf ("    first 100 ms: 5 ms attack %.5f; 2 beats at 60 BPM (2 s) %.5f; 2 beats at 240 BPM (0.5 s) %.5f\n", msStart, slowStart, fastStart);
    CHECK (msStart > 0.05);
    CHECK (slowStart < 0.25 * msStart);
    CHECK (fastStart > 2.0 * slowStart && fastStart < msStart);
    // 2 beats at 240 BPM is 0.5 s: the note is up to full level after 0.8 s, at 60 BPM (2 s) it is not
    CHECK (rmsBetween (fast, 0.8, 1.0) > 0.8 * rmsBetween (ms, 0.8, 1.0));
    CHECK (rmsBetween (slow, 0.8, 1.0) < 0.6 * rmsBetween (ms, 0.8, 1.0));

    // the millisecond key is inert in Beats mode, also as a modulation destination
    auto e = makeVital();
    sine (*e, 60.0f);
    setByKey (*e, "env1.attack_beats", 2.0f);
    setByKey (*e, "env1.units", 1.0f);
    setByKey (*e, "macro.1", 100.0f);
    route (*e, 1, "macro1", "env1.attack", -100.0f);
    prepare (*e);
    TransportInfo transport;
    transport.bpm = 60.0;
    CHECK (rmsBetween (renderWith (*e, { noteOn (0, 69) }, frames (0.5), &transport), 0.0, 0.1) < 0.25 * msStart);
}

TEST (envelope_legato_inverted_retriggers_on_legato_notes)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapEnvBeats));
    const auto pluck = [] (bool inverted)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "master.polyphony", 1.0f);
        setByKey (*e, "master.legato", 1.0f);
        // env 2 is a pluck (sustain 0) that opens the level
        setByKey (*e, "env2.attack", 1.0f);
        setByKey (*e, "env2.decay", 80.0f);
        setByKey (*e, "env2.sustain", 0.0f);
        setByKey (*e, "env2.legato_inverted", inverted ? 1.0f : 0.0f);
        route (*e, 1, "env2", "osc1.level", 100.0f);
        prepare (*e);
        const auto out = renderWith (*e, { noteOn (0, 60), noteOn (frames (0.5), 64) }, frames (0.8));
        return std::make_pair (rmsBetween (out, 0.005, 0.04), rmsBetween (out, 0.505, 0.54));
    };
    const auto normal = pluck (false), inverted = pluck (true);
    std::printf ("    pluck at the first note %.5f / %.5f, at the legato note %.5f / %.5f (normal / inverted)\n", normal.first, inverted.first, normal.second,
                 inverted.second);
    CHECK (normal.first > 0.02 && inverted.first > 0.02);
    CHECK (normal.second < 0.004); // legato: the envelope is not retriggered
    CHECK (inverted.second > 0.02);
}

TEST (velocity_and_note_curves_and_their_legato_glide)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapCurves));
    const auto level = [] (bool curve)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        route (*e, 1, "velocity", "osc1.level", 100.0f);
        prepare (*e);
        if (curve)
        {
            StateContent content;
            content.velocityCurve = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }; // every velocity reads as 1
            e->setContent (content);
        }
        return rmsBetween (renderWith (*e, { noteOn (0, 69, 0.3f) }, frames (0.4)), 0.1, 0.4);
    };
    const double identity = level (false), flat = level (true);
    std::printf ("    velocity 0.3 read through the identity curve %.5f, through a curve that maps everything to 1: %.5f\n", identity, flat);
    CHECK (flat > 3.0 * identity);

    // note curve: a descending curve makes a high note quiet and a low note loud
    const auto note = [] (int key, bool curve)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        route (*e, 1, "note", "osc1.level", 100.0f);
        prepare (*e);
        if (curve)
        {
            StateContent content;
            content.noteCurve = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            e->setContent (content);
        }
        return rmsBetween (renderWith (*e, { noteOn (0, key) }, frames (0.4)), 0.1, 0.4);
    };
    const double highPlain = note (100, false), highCurved = note (100, true), lowPlain = note (30, false), lowCurved = note (30, true);
    std::printf ("    note 100: plain %.5f, inverted curve %.5f; note 30: plain %.5f, inverted curve %.5f\n", highPlain, highCurved, lowPlain, lowCurved);
    CHECK (highPlain > 3.0 * lowPlain);
    CHECK (lowCurved > 3.0 * highCurved);

    // legato glide: a second, louder legato note (velocity 1 after 0.1) reaches its level over the portamento time
    const auto glide = [] (bool on)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "master.polyphony", 1.0f);
        setByKey (*e, "master.legato", 1.0f);
        setByKey (*e, "master.portamento.time", 400.0f);
        setByKey (*e, "curve.velocity.legato", on ? 1.0f : 0.0f);
        route (*e, 1, "velocity", "osc1.level", 100.0f);
        prepare (*e);
        const auto out = renderWith (*e, { noteOn (0, 60, 0.1f), noteOn (frames (0.3), 64, 1.0f) }, frames (1.4));
        return std::make_pair (rmsBetween (out, 0.31, 0.35), rmsBetween (out, 1.1, 1.3));
    };
    const auto jump = glide (false), smooth = glide (true);
    std::printf ("    just after the legato note %.5f (no glide) / %.5f (glide); settled %.5f / %.5f\n", jump.first, smooth.first, jump.second, smooth.second);
    CHECK (jump.first > 2.0 * smooth.first);
    CHECK (smooth.second > 0.7 * jump.second); // it gets there
}

TEST (custom_lfo_shapes_and_point_modulation)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapCustomLfoShape));
    const auto custom = [] (float y, bool withMod, float macro)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "lfo1.shape", 12.0f); // Custom
        setByKey (*e, "lfo1.rate", 3.0f);
        setByKey (*e, "lfo1.smooth", 0.0f);
        setByKey (*e, "macro.1", macro);
        route (*e, 1, "lfo1", "osc1.level", 100.0f);
        prepare (*e);
        StateContent content;
        ContentLfoShape shape;
        shape.lfo = 1;
        shape.points = { { 0.0f, y, 0.0f }, { 1.0f, y, 0.0f } };
        if (withMod)
        {
            shape.points[0].modY = { true, "macro1", 1.0f };
            shape.points[1].modY = { true, "macro1", 1.0f };
        }
        content.lfoShapes.push_back (shape);
        e->setContent (content);
        return renderWith (*e, { noteOn (0, 69) }, frames (0.8));
    };

    // a constant custom shape is a constant level; its height is the level
    const auto high = custom (1.0f, false, 0.0f), low = custom (0.5f, false, 0.0f);
    const double h1 = rmsBetween (high, 0.05, 0.1), h2 = rmsBetween (high, 0.5, 0.55), l1 = rmsBetween (low, 0.05, 0.1);
    std::printf ("    constant custom shapes: y = 1 -> %.5f / %.5f (two moments), y = 0.5 -> %.5f\n", h1, h2, l1);
    CHECK (h1 > 0.02);
    CHECK_NEAR (h1, h2, 0.05 * h1);
    CHECK (l1 < 0.6 * h1 && l1 > 0.05 * h1);

    // without content the Custom shape falls back to the sine (a swinging level, not the constant)
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "lfo1.shape", 12.0f);
        setByKey (*e, "lfo1.rate", 3.0f);
        setByKey (*e, "lfo1.smooth", 0.0f);
        route (*e, 1, "lfo1", "osc1.level", 100.0f);
        prepare (*e);
        const auto out = renderWith (*e, { noteOn (0, 69) }, frames (0.8));
        double lo = 1e9, hi = 0.0;
        for (double t = 0.05; t < 0.7; t += 0.02)
        {
            lo = std::min (lo, rmsBetween (out, t, t + 0.02));
            hi = std::max (hi, rmsBetween (out, t, t + 0.02));
        }
        CHECK (hi > 2.0 * lo);
    }

    // point modulation: the height of the shape follows macro 1 (a global source), also while the note sounds
    const auto closed = custom (0.0f, true, 0.0f), open = custom (0.0f, true, 100.0f);
    expectOpens (rmsBetween (closed, 0.1, 0.8), rmsBetween (open, 0.1, 0.8), "point modulation by macro 1");
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "lfo1.shape", 12.0f);
        setByKey (*e, "lfo1.smooth", 0.0f);
        route (*e, 1, "lfo1", "osc1.level", 100.0f);
        route (*e, 2, "mod_wheel", "macro.1", 100.0f);
        prepare (*e);
        StateContent content;
        ContentLfoShape shape;
        shape.lfo = 1;
        shape.points = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
        shape.points[0].modY = { true, "macro1", 1.0f };
        shape.points[1].modY = { true, "macro1", 1.0f };
        content.lfoShapes.push_back (shape);
        e->setContent (content);
        const auto out = renderWith (*e, { noteOn (0, 69), event (Event::ModWheel, frames (0.3), 0, 0, 1.0f) }, frames (0.7));
        expectOpens (rmsBetween (out, 0.1, 0.29), rmsBetween (out, 0.45, 0.7), "the mod wheel moves a point through macro 1 during the note");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// WP3
// ---------------------------------------------------------------------------------------------------------------------------------
TEST (key_scale_conforms_notes_before_the_allocator)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapKeyScale));
    const auto play = [] (int key, const char* type, const char* mode, int scaleKey)
    {
        auto e = makeVital();
        sine (*e, 60.0f);
        setByKey (*e, "master.scale.type", choiceValue ("master.scale.type", type));
        setByKey (*e, "master.scale.mode", choiceValue ("master.scale.mode", mode));
        setByKey (*e, "master.scale.key", static_cast<float> (scaleKey));
        prepare (*e);
        const auto out = renderWith (*e, { noteOn (0, key), noteOff (frames (0.9), key) }, frames (1.4));
        CHECK_EQ (e->activeVoices(), 0); // the note off finds the conformed note
        return pitchOf (out, 440.0 * std::pow (2.0, (key - 69) / 12.0));
    };
    const auto hz = [] (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); };

    // C major: C# (61) is out; the tie between C (60) and D (62) goes down
    CHECK_NEAR (play (61, "Major", "Conform", 0), hz (60), 0.5);
    // F# (66) in C major: F (65) and G (67) are equally near: down
    CHECK_NEAR (play (66, "Major", "Conform", 0), hz (65), 0.5);
    // in-scale notes are untouched
    CHECK_NEAR (play (64, "Major", "Conform", 0), hz (64), 0.5);
    // the key transposes the scale: D major has C# (61) but not C (60), which goes to the nearer of B (59) and C# (61): down to B
    CHECK_NEAR (play (61, "Major", "Conform", 2), hz (61), 0.5);
    CHECK_NEAR (play (60, "Major", "Conform", 2), hz (59), 0.5);
    // Mark only and Chromatic change nothing
    CHECK_NEAR (play (61, "Major", "Mark only", 0), hz (61), 0.5);
    CHECK_NEAR (play (61, "Chromatic (off)", "Conform", 0), hz (61), 0.5);
    // a pentatonic scale conforms further: D# (63) in C major pentatonic goes to D (62)
    CHECK_NEAR (play (63, "Major Pentatonic", "Conform", 0), hz (62), 0.5);
}

TEST (tuning_concert_pitch_scales_user_file_and_mts_esp)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapTuning));
    const auto hz = [] (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); };
    const auto play = [] (std::unique_ptr<IEngine>& e, int key, double guess, const std::vector<Event>& extra = {})
    {
        std::vector<Event> events = { noteOn (0, key) };
        events.insert (events.end(), extra.begin(), extra.end());
        return pitchOf (renderWith (*e, events, frames (1.2)), guess);
    };
    const auto engine = [] (const std::function<void (IEngine&)>& setup)
    {
        auto e = makeVital();
        sine (*e, 60.0f);
        setup (*e);
        prepare (*e);
        return e;
    };

    // concert pitch
    auto a432 = engine ([] (IEngine& e) { setByKey (e, "master.tuning.a4", 432.0f); });
    auto a440 = engine ([] (IEngine&) {});
    const double f432 = play (a432, 69, 432.0), f440 = play (a440, 69, 440.0);
    std::printf ("    A4 = 432 -> %.2f Hz, A4 = 440 -> %.2f Hz\n", f432, f440);
    CHECK_NEAR (f432, 432.0, 0.3);
    CHECK_NEAR (f440, 440.0, 0.3);
    {
        auto c = engine ([] (IEngine& e) { setByKey (e, "master.tuning.a4", 466.0f); });
        CHECK_NEAR (play (c, 60, hz (60) * 466.0 / 440.0), hz (60) * 466.0 / 440.0, 0.5);
    }

    // the built-in scales: C# (key 61) in 12-TET, Pythagorean (C4 = 440 / (27/16), C# = 256/243) and just intonation (16/15 over C4 = 264)
    const double tet = hz (61), pythagorean = 440.0 / (27.0 / 16.0) * 256.0 / 243.0, just = 264.0 * 16.0 / 15.0;
    auto pyth = engine ([] (IEngine& e) { setByKey (e, "master.tuning.scale", choiceValue ("master.tuning.scale", "Pythagorean")); });
    auto ji = engine ([] (IEngine& e) { setByKey (e, "master.tuning.scale", choiceValue ("master.tuning.scale", "Just intonation")); });
    const double fp = play (pyth, 61, tet), fj = play (ji, 61, tet);
    std::printf ("    key 61: 12-TET %.2f, Pythagorean %.2f (expected %.2f), just %.2f (expected %.2f)\n", tet, fp, pythagorean, fj, just);
    CHECK_NEAR (fp, pythagorean, 0.35);
    CHECK_NEAR (fj, just, 0.35);
    CHECK (std::fabs (fj - tet) > 2.0);

    // the A4 stays at 440 in a scale (the tuning is anchored there)
    CHECK_NEAR (play (pyth, 69, 440.0), 440.0, 0.3);

    // a user file: 24 equal divisions of the octave (50 cents a step) from content.tuning; key 70 is 50 cents above A4
    {
        auto user = engine ([] (IEngine& e) { setByKey (e, "master.tuning.scale", choiceValue ("master.tuning.scale", "User file")); });
        StateContent content;
        content.tuning.present = true;
        content.tuning.scl = "! 24edo.scl\n24 tone equal temperament\n 24\n";
        for (int i = 1; i <= 24; ++i)
            content.tuning.scl += " " + std::to_string (50 * i) + ".0\n";
        user->setContent (content);
        const double f = play (user, 70, hz (70));
        std::printf ("    24-EDO user scale, key 70: %.2f Hz (A4 + 50 cents = %.2f)\n", f, 440.0 * std::pow (2.0, 50.0 / 1200.0));
        CHECK_NEAR (f, 440.0 * std::pow (2.0, 50.0 / 1200.0), 0.5);

        // an unparsable file falls back to 12-TET
        StateContent broken;
        broken.tuning.present = true;
        broken.tuning.scl = "this is not a scale";
        user->setContent (broken);
        CHECK_NEAR (play (user, 70, hz (70)), hz (70), 0.5);

        // a keyboard mapping with an unmapped key ("x"): that key does not sound
        tuning::Tuning mapped;
        CHECK (mapped.loadScl ("! 12.scl\ntwelve\n 12\n 100.0\n 200.0\n 300.0\n 400.0\n 500.0\n 600.0\n 700.0\n 800.0\n 900.0\n 1000.0\n 1100.0\n 1200.0\n").ok());
        tuning::KeyboardMapping mapping = tuning::KeyboardMapping::defaultMapping (12);
        mapping.mapSize = 12;
        mapping.keys.assign (12, 0);
        for (int i = 0; i < 12; ++i)
            mapping.keys[static_cast<size_t> (i)] = i;
        mapping.keys[1] = tuning::kUnmappedKey; // C#
        mapped.setMapping (mapping);
        content.tuning.present = true;
        content.tuning.scl = mapped.toScl();
        content.tuning.hasKbm = true;
        content.tuning.kbm = mapped.toKbm();
        CHECK (mapped.table()[61] == 0.0);
        tuning::Tuning reparsed;
        const bool sclOk = reparsed.loadScl (content.tuning.scl).ok(), kbmOk = reparsed.loadKbm (content.tuning.kbm).ok();
        std::printf ("    .kbm round trip: scl %d, kbm %d, key 61 -> %.3f Hz, key 62 -> %.3f Hz\n", sclOk ? 1 : 0, kbmOk ? 1 : 0, reparsed.table()[61], reparsed.table()[62]);
        auto mappedEngine = engine ([] (IEngine& e) { setByKey (e, "master.tuning.scale", choiceValue ("master.tuning.scale", "User file")); });
        mappedEngine->setContent (content);
        auto muted = renderWith (*mappedEngine, { noteOn (0, 61) }, frames (0.4));
        mappedEngine->reset();
        auto sounding = renderWith (*mappedEngine, { noteOn (0, 62) }, frames (0.4));
        std::printf ("    .kbm with an unmapped key: key 61 rms %.5f, key 62 rms %.5f\n", rmsBetween (muted, 0.2, 0.4), rmsBetween (sounding, 0.2, 0.4));
        CHECK_EQ (sbtest::peak (muted.left, static_cast<size_t> (0.1 * kSr)), 0.0f);
        CHECK (rmsBetween (sounding, 0.2, 0.4) > 0.02);
    }

    // MTS-ESP: a master that is 50 cents sharp overrides the scale and the concert pitch while it is connected
    struct Master final : tuning::ITuningSource
    {
        bool connected = true;
        double cents = 50.0;
        bool isConnected() const noexcept override { return connected; }
        double frequencyForKey (int key) const noexcept override { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0 + cents / 1200.0); }
        bool query128Keys (std::array<double, tuning::kNumKeys>& out) const noexcept override
        {
            if (! connected)
                return false;
            for (int k = 0; k < tuning::kNumKeys; ++k)
                out[static_cast<size_t> (k)] = frequencyForKey (k);
            return true;
        }
    } master;
    {
        auto mts = engine ([] (IEngine& e) { setByKey (e, "master.tuning.mts_esp", 1.0f); setByKey (e, "master.tuning.a4", 432.0f); });
        mts->setTuningSource (&master);
        const double sharp = play (mts, 69, 453.0);
        std::printf ("    MTS-ESP master +50 cents: A4 sounds at %.2f Hz\n", sharp);
        CHECK_NEAR (sharp, 440.0 * std::pow (2.0, 50.0 / 1200.0), 0.4);

        // no master connected: the engine falls back to the scale and the concert pitch
        master.connected = false;
        CHECK_NEAR (play (mts, 69, 432.0), 432.0, 0.3);
        master.connected = true;

        // the master moves while a note sounds: followed unless "note on only"
        for (const bool noteOnOnly : { false, true })
        {
            master.cents = 0.0;
            auto live = engine ([&] (IEngine& e) { setByKey (e, "master.tuning.mts_esp", 1.0f); setByKey (e, "master.tuning.mts_note_on_only", noteOnOnly ? 1.0f : 0.0f); });
            live->setTuningSource (&master);
            std::vector<Event> events = { noteOn (0, 69) };
            auto first = renderWith (*live, events, frames (0.6));
            master.cents = 100.0; // a semitone up
            auto second = renderWith (*live, {}, frames (1.4));
            const double before = pitchOf (first, 440.0, 0.2), after = pitchOf (second, 466.0, 0.5);
            std::printf ("    MTS-ESP master moves 100 cents (note on only %d): %.2f -> %.2f Hz\n", noteOnOnly ? 1 : 0, before, after);
            CHECK_NEAR (before, 440.0, 0.4);
            if (noteOnOnly)
                CHECK_NEAR (after, 440.0, 1.0);
            else
                CHECK_NEAR (after, 466.16, 1.0);
            live->setTuningSource (nullptr);
        }
        mts->setTuningSource (nullptr);
    }
}

TEST (mpe_per_note_bend_pressure_and_slide)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapMpe));
    const auto hz = [] (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); };
    const auto setup = [] (bool mpe, float memberRange)
    {
        auto e = makeVital();
        sine (*e, 60.0f);
        setByKey (*e, "master.mpe.on", mpe ? 1.0f : 0.0f);
        setByKey (*e, "master.mpe.bend_range", memberRange);
        prepare (*e);
        return e;
    };

    // a member channel's wheel: master.mpe.bend_range with MPE on, master.bend_range (2) without
    for (const bool mpe : { false, true })
    {
        auto e = setup (mpe, 12.0f);
        const auto out = renderWith (*e, { noteOn (0, 69, 1.0f, 1), sbtest::pitchBend (frames (0.1), 0.5f, 1) }, frames (1.0));
        const double f = pitchOf (out, mpe ? hz (75) : hz (70), 0.4);
        std::printf ("    wheel +0.5 on a member channel, MPE %s: %.2f Hz\n", mpe ? "on" : "off", f);
        CHECK_NEAR (f, mpe ? hz (75) : hz (70), 0.5);
    }

    // two members bend their own note, the manager channel moves both
    {
        auto e = setup (true, 24.0f);
        const auto out = renderWith (*e, { noteOn (0, 60, 1.0f, 1), noteOn (0, 67, 1.0f, 2), sbtest::pitchBend (frames (0.05), 0.5f, 1), sbtest::pitchBend (frames (0.05), -0.25f, 2) },
                                     frames (1.0));
        const auto spectrum = sbtest::powerSpectrum (out.left, static_cast<size_t> (0.3 * kSr), 32768, 131072);
        const double a = sbtest::peakFrequency (spectrum, 131072, kSr, hz (72)), b = sbtest::peakFrequency (spectrum, 131072, kSr, hz (61));
        std::printf ("    channel 1: key 60 +12 -> %.2f Hz (expected %.2f), channel 2: key 67 -6 -> %.2f Hz (expected %.2f)\n", a, hz (72), b, hz (61));
        CHECK_NEAR (a, hz (72), 1.0);
        CHECK_NEAR (b, hz (61), 1.0);

        auto m = setup (true, 24.0f);
        const auto moved = renderWith (*m, { noteOn (0, 69, 1.0f, 3), sbtest::pitchBend (frames (0.05), 1.0f, 0) }, frames (1.0)); // the manager: master.bend_range 2
        CHECK_NEAR (pitchOf (moved, hz (71), 0.4), hz (71), 0.5);
    }

    // channel pressure of a member channel is that note's pressure (poly aftertouch) with MPE on, and CC 74 its slide
    for (const bool mpe : { false, true })
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "master.mpe.on", mpe ? 1.0f : 0.0f);
        route (*e, 1, "poly_aftertouch", "osc1.level", 100.0f);
        prepare (*e);
        const auto out = renderWith (*e, { noteOn (0, 69, 1.0f, 2), event (Event::Aftertouch, frames (0.3), 2, 0, 1.0f) }, frames (0.7));
        const double after = rmsBetween (out, 0.4, 0.7);
        std::printf ("    member channel pressure, MPE %s: poly aftertouch drives the level to rms %.5f\n", mpe ? "on" : "off", after);
        if (mpe)
            CHECK (after > 0.02);
        else
            CHECK (after < 0.004);
    }
    {
        auto e = makeVital();
        sine (*e, 0.0f);
        setByKey (*e, "master.mpe.on", 1.0f);
        route (*e, 1, "slide", "osc1.level", 100.0f);
        prepare (*e);
        // CC 74 of the second note's channel only: the first note (channel 1) stays closed
        const auto out = renderWith (*e, { noteOn (0, 60, 1.0f, 1), event (Event::NoteExpression, frames (0.2), 2, -1, 1.0f, Event::Brightness) }, frames (0.5));
        CHECK (rmsBetween (out, 0.3, 0.5) < 0.004);
        auto f = makeVital();
        sine (*f, 0.0f);
        setByKey (*f, "master.mpe.on", 1.0f);
        route (*f, 1, "slide", "osc1.level", 100.0f);
        prepare (*f);
        const auto same = renderWith (*f, { noteOn (0, 60, 1.0f, 2), event (Event::NoteExpression, frames (0.2), 2, -1, 1.0f, Event::Brightness) }, frames (0.5));
        CHECK (rmsBetween (same, 0.3, 0.5) > 0.02);
    }
}

TEST (oscillator_mapping_gates_folds_and_keeps_modulating)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapOscMapping));
    const auto hz = [] (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); };
    const auto setup = [] (const std::function<void (IEngine&)>& extra)
    {
        auto e = makeVital();
        sine (*e, 60.0f);
        extra (*e);
        prepare (*e);
        return e;
    };

    // key range 60 .. 72: C3 (48) is outside and silent, F#4 (66) inside
    {
        auto e = setup ([] (IEngine& x) { setByKey (x, "osc1.map.key_low", 60.0f); setByKey (x, "osc1.map.key_high", 72.0f); });
        const double outside = rmsBetween (renderWith (*e, { noteOn (0, 48) }, frames (0.4)), 0.1, 0.4);
        e->reset();
        const double inside = rmsBetween (renderWith (*e, { noteOn (0, 66) }, frames (0.4)), 0.1, 0.4);
        expectOpens (outside, inside, "key range 60..72, key 48 / 66");

        // low > high: the engine swaps them
        auto s = setup ([] (IEngine& x) { setByKey (x, "osc1.map.key_low", 72.0f); setByKey (x, "osc1.map.key_high", 60.0f); });
        CHECK (rmsBetween (renderWith (*s, { noteOn (0, 66) }, frames (0.4)), 0.1, 0.4) > 0.02);
    }

    // velocity range 64 .. 127
    {
        auto e = setup ([] (IEngine& x) { setByKey (x, "osc1.map.vel_low", 64.0f); });
        const double soft = rmsBetween (renderWith (*e, { noteOn (0, 69, 0.2f) }, frames (0.4)), 0.1, 0.4);
        e->reset();
        const double hard = rmsBetween (renderWith (*e, { noteOn (0, 69, 0.9f) }, frames (0.4)), 0.1, 0.4);
        expectOpens (soft, hard, "velocity range 64..127, velocity 0.2 / 0.9");
    }

    // key fold: notes outside the range sound folded by whole octaves into it
    {
        auto e = setup ([] (IEngine& x) { setByKey (x, "osc1.map.key_low", 60.0f); setByKey (x, "osc1.map.key_high", 71.0f); setByKey (x, "osc1.map.key_fold", 1.0f); });
        const double f48 = pitchOf (renderWith (*e, { noteOn (0, 48) }, frames (1.2)), hz (60));
        e->reset();
        const double f86 = pitchOf (renderWith (*e, { noteOn (0, 86) }, frames (1.2)), hz (62)); // 86 - 24 = 62
        std::printf ("    key fold into 60..71: key 48 -> %.2f Hz (C4 = %.2f), key 86 -> %.2f Hz (D4 = %.2f)\n", f48, hz (60), f86, hz (62));
        CHECK_NEAR (f48, hz (60), 0.5);
        CHECK_NEAR (f86, hz (62), 0.7);

        // a range narrower than an octave cannot fold: the note is silent
        auto narrow = setup ([] (IEngine& x) { setByKey (x, "osc1.map.key_low", 60.0f); setByKey (x, "osc1.map.key_high", 65.0f); setByKey (x, "osc1.map.key_fold", 1.0f); });
        CHECK (rmsBetween (renderWith (*narrow, { noteOn (0, 48) }, frames (0.4)), 0.1, 0.4) < 0.004);
    }

    // key warp: the silent oscillator still runs as the FM source of osc 2
    {
        const auto fm = [&] (bool warp)
        {
            auto e = setup ([&] (IEngine& x)
            {
                setByKey (x, "osc1.map.key_low", 100.0f);
                setByKey (x, "osc1.map.key_high", 110.0f);
                setByKey (x, "osc1.map.key_warp", warp ? 1.0f : 0.0f);
                setByKey (x, "osc2.on", 1.0f);
                setByKey (x, "osc2.wavetable", 0.0f);
                setByKey (x, "osc2.level", 60.0f);
                setByKey (x, "osc2.unison.voices", 1.0f);
                setByKey (x, "osc2.warp.type", choiceValue ("osc2.warp.type", "FM from Osc 1"));
                setByKey (x, "osc2.warp.amount", 100.0f);
            });
            const auto out = renderWith (*e, { noteOn (0, 57) }, frames (0.6));
            return sbtest::centroidOf (out.left, static_cast<size_t> (0.2 * kSr), 8192, kSr);
        };
        const double plain = fm (false), modulated = fm (true);
        std::printf ("    osc 2 spectral centroid: osc 1 muted %.1f Hz, muted but warping %.1f Hz\n", plain, modulated);
        CHECK (modulated > 1.5 * plain);
    }

    // the defaults change nothing: a note anywhere sounds
    {
        auto e = setup ([] (IEngine&) {});
        CHECK (rmsBetween (renderWith (*e, { noteOn (0, 1), noteOn (0, 127, 0.01f) }, frames (0.3)), 0.1, 0.3) > 0.02);
    }
}

TEST (set_content_from_another_thread_while_the_engine_plays)
{
    // the message thread replaces the content over and over while the audio thread renders: no crash, no non-finite audio, and the last
    // content set is the one that sounds (a constant custom LFO shape of height 1 opens the level)
    auto e = makeVital();
    sine (*e, 0.0f);
    setByKey (*e, "lfo1.shape", 12.0f);
    setByKey (*e, "lfo1.smooth", 0.0f);
    route (*e, 1, "lfo1", "osc1.level", 100.0f);
    prepare (*e);

    const auto content = [] (float height)
    {
        StateContent c;
        ContentLfoShape shape;
        shape.lfo = 1;
        shape.points = { { 0.0f, height, 0.0f }, { 1.0f, height, 0.0f } };
        c.lfoShapes.push_back (shape);
        c.velocityCurve = { { 0.0f, 0.0f, 0.0f }, { 1.0f, height, 0.0f } };
        c.tuning.present = true;
        c.tuning.scl = "! x\nx\n 1\n 2/1\n";
        return c;
    };
    std::atomic<bool> stop { false };
    std::thread writer ([&]
    {
        for (int i = 0; ! stop.load(); ++i)
            e->setContent (content (static_cast<float> (i % 10) / 10.0f));
    });
    const auto during = renderWith (*e, { noteOn (0, 69) }, frames (1.0));
    stop.store (true);
    writer.join();
    CHECK (sbtest::allFinite (during.left));

    e->setContent (content (1.0f));
    const auto after = renderWith (*e, {}, frames (0.3));
    CHECK (rmsBetween (after, 0.1, 0.3) > 0.02);
}
