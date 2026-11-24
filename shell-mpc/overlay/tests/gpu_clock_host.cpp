// SynthyBoy - MPC overlay tests: the GPU clock hint (gpu_clock.h) on a fake devfreq directory.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <sys/stat.h>

#include "../gpu_clock.h"

using namespace synthyboy_mpc::overlay;

static int g_fails = 0;
static void check (bool ok, const std::string& what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str()); g_fails += ! ok; }
static std::string slurp (const std::string& p) { std::ifstream f (p); std::string s; std::getline (f, s); return s; }

int main()
{
    char tmpl[] = "/tmp/sbgc-XXXXXX";
    const std::string root = ::mkdtemp (tmpl);
    const std::string node = root + "/ffa30000.gpu", state = root + "/orig";
    ::mkdir (node.c_str(), 0755);
    ::mkdir ((root + "/dmc").c_str(), 0755);
    std::ofstream (node + "/min_freq") << "100000000\n";
    std::ofstream (node + "/available_frequencies") << "100000000 200000000 300000000 400000000 600000000\n";
    std::ofstream (root + "/dmc/min_freq") << "5\n";
    {
        GpuClock c (root, state, 300000000);
        check (c.open() && c.node() == node && c.raisedHz() == 300000000, "the node of the GPU is found (not the other devices), the clock to raise to is the lowest one of at least 300 MHz");
        c.boost (true);
        check (c.boosted() && slurp (node + "/min_freq") == "300000000" && slurp (state) == "100000000", "boost: min_freq is raised, the old level is in the state file");
        c.boost (true);
        check (slurp (state) == "100000000", "boost twice: the saved level stays the original one");
        c.boost (false);
        check (! c.boosted() && slurp (node + "/min_freq") == "100000000" && ::access (state.c_str(), F_OK) != 0, "release: min_freq is back, the state file is gone");
        c.boost (true);
    }
    check (slurp (node + "/min_freq") == "100000000", "the destructor puts the clock back");
    // a run that died raised: the next one restores
    std::ofstream (node + "/min_freq") << "300000000\n";
    std::ofstream (state) << "100000000\n";
    {
        GpuClock c (root, state, 300000000);
        c.open();
        check (slurp (node + "/min_freq") == "100000000", "a clock a dead run left raised is put back when the next one opens");
    }
    // already high: not touched
    std::ofstream (node + "/min_freq") << "400000000\n";
    {
        GpuClock c (root, state, 300000000);
        c.boost (true);
        check (! c.boosted() && slurp (node + "/min_freq") == "400000000", "a min_freq that is already at least as high is not touched");
    }
    // the XL: another node name (fb000000.gpu), 300 .. 1000 MHz with min_freq at its lowest: raised to 500 MHz (half of the highest), put back
    {
        char t2[] = "/tmp/sbgc-XXXXXX";
        const std::string r2 = ::mkdtemp (t2), n2 = r2 + "/fb000000.gpu", st2 = r2 + "/orig";
        ::mkdir (n2.c_str(), 0755);
        std::ofstream (n2 + "/min_freq") << "300000000\n";
        std::ofstream (n2 + "/available_frequencies") << "300000000 400000000 500000000 600000000 800000000 1000000000\n";
        {
            GpuClock c (r2, st2, 300000000);
            check (c.open() && c.node() == n2 && c.raisedHz() == 500000000, "XL: the node is found by the name pattern, the clock to raise to is 500 MHz");
            c.boost (true);
            check (c.boosted() && slurp (n2 + "/min_freq") == "500000000", "XL: min_freq is raised above the 300 MHz it sits at");
            c.boost (false);
            check (slurp (n2 + "/min_freq") == "300000000", "XL: put back to the original 300 MHz");
            c.boost (true);
        }
        check (slurp (n2 + "/min_freq") == "300000000", "XL: the destructor puts it back");
        // a device with no gpu node and one whose node lacks the files: silently nothing
        ::mkdir ((r2 + "/gpu-empty").c_str(), 0755);
        ::remove ((n2 + "/available_frequencies").c_str());
        GpuClock c2 (r2, st2, 300000000);
        c2.boost (true);
        check (! c2.open() && ! c2.boosted(), "a gpu node without min_freq / available_frequencies: nothing happens, no error");
        // a helper that finds the file of a dead run puts the level back (open () does it)
        std::ofstream (n2 + "/available_frequencies") << "300000000 500000000 1000000000\n";
        std::ofstream (n2 + "/min_freq") << "500000000\n";
        std::ofstream (st2) << "300000000\n";
        { GpuClock h (r2, st2); h.open(); }
        check (slurp (n2 + "/min_freq") == "300000000" && ::access (st2.c_str(), F_OK) != 0, "a leftover raised level (the plugin died) is put back by whoever opens next (the GPU helper at its exit)");
        (void) std::system (("rm -rf '" + r2 + "'").c_str());
    }
    GpuClock none (root + "/nowhere", state);
    none.boost (true);
    check (! none.open() && ! none.boosted(), "no devfreq directory: nothing happens");
    (void) std::system (("rm -rf '" + root + "'").c_str());
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
