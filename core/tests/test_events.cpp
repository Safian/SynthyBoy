// SynthyBoy - core tests: the event contract (WP1 of docs/ui-design/13-new-keys.md): MIDI translation, sustain pedal, note expression,
// note-off velocity. The engine checks run against every engine that reports kCapEventContract and assert an audible difference.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/midi_events.h"
#include "test_dsp.h"
#include "test_engines.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;

Event sustain (int frame, bool down, int channel = 0)
{
    Event e = noteOn (frame, 0, down ? 1.0f : 0.0f, channel);
    e.type = Event::SustainPedal;
    return e;
}

Event expression (int frame, Event::Expression kind, float value, int key, int channel = 0, int noteId = -1)
{
    Event e = noteOn (frame, key, value, channel, noteId);
    e.type = Event::NoteExpression;
    e.expression = kind;
    return e;
}

void sineSetup (IEngine& e)
{
    setByKey (e, "osc1.wavetable", 0.0f); // Sine
    setByKey (e, "filter1.on", 0.0f);
}
} // namespace

TEST (midi_translation)
{
    Event e;
    const uint8_t noteOnMsg[] = { 0x93, 60, 127 };
    CHECK (translateMidiMessage (noteOnMsg, 3, 17, e));
    CHECK_EQ (e.type, Event::NoteOn);
    CHECK_EQ (e.channel, 3);
    CHECK_EQ (e.key, 60);
    CHECK_EQ (e.frame, 17);
    CHECK_NEAR (e.value, 1.0, 1e-6);

    const uint8_t noteOnZero[] = { 0x90, 61, 0 };
    CHECK (translateMidiMessage (noteOnZero, 3, 0, e));
    CHECK_EQ (e.type, Event::NoteOff);
    CHECK_EQ (e.value, 0.0f);

    const uint8_t noteOffMsg[] = { 0x82, 62, 64 };
    CHECK (translateMidiMessage (noteOffMsg, 3, 0, e));
    CHECK_EQ (e.type, Event::NoteOff);
    CHECK_EQ (e.channel, 2);
    CHECK_NEAR (e.value, 64.0 / 127.0, 1e-6); // the release velocity travels in the note-off event

    const uint8_t pedalDown[] = { 0xB0, 64, 127 }, pedalHalf[] = { 0xB1, 64, 63 }, pedalEdge[] = { 0xB0, 64, 64 };
    CHECK (translateMidiMessage (pedalDown, 3, 0, e));
    CHECK_EQ (e.type, Event::SustainPedal);
    CHECK_EQ (e.value, 1.0f);
    CHECK (translateMidiMessage (pedalHalf, 3, 0, e));
    CHECK_EQ (e.value, 0.0f);
    CHECK_EQ (e.channel, 1);
    CHECK (translateMidiMessage (pedalEdge, 3, 0, e));
    CHECK_EQ (e.value, 1.0f);

    const uint8_t slideMsg[] = { 0xB4, 74, 127 };
    CHECK (translateMidiMessage (slideMsg, 3, 0, e));
    CHECK_EQ (e.type, Event::NoteExpression);
    CHECK_EQ (e.expression, Event::Brightness);
    CHECK_EQ (e.key, -1);
    CHECK_EQ (e.channel, 4);

    const uint8_t polyPressure[] = { 0xA2, 70, 100 };
    CHECK (translateMidiMessage (polyPressure, 3, 0, e));
    CHECK_EQ (e.type, Event::NoteExpression);
    CHECK_EQ (e.expression, Event::Pressure);
    CHECK_EQ (e.key, 70);

    const uint8_t channelPressure[] = { 0xD5, 127 };
    CHECK (translateMidiMessage (channelPressure, 2, 0, e));
    CHECK_EQ (e.type, Event::Aftertouch);
    CHECK_EQ (e.channel, 5);

    const uint8_t bendCentre[] = { 0xE0, 0, 64 }, bendMax[] = { 0xE0, 127, 127 }, bendMin[] = { 0xE0, 0, 0 };
    CHECK (translateMidiMessage (bendCentre, 3, 0, e));
    CHECK_EQ (e.value, 0.0f);
    CHECK (translateMidiMessage (bendMax, 3, 0, e));
    CHECK_NEAR (e.value, 8191.0 / 8192.0, 1e-6);
    CHECK (translateMidiMessage (bendMin, 3, 0, e));
    CHECK_EQ (e.value, -1.0f);

    const uint8_t wheel[] = { 0xB0, 1, 127 }, allOff[] = { 0xB2, 123, 0 }, allSound[] = { 0xB2, 120, 0 };
    CHECK (translateMidiMessage (wheel, 3, 0, e));
    CHECK_EQ (e.type, Event::ModWheel);
    CHECK (translateMidiMessage (allOff, 3, 0, e));
    CHECK_EQ (e.type, Event::AllNotesOff);
    CHECK_EQ (e.channel, 2);
    CHECK (translateMidiMessage (allSound, 3, 0, e));
    CHECK_EQ (e.type, Event::AllNotesOff);

    // not translated: unknown controllers, program change, truncated or invalid messages, null input
    const uint8_t cc7[] = { 0xB0, 7, 100 }, program[] = { 0xC0, 5 }, truncated[] = { 0x90, 60 }, data[] = { 0x40, 60, 60 }, sysex[] = { 0xF0, 1, 2 };
    CHECK (! translateMidiMessage (cc7, 3, 0, e));
    CHECK (! translateMidiMessage (program, 2, 0, e));
    CHECK (! translateMidiMessage (truncated, 2, 0, e));
    CHECK (! translateMidiMessage (data, 3, 0, e));
    CHECK (! translateMidiMessage (sysex, 3, 0, e));
    CHECK (! translateMidiMessage (nullptr, 3, 0, e));
    CHECK (! translateMidiMessage (cc7, 1, 0, e));
}

TEST (sustain_pedal_keeps_released_notes_sounding)
{
    for (const auto& c : sbtest::engineCases())
    {
        sbtest::Context ctx (c.name);
        auto probe = sbtest::makeEngine (c);
        CHECK (capsCover (probe->capabilities(), kCapEventContract));

        const int total = static_cast<int> (4.0 * kSr);
        const size_t late = static_cast<size_t> (2.0 * kSr), end = static_cast<size_t> (3.0 * kSr);

        // without the pedal the note is silent two seconds after its note-off
        auto a = sbtest::makeEngine (c);
        sineSetup (*a);
        const auto released = sbtest::render (*a, { noteOn (0, 69), noteOff (static_cast<int> (0.25 * kSr), 69) }, total, 512);
        CHECK (sbtest::allFinite (released.left));
        CHECK_EQ (sbtest::peak (released.left, late, end), 0.0f);
        CHECK_EQ (a->activeVoices(), 0);

        // with the pedal down the note sounds on: the same stream, only the pedal differs
        auto b = sbtest::makeEngine (c);
        sineSetup (*b);
        const auto held = sbtest::render (*b, { sustain (0, true), noteOn (0, 69), noteOff (static_cast<int> (0.25 * kSr), 69) }, total, 512);
        const double heldRms = sbtest::rmsOf (held.left, late, end);
        std::printf ("    %s: pedal down, 2..3 s after the note-off: rms %.4f (no pedal: %.4f)\n", c.name, heldRms, sbtest::rmsOf (released.left, late, end));
        CHECK (heldRms > 0.01);
        CHECK (b->activeVoices() >= 1);

        // lifting the pedal releases it: silent after the tail
        auto d = sbtest::makeEngine (c);
        sineSetup (*d);
        const int lift = static_cast<int> (1.0 * kSr);
        const auto lifted = sbtest::render (*d, { sustain (0, true), noteOn (0, 69), noteOff (static_cast<int> (0.25 * kSr), 69), sustain (lift, false) },
                                            total, 512);
        CHECK (sbtest::rmsOf (lifted.left, static_cast<size_t> (0.5 * kSr), static_cast<size_t> (0.9 * kSr)) > 0.01);
        CHECK_EQ (sbtest::peak (lifted.left, static_cast<size_t> (lift + (c.tailSeconds + 0.1) * kSr), end), 0.0f);
        CHECK_EQ (d->activeVoices(), 0);

        // the pedal is per channel: a pedal on channel 1 does not hold a note of channel 0
        auto f = sbtest::makeEngine (c);
        sineSetup (*f);
        const auto other = sbtest::render (*f, { sustain (0, true, 1), noteOn (0, 69), noteOff (static_cast<int> (0.25 * kSr), 69) }, total, 512);
        CHECK_EQ (sbtest::peak (other.left, late, end), 0.0f);

        // channel < 0 means every channel
        auto g = sbtest::makeEngine (c);
        sineSetup (*g);
        Event all = sustain (0, true);
        all.channel = -1;
        const auto everywhere = sbtest::render (*g, { all, noteOn (0, 69, 1.0f, 5), noteOff (static_cast<int> (0.25 * kSr), 69, 5) }, total, 512);
        CHECK (sbtest::rmsOf (everywhere.left, late, end) > 0.01);
    }
}

TEST (per_note_tuning_expression_bends_one_note)
{
    for (const auto& c : sbtest::engineCases())
    {
        sbtest::Context ctx (c.name);
        auto e = sbtest::makeEngine (c);
        sineSetup (*e);
        // two notes (A4 on channel 0, and 56 Hz-ish E1 far away); the expression moves only the note it addresses, found by its id
        const int bendAt = static_cast<int> (0.5 * kSr);
        const auto out = sbtest::render (*e, { noteOn (0, 69, 1.0f, 0, 7), expression (bendAt, Event::Tuning, 12.0f, -1, 0, 7) },
                                         static_cast<int>(1.5 * kSr), 512);
        CHECK (sbtest::allFinite (out.left));
        const auto before = sbtest::powerSpectrum (out.left, static_cast<size_t> (0.2 * kSr), 16384, 65536);
        const auto after = sbtest::powerSpectrum (out.left, static_cast<size_t> (1.0 * kSr), 16384, 65536);
        const double f0 = sbtest::peakFrequency (before, 65536, kSr, 440.0), f1 = sbtest::peakFrequency (after, 65536, kSr, 880.0);
        std::printf ("    %s: note expression TUNING +12: %.2f Hz -> %.2f Hz\n", c.name, f0, f1);
        CHECK_NEAR (f0, 440.0, 1.5);
        CHECK_NEAR (f1, 880.0, 3.0);

        // addressed by key and channel; another channel's note is not touched
        auto g = sbtest::makeEngine (c);
        sineSetup (*g);
        const auto two = sbtest::render (*g, { noteOn (0, 69, 1.0f, 0), expression (bendAt, Event::Tuning, 12.0f, 69, 3) }, static_cast<int> (1.5 * kSr), 512);
        const auto stay = sbtest::powerSpectrum (two.left, static_cast<size_t> (1.0 * kSr), 16384, 65536);
        CHECK_NEAR (sbtest::peakFrequency (stay, 65536, kSr, 440.0), 440.0, 1.5);
    }
}

TEST (events_with_a_new_type_never_crash_any_engine)
{
    // every engine, also those that do nothing with them: wild values, wildcard keys and channels, unknown ids
    for (const auto& c : sbtest::engineCases())
    {
        sbtest::Context ctx (c.name);
        auto e = sbtest::makeEngine (c);
        sbtest::Rng rng (99);
        std::vector<Event> events;
        for (int i = 0; i < 400; ++i)
        {
            Event ev = noteOn (i * 20, static_cast<int> (rng.u32() % 128), static_cast<float> (rng.uniform()), static_cast<int> (rng.u32() % 16),
                               static_cast<int> (rng.u32() % 9) - 1);
            const uint32_t pick = rng.u32() % 6;
            ev.type = pick == 0 ? Event::SustainPedal : (pick <= 2 ? Event::NoteExpression : (pick == 3 ? Event::NoteOff : Event::NoteOn));
            ev.expression = static_cast<Event::Expression> (rng.u32() % 3);
            ev.value = static_cast<float> (rng.uniform() * 4.0 - 2.0);
            if (rng.u32() % 7 == 0)
                ev.key = -1;
            if (rng.u32() % 31 == 0)
                ev.value = std::nanf ("");
            events.push_back (ev);
        }
        const auto out = sbtest::render (*e, events, 20000, 333);
        CHECK (sbtest::allFinite (out.left));
        CHECK (sbtest::allFinite (out.right));
    }
}
