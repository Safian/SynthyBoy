// SynthyBoy - MPC shell acceptance: velocity logging in the developer play log (shell-mpc/play_log.h).
// Built with SYNTHYBOY_MPC_DEBUG_HOOKS=1. Usage: play_velocity_host <plugin>
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef SYNTHYBOY_MPC_DEBUG_HOOKS
#define SYNTHYBOY_MPC_DEBUG_HOOKS 1
#endif

#include "mpc_vst2_abi.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <unistd.h>

using Main = AEffect* (*) (audioMasterCallback);
static Main entry = nullptr;
static int fails = 0;

static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}

static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}

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
    if (e)
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
}

static void sleepMs (int ms)
{
    std::this_thread::sleep_for (std::chrono::milliseconds (ms));
}

static std::string slurp (const std::string& path)
{
    std::string s;
    if (FILE* f = std::fopen (path.c_str(), "rb"))
    {
        char buf[4096];
        while (size_t n = std::fread (buf, 1, sizeof (buf), f))
            s.append (buf, n);
        std::fclose (f);
    }
    return s;
}

int main (int argc, char** argv)
{
    const char* pluginPath = (argc >= 2) ? argv[1] : nullptr;
    if (! pluginPath)
    {
        const char* candidates[] = {
            "build/shell-mpc/synthyboy_mpc.dylib",
            "build/shell-mpc/synthyboy_mpc.so",
            "build-test/shell-mpc/synthyboy_mpc.dylib",
            "build-test/shell-mpc/synthyboy_mpc.so",
            "build-mpc-live-ovlon/shell-mpc/synthyboy_mpc.dylib",
            "build-mpc-live-ovlon/shell-mpc/synthyboy_mpc.so",
            nullptr
        };
        for (int i = 0; candidates[i]; ++i)
        {
            if (::access (candidates[i], R_OK) == 0)
            {
                pluginPath = candidates[i];
                break;
            }
        }
    }

    if (! pluginPath)
    {
        std::fprintf (stderr, "usage: play_velocity_host <plugin>\n");
        return 2;
    }

    void* lib = dlopen (pluginPath, RTLD_NOW | RTLD_LOCAL);
    if (! lib)
    {
        std::fprintf (stderr, "dlopen %s: %s\n", pluginPath, dlerror());
        return 2;
    }

    entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry)
    {
        std::fprintf (stderr, "no VSTPluginMain in %s\n", pluginPath);
        return 2;
    }

    const std::string base = "/tmp/sb-play-vel-test-" + std::to_string (static_cast<long> (getpid()));
    setenv ("SYNTHYBOY_MPC_PLAY_LOG_FILE", base.c_str(), 1);
    const std::string txt = base + ".txt";
    std::remove (txt.c_str());
    std::remove (base.c_str());

    // 1. Without switch file: nothing is logged when notes arrive
    {
        AEffect* e = make();
        check (e != nullptr, "instance created without switch file");
        if (e)
        {
            VstMidiEvent m; std::memset (&m, 0, sizeof (m));
            m.type = kVstMidiType; m.byteSize = sizeof (VstMidiEvent);
            m.midiData[0] = static_cast<char> (0x90); m.midiData[1] = 60; m.midiData[2] = 40;
            VstEvents evs; std::memset (&evs, 0, sizeof (evs));
            evs.numEvents = 1; evs.events[0] = reinterpret_cast<VstEvent*> (&m);
            e->dispatcher (e, effProcessEvents, 0, 0, &evs, 0.0f);
            sleepMs (300);
            check (slurp (txt).empty(), "switch file absent: no log file created on incoming notes");
            closeIt (e);
        }
    }

    // 2. With switch file: drive velocity 40 and 127 note-on events through effProcessEvents
    {
        if (FILE* f = std::fopen (base.c_str(), "w")) std::fclose (f);
        AEffect* e = make();
        check (e != nullptr, "instance created with switch file");
        if (e)
        {
            VstMidiEvent m40; std::memset (&m40, 0, sizeof (m40));
            m40.type = kVstMidiType; m40.byteSize = sizeof (VstMidiEvent);
            m40.midiData[0] = static_cast<char> (0x90); m40.midiData[1] = 60; m40.midiData[2] = 40;

            VstMidiEvent m127; std::memset (&m127, 0, sizeof (m127));
            m127.type = kVstMidiType; m127.byteSize = sizeof (VstMidiEvent);
            m127.midiData[0] = static_cast<char> (0x90); m127.midiData[1] = 62; m127.midiData[2] = 127;

            VstEvent* evArr[2] = { reinterpret_cast<VstEvent*> (&m40), reinterpret_cast<VstEvent*> (&m127) };
            VstEvents evs; std::memset (&evs, 0, sizeof (evs));
            evs.numEvents = 2; evs.events[0] = evArr[0]; evs.events[1] = evArr[1];

            e->dispatcher (e, effProcessEvents, 0, 0, &evs, 0.0f);

            // Also send a note-off event to test release velocity logging
            VstMidiEvent mOff; std::memset (&mOff, 0, sizeof (mOff));
            mOff.type = kVstMidiType; mOff.byteSize = sizeof (VstMidiEvent);
            mOff.midiData[0] = static_cast<char> (0x80); mOff.midiData[1] = 60; mOff.midiData[2] = 64;
            VstEvents evsOff; std::memset (&evsOff, 0, sizeof (evsOff));
            evsOff.numEvents = 1; evsOff.events[0] = reinterpret_cast<VstEvent*> (&mOff);
            e->dispatcher (e, effProcessEvents, 0, 0, &evsOff, 0.0f);

            // Give the display thread time to drain the lock-free ring
            sleepMs (700);

            const std::string log = slurp (txt);
            check (! log.empty(), "play log file written");
            check (log.find ("note_on channel 1 note 60 vel 40 engine 40 dropped 0") != std::string::npos,
                   "play log records note_on velocity 40 with channel, note, engine vel and dropped count");
            check (log.find ("note_on channel 1 note 62 vel 127 engine 127 dropped 0") != std::string::npos,
                   "play log records note_on velocity 127 with channel, note, engine vel and dropped count");
            check (log.find ("note_off channel 1 note 60 vel 64 engine 64 dropped 0") != std::string::npos,
                   "play log records note_off event");

            closeIt (e);
        }
        std::remove (base.c_str());
        std::remove (txt.c_str());
    }

    std::printf ("play_velocity_host: %s\n", fails == 0 ? "OK" : "FAILED");
    return fails == 0 ? 0 : 1;
}
