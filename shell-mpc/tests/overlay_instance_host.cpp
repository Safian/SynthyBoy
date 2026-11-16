// SynthyBoy - shell-mpc acceptance: the display parameter "Overlay Instance" of a plugin built with SYNTHYBOY_MPC_OVERLAY (shell-mpc/overlay/instance_slots.h, docs/MPC.md "Overlay").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: overlay_instance_host <plugin library with the overlay>
// Several instances live in the process: each gets the lowest free slot 0 .. 15 when it is created and keeps it until effClose; the slot is reused only after its instance closed; the
// instances beyond the 16th get none. The parameter is the last one of the list, not automatable, not in the chunk, writes are ignored; its value is the film strip frame of the slot /
// 32; the plugin reports it once after creation and again when the value changes (the debug file /tmp/synthyboy-overlay-fake-slot). Slots are also handed out correctly when
// instances are created and closed on several threads at once (run it under ThreadSanitizer too). Links the core (paramCount).
#include "mpc_vst2_abi.h"
#include "overlay/instance_slots.h"
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
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace synthyboy_mpc::overlay;

static const char* kFakeFile = "/tmp/synthyboy-overlay-fake-slot";

struct Report { int count = 0; float last = -1.0f; };
static std::mutex reportMu;
static std::map<AEffect*, std::map<int, Report>> reports;   // effect -> parameter index -> reports

static VstIntPtr hostCallback (AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float opt)
{
    if (opcode == audioMasterAutomate)
    {
        std::lock_guard<std::mutex> g (reportMu);
        Report& r = reports[e][index];
        ++r.count;
        r.last = opt;
        return 1;
    }
    return opcode == audioMasterVersion ? 2400 : 0;
}

static int fails = 0;
static std::mutex failMu;
static void check (bool ok, const std::string& what)
{
    std::lock_guard<std::mutex> g (failMu);
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++fails;
}

static Report reportOf (AEffect* e, int index)
{
    std::lock_guard<std::mutex> g (reportMu);
    return reports[e][index];
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
static void writeFile (const char* content)
{
    if (FILE* f = std::fopen (kFakeFile, "w")) { std::fputs (content, f); std::fclose (f); }
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
static void closeIt (AEffect* e)
{
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
}
/** The slot an instance reports through getParameter: frame / 32 -> slot, kSlotNone for none, kSlotUnknown for blank. */
static int slotOf (AEffect* e, int idx)
{
    return slotOfInstanceFrame (static_cast<int> (std::lround (e->getParameter (e, idx) * 32.0f)));
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: overlay_instance_host <plugin>\n"); return 2; }
    std::remove (kFakeFile);
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    const int n = static_cast<int> (synthyboy::paramCount());

    AEffect* a = make();
    if (! a) { std::fprintf (stderr, "plugin did not load\n"); return 2; }
    constexpr int kPlayParams = 27 + 16;   // the Play page's 3 selectors and 24 proxies and the 16 Q proxies after Overlay Instance (shell-mpc/play_params.h, shell-mpc/q_router.h; play_host.cpp checks them)
    const int idx = a->numParams - 1 - kPlayParams;   // Overlay Instance
    // parameter order (docs/MPC.md "Parameter order"): engine, 4 cursors, 32 spectrum columns, Pad Pressure, Pad Pressure Amount, Overlay Instance LAST
    check (text (a, effGetParamName, idx - 2) == "Pad Pressure" && text (a, effGetParamName, idx - 1) == "Pad Pressure Amount", "the two pad pressure parameters come right before it (their indexes do not move in the overlay build)");
    check (idx == n + 4 + 32 + 2, "Overlay Instance is the first parameter of the overlay block, after the 4 cursors, 32 spectrum columns and the 2 pad pressure parameters (index " + std::to_string (idx) + ")");
    bool have = false;
    check (text (a, effGetParamName, idx, &have) == "Overlay Instance" && have, "its name is Overlay Instance");
    bool beyond = true;
    text (a, effGetParamName, idx + 1 + kPlayParams, &beyond);
    check (! beyond, "nothing after the Play page's parameters, which follow it");
    check (a->dispatcher (a, effCanBeAutomated, idx, 0, nullptr, 0.0f) == 0, "it is not automatable");
    VstParameterProperties props;
    check (a->dispatcher (a, effGetParameterProperties, idx, 0, &props, 0.0f) == 1 && std::fabs (props.stepFloat - 1.0f / 32.0f) < 1e-7f, "its parameter properties have a step of 1/32");
    const std::string chunk = getChunk (a);
    check (! chunk.empty() && chunk.find ("Overlay") == std::string::npos, "the state chunk is there and has no trace of it");
    check (waitFor (1500, [&] { return reportOf (a, idx).count >= 1; }), "the first instance reports it once after creation (audioMasterAutomate)");
    check (reportOf (a, idx).last == instanceParamValue (0) && a->getParameter (a, idx) == instanceParamValue (0) && instanceParamValue (0) == 1.0f / 32.0f, "slot 0: value 1/32, reported and read back");
    check (text (a, effGetParamDisplay, idx) == "slot 0", "display text: slot 0");
    a->setParameter (a, idx, 0.9f);
    check (a->getParameter (a, idx) == instanceParamValue (0) && getChunk (a) == chunk, "a write to it is ignored (value and chunk unchanged)");

    // more instances: the lowest free slot each, stable, reused after close
    AEffect* b = make();
    AEffect* c = make();
    check (slotOf (b, idx) == 1 && slotOf (c, idx) == 2 && slotOf (a, idx) == 0, "the next two instances get slots 1 and 2; the first keeps 0");
    check (waitFor (1500, [&] { return reportOf (b, idx).count >= 1 && reportOf (c, idx).count >= 1; }) && reportOf (b, idx).last == instanceParamValue (1) && reportOf (c, idx).last == instanceParamValue (2),
           "each reports its own value after creation");
    closeIt (b);
    AEffect* d = make();
    check (slotOf (d, idx) == 1 && slotOf (a, idx) == 0 && slotOf (c, idx) == 2, "slot 1 is free after its instance closed and the next instance takes it; the others did not move");
    check (text (d, effGetParamDisplay, idx) == "slot 1", "display text of the reused slot");

    // the debug file: the value of another slot, blank, none; a changed value is reported within a second and a half; removing the file gives the real slot back
    const int before = reportOf (a, idx).count;
    writeFile ("7\n");
    check (waitFor (2500, [&] { return a->getParameter (a, idx) == instanceParamValue (7) && reportOf (a, idx).count > before && reportOf (a, idx).last == instanceParamValue (7); }), "debug file '7': getParameter and the report say slot 7");
    check (text (a, effGetParamDisplay, idx) == "slot 7" && slotOf (d, idx) == 7, "... for every instance (display text slot 7)");
    writeFile ("blank");
    check (waitFor (2500, [&] { return a->getParameter (a, idx) == 0.0f && reportOf (a, idx).last == 0.0f; }) && text (a, effGetParamDisplay, idx) == "blank", "debug file 'blank': value 0 (frame 0), reported");
    writeFile ("none");
    check (waitFor (2500, [&] { return slotOf (a, idx) == kSlotNone && reportOf (a, idx).last == instanceParamValue (-1); }) && text (a, effGetParamDisplay, idx) == "none", "debug file 'none': the none frame, reported");
    writeFile ("stale");
    check (waitFor (2500, [&] { return a->getParameter (a, idx) == 0.0f && reportOf (a, idx).last == 0.0f; }), "debug file 'stale': the blank frame is reported and stays until the overlay thread asks (nobody does here)");
    writeFile ("15");
    check (waitFor (2500, [&] { return slotOf (a, idx) == 15; }), "debug file '15': slot 15");
    std::remove (kFakeFile);
    check (waitFor (2500, [&] { return slotOf (a, idx) == 0 && slotOf (c, idx) == 2 && slotOf (d, idx) == 1 && reportOf (a, idx).last == instanceParamValue (0); }), "file removed: every instance is back at its real slot and reported it");

    // a re-report asked for by the overlay thread: the flag of the instance makes the display thread call the host (the controller test checks the asking; here the answer)
    // (the interface is internal; the debug file is the visible way to see a value reported again, above)

    // 16 slots, the 17th instance has none
    std::vector<AEffect*> many { a, c, d };
    while (many.size() < 16)
        many.push_back (make());
    std::set<int> slots;
    for (AEffect* e : many)
        slots.insert (slotOf (e, idx));
    check (slots.size() == 16 && *slots.begin() == 0 && *slots.rbegin() == 15, "16 instances use the slots 0 .. 15, each once");
    AEffect* seventeenth = make();
    check (slotOf (seventeenth, idx) == kSlotNone && text (seventeenth, effGetParamDisplay, idx) == "none", "the 17th instance reports none (never a slot of another instance)");
    check (waitFor (1500, [&] { return reportOf (seventeenth, idx).count >= 1; }) && reportOf (seventeenth, idx).last == instanceParamValue (-1), "... and says so to the host");
    closeIt (many[1]);   // slot 2 (c)
    check (slotOf (seventeenth, idx) == kSlotNone, "closing another instance does not give the 17th a slot (assigned at creation, stable)");
    AEffect* eighteenth = make();
    check (slotOf (eighteenth, idx) == 2, "the next new instance takes the free slot 2");
    closeIt (eighteenth);
    closeIt (seventeenth);
    for (size_t i = 0; i < many.size(); ++i)
        if (i != 1)
            closeIt (many[i]);

    // threads create and close instances at the same time: no two live instances ever share a slot, every slot is in 0 .. 15 (or none when 16 are alive)
    {
        std::mutex liveMu;
        std::map<int, AEffect*> live;
        std::atomic<int> clashes { 0 }, out { 0 }, cycles { 0 };
        auto worker = [&]
        {
            for (int i = 0; i < 25; ++i)
            {
                AEffect* e = make();
                if (! e) { ++out; continue; }
                const int s = slotOf (e, idx);
                {
                    std::lock_guard<std::mutex> g (liveMu);
                    if (s >= 0 && s < 16)
                    {
                        if (live.count (s)) ++clashes;
                        live[s] = e;
                    }
                    else if (s != kSlotNone)
                        ++out;
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (1 + (i % 3)));
                {
                    std::lock_guard<std::mutex> g (liveMu);
                    if (s >= 0 && s < 16)
                        live.erase (s);
                }
                closeIt (e);
                ++cycles;
            }
        };
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; ++t)
            ts.emplace_back (worker);
        for (auto& t : ts)
            t.join();
        check (clashes.load() == 0 && out.load() == 0 && cycles.load() == 100, "4 threads x 25 instances created and closed at once: never two live instances on one slot (" + std::to_string (cycles.load()) + " cycles)");
    }
    AEffect* last = make();
    check (slotOf (last, idx) == 0, "all closed: the next instance is slot 0 again");
    closeIt (last);
    std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
