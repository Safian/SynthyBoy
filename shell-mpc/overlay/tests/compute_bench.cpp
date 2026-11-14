// SynthyBoy - MPC overlay: how long does one full computation of the filter curve take? (FilterResponse::compute for every model of filter 1, then the three that are
// used as the reference: the cost decides how often the overlay may recompute.) Run it on the device: a static arm32 build is made by the build tree of the plugin.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//   compute_bench [repetitions per model, default 20] [sample rate, default 48000]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "synthyboy/filter_response.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"

int main (int argc, char** argv)
{
    const int reps = argc > 1 ? std::atoi (argv[1]) : 20;
    const double sr = argc > 2 ? std::atof (argv[2]) : 48000.0;
    synthyboy::FilterResponse response (sr);
    synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
    const auto idx = [] (const char* k) { return static_cast<int> (synthyboy::findParamByKey (k) - synthyboy::paramTable()); };
    values.plain[idx ("filter1.on")] = 1.0f;
    values.plain[idx ("filter1.resonance")] = 40.0f;
    float db[256];
    synthyboy::FilterResponse::Request req;
    req.filter = 1;
    req.cutoffHz = 1000.0f;
    double all = 0.0, worst = 0.0;
    std::vector<double> perModel;
    for (int model = 0; model < 27; ++model)
    {
        values.plain[idx ("filter1.model")] = static_cast<float> (model);
        response.compute (values, req, db, 256);   // warm up
        std::vector<double> t;
        for (int i = 0; i < reps; ++i)
        {
            req.cutoffHz = 400.0f + 37.0f * static_cast<float> (i);
            const auto t0 = std::chrono::steady_clock::now();
            response.compute (values, req, db, 256);
            t.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
        }
        std::sort (t.begin(), t.end());
        const double med = t[t.size() / 2], mx = t.back();
        std::printf ("model %2d: median %.2f ms  min %.2f ms  max %.2f ms\n", model, med, t.front(), mx);
        perModel.push_back (med);
        all += med;
        worst = std::max (worst, mx);
    }
    std::vector<double> sorted = perModel;
    std::sort (sorted.begin(), sorted.end());
    std::printf ("SUMMARY sr %.0f: median of the model medians %.2f ms, fastest model %.2f ms, slowest model %.2f ms, worst single call %.2f ms, mean %.2f ms\n", sr,
                 sorted[sorted.size() / 2], sorted.front(), sorted.back(), worst, all / 27.0);
    return 0;
}
