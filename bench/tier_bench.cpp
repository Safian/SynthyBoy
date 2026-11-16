// SynthyBoy - benchmark: which quality profile a device can run. Plays loads through the own engine the way the MPC shell does (44.1 kHz, 128 frame blocks, the
// parameters by setParam and then the content, a real-time quality, flush-to-zero) under a list of quality profiles (shell-mpc/device_profile.h) and prints the time
// of a block as a share of the block's real time: minimum, median, 99th percentile, maximum.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_tier_bench --presets DIR [--core N] [--set settings|ladder|tiers|quick] [--config PROFILE]... [--load NAME]... [--all-presets]
//                        [--seconds 30] [--notes K] [--retrigger 1] [--paced] [--csv FILE] [--block 128] [--sr 44100] [--no-ftz] [--sections] [--list]
//
//   --core N        pin the process to CPU N (Linux: sched_setaffinity; elsewhere the option is refused). Nothing else of the machine is touched: no scheduling class,
//                   no priority, no frequency governor. The CPU the run really was on, its part number and its frequency settings are printed.
//   --set NAME      the list of profiles (default settings):
//                     settings  "low" (what the MPC build ships) and one change at a time: unison 4 / 7 / 16, warp 4x, morph full, math exact, control full, fx all,
//                               samples high; then everything at once with 8 voices; each with the notes of its polyphony
//                     ladder    the candidate profiles from the cheapest to the dearest (kLadder below), each at ITS full polyphony
//                     tiers     low, medium, high at their full polyphony (8 / 16 / 32 notes)
//                     quick     low, low,unison=7 and high,voices=8
//   --config P      one more profile (the grammar of device_profile.h, e.g. "low,unison=7,warp=4x"); with at least one --config and no --set only these run
//   --load NAME     a load (default: the synthetic worst case, the heaviest factory presets of kHeavy and the typical pad / bass / lead of kTypical):
//                     worst        3 oscillators x 16 unison voices, a spectral morph and an oversampled warp on each, sub, noise, both filters, 4 LFOs + 9 matrix routes,
//                                  the nine effects of state version 2 (reverb and flanger only run when the profile's fx mask lets them)
//                     heavy        the heavy patch of bench/own_heavy_patch.h (3 x 7 unison, one morph, one warp): the patch of the earlier device measurements
//                     basic        the default patch (one saw oscillator, one filter)
//                     any other    a factory preset by its name ("Hyper Saw Lead") or file path
//   --all-presets   every .sbpreset below DIR instead of the default loads (the table of cost per preset per profile)
//   --seconds S     measured audio per run (default 30; the first second of every run is played and not measured)
//   --notes K       hold K notes instead of the profile's polyphony (the notes over the polyphony of the profile or of the preset steal voices)
//   --paced         start every block at its real time (a run then takes its audio time): the caches and the frequency governor see what they see under the host. Without it
//                   the blocks follow each other at once (the usual, fast way).
//   --sections      the exclusive time per DSP section (a build with -DSYNTHYBOY_SECTION_TIMING=ON)
//   --list          print the profiles and loads that would run, and stop
//
// Output: one line per run on stdout, a summary per profile (its dearest factory preset by the 99th percentile), and with --csv the same numbers as a table.
// The numbers are for the machine and core they were taken on (docs/PERFORMANCE.md); no assertion is made.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
 #include <sched.h>
 #include <unistd.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
 #include <pmmintrin.h>
 #include <xmmintrin.h>
#endif

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

#include "device_profile.h"
#include "own_heavy_patch.h"

using namespace synthyboy;

namespace
{
// The candidate profiles from the cheapest to the dearest: each step adds what the step before lacked. The order follows the cost and audible gain measured on the
// development machine (the analysis that came with this bench); the device decides how far up it gets.
const char* const kLadder[] = {
    "low",
    "low,unison=4",
    "low,unison=7",
    "low,unison=7,warp=4x",
    "low,unison=7,warp=4x,morph=full",
    "low,unison=7,warp=4x,morph=full,control=full",
    "low,unison=7,warp=4x,morph=full,control=full,math=exact",
    "low,unison=7,warp=4x,morph=full,control=full,math=exact,fx=all",
    "high,voices=8",
    "high,voices=12",
    "high,voices=16",
    "high,voices=24",
    "high",
};
const char* const kSettings[] = {
    "low",
    "low,unison=4",
    "low,unison=7",
    "low,unison=16",
    "low,warp=4x",
    "low,morph=full",
    "low,math=exact",
    "low,control=full",
    "low,fx=all",
    "low,samples=high",
    "high,voices=8",
};
const char* const kTiers[] = { "low", "medium", "high" };
const char* const kQuick[] = { "low", "low,unison=7", "high,voices=8" };

// The heaviest factory presets: the union of the dearest by the median block time on the development machine at "low" and at "high,voices=8", plus the two the earlier
// Key 61 measurements name (docs/MPC.md: Diffusor Cloud Pad the dearest of the 112 at tier low, Hyper Saw Lead the one with the most unison voices). The ranking of a
// device may differ: --all-presets measures every one. Then a typical pad, bass and lead: the presets at the median cost of their category.
const char* const kHeavy[] = { "Phaser Sweep Lead", "EMS Squelch Bass", "Moog Ladder Bass", "Sync Growl Bass", "Deep Sea Ambient", "Twin Warp Lead", "Diode Bite Lead", "Diffusor Cloud Pad", "Hyper Saw Lead" };
const char* const kTypical[] = { "Golden Dawn Pad", "Dirty Distortion Bass", "Warm Vintage Lead" };

/** Flush-to-zero as in the MPC plugin (shell-mpc/synthyboy_mpc.cpp, flushDenormalsOn): the FZ bit on ARM, FTZ and DAZ on x86. */
void flushDenormalsOn() noexcept
{
#if defined(__arm__)
    uint32_t fpscr;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= 0x01000000u;
    __asm__ __volatile__("vmsr fpscr, %0" ::"r"(fpscr));
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= 0x01000000ull;
    __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr));
#elif defined(__x86_64__) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#endif
}

struct Options
{
    std::string presetDir;
    int core = -1;
    std::string set;
    std::vector<std::string> configs;
    std::vector<std::string> loads;
    bool allPresets = false;
    double seconds = 30.0;
    int notes = 0;
    double retrigger = 1.0;
    bool paced = false;
    std::string csv;
    int block = 128;
    double sampleRate = 44100.0;
    bool ftz = true;
    bool sections = false;
    bool list = false;
};

struct PresetData
{
    std::string name, category, path;
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
    out.path = path;
    out.name = meta.name.empty() ? path : meta.name;
    out.category = meta.category;
    return true;
}

struct Load
{
    enum Kind { worst, heavy, basic, preset } kind = basic;
    std::string name;
    std::string group; // "synthetic" or the category of the preset
    PresetData data;
};

void setIfKnown (IEngine& engine, const std::string& key, float value)
{
    if (const auto* info = findParamByKey (key.c_str()))
        engine.setParam (info->id, value);
    else
        std::fprintf (stderr, "tier_bench: unknown parameter %s\n", key.c_str());
}

void slotIfKnown (IEngine& engine, int index, const char* source, const char* destination, float amount)
{
    if (findParamByKey (destination) != nullptr)
        bench::ownSlot (engine, index, source, destination, amount);
    else
        std::fprintf (stderr, "tier_bench: unknown destination %s\n", destination);
}

/** The synthetic worst case: the heavy patch with 16 unison voices, a morph and an oversampled warp on each oscillator, and both moving. */
void worstPatch (IEngine& engine)
{
    bench::ownHeavyPatch (engine);
    const float warps[] = { 4.0f, 1.0f, 33.0f }; // Bend (sine of the phase), Sync, sine fold (a sine of the sample): all three run the oscillator oversampled
    for (int i = 1; i <= 3; ++i)
    {
        const std::string k = "osc" + std::to_string (i) + ".";
        setIfKnown (engine, k + "unison.voices", 16.0f);
        setIfKnown (engine, k + "morph.type", 1.0f);
        setIfKnown (engine, k + "morph.amount", 60.0f);
        setIfKnown (engine, k + "warp.type", warps[i - 1]);
        setIfKnown (engine, k + "warp.amount", 40.0f);
    }
    slotIfKnown (engine, 7, "lfo1", "osc1.morph.amount", 30.0f); // a moving morph: its cycle is rebuilt at the morph rate, the cache does not help
    slotIfKnown (engine, 8, "lfo3", "osc2.warp.amount", 30.0f);
}

void applyLoad (IEngine& engine, const Load& load)
{
    // as the shell: every parameter at its default first (the constructor of the plugin instance), then the program
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        engine.setParam (table[i].id, table[i].def);
    switch (load.kind)
    {
        case Load::worst: worstPatch (engine); break;
        case Load::heavy: bench::ownHeavyPatch (engine); break;
        case Load::basic: setIfKnown (engine, "master.polyphony", 32.0f); break;
        case Load::preset:
            for (size_t i = 0; i < paramCount(); ++i)
                engine.setParam (table[i].id, load.data.values.plain[i]);
            engine.setContent (load.data.content);
            break;
    }
}

struct Result
{
    long long blocks = 0;
    double minMs = 0.0, medianMs = 0.0, p99Ms = 0.0, maxMs = 0.0, meanMs = 0.0;
    long long over = 0;       // blocks longer than their real time
    double voicesMean = 0.0;  // sounding voices at the end of a block, mean
    int voicesMax = 0;
    std::vector<double> sectionMs; // per block, by section (--sections)
};

Result run (const Options& o, const synthyboy_mpc::Profile& profile, const Load& load, int notes)
{
    auto engine = createEngine (EngineKind::own);
    applyLoad (*engine, load);
    Quality quality = synthyboy_mpc::profileQuality (profile);
    quality.realtime = true; // as the shell: the audio thread never waits for a helper thread
    engine->prepare (o.sampleRate, o.block, quality);

    std::vector<float> left (static_cast<size_t> (o.block)), right (static_cast<size_t> (o.block));
    std::vector<Event> events;
    events.reserve (static_cast<size_t> (2 * notes + 2));
    const long long warmup = static_cast<long long> (o.sampleRate / o.block);
    const long long measured = std::max<long long> (1, static_cast<long long> (o.seconds * o.sampleRate / o.block));
    const long long retrigger = std::max<long long> (1, static_cast<long long> (o.retrigger * o.sampleRate / o.block));
    std::vector<double> times;
    times.reserve (static_cast<size_t> (measured));
    double voiceSum = 0.0;
    int voiceMax = 0;
    float sink = 0.0f;
    const auto blockTime = std::chrono::duration<double> (o.block / o.sampleRate);
    const auto start = std::chrono::steady_clock::now();
    for (long long b = 0; b < warmup + measured; ++b)
    {
        events.clear();
        if (b % retrigger == 0)
        {
            // the chord is released and struck again in the same block (the tails of the old notes overlap the new ones: voices are stolen at full polyphony)
            for (int v = 0; v < notes; ++v)
            {
                Event off;
                off.type = Event::NoteOff;
                off.key = static_cast<int16_t> (36 + (v * 7) % 60);
                events.push_back (off);
            }
            for (int v = 0; v < notes; ++v)
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
        if (o.paced)
            std::this_thread::sleep_until (start + std::chrono::duration_cast<std::chrono::steady_clock::duration> (blockTime * static_cast<double> (b)));
        if (b == warmup && o.sections)
            ownSectionReset();
        const auto t0 = std::chrono::steady_clock::now();
        engine->process (events.data(), static_cast<int> (events.size()), left.data(), right.data(), o.block);
        const auto t1 = std::chrono::steady_clock::now();
        sink += left[static_cast<size_t> (o.block / 2)];
        if (b >= warmup)
        {
            times.push_back (std::chrono::duration<double> (t1 - t0).count());
            const int av = engine->activeVoices();
            voiceSum += av;
            voiceMax = std::max (voiceMax, av);
        }
    }
    if (sink == 12345.678f)
        std::puts ("");

    Result r;
    r.blocks = static_cast<long long> (times.size());
    const double budget = o.block / o.sampleRate;
    double sum = 0.0;
    for (const double t : times)
    {
        sum += t;
        r.over += t > budget ? 1 : 0;
    }
    std::sort (times.begin(), times.end());
    r.minMs = 1000.0 * times.front();
    r.medianMs = 1000.0 * times[times.size() / 2];
    r.p99Ms = 1000.0 * times[static_cast<size_t> (0.99 * static_cast<double> (times.size() - 1))];
    r.maxMs = 1000.0 * times.back();
    r.meanMs = 1000.0 * sum / static_cast<double> (times.size());
    r.voicesMean = voiceSum / static_cast<double> (times.size());
    r.voicesMax = voiceMax;
    if (o.sections)
    {
        const int count = ownSectionCount();
        for (int i = 0; i < count; ++i)
        {
            double total = 0.0;
            unsigned long long calls = 0;
            ownSectionRead (i, total, calls);
            r.sectionMs.push_back (total / static_cast<double> (measured));
        }
    }
    return r;
}

std::string readLine (const std::string& path)
{
    std::ifstream in (path);
    std::string s;
    std::getline (in, s);
    return s;
}

/** The CPU this thread runs on, with what the kernel says about it (Linux; "" elsewhere). */
std::string describeCpu (int& cpuOut)
{
    cpuOut = -1;
#if defined(__linux__)
    const int cpu = sched_getcpu();
    cpuOut = cpu;
    std::string part, t;
    {
        // /proc/cpuinfo: the block of "processor : <cpu>", its "CPU part" (0xd05 Cortex-A55, 0xd0b Cortex-A76, 0xc0d Cortex-A17) or "model name"
        std::ifstream in ("/proc/cpuinfo");
        std::string line;
        bool mine = false;
        while (std::getline (in, line))
        {
            if (line.rfind ("processor", 0) == 0)
                mine = std::atoi (line.substr (line.find (':') + 1).c_str()) == cpu;
            else if (mine && (line.rfind ("CPU part", 0) == 0 || line.rfind ("model name", 0) == 0))
                part += (part.empty() ? "" : ", ") + line.substr (line.find (':') + 2);
        }
    }
    const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string (cpu) + "/cpufreq/";
    const char* name = part.find ("0xd0b") != std::string::npos ? " (Cortex-A76)" : (part.find ("0xd05") != std::string::npos ? " (Cortex-A55)" : (part.find ("0xc0d") != std::string::npos ? " (Cortex-A17)" : ""));
    t = "cpu " + std::to_string (cpu) + ", part " + (part.empty() ? "?" : part) + name + ", governor " + readLine (base + "scaling_governor") + ", frequency now " + readLine (base + "scaling_cur_freq")
        + " kHz, max " + readLine (base + "scaling_max_freq") + " kHz";
    return t;
#else
    return "";
#endif
}

[[noreturn]] void usage (const char* message)
{
    std::fprintf (stderr, "%s\nusage: synthyboy_tier_bench --presets DIR [--core N] [--set settings|ladder|tiers|quick] [--config PROFILE]... [--load NAME]... [--all-presets] [--seconds 30] "
                          "[--notes K] [--retrigger 1] [--paced] [--csv FILE] [--block 128] [--sr 44100] [--no-ftz] [--sections] [--list]\n", message);
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
        if (a == "--presets")          o.presetDir = next();
        else if (a == "--core")        o.core = std::atoi (next());
        else if (a == "--set")         o.set = next();
        else if (a == "--config")      o.configs.push_back (next());
        else if (a == "--load")        o.loads.push_back (next());
        else if (a == "--all-presets") o.allPresets = true;
        else if (a == "--seconds")     o.seconds = std::atof (next());
        else if (a == "--notes")       o.notes = std::atoi (next());
        else if (a == "--retrigger")   o.retrigger = std::atof (next());
        else if (a == "--paced")       o.paced = true;
        else if (a == "--csv")         o.csv = next();
        else if (a == "--block")       o.block = std::atoi (next());
        else if (a == "--sr")          o.sampleRate = std::atof (next());
        else if (a == "--no-ftz")      o.ftz = false;
        else if (a == "--sections")    o.sections = true;
        else if (a == "--list")        o.list = true;
        else usage (("unknown argument " + a).c_str());
    }
    if (o.seconds <= 0.0 || o.block < 1 || o.block > 8192 || o.notes < 0 || o.notes > kMaxVoices || o.sampleRate < 8000.0 || o.retrigger <= 0.0)
        usage ("argument out of range");
    if (o.sections && ! ownSectionTimingAvailable())
        usage ("--sections needs a build with -DSYNTHYBOY_SECTION_TIMING=ON");

    // ---- the profiles ----
    std::vector<std::string> configs;
    if (o.set.empty() && o.configs.empty())
        o.set = "settings";
    if (o.set == "settings")     configs.assign (std::begin (kSettings), std::end (kSettings));
    else if (o.set == "ladder")  configs.assign (std::begin (kLadder), std::end (kLadder));
    else if (o.set == "tiers")   configs.assign (std::begin (kTiers), std::end (kTiers));
    else if (o.set == "quick")   configs.assign (std::begin (kQuick), std::end (kQuick));
    else if (! o.set.empty())    usage ("unknown --set");
    for (const auto& c : o.configs)
        configs.push_back (c);
    std::vector<synthyboy_mpc::Profile> profiles;
    for (const auto& c : configs)
    {
        synthyboy_mpc::Profile p;
        std::string why;
        if (! synthyboy_mpc::parseProfile (c, p, &why))
            usage (("profile '" + c + "': " + why).c_str());
        profiles.push_back (p);
    }

    // ---- the loads ----
    std::vector<PresetEntry> entries;
    if (! o.presetDir.empty())
        entries = scanPresetDirectory (o.presetDir.c_str());
    std::sort (entries.begin(), entries.end(), [] (const PresetEntry& x, const PresetEntry& y) { return x.path < y.path; });
    std::vector<PresetData> all;
    for (const auto& e : entries)
    {
        PresetData pd;
        if (loadPresetFile (e.path, pd))
            all.push_back (std::move (pd));
        else
            std::fprintf (stderr, "tier_bench: %s could not be loaded\n", e.path.c_str());
    }
    std::vector<Load> loads;
    auto addNamed = [&] (const std::string& name, bool required) -> bool
    {
        Load l;
        if (name == "worst" || name == "heavy" || name == "basic")
        {
            l.kind = name == "worst" ? Load::worst : (name == "heavy" ? Load::heavy : Load::basic);
            l.name = name;
            l.group = "synthetic";
            loads.push_back (std::move (l));
            return true;
        }
        for (const auto& pd : all)
            if (pd.name == name)
            {
                l.kind = Load::preset;
                l.name = pd.name;
                l.group = pd.category;
                l.data = pd;
                loads.push_back (std::move (l));
                return true;
            }
        PresetData pd;
        if (loadPresetFile (name, pd))
        {
            l.kind = Load::preset;
            l.name = pd.name;
            l.group = pd.category;
            l.data = std::move (pd);
            loads.push_back (std::move (l));
            return true;
        }
        if (required)
            usage (("load '" + name + "' is neither worst / heavy / basic nor a preset name of --presets nor a preset file").c_str());
        std::fprintf (stderr, "tier_bench: the default load '%s' was not found (--presets DIR must be the factory presets); it is left out\n", name.c_str());
        return false;
    };
    if (o.allPresets)
    {
        if (all.empty())
            usage ("--all-presets needs --presets DIR with .sbpreset files");
        for (const auto& pd : all)
        {
            Load l;
            l.kind = Load::preset;
            l.name = pd.name;
            l.group = pd.category;
            l.data = pd;
            loads.push_back (std::move (l));
        }
    }
    for (const auto& n : o.loads)
        addNamed (n, true);
    if (loads.empty())
    {
        addNamed ("worst", true);
        for (const char* n : kHeavy)
            addNamed (n, false);
        for (const char* n : kTypical)
            addNamed (n, false);
    }

    // ---- the CPU ----
    if (o.core >= 0)
    {
#if defined(__linux__)
        cpu_set_t set;
        CPU_ZERO (&set);
        CPU_SET (static_cast<unsigned> (o.core), &set);
        if (sched_setaffinity (0, sizeof set, &set) != 0)
        {
            std::perror ("tier_bench: sched_setaffinity");
            return 1;
        }
#else
        std::fprintf (stderr, "tier_bench: --core needs Linux (sched_setaffinity); run without it here\n");
        return 2;
#endif
    }
    if (o.ftz)
        flushDenormalsOn();
    int cpu = -1;
    const std::string cpuText = describeCpu (cpu);
    const std::string model = readLine ("/proc/device-tree/model").c_str(); // (c_str: the device tree string ends with a 0 byte)
    const double budgetMs = 1000.0 * o.block / o.sampleRate;

    std::printf ("synthyboy_tier_bench: %.0f Hz, block %d frames = %.3f ms, %.1f s measured per run after 1 s of warm-up, retrigger every %.2f s, flush-to-zero %s, %s\n", o.sampleRate, o.block,
                 budgetMs, o.seconds, o.retrigger, o.ftz ? "on" : "off", o.paced ? "blocks paced at real time" : "blocks back to back");
    std::printf ("device: %s; %s; build %s\n", model.empty() ? "(no device tree name)" : model.c_str(), cpuText.empty() ? "cpu not pinned (not Linux)" : cpuText.c_str(),
#if defined(__aarch64__)
                 "arm64"
#elif defined(__arm__)
                 "arm32"
#elif defined(__x86_64__)
                 "x86_64"
#else
                 "other"
#endif
    );
    if (o.core >= 0 && cpu != o.core)
        std::printf ("WARNING: asked for cpu %d, running on cpu %d\n", o.core, cpu);

    if (o.list)
    {
        for (size_t c = 0; c < configs.size(); ++c)
            std::printf ("profile %-64s = %s\n", configs[c].c_str(), synthyboy_mpc::describeProfile (profiles[c]).c_str());
        for (const auto& l : loads)
            std::printf ("load    %-40s (%s)\n", l.name.c_str(), l.group.c_str());
        std::printf ("%zu runs, %.0f s of measured audio\n", configs.size() * loads.size(), static_cast<double> (configs.size() * loads.size()) * (o.seconds + 1.0));
        return 0;
    }

    FILE* csv = o.csv.empty() ? nullptr : std::fopen (o.csv.c_str(), "w");
    if (! o.csv.empty() && csv == nullptr)
    {
        std::perror ("tier_bench: --csv");
        return 1;
    }
    if (csv != nullptr)
        std::fprintf (csv, "device,cpu,cpu_info,profile,settings,load,group,notes,voices_mean,voices_max,sr,block,blocks,min_pct,median_pct,p99_pct,max_pct,mean_pct,min_ms,median_ms,p99_ms,max_ms,mean_ms,blocks_over_100pct\n");

    std::printf ("\nshare of the block's real time (%.3f ms = 100 %%) spent in process(), one core:\n", budgetMs);
    std::printf ("%-58s %-24s %5s %6s | %7s %7s %7s %7s %7s | %5s\n", "profile", "load", "notes", "voices", "min %", "median%", "p99 %", "max %", "mean %", "over");
    struct Summary { std::string preset; double p99 = -1.0, max = -1.0; double worstP99 = -1.0; };
    std::vector<Summary> summaries (configs.size());
    for (size_t c = 0; c < configs.size(); ++c)
    {
        const int notes = o.notes > 0 ? o.notes : profiles[c].maxVoices;
        for (const auto& l : loads)
        {
            const Result r = run (o, profiles[c], l, notes);
            const double k = 100.0 / budgetMs;
            std::printf ("%-58s %-24.24s %5d %6.1f | %7.2f %7.2f %7.2f %7.2f %7.2f | %5lld\n", configs[c].c_str(), l.name.c_str(), notes, r.voicesMean, r.minMs * k, r.medianMs * k, r.p99Ms * k,
                         r.maxMs * k, r.meanMs * k, r.over);
            std::fflush (stdout);
            if (o.sections)
            {
                double total = 0.0;
                for (const double ms : r.sectionMs)
                    total += ms;
                for (size_t i = 0; i < r.sectionMs.size(); ++i)
                    if (r.sectionMs[i] > 0.0)
                        std::printf ("    section %-52s %8.4f ms / block %6.1f %%\n", ownSectionName (static_cast<int> (i)), r.sectionMs[i], total > 0.0 ? 100.0 * r.sectionMs[i] / total : 0.0);
            }
            if (csv != nullptr)
            {
                std::fprintf (csv, "\"%s\",%d,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%d,%.2f,%d,%.0f,%d,%lld,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%lld\n", model.c_str(), cpu, cpuText.c_str(),
                              configs[c].c_str(), synthyboy_mpc::describeProfile (profiles[c]).c_str(), l.name.c_str(), l.group.c_str(), notes, r.voicesMean, r.voicesMax, o.sampleRate, o.block,
                              r.blocks, r.minMs * k, r.medianMs * k, r.p99Ms * k, r.maxMs * k, r.meanMs * k, r.minMs, r.medianMs, r.p99Ms, r.maxMs, r.meanMs, r.over);
                std::fflush (csv);
            }
            if (l.kind == Load::preset && r.p99Ms * k > summaries[c].p99)
            {
                summaries[c].p99 = r.p99Ms * k;
                summaries[c].max = r.maxMs * k;
                summaries[c].preset = l.name;
            }
            if (l.kind == Load::worst)
                summaries[c].worstP99 = r.p99Ms * k;
        }
    }
    if (csv != nullptr)
        std::fclose (csv);

    std::printf ("\nper profile: the dearest factory preset of this run by the 99th percentile (the number the budget rule reads), and the synthetic worst case\n");
    std::printf ("%-58s %5s | %7s %7s  %-24s | %9s\n", "profile", "notes", "p99 %", "max %", "preset", "worst p99");
    for (size_t c = 0; c < configs.size(); ++c)
    {
        const int notes = o.notes > 0 ? o.notes : profiles[c].maxVoices;
        char worst[32] = "-";
        if (summaries[c].worstP99 >= 0.0)
            std::snprintf (worst, sizeof worst, "%.2f", summaries[c].worstP99);
        if (summaries[c].p99 >= 0.0)
            std::printf ("%-58s %5d | %7.2f %7.2f  %-24.24s | %9s\n", configs[c].c_str(), notes, summaries[c].p99, summaries[c].max, summaries[c].preset.c_str(), worst);
        else
            std::printf ("%-58s %5d | %7s %7s  %-24s | %9s\n", configs[c].c_str(), notes, "-", "-", "(no preset in this run)", worst);
    }
    return 0;
}
