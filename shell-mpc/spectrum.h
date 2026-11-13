// SynthyBoy - MPC shell: the live spectrum of the output (ring buffer written by the audio thread, analysis on the display thread).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The skin shows 32 columns (scripts/mpc-skin-extras/40_spectrum.py), each bound to a display-only plugin parameter "Spectrum n". This header holds the two parts that
// are not about the host:
//   SpectrumRing      single producer (the audio thread, push()), single consumer (the display thread, latest()). Samples are relaxed atomics (no data race, no lock, no
//                     allocation, no call to the host); the write position is released after the samples, acquired before them.
//   SpectrumAnalyser  display thread only. Takes the last 2048 samples, FFT with a Hann window (spectrum_fft.h), 32 logarithmic bands from about 30 Hz to 18 kHz
//                     (spectrum_bands.h), dB to level (-60 dB floor = 0, 0 dBFS = 1), fast rise and slow fall smoothing, quantised to 1/64 so that a small flutter does not
//                     make a report. A ring that did not move since the last tick (the host stopped calling) counts as silence.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "spectrum_bands.h"
#include "spectrum_fft.h"

namespace synthyboy_mpc
{
constexpr int kSpectrumCount = kSpectrumBands;   // display parameters "Spectrum 1" .. "Spectrum 32"

class SpectrumRing
{
public:
    static constexpr size_t kSize = 8192;        // a power of two, four times the analysis window

    /** Audio thread. The mono sum (l + r) / 2, so that full scale on both channels stays 1. `r` may be null (mono output). */
    void push (const float* l, const float* r, int n) noexcept
    {
        uint64_t h = head_.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i, ++h)
            buf_[h & (kSize - 1)].store (r != nullptr ? 0.5f * (l[i] + r[i]) : l[i], std::memory_order_relaxed);
        head_.store (h, std::memory_order_release);
    }

    uint64_t position() const noexcept { return head_.load (std::memory_order_acquire); }

    /** Display thread. Copies the newest `n` (<= kSize / 2) samples to `out`, the oldest first; samples that were never written read 0.
        False when the producer overwrote part of the copy (it ran more than kSize - n samples ahead meanwhile), `out` is then not valid. */
    bool latest (float* out, size_t n) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint64_t end = head_.load (std::memory_order_acquire);
            const uint64_t have = std::min<uint64_t> (end, n);
            for (size_t i = 0; i < n - have; ++i)
                out[i] = 0.0f;
            for (uint64_t i = 0; i < have; ++i)
                out[n - have + i] = buf_[(end - have + i) & (kSize - 1)].load (std::memory_order_relaxed);
            if (head_.load (std::memory_order_acquire) - (end - have) <= kSize)
                return true;
        }
        return false;
    }

private:
    std::atomic<float> buf_[kSize] {};
    std::atomic<uint64_t> head_ { 0 };
};

class SpectrumAnalyser
{
public:
    static constexpr float kAttack = 0.7f;       // share of the distance to a higher target that is covered in one tick (fast rise)
    static constexpr float kRelease = 0.15f;     // share of the distance to a lower target that is covered in one tick (slow fall, about 3 s from 1 to 0 at 10 ticks a second)

    SpectrumAnalyser() : fft_ (kSpectrumFftSize), window_ (static_cast<size_t> (kSpectrumFftSize)), mag_ (static_cast<size_t> (fft_.bins())) {}

    /** One tick from the ring. Returns true when the ring has not moved since the last tick (the input counted as silence). */
    bool tick (const SpectrumRing& ring, double sampleRate, float* quantised)
    {
        const uint64_t pos = ring.position();
        const bool stale = pos == lastPos_;
        lastPos_ = pos;
        if (stale || !ring.latest (window_.data(), window_.size()))
            std::fill (window_.begin(), window_.end(), 0.0f);
        tickSamples (window_.data(), sampleRate, quantised);
        return stale;
    }

    /** One tick from 2048 samples (the part of tick() that does not need the ring). `quantised`: 32 values, multiples of 1/64 in 0 .. 1. */
    void tickSamples (const float* x, double sampleRate, float* quantised)
    {
        bool silent = true;
        for (int i = 0; i < kSpectrumFftSize && silent; ++i)
            silent = x[i] == 0.0f;
        float db[kSpectrumBands];
        if (silent)
            std::fill (db, db + kSpectrumBands, -180.0f);
        else
        {
            fft_.magnitudes (x, mag_.data());
            spectrumBandDb (mag_.data(), fft_.bins(), sampleRate, db);
        }
        for (int k = 0; k < kSpectrumBands; ++k)
        {
            const float target = spectrumLevelFromDb (db[k]);
            float& s = smooth_[k];
            s += (target > s ? kAttack : kRelease) * (target - s);
            quantised[k] = spectrumQuantize (s);
        }
    }

    void reset() noexcept { std::fill (smooth_, smooth_ + kSpectrumBands, 0.0f); lastPos_ = 0; }

private:
    SpectrumFft fft_;
    std::vector<float> window_, mag_;
    float smooth_[kSpectrumBands] = {};
    uint64_t lastPos_ = 0;
};
} // namespace synthyboy_mpc
