// SynthyBoy - benchmark: which quality profile a device can run. Plays loads through the own engine the way the MPC shell does (44.1 kHz, 128 frame blocks, the
// parameters by setParam and then the content, a real-time quality, flush-to-zero) under a list of quality profiles (shell-mpc/device_profile.h) and prints the time
// of a block as a share of the block's real time: minimum, median, 99th percentile, maximum.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_tier_bench --presets DIR [--core N] [--set settings|ladder|tiers|quick] [--config PROFILE]... [--load NAME]... [--all-presets]
//                        [--seconds 30] [--repeat N] [--budget PCT] [--notes K] [--retrigger 1] [--paced] [--csv FILE] [--block 128] [--sr 44100] [--no-ftz]
//                        [--sections] [--list] [--rt N] [--mlock] [--rt-gap MS] [--rt-require] [--no-helpers-away] [--retry-invalid N] [--pmu compute|cache] [--pmu-require]
//                        [--evict] [--first-blocks N] [--start-budget-us N] [--start-cache] [--modules] [--module-group GROUP] [--module-reps N] [--no-aa MASK]
//
//   --core N        pin the measuring thread to CPU N (Linux: sched_setaffinity; elsewhere the option is refused). The frequency governor is not touched. The CPU the run
//                   really was on, its part number and its frequency settings are printed. The engine's helper threads are moved to the other CPUs (see --no-helpers-away).
//   --rt N          run the measuring thread SCHED_FIFO with priority N (1 .. 99), as the host's audio thread would run: the block times then do not contain the time other
//                   normal threads took from the core. Needs root; without it the policy is refused, a line says so and the run goes on (--rt-require stops it instead).
//   --mlock         lock the memory of the process (mlockall), so a page fault cannot land in a block
//   --rt-gap MS     with --rt: after every 64 blocks sleep MS milliseconds outside the timed region, so the real-time throttling of the kernel cannot fire (default: the
//                   shortest gap that keeps the duty below 94 %, 12 ms for 128 frames at 44.1 kHz; 0 = no gap)
//   --rt-require    exit 1 when --rt could not be applied
//   --no-helpers-away  leave the engine's helper threads as they are inherited (with --core they are then pinned to the measured core too)
//   --pmu SET       read the hardware counters of the measuring thread around every block (perf_event_open, outside the timed region; bench/perf/perf_pmu.h): compute =
//                   cycles, instructions, branch mispredictions; cache = cycles, L1D refill / access, L2D refill / access. A line "pmu:" follows each run (or says why the
//                   counters are unavailable) and the csv gets the columns pmu_set .. pmu_scaling (scaling 1.000 = the counters ran the whole time)
//   --pmu-require   exit 1 when the counters could not be opened
//   --evict         cold caches: before every block, outside the timed region, a read-modify-write of a buffer of twice the largest cache level of the measured core
//                   (/sys/devices/system/cpu/cpuN/cache; 4 MiB without them) pushes the engine's working set out. The ratio of the evict run to the warm run is the
//                   cache sensitivity of the load. The csv column cold_mode says evict or warm.
//   --modules       no preset loads: the cost of every module on its own (bench/perf/perf_modules.h: every effect type, filter model, warp type, the morph, unison 3 / 7 / 16,
//                   Osc 2, Osc 3, Sub, Noise). Per profile and case: the base load "basic" and the case are run one after the other, --module-reps times, and the cost is the
//                   median over the repetitions of (median block time of the case - that of the base): no clock inside the engine. One line per case, then "module-table:".
//                   --seconds defaults to 2 here. With --rt a run that the health rules mark INVALID is repeated (--retry-invalid).
//   --module-group G  only the cases of one group: fx, filter, warp, morph, unison or osc (default all)
//   --module-reps N   repetitions of every case (default 3)
//   --no-start-cache     Quality::spectralStartCache off (default: by the profile, on at low).
//   --start-cache        Quality::spectralStartCache on: a note struck again takes its first Spectral frames from the process-wide start cache (docs/plans/spectral-start-cache.md).
//   --start-budget-us N  the budget of the Spectral start ledger in microseconds per block (Quality::spectralStartBudgetUs, N >= 1); without it the built-in default.
//   --first-blocks N  no steady run: for every profile and load a fresh engine, the time of prepare (), then the first N blocks (1 .. 64) with the chord struck in block 0 and no
//                   warm-up, each with its time and its minor page faults: the first-note and first-touch cost that a long run hides
//   --retry-invalid N  a run that the health rules mark INVALID (a stall, run queue delay, a frequency drop, a major page fault, heat) is repeated up to N times (default 2);
//                   the last run is kept and its validity is in the csv
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
//                     dist:T       the basic patch with the distortion on: type T, drive +18 dB, mix 100 (a held chord of the profile's polyphony: 8 notes at low)
//                     warp:T:A     the basic patch with the oscillator 1 warp of type T at amount A, unison 3 (with --sections: the cost of that warp per block)
//                     any other    a factory preset by its name ("Hyper Saw Lead") or file path
//   --no-aa MASK    switch the anti-aliasing changes OFF by their bits (core/include/synthyboy/aa_switch.h: 1 distortion ADAA, 2 warp ADAA, 4 BLEP, 8 BLAMP, 16 band limit,
//                   32 library warp ADAA; 63 = all; decimal or 0x hex), before the engines are made. The cost of a stage is the median with --no-aa 0 minus the median with
//                   --no-aa 63 in one binary.
//   --all-presets   every .sbpreset below DIR instead of the default loads (the table of cost per preset per profile)
//   --seconds S     measured audio per run (default 30; the first second of every run is played and not measured)
//   --repeat N      run the whole list N times, one pass after the other (default 1). The summary then takes, per preset, the MEDIAN of the passes' 99th
//                   percentiles and prints the lowest and highest pass next to it (the spread).
//   --budget PCT    add a verdict to the summary: a profile is "inside" when the 99th percentile of its dearest factory preset is at most PCT % of the block and
//                   no block of any of its factory presets, in any pass, was longer than its real time
//   --no-fx-mute    process an effect whose mix is exactly 0 all the same (the engine before that saving: for a before / after with one binary)
//   --scalar-filters  run the voice filters' scalar loops also where a vector kernel exists, and the chorus LFO refresh tap by tap (the same: before / after)
//   --scalar-warp   make the oversampled samples of a warped oscillator one by one also where the four lane form exists (the same: before / after)
//   --profile FILE  a sampling profile of the audio thread (a timer signal every 250 us on Linux ARM): the program counters, one per line, for scripts/bench-profile.py to map to functions
//   --tolerance X   with --verify: a sample may differ by X times the largest sample of the load (the vector forms of the Spectral oscillator differ by rounding, 3e-6 is the stated bound)
//   --scalar-content  the Sample / Granular / Spectral / Multisample oscillators run their per sample (per frame) reference code, not the block kernels (the same: before / after)
//   --no-voice-savings  the control code of the voices and the modulation matrix without the caches and skips of docs/release/CPU-REVIEW-VOICE-MOD.md (the same: before / after)
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
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
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

#include "synthyboy/aa_switch.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"
#include "synthyboy/granular/granular.h"
#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/sampleosc/sample_voice.h"
#include "synthyboy/spectral/kernels.h"
#include "synthyboy/spectral/spectral.h"

#include "device_profile.h"
#include "own_heavy_patch.h"
#include "perf/perf_env.h"
#include "perf/perf_modules.h"
#include "perf/perf_pmu.h"
#include "perf/perf_sched.h"
#include "perf/perf_stats.h"

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
    bool micro = false;      // --micro
    double tolerance = 0.0;  // --verify: a sample may differ by this share of the load's largest sample (default 0: the same values)
    std::string profilePath; // --profile
    uint32_t savingsOff = 0; // --no-fx-mute, --scalar-filters: OwnSaving bits switched off (before / after measurements with one binary)
    int rt = 0;              // --rt: SCHED_FIFO priority of the measuring thread, 0 = normal scheduling
    bool mlock = false;      // --mlock
    double rtGapMs = -1.0;   // --rt-gap: -1 = the shortest gap that keeps the real-time duty below 94 %
    bool rtRequire = false;  // --rt-require
    bool helpersAway = true; // --no-helpers-away clears it
    int retryInvalid = 2;    // --retry-invalid
    std::string pmu;         // --pmu: "" (off), "compute" or "cache"
    bool pmuRequire = false; // --pmu-require
    bool evict = false;      // --evict
    int firstBlocks = 0;     // --first-blocks
    int startBudgetUs = 0;   // --start-budget-us: Quality::spectralStartBudgetUs (0 = the default budget)
    int startCache = -1;     // --start-cache (1) / --no-start-cache (0), default by the profile (on at low): Quality::spectralStartCache (the process-wide cache of the Spectral start frames)
    bool modules = false;    // --modules
    int moduleReps = 3;      // --module-reps
    std::string moduleGroup = "all"; // --module-group
    bool secondsGiven = false;       // --seconds was on the command line (--modules runs 2 s without it)
    uint32_t noAa = 0;               // --no-aa: the bits of synthyboy::aa::Off that are switched off
    bool noAaGiven = false;
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
    enum Kind { worst, heavy, basic, preset, moduleCase, dist, warp } kind = basic;
    std::string name;
    std::string group; // "synthetic" or the category of the preset
    PresetData data;
    std::vector<std::pair<std::string, float>> params; // moduleCase, dist, warp: the parameters set on top of the defaults (perf/perf_modules.h)
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
        case Load::moduleCase:
        case Load::dist:
        case Load::warp:
            setIfKnown (engine, "master.polyphony", 32.0f);   // as basic
            for (const auto& p : load.params)
                setIfKnown (engine, p.first, p.second);
            break;
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
    std::vector<double> slowSectionMs; // the sections of the longest block (--sections)
    double slowBlockMs = 0.0;
    std::vector<double> sectionReads; // clock reads that land in the section's time per block (own scopes + scopes directly inside)
    // health of the run (perf/perf_stats.h: a run disturbed by the scheduler, the governor, page faults or heat is INVALID, never averaged in)
    double p999Ms = 0.0;
    double runDelayShare = 0.0;   // time the thread waited in a run queue / (that + the time it ran), over the whole run
    double threadCpuMs = 0.0;     // cpu time of the measuring thread over the whole run
    double sumMs = 0.0;           // the block times of the measured window added
    long nivcsw = 0;              // involuntary context switches of the thread over the whole run
    long minflt = 0, minfltMeasured = 0, majflt = 0; // minor page faults of the whole run and of the measured window, major faults of the measured window
    long rssKb = -1;
    double tempMaxC = std::numeric_limits<double>::quiet_NaN();
    long khzBefore = 0, khzAfter = 0;
    bool valid = true;
    std::string invalidReason;
    // the hardware counters (--pmu): per block means, scaled to the time the counters were enabled
    bool pmuOpened = false;
    std::string pmuWhy;                  // why they could not be opened
    std::vector<std::string> pmuNames;   // the events of the set, in the order of pmuPerBlock
    std::vector<double> pmuPerBlock;
    double pmuCyclesP99 = 0.0;
    double pmuScaling = 0.0;             // time the counters ran / time they were enabled
};

long currentKhz (int cpu);
std::string readLine (const std::string& path);

/** The per block mean of a counter of the run, NaN when the run has not that event. */
double pmuEvent (const Result& r, const char* name)
{
    for (size_t i = 0; i < r.pmuNames.size(); ++i)
        if (r.pmuNames[i] == name)
            return r.pmuPerBlock[i];
    return std::numeric_limits<double>::quiet_NaN();
}

/** A number as text, "n/a" (or "" for the csv) when it is not there. */
std::string pmuText (double v, const char* format, const char* absent)
{
    if (std::isnan (v))
        return absent;
    char text[48];
    std::snprintf (text, sizeof text, format, v);
    return text;
}

Result run (const Options& o, const synthyboy_mpc::Profile& profile, const Load& load, int notes, int cpu)
{
    auto engine = createEngine (EngineKind::own);
    applyLoad (*engine, load);
    setOwnSavingsOff (*engine, o.savingsOff);
    Quality quality = synthyboy_mpc::profileQuality (profile);
    quality.spectralStartBudgetUs = o.startBudgetUs;
    quality.spectralStartCache = o.startCache;
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
    perf::PmuReader pmu;
    std::string pmuWhy;
    const bool pmuOn = ! o.pmu.empty() && pmu.open (o.pmu == "cache" ? perf::PmuSet::cache : perf::PmuSet::compute, pmuWhy);
    perf::PmuSample pmuS0, pmuS1;
    std::vector<unsigned char> evictBuf;
    if (o.evict)
        evictBuf.assign (perf::evictBytes (perf::cacheLevelBytes (cpu)), 0);
    double pmuSum[8] = {};
    unsigned long long pmuEnabled = 0, pmuRunning = 0;
    long long pmuBlocks = 0;
    std::vector<double> pmuCycles;
    if (pmuOn)
        pmuCycles.reserve (static_cast<size_t> (measured));
    std::vector<double> sectionSum, sectionCalls, sectionReads, slowSections;
    double slowBlock = 0.0;
    double voiceSum = 0.0;
    int voiceMax = 0;
    float sink = 0.0f;
    const auto blockTime = std::chrono::duration<double> (o.block / o.sampleRate);
    // the gap after every perf::kRtGapEvery blocks of a real-time run (outside the timed region): the kernel throttles a real-time thread that is busy for 95 % of a second
    const double gapMs = o.rtGapMs >= 0.0 ? o.rtGapMs : std::ceil (perf::minGapMs (1000.0 * o.block / o.sampleRate, perf::kRtGapEvery, 0.95, 0.01));
    const perf::SchedStat sched0 = perf::readThreadSchedstat();
    const perf::Rusage rusage0 = perf::readRusage();
    perf::Rusage rusageWarm = rusage0;
    const double cpuMs0 = perf::threadCpuMs();
    const long khzBefore = currentKhz (cpu);
    const auto start = std::chrono::steady_clock::now();
    for (long long b = 0; b < warmup + measured; ++b)
    {
        if (b == warmup)
            rusageWarm = perf::readRusage();
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
        if (o.sections)
            ownSectionReset();   // per block: the totals are summed below (the same numbers as one reset at the start), and the longest block keeps its own
        if (o.evict)
            perf::evictCaches (evictBuf);   // before the timed region (and after the paced sleep)
        const bool pmuRead0 = pmuOn && pmu.read (pmuS0);   // the counters are read outside t0 .. t1
        const auto t0 = std::chrono::steady_clock::now();
        engine->process (events.data(), static_cast<int> (events.size()), left.data(), right.data(), o.block);
        const auto t1 = std::chrono::steady_clock::now();
        const bool pmuRead1 = pmuRead0 && pmu.read (pmuS1);
        if (o.sections && b >= warmup)
        {
            const int count = ownSectionCount();
            if (sectionSum.empty())
            {
                sectionSum.assign (static_cast<size_t> (count), 0.0);
                sectionCalls.assign (static_cast<size_t> (count), 0.0);
                sectionReads.assign (static_cast<size_t> (count), 0.0);
                slowSections.assign (static_cast<size_t> (count), 0.0);
            }
            const double thisBlock = std::chrono::duration<double> (t1 - t0).count();
            const bool slowest = thisBlock > slowBlock;
            if (slowest)
                slowBlock = thisBlock;
            for (int i = 0; i < count; ++i)
            {
                double total = 0.0;
                unsigned long long calls = 0;
                ownSectionRead (i, total, calls);
                sectionSum[static_cast<size_t> (i)] += total;
                sectionCalls[static_cast<size_t> (i)] += static_cast<double> (calls);
                sectionReads[static_cast<size_t> (i)] += static_cast<double> (calls + ownSectionChildCalls (i));
                if (slowest)
                    slowSections[static_cast<size_t> (i)] = total;
            }
        }
        sink += left[static_cast<size_t> (o.block / 2)];
        if (b >= warmup)
        {
            times.push_back (std::chrono::duration<double> (t1 - t0).count());
            const int av = engine->activeVoices();
            voiceSum += av;
            voiceMax = std::max (voiceMax, av);
            if (pmuRead1)
            {
                const unsigned long long de = pmuS1.timeEnabled - pmuS0.timeEnabled, dr = pmuS1.timeRunning - pmuS0.timeRunning;
                for (int i = 0; i < pmu.count(); ++i)
                    pmuSum[i] += static_cast<double> (pmuS1.value[i] - pmuS0.value[i]);
                pmuEnabled += de;
                pmuRunning += dr;
                ++pmuBlocks;
                pmuCycles.push_back (std::max (0.0, perf::scaledCount (pmuS1.value[0] - pmuS0.value[0], de, dr)));   // the first event of both sets is the cycle count
            }
        }
        if (o.rt > 0 && ! o.paced && gapMs > 0.0 && (b + 1) % perf::kRtGapEvery == 0)
            std::this_thread::sleep_for (std::chrono::duration<double, std::milli> (gapMs));
    }
    const perf::SchedStat sched1 = perf::readThreadSchedstat();
    const perf::Rusage rusage1 = perf::readRusage();
    const double cpuMs1 = perf::threadCpuMs();
    if (sink == 12345.678f)
        std::puts ("");

    Result r;
    r.blocks = static_cast<long long> (times.size());
    const double budget = o.block / o.sampleRate;
    double sum = 0.0;
    for (const double t : times)
        sum += t;
    const perf::Summary summary = perf::summarize (times, budget);   // the same indices as before: sorted[n / 2], sorted[floor (0.99 * (n - 1))]
    r.over = static_cast<long long> (summary.over);
    r.minMs = 1000.0 * summary.min;
    r.medianMs = 1000.0 * summary.median;
    r.p99Ms = 1000.0 * summary.p99;
    r.p999Ms = 1000.0 * summary.p999;
    r.maxMs = 1000.0 * summary.max;
    r.meanMs = 1000.0 * sum / static_cast<double> (times.size());
    r.sumMs = 1000.0 * sum;
    {
        perf::SchedStat delta;
        delta.ok = sched0.ok && sched1.ok;
        delta.runNs = sched1.runNs - sched0.runNs;
        delta.waitNs = sched1.waitNs - sched0.waitNs;
        r.runDelayShare = delta.ok ? perf::waitShare (delta) : 0.0;
        r.threadCpuMs = cpuMs1 - cpuMs0;
        r.nivcsw = rusage1.nivcsw - rusage0.nivcsw;
        r.minflt = rusage1.minflt - rusage0.minflt;
        r.minfltMeasured = rusage1.minflt - rusageWarm.minflt;
        r.majflt = rusage1.majflt - rusageWarm.majflt;
        r.rssKb = perf::readRssKb();
        r.tempMaxC = perf::maxThermalC();
        r.khzBefore = khzBefore;
        r.khzAfter = currentKhz (cpu);
        perf::RunHealth health;
        health.runDelayShare = r.runDelayShare;
        health.khzBefore = static_cast<double> (r.khzBefore);
        health.khzAfter = static_cast<double> (r.khzAfter);
        health.khzMax = cpu >= 0 ? static_cast<double> (std::atol (readLine ("/sys/devices/system/cpu/cpu" + std::to_string (cpu) + "/cpufreq/scaling_max_freq").c_str())) : 0.0;
        health.tempMaxC = std::isnan (r.tempMaxC) ? 0.0 : r.tempMaxC;
        health.maxMs = r.maxMs;
        health.budgetMs = 1000.0 * budget;
        health.majflt = r.majflt;
        r.invalidReason = perf::invalidReason (health);
        r.valid = r.invalidReason.empty();
    }
    r.voicesMean = voiceSum / static_cast<double> (times.size());
    r.voicesMax = voiceMax;
    if (! o.pmu.empty())
    {
        r.pmuOpened = pmuOn && pmuBlocks > 0;
        r.pmuWhy = pmuOn ? (pmuBlocks > 0 ? "" : "the counters could not be read") : pmuWhy;
        if (r.pmuOpened)
        {
            for (int i = 0; i < pmu.count(); ++i)
            {
                r.pmuNames.push_back (pmu.name (i));
                r.pmuPerBlock.push_back (perf::scaledCount (static_cast<unsigned long long> (pmuSum[i]), pmuEnabled, pmuRunning) / static_cast<double> (pmuBlocks));
            }
            std::sort (pmuCycles.begin(), pmuCycles.end());
            r.pmuCyclesP99 = perf::percentileSorted (pmuCycles, 0.99);
            r.pmuScaling = perf::ratio (static_cast<double> (pmuRunning), static_cast<double> (pmuEnabled));
        }
    }
    if (o.sections)
    {
        // the clock reads that land in a section: one of the two of each of its own scopes, and one of each scope entered directly inside it
        for (size_t i = 0; i < sectionSum.size(); ++i)
        {
            r.sectionMs.push_back (sectionSum[i] / static_cast<double> (measured));
            r.sectionCalls.push_back (sectionCalls[i] / static_cast<double> (measured));
            r.sectionReads.push_back (sectionReads[i] / static_cast<double> (measured));
            r.slowSectionMs.push_back (slowSections[i]);
        }
        r.slowBlockMs = 1000.0 * slowBlock;
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
    quality.spectralStartBudgetUs = o.startBudgetUs;
    quality.spectralStartCache = o.startCache;
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
                          "[--notes K] [--retrigger 1] [--paced] [--repeat N] [--budget PCT] [--verify] [--no-fx-mute] [--scalar-filters] [--scalar-warp] [--no-voice-savings] [--csv FILE] [--block 128] [--sr 44100] [--no-ftz] [--sections] [--list] "
                          "[--rt N] [--mlock] [--rt-gap MS] [--rt-require] [--no-helpers-away] [--retry-invalid N] "
                          "[--pmu compute|cache] [--pmu-require] [--evict] [--first-blocks N] [--start-budget-us N] [--start-cache] [--modules] [--module-group GROUP] [--module-reps N] [--no-aa MASK]\n", message);
    std::exit (2);
}
} // namespace

/** --micro: the content kernels one by one, in microseconds per call (the median of the repetitions), on this machine: where the time of a frame of the Spectral oscillator, of a block of
    grains or of a block of the sample voice goes. Pinned by --core like the rest. */
template <class F>
double microTime (const char* name, int reps, F f)
{
    std::vector<double> t;
    for (int i = 0; i < 10; ++i)
        f();
    for (int i = 0; i < reps; ++i)
    {
        const auto a = std::chrono::steady_clock::now();
        f();
        const auto c = std::chrono::steady_clock::now();
        t.push_back (std::chrono::duration<double, std::micro> (c - a).count());
    }
    std::sort (t.begin(), t.end());
    std::printf ("%-44s %9.2f us   (min %.2f)\n", name, t[t.size() / 2], t.front());
    std::fflush (stdout);
    return t[t.size() / 2];
}

void runMicro()
{
    using namespace synthyboy::spectral;
    namespace k = synthyboy::spectral::kernels;
    std::vector<float> a (kBins), b (kBins), c (kBins), d (kBins), xr (kBins), xi (kBins), zr (kFftSize / 2), zi (kFftSize / 2), ring (kFftSize), phase (kBins), step (kBins);
    uint32_t s = 12345u;
    const auto rnd = [&] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return static_cast<float> (s >> 8) * (1.0f / 16777216.0f); };
    for (int i = 0; i < kBins; ++i)
    {
        a[static_cast<size_t> (i)] = (rnd() - 0.5f) * 20.0f;
        b[static_cast<size_t> (i)] = rnd();
        xr[static_cast<size_t> (i)] = rnd() - 0.5f;
        xi[static_cast<size_t> (i)] = i == 0 || i == kBins - 1 ? 0.0f : rnd() - 0.5f;
        phase[static_cast<size_t> (i)] = (rnd() - 0.5f) * 6.0f;
        step[static_cast<size_t> (i)] = rnd() * 3.0f;
    }
    int peaks[512];
    const int reps = 400;
    std::printf ("spectral kernels, one frame of %d bins (kernels.h; vector forms where the CPU has them: %s)\n", kBins, k::haveNeon() ? "yes" : "no");
    microTime ("sincosBlock", reps, [&] { k::sincosBlock (a.data(), c.data(), d.data(), kBins, true); });
    microTime ("sincosBlock scalar", reps, [&] { k::sincosBlock (a.data(), c.data(), d.data(), kBins, false); });
    microTime ("RealIfft::inverse", reps, [&] { k::RealIfft::instance().inverse (xr.data(), xi.data(), zr.data(), zi.data(), true); });
    microTime ("RealIfft::inverse scalar", reps, [&] { k::RealIfft::instance().inverse (xr.data(), xi.data(), zr.data(), zi.data(), false); });
    microTime ("Fft::inverse (the full length, the reference)", reps, [&] { std::vector<float> re (kFftSize, 0.1f), im (kFftSize, 0.2f); Fft::instance().inverse (re.data(), im.data()); });
    microTime ("overlapAdd", reps, [&] { k::overlapAdd (ring.data(), 512, zr.data(), zi.data(), 0.66f, true); });
    microTime ("advancePhases", reps, [&] { k::advancePhases (phase.data(), step.data(), kBins, true); });
    microTime ("advancePhases scalar", reps, [&] { k::advancePhases (phase.data(), step.data(), kBins, false); });
    microTime ("wrapPhase (double) 1025 values", reps, [&] { float t = 0; for (int i = 0; i < kBins; ++i) t += k::wrapPhase (phase[static_cast<size_t> (i)] * 100.0f); phase[0] = phase[0] + t * 1.0e-20f; });
    microTime ("findPeaks", reps, [&] { k::findPeaks (b.data(), kBins, 1.0f, peaks, 512, true); });
    microTime ("findPeaks scalar", reps, [&] { k::findPeaks (b.data(), kBins, 1.0f, peaks, 512, false); });
    microTime ("maxBins", reps, [&] { volatile float t = k::maxBins (b.data(), kBins, true); (void) t; });
    microTime ("lerpBins", reps, [&] { k::lerpBins (c.data(), a.data(), b.data(), 0.3f, kBins, true); });
    microTime ("mulBins", reps, [&] { k::mulBins (c.data(), b.data(), kBins, true); });
    microTime ("placeRegion x 32 regions of 30 bins", reps, [&] { for (int r = 0; r < 32; ++r) k::placeRegion (a.data(), b.data(), c.data(), d.data(), xr.data(), xi.data(), 1 + r * 30, 30 + r * 30, 3, 1.1f, 0.0f, kBins, true); });
    microTime ("memcpy 4 KB", reps, [&] { std::memcpy (c.data(), a.data(), sizeof (float) * kBins); });
    microTime ("memset 4 KB", reps, [&] { std::memset (c.data(), 0, sizeof (float) * kBins); });
    microTime ("a loop of 1025 scalar mul + add", reps, [&] { for (int i = 0; i < kBins; ++i) c[static_cast<size_t> (i)] = a[static_cast<size_t> (i)] * b[static_cast<size_t> (i)] + 1.0f; });

    // a Spectral voice on a synthetic sample: the four frames of a note start, and a hop
    std::vector<float> tone (66150);
    for (size_t i = 0; i < tone.size(); ++i)
    {
        float v = 0.0f;
        for (int h = 1; h <= 20; ++h)
            v += 0.3f / static_cast<float> (h) * static_cast<float> (std::sin (6.2831853 * 220.0 * h * static_cast<double> (i) / 44100.0 + 0.3 * h));
        tone[i] = v + 0.03f * (static_cast<float> (rand()) / static_cast<float> (RAND_MAX) - 0.5f);   // a noise floor: the spectrum has a peak every few bins, as a recorded sample does
    }
    synthyboy::sampleosc::SampleData data;
    data.loadFromFrames (tone.data(), nullptr, static_cast<int64_t> (tone.size()), 44100.0);
    static Scratch scratch;
    Analysis analysis;
    analysis.build (data.view());
    for (int variant = 0; variant < 2; ++variant)
    {
        SpectralParams p;
        p.scratch = &scratch;
        p.hostRate = 44100.0;
        p.manual = true;
        p.position = 0.4f;
        p.pitchRatio = 1.3f;
        if (variant == 1)
        {
            p.warpType[0] = static_cast<int> (Warp::addHarmonics);
            p.warpAmount[0] = 0.6f;
        }
        for (int reference = 0; reference < 2; ++reference)
        {
            SpectralVoice v;
            v.prepare (44100.0);
            v.setReferenceKernels (reference != 0);
            char name[96];
            std::snprintf (name, sizeof name, "SpectralVoice::noteOn (4 frames)%s%s", variant == 1 ? " + Add Harmonics" : "", reference != 0 ? " reference" : "");
            microTime (name, 100, [&] { v.noteOn (&analysis, p, 0.0f); });
            std::snprintf (name, sizeof name, "SpectralVoice::process 512 (1 frame)%s%s", variant == 1 ? " + Add Harmonics" : "", reference != 0 ? " reference" : "");
            float l[512], r[512];
            microTime (name, 100, [&] { v.process (p, l, r, 512); });
        }
    }

    // a chord struck: eight Spectral voices started one after the other, warm and with the caches emptied before (what a strike after a second of other work finds)
    {
        std::vector<synthyboy::spectral::SpectralVoice> voices (8);
        SpectralParams p;
        p.scratch = &scratch;
        p.hostRate = 44100.0;
        p.manual = true;
        p.position = 0.4f;
        for (auto& v : voices)
            v.prepare (44100.0);
        std::vector<char> flush (16u << 20, 1);
        volatile char sink = 0;
        for (int cold = 0; cold < 2; ++cold)
            for (int reference = 0; reference < 2; ++reference)
            {
                for (auto& v : voices)
                    v.setReferenceKernels (reference != 0);
                std::vector<double> t;
                for (int rep = 0; rep < 40; ++rep)
                {
                    if (cold != 0)
                        for (size_t i = 0; i < flush.size(); i += 32)
                            sink = static_cast<char> (sink + flush[i]++);
                    const auto a = std::chrono::steady_clock::now();
                    for (size_t i = 0; i < voices.size(); ++i)
                    {
                        p.pitchRatio = 0.7f + 0.15f * static_cast<float> (i);
                        voices[i].noteOn (&analysis, p, 0.0f);
                    }
                    const auto c = std::chrono::steady_clock::now();
                    t.push_back (std::chrono::duration<double, std::milli> (c - a).count());
                }
                std::sort (t.begin(), t.end());
                std::printf ("8 x SpectralVoice::noteOn %s%s %9.3f ms   (min %.3f)\n", cold != 0 ? "after the caches were emptied" : "warm", reference != 0 ? ", reference" : "", t[t.size() / 2], t.front());
            }
    }

    // a cloud of grains: the plain read, 14 grains alive
    {
        std::printf ("granular\n");
        synthyboy::granular::GrainParams gp;
        gp.manual = true;
        gp.position = 0.3f;
        gp.densityHz = 42.0f;
        gp.lengthSec = 0.11f * 3.0f;
        gp.maxGrains = 16;
        gp.randPan = 0.5f;
        gp.pitchRatio = 1.2f;
        const auto view = data.view();
        for (int mode = 0; mode < 3; ++mode)
        {
            synthyboy::granular::GranularVoice g;
            g.prepare (44100.0);
            g.setReferenceKernels (mode == 2);
            g.setVectorKernels (mode == 0);
            g.noteOn (&view, gp, 7u, 0.0f, true, 0.0f);
            float l[128], r[128];
            for (int i = 0; i < 100; ++i)
                g.process (gp, l, r, 128, nullptr);
            char name[96];
            std::snprintf (name, sizeof name, "GranularVoice::process 128 (%d grains) %s", g.activeGrains(), mode == 0 ? "vector kernel" : (mode == 1 ? "scalar kernel" : "reference"));
            microTime (name, 600, [&] { g.process (gp, l, r, 128, nullptr); });
            std::snprintf (name, sizeof name, "GranularVoice::process 4 x 32 (control periods)  %s", mode == 0 ? "vector kernel" : (mode == 1 ? "scalar kernel" : "reference"));
            microTime (name, 600, [&] { for (int q = 0; q < 4; ++q) g.process (gp, l, r, 32, nullptr); });
        }
    }
    // the sample voice: a looped read with a crossfade, a pitch up (a mip level), 128 samples in four control periods of 32
    {
        std::printf ("sample voice\n");
        const auto view = data.view();
        for (int mode = 0; mode < 3; ++mode)
        {
            synthyboy::sampleosc::SampleVoice sv;
            sv.prepare (44100.0, 128);
            sv.setReferenceKernels (mode == 2);
            sv.setVectorKernels (mode == 0);
            synthyboy::sampleosc::SampleOscParams sp;
            sp.loopMode = synthyboy::sampleosc::LoopMode::forward;
            sp.loopStart = 0.14f;
            sp.loopEnd = 0.78f;
            sp.loopCrossfade = 0.0125f;
            sp.quality = synthyboy::sampleosc::QualityTier::low;
            sv.noteOn (64, 0.9f, &view, sp);
            sv.setSemitoneOffset (7.0f);
            float l[128], r[128];
            for (int i = 0; i < 300; ++i)
                sv.process (l, r, 128);
            char name[96];
            std::snprintf (name, sizeof name, "SampleVoice::process 128 %s", mode == 0 ? "vector kernel" : (mode == 1 ? "scalar kernel" : "reference"));
            microTime (name, 600, [&] { sv.process (l, r, 128); });
            std::snprintf (name, sizeof name, "SampleVoice::process 4 x 32 %s", mode == 0 ? "vector kernel" : (mode == 1 ? "scalar kernel" : "reference"));
            microTime (name, 600, [&] { for (int q = 0; q < 4; ++q) sv.process (l + 32 * q, r + 32 * q, 32); });
        }
    }
}

namespace
{
/** --first-blocks: a fresh engine as run () makes it, the time of prepare (), then the first `o.firstBlocks` blocks with the chord struck in block 0 and no warm-up: time, share of the
    block and the minor page faults of every block (the first touch of the engine's memory, the first note). */
void runFirstBlocks (const Options& o, const synthyboy_mpc::Profile& profile, const Load& load, int notes)
{
    auto engine = createEngine (EngineKind::own);
    applyLoad (*engine, load);
    setOwnSavingsOff (*engine, o.savingsOff);
    Quality quality = synthyboy_mpc::profileQuality (profile);
    quality.spectralStartBudgetUs = o.startBudgetUs;
    quality.spectralStartCache = o.startCache;
    quality.realtime = true;
    const auto p0 = std::chrono::steady_clock::now();
    engine->prepare (o.sampleRate, o.block, quality);
    const auto p1 = std::chrono::steady_clock::now();
    std::printf ("first: prepare %.1f ms\n", 1000.0 * std::chrono::duration<double> (p1 - p0).count());
    std::vector<float> left (static_cast<size_t> (o.block)), right (static_cast<size_t> (o.block));
    std::vector<Event> events;
    events.reserve (static_cast<size_t> (2 * notes + 2));
    const double budgetMs = 1000.0 * o.block / o.sampleRate;
    float sink = 0.0f;
    for (int b = 0; b < o.firstBlocks; ++b)
    {
        events.clear();
        if (b == 0)
        {
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
        const perf::Rusage before = perf::readRusage();
        const auto t0 = std::chrono::steady_clock::now();
        engine->process (events.data(), static_cast<int> (events.size()), left.data(), right.data(), o.block);
        const auto t1 = std::chrono::steady_clock::now();
        const perf::Rusage after = perf::readRusage();
        sink += left[static_cast<size_t> (o.block / 2)];
        const double ms = 1000.0 * std::chrono::duration<double> (t1 - t0).count();
        std::printf ("first: block %d %.3f ms (%.1f %%) minflt %ld\n", b, ms, 100.0 * ms / budgetMs, after.minflt - before.minflt);
    }
    if (sink == 12345.678f)
        std::puts ("");
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
        else if (a == "--seconds")     { o.seconds = std::atof (next()); o.secondsGiven = true; }
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
        else if (a == "--micro")       o.micro = true;
        else if (a == "--tolerance")   o.tolerance = std::atof (next());
        else if (a == "--profile")     o.profilePath = next();
        else if (a == "--no-fx-mute")  o.savingsOff |= kOwnSavingFxMute;
        else if (a == "--scalar-filters") o.savingsOff |= kOwnSavingVectorFilters;
        else if (a == "--scalar-warp") o.savingsOff |= kOwnSavingVectorWarp;
        else if (a == "--scalar-content") o.savingsOff |= kOwnSavingContentKernels;
        else if (a == "--no-voice-savings") o.savingsOff |= kOwnSavingVoiceControl;
        else if (a == "--budget")      o.budget = std::atof (next());
        else if (a == "--rt")          o.rt = std::atoi (next());
        else if (a == "--mlock")       o.mlock = true;
        else if (a == "--rt-gap")      o.rtGapMs = std::atof (next());
        else if (a == "--rt-require")  o.rtRequire = true;
        else if (a == "--no-helpers-away") o.helpersAway = false;
        else if (a == "--retry-invalid") o.retryInvalid = std::atoi (next());
        else if (a == "--pmu")         o.pmu = next();
        else if (a == "--pmu-require") o.pmuRequire = true;
        else if (a == "--evict")       o.evict = true;
        else if (a == "--modules")     o.modules = true;
        else if (a == "--no-aa")
        {
            char* end = nullptr;
            const char* text = next();
            const unsigned long mask = std::strtoul (text, &end, 0);
            if (end == text || *end != '\0' || mask > static_cast<unsigned long> (synthyboy::aa::kAll))
                usage ("--no-aa needs a mask 0 .. 63");
            o.noAa = static_cast<uint32_t> (mask);
            o.noAaGiven = true;
        }
        else if (a == "--module-group") o.moduleGroup = next();
        else if (a == "--module-reps") o.moduleReps = std::atoi (next());
        else if (a == "--first-blocks") o.firstBlocks = std::atoi (next());
        else if (a == "--start-cache") o.startCache = 1;
        else if (a == "--no-start-cache") o.startCache = 0;
        else if (a == "--start-budget-us") { o.startBudgetUs = std::atoi (next()); if (o.startBudgetUs < 1) usage ("--start-budget-us needs N >= 1"); }
        else usage (("unknown argument " + a).c_str());
    }
    if (o.seconds <= 0.0 || o.block < 1 || o.block > 8192 || o.notes < 0 || o.notes > kMaxVoices || o.sampleRate < 8000.0 || o.retrigger <= 0.0 || o.repeat < 1 || o.repeat > 99 || o.budget < 0.0
        || (o.rt != 0 && (o.rt < 1 || o.rt > 99)) || o.rtGapMs < -1.0 || o.retryInvalid < 0 || o.retryInvalid > 9
        || (! o.pmu.empty() && o.pmu != "compute" && o.pmu != "cache") || o.firstBlocks < 0 || o.firstBlocks > 64
        || o.moduleReps < 1 || o.moduleReps > 99 || (o.moduleGroup != "all" && o.moduleGroup != "fx" && o.moduleGroup != "filter" && o.moduleGroup != "warp" && o.moduleGroup != "morph"
                                                      && o.moduleGroup != "unison" && o.moduleGroup != "osc"))
        usage ("argument out of range");
    if (o.modules && ! o.secondsGiven)
        o.seconds = 2.0;
    synthyboy::aa::setOff (o.noAa);   // before any engine is made
    if (o.micro)
    {
#if defined(__linux__)
        if (o.core >= 0)
        {
            cpu_set_t set;
            CPU_ZERO (&set);
            CPU_SET (static_cast<unsigned> (o.core), &set);
            if (sched_setaffinity (0, sizeof set, &set) != 0)
            {
                std::perror ("tier_bench: sched_setaffinity");
                return 1;
            }
        }
#endif
        if (o.ftz)
            flushDenormalsOn();
        int cpu0 = -1;
        const std::string cpuText0 = describeCpu (cpu0);
        std::printf ("%s\n", cpuText0.c_str());
        runMicro();
        return 0;
    }
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
        // dist:<type> and warp:<type>:<amount>, the synthetic loads of the anti-aliasing plan (docs/plans/distortion-antialiasing.md, A3)
        if (name.rfind ("dist:", 0) == 0 || name.rfind ("warp:", 0) == 0)
        {
            const bool isDist = name[0] == 'd';
            int type = 0;
            float amount = 0.0f;
            char tail = 0;
            const char* text = name.c_str() + 5;
            const int got = isDist ? std::sscanf (text, "%d%c", &type, &tail) : std::sscanf (text, "%d:%f%c", &type, &amount, &tail);
            if (got != (isDist ? 1 : 2) || type < 0)
            {
                if (required)
                    usage (("load '" + name + "': expected dist:<type> or warp:<type>:<amount>").c_str());
                return false;
            }
            l.kind = isDist ? Load::dist : Load::warp;
            l.name = name;
            l.group = "synthetic";
            if (isDist)
                l.params = { { "distortion.on", 1.0f }, { "distortion.type", static_cast<float> (type) }, { "distortion.drive", 18.0f }, { "distortion.mix", 100.0f } };
            else
                l.params = { { "osc1.warp.type", static_cast<float> (type) }, { "osc1.warp.amount", amount }, { "osc1.unison.voices", 3.0f } };
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
    if (loads.empty() && ! o.modules)
    {
        addNamed ("worst", true);
        for (const char* n : kHeavy)
            addNamed (n, false);
        for (const char* n : kTypical)
            addNamed (n, false);
    }

    // ---- the CPU and the scheduling ----
#if ! defined(__linux__)
    if (o.core >= 0)
    {
        std::fprintf (stderr, "tier_bench: --core needs Linux (sched_setaffinity); run without it here\n");
        return 2;
    }
#endif
    if (o.core >= 0 && o.helpersAway)
    {
        // The helper threads of the engine take the mask of their creator unless the start hook moves them; the wavetable builder thread calls no hook, so it is born in the
        // first prepare () with the mask of this thread. So: the hook, then this thread gets the helper mask, a throw-away engine starts that builder thread, and only then
        // this thread pins itself to the measured core.
        perf::installHelperPolicyHook (o.core);
#if defined(__linux__)
        const std::vector<int> helpers = perf::helperCpus (perf::parseCpuList (perf::readTextFile ("/sys/devices/system/cpu/online")), o.core);
        cpu_set_t helperSet;
        CPU_ZERO (&helperSet);
        for (const int c : helpers)
            if (c >= 0 && c < CPU_SETSIZE)
                CPU_SET (static_cast<unsigned> (c), &helperSet);
        (void) sched_setaffinity (0, sizeof helperSet, &helperSet);
        if (! profiles.empty())
        {
            auto warmEngine = createEngine (EngineKind::own);
            Quality warmQuality = synthyboy_mpc::profileQuality (profiles[0]);
            warmQuality.realtime = true;
            warmQuality.spectralStartBudgetUs = o.startBudgetUs;
            warmQuality.spectralStartCache = o.startCache;
            warmEngine->prepare (o.sampleRate, o.block, warmQuality);
        }
        std::string helperText;
        for (const int c : helpers)
            helperText += (helperText.empty() ? "" : ",") + std::to_string (c);
        std::printf ("helpers: engine threads on cpus %s (measured cpu %d)\n", helperText.c_str(), o.core);
#endif
    }
    else if (o.core >= 0)
        std::printf ("helpers: left as inherited\n");
    if (o.core >= 0 || o.rt > 0 || o.mlock)
    {
        const perf::SchedResult sched = perf::applySched ({ o.core, o.rt, o.mlock });
        std::printf ("%s\n", sched.text.c_str());
        if (o.core >= 0 && ! sched.affinityOk)
        {
            std::fprintf (stderr, "tier_bench: sched_setaffinity: %s\n", std::strerror (sched.err));
            return 1;
        }
        if (o.rt > 0 && ! sched.fifoOk && o.rtRequire)
        {
            std::fprintf (stderr, "tier_bench: --rt-require: the real-time policy was refused\n");
            return 1;
        }
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
    if (o.noAaGiven)
        std::printf ("anti-aliasing switch: changes with these bits are OFF: %u (core/include/synthyboy/aa_switch.h; 0 = all on, 63 = all off)\n", static_cast<unsigned> (o.noAa));
    if ((o.savingsOff & kOwnSavingVectorWarp) != 0)
        std::printf ("scalar warp samples (--scalar-warp)\n");
    if ((o.savingsOff & kOwnSavingContentKernels) != 0)
        std::printf ("reference code of the Sample / Granular / Spectral / Multisample oscillators (--scalar-content)\n");
    if ((o.savingsOff & kOwnSavingVoiceControl) != 0)
        std::printf ("voice control and modulation matrix without their caches and skips (--no-voice-savings)\n");
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
        if (o.startBudgetUs > 0)
            std::printf ("spectral start budget: %d us\n", o.startBudgetUs);
        for (const auto& l : loads)
            std::printf ("load    %-40s (%s)\n", l.name.c_str(), l.group.c_str());
        std::printf ("%zu runs, %.0f s of audio (a run takes its audio time multiplied by the load it measures)\n", configs.size() * loads.size() * static_cast<size_t> (o.repeat),
                     static_cast<double> (configs.size() * loads.size()) * (o.seconds + 1.0) * o.repeat);
        return 0;
    }

    if (o.verify)
    {
        std::printf ("\nverify: every load under every profile, with the savings and without (filters one side after the other, effects with mix 0 processed, warp samples one by one, voice control without its caches): the samples must be the same\n");
        int failures = 0, runs = 0;
        for (size_t c = 0; c < configs.size(); ++c)
        {
            const int notes = o.notes > 0 ? o.notes : profiles[c].maxVoices;
            int bad = 0;
            for (const auto& l : loads)
            {
                const std::vector<float> with = renderForVerify (o, profiles[c], l, notes, 0), without = renderForVerify (o, profiles[c], l, notes, kOwnSavingFxMute | kOwnSavingVectorFilters | kOwnSavingVectorWarp | kOwnSavingContentKernels
                                                                                                                          | kOwnSavingVoiceControl);
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

    if (o.modules)
    {
        // the base ("basic") and the case, one after the other, moduleReps times; cost = the median over the repetitions of (median block time of the case - that of the base)
        Load base;
        base.kind = Load::basic;
        base.name = "basic";
        base.group = "synthetic";
        const double budgetUs = 1.0e6 * o.block / o.sampleRate;
        auto median = [] (std::vector<double> v) { std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
        auto runGated = [&] (const synthyboy_mpc::Profile& profile, const Load& l, int notes)
        {
            Result r;
            for (int attempt = 0;; ++attempt)
            {
                r = run (o, profile, l, notes, cpu);
                if (o.rt <= 0 || r.valid || attempt >= o.retryInvalid)
                    break;
                std::printf ("    retry %d: %s\n", attempt + 1, r.invalidReason.c_str());
            }
            return r;
        };
        const std::vector<perf::ModuleCase> cases = perf::moduleCases();
        size_t printed = 0;
        for (size_t c = 0; c < configs.size(); ++c)
        {
            const int notes = o.notes > 0 ? o.notes : profiles[c].maxVoices;
            std::printf ("\nmodule costs under %s with %d notes (%.1f s per run, %d repetition%s)\n", configs[c].c_str(), notes, o.seconds, o.moduleReps, o.moduleReps == 1 ? "" : "s");
            for (const auto& mc : cases)
            {
                if (o.moduleGroup != "all" && mc.group != o.moduleGroup)
                    continue;
                Load l;
                l.kind = Load::moduleCase;
                l.name = mc.name;
                l.group = mc.group;
                l.params = mc.params;
                std::vector<double> baseUs, caseUs, costUs;
                for (int rep = 0; rep < o.moduleReps; ++rep)
                {
                    const double b = 1000.0 * runGated (profiles[c], base, notes).medianMs;
                    const double m = 1000.0 * runGated (profiles[c], l, notes).medianMs;
                    baseUs.push_back (b);
                    caseUs.push_back (m);
                    costUs.push_back (m - b);
                }
                const double baseMed = median (baseUs), caseMed = median (caseUs), cost = median (costUs);
                const double spread = baseMed > 0.0 ? 100.0 * (*std::max_element (costUs.begin(), costUs.end()) - *std::min_element (costUs.begin(), costUs.end())) / baseMed : 0.0;
                std::printf ("module %s | %s | base %.2f us | case %.2f us | cost %+.2f us | %+.2f %% of block | reps %d | spread %.1f %%\n", mc.group.c_str(), mc.name.c_str(), baseMed, caseMed,
                             cost, 100.0 * cost / budgetUs, o.moduleReps, spread);
                std::fflush (stdout);
                ++printed;
            }
        }
        std::printf ("module-table: %zu cases\n", printed);
        return 0;
    }

    if (o.firstBlocks > 0)
    {
        std::printf ("\nfirst blocks: a fresh engine per profile and load, the chord struck in block 0, no warm-up; budget %.3f ms per block\n", budgetMs);
        for (size_t c = 0; c < configs.size(); ++c)
        {
            const int notes = o.notes > 0 ? o.notes : profiles[c].maxVoices;
            for (const auto& l : loads)
            {
                std::printf ("%-58s %-24.24s %5d notes\n", configs[c].c_str(), l.name.c_str(), notes);
                runFirstBlocks (o, profiles[c], l, notes);
                std::fflush (stdout);
            }
        }
        return 0;
    }

    FILE* csv = o.csv.empty() ? nullptr : std::fopen (o.csv.c_str(), "w");
    if (! o.csv.empty() && csv == nullptr)
    {
        std::perror ("tier_bench: --csv");
        return 1;
    }
    if (csv != nullptr)
        std::fprintf (csv, "device,cpu,cpu_info,profile,settings,load,group,notes,voices_mean,voices_max,sr,block,blocks,min_pct,median_pct,p99_pct,max_pct,mean_pct,min_ms,median_ms,p99_ms,max_ms,mean_ms,blocks_over_100pct,cpu_khz_after_run,pass"
                          ",p999_pct,rt_prio,thread_cpu_ms,run_delay_pct,nivcsw,minflt_run,minflt_measured,majflt,rss_kb,temp_c_max,khz_before,valid,invalid_reason"
                          ",pmu_set,cycles_mean,cycles_p99,instr_mean,ipc,l1d_refill_per_block,l1d_access_per_block,l2d_refill_per_block,l2d_access_per_block,br_mis_per_block,pmu_scaling,cold_mode,first_block_ms\n");

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
                const double k = 100.0 / budgetMs;
                Result r;
                long khz = 0;
                for (int attempt = 0;; ++attempt)
                {
                    r = run (o, profiles[c], l, notes, cpu);
                    khz = currentKhz (cpu);
                    std::printf ("%-58s %-24.24s %5d %6.1f | %7.2f %7.2f %7.2f %7.2f %7.2f | %5lld %8ld %4d\n", configs[c].c_str(), l.name.c_str(), notes, r.voicesMean, r.minMs * k, r.medianMs * k,
                                 r.p99Ms * k, r.maxMs * k, r.meanMs * k, r.over, khz / 1000, pass);
                    char temp[24] = "n/a";
                    if (! std::isnan (r.tempMaxC))
                        std::snprintf (temp, sizeof temp, "%.1f", r.tempMaxC);
                    const std::string verdict = r.valid ? std::string ("valid") : "INVALID (" + r.invalidReason + ")";
                    std::printf ("    health: run_delay %.4f %%, thread cpu %.1f ms of %.1f ms, nivcsw %ld, minflt %ld (measured %ld), majflt %ld, rss %ld kB, temp %s C, cpu %ld -> %ld kHz, %s\n",
                                 100.0 * r.runDelayShare, r.threadCpuMs, r.sumMs, r.nivcsw, r.minflt, r.minfltMeasured, r.majflt, r.rssKb, temp, r.khzBefore, r.khzAfter, verdict.c_str());
                    if (! o.pmu.empty())
                    {
                        if (r.pmuOpened)
                        {
                            const double cyc = pmuEvent (r, "cycles"), ins = pmuEvent (r, "instructions"), l1r = pmuEvent (r, "l1d_refill"), l1a = pmuEvent (r, "l1d_access");
                            const double l2r = pmuEvent (r, "l2d_refill"), l2a = pmuEvent (r, "l2d_access"), brm = pmuEvent (r, "br_mis_pred");
                            const double ipc = std::isnan (ins) ? ins : perf::ratio (ins, cyc);
                            const double l1pct = std::isnan (l1r) ? l1r : 100.0 * perf::ratio (l1r, l1a), l2pct = std::isnan (l2r) ? l2r : 100.0 * perf::ratio (l2r, l2a);
                            std::printf ("    pmu: set %s cycles %s/block (p99 %s), ipc %s, l1d refill %s/block (%s %%), l2d refill %s/block (%s %%), br_mis %s/block, scaling %.3f\n", o.pmu.c_str(),
                                         pmuText (cyc, "%.0f", "n/a").c_str(), pmuText (r.pmuCyclesP99, "%.0f", "n/a").c_str(), pmuText (ipc, "%.2f", "n/a").c_str(), pmuText (l1r, "%.1f", "n/a").c_str(),
                                         pmuText (l1pct, "%.2f", "n/a").c_str(), pmuText (l2r, "%.1f", "n/a").c_str(), pmuText (l2pct, "%.2f", "n/a").c_str(), pmuText (brm, "%.1f", "n/a").c_str(), r.pmuScaling);
                        }
                        else
                            std::printf ("    pmu: unavailable (%s)\n", r.pmuWhy.c_str());
                    }
                    std::fflush (stdout);
                    if (! o.pmu.empty() && ! r.pmuOpened && o.pmuRequire)
                    {
                        std::fprintf (stderr, "tier_bench: --pmu-require: %s\n", r.pmuWhy.c_str());
                        return 1;
                    }
                    if (r.valid || attempt >= o.retryInvalid)
                        break;
                    std::printf ("    retry %d: %s\n", attempt + 1, r.invalidReason.c_str());
                }
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
                    // the longest block of the run (with the clock of its scopes in it: a little longer than the plain run's longest): where its time went
                    std::printf ("    the longest block took %.4f ms; its sections (ms):", r.slowBlockMs);
                    std::vector<std::pair<double, size_t>> order;
                    for (size_t i = 0; i < r.slowSectionMs.size(); ++i)
                        if (r.slowSectionMs[i] > 0.0)
                            order.push_back ({ r.slowSectionMs[i], i });
                    std::sort (order.begin(), order.end(), [] (const auto& x, const auto& y) { return x.first > y.first; });
                    for (size_t q = 0; q < order.size() && q < 6; ++q)
                        std::printf ("  %s %.4f;", ownSectionName (static_cast<int> (order[q].second)), order[q].first);
                    std::printf ("\n");
                }
                if (csv != nullptr)
                {
                    char tempText[24] = "";
                    if (! std::isnan (r.tempMaxC))
                        std::snprintf (tempText, sizeof tempText, "%.1f", r.tempMaxC);
                    // the T15 columns, empty when the counters are off, could not be opened or the set has not the event
                    std::string pmuCsv = ",,,,,,,,,,";
                    if (r.pmuOpened)
                    {
                        const double cyc = pmuEvent (r, "cycles"), ins = pmuEvent (r, "instructions"), l1r = pmuEvent (r, "l1d_refill"), l1a = pmuEvent (r, "l1d_access");
                        const double l2r = pmuEvent (r, "l2d_refill"), l2a = pmuEvent (r, "l2d_access"), brm = pmuEvent (r, "br_mis_pred");
                        pmuCsv = o.pmu + "," + pmuText (cyc, "%.1f", "") + "," + pmuText (r.pmuCyclesP99, "%.1f", "") + "," + pmuText (ins, "%.1f", "") + ","
                                 + pmuText (std::isnan (ins) ? ins : perf::ratio (ins, cyc), "%.3f", "") + "," + pmuText (l1r, "%.2f", "") + "," + pmuText (l1a, "%.2f", "") + "," + pmuText (l2r, "%.2f", "") + ","
                                 + pmuText (l2a, "%.2f", "") + "," + pmuText (brm, "%.2f", "") + "," + pmuText (r.pmuScaling, "%.4f", "");
                    }
                    std::fprintf (csv, "\"%s\",%d,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%d,%.2f,%d,%.0f,%d,%lld,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%lld,%ld,%d,%.3f,%d,%.1f,%.4f,%ld,%ld,%ld,%ld,%ld,%s,%ld,%d,\"%s\",%s,%s,\n", model.c_str(), cpu,
                                  cpuText.c_str(), configs[c].c_str(), synthyboy_mpc::describeProfile (profiles[c]).c_str(), l.name.c_str(), l.group.c_str(), notes, r.voicesMean, r.voicesMax,
                                  o.sampleRate, o.block, r.blocks, r.minMs * k, r.medianMs * k, r.p99Ms * k, r.maxMs * k, r.meanMs * k, r.minMs, r.medianMs, r.p99Ms, r.maxMs, r.meanMs, r.over, khz, pass, r.p999Ms * k, o.rt, r.threadCpuMs, 100.0 * r.runDelayShare, r.nivcsw, r.minflt,
                                  r.minfltMeasured, r.majflt, r.rssKb, tempText, r.khzBefore, r.valid ? 1 : 0, r.invalidReason.c_str(), pmuCsv.c_str(), o.evict ? "evict" : "warm");
                    std::fflush (csv);
                }
                cells[c][li].p99.push_back (r.p99Ms * k);
                cells[c][li].max.push_back (r.maxMs * k);
            }
        }
    if (csv != nullptr)
    {
        std::fclose (csv);
        std::printf ("csv schema 2: %s\n", o.csv.c_str());
    }

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
