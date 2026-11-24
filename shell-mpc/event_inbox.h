// SynthyBoy - MPC shell: the hand-over of the host's MIDI events to the audio thread (effProcessEvents -> processReplacing). A fixed ring: the producer side (whichever thread the host
// calls effProcessEvents from; several producers claim slots lock-free with a compare-exchange on the head, no producer ever waits for another) pushes, the audio thread pops at the start of its block. No lock and no
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
    static_assert (std::atomic<size_t>::is_always_lock_free, "lock-free indices");
public:
    EventInbox() noexcept {
        for (size_t i = 0; i < N; ++i)
            slots[i].seq.store (i, std::memory_order_relaxed);
    }
    /** Any thread, lock-free (a bounded MPSC ring with a sequence word per slot: a producer stalled or preempted inside its push never blocks another producer or the audio thread).
        `essential`: a note-off (still accepted when the ring is nearly full). False when the event was dropped. */
    bool push (const E& e, bool essential) noexcept {
        size_t h = head.load (std::memory_order_relaxed);
        for (;;) {
            Slot& s = slots[h & (N - 1)];
            const size_t seq = s.seq.load (std::memory_order_acquire);
            const std::ptrdiff_t diff = static_cast<std::ptrdiff_t> (seq - h);
            if (diff == 0) {
                if (! essential && (h - tail.load (std::memory_order_acquire)) >= N - kReserve) {
                    dropped.fetch_add (1u, std::memory_order_relaxed);
                    return false;
                }
                if (head.compare_exchange_weak (h, h + 1, std::memory_order_relaxed)) {
                    s.data = e;
                    s.seq.store (h + 1, std::memory_order_release);
                    return true;
                }
                // h was reloaded by the failed exchange
            } else if (diff < 0) {
                dropped.fetch_add (1u, std::memory_order_relaxed);   // the ring is full
                return false;
            } else
                h = head.load (std::memory_order_relaxed);
        }
    }
    /** The audio thread only: the next event, false when there is none. */
    bool pop (E& out) noexcept {
        const size_t t = tail.load (std::memory_order_relaxed);
        Slot& s = slots[t & (N - 1)];
        if (s.seq.load (std::memory_order_acquire) != t + 1)
            return false;
        out = s.data;
        tail.store (t + 1, std::memory_order_release);
        s.seq.store (t + N, std::memory_order_release);
        return true;
    }
    size_t size() const noexcept { return head.load (std::memory_order_acquire) - tail.load (std::memory_order_acquire); }
    unsigned droppedCount() const noexcept { return dropped.load (std::memory_order_relaxed); }

private:
    static constexpr size_t kReserve = 256;   // the last slots are for note-offs
    struct Slot { std::atomic<size_t> seq { 0 }; E data {}; };
    Slot slots[N];
    std::atomic<size_t> head { 0 }, tail { 0 };
    std::atomic<unsigned> dropped { 0 };
};

} // namespace synthyboy_mpc
