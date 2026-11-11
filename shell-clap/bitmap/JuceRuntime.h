// SynthyBoy - bitmap GUI: the JUCE runtime of this module (message manager, embedded fonts, the pump of the toolkit's queue).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A foreign host has no JUCE message loop. This module links its own JUCE (8.0.17, hidden symbols), so the toolkit is initialised lazily by the first GUI and torn down with the module.
// Every entry point of the extension calls enter () first: it makes the calling thread the message thread (a host may call from a different "main" thread between sessions, never from
// two at once) and, unless the host runs the platform event loop itself, pump () dispatches the toolkit's own queue (timers, async updates). Nothing here touches the host's windows.
#pragma once

namespace synthyboy::bitmap
{
class JuceRuntime
{
public:
    /** Main thread. Starts the toolkit when this is the first GUI of the module; counts the GUIs. */
    static void acquire();
    /** Main thread. A GUI is gone. */
    static void release() noexcept;
    /** Module unload: shuts the toolkit down when no GUI is left (otherwise it stays: a host that unloads with GUIs alive loses the process wide singletons with the module). */
    static void shutdown() noexcept;

    /** Every entry point: the calling thread is the message thread from here on. */
    static void enter() noexcept;
    /** Dispatches what the toolkit has queued for the message thread (timers, async calls) without waiting; bounded. Only for hosts that do not run the platform loop. */
    static void pump() noexcept;

    static int activeSessions() noexcept;
};
} // namespace synthyboy::bitmap
