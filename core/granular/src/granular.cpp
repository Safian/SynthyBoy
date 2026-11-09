// SynthyBoy - core/granular: the granular oscillator DSP.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/granular/granular.h"

#include <algorithm>
#include <cmath>

namespace synthyboy
{
namespace granular
{
namespace
{
constexpr int kTableSize = 1024;
constexpr double kPi = 3.14159265358979323846;

double baseWindow (WindowShape shape, double x) noexcept
{
    switch (shape)
    {
        case WindowShape::hann:           return 0.5 - 0.5 * std::cos (2.0 * kPi * x);
        case WindowShape::welch:          return 1.0 - (2.0 * x - 1.0) * (2.0 * x - 1.0);
        case WindowShape::gaussian:       return std::exp (-0.5 * ((x - 0.5) / 0.16) * ((x - 0.5) / 0.16));
        case WindowShape::blackmanHarris: return 0.35875 - 0.48829 * std::cos (2.0 * kPi * x) + 0.14128 * std::cos (4.0 * kPi * x) - 0.01168 * std::cos (6.0 * kPi * x);
        case WindowShape::sinc:
        {
            const double u = 10.0 * (x - 0.5);
            return std::fabs (u) < 1.0e-9 ? 1.0 : std::sin (kPi * u) / (kPi * u);
        }
        case WindowShape::tukey:
        {
            const double alpha = 0.5, edge = std::min (x, 1.0 - x);
            return edge >= alpha * 0.5 ? 1.0 : 0.5 - 0.5 * std::cos (2.0 * kPi * edge / alpha);
        }
        case WindowShape::triangle:       return 1.0 - std::fabs (2.0 * x - 1.0);
        case WindowShape::trapezoid:      return std::min(1.0, std::min (x, 1.0 - x) / 0.25);
        case WindowShape::expDec:         return std::exp (-6.0 * x);
        case WindowShape::expDecRev:      return std::exp (-6.0 * (1.0 - x));
    }
    return 0.0;
}

struct Windows
{
    float table[kNumWindowShapes][kTableSize + 2];
    Windows()
    {
        for (int s = 0; s < kNumWindowShapes; ++s)
            for (int i = 0; i < kTableSize + 2; ++i)
                table[s][i] = static_cast<float> (baseWindow (static_cast<WindowShape> (s), std::min (1.0, static_cast<double> (i) / kTableSize)));
    }
};

const Windows& windows() noexcept
{
    static const Windows w;
    return w;
}

float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : (v == v ? v : 0.0f)); }

/** 4 point cubic Hermite read of one channel of a mip level; zero outside the data. */
float hermite (const float* d, int64_t n, double pos) noexcept
{
    const int64_t i = static_cast<int64_t> (std::floor (pos));
    const float t = static_cast<float> (pos - static_cast<double> (i));
    const auto at = [&] (int64_t k) { return k >= 0 && k < n ? d[k] : 0.0f; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void readSample (const SampleView& s, double pos, double rate, float& l, float& r) noexcept
{
    int m = 0;
    if (rate > 1.0 && s.numMipLevels > 1)
        m = std::min (s.numMipLevels - 1, static_cast<int> (std::floor (std::log2 (rate))));
    if (m > 0 && s.mipLevels != nullptr)
    {
        const auto& lvl = s.mipLevels[m];
        const double p = pos / static_cast<double> (1 << m);
        l = hermite (lvl.channels[0], lvl.numFrames, p);
        r = s.numChannels > 1 ? hermite (lvl.channels[1], lvl.numFrames, p) : l;
        return;
    }
    l = hermite (s.channelData[0], s.numFrames, pos);
    r = s.numChannels > 1 ? hermite (s.channelData[1], s.numFrames, pos) : l;
}
} // namespace

float windowValue (WindowShape shape, float x, float amount, float skew) noexcept
{
    x = clamp01 (x);
    amount = clamp01 (amount);
    skew = std::max (-1.0f, std::min (1.0f, skew == skew ? skew : 0.0f));
    const float peak = 0.5f + 0.45f * skew;
    const float u = x < peak ? 0.5f * x / peak : 0.5f + 0.5f * (x - peak) / (1.0f - peak);
    const float f = u * kTableSize;
    const int i = std::min (kTableSize - 1, static_cast<int> (f));
    const float* t = windows().table[static_cast<int> (shape) < kNumWindowShapes ? static_cast<int> (shape) : 0];
    const float w0 = t[i] + (t[i + 1] - t[i]) * (f - static_cast<float> (i));
    const float w = 1.0f + (w0 - 1.0f) * amount;
    const float ramp = std::min (1.0f, std::min (64.0f * x, 64.0f * (1.0f - x)));
    return w * ramp;
}

void GranularVoice::prepare (double hostSampleRate) noexcept
{
    windows(); // the tables are built here, not on the audio thread
    sampleRate_ = hostSampleRate > 1000.0 ? hostSampleRate : 44100.0;
}

void GranularVoice::reset() noexcept
{
    active_ = false;
    sample_ = nullptr;
    for (Grain& g : grains_)
        g.active = false;
}

int GranularVoice::activeGrains() const noexcept
{
    int n = 0;
    for (const Grain& g : grains_)
        n += g.active ? 1 : 0;
    return n;
}

float GranularVoice::random01() noexcept
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float> (rng_ >> 8) * (1.0f / 16777216.0f);
}

void GranularVoice::noteOn (const SampleView* sample, const GrainParams& p, uint32_t seed, float spawnFraction, bool jumpStart, float startFraction) noexcept
{
    reset();
    if (sample == nullptr || ! sample->isValid())
        return;
    sample_ = sample;
    active_ = true;
    released_ = false;
    headDone_ = false;
    headDirection_ = 1;
    rng_ = seed != 0 ? seed : 0x2545F491u;

    const float lo = std::min (p.regionLo, p.regionHi), hi = std::max (p.regionLo, p.regionHi);
    const bool backwards = p.scanSpeed < 0.0f || p.loopMode == LoopMode::reverse;
    head_ = backwards ? hi : lo;
    if (p.loopMode == LoopMode::reverse)
        head_ = std::max (lo, std::min (hi, std::max (p.loopLo, p.loopHi)));
    head_ += static_cast<double> (clamp01 (startFraction)) * (hi - lo) * (backwards ? -1.0 : 1.0);
    head_ = std::max (static_cast<double> (lo), std::min (static_cast<double> (hi), head_));
    if (p.manual)
        head_ = lo + (hi - lo) * clamp01 (p.position);

    const double period = std::max (1.0, sampleRate_ / std::max (0.01, static_cast<double> (p.densityHz)));
    spawnFraction = clamp01 (spawnFraction);
    if (jumpStart)
    {
        const int want = std::max (1, std::min (std::min (std::max (1, p.maxGrains), kMaxGrains),
                                                 static_cast<int> (std::ceil (static_cast<double> (p.lengthSec) * sampleRate_ / period))));
        for (int j = 0; j < want; ++j)
            spawn (p, 0, static_cast<int> (static_cast<double> (j) * period), head_);
        spawnTimer_ = period * (1.0 + static_cast<double> (spawnFraction));
    }
    else
        spawnTimer_ = period * static_cast<double> (spawnFraction);
}

void GranularVoice::spawn (const GrainParams& p, int delay, int age, double headFraction) noexcept
{
    const int limit = std::max (1, std::min (p.maxGrains, kMaxGrains));
    Grain* slot = nullptr;
    int running = 0;
    for (Grain& g : grains_)
    {
        if (g.active)
            ++running;
        else if (slot == nullptr)
            slot = &g;
    }
    if (slot == nullptr || running >= limit)
        return; // the pool is full: the spawn is skipped and waits for a grain to end (manual p.101)

    const double span = std::fabs (static_cast<double> (p.regionHi) - static_cast<double> (p.regionLo));
    const double lengthSec = static_cast<double> (p.lengthSec) * (1.0 - static_cast<double> (p.randLength) * static_cast<double> (random01()));
    Grain& g = *slot;
    g.length = std::max (16, static_cast<int> (lengthSec * sampleRate_));
    g.age = std::min (age, g.length - 1);
    g.delay = delay;
    const double offset = (static_cast<double> (random01()) * 2.0 - 1.0) * static_cast<double> (p.randOffset) * 0.5 * span;
    double fraction = headFraction + offset;
    if (! p.loopGrains)
        fraction = std::max (0.0, std::min (1.0, fraction));
    const bool backwards = (random01() < p.randDir) != p.reverseGrains;
    const double rate = static_cast<double> (p.pitchRatio) * std::exp2 ((static_cast<double> (random01()) * 2.0 - 1.0) * static_cast<double> (p.randPitch) / 12.0);
    const double fileRate = sample_ != nullptr ? sample_->sampleRate : sampleRate_;
    g.inc = std::max (1.0e-4, std::min (64.0, rate)) * fileRate / sampleRate_ * (backwards ? -1.0 : 1.0);
    g.pos = fraction * static_cast<double> (sample_->numFrames) + g.inc * g.age;
    const float pan = (random01() * 2.0f - 1.0f) * p.randPan;
    const float angle = (pan + 1.0f) * 0.78539816f;
    const float level = p.gain * (1.0f - p.randGain * random01());
    const double overlap = std::max (1.0, lengthSec * static_cast<double> (p.densityHz));
    const float norm = static_cast<float> (1.0 / std::sqrt (overlap));
    lastNorm_ = norm;
    g.gl = std::cos (angle) * 1.41421356f * level * norm;
    g.gr = std::sin (angle) * 1.41421356f * level * norm;
    g.windowAmount = clamp01 (p.windowAmount + (random01() * 2.0f - 1.0f) * p.randWindow);
    g.windowSkew = std::max (-1.0f, std::min (1.0f, p.windowSkew + (random01() * 2.0f - 1.0f) * p.randSkew));
    for (int s = 0; s < 2; ++s)
        g.warpAmount[s] = clamp01 (p.shapeAmount[s] + (random01() * 2.0f - 1.0f) * p.randWarp[s]);
    g.active = true;
}

void GranularVoice::advanceHead (const GrainParams& p) noexcept
{
    if (headDone_)
        return;
    const double fileSeconds = static_cast<double> (sample_->numFrames) / sample_->sampleRate;
    double step = static_cast<double> (p.scanSpeed) / (fileSeconds * sampleRate_);
    const double lo = std::min (p.regionLo, p.regionHi), hi = std::max (p.regionLo, p.regionHi);

    const bool looping = p.loopMode != LoopMode::none && ! (p.loopMode == LoopMode::tailed && released_);
    if (! looping)
    {
        head_ += step;
        if (head_ >= hi && step > 0.0) { head_ = hi; headDone_ = true; }
        else if (head_ <= lo && step < 0.0) { head_ = lo; headDone_ = true; }
        return;
    }
    if (p.loopHi < p.loopLo)
        step = -step;
    if (p.loopMode == LoopMode::reverse)
        step = -step;
    if (p.loopMode == LoopMode::pingPong)
        step *= static_cast<double> (headDirection_);
    const double l0 = std::max (lo, std::min (static_cast<double> (p.loopLo), static_cast<double> (p.loopHi)));
    const double l1 = std::min (hi, std::max (static_cast<double> (p.loopLo), static_cast<double> (p.loopHi)));
    head_ += step;
    if (l1 - l0 < 1.0e-6)
    {
        head_ = l0;
        return;
    }
    if (p.loopMode == LoopMode::pingPong)
    {
        if (head_ >= l1) { head_ = l1 - (head_ - l1); headDirection_ = -headDirection_; }
        else if (head_ <= l0) { head_ = l0 + (l0 - head_); headDirection_ = -headDirection_; }
        head_ = std::max (l0, std::min (l1, head_));
        return;
    }
    if (step > 0.0 && head_ >= l1)
        head_ = l0 + std::fmod (head_ - l1, l1 - l0);
    else if (step < 0.0 && head_ <= l0)
        head_ = l1 - std::fmod (l0 - head_, l1 - l0);
}

void GranularVoice::process (const GrainParams& p, float* left, float* right, int numFrames, const float* rateMod) noexcept
{
    std::fill (left, left + numFrames, 0.0f);
    std::fill (right, right + numFrames, 0.0f);
    if (! active_ || sample_ == nullptr || ! sample_->isValid())
    {
        active_ = false;
        return;
    }

    const double period = std::max (1.0, sampleRate_ / std::max (0.01, static_cast<double> (p.densityHz)));
    for (int i = 0; i < numFrames; ++i)
    {
        if (p.manual)
            head_ = std::min (p.regionLo, p.regionHi) + std::fabs (static_cast<double> (p.regionHi) - static_cast<double> (p.regionLo)) * clamp01 (p.position);
        else
            advanceHead (p);
        if (headDone_ && ! p.manual)
            continue;
        spawnTimer_ -= 1.0;
        if (spawnTimer_ <= 0.0)
        {
            spawn (p, i, 0, head_);
            spawnTimer_ += period;
        }
    }

    // loop markers for the grains (Loop Grains)
    const double fileFrames = static_cast<double> (sample_->numFrames);
    const double lg0 = std::min (p.loopLo, p.loopHi) * fileFrames, lg1 = std::max (p.loopLo, p.loopHi) * fileFrames;
    const double loopLen = lg1 - lg0;
    const bool wrap = p.loopGrains && loopLen > 8.0;
    const double xfade = wrap ? std::min (0.5, static_cast<double> (clamp01 (p.loopCrossfade))) * loopLen : 0.0;

    bool any = false;
    for (Grain& g : grains_)
    {
        if (! g.active)
            continue;
        any = true;
        const int start = std::max(0, g.delay);
        g.delay = 0;
        const int sw = p.shapeFn != nullptr ? 1 : 0;
        for (int i = start; i < numFrames; ++i)
        {
            if (g.age >= g.length)
            {
                g.active = false;
                break;
            }
            const float x = static_cast<float> (g.age) / static_cast<float> (g.length);
            float w = windowValue (p.shape, x, g.windowAmount, g.windowSkew);
            double rate = std::fabs (g.inc);
            double step = g.inc;
            if (rateMod != nullptr)
            {
                const double m = std::max (-4.0, std::min (4.0, static_cast<double> (rateMod[i])));
                step *= m;
                rate *= std::fabs (m);
            }
            if (wrap)
            {
                while (g.pos >= lg1) g.pos -= loopLen;
                while (g.pos < lg0) g.pos += loopLen;
            }
            float sl, sr;
            readSample (*sample_, g.pos, rate, sl, sr);
            if (wrap && xfade > 1.0)
            {
                const double toEnd = lg1 - g.pos;
                if (toEnd < xfade && step > 0.0)
                {
                    const float t = static_cast<float> (1.0 - toEnd / xfade);
                    float al, ar;
                    readSample (*sample_, g.pos - loopLen, rate, al, ar);
                    const float a = std::cos (t * 1.5707963f), b = std::sin (t * 1.5707963f);
                    sl = sl * a + al * b;
                    sr = sr * a + ar * b;
                }
            }
            if (sw != 0)
                for (int s = 0; s < 2; ++s)
                    if (p.shapeType[s] >= 0 && p.shapeAmount[s] > 0.0f)
                    {
                        sl = p.shapeFn (p.shapeType[s], sl, g.warpAmount[s]);
                        sr = p.shapeFn (p.shapeType[s], sr, g.warpAmount[s]);
                    }
            left[i] += sl * w * g.gl;
            right[i] += sr * w * g.gr;
            g.pos += step;
            ++g.age;
        }
        if (g.age >= g.length)
            g.active = false;
    }

    for (int i = 0; i < numFrames; ++i)
    {
        left[i] = std::isfinite (left[i]) ? std::max (-16.0f, std::min (16.0f, left[i])) : 0.0f;
        right[i] = std::isfinite (right[i]) ? std::max (-16.0f, std::min (16.0f, right[i])) : 0.0f;
    }
    if (! any && headDone_ && ! p.manual)
        active_ = false;
}
} // namespace granular
} // namespace synthyboy
