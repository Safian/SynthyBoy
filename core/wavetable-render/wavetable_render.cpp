// SynthyBoy - core: clean-room wavetable renderer (docs/ui-design/11-vital-preset-import.md 1.3.1, Annex A.5).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Written from the procedure of the design document and the field list of 1.1 only. The design fixes the render model (frames = last
// keyframe position + 1, per frame every group runs its components in order, groups are summed, then the post-process) but gives no
// formulas for the individual components: every formula below is an own definition and is marked "SPEC-GAP:" (see
// core/vital-import/REPORT.md). The golden references of Annex C.2 decide later whether a definition has to change.

#include "synthyboy/wavetable_render.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <new>

namespace synthyboy
{
namespace wtrender
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int N = kFrameSize;
using Cx = std::complex<double>;

bool finite (double v) noexcept { return std::isfinite (v); }
double clampd (double v, double lo, double hi) noexcept { return ! finite (v) ? lo : (v < lo ? lo : (v > hi ? hi : v)); }

// ---------------------------------------------------------------------------------------------------------------------
// FFT (radix 2, size 2048)
// ---------------------------------------------------------------------------------------------------------------------
void fft (Cx* a, bool inverse) noexcept
{
    for (int i = 1, j = 0; i < N; ++i)
    {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (int len = 2; len <= N; len <<= 1)
    {
        const double ang = 2.0 * kPi / len * (inverse ? 1.0 : -1.0);
        const Cx wl (std::cos (ang), std::sin (ang));
        for (int i = 0; i < N; i += len)
        {
            Cx w (1.0, 0.0);
            for (int k = 0; k < len / 2; ++k)
            {
                const Cx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (int i = 0; i < N; ++i)
            a[i] /= static_cast<double> (N);
}

void toSpectrum (const float* x, std::vector<Cx>& s)
{
    s.assign (N, Cx (0, 0));
    for (int i = 0; i < N; ++i)
        s[static_cast<size_t> (i)] = Cx (x[i], 0.0);
    fft (s.data(), false);
}

void fromSpectrum (std::vector<Cx>& s, float* x) noexcept
{
    // keep the signal real: enforce conjugate symmetry from the lower half
    s[0] = Cx (s[0].real(), 0.0);
    s[N / 2] = Cx (s[N / 2].real(), 0.0);
    for (int n = 1; n < N / 2; ++n)
        s[static_cast<size_t> (N - n)] = std::conj (s[static_cast<size_t> (n)]);
    fft (s.data(), true);
    for (int i = 0; i < N; ++i)
        x[i] = static_cast<float> (s[static_cast<size_t> (i)].real());
}

double powerCurve (double t, double p) noexcept
{
    p = clampd (p, -50.0, 50.0);
    t = clampd (t, 0.0, 1.0);
    if (std::fabs (p) < 1.0e-3)
        return t;
    return (std::exp (p * t) - 1.0) / (std::exp (p) - 1.0);
}

float sampleAt (const float* buf, double pos) noexcept // cyclic, linear interpolation, pos in samples
{
    pos -= std::floor (pos / N) * N;
    int i0 = static_cast<int> (pos);
    if (i0 >= N)
        i0 = N - 1;
    const double f = pos - i0;
    const int i1 = (i0 + 1) % N;
    return static_cast<float> (buf[i0] * (1.0 - f) + buf[i1] * f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Keyframe interpolation
// ---------------------------------------------------------------------------------------------------------------------
struct Blend
{
    int idx[4] = { 0, 0, 0, 0 };
    double w[4] = { 0, 1, 0, 0 };
};

using KeyList = std::vector<const Keyframe*>;

Blend makeBlend (const KeyList& ks, int frame, Interpolation style) noexcept
{
    Blend b;
    const int n = static_cast<int> (ks.size());
    if (n == 0)
        return b;
    if (frame <= ks[0]->position || n == 1)
    {
        b.idx[0] = b.idx[1] = b.idx[2] = b.idx[3] = 0;
        return b;
    }
    if (frame >= ks[static_cast<size_t> (n - 1)]->position)
    {
        b.idx[0] = b.idx[1] = b.idx[2] = b.idx[3] = n - 1;
        return b;
    }
    int i = 0;
    while (i + 1 < n - 1 && ks[static_cast<size_t> (i + 1)]->position <= frame)
        ++i;
    const double t = static_cast<double> (frame - ks[static_cast<size_t> (i)]->position)
                   / static_cast<double> (ks[static_cast<size_t> (i + 1)]->position - ks[static_cast<size_t> (i)]->position);
    b.idx[0] = std::max (i - 1, 0);
    b.idx[1] = i;
    b.idx[2] = i + 1;
    b.idx[3] = std::min (i + 2, n - 1);
    switch (style)
    {
        case Interpolation::none:   b.w[0] = 0; b.w[1] = 1; b.w[2] = 0; b.w[3] = 0; break;
        case Interpolation::linear: b.w[0] = 0; b.w[1] = 1.0 - t; b.w[2] = t; b.w[3] = 0; break;
        case Interpolation::cubic: // Catmull-Rom
        {
            const double t2 = t * t, t3 = t2 * t;
            b.w[0] = -0.5 * t3 + t2 - 0.5 * t;
            b.w[1] = 1.5 * t3 - 2.5 * t2 + 1.0;
            b.w[2] = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
            b.w[3] = 0.5 * t3 - 0.5 * t2;
            break;
        }
    }
    return b;
}

template <class Get>
double blendScalar (const Blend& b, const KeyList& ks, Get get) noexcept
{
    double v = 0.0;
    for (int j = 0; j < 4; ++j)
        if (b.w[j] != 0.0)
            v += b.w[j] * static_cast<double> (get (*ks[static_cast<size_t> (b.idx[j])]));
    return v;
}

/** Per component data prepared once per render. */
struct CompState
{
    KeyList keys;                              // sorted by position, stable
    std::vector<std::vector<float>> waves;     // time domain wave per keyframe (Wave Source, Line Source)
    std::vector<std::vector<Cx>> spectra;      // Wave Source in frequency mode
};

float keyWave (const Keyframe& k, int i) noexcept { return static_cast<size_t> (i) < k.wave.size() ? k.wave[static_cast<size_t> (i)] : 0.0f; }

void prepare (const Component& c, CompState& s)
{
    s.keys.clear();
    for (const auto& k : c.keyframes)
        s.keys.push_back (&k);
    std::stable_sort (s.keys.begin(), s.keys.end(), [] (const Keyframe* a, const Keyframe* b) { return a->position < b->position; });

    if (c.type == ComponentType::waveSource)
    {
        s.waves.resize (s.keys.size());
        for (size_t i = 0; i < s.keys.size(); ++i)
        {
            s.waves[i].resize (N);
            for (int j = 0; j < N; ++j)
                s.waves[i][static_cast<size_t> (j)] = keyWave (*s.keys[i], j);
            if (c.frequencyInterpolation)
            {
                s.spectra.emplace_back();
                toSpectrum (s.waves[i].data(), s.spectra.back());
            }
        }
    }
    else if (c.type == ComponentType::lineSource)
    {
        s.waves.resize (s.keys.size());
        std::vector<float> line (N);
        for (size_t i = 0; i < s.keys.size(); ++i)
        {
            s.waves[i].resize (N);
            renderLine (s.keys[i]->line, line.data(), N);
            // SPEC-GAP: the design gives `pull_power` but not its meaning. Defined here as a power curve applied to the horizontal axis
            // before the line is read; 0 = none. The line's y (0..1, like an LFO line) becomes -1..1 for a wavetable.
            for (int j = 0; j < N; ++j)
            {
                const double x = static_cast<double> (j) / N;
                const double xw = powerCurve (x, s.keys[i]->pullPower) * N;
                s.waves[i][static_cast<size_t> (j)] = sampleAt (line.data(), xw) * 2.0f - 1.0f;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The components. Each takes the interpolated state and works on buf[N].
// ---------------------------------------------------------------------------------------------------------------------
void phaseWrap (double& d) noexcept
{
    d -= 2.0 * kPi * std::floor (d / (2.0 * kPi) + 0.5);
}

void doWaveSource (const Component& c, const CompState& s, const Blend& b, float* buf)
{
    if (s.keys.empty())
        return;
    if (! c.frequencyInterpolation)
    {
        for (int i = 0; i < N; ++i)
        {
            double v = 0.0;
            for (int j = 0; j < 4; ++j)
                if (b.w[j] != 0.0)
                    v += b.w[j] * s.waves[static_cast<size_t> (b.idx[j])][static_cast<size_t> (i)];
            buf[i] = static_cast<float> (v);
        }
        return;
    }
    // frequency mode: magnitudes are blended linearly with the weights, phases along the shortest arc relative to keyframe idx[1]
    std::vector<Cx> out (N, Cx (0, 0));
    const auto& ref = s.spectra[static_cast<size_t> (b.idx[1])];
    for (int n = 0; n <= N / 2; ++n)
    {
        double mag = 0.0, dphase = 0.0;
        const double refPhase = std::arg (ref[static_cast<size_t> (n)]);
        for (int j = 0; j < 4; ++j)
        {
            if (b.w[j] == 0.0)
                continue;
            const Cx& x = s.spectra[static_cast<size_t> (b.idx[j])][static_cast<size_t> (n)];
            mag += b.w[j] * std::abs (x);
            double d = std::arg (x) - refPhase;
            phaseWrap (d);
            dphase += b.w[j] * d;
        }
        // SPEC-GAP: a cubic weight may be negative; a negative magnitude is clamped to 0 (the design does not say).
        out[static_cast<size_t> (n)] = std::polar (std::max (mag, 0.0), refPhase + dphase);
    }
    fromSpectrum (out, buf);
}

void doLineSource (const Component&, const CompState& s, const Blend& b, float* buf)
{
    if (s.keys.empty())
        return;
    for (int i = 0; i < N; ++i)
    {
        double v = 0.0;
        for (int j = 0; j < 4; ++j)
            if (b.w[j] != 0.0)
                v += b.w[j] * s.waves[static_cast<size_t> (b.idx[j])][static_cast<size_t> (i)];
        buf[i] = static_cast<float> (v);
    }
}

double hann (double u) noexcept { return 0.5 - 0.5 * std::cos (2.0 * kPi * u); }

void doAudioFile (const Component& c, const CompState& s, const Blend& b, float* buf)
{
    if (s.keys.empty() || c.audio.empty())
    {
        std::fill (buf, buf + N, 0.0f);
        return;
    }
    const double len = static_cast<double> (c.audio.size());
    const double start = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.startPosition; }), 0.0, std::max (0.0, len - 1.0));
    const double fade = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.windowFade; }), 0.0, 1.0);
    // SPEC-GAP: the design lists window_size per keyframe and per component; the keyframe value wins, the component's is the default
    // of a keyframe that has none (the parser stores it in that case).
    const double w = clampd (std::floor (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.windowSize; }) + 0.5), 2.0, 1048576.0);

    // SPEC-GAP: start_position is read as a sample index into the audio, the window as one cycle that is resampled to 2048 samples.
    std::vector<float> seg (N);
    for (int k = 0; k < N; ++k)
    {
        const double p = start + static_cast<double> (k) * w / N;
        const long i0 = static_cast<long> (p);
        const double f = p - static_cast<double> (i0);
        const double a = i0 < static_cast<long> (c.audio.size()) ? c.audio[static_cast<size_t> (i0)] / 32768.0 : 0.0;
        const double bb = i0 + 1 < static_cast<long> (c.audio.size()) ? c.audio[static_cast<size_t> (i0 + 1)] / 32768.0 : 0.0;
        seg[static_cast<size_t> (k)] = static_cast<float> (a * (1.0 - f) + bb * f);
    }

    // SPEC-GAP: the meaning of the four fade styles is not in the design; defined here: 0 wave blend = the cycle crossfaded with its
    // half-cycle rotation by a Hann window, 1 none, 2 time = Hann window in time, 3 frequency = Hann roll-off over the harmonics.
    if (c.fadeStyle == 0)
    {
        std::vector<float> src = seg;
        for (int k = 0; k < N; ++k)
        {
            const double u = static_cast<double> (k) / N;
            const double blended = hann (u) * src[static_cast<size_t> (k)] + hann (std::fmod (u + 0.5, 1.0)) * src[static_cast<size_t> ((k + N / 2) % N)];
            seg[static_cast<size_t> (k)] = static_cast<float> ((1.0 - fade) * src[static_cast<size_t> (k)] + fade * blended);
        }
    }
    else if (c.fadeStyle == 2)
    {
        for (int k = 0; k < N; ++k)
            seg[static_cast<size_t> (k)] *= static_cast<float> ((1.0 - fade) + fade * hann (static_cast<double> (k) / N));
    }

    if (c.fadeStyle == 3 || c.phaseStyle == 1 || c.phaseStyle == 2)
    {
        std::vector<Cx> sp;
        toSpectrum (seg.data(), sp);
        for (int n = 1; n < N / 2; ++n)
        {
            Cx& x = sp[static_cast<size_t> (n)];
            double mag = std::abs (x), ph = std::arg (x);
            if (c.fadeStyle == 3)
                mag *= (1.0 - fade) + fade * (0.5 + 0.5 * std::cos (kPi * n / (N / 2)));
            if (c.phaseStyle == 1)
                ph = 0.0;
            else if (c.phaseStyle == 2)
            {
                // SPEC-GAP: "vocode" phase style: deterministic pseudo-random phases (seeded by random_seed), magnitudes kept.
                uint32_t h = c.randomSeed * 2654435761u + static_cast<uint32_t> (n) * 40503u + 12345u;
                h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
                ph = (static_cast<double> (h) / 4294967296.0) * 2.0 * kPi;
            }
            x = std::polar (mag, ph);
        }
        fromSpectrum (sp, seg.data());
    }

    double peak = 0.0;
    for (float v : seg)
        peak = std::max (peak, static_cast<double> (std::fabs (v)));
    const double mult = clampd (c.normalizeMult, 0.0, 16.0);
    // SPEC-GAP: normalize_gain read as a switch (non-zero = normalise the frame to a peak of 1), normalize_mult as a further factor.
    const double g = (c.normalizeGain && peak > 1.0e-9 ? 1.0 / peak : 1.0) * mult;
    for (int k = 0; k < N; ++k)
        buf[k] = static_cast<float> (seg[static_cast<size_t> (k)] * g);
}

void doShepard (const CompState& s, int frame, float* buf)
{
    // SPEC-GAP: nothing but "common fields" is specified. Defined: harmonics 1, 2, 4 .. 1024 (11 octaves) with a bell shaped weight
    // over the octave index; with t = frame / last keyframe position the weights glide to those of the next higher octave, so the
    // pattern at t = 1 is the one at t = 0 shifted up by an octave (a rising Shepard tone). Sine phases are zero.
    const int last = s.keys.empty() ? kMaxPosition : std::max (1, s.keys.back()->position);
    const double t = clampd (static_cast<double> (frame) / last, 0.0, 1.0);
    const auto bell = [] (int k) { const double s2 = std::sin (kPi * (k + 0.5) / 11.0); return s2 * s2; };
    std::fill (buf, buf + N, 0.0f);
    for (int k = 0; k <= 10; ++k)
    {
        const double amp = (1.0 - t) * bell (k) + t * bell (k + 1);
        const int harmonic = 1 << k;
        for (int i = 0; i < N; ++i)
            buf[i] += static_cast<float> (amp * std::sin (2.0 * kPi * harmonic * i / N));
    }
    float peak = 0.0f;
    for (int i = 0; i < N; ++i)
        peak = std::max (peak, std::fabs (buf[i]));
    if (peak > 1.0e-9f)
        for (int i = 0; i < N; ++i)
            buf[i] /= peak;
}

void doPhaseShift (const CompState& s, const Blend& b, float* buf)
{
    const double phase = blendScalar (b, s.keys, [] (const Keyframe& k) { return k.phase; });
    const double mix = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.mix; }), 0.0, 1.0);
    const int style = s.keys[static_cast<size_t> (b.idx[1])]->style;
    std::vector<Cx> sp;
    toSpectrum (buf, sp);
    // SPEC-GAP: styles are not listed in the design. Defined: 0 normal = the same phase offset for every harmonic, 1 harmonic = offset
    // proportional to the harmonic number (a rotation of the cycle), 2 inharmonic = proportional to the square of the number.
    for (int n = 1; n < N / 2; ++n)
    {
        const double scale = style == 1 ? n : (style == 2 ? static_cast<double>(n) * n : 1.0);
        double th = std::fmod (2.0 * kPi * phase * scale, 2.0 * kPi);
        sp[static_cast<size_t> (n)] *= Cx (std::cos (th), std::sin (th));
    }
    std::vector<float> wet (N);
    fromSpectrum (sp, wet.data());
    for (int i = 0; i < N; ++i)
        buf[i] = static_cast<float> ((1.0 - mix) * buf[i] + mix * wet[static_cast<size_t> (i)]);
}

double windowShapeValue (int shape, double u) noexcept
{
    // SPEC-GAP: window shapes are not listed in the design. Defined: 0 cosine (Hann), 1 half sine, 2 linear (triangle), 3 square, 4 wiggle.
    switch (shape)
    {
        case 0: return hann (u);
        case 1: return std::sin (kPi * u);
        case 2: return 1.0 - std::fabs (2.0 * u - 1.0);
        case 3: return 1.0;
        default: return std::sin (kPi * u) * (0.5 + 0.5 * std::cos (8.0 * kPi * u));
    }
}

void doWaveWindow (const CompState& s, const Blend& b, float* buf)
{
    double l = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.leftPosition; }), 0.0, 1.0);
    double r = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.rightPosition; }), 0.0, 1.0);
    if (l > r)
        std::swap (l, r);
    const int shape = static_cast<int> (std::floor (s.keys[static_cast<size_t> (b.idx[1])]->windowShape + 0.5f));
    for (int i = 0; i < N; ++i)
    {
        const double x = static_cast<double> (i) / N;
        const double w = (r - l > 1.0e-6 && x >= l && x <= r) ? windowShapeValue (shape, (x - l) / (r - l)) : 0.0;
        buf[i] = static_cast<float> (buf[i] * w);
    }
}

void doFrequencyFilter (const Component& c, const CompState& s, const Blend& b, float* buf)
{
    const double cutoff = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.cutoff; }), 0.0, 1.0);
    const double shape = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.shape; }), 0.0, 1.0);
    const int style = s.keys[static_cast<size_t> (b.idx[1])]->style;
    double inPeak = 0.0;
    for (int i = 0; i < N; ++i)
        inPeak = std::max (inPeak, static_cast<double> (std::fabs (buf[i])));
    std::vector<Cx> sp;
    toSpectrum (buf, sp);
    // SPEC-GAP: cutoff / shape / style are not defined in the design. Defined: the cutoff is a harmonic number 2^(10 * cutoff) (1 .. 1024),
    // shape sets the steepness of a logistic gain over the distance in octaves (1 + 20 * shape), style 0 low pass, 1 band pass,
    // 2 high pass, 3 band reject.
    const double centre = cutoff * 10.0, k = 1.0 + 20.0 * shape;
    for (int n = 1; n < N / 2; ++n)
    {
        const double d = std::log2 (static_cast<double> (n)) - centre;
        const double lp = 1.0 / (1.0 + std::exp (clampd (k * d, -60.0, 60.0)));
        const double bp = std::exp (-std::pow (clampd (d * k * 0.25, -30.0, 30.0), 2.0));
        double g = lp;
        if (style == 1) g = bp;
        else if (style == 2) g = 1.0 - lp;
        else if (style >= 3) g = 1.0 - bp;
        sp[static_cast<size_t> (n)] *= g;
    }
    {
        const double dc = style == 0 ? 1.0 : (style == 3 ? 1.0 : 0.0);
        sp[0] *= dc;
        sp[N / 2] *= (style == 0 ? 0.0 : (style == 3 ? 1.0 : 1.0));
    }
    fromSpectrum (sp, buf);
    if (c.normalize && inPeak > 1.0e-9)
    {
        double peak = 0.0;
        for (int i = 0; i < N; ++i)
            peak = std::max (peak, static_cast<double> (std::fabs (buf[i])));
        if (peak > 1.0e-9)
            for (int i = 0; i < N; ++i)
                buf[i] = static_cast<float> (buf[i] * (inPeak / peak));
    }
}

void doSlewLimiter (const CompState& s, const Blend& b, float* buf)
{
    // SPEC-GAP: "up_run_rise" / "down_run_rise" are not defined. Defined: v in 0..1, largest step per sample = 2 * 2^(-10 v), so 0 does
    // not limit and 1 allows 2 / 1024 per sample. Two passes over the cycle make the result periodic.
    const double up = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.upRunRise; }), 0.0, 1.0);
    const double down = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.downRunRise; }), 0.0, 1.0);
    const double lu = 2.0 * std::exp2 (-10.0 * up), ld = 2.0 * std::exp2 (-10.0 * down);
    std::vector<float> in (buf, buf + N);
    double y = in[0];
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < N; ++i)
        {
            double d = in[static_cast<size_t> (i)] - y;
            d = std::min (d, lu);
            d = std::max (d, -ld);
            y += d;
            if (pass == 1)
                buf[i] = static_cast<float> (y);
        }
}

void doWaveFolder (const CompState& s, const Blend& b, float* buf)
{
    // SPEC-GAP: gain mapping of fold_boost. Defined: gain = 1 + 9 * boost (boost clamped to 0 .. 4), triangle fold into -1 .. 1.
    const double boost = clampd (blendScalar (b, s.keys, [] (const Keyframe& k) { return k.foldBoost; }), 0.0, 4.0);
    const double g = 1.0 + 9.0 * boost;
    for (int i = 0; i < N; ++i)
    {
        double t = (buf[i] * g + 1.0) / 4.0;
        t -= std::floor (t);
        buf[i] = static_cast<float> (1.0 - std::fabs (4.0 * t - 2.0));
    }
}

void doWaveWarp (const CompState& s, const Blend& b, float* buf)
{
    // SPEC-GAP: warp curve definitions. Defined: pwr(t, p) = (e^(p t) - 1) / (e^p - 1). Horizontal: symmetric = each half cycle warped
    // towards its own centre, asymmetric = the whole cycle. Vertical: symmetric = on |y| keeping the sign, asymmetric = on (y + 1) / 2.
    const double hp = blendScalar (b, s.keys, [] (const Keyframe& k) { return k.horizontalPower; });
    const double vp = blendScalar (b, s.keys, [] (const Keyframe& k) { return k.verticalPower; });
    const Keyframe& kf = *s.keys[static_cast<size_t> (b.idx[1])];
    std::vector<float> in (buf, buf + N);
    for (int i = 0; i < N; ++i)
    {
        const double x = static_cast<double> (i) / N;
        double xw;
        if (kf.horizontalAsymmetric)
            xw = powerCurve (x, hp);
        else
            xw = x < 0.5 ? 0.5 * powerCurve (2.0 * x, hp) : 1.0 - 0.5 * powerCurve (2.0 * (1.0 - x), hp);
        const double y = sampleAt (in.data(), xw * N);
        double yw;
        if (kf.verticalAsymmetric)
            yw = 2.0 * powerCurve ((clampd (y, -1.0, 1.0) + 1.0) * 0.5, vp) - 1.0;
        else
            yw = (y < 0 ? -1.0 : 1.0) * powerCurve (std::min (std::fabs (y), 1.0), vp);
        buf[i] = static_cast<float> (yw);
    }
}

uint64_t opsOf (ComponentType t, bool freq) noexcept
{
    const uint64_t fftOps = static_cast<uint64_t> (N) * 40;
    switch (t)
    {
        case ComponentType::waveSource:        return static_cast<uint64_t> (N) * 6 + (freq ? 2 * fftOps : 0);
        case ComponentType::audioFileSource:   return static_cast<uint64_t> (N) * 30 + 2 * fftOps;
        case ComponentType::shepardToneSource: return static_cast<uint64_t> (N) * 11 * 8;
        case ComponentType::phaseShift:
        case ComponentType::frequencyFilter:   return 2 * fftOps + static_cast<uint64_t> (N) * 20;
        default:                               return static_cast<uint64_t> (N) * 12;
    }
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
constexpr const char* kTypeNames[] = { "Wave Source", "Line Source", "Audio File Source", "Shepard Tone Source", "Phase Shift",
                                       "Wave Window", "Frequency Filter", "Slew Limiter", "Wave Folder", "Wave Warp" };
}

const char* componentTypeName (ComponentType type) noexcept { return kTypeNames[static_cast<int> (type)]; }

bool componentTypeFromName (const char* name, ComponentType& out) noexcept
{
    if (name == nullptr)
        return false;
    for (int i = 0; i < 10; ++i)
        if (std::strcmp (name, kTypeNames[i]) == 0)
        {
            out = static_cast<ComponentType> (i);
            return true;
        }
    return false;
}

void renderLine (const Line& line, float* out, int n) noexcept
{
    const size_t np = std::min (line.x.size(), line.y.size());
    if (np == 0 || n <= 0)
    {
        for (int i = 0; i < n; ++i)
            out[i] = 0.0f;
        return;
    }
    // SPEC-GAP: the point semantics are only fixed "by black-box golden" (design 1.3.2): defined here as y held before the first and
    // after the last point; between points y0 + (y1 - y0) * s with s the power curve of the segment's power; `smooth` additionally
    // eases s with smoothstep. x is made non-decreasing on the fly, y and x clamped to 0 .. 1.
    size_t seg = 0;
    for (int i = 0; i < n; ++i)
    {
        const double x = static_cast<double> (i) / n;
        const auto px = [&] (size_t j) { return clampd (line.x[j], 0.0, 1.0); };
        const auto py = [&] (size_t j) { return clampd (line.y[j], 0.0, 1.0); };
        double y;
        if (x <= px (0))
            y = py (0);
        else if (x >= px (np - 1))
            y = py (np - 1);
        else
        {
            while (seg + 1 < np - 1 && px (seg + 1) <= x)
                ++seg;
            double x0 = px (seg), x1 = px (seg + 1);
            if (x1 < x0)
                x1 = x0;
            const double span = x1 - x0;
            double s = span > 1.0e-12 ? (x - x0) / span : 1.0;
            const double p = seg < line.power.size() ? line.power[seg] : 0.0;
            s = powerCurve (s, p);
            if (line.smooth)
                s = s * s * (3.0 - 2.0 * s);
            y = py (seg) + (py (seg + 1) - py (seg)) * s;
        }
        out[i] = static_cast<float> (y);
    }
}

int frameCount (const Description& d) noexcept
{
    int last = 0;
    for (const auto& g : d.groups)
        for (const auto& c : g.components)
            for (const auto& k : c.keyframes)
                last = std::max (last, std::min (std::max (k.position, 0), kMaxPosition));
    return last + 1;
}

Status render (const Description& d, const Options& o, Result& result) noexcept
{
    try
    {
        const int frames = frameCount (d);
        result.numFrames = frames;
        result.frames.assign (static_cast<size_t> (frames) * N, 0.0f);

        // prepare all components once
        std::vector<std::vector<CompState>> states (d.groups.size());
        uint64_t ops = 0;
        for (size_t g = 0; g < d.groups.size(); ++g)
        {
            states[g].resize (d.groups[g].components.size());
            for (size_t c = 0; c < d.groups[g].components.size(); ++c)
            {
                prepare (d.groups[g].components[c], states[g][c]);
                if (d.groups[g].components[c].frequencyInterpolation)
                    ops += d.groups[g].components[c].keyframes.size() * N * 40ull;
            }
        }

        std::vector<float> buf (N), acc (N);
        for (int f = 0; f < frames; ++f)
        {
            std::fill (acc.begin(), acc.end(), 0.0f);
            for (size_t g = 0; g < d.groups.size(); ++g)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (size_t ci = 0; ci < d.groups[g].components.size(); ++ci)
                {
                    const Component& c = d.groups[g].components[ci];
                    const CompState& s = states[g][ci];
                    if (o.cancel && o.cancel())
                        return Status::cancelled;
                    ops += opsOf (c.type, c.frequencyInterpolation);
                    if (ops > o.maxOps)
                        return Status::budgetExceeded;
                    if (s.keys.empty() && c.type != ComponentType::shepardToneSource)
                        continue; // a component without keyframes does nothing
                    const Blend b = makeBlend (s.keys, f, c.interpolationStyle);
                    switch (c.type)
                    {
                        case ComponentType::waveSource:        doWaveSource (c, s, b, buf.data()); break;
                        case ComponentType::lineSource:        doLineSource (c, s, b, buf.data()); break;
                        case ComponentType::audioFileSource:   doAudioFile (c, s, b, buf.data()); break;
                        case ComponentType::shepardToneSource: doShepard (s, f, buf.data()); break;
                        case ComponentType::phaseShift:        doPhaseShift (s, b, buf.data()); break;
                        case ComponentType::waveWindow:        doWaveWindow (s, b, buf.data()); break;
                        case ComponentType::frequencyFilter:   doFrequencyFilter (c, s, b, buf.data()); break;
                        case ComponentType::slewLimiter:       doSlewLimiter (s, b, buf.data()); break;
                        case ComponentType::waveFolder:        doWaveFolder (s, b, buf.data()); break;
                        case ComponentType::waveWarp:          doWaveWarp (s, b, buf.data()); break;
                    }
                }
                for (int i = 0; i < N; ++i)
                    acc[static_cast<size_t> (i)] += buf[static_cast<size_t> (i)];
            }
            std::memcpy (result.frames.data() + static_cast<size_t> (f) * N, acc.data(), sizeof (float) * N);
        }

        // post-process. SPEC-GAP: the design names `remove_all_dc` and `full_normalize` without formulas. Defined: remove_all_dc subtracts the
        // mean of every frame; full_normalize scales the whole table by one factor so that its largest absolute sample is 1.
        if (d.removeAllDc)
            for (int f = 0; f < frames; ++f)
            {
                float* p = result.frames.data() + static_cast<size_t> (f) * N;
                double mean = 0.0;
                for (int i = 0; i < N; ++i)
                    mean += p[i];
                mean /= N;
                for (int i = 0; i < N; ++i)
                    p[i] = static_cast<float> (p[i] - mean);
            }
        if (d.fullNormalize)
        {
            float peak = 0.0f;
            for (float v : result.frames)
                peak = std::max (peak, std::fabs (v));
            if (peak > 1.0e-9f)
                for (float& v : result.frames)
                    v /= peak;
        }
        for (float& v : result.frames)
            if (! std::isfinite (v))
                v = 0.0f;
        return Status::ok;
    }
    catch (const std::bad_alloc&)
    {
        result.numFrames = 0;
        result.frames.clear();
        return Status::budgetExceeded;
    }
}
} // namespace wtrender
} // namespace synthyboy
