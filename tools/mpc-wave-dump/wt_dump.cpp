// SynthyBoy - host tool: writes the frames of the built-in wavetables of the own engine (the MPC build's engine) to stdout.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Output (little endian): for choice 0 .. kNumWavetableChoices - 1: int32 numFrames (0 = no built-in table: "Preset table"), then
// numFrames * 2048 float32 (one cycle per frame, level 0 = every harmonic, i.e. the shape the oscillator plays).
// Used by scripts/gen-mpc-wave-strips.py (build: scripts/gen-mpc-wave-strips.py compiles this file with the host C++ compiler).
#include <cstdint>
#include <cstdio>

#include "osc/wavetable_bank.h"
#include "synthyboy/params.h"

int main()
{
    using namespace synthyboy;
    for (int c = 0; c < kNumWavetableChoices; ++c)
    {
        const own::Wavetable* t = own::builtinWavetable (c);
        const int32_t frames = t != nullptr ? t->numFrames() : 0;
        std::fwrite (&frames, sizeof (frames), 1, stdout);
        for (int f = 0; f < frames; ++f)
            std::fwrite (t->cycle (f, 0), sizeof (float), own::Wavetable::kCycle, stdout);
    }
    return 0;
}
