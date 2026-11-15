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
        std::ifstream left (state_);
        long long old = 0;
        if (left && (left >> old) && old > 0)
        {
            write (old);   // the run before this one died with the clock raised
            std::remove (state_.c_str());
        }
        std::ifstream av (node_ + "/available_frequencies");
        std::vector<long long> f;
        long long v = 0;
        while (av >> v)
            f.push_back (v);
        std::sort (f.begin(), f.end());
        raisedHz_ = 0;
        for (size_t i = 0; i + 1 < f.size(); ++i)
            if (f[i] >= wantHz_)
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
            std::ifstream in (node_ + "/min_freq");
            long long cur = 0;
            if (! (in >> cur) || cur <= 0 || cur >= raisedHz_)
                return;   // unreadable, or already at least that high: not ours to change
            saved_ = cur;
            if (std::ofstream (state_) << saved_ << "\n")
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
        std::remove (state_.c_str());
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
