// SynthyBoy - shell-mpc acceptance: the live spectrum display parameters (compile switch SYNTHYBOY_MPC_SPECTRUM, shell-mpc/spectrum.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: spectrum_live_host <plugin library> off|on|selftest [HZ]
//   HZ is the refresh rate the plugin was built with (SYNTHYBOY_MPC_LFO_LIVE_HZ, default 10): the display thread ticks HZ times a second and reports at most 32 + 4 times a tick.
//   off:      built with SYNTHYBOY_MPC_SPECTRUM=OFF: numParams is the engine's count + 4 (the LFO cursors), nothing exists after them.
//   on:       built with it: 32 display-only parameters "Spectrum 1" .. "Spectrum 32" after the four cursors (index count + 4 + k). No sound: every column 0 and no
//             audioMasterAutomate for them. A note held: columns rise, the loudest column is the one an offline analysis of the same output gives (+- 1), at most 36 calls a tick,
//             nothing but display parameters is reported; after the note is released the columns fall to 0 and the reports stop.
//   selftest: SYNTHYBOY_MPC_SPECTRUM_SELFTEST=ON: a wave runs over all 32 columns without a note; every column moves; at most 36 calls a tick.
// Prints "chunk <bytes> <hash>" of the initial state: it must be the same in every mode of the same source. Links the core (paramCount). Run the TSan build too.
#include "mpc_vst2_abi.h"
#include "spectrum.h"
#include "synthyboy/params.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <vector>

static std::atomic<int> firstSpectrum { -1 }, spectrumCalls[32], cursorCalls { 0 }, otherCalls { 0 };
static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float)
{
    if (opcode == audioMasterAutomate)
    {
        const int k = index - firstSpectrum.load();
        if (k >= 0 && k < 32) spectrumCalls[k].fetch_add (1);
        else if (k >= -4 && k < 0) cursorCalls.fetch_add (1);
        else otherCalls.fetch_add (1);
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

static void midi (AEffect* e, int status, int key)
{
    VstMidiEvent m; std::memset (&m, 0, sizeof (m));
    m.type = kVstMidiType; m.byteSize = sizeof (VstMidiEvent);
    m.midiData[0] = static_cast<char> (status); m.midiData[1] = static_cast<char> (key); m.midiData[2] = 100;
    VstEvents ev; std::memset (&ev, 0, sizeof (ev));
    ev.numEvents = 1; ev.events[0] = reinterpret_cast<VstEvent*> (&m);
    e->dispatcher (e, effProcessEvents, 0, 0, &ev, 0.0f);
}

static int totalSpectrumCalls()
{
    int s = 0;
    for (auto& c : spectrumCalls) s += c.load();
    return s;
}

struct Output { std::vector<float> mono; };   // the newest samples of the plugin's output, mono sum

/** Processes blocks of 256 samples about in real time for `ms`, keeps the last 4096 mono samples and calls `each` after every block. */
static void run (AEffect* e, int ms, Output& out, const std::function<void()>& each = {})
{
    std::vector<float> l (256), r (256);
    float* outs[2] = { l.data(), r.data() };
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (ms))
    {
        e->processReplacing (e, nullptr, outs, 256);
        for (size_t i = 0; i < 256; ++i) out.mono.push_back (0.5f * (l[i] + r[i]));
        if (out.mono.size() > 4096) out.mono.erase (out.mono.begin(), out.mono.end() - 4096);
        if (each) each();
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: spectrum_live_host <plugin> off|on|selftest [HZ]\n"); return 2; }
    const std::string mode = argv[2];
    const int hz = argc > 3 ? std::max (1, std::atoi (argv[3])) : 10;
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    using Main = AEffect* (*) (audioMasterCallback);
    auto entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    const int n = static_cast<int> (synthyboy::paramCount());
    firstSpectrum.store (n + 4);
    AEffect* e = entry (hostCallback);
    if (! e) { std::fprintf (stderr, "plugin did not load\n"); return 2; }
    const auto created = std::chrono::steady_clock::now();
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    const std::string chunk = getChunk (e);
    std::printf ("chunk %zu %zu\n", chunk.size(), std::hash<std::string>{} (chunk));
    check (! chunk.empty() && chunk.find ("Spectrum") == std::string::npos, "state chunk is available and has no spectrum parameter");

    if (mode == "off")
    {
        check (e->numParams == n + 4, "off: numParams is the engine's count + 4, no spectrum column");
        bool ok = true;
        text (e, effGetParamName, n + 4, &ok);
        check (! ok, "off: no parameter name after the four cursors");
        check (e->getParameter (e, n + 4) == 0.0f, "off: getParameter past the list is 0");
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
        return fails ? 1 : 0;
    }

    // the parameter list: indices stable (2637 engine + 4 cursors + 32 columns for the shipped table), names, properties, no automation
    check (e->numParams == n + 4 + 32, "numParams is the engine's count + 4 cursors + 32 spectrum columns");
    {
        bool names = true, notAuto = true, props = true, idle = true;
        VstParameterProperties p;
        for (int k = 0; k < 32; ++k)
        {
            bool ok = false;
            const std::string name = text (e, effGetParamName, n + 4 + k, &ok);
            names = names && ok && name == "Spectrum " + std::to_string (k + 1);
            notAuto = notAuto && e->dispatcher (e, effCanBeAutomated, n + 4 + k, 0, nullptr, 0.0f) == 0;
            props = props && e->dispatcher (e, effGetParameterProperties, n + 4 + k, 0, &p, 0.0f) == 1 && std::fabs (p.stepFloat - 1.0f / 64.0f) < 1e-6f;
            idle = idle && (mode == "selftest" || e->getParameter (e, n + 4 + k) == 0.0f);
        }
        check (names, "names: \"Spectrum 1\" .. \"Spectrum 32\" at count + 4 .. count + 35");
        check (text (e, effGetParamName, n + 3) == "LFO 4 Cursor" && text (e, effGetParamName, n) == "LFO 1 Cursor", "names: the cursors keep their places (count .. count + 3)");
        check (notAuto, "the spectrum columns are not automatable");
        check (props, "the spectrum columns have the parameter properties with a step of 1/64");
        bool afterEnd = true;
        text (e, effGetParamName, n + 36, &afterEnd);
        check (! afterEnd && e->dispatcher (e, effGetParameterProperties, n + 36, 0, &p, 0.0f) == 0, "nothing after the 32nd column");
        check (idle, "at the start (nothing played) every column is 0");
        // writes are ignored
        const std::string before = getChunk (e);
        for (int k = 0; k < 32; ++k) e->setParameter (e, n + 4 + k, 0.75f);
        bool unchanged = getChunk (e) == before;
        for (int k = 0; k < 32; ++k) unchanged = unchanged && e->getParameter (e, n + 4 + k) != 0.75f;
        check (unchanged, "writes to the spectrum columns are ignored (value and state chunk unchanged)");
    }

    Output out;
    if (mode == "on")
    {
        run (e, 1200, out);
        bool zero = true;
        for (int k = 0; k < 32; ++k) zero = zero && e->getParameter (e, n + 4 + k) == 0.0f;
        check (zero && totalSpectrumCalls() == 0, "no sound: every column 0 and no audioMasterAutomate for the spectrum");

        midi (e, 0x90, 57);   // A3 220 Hz, held
        const int callsBefore = totalSpectrumCalls();
        const auto noteAt = std::chrono::steady_clock::now();
        float peak[32] = {};
        run (e, 2500, out, [&] { for (int k = 0; k < 32; ++k) peak[k] = std::max (peak[k], e->getParameter (e, n + 4 + k)); });
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - noteAt).count();
        const int calls = totalSpectrumCalls() - callsBefore;
        int rising = 0;
        for (float v : peak) rising += v > 0.0f;
        std::printf ("INFO  note held %.1f s: %d spectrum calls, %d columns above 0\n", secs, calls, rising);
        check (rising >= 3, "a note held: at least 3 columns rise above 0");
        check (calls > 0 && calls <= static_cast<int> (secs * hz + 2) * 32, "a note held: at most 32 calls per tick (ticks counted from the time of the note + 2)");
        check (otherCalls.load() == 0, "audioMasterAutomate only for display parameters");
        // the loudest column against an offline analysis of the same output (the note is steady by now)
        float live[32];
        for (int k = 0; k < 32; ++k) live[k] = e->getParameter (e, n + 4 + k);
        synthyboy_mpc::SpectrumAnalyser offline;
        float ref[32] = {};
        for (int i = 0; i < 30; ++i) offline.tickSamples (out.mono.data() + out.mono.size() - 2048, 44100.0, ref);
        const int a = static_cast<int> (std::max_element (live, live + 32) - live), b = static_cast<int> (std::max_element (ref, ref + 32) - ref);
        std::printf ("INFO  loudest column: plugin %d (%.3f), offline %d (%.3f)\n", a + 1, live[a], b + 1, ref[b]);
        check (std::abs (a - b) <= 1, "the loudest column of the plugin is the loudest of the offline analysis of the same output (+- 1)");
        {
            bool grid = true;
            for (float v : live) grid = grid && std::fabs (v * 64.0f - std::round (v * 64.0f)) < 1e-4f && v >= 0.0f && v <= 1.0f;
            check (grid, "the values are multiples of 1/64 in 0 .. 1");
        }

        midi (e, 0x80, 57);
        run (e, 9000, out);
        bool zeroAgain = true;
        for (int k = 0; k < 32; ++k) zeroAgain = zeroAgain && e->getParameter (e, n + 4 + k) == 0.0f;
        const int settled = totalSpectrumCalls();
        run (e, 1500, out);
        check (zeroAgain, "after the note: every column has fallen to 0");
        check (totalSpectrumCalls() == settled, "after that: no more reports");
    }
    else // selftest
    {
        std::set<float> seen[32];
        const int before = totalSpectrumCalls();
        const auto t0 = std::chrono::steady_clock::now();
        run (e, 2500, out, [&] { for (int k = 0; k < 32; ++k) seen[k].insert (e->getParameter (e, n + 4 + k)); });
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double sinceCreation = std::chrono::duration<double> (std::chrono::steady_clock::now() - created).count();
        bool moving = true;
        for (auto& s : seen) moving = moving && s.size() >= 6;
        const int calls = totalSpectrumCalls() - before;
        std::printf ("INFO  selftest %.1f s: %d spectrum calls (%.1f a tick of %d a second)\n", secs, calls, calls / (secs * hz), hz);
        check (moving, "selftest: every one of the 32 columns takes at least 6 different values in 2.5 s");
        check (calls > 0 && calls <= static_cast<int> (sinceCreation * hz + 2) * 32, "selftest: at most 32 calls per tick");
        check (calls >= static_cast<int> (secs * hz) * 10, "selftest: many columns change at every tick (at least 10 calls a tick on average)");
        check (otherCalls.load() == 0, "audioMasterAutomate only for display parameters");
    }

    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
    std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
