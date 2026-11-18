// SynthyBoy - own engine: what the engine's helper threads need from their surroundings (CPU review T1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// 1. WakeSignal: a coalescing wake-up flag. notify () never blocks, takes no lock and allocates nothing (it may be called from the audio thread: it is an atomic exchange and, on Linux, one futex
//    wake system call, and only when the flag was clear); wait () blocks without any timeout until a notify () has happened since the last wait () returned. A notify () that comes
//    before the wait () is not lost (the flag stays set), several notify () calls before one wait () are one wake-up (the waiter must look at its own work list, not count wake-ups).
//    Linux: a futex on the flag word (the waiter sleeps only while the word is 0, checked by the kernel atomically, so there is no window for a lost wake-up). Elsewhere (the Mac and Windows
//    builds, tests): a mutex and a condition variable; there notify () takes the mutex for the length of one store, which is the price of the portable form.
// 2. The thread start hook (synthyboy/helper_thread_hook.h, public): the engine core does not know the shell. A shell that wants its policy (scheduling class, the cores) on the helper
//    threads of the engine sets a function once, before the engine is prepared; every engine helper thread calls runHelperThreadStartHook () first thing. Without a hook nothing changes.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "synthyboy/helper_thread_hook.h"

#if defined(__linux__)
 #include <linux/futex.h>
 #include <sys/syscall.h>
 #include <unistd.h>
 #include <cerrno>
#endif

namespace synthyboy
{
namespace own
{
class WakeSignal
{
public:
    /** Sets the flag and wakes the waiter if it sleeps. Lock free on Linux, see the header comment. */
    void notify() noexcept
    {
#if defined(__linux__)
        if (word.exchange (1u, std::memory_order_acq_rel) == 0u)
            futexWake();
#else
        {
            std::lock_guard<std::mutex> lock (mutex);
            flag = true;
        }
        cv.notify_one();
#endif
    }
    /** Blocks until the flag is set, clears it and returns. Without a timeout. One waiting thread at a time. */
    void wait() noexcept
    {
#if defined(__linux__)
        for (;;)
        {
            if (word.exchange (0u, std::memory_order_acq_rel) != 0u)
                return;
            futexWait(); // returns at once when the word is no longer 0 (the kernel checks it atomically), else sleeps until a wake
        }
#else
        std::unique_lock<std::mutex> lock (mutex);
        cv.wait (lock, [this] { return flag; });
        flag = false;
#endif
    }

private:
#if defined(__linux__)
    std::atomic<uint32_t> word { 0u };
    void futexWake() noexcept
    {
        (void) ::syscall (SYS_futex, reinterpret_cast<uint32_t*> (&word), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
    }
    void futexWait() noexcept
    {
        (void) ::syscall (SYS_futex, reinterpret_cast<uint32_t*> (&word), FUTEX_WAIT_PRIVATE, 0u, nullptr, nullptr, 0); // EAGAIN, EINTR: the caller looks at the word again
    }
    static_assert (sizeof (std::atomic<uint32_t>) == sizeof (uint32_t) && std::atomic<uint32_t>::is_always_lock_free, "the futex word");
#else
    std::mutex mutex;
    std::condition_variable cv;
    bool flag = false;
#endif
};

} // namespace own
} // namespace synthyboy
