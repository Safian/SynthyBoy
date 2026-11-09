// SynthyBoy - core/granular tests: windows, density, pitch, scan, randomisation, loops, determinism, bounds.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/granular/granular.h"
#include "synthyboy/sampleosc/sample_data.h"

#include <cmath>
#include <vector>

#include "test_runner.h"

using namespace synthyboy::granular;
using synthyboy::sampleosc::SampleData;

namespace
{
constexpr double kPi = 3.14159265358979323846;

/** A 2 s stereo-less sine at `hz` (44.1 kHz), as SampleData (with the mip levels). */
struct Tone
{
    SampleData data;
    explicit Tone (double hz, double seconds = 2.0)
    {
        std::vector<float> s (static_cast<size_t> (44100.0 * seconds));
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = static_cast<float> (0.5 * std::sin (2.0 * kPi * hz * static_cast<double> (i) / 44100.0));
        data.loadFromFrames (s.data(), nullptr, static_cast<int64_t> (s.size()), 44100.0);
    }
};

std::vector<float> render (GranularVoice& v, const GrainParams& p, int frames, int block = 256, std::vector<float>* right = nullptr)
{
    std::vector<float> out, l (static_cast<size_t> (block)), r (static_cast<size_t> (block));
    for (int done = 0; done < frames; done += block)
    {
        v.process (p, l.data(), r.data(), block, nullptr);
        out.insert (out.end(), l.begin(), l.end());
        if (right != nullptr)
            right->insert (right->end(), r.begin(), r.end());
    }
    return out;
}

double rms (const std::vector<float>& x, size_t from = 0)
{
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (std::max<size_t> (1, x.size() - from)));
}

/** Frequency of the strongest component by counting zero crossings. */
double crossingHz (const std::vector<float>& x, size_t from, size_t to)
{
    int n = 0;
    for (size_t i = from + 1; i < to; ++i)
        n += (x[i - 1] < 0.0f) != (x[i] < 0.0f) ? 1 : 0;
    return 0.5 * n * 44100.0 / static_cast<double> (to - from);
}
} // namespace

TEST (granular_windows_are_bounded_and_shaped)
{
    for (int s = 0; s < kNumWindowShapes; ++s)
        for (float amount : { 0.0f, 0.5f, 1.0f })
            for (float skew : { -1.0f, 0.0f, 1.0f })
                for (int i = 0; i <= 64; ++i)
                {
                    const float w = windowValue (static_cast<WindowShape> (s), static_cast<float> (i) / 64.0f, amount, skew);
                    CHECK (std::isfinite (w));
                    CHECK (w >= -0.3f && w <= 1.0001f);
                }
    CHECK_NEAR (windowValue (WindowShape::hann, 0.0f, 1.0f, 0.0f), 0.0, 1.0e-6);
    CHECK_NEAR (windowValue (WindowShape::hann, 0.5f, 1.0f, 0.0f), 1.0, 1.0e-3);
    CHECK_NEAR (windowValue (WindowShape::hann, 0.5f, 0.0f, 0.0f), 1.0, 1.0e-6); // amount 0: a rectangle
    CHECK (windowValue (WindowShape::hann, 0.25f, 1.0f, 0.0f) > 0.4f);
    // skew moves the peak: at x = 0.2 a window skewed to the start is louder than one skewed to the end
    CHECK (windowValue (WindowShape::hann, 0.2f, 1.0f, -1.0f) > windowValue (WindowShape::hann, 0.2f, 1.0f, 1.0f));
    CHECK (windowValue (WindowShape::expDec, 0.1f, 1.0f, 0.0f) > windowValue (WindowShape::expDec, 0.9f, 1.0f, 0.0f));
    CHECK (windowValue (WindowShape::expDecRev, 0.9f, 1.0f, 0.0f) > windowValue (WindowShape::expDecRev, 0.1f, 1.0f, 0.0f));
}

TEST (granular_plays_the_sample_at_the_pitch_ratio)
{
    Tone tone (440.0);
    const auto view = tone.data.view();
    for (double ratio : { 0.5, 1.0, 2.0 })
    {
        GranularVoice v;
        v.prepare (44100.0);
        GrainParams p;
        p.pitchRatio = static_cast<float> (ratio);
        p.densityHz = 60.0f;
        p.lengthSec = 0.08f;
        p.scanSpeed = 0.2f;
        v.noteOn (&view, p, 7u, 0.0f, true, 0.0f);
        const auto out = render (v, p, 22050);
        CHECK (rms (out, 4000) > 0.05);
        CHECK_NEAR (crossingHz (out, 4000, out.size() - 1000) / (440.0 * ratio), 1.0, 0.06);
    }
}

TEST (granular_scan_moves_the_play_head_and_zero_freezes_it)
{
    Tone tone (440.0, 4.0);
    const auto view = tone.data.view();
    GranularVoice v;
    v.prepare (44100.0);
    GrainParams p;
    p.scanSpeed = 1.0f;
    v.noteOn (&view, p, 3u, 0.0f, true, 0.0f);
    render (v, p, 44100);
    CHECK_NEAR (v.playhead(), 0.25, 0.01); // 1 s of 4 s
    p.scanSpeed = 0.0f;
    const double frozen = v.playhead();
    render (v, p, 8192);
    CHECK_NEAR (v.playhead(), frozen, 1.0e-9);
    p.scanSpeed = -1.0f;
    render (v, p, 22050);
    CHECK_NEAR (v.playhead(), frozen - 0.125, 0.01); // a negative rate reverses the head
}

TEST (granular_manual_mode_places_the_head)
{
    Tone tone (440.0, 4.0);
    const auto view = tone.data.view();
    GranularVoice v;
    v.prepare (44100.0);
    GrainParams p;
    p.manual = true;
    p.position = 0.5f;
    p.regionLo = 0.25f;
    p.regionHi = 0.75f;
    v.noteOn (&view, p, 3u, 0.0f, true, 0.0f);
    render (v, p, 4096);
    CHECK_NEAR (v.playhead(), 0.5, 1.0e-6);
    p.position = 1.0f;
    render (v, p, 256);
    CHECK_NEAR (v.playhead(), 0.75, 1.0e-6);
}

TEST (granular_density_sets_the_overlap_and_the_pool_limit)
{
    Tone tone (440.0);
    const auto view = tone.data.view();
    GranularVoice v;
    v.prepare (44100.0);
    GrainParams p;
    p.densityHz = 100.0f;
    p.lengthSec = 0.1f; // 10 grains overlap
    p.maxGrains = kMaxGrains;
    v.noteOn (&view, p, 5u, 0.0f, true, 0.0f);
    render (v, p, 8192);
    const int full = v.activeGrains();
    CHECK (full >= 8 && full <= 12);
    GranularVoice limited;
    limited.prepare (44100.0);
    p.maxGrains = 3;
    limited.noteOn (&view, p, 5u, 0.0f, true, 0.0f);
    render (limited, p, 8192);
    CHECK (limited.activeGrains() <= 3);
    // without jump start the first block has only a single grain
    GranularVoice soft;
    soft.prepare (44100.0);
    p.maxGrains = kMaxGrains;
    soft.noteOn (&view, p, 5u, 0.0f, false, 0.0f);
    std::vector<float> l (16), r (16);
    soft.process (p, l.data(), r.data(), 16, nullptr);
    CHECK_EQ (soft.activeGrains(), 1);
}

TEST (granular_randomisation_changes_the_sound_and_is_deterministic)
{
    Tone tone (440.0);
    const auto view = tone.data.view();
    GrainParams base;
    base.densityHz = 50.0f;
    base.lengthSec = 0.05f;
    base.scanSpeed = 0.1f;
    auto run = [&] (const GrainParams& p, uint32_t seed)
    {
        GranularVoice v;
        v.prepare (44100.0);
        v.noteOn (&view, p, seed, 0.0f, true, 0.0f);
        return render (v, p, 8192);
    };
    const auto a = run (base, 11u), b = run (base, 11u), c = run (base, 12u);
    CHECK (a == b); // same seed, same grains
    GrainParams pitch = base;
    pitch.randPitch = 7.0f;
    GrainParams pan = base;
    pan.randPan = 1.0f;
    GrainParams gain = base;
    gain.randGain = 1.0f;
    GrainParams offset = base;
    offset.randOffset = 1.0f;
    GrainParams dir = base;
    dir.randDir = 1.0f;
    GrainParams length = base;
    length.randLength = 0.8f;
    GrainParams win = base;
    win.randWindow = 1.0f;
    for (const GrainParams* p : { &pitch, &pan, &gain, &offset, &dir, &length, &win })
    {
        const auto x = run (*p, 11u);
        CHECK (x != a);
        for (float s : x)
            CHECK (std::isfinite (s));
    }
    (void) c;
    // random pan: the two channels differ; no pan: they are equal on a mono sample
    GranularVoice v;
    v.prepare (44100.0);
    v.noteOn (&view, pan, 1u, 0.0f, true, 0.0f);
    std::vector<float> right;
    const auto left = render (v, pan, 4096, 256, &right);
    CHECK (left != right);
    GranularVoice w;
    w.prepare (44100.0);
    w.noteOn (&view, base, 1u, 0.0f, true, 0.0f);
    std::vector<float> right2;
    const auto left2 = render (w, base, 4096, 256, &right2);
    CHECK (left2 == right2);
}

TEST (granular_reverse_grains_play_backwards_and_loop_modes_keep_the_head_in_range)
{
    // a rising ramp: forward grains rise, reversed grains fall
    std::vector<float> ramp (44100);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float> (i) / static_cast<float> (ramp.size());
    SampleData data;
    data.loadFromFrames (ramp.data(), nullptr, static_cast<int64_t> (ramp.size()), 44100.0);
    const auto view = data.view();
    GrainParams p;
    p.densityHz = 2.0f;
    p.lengthSec = 0.4f;
    p.scanSpeed = 0.0f;
    p.manual = true;
    p.position = 0.5f;
    p.windowAmount = 0.0f;
    GranularVoice fw, bw;
    fw.prepare (44100.0);
    bw.prepare (44100.0);
    fw.noteOn (&view, p, 1u, 0.0f, false, 0.0f);
    p.reverseGrains = true;
    bw.noteOn (&view, p, 1u, 0.0f, false, 0.0f);
    GrainParams pf = p;
    pf.reverseGrains = false;
    const auto f = render (fw, pf, 8192), b = render (bw, p, 8192);
    CHECK (f[6000] > f[1000]);
    CHECK (b[6000] < b[1000]);

    for (auto mode : { synthyboy::sampleosc::LoopMode::forward, synthyboy::sampleosc::LoopMode::pingPong, synthyboy::sampleosc::LoopMode::reverse,
                       synthyboy::sampleosc::LoopMode::tailed })
    {
        GrainParams q;
        q.loopMode = mode;
        q.loopLo = 0.25f;
        q.loopHi = 0.75f;
        q.scanSpeed = 3.0f;
        q.loopGrains = true;
        q.loopCrossfade = 0.1f;
        GranularVoice v;
        v.prepare (44100.0);
        v.noteOn (&view, q, 9u, 0.0f, true, 0.0f);
        double lo = 1.0, hi = 0.0;
        for (int i = 0; i < 40; ++i)
        {
            std::vector<float> l (512), r (512);
            v.process (q, l.data(), r.data(), 512, nullptr);
            for (float s : l)
                CHECK (std::isfinite (s));
            if (i == 20 && mode == synthyboy::sampleosc::LoopMode::tailed)
                v.releaseLoop();
            if (i >= 10 && (mode != synthyboy::sampleosc::LoopMode::tailed || i < 20))
            {
                lo = std::min (lo, v.playhead());
                hi = std::max (hi, v.playhead());
            }
        }
        CHECK (lo >= 0.25 - 1.0e-6 || mode == synthyboy::sampleosc::LoopMode::reverse);
        CHECK (hi <= 0.75 + 1.0e-6 || mode == synthyboy::sampleosc::LoopMode::reverse);
    }
    // one shot: the cloud ends after the head ran out and the grains are over
    GrainParams o;
    o.scanSpeed = 8.0f;
    o.lengthSec = 0.02f;
    GranularVoice v;
    v.prepare (44100.0);
    v.noteOn (&view, o, 1u, 0.0f, true, 0.0f);
    render (v, o, 44100);
    CHECK (! v.isActive());
}

TEST (granular_extreme_settings_stay_finite_and_bounded)
{
    Tone tone (1000.0, 1.0);
    const auto view = tone.data.view();
    GrainParams p;
    p.densityHz = 1000.0f;
    p.lengthSec = 4.0f;
    p.maxGrains = kMaxGrains;
    p.pitchRatio = 8.0f;
    p.randPitch = 24.0f;
    p.randOffset = p.randDir = p.randLength = p.randPan = p.randGain = p.randWindow = p.randSkew = 1.0f;
    p.scanSpeed = 8.0f;
    p.windowAmount = 1.0f;
    p.windowSkew = 1.0f;
    p.loopMode = synthyboy::sampleosc::LoopMode::forward;
    p.loopGrains = true;
    for (int shape = 0; shape < kNumWindowShapes; ++shape)
    {
        p.shape = static_cast<WindowShape> (shape);
        GranularVoice v;
        v.prepare (48000.0);
        v.noteOn (&view, p, 77u, 0.5f, true, 1.0f);
        std::vector<float> l (333), r (333);
        for (int i = 0; i < 40; ++i)
        {
            v.process (p, l.data(), r.data(), 333, nullptr);
            for (int k = 0; k < 333; ++k)
            {
                CHECK (std::isfinite (l[k]) && std::isfinite (r[k]));
                CHECK (std::fabs (l[k]) <= 16.0f && std::fabs (r[k]) <= 16.0f);
            }
        }
    }
    // tiny region, zero density, no sample
    GrainParams q;
    q.densityHz = 0.0f;
    q.regionLo = q.regionHi = 0.5f;
    GranularVoice v;
    v.prepare (44100.0);
    v.noteOn (&view, q, 1u, 0.0f, true, 0.0f);
    std::vector<float> l (64), r (64);
    v.process (q, l.data(), r.data(), 64, nullptr);
    GranularVoice none;
    none.prepare (44100.0);
    none.noteOn (nullptr, q, 1u, 0.0f, true, 0.0f);
    none.process (q, l.data(), r.data(), 64, nullptr);
    CHECK (! none.isActive());
    CHECK_EQ (l[10], 0.0f);
}

TEST (granular_block_size_does_not_change_the_sound)
{
    Tone tone (330.0);
    const auto view = tone.data.view();
    GrainParams p;
    p.densityHz = 40.0f;
    p.lengthSec = 0.06f;
    p.randPitch = 2.0f;
    p.randPan = 0.5f;
    p.scanSpeed = 0.5f;
    auto run = [&] (int block)
    {
        GranularVoice v;
        v.prepare (44100.0);
        v.noteOn (&view, p, 21u, 0.25f, true, 0.0f);
        return render (v, p, 4096, block);
    };
    const auto a = run (64), b = run (512);
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::fabs (a[i] - b[i])));
    CHECK (worst < 1.0e-4);
}
