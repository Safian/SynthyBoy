// Robustness review of the overlay controller (review of 2026-11-16, docs: .hw-local/mpc-ui-plans/robustness-review.md). FAILS on the current code on purpose.
//   R3  the once-a-second switch files (/tmp/synthyboy-overlay-detail, -fps, -look, -rot, -pointer-hotspot, -gpuclock) are read by the overlay thread INSIDE the MPC process with `std::ifstream >> std::string` /
//       `std::getline`: a file of one huge line (or one huge token) is read whole into memory, every second. The test makes a 600 MB sparse file the `detail` switch (a file of NUL bytes: one 600 MB token) and
//       runs the controller across two switch intervals in a child process. A plugin that bounds what it reads from a debug file takes no noticeable memory or time.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fakes.h"
#include "fixtures/legacy_layout.h"

using namespace ovt;

static long peakKb()
{
    rusage ru;
    getrusage (RUSAGE_SELF, &ru);
#if defined(__APPLE__)
    return static_cast<long> (ru.ru_maxrss / 1024);   // bytes on macOS
#else
    return static_cast<long> (ru.ru_maxrss);          // kilobytes on Linux
#endif
}

struct Case { const char* name; int which; };

/** Child: the controller ticks for 2.5 simulated seconds with `which` switch file pointing at a 600 MB sparse file. Exit code 0: bounded (below 64 MB extra, below 1 s of wall time); 1: not. */
static int runCase (int which)
{
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    FakeInstance inst;
    frame.views = legacy::kViews;
    Paths p = dir.paths();
    const std::string big = dir.path + "/big";
    {
        const int fd = ::open (big.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0 || ::ftruncate (fd, 600ll << 20) != 0)
            return 2;
        ::close (fd);
    }
    switch (which)
    {
        case 0: p.detail = big; break;
        case 1: p.rate = big; break;
        case 2: p.pointerHotSpot = big; break;
        case 3: p.gpuClock = big; break;
    }
    Controller ctl (plane, frame, renderer, p, Timing(), 10.0, nullptr, nullptr, legacy::kViews, legacy::kNumViews);
    inst.set ("filter1.cutoff", 1000.0f);
    inst.set ("filter1.resonance", 30.0f);
    ctl.attach (&inst);
    frame.setAll (true, 90);
    double now = 0.0;
    for (int i = 0; i < 20; ++i) { now += 10.0; ctl.tick (now); }   // settle
    const long before = peakKb();
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 250; ++i) { now += 10.0; ctl.tick (now); }
    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const long grew = peakKb() - before;
    std::printf ("INFO  case %d: wall %.2f s for 2.5 simulated seconds, peak memory grew by %ld MB, state '%s'\n", which, wall, grew / 1024, ctl.state().c_str());
    return (grew < 64 * 1024 && wall < 1.0) ? 0 : 1;
}

int main()
{
    // Only the `detail` switch is read by every controller; the fps / gpuclock switches are read only when the GPU helper runs, the pointer hot spot only with a mouse (neither is in this fake rig: the
    // same `>> std::string` / std::getline code, found by reading controller.h:1110 / 1131 / 1044-1047).
    static const Case cases[] = { { "detail switch file with one 600 MB token", 0 } };
    for (const Case& c : cases)
    {
        std::fflush (stdout);
        const pid_t pid = fork();
        if (pid == 0)
        {
            alarm (60);
            const int rc = runCase (c.which);
            std::fflush (stdout);
            _exit (rc);
        }
        int st = 0;
        waitpid (pid, &st, 0);
        const bool ok = WIFEXITED (st) && WEXITSTATUS (st) == 0;
        std::printf ("%s  R3: %s: the controller stays small and fast%s\n", ok ? "PASS" : "FAIL", c.name, WIFSIGNALED (st) ? "  (killed by a signal)" : "");
        if (! ok)
            ++g_fails;
    }
    std::printf (g_fails == 0 ? "OK\n" : "%d FAILED\n", g_fails);
    return g_fails == 0 ? 0 : 1;
}
