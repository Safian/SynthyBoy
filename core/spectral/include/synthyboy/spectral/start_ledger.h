// SynthyBoy - core/spectral: the start ledger. It spreads the start work of the Spectral voices over the host blocks (docs/plans/spectral-start.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A Spectral voice's note start makes a few frames and shapings at once and a few later (SpectralVoice::startPattern). A chord of many voices put all of it into one host block. The ledger
// books the warm microseconds of every start into the next five quads (a quad = 4 control periods = one 128 frame host block of the Key 61) and admits a start only while every quad it
// touches stays under the budget; a voice that does not fit asks again at every control period (it outputs silence meanwhile) and starts at the first period at which it fits, in strict
// first come first served order (note serial, oscillator, unison voice). It is a function of the control period count and of the requests, nothing else: no clock, no thread state, no host block
// size. Fixed arrays and integers only (no allocation, no lock, no system call). Disabled (the default): request () returns true and nothing is stored, the start is the one of before.
#pragma once

#include <algorithm>
#include <cstdint>

namespace synthyboy
{
namespace spectral
{
constexpr int kStartTicksPerQuad = 4;
constexpr int kStartQuads = 5;           // a start books at most the 5 quads from its own
constexpr int kStartFrameUs = 89;        // warm us of one synthesis frame on the Key 61 (docs/engine-spec/07 section 4a)
constexpr int kStartShapeUs = 67;        // warm us of one spectral shaping
/** Warm us of start work per quad and block. 1700 since 2026-11-21: the interleaved budget sweep on the Key 61 (2400 / 1700 / 2000 / 1400 us; results in
    .hw-local/mpc-perf/noteon-burst/budget-sweep-results.md) put 10 of 12 Spectral strike rows under 1.5 blocks at 1700 against 0 of 12 at 2400, the steady state unchanged. 8 voices
    (4 notes of unison 2, 1604 us) and a stereo single note of unison 4 still start whole; of a larger chord 8 voices start at once, the others in the next quads (tier low only). */
constexpr int kStartBudgetUs = 1700;

/** The warm us that the start work of one voice puts into the quads 0 .. 4 counted from the quad of its start tick. */
struct StartPattern
{
    uint16_t us[kStartQuads];
};

/** The key that orders the requests: the note serial of the engine, the oscillator (0 .. 2), the unison voice (0 .. 3). */
constexpr uint64_t startKey (uint32_t serial, int osc, int u) noexcept
{
    return (static_cast<uint64_t> (serial) << 4) | (static_cast<uint64_t> (osc) << 2) | static_cast<uint64_t> (u);
}

class StartLedger
{
public:
    /** Switches the ledger and sets the budget; a change of either resets everything. Called when the quality is applied, never per block. */
    void configure (bool enabled, int budgetUs) noexcept
    {
        if (enabled == enabled_ && budgetUs == budget_)
            return;
        enabled_ = enabled;
        budget_ = budgetUs;
        reset();
    }

    void reset() noexcept
    {
        ticks_ = 0;
        for (auto& s : ring_)
            s = 0;
        waiting_ = 0;
        grantCount_ = 0;
        grantHead_ = 0;
        now_ = later_ = 0;
        maxWait_ = maxBooked_ = 0;
    }

    /** Once per control period, before the voices tick: advances the tick count, frees the quad that ended, drops the entries whose voice did not ask in the previous period (it died),
        grants the waiting entries in key order for as long as they fit. Does nothing when disabled. */
    void beginTick() noexcept
    {
        if (! enabled_)
            return;
        ++ticks_;
        const uint32_t t = ticks_ - 1u;
        if (t % kStartTicksPerQuad == 0 && t > 0)
            ring_[((t / kStartTicksPerQuad) - 1u) % kRing] = 0;
        int out = 0;
        for (int i = 0; i < waiting_; ++i)           // an entry asked in the previous period or in this one: its voice lives
            if (t - reg_[i].seen <= 1u)
                reg_[out++] = reg_[i];
        waiting_ = out;
        for (int i = 0; i < waiting_; ++i)
        {
            Entry& e = reg_[i];
            if (e.granted)
                continue;
            if (! fits (t / kStartTicksPerQuad, e.pattern))
                break;                               // strict first come first served
            book (e.key, e.pattern, e.first);
            e.granted = true;
        }
    }

    /** The voice of `key` asks to start in this control period. True: it starts now (its work is booked). False: it waits, and must ask again in the next period. */
    bool request (uint64_t key, const StartPattern& pattern) noexcept
    {
        if (! enabled_)
            return true;
        const uint32_t t = tickNow();
        for (int i = 0; i < waiting_; ++i)
            if (reg_[i].key == key)
            {
                reg_[i].seen = t;
                if (! reg_[i].granted)
                    return false;
                for (int j = i; j + 1 < waiting_; ++j)
                    reg_[j] = reg_[j + 1];
                --waiting_;
                return true;
            }
        bool olderWaits = false;
        for (int i = 0; i < waiting_ && reg_[i].key < key; ++i)
            if (! reg_[i].granted)
                olderWaits = true;
        if (! olderWaits && fits (t / kStartTicksPerQuad, pattern))
        {
            book (key, pattern, t);
            return true;
        }
        if (waiting_ >= kRegistry)
            return true;                             // fail open: never block the sound
        int at = waiting_;
        while (at > 0 && reg_[at - 1].key > key)
        {
            reg_[at] = reg_[at - 1];
            --at;
        }
        Entry& e = reg_[at];
        e.key = key;
        e.pattern = pattern;
        e.seen = t;
        e.first = t;
        e.granted = false;
        ++waiting_;
        return false;
    }

    /** The note `serial` is given up (the polyphony limit): its waiting entries go, and the bookings of its already granted voices in the quads after the current one are given back (the
        work of the current quad was made at the note-on). */
    void cancelSerial (uint32_t serial) noexcept
    {
        if (! enabled_)
            return;
        int out = 0;
        for (int i = 0; i < waiting_; ++i)
            if (static_cast<uint32_t> (reg_[i].key >> 4) != serial)
                reg_[out++] = reg_[i];
        waiting_ = out;
        const uint32_t nowQuad = tickNow() / kStartTicksPerQuad;
        const int n = grantCount_ < kGrants ? grantCount_ : kGrants;
        for (int k = 0; k < n; ++k)
        {
            Grant& g = grants_[k];
            if (! g.valid || static_cast<uint32_t> (g.key >> 4) != serial)
                continue;
            for (int i = 0; i < kStartQuads; ++i)
                if (g.quad + static_cast<uint32_t> (i) > nowQuad)
                {
                    uint32_t& slot = ring_[(g.quad + static_cast<uint32_t> (i)) % kRing];
                    slot = slot > static_cast<uint32_t> (g.pattern.us[i]) ? slot - static_cast<uint32_t> (g.pattern.us[i]) : 0u;
                }
            g.valid = false;
        }
    }

    // test hooks
    uint32_t tickForTest() const noexcept { return ticks_; }
    int waitingForTest() const noexcept { return waiting_; }
    uint32_t grantedNowForTest() const noexcept { return now_; }         // granted in the period of the first request
    uint32_t grantedLaterForTest() const noexcept { return later_; }     // granted after waiting
    uint32_t maxWaitTicksForTest() const noexcept { return maxWait_; }   // the largest (grant tick - first request tick)
    uint32_t maxBookedForTest() const noexcept { return maxBooked_; }    // the largest slot value after any booking
    bool enabledForTest() const noexcept { return enabled_; }

private:
    static constexpr int kRing = 8;
    static constexpr int kRegistry = 128;
    static constexpr int kGrants = 32;

    struct Entry
    {
        uint64_t key;
        StartPattern pattern;
        uint32_t seen;       // the tick of the last request
        uint32_t first;      // the tick of the first request
        bool granted;
    };
    struct Grant
    {
        uint64_t key;
        uint32_t quad;
        StartPattern pattern;
        bool valid;
    };

    uint32_t tickNow() const noexcept { return ticks_ > 0 ? ticks_ - 1u : 0u; }

    bool fits (uint32_t quad, const StartPattern& p) const noexcept
    {
        bool empty = true;
        bool ok = true;
        for (int i = 0; i < kStartQuads; ++i)
        {
            const uint32_t booked = ring_[(quad + static_cast<uint32_t> (i)) % kRing];
            if (booked != 0)
                empty = false;
            if (booked + static_cast<uint32_t> (p.us[i]) > static_cast<uint32_t> (budget_))
                ok = false;
        }
        return ok || empty;                          // a voice always makes progress, whatever the budget is
    }

    void book (uint64_t key, const StartPattern& p, uint32_t first) noexcept
    {
        const uint32_t t = tickNow();
        const uint32_t quad = t / kStartTicksPerQuad;
        for (int i = 0; i < kStartQuads; ++i)
        {
            uint32_t& slot = ring_[(quad + static_cast<uint32_t> (i)) % kRing];
            slot += static_cast<uint32_t> (p.us[i]);
            maxBooked_ = std::max (maxBooked_, slot);
        }
        Grant& g = grants_[grantHead_];
        g.key = key;
        g.quad = quad;
        g.pattern = p;
        g.valid = true;
        grantHead_ = (grantHead_ + 1) % kGrants;
        if (grantCount_ < kGrants)
            ++grantCount_;
        const uint32_t wait = t - first;
        if (wait == 0)
            ++now_;
        else
            ++later_;
        maxWait_ = std::max (maxWait_, wait);
    }

    bool enabled_ = false;
    int budget_ = kStartBudgetUs;
    uint32_t ticks_ = 0;                             // the number of beginTick () calls: the current period is ticks_ - 1
    uint32_t ring_[kRing] = {};
    Entry reg_[kRegistry] = {};
    int waiting_ = 0;
    Grant grants_[kGrants] = {};
    int grantHead_ = 0, grantCount_ = 0;
    uint32_t now_ = 0, later_ = 0, maxWait_ = 0, maxBooked_ = 0;
};
} // namespace spectral
} // namespace synthyboy
