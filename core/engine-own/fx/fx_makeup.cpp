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
constexpr float kMeterCornerHz[4] = { 120.0f, 480.0f, 1900.0f, 7600.0f };
constexpr float kMeterKWeight[kMeterBands] = { 1.0f, 1.0f, 1.0f, 1.35f, 2.1f }; // the shelf of the K weighting above 1.7 kHz, as a gain of the energy of a band (scripts/tier-makeup-regress.py)

int meterBandOf (float hz) noexcept
{
    int b = 0;
    while (b < 4 && hz > kMeterCornerHz[b])
        ++b;
    return b;
}
} // namespace

ProgramDesc standardProgram() noexcept
{
    static const ProgramDesc d = []
    {
        ProgramDesc out;
        double e[kMeterBands] = {}, total = 0.0;
        for (int b = 0; b < kBands; ++b)
            e[meterBandOf (bandFreq (b))] += bandWeight (b);
        for (int i = 0; i < kMeterBands; ++i)
        {
            e[i] *= kMeterKWeight[i];
            total += e[i];
        }
        for (int i = 0; i < kMeterBands; ++i)
            out.share[i] = static_cast<float> (e[i] / total);
        out.wide = 0.3f;
        out.valid = false;
        return out;
    }();
    return d;
}

float applyBands (const ProgramDesc& in, const BandGains& g) noexcept
{
    const ProgramDesc d = in.valid ? in : standardProgram();
    float p = 0.0f;
    for (int i = 0; i < kMeterBands; ++i)
        p += d.share[i] * ((1.0f - d.wide) * g.mono[i] + d.wide * g.wide[i]);
    return p;
}

void ProgramMeter::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    for (int i = 0; i < 4; ++i)
        coef[i] = static_cast<float> (1.0 - std::exp (-kTwoPi * kMeterCornerHz[i] / sampleRate));
    reset();
}

void ProgramMeter::reset() noexcept
{
    for (auto& v : lp) v = 0.0f;
    for (auto& v : sum) v = 0.0;
    sumSide = 0.0;
    count = 0;
    for (auto& v : energy) v = 0.0;
    energySide = 0.0;
    slow = peak = fast = 0.0;
    seen = false;
}

void ProgramMeter::accumulate (const float* l, const float* r, int n) noexcept
{
    float a[kMeterBands] = {};
    float side = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]), sd = 0.5f * (l[i] - r[i]);
        side += sd * sd;
        lp[0] += coef[0] * (m - lp[0]);
        lp[1] += coef[1] * (m - lp[1]);
        lp[2] += coef[2] * (m - lp[2]);
        lp[3] += coef[3] * (m - lp[3]);
        const float b0 = lp[0], b1 = lp[1] - lp[0], b2 = lp[2] - lp[1], b3 = lp[3] - lp[2], b4 = m - lp[3];
        a[0] += b0 * b0; a[1] += b1 * b1; a[2] += b2 * b2; a[3] += b3 * b3; a[4] += b4 * b4;
    }
    for (int i = 0; i < kMeterBands; ++i)
        sum[i] += a[i];
    sumSide += side;
    count += n;
    for (auto& v : lp) v = flushDenormal (v);
}

void ProgramMeter::endPeriod (int periodSamples) noexcept
{
    if (count <= 0)
        return;
    double total = sumSide;
    for (int i = 0; i < kMeterBands; ++i)
        total += sum[i];
    {
        // the steadiness: a slow mean (a second) against the held peak of the loudest 100 ms (silence counts: a pluck line leaves gaps)
        const double kSlow = 1.0 - std::exp (-static_cast<double> (periodSamples) / (1.0 * sr)), kHold = std::exp (-static_cast<double> (periodSamples) / (4.0 * sr));
        const double inst = total / static_cast<double> (count);
        slow += (inst - slow) * kSlow;
        fast += (inst - fast) * (1.0 - std::exp (-static_cast<double> (periodSamples) / (0.1 * sr)));
        peak = std::max (fast, peak * kHold);
    }
    if (total > 1.0e-12 * static_cast<double> (count))
    {
        const double k = seen ? 1.0 - std::exp (-static_cast<double> (periodSamples) / (0.1 * sr)) : 1.0;
        for (int i = 0; i < kMeterBands; ++i)
            energy[i] += (sum[i] / static_cast<double> (count) - energy[i]) * k;
        energySide += (sumSide / static_cast<double> (count) - energySide) * k;
        seen = true;
    }
    for (auto& v : sum) v = 0.0;
    sumSide = 0.0;
    count = 0;
}

ProgramDesc ProgramMeter::desc() const noexcept
{
    ProgramDesc d;
    if (!seen)
        return d;
    double total = 0.0, raw = energySide;
    double w[kMeterBands];
    for (int i = 0; i < kMeterBands; ++i)
    {
        w[i] = energy[i] * kMeterKWeight[i];
        total += w[i];
        raw += energy[i];
    }
    if (total <= 0.0)
        return d;
    for (int i = 0; i < kMeterBands; ++i)
        d.share[i] = static_cast<float> (w[i] / total);
    d.wide = static_cast<float> (std::min (1.0, 2.0 * energySide / std::max (raw, 1.0e-30)));
    d.steady = peak > 1.0e-18 ? static_cast<float> (std::clamp (slow / peak, 0.0, 1.0)) : 0.4f;
    d.valid = true;
    return d;
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

float reverbPower (float mixPct, float wetDb, const ProgramDesc& d) noexcept
{
    const float kappa = kReverbLoudnessPluck + (kReverbLoudnessHeld - kReverbLoudnessPluck) * clamp01 (d.steady);
    return equalPowerMix (mixPct * 0.01f, kappa * fromDb (wetDb));
}

namespace
{
/** One cell of a band table (dB, mono five then wide five) as gains in power. */
BandGains fromCell (const float* cell) noexcept
{
    BandGains g;
    for (int i = 0; i < kMeterBands; ++i)
    {
        g.mono[i] = fromDb (cell[i]);
        g.wide[i] = fromDb (cell[kMeterBands + i]);
    }
    return g;
}
BandGains mixGains (const BandGains& a, const BandGains& b, float t) noexcept
{
    BandGains g;
    for (int i = 0; i < kMeterBands; ++i)
    {
        g.mono[i] = a.mono[i] + (b.mono[i] - a.mono[i]) * t;
        g.wide[i] = a.wide[i] + (b.wide[i] - a.wide[i]) * t;
    }
    return g;
}
} // namespace

BandGains flangerBandGains (float feedbackPct, float centreHz) noexcept
{
    using namespace tables;
    int i, j;
    float fi, fj;
    locate (kFlangerFbPct, kFlangerFb, std::clamp (feedbackPct, -100.0f, 100.0f), i, fi);
    float lg[kFlangerCentre];
    for (int k = 0; k < kFlangerCentre; ++k)
        lg[k] = std::log (kFlangerCentreHz[k]);
    locate (lg, kFlangerCentre, std::clamp (std::log (std::max (centreHz, 20.0f)), lg[0], lg[kFlangerCentre - 1]), j, fj);
    return mixGains (mixGains (fromCell (kFlangerBands[i][j]), fromCell (kFlangerBands[i][j + 1]), fj),
                     mixGains (fromCell (kFlangerBands[i + 1][j]), fromCell (kFlangerBands[i + 1][j + 1]), fj), fi);
}

float flangerPower (float mixPct, const BandGains& wet, const ProgramDesc& d) noexcept
{
    return equalPowerMix (mixPct * 0.01f, kFlangerLoudness * applyBands (d, wet));
}

BandGains hyperBandGains (int unison, float hyperMixPct, float dimMixPct) noexcept
{
    using namespace tables;
    const int u = std::clamp (unison, 0, kHyperUnison - 1);
    int i, j;
    float fi, fj;
    locate (kHyperMixPct, kHyperMix, std::clamp (hyperMixPct, 0.0f, 100.0f), i, fi);
    locate (kHyperDimPct, kHyperDim, std::clamp (dimMixPct, 0.0f, 100.0f), j, fj);
    return mixGains (mixGains (fromCell (kHyperBands[u][i][j]), fromCell (kHyperBands[u][i][j + 1]), fj),
                     mixGains (fromCell (kHyperBands[u][i + 1][j]), fromCell (kHyperBands[u][i + 1][j + 1]), fj), fi);
}

float hyperPower (const BandGains& whole, const ProgramDesc& d) noexcept
{
    return std::max (applyBands (d, whole), 1.0e-4f) * kHyperLoudness;
}

BandGains bodeBandGains (float mixPct, float feedPct, float shiftHz) noexcept
{
    using namespace tables;
    const float m = clamp01 (mixPct * 0.01f);
    int i;
    float f;
    locate (kBodeFeedPct, kBodeFeed, std::clamp (feedPct, 0.0f, 100.0f), i, f);
    const BandGains ws = mixGains (fromCell (kBodeBands[0][i]), fromCell (kBodeBands[0][i + 1]), f);
    const BandGains wp = mixGains (fromCell (kBodeBands[1][i]), fromCell (kBodeBands[1][i + 1]), f);
    // a shift under a few Hz leaves the wet path coherent with the dry one (the wet path is a comb of the program, power gain W: amplitude sqrt (W) in phase), over 20 Hz the two are unrelated
    const float t = clamp01 ((shiftHz - 2.0f) / 18.0f);
    BandGains g;
    for (int k = 0; k < kMeterBands; ++k)
    {
        const float cm = (1.0f - m) + m * std::sqrt (wp.mono[k]), cw = (1.0f - m) + m * std::sqrt (wp.wide[k]);
        g.mono[k] = (1.0f - t) * cm * cm + t * ((1.0f - m) * (1.0f - m) + m * m * ws.mono[k]);
        g.wide[k] = (1.0f - t) * cw * cw + t * ((1.0f - m) * (1.0f - m) + m * m * ws.wide[k]);
    }
    return g;
}

float bodePower (const BandGains& whole, bool monoInput, const ProgramDesc& d) noexcept
{
    float p = applyBands (d, whole) * kBodeLoudness;
    if (monoInput)
        p *= 1.0f - 0.5f * (d.valid ? d.wide : standardProgram().wide); // the mono sum (L + R) / 2 keeps the mid and drops the side, dry and wet alike
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

void convolveBandPowers (const BandSpectrum& spec, const WetShaping& sh, float mix, double sampleRate, float (&out)[kBands], float (&coherent)[kBands]) noexcept
{
    if (!spec.valid)
    {
        for (auto& v : out) v = 1.0f;
        for (auto& v : coherent) v = 1.0f;
        return;
    }
    const float a = clamp01 (mix) * (kPif * 0.5f);
    const float c = std::cos (a), s = std::sin (a);
    const bool tone = sh.tone != 0.0f;
    const float lowGain = std::pow (10.0f, -sh.tone * 6.0f / 20.0f), highGain = std::pow (10.0f, sh.tone * 6.0f / 20.0f);
    const double alpha = 1.0 - std::exp (-kTwoPi * (sh.serumLaw ? std::max (sh.serumCornerHz, 1.0f) : 1000.0f) / sampleRate);
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
        coherent[b] = static_cast<float> (c + s * hr);
        out[b] = static_cast<float> (std::max (static_cast<double> (c) * c + 2.0 * c * s * hr + static_cast<double> (s) * s * pw, 0.0));
    }
}

static float weightedMean (const float (&v)[kBands], const ProgramDesc& d) noexcept
{
    float weight[kBands];
    if (d.valid)
    {
        double inBand[kMeterBands] = {};
        for (int b = 0; b < kBands; ++b)
            inBand[meterBandOf (bandFreq (b))] += table().weight[b];
        for (int b = 0; b < kBands; ++b)
        {
            const int mb = meterBandOf (bandFreq (b));
            weight[b] = static_cast<float> (d.share[mb] * table().weight[b] / std::max (inBand[mb], 1.0e-30));
        }
    }
    else
        for (int b = 0; b < kBands; ++b)
            weight[b] = table().weight[b];
    double total = 0.0;
    for (int b = 0; b < kBands; ++b)
        total += static_cast<double> (weight[b]) * v[b];
    return static_cast<float> (total);
}

float convolvePower (const float (&bandPower)[kBands], const ProgramDesc& d) noexcept { return std::max (weightedMean (bandPower, d), 1.0e-6f); }
float convolveCoherent (const float (&coherent)[kBands], const ProgramDesc& d) noexcept { return weightedMean (coherent, d); }
} // namespace makeup
} // namespace own
} // namespace synthyboy
