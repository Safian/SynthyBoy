// SynthyBoy - core/sample-osc: non-RT sample buffer container, mipmap generator, and UI overview.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/sampleosc/sample_data.h"

#include <algorithm>
#include <cmath>

namespace synthyboy
{
namespace sampleosc
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double besselI0 (double x) noexcept
{
    double sum = 1.0;
    double term = 1.0;
    const double halfx = 0.5 * x;
    for (int k = 1; k < 25; ++k)
    {
        term *= (halfx / static_cast<double> (k)) * (halfx / static_cast<double> (k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}
} // namespace

SampleData::SampleData (SampleData&& other) noexcept
{
    *this = std::move (other);
}

SampleData& SampleData::operator= (SampleData&& other) noexcept
{
    if (this == &other)
        return *this;

    numChannels_ = other.numChannels_;
    numFrames_ = other.numFrames_;
    sampleRate_ = other.sampleRate_;
    rootNote_ = other.rootNote_;
    fineTuneCents_ = other.fineTuneCents_;
    loopStart_ = other.loopStart_;
    loopEnd_ = other.loopEnd_;
    loopType_ = other.loopType_;
    hasSmplLoop_ = other.hasSmplLoop_;
    numMipLevels_ = other.numMipLevels_;

    for (int m = 0; m < kMaxMipLevels; ++m)
    {
        leftData_[m] = std::move (other.leftData_[m]);
        rightData_[m] = std::move (other.rightData_[m]);
    }

    other.clear();
    updateViews();
    return *this;
}

void SampleData::clear() noexcept
{
    numChannels_ = 0;
    numFrames_ = 0;
    sampleRate_ = 44100.0;
    rootNote_ = 60;
    fineTuneCents_ = 0.0f;
    loopStart_ = -1;
    loopEnd_ = -1;
    loopType_ = 0;
    hasSmplLoop_ = false;
    numMipLevels_ = 0;

    for (int m = 0; m < kMaxMipLevels; ++m)
    {
        leftData_[m].clear();
        rightData_[m].clear();
        mipLevels_[m] = MipLevel {};
    }
    channelPtrs_[0] = nullptr;
    channelPtrs_[1] = nullptr;
}

bool SampleData::loadFromMemory (const void* data, size_t size, const char* filename)
{
    clear();
    WavInfo info;
    std::vector<float> left, right;
    if (! readWav (data, size, left, right, info, filename))
        return false;

    numChannels_ = info.numChannels;
    numFrames_ = info.numFrames;
    sampleRate_ = info.sampleRate;
    rootNote_ = info.rootNote;
    fineTuneCents_ = info.fineTuneCents;
    loopStart_ = info.loopStart;
    loopEnd_ = info.loopEnd;
    loopType_ = info.loopType;
    hasSmplLoop_ = info.hasSmplLoop;

    leftData_[0] = std::move (left);
    rightData_[0] = std::move (right);

    buildMipLevels();
    updateViews();
    return true;
}

bool SampleData::loadFromFile (const char* filePath)
{
    clear();
    WavInfo info;
    std::vector<float> left, right;
    if (! readWavFile (filePath, left, right, info))
        return false;

    numChannels_ = info.numChannels;
    numFrames_ = info.numFrames;
    sampleRate_ = info.sampleRate;
    rootNote_ = info.rootNote;
    fineTuneCents_ = info.fineTuneCents;
    loopStart_ = info.loopStart;
    loopEnd_ = info.loopEnd;
    loopType_ = info.loopType;
    hasSmplLoop_ = info.hasSmplLoop;

    leftData_[0] = std::move (left);
    rightData_[0] = std::move (right);

    buildMipLevels();
    updateViews();
    return true;
}

bool SampleData::loadFromFrames (const float* left, const float* right, int64_t numFrames, double sampleRate,
                                int rootNote, float fineTuneCents)
{
    clear();
    if (left == nullptr || numFrames <= 0 || sampleRate <= 0.0)
        return false;

    numChannels_ = (right != nullptr) ? 2 : 1;
    numFrames_ = numFrames;
    sampleRate_ = sampleRate;
    rootNote_ = rootNote;
    fineTuneCents_ = fineTuneCents;

    leftData_[0].resize (static_cast<size_t> (numFrames));
    rightData_[0].resize (static_cast<size_t> (numFrames));

    for (int64_t i = 0; i < numFrames; ++i)
    {
        leftData_[0][static_cast<size_t> (i)] = sanitizeFloat (left[i]);
        rightData_[0][static_cast<size_t> (i)] = (right != nullptr) ? sanitizeFloat (right[i])
                                                                    : leftData_[0][static_cast<size_t> (i)];
    }

    buildMipLevels();
    updateViews();
    return true;
}

std::vector<SampleData::Onset> SampleData::detectOnsets (int maxOnsets) const
{
    std::vector<Onset> out;
    if (leftData_[0].empty() || numFrames_ < 2 || maxOnsets < 1)
        return out;

    const int64_t hop = std::max<int64_t> (1, static_cast<int64_t> (0.002 * sampleRate_));
    const int64_t blocks = numFrames_ / hop;
    if (blocks < 4)
        return out;
    std::vector<float> env (static_cast<size_t> (blocks), 0.0f);
    for (int64_t b = 0; b < blocks; ++b)
    {
        double sum = 0.0;
        for (int64_t i = b * hop; i < (b + 1) * hop; ++i)
        {
            const double l = leftData_[0][static_cast<size_t> (i)];
            const double r = (numChannels_ == 2 && ! rightData_[0].empty()) ? rightData_[0][static_cast<size_t> (i)] : l;
            sum += 0.5 * (l * l + r * r);
        }
        env[static_cast<size_t> (b)] = static_cast<float> (std::sqrt (sum / static_cast<double> (hop)));
    }
    // rise of the envelope against the mean of the previous 10 blocks (20 ms)
    std::vector<float> rise (static_cast<size_t> (blocks), 0.0f);
    float maxRise = 0.0f;
    for (int64_t b = 1; b < blocks; ++b)
    {
        double mean = 0.0;
        const int64_t from = std::max<int64_t> (0, b - 10);
        for (int64_t k = from; k < b; ++k)
            mean += env[static_cast<size_t> (k)];
        mean /= static_cast<double> (b - from);
        rise[static_cast<size_t> (b)] = std::max (0.0f, env[static_cast<size_t> (b)] - static_cast<float> (mean));
        maxRise = std::max (maxRise, rise[static_cast<size_t> (b)]);
    }
    if (maxRise <= 1.0e-6f)
        return out;
    const int64_t minDistance = 10; // blocks (20 ms)
    int64_t last = 0; // the file start is the first slice anyway: no onset within the minimum distance of it
    for (int64_t b = 1; b < blocks && static_cast<int> (out.size()) < maxOnsets; ++b)
    {
        const float r = rise[static_cast<size_t> (b)];
        if (r >= 0.05f * maxRise && r >= rise[static_cast<size_t> (b - 1)] && (b + 1 >= blocks || r > rise[static_cast<size_t> (b + 1)])
            && b - last >= minDistance)
        {
            out.push_back ({ static_cast<float> (static_cast<double> (b * hop) / static_cast<double> (numFrames_)), r / maxRise });
            last = b;
        }
    }
    return out;
}

std::vector<float> SampleData::detectSlices (float threshold, int maxSlices) const
{
    std::vector<float> out { 0.0f };
    const float limit = 1.0f - std::max (0.0f, std::min (1.0f, threshold));
    for (const Onset& o : detectOnsets (256))
        if (o.strength >= limit && static_cast<int> (out.size()) < maxSlices)
            out.push_back (o.position);
    return out;
}

void SampleData::buildMipLevels()
{
    numMipLevels_ = 1;
    if (leftData_[0].empty())
        return;

    float peak = 0.0f;
    for (float v : leftData_[0])
        peak = std::max (peak, std::fabs (v));
    for (float v : rightData_[0])
        peak = std::max (peak, std::fabs (v));
    normalizeGain_ = peak > 1.0e-9f ? 1.0f / peak : 1.0f;

    // Design a 101-tap Kaiser low-pass filter with cutoff at 0.42 * pi and beta = 8.0
    constexpr int kTaps = 101;
    constexpr int kM = (kTaps - 1) / 2;
    constexpr double kBeta = 8.0;
    const double i0Beta = besselI0 (kBeta);

    double h[kTaps];
    double sumH = 0.0;
    for (int k = 0; k < kTaps; ++k)
    {
        const double d = static_cast<double> (k - kM);
        const double sinc = (d == 0.0) ? 0.42 : std::sin (0.42 * kPi * d) / (kPi * d);
        const double arg = 1.0 - (d / static_cast<double> (kM)) * (d / static_cast<double> (kM));
        const double win = (arg >= 0.0) ? besselI0 (kBeta * std::sqrt (arg)) / i0Beta : 0.0;
        h[k] = sinc * win;
        sumH += h[k];
    }
    for (int k = 0; k < kTaps; ++k)
        h[k] /= sumH;

    for (int m = 1; m < kMaxMipLevels; ++m)
    {
        const auto& parentL = leftData_[m - 1];
        const auto& parentR = rightData_[m - 1];
        const int64_t pFrames = static_cast<int64_t> (parentL.size());
        if (pFrames < 32)
            break;

        const int64_t outFrames = pFrames / 2;
        auto& outL = leftData_[m];
        auto& outR = rightData_[m];
        outL.resize (static_cast<size_t> (outFrames));
        outR.resize (static_cast<size_t> (outFrames));

        for (int64_t j = 0; j < outFrames; ++j)
        {
            const int64_t c = 2 * j;
            double sumL = 0.0;
            double sumR = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
                const int64_t idx = std::max<int64_t> (0, std::min<int64_t> (pFrames - 1, c + k - kM));
                sumL += h[k] * static_cast<double> (parentL[static_cast<size_t> (idx)]);
                sumR += h[k] * static_cast<double> (parentR[static_cast<size_t> (idx)]);
            }
            outL[static_cast<size_t> (j)] = sanitizeFloat (static_cast<float> (sumL));
            outR[static_cast<size_t> (j)] = sanitizeFloat (static_cast<float> (sumR));
        }

        ++numMipLevels_;
    }
}

void SampleData::updateViews() noexcept
{
    if (numMipLevels_ > 0 && ! leftData_[0].empty())
    {
        channelPtrs_[0] = leftData_[0].data();
        channelPtrs_[1] = (numChannels_ == 2) ? rightData_[0].data() : leftData_[0].data();
    }
    else
    {
        channelPtrs_[0] = nullptr;
        channelPtrs_[1] = nullptr;
    }

    for (int m = 0; m < numMipLevels_; ++m)
    {
        mipLevels_[m].channels[0] = leftData_[m].data();
        mipLevels_[m].channels[1] = (numChannels_ == 2) ? rightData_[m].data() : leftData_[m].data();
        mipLevels_[m].numFrames = static_cast<int64_t> (leftData_[m].size());
    }
    for (int m = numMipLevels_; m < kMaxMipLevels; ++m)
    {
        mipLevels_[m] = MipLevel {};
    }
}

SampleView SampleData::view() const noexcept
{
    SampleView v;
    if (numFrames_ <= 0 || channelPtrs_[0] == nullptr)
        return v;

    v.channelData = channelPtrs_;
    v.numChannels = numChannels_;
    v.numFrames = numFrames_;
    v.sampleRate = sampleRate_;
    v.rootNote = rootNote_;
    v.fineTuneCents = fineTuneCents_;
    v.numMipLevels = numMipLevels_;
    v.mipLevels = mipLevels_;
    return v;
}

std::vector<OverviewPoint> SampleData::computeOverview (int numPixels, int channel) const
{
    std::vector<OverviewPoint> res;
    if (numPixels <= 0 || numFrames_ <= 0 || channelPtrs_[0] == nullptr)
        return res;

    res.resize (static_cast<size_t> (numPixels));
    const int ch = (channel < numChannels_) ? channel : 0;
    const auto& buf = (ch == 1) ? rightData_[0] : leftData_[0];

    for (int px = 0; px < numPixels; ++px)
    {
        const int64_t f0 = (static_cast<int64_t> (px) * numFrames_) / numPixels;
        int64_t f1 = (static_cast<int64_t> (px + 1) * numFrames_) / numPixels;
        if (f1 <= f0)
            f1 = f0 + 1;
        f1 = std::min (f1, numFrames_);

        float minV = buf[static_cast<size_t> (f0)];
        float maxV = minV;
        for (int64_t i = f0 + 1; i < f1; ++i)
        {
            const float s = buf[static_cast<size_t> (i)];
            if (s < minV) minV = s;
            if (s > maxV) maxV = s;
        }

        res[static_cast<size_t> (px)] = OverviewPoint { minV, maxV };
    }

    return res;
}

void SampleData::computeOverviewStereo (int numPixels, std::vector<OverviewPoint>& outLeft,
                                       std::vector<OverviewPoint>& outRight) const
{
    outLeft = computeOverview (numPixels, 0);
    outRight = computeOverview (numPixels, (numChannels_ == 2) ? 1 : 0);
}

} // namespace sampleosc
} // namespace synthyboy
