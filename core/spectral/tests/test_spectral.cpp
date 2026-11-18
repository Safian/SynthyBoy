// SynthyBoy - core/spectral tests: FFT, analysis, resynthesis (pitch, scan, level), filter, band, warps, determinism, bounds.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/spectral/spectral.h"
#include "synthyboy/spectral/kernels.h"
#include "synthyboy/sampleosc/sample_data.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "test_runner.h"

// (the reference loops of the kernel tests below must not be fused either: the kernels are compiled without fused multiply-add)
#if defined(__clang__)
 #pragma clang fp contract(off)
#elif defined(__GNUC__)
 #pragma GCC optimize ("fp-contract=off")
#endif

using namespace synthyboy::spectral;
using synthyboy::sampleosc::SampleData;

namespace
{
Scratch gScratch;
constexpr double kPi = 3.14159265358979323846;
constexpr double kSr = 48000.0;

struct Tone
{
    SampleData data;
    Analysis analysis;
    Tone (const std::vector<float>& samples)
    {
        data.loadFromFrames (samples.data(), nullptr, static_cast<int64_t> (samples.size()), kSr);
        analysis.build (data.view());
    }
};

std::vector<float> sine (double hz, double seconds, float amp = 0.5f)
{
    std::vector<float> v (static_cast<size_t> (seconds * kSr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = amp * static_cast<float> (std::sin (2.0 * kPi * hz * static_cast<double> (i) / kSr));
    return v;
}

std::vector<float> rich (double hz, double seconds)
{
    std::vector<float> v (static_cast<size_t> (seconds * kSr), 0.0f);
    for (int h = 1; h <= 24; ++h)
        for (size_t i = 0; i < v.size(); ++i)
            v[i] += 0.3f / static_cast<float> (h) * static_cast<float> (std::sin (2.0 * kPi * hz * h * static_cast<double> (i) / kSr + 0.3 * h));
    return v;
}

std::vector<float> render (SpectralVoice& v, const SpectralParams& p, int frames, int block = 256, std::vector<float>* right = nullptr)
{
    std::vector<float> out, l (static_cast<size_t> (block)), r (static_cast<size_t> (block));
    for (int done = 0; done < frames; done += block)
    {
        v.process (p, l.data(), r.data(), block);
        out.insert (out.end(), l.begin(), l.end());
        if (right != nullptr)
            right->insert (right->end(), r.begin(), r.end());
    }
    return out;
}

double rms (const std::vector<float>& x, size_t from = 0, size_t to = static_cast<size_t> (-1))
{
    double s = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (std::max<size_t> (1, to - from)));
}

double crossingHz (const std::vector<float>& x, size_t from, size_t to)
{
    int n = 0;
    for (size_t i = from + 1; i < to; ++i)
        n += (x[i - 1] < 0.0f) != (x[i] < 0.0f) ? 1 : 0;
    return 0.5 * n * kSr / static_cast<double> (to - from);
}

SpectralParams frozen (float position = 0.5f)
{
    SpectralParams p;
    p.scratch = &gScratch;
    p.hostRate = kSr;
    p.manual = true;
    p.position = position;
    return p;
}

/** The magnitude spectrum of x[from, from + 2048) with a Hann window. */
std::vector<float> spectrumOf (const std::vector<float>& x, size_t from)
{
    std::vector<float> re (kFftSize), im (kFftSize, 0.0f);
    for (int n = 0; n < kFftSize; ++n)
        re[static_cast<size_t> (n)] = x[from + static_cast<size_t> (n)] * static_cast<float> (0.5 - 0.5 * std::cos (2.0 * kPi * n / kFftSize));
    Fft::instance().forward (re.data(), im.data());
    std::vector<float> m (kBins);
    for (int k = 0; k < kBins; ++k)
        m[static_cast<size_t> (k)] = std::sqrt (re[static_cast<size_t> (k)] * re[static_cast<size_t> (k)] + im[static_cast<size_t> (k)] * im[static_cast<size_t> (k)]);
    return m;
}
} // namespace

TEST (spectral_fft_round_trip_and_a_sine_lands_in_its_bin)
{
    std::vector<float> re (kFftSize), im (kFftSize, 0.0f), original;
    for (int n = 0; n < kFftSize; ++n)
        re[static_cast<size_t> (n)] = static_cast<float> (std::sin (2.0 * kPi * 100.0 * n / kFftSize) + 0.25 * std::cos (2.0 * kPi * 7.0 * n / kFftSize));
    original = re;
    Fft::instance().forward (re.data(), im.data());
    float peak = 0.0f;
    int where = 0;
    for (int k = 0; k < kBins; ++k)
    {
        const float m = std::sqrt (re[static_cast<size_t> (k)] * re[static_cast<size_t> (k)] + im[static_cast<size_t> (k)] * im[static_cast<size_t> (k)]);
        if (m > peak) { peak = m; where = k; }
    }
    CHECK_EQ (where, 100);
    CHECK_NEAR (peak, kFftSize / 2.0, 1.0);
    Fft::instance().inverse (re.data(), im.data());
    double worst = 0.0;
    for (int n = 0; n < kFftSize; ++n)
        worst = std::max (worst, static_cast<double> (std::fabs (re[static_cast<size_t> (n)] - original[static_cast<size_t> (n)])));
    CHECK (worst < 1.0e-4);
}

TEST (spectral_analysis_finds_the_frequency_of_a_partial_between_two_bins)
{
    Tone tone (sine (1000.0, 1.0));
    CHECK (tone.analysis.valid() && ! tone.analysis.truncated());
    CHECK_EQ (tone.analysis.channels(), 1);
    CHECK (tone.analysis.numFrames() > 80);
    const int frame = 40;
    const float* m = tone.analysis.magnitude (0, frame);
    const float* f = tone.analysis.frequency (0, frame);
    int where = 0;
    for (int k = 1; k < kBins; ++k)
        if (m[k] > m[where])
            where = k;
    const double hz = static_cast<double> (f[where]) * kSr / (2.0 * kPi);
    CHECK_NEAR (hz, 1000.0, 0.5); // the instantaneous frequency is far finer than the bin width of 23 Hz
    CHECK (! tone.analysis.transient (frame));
    // a silent sample, a too short sample
    Analysis none;
    synthyboy::sampleosc::SampleView invalid;
    CHECK (! none.build (invalid));
}

TEST (spectral_resynthesis_keeps_pitch_and_level_and_starts_at_full_level)
{
    Tone tone (sine (1000.0, 2.0, 0.5f));
    SpectralVoice v;
    v.prepare (kSr);
    SpectralParams p = frozen();
    v.noteOn (&tone.analysis, p, 0.0f);
    const auto out = render (v, p, 24000);
    CHECK (rms (out, 0, 512) > 0.3 * 0.5 / std::sqrt (2.0)); // the first hop is not a fade in
    CHECK_NEAR (rms (out, 4000, 20000), 0.5 / std::sqrt (2.0), 0.05);
    CHECK_NEAR (crossingHz (out, 4000, 20000), 1000.0, 15.0);
    for (double ratio : { 0.5, 2.0 })
    {
        SpectralVoice w;
        w.prepare (kSr);
        SpectralParams q = frozen();
        q.pitchRatio = static_cast<float> (ratio);
        w.noteOn (&tone.analysis, q, 0.0f);
        const auto shifted = render (w, q, 24000);
        CHECK_NEAR (crossingHz (shifted, 4000, 20000) / (1000.0 * ratio), 1.0, 0.03);
        CHECK (rms (shifted, 4000, 20000) > 0.15);
    }
}

TEST (spectral_scan_moves_the_head_and_manual_places_it)
{
    std::vector<float> two = sine (300.0, 1.0);
    const auto second = sine (900.0, 1.0);
    two.insert (two.end(), second.begin(), second.end());
    Tone tone (two);
    for (const auto& c : { std::pair<float, double> { 0.2f, 300.0 }, std::pair<float, double> { 0.8f, 900.0 } })
    {
        SpectralVoice v;
        v.prepare (kSr);
        SpectralParams p = frozen (c.first);
        v.noteOn (&tone.analysis, p, 0.0f);
        const auto out = render (v, p, 20000);
        CHECK_NEAR (crossingHz (out, 4000, 18000), c.second, c.second * 0.04);
    }
    // a scan at the original speed moves from the first tone to the second
    SpectralVoice v;
    v.prepare (kSr);
    SpectralParams p;
    p.scratch = &gScratch;
    p.hostRate = kSr;
    p.scanSpeed = 1.0f;
    v.noteOn (&tone.analysis, p, 0.0f);
    const auto out = render (v, p, static_cast<int> (1.9 * kSr), 512);
    CHECK_NEAR (crossingHz (out, 12000, 36000), 300.0, 20.0);
    CHECK_NEAR (crossingHz (out, 60000, 84000), 900.0, 45.0);
    CHECK_NEAR (v.playhead(), 0.95, 0.05);
    // a frozen head does not move
    SpectralParams z = frozen (0.4f);
    SpectralVoice f;
    f.prepare (kSr);
    f.noteOn (&tone.analysis, z, 0.0f);
    render (f, z, 20000);
    CHECK_NEAR (f.playhead(), 0.4, 1.0e-6);
}

TEST (spectral_filter_gains_follow_the_drawn_curve_shift_and_mix)
{
    std::vector<float> gains (kBins);
    // a notch at the middle of the axis (about 632 Hz): 0.5 = unity everywhere else
    const float x[] = { 0.0f, 0.45f, 0.5f, 0.55f, 1.0f }, y[] = { 0.5f, 0.5f, 0.0f, 0.5f, 0.5f };
    buildFilterGains (gains.data(), x, y, 5, 0.0f, 1.0f, kSr / kFftSize);
    const int notch = static_cast<int> (std::lround (632.5 / (kSr / kFftSize)));
    CHECK (gains[static_cast<size_t> (notch)] < 0.05f);
    CHECK_NEAR (gains[static_cast<size_t> (notch * 4)], 1.0, 1.0e-3);
    CHECK_NEAR (gains[3], 1.0, 1.0e-3);
    buildFilterGains (gains.data(), x, y, 5, 0.5f, 1.0f, kSr / kFftSize); // shifted up by a quarter of the axis (x 0.25 = about 2.5 kHz... the notch moves up)
    CHECK (gains[static_cast<size_t> (notch)] > 0.95f);
    CHECK (gains[152] < 0.1f); // about 3.6 kHz (x = 0.75), where the shifted notch is
    buildFilterGains (gains.data(), x, y, 5, 0.0f, 0.0f, kSr / kFftSize); // dry
    CHECK_NEAR (gains[static_cast<size_t> (notch)], 1.0, 1.0e-6);
    buildFilterGains (gains.data(), nullptr, nullptr, 0, 0.0f, 1.0f, kSr / kFftSize);
    CHECK_NEAR (gains[100], 1.0, 1.0e-9);
    // the voice applies the gains: a tone in the notch disappears
    Tone tone (sine (632.5, 1.5));
    buildFilterGains (gains.data(), x, y, 5, 0.0f, 1.0f, kSr / kFftSize);
    SpectralVoice a, b;
    a.prepare (kSr);
    b.prepare (kSr);
    SpectralParams open = frozen(), cut = frozen();
    cut.filterGain = gains.data();
    a.noteOn (&tone.analysis, open, 0.0f);
    b.noteOn (&tone.analysis, cut, 0.0f);
    CHECK (rms (render (b, cut, 20000), 4000, 18000) < 0.15 * rms (render (a, open, 20000), 4000, 18000));
}

TEST (spectral_band_markers_cut_the_spectrum_with_smooth_or_hard_edges)
{
    Tone tone (rich (220.0, 1.5));
    const auto energyAbove = [&] (bool smooth, float lo, float hi)
    {
        SpectralVoice v;
        v.prepare (kSr);
        SpectralParams p = frozen();
        p.freqLoHz = lo;
        p.freqHiHz = hi;
        p.bandSmooth = smooth;
        v.noteOn (&tone.analysis, p, 0.0f);
        const auto out = render (v, p, 20000);
        const auto m = spectrumOf (out, 8000);
        double inBand = 0.0, outBand = 0.0;
        for (int k = 1; k < kBins; ++k)
            (k * kSr / kFftSize >= lo && k * kSr / kFftSize <= hi ? inBand : outBand) += static_cast<double> (m[static_cast<size_t> (k)]) * m[static_cast<size_t> (k)];
        return std::sqrt (outBand / (inBand + 1.0e-9));
    };
    CHECK (energyAbove (false, 600.0f, 1500.0f) < 0.05);
    CHECK (energyAbove (true, 600.0f, 1500.0f) < 1.0 && energyAbove (true, 600.0f, 1500.0f) > energyAbove (false, 600.0f, 1500.0f));
    // the full range changes nothing: level equals the open voice
    SpectralVoice open, wide;
    open.prepare (kSr);
    wide.prepare (kSr);
    SpectralParams a = frozen(), b = frozen();
    b.freqLoHz = 20.0f;
    b.freqHiHz = 20000.0f;
    b.bandSmooth = false;
    open.noteOn (&tone.analysis, a, 0.0f);
    wide.noteOn (&tone.analysis, b, 0.0f);
    CHECK_NEAR (rms (render (open, a, 20000), 4000, 18000), rms (render (wide, b, 20000), 4000, 18000), 0.02);
}

TEST (spectral_every_warp_is_finite_bounded_deterministic_and_changes_the_sound)
{
    Tone tone (rich (220.0, 1.5));
    std::vector<float> external (kBins, 0.0f);
    for (int k = 0; k < kBins; ++k)
        external[static_cast<size_t> (k)] = k < 200 ? 100.0f : 1.0f;
    const auto run = [&] (int warp, float amount, float var)
    {
        SpectralVoice v;
        v.prepare (kSr);
        SpectralParams p = frozen();
        p.warpType[0] = warp;
        p.warpAmount[0] = amount;
        p.warpVar[0] = var;
        for (auto& e : p.external)
            e = external.data();
        v.noteOn (&tone.analysis, p, 0.0f);
        return render (v, p, 16000);
    };
    const auto base = run (-1, 0.0f, 0.5f);
    for (int w = 0; w < kNumSpectralWarps; ++w)
    {
        const auto a = run (w, 0.8f, 0.5f), b = run (w, 0.8f, 0.5f), c = run (w, 1.0f, 1.0f);
        bool finite = true;
        double diff = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            finite = finite && std::isfinite (a[i]) && std::isfinite (c[i]) && std::fabs (a[i]) <= 16.0f && std::fabs (c[i]) <= 16.0f;
            diff += std::fabs (static_cast<double> (a[i]) - base[i]);
        }
        if (! CHECK (finite && a == b && diff > 1.0e-2))
            std::printf ("    spectral warp %d: finite %d deterministic %d difference %g\n", w, finite ? 1 : 0, a == b ? 1 : 0, diff);
    }
    // the pitch shift warp moves the pitch by its amount (0.5 = none, 1 = +12 st)
    Tone steady (sine (500.0, 1.5));
    SpectralVoice v;
    v.prepare (kSr);
    SpectralParams p = frozen();
    p.warpType[0] = static_cast<int> (Warp::pitchShift);
    p.warpAmount[0] = 1.0f;
    v.noteOn (&steady.analysis, p, 0.0f);
    CHECK_NEAR (crossingHz (render (v, p, 20000), 4000, 18000), 1000.0, 30.0);
}

TEST (spectral_phase_lock_transients_shift_and_extreme_settings_stay_bounded)
{
    // a tone with a click: the transient flag, the reset
    std::vector<float> s = sine (400.0, 1.5, 0.3f);
    for (size_t i = 40000; i < 40200; ++i)
        s[i] += 0.9f * static_cast<float> (std::sin (2.0 * kPi * 3000.0 * static_cast<double> (i) / kSr));
    Tone tone (s);
    bool anyTransient = false;
    for (int t = 0; t < tone.analysis.numFrames(); ++t)
        anyTransient = anyTransient || tone.analysis.transient (t);
    CHECK (anyTransient);
    for (int flags = 0; flags < 4; ++flags)
    {
        SpectralVoice v;
        v.prepare (kSr);
        SpectralParams p;
        p.scratch = &gScratch;
        p.hostRate = kSr;
        p.phaseLock = (flags & 1) != 0;
        p.transients = (flags & 2) != 0;
        p.scanSpeed = 1.0f;
        v.noteOn (&tone.analysis, p, 0.0f);
        const auto out = render (v, p, 72000);
        for (float x : out)
            CHECK (std::isfinite (x) && std::fabs (x) < 16.0f);
        CHECK (rms (out) > 0.05);
    }
    // extreme
    Tone rich24 (rich (110.0, 1.5));
    SpectralVoice v;
    v.prepare (44100.0);
    SpectralParams p;
    p.scratch = &gScratch;
    p.hostRate = 44100.0;
    p.pitchRatio = 40.0f;
    p.freqShiftHz = -9000.0f;
    p.freqLoHz = 1.0f;
    p.freqHiHz = 1.0f;
    p.warpType[0] = static_cast<int> (Warp::spread);
    p.warpAmount[0] = 1.0f;
    p.warpType[1] = static_cast<int> (Warp::addHarmonics);
    p.warpAmount[1] = 1.0f;
    p.scanSpeed = 8.0f;
    p.loopMode = synthyboy::sampleosc::LoopMode::pingPong;
    p.loopLo = 0.2f;
    p.loopHi = 0.7f;
    v.noteOn (&rich24.analysis, p, 1.0f);
    std::vector<float> l (333), r (333);
    for (int i = 0; i < 60; ++i)
    {
        v.process (p, l.data(), r.data(), 333);
        for (int k = 0; k < 333; ++k)
            CHECK (std::isfinite (l[k]) && std::fabs (l[k]) <= 16.0f && std::isfinite (r[k]));
    }
    // block size does not change the sound (the voice works in hops)
    const auto run = [&] (int block)
    {
        SpectralVoice w;
        w.prepare (kSr);
        SpectralParams q;
        q.scratch = &gScratch;
        q.hostRate = kSr;
        q.scanSpeed = 0.5f;
        q.pitchRatio = 1.3f;
        w.noteOn (&tone.analysis, q, 0.0f);
        return render (w, q, 8192, block);
    };
    const auto a = run (64), b = run (1024);
    CHECK (a == b);
}

TEST (spectral_one_shot_ends_and_a_stereo_sample_keeps_two_channels)
{
    Tone tone (sine (700.0, 0.4));
    SpectralVoice v;
    v.prepare (kSr);
    SpectralParams p;
    p.scratch = &gScratch;
    p.hostRate = kSr;
    p.scanSpeed = 2.0f;
    v.noteOn (&tone.analysis, p, 0.0f);
    render (v, p, 48000);
    CHECK (! v.isActive());
    // stereo: different tones in the two channels
    std::vector<float> l = sine (500.0, 1.0), r = sine (1500.0, 1.0);
    SampleData data;
    data.loadFromFrames (l.data(), r.data(), static_cast<int64_t> (l.size()), kSr);
    Analysis analysis;
    CHECK (analysis.build (data.view()));
    CHECK_EQ (analysis.channels(), 2);
    SpectralVoice w;
    w.prepare (kSr);
    SpectralParams q = frozen();
    w.noteOn (&analysis, q, 0.0f);
    std::vector<float> right;
    const auto left = render (w, q, 20000, 256, &right);
    CHECK_NEAR (crossingHz (left, 4000, 18000), 500.0, 15.0);
    CHECK_NEAR (crossingHz (right, 4000, 18000), 1500.0, 40.0);
}

TEST (spectral_display_spectrum_is_what_the_voice_synthesizes)
{
    // computeMagnitudeSpectrum (the oscillator display) runs the very code the voice runs for a hop (shapeFrame): the output magnitudes of the display and the ones the voice left in its
    // scratch memory after a hop at the same analysis frame are the same numbers, bit for bit, for every kind of setting that changes the picture
    const Tone tone (rich (220.0, 1.0));
    std::vector<float> gains (kBins, 1.0f);
    for (int k = 0; k < kBins; ++k)
        gains[static_cast<size_t> (k)] = 0.2f + 0.8f * static_cast<float> (k % 7) / 6.0f;
    struct Case { const char* name; float pitch, shift, lo, hi; bool smooth, post; int warp; float amount; bool filter; };
    const Case cases[] = {
        { "plain", 1.0f, 0.0f, 20.0f, 20000.0f, false, false, -1, 0.0f, false },
        { "pitch up a fifth", 1.4983f, 0.0f, 20.0f, 20000.0f, false, false, -1, 0.0f, false },
        { "pitch down an octave and a shift", 0.5f, 150.0f, 20.0f, 20000.0f, false, false, -1, 0.0f, false },
        { "band 300 .. 3000 Hz hard", 1.0f, 0.0f, 300.0f, 3000.0f, false, false, -1, 0.0f, false },
        { "band 300 .. 3000 Hz smooth, after the warp", 1.3f, 0.0f, 300.0f, 3000.0f, true, true, -1, 0.0f, false },
        { "the spectral filter", 1.0f, 0.0f, 20.0f, 20000.0f, false, false, -1, 0.0f, true },
        { "a warp", 1.0f, 0.0f, 20.0f, 20000.0f, false, false, 0, 0.7f, false },
        { "another warp with a pitch change", 1.2f, 40.0f, 100.0f, 9000.0f, true, false, 2, 0.5f, true },
    };
    for (const Case& c : cases)
    {
        SpectralParams p = frozen (0.5f);
        p.pitchRatio = c.pitch;
        p.freqShiftHz = c.shift;
        p.freqLoHz = c.lo;
        p.freqHiHz = c.hi;
        p.bandSmooth = c.smooth;
        p.bandPostWarp = c.post;
        p.warpType[0] = c.warp;
        p.warpAmount[0] = c.amount;
        p.filterGain = c.filter ? gains.data() : nullptr;
        SpectralVoice v;
        v.prepare (kSr);
        v.noteOn (&tone.analysis, p, 0.0f);
        render (v, p, 4096);
        static Scratch display;
        float shown[kBins];
        const double frame = 0.5 * static_cast<double> (tone.analysis.fileFrames()) / kHop;   // where the voice's head stands: the frozen position
        computeMagnitudeSpectrum (tone.analysis, p, frame, shown, display);
        bool same = true, any = false;
        for (int k = 0; k < kBins; ++k)
        {
            same = same && shown[k] == gScratch.om[k];
            any = any || shown[k] > 1.0e-4f;
        }
        CHECK (any);
        if (! same)
            std::printf ("  case '%s': the display's spectrum is not the voice's\n", c.name);
        CHECK (same);
    }
}

// ---- the kernels of a frame (synthyboy/spectral/kernels.h) -------------------------------------------------------------------------------

namespace
{
struct KRng
{
    uint32_t s;
    explicit KRng (uint32_t seed) : s (seed * 2654435761u + 17u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return uni() * 2.0f - 1.0f; }
};
bool sameBits (const float* a, const float* b, size_t n) { return std::memcmp (a, b, n * sizeof (float)) == 0; }
}

TEST (spectral_real_inverse_transform_matches_the_full_transform_and_the_vector_form_matches_the_scalar_form)
{
    using namespace synthyboy::spectral::kernels;
    KRng rng (3);
    for (int round = 0; round < 20; ++round)
    {
        std::vector<float> xr (kBins), xi (kBins);
        for (int k = 0; k < kBins; ++k)
        {
            const float scale = round % 3 == 0 ? 1.0f : (round % 3 == 1 ? 1.0f / (1.0f + static_cast<float> (k)) : (k % 37 == 0 ? 50.0f : 0.001f));
            xr[static_cast<size_t> (k)] = rng.bi() * scale;
            xi[static_cast<size_t> (k)] = k == 0 || k == kBins - 1 ? 0.0f : rng.bi() * scale;
        }
        // the full transform of the mirrored spectrum
        std::vector<float> re (kFftSize), im (kFftSize);
        for (int k = 0; k < kBins; ++k)
        {
            re[static_cast<size_t> (k)] = xr[static_cast<size_t> (k)];
            im[static_cast<size_t> (k)] = xi[static_cast<size_t> (k)];
        }
        for (int j = 1; j < kFftSize / 2; ++j)
        {
            re[static_cast<size_t> (kFftSize - j)] = re[static_cast<size_t> (j)];
            im[static_cast<size_t> (kFftSize - j)] = -im[static_cast<size_t> (j)];
        }
        Fft::instance().inverse (re.data(), im.data());
        std::vector<float> zr (kFftSize / 2), zi (kFftSize / 2), nr (kFftSize / 2), ni (kFftSize / 2);
        RealIfft::instance().inverse (xr.data(), xi.data(), zr.data(), zi.data(), false);
        double peak = 0.0, worst = 0.0;
        for (int n = 0; n < kFftSize; ++n)
        {
            const float mine = (n % 2 == 0 ? zr[static_cast<size_t> (n / 2)] : zi[static_cast<size_t> (n / 2)]) * (1.0f / kFftSize);
            peak = std::max (peak, static_cast<double> (std::fabs (re[static_cast<size_t> (n)])));
            worst = std::max (worst, static_cast<double> (std::fabs (mine - re[static_cast<size_t> (n)])));
        }
        CHECK (worst <= 3.0e-6 * peak);
        if (haveNeon())
        {
            RealIfft::instance().inverse (xr.data(), xi.data(), nr.data(), ni.data(), true);
            CHECK (sameBits (zr.data(), nr.data(), zr.size()) && sameBits (zi.data(), ni.data(), zi.size()));
        }
    }
}

TEST (spectral_sine_cosine_are_accurate_and_the_vector_form_matches_the_scalar_form)
{
    using namespace synthyboy::spectral::kernels;
    KRng rng (5);
    std::vector<float> a (4099), s (4099), c (4099), sv (4099), cv (4099);
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = i < 2000 ? rng.bi() * 1000.0f : (i < 3000 ? rng.bi() * 7.0f : rng.bi() * 300.0f);
    a[0] = 0.0f; a[1] = 3.14159265f; a[2] = -3.14159265f; a[3] = 1.5707963f; a[4] = 100.0f; a[5] = -100.0f;
    sincosBlock (a.data(), s.data(), c.data(), static_cast<int> (a.size()), false);
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        worst = std::max (worst, std::fabs (static_cast<double> (s[i]) - std::sin (static_cast<double> (a[i]))));
        worst = std::max (worst, std::fabs (static_cast<double> (c[i]) - std::cos (static_cast<double> (a[i]))));
    }
    CHECK (worst < 2.0e-7);
    if (haveNeon())
    {
        sincosBlock (a.data(), sv.data(), cv.data(), static_cast<int> (a.size()), true);
        CHECK (sameBits (s.data(), sv.data(), a.size()) && sameBits (c.data(), cv.data(), a.size()));
    }
}

TEST (spectral_phase_wrap_is_the_old_fmod_exactly_and_the_single_precision_wrap_within_rounding)
{
    using namespace synthyboy::spectral::kernels;
    const auto old = [] (float x)
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        x = std::fmod (x, kTwoPi);
        while (x > 3.14159265f) x -= kTwoPi;
        while (x < -3.14159265f) x += kTwoPi;
        return x;
    };
    KRng rng (9);
    int bad = 0, n = 0;
    double worstFloat = 0.0;
    int notExact = 0;
    for (int i = 0; i < 400000; ++i)
    {
        const float range = i % 4 == 0 ? 120.0f : (i % 4 == 1 ? 2000.0f : (i % 4 == 2 ? 60000.0f : 300.0f));
        float x = rng.bi() * range;
        if (i % 50 == 0)
            x = std::copysign (6.28318530717958647692f * static_cast<float> (1 + i % 900), x);   // exact multiples of the float 2 pi
        if (i % 50 == 1)
            x = std::copysign (3.14159265f * static_cast<float> (1 + i % 900), x);
        ++n;
        const float a = old (x), b = wrapPhase (x), c = wrapPhaseFloat (x);
        if (! (a == b))
            ++bad;
        // the single precision wrap (rounding errors of its differences kept): the same value, bit for bit
        if (! (a == c))
            ++notExact;
        worstFloat = std::max (worstFloat, std::fabs (static_cast<double> (a) - c));
    }
    CHECK (n > 0);
    CHECK_EQ (bad, 0);
    CHECK_EQ (notExact, 0);
    CHECK (worstFloat == 0.0);
    // the phase update loop: the scalar form is wrapPhaseFloat (), the vector form gives its bits; both the old loop's values
    std::vector<float> phase (kBins), step (kBins), expect (kBins), oldResult (kBins), vec (kBins);
    for (int k = 0; k < kBins; ++k)
    {
        phase[static_cast<size_t> (k)] = rng.bi() * 3.2f;
        step[static_cast<size_t> (k)] = rng.bi() * 0.2f * static_cast<float> (k % 40);
        float x = phase[static_cast<size_t> (k)] + step[static_cast<size_t> (k)] * kHop;
        oldResult[static_cast<size_t> (k)] = x;
        if (x > 100.0f || x < -100.0f)
            oldResult[static_cast<size_t> (k)] = old (x);
        float y = x;
        if (y > 100.0f || y < -100.0f)
            y = wrapPhaseFloat (y);
        expect[static_cast<size_t> (k)] = y;
    }
    vec = phase;
    const std::vector<float> start = phase;
    advancePhases (phase.data(), step.data(), kBins, false);
    CHECK (sameBits (phase.data(), expect.data(), phase.size()));
    if (haveNeon())
    {
        advancePhases (vec.data(), step.data(), kBins, true);
        CHECK (sameBits (vec.data(), expect.data(), vec.size()));
    }
    // the fused kernel (phase update, sine and cosine, the bins) gives the bits of the separate ones, in both forms
    {
        std::vector<float> mag (kBins), re1 (kBins), im1 (kBins), re2 (kBins), im2 (kBins), ph1 = start, ph2 = start, ph3 = start;
        for (auto& m : mag)
            m = rng.uni();
        advancePhases (ph1.data(), step.data(), kBins, false);
        spectrumFromAngles (ph1.data(), mag.data(), re1.data(), im1.data(), kBins, false);
        advanceToSpectrum (ph2.data(), step.data(), mag.data(), re2.data(), im2.data(), kBins, false);
        CHECK (sameBits (ph1.data(), ph2.data(), ph1.size()) && sameBits (re1.data(), re2.data(), re1.size()) && sameBits (im1.data(), im2.data(), im1.size()));
        if (haveNeon())
        {
            std::vector<float> re3 (kBins), im3 (kBins);
            advanceToSpectrum (ph3.data(), step.data(), mag.data(), re3.data(), im3.data(), kBins, true);
            CHECK (sameBits (ph1.data(), ph3.data(), ph1.size()) && sameBits (re1.data(), re3.data(), re1.size()) && sameBits (im1.data(), im3.data(), im1.size()));
            std::vector<float> re4 (kBins), im4 (kBins);
            spectrumFromAngles (ph1.data(), mag.data(), re4.data(), im4.data(), kBins, true);
            CHECK (sameBits (re1.data(), re4.data(), re1.size()) && sameBits (im1.data(), im4.data(), im1.size()));
        }
    }
    double worst = 0.0;
    for (size_t k = 0; k < phase.size(); ++k)
    {
        double d = std::fabs (static_cast<double> (oldResult[k]) - phase[k]);
        d = std::min (d, std::fabs (d - 6.283185307179586));
        worst = std::max (worst, d);
    }
    CHECK (worst == 0.0);
}

TEST (spectral_overlap_add_vector_form_matches_the_scalar_form)
{
    using namespace synthyboy::spectral::kernels;
    if (! haveNeon())
        return;
    KRng rng (11);
    std::vector<float> zr (kFftSize / 2), zi (kFftSize / 2);
    for (auto& v : zr) v = rng.bi();
    for (auto& v : zi) v = rng.bi();
    for (int at : { 0, 512, 1024, 1536 })
    {
        std::vector<float> a (kFftSize), b (kFftSize);
        for (size_t i = 0; i < a.size(); ++i) a[i] = b[i] = rng.bi();
        overlapAdd (a.data(), at, zr.data(), zi.data(), 1.0f / 1.5f, false);
        overlapAdd (b.data(), at, zr.data(), zi.data(), 1.0f / 1.5f, true);
        CHECK (sameBits (a.data(), b.data(), a.size()));
    }
}

// The voice with the block kernels against the voice with the reference code (setReferenceKernels): the frames agree to rounding. The first 2 s of every warp, with a mono and a stereo
// sample, Phase Lock, Transients and a moving head are compared; the difference is relative to the largest sample.
TEST (spectral_block_kernels_agree_with_the_reference_voice_to_rounding)
{
    Tone tone (rich (220.0, 1.5));
    std::vector<float> l (static_cast<size_t> (1.5 * kSr)), r (l.size());
    for (size_t i = 0; i < l.size(); ++i)
    {
        l[i] = static_cast<float> (0.4 * std::sin (2.0 * kPi * 311.0 * static_cast<double> (i) / kSr) + 0.1 * std::sin (0.01 * static_cast<double> (i * i % 1000)));
        r[i] = static_cast<float> (0.4 * std::sin (2.0 * kPi * 477.0 * static_cast<double> (i) / kSr));
    }
    SampleData stereoData;
    stereoData.loadFromFrames (l.data(), r.data(), static_cast<int64_t> (l.size()), kSr);
    Analysis stereo;
    stereo.build (stereoData.view());
    std::vector<float> external (kBins, 0.0f);
    for (int k = 0; k < kBins; ++k)
        external[static_cast<size_t> (k)] = k < 200 ? 100.0f : 1.0f;
    double worstRelative = 0.0;
    int runs = 0;
    for (int variant = 0; variant < 3; ++variant)
        for (int w = -1; w < kNumSpectralWarps; ++w)
        {
            static Scratch scratchA, scratchB;
            SpectralParams p = frozen();
            p.manual = variant == 0;
            p.scanSpeed = 0.4f;
            p.phaseLock = variant == 1;
            p.transients = variant == 2;
            p.warpType[0] = w;
            p.warpAmount[0] = 0.7f;
            p.warpVar[0] = 0.4f;
            p.pitchRatio = variant == 2 ? 1.7f : 0.8f;
            p.freqShiftHz = variant == 1 ? 30.0f : 0.0f;
            for (auto& e : p.external)
                e = external.data();
            SpectralVoice fast, reference;
            fast.prepare (kSr);
            reference.prepare (kSr);
            reference.setReferenceKernels (true);
            SpectralParams pa = p, pb = p;
            pa.scratch = &scratchA;
            pb.scratch = &scratchB;
            const Analysis* an = variant == 2 ? &stereo : &tone.analysis;
            fast.noteOn (an, pa, 0.0f);
            reference.noteOn (an, pb, 0.0f);
            const auto a = render (fast, pa, 24000), b = render (reference, pb, 24000);
            double peak = 0.0, worst = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
            {
                peak = std::max (peak, static_cast<double> (std::fabs (b[i])));
                worst = std::max (worst, static_cast<double> (std::fabs (a[i] - b[i])));
            }
            ++runs;
            worstRelative = std::max (worstRelative, worst / std::max (peak, 1.0e-9));
            if (! CHECK (worst <= 2.0e-5 * peak))
                std::printf ("    variant %d warp %d: largest difference %g (peak %g)\n", variant, w, worst, peak);
        }
    CHECK (runs > 60);
    std::printf ("    block kernels against the reference: largest difference over %d runs %.3g of the peak\n", runs, worstRelative);
}

// The per bin loops of the frame shaping: the vector forms give the bits of the scalar forms (peaks of random spectra with ties and flat parts, the max, the lerp, the products and the placing
// of regions with every offset).
TEST (spectral_per_bin_kernels_vector_forms_match_the_scalar_forms)
{
    using namespace synthyboy::spectral::kernels;
    if (! haveNeon())
        return;
    KRng rng (21);
    for (int round = 0; round < 200; ++round)
    {
        std::vector<float> m (kBins), a (kBins), b (kBins), o1 (kBins), o2 (kBins);
        for (int k = 0; k < kBins; ++k)
        {
            m[static_cast<size_t> (k)] = round % 3 == 0 ? std::floor (rng.uni() * 6.0f) * 0.25f : (round % 3 == 1 ? rng.uni() : (k % 5 == 0 ? rng.uni() * 100.0f : rng.uni() * 1.0e-6f));
            a[static_cast<size_t> (k)] = rng.bi();
            b[static_cast<size_t> (k)] = rng.bi();
        }
        const float top = maxBins (m.data(), kBins, false);
        CHECK (top == maxBins (m.data(), kBins, true));
        for (int cap : { 512, 7, 1 })
        {
            int p1[512], p2[512];
            const int n1 = findPeaks (m.data(), kBins, top, p1, cap, false), n2 = findPeaks (m.data(), kBins, top, p2, cap, true);
            bool same = n1 == n2;
            for (int i = 0; same && i < n1; ++i)
                same = p1[i] == p2[i];
            CHECK (same);
        }
        const float t = rng.uni();
        lerpBins (o1.data(), a.data(), b.data(), t, kBins - round % 5, false);
        lerpBins (o2.data(), a.data(), b.data(), t, kBins - round % 5, true);
        CHECK (sameBits (o1.data(), o2.data(), o1.size()));
        o1 = a;
        o2 = a;
        mulBins (o1.data(), b.data(), kBins - round % 7, false);
        mulBins (o2.data(), b.data(), kBins - round % 7, true);
        CHECK (sameBits (o1.data(), o2.data(), o1.size()));
        std::vector<float> om1 (kBins, 0.0f), of1 (kBins, 0.0f), oth1 (kBins, 0.0f);
        std::vector<float> om2 = om1, of2 = of1, oth2 = oth1;
        for (int r = 0; r < 20; ++r)
        {
            const int from = 1 + static_cast<int> (rng.uni() * 900.0f), len = static_cast<int> (rng.uni() * 60.0f), offset = static_cast<int> (rng.bi() * 40.0f);
            placeRegion (m.data(), a.data(), b.data(), om1.data(), of1.data(), oth1.data(), from, std::min (from + len, kBins - 1), offset, 1.37f, 0.02f, kBins, false);
            placeRegion (m.data(), a.data(), b.data(), om2.data(), of2.data(), oth2.data(), from, std::min (from + len, kBins - 1), offset, 1.37f, 0.02f, kBins, true);
        }
        CHECK (sameBits (om1.data(), om2.data(), om1.size()) && sameBits (of1.data(), of2.data(), of1.size()) && sameBits (oth1.data(), oth2.data(), oth1.size()));
    }
}

// placeAll () against the loop of the reference (std::lround, one bin at a time): the same bins, for dense and sparse peaks, shifts in both directions and ratios from 1 / 32 to 32.
TEST (spectral_place_all_equals_the_reference_loop)
{
    using namespace synthyboy::spectral::kernels;
    KRng rng (33);
    int bad = 0, runs = 0;
    for (int round = 0; round < 300; ++round)
    {
        std::vector<float> mag (kBins), freq (kBins), theta (kBins);
        for (int k = 0; k < kBins; ++k)
        {
            mag[static_cast<size_t> (k)] = rng.uni();
            freq[static_cast<size_t> (k)] = rng.bi() * 3.0f;
            theta[static_cast<size_t> (k)] = rng.bi() * 3.14f;
        }
        int peaks[512];
        const int numPeaks = findPeaks (mag.data(), kBins, 1.0f, peaks, 512, false);
        const double r = std::exp2 (static_cast<double> (rng.bi()) * 5.0);
        const double shiftBins = round % 3 == 0 ? 0.0 : static_cast<double> (rng.bi()) * 120.0;
        const float shiftRad = rng.bi() * 0.1f;
        std::vector<float> om1 (kBins, 0.0f), of1 (kBins, 0.0f), oth1 (kBins, 0.0f), om2 = om1, of2 = of1, oth2 = oth1, om3 = om1, of3 = of1, oth3 = oth1;
        for (int i = 0; i < numPeaks; ++i)
        {
            const int centre = peaks[i];
            const int from = i == 0 ? 1 : (peaks[i - 1] + centre + 1) / 2;
            const int to = i + 1 == numPeaks ? kBins - 1 : (centre + peaks[i + 1]) / 2;
            const int offset = static_cast<int> (std::lround (static_cast<double> (centre) * r + shiftBins)) - centre;
            for (int k = from; k <= to; ++k)
            {
                const int j = k + offset;
                if (j >= 1 && j < kBins && mag[static_cast<size_t> (k)] > om1[static_cast<size_t> (j)])
                {
                    om1[static_cast<size_t> (j)] = mag[static_cast<size_t> (k)];
                    of1[static_cast<size_t> (j)] = freq[static_cast<size_t> (k)] * static_cast<float> (r) + shiftRad;
                    oth1[static_cast<size_t> (j)] = theta[static_cast<size_t> (k)];
                }
            }
        }
        placeAll (mag.data(), freq.data(), theta.data(), om2.data(), of2.data(), oth2.data(), peaks, numPeaks, r, shiftBins, shiftRad, kBins, false);
        placeAll (mag.data(), freq.data(), theta.data(), om3.data(), of3.data(), oth3.data(), peaks, numPeaks, r, shiftBins, shiftRad, kBins, true);
        ++runs;
        if (! (sameBits (om1.data(), om2.data(), om1.size()) && sameBits (of1.data(), of2.data(), of1.size()) && sameBits (oth1.data(), oth2.data(), oth1.size())
               && sameBits (om1.data(), om3.data(), om1.size()) && sameBits (of1.data(), of3.data(), of1.size()) && sameBits (oth1.data(), oth3.data(), oth1.size())))
        {
            if (bad++ < 3)
                std::printf ("    placeAll differs: round %d peaks %d r %g shift %g scalar form %d vector form %d\n", round, numPeaks, r, shiftBins,
                             sameBits (om1.data(), om2.data(), om1.size()) && sameBits (of1.data(), of2.data(), of1.size()) && sameBits (oth1.data(), oth2.data(), oth1.size()) ? 1 : 0,
                             sameBits (om1.data(), om3.data(), om1.size()) && sameBits (of1.data(), of3.data(), of1.size()) && sameBits (oth1.data(), oth3.data(), oth1.size()) ? 1 : 0);
        }
    }
    CHECK (runs == 300);
    CHECK_EQ (bad, 0);
}
