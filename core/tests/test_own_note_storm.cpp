// SynthyBoy - own engine tests: no note ever stays stuck. A chord held and struck again (both orders of the note-off of the first strike and the note-on of the next), for every
// SAME NOTE x STEAL mode and several polyphony limits, at the tier low (economy: the 3 ms steal ramp) and the tier high; and a seeded storm of random note-ons, note-offs, pedal
// and All Notes Off. After every event the engine's own idea of the held keys is compared with what the events say, and no voice may sustain without a held key or a pedal (the
// bug state: VoiceStats::orphans). At the end everything is released: the voice count is 0 and the output silent.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <vector>

#include "synthyboy/engine.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;

struct Rig
{
    std::unique_ptr<IEngine> e;
    std::set<int> held;        // the keys the events say are down
    bool pedal = false;
    std::vector<float> l = std::vector<float> (256), r = std::vector<float> (256);
    float tailPeak = 0.0f;

    Rig (Tier tier, int polyphony, int sameNote, int steal)
    {
        e = createOwnEngine();
        auto set = [&] (const char* key, float v)
        {
            const ParamInfo* p = findParamByKey (key);
            CHECK (p != nullptr);
            if (p != nullptr)
                e->setParam (p->id, v);
        };
        set ("master.polyphony", static_cast<float> (polyphony));
        e->prepare (kSr, 256, qualityForTier (tier));
        set ("master.polyphony", static_cast<float> (polyphony));
        set ("master.voice.same_note", static_cast<float> (sameNote));
        set ("master.voice.steal_priority", static_cast<float> (steal));
        set ("osc1.wavetable", 1);
        set ("filter1.on", 0);
        set ("env1.attack", 30);
        set ("env1.decay", 50);
        set ("env1.sustain", 100);
        set ("env1.release", 200);
        set ("master.volume", -6);
    }

    /** One event in a block of 64 samples; the invariants are checked after it. `exactHeld`: the held set of the engine equals the one of the events. */
    void send (const Event& ev, bool exactHeld = true)
    {
        e->process (&ev, 1, l.data(), r.data(), 64);
        if (ev.type == Event::NoteOn)
            held.insert (ev.key);
        else if (ev.type == Event::NoteOff)
            held.erase (ev.key);
        else if (ev.type == Event::AllNotesOff)
            held.clear();
        else if (ev.type == Event::SustainPedal)
            pedal = ev.value >= 0.5f;
        check (exactHeld);
    }

    void run (double seconds)
    {
        for (int i = 0; i < static_cast<int>(seconds * kSr / 256.0); ++i)
        {
            e->process (nullptr, 0, l.data(), r.data(), 256);
            check (true);
        }
    }

    void check (bool exactHeld)
    {
        VoiceStats s;
        CHECK (e->voiceStats (s));
        CHECK_EQ (s.orphans, 0); // no voice sustains without a held key or the pedal
        CHECK (s.sustaining <= s.active);
        if (exactHeld)
        {
            CHECK_EQ (s.heldKeys, static_cast<int> (held.size()));
            CHECK_EQ (s.lowestHeld, held.empty() ? -1 : *held.begin());
            CHECK_EQ (s.highestHeld, held.empty() ? -1 : *held.rbegin());
        }
        CHECK_EQ (s.sustainPedal, pedal);
    }

    void endsSilent (const char* what)
    {
        run (0.8);
        VoiceStats s;
        e->voiceStats (s);
        float peak = 0.0f;
        for (int i = 0; i < 20; ++i)
        {
            e->process (nullptr, 0, l.data(), r.data(), 256);
            for (float x : l) peak = std::max (peak, std::fabs (x));
            for (float x : r) peak = std::max (peak, std::fabs (x));
        }
        if (!(s.active == 0 && e->activeVoices() == 0 && peak < 1.0e-6f && s.heldKeys == 0))
            std::printf ("    NOT SILENT (%s): voices %d, held keys %d, peak %g\n", what, s.active, s.heldKeys, static_cast<double> (peak));
        CHECK_EQ (s.active, 0);
        CHECK_EQ (e->activeVoices(), 0);
        CHECK (peak < 1.0e-6f);
        CHECK_EQ (s.heldKeys, 0);
    }
};

const int kChord[8] = { 48, 52, 55, 59, 62, 65, 69, 72 };
} // namespace

// the owner's sequence: eight keys held, the chord struck again five times, then everything released
TEST (own_chord_struck_again_leaves_nothing_stuck)
{
    for (Tier tier : { Tier::low, Tier::high })
        for (int poly : { 1, 2, 8, 16 })
            for (int sameNote : { 0, 1, 2 })
                for (int steal : { 0, 3 })
                    for (int order = 0; order < 2; ++order)
                    {
                        Rig rig (tier, poly, sameNote, steal);
                        for (int k : kChord)
                            rig.send (sbtest::noteOn (0, k, 0.8f));
                        rig.run (0.3);
                        for (int strike = 0; strike < 5; ++strike)
                        {
                            if (order == 0) // the note-off of the old strike first, then the new note-on (a keyboard)
                                for (int k : kChord)
                                {
                                    rig.send (sbtest::noteOff (0, k));
                                    rig.send (sbtest::noteOn (0, k, 0.7f));
                                }
                            else            // the new note-on first, the note-off of the old strike after it (a pad source)
                            {
                                for (int k : kChord)
                                    rig.send (sbtest::noteOn (0, k, 0.7f));
                                for (int k : kChord)
                                    rig.send (sbtest::noteOff (0, k));
                                for (int k : kChord)
                                    rig.send (sbtest::noteOn (0, k, 0.7f)); // the keys stay down after the strike
                            }
                            rig.run (0.06);
                        }
                        for (int k : kChord)
                            rig.send (sbtest::noteOff (0, k));
                        char what[96];
                        std::snprintf (what, sizeof what, "tier %d poly %d same %d steal %d order %d", static_cast<int> (tier), poly, sameNote, steal, order);
                        rig.endsSilent (what);
                    }
}

// a seeded storm of note-ons, note-offs, pedal moves and All Notes Off (CC 123); the invariants hold after every event and the end is silent
TEST (own_note_storm_ends_silent_with_no_stuck_voice)
{
    for (Tier tier : { Tier::low, Tier::high })
        for (int poly : { 2, 8, 16 })
            for (int sameNote : { 0, 1 })
                for (uint64_t seed : { 0x5eed1ull, 0x5eed2ull, 0x5eed3ull })
                {
                    Rig rig (tier, poly, sameNote, static_cast<int>(seed % 5));
                    sbtest::Rng rng (seed + static_cast<uint64_t>(poly) * 977u);
                    for (int i = 0; i < 800; ++i)
                    {
                        const double x = rng.uniform();
                        const int key = 40 + static_cast<int>(rng.uniform() * 30.0);
                        if (x < 0.45)
                            rig.send (sbtest::noteOn (0, key, 0.2f + 0.8f * static_cast<float>(rng.uniform())));
                        else if (x < 0.85)
                            rig.send (sbtest::noteOff (0, key));
                        else if (x < 0.90)
                        {
                            Event p = sbtest::noteOn (0, 0, rng.uniform() < 0.5 ? 1.0f : 0.0f);
                            p.type = Event::SustainPedal;
                            p.channel = -1;
                            rig.send (p);
                        }
                        else if (x < 0.92)
                            rig.send (sbtest::allNotesOff (0));
                        else
                            rig.run (0.01 + 0.05 * rng.uniform());
                    }
                    // everything up: the keys, the pedal, All Notes Off
                    for (int key = 0; key < 128; ++key)
                        if (rig.held.count (key) != 0)
                            rig.send (sbtest::noteOff (0, key));
                    Event up = sbtest::noteOn (0, 0, 0.0f);
                    up.type = Event::SustainPedal;
                    up.channel = -1;
                    rig.send (up);
                    rig.send (sbtest::allNotesOff (0));
                    char what[96];
                    std::snprintf (what, sizeof what, "storm tier %d poly %d same %d seed %llx", static_cast<int> (tier), poly, sameNote, static_cast<unsigned long long>(seed));
                    rig.endsSilent (what);
                }
}

// CC 120 (All Sound Off) silences at once, through the short ramp and not the 200 ms release; CC 123 releases
TEST (own_all_sound_off_silences_at_once)
{
    for (Tier tier : { Tier::low, Tier::high })
    {
        Rig rig (tier, 8, 0, 0);
        for (int k : kChord)
            rig.send (sbtest::noteOn (0, k, 0.8f));
        rig.run (0.3);
        Event off = sbtest::allNotesOff (0);
        off.value = 1.0f; // translateMidiMessage gives CC 120 this value
        rig.send (off);
        rig.run (0.06); // the 50 ms kill fade is over, the 200 ms release would not be
        VoiceStats s;
        rig.e->voiceStats (s);
        CHECK_EQ (s.active, 0);
        CHECK_EQ (s.heldKeys, 0);
    }
}

// a pedal hold of a voice that was stolen does not carry over to the next note on that voice: the held key keeps sounding when the pedal goes up
TEST (own_stale_pedal_hold_does_not_release_a_held_key)
{
    for (Tier tier : { Tier::low, Tier::high })
        for (int poly : { 1, 2 })
        {
            Rig rig (tier, poly, 0, 0);
            Event down = sbtest::noteOn (0, 0, 1.0f);
            down.type = Event::SustainPedal;
            down.channel = -1;
            rig.send (down);
            for (int i = 0; i < 6; ++i)
            {
                rig.send (sbtest::noteOn (0, 50 + i, 0.8f));
                rig.send (sbtest::noteOff (0, 50 + i)); // held by the pedal only
                rig.run (0.05);
            }
            rig.send (sbtest::noteOn (0, 80, 0.8f)); // a key that stays down
            rig.run (0.1);
            Event up = down;
            up.value = 0.0f;
            rig.send (up);
            rig.run (0.1);
            VoiceStats s;
            rig.e->voiceStats (s);
            CHECK (s.sustaining >= 1); // the voice of key 80 is not released by a pedal hold it never had
            CHECK_EQ (s.orphans, 0);
        }
}
