// SynthyBoy - core/spectral tests: the start ledger (the admission of the Spectral voices' start work over the blocks).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The expected grant ticks are those of the cost model of docs/plans/spectral-start.md section 3.4 (89 us a frame, 67 us a shaping) at the
// budget of 2400 us a block that the device measurement fixed (cold factor 0.94, fixed cold penalty 0.070 ms); a loop over the pattern table
// gave them, the engine is not involved. Tick = one control period (32 samples), quad = 4 ticks = one 128 frame host block.

#include "synthyboy/spectral/start_ledger.h"

#include <cstdint>
#include <vector>

#include "test_runner.h"

using namespace synthyboy::spectral;

namespace
{
constexpr int kBudget = 2400;

// the mono patterns of the start by offset (spectral-start.md section 1.2: 156 = 1 frame + 1 shaping, 245 = 2 frames + 1 shaping)
StartPattern patternOf (int offset)
{
    StartPattern p {};
    switch (offset)
    {
        case 0:   p.us[0] = 156; p.us[1] = 156; p.us[4] = 156; break;
        case 128: p.us[0] = 245; p.us[3] = 156; break;
        case 256: p.us[0] = 245; p.us[2] = 156; break;
        default:  p.us[0] = 156; p.us[1] = 245; break;   // 384
    }
    return p;
}

struct Voice
{
    uint64_t key = 0;
    StartPattern pattern {};
    bool dead = false;
    int grant = -1;      // tick of the grant, relative to the tick of the strike
};

int tickNow (const StartLedger& l) { return static_cast<int> (l.tickForTest()) - 1; }

/** The voices of a chord struck at `strikeTick`: `notes` notes of `U` unison voices from the serial `first`, the offsets ((serial * U + u) mod 4) * 128.
    `limit` > 0 is the polyphony: the note n >= limit gives up the note n - limit before its own requests (the Kill burst), with `cancel` it also tells the ledger. */
std::vector<Voice> runChord (StartLedger& l, int notes, int U, int first, int phi, int limit = 0, bool cancel = true)
{
    for (int i = 0; i <= phi; ++i)
        l.beginTick();
    const int t0 = tickNow (l);
    std::vector<Voice> vs;
    auto requestPending = [&] {
        for (auto& v : vs)
            if (v.grant < 0 && ! v.dead && l.request (v.key, v.pattern))
                v.grant = tickNow (l) - t0;
    };
    for (int n = 0; n < notes; ++n)
    {
        const uint32_t serial = static_cast<uint32_t> (first + n);
        if (limit > 0 && n >= limit)
        {
            if (cancel)
                l.cancelSerial (serial - static_cast<uint32_t> (limit));
            for (auto& v : vs)
                if ((v.key >> 4) == serial - static_cast<uint32_t> (limit))
                    v.dead = true;
        }
        for (int u = 0; u < U; ++u)
        {
            Voice v;
            v.key = startKey (serial, 0, u);
            v.pattern = patternOf (static_cast<int> ((serial * static_cast<uint32_t> (U) + static_cast<uint32_t> (u)) % 4u) * 128);
            vs.push_back (v);
            if (l.request (v.key, v.pattern))
                vs.back().grant = tickNow (l) - t0;
        }
    }
    for (int i = 0; i < 300; ++i)
    {
        l.beginTick();
        requestPending();
    }
    return vs;
}

std::vector<int> grantTicks (const std::vector<Voice>& vs, bool survivorsOnly = false)
{
    std::vector<int> t;
    for (const auto& v : vs)
        if (v.grant >= 0 && ! (survivorsOnly && v.dead))
            t.push_back (v.grant);
    return t;
}

StartLedger makeLedger()
{
    StartLedger l;
    l.configure (true, kBudget);
    return l;
}

std::vector<int> repeatTicks (int count, int value) { return std::vector<int> (static_cast<size_t> (count), value); }

std::vector<int> join (std::vector<int> a, const std::vector<int>& b)
{
    a.insert (a.end(), b.begin(), b.end());
    return a;
}
} // namespace

static_assert (sizeof (StartLedger) <= 6144, "the ledger lives once in the engine's Spectral scratch: at most 6 KB");

TEST (start_ledger_disabled_grants_every_request_and_stores_nothing)
{
    StartLedger l;                       // disabled by default
    CHECK (! l.enabledForTest());
    StartPattern big {};
    big.us[0] = 60000;
    int granted = 0;
    for (uint32_t i = 0; i < 100; ++i)
    {
        l.beginTick();
        if (l.request (startKey (i, 0, 0), big))
            ++granted;
    }
    CHECK_EQ (granted, 100);
    CHECK_EQ (l.waitingForTest(), 0);
    CHECK_EQ (l.grantedNowForTest() + l.grantedLaterForTest(), 0u);
    CHECK_EQ (l.maxBookedForTest(), 0u);
    l.configure (true, kBudget);
    CHECK (l.enabledForTest());
    l.configure (false, kBudget);
    CHECK (! l.enabledForTest());
    CHECK (l.request (startKey (1, 0, 0), big));
}

TEST (start_ledger_chord_grant_ticks)
{
    struct Row { int notes, U; std::vector<int> expect; };
    const std::vector<Row> rows = {
        { 1, 4, repeatTicks (4, 0) },                                     // a single four voice note: slot 0 = 802
        { 3, 1, repeatTicks (3, 0) },
        { 3, 2, repeatTicks (6, 0) },                                     // 1203
        { 4, 2, repeatTicks (8, 0) },                                     // 1604: the chord of the three Spectral presets starts whole
        { 8, 1, repeatTicks (8, 0) },
        { 8, 2, join (repeatTicks (11, 0), repeatTicks (5, 4)) },         // 11 voices fit (2250 us), the last 5 start in the next quad
        { 3, 4, join (repeatTicks (11, 0), { 4 }) },
    };
    for (const auto& row : rows)
    {
        StartLedger l = makeLedger();
        const auto vs = runChord (l, row.notes, row.U, 0, 0);
        CHECK (grantTicks (vs) == row.expect);
        CHECK (l.maxBookedForTest() <= static_cast<uint32_t> (kBudget));
        CHECK_EQ (l.waitingForTest(), 0);
    }
    {   // the largest sums of the two chords that fit whole
        StartLedger l = makeLedger();
        runChord (l, 4, 2, 0, 0);
        CHECK_EQ (l.maxBookedForTest(), 1604u);
        StartLedger m = makeLedger();
        runChord (m, 1, 4, 0, 0);
        CHECK_EQ (m.maxBookedForTest(), 802u);
    }
    // the first serial moves the offsets, not the number of voices that fit (the 8 x 2 chord, first serials 0 .. 3)
    for (int first = 0; first < 4; ++first)
    {
        StartLedger l = makeLedger();
        const auto vs = runChord (l, 8, 2, first, 0);
        CHECK (grantTicks (vs) == join (repeatTicks (11, 0), repeatTicks (5, 4)));
    }
    // a chord struck phi ticks into its quad waits phi ticks less for the next quad
    for (int phi = 1; phi <= 3; ++phi)
    {
        StartLedger l = makeLedger();
        const auto vs = runChord (l, 8, 2, 0, phi);
        CHECK (grantTicks (vs) == join (repeatTicks (11, 0), repeatTicks (5, 4 - phi)));
    }
}

TEST (start_ledger_serves_the_oldest_first)
{
    StartLedger l;
    l.configure (true, 300);
    l.beginTick();                                       // tick 0
    StartPattern booked {}, a {}, c {};
    booked.us[0] = 245;
    a.us[0] = 156;
    c.us[0] = 10;
    CHECK (l.request (0, booked));                       // slot 0 = 245
    CHECK (! l.request (1, a));                          // 245 + 156 > 300
    CHECK (! l.request (2, c));                          // would fit (255 <= 300) but A is older
    for (int tick = 1; tick <= 3; ++tick)
    {
        l.beginTick();
        CHECK (! l.request (1, a));
        CHECK (! l.request (2, c));
    }
    l.beginTick();                                       // tick 4: the quad turns, slot of the new quad is empty
    CHECK (l.request (1, a));
    CHECK (l.request (2, c));
    CHECK_EQ (l.waitingForTest(), 0);
}

TEST (start_ledger_a_voice_that_is_not_renewed_is_dropped)
{
    StartPattern blocker {};
    for (auto& us : blocker.us)
        us = 900;
    StartLedger l;
    l.configure (true, 810);
    l.beginTick();
    CHECK (l.request (0, blocker));                      // an empty ledger always grants: slots of the quads 0 .. 4 = 900
    CHECK (! l.request (1, blocker));
    CHECK_EQ (l.waitingForTest(), 1);
    l.beginTick();
    CHECK_EQ (l.waitingForTest(), 1);                    // renewed one period ago
    l.beginTick();                                       // not renewed in the previous period: its voice died
    CHECK_EQ (l.waitingForTest(), 0);

    StartLedger m;
    m.configure (true, 810);
    m.beginTick();
    CHECK (m.request (0, blocker));
    CHECK (! m.request (1, blocker));
    for (int i = 0; i < 10; ++i)
    {
        m.beginTick();
        CHECK (! m.request (1, blocker));                // the slots stay booked until quad 5 (tick 20)
    }
    CHECK_EQ (m.waitingForTest(), 1);
}

TEST (start_ledger_cancel_gives_back_the_future_bookings)
{
    // 16 notes of 2 voices, polyphony 4 (Kill): the survivors are the notes 12 .. 15 (the last 8 voices)
    {
        StartLedger l = makeLedger();
        const auto vs = runChord (l, 16, 2, 0, 0, 4, true);
        CHECK (grantTicks (vs, true) == repeatTicks (8, 4));          // the survivors start in the next quad
        int latest = 0;
        for (const auto& v : vs)
            if (! v.dead && v.grant > latest)
                latest = v.grant;
        CHECK_EQ (latest, 4);
        CHECK_EQ (l.waitingForTest(), 0);
        CHECK (l.maxBookedForTest() <= static_cast<uint32_t> (kBudget));
    }
    {   // the control: the same without cancelSerial: the work booked for the given up voices holds the survivors back a quad longer
        StartLedger l = makeLedger();
        const auto vs = runChord (l, 16, 2, 0, 0, 4, false);
        CHECK (grantTicks (vs, true) == join (repeatTicks (6, 4), repeatTicks (2, 8)));
    }
}

TEST (start_ledger_an_empty_ledger_always_grants_and_a_full_registry_fails_open)
{
    StartPattern big {};
    big.us[0] = 900;
    StartLedger l;
    l.configure (true, 810);
    l.beginTick();
    CHECK (l.request (0, big));                          // nothing booked: granted although 900 > 810 (progress)
    CHECK (! l.request (1, big));                        // now 900 is booked
    CHECK_EQ (l.maxBookedForTest(), 900u);

    StartLedger f;
    f.configure (true, 810);
    f.beginTick();
    CHECK (f.request (0, big));
    for (uint64_t k = 1; k <= 128; ++k)
        CHECK (! f.request (k, big));
    CHECK_EQ (f.waitingForTest(), 128);
    CHECK (f.request (129, big));                        // the registry is full: the voice starts (the old behaviour)
    CHECK_EQ (f.waitingForTest(), 128);
}
