// SynthyBoy - shell-mpc acceptance: the NOTE / VELO curves of the Filter page in a LIVE plugin built with SYNTHYBOY_MPC_OVERLAY and SYNTHYBOY_MPC_DEBUG_HOOKS: the request the curve editor posts (IInstanceView::setScalarCurve ->
// requestScalarCurve) is applied by the display thread (scalarCurvePoll: validateContent, content, engine, the overlay's copy) and reaches the chunk (content.curves.velocity / .note); the two sources are independent, a curve
// the content validation refuses is not applied and the old one stays, an empty list is the identity (the member is gone), a chunk with curves restores into another instance byte for byte, a program change resets them.
// The debug file SYNTHYBOY_MPC_SCALAR_CURVE_FILE stands for the page (its first line is "which x,y,power x,y,power ...": which 0 velocity, 1 note).
//
// Usage: scalar_curve_chunk_host <plugin library with the overlay and the debug hooks>
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpc_vst2_abi.h"
#include "synthyboy/state.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <functional>
#include <string>
#include <thread>
#include <unistd.h>

static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : (opcode == audioMasterAutomate ? 1 : 0); }

static int fails = 0;
static void check (bool ok, const std::string& what)
{
    if (! ok || std::getenv ("SCALAR_CURVE_CHUNK_HOST_VERBOSE"))
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++fails;
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
static bool setChunk (AEffect* e, const std::string& c)
{
    return e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (c.size()), const_cast<char*> (c.data()), 0.0f) != 0;
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

/** The curves of a chunk as the core reads them (the same reader the plugin uses to load it). */
static bool curvesOf (const std::string& chunk, synthyboy::StateContent& out)
{
    synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
    const synthyboy::LoadResult r = synthyboy::loadStateWithContent (chunk.data(), chunk.size(), values, out);
    return r.status != synthyboy::LoadStatus::invalid;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: scalar_curve_chunk_host <plugin>\n"); return 2; }
    const std::string curveFile = "/tmp/synthyboy-scalar-curve-" + std::to_string (static_cast<long> (getpid()));
    setenv ("SYNTHYBOY_MPC_SCALAR_CURVE_FILE", curveFile.c_str(), 1);
    std::remove (curveFile.c_str());
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    auto post = [&] (const std::string& line) { if (FILE* f = std::fopen (curveFile.c_str(), "w")) { std::fputs ((line + "\n").c_str(), f); std::fclose (f); } };

    AEffect* a = make();
    if (! a) { std::fprintf (stderr, "plugin did not load\n"); return 2; }
    const std::string fresh = getChunk (a);
    check (! fresh.empty() && fresh.find ("\"curves\"") == std::string::npos, "a fresh plugin has no curves member in its chunk (the identity is the empty content)");

    // ---- NOTE: three points with a bend, posted as the page does; the display thread applies them and the chunk carries them
    post ("1 0,0,0 0.5,0.8,3 1,1,0");
    check (waitFor (3000, [&] { return getChunk (a).find ("\"note\"") != std::string::npos; }), "the request reached the content through the display thread (the chunk has the note curve)");
    synthyboy::StateContent c;
    check (curvesOf (getChunk (a), c) && c.noteCurve.size() == 3 && c.velocityCurve.empty() && std::fabs (c.noteCurve[1].x - 0.5f) < 1.0e-5f && std::fabs (c.noteCurve[1].y - 0.8f) < 1.0e-5f && c.noteCurve[1].power == 3.0f, "the note curve is exactly the posted points; the velocity curve is untouched");

    // ---- VELO: independent of NOTE
    post ("0 0,0.1,0 0.25,0.3,-2 0.75,0.9,2 1,1,0");
    check (waitFor (3000, [&] { return getChunk (a).find ("\"velocity\"") != std::string::npos; }), "the velocity curve arrived");
    synthyboy::StateContent c2;
    check (curvesOf (getChunk (a), c2) && c2.velocityCurve.size() == 4 && c2.noteCurve.size() == 3 && c2.velocityCurve[1].power == -2.0f, "both curves are in the chunk, each its own");

    // ---- a chunk with curves restores into another instance, and writes back byte for byte
    const std::string both = getChunk (a);
    AEffect* b = make();
    check (setChunk (b, both) && getChunk (b) == both, "a chunk with curves restores into another instance and writes back byte for byte");

    // ---- a curve the content validation refuses (x not ascending) is not applied: the old one stays
    post ("1 0,0,0 0.8,0.5,0 0.4,0.6,0 1,1,0");
    std::this_thread::sleep_for (std::chrono::milliseconds (600));
    check (getChunk (a) == both, "a refused curve (x not ascending) changes nothing");
    post ("1 0,0,0 1.5,0.5,0 1,1,0");   // x beyond 1
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    check (getChunk (a) == both, "a refused curve (x above 1) changes nothing");

    // ---- the identity: an empty list removes the member
    post ("1");
    check (waitFor (3000, [&] { return getChunk (a).find ("\"note\"") == std::string::npos; }) && getChunk (a).find ("\"velocity\"") != std::string::npos, "an empty list is the identity: the note member is gone, the velocity one stays");
    post ("0");
    check (waitFor (3000, [&] { return getChunk (a).find ("\"curves\"") == std::string::npos; }), "both identity: no curves member at all (byte for byte the chunk of before)");
    check (getChunk (a) == fresh, "... and the chunk is the one of the fresh plugin");

    // ---- a source that does not exist is ignored
    post ("2 0,0,0 1,1,0");
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    check (getChunk (a) == fresh, "a request for a source that does not exist changes nothing");

    // ---- a program change resets the curves (they are content of the program)
    post ("1 0,0.2,0 1,0.8,0");
    check (waitFor (3000, [&] { return getChunk (a).find ("\"note\"") != std::string::npos; }), "a curve again");
    a->dispatcher (a, effSetProgram, 0, 0, nullptr, 0.0f);
    check (getChunk (a).find ("\"note\"") == std::string::npos, "a program change (Init) resets the curves");

    std::remove (curveFile.c_str());
    std::printf (fails ? "scalar_curve_chunk_host: FAILED (%d)\n" : "scalar_curve_chunk_host: OK\n", fails);
    return fails ? 1 : 0;
}
