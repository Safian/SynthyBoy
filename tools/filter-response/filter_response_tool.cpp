// SynthyBoy - filter-response: the frequency response of filter 1 / 2 of a preset, as CSV.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_filter_response PRESET.sbpreset [--filter 1|2] [--points N] [--sr 48000] [--note 60] [--cutoff HZ] [--resonance PERCENT] [--fmin HZ] [--fmax HZ] [--level] [--set KEY=VALUE]...
//
// Prints "frequency_hz,gain_db" lines (N log spaced points, default 256 from 20 Hz to 20 kHz); the comment line "# ..." in front names the preset and the filter settings.
// The curve is measured on the real filter code (core/include/synthyboy/filter_response.h). Exit codes: 0 curve printed, 1 the preset does not load, 2 the filter is off,
// 3 bad arguments (unknown option, an option without a value, a value that is not a number, points outside 1 .. 100000, sample rate outside 8000 .. 384000, fmin <= 0 or
// fmax < fmin, a filter other than 1 / 2, a --set with an unknown key). Nothing is printed to stdout for exit code 3 (the message goes to stderr).
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/filter_response.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

using namespace synthyboy;

namespace
{
/** The whole text is one finite number (strtod accepts "nan" and "inf": those are refused). */
bool parseNumber (const std::string& text, double& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod (text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite (v))
        return false;
    out = v;
    return true;
}
} // namespace

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
    std::vector<std::pair<std::string, float>> overrides;   // --set filter1.model=2: any parameter of the preset, applied after it is loaded
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--level")
        {
            req.includeLevel = true;
            continue;
        }
        const bool known = a == "--filter" || a == "--points" || a == "--sr" || a == "--note" || a == "--cutoff" || a == "--resonance" || a == "--fmin" || a == "--fmax" || a == "--set";
        if (!known)
        {
            std::fprintf (stderr, "unknown option %s\n", a.c_str());
            return 3;
        }
        if (i + 1 >= argc)
        {
            std::fprintf (stderr, "option %s needs a value\n", a.c_str());
            return 3;
        }
        const std::string text = argv[++i];
        if (a == "--set")
        {
            const size_t eq = text.find ('=');
            double val = 0.0;
            if (eq == std::string::npos || findParamByKey (text.substr (0, eq).c_str()) == nullptr || !parseNumber (text.substr (eq + 1), val))
            {
                std::fprintf (stderr, "--set needs KEY=VALUE with a known key and a numeric value, got %s\n", text.c_str());
                return 3;
            }
            overrides.emplace_back (text.substr (0, eq), static_cast<float> (val));
            continue;
        }
        double val = 0.0;
        if (!parseNumber (text, val))
        {
            std::fprintf (stderr, "option %s needs a number, got '%s'\n", a.c_str(), text.c_str());
            return 3;
        }
        if (a == "--filter") req.filter = static_cast<int> (val);
        else if (a == "--points") points = static_cast<int> (std::max (-1.0, std::min (val, 1.0e9)));
        else if (a == "--sr") sr = val;
        else if (a == "--note") req.referenceNote = static_cast<int> (std::max (-1000.0, std::min (val, 1000.0)));
        else if (a == "--cutoff") req.cutoffHz = static_cast<float> (val);
        else if (a == "--resonance") req.resonancePercent = static_cast<float> (val);
        else if (a == "--fmin") req.fMin = static_cast<float> (val);
        else if (a == "--fmax") req.fMax = static_cast<float> (val);
    }
    if (points < 1 || points > 100000 || !(sr >= 8000.0 && sr <= 384000.0) || req.filter < 1 || req.filter > 2 || !std::isfinite (req.fMin) || !std::isfinite (req.fMax) || !(req.fMin > 0.0f) ||
        !(req.fMax >= req.fMin) || (req.cutoffHz && !(*req.cutoffHz > 0.0f)))
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
    for (const auto& o : overrides)
        values.plain[findParamIndex (findParamByKey (o.first.c_str())->id)] = o.second;
    auto get = [&] (const char* fmt) { char key[64]; std::snprintf (key, sizeof key, fmt, req.filter); const ParamInfo* p = findParamByKey (key); return p != nullptr ? values.plain[findParamIndex (p->id)] : 0.0f; };
    FilterResponse fr (sr);
    std::vector<float> db (static_cast<size_t> (points));
    const bool on = fr.compute (values, req, db.data(), points);
    std::printf ("# %s filter%d model %d style %d cutoff %.1f Hz resonance %.1f %% (%d samples, %.2f Hz per bin)%s\n", meta.name.c_str(), req.filter, static_cast<int> (get ("filter%d.model")),
                 static_cast<int> (get ("filter%d.style")), req.cutoffHz ? *req.cutoffHz : get ("filter%d.cutoff"),
                 req.resonancePercent ? *req.resonancePercent : get ("filter%d.resonance"), fr.impulseLength(), fr.binWidthHz(), on ? "" : " [filter is off]");
    if (!on)
        return 2;
    const double ratio = points > 1 ? std::pow (static_cast<double> (req.fMax) / static_cast<double> (req.fMin), 1.0 / (points - 1)) : 1.0;
    double f = req.fMin;
    for (int i = 0; i < points; ++i, f *= ratio)
        std::printf ("%.2f,%.3f\n", f, static_cast<double> (db[static_cast<size_t> (i)]));
    return 0;
}
