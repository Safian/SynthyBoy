// SynthyBoy - param-audit: the metrics measured on a render (RMS, peak, centroid, 24 bands, balance, f0, envelope, zero crossings, click, modulation).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <complex>

#include "audit.h"

namespace paudit
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

void fft (std::vector<cd>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * kPi / static_cast<double> (len) * (inverse ? 1 : -1);
        const cd wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w (1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

double db (double lin) { return lin > 1.0e-9 ? 20.0 * std::log10 (lin) : -180.0; }

const char* const kNames[kNumMetrics] = { "rms_db", "peak_db", "centroid_hz", "balance_db", "f0_hz", "attack_ms", "decay_db", "release_ms", "zc_rate", "jump", "env_std_db", "env_hz",
                                          "b00", "b01", "b02", "b03", "b04", "b05", "b06", "b07", "b08", "b09", "b10", "b11", "b12", "b13", "b14", "b15", "b16", "b17", "b18", "b19", "b20", "b21", "b22", "b23" };
} // namespace

const char* metricName (int index) { return kNames[index]; }

double metricValue (const Metrics& m, int i)
{
    switch (i)
    {
        case 0: return m.rmsDb;
        case 1: return m.peakDb;
        case 2: return m.centroid;
        case 3: return m.balanceDb;
        case 4: return m.f0;
        case 5: return m.attackMs;
        case 6: return m.decayDb;
        case 7: return m.releaseMs;
        case 8: return m.zcRate;
        case 9: return m.jump;
        case 10: return m.envStdDb;
        case 11: return m.envHz;
        default: return m.band[i - 12];
    }
}

double metricThreshold (int i)
{
    switch (i)
    {
        case 0: case 1: return 0.1;      // dB
        case 2: return 5.0;              // Hz
        case 3: return 0.1;              // dB
        case 4: return 0.2;              // Hz (about 3 cents at 130 Hz)
        case 5: return 0.5;              // ms
        case 6: return 0.2;              // dB
        case 7: return 2.0;              // ms
        case 8: return 2.0;              // crossings per second
        case 9: return 0.02;             // relative jump
        case 10: return 0.1;             // dB
        case 11: return 0.2;             // Hz
        default: return 0.5;             // dB per band
    }
}

Metrics analyse (const Audio& a, const Script& s)
{
    Metrics m;
    const size_t n = a.l.size();
    m.hash = fnv (a.l.data(), n);
    m.hash = fnv (a.r.data(), n, m.hash);
    double peak = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        if (! std::isfinite (a.l[i]) || ! std::isfinite (a.r[i]))
        {
            m.finite = false;
            return m;
        }
        peak = std::max ({ peak, std::fabs (static_cast<double> (a.l[i])), std::fabs (static_cast<double> (a.r[i])) });
    }
    m.peakDb = db (peak);

    const size_t f0i = static_cast<size_t> (std::max (0, s.steadyFrom));
    const size_t f1i = std::min (n, static_cast<size_t> (std::max (s.steadyFrom, s.steadyTo)));
    const size_t len = f1i > f0i ? f1i - f0i : 0;

    // RMS, balance, zero crossings, click of the steady window
    double sl = 0.0, sr = 0.0, win_peak = 0.0, maxJump = 0.0;
    int zc = 0;
    for (size_t i = f0i; i < f1i; ++i)
    {
        sl += static_cast<double> (a.l[i]) * a.l[i];
        sr += static_cast<double> (a.r[i]) * a.r[i];
        win_peak = std::max ({ win_peak, std::fabs (static_cast<double> (a.l[i])), std::fabs (static_cast<double> (a.r[i])) });
        if (i > f0i)
        {
            maxJump = std::max ({ maxJump, std::fabs (static_cast<double> (a.l[i]) - a.l[i - 1]), std::fabs (static_cast<double> (a.r[i]) - a.r[i - 1]) });
            if (a.l[i - 1] < 0.0f && a.l[i] >= 0.0f)
                ++zc;
        }
    }
    if (len > 0)
    {
        const double rmsL = std::sqrt (sl / static_cast<double> (len)), rmsR = std::sqrt (sr / static_cast<double> (len));
        m.rmsDb = db (std::sqrt (0.5 * (rmsL * rmsL + rmsR * rmsR)));
        m.balanceDb = (rmsL > 1.0e-9 || rmsR > 1.0e-9) ? 20.0 * std::log10 ((rmsL + 1.0e-9) / (rmsR + 1.0e-9)) : 0.0;
        m.zcRate = zc / (static_cast<double> (len) / kSr);
        m.jump = win_peak > 1.0e-9 ? maxJump / win_peak : 0.0;
    }

    // spectrum (Hann, 16384 points from the start of the steady window) -> centroid and 24 bands; autocorrelation -> f0
    constexpr size_t N = 16384;
    if (len >= 4096)
    {
        const size_t take = std::min (N, len);
        std::vector<cd> xl (N), xr (N);
        for (size_t i = 0; i < take; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (take));
            xl[i] = a.l[f0i + i] * w;
            xr[i] = a.r[f0i + i] * w;
        }
        fft (xl, false);
        fft (xr, false);
        double num = 0.0, den = 0.0;
        double bands[kBands] = {};
        const double lo = 40.0, hi = 18000.0, ratio = std::pow (hi / lo, 1.0 / kBands);
        for (size_t k = 1; k < N / 2; ++k)
        {
            const double hz = static_cast<double> (k) * kSr / static_cast<double> (N);
            const double p = std::norm (xl[k]) + std::norm (xr[k]);
            num += hz * p;
            den += p;
            if (hz >= lo && hz < hi)
                bands[std::min (kBands - 1, static_cast<int> (std::log (hz / lo) / std::log (ratio)))] += p;
        }
        m.centroid = den > 1.0e-18 ? num / den : 0.0;
        for (int b = 0; b < kBands; ++b)
            m.band[b] = 10.0 * std::log10 (bands[b] / (static_cast<double> (take) * static_cast<double> (take)) + 1.0e-18);

        // autocorrelation of the mono signal (rectangular window, zero padded to 2N)
        std::vector<cd> ac (2 * N);
        for (size_t i = 0; i < take; ++i)
            ac[i] = 0.5 * (a.l[f0i + i] + a.r[f0i + i]);
        fft (ac, false);
        for (auto& v : ac)
            v = std::norm (v);
        fft (ac, true);
        const double r0 = ac[0].real();
        if (r0 > 1.0e-12)
        {
            const size_t minLag = static_cast<size_t> (kSr / 2000.0), maxLag = std::min (take / 2, static_cast<size_t> (kSr / 30.0));
            std::vector<double> r (maxLag + 2);
            for (size_t l = 0; l <= maxLag + 1; ++l)
                r[l] = ac[l].real() / r0 * static_cast<double> (take) / static_cast<double> (take - l);
            size_t l = 1;
            while (l < maxLag && r[l] > 0.0)
                ++l; // past the first zero crossing
            double best = 0.0;
            for (size_t k = std::max (l, minLag); k < maxLag; ++k)
                if (r[k] > r[k - 1] && r[k] >= r[k + 1])
                    best = std::max (best, r[k]);
            for (size_t k = std::max (l, minLag); k < maxLag && best > 0.2; ++k)
                if (r[k] > r[k - 1] && r[k] >= r[k + 1] && r[k] >= 0.9 * best)
                {
                    const double d = r[k - 1] - 2.0 * r[k] + r[k + 1];
                    const double off = d != 0.0 ? 0.5 * (r[k - 1] - r[k + 1]) / d : 0.0;
                    m.f0 = kSr / (static_cast<double> (k) + off);
                    break;
                }
        }
    }

    // envelope: 2 ms RMS blocks of the mono signal
    const size_t blk = static_cast<size_t> (0.002 * kSr);
    std::vector<double> env;
    for (size_t i = 0; i + blk <= n; i += blk)
    {
        double e = 0.0;
        for (size_t j = i; j < i + blk; ++j)
        {
            const double v = 0.5 * (static_cast<double> (a.l[j]) + a.r[j]);
            e += v * v;
        }
        env.push_back (std::sqrt (e / static_cast<double> (blk)));
    }
    const size_t offB = std::min (env.size(), static_cast<size_t> (std::max (0, s.offFrame)) / blk);
    double envPeak = 0.0;
    for (size_t i = 0; i < offB; ++i)
        envPeak = std::max (envPeak, env[i]);
    if (envPeak > 1.0e-9 && offB > 3)
    {
        size_t i = 0;
        while (i < offB && env[i] < 0.9 * envPeak)
            ++i;
        m.attackMs = 1000.0 * static_cast<double> (i * blk) / kSr;
        m.decayDb = db (env[offB - 2] / envPeak);
        size_t j = offB;
        while (j < env.size() && env[j] > envPeak * 0.01)
            ++j;
        m.releaseMs = 1000.0 * static_cast<double> ((j - offB) * blk) / kSr;
    }
    // modulation of the level in the steady window (10 ms RMS blocks): deviation in dB and the dominant rate (autocorrelation of the dB series)
    {
        const size_t b0 = f0i / blk, b1 = std::min (env.size(), f1i / blk);
        if (b1 > b0 + 20)
        {
            std::vector<double> ed;
            for (size_t i = b0; i < b1; ++i)
                ed.push_back (db (env[i]));
            double mean = 0.0;
            for (double v : ed)
                mean += v;
            mean /= static_cast<double> (ed.size());
            double var = 0.0;
            for (double v : ed)
                var += (v - mean) * (v - mean);
            m.envStdDb = std::sqrt (var / static_cast<double> (ed.size()));
            if (m.envStdDb > 0.05)
            {
                std::vector<double> r (ed.size() / 2);
                for (size_t l = 0; l < r.size(); ++l)
                {
                    double acc = 0.0;
                    for (size_t i = 0; i + l < ed.size(); ++i)
                        acc += (ed[i] - mean) * (ed[i + l] - mean);
                    r[l] = acc;
                }
                size_t l = 1;
                while (l + 1 < r.size() && r[l] > 0.0)
                    ++l;
                double best = -1e300;
                size_t bl = 0;
                for (size_t k = std::max<size_t> (l, 2); k + 1 < r.size(); ++k)
                    if (r[k] > r[k - 1] && r[k] >= r[k + 1] && r[k] > best)
                    {
                        best = r[k];
                        bl = k;
                    }
                if (bl > 0 && best > 0.3 * r[0])
                    m.envHz = 1.0 / (static_cast<double> (bl * blk) / kSr);
            }
        }
    }
    // tail level
    {
        const size_t t0 = n > static_cast<size_t> (0.1 * kSr) ? n - static_cast<size_t> (0.1 * kSr) : 0;
        double acc = 0.0;
        for (size_t i = t0; i < n; ++i)
            acc += 0.5 * (static_cast<double> (a.l[i]) * a.l[i] + static_cast<double> (a.r[i]) * a.r[i]);
        m.tailDb = db (std::sqrt (acc / static_cast<double> (std::max<size_t> (1, n - t0))));
    }
    return m;
}

std::vector<double> welch (const Audio& a, size_t from, size_t to, size_t nfft, int channel)
{
    std::vector<double> acc (nfft / 2 + 1, 0.0);
    int count = 0;
    to = std::min (to, a.l.size());
    for (size_t start = from; start + nfft <= to; start += nfft / 2)
    {
        std::vector<cd> x (nfft);
        for (size_t i = 0; i < nfft; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (nfft));
            const double v = channel == 0 ? a.l[start + i] : channel == 1 ? a.r[start + i] : 0.5 * (static_cast<double> (a.l[start + i]) + a.r[start + i]);
            x[i] = v * w;
        }
        fft (x, false);
        for (size_t k = 0; k <= nfft / 2; ++k)
            acc[k] += std::norm (x[k]);
        ++count;
    }
    for (auto& v : acc)
        v /= std::max (1, count);
    return acc;
}

double bandDb (const std::vector<double>& s, size_t nfft, double hz, double octaves)
{
    const double lo = hz / std::pow (2.0, octaves / 2.0), hi = hz * std::pow (2.0, octaves / 2.0);
    double sum = 0.0;
    int n = 0;
    for (size_t k = 1; k < s.size(); ++k)
    {
        const double f = static_cast<double> (k) * kSr / static_cast<double> (nfft);
        if (f >= lo && f <= hi)
        {
            sum += s[k];
            ++n;
        }
    }
    return 10.0 * std::log10 (std::max (sum / std::max (1, n), 1.0e-30));
}
} // namespace paudit
