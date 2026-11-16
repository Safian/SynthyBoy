// Robustness review of the plugin shell (review of 2026-11-16, docs: .hw-local/mpc-ui-plans/robustness-review.md): the built overlay plugin loaded like the host loads it, hostile conditions one per CHILD process
// (a hang or an AddressSanitizer abort is a result, not the end of the test). Build the plugin with -DSYNTHYBOY_MPC_OVERLAY=ON and the test and the plugin with -fsanitize=address,undefined. Usage: plugin_review_host <plugin>.
//   R4  the name / label / display buffers of effGetParamName / Label / Display: the plugin used to write a NUL at offset 63 every time and names of up to 37 characters, into a buffer whose size it does not
//       know. The bound is kParamStrBytes (32, host_strings.h: the SDK framework's own limit): every parameter of the list is asked into a heap buffer of EXACTLY that size (and of the SDK's program /
//       effect / vendor / product sizes) with a canary after the string: nothing past the terminator may change. Control: a 64 byte buffer is fine.
//   R5  a FIFO at /tmp/synthyboy-overlay-fake-slot (the debug switch the display thread opens with fopen every second): the display thread blocks in fopen for ever, and effClose, which joins it, never returns.
//   R6  a 6 MB single line in the file the display thread getline()s every tick (/tmp/synthyboy-q-fake-group, path from SYNTHYBOY_MPC_Q_FAKE_FILE): the display thread is busy for seconds and effClose waits for it.
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
#include <set>
#include <vector>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "host_strings.h"
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

/** Asks for name / label / display of parameter `index` into a heap buffer of exactly `size` bytes filled with 'Z' (so AddressSanitizer sees every byte past it and the canary shows a write inside it that is not the string).
    The string must end inside the buffer and every byte after its NUL must still be 'Z'. Returns the number of violations. */
static int askStrings (AEffect* e, int index, size_t size, bool exact = false)
{
    int bad = 0;
    for (int op : { effGetParamName, effGetParamLabel, effGetParamDisplay })
    {
        char* buf = static_cast<char*> (std::malloc (size));
        std::memset (buf, 'Z', size);
        e->dispatcher (e, op, index, 0, buf, 0.0f);
        size_t len = 0;
        while (len < size && buf[len] != '\0')
            ++len;
        if (exact)
        {
            bool tail = len < size;
            for (size_t i = len + 1; i < size; ++i)
                tail = tail && buf[i] == 'Z';
            if (! tail)
            {
                std::printf ("INFO  parameter %d opcode %d: the string is not terminated inside %zu bytes or something was written after its NUL\n", index, op, size);
                ++bad;
            }
        }
        std::free (buf);
    }
    return bad;
}

/** The program names, the effect name and the vendor / product strings into buffers of exactly the sizes the SDK documents (+ the NUL), with the same canary. */
static int askOtherStrings (AEffect* e)
{
    int bad = 0;
    auto one = [&] (VstInt32 op, VstInt32 index, size_t size)
    {
        char* buf = static_cast<char*> (std::malloc (size));
        std::memset (buf, 'Z', size);
        e->dispatcher (e, op, index, 0, buf, 0.0f);
        size_t len = 0;
        while (len < size && buf[len] != '\0')
            ++len;
        bool tail = len < size;
        for (size_t i = len + 1; i < size; ++i)
            tail = tail && buf[i] == 'Z';
        if (! tail)
        {
            std::printf ("INFO  opcode %d index %d: not terminated inside %zu bytes or written after the NUL\n", op, index, size);
            ++bad;
        }
        std::free (buf);
    };
    one (effGetProgramName, 0, kVstMaxProgNameLen + 1);
    for (int i = 0; i < e->numPrograms; ++i)
        one (effGetProgramNameIndexed, i, kVstMaxProgNameLen + 1);
    one (effGetEffectName, 0, kVstMaxEffectNameLen + 1);
    one (effGetVendorString, 0, kVstMaxVendorStrLen + 1);
    one (effGetProductString, 0, kVstMaxProductStrLen + 1);
    return bad;
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
    inChild ("R4a: every parameter of the list (the engine's, the display ones, pad pressure, overlay, Play, Q) into a buffer of exactly 32 bytes (kParamStrBytes): nothing written past the string", [&]
    {
        AEffect* e = make();
        if (! e) return 2;
        int bad = 0;
        for (int i = 0; i < e->numParams; ++i)
            bad += askStrings (e, i, synthyboy_mpc::kParamStrBytes, true);
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        return bad == 0 ? 0 : 1;
    });
    inChild ("R4b: the parameter with the longest name into a buffer of exactly 32 bytes; every name of the engine's list is at most 31 characters, readable and unique", [&]
    {
        AEffect* e = make();
        if (! e) return 2;
        int bad = askStrings (e, longest, synthyboy_mpc::kParamStrBytes, true);
        std::set<std::string> seen;
        for (int i = 0; i < nEngine; ++i)
        {
            char name[synthyboy_mpc::kParamStrBytes];
            std::memset (name, 'Z', sizeof name);
            e->dispatcher (e, effGetParamName, i, 0, name, 0.0f);
            const size_t len = ::strnlen (name, sizeof name);
            if (len == 0 || len >= sizeof name || ! seen.insert (name).second)
            {
                std::printf ("INFO  parameter %d: name '%.31s' has %zu characters or is not unique\n", i, name, len);
                ++bad;
            }
        }
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        return bad == 0 ? 0 : 1;
    });
    inChild ("R4c: program names, effect name, vendor and product strings into buffers of exactly the sizes the SDK documents", [&]
    {
        AEffect* e = make();
        if (! e) return 2;
        int bad = askOtherStrings (e);
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        return bad == 0 ? 0 : 1;
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

    inChild ("R6: with a 6 MB single line in the Q-group debug file the plugin stays under 16 MB of extra memory and effClose under 2 s", [&]
    {
        char dir[] = "/tmp/sb-review-XXXXXX";
        if (! mkdtemp (dir)) return 2;
        const std::string file = std::string (dir) + "/qgroup";
        const int fd = ::open (file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0 || ::ftruncate (fd, 6ll << 20) != 0) return 2;   // a sparse file of NUL bytes: no newline anywhere
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
        return (t >= 0.0 && t < 2.0 && grew < 16) ? 0 : 1;
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
