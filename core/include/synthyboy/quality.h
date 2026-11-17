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

/** The cheaper forms of the own engine's DSP, one bit per group (Quality::economy). A tier with oversampling 1 (low) runs all of them, the other tiers none;
    a device profile (profileQuality below) may switch the groups one by one. What each group changes in the sound, with its error bounds, is listed in
    docs/MPC.md "Sound differences at tier low". The groups never touch a stored parameter value. */
enum EconomyFlags : uint32_t
{
    kEconomyWarp    = 1u << 0, // a warped oscillator runs 2x oversampled with a 32 tap decimator (not 4x with 128 taps); its table band limit is 1.0 (not 3.2) times the host rate
    kEconomyMorph   = 1u << 1, // the spectral morph cycle is rebuilt every 512 samples (not 256) from a transform of half the size
    kEconomyMath    = 1u << 2, // table / rational / polynomial forms of tanh, sin, cos, exp, pow, log2; interpolated filter setups, held filter / chorus / phaser coefficients, the cheaper noise draw
    kEconomyControl = 1u << 3, // unread modulators stepped every fourth control period, events on the 32 sample grid, a stolen voice faded by a short ramp, an effect rack on silence and an effect with mix 0 not processed
    kEconomyAll     = (1u << 4) - 1u
};

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
    /** The economy groups of the own engine (EconomyFlags). Negative (default): by the tier, all groups when `oversampling` is 1 and none otherwise, which is what the
        three tiers always did. 0 .. kEconomyAll: exactly these groups, whatever the oversampling says. */
    int economy = -1;
};

/** The economy groups a quality runs with (see Quality::economy). */
constexpr uint32_t economyOf (const Quality& q) noexcept
{
    return q.economy < 0 ? (q.oversampling <= 1 ? static_cast<uint32_t> (kEconomyAll) : 0u) : (static_cast<uint32_t> (q.economy) & static_cast<uint32_t> (kEconomyAll));
}

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
