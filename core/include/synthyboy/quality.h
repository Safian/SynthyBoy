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
    kEconomyWarp    = 1u << 0, // a warped oscillator runs 2x oversampled with a 32 tap decimator (not 4x with 128 taps); its table band limit is 1.0 (not 3.2) times the host rate; distortion runs 1x without oversampling (not 2x with 15 samples latency)
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
    /** false (default): offline / deterministic. Work that an engine does on a helper thread (the Convolve rebuild) is
        handed over at a fixed block boundary: the audio thread waits for it, so the output never depends on thread timing and an offline
        bounce equals itself. true: live playback, the audio thread never waits (the new convolver is taken over when it is ready). */
    bool realtime = false;
    /** The economy groups of the own engine (EconomyFlags). Negative (default): by the tier, all groups when `oversampling` is 1 and none otherwise, which is what the
        three tiers always did. 0 .. kEconomyAll: exactly these groups, whatever the oversampling says. */
    int economy = -1;
    /** false (default): prepare () builds every built-in wavetable before it returns (what every shell did). true (needs `realtime`; the MPC shell sets it): prepare () builds only the one cycle tables and the
        tables the current parameters name; the others are built by a low priority thread, and a caller that applies a state calls IEngine::readyContent () (docs/MPC.md "Start-up time"). */
    bool lazyTables = false;
    /** true (default): an effect that is switched ON in the preset but bypassed by `fxMask` leaves its loudness behind as a gain at its place in the chain (the own engine,
        core/engine-own/fx/fx_makeup.h, docs/release/TIER-LEVEL-COMPENSATION.md): the sound keeps the level of the full quality, the character of the effect is still missing. Does nothing
        while no effect that is on is bypassed (the signal path is then the one of every earlier version). false: the bypassed effect leaves nothing (the levels of before the makeup). */
    bool fxMakeup = true;
    /** The note start of the Spectral oscillator is spread over the host blocks (core/spectral start_ledger.h, docs/plans/spectral-start.md): a chord whose start work does not fit the budget of one block
        starts its last voices some control periods later (they are silent until then). Negative (default): by the tier, on when `oversampling` is 1 (low). 0 off, 1 on. It needs the 32 sample
        event grid of the control economy (see spectralStartOf), without it the setting does nothing. */
    int spectralStart = -1;
    /** The budget of the Spectral start ledger, in microseconds of start work per block. 0 (default): the built-in `kStartBudgetUs` (core/spectral start_ledger.h). Any value > 0 is the budget
        itself (bench / measurement, e.g. tier_bench --start-budget-us); the profiles do not set it. */
    int spectralStartBudgetUs = 0;
    /** The start cache of the Spectral oscillator (core/spectral start_cache.h, docs/plans/spectral-start-cache.md): the first frames of a note are kept in a process-wide table of 3.2 MB and a note
        struck again with the same settings takes them from it. The sound is bit-identical to the cache off (only the work differs). Negative (default): by the tier, on when `oversampling` is 1 (low; owner decision
        2026-11-21), off for medium and high. 0 off, 1 on (bench / measurement, tier_bench --start-cache / --no-start-cache). */
    int spectralStartCache = -1;
    /** The FX cost budget (core/engine-own/fx/fx_budget.h, docs/perf/fx-budget.md): the most the effect instances that run may cost together, in percent of the time of a 128 frame block at 44.1 kHz on the
        Key 61 (the calibration of the cost table). 0 (default): unlimited, every effect the mask lets run runs, the sound is the one of every earlier version. > 0: the instances are admitted in the chain order
        (rack Main, Bus 1, Bus 2, then the order inside the rack) while the running sum of their costs stays within the budget; the rest are bypassed like an effect the mask switches off (the 10 ms crossfade,
        the loudness left as a gain). The parameters keep their values; the engine tells which instances are over budget (IEngine::fxBudgetStatus). */
    int fxBudgetPct = 0;
    /** Prepare-time opt-in for hardware with a process-fixed voice cap and overflow policy. The host prepares again before
        changing that profile. By default the full pool is available to setQuality() while playing. */
    bool fixedVoiceProfile = false;
};

/** The FX budget of tier low, percent of a block (Quality::fxBudgetPct). Measured on the MPC Key 61 (2026-11-21, tier low, Acid Saw Reso, 8 notes, block 2.902 ms, 3 runs, `.hw-local/mpc-perf/key61-night-2026-11-21/fx-stack/`):
    the base without effects takes 21.0 % of the block (median) and 44.5 % at p99; a stack of effects adds about 1.2 to 1.25 times its summed median cost at p99 (16 multiband Limit compressors: summed 154.7 %, p99 +186.3 %;
    the 16 dearest distinct configs: 82.4 % summed, p99 +99.0 %; two of the dearest of each type: 82.1 %, +102.7 %). With 35 % the stack adds at most 35 x 1.25 = 43.8 % at p99: 44.5 + 43.8 = 88.3 %, under the 90 % the
    block may take (a block over 100 % is a dropout; the 90 % leave room for the note starts, the display and a slower clock). 40 % would give 94.5 % and is not enough. The median is 21.0 + 35 = 56 %. */
constexpr int kFxBudgetLowPct = 35;

/** The economy groups a quality runs with (see Quality::economy). */
constexpr uint32_t economyOf (const Quality& q) noexcept
{
    return q.economy < 0 ? (q.oversampling <= 1 ? static_cast<uint32_t> (kEconomyAll) : 0u) : (static_cast<uint32_t> (q.economy) & static_cast<uint32_t> (kEconomyAll));
}

/** Whether the Spectral start stagger runs with this quality: the setting, and the control economy (its event grid makes the stagger independent of the host block size). */
constexpr bool spectralStartOf (const Quality& q) noexcept
{
    return (q.spectralStart < 0 ? q.oversampling <= 1 : q.spectralStart != 0) && (economyOf (q) & static_cast<uint32_t> (kEconomyControl)) != 0u;
}

/** Whether the Spectral start cache runs with this quality: the setting, by the tier (low) when it is negative. */
constexpr bool spectralStartCacheOf (const Quality& q) noexcept
{
    return q.spectralStartCache < 0 ? q.oversampling <= 1 : q.spectralStartCache != 0;
}

constexpr Quality qualityForTier (Tier tier) noexcept
{
    switch (tier)
    {
        // low: the heavy effects (reverb, flanger, and of state version 3 Hyper / Dimension, Bode, Convolve) are bypassed
        case Tier::low:
        {
            Quality q { 8, 4, 1, Interpolation::linear, kFxAll & ~(kFxReverb | kFxFlanger | kFxHyperDim | kFxBode | kFxConvolve) };
            q.fxBudgetPct = kFxBudgetLowPct;
            return q;
        }
        case Tier::medium: return Quality { 16, 8, 2, Interpolation::linear, kFxAll };
        case Tier::high:   return Quality { kMaxVoices, kMaxUnisonVoices, 2, Interpolation::spectral, kFxAll };
    }
    return Quality {};
}

/** The tier the shell runs with (CMake: -DSYNTHYBOY_DEFAULT_TIER=low|medium|high). */
#ifndef SYNTHYBOY_DEFAULT_TIER
 #define SYNTHYBOY_DEFAULT_TIER high
#endif
constexpr Tier kDefaultTier = Tier::SYNTHYBOY_DEFAULT_TIER;
} // namespace synthyboy
