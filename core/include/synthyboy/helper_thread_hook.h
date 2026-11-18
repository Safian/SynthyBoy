// SynthyBoy - the policy hook of the engine's helper threads (CPU review T1). The engine core does not know the shell; a shell that wants a scheduling class and a core mask on the
// helper threads the engine starts (the effect module builder, the Convolve job thread) sets one function here once, before the first engine is prepared. Each helper thread calls it
// first thing, on its own thread, with its name (at most 15 characters). Without a hook a helper thread keeps what it inherits from its creator, as before.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>

namespace synthyboy
{
using HelperThreadStartHook = void (*) (const char* threadName);

inline std::atomic<HelperThreadStartHook>& helperThreadStartHookSlot() noexcept
{
    static std::atomic<HelperThreadStartHook> slot { nullptr };
    return slot;
}
/** Process wide; null removes the hook. */
inline void setHelperThreadStartHook (HelperThreadStartHook hook) noexcept { helperThreadStartHookSlot().store (hook, std::memory_order_release); }
/** Called by an engine helper thread first thing, on that thread. */
inline void runHelperThreadStartHook (const char* threadName) noexcept
{
    if (HelperThreadStartHook h = helperThreadStartHookSlot().load (std::memory_order_acquire))
        h (threadName);
}
} // namespace synthyboy
