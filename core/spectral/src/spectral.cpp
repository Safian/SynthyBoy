// SynthyBoy - core/spectral: the spectral oscillator DSP.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/spectral/spectral.h"
#include "synthyboy/spectral/kernels.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// The reference loops here and the block kernels of kernels.cpp must compute the same bits where they do the same thing: no fused multiply-add in either, on any compiler / CPU.
#if defined(__clang__)
 #pragma clang fp contract(off)
#elif defined(__GNUC__)
 #pragma GCC optimize ("fp-contract=off")
#endif

namespace synthyboy
{
namespace spectral
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kOutputScale = 1.0f / 1.5f; // the sum of the squared Hann windows at a quarter overlap

float hannAt (int n) noexcept
{
    static const struct Table
    {
        float w[kFftSize];
        Table()
        {
            for (int i = 0; i < kFftSize; ++i)
                w[i] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * kPi * i / kFftSize));
        }
    } table;
    return table.w[n];
}

float wrapPi (float x) noexcept
{
    while (x > 3.14159265f) x -= kTwoPi;
    while (x < -3.14159265f) x += kTwoPi;
    return x;
}

float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : (v == v ? v : 0.0f)); }
} // namespace

// ---- FFT ---------------------------------------------------------------------------------------------------------------------------

Fft::Fft()
{
    for (int i = 0; i < kFftSize / 2; ++i)
    {
        cosTable_[i] = static_cast<float> (std::cos (2.0 * kPi * i / kFftSize));
        sinTable_[i] = static_cast<float> (-std::sin (2.0 * kPi * i / kFftSize));
    }
    int bits = 0;
    while ((1 << bits) < kFftSize)
        ++bits;
    for (int i = 0; i < kFftSize; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            r |= ((i >> b) & 1) << (bits - 1 - b);
        bitReverse_[i] = static_cast<uint16_t> (r);
    }
}

const Fft& Fft::instance() noexcept
{
    static const Fft fft;
    return fft;
}

void Fft::run (float* re, float* im, bool inverse) const noexcept
{
    for (int i = 0; i < kFftSize; ++i)
    {
        const int j = bitReverse_[i];
        if (j > i)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    for (int len = 2; len <= kFftSize; len <<= 1)
    {
        const int half = len >> 1, step = kFftSize / len;
        for (int start = 0; start < kFftSize; start += len)
            for (int k = 0; k < half; ++k)
            {
                const float wr = cosTable_[k * step], wi = inverse ? -sinTable_[k * step] : sinTable_[k * step];
                const int a = start + k, b = a + half;
                const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
    }
    if (inverse)
        for (int i = 0; i < kFftSize; ++i)
        {
            re[i] *= 1.0f / kFftSize;
            im[i] *= 1.0f / kFftSize;
        }
}

void Fft::forward (float* re, float* im) const noexcept { run (re, im, false); }
void Fft::inverse (float* re, float* im) const noexcept { run (re, im, true); }

// ---- analysis ----------------------------------------------------------------------------------------------------------------------

bool Analysis::build (const SampleView& sample)
{
    data_.clear();
    transient_.clear();
    frames_ = 0;
    if (! sample.isValid())
        return false;
    channels_ = std::min (2, sample.numChannels);
    fileFrames_ = sample.numFrames;
    fileRate_ = sample.sampleRate;
    int64_t frames = sample.numFrames / kHop + 1;
    truncated_ = frames > kMaxAnalysisFrames;
    frames = std::min<int64_t> (frames, kMaxAnalysisFrames);
    frames_ = static_cast<int> (frames);
    data_.assign (static_cast<size_t> (channels_) * static_cast<size_t> (frames_) * 3u * kBins, 0.0f);

    const Fft& fft = Fft::instance();
    std::vector<float> re (kFftSize), im (kFftSize);
    for (int ch = 0; ch < channels_; ++ch)
    {
        const float* source = sample.channelData[ch];
        for (int t = 0; t < frames_; ++t)
        {
            const int64_t start = static_cast<int64_t> (t) * kHop - kFftSize / 2;
            for (int n = 0; n < kFftSize; ++n)
            {
                const int64_t at = start + n;
                re[static_cast<size_t> (n)] = at >= 0 && at < sample.numFrames ? source[at] * hannAt (n) : 0.0f;
                im[static_cast<size_t> (n)] = 0.0f;
            }
            fft.forward (re.data(), im.data());
            float* m = data_.data() + offset (ch, t);
            float* ph = m + kBins;
            float* fq = m + 2 * kBins;
            const float* prev = t > 0 ? data_.data() + offset (ch, t - 1) + kBins : nullptr;
            for (int k = 0; k < kBins; ++k)
            {
                m[k] = std::sqrt (re[static_cast<size_t> (k)] * re[static_cast<size_t> (k)] + im[static_cast<size_t> (k)] * im[static_cast<size_t> (k)]);
                ph[k] = std::atan2 (im[static_cast<size_t> (k)], re[static_cast<size_t> (k)]);
                const float nominal = kTwoPi * static_cast<float> (k) / kFftSize;
                if (prev != nullptr)
                {
                    const float expected = nominal * kHop;
                    fq[k] = nominal + wrapPi (ph[k] - prev[k] - expected) / kHop;
                }
                else
                    fq[k] = nominal;
            }
        }
    }

    // transient frames: the relative spectral flux stands out of the others
    transient_.assign (static_cast<size_t> (frames_), 0);
    std::vector<float> flux (static_cast<size_t> (frames_), 0.0f);
    double mean = 0.0, meanSquare = 0.0;
    for (int t = 1; t < frames_; ++t)
    {
        const float* a = magnitude (0, t - 1);
        const float* b = magnitude (0, t);
        double up = 0.0, total = 1.0e-9;
        for (int k = 0; k < kBins; ++k)
        {
            up += std::max (0.0f, b[k] - a[k]);
            total += b[k];
        }
        const float r = static_cast<float> (up / total);
        flux[static_cast<size_t> (t)] = r;
        mean += r;
        meanSquare += static_cast<double> (r) * r;
    }
    if (frames_ > 2)
    {
        mean /= frames_ - 1;
        const double deviation = std::sqrt (std::max (0.0, meanSquare / (frames_ - 1) - mean * mean));
        for (int t = 1; t < frames_; ++t)
            if (flux[static_cast<size_t> (t)] > mean + 2.0 * deviation && flux[static_cast<size_t> (t)] > 0.35f)
                transient_[static_cast<size_t> (t)] = 1;
    }
    return true;
}

std::vector<float> Analysis::frameEnergies() const
{
    std::vector<float> out (static_cast<size_t> (frames_), 0.0f);
    float peak = 1.0e-9f;
    for (int t = 0; t < frames_; ++t)
    {
        const float* m = magnitude (0, t);
        float e = 0.0f;
        for (int k = 0; k < kBins; ++k)
            e += m[k];
        out[static_cast<size_t> (t)] = e;
        peak = std::max (peak, e);
    }
    for (float& e : out)
        e /= peak;
    return out;
}

// ---- spectral filter ---------------------------------------------------------------------------------------------------------------

void buildFilterGains (float* gains, const float* x, const float* y, int numPoints, float shift, float mix, double binHz) noexcept
{
    mix = clamp01 (mix);
    if (numPoints < 1 || x == nullptr || y == nullptr)
    {
        for (int k = 0; k < kBins; ++k)
            gains[k] = 1.0f;
        return;
    }
    const float offset = std::max (-1.0f, std::min (1.0f, shift)) * 0.5f;
    int segment = 0;
    for (int k = 0; k < kBins; ++k)
    {
        const double f = std::max (20.0, static_cast<double> (k) * binHz);
        const float pos = std::min (1.0f, std::max (0.0f, static_cast<float> (std::log (f / 20.0) / std::log (1000.0)))) - offset;
        float value;
        if (pos <= x[0])
            value = y[0];
        else if (pos >= x[numPoints - 1])
            value = y[numPoints - 1];
        else
        {
            segment = 0;
            while (segment + 1 < numPoints - 1 && pos > x[segment + 1])
                ++segment;
            const float span = x[segment + 1] - x[segment];
            const float t = span > 1.0e-9f ? (pos - x[segment]) / span : 0.0f;
            value = y[segment] + (y[segment + 1] - y[segment]) * t;
        }
        const float gain = std::max(0.0f, 2.0f * value);
        gains[k] = (1.0f - mix) + mix * gain;
    }
}

// ---- spectral warps ----------------------------------------------------------------------------------------------------------------

namespace
{
/** One spectral warp on the magnitudes m and the frequencies f (radians per file sample) of a frame. Returns an extra pitch ratio (1 = none). `twist` receives the phase twist. */
float applyWarp (int w, float a, float v, float* m, float* f, float* tm, float* tf, const float* external, float* smear, float& twist, Scratch* cache) noexcept
{
    a = clamp01 (a);
    v = clamp01 (v);
    const auto copyOut = [&]
    {
        std::memcpy (tm, m, sizeof (float) * kBins);
        std::memcpy (tf, f, sizeof (float) * kBins);
    };
    const auto copyBack = [&]
    {
        std::memcpy (m, tm, sizeof (float) * kBins);
        std::memcpy (f, tf, sizeof (float) * kBins);
    };
    const auto peaksOf = [&] (int* peaks, int& count)
    {
        float mx = 0.0f;
        for (int k = 0; k < kBins; ++k)
            mx = std::max (mx, m[k]);
        count = 0;
        for (int k = 2; k < kBins - 1 && count < 256; ++k)
            if (m[k] > m[k - 1] && m[k] >= m[k + 1] && m[k] > 0.02f * mx)
                peaks[count++] = k;
    };
    switch (static_cast<Warp> (w))
    {
        case Warp::comb:
        {
            const float period = 2.0f + (1.0f - v) * 60.0f;
            if (cache != nullptr)
            {
                // the gain of every bin depends on the amount and the variation only: made when they change, not for every frame of every voice
                if (cache->combA != a || cache->combV != v)
                {
                    for (int k = 0; k < kBins; ++k)
                        cache->combGain[k] = (1.0f - a) + a * (0.5f + 0.5f * std::cos (kTwoPi * static_cast<float> (k) / period));
                    cache->combA = a;
                    cache->combV = v;
                }
                kernels::mulBins (m, cache->combGain, kBins, kernels::haveNeon());
                break;
            }
            for (int k = 0; k < kBins; ++k)
                m[k] *= (1.0f - a) + a * (0.5f + 0.5f * std::cos (kTwoPi * static_cast<float> (k) / period));
            break;
        }
        case Warp::addHarmonics:
        {
            const int n = 1 + static_cast<int> (v * 3.99f);
            copyOut();
            for (int k = 1; k < kBins; ++k)
                for (int h = 2; h <= n + 1 && k * h < kBins; ++h)
                {
                    const float value = a * m[k] / static_cast<float> (h);
                    if (value > tm[k * h])
                    {
                        tm[k * h] = value;
                        tf[k * h] = f[k] * static_cast<float> (h);
                    }
                }
            copyBack();
            break;
        }
        case Warp::addSubharmonics:
        {
            const int n = 1 + static_cast<int> (v * 3.99f);
            copyOut();
            if (cache != nullptr)
            {
                // the multiples of each target in the order of the loop below (k = t h rises with h), without a division or a remainder per bin
                for (int h = 2; h <= n + 1; ++h)
                    for (int t = 1; t * h < kBins; ++t)
                    {
                        const int k = t * h;
                        const float value = a * m[k] * 0.7f;
                        if (value > tm[t])
                        {
                            tm[t] = value;
                            tf[t] = f[k] / static_cast<float> (h);
                        }
                    }
                copyBack();
                break;
            }
            for (int k = 2; k < kBins; ++k)
                for (int h = 2; h <= n + 1; ++h)
                    if (k % h == 0)
                    {
                        const int t = k / h;
                        const float value = a * m[k] * 0.7f;
                        if (value > tm[t])
                        {
                            tm[t] = value;
                            tf[t] = f[k] / static_cast<float> (h);
                        }
                    }
            copyBack();
            break;
        }
        case Warp::detune:
        {
            for (int k = 1; k < kBins; ++k)
            {
                float s = std::sin (static_cast<float> (k) * 12.9898f) * 43758.5453f;
                s -= std::floor (s);
                f[k] *= std::exp2 (a * (0.3f + 0.7f * v) * (2.0f * s - 1.0f) * 50.0f / 1200.0f);
            }
            break;
        }
        case Warp::shepardNarrow:
        case Warp::shepardFilter:
        {
            const float centre = 2.0f + 8.0f * v;
            const float sigma = static_cast<Warp> (w) == Warp::shepardNarrow ? 0.12f : 0.3f;
            for (int k = 1; k < kBins; ++k)
            {
                const float l = std::log2 (static_cast<float> (k)) - centre;
                const float d = l - std::round (l);
                m[k] *= (1.0f - a) + a * std::exp (-d * d / (2.0f * sigma * sigma));
            }
            break;
        }
        case Warp::peakOctaveDown:
        case Warp::peakOctaveUp:
        case Warp::peakHarmonicDown:
        case Warp::peakHarmonicUp:
        {
            int peaks[256], count;
            peaksOf (peaks, count);
            copyOut();
            const Warp kind = static_cast<Warp> (w);
            const bool down = kind == Warp::peakOctaveDown || kind == Warp::peakHarmonicDown;
            const float factor = kind == Warp::peakOctaveDown ? 0.5f : kind == Warp::peakOctaveUp ? 2.0f : kind == Warp::peakHarmonicDown ? 2.0f / 3.0f : 1.5f;
            for (int i = 0; i < count; ++i)
            {
                const int p = peaks[i];
                const int t = static_cast<int> (std::lround (static_cast<float> (p) * factor));
                if (t < 1 || t >= kBins)
                    continue;
                const float value = a * m[p];
                if (value > tm[t])
                {
                    tm[t] = value;
                    tf[t] = f[p] * factor;
                }
                tm[p] = std::min (tm[p], m[p] * (1.0f - a * (down ? 0.8f : 0.8f)));
            }
            copyBack();
            break;
        }
        case Warp::gate:
        {
            float mx = 0.0f;
            for (int k = 0; k < kBins; ++k)
                mx = std::max (mx, m[k]);
            const float threshold = a * mx * 0.3f;
            for (int k = 0; k < kBins; ++k)
                if (m[k] < threshold)
                    m[k] *= (1.0f - a) * (1.0f - a);
            break;
        }
        case Warp::pitchShift:
            return std::exp2 ((a - 0.5f) * 24.0f / 12.0f);
        case Warp::phaseTwist:
            twist = a * (0.5f + 4.0f * v);
            break;
        case Warp::spread:
        {
            const int width = 1 + static_cast<int> (a * (2.0f + v * 14.0f));
            if (width <= 1)
                break;
            copyOut();
            float sum = 0.0f;
            const int half = width / 2;
            for (int k = 0; k < kBins; ++k)
            {
                sum = 0.0f;
                int n = 0;
                for (int j = std::max (0, k - half); j <= std::min (kBins - 1, k + half); ++j, ++n)
                    sum += m[j];
                tm[k] = sum / static_cast<float> (std::max (1, n));
            }
            std::memcpy (m, tm, sizeof (float) * kBins);
            break;
        }
        case Warp::spectralShift:
        {
            const int delta = static_cast<int> (std::lround ((a - 0.5f) * 2.0f * (10.0f + v * 190.0f)));
            std::memset (tm, 0, sizeof (float) * kBins);
            std::memset (tf, 0, sizeof (float) * kBins);
            for (int k = 1; k < kBins; ++k)
            {
                const int t = k + delta;
                if (t >= 1 && t < kBins)
                {
                    tm[t] = m[k];
                    tf[t] = f[k] + kTwoPi * static_cast<float> (delta) / kFftSize;
                }
            }
            copyBack();
            break;
        }
        case Warp::smear:
        {
            const float coefficient = a * 0.9f;
            for (int k = 0; k < kBins; ++k)
            {
                smear[k] = smear[k] * coefficient + m[k] * (1.0f - coefficient);
                m[k] = smear[k];
            }
            break;
        }
        case Warp::mirror:
        {
            copyOut();
            for (int k = 1; k < kBins; ++k)
            {
                const float value = a * m[kBins - 1 - k];
                if (value > m[k])
                {
                    tm[k] = value;
                    tf[k] = kTwoPi * static_cast<float> (k) / kFftSize;
                }
            }
            copyBack();
            break;
        }
        case Warp::maskFirst:
        case Warp::maskSecond:
        case Warp::maskNoise:
        {
            if (external == nullptr)
                break;
            float mx = 1.0e-9f;
            for (int k = 0; k < kBins; ++k)
                mx = std::max (mx, external[k]);
            for (int k = 0; k < kBins; ++k)
                m[k] *= (1.0f - a) + a * std::min (1.0f, external[k] / mx * 6.0f);
            break;
        }
        case Warp::vocodeFirst:
        case Warp::vocodeSecond:
        case Warp::vocodeNoise:
        {
            if (external == nullptr)
                break;
            float sumM = 1.0e-9f, sumE = 1.0e-9f;
            for (int k = 0; k < kBins; ++k)
            {
                sumM += m[k];
                sumE += external[k];
            }
            const float level = sumM / sumE;
            for (int k = 0; k < kBins; ++k)
            {
                float envM = 1.0e-9f, envE = 0.0f;
                int n = 0;
                for (int j = std::max (0, k - 4); j <= std::min (kBins - 1, k + 4); ++j, ++n)
                {
                    envM += m[j];
                    envE += external[j] * level;
                }
                const float ratio = std::min (10.0f, envE / envM);
                m[k] *= (1.0f - a) + a * ratio;
            }
            break;
        }
    }
    return 1.0f;
}
} // namespace

// ---- the voice ---------------------------------------------------------------------------------------------------------------------

void SpectralVoice::prepare (double hostSampleRate) noexcept
{
    Fft::instance();
    hannAt (0);
    hostRate_ = hostSampleRate > 1000.0 ? hostSampleRate : 48000.0;
}

void SpectralVoice::reset() noexcept
{
    active_ = false;
    analysis_ = nullptr;
    for (Channel& c : channel_)
    {
        std::fill (c.synthPhase, c.synthPhase + kBins, 0.0f);
        std::fill (c.smear, c.smear + kBins, 0.0f);
        std::fill (c.ring, c.ring + kFftSize, 0.0f);
        std::fill (c.finished, c.finished + kHop, 0.0f);
    }
}

void SpectralVoice::noteOn (const Analysis* analysis, const SpectralParams& p, float startFraction, int hopOffset) noexcept
{
    active_ = false;
    analysis_ = nullptr;
    if (analysis == nullptr || ! analysis->valid() || p.scratch == nullptr)
        return;
    // what a note needs cleared: the smear memory and the ring of the channels it uses (the phases are set by the first frame, `finished` is written before it is read)
    for (int ch = 0; ch < (analysis->channels() > 1 ? 2 : 1); ++ch)
    {
        std::fill (channel_[ch].smear, channel_[ch].smear + kBins, 0.0f);
        std::fill (channel_[ch].ring, channel_[ch].ring + kFftSize, 0.0f);
        std::fill (channel_[ch].synthPhase, channel_[ch].synthPhase + kBins, 0.0f);
    }
    analysis_ = analysis;
    hostRate_ = p.hostRate > 1000.0 ? p.hostRate : hostRate_;
    active_ = true;
    released_ = false;
    headDone_ = false;
    headDirection_ = 1;
    outPos_ = kHop;
    ola_ = 0;
    primed_ = false;
    framePrimed_ = false;
    startSkip_ = std::max (0, std::min (kHop - 1, hopOffset));
    lastTransientFrame_ = -1;
    drain_ = 0;
    phaseInit_[0] = phaseInit_[1] = false;
    const float lo = std::min (p.regionLo, p.regionHi), hi = std::max (p.regionLo, p.regionHi);
    const bool backwards = p.scanSpeed < 0.0f || p.loopMode == LoopMode::reverse;
    head_ = backwards ? hi : lo;
    if (p.loopMode == LoopMode::reverse)
        head_ = std::max (lo, std::min (hi, std::max (p.loopLo, p.loopHi)));
    head_ += static_cast<double> (clamp01 (startFraction)) * (hi - lo) * (backwards ? -1.0 : 1.0);
    head_ = std::max (static_cast<double> (lo), std::min (static_cast<double> (hi), head_));
    if (p.manual)
        head_ = lo + (hi - lo) * clamp01 (p.position);
    startActive_ = false;
    startMask_ = 0;
    startJoin_[0] = startJoin_[1] = startJoin_[2] = -1;
    phaseFrame_[0] = phaseFrame_[1] = 0;
    if (legacyStart_)
    {
        // prime: three frames at the start position, so that the first output hop is the sum of four frames (the fourth is the frame of the hop itself)
        if (reference_)
            for (int i = 0; i < 3; ++i)
                synthesizeHop (p, false);
        else
            primeFast (p);
        primed_ = true;
        outPos_ = kHop;
        return;
    }
    // the start of docs/engine-spec/07 section 4a: frames 1 .. 3 of the priming (frame 0 is never made); the first hop is their sum divided by the squared windows of the frames that exist
    {
        const double fileFrames = static_cast<double> (analysis_->fileFrames());
        startFrame_ = head_ * fileFrames / kHop;
        const int index = static_cast<int> (std::lround (startFrame_));
        lastTransientFrame_ = index;
        bool smears = false;
        for (int w = 0; w < 2; ++w)
            smears = smears || (p.warpType[w] == static_cast<int> (Warp::smear) && p.warpAmount[w] > 0.0f);
        startSchedule (startSkip_, smears, startJoin_);
        int strike = 0;
        for (int j = 0; j < 3; ++j)
            if (startJoin_[j] <= startSkip_)
            {
                strike |= 1 << j;
                startJoin_[j] = -1;
            }
        if (strike == 0)
        {
            // (never with the schedule: the frame that weighs most at the first sample has a squared window above 0.5)
            int best = 0;
            double bestWeight = -1.0;
            for (int j = 1; j <= 3; ++j)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (startSkip_ + (3 - j) * kHop) / kFftSize);
                if (w > bestWeight)
                {
                    bestWeight = w;
                    best = j;
                }
            }
            strike = 1 << (best - 1);
            startJoin_[best - 1] = -1;
        }
        startFrames (p, strike, smears);
        startMask_ = strike;
        startActive_ = true;
        ola_ = 0;
        outPos_ = startSkip_;
        refreshFirstHop (startSkip_);
        startSkip_ = 0;
        if (! p.manual)
            advanceHead (p);
    }
    primed_ = true;
}

void SpectralVoice::advanceHead (const SpectralParams& p) noexcept
{
    if (headDone_ || analysis_ == nullptr)
        return;
    const double fileSeconds = static_cast<double> (analysis_->fileFrames()) / analysis_->fileRate();
    double step = static_cast<double> (p.scanSpeed) * (static_cast<double> (kHop) / hostRate_) / fileSeconds;
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

/** The first half of a hop of one channel, shared by the voice and by the display (computeMagnitudeSpectrum): the analysis frame `frame` of channel `src` (interpolated between two analysis
    frames), the spectral filter, the band, the warps and the pitch / frequency shift, down to the magnitudes (scratch.om), frequencies (scratch.of) and phases (scratch.oth) of the output bins.
    `smear`: the per channel state of the smear warp (kBins values). `twist`: the phase twist the warps ask for. One code for both, so that the display shows what the voice plays. */
static void shapeFrame (const SpectralParams& p, const Analysis& an, int src, double frame, double hostRate, Scratch& scratch, float* smear, float& twist, bool fast) noexcept
{
    float* const mag_ = scratch.mag; float* const freq_ = scratch.freq; float* const theta_ = scratch.theta;
    float* const om_ = scratch.om; float* const of_ = scratch.of; float* const oth_ = scratch.oth; float* const tm_ = scratch.tm; float* const tf_ = scratch.tf;
    const int last = an.numFrames() - 1;
    const int i0 = std::max (0, std::min (last, static_cast<int> (std::floor (frame))));
    const int i1 = std::min (last, i0 + 1);
    const float fr = static_cast<float> (std::max (0.0, std::min (1.0, frame - std::floor (frame))));
    const int near = std::max (0, std::min (last, static_cast<int> (std::lround (frame))));
    const float* m0 = an.magnitude (src, i0);
    const float* m1 = an.magnitude (src, i1);
    const float* fq = an.frequency (src, near);
    const float* ph = an.phase (src, near);
    const double fileRate = an.fileRate();
    const double fileBinHz = fileRate / kFftSize, hostBinHz = hostRate / kFftSize;

    const bool neon = kernels::haveNeon();
    if (fast)
    {
        kernels::lerpBins (mag_, m0, m1, fr, kBins, neon);
        std::memcpy (freq_, fq, sizeof (float) * kBins);
        std::memcpy (theta_, ph, sizeof (float) * kBins);
    }
    else
        for (int k = 0; k < kBins; ++k)
        {
            mag_[k] = m0[k] + (m1[k] - m0[k]) * fr;
            freq_[k] = fq[k];
            theta_[k] = ph[k];
        }
    const auto band = [&] (float* m, double binHz)
    {
        const float lo = std::max (1.0f, p.freqLoHz), hi = std::max (lo, p.freqHiHz);
        if (fast)
        {
            // the gains depend on the settings and the bin spacing only: computed when they change, not for every frame of every voice
            if (scratch.bandLo != lo || scratch.bandHi != hi || scratch.bandBinHz != binHz || scratch.bandSmooth != p.bandSmooth)
            {
                for (int k = 0; k < kBins; ++k)
                {
                    const float f = static_cast<float> (k * binHz);
                    float g;
                    if (p.bandSmooth)
                    {
                        if (f <= 0.0f)
                            g = 0.0f;
                        else
                        {
                            const float a = lo / f, b = f / hi;
                            const float a2 = a * a, a4 = a2 * a2, b2 = b * b, b4 = b2 * b2;
                            g = 1.0f / std::sqrt ((1.0f + a4 * a4) * (1.0f + b4 * b4));
                        }
                    }
                    else
                        g = f >= lo && f <= hi ? 1.0f : 0.0f;
                    scratch.bandGain[k] = g;
                }
                scratch.bandLo = lo;
                scratch.bandHi = hi;
                scratch.bandBinHz = binHz;
                scratch.bandSmooth = p.bandSmooth;
            }
            kernels::mulBins (m, scratch.bandGain, kBins, neon);
            return;
        }
        for (int k = 0; k < kBins; ++k)
        {
            const float f = static_cast<float> (k * binHz);
            float g;
            if (p.bandSmooth)
            {
                if (f <= 0.0f)
                    g = 0.0f;
                else
                {
                    const float a = lo / f, b = f / hi;
                    const float a2 = a * a, a4 = a2 * a2, b2 = b * b, b4 = b2 * b2;
                    g = 1.0f / std::sqrt ((1.0f + a4 * a4) * (1.0f + b4 * b4));
                }
            }
            else
                g = f >= lo && f <= hi ? 1.0f : 0.0f;
            m[k] *= g;
        }
    };
    if (p.filterGain != nullptr)
    {
        if (fast)
            kernels::mulBins (mag_, p.filterGain, kBins, neon);
        else
            for (int k = 0; k < kBins; ++k)
                mag_[k] *= p.filterGain[k];
    }
    if (! p.bandPostWarp)
        band (mag_, fileBinHz);

    float extra = 1.0f;
    twist = 0.0f;
    for (int w = 0; w < 2; ++w)
        if (p.warpType[w] >= 0 && p.warpType[w] < kNumSpectralWarps && p.warpAmount[w] > 0.0f)
        {
            const float* ext = isExternalWarp (p.warpType[w]) ? p.external[externalSource (p.warpType[w])] : nullptr;
            extra *= applyWarp (p.warpType[w], p.warpAmount[w], p.warpVar[w], mag_, freq_, tm_, tf_, ext, smear, twist, fast ? &scratch : nullptr);
        }

    // the pitch: bin k goes to bin k * r (+ the frequency shift) with its frequency scaled the same way
    const double r = std::max (1.0 / 32.0, std::min (32.0, static_cast<double> (p.pitchRatio) * static_cast<double> (extra) * fileRate / hostRate));
    const double shiftBins = static_cast<double> (p.freqShiftHz) / hostBinHz;
    const float shiftRad = kTwoPi * p.freqShiftHz / static_cast<float> (hostRate);
    std::fill (om_, om_ + kBins, 0.0f);
    std::fill (of_, of_ + kBins, 0.0f);
    std::fill (oth_, oth_ + kBins, 0.0f);
    {
        // the regions around the peaks of the spectrum move as a whole (every bin by the offset of its peak), so that the shape of a partial's main lobe is kept
        // (Laroche / Dolson); the frequencies of the bins are scaled by the same ratio
        int peaks[kBins / 2];
        int numPeaks = 0;
        float top = 0.0f;
        if (fast)
        {
            top = kernels::maxBins (mag_, kBins, neon);
            numPeaks = kernels::findPeaks (mag_, kBins, top, peaks, kBins / 2, kernels::findPeaksVector());
        }
        else
        {
            for (int k = 0; k < kBins; ++k)
                top = std::max (top, mag_[k]);
            for (int k = 1; k < kBins - 1 && numPeaks < kBins / 2; ++k)
                if (mag_[k] > mag_[k - 1] && mag_[k] >= mag_[k + 1] && mag_[k] > 1.0e-5f * top)
                    peaks[numPeaks++] = k;
        }
        const auto place = [&] (int k, int offset)
        {
            const int j = k + offset;
            if (j >= 1 && j < kBins && mag_[k] > om_[j])
            {
                om_[j] = mag_[k];
                of_[j] = freq_[k] * static_cast<float> (r) + shiftRad;
                oth_[j] = theta_[k];
            }
        };
        if (fast)
            kernels::placeAll (mag_, freq_, theta_, om_, of_, oth_, peaks, numPeaks, r, shiftBins, shiftRad, kBins, neon);
        else
            for (int i = 0; i < numPeaks; ++i)
            {
                const int centre = peaks[i];
                const int from = i == 0 ? 1 : (peaks[i - 1] + centre + 1) / 2;
                const int to = i + 1 == numPeaks ? kBins - 1 : (centre + peaks[i + 1]) / 2;
                const int offset = static_cast<int> (std::lround (static_cast<double> (centre) * r + shiftBins)) - centre;
                for (int k = from; k <= to; ++k)
                    place (k, offset);
            }
    }
    if (p.bandPostWarp)
        band (om_, hostBinHz);
}

void SpectralVoice::renderChannel (const SpectralParams& p, int ch, double frame, bool transientHit) noexcept
{
    const Analysis& an = *analysis_;
    Channel& c = channel_[ch];
    Scratch& scratch = *p.scratch;
    float* const re_ = scratch.re; float* const im_ = scratch.im; float* const om_ = scratch.om; float* const of_ = scratch.of; float* const oth_ = scratch.oth;
    float twist = 0.0f;
    shapeFrame (p, an, std::min (ch, an.channels() - 1), frame, hostRate_, scratch, c.smear, twist, false);

    // phases: advance by the frequency of the partial; after a transient (Transients) restart from the phases of the analysis; Phase Lock ties the bins around a peak to it
    if ((transientHit && p.transients) || ! phaseInit_[ch])
    {
        phaseInit_[ch] = true;
        for (int j = 0; j < kBins; ++j)
            c.synthPhase[j] = oth_[j];
    }
    else
        for (int j = 0; j < kBins; ++j)
        {
            float x = c.synthPhase[j] + of_[j] * kHop;
            if (x > 100.0f || x < -100.0f)
                x = wrapPi (std::fmod (x, kTwoPi));
            c.synthPhase[j] = x;
        }
    if (p.phaseLock)
    {
        int peak = -1;
        for (int j = 1; j < kBins - 1; ++j)
        {
            if (! (om_[j] > om_[j - 1] && om_[j] >= om_[j + 1] && om_[j] > 1.0e-6f))
                continue;
            // the region of the previous peak ends in the middle between the two
            const int from = peak < 0 ? 1 : (peak + j) / 2 + 1;
            if (peak >= 0)
                for (int q = peak + 1; q < from; ++q)
                    c.synthPhase[q] = c.synthPhase[peak] + (oth_[q] - oth_[peak]);
            else
                for (int q = 1; q < j; ++q)
                    c.synthPhase[q] = c.synthPhase[j] + (oth_[q] - oth_[j]);
            peak = j;
        }
        if (peak >= 0)
            for (int q = peak + 1; q < kBins; ++q)
                c.synthPhase[q] = c.synthPhase[peak] + (oth_[q] - oth_[peak]);
    }

    for (int j = 0; j < kBins; ++j)
    {
        float angle = c.synthPhase[j];
        if (twist != 0.0f)
            angle += twist * kTwoPi * static_cast<float> (j) * static_cast<float> (j) * 0.05f / kBins;
        const float magnitude = j == 0 ? 0.0f : om_[j];
        re_[j] = magnitude * std::cos (angle);
        im_[j] = j == kBins - 1 ? 0.0f : magnitude * std::sin (angle);
    }
    for (int j = 1; j < kFftSize / 2; ++j)
    {
        re_[kFftSize - j] = re_[j];
        im_[kFftSize - j] = -im_[j];
    }
    Fft::instance().inverse (re_, im_);
    for (int n = 0; n < kFftSize; ++n)
        c.ring[(ola_ + n) & (kFftSize - 1)] += re_[n] * hannAt (n) * kOutputScale;
}

void SpectralVoice::renderChannelFast (const SpectralParams& p, int ch, double frame, bool transientHit, bool reuseShape, float* twistState) noexcept
{
    const Analysis& an = *analysis_;
    Channel& c = channel_[ch];
    Scratch& scratch = *p.scratch;
    float* const re_ = scratch.re; float* const im_ = scratch.im; float* const om_ = scratch.om; float* const of_ = scratch.of; float* const oth_ = scratch.oth;
    const bool neon = kernels::haveNeon();
    float twist = 0.0f;
    if (reuseShape && twistState != nullptr)
        twist = *twistState;
    else
    {
        shapeFrame (p, an, std::min (ch, an.channels() - 1), frame, hostRate_, scratch, c.smear, twist, true);
        if (twistState != nullptr)
            *twistState = twist;
    }

    // phases: advance by the frequency of the partial; after a transient (Transients) restart from the phases of the analysis; Phase Lock ties the bins around a peak to it
    bool fusedAdvance = false;
    if ((transientHit && p.transients) || ! phaseInit_[ch])
    {
        phaseInit_[ch] = true;
        for (int j = 0; j < kBins; ++j)
            c.synthPhase[j] = oth_[j];
    }
    else if (p.phaseLock || twist != 0.0f)
        kernels::advancePhases (c.synthPhase, of_, kBins, neon);
    else
        fusedAdvance = true;   // phase update, sine and cosine and the bins in one pass below
    if (p.phaseLock)
    {
        int peak = -1;
        for (int j = 1; j < kBins - 1; ++j)
        {
            if (! (om_[j] > om_[j - 1] && om_[j] >= om_[j + 1] && om_[j] > 1.0e-6f))
                continue;
            const int from = peak < 0 ? 1 : (peak + j) / 2 + 1;
            if (peak >= 0)
                for (int q = peak + 1; q < from; ++q)
                    c.synthPhase[q] = c.synthPhase[peak] + (oth_[q] - oth_[peak]);
            else
                for (int q = 1; q < j; ++q)
                    c.synthPhase[q] = c.synthPhase[j] + (oth_[q] - oth_[j]);
            peak = j;
        }
        if (peak >= 0)
            for (int q = peak + 1; q < kBins; ++q)
                c.synthPhase[q] = c.synthPhase[peak] + (oth_[q] - oth_[peak]);
    }

    // the bins of the frame: magnitude * (cos, sin) of the phase (bin 0 silent, the Nyquist bin real), then the transform of the real signal and the overlap add
    if (fusedAdvance)
        kernels::advanceToSpectrum (c.synthPhase, of_, om_, re_, im_, kBins, neon);
    else
    {
        const float* angle = c.synthPhase;
        if (twist != 0.0f)
        {
            for (int j = 0; j < kBins; ++j)
                scratch.angle[j] = c.synthPhase[j] + twist * kTwoPi * static_cast<float> (j) * static_cast<float> (j) * 0.05f / kBins;
            angle = scratch.angle;
        }
        kernels::spectrumFromAngles (angle, om_, re_, im_, kBins, neon);
    }
    re_[0] = 0.0f;               // bin 0 is silent, and the Nyquist bin is real
    im_[0] = 0.0f;
    im_[kBins - 1] = 0.0f;
    kernels::RealIfft::instance().inverse (re_, im_, scratch.zr, scratch.zi, neon);
    kernels::overlapAdd (c.ring, ola_, scratch.zr, scratch.zi, kOutputScale, neon);
}

/** The gains of the first hop for each set of made frames: 1.5 over the sum of the squared synthesis windows of the frames 1 .. 3 that are in the ring (bit j - 1: frame j), at the sample q of the
    first hop (frame j is at n = q + (3 - j) kHop there). With all four frames the sum is 1.5 and the gain 1; without the others it renormalises the output to the level of the frames that exist. */
static const float* startGain (int mask) noexcept
{
    static const struct Table
    {
        float g[8][kHop];
        Table()
        {
            for (int m = 0; m < 8; ++m)
                for (int q = 0; q < kHop; ++q)
                {
                    double sum = 0.0;
                    for (int j = 1; j <= 3; ++j)
                        if (m & (1 << (j - 1)))
                        {
                            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (q + (3 - j) * kHop) / kFftSize);
                            sum += w * w;
                        }
                    g[m][q] = static_cast<float> (1.5 / std::max (sum, 0.02));
                }
        }
    } table;
    return table.g[mask & 7];
}

/** The most that a frame left out may weigh (the squared window of its own sample) at any sample of the first hop: it is made at the last block boundary before it would weigh more. */
static constexpr double kMaxMissingWeight = 0.5;

void SpectralVoice::startSchedule (int offset, bool smears, int join[3]) noexcept
{
    const int d = std::max (0, std::min (kHop - 1, offset));
    for (int j = 1; j <= 3; ++j)
    {
        int latest = kHop;
        if (smears)
            latest = d;   // the smear memory follows the order of the four shapings of the priming: all frames at once
        else
            for (int q = d; q < kHop; ++q)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (q + (3 - j) * kHop) / kFftSize);
                if (w * w > kMaxMissingWeight)
                {
                    latest = q;
                    break;
                }
            }
        join[j - 1] = std::max (d, (latest / kStartStep) * kStartStep);
    }
}

/** The phases of frame j of the priming (j = 1 .. 3) into `out`: the analysis phase (the phase lock applied, as frame 0 had it) advanced j times by the bin frequencies, wrapped as always; from the
    phases the voice holds for an earlier frame when there are such. When frame j is the latest so far the voice goes on from it (synthPhase). scratch.of / oth / om are those of the frame. */
void SpectralVoice::startPhases (const SpectralParams& p, int ch, int j, float* out) noexcept
{
    Scratch& scratch = *p.scratch;
    Channel& c = channel_[ch];
    const bool neon = kernels::haveNeon();
    const float* const of_ = scratch.of; const float* const oth_ = scratch.oth; const float* const om_ = scratch.om;
    const auto lock = [&]
    {
        int peak = -1;
        for (int q = 1; q < kBins - 1; ++q)
        {
            if (! (om_[q] > om_[q - 1] && om_[q] >= om_[q + 1] && om_[q] > 1.0e-6f))
                continue;
            const int from = peak < 0 ? 1 : (peak + q) / 2 + 1;
            if (peak >= 0)
                for (int r = peak + 1; r < from; ++r)
                    out[r] = out[peak] + (oth_[r] - oth_[peak]);
            else
                for (int r = 1; r < q; ++r)
                    out[r] = out[q] + (oth_[r] - oth_[q]);
            peak = q;
        }
        if (peak >= 0)
            for (int r = peak + 1; r < kBins; ++r)
                out[r] = out[peak] + (oth_[r] - oth_[peak]);
    };
    int steps;
    if (phaseFrame_[ch] >= 1 && phaseFrame_[ch] <= j)
    {
        std::memcpy (out, c.synthPhase, sizeof (float) * kBins);
        steps = j - phaseFrame_[ch];
    }
    else
    {
        std::memcpy (out, oth_, sizeof (float) * kBins);
        if (p.phaseLock)
            lock();
        steps = j;
    }
    for (int i = 0; i < steps; ++i)
    {
        if (reference_)
            for (int k = 0; k < kBins; ++k)
            {
                float x = out[k] + of_[k] * kHop;
                if (x > 100.0f || x < -100.0f)
                    x = wrapPi (std::fmod (x, kTwoPi));
                out[k] = x;
            }
        else
            kernels::advancePhases (out, of_, kBins, neon);
        if (p.phaseLock)
            lock();
    }
    if (j > phaseFrame_[ch])
    {
        std::memcpy (c.synthPhase, out, sizeof (float) * kBins);
        phaseFrame_[ch] = j;
        phaseInit_[ch] = true;
    }
}

/** Makes the frames of `mask` (bit j - 1: frame j of the priming) of every channel into the ring, in order, at the position of the start (startFrame_) with the settings `p` has now: one shaping for
    them all (the Smear warp, which keeps a memory that each shaping changes, shapes every frame, after one more shaping for the frame 0 that is never made when `phantom`: its memory is then
    what the four shapings of the old priming left). Frame j lies at ring position j kHop. */
void SpectralVoice::startFrames (const SpectralParams& p, int mask, bool phantom) noexcept
{
    const Analysis& an = *analysis_;
    const int channels = an.channels() > 1 ? 2 : 1;
    Scratch& scratch = *p.scratch;
    const bool neon = kernels::haveNeon();
    bool smears = false;
    for (int w = 0; w < 2; ++w)
        smears = smears || (p.warpType[w] == static_cast<int> (Warp::smear) && p.warpAmount[w] > 0.0f);
    for (int ch = 0; ch < channels; ++ch)
    {
        Channel& c = channel_[ch];
        float twist = 0.0f;
        bool shaped = false;
        if (phantom && smears && mask != 0)
            shapeFrame (p, an, std::min (ch, an.channels() - 1), startFrame_, hostRate_, scratch, c.smear, twist, ! reference_);
        for (int j = 1; j <= 3; ++j)
        {
            if (! (mask & (1 << (j - 1))))
                continue;
            if (! shaped || smears)
            {
                shapeFrame (p, an, std::min (ch, an.channels() - 1), startFrame_, hostRate_, scratch, c.smear, twist, ! reference_);
                shaped = true;
            }
            startPhases (p, ch, j, scratch.phaseTmp);
            const int at = (j * kHop) & (kFftSize - 1);
            if (reference_)
            {
                for (int k = 0; k < kBins; ++k)
                {
                    float angle = scratch.phaseTmp[k];
                    if (twist != 0.0f)
                        angle += twist * kTwoPi * static_cast<float> (k) * static_cast<float> (k) * 0.05f / kBins;
                    const float magnitude = k == 0 ? 0.0f : scratch.om[k];
                    scratch.re[k] = magnitude * std::cos (angle);
                    scratch.im[k] = k == kBins - 1 ? 0.0f : magnitude * std::sin (angle);
                }
                for (int k = 1; k < kFftSize / 2; ++k)
                {
                    scratch.re[kFftSize - k] = scratch.re[k];
                    scratch.im[kFftSize - k] = -scratch.im[k];
                }
                Fft::instance().inverse (scratch.re, scratch.im);
                for (int n = (3 - j) * kHop; n < kFftSize; ++n)   // frame j began (3 - j) hops before the first output sample: that part is gone
                    c.ring[(at + n) & (kFftSize - 1)] += scratch.re[n] * hannAt (n) * kOutputScale;
            }
            else
            {
                const float* angle = scratch.phaseTmp;
                if (twist != 0.0f)
                {
                    for (int k = 0; k < kBins; ++k)
                        scratch.angle[k] = scratch.phaseTmp[k] + twist * kTwoPi * static_cast<float> (k) * static_cast<float> (k) * 0.05f / kBins;
                    angle = scratch.angle;
                }
                kernels::spectrumFromAngles (angle, scratch.om, scratch.re, scratch.im, kBins, neon);
                scratch.re[0] = 0.0f;
                scratch.im[0] = 0.0f;
                scratch.im[kBins - 1] = 0.0f;
                kernels::RealIfft::instance().inverse (scratch.re, scratch.im, scratch.zr, scratch.zi, neon);
                std::memset (scratch.olaTmp, 0, sizeof (float) * kFftSize);
                kernels::overlapAdd (scratch.olaTmp, 0, scratch.zr, scratch.zi, kOutputScale, neon);
                for (int n = (3 - j) * kHop; n < kFftSize; ++n)
                    c.ring[(at + n) & (kFftSize - 1)] += scratch.olaTmp[n];
            }
        }
    }
}

/** The samples `from` .. kHop - 1 of the first hop into `finished`: the ring of the hop (position 3 kHop) times the gain of the frames made so far. */
void SpectralVoice::refreshFirstHop (int from) noexcept
{
    const int channels = analysis_ != nullptr && analysis_->channels() > 1 ? 2 : 1;
    const float* const gain = startGain (startMask_);
    for (int ch = 0; ch < channels; ++ch)
    {
        Channel& c = channel_[ch];
        for (int q = from; q < kHop; ++q)
            c.finished[q] = c.ring[(3 * kHop + q) & (kFftSize - 1)] * gain[q];
    }
}

/** The first hop has been read: the frames that were still to be made are made now (their weight in the hop is gone, their later part is in the ring), the ring slots of the hop are cleared (the next
    frame writes over them). (The play head took its first step in noteOn, as in the first synthesizeHop of the old start.) */
void SpectralVoice::endFirstHop (const SpectralParams& p) noexcept
{
    int due = 0;
    for (int j = 0; j < 3; ++j)
        if (startJoin_[j] >= 0)
        {
            due |= 1 << j;
            startJoin_[j] = -1;
        }
    if (due != 0)
    {
        startFrames (p, due, false);
        startMask_ |= due;
    }
    const int channels = analysis_ != nullptr && analysis_->channels() > 1 ? 2 : 1;
    for (int ch = 0; ch < channels; ++ch)
        for (int q = 0; q < kHop; ++q)
            channel_[ch].ring[(3 * kHop + q) & (kFftSize - 1)] = 0.0f;
    startActive_ = false;
}

/** The four frames of the start of a note (the three of synthesizeHop (p, false) and the one that the first synthesizeHop (p, true) makes), one channel after the other: the frames are at the
    same position of the analysis with the same settings, so the shaped bins are the same for all four and are made once per channel (the phases differ and are advanced for each). Only the
    smear warp keeps a memory that each call changes: a note with it shapes every frame, as the four calls did. The frame of the fourth hop stays in the ring (the first synthesizeHop reads the
    hop and does not synthesise again: `framePrimed_`), the ring of the overlap add ends up exactly where the three hops and the fourth frame would leave it.
    The first real frame is the fourth: it carries the phases that the voice goes on from (the analysis phase advanced three times by the bin frequencies, wrapped as always), so that
    everything after the priming is what the voice has always played. (A circular shift of one frame cannot stand in for the earlier three: see docs/engine-spec/07 section 8.) */
void SpectralVoice::primeFast (const SpectralParams& p) noexcept
{
    if (analysis_ == nullptr)
        return;
    const int channels = analysis_->channels() > 1 ? 2 : 1;
    const double fileFrames = static_cast<double> (analysis_->fileFrames());
    if (p.manual)
        head_ = std::min (p.regionLo, p.regionHi) + std::fabs (static_cast<double> (p.regionHi) - static_cast<double> (p.regionLo)) * clamp01 (p.position);
    const double frame = head_ * fileFrames / kHop;
    const int index = static_cast<int> (std::lround (frame));
    const bool transientHit = analysis_->transient (index) && index != lastTransientFrame_;
    lastTransientFrame_ = index;
    bool smears = false;
    for (int w = 0; w < 2; ++w)
        smears = smears || (p.warpType[w] == static_cast<int> (Warp::smear) && p.warpAmount[w] > 0.0f);
    for (int ch = 0; ch < channels; ++ch)
    {
        Channel& c = channel_[ch];
        float twist = 0.0f;
        for (int hop = 0; hop < 4; ++hop)
        {
            ola_ = hop * kHop;
            renderChannelFast (p, ch, frame, hop == 0 && transientHit, hop > 0 && ! smears, &twist);
            if (hop == 3)
                break;
            for (int n = 0; n < kHop; ++n)
            {
                const int at = (ola_ + n) & (kFftSize - 1);
                c.finished[n] = c.ring[at];
                c.ring[at] = 0.0f;
            }
        }
    }
    ola_ = (3 * kHop) & (kFftSize - 1);
    framePrimed_ = true;
}

void SpectralVoice::synthesizeHop (const SpectralParams& p, bool advance) noexcept
{
    if (analysis_ == nullptr)
        return;
    const int channels = analysis_->channels() > 1 ? 2 : 1;
    const double fileFrames = static_cast<double> (analysis_->fileFrames());
    double frame = head_ * fileFrames / kHop;
    if (p.manual)
        head_ = std::min (p.regionLo, p.regionHi) + std::fabs (static_cast<double> (p.regionHi) - static_cast<double> (p.regionLo)) * clamp01 (p.position);
    frame = head_ * fileFrames / kHop;

    bool transientHit = false;
    const int index = static_cast<int> (std::lround (frame));
    if (analysis_->transient (index) && index != lastTransientFrame_)
        transientHit = true;
    lastTransientFrame_ = index;

    const bool rendered = framePrimed_;   // primeFast made the frame of this hop (the first one of the note)
    framePrimed_ = false;
    if (headDone_ && advance)
    {
        // the play head ran out: the overlap buffer drains for four hops, then the voice ends
        if (++drain_ > 4)
        {
            active_ = false;
            return;
        }
    }
    else if (! rendered)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            if (reference_)
                renderChannel (p, ch, frame, transientHit);
            else
                renderChannelFast (p, ch, frame, transientHit);
        }
    }

    for (int ch = 0; ch < channels; ++ch)
    {
        Channel& c = channel_[ch];
        for (int n = 0; n < kHop; ++n)
        {
            const int at = (ola_ + n) & (kFftSize - 1);
            c.finished[n] = c.ring[at];
            c.ring[at] = 0.0f;
        }
    }
    ola_ = (ola_ + kHop) & (kFftSize - 1);
    if (advance && ! p.manual)
        advanceHead (p);
}

void SpectralVoice::process (const SpectralParams& p, float* left, float* right, int numFrames) noexcept
{
    std::fill (left, left + numFrames, 0.0f);
    std::fill (right, right + numFrames, 0.0f);
    if (! active_ || analysis_ == nullptr || p.scratch == nullptr)
        return;
    const bool stereo = analysis_->channels() > 1;
    for (int i = 0; i < numFrames; ++i)
    {
        if (startActive_)
        {
            if (outPos_ >= kHop)
                endFirstHop (p);
            else
            {
                int due = 0;
                for (int j = 0; j < 3; ++j)
                    if (startJoin_[j] >= 0 && outPos_ >= startJoin_[j])
                    {
                        due |= 1 << j;
                        startJoin_[j] = -1;
                    }
                if (due != 0)
                {
                    startFrames (p, due, false);
                    startMask_ |= due;
                    refreshFirstHop (outPos_);
                }
            }
        }
        if (outPos_ >= kHop)
        {
            synthesizeHop (p, true);
            outPos_ = startSkip_;   // the first hop of a note is read from its offset on
            startSkip_ = 0;
            if (! active_)
                return;
        }
        const float l = channel_[0].finished[outPos_] * p.gain, r = (stereo ? channel_[1].finished[outPos_] : channel_[0].finished[outPos_]) * p.gain;
        left[i] = std::isfinite (l) ? std::max (-16.0f, std::min (16.0f, l)) : 0.0f;
        right[i] = std::isfinite (r) ? std::max (-16.0f, std::min (16.0f, r)) : 0.0f;
        ++outPos_;
    }
}

void computeMagnitudeSpectrum (const Analysis& an, const SpectralParams& p, double frame, float* om, Scratch& scratch) noexcept
{
    if (om == nullptr)
        return;
    std::fill (om, om + kBins, 0.0f);
    if (! an.valid() || an.numFrames() <= 0)
        return;
    // the very code the voice runs for one hop (shapeFrame), on channel 0, with a smear memory of its own that starts empty
    float smear[kBins] = {};
    float twist = 0.0f;
    const int last = an.numFrames() - 1;
    frame = std::max (0.0, std::min (static_cast<double> (last), frame));
    shapeFrame (p, an, 0, frame, p.hostRate > 0.0 ? p.hostRate : 48000.0, scratch, smear, twist, false);
    std::copy (scratch.om, scratch.om + kBins, om);
}
} // namespace spectral
} // namespace synthyboy
