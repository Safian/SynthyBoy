// SynthyBoy - core/granular: the granular oscillator DSP.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/granular/granular.h"

#include <algorithm>
#include <cmath>

#if defined(__ARM_NEON) && ! defined(SB_NO_NEON)
 #include <arm_neon.h>
 #define SB_GRAIN_NEON 1
#else
 #define SB_GRAIN_NEON 0
#endif

// The scalar kernel and the vector kernel must compute the same bits: no fused multiply-add in either, on any compiler / CPU.
#if defined(__clang__)
 #pragma clang fp contract(off)
#elif defined(__GNUC__)
 #pragma GCC optimize ("fp-contract=off")
#endif

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
// ---- the grain kernel --------------------------------------------------------------------------------------------------------------
// What does not change while a grain plays inside one block is computed once per grain and block (the window's peak and amount, the mip level of the read, the table of the shape);
// the per sample arithmetic is the reference loop's, expression by expression.

/** hermite () with the four taps read directly where they all lie inside the data (the same bits). */
inline float hermiteFast (const float* d, int n, double pos) noexcept
{
    int i = static_cast<int> (pos); // 32 bit integers: a 64 bit conversion of a double is a library call on 32 bit ARM
    if (static_cast<double> (i) > pos)
        --i; // floor () of a negative position
    const float t = static_cast<float> (pos - static_cast<double> (i));
    float y0, y1, y2, y3;
    if (i >= 1 && i + 2 < n)
    {
        y0 = d[i - 1]; y1 = d[i]; y2 = d[i + 1]; y3 = d[i + 2];
    }
    else
    {
        const auto at = [&] (int k) { return k >= 0 && k < n ? d[k] : 0.0f; };
        y0 = at (i - 1); y1 = at (i); y2 = at (i + 1); y3 = at (i + 2);
    }
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

/** Where a grain reads for one mip level (readSample () of the reference picks the level from the rate, for every sample). */
struct GrainRead
{
    const float* ch[2] = { nullptr, nullptr };
    int n = 0;
    double invScale = 1.0;
    bool stereo = false;
    bool scaled = false;
    void set (const SampleView& s, int m) noexcept
    {
        stereo = s.numChannels > 1;
        if (m > 0 && s.mipLevels != nullptr)
        {
            const auto& lvl = s.mipLevels[m];
            ch[0] = lvl.channels[0];
            ch[1] = stereo ? lvl.channels[1] : nullptr;
            n = static_cast<int> (lvl.numFrames);
            invScale = 1.0 / static_cast<double> (1 << m); // a power of two: pos * invScale is pos / scale
            scaled = true;
        }
        else
        {
            ch[0] = s.channelData[0];
            ch[1] = stereo ? s.channelData[1] : nullptr;
            n = static_cast<int> (s.numFrames);
            invScale = 1.0;
            scaled = false;
        }
    }
    inline void read (double pos, float& l, float& r) const noexcept
    {
        const double p = scaled ? pos * invScale : pos;
        l = hermiteFast (ch[0], n, p);
        r = stereo ? hermiteFast (ch[1], n, p) : l;
    }
};

/** The mip level the reference picks for a read at `rate` (frames per output sample, > 0). */
inline int mipFor (const SampleView& s, double rate) noexcept
{
    if (rate > 1.0 && s.numMipLevels > 1)
        return std::min (s.numMipLevels - 1, static_cast<int> (std::floor (std::log2 (rate))));
    return 0;
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

int GranularVoice::snapshot (float* pos, float* life, int maxN) const noexcept
{
    int n = 0;
    if (sample_ == nullptr || sample_->numFrames < 2)
        return 0;
    const double frames = static_cast<double> (sample_->numFrames);
    for (const Grain& g : grains_)
    {
        if (n >= maxN)
            break;
        if (! g.active || g.delay > 0)
            continue;
        const double p = g.pos / frames;
        pos[n] = static_cast<float> (p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p));
        life[n] = static_cast<float> (g.age) / static_cast<float> (g.length > 0 ? g.length : 1);
        ++n;
    }
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
    // the constants of the kernel (see renderGrains)
    g.winAmount = clamp01 (g.windowAmount);
    const float skew = std::max (-1.0f, std::min (1.0f, g.windowSkew == g.windowSkew ? g.windowSkew : 0.0f));
    g.winPeak = 0.5f + 0.45f * skew;
    g.winK1 = 0.5f / g.winPeak;
    g.winK2 = 0.5f / (1.0f - g.winPeak);
    g.invLength = 1.0f / static_cast<float> (g.length);
    g.mip = mipFor (*sample_, std::fabs (g.inc));
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

bool GranularVoice::renderGrainsReference (const GrainParams& p, float* left, float* right, int numFrames, const float* rateMod) noexcept
{
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
    return any;
}

namespace
{
// The window of a grain in the form the kernels use: no division per sample (the grain's x is age * (1 / length), the two slopes of the skewed window are constants of the grain), so the
// values differ from windowValue () by a rounding (about 1e-7); the scalar and the vector forms below do the same operations and give the same bits.
struct GrainWindow
{
    const float* table = nullptr;
    float peak = 0.5f, k1 = 1.0f, k2 = 1.0f, amount = 0.5f, invLength = 1.0f;
};

inline float windowKernel (const GrainWindow& g, float x) noexcept
{
    const float u = x < g.peak ? x * g.k1 : 0.5f + (x - g.peak) * g.k2;
    const float f = u * static_cast<float> (kTableSize);
    const int i = std::min (kTableSize - 1, static_cast<int> (f));
    const float w0 = g.table[i] + (g.table[i + 1] - g.table[i]) * (f - static_cast<float> (i));
    const float wa = 1.0f + (w0 - 1.0f) * g.amount;
    const float ramp = std::min (1.0f, std::min (64.0f * x, 64.0f * (1.0f - x)));
    return wa * ramp;
}

#if SB_GRAIN_NEON
/** windowKernel () of the ages age .. age + 3. */
inline float32x4_t windowKernel4 (const GrainWindow& g, int age) noexcept
{
    static const int32_t kIota[4] = { 0, 1, 2, 3 };
    const float32x4_t x = vmulq_f32 (vcvtq_f32_s32 (vaddq_s32 (vdupq_n_s32 (age), vld1q_s32 (kIota))), vdupq_n_f32 (g.invLength));
    const float32x4_t peak = vdupq_n_f32 (g.peak);
    const uint32x4_t first = vcltq_f32 (x, peak);
    const float32x4_t u1 = vmulq_f32 (x, vdupq_n_f32 (g.k1));
    const float32x4_t u2 = vaddq_f32 (vdupq_n_f32 (0.5f), vmulq_f32 (vsubq_f32 (x, peak), vdupq_n_f32 (g.k2)));
    const float32x4_t f = vmulq_f32 (vbslq_f32 (first, u1, u2), vdupq_n_f32 (static_cast<float> (kTableSize)));
    const int32x4_t iv = vminq_s32 (vcvtq_s32_f32 (f), vdupq_n_s32 (kTableSize - 1));
    const int i0 = vgetq_lane_s32 (iv, 0), i1 = vgetq_lane_s32 (iv, 1), i2 = vgetq_lane_s32 (iv, 2), i3 = vgetq_lane_s32 (iv, 3);
    // the table cells straight into the lanes (no store of the lanes one by one and load of the vector, which waits for the stores)
    float32x4_t a = vld1q_lane_f32 (g.table + i0, vdupq_n_f32 (0.0f), 0);
    a = vld1q_lane_f32 (g.table + i1, a, 1);
    a = vld1q_lane_f32 (g.table + i2, a, 2);
    a = vld1q_lane_f32 (g.table + i3, a, 3);
    float32x4_t b = vld1q_lane_f32 (g.table + i0 + 1, vdupq_n_f32 (0.0f), 0);
    b = vld1q_lane_f32 (g.table + i1 + 1, b, 1);
    b = vld1q_lane_f32 (g.table + i2 + 1, b, 2);
    b = vld1q_lane_f32 (g.table + i3 + 1, b, 3);
    const float32x4_t fr = vsubq_f32 (f, vcvtq_f32_s32 (iv));
    const float32x4_t w0 = vaddq_f32 (a, vmulq_f32 (vsubq_f32 (b, a), fr));
    const float32x4_t one = vdupq_n_f32 (1.0f);
    const float32x4_t wa = vaddq_f32 (one, vmulq_f32 (vsubq_f32 (w0, one), vdupq_n_f32 (g.amount)));
    const float32x4_t sixtyFour = vdupq_n_f32 (64.0f);
    const float32x4_t ramp = vminq_f32 (one, vminq_f32 (vmulq_f32 (sixtyFour, x), vmulq_f32 (sixtyFour, vsubq_f32 (one, x))));
    return vmulq_f32 (wa, ramp);
}

/** hermiteFast () at four positions (i[k], t[k]) of one channel; every tap lies inside the data (the caller checked). */
inline float32x4_t hermite4 (const float* d, const int32_t* i, float32x4_t t) noexcept
{
    const float32x4_t r0 = vld1q_f32 (d + i[0] - 1), r1 = vld1q_f32 (d + i[1] - 1), r2 = vld1q_f32 (d + i[2] - 1), r3 = vld1q_f32 (d + i[3] - 1);
    const float32x4x2_t t01 = vtrnq_f32 (r0, r1), t23 = vtrnq_f32 (r2, r3);
    const float32x4_t y0 = vcombine_f32 (vget_low_f32 (t01.val[0]), vget_low_f32 (t23.val[0]));
    const float32x4_t y1 = vcombine_f32 (vget_low_f32 (t01.val[1]), vget_low_f32 (t23.val[1]));
    const float32x4_t y2 = vcombine_f32 (vget_high_f32 (t01.val[0]), vget_high_f32 (t23.val[0]));
    const float32x4_t y3 = vcombine_f32 (vget_high_f32 (t01.val[1]), vget_high_f32 (t23.val[1]));
    const float32x4_t half = vdupq_n_f32 (0.5f);
    const float32x4_t c1 = vmulq_f32 (half, vsubq_f32 (y2, y0));
    float32x4_t c2 = vsubq_f32 (y0, vmulq_f32 (vdupq_n_f32 (2.5f), y1));
    c2 = vaddq_f32 (c2, vmulq_f32 (vdupq_n_f32 (2.0f), y2));
    c2 = vsubq_f32 (c2, vmulq_f32 (half, y3));
    const float32x4_t c3 = vaddq_f32 (vmulq_f32 (half, vsubq_f32 (y3, y0)), vmulq_f32 (vdupq_n_f32 (1.5f), vsubq_f32 (y1, y2)));
    return vaddq_f32 (vmulq_f32 (vaddq_f32 (vmulq_f32 (vaddq_f32 (vmulq_f32 (c3, t), c2), t), c1), t), y1);
}
#endif
} // namespace

bool GranularVoice::renderGrains (const GrainParams& p, float* left, float* right, int numFrames, const float* rateMod) noexcept
{
    // loop markers for the grains (Loop Grains)
    const double fileFrames = static_cast<double> (sample_->numFrames);
    const double lg0 = std::min (p.loopLo, p.loopHi) * fileFrames, lg1 = std::max (p.loopLo, p.loopHi) * fileFrames;
    const double loopLen = lg1 - lg0;
    const bool wrap = p.loopGrains && loopLen > 8.0;
    const double xfade = wrap ? std::min (0.5, static_cast<double> (clamp01 (p.loopCrossfade))) * loopLen : 0.0;
    const SampleView& smp = *sample_;
    const float* const table = windows().table[static_cast<int> (p.shape) < kNumWindowShapes ? static_cast<int> (p.shape) : 0];
    // a pointwise warp plays only where a slot has a type and an amount (the engine hands the function over always)
    const bool shaped = p.shapeFn != nullptr && ((p.shapeType[0] >= 0 && p.shapeAmount[0] > 0.0f) || (p.shapeType[1] >= 0 && p.shapeAmount[1] > 0.0f));
#if SB_GRAIN_NEON
    // four samples of a grain at a time where nothing but the plain read plays: no rate modulation, no loop wrap, no pointwise warp
    const bool vectorOk = vectors_ && rateMod == nullptr && ! wrap && ! shaped;
#endif

    bool any = false;
    for (Grain& g : grains_)
    {
        if (! g.active)
            continue;
        any = true;
        int i = std::max (0, g.delay);
        g.delay = 0;

        // the window of this grain
        GrainWindow win;
        win.table = table;
        win.amount = g.winAmount;
        win.peak = g.winPeak;
        win.k1 = g.winK1;
        win.k2 = g.winK2;
        win.invLength = g.invLength;

        // the read: one mip level for the whole block unless the rate modulation changes the rate per sample
        const double rate0 = std::fabs (g.inc);
        GrainRead read;
        if (rateMod == nullptr)
            read.set (smp, g.mip);
        const double step0 = g.inc;
        const int nRead = read.n;

        while (i < numFrames)
        {
            if (g.age >= g.length)
            {
                g.active = false;
                break;
            }
#if SB_GRAIN_NEON
            if (vectorOk && i + 4 <= numFrames && g.age + 4 <= g.length)
            {
                // the four positions the sample loop would visit (the same additions in the same order), and whether every tap of them lies inside the data
                double pos[4];
                pos[0] = g.pos;
                pos[1] = pos[0] + step0;
                pos[2] = pos[1] + step0;
                pos[3] = pos[2] + step0;
                double scaled[4];
                for (int k = 0; k < 4; ++k)
                    scaled[k] = read.scaled ? pos[k] * read.invScale : pos[k];
                const double lowest = std::min (scaled[0], scaled[3]), highest = std::max (scaled[0], scaled[3]);
                if (lowest >= 1.0 && highest + 3.0 < static_cast<double> (nRead))
                {
                    int32_t idx[4];
                    float32x4_t t = vdupq_n_f32 (0.0f);
                    idx[0] = static_cast<int> (scaled[0]);
                    idx[1] = static_cast<int> (scaled[1]);
                    idx[2] = static_cast<int> (scaled[2]);
                    idx[3] = static_cast<int> (scaled[3]);
                    t = vsetq_lane_f32 (static_cast<float> (scaled[0] - static_cast<double> (idx[0])), t, 0);
                    t = vsetq_lane_f32 (static_cast<float> (scaled[1] - static_cast<double> (idx[1])), t, 1);
                    t = vsetq_lane_f32 (static_cast<float> (scaled[2] - static_cast<double> (idx[2])), t, 2);
                    t = vsetq_lane_f32 (static_cast<float> (scaled[3] - static_cast<double> (idx[3])), t, 3);
                    const float32x4_t w = windowKernel4 (win, g.age);
                    const float32x4_t sl = hermite4 (read.ch[0], idx, t);
                    const float32x4_t sr = read.stereo ? hermite4 (read.ch[1], idx, t) : sl;
                    vst1q_f32 (left + i, vaddq_f32 (vld1q_f32 (left + i), vmulq_f32 (vmulq_f32 (sl, w), vdupq_n_f32 (g.gl))));
                    vst1q_f32 (right + i, vaddq_f32 (vld1q_f32 (right + i), vmulq_f32 (vmulq_f32 (sr, w), vdupq_n_f32 (g.gr))));
                    g.pos = pos[3] + step0;
                    g.age += 4;
                    i += 4;
                    continue;
                }
            }
#endif
            const float x = static_cast<float> (g.age) * win.invLength;
            const float w = windowKernel (win, x);
            double step = g.inc;
            double rate = rate0;
            if (rateMod != nullptr)
            {
                const double m = std::max (-4.0, std::min (4.0, static_cast<double> (rateMod[i])));
                step *= m;
                rate *= std::fabs (m);
                read.set (smp, mipFor (smp, rate));
            }
            if (wrap)
            {
                while (g.pos >= lg1) g.pos -= loopLen;
                while (g.pos < lg0) g.pos += loopLen;
            }
            float sl, sr;
            read.read (g.pos, sl, sr);
            if (wrap && xfade > 1.0)
            {
                const double toEnd = lg1 - g.pos;
                if (toEnd < xfade && step > 0.0)
                {
                    const float t = static_cast<float> (1.0 - toEnd / xfade);
                    float al, ar;
                    read.read (g.pos - loopLen, al, ar);
                    const float a = std::cos (t * 1.5707963f), b = std::sin (t * 1.5707963f);
                    sl = sl * a + al * b;
                    sr = sr * a + ar * b;
                }
            }
            if (shaped)
                for (int sIdx = 0; sIdx < 2; ++sIdx)
                    if (p.shapeType[sIdx] >= 0 && p.shapeAmount[sIdx] > 0.0f)
                    {
                        sl = p.shapeFn (p.shapeType[sIdx], sl, g.warpAmount[sIdx]);
                        sr = p.shapeFn (p.shapeType[sIdx], sr, g.warpAmount[sIdx]);
                    }
            left[i] += sl * w * g.gl;
            right[i] += sr * w * g.gr;
            g.pos += step;
            ++g.age;
            ++i;
        }
        if (g.age >= g.length)
            g.active = false;
    }
    return any;
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
    if (p.manual && ! reference_)
    {
        // the head does not move: the spawns are found from the timer (it falls by exactly 1 per sample, so a spawn comes at the sample where it reaches 0) instead of one test per sample
        head_ = std::min (p.regionLo, p.regionHi) + std::fabs (static_cast<double> (p.regionHi) - static_cast<double> (p.regionLo)) * clamp01 (p.position);
        int i = 0;
        while (i < numFrames)
        {
            const double left = spawnTimer_ - static_cast<double> (numFrames - i);
            if (left > 0.0)
            {
                spawnTimer_ = left;
                break;
            }
            const int at = spawnTimer_ <= 1.0 ? 0 : static_cast<int> (std::ceil (spawnTimer_)) - 1;   // the sample (counted from i) at which the timer has fallen to 0 or below
            spawnTimer_ -= static_cast<double> (at + 1);
            spawn (p, i + at, 0, head_);
            spawnTimer_ += period;
            i += at + 1;
        }
    }
    else
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

    bool any;
    if (reference_ || sample_->numFrames > (int64_t { 1 } << 29))   // (the kernel counts frames with 32 bit integers)
        any = renderGrainsReference (p, left, right, numFrames, rateMod);
    else
        any = renderGrains (p, left, right, numFrames, rateMod);

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
