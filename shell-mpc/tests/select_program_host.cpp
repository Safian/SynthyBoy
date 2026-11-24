// SynthyBoy - shell-mpc acceptance: remote program (preset) select hook (shell-mpc/synthyboy_mpc.cpp).
// Built with SYNTHYBOY_MPC_DEBUG_HOOKS=1 and SYNTHYBOY_MPC_OVERLAY=1.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: select_program_host <plugin library with debug hooks>
// Checks:
//   - initial program is 0 ("Init")
//   - audio renders a baseline note
//   - selecting preset 1 via decimal index in /tmp/synthyboy-select-program:
//     * request file is deleted
//     * ack file contains monotonic counter 1, ok, index 1, preset name
//     * audioMasterUpdateDisplay was called
//     * effGetProgramName returns the preset name
//     * audio note rendered on preset 1 differs from Init preset
//   - selecting preset 0 via exact name "Init":
//     * request file is deleted
//     * ack file contains monotonic counter 2, ok, 0, Init
//     * effGetProgramName returns "Init"
//     * audio matches Init baseline
//   - selecting preset 1 via exact name:
//     * request file is deleted
//     * ack file contains monotonic counter 3, ok, 1, preset name
//     * effGetProgramName returns preset name
//   - error handling:
//     * out of range index (99999) -> error out of range (counter 4)
//     * negative index (-1) -> error out of range (counter 5)
//     * unknown name -> error unknown name (counter 6)
//     * empty line -> error unknown name (counter 7)
//     * program does not change on error

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "mpc_vst2_abi.h"

static std::atomic<int> displayUpdates { 0 };

static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion)
        return 2400;
    if (opcode == audioMasterUpdateDisplay)
    {
        displayUpdates.fetch_add (1);
        return 1;
    }
    if (opcode == audioMasterAutomate)
        return 1;
    return 0;
}

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}

static void sendMidi (AEffect* e, int status, int note, int vel)
{
    VstMidiEvent ev;
    std::memset (&ev, 0, sizeof (ev));
    ev.type = kVstMidiType;
    ev.byteSize = sizeof (VstMidiEvent);
    ev.midiData[0] = static_cast<char> (status);
    ev.midiData[1] = static_cast<char> (note);
    ev.midiData[2] = static_cast<char> (vel);

    VstEvents events;
    events.numEvents = 1;
    events.reserved = 0;
    events.events[0] = reinterpret_cast<VstEvent*> (&ev);
    events.events[1] = nullptr;
    e->dispatcher (e, effProcessEvents, 0, 0, &events, 0.0f);
}

static std::vector<float> renderNote (AEffect* e, int note, int velocity, int numBlocks = 32)
{
    std::vector<float> left (256, 0.0f), right (256, 0.0f);
    float* outs[2] = { left.data(), right.data() };
    std::vector<float> recorded;
    recorded.reserve (numBlocks * 256 * 2);

    sendMidi (e, 0x90, note, velocity);
    for (int b = 0; b < numBlocks; ++b)
    {
        e->processReplacing (e, nullptr, outs, 256);
        for (int i = 0; i < 256; ++i)
        {
            recorded.push_back (left[i]);
            recorded.push_back (right[i]);
        }
    }
    sendMidi (e, 0x80, note, 0);
    for (int b = 0; b < 16; ++b)
        e->processReplacing (e, nullptr, outs, 256);

    return recorded;
}

static bool writeRequest (const std::string& path, const std::string& text)
{
    FILE* f = std::fopen (path.c_str(), "w");
    if (! f) return false;
    std::fputs (text.c_str(), f);
    std::fclose (f);
    return true;
}

static bool waitForAck (const std::string& ackPath, std::string& outLine, int timeoutMs = 2500)
{
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (timeoutMs))
    {
        FILE* f = std::fopen (ackPath.c_str(), "r");
        if (f)
        {
            char buf[512] = {};
            if (std::fgets (buf, sizeof buf, f))
            {
                std::fclose (f);
                outLine = buf;
                return true;
            }
            std::fclose (f);
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return false;
}

using MainProc = AEffect* (*) (audioMasterCallback);

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: select_program_host <plugin-library>\n");
        return 2;
    }

    const std::string selPath = "/tmp/synthyboy-select-program";
    const std::string ackPath = selPath + ".ack";
    std::remove (selPath.c_str());
    std::remove (ackPath.c_str());

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib)
    {
        std::fprintf (stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
    auto entry = reinterpret_cast<MainProc> (dlsym (lib, "VSTPluginMain"));
    if (! entry)
    {
        std::fprintf (stderr, "no VSTPluginMain in %s\n", argv[1]);
        return 2;
    }

    AEffect* e = entry (hostCallback);
    if (! e || e->magic != kEffectMagic)
    {
        std::fprintf (stderr, "bad AEffect\n");
        return 2;
    }

    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    // Allow the display thread to start and complete its first idle tick
    std::this_thread::sleep_for (std::chrono::milliseconds (250));

    const int numProgs = e->numPrograms;
    std::printf ("Plugin loaded with %d programs\n", numProgs);
    check (numProgs >= 1, "at least Init program exists");

    // Check initial program is 0 and name is "Init"
    const int initProg = static_cast<int> (e->dispatcher (e, effGetProgram, 0, 0, nullptr, 0.0f));
    check (initProg == 0, "initial program is index 0");

    char initialName[kVstMaxProgNameLen + 1] = {};
    e->dispatcher (e, effGetProgramName, 0, 0, initialName, 0.0f);
    check (std::string (initialName) == "Init", "initial program name is Init");

    // Baseline audio render on Init
    const auto audioInit = renderNote (e, 60, 100);
    check (! audioInit.empty(), "rendered baseline note on Init");

    if (numProgs > 1)
    {
        // Query preset 1 name via effGetProgramNameIndexed
        char p1Name[kVstMaxProgNameLen + 1] = {};
        e->dispatcher (e, effGetProgramNameIndexed, 1, 0, p1Name, 0.0f);
        std::printf ("Program 1 indexed name: '%s'\n", p1Name);
        check (std::strlen (p1Name) > 0, "program 1 has a non-empty name");

        // ---- Step 1: Select program 1 by decimal index ----
        std::remove (ackPath.c_str());
        const int preUpdateCount = displayUpdates.load();
        check (writeRequest (selPath, "1\n"), "wrote decimal index '1' to select-program file");

        std::string ack1;
        check (waitForAck (ackPath, ack1), "ack file appeared after selecting index 1");
        check (access (selPath.c_str(), F_OK) != 0, "select-program request file was deleted");

        long long seq = 0;
        int ackIdx = -1;
        char ackName[256] = {};
        int parsed = std::sscanf (ack1.c_str(), "%lld ok %d %[^\r\n]", &seq, &ackIdx, ackName);
        check (parsed == 3, "ack 1 parsed as '<seq> ok <index> <name>'");
        check (seq == 1, "ack 1 sequence counter is 1");
        check (ackIdx == 1, "ack 1 index is 1");
        check (std::string (ackName) == p1Name, "ack 1 name matches effGetProgramNameIndexed");

        check (displayUpdates.load() > preUpdateCount, "audioMasterUpdateDisplay was called");

        char curName[kVstMaxProgNameLen + 1] = {};
        e->dispatcher (e, effGetProgramName, 0, 0, curName, 0.0f);
        check (std::string (curName) == p1Name, "effGetProgramName returns preset 1 name");

        // Render note on preset 1 and verify audio differs from Init
        const auto audioP1 = renderNote (e, 60, 100);
        double diffP1 = 0.0;
        const size_t len = std::min (audioInit.size(), audioP1.size());
        for (size_t i = 0; i < len; ++i)
            diffP1 += std::fabs (audioInit[i] - audioP1[i]);
        std::printf ("Audio difference between Init and Preset 1: %f\n", diffP1);
        check (diffP1 > 0.5, "rendered note on preset 1 differs from Init");

        // ---- Step 2: Select program 0 by exact name 'Init' ----
        std::remove (ackPath.c_str());
        check (writeRequest (selPath, "Init\n"), "wrote exact name 'Init' to select-program file");

        std::string ack2;
        check (waitForAck (ackPath, ack2), "ack file appeared after selecting name 'Init'");
        check (access (selPath.c_str(), F_OK) != 0, "select-program file was deleted");

        parsed = std::sscanf (ack2.c_str(), "%lld ok %d %[^\r\n]", &seq, &ackIdx, ackName);
        check (parsed == 3, "ack 2 parsed as '<seq> ok <index> <name>'");
        check (seq == 2, "ack 2 sequence counter is 2 (monotonic)");
        check (ackIdx == 0, "ack 2 index is 0");
        check (std::string (ackName) == "Init", "ack 2 name is 'Init'");

        std::memset (curName, 0, sizeof (curName));
        e->dispatcher (e, effGetProgramName, 0, 0, curName, 0.0f);
        check (std::string (curName) == "Init", "effGetProgramName returned 'Init'");

        const auto audioInit2 = renderNote (e, 60, 100);
        double diffFromP1 = 0.0;
        for (size_t i = 0; i < len; ++i)
            diffFromP1 += std::fabs (audioInit2[i] - audioP1[i]);
        std::printf ("Audio difference between Preset 1 and re-selected Init: %f\n", diffFromP1);
        check (diffFromP1 > 0.5, "rendered note after selecting Init differs from previous preset");

        // ---- Step 3: Select program 1 by exact name ----
        std::remove (ackPath.c_str());
        check (writeRequest (selPath, std::string (p1Name) + "\n"), "wrote preset 1 name to select-program file");

        std::string ack3;
        check (waitForAck (ackPath, ack3), "ack file appeared after selecting preset 1 by name");
        check (access (selPath.c_str(), F_OK) != 0, "select-program file was deleted");

        parsed = std::sscanf (ack3.c_str(), "%lld ok %d %[^\r\n]", &seq, &ackIdx, ackName);
        check (parsed == 3, "ack 3 parsed as '<seq> ok <index> <name>'");
        check (seq == 3, "ack 3 sequence counter is 3 (monotonic)");
        check (ackIdx == 1, "ack 3 index is 1");
        check (std::string (ackName) == p1Name, "ack 3 name matches preset 1");

        std::memset (curName, 0, sizeof (curName));
        e->dispatcher (e, effGetProgramName, 0, 0, curName, 0.0f);
        check (std::string (curName) == p1Name, "effGetProgramName returns preset 1 name");

        const auto audioP1_again = renderNote (e, 60, 100);
        double diffFromInit = 0.0;
        for (size_t i = 0; i < len; ++i)
            diffFromInit += std::fabs (audioP1_again[i] - audioInit2[i]);
        std::printf ("Audio difference between Init and Preset 1 (by name): %f\n", diffFromInit);
        check (diffFromInit > 0.5, "rendered note after selecting preset 1 differs from previous preset");

        // ---- Step 4: Out of range index ----
        std::remove (ackPath.c_str());
        check (writeRequest (selPath, "99999\n"), "wrote out-of-range index 99999");

        std::string ack4;
        check (waitForAck (ackPath, ack4), "ack file appeared for out of range index");
        check (access (selPath.c_str(), F_OK) != 0, "select-program file was deleted");

        char ackReason[256] = {};
        parsed = std::sscanf (ack4.c_str(), "%lld error %[^\r\n]", &seq, ackReason);
        check (parsed == 2, "ack 4 parsed as '<seq> error <reason>'");
        check (seq == 4, "ack 4 sequence counter is 4 (monotonic)");
        check (std::string (ackReason) == "out of range", "ack 4 reason is 'out of range'");

        // Program remains preset 1
        std::memset (curName, 0, sizeof (curName));
        e->dispatcher (e, effGetProgramName, 0, 0, curName, 0.0f);
        check (std::string (curName) == p1Name, "program did not change after out of range error");

        // ---- Step 5: Negative index ----
        std::remove (ackPath.c_str());
        check (writeRequest (selPath, "-5\n"), "wrote negative index -5");

        std::string ack5;
        check (waitForAck (ackPath, ack5), "ack file appeared for negative index");
        check (access (selPath.c_str(), F_OK) != 0, "select-program file was deleted");

        std::memset (ackReason, 0, sizeof (ackReason));
        parsed = std::sscanf (ack5.c_str(), "%lld error %[^\r\n]", &seq, ackReason);
        check (parsed == 2, "ack 5 parsed as '<seq> error <reason>'");
        check (seq == 5, "ack 5 sequence counter is 5 (monotonic)");
        check (std::string (ackReason) == "out of range", "ack 5 reason is 'out of range'");

        // ---- Step 6: Unknown preset name ----
        std::remove (ackPath.c_str());
        check (writeRequest (selPath, "NonexistentPreset_XYZ_98765\n"), "wrote unknown preset name");

        std::string ack6;
        check (waitForAck (ackPath, ack6), "ack file appeared for unknown name");
        check (access (selPath.c_str(), F_OK) != 0, "select-program file was deleted");

        std::memset (ackReason, 0, sizeof (ackReason));
        parsed = std::sscanf (ack6.c_str(), "%lld error %[^\r\n]", &seq, ackReason);
        check (parsed == 2, "ack 6 parsed as '<seq> error <reason>'");
        check (seq == 6, "ack 6 sequence counter is 6 (monotonic)");
        check (std::string (ackReason) == "unknown name", "ack 6 reason is 'unknown name'");

        // ---- Step 7: Empty line ----
        std::remove (ackPath.c_str());
        check (writeRequest (selPath, "   \n"), "wrote empty line");

        std::string ack7;
        check (waitForAck (ackPath, ack7), "ack file appeared for empty line");
        check (access (selPath.c_str(), F_OK) != 0, "select-program file was deleted");

        std::memset (ackReason, 0, sizeof (ackReason));
        parsed = std::sscanf (ack7.c_str(), "%lld error %[^\r\n]", &seq, ackReason);
        check (parsed == 2, "ack 7 parsed as '<seq> error <reason>'");
        check (seq == 7, "ack 7 sequence counter is 7 (monotonic)");
        check (std::string (ackReason) == "unknown name", "ack 7 reason is 'unknown name'");
    }

    // Clean up files
    std::remove (selPath.c_str());
    std::remove (ackPath.c_str());

    e->dispatcher (e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
    dlclose (lib);

    std::printf ("select_program_host: %s (fails: %d)\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
