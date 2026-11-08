// SynthyBoy - core tests: counting replacement of the global operator new / delete (for the real-time rule "no allocation in
// process()", docs/ARCHITECTURE.md 3.7). Include it in exactly ONE translation unit of a test executable.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdlib>
#include <new>

#if defined (_WIN32)
 #include <malloc.h>
#endif

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "test_engines.h"
#include "test_runner.h"
#include "test_util.h"

namespace
{
std::atomic<unsigned long> g_allocations { 0 };
std::atomic<unsigned long> g_frees { 0 };
// Only the thread that runs the test (and calls process ()) is counted: the engine-vital Convolve module partitions its impulse response on
// a builder thread of its own (docs/UPSTREAM.md), which is allowed to allocate; the real-time rule is about the audio thread.
thread_local bool t_countThisThread = false;
struct CountThisThread
{
    CountThisThread() { t_countThisThread = true; }
};
const CountThisThread g_countMainThread;

void* countedAlloc (std::size_t size) noexcept
{
    if (t_countThisThread)
        g_allocations.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (size != 0 ? size : 1);
}

void* countedAlignedAlloc (std::size_t size, std::size_t alignment) noexcept
{
    if (t_countThisThread)
        g_allocations.fetch_add (1, std::memory_order_relaxed);
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

void countedFree (void* p) noexcept
{
    if (p != nullptr)
        g_frees.fetch_add (1, std::memory_order_relaxed);
    std::free (p);
}

void countedAlignedFree (void* p) noexcept
{
    if (p != nullptr)
        g_frees.fetch_add (1, std::memory_order_relaxed);
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

