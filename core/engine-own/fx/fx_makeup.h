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

/** The loudness factors of the models: the share of the steady state power of the wet path that the loudest 100 ms of a played note carries (a reverb tail builds up and falls, a
    swelling pad is ahead of its reverb), measured against the engine's own loudness figure (scripts/tier-makeup-population.py, docs/release/TIER-LEVEL-COMPENSATION.md section 4). */
constexpr float kReverbLoudness = 0.4f;
constexpr float kFlangerLoudness = 1.0f;
constexpr float kHyperLoudness = 1.0f;
constexpr float kBodeLoudness = 1.4f;

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
float reverbPower (const ReverbIn& in) noexcept;

/** Flanger: equal power crossfade, wet power by feedback (percent) and centre frequency (Hz). */
float flangerPower (float mixPct, float feedbackPct, float centreHz) noexcept;
/** Hyper / Dimension: the power of the whole module by unison choice (0 .. 7), hyper mix and dim mix (percent). */
float hyperPower (int unison, float hyperMixPct, float dimMixPct) noexcept;
/** Bode: linear crossfade; the wet power by feed (percent), shifted or not (the shift in Hz is |shift x range|); `monoInput` sums the input to mono first. */
float bodePower (float mixPct, float feedPct, float shiftHz, bool monoInput) noexcept;

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
float convolvePower (const BandSpectrum& spec, const WetShaping& shaping, float mix, double sampleRate) noexcept;
} // namespace makeup
} // namespace own
} // namespace synthyboy
