// SynthyBoy - preset-render: renders the factory presets through an engine at every quality tier and checks them.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_preset_render --presets DIR [--engine vital|own] [--tier low|medium|high|all] [--summary-only] [--velocity]
//                           [--wav OUTDIR] [--filter SUBSTRING] [--sr 48000]
//
// Per preset and tier (a fresh engine each, the state loaded the way the shell loads it: every parameter, then the content):
//   single   one note (C1 for the category Bass, else C3), velocity 0.8, held 1.5 s, then released until the output is silent
//   chord    four notes at velocity 1.0 held 1 s (the loudest thing a player does); its RMS is the level measure of the factory presets
//   --velocity (the report): single notes at velocity 0.25 / 0.5 / 0.75 / 1.0 held 0.5 s
// Checks (exit code 1 if one fails): every sample finite; the peak of single and chord not above +3 dBFS; the single note not silent
// (peak at least -60 dBFS); the tail ends: the output stays below -90 dBFS for 250 ms within 12 s of the note off.
// Prints per render the peak, the RMS of the loudest 300 ms, the tail time, then per tier the spread of the levels (the
// owner listens to the outliers) and with --velocity the velocity curve (dB per velocity step, median over the presets).
// No timing is measured. Deterministic: a render offline (Quality::realtime = false).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/engine_caps.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/quality.h"
#include "synthyboy/state.h"
#include "wav_reader.h"

using namespace synthyboy;

namespace
{
constexpr int kBlock = 256;
constexpr double kHoldSingle = 1.5;
constexpr double kHoldChord = 1.0;
constexpr double kHoldVelocity = 0.5;
constexpr double kTailCap = 12.0;
constexpr double kSilentDb = -90.0;
constexpr double kClipLimitDb = 3.0;
constexpr double kNotSilentDb = -60.0;

struct Options
{
    std::string presets;
    EngineKind engine = EngineKind::vital;
    std::string tier = "all";
    bool summaryOnly = false;
    bool velocity = false;
    std::string wavDir;
    std::string filter;
    double sampleRate = 48000.0;
};

struct Loaded
{
    std::string file;
    PresetMeta meta;
    ParamValues values;
    StateContent content;
};

double toDb (double linear) { return linear > 1.0e-9 ? 20.0 * std::log10 (linear) : -180.0; }

struct Render
{
    bool finite = true;
    double peakDb = -180.0;      // whole render
    double holdPeakDb = -180.0;  // while the keys are down
    double rmsDb = -180.0;       // RMS of the loudest 300 ms window while the key is down (single only)
    double dc = 0.0;
    double tailSeconds = 0.0;    // after the note off until silent; kTailCap + 1 when it never ends
    bool tailEnds = true;
    std::vector<float> left, right;
};

std::unique_ptr<IEngine> makeEngine (const Options& o, const Quality& q, const Loaded& p)
{
    auto engine = createEngine (o.engine);
    if (engine == nullptr)
        return engine;
    engine->prepare (o.sampleRate, kBlock, q);
    for (size_t i = 0; i < paramCount(); ++i)
        engine->setParam (paramTable()[i].id, p.values.plain[i]);
    engine->setContent (p.content);
    return engine;
}

/** Plays `keys` at `velocity` for holdSeconds, then lets them ring out (up to the cap when tail is true). */
Render render (const Options& o, const Quality& q, const Loaded& p, const std::vector<int>& keys, float velocity, double holdSeconds, bool tail,
               bool keepAudio)
{
    Render r;
    auto engine = makeEngine (o, q, p);
    if (engine == nullptr)
    {
        r.finite = false;
        return r;
    }
    const int holdBlocks = static_cast<int> (std::ceil (holdSeconds * o.sampleRate / kBlock));
    const int capBlocks = holdBlocks + (tail ? static_cast<int> (std::ceil (kTailCap * o.sampleRate / kBlock)) : 0);
    const int silentBlocksNeeded = static_cast<int> (std::ceil (0.25 * o.sampleRate / kBlock));
    const double silentLinear = std::pow (10.0, kSilentDb / 20.0);

    std::vector<float> l (kBlock), rr (kBlock);
    double peak = 0.0, holdPeak = 0.0, sum = 0.0;
    long dcCount = 0;
    int silentRun = 0, silentSince = -1;
    std::vector<double> blockEnergy; // per block of the hold: sum of the squares (mean of both channels) and the number of samples
    long frame = 0;

    for (int b = 0; b < capBlocks; ++b)
    {
        std::vector<Event> ev;
        if (b == 0)
        {
            int id = 1;
            for (int k : keys)
            {
                Event e;
                e.frame = 0;
                e.type = Event::NoteOn;
                e.channel = 0;
                e.key = static_cast<int16_t> (k);
                e.value = velocity;
                e.noteId = id++;
                ev.push_back (e);
            }
        }
        else if (b == holdBlocks)
        {
            for (int k : keys)
            {
                Event e;
                e.frame = 0;
                e.type = Event::NoteOff;
                e.channel = 0;
                e.key = static_cast<int16_t> (k);
                e.value = 0.0f;
                e.noteId = -1;
                ev.push_back (e);
            }
        }
        engine->process (ev.empty() ? nullptr : ev.data(), static_cast<int> (ev.size()), l.data(), rr.data(), kBlock);

        double blockPeak = 0.0, blockSq = 0.0;
        for (int i = 0; i < kBlock; ++i, ++frame)
        {
            const float a = l[static_cast<size_t> (i)], c = rr[static_cast<size_t> (i)];
            if (! std::isfinite (a) || ! std::isfinite (c))
            {
                r.finite = false;
                l[static_cast<size_t> (i)] = rr[static_cast<size_t> (i)] = 0.0f;
                continue;
            }
            blockPeak = std::max ({ blockPeak, static_cast<double> (std::fabs (a)), static_cast<double> (std::fabs (c)) });
            blockSq += 0.5 * (static_cast<double> (a) * a + static_cast<double> (c) * c);
            if (b < holdBlocks)
            {
                sum += 0.5 * (static_cast<double> (a) + c);
                ++dcCount;
            }
        }
        if (b < holdBlocks)
            blockEnergy.push_back (blockSq);
        peak = std::max (peak, blockPeak);
        if (b < holdBlocks)
            holdPeak = std::max (holdPeak, blockPeak);
        if (keepAudio)
        {
            r.left.insert (r.left.end(), l.begin(), l.end());
            r.right.insert (r.right.end(), rr.begin(), rr.end());
        }

        if (tail && b >= holdBlocks)
        {
            if (blockPeak < silentLinear)
            {
                if (silentRun == 0)
                    silentSince = b;
                if (++silentRun >= silentBlocksNeeded)
                {
                    r.tailSeconds = (silentSince - holdBlocks) * static_cast<double> (kBlock) / o.sampleRate;
                    break;
                }
            }
            else
                silentRun = 0;
        }
        if (b == capBlocks - 1 && tail)
        {
            r.tailEnds = false;
            r.tailSeconds = kTailCap + 1.0;
        }
    }
    r.peakDb = toDb (peak);
    r.holdPeakDb = toDb (holdPeak);
    if (dcCount > 0)
    {
        const size_t window = std::min (blockEnergy.size(), static_cast<size_t> (std::ceil (0.3 * o.sampleRate / kBlock)));
        double loudest = 0.0, acc = 0.0;
        for (size_t i = 0; i < blockEnergy.size(); ++i)
        {
            acc += blockEnergy[i];
            if (i >= window)
                acc -= blockEnergy[i - window];
            if (i + 1 >= window)
                loudest = std::max (loudest, acc);
        }
        r.rmsDb = toDb (std::sqrt (loudest / static_cast<double> (window * kBlock)));
        r.dc = sum / static_cast<double> (dcCount);
    }
    return r;
}

std::string readFile (const std::filesystem::path& p)
{
    std::ifstream f (p, std::ios::binary);
    return std::string ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

double median (std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort (v.begin(), v.end());
    return v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
}

double stddev (const std::vector<double>& v)
{
    if (v.size() < 2)
        return 0.0;
    double m = 0.0;
    for (double x : v)
        m += x;
    m /= static_cast<double> (v.size());
    double s = 0.0;
    for (double x : v)
        s += (x - m) * (x - m);
    return std::sqrt (s / static_cast<double> (v.size()));
}
} // namespace

int main (int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--presets") o.presets = next();
        else if (a == "--engine") o.engine = next() == "own" ? EngineKind::own : EngineKind::vital;
        else if (a == "--tier") o.tier = next();
        else if (a == "--summary-only") o.summaryOnly = true;
        else if (a == "--velocity") o.velocity = true;
        else if (a == "--wav") o.wavDir = next();
        else if (a == "--filter") o.filter = next();
        else if (a == "--sr") o.sampleRate = std::atof (next().c_str());
        else
        {
            std::fprintf (stderr, "preset-render: unknown argument %s\n", a.c_str());
            return 2;
        }
    }
    if (o.presets.empty() || ! isEngineAvailable (o.engine))
    {
        std::fprintf (stderr, "usage: synthyboy_preset_render --presets DIR [--engine vital|own] [--tier low|medium|high|all] [--summary-only] [--velocity] "
                              "[--wav OUTDIR] [--filter TEXT] [--sr 48000]  (the engine must be built in)\n");
        return 2;
    }

    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::recursive_directory_iterator (o.presets, ec))
        if (e.is_regular_file() && e.path().extension() == ".sbpreset" && e.path().string().find (o.filter) != std::string::npos)
            files.push_back (e.path());
    std::sort (files.begin(), files.end());

    std::vector<Loaded> presets;
    int unreadable = 0;
    for (const auto& f : files)
    {
        const std::string text = readFile (f);
        Loaded p;
        p.file = f.filename().string();
        p.values = ParamValues::defaults();
        if (loadPreset (text.data(), text.size(), p.meta, p.values, p.content).status != LoadStatus::ok)
        {
            std::printf ("FAIL %s: does not load\n", p.file.c_str());
            ++unreadable;
            continue;
        }
        presets.push_back (std::move (p));
    }

    struct TierRun { const char* name; Tier tier; };
    std::vector<TierRun> tiers;
    for (const TierRun t : { TierRun { "low", Tier::low }, TierRun { "medium", Tier::medium }, TierRun { "high", Tier::high } })
        if (o.tier == "all" || o.tier == t.name)
            tiers.push_back (t);

    int failures = unreadable;

    // a preset may only use what the engine implements: every key that differs from its default is supported (capability bits of the
    // key and of the value, e.g. a Scream filter model, a Custom LFO shape)
    {
        const auto probe = createEngine (o.engine);
        const EngineCapsMask caps = probe->capabilities();
        const ParamValues defaults = ParamValues::defaults();
        for (const auto& p : presets)
            for (size_t i = 0; i < paramCount(); ++i)
            {
                if (p.values.plain[i] == defaults.plain[i])
                    continue;
                if (! probe->supportsParam (paramTable()[i].id) || ! capsCover (caps, requiredCapsForValue (i, p.values.plain[i])))
                {
                    std::printf ("FAIL %s: %s = %g needs engine capabilities the engine does not have\n", p.file.c_str(), paramTable()[i].key, static_cast<double> (p.values.plain[i]));
                    ++failures;
                }
            }
    }
    std::printf ("preset-render: %zu presets, %zu tiers, engine %s, %.0f Hz\n", presets.size(), tiers.size(),
                 o.engine == EngineKind::vital ? "vital" : "own", o.sampleRate);

    for (const auto& t : tiers)
    {
        const Quality q = qualityForTier (t.tier);
        std::vector<double> rmsLevels, chordLevels, peaks, tails;
        std::vector<std::string> names;
        std::vector<std::vector<double>> velPeaks (4); // per velocity step, the peak in dB for every preset
        std::vector<double> velSlope;

        if (! o.summaryOnly)
            std::printf ("\n== tier %s ==\n%-34s %-8s %8s %8s %8s %9s %8s %7s\n", t.name, "preset", "category", "peak", "chordPk", "rms", "chordRms", "dc", "tail s");

        for (const auto& p : presets)
        {
            const std::vector<int> keys { p.meta.category == "Bass" ? 24 + 12 : 60 };
            const std::vector<int> chord { keys[0], keys[0] + 4, keys[0] + 7, keys[0] + 12 };
            const bool wav = ! o.wavDir.empty();
            const Render s = render (o, q, p, keys, 0.8f, kHoldSingle, true, wav);
            const Render c = render (o, q, p, chord, 1.0f, kHoldChord, false, false);

            std::vector<std::string> problems;
            if (! s.finite || ! c.finite)
                problems.push_back ("NaN / inf in the output");
            if (std::max (s.peakDb, c.peakDb) > kClipLimitDb)
                problems.push_back ("peak above +3 dBFS");
            if (s.holdPeakDb < kNotSilentDb)
                problems.push_back ("silent");
            if (! s.tailEnds)
                problems.push_back ("the tail does not end within 12 s");

            if (o.velocity)
            {
                const float vs[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
                for (int k = 0; k < 4; ++k)
                    velPeaks[static_cast<size_t> (k)].push_back (render (o, q, p, keys, vs[k], kHoldVelocity, false, false).holdPeakDb);
                velSlope.push_back (velPeaks[3].back() - velPeaks[0].back());
            }

            if (wav && s.finite && ! s.left.empty())
            {
                std::filesystem::create_directories (std::filesystem::path (o.wavDir) / t.name, ec);
                std::vector<float> inter (s.left.size() * 2);
                for (size_t i = 0; i < s.left.size(); ++i)
                {
                    inter[2 * i] = s.left[i];
                    inter[2 * i + 1] = s.right[i];
                }
                std::vector<uint8_t> bytes;
                if (fx::writeWav (bytes, inter.data(), static_cast<int> (s.left.size()), 2, static_cast<int> (o.sampleRate), 24, false))
                {
                    std::ofstream out (std::filesystem::path (o.wavDir) / t.name / (std::filesystem::path (p.file).stem().string() + ".wav"), std::ios::binary);
                    out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
                }
            }

            if (s.finite)
            {
                rmsLevels.push_back (s.rmsDb);
                chordLevels.push_back (c.rmsDb);
                peaks.push_back (std::max (s.peakDb, c.peakDb));
                tails.push_back (s.tailSeconds);
                names.push_back (p.file);
            }
            if (! o.summaryOnly)
                std::printf ("%-34s %-8s %8.1f %8.1f %8.1f %9.1f %8.4f %7.2f%s\n", p.file.c_str(), p.meta.category.c_str(), s.peakDb, c.peakDb, s.rmsDb, c.rmsDb, s.dc,
                             s.tailSeconds, problems.empty() ? "" : "  <== PROBLEM");
            for (const auto& m : problems)
            {
                std::printf ("FAIL [%s] %s: %s (peak %.1f dBFS, chord peak %.1f dBFS, tail %.2f s)\n", t.name, p.file.c_str(), m.c_str(), s.peakDb, c.peakDb,
                             s.tailSeconds);
                ++failures;
            }
        }

        if (! rmsLevels.empty())
        {
            const double med = median (rmsLevels);
            std::printf ("\n[%s] level (RMS of the loudest 300 ms, velocity 0.8): min %.1f  median %.1f  max %.1f dBFS, spread (sd) %.1f dB; peak max %.1f dBFS; "
                        "tail median %.2f s, max %.2f s\n",
                        t.name, *std::min_element (rmsLevels.begin(), rmsLevels.end()), med, *std::max_element (rmsLevels.begin(), rmsLevels.end()),
                        stddev (rmsLevels), *std::max_element (peaks.begin(), peaks.end()), median (tails), *std::max_element (tails.begin(), tails.end()));
            for (size_t i = 0; i < rmsLevels.size(); ++i)
                if (std::fabs (rmsLevels[i] - med) > 8.0)
                    std::printf ("[%s]   level outlier (%+.1f dB from the median): %s (%.1f dBFS)\n", t.name, rmsLevels[i] - med, names[i].c_str(), rmsLevels[i]);
        }
        if (! chordLevels.empty())
            std::printf ("[%s] chord level (RMS of the loudest 300 ms, four notes, velocity 1.0): min %.1f  median %.1f  max %.1f dBFS, spread (sd) %.1f dB\n",
                         t.name, *std::min_element (chordLevels.begin(), chordLevels.end()), median (chordLevels),
                         *std::max_element (chordLevels.begin(), chordLevels.end()), stddev (chordLevels));
        if (o.velocity && ! velSlope.empty())
        {
            std::printf ("[%s] velocity curve, peak in dBFS, median over the presets: v0.25 %.1f  v0.5 %.1f  v0.75 %.1f  v1.0 %.1f;  v0.25 -> v1.0: %.1f dB "
                         "(min %.1f, max %.1f)\n",
                         t.name, median (velPeaks[0]), median (velPeaks[1]), median (velPeaks[2]), median (velPeaks[3]), median (velSlope),
                         *std::min_element (velSlope.begin(), velSlope.end()), *std::max_element (velSlope.begin(), velSlope.end()));
            for (size_t i = 0; i < velSlope.size() && i < names.size(); ++i)
                if (velSlope[i] < 3.0)
                    std::printf ("[%s]   flat velocity response (%.1f dB over the range): %s\n", t.name, velSlope[i], names[i].c_str());
        }
    }

    std::printf ("\npreset-render: %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
