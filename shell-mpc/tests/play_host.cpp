// SynthyBoy - shell-mpc acceptance: the Play page's parameters of a plugin built with SYNTHYBOY_MPC_OVERLAY (shell-mpc/play_params.h, docs/MPC.md "Play page"): the three selectors and the 24
// proxy parameters that stand for the real parameter of the selected oscillator / envelope / LFO.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: play_host <plugin library with the overlay>
// Checks: the parameter list and its order, automation flags, every proxy in both directions for every instance, the display text / label / properties of the proxy are the real parameter's,
// a selector change reports exactly the proxies of its group once (the display thread), the chunk round trip of the selectors (an older chunk without them leaves them), a preset change keeps
// the selectors and moves the proxies, no feedback loop when the host echoes every report back (the engine is written only by a real change), the engine's own count of parameters
// instances of every group, the uniformity of the kinds of parameter between the instances of a group. Run under ThreadSanitizer too.
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
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pl = synthyboy_mpc::play;

struct Report { int count = 0; float last = -1.0f; };
static std::mutex reportMu;
static std::map<AEffect*, std::map<int, Report>> reports;
static std::atomic<bool> echo { false };   // a host that sets every reported value back (the worst case for a feedback loop)
static std::atomic<int> totalReports { 0 };

static VstIntPtr hostCallback (AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float opt)
{
    if (opcode == audioMasterAutomate)
    {
        {
            std::lock_guard<std::mutex> g (reportMu);
            Report& r = reports[e][index];
            ++r.count;
            r.last = opt;
        }
        totalReports.fetch_add (1);
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
static Report reportOf (AEffect* e, int index)
{
    std::lock_guard<std::mutex> g (reportMu);
    return reports[e][index];
}
static void resetReports (AEffect* e)
{
    std::lock_guard<std::mutex> g (reportMu);
    reports[e].clear();
}
static bool waitFor (int ms, const std::function<bool()>& cond)
{
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (ms))
    {
        if (cond())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return cond();
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
static std::string text (AEffect* e, VstInt32 opcode, int index, bool* ok = nullptr)
{
    char buf[256]; std::memset (buf, 0, sizeof (buf));
    const VstIntPtr r = e->dispatcher (e, opcode, index, 0, buf, 0.0f);
    if (ok) *ok = r != 0;
    return buf;
}

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

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: play_host <plugin>\n"); return 2; }
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    const int n = static_cast<int> (synthyboy::paramCount());

    // ---- what the engine has (the numbers of play_params.h are read from the table, not assumed)
    {
        int osc = 0, env = 0, lfo = 0;
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        {
            const char* k = synthyboy::paramTable()[i].key;
            if (std::strcmp (k, "osc3.frame") == 0 || std::strcmp (k, "osc2.frame") == 0 || std::strcmp (k, "osc1.frame") == 0) ++osc;
            for (int m = 1; m <= 12; ++m)
            {
                if (std::strcmp (k, ("env" + std::to_string (m) + ".attack").c_str()) == 0) env = std::max (env, m);
                if (std::strcmp (k, ("lfo" + std::to_string (m) + ".rate").c_str()) == 0) lfo = std::max (lfo, m);
            }
        }
        check (osc == pl::kInstances[0] && env == pl::kInstances[1] && lfo == pl::kInstances[2], "the engine has 3 oscillators, " + std::to_string (env) + " envelopes and " + std::to_string (lfo) + " LFOs: the selectors' ranges");
        std::string why;
        const bool uni = pl::uniform (&why);
        check (uni, "every instance of a group has the same kinds of parameter (range, steps, unit, choices): " + why);
    }

    AEffect* a = make();
    if (! a) { std::fprintf (stderr, "plugin did not load\n"); return 2; }
    const int ovl = n + 4 + 32 + 2;   // "Overlay Instance"
    const int sel0 = ovl + 1, prox0 = ovl + 1 + pl::kSelectors;
    check (a->numParams == ovl + 1 + pl::kParams, "numParams is the engine's count + 4 + 32 + 2 + Overlay Instance + 3 selectors + 24 proxies (" + std::to_string (a->numParams) + ")");
    check (text (a, effGetParamName, ovl) == "Overlay Instance" && text (a, effGetParamName, sel0) == "Play Osc" && text (a, effGetParamName, sel0 + 1) == "Play Env" && text (a, effGetParamName, sel0 + 2) == "Play LFO", "order: Overlay Instance, Play Osc, Play Env, Play LFO");
    bool names = true, automation = true;
    for (int g = 0; g < pl::kGroups; ++g)
        for (int s = 0; s < pl::kSlots; ++s)
            names = names && text (a, effGetParamName, prox0 + g * pl::kSlots + s) == pl::slots (g)[s].proxyName;
    check (names, "the 24 proxies are named as in the table, group after group");
    bool past = true;
    text (a, effGetParamName, a->numParams, &past);
    check (! past, "nothing after the last proxy");
    for (int i = sel0; i < a->numParams; ++i)
        automation = automation && a->dispatcher (a, effCanBeAutomated, i, 0, nullptr, 0.0f) == 0;
    check (automation && a->dispatcher (a, effCanBeAutomated, 0, 0, nullptr, 0.0f) == 1 && a->dispatcher (a, effCanBeAutomated, ovl, 0, nullptr, 0.0f) == 0, "selectors and proxies are not automatable, the engine's parameters are");
    {
        VstParameterProperties props;
        bool ok = true;
        for (int g = 0; g < pl::kGroups; ++g)
        {
            std::memset (&props, 0, sizeof props);
            ok = ok && a->dispatcher (a, effGetParameterProperties, sel0 + g, 0, &props, 0.0f) == 1 && props.minInteger == 0 && props.maxInteger == pl::kInstances[g] - 1 && (props.flags & kVstParameterUsesIntegerMinMax) != 0;
        }
        check (ok, "the selectors are stepped integers 0 .. n - 1 (displayed as 1 .. n)");
    }
    // the defaults of the selectors
    check (a->getParameter (a, sel0) == 0.0f && a->getParameter (a, sel0 + 1) == 0.0f && a->getParameter (a, sel0 + 2) == 0.0f && text (a, effGetParamDisplay, sel0 + 2) == "LFO 1", "the selectors start at 1 / 1 / 1");

    // ---- every proxy, every instance, both directions
    int writes = 0, bad = 0;
    for (int g = 0; g < pl::kGroups; ++g)
        for (int inst = 1; inst <= pl::kInstances[g]; ++inst)
        {
            a->setParameter (a, sel0 + g, pl::selectorNormalised (g, inst));
            if (text (a, effGetParamDisplay, sel0 + g) != pl::selectorText (g, inst)) { ++bad; std::printf ("FAIL  selector text %s\n", pl::selectorText (g, inst).c_str()); }
            for (int s = 0; s < pl::kSlots; ++s)
            {
                const int p = g * pl::kSlots + s, proxy = prox0 + p, real = pl::realIndex (g, inst, s);
                if (real < 0) { ++bad; continue; }
                // the proxy shows the real parameter: value, display text, label, properties
                if (a->getParameter (a, proxy) != a->getParameter (a, real)) { ++bad; std::printf ("FAIL  value of %s for instance %d\n", pl::slots (g)[s].proxyName, inst); }
                if (text (a, effGetParamDisplay, proxy) != text (a, effGetParamDisplay, real) || text (a, effGetParamLabel, proxy) != text (a, effGetParamLabel, real)) { ++bad; std::printf ("FAIL  text of %s for instance %d\n", pl::slots (g)[s].proxyName, inst); }
                VstParameterProperties pp, rp;
                std::memset (&pp, 0, sizeof pp); std::memset (&rp, 0, sizeof rp);
                const bool pOk = a->dispatcher (a, effGetParameterProperties, proxy, 0, &pp, 0.0f) == 1, rOk = a->dispatcher (a, effGetParameterProperties, real, 0, &rp, 0.0f) == 1;
                if (! pOk || ! rOk || std::memcmp (&pp, &rp, sizeof pp) != 0) { ++bad; std::printf ("FAIL  properties of %s for instance %d\n", pl::slots (g)[s].proxyName, inst); }
                // host sets the proxy: the REAL parameter of the selected instance changes, nothing else does
                std::vector<float> before;
                for (int other = 1; other <= pl::kInstances[g]; ++other)
                    before.push_back (a->getParameter (a, pl::realIndex (g, other, s)));
                const float target = std::fmod (0.31f + 0.173f * static_cast<float> (inst + s), 1.0f);
                a->setParameter (a, proxy, target);
                ++writes;
                bool onlyThis = true;
                for (int other = 1; other <= pl::kInstances[g]; ++other)
                {
                    const float now = a->getParameter (a, pl::realIndex (g, other, s));
                    onlyThis = onlyThis && (other == inst ? a->getParameter (a, proxy) == now : now == before[static_cast<size_t> (other - 1)]);
                }
                const synthyboy::ParamInfo& info = synthyboy::paramTable()[real];
                const float expected = synthyboy::toNormalised (info, synthyboy::constrainValue (info, synthyboy::fromNormalised (info, target)));
                if (! onlyThis || std::fabs (a->getParameter (a, real) - expected) > 1e-6f) { ++bad; std::printf ("FAIL  write of %s for instance %d (real %g expected %g)\n", pl::slots (g)[s].proxyName, inst, static_cast<double> (a->getParameter (a, real)), static_cast<double> (expected)); }
                // the real parameter changes by its own route: the proxy follows
                a->setParameter (a, real, 1.0f - target);
                if (a->getParameter (a, proxy) != a->getParameter (a, real)) { ++bad; std::printf ("FAIL  proxy %s does not follow the real parameter of instance %d\n", pl::slots (g)[s].proxyName, inst); }
                // a write to a real parameter of ANOTHER instance does not move the proxy
                const int otherInst = inst == 1 ? 2 : 1;
                if (otherInst <= pl::kInstances[g])
                {
                    const float pv = a->getParameter (a, proxy);
                    a->setParameter (a, pl::realIndex (g, otherInst, s), 0.77f);
                    if (a->getParameter (a, proxy) != pv) { ++bad; std::printf ("FAIL  proxy %s moved by instance %d\n", pl::slots (g)[s].proxyName, otherInst); }
                }
            }
        }
    check (bad == 0, "every proxy of every instance: value, text, label, properties are the real parameter's; a write reaches exactly that parameter; the proxy follows its own route (" + std::to_string (writes) + " writes)");
    closeIt (a);

    // ---- a selector change re-reports exactly the group's proxies, once
    a = make();
    sleepMs (400);
    resetReports (a);
    a->setParameter (a, sel0 + 1, pl::selectorNormalised (1, 4));
    check (waitFor (1500, [&] { for (int s = 0; s < pl::kSlots; ++s) if (reportOf (a, prox0 + pl::kSlots + s).count < 1) return false; return true; }), "Play Env 4: the host is told the eight envelope proxies");
    sleepMs (500);
    bool once = true, others = true;
    for (int s = 0; s < pl::kSlots; ++s)
    {
        once = once && reportOf (a, prox0 + pl::kSlots + s).count == 1 && reportOf (a, prox0 + pl::kSlots + s).last == a->getParameter (a, prox0 + pl::kSlots + s);
        others = others && reportOf (a, prox0 + s).count == 0 && reportOf (a, prox0 + 2 * pl::kSlots + s).count == 0;
    }
    check (once, "each of the eight envelope proxies was reported exactly once, with the value of the new instance");
    check (others, "the proxies of the other two groups were not reported");
    check (reportOf (a, sel0 + 1).count <= 1, "the selector itself at most once");
    int realReports = 0;
    for (int i = 0; i < n; ++i) realReports += reportOf (a, i).count;
    check (realReports == 0, "no real parameter was reported by a selector change");
    // the same selection again: nothing
    resetReports (a);
    a->setParameter (a, sel0 + 1, pl::selectorNormalised (1, 4));
    sleepMs (500);
    check (reportOf (a, prox0 + pl::kSlots).count == 0, "the same selection again reports nothing");
    // 20 quick changes: the group is reported (coalesced), never more often than once per tick of the display thread
    resetReports (a);
    for (int k = 0; k < 20; ++k)
        a->setParameter (a, sel0 + 1, pl::selectorNormalised (1, 1 + (k % 6)));
    sleepMs (600);
    check (reportOf (a, prox0 + pl::kSlots).count <= 3 && reportOf (a, prox0 + pl::kSlots).last == a->getParameter (a, prox0 + pl::kSlots), "20 changes in a burst: a few coalesced reports, the last one is the current value");

    // ---- a host write of a proxy is reported on the real parameter (its own tab moves), not on the proxy; the real parameter written elsewhere is reported on the proxy
    a->setParameter (a, sel0, pl::selectorNormalised (0, 2));
    sleepMs (300);
    resetReports (a);
    const int real = pl::realIndex (0, 2, 7);
    a->setParameter (a, prox0 + 7, 0.42f);
    check (waitFor (1500, [&] { return reportOf (a, real).count >= 1; }) && std::fabs (reportOf (a, real).last - a->getParameter (a, real)) < 1e-6f, "a proxy write: the host is told that Osc 2 Level changed (its own tab)");
    sleepMs (300);
    check (reportOf (a, prox0 + 7).count == 0, "... and the proxy is not reported back to the host that wrote it");
    resetReports (a);
    a->setParameter (a, real, 0.9f);
    check (waitFor (1500, [&] { return reportOf (a, prox0 + 7).count >= 1; }) && std::fabs (reportOf (a, prox0 + 7).last - 0.9f) < 1e-5f, "a write on the module's own tab: the proxy is reported with the new value");

    // ---- preset change: the selectors stay, the proxies follow
    a->setParameter (a, sel0 + 1, pl::selectorNormalised (1, 3));
    a->setParameter (a, pl::realIndex (1, 3, 0), 0.8f);   // Env 3 attack
    sleepMs (300);
    const float proxyBefore = a->getParameter (a, prox0 + pl::kSlots);
    resetReports (a);
    a->dispatcher (a, effSetProgram, 0, 0, nullptr, 0.0f);   // Init: the engine's defaults
    check (a->getParameter (a, sel0 + 1) == pl::selectorNormalised (1, 3) && a->getParameter (a, sel0) == pl::selectorNormalised (0, 2), "a preset change keeps the selectors");
    const float def = synthyboy::toNormalised (synthyboy::paramTable()[pl::realIndex (1, 3, 0)], synthyboy::paramTable()[pl::realIndex (1, 3, 0)].def);
    check (std::fabs (proxyBefore - def) > 0.01f && std::fabs (a->getParameter (a, prox0 + pl::kSlots) - def) < 1e-6f, "... and the proxy has the value of the new sound");
    check (waitFor (1500, [&] { return reportOf (a, prox0 + pl::kSlots).count >= 1; }) && std::fabs (reportOf (a, prox0 + pl::kSlots).last - def) < 1e-6f, "... and the host is told");

    // ---- chunk: round trip of the selectors, an older chunk leaves them
    a->setParameter (a, sel0, pl::selectorNormalised (0, 3));
    a->setParameter (a, sel0 + 1, pl::selectorNormalised (1, 5));
    a->setParameter (a, sel0 + 2, pl::selectorNormalised (2, 9));
    const std::string chunk = getChunk (a);
    check (chunk.find ("\"mpcPlayOsc\":3,\"mpcPlayEnv\":5,\"mpcPlayLfo\":9") != std::string::npos, "the chunk carries the three selectors");
    check (chunk.find ("mpcPadPressure") != std::string::npos && chunk.rfind ("mpcPadPressure") > chunk.rfind ("mpcPlayOsc"), "... before the pad pressure members, which stay at the very end");
    AEffect* b = make();
    check (b->getParameter (b, sel0) == 0.0f && b->getParameter (b, sel0 + 1) == 0.0f && b->getParameter (b, sel0 + 2) == 0.0f, "a new instance starts at 1 / 1 / 1");
    check (setChunk (b, chunk) && text (b, effGetParamDisplay, sel0) == "Osc 3" && text (b, effGetParamDisplay, sel0 + 1) == "Env 5" && text (b, effGetParamDisplay, sel0 + 2) == "LFO 9", "chunk round trip: Osc 3 / Env 5 / LFO 9");
    check (waitFor (1500, [&] { return reportOf (b, sel0 + 2).count >= 1; }), "the host is told about the selectors of a restored chunk");
    check (getChunk (b) == chunk, "the chunk of the restored instance is the same");
    // an older chunk (without the members) leaves the selectors as they are, and loads
    std::string old = chunk;
    const size_t cut = old.find (",\"mpcPlayOsc\"");
    const size_t end = old.find ("\"mpcPlayLfo\":9") + std::strlen ("\"mpcPlayLfo\":9");
    old.erase (cut, end - cut);
    check (old.find ("mpcPlay") == std::string::npos, "(an older chunk: the three members removed)");
    AEffect* c = make();
    check (setChunk (c, old) && c->getParameter (c, sel0) == 0.0f && c->getParameter (c, sel0 + 1) == 0.0f && c->getParameter (c, sel0 + 2) == 0.0f, "an old chunk loads and the selectors are 1 / 1 / 1");
    b->setParameter (b, sel0 + 1, pl::selectorNormalised (1, 2));
    check (setChunk (b, old) && text (b, effGetParamDisplay, sel0 + 1) == "Env 2", "an old chunk leaves the selectors as they are");
    // an out of range selector in a chunk is not taken
    std::string bad2 = chunk;
    bad2.replace (bad2.find ("\"mpcPlayEnv\":5"), 14, "\"mpcPlayEnv\":9");
    check (setChunk (b, bad2) && text (b, effGetParamDisplay, sel0 + 1) == "Env 2", "a selector out of range (Env 9) in a chunk is ignored as a whole");
    closeIt (b);
    closeIt (c);

    // ---- the "chunk wins" protection is not disarmed by the reports (nor by a host that echoes them): the program selection that follows a restored chunk changes nothing
    {
        AEffect* p = make();
        p->setParameter (p, pl::realIndex (1, 2, 4), 0.66f);   // Env 2 release
        p->setParameter (p, sel0 + 1, pl::selectorNormalised (1, 2));
        const std::string ch = getChunk (p);
        const float edited = p->getParameter (p, pl::realIndex (1, 2, 4));
        closeIt (p);
        AEffect* q = make();
        echo.store (true);
        check (setChunk (q, ch) && std::fabs (q->getParameter (q, pl::realIndex (1, 2, 4)) - edited) < 1e-6f, "the chunk of an edited sound is restored");
        sleepMs (700);   // the display thread reports the selectors and the proxies; the echoing host writes them back
        q->dispatcher (q, effSetProgram, 0, 0, nullptr, 0.0f);   // what the MPC does right after a restored chunk
        check (std::fabs (q->getParameter (q, pl::realIndex (1, 2, 4)) - edited) < 1e-6f, "the proxy reports and their echoes did not disarm the protection: the restored sound stays");
        q->dispatcher (q, effSetProgram, 0, 0, nullptr, 0.0f);   // a second selection is a real one
        check (std::fabs (q->getParameter (q, pl::realIndex (1, 2, 4)) - edited) > 0.01f, "(a second program selection loads the program: the protection was armed once)");
        echo.store (false);
        closeIt (q);
    }

    // ---- no feedback loop: a host that echoes every report back
    echo.store (true);
    a->setParameter (a, sel0 + 2, pl::selectorNormalised (2, 4));
    sleepMs (400);
    const int base = totalReports.load();
    for (int k = 0; k < 10; ++k)
    {
        a->setParameter (a, prox0 + 2 * pl::kSlots + (k % 4), 0.1f + 0.07f * static_cast<float> (k));
        sleepMs (30);
    }
    sleepMs (1200);
    const int after = totalReports.load();
    sleepMs (1200);
    const int stable = totalReports.load();
    check (stable == after, "echoing host: the reports stop (no feedback loop), " + std::to_string (after - base) + " reports for 10 writes");
    check (after - base <= 25, "echoing host: at most a few reports per write");
    // a selector change under the echo: the same (the reports of the earlier writes may still be on their way when a run is slow, so the count is taken when it has been still for a while)
    sleepMs (1500);
    const int b2 = totalReports.load();
    a->setParameter (a, sel0 + 2, pl::selectorNormalised (2, 7));
    int last = -1, still = 0;
    for (int k = 0; k < 120 && still < 12; ++k)
    {
        sleepMs (50);
        const int now = totalReports.load();
        still = now == last ? still + 1 : 0;
        last = now;
    }
    check (still >= 12 && totalReports.load() - b2 <= 14 && a->getParameter (a, prox0 + 2 * pl::kSlots) == a->getParameter (a, pl::realIndex (2, 7, 0)), "echoing host: a selector change causes the 8 proxy reports (+ selector + echoes), then nothing runs on (" + std::to_string (totalReports.load() - b2) + " reports)");
    echo.store (false);
    closeIt (a);
    std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
