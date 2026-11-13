// SynthyBoy - shell-mpc acceptance: the audio callback statistics (compile switch SYNTHYBOY_MPC_CALLBACK_STATS, shell-mpc/callback_stats.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: callback_stats_host <plugin library built with SYNTHYBOY_MPC_CALLBACK_STATS=ON> [file]
// The host calls processReplacing with blocks of 480 frames at 48 kHz (nominal block time 10 ms) at chosen, deliberately uneven intervals: gaps of about 8, 17.5,
// 30, 55 and 100 ms (the 1.5x, 2x, 4x, 8x limits are 15, 20, 40, 80 ms), and the host callback of one of the calls sleeps 6 ms (a slow call inside the callback).
// The statistics file (SYNTHYBOY_MPC_CALLBACK_STATS_FILE) must show the gaps in the expected buckets, the largest gaps in order, the process time and the
// build line; it must appear while the plugin runs (periodic, SYNTHYBOY_MPC_CALLBACK_STATS_INTERVAL_MS) and at effClose (shutdown). No part of this test needs
// the audio thread to be real time; the sleeps only ever last longer than asked, so every check is "at least" in the bucket it belongs to or above.
// The sections added in format 2 are checked with exact numbers: the host sends a known MIDI sequence (notes, poly aftertouch, CC 74, pitch bend and channel
// pressure on several channels, a program change, a clock byte, a sysex event and an event of an unknown type), asks effCanDo questions and calls a few opcodes
// (effVendorSpecific ten times, an opcode the ABI header does not know); then it asks for a reset (the reset file) and sends 70 messages to see the ring wrap.
#include "mpc_vst2_abi.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::atomic<int> slowNext { 0 };
static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterGetTime && slowNext.exchange (0) != 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (6)); // the callback body takes 6 ms
    return opcode == audioMasterVersion ? 2400 : 0;
}

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}

/** A VstEvents block with the given MIDI events (and, if asked, a sysex event and an event of type 5) kept alive by the object. */
struct EventBlock
{
    std::vector<VstMidiEvent> midi;
    VstMidiSysexEvent sysex {};
    VstEvent other {};
    std::vector<char> raw;
    VstEvents* build (bool withSysex, bool withOther)
    {
        std::vector<VstEvent*> list;
        for (auto& m : midi) list.push_back (reinterpret_cast<VstEvent*> (&m));
        if (withSysex) { sysex.type = kVstSysExType; sysex.byteSize = sizeof (sysex); list.push_back (reinterpret_cast<VstEvent*> (&sysex)); }
        if (withOther) { other.type = 5; other.byteSize = sizeof (other); list.push_back (&other); }
        raw.assign (sizeof (VstEvents) + list.size() * sizeof (VstEvent*), 0);
        auto* ev = reinterpret_cast<VstEvents*> (raw.data());
        ev->numEvents = static_cast<VstInt32> (list.size());
        for (size_t i = 0; i < list.size(); ++i) ev->events[i] = list[i]; // the array runs past events[2], as it does for a real host
        return ev;
    }
    void add (int status, int d1, int d2, int delta)
    {
        VstMidiEvent m {};
        m.type = kVstMidiType; m.byteSize = sizeof (m); m.deltaFrames = delta;
        m.midiData[0] = static_cast<char> (status); m.midiData[1] = static_cast<char> (d1); m.midiData[2] = static_cast<char> (d2);
        midi.push_back (m);
    }
};

static bool has (const std::string& t, const std::string& s) { return t.find (s) != std::string::npos; }

static std::string slurp (const std::string& path)
{
    std::ifstream f (path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/** The number after `key` in the file text (first occurrence), -1 when absent. */
static long long field (const std::string& t, const std::string& key)
{
    const size_t p = t.find (key);
    return p == std::string::npos ? -1 : std::atoll (t.c_str() + p + key.size());
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: callback_stats_host <plugin> [file]\n"); return 2; }
    const std::string file = argc > 2 ? argv[2] : "callback-stats-test.txt";
    std::remove (file.c_str());
    setenv ("SYNTHYBOY_MPC_CALLBACK_STATS_FILE", file.c_str(), 1);
    setenv ("SYNTHYBOY_MPC_CALLBACK_STATS_INTERVAL_MS", "100", 1);
    const std::string resetFile = file + ".reset";
    std::remove (resetFile.c_str());
    setenv ("SYNTHYBOY_MPC_CALLBACK_STATS_RESET_FILE", resetFile.c_str(), 1);

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    using Main = AEffect* (*) (audioMasterCallback);
    auto entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    AEffect* e = entry (hostCallback);
    if (! e) { std::fprintf (stderr, "plugin did not load\n"); return 2; }
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 48000.0f);
    e->dispatcher (e, effSetBlockSize, 0, 480, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    std::vector<float> l (480), r (480);
    float* outs[2] = { l.data(), r.data() };
    auto block = [&] { e->processReplacing (e, nullptr, outs, 480); };
    auto wait = [] (double ms) { std::this_thread::sleep_for (std::chrono::microseconds (static_cast<long long> (ms * 1000))); };

    // gaps: 4 x ~8 ms (bucket 0), 2 x ~17.5 (1), 2 x ~30 (2), 1 x ~55 + 6 (3), 1 x ~100 (4); the call after the second ~30 ms wait is the slow one
    block();
    for (int i = 0; i < 4; ++i) { wait (8); block(); }
    for (int i = 0; i < 2; ++i) { wait (17.5); block(); }
    wait (30); block();
    wait (30); slowNext = 1; block();
    wait (55); block();
    wait (100); block();

    // the host side: a known MIDI sequence in two effProcessEvents calls, questions, a few opcodes
    {
        EventBlock a;
        a.add (0x90, 60, 100, 5);   // note on, channel 1
        a.add (0x90, 60, 0, 10);    // note on with velocity 0 (a note off)
        a.add (0x81, 61, 64, 0);    // note off, channel 2
        a.add (0xA0, 60, 55, 1);    // poly aftertouch, channel 1
        a.add (0xA2, 61, 20, 2);    // poly aftertouch, channel 3
        a.add (0xB0, 74, 10, 3);    // CC 74, channel 1
        a.add (0xB1, 74, 120, 4);   // CC 74, channel 2
        e->dispatcher (e, effProcessEvents, 0, 0, a.build (false, false), 0.0f);
        EventBlock b;
        b.add (0xB2, 74, 64, 6);    // CC 74, channel 3
        b.add (0xB0, 1, 5, 7);      // CC 1, channel 1
        b.add (0xE0, 0, 64, 8);     // pitch bend, channel 1: 8192
        b.add (0xE3, 127, 127, 9);  // pitch bend, channel 4: 16383
        b.add (0xE3, 0, 0, 10);     // pitch bend, channel 4: 0
        b.add (0xD4, 33, 0, 11);    // channel pressure, channel 5
        b.add (0xD4, 99, 0, 12);    // channel pressure, channel 5
        b.add (0xCF, 7, 0, 13);     // program change, channel 16
        b.add (0xF8, 0, 0, 14);     // clock
        e->dispatcher (e, effProcessEvents, 0, 0, b.build (true, true), 0.0f);

        char s1[32] = "receiveVstMidiEvent", s2[32] = "sendVstMidiEvent", s3[32] = "someUnknownThing", s4[32] = "MPE";
        e->dispatcher (e, effCanDo, 0, 0, s1, 0.0f);
        e->dispatcher (e, effCanDo, 0, 0, s2, 0.0f);
        e->dispatcher (e, effCanDo, 0, 0, s1, 0.0f);
        e->dispatcher (e, effCanDo, 0, 0, s3, 0.0f);
        e->dispatcher (e, effCanDo, 0, 0, s4, 0.0f);
        int dummy = 0;
        for (int i = 0; i < 10; ++i)
            e->dispatcher (e, effVendorSpecific, i + 1, 1000 + i, i == 1 ? static_cast<void*> (&dummy) : nullptr, 0.5f * static_cast<float> (i + 1));
        e->dispatcher (e, 200, 3, 9, nullptr, 0.0f);
        e->dispatcher (e, effGetVstVersion, 0, 0, nullptr, 0.0f);
    }
    wait (350); // at least three periodic writes of the 100 ms interval while the plugin runs and no call arrives
    const std::string periodic = slurp (file);
    check (periodic.find ("synthyboy-callback-stats 2") == 0 && periodic.find ("written: periodic") != std::string::npos, "periodic: the file exists while the plugin runs");
    check (field (periodic, "callbacks: ") == 11, "periodic: 11 callbacks counted");

    // ---- format 2 sections, exact numbers ----
    check (has (periodic, "\nmidi:\n") && has (periodic, "\nhost_can_do:\n") && has (periodic, "\ndispatcher_opcodes:\n") && has (periodic, "\naudio_master_calls:\n"),
           "the four sections are in the file");
    check (periodic.find ("largest_gaps:") < periodic.find ("\nmidi:\n"), "the sections come after the earlier lines");
    check (has (periodic, "  vst_events: midi=16 sysex=1 other_types=1\n"), "midi: 16 MIDI events, 1 sysex event, 1 event of another type");
    check (has (periodic, "  status_totals: note_off=1 note_on=2 poly_at=2 cc=4 program=1 chan_pressure=2 pitch_bend=3 system_or_other=1 note_on_velocity0=1\n"),
           "midi: totals by status type");
    check (has (periodic, "  channel 1: note_off=0 note_on=2 poly_at=1 cc=2 program=0 chan_pressure=0 pitch_bend=1\n"), "midi: channel 1");
    check (has (periodic, "  channel 2: note_off=1 note_on=0 poly_at=0 cc=1 program=0 chan_pressure=0 pitch_bend=0\n"), "midi: channel 2");
    check (has (periodic, "  channel 3: note_off=0 note_on=0 poly_at=1 cc=1 program=0 chan_pressure=0 pitch_bend=0\n"), "midi: channel 3");
    check (has (periodic, "  channel 4: note_off=0 note_on=0 poly_at=0 cc=0 program=0 chan_pressure=0 pitch_bend=2\n"), "midi: channel 4");
    check (has (periodic, "  channel 5: note_off=0 note_on=0 poly_at=0 cc=0 program=0 chan_pressure=2 pitch_bend=0\n"), "midi: channel 5");
    check (has (periodic, "  channel 16: note_off=0 note_on=0 poly_at=0 cc=0 program=1 chan_pressure=0 pitch_bend=0\n"), "midi: channel 16");
    check (! has (periodic, "  channel 6:") && ! has (periodic, "  channel 15:"), "midi: channels without messages are not listed");
    check (has (periodic, "    cc=1 count=1\n") && has (periodic, "    cc=74 count=3\n") && ! has (periodic, "    cc=2 "), "midi: CC numbers, non zero rows only");
    check (has (periodic, "  pitch_bend (14 bit, centre 8192): min=0 max=16383\n"), "midi: pitch bend range");
    check (has (periodic, "  channel_pressure: min=33 max=99\n"), "midi: channel pressure range");
    check (has (periodic, "  poly_at (value): min=20 max=55\n"), "midi: poly aftertouch range");
    check (has (periodic, "  cc74: min=10 max=120\n"), "midi: CC 74 range");
    check (has (periodic, "last_events (of 16,") && has (periodic, "    1 at_ms=") && has (periodic, " delta=5 status=0x90 ch=1 d1=60 d2=100\n")
           && has (periodic, "    16 at_ms=") && has (periodic, " delta=14 status=0xF8 ch=- d1=0 d2=0\n") && ! has (periodic, "    17 at_ms="), "midi: the ring holds the 16 messages in order");
    check (has (periodic, "  questions=5 distinct=4 not_stored=0"), "host_can_do: 5 questions, 4 distinct");
    check (has (periodic, "  \"receiveVstMidiEvent\" asked=2 answer=1\n") && has (periodic, "  \"sendVstMidiEvent\" asked=1 answer=-1\n")
           && has (periodic, "  \"someUnknownThing\" asked=1 answer=0\n") && has (periodic, "  \"MPE\" asked=1 answer=1\n"), "host_can_do: texts, counts and the plugin's answers");
    check (has (periodic, "  op=0 name=effOpen count=1\n") && has (periodic, "  op=10 name=effSetSampleRate count=1\n") && has (periodic, "  op=25 name=effProcessEvents count=2\n")
           && has (periodic, "  op=51 name=effCanDo count=5\n") && has (periodic, "  op=50 name=effVendorSpecific count=10\n") && has (periodic, "  op=58 name=effGetVstVersion count=1\n"),
           "dispatcher_opcodes: counts with names");
    check (has (periodic, "  op=200 name=unknown_to_the_ABI_header count=1\n") && ! has (periodic, "  op=7 name=effGetParamDisplay"), "dispatcher_opcodes: an unknown opcode is listed, unused ones are not");
    check (has (periodic, "vendor_specific_first_calls (effVendorSpecific): seen=10 shown=8\n") && has (periodic, "    1 opcode=50 index=1 value=1000 opt=0.5 ptr=null\n")
           && has (periodic, "    2 opcode=50 index=2 value=1001 opt=1 ptr=set\n") && has (periodic, "    8 opcode=50 index=8 value=1007 opt=4 ptr=null\n")
           && ! has (periodic, "    9 opcode=50"), "dispatcher_opcodes: the first 8 effVendorSpecific calls with index, value, opt");
    check (has (periodic, "unknown_opcode_first_calls: seen=1 shown=1\n") && has (periodic, "    1 opcode=200 index=3 value=9 opt=0 ptr=null\n"), "dispatcher_opcodes: the unknown opcode call is shown");
    check (has (periodic, "  op=1 name=audioMasterVersion count=1\n") && has (periodic, "  op=7 name=audioMasterGetTime count=11\n"), "audio_master_calls: version once, getTime once per block");

    // ---- reset: the file asks the writer to zero the MIDI part (the rest stays) ----
    { std::ofstream r (resetFile); r << "x"; }
    wait (350);
    const std::string afterReset = slurp (file);
    check (! std::ifstream (resetFile).good(), "reset: the writer deleted the reset file");
    check (has (afterReset, "resets=1\n") && has (afterReset, "  vst_events: midi=0 sysex=0 other_types=0\n")
           && has (afterReset, "  status_totals: note_off=0 note_on=0 poly_at=0 cc=0 program=0 chan_pressure=0 pitch_bend=0 system_or_other=0 note_on_velocity0=0\n")
           && ! has (afterReset, "  channel 1:") && ! has (afterReset, "    cc=74") && has (afterReset, "  pitch_bend (14 bit, centre 8192): none\n") && has (afterReset, "  cc74: none\n")
           && has (afterReset, "last_events (of 0,"), "reset: MIDI counters, ranges and ring are zero");
    check (has (afterReset, "  op=51 name=effCanDo count=5\n") && has (afterReset, "  questions=5 distinct=4") && field (afterReset, "callbacks: ") == 11, "reset: the other statistics stay");
    {
        EventBlock c;
        for (int i = 0; i < 70; ++i) c.add (0x99, i & 127, 100, i);
        e->dispatcher (e, effProcessEvents, 0, 0, c.build (false, false), 0.0f);
    }
    wait (350);
    const std::string afterWrap = slurp (file);
    check (has (afterWrap, "  vst_events: midi=70 sysex=0 other_types=0\n") && has (afterWrap, "  channel 10: note_off=0 note_on=70 ") && has (afterWrap, "last_events (of 70,")
           && has (afterWrap, "    7 at_ms=") && has (afterWrap, "    70 at_ms=") && ! has (afterWrap, "    6 at_ms=") && has (afterWrap, " delta=69 status=0x99 ch=10 d1=69 d2=100\n"),
           "ring: 70 messages, the last 64 are listed (7..70) in order");

    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f); // writes the file for the last time
    const std::string t = slurp (file);
    std::printf ("%s", t.c_str());
    check (t.find ("written: shutdown") != std::string::npos, "shutdown: the file is written at effClose");
    check (t.find ("build: ") != std::string::npos && t.find ("LFO_LIVE=") != std::string::npos && t.find ("arch=") != std::string::npos && t.find ("params=") != std::string::npos,
           "the build line names the variant (LFO_LIVE, arch, params)");
    check (field (t, "callbacks: ") == 11 && field (t, "gaps_counted: ") == 10, "11 callbacks, 10 gaps");
    check (field (t, "block_frames_last: ") == 480 && field (t, "nominal_block_us: ") == 10000, "nominal block time 480 / 48000 = 10000 us");
    const long long b0 = field (t, "<1.5x="), b1 = field (t, "<2x="), b2 = field (t, "<4x="), b3 = field (t, "<8x="), b4 = field (t, ">=8x=");
    std::printf ("INFO  buckets %lld %lld %lld %lld %lld\n", b0, b1, b2, b3, b4);
    check (b0 + b1 + b2 + b3 + b4 == 10, "the buckets add up to the gaps counted");
    check (b4 >= 1 && b3 >= 1 && b3 + b4 >= 2 && b2 + b3 + b4 >= 4 && b1 + b2 + b3 + b4 >= 6, "gaps land in their bucket or above (sleeps are never shorter than asked)");
    check (b0 >= 1 && b0 <= 5, "the short gaps are in the first bucket (a late wake-up may push one up)");
    check (field (t, "gap_max_us: ") >= 100000, "the largest gap is at least the 100 ms wait");
    check (field (t, "process_max_us: ") >= 6000 && field (t, "process_max_us: ") < 90000, "the slow call (6 ms inside the callback) is the process maximum");
    const long long avg = field (t, "process_avg_us: ");
    check (avg >= 0 && avg < field (t, "process_max_us: "), "the mean process time is below the maximum");
    check (field (t, "largest_gaps: ") == 10, "all ten gaps are listed (at most ten are kept)");
    const size_t one = t.find ("  1 at_ms="), two = t.find ("  2 at_ms=");
    check (one != std::string::npos && two != std::string::npos && field (t.substr (one), "gap_us=") >= field (t.substr (two), "gap_us="), "the list is in descending order");
    std::printf ("%s  (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
