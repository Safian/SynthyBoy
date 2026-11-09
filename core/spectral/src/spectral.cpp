// SynthyBoy - core/spectral: the spectral oscillator DSP.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/spectral/spectral.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
float applyWarp (int w, float a, float v, float* m, float* f, float* tm, float* tf, const float* external, float* smear, float& twist) noexcept
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

void SpectralVoice::noteOn (const Analysis* analysis, const SpectralParams& p, float startFraction) noexcept
{
    reset();
    if (analysis == nullptr || ! analysis->valid() || p.scratch == nullptr)
        return;
    analysis_ = analysis;
    hostRate_ = p.hostRate > 1000.0 ? p.hostRate : hostRate_;
    active_ = true;
    released_ = false;
    headDone_ = false;
    headDirection_ = 1;
    outPos_ = kHop;
    ola_ = 0;
    primed_ = false;
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
    // prime: three frames at the start position, so that the first output hop is the sum of four frames
    for (int i = 0; i < 3; ++i)
        synthesizeHop (p, false);
    primed_ = true;
    outPos_ = kHop;
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

void SpectralVoice::renderChannel (const SpectralParams& p, int ch, double frame, bool transientHit) noexcept
{
    const Analysis& an = *analysis_;
    Channel& c = channel_[ch];
    Scratch& scratch = *p.scratch;
    float* const re_ = scratch.re; float* const im_ = scratch.im; float* const mag_ = scratch.mag; float* const freq_ = scratch.freq; float* const theta_ = scratch.theta;
    float* const om_ = scratch.om; float* const of_ = scratch.of; float* const oth_ = scratch.oth; float* const tm_ = scratch.tm; float* const tf_ = scratch.tf;
    const int last = an.numFrames() - 1;
    const int i0 = std::max (0, std::min (last, static_cast<int> (std::floor (frame))));
    const int i1 = std::min (last, i0 + 1);
    const float fr = static_cast<float> (std::max (0.0, std::min (1.0, frame - std::floor (frame))));
    const int near = std::max (0, std::min (last, static_cast<int> (std::lround (frame))));
    const int src = std::min (ch, an.channels() - 1);
    const float* m0 = an.magnitude (src, i0);
    const float* m1 = an.magnitude (src, i1);
    const float* fq = an.frequency (src, near);
    const float* ph = an.phase (src, near);
    const double fileRate = an.fileRate();
    const double fileBinHz = fileRate / kFftSize, hostBinHz = hostRate_ / kFftSize;

    for (int k = 0; k < kBins; ++k)
    {
        mag_[k] = m0[k] + (m1[k] - m0[k]) * fr;
        freq_[k] = fq[k];
        theta_[k] = ph[k];
    }
    const auto band = [&] (float* m, double binHz)
    {
        const float lo = std::max (1.0f, p.freqLoHz), hi = std::max (lo, p.freqHiHz);
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
        for (int k = 0; k < kBins; ++k)
            mag_[k] *= p.filterGain[k];
    if (! p.bandPostWarp)
        band (mag_, fileBinHz);

    float extra = 1.0f, twist = 0.0f;
    for (int w = 0; w < 2; ++w)
        if (p.warpType[w] >= 0 && p.warpType[w] < kNumSpectralWarps && p.warpAmount[w] > 0.0f)
        {
            const float* ext = isExternalWarp (p.warpType[w]) ? p.external[externalSource (p.warpType[w])] : nullptr;
            extra *= applyWarp (p.warpType[w], p.warpAmount[w], p.warpVar[w], mag_, freq_, tm_, tf_, ext, c.smear, twist);
        }

    // the pitch: bin k goes to bin k * r (+ the frequency shift) with its frequency scaled the same way
    const double r = std::max (1.0 / 32.0, std::min (32.0, static_cast<double> (p.pitchRatio) * static_cast<double> (extra) * fileRate / hostRate_));
    const double shiftBins = static_cast<double> (p.freqShiftHz) / hostBinHz;
    const float shiftRad = kTwoPi * p.freqShiftHz / static_cast<float> (hostRate_);
    std::fill (om_, om_ + kBins, 0.0f);
    std::fill (of_, of_ + kBins, 0.0f);
    std::fill (oth_, oth_ + kBins, 0.0f);
    {
        // the regions around the peaks of the spectrum move as a whole (every bin by the offset of its peak), so that the shape of a partial's main lobe is kept
        // (Laroche / Dolson); the frequencies of the bins are scaled by the same ratio
        int peaks[kBins / 2];
        int numPeaks = 0;
        float top = 0.0f;
        for (int k = 0; k < kBins; ++k)
            top = std::max (top, mag_[k]);
        for (int k = 1; k < kBins - 1 && numPeaks < kBins / 2; ++k)
            if (mag_[k] > mag_[k - 1] && mag_[k] >= mag_[k + 1] && mag_[k] > 1.0e-5f * top)
                peaks[numPeaks++] = k;
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

    if (headDone_ && advance)
    {
        // the play head ran out: the overlap buffer drains for four hops, then the voice ends
        if (++drain_ > 4)
        {
            active_ = false;
            return;
        }
    }
    else
        for (int ch = 0; ch < channels; ++ch)
            renderChannel (p, ch, frame, transientHit);

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
        if (outPos_ >= kHop)
        {
            synthesizeHop (p, true);
            outPos_ = 0;
            if (! active_)
                return;
        }
        const float l = channel_[0].finished[outPos_] * p.gain, r = (stereo ? channel_[1].finished[outPos_] : channel_[0].finished[outPos_]) * p.gain;
        left[i] = std::isfinite (l) ? std::max (-16.0f, std::min (16.0f, l)) : 0.0f;
        right[i] = std::isfinite (r) ? std::max (-16.0f, std::min (16.0f, r)) : 0.0f;
        ++outPos_;
    }
}
} // namespace spectral
} // namespace synthyboy
