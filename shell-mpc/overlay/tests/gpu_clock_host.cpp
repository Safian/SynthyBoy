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
    GpuClock none (root + "/nowhere", state);
    none.boost (true);
    check (! none.open() && ! none.boosted(), "no devfreq directory: nothing happens");
    (void) std::system (("rm -rf '" + root + "'").c_str());
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
