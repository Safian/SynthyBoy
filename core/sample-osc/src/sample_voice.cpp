// SynthyBoy - core/sample-osc: polyphony-ready, real-time safe sample oscillator voice.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/sampleosc/sample_voice.h"

#include <algorithm>
#include <cmath>

#if defined(__ARM_NEON) && ! defined(SB_NO_NEON)
 #include <arm_neon.h>
 #define SB_SAMPLE_NEON 1
#else
 #define SB_SAMPLE_NEON 0
#endif

// The block kernel below and the per sample reference (renderFrame) must compute the same bits: no fused multiply-add in either, on any compiler / CPU (AArch64 always has it
// and GCC / clang contract a * b + c by default).
#if defined(__clang__)
 #pragma clang fp contract(off)
#elif defined(__GNUC__)
 #pragma GCC optimize ("fp-contract=off")
#endif

namespace synthyboy
{
namespace sampleosc
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

inline float interpolateChannel (const float* buf, int64_t numFrames, double pos, QualityTier tier,
                                const SincTables& tables) noexcept
{
    if (buf == nullptr || numFrames <= 0)
        return 0.0f;

    pos = std::max (0.0, std::min (static_cast<double> (numFrames - 1), pos));
    const auto i = static_cast<int64_t> (std::floor (pos));
    const double frac = pos - static_cast<double> (i);
    const int p = std::max (0, std::min (SincTables::kPhases - 1, static_cast<int> (frac * SincTables::kPhases)));

    if (tier == QualityTier::low)
    {
        const float* c = tables.low + p * SincTables::kLowTaps;
        if (i >= 1 && i + 2 < numFrames)
        {
            return c[0] * buf[i - 1] + c[1] * buf[i] + c[2] * buf[i + 1] + c[3] * buf[i + 2];
        }
        else
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                const int64_t idx = std::max<int64_t> (0, std::min<int64_t> (numFrames - 1, i - 1 + k));
                sum += c[k] * buf[idx];
            }
            return sum;
        }
    }
    else if (tier == QualityTier::medium)
    {
        const float* c = tables.med + p * SincTables::kMedTaps;
        if (i >= 3 && i + 4 < numFrames)
        {
            float sum = 0.0f;
            for (int k = 0; k < 8; ++k)
                sum += c[k] * buf[i - 3 + k];
            return sum;
        }
        else
        {
            float sum = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                const int64_t idx = std::max<int64_t> (0, std::min<int64_t> (numFrames - 1, i - 3 + k));
                sum += c[k] * buf[idx];
            }
            return sum;
        }
    }
    else // QualityTier::high
    {
        const float* c = tables.high + p * SincTables::kHighTaps;
        if (i >= 7 && i + 8 < numFrames)
        {
            float sum = 0.0f;
            for (int k = 0; k < 16; ++k)
                sum += c[k] * buf[i - 7 + k];
            return sum;
        }
        else
        {
            float sum = 0.0f;
            for (int k = 0; k < 16; ++k)
            {
                const int64_t idx = std::max<int64_t> (0, std::min<int64_t> (numFrames - 1, i - 7 + k));
                sum += c[k] * buf[idx];
            }
            return sum;
        }
    }
}

inline void readSampleStereo (const SampleView& sample, double pos, double stepRatio, QualityTier tier,
                              const SincTables& tables, float& outL, float& outR) noexcept
{
    if (sample.numMipLevels <= 1 || stepRatio <= 1.0)
    {
        outL = interpolateChannel (sample.channelData[0], sample.numFrames, pos, tier, tables);
        outR = (sample.numChannels == 2)
            ? interpolateChannel (sample.channelData[1], sample.numFrames, pos, tier, tables)
            : outL;
        return;
    }

    const double rho = std::log2 (stepRatio);

    if (tier == QualityTier::high)
    {
        const int m0 = std::max (0, std::min (sample.numMipLevels - 1, static_cast<int> (rho)));
        const int m1 = std::min (m0 + 1, sample.numMipLevels - 1);
        const double alpha = std::max (0.0, std::min (1.0, rho - static_cast<double> (m0)));

        const double scale0 = static_cast<double> (1 << m0);
        const double pos0 = pos / scale0;
        const auto& lvl0 = sample.mipLevels[m0];
        const float l0 = interpolateChannel (lvl0.channels[0], lvl0.numFrames, pos0, tier, tables);
        const float r0 = (sample.numChannels == 2)
            ? interpolateChannel (lvl0.channels[1], lvl0.numFrames, pos0, tier, tables)
            : l0;

        if (m1 > m0 && alpha > 0.0)
        {
            const double scale1 = static_cast<double> (1 << m1);
            const double pos1 = pos / scale1;
            const auto& lvl1 = sample.mipLevels[m1];
            const float l1 = interpolateChannel (lvl1.channels[0], lvl1.numFrames, pos1, tier, tables);
            const float r1 = (sample.numChannels == 2)
                ? interpolateChannel (lvl1.channels[1], lvl1.numFrames, pos1, tier, tables)
                : l1;

            outL = static_cast<float> ((1.0 - alpha) * l0 + alpha * l1);
            outR = static_cast<float> ((1.0 - alpha) * r0 + alpha * r1);
        }
        else
        {
            outL = l0;
            outR = r0;
        }
    }
    else
    {
        const int m = std::max (0, std::min (sample.numMipLevels - 1, static_cast<int> (std::round (rho))));
        const double scale = static_cast<double> (1 << m);
        const double posM = pos / scale;
        const auto& lvl = sample.mipLevels[m];
        outL = interpolateChannel (lvl.channels[0], lvl.numFrames, posM, tier, tables);
        outR = (sample.numChannels == 2)
            ? interpolateChannel (lvl.channels[1], lvl.numFrames, posM, tier, tables)
            : outL;
    }
}

// ---- the block kernel --------------------------------------------------------------------------------------------------------------
// Everything that does not change inside one call (the region and loop bounds, the pan gains, the mip level and its blend, the table of the tier) is computed once per call
// instead of once per sample; the per sample arithmetic is the reference's (renderFrame), expression by expression, so the output is the same bits.


template <QualityTier Tier>
inline float interpBlock (const float* buf, int numFrames, double pos, const float* table) noexcept
{
    pos = std::max (0.0, std::min (static_cast<double> (numFrames - 1), pos));
    const int i = static_cast<int> (pos); // pos >= 0: the truncation is the floor
    const double frac = pos - static_cast<double> (i);
    const int p = std::max (0, std::min (SincTables::kPhases - 1, static_cast<int> (frac * SincTables::kPhases)));
    if (Tier == QualityTier::low)
    {
        const float* c = table + p * SincTables::kLowTaps;
        if (i >= 1 && i + 2 < numFrames)
            return c[0] * buf[i - 1] + c[1] * buf[i] + c[2] * buf[i + 1] + c[3] * buf[i + 2];
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k)
            sum += c[k] * buf[std::max (0, std::min (numFrames - 1, i - 1 + k))];
        return sum;
    }
    else if (Tier == QualityTier::medium)
    {
        const float* c = table + p * SincTables::kMedTaps;
        float sum = 0.0f;
        if (i >= 3 && i + 4 < numFrames)
            for (int k = 0; k < 8; ++k)
                sum += c[k] * buf[i - 3 + k];
        else
            for (int k = 0; k < 8; ++k)
                sum += c[k] * buf[std::max (0, std::min (numFrames - 1, i - 3 + k))];
        return sum;
    }
    else
    {
        const float* c = table + p * SincTables::kHighTaps;
        float sum = 0.0f;
        if (i >= 7 && i + 8 < numFrames)
            for (int k = 0; k < 16; ++k)
                sum += c[k] * buf[i - 7 + k];
        else
            for (int k = 0; k < 16; ++k)
                sum += c[k] * buf[std::max (0, std::min (numFrames - 1, i - 7 + k))];
        return sum;
    }
}

/** How a block reads the sample: chosen once from the pitch ratio (readSampleStereo of the reference decides the same, per sample). */
struct ReadPlan
{
    enum Kind { direct, oneLevel, blend } kind = direct;
    bool stereo = false;
    const float* a[2] = { nullptr, nullptr };   // level 0 (direct), the level of oneLevel, the lower level of blend
    int na = 0;
    double invScale0 = 1.0;                     // 1 / 2^level: a division by a power of two is exact, so pos * inv is pos / scale
    const float* b[2] = { nullptr, nullptr };   // the upper level of blend
    int nb = 0;
    double invScale1 = 1.0;
    bool useUpper = false;
    double alpha = 0.0, oneMinusAlpha = 1.0;
};

inline ReadPlan makePlan (const SampleView& sample, double stepRatio, QualityTier tier) noexcept
{
    ReadPlan r;
    r.stereo = sample.numChannels == 2;
    if (sample.numMipLevels <= 1 || stepRatio <= 1.0)
    {
        r.kind = ReadPlan::direct;
        r.a[0] = sample.channelData[0];
        r.a[1] = r.stereo ? sample.channelData[1] : nullptr;
        r.na = static_cast<int> (sample.numFrames);
        return r;
    }
    const double rho = std::log2 (stepRatio);
    if (tier == QualityTier::high)
    {
        const int m0 = std::max (0, std::min (sample.numMipLevels - 1, static_cast<int> (rho)));
        const int m1 = std::min (m0 + 1, sample.numMipLevels - 1);
        const double alpha = std::max (0.0, std::min (1.0, rho - static_cast<double> (m0)));
        const auto& l0 = sample.mipLevels[m0];
        r.kind = ReadPlan::blend;
        r.a[0] = l0.channels[0];
        r.a[1] = r.stereo ? l0.channels[1] : nullptr;
        r.na = static_cast<int> (l0.numFrames);
        r.invScale0 = 1.0 / static_cast<double> (1 << m0);
        r.useUpper = m1 > m0 && alpha > 0.0;
        if (r.useUpper)
        {
            const auto& l1 = sample.mipLevels[m1];
            r.b[0] = l1.channels[0];
            r.b[1] = r.stereo ? l1.channels[1] : nullptr;
            r.nb = static_cast<int> (l1.numFrames);
            r.invScale1 = 1.0 / static_cast<double> (1 << m1);
            r.alpha = alpha;
            r.oneMinusAlpha = 1.0 - alpha;
        }
        return r;
    }
    const int m = std::max (0, std::min (sample.numMipLevels - 1, static_cast<int> (std::round (rho))));
    const auto& lvl = sample.mipLevels[m];
    r.kind = ReadPlan::oneLevel;
    r.a[0] = lvl.channels[0];
    r.a[1] = r.stereo ? lvl.channels[1] : nullptr;
    r.na = static_cast<int> (lvl.numFrames);
    r.invScale0 = 1.0 / static_cast<double> (1 << m);
    return r;
}

template <QualityTier Tier>
inline void readPlanned (const ReadPlan& r, const float* table, double pos, float& outL, float& outR) noexcept
{
    if (r.kind == ReadPlan::direct)
    {
        outL = interpBlock<Tier> (r.a[0], r.na, pos, table);
        outR = r.stereo ? interpBlock<Tier> (r.a[1], r.na, pos, table) : outL;
    }
    else if (r.kind == ReadPlan::oneLevel)
    {
        const double posM = pos * r.invScale0;
        outL = interpBlock<Tier> (r.a[0], r.na, posM, table);
        outR = r.stereo ? interpBlock<Tier> (r.a[1], r.na, posM, table) : outL;
    }
    else
    {
        const double pos0 = pos * r.invScale0;
        const float l0 = interpBlock<Tier> (r.a[0], r.na, pos0, table);
        const float r0 = r.stereo ? interpBlock<Tier> (r.a[1], r.na, pos0, table) : l0;
        if (r.useUpper)
        {
            const double pos1 = pos * r.invScale1;
            const float l1 = interpBlock<Tier> (r.b[0], r.nb, pos1, table);
            const float r1 = r.stereo ? interpBlock<Tier> (r.b[1], r.nb, pos1, table) : l1;
            outL = static_cast<float> (r.oneMinusAlpha * l0 + r.alpha * l1);
            outR = static_cast<float> (r.oneMinusAlpha * r0 + r.alpha * r1);
        }
        else
        {
            outL = l0;
            outR = r0;
        }
    }
}
#if SB_SAMPLE_NEON
/** Four consecutive samples of one channel: the interpolation of interpBlock () with the four sample positions in the four lanes (the taps of every lane are summed in the same order as the
    scalar sum, so the bits are the same). `idx`: the integer positions, `ph`: the phase of the coefficient set. Every tap lies inside the data (the caller checked). */
template <QualityTier Tier>
inline float32x4_t interp4 (const float* buf, const int32_t* idx, const int32_t* ph, const float* table) noexcept
{
    constexpr int kTaps = Tier == QualityTier::low ? 4 : (Tier == QualityTier::medium ? 8 : 16);
    constexpr int kBefore = kTaps / 2 - 1;   // taps before the position: i - 1 (4), i - 3 (8), i - 7 (16)
    float32x4_t sum = vdupq_n_f32 (0.0f);
    for (int g = 0; g < kTaps / 4; ++g)
    {
        float32x4_t c[4], b[4];
        for (int k = 0; k < 4; ++k)
        {
            c[k] = vld1q_f32 (table + ph[k] * kTaps + 4 * g);
            b[k] = vld1q_f32 (buf + idx[k] - kBefore + 4 * g);
        }
        const float32x4x2_t c01 = vtrnq_f32 (c[0], c[1]), c23 = vtrnq_f32 (c[2], c[3]), b01 = vtrnq_f32 (b[0], b[1]), b23 = vtrnq_f32 (b[2], b[3]);
        const float32x4_t cc[4] = { vcombine_f32 (vget_low_f32 (c01.val[0]), vget_low_f32 (c23.val[0])), vcombine_f32 (vget_low_f32 (c01.val[1]), vget_low_f32 (c23.val[1])),
                                    vcombine_f32 (vget_high_f32 (c01.val[0]), vget_high_f32 (c23.val[0])), vcombine_f32 (vget_high_f32 (c01.val[1]), vget_high_f32 (c23.val[1])) };
        const float32x4_t bb[4] = { vcombine_f32 (vget_low_f32 (b01.val[0]), vget_low_f32 (b23.val[0])), vcombine_f32 (vget_low_f32 (b01.val[1]), vget_low_f32 (b23.val[1])),
                                    vcombine_f32 (vget_high_f32 (b01.val[0]), vget_high_f32 (b23.val[0])), vcombine_f32 (vget_high_f32 (b01.val[1]), vget_high_f32 (b23.val[1])) };
        for (int j = 0; j < 4; ++j)
            sum = vaddq_f32 (sum, vmulq_f32 (cc[j], bb[j]));
    }
    return sum; // (0 + c0 b0 is c0 b0 exactly: the scalar low form starts with the product, the others with 0 +)
}

/** sanitizeFloat () of four lanes: a NaN, an infinity and a value below 1e-18 in magnitude (denormals, zeros) become +0. */
inline float32x4_t sanitize4 (float32x4_t v) noexcept
{
    const float32x4_t a = vabsq_f32 (v);
    const uint32x4_t keep = vandq_u32 (vcgeq_f32 (a, vdupq_n_f32 (1.0e-18f)), vcltq_f32 (a, vdupq_n_f32 (INFINITY)));
    return vreinterpretq_f32_u32 (vandq_u32 (vreinterpretq_u32_f32 (v), keep));
}
#endif

} // namespace

void SincTables::init() noexcept
{
    // Low: 4 taps, M = 2, Hann window
    for (int p = 0; p < kPhases; ++p)
    {
        const double mu = static_cast<double> (p) / static_cast<double> (kPhases);
        double sum = 0.0;
        for (int k = 0; k < kLowTaps; ++k)
        {
            const double d = static_cast<double> (k - 1) - mu;
            const double sinc = (std::fabs (d) < 1.0e-7) ? 1.0 : std::sin (kPi * d) / (kPi * d);
            const double win = 0.5 + 0.5 * std::cos (kPi * d * 0.5);
            low[p * kLowTaps + k] = static_cast<float> (sinc * win);
            sum += low[p * kLowTaps + k];
        }
        if (sum != 0.0)
            for (int k = 0; k < kLowTaps; ++k)
                low[p * kLowTaps + k] = static_cast<float> (low[p * kLowTaps + k] / sum);
    }

    // Medium: 8 taps, M = 4, Blackman window
    for (int p = 0; p < kPhases; ++p)
    {
        const double mu = static_cast<double> (p) / static_cast<double> (kPhases);
        double sum = 0.0;
        for (int k = 0; k < kMedTaps; ++k)
        {
            const double d = static_cast<double> (k - 3) - mu;
            const double sinc = (std::fabs (d) < 1.0e-7) ? 1.0 : std::sin (kPi * d) / (kPi * d);
            const double u = kPi * d * 0.25;
            const double win = 0.42 + 0.5 * std::cos (u) + 0.08 * std::cos (2.0 * u);
            med[p * kMedTaps + k] = static_cast<float> (sinc * win);
            sum += med[p * kMedTaps + k];
        }
        if (sum != 0.0)
            for (int k = 0; k < kMedTaps; ++k)
                med[p * kMedTaps + k] = static_cast<float> (med[p * kMedTaps + k] / sum);
    }

    // High: 16 taps, M = 8, 4-term Blackman-Harris window
    for (int p = 0; p < kPhases; ++p)
    {
        const double mu = static_cast<double> (p) / static_cast<double> (kPhases);
        double sum = 0.0;
        for (int k = 0; k < kHighTaps; ++k)
        {
            const double d = static_cast<double> (k - 7) - mu;
            const double sinc = (std::fabs (d) < 1.0e-7) ? 1.0 : std::sin (kPi * d) / (kPi * d);
            const double u = kPi * d * 0.125;
            const double win = 0.35875 + 0.48829 * std::cos (u) + 0.14128 * std::cos (2.0 * u) + 0.01168 * std::cos (3.0 * u);
            high[p * kHighTaps + k] = static_cast<float> (sinc * win);
            sum += high[p * kHighTaps + k];
        }
        if (sum != 0.0)
            for (int k = 0; k < kHighTaps; ++k)
                high[p * kHighTaps + k] = static_cast<float> (high[p * kHighTaps + k] / sum);
    }
}

const SincTables& SincTables::instance() noexcept
{
    static const SincTables tables = []() {
        SincTables t;
        t.init();
        return t;
    }();
    return tables;
}

void SampleVoice::prepare (double hostSampleRate, int /*maxBlockFrames*/) noexcept
{
    hostSampleRate_ = (hostSampleRate >= 22050.0 && hostSampleRate <= 768000.0) ? hostSampleRate : 44100.0;
    SincTables::instance(); // warm up table
    reset();
}

void SampleVoice::reset() noexcept
{
    active_ = false;
    stopping_ = false;
    sample_ = nullptr;
    playhead_ = 0.0;
    direction_ = 1.0;
    antiClickGain_ = 0.0f;
    antiClickStep_ = 0.0f;
    unisonPitchRatio_ = 1.0f;
    unisonStartOffset_ = 0.0f;
    unisonPan_ = 0.0f;
    unisonLevel_ = 1.0f;
    semitoneOffset_ = std::numeric_limits<float>::quiet_NaN();
    speed_ = 1.0;
}

void SampleVoice::noteOn (int midiKey, float velocity, const SampleView* sample, const SampleOscParams& params) noexcept
{
    if (sample == nullptr || ! sample->isValid())
    {
        reset();
        return;
    }

    sample_ = sample;
    params_ = params;
    midiKey_ = midiKey;
    velocity_ = std::max (0.0f, std::min (1.0f, velocity));
    stopping_ = false;

    updatePitchRatio();

    const auto numFrames = static_cast<double> (sample_->numFrames);
    const double fStart = std::max (0.0, std::min (1.0, static_cast<double> (params_.startOffset))) * (numFrames - 1.0);
    const double fEnd = std::max (0.0, std::min (1.0, static_cast<double> (params_.endOffset))) * (numFrames - 1.0);
    const double span = std::fabs (fEnd - fStart);

    if (params_.reverse)
    {
        direction_ = -1.0;
        playhead_ = std::max (fStart, fEnd) - static_cast<double> (unisonStartOffset_) * span;
    }
    else
    {
        direction_ = 1.0;
        playhead_ = std::min (fStart, fEnd) + static_cast<double> (unisonStartOffset_) * span;
    }
    playhead_ = std::max (0.0, std::min (numFrames - 1.0, playhead_));

    // Anti-click start ramp (~1.5 ms)
    const double rampFrames = std::max (16.0, 0.0015 * hostSampleRate_);
    antiClickGain_ = 0.0f;
    antiClickStep_ = static_cast<float> (1.0 / rampFrames);
    active_ = true;
}

void SampleVoice::noteOff() noexcept
{
    if (! active_ || stopping_)
        return;

    stopping_ = true;
    const double releaseFrames = std::max (16.0, 0.002 * hostSampleRate_);
    antiClickStep_ = -static_cast<float> (1.0 / releaseFrames);
}

void SampleVoice::stop() noexcept
{
    noteOff();
}

void SampleVoice::setParams (const SampleOscParams& params) noexcept
{
    params_ = params;
    updatePitchRatio();
}

void SampleVoice::setUnisonPitchRatio (float ratio) noexcept
{
    unisonPitchRatio_ = std::max (0.0001f, std::min (64.0f, ratio));
    updatePitchRatio();
}

void SampleVoice::setUnisonStartOffset (float normalizedOffset) noexcept
{
    unisonStartOffset_ = std::max (0.0f, std::min (1.0f, normalizedOffset));
}

void SampleVoice::setUnisonPan (float pan) noexcept
{
    unisonPan_ = std::max (-1.0f, std::min (1.0f, pan));
}

void SampleVoice::setUnisonLevel (float level) noexcept
{
    unisonLevel_ = std::max (0.0f, std::min (4.0f, level));
}

void SampleVoice::setSemitoneOffset (float semitones) noexcept
{
    semitoneOffset_ = std::isfinite (semitones) ? std::max (-120.0f, std::min (120.0f, semitones))
                                                : std::numeric_limits<float>::quiet_NaN();
    updatePitchRatio();
}

void SampleVoice::setSpeedMultiplier (double speed) noexcept
{
    speed_ = std::isfinite (speed) ? std::max (-64.0, std::min (64.0, speed)) : 1.0;
}

void SampleVoice::releaseLoop() noexcept
{
    params_.loopMode = LoopMode::none;
}

void SampleVoice::seekFraction (double fraction) noexcept
{
    if (sample_ == nullptr || ! sample_->isValid())
        return;
    const auto numFrames = static_cast<double> (sample_->numFrames);
    const double a = std::max (0.0, std::min (1.0, static_cast<double> (params_.startOffset))) * (numFrames - 1.0);
    const double b = std::max (0.0, std::min (1.0, static_cast<double> (params_.endOffset))) * (numFrames - 1.0);
    const double lo = std::min (a, b), hi = std::max (a, b);
    fraction = std::max (0.0, std::min (1.0, fraction));
    playhead_ = direction_ >= 0.0 ? lo + fraction * (hi - lo) : hi - fraction * (hi - lo);
}

void SampleVoice::processModulated (float* outLeft, float* outRight, int numFrames, const float* rateMod) noexcept
{
    if (outLeft == nullptr || numFrames <= 0)
        return;
    if (useKernel())
    {
        renderBlock (outLeft, outRight, numFrames, rateMod, false);
        return;
    }
    for (int i = 0; i < numFrames; ++i)
    {
        float l = 0.0f, r = 0.0f;
        renderFrame (l, r, rateMod != nullptr ? static_cast<double> (rateMod[i]) : 1.0);
        outLeft[i] = l;
        if (outRight != nullptr)
            outRight[i] = r;
    }
}

void SampleVoice::updatePitchRatio() noexcept
{
    if (sample_ == nullptr || ! sample_->isValid() || hostSampleRate_ <= 0.0)
    {
        pitchRatio_ = 1.0;
        return;
    }

    float semitones;
    if (std::isnan (semitoneOffset_))
    {
        const float playedKey = params_.pitchTracking ? static_cast<float> (midiKey_)
                                                      : static_cast<float> (params_.rootKey);
        semitones = (playedKey - static_cast<float> (params_.rootKey))
                  + params_.tune
                  + ((params_.fine + sample_->fineTuneCents) / 100.0f);
    }
    else
        semitones = semitoneOffset_ + (params_.fine + sample_->fineTuneCents) / 100.0f;

    if (! (semitones == cachedSemitones_)) // (a NaN never matches: the first call and any non finite value compute)
    {
        cachedSpeedFromPitch_ = std::pow (2.0, static_cast<double> (semitones) / 12.0);
        cachedSemitones_ = semitones;
    }
    const double speedFromPitch = cachedSpeedFromPitch_;
    const double speedFromSampleRate = sample_->sampleRate / hostSampleRate_;

    pitchRatio_ = std::max (1.0e-5, std::min (64.0, speedFromPitch * speedFromSampleRate * static_cast<double> (unisonPitchRatio_)));
}

void SampleVoice::renderFrame (float& outL, float& outR) noexcept
{
    renderFrame (outL, outR, 1.0);
}

void SampleVoice::renderFrame (float& outL, float& outR, double rateMult) noexcept
{
    if (! active_ || sample_ == nullptr || ! sample_->isValid())
    {
        outL = 0.0f;
        outR = 0.0f;
        return;
    }

    const auto numFrames = static_cast<double> (sample_->numFrames);
    const double fStart = std::max (0.0, std::min (1.0, static_cast<double> (params_.startOffset))) * (numFrames - 1.0);
    const double fEnd = std::max (0.0, std::min (1.0, static_cast<double> (params_.endOffset))) * (numFrames - 1.0);
    const double boundLo = std::min (fStart, fEnd);
    const double boundHi = std::max (fStart, fEnd);

    if (boundHi - boundLo < 1.0)
    {
        active_ = false;
        outL = 0.0f;
        outR = 0.0f;
        return;
    }

    double lStart = std::max (0.0, std::min (1.0, static_cast<double> (params_.loopStart))) * (numFrames - 1.0);
    double lEnd = std::max (0.0, std::min (1.0, static_cast<double> (params_.loopEnd))) * (numFrames - 1.0);
    if (lEnd < lStart)
        std::swap (lStart, lEnd);

    lStart = std::max (boundLo, std::min (boundHi, lStart));
    lEnd = std::max (boundLo, std::min (boundHi, lEnd));
    const double lLen = lEnd - lStart;

    const auto& tables = SincTables::instance();
    float sL = 0.0f;
    float sR = 0.0f;

    const LoopMode mode = params_.loopMode == LoopMode::tailed ? LoopMode::forward : params_.loopMode;
    const double xLen = (mode == LoopMode::forward && lLen > 2.0)
        ? std::max (0.0, std::min (0.49 * lLen, static_cast<double> (params_.loopCrossfade) * lLen))
        : 0.0;

    if (xLen > 1.0 && playhead_ >= lEnd - xLen && playhead_ < lEnd)
    {
        const double t = (playhead_ - (lEnd - xLen)) / xLen;
        const double wrapPos = lStart + t * xLen;

        float outL1 = 0.0f, outR1 = 0.0f;
        float outL2 = 0.0f, outR2 = 0.0f;
        readSampleStereo (*sample_, playhead_, pitchRatio_, params_.quality, tables, outL1, outR1);
        readSampleStereo (*sample_, wrapPos, pitchRatio_, params_.quality, tables, outL2, outR2);

        const double wOut = std::cos (t * 0.5 * kPi);
        const double wIn = std::sin (t * 0.5 * kPi);
        sL = static_cast<float> (wOut * outL1 + wIn * outL2);
        sR = static_cast<float> (wOut * outR1 + wIn * outR2);
    }
    else
    {
        readSampleStereo (*sample_, playhead_, pitchRatio_, params_.quality, tables, sL, sR);
    }

    // Panning (equal-power)
    const float pan = std::max (-1.0f, std::min (1.0f, params_.pan + unisonPan_));
    const float theta = (pan + 1.0f) * static_cast<float> (kPi * 0.25);
    const float gainL = std::cos (theta);
    const float gainR = std::sin (theta);
    float edge = 1.0f;
    if (params_.edgeFadeMs > 0.0f)
    {
        const double fadeFrames = std::max (1.0, static_cast<double> (params_.edgeFadeMs) * 0.001 * sample_->sampleRate);
        edge = static_cast<float> (std::max (0.0, std::min (1.0, std::min ((playhead_ - boundLo) / fadeFrames, (boundHi - playhead_) / fadeFrames))));
    }
    const float totalGain = std::max (0.0f, params_.level) * std::max (0.0f, unisonLevel_) * velocity_ * antiClickGain_ * edge;

    outL = sanitizeFloat (sL * gainL * totalGain);
    outR = sanitizeFloat (sR * gainR * totalGain);

    // Anti-click ramp update
    if (stopping_)
    {
        antiClickGain_ += antiClickStep_;
        if (antiClickGain_ <= 0.0f)
        {
            antiClickGain_ = 0.0f;
            active_ = false;
            return;
        }
    }
    else if (antiClickGain_ < 1.0f)
    {
        antiClickGain_ += antiClickStep_;
        if (antiClickGain_ >= 1.0f)
            antiClickGain_ = 1.0f;
    }

    // Advance playhead
    const double step = pitchRatio_ * speed_ * rateMult * direction_;
    playhead_ += step;
    const double dir = step >= 0.0 ? 1.0 : -1.0; // the effective direction (the scan can run the head backwards)

    // Looping / boundary state machine
    switch (mode)
    {
        case LoopMode::none:
        {
            if (dir > 0.0 && playhead_ >= boundHi)
                active_ = false;
            else if (dir < 0.0 && playhead_ <= boundLo)
                active_ = false;
            break;
        }
        case LoopMode::tailed: // handled as forward above
        case LoopMode::forward:
        {
            if (dir > 0.0 && playhead_ >= lEnd)
            {
                if (lLen > 1.0)
                {
                    playhead_ = lStart + xLen + (playhead_ - lEnd);
                    while (playhead_ >= lEnd)
                        playhead_ -= std::max (1.0, lLen - xLen);
                }
                else
                {
                    active_ = false;
                }
            }
            else if (dir < 0.0 && playhead_ <= lStart)
            {
                if (lLen > 1.0)
                    playhead_ = lEnd - (lStart - playhead_);
                else
                    active_ = false;
            }
            break;
        }
        case LoopMode::pingPong:
        {
            if (dir > 0.0 && playhead_ >= lEnd)
            {
                playhead_ = lEnd - (playhead_ - lEnd);
                direction_ = -direction_;
            }
            else if (dir < 0.0 && playhead_ <= lStart)
            {
                playhead_ = lStart + (lStart - playhead_);
                direction_ = -direction_;
            }
            break;
        }
        case LoopMode::reverse:
        {
            if (dir < 0.0 && playhead_ <= lStart)
            {
                if (lLen > 1.0)
                    playhead_ = lEnd - (lStart - playhead_);
                else
                    active_ = false;
            }
            else if (dir > 0.0 && playhead_ >= lEnd)
            {
                direction_ = -direction_;
            }
            break;
        }
    }
}

void SampleVoice::process (float* outLeft, float* outRight, int numFrames) noexcept
{
    if (outLeft == nullptr || numFrames <= 0)
        return;
    if (useKernel())
    {
        renderBlock (outLeft, outRight, numFrames, nullptr, false);
        return;
    }
    for (int i = 0; i < numFrames; ++i)
    {
        float l = 0.0f, r = 0.0f;
        renderFrame (l, r);
        outLeft[i] = l;
        if (outRight != nullptr)
            outRight[i] = r;
    }
}

void SampleVoice::processAdd (float* outLeft, float* outRight, int numFrames) noexcept
{
    if (outLeft == nullptr || numFrames <= 0)
        return;
    if (useKernel())
    {
        renderBlock (outLeft, outRight, numFrames, nullptr, true);
        return;
    }
    for (int i = 0; i < numFrames; ++i)
    {
        float l = 0.0f, r = 0.0f;
        renderFrame (l, r);
        outLeft[i] += l;
        if (outRight != nullptr)
            outRight[i] += r;
    }
}

void SampleVoice::renderBlock (float* outLeft, float* outRight, int numFrames, const float* rateMod, bool add) noexcept
{
    if (! active_ || sample_ == nullptr || ! sample_->isValid())
    {
        if (! add)
            for (int i = 0; i < numFrames; ++i)
            {
                outLeft[i] = 0.0f;
                if (outRight != nullptr)
                    outRight[i] = 0.0f;
            }
        return;
    }
    switch (params_.quality)
    {
        case QualityTier::low:
            if (add) renderBlockT<QualityTier::low, true> (outLeft, outRight, numFrames, rateMod); else renderBlockT<QualityTier::low, false> (outLeft, outRight, numFrames, rateMod);
            break;
        case QualityTier::medium:
            if (add) renderBlockT<QualityTier::medium, true> (outLeft, outRight, numFrames, rateMod); else renderBlockT<QualityTier::medium, false> (outLeft, outRight, numFrames, rateMod);
            break;
        default:
            if (add) renderBlockT<QualityTier::high, true> (outLeft, outRight, numFrames, rateMod); else renderBlockT<QualityTier::high, false> (outLeft, outRight, numFrames, rateMod);
            break;
    }
}

template <QualityTier Tier, bool Add>
void SampleVoice::renderBlockT (float* outLeft, float* outRight, int numFrames, const float* rateMod) noexcept
{
    const auto numFramesD = static_cast<double> (sample_->numFrames);
    const double fStart = std::max (0.0, std::min (1.0, static_cast<double> (params_.startOffset))) * (numFramesD - 1.0);
    const double fEnd = std::max (0.0, std::min (1.0, static_cast<double> (params_.endOffset))) * (numFramesD - 1.0);
    const double boundLo = std::min (fStart, fEnd);
    const double boundHi = std::max (fStart, fEnd);
    if (boundHi - boundLo < 1.0)
    {
        active_ = false;
        if (! Add)
            for (int i = 0; i < numFrames; ++i)
            {
                outLeft[i] = 0.0f;
                if (outRight != nullptr)
                    outRight[i] = 0.0f;
            }
        return;
    }

    double lStart = std::max (0.0, std::min (1.0, static_cast<double> (params_.loopStart))) * (numFramesD - 1.0);
    double lEnd = std::max (0.0, std::min (1.0, static_cast<double> (params_.loopEnd))) * (numFramesD - 1.0);
    if (lEnd < lStart)
        std::swap (lStart, lEnd);
    lStart = std::max (boundLo, std::min (boundHi, lStart));
    lEnd = std::max (boundLo, std::min (boundHi, lEnd));
    const double lLen = lEnd - lStart;

    const auto& tables = SincTables::instance();
    const float* const table = Tier == QualityTier::low ? tables.low : (Tier == QualityTier::medium ? tables.med : tables.high);
    const ReadPlan plan = makePlan (*sample_, pitchRatio_, Tier);

    const LoopMode mode = params_.loopMode == LoopMode::tailed ? LoopMode::forward : params_.loopMode;
    const double xLen = (mode == LoopMode::forward && lLen > 2.0)
        ? std::max (0.0, std::min (0.49 * lLen, static_cast<double> (params_.loopCrossfade) * lLen))
        : 0.0;

    const float pan = std::max (-1.0f, std::min (1.0f, params_.pan + unisonPan_));
    const float theta = (pan + 1.0f) * static_cast<float> (kPi * 0.25);
    const float gainL = std::cos (theta);
    const float gainR = std::sin (theta);
    const bool fadeEdges = params_.edgeFadeMs > 0.0f;
    const double fadeFrames = fadeEdges ? std::max (1.0, static_cast<double> (params_.edgeFadeMs) * 0.001 * sample_->sampleRate) : 1.0;
    const float levelPart = std::max (0.0f, params_.level) * std::max (0.0f, unisonLevel_) * velocity_;

#if SB_SAMPLE_NEON
    // a run of samples in which nothing but the plain read happens (no fade, no release, no rate modulation, no loop event, no crossfade, no mip blend): four at a time
    constexpr int kTaps = Tier == QualityTier::low ? 4 : (Tier == QualityTier::medium ? 8 : 16);
    const bool runOk = vectors_ && rateMod == nullptr && ! fadeEdges && plan.kind != ReadPlan::blend && ! plan.useUpper;
    // the interval of positions (of the playhead) inside which no event can happen and every tap of the read lies inside the data
    double runLo = boundLo, runHi = boundHi;
    if (mode == LoopMode::forward || mode == LoopMode::pingPong || mode == LoopMode::reverse)
    {
        runLo = lStart;
        runHi = (mode == LoopMode::forward && xLen > 1.0) ? lEnd - xLen : lEnd;
    }
    {
        const double inv = plan.kind == ReadPlan::oneLevel ? plan.invScale0 : 1.0;
        const double marginLo = static_cast<double> (kTaps / 2 - 1) + 1.0, marginHi = static_cast<double> (kTaps / 2) + 1.0;
        runLo = std::max (runLo, marginLo / inv);
        runHi = std::min (runHi, (static_cast<double> (plan.na) - marginHi) / inv);
    }
#endif
    int i = 0;
    for (; i < numFrames; ++i)
    {
        if (! active_)
            break;
#if SB_SAMPLE_NEON
        const double runStep = pitchRatio_ * speed_ * 1.0 * direction_; // (the direction changes at a ping pong turn)
        if (runOk && ! stopping_ && antiClickGain_ >= 1.0f && i + 4 <= numFrames && runStep != 0.0 && playhead_ > runLo && playhead_ < runHi)
        {
            // whole samples that keep the playhead (and the next one) strictly inside the interval, with two samples of margin for the rounding of the repeated additions
            const double room = runStep > 0.0 ? (runHi - playhead_) / runStep : (runLo - playhead_) / runStep;
            const int fit = static_cast<int> (std::min (room, 1.0e6)) - 2;
            int n = std::min (fit, numFrames - i) & ~3;
            if (n >= 4)
            {
                const double inv = plan.kind == ReadPlan::oneLevel ? plan.invScale0 : 1.0;
                const float32x4_t gL = vdupq_n_f32 (gainL), gR = vdupq_n_f32 (gainR), total = vdupq_n_f32 (levelPart * antiClickGain_ * 1.0f);
                for (int done = 0; done < n; done += 4)
                {
                    double pos[4];
                    pos[0] = playhead_;
                    pos[1] = pos[0] + runStep;
                    pos[2] = pos[1] + runStep;
                    pos[3] = pos[2] + runStep;
                    int32_t idx[4], ph[4];
                    for (int k = 0; k < 4; ++k)
                    {
                        const double q = plan.kind == ReadPlan::oneLevel ? pos[k] * inv : pos[k];
                        idx[k] = static_cast<int> (q);
                        ph[k] = static_cast<int> ((q - static_cast<double> (idx[k])) * SincTables::kPhases);
                    }
                    const float32x4_t sL = interp4<Tier> (plan.a[0], idx, ph, table);
                    const float32x4_t sR = plan.stereo ? interp4<Tier> (plan.a[1], idx, ph, table) : sL;
                    const float32x4_t l = sanitize4 (vmulq_f32 (vmulq_f32 (sL, gL), total));
                    const float32x4_t r = sanitize4 (vmulq_f32 (vmulq_f32 (sR, gR), total));
                    if (Add)
                    {
                        vst1q_f32 (outLeft + i + done, vaddq_f32 (vld1q_f32 (outLeft + i + done), l));
                        if (outRight != nullptr)
                            vst1q_f32 (outRight + i + done, vaddq_f32 (vld1q_f32 (outRight + i + done), r));
                    }
                    else
                    {
                        vst1q_f32 (outLeft + i + done, l);
                        if (outRight != nullptr)
                            vst1q_f32 (outRight + i + done, r);
                    }
                    playhead_ = pos[3] + runStep;
                }
                i += n - 1; // (the loop's own increment follows)
                continue;
            }
        }
#endif
        float sL = 0.0f;
        float sR = 0.0f;
        if (xLen > 1.0 && playhead_ >= lEnd - xLen && playhead_ < lEnd)
        {
            const double t = (playhead_ - (lEnd - xLen)) / xLen;
            const double wrapPos = lStart + t * xLen;
            float outL1 = 0.0f, outR1 = 0.0f;
            float outL2 = 0.0f, outR2 = 0.0f;
            readPlanned<Tier> (plan, table, playhead_, outL1, outR1);
            readPlanned<Tier> (plan, table, wrapPos, outL2, outR2);
            const double wOut = std::cos (t * 0.5 * kPi);
            const double wIn = std::sin (t * 0.5 * kPi);
            sL = static_cast<float> (wOut * outL1 + wIn * outL2);
            sR = static_cast<float> (wOut * outR1 + wIn * outR2);
        }
        else
            readPlanned<Tier> (plan, table, playhead_, sL, sR);

        float edge = 1.0f;
        if (fadeEdges)
            edge = static_cast<float> (std::max (0.0, std::min (1.0, std::min ((playhead_ - boundLo) / fadeFrames, (boundHi - playhead_) / fadeFrames))));
        const float totalGain = levelPart * antiClickGain_ * edge;
        const float l = sanitizeFloat (sL * gainL * totalGain);
        const float r = sanitizeFloat (sR * gainR * totalGain);
        if (Add)
        {
            outLeft[i] += l;
            if (outRight != nullptr)
                outRight[i] += r;
        }
        else
        {
            outLeft[i] = l;
            if (outRight != nullptr)
                outRight[i] = r;
        }

        // Anti-click ramp update
        if (stopping_)
        {
            antiClickGain_ += antiClickStep_;
            if (antiClickGain_ <= 0.0f)
            {
                antiClickGain_ = 0.0f;
                active_ = false;
                continue;
            }
        }
        else if (antiClickGain_ < 1.0f)
        {
            antiClickGain_ += antiClickStep_;
            if (antiClickGain_ >= 1.0f)
                antiClickGain_ = 1.0f;
        }

        // Advance playhead
        const double step = pitchRatio_ * speed_ * (rateMod != nullptr ? static_cast<double> (rateMod[i]) : 1.0) * direction_;
        playhead_ += step;
        const double dir = step >= 0.0 ? 1.0 : -1.0;

        switch (mode)
        {
            case LoopMode::none:
            {
                if (dir > 0.0 && playhead_ >= boundHi)
                    active_ = false;
                else if (dir < 0.0 && playhead_ <= boundLo)
                    active_ = false;
                break;
            }
            case LoopMode::tailed:
            case LoopMode::forward:
            {
                if (dir > 0.0 && playhead_ >= lEnd)
                {
                    if (lLen > 1.0)
                    {
                        playhead_ = lStart + xLen + (playhead_ - lEnd);
                        while (playhead_ >= lEnd)
                            playhead_ -= std::max (1.0, lLen - xLen);
                    }
                    else
                        active_ = false;
                }
                else if (dir < 0.0 && playhead_ <= lStart)
                {
                    if (lLen > 1.0)
                        playhead_ = lEnd - (lStart - playhead_);
                    else
                        active_ = false;
                }
                break;
            }
            case LoopMode::pingPong:
            {
                if (dir > 0.0 && playhead_ >= lEnd)
                {
                    playhead_ = lEnd - (playhead_ - lEnd);
                    direction_ = -direction_;
                }
                else if (dir < 0.0 && playhead_ <= lStart)
                {
                    playhead_ = lStart + (lStart - playhead_);
                    direction_ = -direction_;
                }
                break;
            }
            case LoopMode::reverse:
            {
                if (dir < 0.0 && playhead_ <= lStart)
                {
                    if (lLen > 1.0)
                        playhead_ = lEnd - (lStart - playhead_);
                    else
                        active_ = false;
                }
                else if (dir > 0.0 && playhead_ >= lEnd)
                    direction_ = -direction_;
                break;
            }
        }
    }
    if (! Add)
        for (; i < numFrames; ++i)
        {
            outLeft[i] = 0.0f;
            if (outRight != nullptr)
                outRight[i] = 0.0f;
        }
}

} // namespace sampleosc
} // namespace synthyboy
