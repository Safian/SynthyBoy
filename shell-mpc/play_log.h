// SynthyBoy - MPC shell (overlay build only): the debug log of the host's calls that the Play page depends on (docs/MPC.md "Play page", "Project save and reload").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ONLY WITH SYNTHYBOY_MPC_DEBUG_HOOKS (CMake option of the same name, default OFF, never in the package): without it every function below is an empty inline one and the plugin has no trace of the log,
// of its switch file or of its dump (robustness review F8: a leftover file in /tmp must not make setParameter do file I/O on the host's audio thread).
//
// With the option: off unless the file /tmp/synthyboy-play-log exists (environment variable SYNTHYBOY_MPC_PLAY_LOG_FILE changes the path; the display thread looks at it on every tick, so creating or deleting the
// file switches the log on and off while the app runs). Then every call of effSetChunk, effGetChunk, effSetProgram, setParameter and incoming MIDI note-on / note-off is logged as one line
//     <counter> <milliseconds since the first line> <what> <arguments> [-> what the plugin did]
//     (for note events: <counter> <ms> note_on channel <1-16> note <num> vel <host_vel> engine <engine_vel> dropped <count>)
// together with the number of getParameter calls since the previous line. The thread that logs (the host's audio thread included) only FORMATS the line into a fixed slot of a lock free ring (no allocation,
// no lock, no file): the display thread writes the ring out to /tmp/synthyboy-play-log.txt (the switch file's own name plus ".txt") once a tick (drain ()). A full ring drops the line and counts it; the file
// stops growing at 512 KB (the last line says so). A request file "<log path>-dump" makes the display thread write the state of the instance to "<log path>-dump.txt" and delete the request.
// The chunks effGetChunk / effSetChunk handle are handed over the same way (offerChunk ()): the display thread writes "<log path>-chunk-get.json" / "-chunk-set.json".
#pragma once

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#ifndef SYNTHYBOY_MPC_DEBUG_HOOKS
#define SYNTHYBOY_MPC_DEBUG_HOOKS 0
#endif

#if SYNTHYBOY_MPC_DEBUG_HOOKS
#include "safe_file.h"
#endif

namespace synthyboy_mpc {

#if SYNTHYBOY_MPC_DEBUG_HOOKS

class PlayLog
{
public:
    static constexpr long kMaxBytes = 512 * 1024;
    static constexpr size_t kSlots = 4096;       // lines the ring holds between two ticks of the display thread
    static constexpr size_t kLineBytes = 192;    // a line longer than this is cut

    static const std::string& path()
    {
        static const std::string p = [] { const char* e = std::getenv ("SYNTHYBOY_MPC_PLAY_LOG_FILE"); return std::string (e != nullptr && *e != 0 ? e : "/tmp/synthyboy-play-log"); }();
        return p;
    }
    static bool exists (const std::string& p) { return safefile::regularFileSize (p.c_str()) >= 0; }

    /** true while the switch file exists, as of the last refresh() (any thread, a relaxed atomic read: nothing here touches the file system). */
    static bool enabled() noexcept { return on().load (std::memory_order_relaxed); }
    /** Looks at the switch file (the display thread every tick, and a new instance once): the log goes on and off within a tick while the app runs. */
    static void refresh() noexcept
    {
        (void) ring();   // the ring is made here (the host's loading thread), never by the first line () of an audio thread
        on().store (exists (path()), std::memory_order_relaxed);
    }

    /** FNV-1a of a chunk: the lines of a save and of the following load can be compared by eye. */
    static unsigned hash (const void* data, size_t size) noexcept
    {
        unsigned h = 2166136261u;
        for (size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<unsigned char> (static_cast<const char*> (data)[i])) * 16777619u;
        return h;
    }

    /** Counts a getParameter call (the next line reports the count). */
    static void countGet() noexcept { gets().fetch_add (1, std::memory_order_relaxed); }

    /** Any thread, the audio thread included: formats the line and puts it into the ring. No allocation, no lock, no file. */
    static void line (const char* fmt, ...) noexcept __attribute__ ((format (printf, 1, 2)))
    {
        if (! enabled())
            return;
        size_t pos = 0;
        Slot* s = claim (pos);
        if (s == nullptr)
        {
            dropped().fetch_add (1, std::memory_order_relaxed);
            droppedTotal().fetch_add (1, std::memory_order_relaxed);
            return;
        }
        va_list ap;
        va_start (ap, fmt);
        std::vsnprintf (s->text, kLineBytes, fmt, ap);
        va_end (ap);
        s->ms = nowMs();
        s->gets = gets().exchange (0, std::memory_order_relaxed);
        s->seq.store (pos + 1, std::memory_order_release);   // published: the display thread may take it
    }

    /** Incoming MIDI note event (NoteOn / NoteOff). Lock-free, no allocation, no file I/O. */
    static void note (bool isNoteOn, int channel, int noteNumber, int hostVelocity, int engineVelocity) noexcept
    {
        if (! enabled())
            return;
        const long drop = droppedTotal().load (std::memory_order_relaxed);
        line ("%s channel %d note %d vel %d engine %d dropped %ld",
              isNoteOn ? "note_on" : "note_off",
              channel, noteNumber, hostVelocity, engineVelocity, drop);
    }

    /** The display thread, every tick: the lines of the ring go to the file ("<log path>.txt"), the chunks that were offered to their files. */
    static void drain()
    {
        std::lock_guard<std::mutex> g (mutex());
        State& st = state();
        for (;;)
        {
            const size_t tail = st.tail;
            Slot& s = ring().s[tail % kSlots];
            if (s.seq.load (std::memory_order_acquire) != tail + 1)
                break;
            if (! st.full)
            {
                if (st.first < 0)
                    st.first = s.ms;
                const std::string file = path() + ".txt";
                std::string text;
                char head[96];
                if (s.gets > 0)
                {
                    std::snprintf (head, sizeof head, "%lld %lld getParameter x%ld\n", ++st.counter, s.ms - st.first, s.gets);
                    text += head;
                }
                std::snprintf (head, sizeof head, "%lld %lld ", ++st.counter, s.ms - st.first);
                text += head;
                text += s.text;
                text += '\n';
                if (safefile::regularFileSize (file.c_str()) > kMaxBytes)
                {
                    const std::string last = "-- log full (" + std::to_string (kMaxBytes) + " bytes), nothing more is written --\n";
                    safefile::appendSmall (file.c_str(), last.data(), last.size());
                    st.full = true;
                }
                else
                    safefile::appendSmall (file.c_str(), text.data(), text.size());
            }
            st.tail = tail + 1;
            s.seq.store (tail + kSlots, std::memory_order_release);   // free for the next lap
        }
        const long lost = dropped().exchange (0, std::memory_order_relaxed);
        if (lost > 0 && ! st.full)
        {
            const std::string note = "-- " + std::to_string (lost) + " lines lost: the display thread did not write them out fast enough --\n";
            safefile::appendSmall ((path() + ".txt").c_str(), note.data(), note.size());
        }
        writeChunk ("-chunk-get.json", chunkGet());
        writeChunk ("-chunk-set.json", chunkSet());
    }

    /** The chunk of effGetChunk (`get`) or effSetChunk, handed to the display thread: it is copied here (the host thread allocates; effGetChunk / effSetChunk are not audio calls), written on the next tick. */
    static void offerChunk (bool get, const void* data, size_t size)
    {
        if (! enabled() || data == nullptr)
            return;
        std::lock_guard<std::mutex> g (chunkMutex());
        (get ? chunkGet() : chunkSet()).assign (static_cast<const char*> (data), size < static_cast<size_t> (kMaxChunkBytes) ? size : static_cast<size_t> (kMaxChunkBytes));
        (get ? chunkGetDue() : chunkSetDue()) = true;
    }

    /** The dump request: true once when "<log path>-dump" exists (the file is deleted). Display thread. */
    static bool dumpRequested()
    {
        if (! enabled())
            return false;
        const std::string req = path() + "-dump";
        if (! exists (req))
            return false;
        ::unlink (req.c_str());
        return true;
    }
    static std::string dumpPath() { return path() + "-dump.txt"; }

    /** The switch off and the ring empty (tests that start the log again). */
    static void resetForTests() noexcept
    {
        std::lock_guard<std::mutex> g (mutex());
        state() = State();
        dropped().store (0, std::memory_order_relaxed);
        droppedTotal().store (0, std::memory_order_relaxed);
    }

private:
    static constexpr long kMaxChunkBytes = 8 * 1024 * 1024;
    struct Slot { std::atomic<size_t> seq { 0 }; long long ms = 0; long gets = 0; char text[kLineBytes]; };
    struct Ring { Slot s[kSlots]; Ring() { for (size_t i = 0; i < kSlots; ++i) s[i].seq.store (i, std::memory_order_relaxed); } };
    struct State { long long counter = 0; long long first = -1; bool full = false; size_t tail = 0; };

    /** A free slot for the calling thread (and its position), or null when the ring is full. Multi producer (the bounded queue of D. Vyukov): the head is claimed with a compare and swap against the slot's sequence number. */
    static Slot* claim (size_t& pos) noexcept
    {
        size_t head = headCounter().load (std::memory_order_relaxed);
        for (;;)
        {
            Slot& slot = ring().s[head % kSlots];
            const size_t seq = slot.seq.load (std::memory_order_acquire);
            if (seq == head)
            {
                if (headCounter().compare_exchange_weak (head, head + 1, std::memory_order_relaxed))
                {
                    pos = head;
                    return &slot;
                }
            }
            else if (seq < head)
                return nullptr;   // the slot of the previous lap is not free yet: the ring is full
            else
                head = headCounter().load (std::memory_order_relaxed);
        }
    }
    static Ring& ring() { static Ring r; return r; }
    static std::atomic<size_t>& headCounter() { static std::atomic<size_t> h { 0 }; return h; }
    static std::atomic<long>& dropped() { static std::atomic<long> d { 0 }; return d; }
    static std::atomic<long>& droppedTotal() { static std::atomic<long> dt { 0 }; return dt; }
    static State& state() { static State s; return s; }
    static std::mutex& mutex() { static std::mutex m; return m; }
    static std::mutex& chunkMutex() { static std::mutex m; return m; }
    static std::string& chunkGet() { static std::string s; return s; }
    static std::string& chunkSet() { static std::string s; return s; }
    static bool& chunkGetDue() { static bool b = false; return b; }
    static bool& chunkSetDue() { static bool b = false; return b; }
    static void writeChunk (const char* suffix, std::string& chunk)
    {
        std::string copy;
        bool due = false;
        {
            std::lock_guard<std::mutex> g (chunkMutex());
            bool& flag = &chunk == &chunkGet() ? chunkGetDue() : chunkSetDue();
            due = flag;
            flag = false;
            if (due)
                copy.swap (chunk);
        }
        if (due)
            safefile::writeSmall ((path() + suffix).c_str(), copy.data(), copy.size());
    }
    static std::atomic<long>& gets() { static std::atomic<long> g { 0 }; return g; }
    static std::atomic<bool>& on() { static std::atomic<bool> b { false }; return b; }
    static long long nowMs() noexcept { return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
};

#else // no debug hooks: nothing is looked at, nothing is written

class PlayLog
{
public:
    static bool enabled() noexcept { return false; }
    static void refresh() noexcept {}
    static unsigned hash (const void*, size_t) noexcept { return 0; }
    static void countGet() noexcept {}
    static void line (const char*, ...) noexcept __attribute__ ((format (printf, 1, 2))) {}
    static void note (bool, int, int, int, int) noexcept {}
    static void drain() noexcept {}
    static void offerChunk (bool, const void*, size_t) noexcept {}
    static bool dumpRequested() noexcept { return false; }
    static std::string dumpPath() { return std::string(); }
    static std::string path() { return std::string(); }
};

#endif

} // namespace synthyboy_mpc
