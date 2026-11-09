// SynthyBoy - core/spectral tests: FFT, analysis, resynthesis (pitch, scan, level), filter, band, warps, determinism, bounds.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/spectral/spectral.h"
#include "synthyboy/sampleosc/sample_data.h"

#include <cmath>
#include <vector>

#include "test_runner.h"

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
