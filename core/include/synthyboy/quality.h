// SynthyBoy - core: quality tiers (platform scaling), see docs/ARCHITECTURE.md 3.6.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace synthyboy
{
/** Hard upper bound of the voice pool; Quality::maxVoices is capped to it by Engine::prepare. */
constexpr int kMaxVoices = 32;
/** Hard upper bound of the unison voices of an oscillator. */
constexpr int kMaxUnisonVoices = 16;

enum class Tier
{
    low,    // MPC-class ARMv7
    medium, // phone
    high    // desktop
};

enum class Interpolation : uint8_t
{
    linear,  // cross-fade between neighbouring wavetable frames
    spectral // interpolate the spectra of neighbouring frames (smoother morphing, dearer)
};

/** The effects that a tier may switch off (bit k = effect slot k of the FX chain: Chorus, Compressor, Delay, Distortion, EQ,
    FX Filter, Flanger, Phaser, Reverb). A disabled effect is bypassed; its parameters keep their values. */
enum FxMask : uint32_t
{
    kFxChorus     = 1u << 0,
    kFxCompressor = 1u << 1,
    kFxDelay      = 1u << 2,
    kFxDistortion = 1u << 3,
    kFxEq         = 1u << 4,
    kFxFilter     = 1u << 5,
    kFxFlanger    = 1u << 6,
    kFxPhaser     = 1u << 7,
    kFxReverb     = 1u << 8,
    // the Serum modules of state version 3 (docs/ui-design/13-new-keys.md 4.12): effect slots 9 .. 15
    kFxHyperDim   = 1u << 9,
    kFxUtility    = 1u << 10,
    kFxBode       = 1u << 11,
    kFxConvolve   = 1u << 12,
    kFxSplitterLh = 1u << 13,
    kFxSplitterLmh = 1u << 14,
    kFxSplitterMs = 1u << 15,
    kFxLegacy     = (1u << 9) - 1u,   // the nine effects of state version 2
    kFxAll        = (1u << 16) - 1u
};

/** The effects a tier may run, with the "force enable" of the reverb (Q27): the user trades polyphony for the reverb, so a tier that masks
    the reverb out does not remove it while `reverb.force_enable` is on. */
constexpr uint32_t effectiveFxMask (uint32_t tierMask, bool reverbForceEnable) noexcept
{
    return reverbForceEnable ? (tierMask | kFxReverb) : tierMask;
}

/**
 * Per-instance quality settings (never a global). A tier only CAPS what a preset asks for (voices, unison voices,
 * oversampling) or bypasses heavy effects; it never changes the stored parameter values, so a preset has the same shape
 * on every device and is just cheaper on the small ones. The numbers of a tier are targets, not measured claims
 * (docs/PERFORMANCE.md).
 */
struct Quality
{
    int maxVoices = 8;                                      // polyphony cap, 1 .. kMaxVoices
    int maxUnison = kMaxUnisonVoices;                       // cap of oscN.unison.voices, 1 .. kMaxUnisonVoices
    int oversampling = 2;                                   // 1 or 2
    Interpolation interpolation = Interpolation::linear;    // wavetable frame interpolation
    uint32_t fxMask = kFxAll;                               // effects that may be active
    /** false (default): offline / deterministic. Work that an engine does on a helper thread (the Convolve rebuild of engine-vital) is
        handed over at a fixed block boundary: the audio thread waits for it, so the output never depends on thread timing and an offline
        bounce equals itself. true: live playback, the audio thread never waits (the new convolver is taken over when it is ready). */
    bool realtime = false;
};

constexpr Quality qualityForTier (Tier tier) noexcept
{
    switch (tier)
    {
        // low: the heavy effects (reverb, flanger, and of state version 3 Hyper / Dimension, Bode, Convolve) are bypassed
        case Tier::low:    return Quality { 8, 4, 1, Interpolation::linear, kFxAll & ~(kFxReverb | kFxFlanger | kFxHyperDim | kFxBode | kFxConvolve) };
        case Tier::medium: return Quality { 16, 8, 2, Interpolation::linear, kFxAll };
        case Tier::high:   return Quality { kMaxVoices, kMaxUnisonVoices, 2, Interpolation::spectral, kFxAll };
    }
    return Quality {};
}

/** The tier the JUCE shell runs with (CMake: -DSYNTHYBOY_DEFAULT_TIER=low|medium|high, high on desktop). */
#ifndef SYNTHYBOY_DEFAULT_TIER
 #define SYNTHYBOY_DEFAULT_TIER high
#endif
constexpr Tier kDefaultTier = Tier::SYNTHYBOY_DEFAULT_TIER;
} // namespace synthyboy
