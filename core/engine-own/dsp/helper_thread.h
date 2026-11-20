// SynthyBoy - own engine: what the engine's helper threads need from their surroundings (CPU review T1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// 1. WakeSignal: a coalescing wake-up flag. notify () never blocks, takes no lock and allocates nothing (it may be called from the audio thread: it is one atomic exchange and, only when the waiter
//    sleeps, one non-blocking OS wake); wait () blocks without any timeout until a notify () has happened since the last wait () returned. A notify () that comes before the wait () is not
//    lost (the word stays 1), several notify () calls before one wait () are one wake-up (the waiter must look at its own work list, not count wake-ups).
//    One word with three states: 0 idle, 1 signaled, 2 the waiter sleeps. The OS wake happens only on the change 2 -> 1, so signal and wait pair one to one. Linux: a futex on the word (the
//    kernel sleeps only while it is still 2). Apple: a dispatch semaphore. Windows: a semaphore of maximum count 1.
// 2. The thread start hook (synthyboy/helper_thread_hook.h, public): the engine core does not know the shell. A shell that wants its policy (scheduling class, the cores) on the helper
//    threads of the engine sets a function once, before the engine is prepared; every engine helper thread calls runHelperThreadStartHook () first thing. Without a hook nothing changes.
#pragma once

#include <atomic>
#include <cstdint>

#include "synthyboy/helper_thread_hook.h"

#if defined(__linux__)
 #include <linux/futex.h>
 #include <sys/syscall.h>
 #include <unistd.h>
 #include <cerrno>
#elif defined(__APPLE__)
 #include <dispatch/dispatch.h>
#elif defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #error "WakeSignal needs a non-blocking OS wake (futex, dispatch semaphore or Win32 semaphore) on this platform"
#endif

namespace synthyboy
{
namespace own
{
class WakeSignal
{
public:
    static constexpr bool kNotifyLockFree = true;

    WakeSignal() = default;
    WakeSignal (const WakeSignal&) = delete;
    WakeSignal& operator= (const WakeSignal&) = delete;

    /** Sets the word to 1 and wakes the waiter if it sleeps. Lock free, see the header comment. */
    void notify() noexcept
    {
        if (word.exchange (1u, std::memory_order_acq_rel) == 2u)
            osWake();
    }
    /** Blocks until a notify () has happened, clears it and returns. Without a timeout. One waiting thread at a time. */
    void wait() noexcept
    {
        for (;;)
        {
            uint32_t expected = 1u;
            if (word.compare_exchange_strong (expected, 0u, std::memory_order_acq_rel))
                return;
            expected = 0u;
            if (word.compare_exchange_strong (expected, 2u, std::memory_order_acq_rel))
                osWait();   // returns after the wake (or at once on Linux when the word is no longer 2); the loop looks at the word again
        }
    }

private:
    std::atomic<uint32_t> word { 0u };
    static_assert (sizeof (std::atomic<uint32_t>) == sizeof (uint32_t) && std::atomic<uint32_t>::is_always_lock_free, "the wake word");
#if defined(__linux__)
    void osWake() noexcept
    {
        (void) ::syscall (SYS_futex, reinterpret_cast<uint32_t*> (&word), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
    }
    void osWait() noexcept
    {
        (void) ::syscall (SYS_futex, reinterpret_cast<uint32_t*> (&word), FUTEX_WAIT_PRIVATE, 2u, nullptr, nullptr, 0); // EAGAIN, EINTR: the caller looks at the word again
    }
#elif defined(__APPLE__)
    dispatch_semaphore_t sem = dispatch_semaphore_create (0);
public:
    ~WakeSignal() { dispatch_release (sem); }   // signal and wait are paired one to one, so the count is back at 0 and the release does not trap
private:
    void osWake() noexcept { dispatch_semaphore_signal (sem); }
    void osWait() noexcept { dispatch_semaphore_wait (sem, DISPATCH_TIME_FOREVER); }
#else
    HANDLE sem = CreateSemaphoreW (nullptr, 0, 1, nullptr);
public:
    ~WakeSignal() { CloseHandle (sem); }
private:
    void osWake() noexcept { ReleaseSemaphore (sem, 1, nullptr); }
    void osWait() noexcept { WaitForSingleObject (sem, INFINITE); }
#endif
};

} // namespace own
} // namespace synthyboy
