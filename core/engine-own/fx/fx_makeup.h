// SynthyBoy - own engine: the loudness models of the effects that a quality tier bypasses (docs/release/TIER-LEVEL-COMPENSATION.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace synthyboy
{
namespace own
{
namespace makeup
{
/** The program the models are calibrated for: sustained music, whose spectrum falls with f^-1.5 (power per Hz) from 30 Hz to 12 kHz (a pad or a bass note: most of the energy below
    1 kHz). A band is a sixth of an octave; the weight of band b in a mean over the program is `bandWeight (b)` = f^-1.5 x bandwidth. */
constexpr int kBands = 54;
float bandFreq (int band) noexcept;   // centre, Hz
float bandWeight (int band) noexcept; // sums to 1 over the bands

/** The equal power crossfade of the effects (`MixGains` of fx_reverb.h: dry cos, wet sin, a quarter turn over the knob) of a wet path with the mean power W against the dry path,
    wet and dry uncorrelated (a reverb, an echo cloud): cos^2 + sin^2 W. `mix` 0 .. 1. */
float equalPowerMix (float mix, float wetPower) noexcept;
/** The linear crossfade (1 - m) x + m w of Hyper (the Dimension part adds on top, hyperPower ()), wet power W, cross correlation of dry and wet c (as a power: E [x w]). */
float linearMix (float mix, float wetPower, float cross) noexcept;

/** The wet path of an effect that acts like a linear filter on the program, seen per band: the mean power gain |H|^2 and the mean of the complex gain H over the band (the mean of
    H is what dry and wet add up to when they are correlated: a short impulse response, a delay line). Convolve fills it from its impulse (on the helper thread), everything else is
    a model of its own. */
struct BandSpectrum
{
    float power[kBands] = {};
    float re[kBands] = {}, im[kBands] = {};
    bool valid = false;
};

/** The spectrum of an impulse response (the left and right channel, `frames` each, the sample rate of the effect chain). The bands are the means over the bins of an FFT at least 64 k
    long; the program weight is the one above. Not real time (an FFT of the length of the impulse). */
bool irBandSpectrum (const float* irL, const float* irR, int frames, double sampleRate, BandSpectrum& out);

/** What the models know about the program at the place of the bypassed effect: the K-weighted shares of the energy in five bands (corners 120, 480, 1900, 7600 Hz) of the mid signal, and
    how wide the signal is (0 mono, 1 independent channels), taken from the input of the module over the last 100 ms by a ProgramMeter. `valid` false: no signal seen yet, the models
    use the standard program (power per Hz falling with f^-1.5, channels sharing 70 % of their power). */
constexpr int kMeterBands = 5;
struct ProgramDesc
{
    float share[kMeterBands] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float wide = 0.3f;
    /** How sustained the program is: its energy over the last second against the loudest 100 ms of the last few seconds (1 a held note or a pad, near 0 a pluck or a hit); the reverb
        reaches its steady state power only for a sustained program. 0.4 for the standard program. */
    float steady = 0.4f;
    bool valid = false;
};
/** The standard program as a descriptor. */
ProgramDesc standardProgram() noexcept;

/** Gains of a wet or whole path per meter band, power, for a mono and for a wide input. */
struct BandGains
{
    float mono[kMeterBands], wide[kMeterBands];
};
/** The power gain of a path with `g` on the program `d`. */
float applyBands (const ProgramDesc& d, const BandGains& g) noexcept;

/** The control rate meter of the program at the place of a bypassed effect (the one control rate follower of the makeup): per sample it takes the mid signal through four one pole splits
    and sums the squares of the five bands and of the side (about 30 operations); once per control period `endPeriod` folds the sums into energies smoothed with a time constant of 100 ms
    (an effect that sees silence keeps what it knew). Audio thread only. */
class ProgramMeter
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void accumulate (const float* l, const float* r, int n) noexcept;
    void endPeriod (int periodSamples) noexcept;
    ProgramDesc desc() const noexcept;

private:
    double sr = 44100.0;
    float coef[4] = {};
    float lp[4] = {};
    double sum[kMeterBands] = {}, sumSide = 0.0;
    long count = 0;
    double energy[kMeterBands] = {}, energySide = 0.0;
    double slow = 0.0, fast = 0.0, peak = 0.0; // the energy over about a second, and the held loudest 100 ms (decaying over 4 s)
    bool seen = false;
    // the smoothing constants of endPeriod, made again only when the period length or the sample rate changes (prepare)
    int constPeriod = -1;
    double kSlowC = 0.0, kHoldC = 0.0, kFastC = 0.0;
};

/** The loudness factors of the models: the share of the steady state power of the wet path that the loudest 100 ms of a played note carries (a reverb tail builds up and falls, a
    swelling pad is ahead of its reverb), measured against the engine's own loudness figure (scripts/tier-makeup-population.py, docs/release/TIER-LEVEL-COMPENSATION.md section 4). */
constexpr float kReverbLoudnessPluck = 0.4f;    // the share of the steady state wet power in the loudest 100 ms of a program with steadiness 0 (measured on the factory presets: 0.4 whatever the steadiness of their gestures) ...
constexpr float kReverbLoudnessHeld = 1.0f;     // ... and 1 (a held note: the reverb has its steady state; steady 4 below)
constexpr float kReverbSteadyPower = 4.0f;      // the steepness: the gestures of 24 factory presets have a steadiness of 0.12 to 0.66 (median 0.47), one of 0.95 (a held saw chord): only a really held sound goes towards 1
constexpr float kFlangerLoudness = 1.0f;
constexpr float kHyperLoudness = 0.91f; // the bias of the band model over 36 programs (+0.43 dB loudest 100 ms): section 8
constexpr float kBodeLoudness = 1.0f;

/** The keys of the reverb as the model reads them (plain values of the keys; mix in percent too). */
struct ReverbIn
{
    float mix = 25.0f, size = 50.0f, decayMs = 1500.0f, lowCutHz = 20.0f, highCutHz = 8000.0f, highShelfDb = -1.0f, highShelfHz = 1400.0f, lowShelfDb = 0.0f, widthPct = 100.0f, feedbackPct = 50.0f;
    int type = 0, room = 2;
};
/** The power of the reverb at mix 100 on the program, in dB: the table of the neutral reverb by type (room), decay and size (feedback scales the decay of types 4 and 5), then the curves of
    the low cut, high cut, shelves and width; the `reverbLoudness` factor of the time smear of the loudest 100 ms is not in it. */
float reverbWetDb (const ReverbIn& in) noexcept;
/** Program power of the reverb (equal power crossfade, wet and dry uncorrelated, the wet power scaled by kReverbLoudness). */
float reverbPower (float mixPct, float wetDb, const ProgramDesc& d) noexcept;

/** Flanger: equal power crossfade, wet power by feedback (percent) and centre frequency (Hz). */
BandGains flangerBandGains (float feedbackPct, float centreHz) noexcept;
float flangerPower (float mixPct, const BandGains& wet, const ProgramDesc& d) noexcept;
/** Hyper / Dimension: the power of the whole module by unison choice (0 .. 7), hyper mix and dim mix (percent). */
BandGains hyperBandGains (int unison, float hyperMixPct, float dimMixPct) noexcept;
float hyperPower (const BandGains& whole, const ProgramDesc& d) noexcept;
/** Bode: linear crossfade; the wet power by feed (percent), shifted or not (the shift in Hz is |shift x range|); `monoInput` sums the input to mono first. */
BandGains bodeBandGains (float mixPct, float feedPct, float shiftHz) noexcept;
float bodePower (const BandGains& whole, bool monoInput, const ProgramDesc& d) noexcept;

/** The tone and pre delay of Convolve on the wet path: the tilt of the engine's law (`serumLaw` false: +-6 dB around 1 kHz, a one pole split) or the first order low / high pass of the
    Serum law (corner `serumCornerHz`), and `delaySeconds` of pre delay. */
struct WetShaping
{
    float tone = 0.0f;           // -1 .. 1
    bool serumLaw = false;
    float serumCornerHz = 0.0f;
    float delaySeconds = 0.0f;
};

/** Program power of dry x cos(a) + wet x sin(a) with the wet path `spec` shaped by `shaping`, dry and wet added with their phases (a coherent sum per band), against the dry x alone.
    `mix` 0 .. 1 is the knob of the equal power law. */
void convolveBandPowers (const BandSpectrum& spec, const WetShaping& shaping, float mix, double sampleRate, float (&out)[kBands], float (&coherent)[kBands]) noexcept;
/** (`coherent` of convolveBandPowers is the amplitude with which the signal itself reaches the output per band, cos a + sin a Re H: what a copy of the signal on another path adds up with.)
    The program weighted mean of the band powers: the weight of a band is the share of its meter band of `d`, spread by the standard program (the standard program when `d` is not valid). */
float convolvePower (const float (&bandPower)[kBands], const ProgramDesc& d) noexcept;
float convolveCoherent (const float (&coherent)[kBands], const ProgramDesc& d) noexcept;

/** The weights of the means above for one program: they depend on the shares of `d` only, so the module keeps them and `updateWeights` rebuilds them only when the shares (or `valid`) change; the power and the
    coherent mean of a period then share one set. The two overloads give the bits of the two functions above. */
struct BandWeights
{
    bool built = false;
    bool valid = false;
    float share[kMeterBands] = {};
    float w[kBands] = {};
};
void updateWeights (BandWeights& weights, const ProgramDesc& d) noexcept;
float convolvePower (const float (&bandPower)[kBands], const BandWeights& weights) noexcept;
float convolveCoherent (const float (&coherent)[kBands], const BandWeights& weights) noexcept;

/** Tests only: the models and the meter compute everything per call, as before the caches (the constants of the meter, the weights, the key of Convolve, the level gains). Process wide. */
void setLegacyCachesForTest (bool on) noexcept;
bool legacyCaches() noexcept;
} // namespace makeup
} // namespace own
} // namespace synthyboy
