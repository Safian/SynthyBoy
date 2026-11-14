// SynthyBoy - shell-mpc acceptance: pad pressure to Filter 1 cutoff (compile switch SYNTHYBOY_MPC_PAD_PRESSURE, shell-mpc/pad_pressure.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: pad_pressure_host <plugin library> <own|vital> [off] [DISPLAY_PARAMS]
//   DISPLAY_PARAMS: the display parameters between the engine's list and the pad pressure parameters (default 36 = 4 LFO cursors + 32 spectrum columns; 0 = LFO_LIVE=OFF).
//   The plugin must be built with SYNTHYBOY_MPC_PAD_PRESSURE=ON and SYNTHYBOY_MPC_LFO_LIVE / SPECTRUM as in the default of the shell (the two parameters are the last two of the list).
//   own:   the own engine (the engine of the MPC package): the pressure of a note opens Filter 1 (checked on the audio).
//   vital: the Vital engine has no such layer: the two parameters exist and are saved, the audio does not depend on them.
//   off:   a build with SYNTHYBOY_MPC_PAD_PRESSURE=OFF: nothing after the display parameters, no member in the chunk.
// Also checks the pure functions of pad_pressure.h. Links the core (paramCount, parameter table). Run the TSan build too.
#include "mpc_vst2_abi.h"
#include "pad_pressure.h"
#include "synthyboy/params.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
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

static void midi (AEffect* e, int status, int d1, int d2)
{
    VstMidiEvent m; std::memset (&m, 0, sizeof (m));
    m.type = kVstMidiType; m.byteSize = sizeof (VstMidiEvent);
    m.midiData[0] = static_cast<char> (status); m.midiData[1] = static_cast<char> (d1); m.midiData[2] = static_cast<char> (d2);
    VstEvents ev; std::memset (&ev, 0, sizeof (ev));
    ev.numEvents = 1; ev.events[0] = reinterpret_cast<VstEvent*> (&m);
    e->dispatcher (e, effProcessEvents, 0, 0, &ev, 0.0f);
}

static void setEngineParam (AEffect* e, const char* key, float plain)
{
    const synthyboy::ParamInfo* info = synthyboy::findParamByKey (key);
    if (info == nullptr) { std::printf ("FAIL  no parameter %s\n", key); ++fails; return; }
    e->setParameter (e, static_cast<VstInt32> (info - synthyboy::paramTable()), synthyboy::toNormalised (*info, plain));
}

/** Holds key 60 for ~1.2 s with a poly aftertouch message of `pressure` in every block (256 frames at 44.1 kHz = about 172 a second, the rate of the pad). Returns the brightness
    (first difference energy over energy) of the last third; the first block is kept in `first`. */
static double brightness (AEffect* e, int pressure, std::vector<float>* first = nullptr)
{
    std::vector<float> l (256), r (256);
    float* outs[2] = { l.data(), r.data() };
    double en = 0.0, dn = 0.0;
    midi (e, 0x90, 60, 127);
    for (int b = 0; b < 200; ++b)
    {
        midi (e, 0xA0, 60, pressure);
        e->processReplacing (e, nullptr, outs, 256);
        if (b == 0 && first) *first = l;
        if (b >= 130)
            for (int i = 1; i < 256; ++i)
            {
                en += static_cast<double> (l[static_cast<size_t> (i)]) * l[static_cast<size_t> (i)];
                const double d = static_cast<double> (l[static_cast<size_t> (i)]) - l[static_cast<size_t> (i - 1)];
                dn += d * d;
            }
    }
    midi (e, 0x80, 60, 0);
    for (int b = 0; b < 100; ++b) e->processReplacing (e, nullptr, outs, 256);
    return en > 0.0 ? dn / en : 0.0;
}

static AEffect* make (void* lib)
{
    using Main = AEffect* (*) (audioMasterCallback);
    auto entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    AEffect* e = entry ? entry (hostCallback) : nullptr;
    if (! e) { std::fprintf (stderr, "plugin did not load\n"); std::exit (2); }
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);
    return e;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: pad_pressure_host <plugin> own|vital [off]\n"); return 2; }
    const bool own = std::string (argv[2]) == "own";
    const bool off = argc > 3 && std::string (argv[3]) == "off";
    const int display = argc > (off ? 4 : 3) ? std::atoi (argv[off ? 4 : 3]) : 36;

    // ---- the pure functions
    {
        using namespace synthyboy_mpc;
        check (padPressureDepth (true, 35.0f) == 0.35f && padPressureDepth (false, 35.0f) == 0.0f && padPressureDepth (true, 500.0f) == 1.0f && padPressureDepth (true, -3.0f) == 0.0f,
               "depth: percent / 100, 0 when off, clamped to 0 .. 1");
        const std::string doc = "{\"mpcProgram\":3,\"mpcProgramName\":\"A\",\"version\":4,\"params\":{\"a\":1}}";
        const std::string c = chunkWithPadPressure (doc, false, 62.5f);
        check (c == "{\"mpcProgram\":3,\"mpcProgramName\":\"A\",\"version\":4,\"params\":{\"a\":1},\"mpcPadPressure\":0,\"mpcPadPressureAmount\":62.50}",
               "chunk: the two members sit before the last closing brace, the program prefix is untouched");
        bool on = true; float pct = 0.0f;
        check (padPressureFromChunk (c.data(), c.size(), on, pct) && ! on && pct == 62.5f, "chunk: the members are read back");
        on = true; pct = 7.0f;
        check (! padPressureFromChunk (doc.data(), doc.size(), on, pct) && on && pct == 7.0f, "chunk: no members, the settings stay as they are");
        const std::string bad1 = "{\"mpcProgram\":3,\"mpcProgramName\":\"A\",\"x\":{\"a\":1},\"mpcPadPressure\":2,\"mpcPadPressureAmount\":50.00}";
        const std::string bad2 = "{\"mpcProgram\":3,\"mpcProgramName\":\"A\",\"x\":{\"a\":1},\"mpcPadPressure\":1,\"mpcPadPressureAmount\":x}";
        const std::string bad3 = "{\"mpcProgram\":3,\"mpcProgramName\":\"A\",\"x\":{\"a\":1},\"mpcPadPressure\":1}";
        on = true; pct = 7.0f;
        check (! padPressureFromChunk (bad1.data(), bad1.size(), on, pct) && ! padPressureFromChunk (bad2.data(), bad2.size(), on, pct) && ! padPressureFromChunk (bad3.data(), bad3.size(), on, pct)
               && ! padPressureFromChunk (nullptr, 0, on, pct) && on && pct == 7.0f, "chunk: a malformed or half tail is ignored");
        const std::string big = "{\"mpcProgram\":3,\"mpcProgramName\":\"A\",\"x\":{\"a\":1},\"mpcPadPressure\":1,\"mpcPadPressureAmount\":900.00}";
        check (padPressureFromChunk (big.data(), big.size(), on, pct) && pct == 100.0f, "chunk: the amount is clamped to 100");
        check (chunkWithPadPressure ("nothing", true, 1.0f) == "nothing", "chunk: a document without a closing brace is returned as it is");
    }

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    AEffect* e = make (lib);
    const int n = static_cast<int> (synthyboy::paramCount());
    const int pp = n + display;   // "Pad Pressure"; "Pad Pressure Amount" follows

    const std::string initial = getChunk (e);
    if (off)
    {
        check (e->numParams == n + display, "off: numParams is the engine's count + the display parameters, nothing after them");
        check (initial.find ("mpcPadPressure") == std::string::npos, "off: no pad pressure member in the chunk");
        bool ok = true;
        text (e, effGetParamName, pp, &ok);
        check (! ok, "off: no parameter name after the display parameters");
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
        return fails ? 1 : 0;
    }

    // ---- the list: the display parameters keep their places, the two new ones are the last
    check (e->numParams == pp + 2, "numParams is the engine's count + the display parameters (4 cursors, 32 spectrum columns) + 2");
    check (display == 0 || (text (e, effGetParamName, n) == "LFO 1 Cursor" && text (e, effGetParamName, n + 3) == "LFO 4 Cursor"
           && (display == 4 || (text (e, effGetParamName, n + 4) == "Spectrum 1" && text (e, effGetParamName, pp - 1) == "Spectrum 32"))), "the cursors and the spectrum columns keep their places");
    bool ok[3] = {};
    check (text (e, effGetParamName, pp, ok) == "Pad Pressure" && text (e, effGetParamName, pp + 1, ok + 1) == "Pad Pressure Amount" && ok[0] && ok[1], "names");
    text (e, effGetParamName, pp + 2, ok + 2);
    check (! ok[2] && e->getParameter (e, pp + 2) == 0.0f, "nothing after them");
    check (text (e, effGetParamLabel, pp + 1) == "%" && text (e, effGetParamLabel, pp).empty(), "labels");

    // ---- defaults, writes, display text
    check (e->getParameter (e, pp) == 1.0f && std::fabs (e->getParameter (e, pp + 1) - 0.35f) < 1e-6f, "defaults: on, 35 %");
    check (text (e, effGetParamDisplay, pp) == "On" && text (e, effGetParamDisplay, pp + 1) == "35", "display text: On, 35");
    VstParameterProperties props;
    check (e->dispatcher (e, effCanBeAutomated, pp, 0, nullptr, 0.0f) == 1 && e->dispatcher (e, effCanBeAutomated, pp + 1, 0, nullptr, 0.0f) == 1, "both are automatable");
    check (e->dispatcher (e, effGetParameterProperties, pp, 0, &props, 0.0f) == 1 && (props.flags & kVstParameterIsSwitch) != 0
           && e->dispatcher (e, effGetParameterProperties, pp + 1, 0, &props, 0.0f) == 1 && std::fabs (props.stepFloat - 0.01f) < 1e-6f
           && e->dispatcher (e, effGetParameterProperties, pp + 2, 0, &props, 0.0f) == 0, "parameter properties: a switch and a 1 % step, none after");
    e->setParameter (e, pp, 0.0f);
    e->setParameter (e, pp + 1, 0.8f);
    check (e->getParameter (e, pp) == 0.0f && std::fabs (e->getParameter (e, pp + 1) - 0.8f) < 1e-6f && text (e, effGetParamDisplay, pp) == "Off" && text (e, effGetParamDisplay, pp + 1) == "80",
           "writes: value and display text follow");
    e->setParameter (e, pp + 1, 7.0f);
    e->setParameter (e, pp + 1, std::nanf (""));
    check (e->getParameter (e, pp + 1) >= 0.0f && e->getParameter (e, pp + 1) <= 1.0f, "an out of range or NaN write stays within 0 .. 1");
    e->setParameter (e, pp + 1, 0.8f);

    // ---- the state chunk: members at the end, round trip, older chunks, programs
    const std::string c1 = getChunk (e);
    const std::string tail = "\"mpcPadPressure\":0,\"mpcPadPressureAmount\":80.00}";
    check (c1.size() > tail.size() && c1.compare (c1.size() - tail.size(), tail.size(), tail) == 0, "chunk: the settings are saved at the end");
    check (initial.find ("\"mpcPadPressure\":1,\"mpcPadPressureAmount\":35.00}") != std::string::npos, "chunk: the default is saved too");
    e->setParameter (e, pp, 1.0f);
    e->setParameter (e, pp + 1, 0.1f);
    check (setChunk (e, c1) && e->getParameter (e, pp) == 0.0f && std::fabs (e->getParameter (e, pp + 1) - 0.8f) < 1e-6f, "chunk: the settings come back");
    std::string old = c1;
    const size_t cut = old.rfind (",\"mpcPadPressure\":");
    old.erase (cut, old.size() - 1 - cut);
    e->setParameter (e, pp, 1.0f);
    e->setParameter (e, pp + 1, 0.5f);
    check (old.find ("mpcPadPressure") == std::string::npos && setChunk (e, old) && e->getParameter (e, pp) == 1.0f && std::fabs (e->getParameter (e, pp + 1) - 0.5f) < 1e-6f,
           "chunk: an older chunk without the members leaves the settings as they are");
    e->dispatcher (e, effSetProgram, 0, 0, nullptr, 0.0f);
    check (e->getParameter (e, pp) == 1.0f && std::fabs (e->getParameter (e, pp + 1) - 0.5f) < 1e-6f, "a program change leaves the pad settings alone");
    check (getChunk (e).find ("\"mpcProgram\":0,") == 1, "chunk: the program prefix is still first");
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);

    // ---- the audio: a pressed note, filter 1 closed down on a bright patch
    auto engine = [&] (bool on, float amount)
    {
        AEffect* x = make (lib);
        setEngineParam (x, "filter1.on", 1.0f);
        setEngineParam (x, "filter1.cutoff", 400.0f);
        x->setParameter (x, pp, on ? 1.0f : 0.0f);
        x->setParameter (x, pp + 1, amount);
        return x;
    };
    AEffect* base = engine (false, 1.0f);
    AEffect* offNoPressure = engine (false, 1.0f);
    std::vector<float> a, b;
    const double bBase = brightness (base, 127, &a);
    const double bQuiet = brightness (offNoPressure, 0, &b);
    check ((a == b && bBase == bQuiet) || ! own, "switch off: the pressure changes nothing (identical audio with and without pressure)");
    if (own)
    {
        AEffect* mid = engine (true, 0.35f);
        AEffect* full = engine (true, 1.0f);
        AEffect* idle = engine (true, 1.0f);
        const double bMid = brightness (mid, 127);
        const double bFull = brightness (full, 127);
        const double bIdle = brightness (idle, 0);
        std::printf ("INFO  brightness: off %.5f, 35 %% %.5f, 100 %% %.5f, 100 %% without pressure %.5f (no pressure, off %.5f)\n", bBase, bMid, bFull, bIdle, bQuiet);
        check (bMid > bBase * 1.5 && bFull > bMid * 1.5, "own engine: pressure opens the filter, more with the larger amount");
        check (std::fabs (bIdle - bQuiet) < 1e-9 * (1.0 + bQuiet) || bIdle == bQuiet, "own engine: a pad that sends no pressure leaves the sound as it is");
        // switching it off in mid-note returns the cutoff to the knob
        AEffect* sw = engine (true, 1.0f);
        std::vector<float> l (256), r (256);
        float* outs[2] = { l.data(), r.data() };
        midi (sw, 0x90, 60, 127);
        for (int k = 0; k < 100; ++k) { midi (sw, 0xA0, 60, 127); sw->processReplacing (sw, nullptr, outs, 256); }
        sw->setParameter (sw, pp, 0.0f);
        double en = 0.0, dn = 0.0;
        for (int k = 0; k < 100; ++k)
        {
            midi (sw, 0xA0, 60, 127); sw->processReplacing (sw, nullptr, outs, 256);
            if (k >= 60) for (int i = 1; i < 256; ++i) { en += double (l[size_t (i)]) * l[size_t (i)]; const double d = double (l[size_t (i)]) - l[size_t (i - 1)]; dn += d * d; }
        }
        check (en > 0.0 && dn / en < bFull * 0.5, "own engine: switched off during a note the filter closes again");
        for (AEffect* x : { mid, full, idle, sw }) x->dispatcher (x, effClose, 0, 0, nullptr, 0.0f);
    }
    else
    {
        AEffect* full = engine (true, 1.0f);
        const double bFull = brightness (full, 127);
        check (std::fabs (bFull - bBase) < 1e-9 * (1.0 + bBase), "vital engine: no pressure layer, the audio does not depend on the two parameters");
        full->dispatcher (full, effClose, 0, 0, nullptr, 0.0f);
    }
    base->dispatcher (base, effClose, 0, 0, nullptr, 0.0f);
    offNoPressure->dispatcher (offNoPressure, effClose, 0, 0, nullptr, 0.0f);
    std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
