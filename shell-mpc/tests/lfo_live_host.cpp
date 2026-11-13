// SynthyBoy - shell-mpc acceptance: the live LFO display parameters (compile switch SYNTHYBOY_MPC_LFO_LIVE, shell-mpc/lfo_live.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: lfo_live_host <plugin library> off|on|selftest|quiet [HZ]
//   HZ is the refresh rate the plugin was built with (SYNTHYBOY_MPC_LFO_LIVE_HZ, default 10): the number of audioMasterAutomate calls per cursor and second is
//   at most HZ (+1 for the first tick), and about HZ for a cursor that changes cell at every tick.
//   off:      built with SYNTHYBOY_MPC_LFO_LIVE=0: numParams is the engine's count, nothing exists after it.
//   on:       built with it (default settings): four display-only parameters "LFO n Cursor" after the engine's list, idle (0, no audioMasterAutomate)
//             while no voice sounds; with a note held they follow the LFO cycle and the plugin calls audioMasterAutomate for them (and for nothing else).
//   selftest: SYNTHYBOY_MPC_LFO_LIVE_SELFTEST=ON: all four sweep without a note, LFO n at 0.25 * 2^(n-1) cycles a second.
//   quiet:    selftest build with SYNTHYBOY_MPC_LFO_LIVE_NOTIFY=OFF: the values sweep, audioMasterAutomate is never called.
// Prints "chunk <bytes> <hash>" of the initial state: it must be the same in every mode of the same source. Links the core (paramCount). Run the TSan build too.
#include "lfo_live.h"
#include "mpc_vst2_abi.h"
#include "synthyboy/params.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <set>
#include <string>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <dirent.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

static std::atomic<int> automate[4], automateOther { 0 };
static std::atomic<int> firstIndex { -1 };
static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float)
{
    if (opcode == audioMasterAutomate)
    {
        const int k = index - firstIndex.load();
        if (k >= 0 && k < 4) automate[k].fetch_add (1);
        else automateOther.fetch_add (1);
        return 1;
    }
    return opcode == audioMasterVersion ? 2400 : 0;
}

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}

static std::string getChunk (AEffect* e)
{
    void* data = nullptr;
    const VstIntPtr n = e->dispatcher (e, effGetChunk, 0, 0, &data, 0.0f);
    return (data != nullptr && n > 0) ? std::string (static_cast<const char*> (data), static_cast<size_t> (n)) : std::string();
}

static std::string text (AEffect* e, VstInt32 opcode, int index, bool* ok = nullptr)
{
    char buf[256]; std::memset (buf, 0, sizeof (buf));
    const VstIntPtr r = e->dispatcher (e, opcode, index, 0, buf, 0.0f);
    if (ok) *ok = r != 0;
    return buf;
}

static void noteOn (AEffect* e, int key)
{
    VstMidiEvent m; std::memset (&m, 0, sizeof (m));
    m.type = kVstMidiType; m.byteSize = sizeof (VstMidiEvent);
    m.midiData[0] = static_cast<char> (0x90); m.midiData[1] = static_cast<char> (key); m.midiData[2] = 100;
    VstEvents ev; std::memset (&ev, 0, sizeof (ev));
    ev.numEvents = 1; ev.events[0] = reinterpret_cast<VstEvent*> (&m);
    e->dispatcher (e, effProcessEvents, 0, 0, &ev, 0.0f);
}

/** Process blocks about in real time for `ms`, sampling parameter `index` (and the other cursors) every block. */
struct Sweep { std::set<float> seen[4]; float lo[4] = { 2, 2, 2, 2 } /* smallest non-idle value */, hi[4] = { -1, -1, -1, -1 }; };
static Sweep run (AEffect* e, int n, int ms, bool audio)
{
    Sweep s;
    std::vector<float> l (256), r (256);
    float* outs[2] = { l.data(), r.data() };
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (ms))
    {
        if (audio) e->processReplacing (e, nullptr, outs, 256);
        for (int k = 0; k < 4; ++k)
        {
            const float v = e->getParameter (e, n + k);
            s.seen[k].insert (v); if (v > 0.0f) s.lo[k] = std::min (s.lo[k], v); s.hi[k] = std::max (s.hi[k], v);
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    return s;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: lfo_live_host <plugin> off|on|selftest|quiet\n"); return 2; }
    const std::string mode = argv[2];
    const int hz = argc > 3 ? std::max (1, std::atoi (argv[3])) : 10;
    const int maxCalls = 2 * hz + 1;       // 2 s: one call per tick and parameter at the most
    const int minFast = (2 * hz * 3) / 4;  // a cursor that changes cell at every tick: nearly all ticks call

    // the pure function first: the mapping of the engine's readout to a cursor step
    using synthyboy_mpc::lfoLiveStep;
    check (lfoLiveStep (-1.0f, 0, 0.0f) == 0, "step: no voice (-1) is idle");
    check (lfoLiveStep (0.0f, 0, 0.0f) == 1 && lfoLiveStep (0.999f, 0, 0.0f) == 32 && lfoLiveStep (0.5f, 0, 0.0f) == 17, "step: cells 1 .. 32 over the cycle");
    check (lfoLiveStep (0.0f, 0, 180.0f) == 17 && lfoLiveStep (0.75f, 1, 180.0f) == 9, "step: the phase knob shifts the position and wraps (retrigger, free-run)");
    check (lfoLiveStep (0.25f, 4, 180.0f) == 9 && lfoLiveStep (0.25f, 5, 180.0f) == 9, "step: the loop trigger types ignore the knob here (it is the loop point)");
    check (lfoLiveStep (0.9f, 2, 180.0f) == 32 && lfoLiveStep (0.25f, 2, 90.0f) == 17, "step: envelope trigger adds the knob and stops at 1");
    check (lfoLiveStep (7.0f, 0, 0.0f) >= 1 && lfoLiveStep (7.0f, 0, 0.0f) <= 32 && lfoLiveStep (0.5f, 0, 720.0f) >= 1, "step: out of range input stays within 1 .. 32");
    check (synthyboy_mpc::lfoLiveValue (0) == 0.0f && synthyboy_mpc::lfoLiveValue (32) == 1.0f, "value: step / 32");
    bool floorOk = true, roundOk = true;
    for (int step = 0; step <= 32; ++step)
    {
        const float v = synthyboy_mpc::lfoLiveValue (step);
        floorOk = floorOk && std::min (static_cast<int> (std::floor (v * 33.0f)), 32) == step; // 33 frames, the last one clamped
        roundOk = roundOk && static_cast<int> (std::lround (v * 32.0f)) == step;
    }
    check (floorOk && roundOk, "value: both readings of the normalised value (floor(v * 33), round(v * 32)) give the step 0 .. 32");

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    using Main = AEffect* (*) (audioMasterCallback);
    auto entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }

    const int n = static_cast<int> (synthyboy::paramCount());
    firstIndex.store (n);
#if defined(__linux__)
    // The plugin is created by a thread that is as unlike a normal thread as an unprivileged test can make it: nice 5 and pinned to one CPU. A helper thread
    // of the plugin must not inherit that: policy SCHED_OTHER, nice 0, every CPU (the same code undoes an inherited SCHED_FIFO: the plugin sets its policy itself).
    const auto threadIds = [] { std::set<int> ids; if (DIR* d = opendir ("/proc/self/task")) { while (dirent* de = readdir (d)) if (de->d_name[0] != '.') ids.insert (std::atoi (de->d_name)); closedir (d); } return ids; };
    const std::set<int> before = threadIds();
    AEffect* e = nullptr;
    std::thread creator ([&]
    {
        setpriority (PRIO_PROCESS, static_cast<id_t> (syscall (SYS_gettid)), 5);
        cpu_set_t one; CPU_ZERO (&one);
        cpu_set_t now; CPU_ZERO (&now);
        sched_getaffinity (0, sizeof (now), &now);
        for (int c = 0; c < CPU_SETSIZE; ++c) if (CPU_ISSET (c, &now)) { CPU_SET (c, &one); break; }
        sched_setaffinity (0, sizeof (one), &one);
        e = entry (hostCallback);
    });
    creator.join();
    std::set<int> added;
    for (int id : threadIds()) if (! before.count (id)) added.insert (id);
    if (mode != "off")
    {
        bool allNormal = ! added.empty();
        const long cpusOnline = sysconf (_SC_NPROCESSORS_ONLN);
        for (int id : added)
        {
            cpu_set_t m; CPU_ZERO (&m);
            const bool aff = sched_getaffinity (id, sizeof (m), &m) == 0 && (CPU_COUNT (&m) > 1 || cpusOnline == 1);
            const bool policy = sched_getscheduler (id) == SCHED_OTHER;
            const int nice = getpriority (PRIO_PROCESS, static_cast<id_t> (id));
            std::printf ("INFO  helper thread %d: policy %d, nice %d, cpus %d\n", id, sched_getscheduler (id), nice, CPU_COUNT (&m));
            allNormal = allNormal && aff && policy && nice == 0;
        }
        check (allNormal, "threads: the helper thread is SCHED_OTHER, nice 0, on all CPUs although its creator is nice 5 on one CPU");
    }
#else
    AEffect* e = entry (hostCallback);
#endif
    if (! e) { std::fprintf (stderr, "plugin did not load\n"); return 2; }
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    const std::string chunk = getChunk (e);
    std::printf ("chunk %zu %zu\n", chunk.size(), std::hash<std::string>{} (chunk));
    check (! chunk.empty() && chunk.find ("Cursor") == std::string::npos, "state chunk is available and has no display parameter");

    if (mode == "off")
    {
        check (e->numParams == n, "off: numParams is the engine's count");
        bool ok = true;
        text (e, effGetParamName, n, &ok);
        check (! ok, "off: no parameter name after the engine's list");
        check (e->getParameter (e, n) == 0.0f, "off: getParameter past the list is 0");
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
        return fails ? 1 : 0;
    }

    check (e->numParams == n + 4, "numParams is the engine's count + 4");
    bool ok[5] = {};
    const std::string names[5] = { text (e, effGetParamName, n, ok), text (e, effGetParamName, n + 1, ok + 1), text (e, effGetParamName, n + 2, ok + 2),
                                   text (e, effGetParamName, n + 3, ok + 3), text (e, effGetParamName, n + 4, ok + 4) };
    check (ok[0] && ok[1] && ok[2] && ok[3] && ! ok[4] && names[0] == "LFO 1 Cursor" && names[3] == "LFO 4 Cursor", "the four names, nothing after them");
    bool allOff = true, propsOk = true;
    VstParameterProperties props;
    for (int k = 0; k < 4; ++k)
    {
        allOff = allOff && e->dispatcher (e, effCanBeAutomated, n + k, 0, nullptr, 0.0f) == 0;
        propsOk = propsOk && e->dispatcher (e, effGetParameterProperties, n + k, 0, &props, 0.0f) == 1;
    }
    check (allOff && e->dispatcher (e, effCanBeAutomated, 0, 0, nullptr, 0.0f) == 1, "display parameters are not automatable, engine parameters are");
    check (propsOk && e->dispatcher (e, effGetParameterProperties, n + 4, 0, &props, 0.0f) == 0, "parameter properties exist for the four only");

    if (mode == "on")
    {
        Sweep idle = run (e, n, 600, true);
        bool zero = true;
        for (int k = 0; k < 4; ++k) zero = zero && idle.hi[k] == 0.0f;
        check (zero, "on: idle (0) while no voice sounds");
        check (automate[0] + automate[1] + automate[2] + automate[3] + automateOther == 0, "on: no audioMasterAutomate while idle");
        check (text (e, effGetParamDisplay, n) == "idle", "on: display text of an idle cursor");
        noteOn (e, 60);
        Sweep s = run (e, n, 2000, true);
        std::printf ("INFO  note held 2 s: LFO 1 %zu distinct cursor values, automate calls %d %d %d %d, other %d\n", s.seen[0].size(), automate[0].load(),
                     automate[1].load(), automate[2].load(), automate[3].load(), automateOther.load());
        check (s.lo[0] > 0.0f && s.hi[0] <= 1.0f && static_cast<int> (s.seen[0].size()) >= std::min (minFast, 8), "on: LFO 1 cursor follows the cycle (several different cells in 2 s, never idle)");
        bool few = true;
        for (int k = 0; k < 4; ++k) few = few && automate[k].load() <= maxCalls;
        check (few, "on: at most HZ calls a second (+1) for every cursor: <= 2 * HZ + 1 in 2 s");
        check (automate[0].load() >= minFast && automate[0].load() <= maxCalls, "on: about HZ calls a second for a fast LFO (3/4 of the ticks .. 2 * HZ + 1 in 2 s)");
        check (automateOther.load() == 0, "on: audioMasterAutomate only for the display parameters");
        check (text (e, effGetParamDisplay, n).find ('/') != std::string::npos, "on: display text of a cursor cell");
    }
    else
    {
        Sweep s = run (e, n, 2000, false);
        std::printf ("INFO  selftest 2 s: distinct values %zu %zu %zu %zu, automate calls %d %d %d %d, other %d\n", s.seen[0].size(), s.seen[1].size(), s.seen[2].size(),
                     s.seen[3].size(), automate[0].load(), automate[1].load(), automate[2].load(), automate[3].load(), automateOther.load());
        bool sweeps = true;
        for (int k = 0; k < 4; ++k) sweeps = sweeps && s.seen[k].size() >= (hz >= 25 ? 8u : 4u) && s.lo[k] > 0.0f && s.hi[k] <= 1.0f;
        check (sweeps, "selftest: all four cursors move within 1/32 .. 1");
        // at 10 ticks a second LFO 4 (64 cells a second) steps 6.4 cells per tick and shows few different cells (the cursor aliases): the call counts tell the speeds apart
        check (hz >= 25 ? s.seen[3].size() > s.seen[0].size() : automate[3].load() >= automate[0].load(), "selftest: LFO 4 is faster than LFO 1");
        if (mode == "quiet")
            check (automate[0] + automate[1] + automate[2] + automate[3] + automateOther == 0, "quiet: audioMasterAutomate is never called");
        else
        {
            bool few = true;
            for (int k = 0; k < 4; ++k) few = few && automate[k].load() <= maxCalls;
            check (few, "selftest: at most HZ calls a second (+1) for every cursor");
            check (automate[0].load() >= std::min (8, minFast) && automate[3].load() >= minFast && automateOther.load() == 0, "selftest: audioMasterAutomate per changed cell, display parameters only");
        }
    }

    const float before = e->getParameter (e, n);
    e->setParameter (e, n, 0.5f);
    e->setParameter (e, n + 4, 0.5f);
    check (getChunk (e) == chunk, "writes to the display parameters do not change the state chunk");
    (void) before;
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
    std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
