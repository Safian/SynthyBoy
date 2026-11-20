// SynthyBoy - bench: the cost of one effect object on its own (docs/plans/reverb-convolve.md section 2.2, task T01): Reverb (six algorithms), Convolve (five impulse lengths) and
// Hyper / Dimension (unison 1 .. 7, Dimension off and on), run directly with no engine around them on a fixed stereo program in 32-sample chunks (the way FxModule::process calls
// the effects; Convolve in blocks of 128), 44.1 kHz, thread CPU time per chunk.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_fx_cost_bench [--seconds S] [--repeat R] [--core N] [--csv FILE] [--quick] [--only reverb|convolve|hyper]
//
// One line per measurement on stdout:
//   FXCOST reverb type=<0..5> sr=44100 ns_per_sample=<median> p99_chunk_ns=<..> pct_of_128_block=<ns_per_sample * 128 / 2902494 * 100>
//   FXCOST convolve ir_frames=<3969|30870|63945|262144|524288> sr=44100 ns_per_sample=<..> p99_block_ns=<..> pct_of_128_block=<..> bytes=<..|n/a> build_ms=<time of loadIr>
//   FXCOST hyper unison=<1..7> dim=<0|1> ns_per_sample=<..>
// and FXCOST-DONE lines=<n> last; the exit code is 1 if n is not the expected number (25 for all three groups).
// ns_per_sample is the median over the repetitions of the median chunk time per sample, the p99 columns are the best (smallest) 99th percentile over the repetitions.
// --quick: 0.2 s per line, one repetition. --core N: pin the thread to core N (Linux).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <type_traits>
#include <vector>

#if defined(__linux__)
 #include <sched.h>
#endif

#include "../core/fx-serum-b/convolve.h"
#include "fx/fx_reverb.h"
#include "synthyboy/fx/hyper_dimension.h"

namespace
{
constexpr double kSr = 44100.0;
constexpr double kBlockNs = 128.0 / 44100.0 * 1.0e9; // 2 902 494 ns

double threadNowNs()
{
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec t;
    if (clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t) == 0)
        return static_cast<double> (t.tv_sec) * 1.0e9 + static_cast<double> (t.tv_nsec);
#endif
    return std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

/** The program: band-limited noise plus a saw chord (A2, E3, A3 as 110, 164.8, 220 Hz), one second, looped; 0.1 rms noise, 0.1 per saw. */
struct Program
{
    std::vector<float> l, r;
    Program()
    {
        const size_t n = static_cast<size_t> (kSr);
        l.resize (n);
        r.resize (n);
        uint64_t s = 0x9E3779B97F4A7C15ull;
        double lpL = 0.0, lpR = 0.0;
        const double a = std::exp (-6.283185307179586 * 4000.0 / kSr);
        for (size_t i = 0; i < n; ++i)
        {
            auto white = [&s] () {
                s ^= s << 13;
                s ^= s >> 7;
                s ^= s << 17;
                return static_cast<double> (s >> 11) / 9007199254740992.0 * 2.0 - 1.0;
            };
            lpL = (1.0 - a) * white() + a * lpL;
            lpR = (1.0 - a) * white() + a * lpR;
            const double t = static_cast<double> (i) / kSr;
            double chord = 0.0;
            for (const double f : { 110.0, 164.8, 220.0 })
                chord += 0.1 * (2.0 * std::fmod (f * t, 1.0) - 1.0);
            l[i] = static_cast<float> (0.3 * lpL + chord);
            r[i] = static_cast<float> (0.3 * lpR + chord);
        }
    }
};

struct Measure
{
    double nsPerSample = 0.0, p99ChunkNs = 0.0;
};

struct Options
{
    double seconds = 10.0;
    int repeat = 7;
    int core = -1;
    std::string csv, only;
    bool quick = false;
};

/** Runs `process (l, r, n)` over the looped program in chunks of `chunk` samples for `seconds`, `repeat` times (after one untimed pass of the same length); times each chunk. */
template <class Fn>
Measure measure (const Program& prog, const Options& o, int chunk, Fn&& process)
{
    const double seconds = o.quick ? 0.2 : o.seconds;
    const int repeat = o.quick ? 1 : std::max (o.repeat, 1);
    const size_t chunks = std::max<size_t> (static_cast<size_t> (seconds * kSr / chunk), 16);
    std::vector<float> l (static_cast<size_t> (chunk)), r (static_cast<size_t> (chunk));
    std::vector<double> perRepMedian, perRepP99;
    std::vector<double> times (chunks);
    size_t pos = 0;
    for (int rep = -1; rep < repeat; ++rep) // repetition -1 is the warm up and is not kept
    {
        for (size_t c = 0; c < chunks; ++c)
        {
            if (pos + static_cast<size_t> (chunk) > prog.l.size())
                pos = 0;
            std::memcpy (l.data(), prog.l.data() + pos, sizeof (float) * static_cast<size_t> (chunk));
            std::memcpy (r.data(), prog.r.data() + pos, sizeof (float) * static_cast<size_t> (chunk));
            pos += static_cast<size_t> (chunk);
            const double t0 = threadNowNs();
            process (l.data(), r.data(), chunk);
            times[c] = threadNowNs() - t0;
        }
        if (rep < 0)
            continue;
        std::vector<double> sorted (times);
        std::sort (sorted.begin(), sorted.end());
        perRepMedian.push_back (sorted[sorted.size() / 2] / chunk);
        perRepP99.push_back (sorted[std::min (sorted.size() - 1, static_cast<size_t> (static_cast<double> (sorted.size()) * 0.99))]);
    }
    std::sort (perRepMedian.begin(), perRepMedian.end());
    Measure m;
    m.nsPerSample = perRepMedian[perRepMedian.size() / 2];
    m.p99ChunkNs = *std::min_element (perRepP99.begin(), perRepP99.end());
    return m;
}

// the heap bytes an object reports through memoryBytes (), or n/a for a class that has none (so the file compiles before and after the Convolve rewrite)
template <class T>
auto bytesOf (const T& t, int) -> decltype (t.memoryBytes(), std::string())
{
    return std::to_string (static_cast<unsigned long long> (t.memoryBytes()));
}
template <class T>
std::string bytesOf (const T&, long)
{
    return "n/a";
}

struct Output
{
    int lines = 0;
    std::FILE* csv = nullptr;

    void line (const std::string& text, const std::string& csvRow)
    {
        std::printf ("%s\n", text.c_str());
        std::fflush (stdout);
        if (csv != nullptr)
            std::fprintf (csv, "%s\n", csvRow.c_str());
        ++lines;
    }
};

std::string fmt (const char* f, ...)
{
    char buf[512];
    va_list ap;
    va_start (ap, f);
    std::vsnprintf (buf, sizeof buf, f, ap);
    va_end (ap);
    return buf;
}

void runReverb (const Program& prog, const Options& o, Output& out)
{
    using synthyboy::own::Reverb;
    for (int type = 0; type <= 5; ++type)
    {
        Reverb reverb;
        reverb.prepare (kSr);
        Reverb::Params p;
        p.mix = 1.0f;
        p.decay = 1.5f;
        p.type = type;
        reverb.set (p);
        const Measure m = measure (prog, o, 32, [&reverb] (float* l, float* r, int n) { reverb.process (l, r, n); });
        const double pct = m.nsPerSample * 128.0 / kBlockNs * 100.0;
        out.line (fmt ("FXCOST reverb type=%d sr=44100 ns_per_sample=%.2f p99_chunk_ns=%.0f pct_of_128_block=%.4f", type, m.nsPerSample, m.p99ChunkNs, pct),
                  fmt ("reverb,%d,%.2f,%.0f,%.4f,%s,", type, m.nsPerSample, m.p99ChunkNs, pct, bytesOf (reverb, 0).c_str()));
    }
}

void runConvolve (const Program& prog, const Options& o, Output& out)
{
    const int lengths[5] = { 3969, 30870, 63945, 262144, 524288 };
    for (const int frames : lengths)
    {
        std::vector<float> ir (static_cast<size_t> (frames));
        uint64_t s = 0x2545F4914F6CDD1Dull;
        for (int i = 0; i < frames; ++i)
        {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            const double white = static_cast<double> (s >> 11) / 9007199254740992.0 * 2.0 - 1.0;
            ir[static_cast<size_t> (i)] = static_cast<float> (white * std::exp (-static_cast<double> (i) / (0.3 * frames)));
        }
        synthyboy::fx::Convolve convolve;
        convolve.prepare (kSr, 128);
        convolve.params.mix = 1.0f;
        const double t0 = threadNowNs();
        convolve.loadIr (ir.data(), frames, 1, kSr);
        const double buildMs = (threadNowNs() - t0) / 1.0e6;
        const Measure m = measure (prog, o, 128, [&convolve] (float* l, float* r, int n) { convolve.process (l, r, n); });
        const double pct = m.nsPerSample * 128.0 / kBlockNs * 100.0;
        out.line (fmt ("FXCOST convolve ir_frames=%d sr=44100 ns_per_sample=%.2f p99_block_ns=%.0f pct_of_128_block=%.4f bytes=%s build_ms=%.1f", frames, m.nsPerSample, m.p99ChunkNs, pct,
                       bytesOf (convolve, 0).c_str(), buildMs),
                  fmt ("convolve,%d,%.2f,%.0f,%.4f,%s,%.1f", frames, m.nsPerSample, m.p99ChunkNs, pct, bytesOf (convolve, 0).c_str(), buildMs));
    }
}

void runHyper (const Program& prog, const Options& o, Output& out)
{
    for (int unison = 1; unison <= 7; ++unison)
        for (int dim = 0; dim <= 1; ++dim)
        {
            synthyboy::fx::HyperDimension hyper;
            hyper.prepare (kSr, 128);
            synthyboy::fx::HyperDimension::Params p;
            p.hyperUnison = unison;
            p.hyperMix = 0.5f;
            p.dimMix = dim != 0 ? 0.5f : 0.0f;
            hyper.setParams (p);
            hyper.reset();
            const Measure m = measure (prog, o, 32, [&hyper] (float* l, float* r, int n) { hyper.process (l, r, n); });
            out.line (fmt ("FXCOST hyper unison=%d dim=%d ns_per_sample=%.2f", unison, dim, m.nsPerSample), fmt ("hyper,unison%d_dim%d,%.2f,%.0f,,,", unison, dim, m.nsPerSample, m.p99ChunkNs));
        }
}
} // namespace

int main (int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--seconds") o.seconds = std::atof (next());
        else if (a == "--repeat") o.repeat = std::atoi (next());
        else if (a == "--core") o.core = std::atoi (next());
        else if (a == "--csv") o.csv = next();
        else if (a == "--only") o.only = next();
        else if (a == "--quick") o.quick = true;
        else
        {
            std::fprintf (stderr, "usage: synthyboy_fx_cost_bench [--seconds S] [--repeat R] [--core N] [--csv FILE] [--quick] [--only reverb|convolve|hyper]\n");
            return 2;
        }
    }
    if (!o.only.empty() && o.only != "reverb" && o.only != "convolve" && o.only != "hyper")
    {
        std::fprintf (stderr, "--only takes reverb, convolve or hyper\n");
        return 2;
    }
#if defined(__linux__)
    if (o.core >= 0)
    {
        cpu_set_t set;
        CPU_ZERO (&set);
        CPU_SET (o.core, &set);
        if (sched_setaffinity (0, sizeof set, &set) != 0)
            std::fprintf (stderr, "warning: could not pin the thread to core %d\n", o.core);
    }
#endif
    Output out;
    if (!o.csv.empty())
    {
        out.csv = std::fopen (o.csv.c_str(), "w");
        if (out.csv != nullptr)
            std::fprintf (out.csv, "kind,key,ns_per_sample,p99_ns,pct_of_128_block,bytes,build_ms\n");
    }
    const Program prog;
    int expected = 0;
    if (o.only.empty() || o.only == "reverb")
    {
        runReverb (prog, o, out);
        expected += 6;
    }
    if (o.only.empty() || o.only == "convolve")
    {
        runConvolve (prog, o, out);
        expected += 5;
    }
    if (o.only.empty() || o.only == "hyper")
    {
        runHyper (prog, o, out);
        expected += 14;
    }
    std::printf ("FXCOST-DONE lines=%d\n", out.lines);
    if (out.csv != nullptr)
        std::fclose (out.csv);
    return out.lines == expected ? 0 : 1;
}
