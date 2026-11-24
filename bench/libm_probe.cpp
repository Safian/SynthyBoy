// SynthyBoy - benchmark: the cost of the libm functions the engine calls and of the two clocks, per call, on this machine and with this link
// (SYNTHYBOY_TIER_BENCH_STATIC: the libm.a of the build container, direct calls; otherwise the libm.so of the machine through the PLT). It links nothing of the engine.
// Output: "LIBM-PROBE link <static|dynamic>", "LIBM-PROBE <function> <ns per call> ns" for the twelve functions, "LIBM-PROBE clock_steady <ns> ns",
// "LIBM-PROBE clock_gettime <ns> ns" and the last line "LIBM-PROBE-RESULT ok" (every figure finite and above 0).
// Usage: synthyboy_libm_probe [--quick]      (--quick: 20000 calls per repetition instead of 200000)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <math.h>
#include <vector>

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

#ifndef SB_LIBM_PROBE_LINK
 #define SB_LIBM_PROBE_LINK "unknown"
#endif

namespace
{
volatile double g_sink = 0.0;   // the sum of every loop goes here so no loop is removed

constexpr int kRepetitions = 11;

/** xorshift32 mapped to [lo, hi]: a deterministic input ramp. */
struct Rng
{
    uint32_t state = 12345u;
    float next (float lo, float hi)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return lo + (hi - lo) * (static_cast<float> (state) * (1.0f / 4294967296.0f));
    }
};

std::vector<float> ramp (int n, float lo, float hi)
{
    Rng rng;
    std::vector<float> v (static_cast<size_t> (n));
    for (auto& x : v)
        x = rng.next (lo, hi);
    return v;
}

/** The median over kRepetitions of the nanoseconds per call of body (n), body runs n calls and returns their sum. */
template <class Body>
double measureNs (int n, Body&& body)
{
    std::vector<double> reps;
    for (int r = 0; r < kRepetitions; ++r)
    {
        const auto t0 = std::chrono::steady_clock::now();
        const double sum = body (n);
        const auto t1 = std::chrono::steady_clock::now();
        g_sink = g_sink + sum;
        reps.push_back (std::chrono::duration<double, std::nano> (t1 - t0).count() / static_cast<double> (n));
    }
    std::sort (reps.begin(), reps.end());
    return reps[reps.size() / 2];
}

double clockGettimeNs()
{
#ifdef _WIN32
    LARGE_INTEGER c;
    QueryPerformanceCounter (&c);
    return static_cast<double> (c.QuadPart);
#else
    timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return static_cast<double> (ts.tv_sec) * 1.0e9 + static_cast<double> (ts.tv_nsec);
#endif
}

void report (const char* name, double ns, bool& ok)
{
    if (! std::isfinite (ns) || ns <= 0.0)
        ok = false;
    std::printf ("LIBM-PROBE %s %.3f ns\n", name, ns);
}
} // namespace

int main (int argc, char** argv)
{
    int n = 200000;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--quick") == 0)
            n = 20000;
        else
        {
            std::fprintf (stderr, "usage: synthyboy_libm_probe [--quick]\n");
            return 2;
        }
    }

    bool ok = true;
    std::printf ("LIBM-PROBE link %s\n", SB_LIBM_PROBE_LINK);

    const std::vector<float> expIn = ramp (n, -10.0f, 10.0f);       // expf, exp2f
    const std::vector<float> logIn = ramp (n, 0.001f, 1000.0f);     // logf, log2f
    const std::vector<float> powBase = ramp (n, 0.1f, 10.0f);
    const std::vector<float> powExp = ramp (n, -3.0f, 3.0f);
    const std::vector<float> trigIn = ramp (n, -10.0f, 10.0f);      // sinf, cosf
    const std::vector<float> tanIn = ramp (n, -1.5f, 1.5f);
    const std::vector<float> intIn = ramp (n, -1000.0f, 1000.0f);   // floorf, roundf, fmaxf
    const std::vector<float> intIn2 = ramp (n, -1000.0f, 1000.0f);
    const std::vector<float> sqrtIn = ramp (n, 0.0f, 100.0f);

    auto sumOver = [] (const std::vector<float>& in, auto fn) {
        return [&in, fn] (int count) {
            double acc = 0.0;
            for (int i = 0; i < count; ++i)
                acc += static_cast<double> (fn (in[static_cast<size_t> (i)]));
            return acc;
        };
    };

    report ("expf", measureNs (n, sumOver (expIn, [] (float x) { return expf (x); })), ok);
    report ("logf", measureNs (n, sumOver (logIn, [] (float x) { return logf (x); })), ok);
    report ("powf", measureNs (n, [&] (int count) {
        double acc = 0.0;
        for (int i = 0; i < count; ++i)
            acc += static_cast<double> (powf (powBase[static_cast<size_t> (i)], powExp[static_cast<size_t> (i)]));
        return acc;
    }), ok);
    report ("sinf", measureNs (n, sumOver (trigIn, [] (float x) { return sinf (x); })), ok);
    report ("cosf", measureNs (n, sumOver (trigIn, [] (float x) { return cosf (x); })), ok);
    report ("tanf", measureNs (n, sumOver (tanIn, [] (float x) { return tanf (x); })), ok);
    report ("exp2f", measureNs (n, sumOver (expIn, [] (float x) { return exp2f (x); })), ok);
    report ("log2f", measureNs (n, sumOver (logIn, [] (float x) { return log2f (x); })), ok);
    report ("floorf", measureNs (n, sumOver (intIn, [] (float x) { return floorf (x); })), ok);
    report ("roundf", measureNs (n, sumOver (intIn, [] (float x) { return roundf (x); })), ok);
    report ("fmaxf", measureNs (n, [&] (int count) {
        double acc = 0.0;
        for (int i = 0; i < count; ++i)
            acc += static_cast<double> (fmaxf (intIn[static_cast<size_t> (i)], intIn2[static_cast<size_t> (i)]));
        return acc;
    }), ok);
    report ("sqrtf", measureNs (n, sumOver (sqrtIn, [] (float x) { return sqrtf (x); })), ok);

    report ("clock_steady", measureNs (n, [] (int count) {
        double acc = 0.0;
        for (int i = 0; i < count; ++i)
            acc += static_cast<double> (std::chrono::steady_clock::now().time_since_epoch().count() & 0xff);
        return acc;
    }), ok);
    report ("clock_gettime", measureNs (n, [] (int count) {
        double acc = 0.0;
        for (int i = 0; i < count; ++i)
            acc += std::fmod (clockGettimeNs(), 256.0);
        return acc;
    }), ok);

    std::puts (ok ? "LIBM-PROBE-RESULT ok" : "LIBM-PROBE-RESULT FAILED");
    return ok ? 0 : 1;
}
