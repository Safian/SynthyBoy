// SynthyBoy - what a quality profile step changes in the sound: renders every factory preset under a reference profile and under other profiles
// (shell-mpc/device_profile.h) and prints, per preset and profile, the difference of the level and of the long term spectrum.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_profile_compare --presets DIR [--reference low] [--config PROFILE]... [--top 10] [--csv FILE] [--filter SUBSTRING] [--sr 44100] [--bands]
//
// The render: a chord of four notes (C2 E2 G2 C3 for the category Bass, else C3 E3 G3 C4) at velocity 0.8, all at the start of a block, held 2 s and released for
// 1.5 s; 128 frame blocks, the parameters by setParam and then the content, as the MPC shell loads a program. Deterministic (Quality::realtime = false).
// The numbers, B (the profile) against A (the reference):
//   level    RMS of B minus RMS of A over the whole render, dB
//   peak     peak of B minus peak of A, dB
//   bands    the power spectrum (Hann windows of 4096 samples, hop 2048, both channels, averaged over the render) summed into third octave bands from 40 Hz to
//            20 kHz. "loud bands": the mean absolute difference in dB over the bands within 30 dB of the loudest band of A or B (what carries the sound; the list is
//            sorted by it). "all bands": the same over the bands within 60 dB, with the largest single difference and its band: this one also counts content far
//            below the sound itself (a filter's limiter driven a little harder shows as 40 dB here and is not heard as that)
//   above    the level of everything above 10 kHz, B minus A, dB (aliasing and added brilliance show here)
// Default profiles: the single steps over low (unison 7 and 16, warp 4x, morph full, math exact, control full, fx all) and "high,voices=8".
// Prints the --top presets by mean band difference for each profile, and the median over all presets. No assertion: the numbers are for the owner to judge a step.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

#include "device_profile.h"

using namespace synthyboy;

namespace
{
struct PresetData
{
    std::string name, category;
    ParamValues values = ParamValues::defaults();
    StateContent content;
};

bool loadPresetFile (const std::string& path, PresetData& out)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return false;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    PresetMeta meta;
    const LoadResult r = loadPreset (text.data(), text.size(), meta, out.values, out.content);
    if (r.status != LoadStatus::ok && r.status != LoadStatus::newer_version)
        return false;
    out.name = meta.name.empty() ? path : meta.name;
    out.category = meta.category;
    return true;
}

struct Render
{
    std::vector<float> l, r;
};

Render render (const PresetData& pd, const synthyboy_mpc::Profile& profile, double sampleRate)
{
    auto engine = createEngine (EngineKind::own);
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        engine->setParam (table[i].id, table[i].def);
    for (size_t i = 0; i < paramCount(); ++i)
        engine->setParam (table[i].id, pd.values.plain[i]);
    engine->setContent (pd.content);
    Quality q = synthyboy_mpc::profileQuality (profile);
    q.realtime = false;
    const int block = 128;
    engine->prepare (sampleRate, block, q);
    const int base = pd.category == "Bass" ? 36 : 48;
    const int keys[4] = { base, base + 4, base + 7, base + 12 };
    const int holdBlocks = static_cast<int> (2.0 * sampleRate / block), totalBlocks = static_cast<int> (3.5 * sampleRate / block);
    Render out;
    std::vector<float> l (block), r (block);
    for (int b = 0; b < totalBlocks; ++b)
    {
        Event ev[4];
        int n = 0;
        if (b == 0 || b == holdBlocks)
            for (int k = 0; k < 4; ++k)
            {
                ev[n].type = b == 0 ? Event::NoteOn : Event::NoteOff;
                ev[n].key = static_cast<int16_t> (keys[k]);
                ev[n].value = 0.8f;
                ++n;
            }
        engine->process (ev, n, l.data(), r.data(), block);
        out.l.insert (out.l.end(), l.begin(), l.end());
        out.r.insert (out.r.end(), r.begin(), r.end());
    }
    return out;
}

void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

constexpr int kFft = 4096;
constexpr int kBands = 28; // third octaves, 40 Hz .. 20 kHz

struct Spectrum
{
    double band[kBands] = {};
    double above10k = 0.0;
    double rms = 0.0, peak = 0.0;
};

Spectrum analyse (const Render& x, double sampleRate)
{
    Spectrum s;
    std::vector<double> power (kFft / 2 + 1, 0.0);
    std::vector<std::complex<double>> buf (kFft);
    int frames = 0;
    for (const std::vector<float>* ch : { &x.l, &x.r })
        for (size_t at = 0; at + kFft <= ch->size(); at += kFft / 2)
        {
            for (int i = 0; i < kFft; ++i)
                buf[static_cast<size_t> (i)] = static_cast<double> ((*ch)[at + static_cast<size_t> (i)]) * (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / kFft));
            fft (buf);
            for (int i = 0; i <= kFft / 2; ++i)
                power[static_cast<size_t> (i)] += std::norm (buf[static_cast<size_t> (i)]);
            ++frames;
        }
    for (int i = 1; i <= kFft / 2; ++i)
    {
        const double hz = i * sampleRate / kFft, p = power[static_cast<size_t> (i)] / std::max (frames, 1);
        const int b = static_cast<int> (std::floor (3.0 * std::log2 (hz / 40.0) + 0.5));
        if (b >= 0 && b < kBands)
            s.band[b] += p;
        if (hz > 10000.0)
            s.above10k += p;
    }
    double sum = 0.0;
    for (const std::vector<float>* ch : { &x.l, &x.r })
        for (const float v : *ch)
        {
            sum += static_cast<double> (v) * v;
            s.peak = std::max (s.peak, static_cast<double> (std::fabs (v)));
        }
    s.rms = std::sqrt (sum / static_cast<double> (x.l.size() + x.r.size()));
    return s;
}

double db (double power) { return 10.0 * std::log10 (power + 1e-30); }

struct Row
{
    std::string preset, category;
    double level = 0.0, peak = 0.0, bandMean = 0.0, bandMax = 0.0, bandMaxHz = 0.0, above = 0.0, loudMean = 0.0;
    bool identical = false;
};
} // namespace

int main (int argc, char** argv)
{
    std::string dir, reference = "low", csvPath, filter;
    std::vector<std::string> configs;
    int top = 10;
    bool bands = false;
    double sampleRate = 44100.0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> const char* { if (i + 1 >= argc) { std::fprintf (stderr, "missing value\n"); std::exit (2); } return argv[++i]; };
        if (a == "--presets") dir = next();
        else if (a == "--reference") reference = next();
        else if (a == "--config") configs.push_back (next());
        else if (a == "--top") top = std::atoi (next());
        else if (a == "--csv") csvPath = next();
        else if (a == "--filter") filter = next();
        else if (a == "--sr") sampleRate = std::atof (next());
        else if (a == "--bands") bands = true;
        else { std::fprintf (stderr, "usage: synthyboy_profile_compare --presets DIR [--reference low] [--config PROFILE]... [--top 10] [--csv FILE] [--filter SUBSTRING] [--sr 44100]\n"); return 2; }
    }
    if (dir.empty())
    {
        std::fprintf (stderr, "--presets DIR is needed\n");
        return 2;
    }
    if (configs.empty())
        configs = { "low,unison=4", "low,unison=7", "low,unison=16", "low,warp=4x", "low,morph=full", "low,math=exact", "low,control=full", "low,fx=all", "high,voices=8" };
    synthyboy_mpc::Profile ref;
    std::string why;
    if (! synthyboy_mpc::parseProfile (reference, ref, &why))
    {
        std::fprintf (stderr, "reference '%s': %s\n", reference.c_str(), why.c_str());
        return 2;
    }
    std::vector<synthyboy_mpc::Profile> profiles;
    for (const auto& c : configs)
    {
        synthyboy_mpc::Profile p;
        if (! synthyboy_mpc::parseProfile (c, p, &why))
        {
            std::fprintf (stderr, "profile '%s': %s\n", c.c_str(), why.c_str());
            return 2;
        }
        profiles.push_back (p);
    }
    auto entries = scanPresetDirectory (dir.c_str());
    std::sort (entries.begin(), entries.end(), [] (const PresetEntry& x, const PresetEntry& y) { return x.path < y.path; });
    std::vector<PresetData> presets;
    for (const auto& e : entries)
    {
        PresetData pd;
        if (loadPresetFile (e.path, pd) && (filter.empty() || pd.name.find (filter) != std::string::npos))
            presets.push_back (std::move (pd));
    }
    if (presets.empty())
    {
        std::fprintf (stderr, "no preset below %s\n", dir.c_str());
        return 1;
    }

    std::vector<Render> refRender;
    std::vector<Spectrum> refSpec;
    for (const auto& pd : presets)
    {
        refRender.push_back (render (pd, ref, sampleRate));
        refSpec.push_back (analyse (refRender.back(), sampleRate));
    }
    FILE* csv = csvPath.empty() ? nullptr : std::fopen (csvPath.c_str(), "w");
    if (csv != nullptr)
        std::fprintf (csv, "reference,profile,preset,category,identical,level_db,peak_db,loud_band_mean_db,band_mean_db,band_max_db,band_max_hz,above_10k_db\n");
    std::printf ("synthyboy_profile_compare: %zu presets, reference '%s', %.0f Hz, a four note chord held 2 s + 1.5 s of release\n", presets.size(), reference.c_str(), sampleRate);
    for (size_t c = 0; c < configs.size(); ++c)
    {
        std::vector<Row> rows;
        int identical = 0;
        for (size_t p = 0; p < presets.size(); ++p)
        {
            const Render b = render (presets[p], profiles[c], sampleRate);
            const Spectrum sb = analyse (b, sampleRate);
            const Spectrum& sa = refSpec[p];
            Row row;
            row.preset = presets[p].name;
            row.category = presets[p].category;
            row.identical = b.l == refRender[p].l && b.r == refRender[p].r;
            identical += row.identical ? 1 : 0;
            row.level = 20.0 * std::log10 ((sb.rms + 1e-15) / (sa.rms + 1e-15));
            row.peak = 20.0 * std::log10 ((sb.peak + 1e-15) / (sa.peak + 1e-15));
            row.above = db (sb.above10k) - db (sa.above10k);
            double loudest = 0.0;
            for (int k = 0; k < kBands; ++k)
                loudest = std::max (loudest, std::max (sa.band[k], sb.band[k]));
            int used = 0, usedLoud = 0;
            for (int k = 0; k < kBands; ++k)
            {
                if (std::max (sa.band[k], sb.band[k]) < loudest * 1e-6)
                    continue;
                const double d = std::fabs (db (sb.band[k]) - db (sa.band[k]));
                row.bandMean += d;
                ++used;
                if (std::max (sa.band[k], sb.band[k]) >= loudest * 1e-3)
                {
                    row.loudMean += d;
                    ++usedLoud;
                }
                if (d > row.bandMax)
                {
                    row.bandMax = d;
                    row.bandMaxHz = 40.0 * std::pow (2.0, k / 3.0);
                }
            }
            row.bandMean = used > 0 ? row.bandMean / used : 0.0;
            row.loudMean = usedLoud > 0 ? row.loudMean / usedLoud : 0.0;
            if (bands)
            {
                // --bands: the level of every third octave band of the reference and of the profile, dB (for looking at one preset with --filter)
                std::printf ("%s, %s (first) against %s (second), band centre Hz: dB dB\n", row.preset.c_str(), reference.c_str(), configs[c].c_str());
                for (int k = 0; k < kBands; ++k)
                    std::printf ("  %6.0f: %7.1f %7.1f\n", 40.0 * std::pow (2.0, k / 3.0), db (sa.band[k]), db (sb.band[k]));
            }
            if (csv != nullptr)
                std::fprintf (csv, "\"%s\",\"%s\",\"%s\",\"%s\",%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.3f\n", reference.c_str(), configs[c].c_str(), row.preset.c_str(), row.category.c_str(), row.identical ? 1 : 0,
                              row.level, row.peak, row.loudMean, row.bandMean, row.bandMax, row.bandMaxHz, row.above);
            rows.push_back (row);
        }
        std::vector<double> means, levels;
        for (const Row& r : rows)
        {
            means.push_back (r.loudMean);
            levels.push_back (std::fabs (r.level));
        }
        std::sort (means.begin(), means.end());
        std::sort (levels.begin(), levels.end());
        std::printf ("\n%s against %s: %d of %zu presets render the same samples; over all presets the mean difference of the loud bands has the median %.3f dB and the maximum %.2f dB, the level difference the median %.3f dB and the maximum %.2f dB\n",
                     configs[c].c_str(), reference.c_str(), identical, rows.size(), means[means.size() / 2], means.back(), levels[levels.size() / 2], levels.back());
        std::sort (rows.begin(), rows.end(), [] (const Row& x, const Row& y) { return x.loudMean != y.loudMean ? x.loudMean > y.loudMean : x.bandMean > y.bandMean; });
        std::printf ("  %-28s %-6s %8s %8s %10s %10s %18s %10s\n", "preset", "cat.", "level dB", "peak dB", "loud bands", "all bands", "bands max (at Hz)", "> 10 kHz");
        for (int i = 0; i < top && i < static_cast<int> (rows.size()); ++i)
        {
            const Row& r = rows[static_cast<size_t> (i)];
            if (r.identical)
                break;
            std::printf ("  %-28.28s %-6.6s %+8.2f %+8.2f %10.2f %10.2f %9.2f (%6.0f) %+10.2f\n", r.preset.c_str(), r.category.c_str(), r.level, r.peak, r.loudMean, r.bandMean, r.bandMax, r.bandMaxHz,
                         r.above);
        }
        std::fflush (stdout);
    }
    if (csv != nullptr)
        std::fclose (csv);
    return 0;
}
