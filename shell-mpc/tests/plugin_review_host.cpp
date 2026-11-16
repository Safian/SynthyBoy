// Robustness review of the plugin shell (review of 2026-11-16, docs: .hw-local/mpc-ui-plans/robustness-review.md): the built overlay plugin loaded like the host loads it, hostile conditions one per CHILD process
// (a hang or an AddressSanitizer abort is a result, not the end of the test). Build the plugin with -DSYNTHYBOY_MPC_OVERLAY=ON and the test and the plugin with -fsanitize=address,undefined. Usage: plugin_review_host <plugin>.
//   R4  the name / label / display buffers of effGetParamName / Label / Display: the SDK (mpc_vst2_abi.h: kVstMaxParamStrLen = 8, "the conservative limit") promises 9 bytes, the shell's own comment assumes
//       24, and it writes a NUL at offset 63 every time (`buf[63] = '\0'`, synthyboy_mpc.cpp:1666 / 1722 / 1761 / ...) and names of up to 41 characters. Control: a 64 byte buffer is fine.
//   R5  a FIFO at /tmp/synthyboy-overlay-fake-slot (the debug switch the display thread opens with fopen every second): the display thread blocks in fopen for ever, and effClose, which joins it, never returns.
//   R6  a 700 MB single line in the file the display thread getline()s every tick (/tmp/synthyboy-q-fake-group, path from SYNTHYBOY_MPC_Q_FAKE_FILE): the display thread is busy for seconds and effClose waits for it.
//   R7  with the play log switched on (the file /tmp/synthyboy-play-log; here SYNTHYBOY_MPC_PLAY_LOG_FILE) every setParameter of the host takes a mutex and does fopen / fwrite / fclose of a file: file I/O on the
//       thread the host calls setParameter from (the audio thread during automation).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "mpc_vst2_abi.h"
#include "synthyboy/params.h"

using Main = AEffect* (*) (audioMasterCallback);
static Main entry = nullptr;
static int fails = 0;

static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 1; }

static AEffect* make()
{
    AEffect* e = entry (hostCallback);
    if (! e)
        return nullptr;
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    return e;
}

static long peakMb()
{
    rusage ru;
    getrusage (RUSAGE_SELF, &ru);
#if defined(__APPLE__)
    return static_cast<long> (ru.ru_maxrss / (1024 * 1024));   // bytes
#else
    return static_cast<long> (ru.ru_maxrss / 1024);            // kilobytes
#endif
}

static double nowSec() { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); }

/** effClose with a watchdog: seconds it took, or -1 when it was still running after `limit` seconds. */
static double closeTimed (AEffect* e, double limit)
{
    std::atomic<bool> done { false };
    const double t0 = nowSec();
    std::thread t ([&] { e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f); done.store (true); });
    while (! done.load() && nowSec() - t0 < limit)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    if (! done.load())
    {
        t.detach();
        return -1.0;
    }
    t.join();
    return nowSec() - t0;
}

template <class F>
static void inChild (const char* what, F&& body)
{
    std::fflush (stdout);
    const pid_t pid = fork();
    if (pid == 0)
    {
        alarm (60);
        const int rc = body();
        std::fflush (stdout);
        _exit (rc);
    }
    int st = 0;
    waitpid (pid, &st, 0);
    const bool ok = WIFEXITED (st) && WEXITSTATUS (st) == 0;
    std::printf ("%s  %s", ok ? "PASS" : "FAIL", what);
    if (! ok)
    {
        if (WIFSIGNALED (st))
            std::printf ("  (killed by signal %d)", WTERMSIG (st));
        else
            std::printf ("  (exit %d: AddressSanitizer abort or the case's own verdict)", WEXITSTATUS (st));
        ++fails;
    }
    std::printf ("\n");
}

/** Asks for name / label / display of parameter `index` into a heap buffer of exactly `size` bytes (so AddressSanitizer sees every byte past it). */
static void askStrings (AEffect* e, int index, size_t size)
{
    for (int op : { effGetParamName, effGetParamLabel, effGetParamDisplay })
    {
        char* buf = static_cast<char*> (std::malloc (size));
        std::memset (buf, 'Z', size);
        e->dispatcher (e, op, index, 0, buf, 0.0f);
        std::free (buf);
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: plugin_review_host <plugin>\n"); return 2; }
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    const int nEngine = static_cast<int> (synthyboy::paramCount());
    int longest = 0;
    for (int i = 0; i < nEngine; ++i)
        if (std::strlen (synthyboy::paramTable()[i].name) > std::strlen (synthyboy::paramTable()[longest].name))
            longest = i;
    std::printf ("INFO  %d engine parameters, the longest name has %zu characters (parameter %d '%s')\n", nEngine, std::strlen (synthyboy::paramTable()[longest].name), longest, synthyboy::paramTable()[longest].name);

    inChild ("R4 control: name / label / display of every parameter into a 64 byte buffer", [&]
    {
        AEffect* e = make();
        if (! e) return 2;
        for (int i = 0; i < e->numParams; ++i)
            askStrings (e, i, 64);
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        return 0;
    });
    inChild ("R4a: parameter 0 (a short name) into a 32 byte buffer: nothing may be written past the string", [&]
    {
        AEffect* e = make();
        if (! e) return 2;
        askStrings (e, 0, 32);
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        return 0;
    });
    inChild ("R4b: the parameter with the longest name into a 24 byte buffer (the size the shell's own comment assumes)", [&]
    {
        AEffect* e = make();
        if (! e) return 2;
        askStrings (e, longest, 24);
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        return 0;
    });
    inChild ("R4c: parameter 0 into a 9 byte buffer (kVstMaxParamStrLen + 1, the SDK's own number)", [&]
    {
        AEffect* e = make();
        if (! e) return 2;
        askStrings (e, 0, 9);
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        return 0;
    });

    inChild ("R5: effClose returns within 3 s although /tmp/synthyboy-overlay-fake-slot is a FIFO nobody writes", [&]
    {
        const char* path = "/tmp/synthyboy-overlay-fake-slot";
        ::unlink (path);
        if (::mkfifo (path, 0600) != 0) return 2;
        AEffect* e = make();
        if (! e) { ::unlink (path); return 2; }
        std::this_thread::sleep_for (std::chrono::milliseconds (2500));   // the display thread looks at the file once a second
        const double t = closeTimed (e, 3.0);
        std::printf ("INFO  effClose took %s\n", t < 0 ? "more than 3 s (never)" : (std::to_string (t) + " s").c_str());
        ::unlink (path);
        return t >= 0.0 ? 0 : 1;
    });

    inChild ("R6: with a 700 MB single line in the Q-group debug file the plugin stays under 100 MB of extra memory and effClose under 2 s", [&]
    {
        char dir[] = "/tmp/sb-review-XXXXXX";
        if (! mkdtemp (dir)) return 2;
        const std::string file = std::string (dir) + "/qgroup";
        const int fd = ::open (file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0 || ::ftruncate (fd, 700ll << 20) != 0) return 2;   // a sparse file of NUL bytes: no newline anywhere
        ::close (fd);
        ::setenv ("SYNTHYBOY_MPC_Q_FAKE_FILE", file.c_str(), 1);
        const long before = peakMb();
        AEffect* e = make();
        if (! e) return 2;
        std::this_thread::sleep_for (std::chrono::milliseconds (400));   // the display thread's first ticks: it is inside getline now
        const double t = closeTimed (e, 20.0);
        const long grew = peakMb() - before;
        std::printf ("INFO  effClose took %s, peak memory grew by %ld MB\n", t < 0 ? "more than 20 s" : (std::to_string (t) + " s").c_str(), grew);
        std::remove (file.c_str());
        ::rmdir (dir);
        return (t >= 0.0 && t < 2.0 && grew < 100) ? 0 : 1;
    });

    inChild ("R7: setParameter does no file I/O while the play log switch file exists", [&]
    {
        char dir[] = "/tmp/sb-review-XXXXXX";
        if (! mkdtemp (dir)) return 2;
        const std::string sw = std::string (dir) + "/log";
        if (FILE* f = std::fopen (sw.c_str(), "w")) std::fclose (f);
        ::setenv ("SYNTHYBOY_MPC_PLAY_LOG_FILE", sw.c_str(), 1);
        AEffect* e = make();
        if (! e) return 2;
        const std::string out = sw + ".txt";
        std::remove (out.c_str());
        const int idx = 5;
        const double t0 = nowSec();
        for (int i = 0; i < 2000; ++i)
            e->setParameter (e, idx, (i & 1) ? 0.25f : 0.75f);
        const double us = (nowSec() - t0) * 1e6 / 2000.0;
        struct stat st;
        const bool wrote = ::stat (out.c_str(), &st) == 0 && st.st_size > 0;
        std::printf ("INFO  2000 setParameter calls: %.1f us each, log file %s (%lld bytes)\n", us, wrote ? "WRITTEN" : "not written", wrote ? static_cast<long long> (st.st_size) : 0ll);
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        std::remove (out.c_str());
        std::remove (sw.c_str());
        ::rmdir (dir);
        return wrote ? 1 : 0;
    });

    std::printf (fails == 0 ? "OK\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
