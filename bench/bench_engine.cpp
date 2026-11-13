// SynthyBoy - benchmark: renders held chords through an engine and prints the real-time factor and block times.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_bench [--engine own|vital] [--patch default|heavy|both] [--seconds N] [--block 128] [--voices K] [--preset FILE] [--preset-dir DIR] [--sr 48000] [--retrigger S]
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
#include <fstream>
#include <sstream>

#if defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
 #define SB_BENCH_PROFILE 1
 #include <csignal>
 #include <sys/time.h>
 #include <ucontext.h>
#endif

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

#include "own_heavy_patch.h"

using namespace synthyboy;

namespace
{
#ifdef SB_BENCH_PROFILE
/** --profile FILE: a SIGPROF sampler (about 1000 Hz of CPU time) stores the interrupted program counter in a preallocated array; the file holds the load bases of
    the executable and the raw addresses (scripts/symbolize-profile.py turns it into function names with the unstripped binary). */
struct Profiler
{
    static constexpr size_t kMax = 1 << 20;
    static inline unsigned long* pcs = nullptr;
    static inline volatile size_t count = 0;
    static inline volatile size_t dropped = 0;
    static inline volatile sig_atomic_t active = 0; // samples are kept only while process () runs in the measured part

    static void handler (int, siginfo_t*, void* ctx)
    {
        const ucontext_t* uc = static_cast<const ucontext_t*> (ctx);
 #if defined(__arm__)
        const unsigned long pc = uc->uc_mcontext.arm_pc;
 #else
        const unsigned long pc = uc->uc_mcontext.pc;
 #endif
        if (!active)
            return;
        if (count < kMax)
            pcs[count++] = pc;
        else
            dropped = dropped + 1;
    }
    static void start()
    {
        pcs = static_cast<unsigned long*> (std::calloc (kMax, sizeof (unsigned long)));
        struct sigaction sa;
        std::memset (&sa, 0, sizeof (sa));
        sa.sa_sigaction = &Profiler::handler;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigaction (SIGPROF, &sa, nullptr);
        itimerval it;
        it.it_interval.tv_sec = 0;
        it.it_interval.tv_usec = 1000;
        it.it_value = it.it_interval;
        setitimer (ITIMER_PROF, &it, nullptr);
    }
    static void stop (const char* file)
    {
        itimerval it;
        std::memset (&it, 0, sizeof (it));
        setitimer (ITIMER_PROF, &it, nullptr);
        FILE* f = std::fopen (file, "w");
        if (f == nullptr)
            return;
        // the mappings of the executable (and the libraries): "start end offset path"; the symbolizer picks the one that contains an address
        std::fprintf (f, "# synthyboy profile v1, %zu samples, %zu dropped\n", static_cast<size_t> (count), static_cast<size_t> (dropped));
        if (FILE* m = std::fopen ("/proc/self/maps", "r"))
        {
            char line[512];
            while (std::fgets (line, sizeof (line), m) != nullptr)
            {
                unsigned long a = 0, b = 0, off = 0;
                char perms[8] = {}, path[400] = {};
                if (std::sscanf (line, "%lx-%lx %7s %lx %*s %*s %399s", &a, &b, perms, &off, path) >= 4 && perms[2] == 'x')
                    std::fprintf (f, "map %lx %lx %lx %s\n", a, b, off, path);
            }
            std::fclose (m);
        }
        for (size_t i = 0; i < count; ++i)
            std::fprintf (f, "%lx\n", pcs[i]);
        std::fclose (f);
    }
};
#endif

/** Flush-to-zero like the MPC plugin (shell-mpc/synthyboy_mpc.cpp): ARM FPSCR bit 24. */
void enableFtz()
{
#if defined(__arm__)
    uint32_t fpscr;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= 0x01000000u;
    __asm__ __volatile__("vmsr fpscr, %0" ::"r"(fpscr));
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ull << 24);
    __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr));
#else
    std::fprintf (stderr, "bench: --ftz is only implemented for ARM\n");
#endif
}

struct Options
{
    EngineKind engine = EngineKind::own;
    double seconds = 10.0;
    bool secondsGiven = false;
    int block = 128;
    int voices = 8;
    bool voicesGiven = false; // --voices K: K notes are played (tier runs: at most the voices of the tier)
    const char* presetFile = nullptr; // --preset FILE: a .sbpreset instead of the default / heavy patch
    const char* presetDir = nullptr;  // --preset-dir DIR: every .sbpreset below DIR, one line each
    double sampleRate = 48000.0;
    double retrigger = 1.0;
    bool defaultPatch = true, heavyPatch = true;
    const char* profileFile = nullptr;
    std::string tier = "all"; // requested | low | medium | high | all
    bool ftz = false;
    int morphSpread = -1; // --morph-spread 0|1: keep the morph spread across the unison voices at tier low (-1 = the engine default)
    int unisonCap = -1; // --unison-cap N: tier low unison voices per oscillator of the own engine (0 = none, -1 = the engine default)
    bool sections = false; // --sections: the time of a block by section (a build with SYNTHYBOY_SECTION_TIMING=ON)
    bool idle = false; // --idle: no notes at all (the cost of an empty block)
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

/** A preset as the shell loads it: the parameter values go to the engine one by one (setParam) and the content section by setContent (shell-mpc, effSetChunk). */
struct PresetData
{
    std::string name, path;
    ParamValues values = ParamValues::defaults();
    StateContent content;
};

bool loadPresetFile (const std::string& path, PresetData& out)
{
    std::ifstream in (path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    PresetMeta meta;
    const LoadResult r = loadPreset (text.data(), text.size(), meta, out.values, out.content);
    if (r.status != LoadStatus::ok && r.status != LoadStatus::newer_version)
        return false;
    out.path = path;
    out.name = meta.name.empty() ? path : meta.name;
    return true;
}

Result run (const Options& o, int voicesIn, const Quality& quality, bool heavy, const PresetData* preset = nullptr)
{
    const int voices = o.idle ? 0 : voicesIn;
    auto engine = createEngine (o.engine);
    if (preset != nullptr)
    {
        const auto* table = paramTable();
        for (size_t i = 0; i < paramCount(); ++i)
            engine->setParam (table[i].id, preset->values.plain[i]);
        engine->setContent (preset->content);
    }
    else if (heavy && o.engine == EngineKind::own)
        bench::ownHeavyPatch (*engine);
    else if (heavy)
        heavyPatch (*engine);
    else
        set (*engine, "master.polyphony", 32.0f);
    if (o.unisonCap >= 0)
        setOwnLowTierUnisonCap (*engine, o.unisonCap);
    if (o.morphSpread >= 0)
        setOwnLowTierMorphSpread (*engine, o.morphSpread != 0);
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

        if (b == warmupBlocks && o.sections)
            ownSectionReset();
#ifdef SB_BENCH_PROFILE
        Profiler::active = (o.profileFile != nullptr && b >= warmupBlocks) ? 1 : 0;
#endif
        const auto t0 = std::chrono::steady_clock::now();
        engine->process (events.data(), static_cast<int> (events.size()), left.data(), right.data(), o.block);
        const auto t1 = std::chrono::steady_clock::now();
#ifdef SB_BENCH_PROFILE
        Profiler::active = 0;
#endif
        sink += left[static_cast<size_t> (o.block / 2)];
        if (b >= warmupBlocks)
            blockSeconds.push_back (std::chrono::duration<double> (t1 - t0).count());
    }
    if (sink == 12345.678f)
        std::puts ("");

    if (o.sections)
    {
        const double blocks = static_cast<double> (measuredBlocks);
        double total = 0.0;
        unsigned long long totalCalls = 0;
        double ms[64] = {};
        unsigned long long calls[64] = {};
        const int count = std::min (ownSectionCount(), 64);
        for (int i = 0; i < count; ++i)
        {
            ownSectionRead (i, ms[i], calls[i]);
            total += ms[i];
            totalCalls += calls[i];
        }
        const double clockNs = ownSectionClockCostNs();
        std::printf ("    sections (exclusive time, %.0f blocks; one scope costs about %.0f ns, %.1f scopes per block = %.4f ms per block of the total)\n", blocks, clockNs,
                     static_cast<double> (totalCalls) / blocks, clockNs * 1.0e-6 * static_cast<double> (totalCalls) / blocks);
        std::printf ("    %-52s %12s %8s %12s\n", "section", "ms / block", "share", "calls / blk");
        for (int i = 0; i < count; ++i)
            if (calls[i] > 0)
                std::printf ("    %-52s %12.4f %7.1f%% %12.1f\n", ownSectionName (i), ms[i] / blocks, total > 0.0 ? 100.0 * ms[i] / total : 0.0, static_cast<double> (calls[i]) / blocks);
        std::printf ("    %-52s %12.4f %7.1f%%\n", "sum of the sections", total / blocks, 100.0);
    }

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
    std::fprintf (stderr, "%s\nusage: synthyboy_bench [--engine own|vital] [--patch default|heavy|both] [--seconds N] [--block 128] [--voices K] [--sr 48000] [--retrigger S] [--profile FILE] [--ftz] [--idle] [--sections] [--unison-cap N] [--morph-spread 0|1] [--tier requested|low|medium|high|all]\n", message);
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
        else if (a == "--seconds")   { o.seconds = std::atof (next()); o.secondsGiven = true; }
        else if (a == "--block")     o.block = std::atoi (next());
        else if (a == "--voices")    { o.voices = std::atoi (next()); o.voicesGiven = true; }
        else if (a == "--preset")    o.presetFile = next();
        else if (a == "--preset-dir") o.presetDir = next();
        else if (a == "--sr")        o.sampleRate = std::atof (next());
        else if (a == "--retrigger") o.retrigger = std::atof (next());
        else if (a == "--profile")   o.profileFile = next();
        else if (a == "--ftz")       o.ftz = true;
        else if (a == "--idle")      o.idle = true;
        else if (a == "--sections")  o.sections = true;
        else if (a == "--unison-cap") o.unisonCap = std::atoi (next());
        else if (a == "--morph-spread") o.morphSpread = std::atoi (next());
        else if (a == "--tier")      o.tier = next();
        else usage ("unknown argument");
    }
    if (o.seconds <= 0.0 || o.block < 1 || o.block > 8192 || o.voices < 1 || o.voices > kMaxVoices || o.sampleRate < 8000.0)
        usage ("argument out of range");

    if (o.ftz)
        enableFtz();
#ifdef SB_BENCH_PROFILE
    if (o.profileFile != nullptr)
        Profiler::start();
#else
    if (o.profileFile != nullptr)
        std::fprintf (stderr, "bench: --profile needs Linux ARM; ignored\n");
#endif
    if (o.sections && !ownSectionTimingAvailable())
    {
        std::fprintf (stderr, "bench: --sections needs a build with -DSYNTHYBOY_SECTION_TIMING=ON\n");
        return 2;
    }
    const char* error = nullptr;
    if (createEngine (o.engine, &error) == nullptr)
    {
        std::fprintf (stderr, "engine %s not available: %s\n", engineName (o.engine), error != nullptr ? error : "?");
        return 1;
    }

    std::printf ("synthyboy_bench: engine %s, %.0f Hz, block %d, %.1f s per run, retrigger every %.1f s\n", engineName (o.engine), o.sampleRate,
                 o.block, o.seconds, o.retrigger);
    if (o.presetFile != nullptr || o.presetDir != nullptr)
    {
        // presets: tier low (or --tier), the notes of --voices (default: the voices of the tier), --seconds (default 2 here)
        std::vector<std::string> files;
        if (o.presetFile != nullptr)
            files.push_back (o.presetFile);
        else
            for (const auto& e : scanPresetDirectory (o.presetDir))
                files.push_back (e.path);
        std::sort (files.begin(), files.end());
        if (!o.secondsGiven)
            o.seconds = 2.0;
        const Tier tier = o.tier == "medium" ? Tier::medium : (o.tier == "high" ? Tier::high : Tier::low);
        const Quality q = qualityForTier (tier);
        const int notes = o.voicesGiven ? std::min (o.voices, kMaxVoices) : q.maxVoices;
        const double budgetMs = 1000.0 * o.block / o.sampleRate;
        struct Row { std::string name; Result r; };
        std::vector<Row> rows;
        for (const auto& f : files)
        {
            PresetData pd;
            if (!loadPresetFile (f, pd))
            {
                std::printf ("%-44s could not be loaded\n", f.c_str());
                continue;
            }
            const Result r = run (o, notes, q, false, &pd);
            rows.push_back ({ pd.name, r });
            std::printf ("%-44s voices %2d: mean %.4f  p99 %.4f  worst %.4f ms%s\n", pd.name.c_str(), notes, r.meanMs, r.p99Ms, r.worstMs, r.worstMs > budgetMs ? "   OVER" : "");
        }
        std::printf ("\npresets over the block budget (%.3f ms) in the worst block:\n", budgetMs);
        int over = 0;
        for (const auto& r : rows)
            if (r.r.worstMs > budgetMs)
            {
                std::printf ("  %-42s worst %.4f ms (mean %.4f)\n", r.name.c_str(), r.r.worstMs, r.r.meanMs);
                ++over;
            }
        std::printf ("  %d of %zu\n\ntop 15 by mean:\n", over, rows.size());
        std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.r.meanMs > b.r.meanMs; });
        for (size_t i = 0; i < rows.size() && i < 15; ++i)
            std::printf ("  %-42s mean %.4f  p99 %.4f  worst %.4f ms\n", rows[i].name.c_str(), rows[i].r.meanMs, rows[i].r.p99Ms, rows[i].r.worstMs);
        return 0;
    }
    for (const bool heavy : { false, true })
    {
        if ((heavy && ! o.heavyPatch) || (! heavy && ! o.defaultPatch))
            continue;
        std::printf ("%s:\n", heavy ? (o.engine == EngineKind::own ? "heavy patch of the own engine (3 oscillators x 7 unison voices with morph and warp, sub, noise, 2 filters, 4 LFOs + 7 matrix routes, effects)"
                                                                   : "heavy patch (3 oscillators x 7 unison voices, 2 filters, 3 LFOs + matrix, all effects)")
                                     : "default patch (one saw oscillator, filter 12 kHz, no effects)");
        Quality requested;
        requested.maxVoices = o.voices;
        if (o.tier == "all" || o.tier == "requested")
            print ("requested", o, o.voices, run (o, o.voices, requested, heavy));
        for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
        {
            if (o.tier != "all" && o.tier != (tier == Tier::low ? "low" : (tier == Tier::medium ? "medium" : "high")))
                continue;
            const Quality q = qualityForTier (tier);
            {
                const int nv = o.voicesGiven ? std::min (o.voices, kMaxVoices) : q.maxVoices;
                print (tier == Tier::low ? "tier low" : (tier == Tier::medium ? "tier medium" : "tier high"), o, nv, run (o, nv, q, heavy));
            }
        }
    }
#ifdef SB_BENCH_PROFILE
    if (o.profileFile != nullptr)
        Profiler::stop (o.profileFile);
#endif
    return 0;
}
