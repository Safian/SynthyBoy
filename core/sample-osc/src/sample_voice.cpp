// SynthyBoy - core/sample-osc: polyphony-ready, real-time safe sample oscillator voice.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/sampleosc/sample_voice.h"

#include <algorithm>
#include <cmath>

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

    const double speedFromPitch = std::pow (2.0, static_cast<double> (semitones) / 12.0);
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

    for (int i = 0; i < numFrames; ++i)
    {
        float l = 0.0f, r = 0.0f;
        renderFrame (l, r);
        outLeft[i] += l;
        if (outRight != nullptr)
            outRight[i] += r;
    }
}

} // namespace sampleosc
} // namespace synthyboy
