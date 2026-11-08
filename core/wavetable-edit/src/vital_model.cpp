// SynthyBoy - wavetable-edit: Vital Group/Component/Keyframe model implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::wtedit::vital
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Hermite / Catmull-Rom interpolation
float catmullRom(float p0, float p1, float p2, float p3, float t) noexcept
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

// Triangle folding for WaveFoldModifier
float foldSample(float x, float boost) noexcept
{
    float val = x * (1.0f + boost * 5.0f);
    // Periodically fold into [-1, 1]
    while (val > 1.0f || val < -1.0f)
    {
        if (val > 1.0f)
            val = 2.0f - val;
        else if (val < -1.0f)
            val = -2.0f - val;
    }
    return val;
}
} // namespace

Component::Component(ComponentType type, const std::string& name)
    : type_(type), name_(name)
{
    if (name_.empty())
    {
        switch (type)
        {
        case ComponentType::WaveSource:              name_ = "Wave Source"; break;
        case ComponentType::LineSource:              name_ = "Line Source"; break;
        case ComponentType::FileSource:              name_ = "File Source"; break;
        case ComponentType::PhaseModifier:           name_ = "Phase Modifier"; break;
        case ComponentType::WaveWindowModifier:      name_ = "Wave Window Modifier"; break;
        case ComponentType::FrequencyFilterModifier: name_ = "Frequency Filter Modifier"; break;
        case ComponentType::SlewLimitModifier:       name_ = "Slew Limit Modifier"; break;
        case ComponentType::WaveFoldModifier:        name_ = "Wave Fold Modifier"; break;
        case ComponentType::WaveWarpModifier:        name_ = "Wave Warp Modifier"; break;
        }
    }
}

void Component::insertKeyframe(const Keyframe& kf)
{
    auto it = std::lower_bound(keyframes_.begin(), keyframes_.end(), kf.position,
                               [](const Keyframe& item, float pos) {
                                   return item.position < pos;
                               });
    keyframes_.insert(it, kf);
}

void Component::removeKeyframe(size_t index)
{
    if (index < keyframes_.size())
        keyframes_.erase(keyframes_.begin() + static_cast<std::ptrdiff_t>(index));
}

void Component::process(float position, Frame& frame) const noexcept
{
    if (!enabled_ || keyframes_.empty())
        return;

    // Find keyframe interval
    size_t leftIdx = 0;
    size_t rightIdx = 0;
    float t = 0.0f;

    if (position <= keyframes_.front().position)
    {
        leftIdx = 0;
        rightIdx = 0;
        t = 0.0f;
    }
    else if (position >= keyframes_.back().position)
    {
        leftIdx = keyframes_.size() - 1;
        rightIdx = leftIdx;
        t = 0.0f;
    }
    else
    {
        for (size_t i = 0; i < keyframes_.size() - 1; ++i)
        {
            if (position >= keyframes_[i].position && position <= keyframes_[i + 1].position)
            {
                leftIdx = i;
                rightIdx = i + 1;
                const float span = keyframes_[rightIdx].position - keyframes_[leftIdx].position;
                t = (span > 1e-6f) ? ((position - keyframes_[leftIdx].position) / span) : 0.0f;
                break;
            }
        }
    }

    const Keyframe& kfLeft = keyframes_[leftIdx];
    const Keyframe& kfRight = keyframes_[rightIdx];

    // Check if component is a source
    const bool isSource = (type_ == ComponentType::WaveSource ||
                           type_ == ComponentType::LineSource ||
                           type_ == ComponentType::FileSource);

    if (isSource)
    {
        if (leftIdx == rightIdx || interpStyle_ == InterpolationStyle::None)
        {
            frame = kfLeft.waveform;
        }
        else if (interpStyle_ == InterpolationStyle::Linear)
        {
            for (size_t i = 0; i < kWaveformSize; ++i)
                frame[i] = (1.0f - t) * kfLeft.waveform[i] + t * kfRight.waveform[i];
        }
        else if (interpStyle_ == InterpolationStyle::Cubic)
        {
            const size_t i0 = (leftIdx > 0) ? (leftIdx - 1) : leftIdx;
            const size_t i3 = (rightIdx + 1 < keyframes_.size()) ? (rightIdx + 1) : rightIdx;
            const Keyframe& kf0 = keyframes_[i0];
            const Keyframe& kf3 = keyframes_[i3];

            for (size_t i = 0; i < kWaveformSize; ++i)
            {
                frame[i] = catmullRom(kf0.waveform[i], kfLeft.waveform[i], kfRight.waveform[i], kf3.waveform[i], t);
            }
        }
        else if (interpStyle_ == InterpolationStyle::Spectral)
        {
            std::array<float, kNumFftBins + 1> magL {};
            std::array<float, kNumFftBins + 1> phaseL {};
            std::array<float, kNumFftBins + 1> magR {};
            std::array<float, kNumFftBins + 1> phaseR {};

            Radix2Fft::realToPolar(kfLeft.waveform.sampleData(), magL.data(), phaseL.data(), kWaveformSize);
            Radix2Fft::realToPolar(kfRight.waveform.sampleData(), magR.data(), phaseR.data(), kWaveformSize);

            std::array<float, kNumFftBins + 1> mag {};
            std::array<float, kNumFftBins + 1> phase {};

            for (size_t k = 0; k <= kNumFftBins; ++k)
            {
                mag[k] = (1.0f - t) * magL[k] + t * magR[k];
                float diff = phaseR[k] - phaseL[k];
                while (diff > static_cast<float>(kPi)) diff -= static_cast<float>(2.0 * kPi);
                while (diff < static_cast<float>(-kPi)) diff += static_cast<float>(2.0 * kPi);
                phase[k] = phaseL[k] + t * diff;
            }

            Radix2Fft::polarToReal(mag.data(), phase.data(), frame.sampleData(), kWaveformSize);
        }
        return;
    }

    // Modifiers: interpolate modifier settings
    const float p1 = (1.0f - t) * kfLeft.param1 + t * kfRight.param1;
    const float p2 = (1.0f - t) * kfLeft.param2 + t * kfRight.param2;
    const int style = (t < 0.5f) ? kfLeft.style : kfRight.style;

    switch (type_)
    {
    case ComponentType::PhaseModifier:
    {
        // p1: phase offset, p2: mix
        std::array<float, kNumFftBins + 1> mags {};
        std::array<float, kNumFftBins + 1> phases {};
        Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

        const float phaseShift = p1 * static_cast<float>(2.0 * kPi);
        for (size_t k = 1; k <= kNumFftBins; ++k)
        {
            float shift = phaseShift;
            if (style == 1) // Even / Odd
                shift = (k % 2 == 0) ? phaseShift : -phaseShift;
            else if (style == 2) // Harmonic
                shift = phaseShift * static_cast<float>(k);
            else if (style == 3) // Harmonic Even / Odd
                shift = (k % 2 == 0) ? (phaseShift * static_cast<float>(k)) : (-phaseShift * static_cast<float>(k));
            else if (style == 4) // Clear
                shift = -phases[k];

            phases[k] += shift * p2;
        }

        Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
        break;
    }

    case ComponentType::WaveWindowModifier:
    {
        // p1: window left, p2: window right
        const float left = std::max(0.0f, std::min(1.0f, p1));
        const float right = std::max(left, std::min(1.0f, p2));

        for (size_t i = 0; i < kWaveformSize; ++i)
        {
            const float pos = static_cast<float>(i) / static_cast<float>(kWaveformSize);
            float win = 1.0f;
            if (pos < left || pos > right)
            {
                win = 0.0f;
            }
            else if (right > left)
            {
                const float normPos = (pos - left) / (right - left);
                if (style == 0) // Cosine
                    win = 0.5f * (1.0f - std::cos(static_cast<float>(2.0 * kPi) * normPos));
                else if (style == 1) // Half Sin
                    win = std::sin(static_cast<float>(kPi) * normPos);
                else if (style == 2) // Linear
                    win = (normPos < 0.5f) ? (2.0f * normPos) : (2.0f * (1.0f - normPos));
                else if (style == 3) // Square
                    win = 1.0f;
                else if (style == 4) // Wiggle
                    win = 0.5f * (1.0f + std::sin(static_cast<float>(6.0 * kPi) * normPos));
            }
            frame[i] *= win;
        }
        break;
    }

    case ComponentType::FrequencyFilterModifier:
    {
        // p1: cutoff [0..1], p2: shape [0..1]
        std::array<float, kNumFftBins + 1> mags {};
        std::array<float, kNumFftBins + 1> phases {};
        Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

        const float cutoffBin = std::max(1.0f, p1 * static_cast<float>(kNumFftBins));

        for (size_t k = 1; k <= kNumFftBins; ++k)
        {
            const float kF = static_cast<float>(k);
            float gain = 1.0f;
            if (style == 0) // Low Pass
                gain = (kF <= cutoffBin) ? 1.0f : std::max(0.0f, 1.0f - (kF - cutoffBin) / (cutoffBin * (1.0f + p2) + 1.0f));
            else if (style == 1) // High Pass
                gain = (kF >= cutoffBin) ? 1.0f : std::max(0.0f, 1.0f - (cutoffBin - kF) / (cutoffBin * (1.0f + p2) + 1.0f));
            else if (style == 2) // Band Pass
                gain = std::max(0.0f, 1.0f - std::fabs(kF - cutoffBin) / (cutoffBin * 0.5f + 1.0f));
            else if (style == 3) // Comb
                gain = 0.5f * (1.0f + std::cos(static_cast<float>(kPi) * kF / cutoffBin));

            mags[k] *= gain;
        }

        Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
        break;
    }

    case ComponentType::SlewLimitModifier:
    {
        // p1: up limit, p2: down limit
        const float upLimit = std::max(1e-4f, p1 * 0.5f);
        const float downLimit = std::max(1e-4f, p2 * 0.5f);

        for (size_t i = 1; i < kWaveformSize; ++i)
        {
            const float delta = frame[i] - frame[i - 1];
            if (delta > upLimit)
                frame[i] = frame[i - 1] + upLimit;
            else if (delta < -downLimit)
                frame[i] = frame[i - 1] - downLimit;
        }
        break;
    }

    case ComponentType::WaveFoldModifier:
    {
        // p1: boost
        for (size_t i = 0; i < kWaveformSize; ++i)
            frame[i] = foldSample(frame[i], p1);
        break;
    }

    case ComponentType::WaveWarpModifier:
    {
        // p1: horizontal power, p2: vertical power
        const double hPow = (p1 >= 0.0f) ? (1.0 + p1 * 4.0) : (1.0 / (1.0 - p1 * 4.0));
        const double vPow = (p2 >= 0.0f) ? (1.0 + p2 * 4.0) : (1.0 / (1.0 - p2 * 4.0));

        Frame warped;
        for (size_t i = 0; i < kWaveformSize; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(kWaveformSize);
            const double wt = std::pow(t, hPow);
            const size_t idx0 = static_cast<size_t>(wt * static_cast<double>(kWaveformSize)) % kWaveformSize;
            const size_t idx1 = (idx0 + 1) % kWaveformSize;
            const float frac = static_cast<float>(wt * static_cast<double>(kWaveformSize) - static_cast<double>(idx0));

            float s = (1.0f - frac) * frame[idx0] + frac * frame[idx1];
            const float sign = (s >= 0.0f) ? 1.0f : -1.0f;
            warped[i] = sign * static_cast<float>(std::pow(std::fabs(s), vPow));
        }
        frame = warped;
        break;
    }

    default:
        break;
    }
}

Group::Group()
{
    // Default group with a WaveSource containing a saw keyframe
    auto waveSource = std::make_unique<Component>(ComponentType::WaveSource, "Wave Source");
    Keyframe kf(0.0f);
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(kWaveformSize);
        kf.waveform[i] = static_cast<float>(1.0 - 2.0 * t);
    }
    waveSource->insertKeyframe(kf);
    components_.push_back(std::move(waveSource));
}

void Group::addComponent(std::unique_ptr<Component> component)
{
    if (component)
        components_.push_back(std::move(component));
}

void Group::removeComponent(size_t index)
{
    if (index < components_.size())
        components_.erase(components_.begin() + static_cast<std::ptrdiff_t>(index));
}

void Group::renderFrame(float position, Frame& outFrame) const noexcept
{
    outFrame.data.fill(0.0f);
    for (const auto& comp : components_)
    {
        comp->process(position, outFrame);
    }
}

void Group::renderWavetable(Wavetable& outWt, size_t numFrames) const
{
    if (numFrames == 0)
        numFrames = 1;
    if (numFrames > kMaxFrames)
        numFrames = kMaxFrames;

    outWt.frames().clear();
    outWt.reserve(numFrames);

    for (size_t f = 0; f < numFrames; ++f)
    {
        const float pos = (numFrames > 1) ? (static_cast<float>(f) / static_cast<float>(numFrames - 1) * 255.0f) : 0.0f;
        Frame frame;
        renderFrame(pos, frame);
        outWt.frames().push_back(frame);
    }
}

void Group::initFromWavetable(const Wavetable& wt)
{
    components_.clear();
    auto waveSource = std::make_unique<Component>(ComponentType::WaveSource, "Wave Source");

    const size_t count = wt.numFrames();
    for (size_t f = 0; f < count; ++f)
    {
        const float pos = (count > 1) ? (static_cast<float>(f) / static_cast<float>(count - 1) * 255.0f) : 0.0f;
        Keyframe kf(pos);
        kf.waveform = wt.getFrame(f);
        waveSource->insertKeyframe(kf);
    }

    components_.push_back(std::move(waveSource));
}

} // namespace synthyboy::wtedit::vital
