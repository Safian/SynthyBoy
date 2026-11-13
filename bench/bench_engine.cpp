// SynthyBoy - benchmark: renders held chords through an engine and prints the real-time factor and block times.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_bench [--engine own|vital] [--patch default|heavy|both] [--seconds N] [--block 128] [--voices K] [--sr 48000] [--retrigger S]
//
// K notes are held and retriggered every S seconds (default 1). The first second is rendered but not measured. Prints,
// for the requested configuration and then once per quality tier (the tier's quality: voices, unison, oversampling, ...), the
// real-time factor (audio seconds / seconds spent in process()), the mean, the 99th percentile and the worst block time.
// Patches: "default" = the table's defaults (one saw oscillator, filter 1, no effects); "heavy" = three oscillators with 7
// unison voices, two filters in series, three LFOs, an envelope and a velocity routing in the modulation matrix, all nine effects.
// With --engine own the heavy patch is own_heavy_patch.h instead: the same idea with only parameters that engine-own implements (3 oscillators
// x 7 unison with a morph and a warp, sub, noise, both filters, 4 LFOs and 7 matrix routes, the effects its tier allows). No assertions: the numbers are
// for looking at, on the machine they were taken on (docs/PERFORMANCE.md).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"

#include "own_heavy_patch.h"

using namespace synthyboy;

namespace
{
struct Options
{
    EngineKind engine = EngineKind::own;
    double seconds = 10.0;
    int block = 128;
    int voices = 8;
    double sampleRate = 48000.0;
    double retrigger = 1.0;
    bool defaultPatch = true, heavyPatch = true;
};

struct Result
{
    double audioSeconds = 0.0;
    double processSeconds = 0.0;
    double meanMs = 0.0, p99Ms = 0.0, worstMs = 0.0;
};

void set (IEngine& engine, const char* key, float value)
{
    if (const auto* info = findParamByKey (key))
        engine.setParam (info->id, value);
    else
        std::fprintf (stderr, "bench: unknown parameter %s\n", key);
}

void slot (IEngine& engine, int index, const char* sourceKey, const char* destinationKey, float amountPercent)
{
    const auto s = modSlotParams (index);
    float source = 0.0f;
    for (size_t k = 0; k < modSourceCount(); ++k)
        if (std::strcmp (modSourceTable()[k].key, sourceKey) == 0)
            source = static_cast<float> (k + 1);
    const auto* destination = findParamByKey (destinationKey);
    engine.setParam (paramTable()[s.source].id, source);
    engine.setParam (paramTable()[s.destination].id, static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (destination->id)))));
    engine.setParam (paramTable()[s.amount].id, amountPercent);
}

/** The heavy patch: 3 oscillators x 7 unison voices, 2 filters in series, 3 LFOs and an envelope in the matrix, all effects. */
void heavyPatch (IEngine& engine)
{
    set (engine, "master.polyphony", 32.0f);
    const float tables[] = { 1.0f, 2.0f, 5.0f }; // saw, square, saw to square
    for (int i = 1; i <= 3; ++i)
    {
        const std::string k = "osc" + std::to_string (i) + ".";
        set (engine, (k + "on").c_str(), 1.0f);
        set (engine, (k + "wavetable").c_str(), tables[i - 1]);
        set (engine, (k + "level").c_str(), 35.0f);
        set (engine, (k + "unison.voices").c_str(), 7.0f);
        set (engine, (k + "unison.detune").c_str(), 30.0f);
        set (engine, (k + "transpose").c_str(), static_cast<float> ((i - 2) * 12));
        set (engine, (k + "random_phase").c_str(), 100.0f);
    }
    set (engine, "osc1.morph.type", 1.0f);
    set (engine, "filter1.model", 2.0f);
    set (engine, "filter1.cutoff", 3000.0f);
    set (engine, "filter1.resonance", 40.0f);
    set (engine, "filter2.on", 1.0f);
    set (engine, "filter2.model", 0.0f);
    set (engine, "filter2.cutoff", 6000.0f);
    set (engine, "filter.routing", 1.0f);
    set (engine, "lfo1.sync", 0.0f);
    set (engine, "lfo2.sync", 0.0f);
    set (engine, "lfo3.sync", 0.0f);
    set (engine, "lfo1.rate", 3.0f);
    set (engine, "lfo2.rate", 0.7f);
    set (engine, "lfo3.rate", 5.0f);
    set (engine, "env2.attack", 300.0f);
    slot (engine, 0, "lfo1", "filter1.cutoff", 30.0f);
    slot (engine, 1, "lfo2", "osc1.frame", 50.0f);
    slot (engine, 2, "lfo3", "osc2.pan", 40.0f);
    slot (engine, 3, "env2", "filter2.cutoff", 40.0f);
    slot (engine, 4, "velocity", "osc3.level", 30.0f);
    for (const char* fx : { "chorus", "compressor", "delay", "distortion", "eq", "fxfilter", "flanger", "phaser", "reverb" })
        set (engine, (std::string (fx) + ".on").c_str(), 1.0f);
    set (engine, "distortion.drive", 6.0f);
    set (engine, "eq.mid.gain", 4.0f);
}

Result run (const Options& o, int voices, const Quality& quality, bool heavy)
{
    auto engine = createEngine (o.engine);
    if (heavy && o.engine == EngineKind::own)
        bench::ownHeavyPatch (*engine);
    else if (heavy)
        heavyPatch (*engine);
    else
        set (*engine, "master.polyphony", 32.0f);
    engine->prepare (o.sampleRate, o.block, quality);

    std::vector<float> left (static_cast<size_t> (o.block)), right (static_cast<size_t> (o.block));
    std::vector<Event> events;
    events.reserve (static_cast<size_t> (2 * voices + 2));

    const long long warmupBlocks = static_cast<long long> (o.sampleRate / o.block);
    const long long measuredBlocks = std::max<long long> (1, static_cast<long long> (o.seconds * o.sampleRate / o.block));
    const long long retriggerBlocks = std::max<long long> (1, static_cast<long long> (o.retrigger * o.sampleRate / o.block));

    std::vector<double> blockSeconds;
    blockSeconds.reserve (static_cast<size_t> (measuredBlocks));

    float sink = 0.0f; // keeps the output alive
    for (long long b = 0; b < warmupBlocks + measuredBlocks; ++b)
    {
        events.clear();
        if (b % retriggerBlocks == 0)
        {
            for (int v = 0; v < voices; ++v)
            {
                Event off;
                off.type = Event::NoteOff;
                off.key = static_cast<int16_t> (36 + (v * 7) % 60);
                events.push_back (off);
            }
            for (int v = 0; v < voices; ++v)
            {
                Event on;
                on.type = Event::NoteOn;
                on.key = static_cast<int16_t> (36 + (v * 7) % 60);
                on.value = 0.6f + 0.4f * static_cast<float> (v % 3) / 2.0f;
                on.frame = std::min (v, o.block - 1);
                events.push_back (on);
            }
            std::stable_sort (events.begin(), events.end(), [] (const Event& x, const Event& y) { return x.frame < y.frame; });
        }

        const auto t0 = std::chrono::steady_clock::now();
        engine->process (events.data(), static_cast<int> (events.size()), left.data(), right.data(), o.block);
        const auto t1 = std::chrono::steady_clock::now();
        sink += left[static_cast<size_t> (o.block / 2)];
        if (b >= warmupBlocks)
            blockSeconds.push_back (std::chrono::duration<double> (t1 - t0).count());
    }
    if (sink == 12345.678f)
        std::puts ("");

    Result r;
    double sum = 0.0;
    for (const double s : blockSeconds)
        sum += s;
    std::sort (blockSeconds.begin(), blockSeconds.end());
    r.processSeconds = sum;
    r.audioSeconds = static_cast<double> (measuredBlocks) * o.block / o.sampleRate;
    r.meanMs = 1000.0 * sum / static_cast<double> (blockSeconds.size());
    r.p99Ms = 1000.0 * blockSeconds[static_cast<size_t> (0.99 * static_cast<double> (blockSeconds.size() - 1))];
    r.worstMs = 1000.0 * blockSeconds.back();
    return r;
}

void print (const char* label, const Options& o, int voices, const Result& r)
{
    const double budgetMs = 1000.0 * o.block / o.sampleRate;
    std::printf ("  %-12s voices %2d: real-time factor %8.1fx   block %d frames (budget %.3f ms): mean %.4f  p99 %.4f  worst %.4f ms   (%.1f s audio in %.3f s)\n",
                 label, voices, r.audioSeconds / r.processSeconds, o.block, budgetMs, r.meanMs, r.p99Ms, r.worstMs, r.audioSeconds, r.processSeconds);
}

[[noreturn]] void usage (const char* message)
{
    std::fprintf (stderr, "%s\nusage: synthyboy_bench [--engine own|vital] [--patch default|heavy|both] [--seconds N] [--block 128] [--voices K] [--sr 48000] [--retrigger S]\n", message);
    std::exit (2);
}
} // namespace

int main (int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> const char* { if (i + 1 >= argc) usage ("missing value"); return argv[++i]; };
        if (a == "--engine")
        {
            const std::string v = next();
            if (v == "own") o.engine = EngineKind::own;
            else if (v == "vital") o.engine = EngineKind::vital;
            else usage ("unknown engine");
        }
        else if (a == "--patch")
        {
            const std::string v = next();
            if (v == "default") o.heavyPatch = false;
            else if (v == "heavy") o.defaultPatch = false;
            else if (v != "both") usage ("unknown patch");
        }
        else if (a == "--seconds")   o.seconds = std::atof (next());
        else if (a == "--block")     o.block = std::atoi (next());
        else if (a == "--voices")    o.voices = std::atoi (next());
        else if (a == "--sr")        o.sampleRate = std::atof (next());
        else if (a == "--retrigger") o.retrigger = std::atof (next());
        else usage ("unknown argument");
    }
    if (o.seconds <= 0.0 || o.block < 1 || o.block > 8192 || o.voices < 1 || o.voices > kMaxVoices || o.sampleRate < 8000.0)
        usage ("argument out of range");

    const char* error = nullptr;
    if (createEngine (o.engine, &error) == nullptr)
    {
        std::fprintf (stderr, "engine %s not available: %s\n", engineName (o.engine), error != nullptr ? error : "?");
        return 1;
    }

    std::printf ("synthyboy_bench: engine %s, %.0f Hz, block %d, %.1f s per run, retrigger every %.1f s\n", engineName (o.engine), o.sampleRate,
                 o.block, o.seconds, o.retrigger);
    for (const bool heavy : { false, true })
    {
        if ((heavy && ! o.heavyPatch) || (! heavy && ! o.defaultPatch))
            continue;
        std::printf ("%s:\n", heavy ? (o.engine == EngineKind::own ? "heavy patch of the own engine (3 oscillators x 7 unison voices with morph and warp, sub, noise, 2 filters, 4 LFOs + 7 matrix routes, effects)"
                                                                   : "heavy patch (3 oscillators x 7 unison voices, 2 filters, 3 LFOs + matrix, all effects)")
                                     : "default patch (one saw oscillator, filter 12 kHz, no effects)");
        Quality requested;
        requested.maxVoices = o.voices;
        print ("requested", o, o.voices, run (o, o.voices, requested, heavy));
        for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
        {
            const Quality q = qualityForTier (tier);
            print (tier == Tier::low ? "tier low" : (tier == Tier::medium ? "tier medium" : "tier high"), o, q.maxVoices, run (o, q.maxVoices, q, heavy));
        }
    }
    return 0;
}
