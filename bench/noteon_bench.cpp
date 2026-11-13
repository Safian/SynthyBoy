// SynthyBoy - benchmark: cost of a block that contains note-ons versus a steady block with the same voices sounding.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_noteon_bench [--reps N] [--steady B] [--patch default|heavy] [--sr 44100] [--block 128]
//
// Own engine, tier low, realtime = true. For chords of 1, 2, 4, 6, 8 simultaneous note-ons: a chord is struck at the start of a
// block, held for B steady blocks, released (AllNotesOff) and rendered until the voices are gone; repeated N times. Prints the mean and
// worst time of the note-on block, the mean time of the steady blocks that follow (the first two blocks after the strike are skipped)
// and the ratio (note-on block - an idle block) / (steady block / voices): the note-on cost in steady voice-blocks per note-on.
// Also prints the duration of prepare().

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "synthyboy/engine.h"

#include "own_heavy_patch.h"

using namespace synthyboy;

namespace
{
using Clock = std::chrono::steady_clock;
double ms (Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli> (b - a).count(); }
} // namespace

int main (int argc, char** argv)
{
    int reps = 200, steady = 12, block = 128;
    double sr = 44100.0;
    bool heavy = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> const char* { if (i + 1 >= argc) { std::fprintf (stderr, "missing value\n"); std::exit (2); } return argv[++i]; };
        if (a == "--reps") reps = std::atoi (next());
        else if (a == "--steady") steady = std::atoi (next());
        else if (a == "--block") block = std::atoi (next());
        else if (a == "--sr") sr = std::atof (next());
        else if (a == "--patch") heavy = std::string (next()) == "heavy";
        else { std::fprintf (stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }

    auto engine = createEngine (EngineKind::own);
    if (heavy)
        bench::ownHeavyPatch (*engine);
    Quality q = qualityForTier (Tier::low);
    q.realtime = true;
    const auto p0 = Clock::now();
    engine->prepare (sr, block, q);
    const auto p1 = Clock::now();
    std::printf ("synthyboy_noteon_bench: own engine, tier low, realtime, %.0f Hz, block %d (budget %.3f ms), patch %s, %d reps\n", sr, block,
                 1000.0 * block / sr, heavy ? "own-heavy" : "default", reps);
    std::printf ("prepare(): %.1f ms\n", ms (p0, p1));

    std::vector<float> l (static_cast<size_t> (block)), r (static_cast<size_t> (block));
    float sink = 0.0f;
    auto render = [&] (const Event* e, int n) {
        const auto t0 = Clock::now();
        engine->process (e, n, l.data(), r.data(), block);
        const auto t1 = Clock::now();
        sink += l[static_cast<size_t> (block / 2)];
        return ms (t0, t1);
    };

    // idle block (no voices) for reference
    for (int i = 0; i < 50; ++i) render (nullptr, 0);
    double idle = 0.0;
    for (int i = 0; i < 200; ++i) idle += render (nullptr, 0);
    idle /= 200.0;
    std::printf ("idle block (no voices): %.4f ms\n\n", idle);
    std::printf ("%6s | %12s %12s | %14s %10s | %s\n", "notes", "noteon mean", "noteon worst", "steady mean", "per voice", "ratio (note-on / steady voice-block)");

    for (const int notes : { 1, 2, 4, 6, 8 })
    {
        double onSum = 0.0, onWorst = 0.0, stSum = 0.0;
        long stCount = 0;
        for (int rep = 0; rep < reps + 5; ++rep)
        {
            std::vector<Event> ev;
            for (int k = 0; k < notes; ++k)
            {
                Event e;
                e.type = Event::NoteOn;
                e.key = static_cast<int16_t> (48 + k * 4 + (k > 3 ? 1 : 0));
                e.value = 0.7f;
                e.frame = 0;
                ev.push_back (e);
            }
            const double on = render (ev.data(), static_cast<int> (ev.size()));
            double st = 0.0;
            int cnt = 0;
            for (int b = 0; b < steady; ++b)
            {
                const double t = render (nullptr, 0);
                if (b >= 2) { st += t; ++cnt; }
            }
            Event off;
            off.type = Event::AllNotesOff;
            off.channel = -1;
            render (&off, 1);
            for (int b = 0; b < 3000 && engine->activeVoices() > 0; ++b) render (nullptr, 0);
            if (rep >= 5) // 5 warm-up reps
            {
                onSum += on;
                onWorst = std::max (onWorst, on);
                stSum += st / cnt;
                ++stCount;
            }
        }
        const double onMean = onSum / static_cast<double> (reps);
        const double stMean = stSum / static_cast<double> (stCount);
        const double perVoice = (stMean - idle) / notes;
        std::printf ("%6d | %9.4f ms %9.4f ms | %11.4f ms %7.4f ms | %.1f\n", notes, onMean, onWorst, stMean, perVoice,
                     perVoice > 0 ? (onMean - stMean) / notes / perVoice : 0.0);
    }
    if (sink == 12345.678f) std::puts ("");
    return 0;
}
