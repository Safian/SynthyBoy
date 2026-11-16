// SynthyBoy - MPC overlay: the clock of the GPU while the picture moves. The GPUs of the MPCs are under a devfreq governor (the Key 61: simple_ondemand, 100 .. 600 MHz) that lowers the clock when the load is
// low: a picture that needs 2 ms at 600 MHz then takes 10 ms at 100 MHz, the clock changes cost a stall of tens of milliseconds, and the animation misses its slots. While pictures are produced
// continuously the plugin raises the LOWEST clock the governor may use (min_freq) to a moderate one and puts it back when the picture rests; the governor still goes up when it needs to. The level it was
// at is kept in a file, so a run that died raised does not leave the machine raised. Plain file operations in a directory that tests can fake.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <unistd.h>

#include "../safe_file.h"

namespace synthyboy_mpc { namespace overlay {

class GpuClock
{
public:
    explicit GpuClock (std::string devfreqRoot = "/sys/class/devfreq", std::string stateFile = "/tmp/synthyboy-overlay-gpu-minfreq.orig", long long wantHz = 300000000LL)
        : root_ (std::move (devfreqRoot)), state_ (std::move (stateFile)), wantHz_ (wantHz) {}
    ~GpuClock() { release(); }
    GpuClock (const GpuClock&) = delete;
    GpuClock& operator= (const GpuClock&) = delete;

    /** Finds the node of the GPU (a directory of the devfreq root with "gpu" in its name that has min_freq and available_frequencies) and the clock to raise to: the lowest available one that is at least
        wantHz and below the highest. A min_freq that an earlier run left raised (the state file) is put back first. false: no such node (nothing is done). */
    bool open()
    {
        if (opened_)
            return ! node_.empty();
        opened_ = true;
        node_.clear();
        if (DIR* d = ::opendir (root_.c_str()))
        {
            while (const dirent* e = ::readdir (d))
            {
                const std::string n = e->d_name;
                if (n.empty() || n[0] == '.' || n.find ("gpu") == std::string::npos)
                    continue;
                const std::string p = root_ + "/" + n;
                if (::access ((p + "/min_freq").c_str(), W_OK) == 0 && ::access ((p + "/available_frequencies").c_str(), R_OK) == 0)
                {
                    node_ = p;
                    break;
                }
            }
            ::closedir (d);
        }
        if (node_.empty())
            return false;
        // the clocks the GPU offers (a sysfs file: at most 4 KB of it are read); every number written into min_freq below is one of these (robustness review F16: a file in /tmp must not choose what goes into sysfs)
        std::vector<long long> f;
        {
            char text[4096];
            if (safefile::readSmall ((node_ + "/available_frequencies").c_str(), text, sizeof text) > 0)
                for (const char* at = text; *at != '\0';)
                {
                    char* end = nullptr;
                    const long long v = std::strtoll (at, &end, 10);
                    if (end == at)
                        break;
                    f.push_back (v);
                    at = end;
                }
        }
        long long old = 0;
        if (long n = 0; safefile::readInt (state_.c_str(), n) && n > 0)
            old = n;
        if (old > 0)
        {
            if (std::find (f.begin(), f.end(), old) != f.end())
                write (old);   // the run before this one died with the clock raised
            ::unlink (state_.c_str());
        }
        std::sort (f.begin(), f.end());
        raisedHz_ = 0;
        // a moderate level: at least wantHz and at least half of the highest clock (the Key 61: 100 .. 600 MHz -> 300; the XL: 300 .. 1000 MHz, its lowest is 300 -> 500)
        const long long floorHz = f.empty() ? wantHz_ : std::max (wantHz_, f.back() / 2);
        for (size_t i = 0; i + 1 < f.size(); ++i)
            if (f[i] >= floorHz)
            {
                raisedHz_ = f[i];
                break;
            }
        if (raisedHz_ == 0 && f.size() >= 2)
            raisedHz_ = f[f.size() - 2];
        if (raisedHz_ == 0)
            node_.clear();
        return ! node_.empty();
    }

    /** true: min_freq is raised (the level before is remembered and written to the state file); false: put back. Does nothing when there is no node. */
    void boost (bool on)
    {
        if (! open() || on == boosted_)
            return;
        if (on)
        {
            long cur = 0;
            if (! safefile::readInt ((node_ + "/min_freq").c_str(), cur) || cur <= 0 || cur >= raisedHz_)
                return;   // unreadable, or already at least that high: not ours to change
            saved_ = cur;
            char text[32];
            const int len = std::snprintf (text, sizeof text, "%lld\n", saved_);
            if (safefile::writeSmall (state_.c_str(), text, static_cast<size_t> (len)))
                if (write (raisedHz_))
                    boosted_ = true;
        }
        else
            release();
    }
    void release()
    {
        if (! boosted_)
            return;
        write (saved_);
        ::unlink (state_.c_str());
        boosted_ = false;
    }
    bool boosted() const noexcept { return boosted_; }
    long long raisedHz() const noexcept { return raisedHz_; }
    const std::string& node() const noexcept { return node_; }

private:
    bool write (long long hz) const
    {
        std::ofstream out (node_ + "/min_freq");
        out << hz << "\n";
        return static_cast<bool> (out);
    }
    std::string root_, state_, node_;
    long long wantHz_, raisedHz_ = 0, saved_ = 0;
    bool opened_ = false, boosted_ = false;
};

}} // namespace synthyboy_mpc::overlay
