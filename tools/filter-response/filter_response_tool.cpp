// SynthyBoy - filter-response: the frequency response of filter 1 / 2 of a preset, as CSV.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_filter_response PRESET.sbpreset [--filter 1|2] [--points N] [--sr 48000] [--note 60] [--cutoff HZ] [--resonance PERCENT] [--fmin HZ] [--fmax HZ] [--level]
//
// Prints "frequency_hz,gain_db" lines (N log spaced points, default 256 from 20 Hz to 20 kHz); the comment line "# ..." in front names the preset and the filter settings.
// The curve is measured on the real filter code (core/include/synthyboy/filter_response.h). Exit code 1: the preset does not load, 2: the filter is off, 3: bad arguments.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "synthyboy/filter_response.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

using namespace synthyboy;

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: %s PRESET.sbpreset [--filter 1|2] [--points N] [--sr HZ] [--note N] [--cutoff HZ] [--resonance PERCENT] [--fmin HZ] [--fmax HZ] [--level]\n", argv[0]);
        return 3;
    }
    const char* path = argv[1];
    FilterResponse::Request req;
    int points = 256;
    double sr = 48000.0;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if (a == "--filter") req.filter = std::atoi (next());
        else if (a == "--points") points = std::atoi (next());
        else if (a == "--sr") sr = std::atof (next());
        else if (a == "--note") req.referenceNote = std::atoi (next());
        else if (a == "--cutoff") req.cutoffHz = static_cast<float> (std::atof (next()));
        else if (a == "--resonance") req.resonancePercent = static_cast<float> (std::atof (next()));
        else if (a == "--fmin") req.fMin = static_cast<float> (std::atof (next()));
        else if (a == "--fmax") req.fMax = static_cast<float> (std::atof (next()));
        else if (a == "--level") req.includeLevel = true;
        else { std::fprintf (stderr, "unknown option %s\n", a.c_str()); return 3; }
    }
    if (points < 1 || points > 100000 || sr < 8000.0 || sr > 384000.0 || req.filter < 1 || req.filter > 2)
    {
        std::fprintf (stderr, "bad arguments\n");
        return 3;
    }
    std::ifstream in (path, std::ios::binary);
    const std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    PresetMeta meta;
    ParamValues values = ParamValues::defaults();
    StateContent content;
    if (text.empty() || loadPreset (text.data(), text.size(), meta, values, content).status != LoadStatus::ok)
    {
        std::fprintf (stderr, "%s: does not load\n", path);
        return 1;
    }
    auto get = [&] (const char* fmt) { char key[64]; std::snprintf (key, sizeof key, fmt, req.filter); const ParamInfo* p = findParamByKey (key); return p != nullptr ? values.plain[findParamIndex (p->id)] : 0.0f; };
    FilterResponse fr (sr);
    std::vector<float> db (static_cast<size_t> (points));
    const bool on = fr.compute (values, req, db.data(), points);
    std::printf ("# %s filter%d model %d style %d cutoff %.1f Hz resonance %.1f %% (%d samples, %.2f Hz per bin)%s\n", meta.name.c_str(), req.filter, static_cast<int> (get ("filter%d.model")),
                 static_cast<int> (get ("filter%d.style")), req.cutoffHz > 0.0f ? req.cutoffHz : get ("filter%d.cutoff"),
                 req.resonancePercent >= 0.0f ? req.resonancePercent : get ("filter%d.resonance"), fr.impulseLength(), fr.binWidthHz(), on ? "" : " [filter is off]");
    if (!on)
        return 2;
    const double ratio = points > 1 ? std::pow (static_cast<double> (req.fMax) / static_cast<double> (req.fMin), 1.0 / (points - 1)) : 1.0;
    double f = req.fMin;
    for (int i = 0; i < points; ++i, f *= ratio)
        std::printf ("%.2f,%.3f\n", f, static_cast<double> (db[static_cast<size_t> (i)]));
    return 0;
}
