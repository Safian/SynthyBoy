// SynthyBoy - shell-mpc acceptance: the audio callback statistics (compile switch SYNTHYBOY_MPC_CALLBACK_STATS, shell-mpc/callback_stats.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: callback_stats_host <plugin library built with SYNTHYBOY_MPC_CALLBACK_STATS=ON> [file]
// The host calls processReplacing with blocks of 480 frames at 48 kHz (nominal block time 10 ms) at chosen, deliberately uneven intervals: gaps of about 8, 17.5,
// 30, 55 and 100 ms (the 1.5x, 2x, 4x, 8x limits are 15, 20, 40, 80 ms), and the host callback of one of the calls sleeps 6 ms (a slow call inside the callback).
// The statistics file (SYNTHYBOY_MPC_CALLBACK_STATS_FILE) must show the gaps in the expected buckets, the largest gaps in order, the process time and the
// build line; it must appear while the plugin runs (periodic, SYNTHYBOY_MPC_CALLBACK_STATS_INTERVAL_MS) and at effClose (shutdown). No part of this test needs
// the audio thread to be real time; the sleeps only ever last longer than asked, so every check is "at least" in the bucket it belongs to or above.
#include "mpc_vst2_abi.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::atomic<int> slowNext { 0 };
static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterGetTime && slowNext.exchange (0) != 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (6)); // the callback body takes 6 ms
    return opcode == audioMasterVersion ? 2400 : 0;
}

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}

static std::string slurp (const std::string& path)
{
    std::ifstream f (path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/** The number after `key` in the file text (first occurrence), -1 when absent. */
static long long field (const std::string& t, const std::string& key)
{
    const size_t p = t.find (key);
    return p == std::string::npos ? -1 : std::atoll (t.c_str() + p + key.size());
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: callback_stats_host <plugin> [file]\n"); return 2; }
    const std::string file = argc > 2 ? argv[2] : "callback-stats-test.txt";
    std::remove (file.c_str());
    setenv ("SYNTHYBOY_MPC_CALLBACK_STATS_FILE", file.c_str(), 1);
    setenv ("SYNTHYBOY_MPC_CALLBACK_STATS_INTERVAL_MS", "100", 1);

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    using Main = AEffect* (*) (audioMasterCallback);
    auto entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    AEffect* e = entry (hostCallback);
    if (! e) { std::fprintf (stderr, "plugin did not load\n"); return 2; }
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 48000.0f);
    e->dispatcher (e, effSetBlockSize, 0, 480, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    std::vector<float> l (480), r (480);
    float* outs[2] = { l.data(), r.data() };
    auto block = [&] { e->processReplacing (e, nullptr, outs, 480); };
    auto wait = [] (double ms) { std::this_thread::sleep_for (std::chrono::microseconds (static_cast<long long> (ms * 1000))); };

    // gaps: 4 x ~8 ms (bucket 0), 2 x ~17.5 (1), 2 x ~30 (2), 1 x ~55 + 6 (3), 1 x ~100 (4); the call after the second ~30 ms wait is the slow one
    block();
    for (int i = 0; i < 4; ++i) { wait (8); block(); }
    for (int i = 0; i < 2; ++i) { wait (17.5); block(); }
    wait (30); block();
    wait (30); slowNext = 1; block();
    wait (55); block();
    wait (100); block();
    wait (350); // at least three periodic writes of the 100 ms interval while the plugin runs and no call arrives
    const std::string periodic = slurp (file);
    check (periodic.find ("synthyboy-callback-stats 1") == 0 && periodic.find ("written: periodic") != std::string::npos, "periodic: the file exists while the plugin runs");
    check (field (periodic, "callbacks: ") == 11, "periodic: 11 callbacks counted");

    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f); // writes the file for the last time
    const std::string t = slurp (file);
    std::printf ("%s", t.c_str());
    check (t.find ("written: shutdown") != std::string::npos, "shutdown: the file is written at effClose");
    check (t.find ("build: ") != std::string::npos && t.find ("LFO_LIVE=") != std::string::npos && t.find ("arch=") != std::string::npos && t.find ("params=") != std::string::npos,
           "the build line names the variant (LFO_LIVE, arch, params)");
    check (field (t, "callbacks: ") == 11 && field (t, "gaps_counted: ") == 10, "11 callbacks, 10 gaps");
    check (field (t, "block_frames_last: ") == 480 && field (t, "nominal_block_us: ") == 10000, "nominal block time 480 / 48000 = 10000 us");
    const long long b0 = field (t, "<1.5x="), b1 = field (t, "<2x="), b2 = field (t, "<4x="), b3 = field (t, "<8x="), b4 = field (t, ">=8x=");
    std::printf ("INFO  buckets %lld %lld %lld %lld %lld\n", b0, b1, b2, b3, b4);
    check (b0 + b1 + b2 + b3 + b4 == 10, "the buckets add up to the gaps counted");
    check (b4 >= 1 && b3 >= 1 && b3 + b4 >= 2 && b2 + b3 + b4 >= 4 && b1 + b2 + b3 + b4 >= 6, "gaps land in their bucket or above (sleeps are never shorter than asked)");
    check (b0 >= 1 && b0 <= 5, "the short gaps are in the first bucket (a late wake-up may push one up)");
    check (field (t, "gap_max_us: ") >= 100000, "the largest gap is at least the 100 ms wait");
    check (field (t, "process_max_us: ") >= 6000 && field (t, "process_max_us: ") < 90000, "the slow call (6 ms inside the callback) is the process maximum");
    const long long avg = field (t, "process_avg_us: ");
    check (avg >= 0 && avg < field (t, "process_max_us: "), "the mean process time is below the maximum");
    check (field (t, "largest_gaps: ") == 10, "all ten gaps are listed (at most ten are kept)");
    const size_t one = t.find ("  1 at_ms="), two = t.find ("  2 at_ms=");
    check (one != std::string::npos && two != std::string::npos && field (t.substr (one), "gap_us=") >= field (t.substr (two), "gap_us="), "the list is in descending order");
    std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
