// SynthyBoy - MPC shell: the hand-over of the host's MIDI events to the audio thread (effProcessEvents -> processReplacing). A fixed ring: the producer side (whichever thread the host
// calls effProcessEvents from; several producers are serialised by a tiny spin flag that the audio thread never touches) pushes, the audio thread pops at the start of its block. No lock and no
// allocation on the audio thread; an event is delivered exactly once and in the order it was pushed, with its sample offset (the offset counts from the start of the block that takes it over).
// When the ring is nearly full only note-offs are still accepted (a lost note-off hangs a note); a full ring drops the new event and counts it.
// Tested by shell-mpc/tests/event_inbox_host.cpp.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>

namespace synthyboy_mpc {

/** E must be trivially copyable and have `bool isNoteOff() const`-like classification through the predicate given to push (keeps this header free of the core's headers). */
template <class E, size_t N = 4096>
class EventInbox {
    static_assert ((N & (N - 1)) == 0, "N is a power of two");
public:
    /** Any thread. `essential`: a note-off (still accepted when the ring is nearly full). False when the event was dropped. */
    bool push (const E& e, bool essential) noexcept {
        while (producers.test_and_set (std::memory_order_acquire)) {}
        const size_t h = head.load (std::memory_order_relaxed), t = tail.load (std::memory_order_acquire);
        const size_t used = h - t;
        const bool ok = used < N - (essential ? 0 : kReserve);
        if (ok) {
            slots[h & (N - 1)] = e;
            head.store (h + 1, std::memory_order_release);
        } else
            dropped.fetch_add (1u, std::memory_order_relaxed);
        producers.clear (std::memory_order_release);
        return ok;
    }
    /** The audio thread only: the next event, false when there is none. */
    bool pop (E& out) noexcept {
        const size_t t = tail.load (std::memory_order_relaxed);
        if (t == head.load (std::memory_order_acquire))
            return false;
        out = slots[t & (N - 1)];
        tail.store (t + 1, std::memory_order_release);
        return true;
    }
    size_t size() const noexcept { return head.load (std::memory_order_acquire) - tail.load (std::memory_order_acquire); }
    unsigned droppedCount() const noexcept { return dropped.load (std::memory_order_relaxed); }

private:
    static constexpr size_t kReserve = 256;   // the last slots are for note-offs
    E slots[N];
    std::atomic<size_t> head { 0 }, tail { 0 };
    std::atomic<unsigned> dropped { 0 };
    std::atomic_flag producers = ATOMIC_FLAG_INIT;
};

} // namespace synthyboy_mpc
