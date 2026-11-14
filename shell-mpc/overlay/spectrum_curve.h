// SynthyBoy - MPC overlay: the spectrum CURVE. The data source of the spectrum view: samples of the plugin's own output (the ring of shell-mpc/spectrum.h, written by the audio
// thread, which is not touched) -> an FFT on the overlay thread -> 256 points on a logarithmic frequency axis, 20 Hz .. 20 kHz -> dB -> level (0 .. 1) on the scale of the 32 meters
// of the skin (kSpectrumFloorDb .. kSpectrumCeilingDb of spectrum_bands.h) -> fast attack / slow release smoothing per point in time.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The analysis, and why it is what it is:
//  - FFT of 4096 samples, Hann window (periodic). At 44.1 kHz that is 93 ms and a bin of 10.8 Hz: the partials of a note from about 40 Hz up are separate peaks (an FFT of 2048, as the
//    32 meters use, has 21.5 Hz bins and shows the lowest octave as one lump); a shorter window than 93 ms would be too jumpy for a picture drawn 30 times a second. The window
//    hops by one picture (about 33 ms), so consecutive analyses overlap by about 65 %: a new picture is mostly the old one moved on, which looks calm, and every picture still
//    has new samples (a hop of one picture is the cheapest rate that does not skip sound). Cost: one 4096 point FFT per picture, about 30 a second, only while sound plays.
//  - The mean of the window (a DC offset of the output) is taken out before the FFT: it is not sound, and the window would spread it over the lowest points.
//  - The tone level: the bins are first turned into S[k] = (|X[k-1]|^2 + |X[k]|^2 + |X[k+1]|^2) / 1.5, the power in the three bins of the Hann main lobe divided by the noise bandwidth
//    of the window (1.5 bins). A sine of amplitude A gives S = A^2 at the bin of its frequency whatever its offset from the bin centre (the scalloping loss of a plain bin read-out,
//    up to 1.4 dB, is gone) and the picture reads in dB of the sine amplitude, full scale = 0 dB, exactly the unit of the 32 meters. White noise of variance s^2 gives 12 s^2 / N.
//  - A point covers a stretch of the frequency axis (the log axis has 256 points from 20 Hz to 20 kHz; a point is 2.7 % wide). A point narrower than a bin (below about 400 Hz at
//    44.1 kHz) is interpolated between two bins. A point narrower than the main lobe of the window (up to 6 bins, about 1.5 kHz) takes the PEAK of S over its stretch, so a tone keeps
//    its level (within 0.1 dB measured, wherever it lies between two points). A point wider than 12 bins (above about 3 kHz) takes the MEAN of S over its stretch (the integral of the
//    linear interpolation of S, divided by the width): white noise is flat there, and the high end is calm; a narrow tone is spread over the stretch and reads lower by about
//    10 log10 (bins covered / 3) (5 dB at 6 kHz). In between the two are crossfaded.
//  - Light smoothing across frequency: a point is never lowered, only raised towards the mean of itself and its two neighbours (v' = max (v, (v[-1] + 2 v + v[+1]) / 4) in dB): the valleys
//    between partials fill a little and the picture is calmer, every peak keeps its height.
//  - In time: per point, level += (target - level) * (1 - exp (-dt / tau)), tau 20 ms upwards (fast attack) and 350 ms downwards (slow release), by the real time between two
//    analyses. A point that has fallen under 1/256 of the scale with a target at the floor goes to exactly 0, so silence settles at the floor and stays there.
//  - Silence: a window whose largest sample is under kSilentPeak (-80 dBFS, far below the floor of the scale), or a ring that did not move, is not analysed at all (no FFT): the targets
//    are the floor. When the curve has settled at the floor and the window is silent, step () does nothing but report "unchanged": no picture is needed, no CPU is spent.
// Not thread safe: the overlay thread only (the ring is the only thing shared with the audio thread).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "../spectrum.h"
#include "../spectrum_bands.h"
#include "../spectrum_fft.h"

namespace synthyboy_mpc { namespace overlay {

class SpectrumCurve
{
public:
    static constexpr int kFftSize = 4096;
    static constexpr int kPoints = 256;
    static constexpr double kFMin = 20.0, kFMax = 20000.0;
    static constexpr float kSilentPeak = 1.0e-4f;       // -80 dBFS
    static constexpr double kAttackMs = 20.0, kReleaseMs = 350.0;
    static constexpr float kSettle = 1.0f / 256.0f;     // a falling point under this goes to 0
    static constexpr int kPictureSteps = 512;           // the curve counts as changed when a point moved by a 1/512 of the scale (a quarter of a pixel on the 136 px high view)

    explicit SpectrumCurve (double sampleRate = 44100.0) { setSampleRate (sampleRate); }

    void setSampleRate (double sampleRate)
    {
        if (sampleRate == rate_ && fft_)
            return;
        rate_ = sampleRate > 8000.0 ? sampleRate : 44100.0;
        if (! fft_)
        {
            fft_ = std::make_unique<SpectrumFft> (kFftSize);
            mag_.assign (static_cast<size_t> (fft_->bins()), 0.0f);
            cum_.assign (static_cast<size_t> (fft_->bins()) + 1, 0.0);
            s_.assign (static_cast<size_t> (fft_->bins()), 0.0);
        }
        // the edges of the points in bins: point i covers [edge (i), edge (i + 1)] with edge (i) = kFMin * ratio^(i - 1/2), ratio = (kFMax / kFMin)^(1 / (kPoints - 1))
        const double ratio = std::pow (kFMax / kFMin, 1.0 / (kPoints - 1));
        const double binHz = rate_ / kFftSize, nyquistBin = kFftSize / 2.0;
        for (int i = 0; i <= kPoints; ++i)
            edgeBin_[static_cast<size_t> (i)] = std::min (kFMin * std::pow (ratio, i - 0.5) / binHz, nyquistBin);
        reset();
    }
    double sampleRate() const noexcept { return rate_; }

    /** The curve to the floor, as after silence (a new instance, a long gap). */
    void reset() noexcept
    {
        std::fill (level_, level_ + kPoints, 0.0f);
        std::fill (target_, target_ + kPoints, 0.0f);
        std::fill (shown_, shown_ + kPoints, static_cast<int16_t> (0));
    }

    /** One step from the ring of the audio thread: the newest kFftSize samples, or silence when the ring did not move since the last step (the host stopped calling) or the copy was
        overtaken by the writer. The sample rate is taken over first (a change resets the curve). */
    bool update (const SpectrumRing& ring, double sampleRate, double dtMs, bool* analysed = nullptr)
    {
        setSampleRate (sampleRate);
        const uint64_t pos = ring.position();
        const bool stale = pos == lastPos_;
        lastPos_ = pos;
        window_.resize (static_cast<size_t> (kFftSize));
        const bool ok = ! stale && ring.latest (window_.data(), static_cast<size_t> (kFftSize));
        return step (ok ? window_.data() : nullptr, dtMs, analysed);
    }

    /** One analysis step. `x`: the newest kFftSize samples (the oldest first), or null when the input counts as silence (the ring did not move). `dtMs`: the time since the last step.
        Returns true when the picture changed (some point moved by a picture step since the last true). `analysed` (if not null) tells whether an FFT ran. */
    bool step (const float* x, double dtMs, bool* analysed = nullptr)
    {
        bool silent = x == nullptr;
        if (! silent)
        {
            float peak = 0.0f;
            for (int i = 0; i < kFftSize; ++i)
                peak = std::max (peak, std::fabs (x[i]));
            silent = ! (peak >= kSilentPeak);   // also NaN
        }
        if (analysed)
            *analysed = ! silent;
        if (silent)
        {
            std::fill (target_, target_ + kPoints, 0.0f);
            if (settled_)
                return false;                   // nothing to compute, nothing moves
        }
        else
        {
            analyse (x);
            settled_ = false;
        }
        smooth (dtMs);
        bool allZero = true;
        for (int i = 0; i < kPoints; ++i)
            allZero = allZero && level_[i] == 0.0f && target_[i] == 0.0f;
        settled_ = allZero;
        return commitIfMoved();
    }

    /** The smoothed level of point i, 0 .. 1 (0 = floor of the scale, 1 = ceiling). */
    const float* level() const noexcept { return level_; }
    /** The level before the smoothing in time (what the last analysis found), 0 .. 1. */
    const float* target() const noexcept { return target_; }
    /** The frequency of point i in Hz. */
    static double pointHz (int i) noexcept { return kFMin * std::pow (kFMax / kFMin, static_cast<double> (i) / (kPoints - 1)); }
    bool settled() const noexcept { return settled_; }
    /** A hash of what is drawn (the quantised levels), for the status file. */
    uint32_t hash() const noexcept
    {
        uint32_t h = 2166136261u;
        for (int i = 0; i < kPoints; ++i)
            h = (h ^ static_cast<uint32_t> (static_cast<uint16_t> (shown_[i]))) * 16777619u;
        return h;
    }
    float maxLevel() const noexcept { return *std::max_element (level_, level_ + kPoints); }

private:
    void analyse (const float* x)
    {
        // the mean of the window is taken out first: a DC offset of the output (the filters and the effects leave a little) is not sound, and the window would spread it over the first points
        double sum = 0.0;
        for (int i = 0; i < kFftSize; ++i)
            sum += static_cast<double>(x[i]);
        const float mean0 = static_cast<float> (sum / kFftSize);
        centred_.resize (static_cast<size_t> (kFftSize));
        for (int i = 0; i < kFftSize; ++i)
            centred_[static_cast<size_t> (i)] = x[i] - mean0;
        fft_->magnitudes (centred_.data(), mag_.data());
        const int bins = fft_->bins();
        // S[k], the lobe power; cum_[k] = integral of the linear interpolation of S from bin 0 to bin k (trapezoids)
        for (int k = 0; k < bins; ++k)
        {
            const double a = k > 0 ? static_cast<double> (mag_[static_cast<size_t> (k - 1)]) : 0.0, b = mag_[static_cast<size_t> (k)], c = k + 1 < bins ? static_cast<double> (mag_[static_cast<size_t> (k + 1)]) : 0.0;
            s_[static_cast<size_t> (k)] = (a * a + b * b + c * c) / 1.5;
        }
        cum_[0] = 0.0;
        for (int k = 0; k + 1 < bins; ++k)
            cum_[static_cast<size_t> (k) + 1] = cum_[static_cast<size_t> (k)] + 0.5 * (s_[static_cast<size_t> (k)] + s_[static_cast<size_t> (k) + 1]);
        float db[kPoints];
        for (int i = 0; i < kPoints; ++i)
        {
            const double lo = edgeBin_[static_cast<size_t> (i)], hi = edgeBin_[static_cast<size_t> (i) + 1], hw = 0.5 * (hi - lo);
            double mean = hi - lo < 1.0e-6 ? at ((lo + hi) * 0.5) : (integral (hi) - integral (lo)) / (hi - lo);
            // a point narrower than the main lobe of the window (3 bins, 4 bins of the lobe's skirts) takes the PEAK of S over its stretch: a tone lies inside the stretch of one point or
            // two, and keeps its level wherever it lies between the points; wider than 12 bins it is the mean (noise is flat, the high end is calm); in between the two are crossfaded
            const double m = std::clamp ((6.0 - hw) / 4.5, 0.0, 1.0);
            if (m > 0.0)
            {
                double peak = std::max (at (lo), at (hi));
                for (int k = static_cast<int> (std::ceil (lo)); k <= static_cast<int> (std::floor (hi)); ++k)
                    peak = std::max (peak, s_[static_cast<size_t> (std::min (k, static_cast<int> (s_.size()) - 1))]);
                mean = (1.0 - m) * mean + m * peak;
            }
            db[i] = mean > 1.0e-18 ? static_cast<float> (10.0 * std::log10 (mean)) : -180.0f;
        }
        // light smoothing across frequency: raised towards the neighbours' mean, never lowered
        float sm[kPoints];
        for (int i = 0; i < kPoints; ++i)
        {
            const float l = db[i > 0 ? i - 1 : 0], r = db[i < kPoints - 1 ? i + 1 : kPoints - 1];
            sm[i] = std::max (db[i], 0.25f * l + 0.5f * db[i] + 0.25f * r);
        }
        for (int i = 0; i < kPoints; ++i)
            target_[i] = spectrumLevelFromDb (sm[i]);
    }

    /** S at a (fractional) bin position, linear between the bin centres. */
    double at (double pos) const noexcept
    {
        const int last = static_cast<int> (s_.size()) - 1;
        pos = std::clamp (pos, 0.0, static_cast<double> (last));
        const int k = std::min (static_cast<int> (pos), last - 1);
        const double f = pos - k;
        return s_[static_cast<size_t> (k)] * (1.0 - f) + s_[static_cast<size_t> (k) + 1] * f;
    }
    /** The integral of the interpolated S from bin 0 to the position. */
    double integral (double pos) const noexcept
    {
        const int last = static_cast<int> (s_.size()) - 1;
        pos = std::clamp (pos, 0.0, static_cast<double> (last));
        const int k = std::min (static_cast<int> (pos), last - 1);
        const double f = pos - k, a = s_[static_cast<size_t> (k)], b = s_[static_cast<size_t> (k) + 1];
        return cum_[static_cast<size_t> (k)] + a * f + 0.5 * (b - a) * f * f;
    }

    void smooth (double dtMs) noexcept
    {
        dtMs = std::clamp (dtMs, 1.0, 250.0);
        const float up = static_cast<float> (1.0 - std::exp (-dtMs / kAttackMs)), down = static_cast<float> (1.0 - std::exp (-dtMs / kReleaseMs));
        for (int i = 0; i < kPoints; ++i)
        {
            float& l = level_[i];
            l += (target_[i] > l ? up : down) * (target_[i] - l);
            if (target_[i] <= 0.0f && l < kSettle)
                l = 0.0f;
        }
    }

    bool commitIfMoved() noexcept
    {
        bool moved = false;
        for (int i = 0; i < kPoints; ++i)
        {
            const int16_t q = static_cast<int16_t> (std::lround (level_[i] * kPictureSteps));
            if (q != shown_[i])
            {
                shown_[i] = q;
                moved = true;
            }
        }
        return moved;
    }

    double rate_ = 0.0;
    std::unique_ptr<SpectrumFft> fft_;
    std::vector<float> mag_, window_, centred_;
    uint64_t lastPos_ = 0;
    std::vector<double> cum_, s_;
    std::array<double, kPoints + 1> edgeBin_ {};
    float level_[kPoints] = {};
    float target_[kPoints] = {};
    int16_t shown_[kPoints] = {};
    bool settled_ = true;
};

}} // namespace synthyboy_mpc::overlay
