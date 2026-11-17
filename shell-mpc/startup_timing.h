// SynthyBoy - MPC shell: the start-up record. Where the time of the first open of the plugin goes, measured on the device instead of guessed (docs/MPC.md "Start-up time").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A handful of steady_clock stamps, in every build (release included): no allocation after the first call, no lock, no file; the audio thread's one stamp is a relaxed load and, once, a compare and swap.
// The record is per PROCESS and describes the first instance: `begin ()` is the first entry into VSTPluginMain, every milestone is stamped once (the first time it is reached, by any thread), later ones are ignored.
//
// Milestones, in the order they normally come (the NAME is the interval that ENDS at the milestone, counted from the previous milestone in time):
//   engine_create          the PluginInstance constructor up to the engine object (play log refresh, output stage, createEngine)
//   content_store          the content store object and its pack root (no scan, no hashing: files are read when a state names them)
//   scan_presets           the factory preset directory: names and categories of the programs
//   default_params         the default values into the engine and the mirror
//   vst_main_rest          the rest of VSTPluginMain: the AEffect, the display thread's start, the overlay's attach (slot), the identification call to the host
//   host_until_first_call  the host's own time until its first call of our dispatcher
//   host_until_resume      from there to the entry of effMainsChanged (on): effOpen, sample rate, block size, program and chunk queries (ours and the host's)
//   engine_prepare         effMainsChanged (on): the first engine prepare (voices, effects, the tables the program names)
//   host_until_first_audio from there to the end of the first processReplacing: the host's time and one block
// open_ms_total is the time from begin () to the first audio block; open_ms_total_ui from begin () to the first frame of our UI shown. The parts are sorted by time and are the differences of neighbours, so
// they add up to open_ms_total exactly (the printed values differ from it only by the rounding to 3 decimals).
// The details are durations inside those intervals (not part of the sum); host_call_max_ms_* is the longest time any call of the host stayed in our dispatcher (point 4 of the task: what the host waits for).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace synthyboy_mpc {

class StartupTimes
{
public:
    enum Milestone : int
    {
        kEngineCreate, kContentStore, kScanPresets, kDefaultParams, kMainReturned, kFirstCall, kResume, kPrepared, kFirstAudio,
        kMilestones
    };
    enum Detail : int { kStartLive, kStartOverlay, kOverlayReadout, kEngineConstruct, kDetails };
    /** The calls of the host whose longest stay in the dispatcher is kept. */
    enum Call : int { kCallOpen, kCallSetProgram, kCallGetProgramName, kCallGetProgramNameIndexed, kCallGetChunk, kCallSetChunk, kCallMains, kCallSampleRate, kCallBlockSize, kCallParamInfo, kCallOther, kCalls };

    static const char* milestoneName (int m) noexcept
    {
        static const char* const kNames[kMilestones] = { "engine_create", "content_store", "scan_presets", "default_params", "vst_main_rest", "host_until_first_call", "host_until_resume", "engine_prepare", "host_until_first_audio" };
        return m >= 0 && m < kMilestones ? kNames[m] : "?";
    }
    static const char* detailName (int d) noexcept
    {
        static const char* const kNames[kDetails] = { "start_live", "start_overlay", "overlay_readout", "engine_construct" };
        return d >= 0 && d < kDetails ? kNames[d] : "?";
    }
    static const char* callName (int c) noexcept
    {
        static const char* const kNames[kCalls] = { "effOpen", "effSetProgram", "effGetProgramName", "effGetProgramNameIndexed", "effGetChunk", "effSetChunk", "effMainsChanged", "effSetSampleRate", "effSetBlockSize", "param_info_calls", "other" };
        return c >= 0 && c < kCalls ? kNames[c] : "?";
    }

    static int64_t nowNs() noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    /** The first entry into VSTPluginMain (later calls change nothing). */
    void begin() noexcept
    {
        int64_t expected = 0;
        originNs_.compare_exchange_strong (expected, nowNs(), std::memory_order_acq_rel);
    }

    /** Stamps a milestone the first time it is reached; any thread, the audio thread included. */
    void mark (Milestone m) noexcept
    {
        if (m < 0 || m >= kMilestones || stamp_[m].load (std::memory_order_relaxed) != 0)
            return;
        const int64_t origin = originNs_.load (std::memory_order_acquire);
        if (origin == 0)
            return;
        int64_t d = nowNs() - origin;
        if (d < 1)
            d = 1;   // 0 marks "not reached"
        int64_t expected = 0;
        stamp_[m].compare_exchange_strong (expected, d, std::memory_order_acq_rel);
    }
    bool reached (Milestone m) const noexcept { return stamp_[m].load (std::memory_order_relaxed) != 0; }

    /** The first frame of our UI that was shown (overlay thread). */
    void markUi() noexcept
    {
        if (uiStamp_.load (std::memory_order_relaxed) != 0)
            return;
        const int64_t origin = originNs_.load (std::memory_order_acquire);
        if (origin == 0)
            return;
        int64_t d = nowNs() - origin;
        if (d < 1)
            d = 1;
        int64_t expected = 0;
        uiStamp_.compare_exchange_strong (expected, d, std::memory_order_acq_rel);
    }
    bool uiReached() const noexcept { return uiStamp_.load (std::memory_order_relaxed) != 0; }

    /** Adds a duration to a detail (the first instance and the later ones alike: they sum up). */
    void addDetail (Detail d, int64_t ns) noexcept { detailNs_[d].fetch_add (ns, std::memory_order_relaxed); }
    /** The engine's wavetable build time (summed over its threads), in ms: stored as the latest value. */
    void setWavetableMs (double ms) noexcept { wavetableMs_.store (ms, std::memory_order_relaxed); }

    /** What one call of the host cost inside our dispatcher (host thread). */
    void callDone (Call c, int64_t ns) noexcept
    {
        callCount_[c].fetch_add (1, std::memory_order_relaxed);
        callSum_[c].fetch_add (ns, std::memory_order_relaxed);
        int64_t cur = callMax_[c].load (std::memory_order_relaxed);
        while (ns > cur && ! callMax_[c].compare_exchange_weak (cur, ns, std::memory_order_relaxed))
        {
        }
    }

    /** The numbers of the record, for the status text and the tests. */
    struct Parts
    {
        double ms[kMilestones] = {};     // the part that ends at each milestone; -1 when the milestone was not reached
        bool reached[kMilestones] = {};
        double totalMs = -1.0;           // begin () to the last milestone reached, or -1 when none
        double totalFirstAudioMs = -1.0; // begin () to the first audio block, or -1 when not reached
        double totalUiMs = -1.0;         // begin () to the first frame of the UI, or -1
        double sumOfPartsMs = 0.0;       // the sum of the parts that were reached (equals totalMs)
    };

    Parts parts() const noexcept
    {
        Parts p;
        int order[kMilestones];
        int n = 0;
        int64_t t[kMilestones];
        for (int m = 0; m < kMilestones; ++m)
        {
            p.ms[m] = -1.0;
            t[m] = stamp_[m].load (std::memory_order_acquire);
            p.reached[m] = t[m] != 0;
            if (t[m] != 0)
                order[n++] = m;
        }
        for (int i = 1; i < n; ++i)   // insertion sort by time
        {
            const int key = order[i];
            int j = i - 1;
            while (j >= 0 && t[order[j]] > t[key])
            {
                order[j + 1] = order[j];
                --j;
            }
            order[j + 1] = key;
        }
        int64_t prev = 0, sum = 0;
        for (int i = 0; i < n; ++i)
        {
            const int64_t d = t[order[i]] - prev;
            p.ms[order[i]] = static_cast<double> (d) / 1.0e6;
            sum += d;
            prev = t[order[i]];
        }
        p.sumOfPartsMs = static_cast<double> (sum) / 1.0e6;
        if (n > 0)
            p.totalMs = static_cast<double> (prev) / 1.0e6;
        if (t[kFirstAudio] != 0)
            p.totalFirstAudioMs = static_cast<double> (t[kFirstAudio]) / 1.0e6;
        const int64_t ui = uiStamp_.load (std::memory_order_acquire);
        if (ui != 0)
            p.totalUiMs = static_cast<double> (ui) / 1.0e6;
        return p;
    }

    /** "key: value" lines for the status file (the overlay's, see Service::cpuStatus). Every step has its line; a step not reached says "n/a". open_ms_total is the time to the first audio block
        (n/a until it came); the parts are rounded to three decimals each. Allocates: the overlay thread. */
    std::string describe() const
    {
        const Parts p = parts();
        std::string out;
        char line[160];
        for (int m = 0; m < kMilestones; ++m)
        {
            if (p.reached[m])
                std::snprintf (line, sizeof line, "open_ms_%s: %.3f\n", milestoneName (m), p.ms[m]);
            else
                std::snprintf (line, sizeof line, "open_ms_%s: n/a\n", milestoneName (m));
            out += line;
        }
        if (p.totalFirstAudioMs >= 0.0)
            std::snprintf (line, sizeof line, "open_ms_total: %.3f\n", p.totalFirstAudioMs);
        else
            std::snprintf (line, sizeof line, "open_ms_total: n/a\n");
        out += line;
        if (p.totalUiMs >= 0.0)
            std::snprintf (line, sizeof line, "open_ms_total_ui: %.3f\n", p.totalUiMs);
        else
            std::snprintf (line, sizeof line, "open_ms_total_ui: n/a\n");
        out += line;
        for (int d = 0; d < kDetails; ++d)
        {
            std::snprintf (line, sizeof line, "open_ms_detail_%s: %.3f\n", detailName (d), static_cast<double> (detailNs_[d].load (std::memory_order_relaxed)) / 1.0e6);
            out += line;
        }
        std::snprintf (line, sizeof line, "open_ms_detail_wavetables: %.3f\n", wavetableMs_.load (std::memory_order_relaxed));
        out += line;
        for (int c = 0; c < kCalls; ++c)
        {
            const unsigned long n = callCount_[c].load (std::memory_order_relaxed);
            std::snprintf (line, sizeof line, "host_call_max_ms_%s: %.3f (%lu calls, %.3f ms in all)\n", callName (c), static_cast<double> (callMax_[c].load (std::memory_order_relaxed)) / 1.0e6, n,
                           static_cast<double> (callSum_[c].load (std::memory_order_relaxed)) / 1.0e6);
            out += line;
        }
        return out;
    }

    /** Back to nothing (tests). */
    void resetForTests() noexcept
    {
        originNs_.store (0);
        uiStamp_.store (0);
        for (auto& s : stamp_) s.store (0);
        for (auto& d : detailNs_) d.store (0);
        for (int c = 0; c < kCalls; ++c) { callCount_[c].store (0); callSum_[c].store (0); callMax_[c].store (0); }
        wavetableMs_.store (0.0);
    }

private:
    std::atomic<int64_t> originNs_ { 0 }, uiStamp_ { 0 };
    std::atomic<int64_t> stamp_[kMilestones] = {};
    std::atomic<int64_t> detailNs_[kDetails] = {};
    std::atomic<unsigned long> callCount_[kCalls] = {};
    std::atomic<int64_t> callSum_[kCalls] = {}, callMax_[kCalls] = {};
    std::atomic<double> wavetableMs_ { 0.0 };
};

inline StartupTimes& startupTimes() noexcept
{
    static StartupTimes t;
    return t;
}

/** Adds the time of its scope to a detail of the record. */
class StartupDetailScope
{
public:
    explicit StartupDetailScope (StartupTimes::Detail d) noexcept : d_ (d), t0_ (StartupTimes::nowNs()) {}
    ~StartupDetailScope() { startupTimes().addDetail (d_, StartupTimes::nowNs() - t0_); }
    StartupDetailScope (const StartupDetailScope&) = delete;
    StartupDetailScope& operator= (const StartupDetailScope&) = delete;

private:
    StartupTimes::Detail d_;
    int64_t t0_;
};

} // namespace synthyboy_mpc
