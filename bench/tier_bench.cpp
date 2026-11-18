// SynthyBoy - benchmark: which quality profile a device can run. Plays loads through the own engine the way the MPC shell does (44.1 kHz, 128 frame blocks, the
// parameters by setParam and then the content, a real-time quality, flush-to-zero) under a list of quality profiles (shell-mpc/device_profile.h) and prints the time
// of a block as a share of the block's real time: minimum, median, 99th percentile, maximum.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_tier_bench --presets DIR [--core N] [--set settings|ladder|tiers|quick] [--config PROFILE]... [--load NAME]... [--all-presets]
//                        [--seconds 30] [--repeat N] [--budget PCT] [--notes K] [--retrigger 1] [--paced] [--csv FILE] [--block 128] [--sr 44100] [--no-ftz]
//                        [--sections] [--list]
//
//   --core N        pin the process to CPU N (Linux: sched_setaffinity; elsewhere the option is refused). Nothing else of the machine is touched: no scheduling class,
//                   no priority, no frequency governor. The CPU the run really was on, its part number and its frequency settings are printed.
//   --set NAME      the list of profiles (default settings):
//                     settings  "low" (what the MPC build ships) and one change at a time: unison 4 / 7 / 16, warp 4x, morph full, math exact, control full, fx all,
//                               samples high; then everything at once with 8 voices; each with the notes of its polyphony
//                     ladder    the candidate profiles from the cheapest to the dearest (kLadder below), each at ITS full polyphony (8, 12, 16, 32 notes)
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
//   --repeat N      run the whole list N times, one pass after the other (default 1). The summary then takes, per preset, the MEDIAN of the passes' 99th
//                   percentiles and prints the lowest and highest pass next to it (the spread).
//   --budget PCT    add a verdict to the summary: a profile is "inside" when the 99th percentile of its dearest factory preset is at most PCT % of the block and
//                   no block of any of its factory presets, in any pass, was longer than its real time
//   --no-fx-mute    process an effect whose mix is exactly 0 all the same (the engine before that saving: for a before / after with one binary)
//   --scalar-filters  run the voice filters' scalar loops also where a vector kernel exists (the same: before / after)
//   --scalar-warp   make the oversampled samples of a warped oscillator one by one also where the four lane form exists (the same: before / after)
//   --profile FILE  a sampling profile of the audio thread (a timer signal every 250 us on Linux ARM): the program counters, one per line, for scripts/bench-profile.py to map to functions
//   --tolerance X   with --verify: a sample may differ by X times the largest sample of the load (the vector forms of the Spectral oscillator differ by rounding, 3e-6 is the stated bound)
//   --scalar-content  the Sample / Granular / Spectral / Multisample oscillators run their per sample (per frame) reference code, not the block kernels (the same: before / after)
//   --verify        no timing: every load is rendered twice under every profile, once with the savings (the pair kernels of the filters, the skipped effects with
//                   mix 0) and once without, offline, 3 s with the chord struck twice, and the samples are compared. They must be the same values (the exit code is
//                   1 otherwise): this is the check that the vector code of THIS CPU computes what the scalar code computes.
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
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
 #include <csignal>
 #include <ctime>
 #include <sched.h>
 #include <sys/syscall.h>
 #include <ucontext.h>
 #include <unistd.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
 #include <pmmintrin.h>
 #include <xmmintrin.h>
#endif

#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

#include "device_profile.h"
#include "own_heavy_patch.h"

using namespace synthyboy;

namespace
{
#if defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
/** --profile FILE: a sampling profile of the audio thread by the program counter (a timer signal every 250 microseconds of real time, aimed at the calling thread). The addresses are written as
    text, one per line, and map to functions with the symbol table of the unstripped binary (scripts/bench-profile.py). Nothing but the benchmark uses it. */
struct PcProfiler
{
    static constexpr size_t kMax = 400000;
    static inline uintptr_t pcs[kMax];
    static inline volatile size_t count = 0;
    timer_t timer {};
    bool running = false;
    static void handler (int, siginfo_t*, void* ctx)
    {
        const ucontext_t* uc = static_cast<const ucontext_t*> (ctx);
 #if defined(__arm__)
        const uintptr_t pc = uc->uc_mcontext.arm_pc;
 #else
        const uintptr_t pc = static_cast<uintptr_t> (uc->uc_mcontext.pc);
 #endif
        const size_t i = count;
        if (i < kMax)
        {
            pcs[i] = pc;
            count = i + 1;
        }
    }
    bool start()
    {
        struct sigaction sa {};
        sa.sa_sigaction = &PcProfiler::handler;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset (&sa.sa_mask);
        if (sigaction (SIGPROF, &sa, nullptr) != 0)
            return false;
        struct sigevent sev {};
        sev.sigev_notify = SIGEV_THREAD_ID;
        sev.sigev_signo = SIGPROF;
        sev._sigev_un._tid = static_cast<pid_t> (syscall (SYS_gettid));
        if (timer_create (CLOCK_MONOTONIC, &sev, &timer) != 0)
            return false;
        struct itimerspec its {};
        its.it_interval.tv_nsec = 250000;
        its.it_value.tv_nsec = 250000;
        running = timer_settime (timer, 0, &its, nullptr) == 0;
        return running;
    }
    void stopAndWrite (const char* path)
    {
        if (running)
        {
            struct itimerspec off {};
            timer_settime (timer, 0, &off, nullptr);
            timer_delete (timer);
            running = false;
        }
        FILE* f = std::fopen (path, "w");
        if (f == nullptr)
            return;
        for (size_t i = 0; i < count; ++i)
            std::fprintf (f, "%lx\n", static_cast<unsigned long> (pcs[i]));
        std::fclose (f);
        std::printf ("profile: %zu samples written to %s\n", static_cast<size_t> (count), path);
    }
};
#endif
// The candidate profiles from the cheapest to the dearest. The order is the audible gain per cost found with bench/profile_compare.cpp and this bench on the
// development machine: first the unison voices (14 factory presets ask for more than 3; the texture of the detuned voices, levels within 3 dB), then the effects that low bypasses
// (31 factory presets sound through them), then the polyphony, then the 4x warps (7 presets, a small difference) and the full morph rate; then the exact math and
// the better sample interpolation, which no measurement could tell from the cheap forms on the factory presets; and last the full control rate: its 50 ms fade
// of a stolen voice doubles the dearest blocks (the old and the new chord sound together) for a difference only heard when voices are stolen. The factory presets
// ask for at most 16 voices themselves, so the last step costs them nothing more: it is for user patches.
const char* const kLadder[] = {
    "low",
    "low,unison=7",
    "low,unison=7,fx=all",
    "low,unison=16,fx=all",
    "low,unison=16,fx=all,voices=12",
    "low,unison=16,fx=all,voices=16",
    "low,unison=16,fx=all,voices=16,warp=4x",
    "low,unison=16,fx=all,voices=16,warp=4x,morph=full",
    "high,voices=16,control=economy",
    "high,voices=16",
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
    int repeat = 1;
    double budget = 0.0;
    bool verify = false;
    double tolerance = 0.0;  // --verify: a sample may differ by this share of the load's largest sample (default 0: the same values)
    std::string profilePath; // --profile
    uint32_t savingsOff = 0; // --no-fx-mute, --scalar-filters: OwnSaving bits switched off (before / after measurements with one binary)
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

/** The store for the files a preset names (pack id + file): $SYNTHYBOY_CONTENT_DIR/<pack id>/<file>; null without the variable (then a sample oscillator is silent and Convolve absent, as on a device without content). */
const IContentStore* benchContentStore()
{
    static const std::unique_ptr<DirectoryContentStore> store = []() -> std::unique_ptr<DirectoryContentStore>
    {
        const char* dir = std::getenv ("SYNTHYBOY_CONTENT_DIR");
        return dir != nullptr && dir[0] != '\0' ? std::make_unique<DirectoryContentStore> (dir) : nullptr;
    }();
    return store.get();
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
            engine.setContentStore (benchContentStore());
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
    std::vector<double> sectionCalls; // scopes entered per block, by section
    std::vector<double> sectionReads; // clock reads that land in the section's time per block (own scopes + scopes directly inside)
};

Result run (const Options& o, const synthyboy_mpc::Profile& profile, const Load& load, int notes)
{
    auto engine = createEngine (EngineKind::own);
    applyLoad (*engine, load);
    setOwnSavingsOff (*engine, o.savingsOff);
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
            // the clock reads that land in this section: one of the two of each of its own scopes, and one of each scope entered directly inside it
            r.sectionCalls.push_back (static_cast<double> (calls) / static_cast<double> (measured));
            r.sectionReads.push_back (static_cast<double> (calls + ownSectionChildCalls (i)) / static_cast<double> (measured));
        }
    }
    return r;
}

/** --verify: the load under the profile, offline, with the savings named in `off` switched off; left then right of every block. */
std::vector<float> renderForVerify (const Options& o, const synthyboy_mpc::Profile& profile, const Load& load, int notes, uint32_t off)
{
    auto engine = createEngine (EngineKind::own);
    applyLoad (*engine, load);
    setOwnSavingsOff (*engine, off);
    Quality quality = synthyboy_mpc::profileQuality (profile);
    quality.realtime = false;
    engine->prepare (o.sampleRate, o.block, quality);
    std::vector<float> out, l (static_cast<size_t> (o.block)), r (static_cast<size_t> (o.block));
    const int blocks = static_cast<int> (3.0 * o.sampleRate / o.block), second = static_cast<int> (1.2 * o.sampleRate / o.block);
    std::vector<Event> events;
    for (int b = 0; b < blocks; ++b)
    {
        events.clear();
        if (b == 0 || b == second)
        {
            for (int v = 0; v < notes && b != 0; ++v)
            {
                Event off2;
                off2.type = Event::NoteOff;
                off2.key = static_cast<int16_t> (36 + (v * 7) % 60);
                events.push_back (off2);
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
        engine->process (events.data(), static_cast<int> (events.size()), l.data(), r.data(), o.block);
        out.insert (out.end(), l.begin(), l.end());
        out.insert (out.end(), r.begin(), r.end());
    }
    return out;
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

/** The frequency the kernel reports for a CPU right now in kHz (Linux cpufreq; 0 when it cannot be read). Read after every run: a number taken on a core that
    sat at a low frequency says nothing about the core. */
long currentKhz (int cpu)
{
    if (cpu < 0)
        return 0;
    return std::atol (readLine ("/sys/devices/system/cpu/cpu" + std::to_string (cpu) + "/cpufreq/scaling_cur_freq").c_str());
}

[[noreturn]] void usage (const char* message)
{
    std::fprintf (stderr, "%s\nusage: synthyboy_tier_bench --presets DIR [--core N] [--set settings|ladder|tiers|quick] [--config PROFILE]... [--load NAME]... [--all-presets] [--seconds 30] "
                          "[--notes K] [--retrigger 1] [--paced] [--repeat N] [--budget PCT] [--verify] [--no-fx-mute] [--scalar-filters] [--scalar-warp] [--csv FILE] [--block 128] [--sr 44100] [--no-ftz] [--sections] [--list]\n", message);
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
        else if (a == "--repeat")      o.repeat = std::atoi (next());
        else if (a == "--verify")      o.verify = true;
        else if (a == "--tolerance")   o.tolerance = std::atof (next());
        else if (a == "--profile")     o.profilePath = next();
        else if (a == "--no-fx-mute")  o.savingsOff |= kOwnSavingFxMute;
        else if (a == "--scalar-filters") o.savingsOff |= kOwnSavingVectorFilters;
        else if (a == "--scalar-warp") o.savingsOff |= kOwnSavingVectorWarp;
        else if (a == "--scalar-content") o.savingsOff |= kOwnSavingContentKernels;
        else if (a == "--budget")      o.budget = std::atof (next());
        else usage (("unknown argument " + a).c_str());
    }
    if (o.seconds <= 0.0 || o.block < 1 || o.block > 8192 || o.notes < 0 || o.notes > kMaxVoices || o.sampleRate < 8000.0 || o.retrigger <= 0.0 || o.repeat < 1 || o.repeat > 99 || o.budget < 0.0)
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

    std::printf ("synthyboy_tier_bench: %.0f Hz, block %d frames = %.3f ms, %.1f s measured per run after 1 s of warm-up, retrigger every %.2f s, flush-to-zero %s, %s%s%s\n", o.sampleRate, o.block,
                 budgetMs, o.seconds, o.retrigger, o.ftz ? "on" : "off", o.paced ? "blocks paced at real time" : "blocks back to back",
                 (o.savingsOff & kOwnSavingFxMute) != 0 ? ", effects with mix 0 processed (--no-fx-mute)" : "", (o.savingsOff & kOwnSavingVectorFilters) != 0 ? ", scalar filters (--scalar-filters)" : "");
    if ((o.savingsOff & kOwnSavingVectorWarp) != 0)
        std::printf ("scalar warp samples (--scalar-warp)\n");
    if ((o.savingsOff & kOwnSavingContentKernels) != 0)
        std::printf ("reference code of the Sample / Granular / Spectral / Multisample oscillators (--scalar-content)\n");
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
        std::printf ("%zu runs, %.0f s of audio (a run takes its audio time multiplied by the load it measures)\n", configs.size() * loads.size() * static_cast<size_t> (o.repeat),
                     static_cast<double> (configs.size() * loads.size()) * (o.seconds + 1.0) * o.repeat);
        return 0;
    }

    if (o.verify)
    {
        std::printf ("\nverify: every load under every profile, with the savings and without (filters one side after the other, effects with mix 0 processed, warp samples one by one): the samples must be the same\n");
        int failures = 0, runs = 0;
        for (size_t c = 0; c < configs.size(); ++c)
        {
            const int notes = o.notes > 0 ? o.notes : profiles[c].maxVoices;
            int bad = 0;
            for (const auto& l : loads)
            {
                const std::vector<float> with = renderForVerify (o, profiles[c], l, notes, 0), without = renderForVerify (o, profiles[c], l, notes, kOwnSavingFxMute | kOwnSavingVectorFilters | kOwnSavingVectorWarp | kOwnSavingContentKernels);
                size_t differ = 0;
                float worst = 0.0f, peak = 0.0f;
                for (size_t i = 0; i < with.size(); ++i)
                    peak = std::max (peak, std::fabs (with[i]));
                for (size_t i = 0; i < with.size(); ++i)
                    if (! (with[i] == without[i])) // values: +0 and -0 are the same sample; a NaN on either side counts as a difference
                    {
                        const float d = std::fabs (with[i] - without[i]);
                        if (d <= static_cast<float> (o.tolerance) * peak) // --tolerance: rounding level differences of the vector forms that are stated as such
                        {
                            worst = std::max (worst, d);
                            continue;
                        }
                        ++differ;
                        worst = std::max (worst, d);
                    }
                ++runs;
                if (differ == 0 && worst > 0.0f)
                    std::printf ("within the tolerance  %-58s %-24.24s largest difference %.3g = %.3g of the peak %.3f\n", configs[c].c_str(), l.name.c_str(), static_cast<double> (worst),
                                 static_cast<double> (worst / std::max (peak, 1.0e-30f)), static_cast<double> (peak));
                if (differ != 0)
                {
                    ++bad;
                    std::printf ("DIFFERENT  %-58s %-24.24s %zu of %zu samples, largest difference %.3g (peak %.3f)\n", configs[c].c_str(), l.name.c_str(), differ, with.size(),
                                 static_cast<double> (worst), static_cast<double> (peak));
                }
                std::fflush (stdout);
            }
            failures += bad;
            std::printf ("%-58s %zu loads: %s\n", configs[c].c_str(), loads.size(), bad == 0 ? (o.tolerance > 0.0 ? "within the tolerance" : "the same samples") : "DIFFERENCES (above)");
        }
        std::printf ("verify: %d of %d renders differ\n", failures, runs);
        return failures == 0 ? 0 : 1;
    }

    FILE* csv = o.csv.empty() ? nullptr : std::fopen (o.csv.c_str(), "w");
    if (! o.csv.empty() && csv == nullptr)
    {
        std::perror ("tier_bench: --csv");
        return 1;
    }
    if (csv != nullptr)
        std::fprintf (csv, "device,cpu,cpu_info,profile,settings,load,group,notes,voices_mean,voices_max,sr,block,blocks,min_pct,median_pct,p99_pct,max_pct,mean_pct,min_ms,median_ms,p99_ms,max_ms,mean_ms,blocks_over_100pct,cpu_khz_after_run,pass\n");

    std::printf ("\nshare of the block's real time (%.3f ms = 100 %%) spent in process(), one core:\n", budgetMs);
    std::printf ("%-58s %-24s %5s %6s | %7s %7s %7s %7s %7s | %5s %8s %4s\n", "profile", "load", "notes", "voices", "min %", "median%", "p99 %", "max %", "mean %", "over", "cpu MHz", "pass");
    // per profile and load: the 99th percentile and the longest block of every pass
    struct Cell { std::vector<double> p99, max; };
    std::vector<std::vector<Cell>> cells (configs.size(), std::vector<Cell> (loads.size()));
#if defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
    PcProfiler profiler;
    if (! o.profilePath.empty() && ! profiler.start())
        std::printf ("--profile: the sampling timer could not be started\n");
#else
    if (! o.profilePath.empty())
        usage ("--profile needs a Linux ARM build");
#endif
    for (int pass = 1; pass <= o.repeat; ++pass)
        for (size_t c = 0; c < configs.size(); ++c)
        {
            const int notes = o.notes > 0 ? o.notes : profiles[c].maxVoices;
            for (size_t li = 0; li < loads.size(); ++li)
            {
                const Load& l = loads[li];
                const Result r = run (o, profiles[c], l, notes);
                const double k = 100.0 / budgetMs;
                const long khz = currentKhz (cpu);
                std::printf ("%-58s %-24.24s %5d %6.1f | %7.2f %7.2f %7.2f %7.2f %7.2f | %5lld %8ld %4d\n", configs[c].c_str(), l.name.c_str(), notes, r.voicesMean, r.minMs * k, r.medianMs * k,
                             r.p99Ms * k, r.maxMs * k, r.meanMs * k, r.over, khz / 1000, pass);
                std::fflush (stdout);
                if (o.sections)
                {
                    // Every scope reads the clock twice: about one read lands in the scope's own section and one in the section around it. "less clock" takes, per
                    // section, (its own scopes + the scopes entered directly inside it) x half the measured cost of a scope off. Without the second term a section
                    // that only holds many small sections (the voice tick, the render) looks several times dearer than it is on a device whose clock costs a microsecond.
                    const double clockMs = 0.5 * ownSectionClockCostNs() * 1.0e-6;
                    double total = 0.0, totalLess = 0.0;
                    for (size_t i = 0; i < r.sectionMs.size(); ++i)
                    {
                        total += r.sectionMs[i];
                        totalLess += std::max (0.0, r.sectionMs[i] - r.sectionReads[i] * clockMs);
                    }
                    std::printf ("    sections: one scope costs about %.0f ns here; sum %.4f ms / block, less the clock %.4f ms / block\n", ownSectionClockCostNs(), total, totalLess);
                    for (size_t i = 0; i < r.sectionMs.size(); ++i)
                        if (r.sectionMs[i] > 0.0)
                        {
                            const double less = std::max (0.0, r.sectionMs[i] - r.sectionReads[i] * clockMs);
                            std::printf ("    section %-52s %8.4f ms / block %6.1f %%  %7.1f calls / block %7.1f clock reads  less clock %8.4f ms %6.1f %%\n", ownSectionName (static_cast<int> (i)),
                                         r.sectionMs[i], total > 0.0 ? 100.0 * r.sectionMs[i] / total : 0.0, r.sectionCalls[i], r.sectionReads[i], less, totalLess > 0.0 ? 100.0 * less / totalLess : 0.0);
                        }
                }
                if (csv != nullptr)
                {
                    std::fprintf (csv, "\"%s\",%d,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%d,%.2f,%d,%.0f,%d,%lld,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%lld,%ld,%d\n", model.c_str(), cpu,
                                  cpuText.c_str(), configs[c].c_str(), synthyboy_mpc::describeProfile (profiles[c]).c_str(), l.name.c_str(), l.group.c_str(), notes, r.voicesMean, r.voicesMax,
                                  o.sampleRate, o.block, r.blocks, r.minMs * k, r.medianMs * k, r.p99Ms * k, r.maxMs * k, r.meanMs * k, r.minMs, r.medianMs, r.p99Ms, r.maxMs, r.meanMs, r.over, khz, pass);
                    std::fflush (csv);
                }
                cells[c][li].p99.push_back (r.p99Ms * k);
                cells[c][li].max.push_back (r.maxMs * k);
            }
        }
    if (csv != nullptr)
        std::fclose (csv);

    // The number the budget rule reads: per profile, the factory preset with the highest 99th percentile, where a preset's 99th percentile is the MEDIAN of its passes;
    // next to it the lowest and highest pass (the spread) and the longest single block of any pass.
    auto median = [] (std::vector<double> v) { std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
#if defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
    if (! o.profilePath.empty())
        profiler.stopAndWrite (o.profilePath.c_str());
#endif
    std::printf ("\nper profile: the dearest factory preset of this run (99th percentile: the median of %d pass%s, with the lowest and highest pass; max: the longest block of any pass), and the synthetic worst case\n",
                 o.repeat, o.repeat == 1 ? "" : "es");
    std::printf ("%-58s %5s | %7s %15s %7s  %-24s | %9s%s\n", "profile", "notes", "p99 %", "(passes)", "max %", "preset", "worst p99", o.budget > 0.0 ? " | budget" : "");
    for (size_t c = 0; c < configs.size(); ++c)
    {
        const int notes = o.notes > 0 ? o.notes : profiles[c].maxVoices;
        int best = -1;
        double bestP99 = -1.0, worstP99 = -1.0;
        for (size_t li = 0; li < loads.size(); ++li)
        {
            const double m = median (cells[c][li].p99);
            if (loads[li].kind == Load::preset && m > bestP99)
            {
                bestP99 = m;
                best = static_cast<int> (li);
            }
            if (loads[li].kind == Load::worst)
                worstP99 = m;
        }
        char worst[32] = "-";
        if (worstP99 >= 0.0)
            std::snprintf (worst, sizeof worst, "%.2f", worstP99);
        if (best >= 0)
        {
            const Cell& cell = cells[c][static_cast<size_t> (best)];
            const double lo = *std::min_element (cell.p99.begin(), cell.p99.end()), hi = *std::max_element (cell.p99.begin(), cell.p99.end());
            const double longest = *std::max_element (cell.max.begin(), cell.max.end());
            char range[40];
            std::snprintf (range, sizeof range, "(%.2f - %.2f)", lo, hi);
            std::printf ("%-58s %5d | %7.2f %15s %7.2f  %-24.24s | %9s", configs[c].c_str(), notes, bestP99, range, longest, loads[static_cast<size_t> (best)].name.c_str(), worst);
            if (o.budget > 0.0)
            {
                // the longest block of ANY preset of the profile, not only of the dearest one
                double longestAll = 0.0;
                for (size_t li = 0; li < loads.size(); ++li)
                    if (loads[li].kind == Load::preset)
                        longestAll = std::max (longestAll, *std::max_element (cells[c][li].max.begin(), cells[c][li].max.end()));
                std::printf (" | %s", bestP99 <= o.budget && longestAll <= 100.0 ? "inside" : (bestP99 <= o.budget ? "OVER (a block longer than its real time)" : "OVER"));
            }
            std::printf ("\n");
        }
        else
            std::printf ("%-58s %5d | %7s %15s %7s  %-24s | %9s\n", configs[c].c_str(), notes, "-", "", "-", "(no preset in this run)", worst);
    }
    return 0;
}
