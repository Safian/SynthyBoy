// SynthyBoy - own engine: the small pure rules of the oscillator options that change which note an oscillator plays: the transpose snap (a scale mask of the semitones of an octave, as Vital's
// transpose quantize: bits 0 .. 11 the allowed notes, bit 12 snaps the whole note instead of the transpose alone) and the key fold of the key range (docs/MPC.md "Oscillator UI", Serum 2 manual p.271).
// No allocation, no state: usable on the audio thread.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>

namespace synthyboy
{
namespace own
{
/** The snapped note of every semitone offset 0 .. 12 inside an octave for the scale `mask` (bits 0 .. 11): the nearest allowed note, a tie goes down; octave end 12 may snap to the first note of the next octave. */
inline void fillSnapTable (int mask, float* snap13) noexcept
{
    int lo = -1, hi = -1;
    for (int i = 0; i < 12; ++i)
        if ((mask >> i) & 1)
        {
            if (lo < 0)
                lo = i;
            hi = i;
        }
    for (int i = 0; i <= 12; ++i)
    {
        float best = 0.0f, bestD = 1.0e9f;
        // the allowed notes of this octave and the first one of the next, the last one of the previous
        for (int k = -1; k <= 12; ++k)
        {
            const bool ok = k == -1 ? hi >= 0 : (k == 12 ? lo >= 0 : ((mask >> k) & 1) != 0);
            if (!ok)
                continue;
            const float note = k == -1 ? static_cast<float> (hi - 12) : (k == 12 ? static_cast<float> (lo + 12) : static_cast<float> (k));
            const float d = std::fabs (static_cast<float> (i) - note);
            if (d < bestD)
            {
                bestD = d;
                best = note;
            }
        }
        snap13[i] = best;
    }
}

/** The transpose in semitones an oscillator plays after the snap. `mask` 0 (or no note bit) leaves it as it is. Local (bit 12 clear): the transpose alone is snapped to the scale, the note is then added;
    global (bit 12 set): the sum of the note `midi` and the transpose is snapped, so the played note always lies in the scale. */
inline float snapTranspose (int mask, float midi, float transpose) noexcept
{
    if ((mask & 4095) == 0)
        return transpose;
    float table[13];
    fillSnapTable (mask & 4095, table);
    const bool global = (mask >> 12) != 0;
    const float x = global ? midi + transpose : transpose;
    const float inOct = x - 12.0f * std::floor (x / 12.0f);
    const int idx = static_cast<int> (std::lround (inOct));
    const float snapped = (x - inOct) + table[idx < 0 ? 0 : (idx > 12 ? 12 : idx)];
    return global ? snapped - midi : snapped;
}

/** The key an oscillator plays for `key` when its key range [lo, hi] folds: a key outside is moved by whole octaves into the range. A range narrower than an octave cannot take every key: the key then stays where it
    is (and is out of range). */
inline int foldKey (int key, int lo, int hi) noexcept
{
    if (key >= lo && key <= hi)
        return key;
    if (hi - lo + 1 < 12)
        return key;
    while (key < lo)
        key += 12;
    while (key > hi)
        key -= 12;
    return key;
}
} // namespace own
} // namespace synthyboy
