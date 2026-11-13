// SynthyBoy - MPC shell: diagnostic statistics of the audio callback (compile switch SYNTHYBOY_MPC_CALLBACK_STATS, default OFF).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Question it answers: does something (the live LFO display, a host redraw) hold up the host's audio thread? The audio thread calls begin () and end ()
// around every processReplacing; they measure
//   - the time between two callback starts (the gap) against the nominal block time of the previous block (frames / sample rate): the maximum, a histogram
//     (< 1.5x, < 2x, < 4x, < 8x, >= 8x) and the ten largest gaps with the time they happened at;
//   - the time spent inside the callback (maximum, mean).
// On the audio thread this is arithmetic and relaxed atomic stores to storage that exists from the start (one writer, so no read-modify-write): no
// allocation, no lock, no system call except the monotonic clock (vDSO), no file access. A separate low priority thread writes a text file every few
// seconds and at shutdown (format: see format ()). The file is the only output.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#include "thread_policy.h"

namespace synthyboy_mpc
{
class CallbackStats
{
public:
    static constexpr int kTop = 10;
    static constexpr int kBuckets = 5;

    CallbackStats() : origin (nowNs()) {}
    ~CallbackStats() { stop(); }
    CallbackStats (const CallbackStats&) = delete;
    CallbackStats& operator= (const CallbackStats&) = delete;

    static uint64_t nowNs() noexcept
    {
        return static_cast<uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // ---- audio thread --------------------------------------------------------------------------------------------------------------------

    /** Start of a callback of `frames` samples; returns the start time for end (). */
    uint64_t begin (int frames, double sampleRate) noexcept
    {
        const uint64_t t0 = nowNs();
        const uint64_t nominal = frames > 0 && sampleRate > 0.0 ? static_cast<uint64_t> (static_cast<double> (frames) * 1.0e9 / sampleRate) : 0;
        if (restart.load (std::memory_order_relaxed) && restart.exchange (false, std::memory_order_relaxed))
            havePrev = false;
        if (havePrev && prevNominal > 0 && t0 >= prevStart)
        {
            const uint64_t gap = t0 - prevStart;
            const int b = gap * 2 < prevNominal * 3 ? 0 : gap < prevNominal * 2 ? 1 : gap < prevNominal * 4 ? 2 : gap < prevNominal * 8 ? 3 : 4;
            bump (bucket[b]);
            bump (gaps);
            if (gap > gapMax.load (std::memory_order_relaxed))
                gapMax.store (gap, std::memory_order_relaxed);
            recordTop (gap, t0, prevNominal, prevFrames);
        }
        prevStart = t0;
        prevNominal = nominal;
        prevFrames = frames;
        havePrev = true;
        bump (callbacks);
        lastFrames.store (frames, std::memory_order_relaxed);
        lastNominalNs.store (nominal, std::memory_order_relaxed);
        return t0;
    }

    /** End of the callback that began at t0. */
    void end (uint64_t t0) noexcept
    {
        const uint64_t t1 = nowNs();
        const uint64_t d = t1 >= t0 ? t1 - t0 : 0;
        if (d > procMax.load (std::memory_order_relaxed))
            procMax.store (d, std::memory_order_relaxed);
        procSum.store (procSum.load (std::memory_order_relaxed) + d, std::memory_order_relaxed);
        bump (procCount);
    }

    // ---- any other thread ----------------------------------------------------------------------------------------------------------------

    /** The gap after a pause (mains off / on) is not a gap of the audio: forget the previous callback. */
    void restartGaps() noexcept { restart.store (true, std::memory_order_relaxed); }

    struct Snapshot
    {
        uint64_t callbacks = 0, gaps = 0, bucket[kBuckets] = {}, gapMax = 0, procMax = 0, procSum = 0, procCount = 0, nominalNs = 0;
        int frames = 0;
        struct Top { uint64_t gap, at, nominal; int frames; } top[kTop] = {};
        int topCount = 0;
    };

    Snapshot snapshot() const noexcept
    {
        Snapshot s;
        s.callbacks = callbacks.load (std::memory_order_relaxed);
        s.gaps = gaps.load (std::memory_order_relaxed);
        for (int i = 0; i < kBuckets; ++i) s.bucket[i] = bucket[i].load (std::memory_order_relaxed);
        s.gapMax = gapMax.load (std::memory_order_relaxed);
        s.procMax = procMax.load (std::memory_order_relaxed);
        s.procSum = procSum.load (std::memory_order_relaxed);
        s.procCount = procCount.load (std::memory_order_relaxed);
        s.nominalNs = lastNominalNs.load (std::memory_order_relaxed);
        s.frames = lastFrames.load (std::memory_order_relaxed);
        for (int i = 0; i < kTop; ++i)
        {
            Snapshot::Top t {};
            for (int attempt = 0; attempt < 4; ++attempt) // slot is written by the audio thread: read it consistently (sequence counter)
            {
                const uint32_t a = slot[i].seq.load (std::memory_order_acquire);
                t.gap = slot[i].gap.load (std::memory_order_relaxed);
                t.at = slot[i].at.load (std::memory_order_relaxed);
                t.nominal = slot[i].nominal.load (std::memory_order_relaxed);
                t.frames = slot[i].frames.load (std::memory_order_relaxed);
                std::atomic_thread_fence (std::memory_order_acquire);
                if ((a & 1u) == 0 && a == slot[i].seq.load (std::memory_order_relaxed))
                    break;
                t.gap = 0;
            }
            if (t.gap > 0) s.top[s.topCount++] = t;
        }
        std::sort (s.top, s.top + s.topCount, [] (const Snapshot::Top& a, const Snapshot::Top& b) { return a.gap > b.gap; });
        return s;
    }

    /** The text of the statistics file. `build` is one line naming the build variant. Times are microseconds; "at_ms" is milliseconds since the plugin was created. */
    std::string format (const std::string& build, const char* reason) const
    {
        const Snapshot s = snapshot();
        const uint64_t now = nowNs();
        std::string o;
        o += "synthyboy-callback-stats 1\n";
        appendf (o, "build: %s\n", build.c_str());
        appendf (o, "written: %s at_ms=%llu\n", reason, ms (now - origin));
        appendf (o, "block_frames_last: %d\n", s.frames);
        appendf (o, "nominal_block_us: %llu\n", s.nominalNs / 1000);
        appendf (o, "callbacks: %llu\n", s.callbacks);
        appendf (o, "gaps_counted: %llu\n", s.gaps);
        appendf (o, "gap_max_us: %llu\n", s.gapMax / 1000);
        appendf (o, "gap_buckets (gap / nominal block time): <1.5x=%llu <2x=%llu <4x=%llu <8x=%llu >=8x=%llu\n", s.bucket[0], s.bucket[1], s.bucket[2], s.bucket[3], s.bucket[4]);
        appendf (o, "process_max_us: %llu\n", s.procMax / 1000);
        appendf (o, "process_avg_us: %llu.%03llu\n", s.procCount ? s.procSum / s.procCount / 1000 : 0ull, s.procCount ? (s.procSum / s.procCount) % 1000 : 0ull);
        appendf (o, "largest_gaps: %d\n", s.topCount);
        for (int i = 0; i < s.topCount; ++i)
        {
            const auto& t = s.top[i];
            appendf (o, "  %d at_ms=%llu gap_us=%llu ratio=%.2fx block_frames=%d\n", i + 1, ms (t.at >= origin ? t.at - origin : 0), t.gap / 1000,
                 t.nominal ? static_cast<double> (t.gap) / static_cast<double> (t.nominal) : 0.0, t.frames);
        }
        return o;
    }

    /** Starts the writer thread (low priority). `path`: the file; `build`: the line for format (); `intervalMs`: pause between two writes. */
    void start (std::string path, std::string build, int intervalMs)
    {
        filePath = std::move (path);
        buildLine = std::move (build);
        writer = std::thread ([this, intervalMs]
        {
            useNormalScheduling (10);
            std::unique_lock<std::mutex> lock (stopMutex);
            while (! stopping)
            {
                stopCv.wait_for (lock, std::chrono::milliseconds (intervalMs), [this] { return stopping; });
                if (! stopping)
                    write ("periodic");
            }
        });
    }

    void stop() noexcept
    {
        if (! writer.joinable())
            return;
        {
            std::lock_guard<std::mutex> lock (stopMutex);
            stopping = true;
        }
        stopCv.notify_all();
        writer.join();
        write ("shutdown");
    }

    /** Writes the file now (through a temporary file and rename, so a reader never sees half of it). Never call it on the audio thread. */
    bool write (const char* reason) const noexcept
    {
        try
        {
            const std::string tmp = filePath + ".tmp";
            const std::string text = format (buildLine, reason);
            std::FILE* f = std::fopen (tmp.c_str(), "w");
            if (! f)
                return false;
            const bool ok = std::fwrite (text.data(), 1, text.size(), f) == text.size();
            const bool closed = std::fclose (f) == 0;
            if (! ok || ! closed || std::rename (tmp.c_str(), filePath.c_str()) != 0)
            {
                std::remove (tmp.c_str());
                return false;
            }
            return true;
        }
        catch (...) { return false; }
    }

private:
    struct Slot
    {
        std::atomic<uint32_t> seq { 0 };
        std::atomic<uint64_t> gap { 0 }, at { 0 }, nominal { 0 };
        std::atomic<int> frames { 0 };
    };

    __attribute__ ((format (printf, 2, 0))) static void appendfv (std::string& o, const char* fmt, va_list args)
    {
        char b[512];
        std::vsnprintf (b, sizeof (b), fmt, args);
        o += b;
    }
    __attribute__ ((format (printf, 2, 3))) static void appendf (std::string& o, const char* fmt, ...)
    {
        va_list args;
        va_start (args, fmt);
        appendfv (o, fmt, args);
        va_end (args);
    }

    static unsigned long long ms (uint64_t ns) noexcept { return static_cast<unsigned long long> (ns / 1000000u); }
    static void bump (std::atomic<uint64_t>& a) noexcept { a.store (a.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed); } // one writer

    /** Keeps the kTop largest gaps: replaces the smallest one when this gap is larger. Audio thread only (it is the only writer of the slots). */
    void recordTop (uint64_t gap, uint64_t at, uint64_t nominal, int frames) noexcept
    {
        int smallest = 0;
        uint64_t smallestGap = slot[0].gap.load (std::memory_order_relaxed);
        for (int i = 1; i < kTop; ++i)
        {
            const uint64_t g = slot[i].gap.load (std::memory_order_relaxed);
            if (g < smallestGap) { smallestGap = g; smallest = i; }
        }
        if (gap <= smallestGap)
            return;
        Slot& s = slot[smallest];
        const uint32_t q = s.seq.load (std::memory_order_relaxed);
        s.seq.store (q + 1, std::memory_order_relaxed); // odd: being written
        std::atomic_thread_fence (std::memory_order_release);
        s.gap.store (gap, std::memory_order_relaxed);
        s.at.store (at, std::memory_order_relaxed);
        s.nominal.store (nominal, std::memory_order_relaxed);
        s.frames.store (frames, std::memory_order_relaxed);
        s.seq.store (q + 2, std::memory_order_release);
    }

    const uint64_t origin;
    // audio thread only
    bool havePrev = false;
    uint64_t prevStart = 0, prevNominal = 0;
    int prevFrames = 0;
    // written by the audio thread, read by the writer
    std::atomic<uint64_t> callbacks { 0 }, gaps { 0 }, bucket[kBuckets] {}, gapMax { 0 }, procMax { 0 }, procSum { 0 }, procCount { 0 }, lastNominalNs { 0 };
    std::atomic<int> lastFrames { 0 };
    Slot slot[kTop];
    std::atomic<bool> restart { false };
    // writer thread
    std::string filePath, buildLine;
    std::thread writer;
    std::mutex stopMutex;
    std::condition_variable stopCv;
    bool stopping = false;
};
} // namespace synthyboy_mpc
