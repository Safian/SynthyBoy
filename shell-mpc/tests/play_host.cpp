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
#include "q_router.h"
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
#include <unistd.h>
#include <vector>

namespace pl = synthyboy_mpc::play;

struct Report { int count = 0; float last = -1.0f; };
static std::mutex reportMu;
static std::map<AEffect*, std::map<int, Report>> reports;
static std::atomic<bool> echo { false };   // a host that sets every reported value back (the worst case for a feedback loop)
static std::atomic<int> totalReports { 0 };
static std::map<AEffect*, int> updateDisplays;   // audioMasterUpdateDisplay calls of each instance (the host re-reads the names of the Q proxies); under reportMu

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
    if (opcode == audioMasterUpdateDisplay)
    {
        std::lock_guard<std::mutex> g (reportMu);
        ++updateDisplays[e];
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

static std::string qFake;   // the file that sets the Q group of the plugin (a debug hook, see PluginInstance::qFakeGroup)
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
    qFake = "/tmp/synthyboy-q-fake-group-playhost-" + std::to_string (static_cast<long> (getpid()));
    setenv ("SYNTHYBOY_MPC_Q_FAKE_FILE", qFake.c_str(), 1);   // the plugin reads it when it first needs it (the Q proxies); removed at the end
    std::remove (qFake.c_str());
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
        check (osc == 3 && pl::kInstances[0] == 5 && env == pl::kInstances[1] && lfo == pl::kInstances[2], "the engine has 3 wavetable oscillators (+ Sub and Noise = 5 selector instances), " + std::to_string (env) + " envelopes and " + std::to_string (lfo) + " LFOs: the selectors' ranges");
        std::string why;
        const bool uni = pl::uniform (&why);
        check (uni, "every instance of a group has the same kinds of parameter (range, steps, unit, choices): " + why);
    }

    AEffect* a = make();
    if (! a) { std::fprintf (stderr, "plugin did not load\n"); return 2; }
    const int ovl = n + 4 + 32 + 2;   // "Overlay Instance"
    const int sel0 = ovl + 1, prox0 = ovl + 1 + pl::kSelectors;
    const int q0 = ovl + 1 + pl::kParams;   // "Q 1"
    // THE FORMAT (docs/MPC.md "Parameter order"): saved projects keep parameter indices, so the old ones must never move. Q 1 .. Q 4 stay at 2703 .. 2706, the twelve new proxies Q 5 .. Q 16 are appended after them
    // (2707 .. 2718), the count is 2719. Every number is written out on purpose: a change of the layout fails here.
    check (n == 2637 && ovl == 2675 && sel0 == 2676 && prox0 == 2679 && q0 == 2703, "the old indices: engine 0 .. 2636, Overlay Instance 2675, Play selectors 2676 .. 2678, Play proxies 2679 .. 2702, Q 1 at 2703");
    check (synthyboy_mpc::QRouter::kCount == 16 && a->numParams == q0 + 16 && a->numParams == 2719, "numParams is the engine's count + 4 + 32 + 2 + Overlay Instance + 3 selectors + 24 proxies + 16 Q proxies = 2719 (" + std::to_string (a->numParams) + ")");
    {
        bool qn = true;
        for (int i = 0; i < 16; ++i)
            qn = qn && text (a, effGetParamName, q0 + i) == "Q " + std::to_string (i + 1) && a->dispatcher (a, effCanBeAutomated, q0 + i, 0, nullptr, 0.0f) == 0;
        check (qn && text (a, effGetParamName, q0 - 1) == pl::slots (2)[7].proxyName, "order: the last Play proxy, then Q 1 .. Q 16; the Q proxies are not automatable");
        check (text (a, effGetParamName, 2703) == "Q 1" && text (a, effGetParamName, 2706) == "Q 4" && text (a, effGetParamName, 2707) == "Q 5" && text (a, effGetParamName, 2718) == "Q 16", "Q 1 .. Q 4 at 2703 .. 2706 (as in the first builds), Q 5 at 2707, Q 16 at 2718");
        check (text (a, effGetParamName, 0) == synthyboy::paramTable()[0].name && text (a, effGetParamName, n - 1) == synthyboy::paramTable()[n - 1].name && text (a, effGetParamName, n) == "LFO 1 Cursor" && text (a, effGetParamName, n + 4) == "Spectrum 1"
                   && text (a, effGetParamName, n + 36) == "Pad Pressure" && text (a, effGetParamName, n + 37) == "Pad Pressure Amount", "the indices below the overlay block did not move either");
    }
    check (text (a, effGetParamName, ovl) == "Overlay Instance" && text (a, effGetParamName, sel0) == "Play Osc" && text (a, effGetParamName, sel0 + 1) == "Play Env" && text (a, effGetParamName, sel0 + 2) == "Play LFO", "order: Overlay Instance, Play Osc, Play Env, Play LFO");
    bool names = true, automation = true;
    for (int g = 0; g < pl::kGroups; ++g)
        for (int s = 0; s < pl::kSlots; ++s)
            names = names && text (a, effGetParamName, prox0 + g * pl::kSlots + s) == pl::slots (g)[s].proxyName;
    check (names, "the 24 proxies are named as in the table, group after group");
    check (text (a, effGetParamDisplay, sel0) == "Sub" && pl::selectorText (0, 2) == "Osc A" && pl::selectorText (0, 5) == "Noise" && pl::kInstances[0] == 5, "the oscillator selector has five instances: Sub, Osc A, Osc B, Osc C, Noise");
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
    {
        // no mark of a changed program in any name the host reads (removed by design: docs/MPC.md "Program name")
        AEffect* pm = make();
        char nm[64] = {};
        auto endsWithStar = [] (const char* n) { std::string t = n; while (! t.empty() && t.back() == ' ') t.pop_back(); return ! t.empty() && t.back() == '*'; };
        pm->dispatcher (pm, effGetProgramName, 0, 0, nm, 0.0f);
        check (! endsWithStar (nm), std::string ("a fresh instance: the program name has no mark (") + nm + ")");
        pm->setParameter (pm, 0, 0.123f);
        std::memset (nm, 0, sizeof nm);
        pm->dispatcher (pm, effGetProgramName, 0, 0, nm, 0.0f);
        check (! endsWithStar (nm), std::string ("a parameter edit: the name has no mark (") + nm + ")");
        pm->dispatcher (pm, effSetProgram, 0, 1, nullptr, 0.0f);
        std::memset (nm, 0, sizeof nm);
        pm->dispatcher (pm, effGetProgramName, 0, 0, nm, 0.0f);
        check (! endsWithStar (nm), std::string ("loading a program: the name has no mark (") + nm + ")");
        pm->setParameter (pm, 0, 0.5f);
        bool listed = true;
        for (int i = 0; i < 400; ++i)
        {
            std::memset (nm, 0, sizeof nm);
            if (pm->dispatcher (pm, effGetProgramNameIndexed, i, 0, nm, 0.0f) != 1) break;
            listed = listed && ! endsWithStar (nm);
        }
        check (listed, "after a parameter edit no listed program name (effGetProgramNameIndexed) has a mark");
        closeIt (pm);
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
                if (real < 0)
                {
                    // Sub and Noise name four parameters (the first four slots); the other four slots have none: the proxy reads 0, shows nothing and ignores a write
                    if (g != 0 || (inst != 1 && inst != 5) || s < 4) { ++bad; std::printf ("FAIL  no real parameter for %s instance %d\n", pl::slots (g)[s].proxyName, inst); continue; }
                    a->setParameter (a, proxy, 0.6f);
                    if (a->getParameter (a, proxy) != 0.0f || ! text (a, effGetParamDisplay, proxy).empty() || ! text (a, effGetParamLabel, proxy).empty()) { ++bad; std::printf ("FAIL  empty slot %s instance %d\n", pl::slots (g)[s].proxyName, inst); }
                    VstParameterProperties ep;
                    std::memset (&ep, 0, sizeof ep);
                    if (a->dispatcher (a, effGetParameterProperties, proxy, 0, &ep, 0.0f) != 0) { ++bad; std::printf ("FAIL  properties of the empty slot %s instance %d\n", pl::slots (g)[s].proxyName, inst); }
                    ++writes;
                    continue;
                }
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
                    before.push_back (pl::realIndex (g, other, s) >= 0 ? a->getParameter (a, pl::realIndex (g, other, s)) : 0.0f);
                const float target = std::fmod (0.31f + 0.173f * static_cast<float> (inst + s), 1.0f);
                a->setParameter (a, proxy, target);
                ++writes;
                bool onlyThis = true;
                for (int other = 1; other <= pl::kInstances[g]; ++other)
                {
                    if (pl::realIndex (g, other, s) < 0)
                        continue;
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
                if (otherInst <= pl::kInstances[g] && pl::realIndex (g, otherInst, s) >= 0)
                {
                    const float pv = a->getParameter (a, proxy);
                    a->setParameter (a, pl::realIndex (g, otherInst, s), 0.77f);
                    if (a->getParameter (a, proxy) != pv) { ++bad; std::printf ("FAIL  proxy %s moved by instance %d\n", pl::slots (g)[s].proxyName, otherInst); }
                }
            }
        }
    check (bad == 0, "every proxy of every instance: value, text, label, properties are the real parameter's; a write reaches exactly that parameter; the proxy follows its own route (" + std::to_string (writes) + " writes)");
    {
        // the mapping of the oscillator selector: Sub, A, B, C, Noise; Sub and Noise name the four parameters of their Basic group in the first four slots (docs/MPC.md "Play page")
        auto idx = [] (const char* key) { const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key); return p ? synthyboy::findParamIndex (p->id) : -1; };
        const char* const sub[4] = { "sub.waveform", "sub.octave", "sub.pan", "sub.level" };
        const char* const noise[4] = { "noise.sample", "noise.filter", "noise.pan", "noise.level" };
        bool ok = true;
        for (int k = 0; k < 4; ++k)
            ok = ok && idx (sub[k]) >= 0 && idx (noise[k]) >= 0 && pl::realIndex (0, 1, k) == idx (sub[k]) && pl::realIndex (0, 5, k) == idx (noise[k]);
        for (int s = 4; s < pl::kSlots; ++s)
            ok = ok && pl::realIndex (0, 1, s) == -1 && pl::realIndex (0, 5, s) == -1;
        for (int o = 1; o <= 3; ++o)
            for (int s = 0; s < pl::kSlots; ++s)
                ok = ok && pl::realIndex (0, o + 1, s) == idx ((std::string ("osc") + std::to_string (o) + "." + pl::slots (0)[s].suffix).c_str());
        check (ok, "Sub = [waveform, octave, pan, level], Noise = [type, filter, pan, level] in slots 1 .. 4, A / B / C = osc1 .. osc3 as before (instances 2 .. 4)");
        // a write through the Sub proxies reaches sub.* and nothing of the oscillators
        a->setParameter (a, sel0, pl::selectorNormalised (0, 1));
        const float osc1Level = a->getParameter (a, idx ("osc1.level"));
        a->setParameter (a, prox0 + 3, 0.25f);
        check (std::fabs (a->getParameter (a, idx ("sub.level")) - 0.25f) < 1e-6f && a->getParameter (a, idx ("osc1.level")) == osc1Level && text (a, effGetParamDisplay, prox0 + 3) == text (a, effGetParamDisplay, idx ("sub.level")), "Sub selected: the Level proxy is sub.level");
        a->setParameter (a, sel0, pl::selectorNormalised (0, 5));
        a->setParameter (a, prox0, 1.0f);
        check (a->getParameter (a, idx ("noise.sample")) == 1.0f && text (a, effGetParamDisplay, prox0) == text (a, effGetParamDisplay, idx ("noise.sample")), "Noise selected: the first proxy is noise.sample");
    }
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
    a->setParameter (a, sel0, pl::selectorNormalised (0, 3));
    sleepMs (300);
    resetReports (a);
    const int real = pl::realIndex (0, 3, 7);
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
    check (a->getParameter (a, sel0 + 1) == pl::selectorNormalised (1, 3) && a->getParameter (a, sel0) == pl::selectorNormalised (0, 3), "a preset change keeps the selectors");
    const float def = synthyboy::toNormalised (synthyboy::paramTable()[pl::realIndex (1, 3, 0)], synthyboy::paramTable()[pl::realIndex (1, 3, 0)].def);
    check (std::fabs (proxyBefore - def) > 0.01f && std::fabs (a->getParameter (a, prox0 + pl::kSlots) - def) < 1e-6f, "... and the proxy has the value of the new sound");
    check (waitFor (1500, [&] { return reportOf (a, prox0 + pl::kSlots).count >= 1; }) && std::fabs (reportOf (a, prox0 + pl::kSlots).last - def) < 1e-6f, "... and the host is told");

    // ---- chunk: round trip of the selectors, an older chunk leaves them
    a->setParameter (a, sel0, pl::selectorNormalised (0, 3));
    a->setParameter (a, sel0 + 1, pl::selectorNormalised (1, 5));
    a->setParameter (a, sel0 + 2, pl::selectorNormalised (2, 9));
    const std::string chunk = getChunk (a);
    check (chunk.find ("\"mpcPlayOsc5\":3,\"mpcPlayEnv\":5,\"mpcPlayLfo\":9") != std::string::npos && chunk.find ("\"mpcPlayOsc\":") == std::string::npos, "the chunk carries the three selectors (the oscillator one under its five-instance name mpcPlayOsc5)");
    check (chunk.find ("mpcPadPressure") != std::string::npos && chunk.rfind ("mpcPadPressure") > chunk.rfind ("mpcPlayOsc"), "... before the pad pressure members, which stay at the very end");
    AEffect* b = make();
    check (b->getParameter (b, sel0) == 0.0f && b->getParameter (b, sel0 + 1) == 0.0f && b->getParameter (b, sel0 + 2) == 0.0f, "a new instance starts at 1 / 1 / 1");
    check (setChunk (b, chunk) && text (b, effGetParamDisplay, sel0) == "Osc B" && text (b, effGetParamDisplay, sel0 + 1) == "Env 5" && text (b, effGetParamDisplay, sel0 + 2) == "LFO 9", "chunk round trip: Osc B / Env 5 / LFO 9");
    check (waitFor (1500, [&] { return reportOf (b, sel0 + 2).count >= 1; }), "the host is told about the selectors of a restored chunk");
    check (getChunk (b) == chunk, "the chunk of the restored instance is the same");
    // an older chunk (without the members) leaves the selectors as they are, and loads
    std::string old = chunk;
    const size_t cut = old.find (",\"mpcPlayOsc5\"");
    const size_t end = old.find ("\"mpcPlayLfo\":9") + std::strlen ("\"mpcPlayLfo\":9");
    old.erase (cut, end - cut);
    check (old.find ("mpcPlay") == std::string::npos, "(an older chunk: the three members removed)");
    AEffect* c = make();
    check (setChunk (c, old) && c->getParameter (c, sel0) == 0.0f && c->getParameter (c, sel0 + 1) == 0.0f && c->getParameter (c, sel0 + 2) == 0.0f, "an old chunk loads and the selectors are 1 / 1 / 1");
    b->setParameter (b, 3, 1.0f - b->getParameter (b, 3));   // a real edit ends the restore window of the chunk (docs/MPC.md "Project save and reload"): a selector write is ignored while it is open
    b->setParameter (b, sel0 + 1, pl::selectorNormalised (1, 2));
    check (setChunk (b, old) && text (b, effGetParamDisplay, sel0 + 1) == "Env 2", "an old chunk leaves the selectors as they are");
    // an out of range selector in a chunk is not taken
    std::string bad2 = chunk;
    bad2.replace (bad2.find ("\"mpcPlayEnv\":5"), 14, "\"mpcPlayEnv\":9");
    check (setChunk (b, bad2) && text (b, effGetParamDisplay, sel0 + 1) == "Env 2", "a selector out of range (Env 9) in a chunk is ignored as a whole");
    // the chunk of the build with THREE oscillator instances ("mpcPlayOsc" 1 .. 3 = A, B, C): the same oscillator after the migration (the selector's instances are now Sub, A, B, C, Noise)
    {
        const char* const expect[3] = { "Osc A", "Osc B", "Osc C" };
        bool ok = true, resaved = true;
        for (int k = 1; k <= 3; ++k)
        {
            std::string threeInst = chunk;
            threeInst.replace (threeInst.find ("\"mpcPlayOsc5\":3"), std::strlen ("\"mpcPlayOsc5\":3"), "\"mpcPlayOsc\":" + std::to_string (k));
            AEffect* m = make();
            ok = ok && setChunk (m, threeInst) && text (m, effGetParamDisplay, sel0) == expect[k - 1] && m->getParameter (m, sel0) == pl::selectorNormalised (0, k + 1)
                    && pl::realIndex (0, k + 1, 7) == synthyboy::findParamIndex (synthyboy::findParamByKey (("osc" + std::to_string (k) + ".level").c_str())->id);
            const std::string again = getChunk (m);
            resaved = resaved && again.find ("\"mpcPlayOsc5\":" + std::to_string (k + 1) + ",") != std::string::npos && again.find ("\"mpcPlayOsc\":") == std::string::npos;
            closeIt (m);
        }
        check (ok, "a chunk of the three-instance build: old Osc 1 / 2 / 3 load as Osc A / B / C, the same oscillator as before");
        check (resaved, "... and are saved again in the new form (mpcPlayOsc5 with the new number)");
        std::string tooBig = chunk;
        tooBig.replace (tooBig.find ("\"mpcPlayOsc5\":3"), std::strlen ("\"mpcPlayOsc5\":3"), "\"mpcPlayOsc\":4");   // 4 was never a value of the old build
        AEffect* m = make();
        check (setChunk (m, tooBig) && text (m, effGetParamDisplay, sel0) == "Sub" && text (m, effGetParamDisplay, sel0 + 1) == "Env 1", "an old-form value out of its 1 .. 3 is ignored as a whole (no new numbering is read from the old name)");
        // the new numbering: Sub (1) and Noise (5) are values of mpcPlayOsc5
        std::string noiseChunk = chunk, subChunk = chunk;
        noiseChunk.replace (noiseChunk.find ("\"mpcPlayOsc5\":3"), std::strlen ("\"mpcPlayOsc5\":3"), "\"mpcPlayOsc5\":5");
        subChunk.replace (subChunk.find ("\"mpcPlayOsc5\":3"), std::strlen ("\"mpcPlayOsc5\":3"), "\"mpcPlayOsc5\":1");
        AEffect* m2 = make();
        check (setChunk (m2, noiseChunk) && text (m2, effGetParamDisplay, sel0) == "Noise", "mpcPlayOsc5 = 5 is Noise");
        closeIt (m2);
        m2 = make();
        m2->setParameter (m2, sel0, pl::selectorNormalised (0, 4));
        check (setChunk (m2, subChunk) && text (m2, effGetParamDisplay, sel0) == "Sub", "mpcPlayOsc5 = 1 is Sub");
        std::string beyond = chunk;
        beyond.replace (beyond.find ("\"mpcPlayOsc5\":3"), std::strlen ("\"mpcPlayOsc5\":3"), "\"mpcPlayOsc5\":6");
        check (setChunk (m2, beyond) && text (m2, effGetParamDisplay, sel0) == "Sub", "mpcPlayOsc5 = 6 is ignored as a whole");
        closeIt (m2);
        closeIt (m);
        // the parser alone, on the exact tails
        int sel[3] = { 0, 0, 0 };
        const std::string tail3 = std::string (60, ' ') + "\"x\":1,\"mpcPlayOsc\":2,\"mpcPlayEnv\":3,\"mpcPlayLfo\":4}";
        check (pl::playFromChunk (tail3.data(), tail3.size(), sel) && sel[0] == 3 && sel[1] == 3 && sel[2] == 4, "playFromChunk: old osc 2 (B) is instance 3");
        const std::string tail5 = std::string (60, ' ') + "\"x\":1,\"mpcPlayOsc5\":5,\"mpcPlayEnv\":6,\"mpcPlayLfo\":10}";
        check (pl::playFromChunk (tail5.data(), tail5.size(), sel) && sel[0] == 5 && sel[1] == 6 && sel[2] == 10, "playFromChunk: the new form");
    }
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

    // ---- the Q proxies (shell-mpc/q_router.h): what the group the UI sets maps the four Q parameters to. The UI is not in this test: the debug file of the plugin sets the group (SYNTHYBOY_MPC_Q_FAKE_FILE)
    {
        auto idx = [] (const char* key) { const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key); return p ? synthyboy::findParamIndex (p->id) : -1; };
        const int tLevel = idx ("osc1.level"), tPan = idx ("osc1.pan"), tSubLevel = idx ("sub.level"), tCut = idx ("filter1.cutoff");
        auto writeGroup = [&] (const std::string& line) { if (FILE* f = std::fopen (qFake.c_str(), "w")) { std::fputs ((line + "\n").c_str(), f); std::fclose (f); } };
        AEffect* qa = make();
        sleepMs (400);
        bool none = true;
        for (int i = 0; i < 4; ++i)
            none = none && qa->getParameter (qa, q0 + i) == 0.0f && text (qa, effGetParamDisplay, q0 + i).empty();
        check (none, "with no group the four Q proxies read 0 and show nothing");
        resetReports (qa);
        auto updates = [&] { std::lock_guard<std::mutex> g (reportMu); return updateDisplays[qa]; };
        const int ud0 = updates();
        writeGroup (std::to_string (tLevel) + " -1 " + std::to_string (tPan) + " " + std::to_string (tSubLevel));
        check (waitFor (2000, [&] { for (int i = 0; i < 4; ++i) if (reportOf (qa, q0 + i).count < 1) return false; return true; }), "a group is set: the host is told the values of all four Q proxies");
        sleepMs (400);
        bool once = true;
        for (int i = 0; i < 4; ++i)
            once = once && reportOf (qa, q0 + i).count == 1 && reportOf (qa, q0 + i).last == qa->getParameter (qa, q0 + i);
        check (once, "... once each, with the value the plugin reports on a read");
        check (updates() == ud0 + 1, "... and, once for the whole group, audioMasterUpdateDisplay: the host reads the new names again (" + std::to_string (updates() - ud0) + ")");
        check (qa->getParameter (qa, q0) == qa->getParameter (qa, tLevel) && qa->getParameter (qa, q0 + 1) == 0.0f && qa->getParameter (qa, q0 + 2) == qa->getParameter (qa, tPan) && qa->getParameter (qa, q0 + 3) == qa->getParameter (qa, tSubLevel),
              "getParameter on Q n is the target's value (0 for none)");
        check (text (qa, effGetParamDisplay, q0) == text (qa, effGetParamDisplay, tLevel) && text (qa, effGetParamLabel, q0) == text (qa, effGetParamLabel, tLevel) && text (qa, effGetParamDisplay, q0 + 1).empty()
               && text (qa, effGetParamName, q0 + 1) == "Q 2", "display text and label are the target's, a Q without a target shows nothing (the name stays Q n)");
        check (text (qa, effGetParamName, q0) == synthyboy::paramTable()[tLevel].name && text (qa, effGetParamName, q0 + 2) == synthyboy::paramTable()[tPan].name && text (qa, effGetParamName, q0 + 3) == synthyboy::paramTable()[tSubLevel].name,
               "the name of a Q is the name of its target (" + text (qa, effGetParamName, q0) + ", " + text (qa, effGetParamName, q0 + 2) + ", " + text (qa, effGetParamName, q0 + 3) + ")");
        {
            VstParameterProperties qp, rp;
            std::memset (&qp, 0, sizeof qp); std::memset (&rp, 0, sizeof rp);
            check (qa->dispatcher (qa, effGetParameterProperties, q0, 0, &qp, 0.0f) == 1 && qa->dispatcher (qa, effGetParameterProperties, tLevel, 0, &rp, 0.0f) == 1 && std::memcmp (&qp, &rp, sizeof qp) == 0, "the properties of a Q are its target's");
        }
        // setParameter on Q 1 writes the target and tells the host about the REAL parameter, not about the Q that was written
        resetReports (qa);
        qa->setParameter (qa, q0, 0.42f);
        const synthyboy::ParamInfo& li = synthyboy::paramTable()[tLevel];
        const float expected = synthyboy::toNormalised (li, synthyboy::constrainValue (li, synthyboy::fromNormalised (li, 0.42f)));
        check (std::fabs (qa->getParameter (qa, tLevel) - expected) < 1e-6f && qa->getParameter (qa, q0) == qa->getParameter (qa, tLevel), "setParameter on Q 1 writes the target (the same write as the host's own)");
        check (waitFor (1500, [&] { return reportOf (qa, tLevel).count >= 1; }) && std::fabs (reportOf (qa, tLevel).last - expected) < 1e-6f, "the host is told that the real parameter changed (its own page follows)");
        sleepMs (400);
        check (reportOf (qa, q0).count == 0, "... and the Q is not reported back to the host that wrote it");
        qa->setParameter (qa, q0, qa->getParameter (qa, q0));
        qa->setParameter (qa, q0 + 1, 0.9f);
        check (qa->getParameter (qa, q0 + 1) == 0.0f, "an echo writes nothing, a write to a Q without target is ignored");
        // the real parameter changes by its own route: the Q follows
        resetReports (qa);
        qa->setParameter (qa, tPan, 0.9f);
        check (waitFor (1500, [&] { return reportOf (qa, q0 + 2).count >= 1; }) && std::fabs (reportOf (qa, q0 + 2).last - 0.9f) < 1e-5f && qa->getParameter (qa, q0 + 2) == qa->getParameter (qa, tPan), "a write on the target's own page moves the Q (reported)");
        // a new group: the host is told the four new values
        resetReports (qa);
        writeGroup (std::to_string (tCut) + " " + std::to_string (tLevel) + " -1 -1");
        check (waitFor (2000, [&] { for (int i = 0; i < 4; ++i) if (reportOf (qa, q0 + i).count < 1) return false; return true; }) && reportOf (qa, q0).last == qa->getParameter (qa, tCut) && reportOf (qa, q0 + 2).last == 0.0f && reportOf (qa, q0 + 3).last == 0.0f,
              "another group: the host is told the four new values (0 for the Qs without a target)");
        check (text (qa, effGetParamName, q0) == synthyboy::paramTable()[tCut].name && text (qa, effGetParamName, q0 + 1) == synthyboy::paramTable()[tLevel].name && text (qa, effGetParamName, q0 + 2) == "Q 3",
              "a new group: the names follow (" + text (qa, effGetParamName, q0) + ", " + text (qa, effGetParamName, q0 + 1) + ", " + text (qa, effGetParamName, q0 + 2) + ")");
        {
            // a transpose that is split into octaves and semitones names its two knobs and shows each part
            const int tTrans = idx ("master.transpose");
            resetReports (qa);
            qa->setParameter (qa, tTrans, synthyboy::toNormalised (synthyboy::paramTable()[tTrans], 14.0f));
            writeGroup (std::to_string (tTrans | (synthyboy_mpc::QRouter::kOct << synthyboy_mpc::QRouter::kModeShift)) + " " + std::to_string (tTrans | (synthyboy_mpc::QRouter::kSem << synthyboy_mpc::QRouter::kModeShift)) + " -1 -1");
            check (waitFor (2000, [&] { return text (qa, effGetParamName, q0) == std::string (synthyboy::paramTable()[tTrans].name) + " Oct"; }) && text (qa, effGetParamName, q0 + 1) == std::string (synthyboy::paramTable()[tTrans].name) + " Sem"
                       && text (qa, effGetParamDisplay, q0) == "+1" && text (qa, effGetParamDisplay, q0 + 1) == "+2" && text (qa, effGetParamLabel, q0) == "oct" && text (qa, effGetParamLabel, q0 + 1) == "st",
                   "a split transpose: Oct and Sem names, +1 oct and +2 st for 14 (" + text (qa, effGetParamName, q0) + " " + text (qa, effGetParamDisplay, q0) + " / " + text (qa, effGetParamName, q0 + 1) + " " + text (qa, effGetParamDisplay, q0 + 1) + ")");
        }
        // stepped targets are driven relatively (QRouter, kStepTravelDefault): the Master page's VOICE OVERRIDE (a list) and a toggle step both ways with small knob increments, a continuous one does not change its behaviour
        {
            const int tList = idx ("master.voice_override"), tTog = idx ("master.legato");
            writeGroup (std::to_string (tList) + " " + std::to_string (tTog) + " -1 -1");
            sleepMs (700);
            const synthyboy::ParamInfo& li = synthyboy::paramTable()[tList];
            auto plain = [&] (int t) { return synthyboy::fromNormalised (synthyboy::paramTable()[t], qa->getParameter (qa, t)); };
            float held[2] = { qa->getParameter (qa, q0), qa->getParameter (qa, q0 + 1) };
            auto knob = [&] (int q, int tgt, int incs)   // the host: its value is the reported one plus the increments (1/128 each)
            {
                held[q] = std::clamp (held[q] + static_cast<float> (incs) / 128.0f, 0.0f, 1.0f);   // the host holds its own value and does not take our report
                qa->setParameter (qa, q0 + q, held[q]);
                (void) tgt;
            };
            const float l0 = plain (tList);
            for (int i = 0; i < 5; ++i) knob (0, tList, 1);   // five increments: one short of a step (6 / 128 of travel)
            const bool notYet = plain (tList) == l0;
            knob (0, tList, 1);
            const float l1 = plain (tList);
            knob (0, tList, -3); knob (0, tList, -3);
            const float l2 = plain (tList);
            (void) li;
            check (notYet && l1 == l0 + 1.0f && l2 == l0, "VOICE OVERRIDE (a two value list) steps after 6 increments of travel (not before) and back (the host keeps its own value here)");
            const float t0 = plain (tTog);
            knob (1, tTog, 6);
            const float t1 = plain (tTog);
            knob (1, tTog, -6);
            check (t1 != t0 && plain (tTog) == t0, "a toggle flips after 6 increments and flips back");
        }
        // a stepped target (here a two value list and a toggle) shows the host a plain knob: no integer range, a step of 1/128 (the host's own integer stepping made lists of 5 jump four entries for six increments on the Key 61)
        {
            const int tList = idx ("master.voice_override");
            writeGroup (std::to_string (tList) + " " + std::to_string (idx ("master.polyphony")) + " " + std::to_string (tLevel) + " -1");
            sleepMs (700);
            VstParameterProperties pl{}, pp{}, pc{};
            const bool got = qa->dispatcher (qa, effGetParameterProperties, q0, 0, &pl, 0.0f) == 1 && qa->dispatcher (qa, effGetParameterProperties, q0 + 1, 0, &pp, 0.0f) == 1 && qa->dispatcher (qa, effGetParameterProperties, q0 + 2, 0, &pc, 0.0f) == 1;
            check (got && (pl.flags & kVstParameterUsesIntegerMinMax) == 0 && pl.stepInteger == 0 && std::fabs (pl.stepFloat - 1.0f / 128.0f) < 1e-7f && (pp.flags & kVstParameterUsesIntegerMinMax) == 0 && std::fabs (pp.stepFloat - 1.0f / 128.0f) < 1e-7f,
                   "the Q of a list and the Q of an integer stepper (VOICE OVERRIDE, polyphony) are plain knobs of 1/128 for the host");
            check (got && std::fabs (pc.stepFloat - 1.0f / 128.0f) > 1e-7f, "the Q of a continuous target keeps its target's step");
        }
        // all sixteen slots: Q 5 .. Q 16 are proxies of the next groups; names, units, values and writes like Q 1 .. Q 4
        {
            const int sixteen[16] = { tLevel, tPan, tSubLevel, tCut, idx ("osc2.level"), idx ("osc2.pan"), idx ("osc3.level"), idx ("osc3.pan"), idx ("filter1.resonance"), idx ("filter2.cutoff"), idx ("filter2.resonance"), idx ("master.volume"), -1, idx ("osc1.unison.detune"), idx ("env1.attack"), idx ("env1.release") };
            std::string line;
            for (int i = 0; i < 16; ++i)
                line += (i ? " " : "") + std::to_string (sixteen[i]);
            resetReports (qa);
            const int ud1 = updates();
            writeGroup (line);
            check (waitFor (2000, [&] { for (int i = 0; i < 16; ++i) if (reportOf (qa, q0 + i).count < 1) return false; return true; }), "a 16 slot group: the host is told the values of all sixteen Q proxies");
            sleepMs (400);
            check (updates() == ud1 + 1, "... with one audioMasterUpdateDisplay for the whole change (" + std::to_string (updates() - ud1) + ")");
            bool ok = true;
            for (int i = 0; i < 16; ++i)
            {
                if (sixteen[i] < 0)
                {
                    ok = ok && text (qa, effGetParamName, q0 + i) == "Q " + std::to_string (i + 1) && qa->getParameter (qa, q0 + i) == 0.0f;
                    continue;
                }
                const synthyboy::ParamInfo& pi = synthyboy::paramTable()[sixteen[i]];
                ok = ok && text (qa, effGetParamName, q0 + i) == pi.name && text (qa, effGetParamLabel, q0 + i) == synthyboy::unitLabel (pi.unit)
                     && text (qa, effGetParamDisplay, q0 + i) == text (qa, effGetParamDisplay, sixteen[i]) && qa->getParameter (qa, q0 + i) == qa->getParameter (qa, sixteen[i]);
            }
            check (ok, "every one of Q 1 .. Q 16 reports the name, unit, value text and value of its target (a slot without a target: Q n, 0)");
            const float before = qa->getParameter (qa, sixteen[13]);
            qa->setParameter (qa, q0 + 13, std::fabs (before - 0.37f) < 0.05f ? 0.8f : 0.37f);   // Q 14: osc1.unison.detune
            check (std::fabs (qa->getParameter (qa, sixteen[13]) - before) > 0.01f && qa->getParameter (qa, q0 + 13) == qa->getParameter (qa, sixteen[13]), "a write to Q 14 reaches its target");
            const int ud2 = updates();
            writeGroup (line + " ");   // the same targets with a trailing blank: another line, the same slots
            sleepMs (500);
            check (updates() == ud2, "the same sixteen targets again: no notification to the host");
            std::string line2 = line;
            line2.replace (line2.rfind (' '), std::string::npos, " " + std::to_string (tLevel));   // only the last slot changes
            writeGroup (line2);
            check (waitFor (2000, [&] { return updates() == ud2 + 1; }), "a change of Q 16 alone: one notification");
            check (text (qa, effGetParamName, q0 + 15) == synthyboy::paramTable()[tLevel].name, "... and its name follows");
        }
        // the Q proxies are not in the chunk and a group does not change it
        const std::string withGroup = getChunk (qa);
        writeGroup ("-1 -1 -1 -1");
        sleepMs (400);
        check (withGroup == getChunk (qa) && withGroup.find ("mpcQ") == std::string::npos && withGroup.find ("\"Q ") == std::string::npos, "the chunk does not hold the Q proxies or the group");
        // the restore window: a write to a Q is ignored (the chunk wins), like a Play proxy
        writeGroup (std::to_string (tLevel) + " -1 -1 -1");
        sleepMs (400);
        qa->setParameter (qa, tPan, 0.5f);   // an ordinary edit ends any window of an earlier chunk
        const std::string project = getChunk (qa);
        const float kept = qa->getParameter (qa, tLevel);
        check (setChunk (qa, project), "(a chunk is restored: the window is open)");
        qa->setParameter (qa, q0, std::fabs (kept - 0.6f) < 0.05f ? 0.2f : 0.6f);
        check (qa->getParameter (qa, tLevel) == kept, "a write to a Q inside the restore window is ignored");
        qa->setParameter (qa, q0, kept);
        check (qa->getParameter (qa, tLevel) == kept, "... and its echo changes nothing");
        // not automatable, and a restored chunk does not touch the group
        check (qa->dispatcher (qa, effCanBeAutomated, q0, 0, nullptr, 0.0f) == 0, "a Q proxy is not offered for automation");
        closeIt (qa);
        std::remove (qFake.c_str());
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
