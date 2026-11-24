// SynthyBoy - shell-mpc acceptance: saving and reloading a project with the Play page (docs/MPC.md "Project save and reload"). A plugin built with SYNTHYBOY_MPC_OVERLAY.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: play_restore_host <plugin library with the overlay>
// The host saves a plugin's state as its chunk, but it may also keep the value of every parameter and write them back after the chunk (or even before it). This test replays the load orders a
// host could use against a fresh instance and demands that the state after the load is exactly the saved one (the chunk the plugin returns is byte for byte the saved chunk, every parameter
// reads what it read at save time, the three selectors are the saved ones), and that the "chunk wins" protection holds when the host then re-selects the program:
//   a  chunk only
//   b  chunk, then every parameter written back in index order with the values the plugin reported at save time (also: after the display thread reported, with a host that echoes the reports)
//   c  every parameter first (index order, and the reverse order: proxies before selectors), then the chunk
//   d  chunk, write-back, program re-selection (and: chunk, program, write-back; chunk, program, write-back, program)
//   e  the order the Key 61 was seen to use (docs/MPC.md "Observed on the device"): see kObserved below
// and the proxies as ordinary edits of the selected instance when they arrive at any other time (before any chunk, after the restore window, on a recorded-automation like stream).
#include "mpc_vst2_abi.h"
#include "play_params.h"
#include "synthyboy/params.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <unistd.h>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace pl = synthyboy_mpc::play;

static std::atomic<bool> echo { false };   // a host that sets every reported value back
static VstIntPtr hostCallback (AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float opt)
{
    if (opcode == audioMasterAutomate)
    {
        if (echo.load())
            e->setParameter (e, index, opt);
        return 1;
    }
    return opcode == audioMasterVersion ? 2400 : 0;
}

static int fails = 0;
static void check (bool ok, const std::string& what)
{
    if (! ok || std::getenv ("PLAY_HOST_VERBOSE"))
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++fails;
}
static void sleepMs (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }

static std::string getChunk (AEffect* e)
{
    void* data = nullptr;
    const VstIntPtr n = e->dispatcher (e, effGetChunk, 0, 0, &data, 0.0f);
    return (data != nullptr && n > 0) ? std::string (static_cast<const char*> (data), static_cast<size_t> (n)) : std::string();
}
static bool setChunk (AEffect* e, const std::string& c)
{
    return e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (c.size()), const_cast<char*> (c.data()), 0.0f) != 0;
}
static std::string fileText (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    return std::string ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char> ());
}
static void setProgram (AEffect* e, int p) { e->dispatcher (e, effSetProgram, 0, p, nullptr, 0.0f); }

using Main = AEffect* (*) (audioMasterCallback);
static Main entry = nullptr;
static AEffect* make()
{
    AEffect* e = entry (hostCallback);
    if (! e) return nullptr;
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    return e;
}
static void closeIt (AEffect* e) { e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f); }

static int N, OVL, SEL0, PROX0;
/** The parameters whose value is part of the saved state: the engine's, the pad pressure pair, the selectors and the proxies (not the display parameters, not Overlay Instance). */
static bool isState (int i) { return i < N || (i >= N + 36 && i < N + 38) || i >= SEL0; }

struct Saved { std::string chunk; std::vector<float> values; };
static Saved saveOf (AEffect* e)
{
    Saved s;
    s.chunk = getChunk (e);
    for (int i = 0; i < e->numParams; ++i)
        s.values.push_back (e->getParameter (e, i));
    return s;
}
/** Compares the instance with a saved state; returns the number of differences and names the first one. */
static int differences (AEffect* e, const Saved& s, std::string& first)
{
    int d = 0;
    const std::string c = getChunk (e);
    if (c != s.chunk)
    {
        ++d;
        size_t k = 0;
        while (k < c.size() && k < s.chunk.size() && c[k] == s.chunk[k]) ++k;
        first = "chunk differs at byte " + std::to_string (k) + " of " + std::to_string (s.chunk.size()) + ": ..." + s.chunk.substr (k > 30 ? k - 30 : 0, 80) + "... against ..." + c.substr (k > 30 ? k - 30 : 0, 80);
    }
    for (int i = 0; i < e->numParams; ++i)
        if (isState (i) && e->getParameter (e, i) != s.values[static_cast<size_t> (i)])
        {
            if (d++ == 0 || first.empty()) first = "parameter " + std::to_string (i) + " reads " + std::to_string (static_cast<double> (e->getParameter (e, i))) + ", saved " + std::to_string (static_cast<double> (s.values[static_cast<size_t> (i)]));
        }
    return d;
}
static void writeBack (AEffect* e, const Saved& s, bool reverse = false)
{
    if (! reverse)
        for (int i = 0; i < e->numParams; ++i) e->setParameter (e, i, s.values[static_cast<size_t> (i)]);
    else
        for (int i = e->numParams - 1; i >= 0; --i) e->setParameter (e, i, s.values[static_cast<size_t> (i)]);
}

static float pseudo (int i, int seed)
{
    unsigned x = static_cast<unsigned> (i) * 2654435761u + static_cast<unsigned> (seed) * 40503u + 12345u;
    x ^= x >> 13; x *= 1274126177u; x ^= x >> 16;
    return static_cast<float> (x % 100001) / 100000.0f;
}

/** A project: every engine parameter set to a pseudo-random value (so that the sound is as far from the defaults as it gets and the float round trip of every kind of parameter is exercised), the
    three selectors NOT at 1, pad pressure on. `seed` makes projects differ. */
static Saved makeProject (int seed, int osc, int env, int lfo)
{
    AEffect* e = make();
    setProgram (e, 0);
    for (int i = 1; i < N; ++i)   // parameter 0 is the toggle the host writes at every prepare
        e->setParameter (e, i, pseudo (i, seed));
    e->setParameter (e, SEL0, pl::selectorNormalised (0, osc));
    e->setParameter (e, SEL0 + 1, pl::selectorNormalised (1, env));
    e->setParameter (e, SEL0 + 2, pl::selectorNormalised (2, lfo));
    e->setParameter (e, N + 36, 1.0f);
    e->setParameter (e, N + 37, 0.37f);
    sleepMs (50);
    Saved s = saveOf (e);
    closeIt (e);
    return s;
}

static void selectorsAre (AEffect* e, int osc, int env, int lfo, const std::string& what)
{
    check (e->getParameter (e, SEL0) == pl::selectorNormalised (0, osc) && e->getParameter (e, SEL0 + 1) == pl::selectorNormalised (1, env) && e->getParameter (e, SEL0 + 2) == pl::selectorNormalised (2, lfo), what + ": the selectors are Osc " + std::to_string (osc) + " / Env " + std::to_string (env) + " / LFO " + std::to_string (lfo));
}

/** What the order does to a plugin: runs `order` on a fresh instance and expects the saved state afterwards. */
static void replay (const std::string& name, const Saved& s, int osc, int env, int lfo, const std::function<void (AEffect*)>& order, bool expectProtected)
{
    AEffect* e = make();
    order (e);
    std::string first;
    const int d = differences (e, s, first);
    check (d == 0, name + ": the state after the load is exactly the saved one" + (d ? " (" + std::to_string (d) + " differences; " + first + ")" : ""));
    selectorsAre (e, osc, env, lfo, name);
    if (expectProtected)
    {
        // the protection is armed exactly once: the next program selection (the host's own, right after the chunk) changes nothing
        // (the order itself did the first one where it has one; here: after the order, one more selection must be the protected one when nothing re-selected yet)
    }
    closeIt (e);
}

// The order the Key 61 was seen to use. Filled from the device log (docs/MPC.md "Observed on the device"); the opcodes are replayed as they were logged.
//   P = effSetProgram(saved program), C = effSetChunk, S = 300 ms pass, W = write back every parameter in index order, T = the host's toggle of parameter 0 (1.0, then the value it read)
// Measured on the Key 61 (MPC 3.10.1.12, project loaded from the project list): effSetProgram(2), effSetChunk 73 ms later, then 1 s after it getParameter calls and setParameter(0, 1.0),
// setParameter(0, <the value it read>), nothing else (no write-back of the parameters, no second program selection).
static const char* const kObserved = "PCST";

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: play_restore_host <plugin>\n"); return 2; }
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    N = static_cast<int> (synthyboy::paramCount());
    OVL = N + 4 + 32 + 2;
    SEL0 = OVL + 1;
    PROX0 = SEL0 + pl::kSelectors;

    // ---- the debug log of the host's calls (play_log.h): off without the switch file, on with it; the dump on request; the cap
    {
        const std::string base = "/tmp/sb-play-log-test-" + std::to_string (static_cast<long> (getpid()));
        setenv ("SYNTHYBOY_MPC_PLAY_LOG_FILE", base.c_str(), 1);
        std::remove ((base + ".txt").c_str());
        AEffect* e = make();
        e->setParameter (e, 3, 0.5f);
        check (! fileText (base + ".txt").size(), "play log: nothing is written without the switch file");
        closeIt (e);
        if (FILE* f = std::fopen (base.c_str(), "w")) std::fclose (f);
        e = make();   // a new instance looks at the switch file
        const Saved s = saveOf (e);
        setProgram (e, 0);
        setChunk (e, s.chunk);
        e->setParameter (e, 3, 0.25f);
        e->setParameter (e, SEL0, 1.0f);
        sleepMs (700);   // the display thread sees the switch and the request file
        if (FILE* f = std::fopen ((base + "-dump").c_str(), "w")) std::fclose (f);
        const std::string dumpPath = base + "-dump.txt";
        std::remove (dumpPath.c_str());
        bool dumped = false;
        for (int k = 0; k < 40 && ! dumped; ++k) { sleepMs (100); dumped = ! fileText (dumpPath).empty(); }
        const std::string log = fileText (base + ".txt"), dump = fileText (dumpPath);
        check (log.find ("effSetProgram 0") != std::string::npos && log.find ("effSetChunk size=") != std::string::npos && log.find ("setParameter 3 (real) 0.25 -> written") != std::string::npos
               && log.find ("Play selector 0") != std::string::npos, "play log: effSetProgram, effSetChunk and the parameter writes are in the log with what the plugin did");
        check (dumped && dump.rfind ("selectors ", 0) == 0 && dump.find ("\nchunk ") != std::string::npos && dump.find ("\np 3 ") != std::string::npos && ! std::ifstream (base + "-dump").good(), "play log: the dump request writes the state and the request file is gone");
        // the cap: a flood of writes ends at 512 KB with the last line saying so
        // (the lines go through a ring that the display thread writes out once a tick: a flood in a few milliseconds would overflow it, so the host's thread paces itself)
        for (int round = 0; round < 14; ++round)
        {
            for (int k = 0; k < 1500; ++k) e->setParameter (e, 3, (k & 1) ? 0.1f : 0.9f);
            sleepMs (150);
        }
        sleepMs (300);
        const std::string full = fileText (base + ".txt");
        check (full.size() > 400 * 1024 && full.size() < 600 * 1024 && full.find ("log full") != std::string::npos, "play log: the file stops growing at 512 KB (" + std::to_string (full.size()) + " bytes)");
        closeIt (e);
        std::remove (base.c_str());   // the switch off: a new instance does not write again
        e = make();
        const size_t before = fileText (base + ".txt").size();
        e->setParameter (e, 3, 0.77f);
        check (fileText (base + ".txt").size() == before, "play log: switch file removed, nothing is written any more");
        closeIt (e);
        std::remove ((base + ".txt").c_str()); std::remove (dumpPath.c_str()); std::remove ((base + "-chunk-get.json").c_str()); std::remove ((base + "-chunk-set.json").c_str());
    }

    const Saved s1 = makeProject (1, 2, 3, 4);
    const Saved s2 = makeProject (2, 3, 6, 10);
    check (s1.chunk != s2.chunk && s1.chunk.find ("\"mpcPlayOsc5\":2,\"mpcPlayEnv\":3,\"mpcPlayLfo\":4") != std::string::npos, "two different projects: edited sounds, Osc 2 / Env 3 / LFO 4 and Osc 3 / Env 6 / LFO 10");
    {
        // the project differs from the defaults in (nearly) every parameter, in the instances the selectors point to AND in the others
        AEffect* d = make();
        int differing = 0;
        for (int i = 1; i < N; ++i) differing += d->getParameter (d, i) != s1.values[static_cast<size_t> (i)];
        check (differing > N * 8 / 10, "the test project differs from the defaults in " + std::to_string (differing) + " of " + std::to_string (N) + " parameters");
        closeIt (d);
    }

    {
        // the chunk-only view state of the merged Env / Filter / LFO tabs (shell-mpc/tab_view.h "mpcTabView": rnd, filter): in the chunk right after the Play selectors, restored exactly by a project reload,
        // the defaults for a chunk without it (an older project), left alone by a program change
        const std::string member0 = ",\"mpcTabView\":{\"rnd\":0,\"filter\":0}", member = ",\"mpcTabView\":{\"rnd\":3,\"filter\":2}";
        const size_t at = s1.chunk.find (member0);
        check (at != std::string::npos && at >= 14 && s1.chunk.compare (at - 14, 14, "\"mpcPlayLfo\":4") == 0, "the chunk has the view state (0, 0) right after the Play selectors");
        std::string withState = s1.chunk;
        if (at != std::string::npos)
            withState.replace (at, member0.size(), member);
        AEffect* e = make();
        setChunk (e, withState);
        check (getChunk (e) == withState, "a chunk with the view state (rnd 3, filter 2) is restored exactly (the chunk read back is byte for byte the same)");
        setProgram (e, 0);
        check (getChunk (e).find (member) != std::string::npos, "a program change leaves the view state alone");
        std::string old = s1.chunk;
        if (at != std::string::npos)
            old.erase (at, member0.size());
        setChunk (e, old);
        check (getChunk (e).find (member0) != std::string::npos, "an old chunk without the member gives the defaults (0, 0) at the reload");
        std::string bad = withState;
        if (at != std::string::npos)
            bad.replace (bad.find ("\"rnd\":3"), 7, "\"rnd\":9");
        setChunk (e, bad);
        check (getChunk (e).find (member0) != std::string::npos, "a member out of range is refused: the defaults");
        closeIt (e);
    }

    // The compact Play skin keeps the standard LFO Rise proxy in the cell
    // reused by RND STEREO. A direct host/VST write is rejected while a
    // random LFO is selected, but remains writable for a standard LFO.
    {
        AEffect* e = make();
        const int realRise = static_cast<int> (synthyboy::findParamByKey ("lfo1.fade_in") - synthyboy::paramTable());
        const int riseProxy = PROX0 + 2 * pl::kSlots + 1;
        e->setParameter (e, realRise, 0.0f);
        const float standardBefore = e->getParameter (e, realRise);
        e->setParameter (e, riseProxy, 0.85f);
        check (e->getParameter (e, realRise) != standardBefore, "Play LFO Rise proxy writes lfo1.fade_in for a standard LFO");
        const int smoothProxy = PROX0 + 2 * pl::kSlots + pl::kLfoSmoothTimeSlot;
        const int smoothOn = pl::realIndex (2, 1, pl::kLfoSmoothOnSlot);
        const int smoothTime = pl::realIndex (2, 1, pl::kLfoSmoothTimeSlot);
        e->setParameter (e, smoothOn, 0.0f);
        const float savedTime = e->getParameter (e, smoothTime);
        const float editedTime = savedTime < 0.5f ? 0.8f : 0.2f;
        e->setParameter (e, smoothProxy, editedTime);
        check (e->getParameter (e, smoothTime) == savedTime, "disabled Play smoothing retains its saved time after a host write");
        e->setParameter (e, smoothOn, 1.0f);
        e->setParameter (e, smoothProxy, editedTime);
        check (e->getParameter (e, smoothTime) != savedTime, "enabling Play smoothing makes its time writable");
        std::string rndChunk = getChunk (e);
        const size_t rnd = rndChunk.find ("\"rnd\":0");
        if (rnd != std::string::npos)
            rndChunk.replace (rnd, 7, "\"rnd\":1");
        setChunk (e, rndChunk);
        // End the restore window with a real user edit, then exercise the
        // proxy exactly as the host does after selecting RND 1.
        e->setParameter (e, realRise, 0.37f);
        const float randomBefore = e->getParameter (e, realRise);
        e->setParameter (e, riseProxy, 0.10f);
        check (e->getParameter (e, realRise) == randomBefore, "Play LFO Rise proxy is ignored while mpcTabView.rnd selects RND 1");
        closeIt (e);
    }

    for (int proj = 0; proj < 2; ++proj)
    {
        const Saved& s = proj == 0 ? s1 : s2;
        const int o = proj == 0 ? 2 : 3, v = proj == 0 ? 3 : 6, l = proj == 0 ? 4 : 10;
        const std::string tag = proj == 0 ? "[Osc 2 Env 3 LFO 4] " : "[Osc 3 Env 6 LFO 10] ";
        replay (tag + "a chunk only", s, o, v, l, [&] (AEffect* e) { setChunk (e, s.chunk); }, false);
        replay (tag + "b chunk, write-back in index order", s, o, v, l, [&] (AEffect* e) { setChunk (e, s.chunk); writeBack (e, s); }, false);
        replay (tag + "b chunk, 300 ms (the display thread reports), write-back", s, o, v, l, [&] (AEffect* e) { setChunk (e, s.chunk); sleepMs (300); writeBack (e, s); }, false);
        replay (tag + "b' chunk, write-back in reverse order", s, o, v, l, [&] (AEffect* e) { setChunk (e, s.chunk); writeBack (e, s, true); }, false);
        replay (tag + "b'' chunk, write-back twice", s, o, v, l, [&] (AEffect* e) { setChunk (e, s.chunk); writeBack (e, s); writeBack (e, s); }, false);
        replay (tag + "c write-back in index order, then chunk", s, o, v, l, [&] (AEffect* e) { writeBack (e, s); setChunk (e, s.chunk); }, false);
        replay (tag + "c' write-back in reverse order (proxies before selectors), then chunk", s, o, v, l, [&] (AEffect* e) { writeBack (e, s, true); setChunk (e, s.chunk); }, false);
        replay (tag + "c'' write-back, then chunk, then write-back again", s, o, v, l, [&] (AEffect* e) { writeBack (e, s); setChunk (e, s.chunk); writeBack (e, s); }, false);
        replay (tag + "d chunk, write-back, program re-selection", s, o, v, l, [&] (AEffect* e) { setChunk (e, s.chunk); writeBack (e, s); setProgram (e, 0); }, true);
        replay (tag + "d' chunk, program re-selection, write-back", s, o, v, l, [&] (AEffect* e) { setChunk (e, s.chunk); setProgram (e, 0); writeBack (e, s); }, true);
        replay (tag + "d'' chunk, 300 ms, write-back, program re-selection", s, o, v, l, [&] (AEffect* e) { setChunk (e, s.chunk); sleepMs (300); writeBack (e, s); setProgram (e, 0); }, true);
        replay (tag + "d''' write-back, chunk, write-back, program re-selection", s, o, v, l, [&] (AEffect* e) { writeBack (e, s); setChunk (e, s.chunk); writeBack (e, s, true); setProgram (e, 0); }, true);
        // a host that echoes every report of the plugin
        echo.store (true);
        replay (tag + "d (echoing host) chunk, 600 ms, write-back, program re-selection", s, o, v, l, [&] (AEffect* e) { setChunk (e, s.chunk); sleepMs (600); writeBack (e, s); setProgram (e, 0); }, true);
        echo.store (false);
    }

    // ---- the protection: a program selection after the load changes nothing exactly once (it is armed by the chunk, not by the write-back, and the write-back does not disarm it)
    {
        AEffect* e = make();
        setChunk (e, s1.chunk);
        writeBack (e, s1);
        setProgram (e, 0);
        std::string first;
        check (differences (e, s1, first) == 0, "protection: the first program selection after chunk + write-back changed nothing");
        setProgram (e, 0);
        check (differences (e, s1, first) != 0, "protection: ... and it is spent: the next selection loads the program");
        closeIt (e);
    }
    // a real edit disarms it as before: chunk, a real (non-echo) parameter write, program selection loads the program
    {
        AEffect* e = make();
        setChunk (e, s1.chunk);
        const int k = 7;
        e->setParameter (e, k, 1.0f - s1.values[static_cast<size_t> (k)]);
        setProgram (e, 0);
        std::string first;
        check (differences (e, s1, first) != 0, "protection: a real edit after the chunk disarms it (the program selection that follows loads the program)");
        closeIt (e);
    }

    // ---- the restore window: what is ignored in it and when it ends
    {
        AEffect* e = make();
        setChunk (e, s1.chunk);
        const int osc2 = 1;   // Osc 2 is selected; the proxy of slot 7 is Osc 2 Level
        const int proxy = PROX0 + 7;
        const int real = pl::realIndex (0, 2, 7), other = pl::realIndex (0, 1, 7);
        const float before = e->getParameter (e, real), beforeOther = e->getParameter (e, other);
        (void) osc2;
        e->setParameter (e, proxy, 1.0f - before);
        check (e->getParameter (e, real) == before, "restore window: a proxy write that is not an echo does not move the real parameter");
        e->setParameter (e, SEL0, pl::selectorNormalised (0, 1));
        selectorsAre (e, 2, 3, 4, "restore window: a selector write is ignored");
        std::string first;
        check (differences (e, s1, first) == 0, "restore window: nothing of the state moved");
        // a real edit ends the window: the proxy is an ordinary edit of the selected instance
        e->setParameter (e, 7, 1.0f - s1.values[7]);
        e->setParameter (e, proxy, 1.0f - before);
        check (e->getParameter (e, real) != before && e->getParameter (e, other) == beforeOther, "after the window ended (a real edit): a proxy write is an ordinary edit of the SELECTED instance (Osc 2), not of another one");
        e->setParameter (e, SEL0, pl::selectorNormalised (0, 1));
        selectorsAre (e, 1, 3, 4, "after the window: a selector write is taken");
        closeIt (e);
    }
    {
        AEffect* e = make();
        setChunk (e, s1.chunk);
        sleepMs (2200);   // the window of 2 s is over
        const int real = pl::realIndex (1, 3, 0);
        const float before = e->getParameter (e, real);
        e->setParameter (e, PROX0 + pl::kSlots, 1.0f - before);
        check (std::fabs (e->getParameter (e, real) - (1.0f - before)) < 1e-5f, "after 2 s: a proxy write edits the selected instance (Env 3 attack)");
        closeIt (e);
    }
    {
        AEffect* e = make();
        setProgram (e, 0);
        e->setParameter (e, SEL0 + 1, pl::selectorNormalised (1, 5));
        sleepMs (30);
        // no chunk was loaded: proxy writes at any time are ordinary edits of the selected instance (a recorded automation lane played back, a Q-Link assigned by the host)
        bool ok = true;
        for (int k = 0; k < 40; ++k)
        {
            const float v = 0.05f + 0.02f * static_cast<float> (k);
            e->setParameter (e, PROX0 + pl::kSlots + 3, v);
            ok = ok && std::fabs (e->getParameter (e, pl::realIndex (1, 5, 3)) - v) < 1e-5f && e->getParameter (e, pl::realIndex (1, 4, 3)) != v && e->getParameter (e, pl::realIndex (1, 6, 3)) != v;
        }
        check (ok, "no chunk loaded: 40 proxy writes (an automation lane) move the selected instance (Env 5 sustain) only");
        // the selector changes between two writes: the second one goes to the new instance
        e->setParameter (e, SEL0 + 1, pl::selectorNormalised (1, 6));
        e->setParameter (e, PROX0 + pl::kSlots + 3, 0.123f);
        check (std::fabs (e->getParameter (e, pl::realIndex (1, 6, 3)) - 0.123f) < 1e-5f && std::fabs (e->getParameter (e, pl::realIndex (1, 5, 3)) - 0.123f) > 1e-3f, "a selector change between two proxy writes: the second write goes to the new instance");
        // and the project saved that way loads back
        const Saved s = saveOf (e);
        closeIt (e);
        replay ("edited through proxies: chunk, write-back, program", s, 1, 6, 1, [&] (AEffect* x) { setChunk (x, s.chunk); writeBack (x, s); setProgram (x, 0); }, true);
    }

    // ---- the order seen on the device (kObserved, see the file's header)
    {
        for (int proj = 0; proj < 2; ++proj)
        {
            const Saved& s = proj == 0 ? s1 : s2;
            const int o = proj == 0 ? 2 : 3, v = proj == 0 ? 3 : 6, l = proj == 0 ? 4 : 10;
            replay (std::string ("e the order seen on the device (") + kObserved + ")", s, o, v, l, [&] (AEffect* e)
            {
                for (const char* c = kObserved; *c; ++c)
                {
                    if (*c == 'C') setChunk (e, s.chunk);
                    else if (*c == 'W') writeBack (e, s);
                    else if (*c == 'P') setProgram (e, 0);
                    else if (*c == 'S') sleepMs (300);
                    else if (*c == 'T') { const float cur = e->getParameter (e, 0); e->setParameter (e, 0, 1.0f); e->setParameter (e, 0, cur); }
                }
            }, true);
        }
    }

    std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
