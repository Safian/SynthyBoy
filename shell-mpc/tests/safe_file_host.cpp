// The one reader / writer of the switch and status files (shell-mpc/safe_file.h): what it does with the files a hostile /tmp can put at the paths the plugin looks at (review findings F3, F4, F16).
// Plain C++17 and POSIX; runs under AddressSanitizer + UBSan and ThreadSanitizer. Usage: safe_file_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "safe_file.h"

using namespace synthyboy_mpc::safefile;

static int fails = 0;
static void check (bool ok, const char* what)
{
    if (! ok)
    {
        std::printf ("FAIL %s\n", what);
        ++fails;
    }
}
static mode_t curUmask() { const mode_t m = ::umask (022); ::umask (m); return m; }
static double nowSec() { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
static long peakKb()
{
    rusage ru;
    getrusage (RUSAGE_SELF, &ru);
#if defined(__APPLE__)
    return static_cast<long> (ru.ru_maxrss / 1024);
#else
    return static_cast<long> (ru.ru_maxrss);
#endif
}
static void put (const std::string& path, const std::string& text)
{
    if (FILE* f = std::fopen (path.c_str(), "wb")) { std::fwrite (text.data(), 1, text.size(), f); std::fclose (f); }
}
static std::string slurp (const std::string& path)
{
    std::string s;
    if (FILE* f = std::fopen (path.c_str(), "rb")) { char b[4096]; size_t n; while ((n = std::fread (b, 1, sizeof b, f)) > 0) s.append (b, n); std::fclose (f); }
    return s;
}

int main()
{
    char tmpl[] = "/tmp/sb-safefile-XXXXXX";
    if (! mkdtemp (tmpl)) return 2;
    const std::string d = tmpl;
    char buf[kSwitchBytes];

    // an ordinary file: the number, the word, the line
    put (d + "/num", "  90 \n");
    double v = 0.0;
    long iv = 0;
    check (readNumber ((d + "/num").c_str(), v) && v == 90.0, "number");
    check (readInt ((d + "/num").c_str(), iv) && iv == 90, "integer");
    put (d + "/word", "\nreduced extra\n");
    check (readWord ((d + "/word").c_str(), buf, sizeof buf) && std::strcmp (buf, "reduced") == 0, "first word");
    put (d + "/line", "3 7 -1 -1\nsecond line\n");
    check (readFirstLine ((d + "/line").c_str(), buf, sizeof buf) && std::strcmp (buf, "3 7 -1 -1") == 0, "first line");
    put (d + "/two", "2 0.75\n");
    double a = 0.0, b = 0.0;
    check (readTwoNumbers ((d + "/two").c_str(), a, b) && a == 2.0 && b == 0.75, "two numbers");
    put (d + "/nan", "nan\n");
    check (! readNumber ((d + "/nan").c_str(), v), "nan is no number");
    put (d + "/empty", "");
    check (readSmall ((d + "/empty").c_str(), buf, sizeof buf) == 0 && ! readWord ((d + "/empty").c_str(), buf, sizeof buf), "empty file");
    check (readSmall ((d + "/absent").c_str(), buf, sizeof buf) == -1, "absent file");
    check (readSmall (nullptr, buf, sizeof buf) == -1 && readSmall ("", buf, sizeof buf) == -1, "no path");

    // the bound: a long single line is cut at cap - 1, the buffer is NUL terminated
    put (d + "/long", std::string (100000, 'x'));
    check (readSmall ((d + "/long").c_str(), buf, 16) == 15 && std::strlen (buf) == 15, "long file is cut at the buffer");
    check (readWord ((d + "/long").c_str(), buf, 8) && std::strlen (buf) == 7, "long word is cut at the buffer");

    // a FIFO nobody writes: an ordinary fopen / ifstream blocks here for ever; this returns at once
    ::mkfifo ((d + "/fifo").c_str(), 0600);
    double t0 = nowSec();
    check (readSmall ((d + "/fifo").c_str(), buf, sizeof buf) == -1, "FIFO without a writer is ignored");
    check (nowSec() - t0 < 1.0, "FIFO read does not block");
    // a FIFO with a writer that wrote something is still a FIFO: ignored
    {
        const int wfd = ::open ((d + "/fifo").c_str(), O_RDWR | O_NONBLOCK);
        if (wfd >= 0) { (void) ::write (wfd, "123\n", 4); check (readSmall ((d + "/fifo").c_str(), buf, sizeof buf) == -1, "FIFO with data is ignored"); ::close (wfd); }
    }
    // a FIFO for writing: the open must not block and must fail
    t0 = nowSec();
    check (! writeSmall ((d + "/fifo").c_str(), "x", 1), "FIFO is not written");
    check (nowSec() - t0 < 1.0, "FIFO write does not block");

    // symlinks (to a regular file, to a device, to a directory): not followed
    put (d + "/target", "42\n");
    ::symlink ((d + "/target").c_str(), (d + "/link").c_str());
    check (readSmall ((d + "/link").c_str(), buf, sizeof buf) == -1, "symlink to a file is not followed for reading");
    ::symlink ("/dev/zero", (d + "/zero").c_str());
    check (readSmall ((d + "/zero").c_str(), buf, sizeof buf) == -1, "symlink to /dev/zero is ignored");
    check (readSmall ("/dev/zero", buf, sizeof buf) == -1, "/dev/zero itself is ignored (not a regular file)");
    check (readSmall (d.c_str(), buf, sizeof buf) == -1, "a directory is ignored");
    check (! writeSmall ((d + "/link").c_str(), "overwritten", 11) && slurp (d + "/target") == "42\n", "a symlink is not written through");
    check (! appendSmall ((d + "/link").c_str(), "more", 4) && slurp (d + "/target") == "42\n", "a symlink is not appended through");
    check (! truncateRegular ((d + "/link").c_str()) && slurp (d + "/target") == "42\n", "a symlink is not truncated through");
    check (regularFileSize ((d + "/link").c_str()) == -1 && regularFileSize ((d + "/target").c_str()) == 3, "regularFileSize does not follow");

    // a huge sparse file: the read costs 256 bytes, no memory, no time
    {
        const int fd = ::open ((d + "/sparse").c_str(), O_CREAT | O_WRONLY, 0644);
        check (fd >= 0 && ::ftruncate (fd, 8ll << 20) == 0, "make a 8 MB sparse file");
        if (fd >= 0) ::close (fd);
        const long before = peakKb();
        t0 = nowSec();
        for (int i = 0; i < 200; ++i)
            readSmall ((d + "/sparse").c_str(), buf, sizeof buf);
        check (nowSec() - t0 < 2.0, "200 reads of a 8 MB file are fast");
        check (peakKb() - before < 2048, "reading a 8 MB file does not grow the memory");
        check (readWord ((d + "/sparse").c_str(), buf, sizeof buf) == false, "a sparse file of NULs has no word");
    }

    // writing: create with the mode, truncate, bounded, replacing
    check (writeSmall ((d + "/w").c_str(), "hello\n", 6) && slurp (d + "/w") == "hello\n", "write");
    check (writeSmall ((d + "/w").c_str(), "yo", 2) && slurp (d + "/w") == "yo", "write truncates");
    check (appendSmall ((d + "/w").c_str(), "!", 1) && slurp (d + "/w") == "yo!", "append");
    {
        struct stat st;
        ::stat ((d + "/w").c_str(), &st);
        check ((st.st_mode & 0777) == (0644 & ~curUmask()), "mode is 0644 (minus umask)");
    }
    check (writeReplacing ((d + "/atomic").c_str(), "one", 3) && slurp (d + "/atomic") == "one", "replacing: create");
    check (writeReplacing ((d + "/atomic").c_str(), "two!", 4) && slurp (d + "/atomic") == "two!", "replacing: replace");
    ::symlink ((d + "/target").c_str(), (d + "/atomiclink").c_str());
    check (writeReplacing ((d + "/atomiclink").c_str(), "R", 1) && slurp (d + "/target") == "42\n" && slurp (d + "/atomiclink") == "R", "replacing: a symlink at the name is replaced, its target untouched");
    ::symlink ((d + "/target").c_str(), (d + "/atomic2.tmp").c_str());
    check (writeReplacing ((d + "/atomic2").c_str(), "Z", 1) && slurp (d + "/target") == "42\n" && slurp (d + "/atomic2") == "Z", "replacing: a symlink at the temporary name is not followed");

    // two threads reading and writing the same file (TSan): no crash, always a whole value or nothing
    {
        std::atomic<bool> stop { false };
        std::thread w ([&] { int i = 0; while (! stop.load()) { char t[16]; const int n = std::snprintf (t, sizeof t, "%d\n", (i++ & 1) ? 30 : 60); writeReplacing ((d + "/race").c_str(), t, static_cast<size_t> (n)); } });
        int bad = 0;
        for (int i = 0; i < 20000; ++i)
        {
            long x = 0;
            if (readInt ((d + "/race").c_str(), x) && x != 30 && x != 60) ++bad;
        }
        stop.store (true);
        w.join();
        check (bad == 0, "a replaced file is read whole or not at all");
    }

    std::string cmd = "rm -rf '" + d + "'";
    if (std::system (cmd.c_str()) != 0) {}
    std::printf (fails == 0 ? "OK\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
