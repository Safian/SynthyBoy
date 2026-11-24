// The start-up record (shell-mpc/startup_timing.h): every step has its line, open_ms_total is the sum of the parts (within the rounding of the printed values), a step that was not reached says n/a,
// a milestone is stamped once, the host call maxima are kept, and the parts add up whatever order the milestones come in.
// Usage: startup_timing_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "startup_timing.h"

using synthyboy_mpc::StartupTimes;

static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FAIL %s\n", what); ++fails; } }
static void nap() { std::this_thread::sleep_for (std::chrono::milliseconds (2)); }

/** The value after "key: " in the text, or -1 when the line is missing or says n/a. */
static double valueOf (const std::string& text, const std::string& key)
{
    const std::string needle = key + ": ";
    size_t at = text.find ("\n" + needle);
    if (at == std::string::npos)
    {
        if (text.compare (0, needle.size(), needle) != 0)
            return -2.0;   // no such line
        at = 0;
    }
    else
        at += 1;
    const char* v = text.c_str() + at + needle.size();
    if (std::strncmp (v, "n/a", 3) == 0)
        return -1.0;
    return std::atof (v);
}

int main()
{
    {
        StartupTimes t;
        // nothing begun: marks are ignored, every step says n/a, the total too
        t.mark (StartupTimes::kEngineCreate);
        std::string text = t.describe();
        bool allNa = true;
        for (int m = 0; m < StartupTimes::kMilestones; ++m)
            allNa = allNa && valueOf (text, std::string ("open_ms_") + StartupTimes::milestoneName (m)) == -1.0;
        check (allNa, "before begin () every step is n/a");
        check (valueOf (text, "open_ms_total") == -1.0 && valueOf (text, "open_ms_total_ui") == -1.0, "total and total_ui are n/a before anything happened");
    }
    {
        StartupTimes t;
        t.begin();
        for (int m = 0; m < StartupTimes::kMilestones; ++m)
        {
            nap();
            t.mark (static_cast<StartupTimes::Milestone> (m));
        }
        nap();
        t.markUi();
        t.addDetail (StartupTimes::kStartOverlay, 5000000);
        t.setWavetableMs (12.5);
        t.callDone (StartupTimes::kCallGetChunk, 3000000);
        t.callDone (StartupTimes::kCallGetChunk, 1000000);
        const std::string text = t.describe();

        double sum = 0.0;
        bool every = true;
        for (int m = 0; m < StartupTimes::kMilestones; ++m)
        {
            const double v = valueOf (text, std::string ("open_ms_") + StartupTimes::milestoneName (m));
            every = every && v > 0.0;
            sum += v;
        }
        check (every, "every step has a line with a positive time");
        const double total = valueOf (text, "open_ms_total");
        check (total > 0.0, "open_ms_total is there");
        check (std::fabs (sum - total) <= 0.0005 * (StartupTimes::kMilestones + 1) + 1.0e-9, "open_ms_total is the sum of the parts within the rounding");
        const double totalUi = valueOf (text, "open_ms_total_ui");
        check (totalUi > total, "the first UI frame came after the first audio block in this sequence: open_ms_total_ui > open_ms_total");
        check (valueOf (text, "open_ms_detail_start_overlay") == 5.0, "a detail is kept in ms");
        check (valueOf (text, "open_ms_detail_wavetables") == 12.5, "the wavetable time is kept");
        // our time inside the host's calls is split by where it falls
        {
            StartupTimes u;
            u.begin();
            u.callDone (StartupTimes::kCallSetChunk, 7000000);          // before the resume call
            nap(); u.mark (StartupTimes::kResume);
            u.callDone (StartupTimes::kCallGetChunk, 2000000);          // after it, before the first audio block
            u.callDone (StartupTimes::kCallMains, 50000000);            // the resume call itself is not counted here (it is engine_prepare)
            nap(); u.mark (StartupTimes::kPrepared);
            u.addDetail (StartupTimes::kFirstBlock, 1000000);
            nap(); u.mark (StartupTimes::kFirstAudio);
            u.callDone (StartupTimes::kCallSetChunk, 9000000);          // after the first audio: in no sum
            const std::string t = u.describe();
            check (valueOf (t, "open_ms_ours_in_calls_before_resume") == 7.0, "our time in calls before the resume call");
            check (valueOf (t, "open_ms_ours_in_calls_resume_to_first_audio") == 2.0, "our time in calls from the resume call to the first audio block (the resume call itself excluded)");
            check (t.find ("open_ms_host_only_until_first_audio:") != std::string::npos && valueOf (t, "open_ms_host_only_until_first_audio") >= 0.0, "the host only share is given");
            u.markUiStep (StartupTimes::kUiHelperAsked);
            const std::string t2 = u.describe();
            check (valueOf (t2, "open_ms_at_helper_asked") > 0.0 && valueOf (t2, "open_ms_at_first_frame") == -1.0, "the UI's steps have lines, n/a until reached");
        }
        check (text.find ("host_call_max_ms_effGetChunk: 3.000 (2 calls, 4.000 ms in all)") != std::string::npos, "the longest stay of a host call and the count are kept");

        const StartupTimes::Parts p = t.parts();
        check (std::fabs (p.sumOfPartsMs - p.totalMs) < 1.0e-9, "the parts add up to the total exactly");

        // a milestone is stamped once: a later mark changes nothing
        const std::string before = t.describe();
        nap();
        t.mark (StartupTimes::kEngineCreate);
        check (t.describe() == before, "a milestone is stamped once");
    }
    {
        // out of order (the host's first audio block before our resume stamp, e.g. a second instance): the parts are the differences of neighbours in time and still add up
        StartupTimes t;
        t.begin();
        nap(); t.mark (StartupTimes::kFirstAudio);
        nap(); t.mark (StartupTimes::kEngineCreate);
        nap(); t.mark (StartupTimes::kPrepared);
        const StartupTimes::Parts p = t.parts();
        check (p.reached[StartupTimes::kFirstAudio] && p.reached[StartupTimes::kEngineCreate] && p.reached[StartupTimes::kPrepared] && ! p.reached[StartupTimes::kResume], "only the stamped steps are reached");
        check (std::fabs (p.sumOfPartsMs - p.totalMs) < 1.0e-9 && p.totalMs > 0.0, "out of order the parts still add up to the last stamp");
        const std::string text = t.describe();
        check (valueOf (text, "open_ms_engine_prepare") > 0.0 && valueOf (text, "open_ms_host_until_resume") == -1.0, "a step that was not reached says n/a");
    }
    {
        // the record has every step by name (the list the integration session reads on the device)
        StartupTimes t;
        const std::string text = t.describe();
        static const char* const kWant[] = { "engine_create", "content_store", "scan_presets", "default_params", "vst_main_rest", "host_until_first_call", "host_until_resume", "engine_prepare", "host_until_first_audio" };
        bool names = StartupTimes::kMilestones == static_cast<int> (sizeof kWant / sizeof kWant[0]);
        for (int m = 0; names && m < StartupTimes::kMilestones; ++m)
            names = std::strcmp (StartupTimes::milestoneName (m), kWant[m]) == 0 && text.find (std::string ("open_ms_") + kWant[m] + ":") != std::string::npos;
        check (names, "the nine steps have the documented names and a line each");
        check (text.find ("open_ms_total:") != std::string::npos && text.find ("open_ms_total_ui:") != std::string::npos, "open_ms_total and open_ms_total_ui have a line");
    }
    if (fails == 0)
        std::printf ("startup_timing_host: all passed\n");
    return fails == 0 ? 0 : 1;
}
