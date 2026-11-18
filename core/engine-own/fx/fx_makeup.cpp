// SynthyBoy - own engine: the loudness models of the effects that a quality tier bypasses (docs/release/TIER-LEVEL-COMPENSATION.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fx_makeup.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../dsp/util.h"
#include "fft.h"
#include "fx_makeup_tables.h"

namespace synthyboy
{
namespace own
{
namespace makeup
{
namespace
{
constexpr double kLowestHz = 30.0;
constexpr double kBandsPerOctave = 6.0;

struct Table
{
    float freq[kBands], weight[kBands];
    Table()
    {
        double sum = 0.0;
        double w[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            const double f = kLowestHz * std::exp2 (b / kBandsPerOctave);
            freq[b] = static_cast<float> (f);
            w[b] = std::pow (f, -1.5) * f; // power per Hz x bandwidth (proportional to f)
            sum += w[b];
        }
        for (int b = 0; b < kBands; ++b)
            weight[b] = static_cast<float> (w[b] / sum);
    }
};
const Table& table() noexcept
{
    static const Table t;
    return t;
}
} // namespace

float bandFreq (int band) noexcept { return table().freq[std::clamp (band, 0, kBands - 1)]; }
float bandWeight (int band) noexcept { return table().weight[std::clamp (band, 0, kBands - 1)]; }

float equalPowerMix (float mix, float wetPower) noexcept
{
    const float a = clamp01 (mix) * (kPif * 0.5f);
    const float c = std::cos (a), s = std::sin (a);
    return c * c + s * s * wetPower;
}

float linearMix (float mix, float wetPower, float cross) noexcept
{
    const float m = clamp01 (mix);
    return (1.0f - m) * (1.0f - m) + m * m * wetPower + 2.0f * m * (1.0f - m) * cross;
}

namespace
{
/** Linear interpolation of ys over xs (ascending), clamped at both ends. */
float interp (const float* xs, const float* ys, int n, float x) noexcept
{
    if (x <= xs[0])
        return ys[0];
    for (int i = 1; i < n; ++i)
        if (x <= xs[i])
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    return ys[n - 1];
}

/** Position of x in the ascending grid xs as an index and a fraction (clamped). */
void locate (const float* xs, int n, float x, int& i, float& f) noexcept
{
    if (x <= xs[0]) { i = 0; f = 0.0f; return; }
    if (x >= xs[n - 1]) { i = n - 2; f = 1.0f; return; }
    i = 0;
    while (i < n - 2 && x > xs[i + 1])
        ++i;
    f = (x - xs[i]) / (xs[i + 1] - xs[i]);
}

float fromDb (float db) noexcept { return std::pow (10.0f, db * 0.1f); }
} // namespace

float reverbWetDb (const ReverbIn& in) noexcept
{
    using namespace tables;
    const int type = std::clamp (in.type, 0, 5);
    const int idx = type == 4 ? 5 + std::clamp (in.room, 0, 4) : (type == 5 ? 4 : type);
    float decay = std::clamp (in.decayMs, 100.0f, 100000.0f);
    if (type == 4 || type == 5)
        decay = std::clamp (decay * std::exp2 ((std::clamp (in.feedbackPct, 0.0f, 100.0f) * 0.01f - 0.5f) * 4.0f), 50.0f, 120000.0f);
    // the decay grid is a doubling from 100 ms: the position is log2 (decay / 100)
    float lt = std::log2 (std::max (decay, 100.0f) / 100.0f);
    lt = std::min (lt, static_cast<float> (kReverbT - 1));
    const int i = std::min (static_cast<int> (lt), kReverbT - 2);
    const float ft = lt - static_cast<float> (i);
    int j;
    float fs;
    locate (kReverbSizePct, kReverbSize, std::clamp (in.size, 0.0f, 100.0f), j, fs);
    const auto at = [&] (int a, int b) { return kReverbDb[idx][a][b]; };
    float db = (1.0f - ft) * ((1.0f - fs) * at (i, j) + fs * at (i, j + 1)) + ft * ((1.0f - fs) * at (i + 1, j) + fs * at (i + 1, j + 1));
    // the pre filters (measured on types 0, 2 and 4: the same within 1.5 dB at the corners, averaged), relative to the neutral reverb (low cut 20 Hz, high cut 8 kHz)
    static const float kLcHz[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 12000.0f };
    static const float kLcDb[] = { 0.0f, -1.3f, -3.0f, -5.1f, -8.3f, -11.4f, -15.6f, -23.1f, -35.0f };
    static const float kHcHz[] = { 20.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 12000.0f };
    static const float kHcDb[] = { -12.0f, -6.0f, -2.6f, -1.2f, -0.6f, -0.3f, -0.1f, 0.0f, 0.03f };
    db += interp (kLcHz, kLcDb, 9, std::clamp (in.lowCutHz, 20.0f, 12000.0f));
    db += interp (kHcHz, kHcDb, 9, std::clamp (in.highCutHz, 20.0f, 12000.0f));
    // the shelves: the high shelf (default -1 dB at 1.4 kHz) moves the power by 0.05 dB per dB, 0.2 dB per dB when it sits at 300 Hz; the low shelf by -0.48 dB per dB
    const float hsCoef = 0.05f + 0.15f * std::clamp (std::log (1400.0f / std::max (in.highShelfHz, 20.0f)) / std::log (1400.0f / 300.0f), 0.0f, 1.0f);
    db += hsCoef * (std::clamp (in.highShelfDb, -6.0f, 0.0f) + 1.0f);
    db += 0.48f * std::clamp (in.lowShelfDb, -6.0f, 0.0f);
    // the width scales the side of the wet signal: mid and side carry equal power, so the power is (1 + w^2) / 2 (measured: 2.1 dB at 150 %, 4.0 dB at 200 %)
    const float w = std::clamp (in.widthPct, 0.0f, 200.0f) * 0.01f;
    db += 10.0f * std::log10 ((1.0f + w * w) * 0.5f);
    return db;
}

float reverbPower (const ReverbIn& in) noexcept
{
    return equalPowerMix (in.mix * 0.01f, kReverbLoudness * fromDb (reverbWetDb (in)));
}

float flangerPower (float mixPct, float feedbackPct, float centreHz) noexcept
{
    using namespace tables;
    int i, j;
    float fi, fj;
    locate (kFlangerFbPct, kFlangerFb, std::clamp (feedbackPct, -100.0f, 100.0f), i, fi);
    // the centre grid is logarithmic: interpolate in log frequency
    float lg[kFlangerCentre];
    for (int k = 0; k < kFlangerCentre; ++k)
        lg[k] = std::log (kFlangerCentreHz[k]);
    locate (lg, kFlangerCentre, std::clamp (std::log (std::max (centreHz, 20.0f)), lg[0], lg[kFlangerCentre - 1]), j, fj);
    const auto at = [&] (int a, int b) { return kFlangerDb[a][b]; };
    const float db = (1.0f - fi) * ((1.0f - fj) * at (i, j) + fj * at (i, j + 1)) + fi * ((1.0f - fj) * at (i + 1, j) + fj * at (i + 1, j + 1));
    return equalPowerMix (mixPct * 0.01f, kFlangerLoudness * fromDb (db));
}

float hyperPower (int unison, float hyperMixPct, float dimMixPct) noexcept
{
    using namespace tables;
    const int u = std::clamp (unison, 0, kHyperUnison - 1);
    int i, j;
    float fi, fj;
    locate (kHyperMixPct, kHyperMix, std::clamp (hyperMixPct, 0.0f, 100.0f), i, fi);
    locate (kHyperDimPct, kHyperDim, std::clamp (dimMixPct, 0.0f, 100.0f), j, fj);
    const auto at = [&] (int a, int b) { return kHyperDb[u][a][b]; };
    const float db = (1.0f - fi) * ((1.0f - fj) * at (i, j) + fj * at (i, j + 1)) + fi * ((1.0f - fj) * at (i + 1, j) + fj * at (i + 1, j + 1));
    return fromDb (std::max (db, -40.0f)) * kHyperLoudness;
}

float bodePower (float mixPct, float feedPct, float shiftHz, bool monoInput) noexcept
{
    using namespace tables;
    const float m = clamp01 (mixPct * 0.01f);
    const float feed = std::clamp (feedPct, 0.0f, 100.0f);
    const float wShifted = fromDb (interp (kBodeFeedPct, kBodeDb[0], kBodeFeed, feed)) * kBodeLoudness;
    const float wPlain = fromDb (interp (kBodeFeedPct, kBodeDb[1], kBodeFeed, feed));
    // a shift under a few Hz leaves the wet path coherent with the dry one (the wet path is a comb of the program), over 20 Hz the two are unrelated
    const float t = clamp01 ((shiftHz - 2.0f) / 18.0f);
    const float coherent = (1.0f - m) + m * std::sqrt (wPlain);
    const float incoherent = (1.0f - m) * (1.0f - m) + m * m * wShifted;
    float p = (1.0f - t) * coherent * coherent + t * incoherent;
    if (monoInput)
        p *= 0.85f; // the channels of the program share 70 % of their power: the mono sum (L + R) / 2 has (1 + 0.7) / 2 of it, dry and wet alike
    return p;
}

bool irBandSpectrum (const float* irL, const float* irR, int frames, double sampleRate, BandSpectrum& out)
{
    out = BandSpectrum();
    if (irL == nullptr || irR == nullptr || frames <= 0 || sampleRate <= 0.0)
        return false;
    int n = 1 << 16;
    while (n < frames && n < (1 << 21))
        n <<= 1;
    fx::Radix2FFT fft (n);
    std::vector<fx::Complex> spec (static_cast<size_t> (n));
    const int usable = std::min (frames, n);
    const double edge = std::exp2 (1.0 / (2.0 * kBandsPerOctave));
    double power[kBands] = {}, re[kBands] = {}, im[kBands] = {};
    int count[kBands] = {};
    for (int ch = 0; ch < 2; ++ch)
    {
        fft.forwardReal (ch == 0 ? irL : irR, usable, spec.data());
        for (int b = 0; b < kBands; ++b)
        {
            const double lo = table().freq[b] / edge, hi = table().freq[b] * edge;
            const int k0 = std::max (1, static_cast<int> (std::ceil (lo * n / sampleRate)));
            const int k1 = std::min (n / 2 - 1, static_cast<int> (std::floor (hi * n / sampleRate)));
            if (k1 < k0)
            {
                // a band narrower than a bin (cannot happen at 64 k, kept for safety): the nearest bin
                const int k = std::clamp (static_cast<int> (std::lround (table().freq[b] * n / sampleRate)), 1, n / 2 - 1);
                power[b] += spec[static_cast<size_t> (k)].norm();
                re[b] += spec[static_cast<size_t> (k)].r;
                im[b] += spec[static_cast<size_t> (k)].i;
                ++count[b];
                continue;
            }
            for (int k = k0; k <= k1; ++k)
            {
                power[b] += spec[static_cast<size_t> (k)].norm();
                re[b] += spec[static_cast<size_t> (k)].r;
                im[b] += spec[static_cast<size_t> (k)].i;
                ++count[b];
            }
        }
    }
    for (int b = 0; b < kBands; ++b)
    {
        const double c = std::max (count[b], 1);
        out.power[b] = static_cast<float> (power[b] / c);
        out.re[b] = static_cast<float> (re[b] / c);
        out.im[b] = static_cast<float> (im[b] / c);
    }
    out.valid = true;
    return true;
}

float convolvePower (const BandSpectrum& spec, const WetShaping& sh, float mix, double sampleRate) noexcept
{
    if (!spec.valid)
        return 1.0f;
    const float a = clamp01 (mix) * (kPif * 0.5f);
    const float c = std::cos (a), s = std::sin (a);
    const bool tone = sh.tone != 0.0f;
    const float lowGain = std::pow (10.0f, -sh.tone * 6.0f / 20.0f), highGain = std::pow (10.0f, sh.tone * 6.0f / 20.0f);
    const double alpha = 1.0 - std::exp (-kTwoPi * (sh.serumLaw ? std::max (sh.serumCornerHz, 1.0f) : 1000.0f) / sampleRate);
    double total = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        const double w = kTwoPi * table().freq[b] / sampleRate; // rad per sample
        double hr = spec.re[b], hi = spec.im[b], pw = spec.power[b];
        if (tone)
        {
            // the one pole low pass L(z) = alpha / (1 - (1 - alpha) z^-1) at the band centre
            const double zr = std::cos (w), zi = -std::sin (w); // z^-1
            const double dr = 1.0 - (1.0 - alpha) * zr, di = -(1.0 - alpha) * zi;
            const double dd = dr * dr + di * di;
            const double lr = alpha * dr / dd, li = -alpha * di / dd;
            double tr, ti;
            if (sh.serumLaw)
            {
                tr = sh.tone < 0.0f ? lr : 1.0 - lr;
                ti = sh.tone < 0.0f ? li : -li;
            }
            else
            {
                tr = lowGain * lr + highGain * (1.0 - lr);
                ti = lowGain * li - highGain * li;
            }
            const double nr = hr * tr - hi * ti, ni = hr * ti + hi * tr;
            hr = nr;
            hi = ni;
            pw *= tr * tr + ti * ti;
        }
        if (sh.delaySeconds > 0.0f)
        {
            // pre delay: the phase of the band centre, and the loss of coherence across the width of the band (a sinc of half the phase spread)
            const double ph = w * sh.delaySeconds * sampleRate;
            const double spread = 0.5 * w * (std::exp2 (1.0 / kBandsPerOctave) - 1.0) * sh.delaySeconds * sampleRate;
            const double sinc = spread > 1.0e-6 ? std::sin (spread) / spread : 1.0;
            const double cr = std::cos (ph) * sinc, ci = -std::sin (ph) * sinc;
            const double nr = hr * cr - hi * ci, ni = hr * ci + hi * cr;
            hr = nr;
            hi = ni;
        }
        total += static_cast<double> (table().weight[b]) * (static_cast<double> (c) * c + 2.0 * c * s * hr + static_cast<double> (s) * s * pw);
    }
    return static_cast<float> (std::max (total, 1.0e-6));
}
} // namespace makeup
} // namespace own
} // namespace synthyboy
