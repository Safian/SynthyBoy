// SynthyBoy - host tool: dumps one cycle of every LFO shape of the own engine (core/engine-own/env_lfo/lfo.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Build (done by scripts/gen-mpc-lfo-strips.py):  c++ -std=c++17 -Icore/engine-own -Icore/include scripts/lfo-shape-dump.cpp -o lfo-shape-dump
// Usage: lfo-shape-dump [samples]      (default 480)
// Output: one line per shape 0 .. kNumLfoShapes-1: "<shape> <v0> <v1> ... <v(n-1)>", the shape at cycle position i / n
//         (the cycle end, position 1, is the start of the next cycle and not printed), values rounded to 5 decimals.
// Shape kNumLfoShapes (Custom) has no fixed shape and is not printed.
#include <cstdio>
#include <cstdlib>
#include "env_lfo/lfo.h"

int main (int argc, char** argv)
{
    const int n = argc > 1 ? std::atoi (argv[1]) : 480;
    if (n < 2)
    {
        std::fprintf (stderr, "lfo-shape-dump: samples must be at least 2\n");
        return 2;
    }
    for (int shape = 0; shape < synthyboy::kNumLfoShapes; ++shape)
    {
        std::printf ("%d", shape);
        for (int i = 0; i < n; ++i)
            std::printf (" %.5f", static_cast<double> (synthyboy::own::lfoShape (shape, static_cast<float> (i) / static_cast<float> (n), 0u)));
        std::printf ("\n");
    }
    return 0;
}
