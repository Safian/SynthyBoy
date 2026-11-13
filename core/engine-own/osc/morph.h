// SynthyBoy - own engine: spectral morph of a wavetable frame (docs/engine-spec/02-oscillator.md section 7; original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <vector>
#include "wavetable_bank.h"

namespace synthyboy
{
namespace own
{
/** Spectral morph types in the order of oscN.morph.type. */
enum MorphType : int
{
    kMorphNone = 0, kMorphVocode, kMorphFormantScale, kMorphHarmonicStretch, kMorphInharmonicStretch, kMorphSmear, kMorphRandomAmplitudes,
    kMorphLowPass, kMorphHighPass, kMorphPhaseDisperse, kMorphShepard, kMorphTimeSkew
};

/** Work arrays of one morph rebuild (kept by the oscillator: no allocation while playing). */
struct MorphScratch
{
    float re[Wavetable::kCycle], im[Wavetable::kCycle];
    float c[Wavetable::kHarmonics + 2], s[Wavetable::kHarmonics + 2];
};

/** Builds one band limited cycle (2048 samples, with the guard samples: out[-1] .. out[2050] are written) of the table at the frame position `framePos`
    (0 .. frames - 1; the two neighbouring frames are mixed when `interpolate`), changed by the spectral morph `type` with the amount `m` (0 .. 1), keeping
    only the harmonics up to `maxHarmonic` (the band limit of the voice). `phaseInc` = the phase increment of the voice (cycles per sample) and `hostRate`
    are used by Vocode. `fast` (tier low): the transform of the real signal runs as one complex transform of half the size (rounding differs by about 1e-7 relative). Real time safe. */
void buildMorphCycle (const Wavetable& wt, float framePos, bool interpolate, int type, float m, int maxHarmonic, float phaseInc, double hostRate, float* out,
                      MorphScratch& scratch, bool fast = false) noexcept;
/** The last cycles that buildMorphCycle () made, by their arguments: a chord that is played again (or the same note on several voices) asks for the same cycles again, and a
    copy gives the same samples as a new build. One per engine (allocated in prepare ()); the audio thread only. */
class MorphCache
{
public:
    static constexpr int kEntries = 16;
    static constexpr int kStride = Wavetable::kCycle + 4; // out[-1] .. out[kCycle + 2]
    struct Key
    {
        const Wavetable* wt = nullptr;
        float framePos = 0.0f, m = 0.0f, phaseInc = 0.0f;
        int type = 0, maxHarmonic = 0;
        double hostRate = 0.0;
        bool fast = false;
    };
    void prepare() { data.assign (static_cast<size_t> (kEntries) * kStride, 0.0f); clear(); }
    void clear() noexcept
    {
        for (auto& e : keys)
            e.valid = false;
        next = 0;
    }
    /** Copies the cycle (kStride floats starting at out[-1]) if it is cached. */
    bool fetch (const Key& k, float* outMinus1) const noexcept
    {
        for (int i = 0; i < kEntries; ++i)
            if (keys[i].valid && same (keys[i].key, k))
            {
                std::copy (data.data() + static_cast<size_t> (i) * kStride, data.data() + static_cast<size_t> (i + 1) * kStride, outMinus1);
                return true;
            }
        return false;
    }
    void store (const Key& k, const float* outMinus1) noexcept
    {
        if (data.empty())
            return;
        std::copy (outMinus1, outMinus1 + kStride, data.data() + static_cast<size_t> (next) * kStride);
        keys[next].key = k;
        keys[next].valid = true;
        next = (next + 1) % kEntries;
    }

private:
    static bool same (const Key& a, const Key& b) noexcept
    {
        return a.wt == b.wt && a.framePos == b.framePos && a.m == b.m && a.phaseInc == b.phaseInc && a.type == b.type && a.maxHarmonic == b.maxHarmonic && a.hostRate == b.hostRate
            && a.fast == b.fast;
    }
    struct Slot { Key key; bool valid = false; };
    std::vector<float> data;
    Slot keys[kEntries];
    int next = 0;
};
} // namespace own
} // namespace synthyboy
