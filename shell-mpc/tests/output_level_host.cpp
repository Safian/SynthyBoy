// SynthyBoy - shell-mpc acceptance: the output level of the plugin (make-up gain and peak limiter, shell-mpc/output_stage.h, wired into processReplacing).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: output_level_host <plugin built with the make-up gain> <plugin built with SYNTHYBOY_MPC_OUTPUT_GAIN_DB=0> <gain in dB>
//   quiet: one soft note at a low Master Volume, peaks far under the ceiling in both: the first plugin is louder by the gain (RMS, +-0.3 dB), neither limits.
//   loud:  a six-note chord at full velocity and the highest Master Volume: no sample of either plugin is above the ceiling (-1 dBFS), both reach it,
//          and the first plugin is still the louder one (RMS).
// Prints the measured levels. Exit 0 = every check passed.
#include "mpc_vst2_abi.h"
#include "output_stage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <vector>

static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; }

static AEffect* load (const char* path)
{
    void* lib = dlopen (path, RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); std::exit (2); }
    using MainProc = AEffect* (*) (audioMasterCallback);
    auto entry = reinterpret_cast<MainProc> (dlsym (lib, "VSTPluginMain"));
    if (entry == nullptr) { std::fprintf (stderr, "no VSTPluginMain in %s\n", path); std::exit (2); }
    AEffect* e = entry (hostCallback);
    if (e == nullptr || e->magic != kEffectMagic) { std::fprintf (stderr, "bad AEffect\n"); std::exit (2); }
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);
    return e;
}

struct Level { double peak = 0.0, rms = 0.0; };

/** Sets Master Volume (parameter 0, checked by name), plays the notes at the velocity for `seconds` and measures both channels. */
static Level play (AEffect* e, float masterNormalised, const std::vector<int>& notes, int velocity, double seconds)
{
    e->setParameter (e, 0, masterNormalised);
    const int count = static_cast<int> (notes.size());
    std::vector<VstMidiEvent> midi (notes.size());
    std::vector<char> block (sizeof (VstEvents) + sizeof (VstEvent*) * notes.size());
    auto* list = reinterpret_cast<VstEvents*> (block.data());
    auto send = [&] (int status, int vel)
    {
        list->numEvents = count; list->reserved = 0;
        for (int i = 0; i < count; ++i)
        {
            VstMidiEvent& m = midi[static_cast<size_t> (i)];
            std::memset (&m, 0, sizeof (m));
            m.type = kVstMidiType; m.byteSize = sizeof (VstMidiEvent);
            m.midiData[0] = static_cast<char> (status); m.midiData[1] = static_cast<char> (notes[static_cast<size_t> (i)]); m.midiData[2] = static_cast<char> (vel);
            list->events[i] = reinterpret_cast<VstEvent*> (&m);
        }
        e->dispatcher (e, effProcessEvents, 0, 0, list, 0.0f);
    };
    std::vector<float> left (256), right (256);
    float* outs[2] = { left.data(), right.data() };
    Level v; double sum = 0.0; long n = 0;
    send (0x90, velocity);
    const int blocks = static_cast<int> (seconds * 44100.0 / 256.0);
    for (int b = 0; b < blocks; ++b)
    {
        e->processReplacing (e, nullptr, outs, 256);
        for (int i = 0; i < 256; ++i)
            for (double s : { static_cast<double> (left[static_cast<size_t> (i)]), static_cast<double> (right[static_cast<size_t> (i)]) })
            {
                v.peak = std::fmax (v.peak, std::fabs (s)); sum += s * s; ++n;
            }
    }
    send (0x80, 0);
    for (int b = 0; b < 4 * 44100 / 256; ++b) e->processReplacing (e, nullptr, outs, 256); // let the release end before the next measurement
    v.rms = std::sqrt (sum / static_cast<double> (n));
    return v;
}

static double db (double x) { return 20.0 * std::log10 (x > 1e-12 ? x : 1e-12); }

int main (int argc, char** argv)
{
    if (argc < 4) { std::fprintf (stderr, "usage: output_level_host <plugin with gain> <plugin with gain 0> <gain dB>\n"); return 2; }
    AEffect* withGain = load (argv[1]);
    AEffect* plain = load (argv[2]);
    const double gain = std::atof (argv[3]);
    const double ceiling = static_cast<double> (synthyboy_mpc::OutputStage::kCeiling);

    char name[64] = {};
    withGain->dispatcher (withGain, effGetParamName, 0, 0, name, 0.0f);
    check (std::strcmp (name, "Master Volume") == 0, "parameter 0 is Master Volume");

    const Level qg = play (withGain, 0.35f, { 60 }, 40, 1.0), qp = play (plain, 0.35f, { 60 }, 40, 1.0);
    std::printf ("quiet: with gain peak %.2f dBFS rms %.2f dBFS; gain 0 peak %.2f dBFS rms %.2f dBFS; difference %.2f dB\n", db (qg.peak), db (qg.rms), db (qp.peak), db (qp.rms), db (qg.rms) - db (qp.rms));
    check (qp.rms > 1e-5, "quiet: the note sounds");
    check (qg.peak < 0.5 * ceiling && qp.peak < 0.5 * ceiling, "quiet: both stay far under the ceiling (the limiter does nothing)");
    check (std::fabs ((db (qg.rms) - db (qp.rms)) - gain) < 0.3, "quiet: the plugin with the make-up gain is louder by the gain (+-0.3 dB)");

    const std::vector<int> chord { 36, 43, 48, 52, 55, 60 };
    const Level lg = play (withGain, 1.0f, chord, 127, 2.0), lp = play (plain, 1.0f, chord, 127, 2.0);
    std::printf ("loud:  with gain peak %.2f dBFS rms %.2f dBFS; gain 0 peak %.2f dBFS rms %.2f dBFS; difference %.2f dB\n", db (lg.peak), db (lg.rms), db (lp.peak), db (lp.rms), db (lg.rms) - db (lp.rms));
    check (lg.peak <= ceiling * 1.000001 && lp.peak <= ceiling * 1.000001, "loud: no sample of either plugin is above the ceiling (-1 dBFS)");
    check (lg.peak > 0.99 * ceiling, "loud: the plugin with the make-up gain reaches the ceiling (the limiter works)");
    check (lg.rms > lp.rms, "loud: the plugin with the make-up gain is still the louder one");

    std::printf (fails == 0 ? "output_level_host: OK\n" : "output_level_host: %d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
