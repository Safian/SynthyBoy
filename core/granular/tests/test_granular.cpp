// SynthyBoy - core/granular tests: windows, density, pitch, scan, randomisation, loops, determinism, bounds.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/granular/granular.h"
#include "synthyboy/sampleosc/sample_data.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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

// The grain kernels against the reference loop (setReferenceKernels (true)) and against each other: over every window shape, pitch ratios that use the mip levels, loop grains with the
// crossfade, reversed and randomised grains, pointwise warps, a rate modulation, and stereo. The vector form (four samples of a grain at a time, setVectorKernels) gives the very bits of the
// scalar form of the kernel; the kernel and the reference loop agree to rounding (the window of the kernel multiplies by a reciprocal where the reference divides: about 1e-7 of the
// grain's level; the largest difference is at the tail of a grain, where the window is nearly 0 and the 64 (1 - x) ramp turns the 1 ulp of x into 4e-6: the bound is stated as 3e-5 of the block's largest sample).
namespace
{
float testShape (int type, float x, float amount) { return type == 0 ? std::tanh (x * (1.0f + 4.0f * amount)) : std::round (x * (2.0f + 30.0f * amount)) / (2.0f + 30.0f * amount); }
}
TEST (granular_kernels_agree_with_each_other_and_with_the_reference_loop)
{
    Tone mono (220.0, 1.5);
    SampleData stereo;
    {
        std::vector<float> l (66000), r (66000);
        for (size_t i = 0; i < l.size(); ++i)
        {
            l[i] = static_cast<float> (0.5 * std::sin (2.0 * kPi * 330.0 * static_cast<double> (i) / 44100.0));
            r[i] = static_cast<float> (0.4 * std::sin (2.0 * kPi * 551.0 * static_cast<double> (i) / 44100.0) + 0.1 * std::sin (0.37 * static_cast<double> (i)));
        }
        stereo.loadFromFrames (l.data(), r.data(), static_cast<int64_t> (l.size()), 44100.0);
    }
    int blocks = 0, vectorDifferent = 0;
    double worstRelative = 0.0;
    for (int variant = 0; variant < 4; ++variant)
        for (int shape = 0; shape < kNumWindowShapes; ++shape)
            for (float pitch : { 0.7f, 1.0f, 2.9f, 9.5f })
            {
                const auto view = variant % 2 == 0 ? mono.data.view() : stereo.view();
                GrainParams p;
                p.shape = static_cast<WindowShape> (shape);
                p.windowAmount = 0.8f;
                p.windowSkew = variant == 1 ? 0.6f : -0.3f;
                p.pitchRatio = pitch;
                p.densityHz = 70.0f;
                p.lengthSec = 0.09f;
                p.maxGrains = 24;
                p.scanSpeed = 0.5f;
                p.loopMode = variant >= 2 ? LoopMode::forward : LoopMode::none;
                p.loopLo = 0.2f;
                p.loopHi = 0.7f;
                p.loopGrains = variant >= 2;
                p.loopCrossfade = 0.3f;
                p.randOffset = 0.3f;
                p.randDir = variant == 3 ? 0.5f : 0.0f;
                p.randPitch = 2.0f;
                p.randLength = 0.3f;
                p.randPan = 0.6f;
                p.randGain = 0.3f;
                p.randWindow = 0.2f;
                p.randSkew = 0.2f;
                p.manual = variant == 0 && shape % 2 == 0;   // the head placed: the spawns come from the timer (the reference tests every sample)
                p.position = 0.4f;
                if (variant == 1 || variant == 3)
                {
                    p.shapeFn = &testShape;
                    p.shapeType[0] = 0;
                    p.shapeType[1] = 1;
                    p.shapeAmount[0] = 0.5f;
                    p.shapeAmount[1] = 0.4f;
                    p.randWarp[0] = 0.2f;
                }
                // variant 0 and 2 plain (the vector form runs; 2 is looped, so it does not), 1 and 3 with the warps (it does not): the plain ones need a second set with nothing but the read
                GranularVoice vec, scalar, reference;
                for (GranularVoice* v : { &vec, &scalar, &reference })
                    v->prepare (44100.0);
                scalar.setVectorKernels (false);
                reference.setReferenceKernels (true);
                for (GranularVoice* v : { &vec, &scalar, &reference })
                    v->noteOn (&view, p, 99u + static_cast<uint32_t> (shape), 0.3f, true, 0.1f);
                std::vector<float> rate (128);
                for (size_t i = 0; i < 128; ++i)
                    rate[i] = 1.0f + 0.5f * static_cast<float> (std::sin (0.2 * static_cast<double> (i)));
                for (int block = 0; block < 60; ++block)
                {
                    const int n = block % 2 == 0 ? 128 : 53;
                    std::vector<float> aL (128), aR (128), bL (128), bR (128), cL (128), cR (128);
                    const float* rm = variant == 2 && block % 3 == 0 ? rate.data() : nullptr;
                    vec.process (p, aL.data(), aR.data(), n, rm);
                    scalar.process (p, bL.data(), bR.data(), n, rm);
                    reference.process (p, cL.data(), cR.data(), n, rm);
                    ++blocks;
                    if (std::memcmp (aL.data(), bL.data(), sizeof (float) * 128) != 0 || std::memcmp (aR.data(), bR.data(), sizeof (float) * 128) != 0
                        || vec.activeGrains() != reference.activeGrains())
                        ++vectorDifferent;
                    double peak = 1.0e-9, worst = 0.0;
                    for (size_t i = 0; i < 128; ++i)
                    {
                        peak = std::max ({ peak, static_cast<double> (std::fabs (cL[i])), static_cast<double> (std::fabs (cR[i])) });
                        worst = std::max ({ worst, static_cast<double> (std::fabs (aL[i] - cL[i])), static_cast<double> (std::fabs (aR[i] - cR[i])) });
                    }
                    worstRelative = std::max (worstRelative, worst / peak);
                }
            }
    CHECK (blocks > 1000);
    CHECK_EQ (vectorDifferent, 0);
    CHECK (worstRelative < 3.0e-5);
    std::printf ("    grain kernels: vector against scalar %d of %d blocks differ; kernel against reference: largest difference %.3g of the block's peak\n", vectorDifferent, blocks, worstRelative);
}

// Plain grains (no warp, no loop wrap, no rate modulation) so that the vector form really runs: it must give the bits of the scalar form over a long play.
TEST (granular_vector_form_equals_the_scalar_form_on_plain_grains)
{
    Tone mono (220.0, 2.0);
    int different = 0, blocks = 0;
    for (float pitch : { 0.5f, 1.0f, 1.7f, 5.0f })
        for (int skewed = 0; skewed < 2; ++skewed)
        {
            const auto view = mono.data.view();
            GrainParams p;
            p.pitchRatio = pitch;
            p.densityHz = 90.0f;
            p.lengthSec = 0.12f;
            p.maxGrains = 32;
            p.scanSpeed = 0.3f;
            p.windowSkew = skewed != 0 ? 0.5f : 0.0f;
            p.randOffset = 0.5f;
            p.randDir = 0.3f;
            p.randPan = 0.5f;
            p.shapeFn = &testShape;   // the engine always hands the function over; no slot has a type, so no warp plays and the vector form may run
            GranularVoice vec, scalar;
            vec.prepare (44100.0);
            scalar.prepare (44100.0);
            scalar.setVectorKernels (false);
            vec.noteOn (&view, p, 5u, 0.0f, true, 0.0f);
            scalar.noteOn (&view, p, 5u, 0.0f, true, 0.0f);
            for (int block = 0; block < 400; ++block)
            {
                std::vector<float> aL (128), aR (128), bL (128), bR (128);
                vec.process (p, aL.data(), aR.data(), 128, nullptr);
                scalar.process (p, bL.data(), bR.data(), 128, nullptr);
                ++blocks;
                if (std::memcmp (aL.data(), bL.data(), sizeof (float) * 128) != 0 || std::memcmp (aR.data(), bR.data(), sizeof (float) * 128) != 0)
                    ++different;
            }
        }
    CHECK (blocks == 3200);
    CHECK_EQ (different, 0);
}

// SP1 (docs/release/CPU-REVIEW-CONTENT.md): reset() skips the 48 `active` writes while no grain was spawned since the last reset. A voice that is reset and restarted again and again (live clouds,
// resets of an idle voice, jump start and plain start, manual and scanning) is bit for bit the voice with the old reset (`setLegacyReset`), and a reset leaves no grain active.
TEST (granular_reset_that_skips_idle_voices_is_the_old_reset)
{
    Tone tone (440.0);
    const auto view = tone.data.view();
    int blocks = 0, different = 0;
    for (int manual = 0; manual < 2; ++manual)
        for (int jump = 0; jump < 2; ++jump)
        {
            GrainParams p;
            p.densityHz = 80.0f;
            p.lengthSec = 0.08f;
            p.maxGrains = kMaxGrains;
            p.manual = manual != 0;
            p.position = 0.3f;
            p.scanSpeed = 0.5f;
            GranularVoice fast, old;
            fast.prepare (44100.0);
            old.prepare (44100.0);
            old.setLegacyReset (true);
            for (int note = 0; note < 6; ++note)
            {
                if (note % 3 == 2)
                {
                    fast.reset();   // a reset of a voice that is idle already
                    old.reset();
                }
                fast.noteOn (&view, p, 5u + static_cast<uint32_t> (note), 0.0f, jump != 0, 0.1f * static_cast<float> (note));
                old.noteOn (&view, p, 5u + static_cast<uint32_t> (note), 0.0f, jump != 0, 0.1f * static_cast<float> (note));
                for (int block = 0; block < 40; ++block)
                {
                    std::vector<float> aL (128), aR (128), bL (128), bR (128);
                    fast.process (p, aL.data(), aR.data(), 128, nullptr);
                    old.process (p, bL.data(), bR.data(), 128, nullptr);
                    ++blocks;
                    if (std::memcmp (aL.data(), bL.data(), sizeof (float) * 128) != 0 || std::memcmp (aR.data(), bR.data(), sizeof (float) * 128) != 0)
                        ++different;
                }
                CHECK_EQ (fast.activeGrains(), old.activeGrains());
                fast.reset();
                old.reset();
                CHECK_EQ (fast.activeGrains(), 0);
                CHECK_EQ (old.activeGrains(), 0);
                CHECK (! fast.isActive());
            }
        }
    CHECK_EQ (blocks, 4 * 6 * 40);
    CHECK_EQ (different, 0);
}

// GR1: the scanning head with the divisions and loop bounds made once per call is the head of every sample's own computation (`setLegacyHead`): the grains it spawns (the output, bit for bit) and the play head
// agree over every loop mode, both scan directions, reversed and degenerate loop markers, a Tailed loop that is released, and blocks of different sizes.
TEST (granular_the_scanning_head_with_constants_made_once_is_the_head_of_every_sample)
{
    Tone tone (330.0, 1.5);
    const auto view = tone.data.view();
    int runs = 0, different = 0;
    for (LoopMode mode : { LoopMode::none, LoopMode::forward, LoopMode::pingPong, LoopMode::reverse, LoopMode::tailed })
        for (float speed : { 1.0f, -0.7f, 3.5f })
            for (int shape = 0; shape < 3; ++shape)
            {
                GrainParams p;
                p.densityHz = 70.0f;
                p.lengthSec = 0.05f;
                p.maxGrains = kMaxGrains;
                p.manual = false;
                p.scanSpeed = speed;
                p.loopMode = mode;
                p.regionLo = 0.1f;
                p.regionHi = 0.9f;
                p.loopLo = shape == 1 ? 0.6f : 0.3f;
                p.loopHi = shape == 1 ? 0.3f : (shape == 2 ? 0.3f : 0.55f);   // shape 1: reversed markers, shape 2: a loop of no length
                GranularVoice fast, old;
                fast.prepare (44100.0);
                old.prepare (44100.0);
                old.setLegacyHead (true);
                fast.noteOn (&view, p, 9u, 0.0f, true, 0.0f);
                old.noteOn (&view, p, 9u, 0.0f, true, 0.0f);
                for (int block = 0; block < 150; ++block)
                {
                    const int n = block % 3 == 0 ? 64 : (block % 3 == 1 ? 128 : 100);
                    if (block == 90)
                    {
                        fast.releaseLoop();
                        old.releaseLoop();
                    }
                    std::vector<float> aL (static_cast<size_t> (n)), aR (aL), bL (aL), bR (aL);
                    fast.process (p, aL.data(), aR.data(), n, nullptr);
                    old.process (p, bL.data(), bR.data(), n, nullptr);
                    if (std::memcmp (aL.data(), bL.data(), sizeof (float) * static_cast<size_t> (n)) != 0 || std::memcmp (aR.data(), bR.data(), sizeof (float) * static_cast<size_t> (n)) != 0
                        || fast.playhead() != old.playhead() || fast.activeGrains() != old.activeGrains())
                        ++different;
                }
                ++runs;
            }
    CHECK_EQ (runs, 45);
    CHECK_EQ (different, 0);
    // Mac timing: a scanning cloud for 128 samples
    double us[2] = {};
    int live = 0;
    for (int legacy = 0; legacy < 2; ++legacy)
    {
        GrainParams p;
        p.densityHz = 60.0f;
        p.lengthSec = 0.07f;
        p.maxGrains = kMaxGrains;
        p.manual = false;
        p.scanSpeed = 0.3f;
        p.loopMode = LoopMode::forward;
        p.loopLo = 0.2f;
        p.loopHi = 0.8f;
        GranularVoice v;
        v.prepare (44100.0);
        v.setLegacyHead (legacy != 0);
        v.noteOn (&view, p, 9u, 0.0f, true, 0.0f);
        std::vector<float> l (128), r (128);
        const auto t0 = std::chrono::steady_clock::now();
        for (int block = 0; block < 5000; ++block)
            v.process (p, l.data(), r.data(), 128, nullptr);
        us[legacy] = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / 5000.0;
        live = v.activeGrains();
    }
    std::printf ("    scanning cloud (%d grains live), 128 samples: %.2f us now, %.2f us before (this Mac)\n", live, us[0], us[1]);
}

// SP10: the output stage of the cloud (mask of non finite values, clamp to +-16) in NEON lanes: the kernel against the scalar loop on values of every kind, and the voice against the voice with the scalar stage.
TEST (granular_the_output_stage_in_lanes_is_the_per_sample_stage)
{
    const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> special = { 0.0f, -0.0f, 1.0f, -1.0f, 16.0f, -16.0f, 16.0001f, -16.0001f, 15.9999f, 1.0e30f, -1.0e30f, 3.4e38f, -3.4e38f, inf, -inf, nan, -nan, 1.0e-30f, 1.0e-39f, -1.0e-39f, 0.5f };
    uint32_t seed = 77u;
    for (int i = 0; i < 500; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        special.push_back (40.0f * (static_cast<float> (seed >> 8) * (1.0f / 8388608.0f) - 1.0f));
    }
    for (int n : { 0, 1, 3, 4, 5, 8, 17, static_cast<int> (special.size()) })
        for (int neon = 0; neon < 2; ++neon)
        {
            std::vector<float> a (special.begin(), special.end()), ref (special.begin(), special.end());
            clampFinite (a.data(), n, neon != 0);
            for (int i = 0; i < n; ++i)
                ref[static_cast<size_t> (i)] = std::isfinite (ref[static_cast<size_t> (i)]) ? std::max (-16.0f, std::min (16.0f, ref[static_cast<size_t> (i)])) : 0.0f;
            CHECK (std::memcmp (a.data(), ref.data(), a.size() * sizeof (float)) == 0);   // (the values after n are untouched)
        }
    Tone tone (330.0, 1.5);
    const auto view = tone.data.view();
    int blocks = 0, different = 0;
    for (int jump = 0; jump < 2; ++jump)
    {
        GrainParams p;
        p.densityHz = 400.0f;   // many grains: the sum goes past 16 and the clamp works
        p.lengthSec = 0.2f;
        p.maxGrains = kMaxGrains;
        GranularVoice a, b;
        a.prepare (44100.0);
        b.prepare (44100.0);
        b.setLegacyOutput (true);
        a.noteOn (&view, p, 3u, 0.0f, jump != 0, 0.0f);
        b.noteOn (&view, p, 3u, 0.0f, jump != 0, 0.0f);
        for (int block = 0; block < 100; ++block)
        {
            const int n = block % 4 == 0 ? 1 : (block % 4 == 1 ? 33 : 128);
            std::vector<float> aL (static_cast<size_t> (n)), aR (aL), bL (aL), bR (aL);
            a.process (p, aL.data(), aR.data(), n, nullptr);
            b.process (p, bL.data(), bR.data(), n, nullptr);
            ++blocks;
            if (std::memcmp (aL.data(), bL.data(), sizeof (float) * static_cast<size_t> (n)) != 0 || std::memcmp (aR.data(), bR.data(), sizeof (float) * static_cast<size_t> (n)) != 0)
                ++different;
        }
    }
    CHECK_EQ (blocks, 200);
    CHECK_EQ (different, 0);
}

// Rank 17: loop grains crossfade is continuous for non-periodic material (decisive test from analysis Q7)
TEST (granular_loop_grains_crossfade_boundary)
{
    // Q7.1: Non-periodic FM cosine + linear DC ramp, 2.0 s at 44.1 kHz
    const double sampleRate = 44100.0;
    const int totalFrames = static_cast<int> (sampleRate * 2.0); // 88200 frames
    std::vector<float> sampleData (static_cast<size_t> (totalFrames));
    for (size_t i = 0; i < sampleData.size(); ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        const double phase = 2.0 * kPi * (173.3 * t + 241.7 * t * t);
        sampleData[i] = static_cast<float> (0.25 * std::cos (phase) + 0.30 * (1.0 - 1.5 * t));
    }

    SampleData sample;
    sample.loadFromFrames (sampleData.data(), nullptr, totalFrames, sampleRate);
    const auto view = sample.view();

    float maxSampleStep = 0.0f;
    for (size_t i = 1; i < sampleData.size(); ++i)
        maxSampleStep = std::max (maxSampleStep, std::fabs (sampleData[i] - sampleData[i - 1]));

    const float wrapLimit = 1.15f * maxSampleStep;
    const float regionLimit = 1.25f * maxSampleStep;

    struct TestCase
    {
        const char* name;
        float loopLo;
        float loopHi;
        bool isBitIdentityCase;
    };

    const TestCase cases[] = {
        { "A (loopLo = 0.0, loopHi = 0.5: no pre-roll, post-roll exists: F2/R1)", 0.0f, 0.5f, false },
        { "B (loopLo = 0.25, loopHi = 0.75: both exist: F1/R1)", 0.25f, 0.75f, true },
        { "C (loopLo = 0.5, loopHi = 0.9999: pre-roll exists, no post-roll: F1/R2)", 0.5f, 0.9999f, false }
    };

    for (const auto& tc : cases)
    {
        // Q7.2: Grain parameters
        GrainParams p;
        p.loopLo = tc.loopLo;
        p.loopHi = tc.loopHi;
        p.loopGrains = true;
        p.loopCrossfade = 0.1f;
        p.densityHz = 0.5f;
        p.maxGrains = 1;
        p.lengthSec = 1.0f;
        p.gain = 1.0f;
        p.windowAmount = 0.0f;
        p.manual = true;
        p.scanSpeed = 0.0f;
        p.pitchRatio = 1.0f;
        p.regionLo = 0.0f;
        p.regionHi = 1.0f;

        // Forward grains: start just before loopHi, crossing into loopLo at i_wrap = 4410
        p.reverseGrains = false;
        p.position = tc.loopHi - 0.05f;
        GranularVoice vf;
        vf.prepare (sampleRate);
        vf.noteOn (&view, p, 1u, 0.0f, true, 0.0f);
        std::vector<float> rightFwd;
        auto outFwd = render (vf, p, 12000, 256, &rightFwd);
        outFwd.resize (12000);
        rightFwd.resize (12000);

        // Q7.3: Tight wrap window [i_wrap - 8, i_wrap + 8] = [4402, 4418]
        const int iWrapFwd = 4410;
        float maxJumpWrapFwd = 0.0f;
        for (int i = iWrapFwd - 8; i <= iWrapFwd + 8; ++i)
            maxJumpWrapFwd = std::max (maxJumpWrapFwd, std::fabs (outFwd[static_cast<size_t> (i)] - outFwd[static_cast<size_t> (i - 1)]));

        // Full crossfade + wrap region [1000, 9000]
        float maxJumpRegionFwd = 0.0f;
        for (size_t i = 1000; i <= 9000; ++i)
            maxJumpRegionFwd = std::max (maxJumpRegionFwd, std::fabs (outFwd[i] - outFwd[i - 1]));

        // Q7.5: Bit-identity case for B-Fwd (F1): compare kernel and reference, print checksum
        double f1SumKernel = 0.0;
        double f1SumRef = 0.0;
        if (tc.isBitIdentityCase)
        {
            GranularVoice vfRef;
            vfRef.prepare (sampleRate);
            vfRef.setReferenceKernels (true);
            vfRef.noteOn (&view, p, 1u, 0.0f, true, 0.0f);
            std::vector<float> rightRef;
            auto outFwdRef = render (vfRef, p, 12000, 256, &rightRef);
            outFwdRef.resize (12000);
            rightRef.resize (12000);

            for (float s : outFwd)
                f1SumKernel += static_cast<double> (s);

            for (float s : outFwdRef)
                f1SumRef += static_cast<double> (s);
        }

        // Reverse grains: start just after loopLo, crossing into loopHi at i_wrap = 4411
        p.reverseGrains = true;
        p.position = tc.loopLo + 0.05f;
        GranularVoice vr;
        vr.prepare (sampleRate);
        vr.noteOn (&view, p, 1u, 0.0f, true, 0.0f);
        std::vector<float> rightRev;
        auto outRev = render (vr, p, 12000, 256, &rightRev);
        outRev.resize (12000);
        rightRev.resize (12000);

        // Q7.3: Tight wrap window [i_wrap - 8, i_wrap + 8] = [4403, 4419]
        const int iWrapRev = 4411;
        float maxJumpWrapRev = 0.0f;
        for (int i = iWrapRev - 8; i <= iWrapRev + 8; ++i)
            maxJumpWrapRev = std::max (maxJumpWrapRev, std::fabs (outRev[static_cast<size_t> (i)] - outRev[static_cast<size_t> (i - 1)]));

        // Full crossfade + wrap region [1000, 9000]
        float maxJumpRegionRev = 0.0f;
        for (size_t i = 1000; i <= 9000; ++i)
            maxJumpRegionRev = std::max (maxJumpRegionRev, std::fabs (outRev[i] - outRev[i - 1]));

        std::printf ("    case %s:\n", tc.name);
        if (tc.isBitIdentityCase)
            std::printf ("F1-CHECKSUM: %#.9g (ref: %#.9g)\n", f1SumKernel, f1SumRef);
        std::printf ("      sample max step: %f, wrap limit: %f, region limit: %f\n",
                     maxSampleStep, wrapLimit, regionLimit);
        std::printf ("      forward wrap jump: %f (ratio: %.3fx), region jump: %f (ratio: %.3fx)\n",
                     maxJumpWrapFwd, maxJumpWrapFwd / maxSampleStep,
                     maxJumpRegionFwd, maxJumpRegionFwd / maxSampleStep);
        std::printf ("      reverse wrap jump: %f (ratio: %.3fx), region jump: %f (ratio: %.3fx)\n",
                     maxJumpWrapRev, maxJumpWrapRev / maxSampleStep,
                     maxJumpRegionRev, maxJumpRegionRev / maxSampleStep);

        CHECK (maxJumpWrapFwd <= wrapLimit);
        CHECK (maxJumpRegionFwd <= regionLimit);
        CHECK (maxJumpWrapRev <= wrapLimit);
        CHECK (maxJumpRegionRev <= regionLimit);
    }
}

// granular_mip_selection_blocks_fractional_octave_aliasing
// Reuses helper types, includes, set-up calls and check macros from granular_loop_grains_crossfade_boundary (lines 691-765)
TEST (granular_mip_selection_blocks_fractional_octave_aliasing)
{
    const double sampleRate = 44100.0;
    constexpr int kFrames = 8192;
    std::vector<float> sineData (kFrames);
    for (int i = 0; i < kFrames; ++i)
        sineData[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * 18000.0 * static_cast<double> (i) / sampleRate));

    SampleData sample;
    CHECK (sample.loadFromFrames (sineData.data(), nullptr, kFrames, sampleRate));
    CHECK (sample.numMipLevels() >= 2);
    const auto view = sample.view();

    GrainParams p;
    p.pitchRatio = 1.8f;
    p.densityHz = 4.0f;
    p.lengthSec = 0.5f;
    p.maxGrains = 1;
    p.windowAmount = 0.0f;
    p.manual = true;
    p.position = 0.1f;
    p.loopGrains = true;
    p.loopLo = 0.0f;
    p.loopHi = 1.0f;
    p.gain = 1.0f;

    GranularVoice voice;
    voice.prepare (sampleRate);
    voice.noteOn (&view, p, 1u, 0.0f, true, 0.0f);

    constexpr int kRenderLen = 4096;
    auto rendered = render (voice, p, kRenderLen, 256);
    rendered.resize (kRenderLen);

    const double measuredRms = rms (rendered, 512);
    const double unattenuatedRms = 0.7071;
    const double rejectionDb = 20.0 * std::log10 (std::max (measuredRms / unattenuatedRms, 1e-12));

    // BEFORE FIX: floor(log2(1.8)) = floor(0.848) = 0 (mip 0) -> rejectionDb ≈ 0 dB (rms ≈ 0.707).
    // AFTER FIX:  ceil(0.848) = 1 (mip 1) -> rejectionDb < -40 dB (rms < 0.007).
    CHECK (rejectionDb < -35.0);

    // Also render at pitchRatio = 1.0f and print GRANMIP-CHECKSUM (sum of abs + last 8 samples, %.9g) for unity bit-identity.
    GrainParams pUnity = p;
    pUnity.pitchRatio = 1.0f;

    GranularVoice voiceUnity;
    voiceUnity.prepare (sampleRate);
    voiceUnity.noteOn (&view, pUnity, 1u, 0.0f, true, 0.0f);

    auto renderedUnity = render (voiceUnity, pUnity, kRenderLen, 256);
    renderedUnity.resize (kRenderLen);

    double sumAbs = 0.0;
    for (float s : renderedUnity)
        sumAbs += static_cast<double> (std::fabs (s));
    std::printf ("GRANMIP-CHECKSUM: %.9g", sumAbs);
    for (int i = kRenderLen - 8; i < kRenderLen; ++i)
        std::printf (" [%d]=%.9g", i, static_cast<double> (renderedUnity[static_cast<size_t> (i)]));
    std::printf ("\n");
}

