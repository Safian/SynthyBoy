// SynthyBoy - own engine tests: memory footprint and prepare time of OwnEngine::prepare () (docs/plans/memory-footprint.md, task T1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A measurement probe: the global operator new / delete of this executable are replaced by versions that count the calls and the bytes (all threads; plain allocations also keep the live
// and the peak bytes through a size header). The tests print "MEM-BASELINE ..." lines (the numbers before and after the memory plan are compared from them); the strict upper bounds are
// locked by a later task. The checks here only prove that the hook sees the allocations and that the built-in wavetable bank has the size the documentation names.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#if defined (_WIN32)
 #include <malloc.h>
#endif

#include "osc/wavetable_bank.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
std::atomic<unsigned long long> g_allocCount { 0 };
std::atomic<unsigned long long> g_allocBytes { 0 };   // total bytes requested
std::atomic<long long> g_liveBytes { 0 };             // plain (non aligned) allocations only
std::atomic<long long> g_peakBytes { 0 };

constexpr std::size_t kHeader = 16;   // keeps the 16 byte alignment of malloc

void notePeak (long long live) noexcept
{
    long long peak = g_peakBytes.load (std::memory_order_relaxed);
    while (live > peak && ! g_peakBytes.compare_exchange_weak (peak, live, std::memory_order_relaxed)) {}
}

void* countedAlloc (std::size_t size) noexcept
{
    void* raw = std::malloc (size + kHeader);
    if (raw == nullptr)
        return nullptr;
    *static_cast<std::size_t*> (raw) = size;
    g_allocCount.fetch_add (1, std::memory_order_relaxed);
    g_allocBytes.fetch_add (size, std::memory_order_relaxed);
    notePeak (g_liveBytes.fetch_add (static_cast<long long> (size), std::memory_order_relaxed) + static_cast<long long> (size));
    return static_cast<char*> (raw) + kHeader;
}

void countedFree (void* p) noexcept
{
    if (p == nullptr)
        return;
    void* raw = static_cast<char*> (p) - kHeader;
    g_liveBytes.fetch_sub (static_cast<long long> (*static_cast<std::size_t*> (raw)), std::memory_order_relaxed);
    std::free (raw);
}

void* countedAlignedAlloc (std::size_t size, std::size_t alignment) noexcept
{
    g_allocCount.fetch_add (1, std::memory_order_relaxed);
    g_allocBytes.fetch_add (size, std::memory_order_relaxed);
    if (size == 0)
        size = 1;
#if defined (_WIN32)
    return _aligned_malloc (size, alignment);
#else
    void* p = nullptr;
    if (alignment < sizeof (void*))
        alignment = sizeof (void*);
    return posix_memalign (&p, alignment, size) == 0 ? p : nullptr;
#endif
}

void countedAlignedFree (void* p) noexcept
{
#if defined (_WIN32)
    _aligned_free (p);
#else
    std::free (p);
#endif
}
} // namespace

void* operator new (std::size_t n)                                    { if (void* p = countedAlloc (n)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n)                                  { if (void* p = countedAlloc (n)) return p; throw std::bad_alloc(); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept    { return countedAlloc (n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept  { return countedAlloc (n); }
void* operator new (std::size_t n, std::align_val_t a)                { if (void* p = countedAlignedAlloc (n, static_cast<std::size_t> (a))) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n, std::align_val_t a)              { if (void* p = countedAlignedAlloc (n, static_cast<std::size_t> (a))) return p; throw std::bad_alloc(); }
void* operator new (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept   { return countedAlignedAlloc (n, static_cast<std::size_t> (a)); }
void* operator new[] (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return countedAlignedAlloc (n, static_cast<std::size_t> (a)); }

void operator delete (void* p) noexcept                                       { countedFree (p); }
void operator delete[] (void* p) noexcept                                     { countedFree (p); }
void operator delete (void* p, std::size_t) noexcept                          { countedFree (p); }
void operator delete[] (void* p, std::size_t) noexcept                        { countedFree (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept                { countedFree (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept              { countedFree (p); }
void operator delete (void* p, std::align_val_t) noexcept                     { countedAlignedFree (p); }
void operator delete[] (void* p, std::align_val_t) noexcept                   { countedAlignedFree (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept        { countedAlignedFree (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept      { countedAlignedFree (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept   { countedAlignedFree (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { countedAlignedFree (p); }

namespace
{
constexpr double kSr = 44100.0;
constexpr int kBlock = 128;

/** Parses the VmRSS value in kilobytes from /proc/self/status text. Returns -1 if not found or malformed. */
long parseVmRssKb (const char* text) noexcept
{
    if (text == nullptr)
        return -1;
    const char* p = text;
    while (*p != '\0')
    {
        if (std::strncmp (p, "VmRSS:", 6) == 0)
        {
            p += 6;
            while (*p == ' ' || *p == '\t')
                ++p;
            char* end = nullptr;
            const long val = std::strtol (p, &end, 10);
            if (end != p && val >= 0)
                return val;
            return -1;
        }
        while (*p != '\0' && *p != '\n')
            ++p;
        if (*p == '\n')
            ++p;
    }
    return -1;
}

/** The resident set of this process in kilobytes from /proc/self/status on Linux (-1 where unavailable). */
long residentKb() noexcept
{
#if defined (__linux__)
    std::FILE* f = std::fopen ("/proc/self/status", "r");
    if (f == nullptr)
        return -1;
    char line[256];
    long kb = -1;
    while (std::fgets (line, sizeof (line), f) != nullptr)
    {
        const long val = parseVmRssKb (line);
        if (val >= 0)
        {
            kb = val;
            break;
        }
    }
    std::fclose (f);
    return kb;
#else
    return -1;
#endif
}

void setKey (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

struct Snapshot
{
    unsigned long long allocs = 0;
    unsigned long long bytes = 0;
    long long live = 0;
    long rssKb = 0;
};

Snapshot snapshot()
{
    return { g_allocCount.load(), g_allocBytes.load(), g_liveBytes.load(), residentKb() };
}

/** The quality of a measured configuration: the caps of the device profile, the lazy tables off. */
Quality measuredQuality (int maxVoices, int maxUnison, uint32_t fxMask)
{
    Quality q = qualityForTier (Tier::high);
    q.maxVoices = maxVoices;
    q.maxUnison = maxUnison;
    q.fxMask = fxMask;
    q.realtime = false;
    q.lazyTables = false;
    return q;
}

constexpr uint32_t kLowMask = kFxAll & ~(kFxReverb | kFxFlanger | kFxHyperDim | kFxBode | kFxConvolve);

/** Builds the shared wavetable bank once, so the numbers below are those of the engine and not of the process-shared bank. */
void warmBank()
{
    auto e = createOwnEngine();
    e->prepare (kSr, kBlock, measuredQuality (8, 4, kLowMask));
}

struct Config
{
    const char* name;
    int voices;
    int unison;
    uint32_t mask;
};

void report (const char* name, const std::vector<std::unique_ptr<IEngine>>& engines, const Snapshot& before, const Snapshot& after)
{
    double voicesMs = 0.0, fxMs = 0.0;
    for (const auto& e : engines)
    {
        voicesMs += e->prepareDetailMs (0);
        fxMs += e->prepareDetailMs (1);
    }
    std::printf ("MEM-BASELINE tier=%s allocs=%llu bytes=%llu live_bytes=%lld peak_bytes=%lld prepare_voices_ms=%.2f prepare_fx_ms=%.2f rss_kb=%ld\n",
                 name, after.allocs - before.allocs, after.bytes - before.bytes, after.live - before.live, g_peakBytes.load(), voicesMs, fxMs, after.rssKb);
}
} // namespace

TEST (own_memory_hook_counts_allocations_and_the_bank_has_its_documented_size)
{
    const auto before = snapshot();
    {
        std::vector<char> v (100000);
        v[0] = 1;
        const auto during = snapshot();
        CHECK (during.allocs > before.allocs);
        CHECK (during.bytes >= before.bytes + 100000u);
        CHECK (during.live >= before.live + 100000);
    }
    CHECK_EQ (own::builtinBankBytes(), static_cast<size_t> (33099744u));
}

TEST (own_memory_report_single_tiers)
{
    warmBank();
    const Config configs[] = {
        { "low", 8, 4, kLowMask },   // the Key 61 profile
        { "xl", 16, 16, kFxAll },
        { "high", 32, 16, kFxAll },
    };
    for (const Config& c : configs)
    {
        std::vector<std::unique_ptr<IEngine>> engines;
        const auto before = snapshot();
        engines.push_back (createOwnEngine());
        engines[0]->prepare (kSr, kBlock, measuredQuality (c.voices, c.unison, c.mask));
        const auto after = snapshot();
        CHECK (after.allocs > before.allocs);
        CHECK (after.bytes > before.bytes);
        report (c.name, engines, before, after);
    }
}

TEST (own_memory_report_four_instances_low)
{
    warmBank();
    std::vector<std::unique_ptr<IEngine>> engines;
    const auto before = snapshot();
    for (int i = 0; i < 4; ++i)
    {
        engines.push_back (createOwnEngine());
        engines.back()->prepare (kSr, kBlock, measuredQuality (8, 4, kLowMask));
    }
    const auto after = snapshot();
    CHECK (after.allocs > before.allocs);
    CHECK (after.bytes > before.bytes);
    report ("low_x4", engines, before, after);
}

TEST (own_memory_report_content_and_reverb)
{
    warmBank();
    std::vector<std::unique_ptr<IEngine>> engines;
    const auto before = snapshot();
    engines.push_back (createOwnEngine());
    engines[0]->prepare (kSr, kBlock, measuredQuality (8, 4, kLowMask));
    setKey (*engines[0], "osc1.type", static_cast<float> (kOscTypeSpectral));
    setKey (*engines[0], "reverb.force_enable", 1.0f);
    const auto after = snapshot();
    CHECK (after.allocs > before.allocs);
    CHECK (after.bytes > before.bytes);
    report ("low_spectral_reverb", engines, before, after);
}

TEST (own_memory_parse_vm_rss)
{
    CHECK_EQ (parseVmRssKb (nullptr), -1L);
    CHECK_EQ (parseVmRssKb (""), -1L);
    CHECK_EQ (parseVmRssKb ("Name:\tsynth\nVmPeak:\t  120000 kB\n"), -1L);
    CHECK_EQ (parseVmRssKb ("VmRSS:\t   74112 kB\n"), 74112L);
    const char* sampleStatus =
        "Name:\tsynthyboy_test\n"
        "State:\tR (running)\n"
        "VmPeak:\t  120000 kB\n"
        "VmSize:\t  115000 kB\n"
        "VmHWM:\t   80000 kB\n"
        "VmRSS:\t   74112 kB\n"
        "VmData:\t   60000 kB\n";
    CHECK_EQ (parseVmRssKb (sampleStatus), 74112L);
    CHECK_EQ (parseVmRssKb ("VmRSS: 0 kB\n"), 0L);
    CHECK_EQ (parseVmRssKb ("VmRSS:\n"), -1L);
    CHECK_EQ (parseVmRssKb ("VmRSS: invalid kB\n"), -1L);
}
