// SynthyBoy - tests of the clean-room wavetable renderer (core/wavetable-render).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Property tests per component type (the definitions are our own, see the SPEC-GAP comments); the golden references of
// docs/ui-design/11-vital-preset-import.md Annex C.2 are not available yet.

#include <cmath>
#include <cstdint>
#include <vector>

#include "synthyboy/wavetable_render.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy::wtrender;

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine (int harmonic, double phase = 0.0)
{
    std::vector<float> w (kFrameSize);
    for (int i = 0; i < kFrameSize; ++i)
        w[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * harmonic * i / kFrameSize + phase));
    return w;
}

Keyframe waveKey (int position, std::vector<float> w)
{
    Keyframe k;
    k.position = position;
    k.wave = std::move (w);
    return k;
}

Description single (Component c)
{
    Description d;
    d.groups.emplace_back();
    d.groups[0].components.push_back (std::move (c));
    return d;
}

Description chain (std::vector<Component> cs)
{
    Description d;
    d.groups.emplace_back();
    d.groups[0].components = std::move (cs);
    return d;
}

Result run (const Description& d)
{
    Result r;
    CHECK (render (d, Options {}, r) == Status::ok);
    return r;
}

const float* frame (const Result& r, int f) { return r.frames.data() + static_cast<size_t> (f) * kFrameSize; }

double maxAbsDiff (const float* a, const float* b)
{
    double m = 0;
    for (int i = 0; i < kFrameSize; ++i)
        m = std::max (m, static_cast<double> (std::fabs (a[i] - b[i])));
    return m;
}

double peakOf (const float* a)
{
    double m = 0;
    for (int i = 0; i < kFrameSize; ++i)
        m = std::max (m, static_cast<double> (std::fabs (a[i])));
    return m;
}

/** magnitude of harmonic h */
double harmonicMag (const float* a, int h)
{
    double re = 0, im = 0;
    for (int i = 0; i < kFrameSize; ++i)
    {
        re += a[i] * std::cos (2.0 * kPi * h * i / kFrameSize);
        im += a[i] * std::sin (2.0 * kPi * h * i / kFrameSize);
    }
    return 2.0 * std::sqrt (re * re + im * im) / kFrameSize;
}

Component waveSource (std::vector<Keyframe> keys, Interpolation style = Interpolation::linear, bool freq = false)
{
    Component c;
    c.type = ComponentType::waveSource;
    c.interpolationStyle = style;
    c.frequencyInterpolation = freq;
    c.keyframes = std::move (keys);
    return c;
}

Component modifier (ComponentType t, Keyframe k)
{
    Component c;
    c.type = t;
    c.keyframes.push_back (std::move (k));
    return c;
}
} // namespace

TEST (wt_names_roundtrip)
{
    for (int i = 0; i < 10; ++i)
    {
        ComponentType t;
        CHECK (componentTypeFromName (componentTypeName (static_cast<ComponentType> (i)), t));
        CHECK_EQ (static_cast<int> (t), i);
    }
    ComponentType t;
    CHECK (! componentTypeFromName ("Nope", t));
    CHECK (! componentTypeFromName (nullptr, t));
}

TEST (wt_wave_source_single_keyframe)
{
    const auto r = run (single (waveSource ({ waveKey (0, sine (1)) })));
    CHECK_EQ (r.numFrames, 1);
    CHECK_EQ (r.frames.size(), static_cast<size_t> (kFrameSize));
    CHECK (maxAbsDiff (frame (r, 0), sine (1).data()) < 1e-6);
}

TEST (wt_wave_source_interpolation_styles)
{
    const auto a = sine (1), b = sine (2);
    {
        const auto r = run (single (waveSource ({ waveKey (0, a), waveKey (10, b) }, Interpolation::linear)));
        CHECK_EQ (r.numFrames, 11);
        CHECK (maxAbsDiff (frame (r, 0), a.data()) < 1e-6);
        CHECK (maxAbsDiff (frame (r, 10), b.data()) < 1e-6);
        std::vector<float> mid (kFrameSize);
        for (int i = 0; i < kFrameSize; ++i)
            mid[static_cast<size_t> (i)] = 0.5f * (a[static_cast<size_t> (i)] + b[static_cast<size_t> (i)]);
        CHECK (maxAbsDiff (frame (r, 5), mid.data()) < 1e-5);
    }
    {
        const auto r = run (single (waveSource ({ waveKey (0, a), waveKey (10, b) }, Interpolation::none)));
        CHECK (maxAbsDiff (frame (r, 9), a.data()) < 1e-6);
        CHECK (maxAbsDiff (frame (r, 10), b.data()) < 1e-6);
    }
    {
        // cubic passes through the keyframes
        const auto r = run (single (waveSource ({ waveKey (0, a), waveKey (4, b), waveKey (8, a), waveKey (12, b) }, Interpolation::cubic)));
        CHECK_EQ (r.numFrames, 13);
        CHECK (maxAbsDiff (frame (r, 4), b.data()) < 1e-5);
        CHECK (maxAbsDiff (frame (r, 8), a.data()) < 1e-5);
        CHECK (maxAbsDiff (frame (r, 2), frame (r, 2)) == 0.0);
    }
}

TEST (wt_wave_source_keyframes_unsorted_and_beyond_range)
{
    const auto a = sine (1), b = sine (3);
    const auto r = run (single (waveSource ({ waveKey (6, b), waveKey (2, a) })));
    CHECK_EQ (r.numFrames, 7);
    CHECK (maxAbsDiff (frame (r, 0), a.data()) < 1e-6); // before the first keyframe: the first one
    CHECK (maxAbsDiff (frame (r, 6), b.data()) < 1e-6);
}

TEST (wt_wave_source_frequency_interpolation)
{
    const auto a = sine (1), b = sine (2, 0.7);
    const auto r = run (single (waveSource ({ waveKey (0, a), waveKey (8, b) }, Interpolation::linear, true)));
    CHECK (maxAbsDiff (frame (r, 0), a.data()) < 1e-4);
    CHECK (maxAbsDiff (frame (r, 8), b.data()) < 1e-4);
    // in frequency mode a harmonic keeps its level while it does not exist in the other keyframe: between two sines the sum of both
    // magnitudes is 1 at mid (time mode gives the same magnitudes: 0.5 each)
    CHECK_NEAR (harmonicMag (frame (r, 4), 1), 0.5, 0.01);
    CHECK_NEAR (harmonicMag (frame (r, 4), 2), 0.5, 0.01);
    // identical keyframes: identity
    const auto same = run (single (waveSource ({ waveKey (0, a), waveKey (8, a) }, Interpolation::linear, true)));
    CHECK (maxAbsDiff (frame (same, 3), a.data()) < 1e-4);
}

TEST (wt_line_source)
{
    Component c;
    c.type = ComponentType::lineSource;
    Keyframe k;
    k.line.x = { 0.0f, 1.0f };
    k.line.y = { 0.0f, 1.0f };
    k.line.power = { 0.0f };
    c.keyframes.push_back (k);
    const auto r = run (single (c));
    CHECK_NEAR (frame (r, 0)[0], -1.0, 1e-5);
    CHECK_NEAR (frame (r, 0)[kFrameSize / 2], 0.0, 1e-4);
    CHECK (frame (r, 0)[kFrameSize - 1] > 0.99f);

    // power bends the curve below the straight line
    c.keyframes[0].line.power = { 6.0f };
    const auto bent = run (single (c));
    CHECK (frame (bent, 0)[kFrameSize / 2] < -0.5f);

    // pull power moves the horizontal axis
    c.keyframes[0].line.power = { 0.0f };
    c.keyframes[0].pullPower = 6.0f;
    const auto pulled = run (single (c));
    CHECK (frame (pulled, 0)[kFrameSize / 2] < -0.5f);
}

TEST (wt_render_line)
{
    Line l;
    l.x = { 0.0f, 0.5f, 1.0f };
    l.y = { 0.0f, 1.0f, 0.0f };
    l.power = { 0.0f, 0.0f };
    float out[100];
    renderLine (l, out, 100);
    CHECK_NEAR (out[0], 0.0, 1e-6);
    CHECK_NEAR (out[50], 1.0, 1e-5);
    CHECK_NEAR (out[25], 0.5, 1e-5);
    l.smooth = true;
    renderLine (l, out, 100);
    CHECK_NEAR (out[25], 0.5, 1e-5); // smoothstep(0.5) = 0.5
    CHECK (out[12] < 0.25f);
    // degenerate input never crashes and stays in range
    Line bad;
    renderLine (bad, out, 100);
    bad.x = { 0.8f, 0.2f, std::nanf ("") };
    bad.y = { 5.0f, -3.0f, 0.5f };
    bad.power = { 1e30f };
    renderLine (bad, out, 100);
    for (float v : out)
        CHECK (v >= 0.0f && v <= 1.0f);
}

TEST (wt_audio_file_source)
{
    Component c;
    c.type = ComponentType::audioFileSource;
    for (int i = 0; i < 8192; ++i)
        c.audio.push_back (static_cast<int16_t> (std::lround (12000.0 * std::sin (2.0 * kPi * i / 512.0))));
    c.fadeStyle = 1; // none
    c.windowSize = 512;
    c.normalizeGain = true; // normalise and raise to full scale
    Keyframe k;
    k.windowSize = 512;
    k.startPosition = 0;
    c.keyframes.push_back (k);
    auto r = run (single (c));
    CHECK_NEAR (peakOf (frame (r, 0)), 1.0, 1e-3); // normalised
    CHECK_NEAR (harmonicMag (frame (r, 0), 1), 1.0, 0.02); // 512 samples = one cycle -> fundamental
    // normalize_mult on, normalize_gain off: a frame below full scale is not raised
    c.normalizeGain = false;
    r = run (single (c));
    CHECK_NEAR (peakOf (frame (r, 0)), 12000.0 / 32768.0, 0.01);
    // regression: both switches off (what real .vital files of audio-file tables have) leave the PCM level; they once gave a gain of 0 = silence
    c.normalizeMult = false;
    r = run (single (c));
    CHECK_NEAR (peakOf (frame (r, 0)), 12000.0 / 32768.0, 0.01);
    CHECK (peakOf (frame (r, 0)) > 0.1);
    // phase styles keep the magnitudes; "clear" gives a symmetric (cosine phase) cycle
    c.normalizeMult = true;
    c.normalizeGain = true;
    c.phaseStyle = 1;
    r = run (single (c));
    CHECK_NEAR (harmonicMag (frame (r, 0), 1), 1.0, 0.02);
    CHECK_NEAR (frame (r, 0)[1], frame (r, 0)[kFrameSize - 1], 1e-3);
    c.phaseStyle = 2;
    c.randomSeed = 5;
    const auto v1 = run (single (c));
    const auto v2 = run (single (c));
    CHECK (maxAbsDiff (frame (v1, 0), frame (v2, 0)) == 0.0); // deterministic
    CHECK_NEAR (harmonicMag (frame (v1, 0), 1), 1.0, 0.02);
    // every fade style runs and stays finite
    for (int fs = 0; fs < 4; ++fs)
    {
        c.phaseStyle = 0;
        c.fadeStyle = fs;
        c.keyframes[0].windowFade = 1.0f;
        r = run (single (c));
        CHECK (peakOf (frame (r, 0)) > 0.1);
    }
    // empty audio renders silence
    c.audio.clear();
    r = run (single (c));
    CHECK_EQ (peakOf (frame (r, 0)), 0.0);
}

TEST (wt_shepard)
{
    Component c;
    c.type = ComponentType::shepardToneSource;
    Keyframe a, b;
    a.position = 0;
    b.position = 256;
    c.keyframes = { a, b };
    const auto r = run (single (c));
    CHECK_EQ (r.numFrames, 257);
    CHECK_NEAR (peakOf (frame (r, 0)), 1.0, 1e-5);
    CHECK (maxAbsDiff (frame (r, 0), frame (r, 128)) > 0.05);
    for (float v : r.frames)
        CHECK (std::isfinite (v));
}

TEST (wt_phase_shift)
{
    Keyframe k;
    k.phase = 0.25f;
    k.style = 1; // harmonic: a rotation of the cycle
    k.mix = 1.0f;
    const auto in = sine (1);
    const auto r = run (chain ({ waveSource ({ waveKey (0, in) }), modifier (ComponentType::phaseShift, k) }));
    // result is the input rotated by a quarter cycle (either direction)
    std::vector<float> a (kFrameSize), b (kFrameSize);
    for (int i = 0; i < kFrameSize; ++i)
    {
        a[static_cast<size_t> (i)] = in[static_cast<size_t> ((i + 512) % kFrameSize)];
        b[static_cast<size_t> (i)] = in[static_cast<size_t> ((i + kFrameSize - 512) % kFrameSize)];
    }
    CHECK (std::min (maxAbsDiff (frame (r, 0), a.data()), maxAbsDiff (frame (r, 0), b.data())) < 1e-4);
    // mix 0 = dry
    k.mix = 0.0f;
    const auto dry = run (chain ({ waveSource ({ waveKey (0, in) }), modifier (ComponentType::phaseShift, k) }));
    CHECK (maxAbsDiff (frame (dry, 0), in.data()) < 1e-5);
    // magnitudes unchanged by any style
    for (int style = 0; style < 3; ++style)
    {
        k.mix = 1.0f;
        k.style = style;
        const auto s = run (chain ({ waveSource ({ waveKey (0, sine (3)) }), modifier (ComponentType::phaseShift, k) }));
        CHECK_NEAR (harmonicMag (frame (s, 0), 3), 1.0, 1e-3);
    }
}

TEST (wt_wave_window)
{
    Keyframe k;
    k.leftPosition = 0.25f;
    k.rightPosition = 0.75f;
    k.windowShape = 3; // square
    std::vector<float> ones (kFrameSize, 1.0f);
    auto r = run (chain ({ waveSource ({ waveKey (0, ones) }), modifier (ComponentType::waveWindow, k) }));
    CHECK_EQ (frame (r, 0)[100], 0.0f);
    CHECK_EQ (frame (r, 0)[1900], 0.0f);
    CHECK_EQ (frame (r, 0)[1024], 1.0f);
    k.windowShape = 0; // cosine: zero at the edges, 1 in the middle
    r = run (chain ({ waveSource ({ waveKey (0, ones) }), modifier (ComponentType::waveWindow, k) }));
    CHECK_NEAR (frame (r, 0)[1024], 1.0, 1e-3);
    CHECK (frame (r, 0)[520] < 0.01f);
    // swapped edges give the same result
    std::swap (k.leftPosition, k.rightPosition);
    const auto r2 = run (chain ({ waveSource ({ waveKey (0, ones) }), modifier (ComponentType::waveWindow, k) }));
    CHECK (maxAbsDiff (frame (r, 0), frame (r2, 0)) == 0.0);
}

TEST (wt_frequency_filter)
{
    std::vector<float> w (kFrameSize);
    for (int i = 0; i < kFrameSize; ++i)
        w[static_cast<size_t> (i)] = 0.5f * (std::sin (2.0 * kPi * 2 * i / kFrameSize) + std::sin (2.0 * kPi * 200 * i / kFrameSize));
    Keyframe k;
    k.cutoff = 0.43f; // harmonic 2^4.3 = 19.7
    k.shape = 1.0f;
    k.style = 0;
    Component f = modifier (ComponentType::frequencyFilter, k);
    f.normalize = false;
    auto r = run (chain ({ waveSource ({ waveKey (0, w) }), f }));
    CHECK_NEAR (harmonicMag (frame (r, 0), 2), 0.5, 0.01);
    CHECK (harmonicMag (frame (r, 0), 200) < 0.01);
    f.keyframes[0].style = 2; // high pass
    r = run (chain ({ waveSource ({ waveKey (0, w) }), f }));
    CHECK (harmonicMag (frame (r, 0), 2) < 0.01);
    CHECK_NEAR (harmonicMag (frame (r, 0), 200), 0.5, 0.01);
    f.keyframes[0].style = 0;
    f.normalize = true;
    r = run (chain ({ waveSource ({ waveKey (0, w) }), f }));
    CHECK_NEAR (peakOf (frame (r, 0)), peakOf (w.data()), 1e-3);
}

TEST (wt_slew_limiter)
{
    std::vector<float> sq (kFrameSize);
    for (int i = 0; i < kFrameSize; ++i)
        sq[static_cast<size_t> (i)] = i < kFrameSize / 2 ? 1.0f : -1.0f;
    Keyframe k;
    k.upRunRise = 0.0f;
    k.downRunRise = 0.0f;
    auto r = run (chain ({ waveSource ({ waveKey (0, sq) }), modifier (ComponentType::slewLimiter, k) }));
    CHECK (maxAbsDiff (frame (r, 0), sq.data()) < 1e-6); // 0 = no limit
    k.upRunRise = k.downRunRise = 0.7f;
    r = run (chain ({ waveSource ({ waveKey (0, sq) }), modifier (ComponentType::slewLimiter, k) }));
    const double limit = 2.0 * std::exp2 (-7.0);
    for (int i = 1; i < kFrameSize; ++i)
        CHECK (std::fabs (frame (r, 0)[i] - frame (r, 0)[i - 1]) <= limit + 1e-6);
    CHECK (std::fabs (frame (r, 0)[0] - frame (r, 0)[kFrameSize - 1]) <= limit + 1e-6); // periodic
}

TEST (wt_wave_folder)
{
    Keyframe k;
    k.foldBoost = 0.0f;
    const auto in = sine (1);
    auto r = run (chain ({ waveSource ({ waveKey (0, in) }), modifier (ComponentType::waveFolder, k) }));
    CHECK (maxAbsDiff (frame (r, 0), in.data()) < 1e-5); // gain 1 inside -1 .. 1: identity
    k.foldBoost = 0.5f;
    r = run (chain ({ waveSource ({ waveKey (0, in) }), modifier (ComponentType::waveFolder, k) }));
    CHECK (peakOf (frame (r, 0)) <= 1.0 + 1e-6);
    CHECK (maxAbsDiff (frame (r, 0), in.data()) > 0.1);
}

TEST (wt_wave_warp)
{
    Keyframe k; // powers 0 = identity
    const auto in = sine (1);
    auto r = run (chain ({ waveSource ({ waveKey (0, in) }), modifier (ComponentType::waveWarp, k) }));
    CHECK (maxAbsDiff (frame (r, 0), in.data()) < 1e-4);
    k.horizontalPower = 4.0f;
    k.horizontalAsymmetric = true;
    r = run (chain ({ waveSource ({ waveKey (0, in) }), modifier (ComponentType::waveWarp, k) }));
    CHECK (maxAbsDiff (frame (r, 0), in.data()) > 0.1);
    k.horizontalPower = 0.0f;
    k.verticalPower = 4.0f;
    k.verticalAsymmetric = false;
    r = run (chain ({ waveSource ({ waveKey (0, in) }), modifier (ComponentType::waveWarp, k) }));
    CHECK (peakOf (frame (r, 0)) <= 1.0 + 1e-5);
    CHECK (frame (r, 0)[512] > 0.0f && frame (r, 0)[1536] < 0.0f); // sign kept
}

TEST (wt_groups_summed_and_post_process)
{
    Description d;
    d.groups.resize (2);
    d.groups[0].components.push_back (waveSource ({ waveKey (0, sine (1)) }));
    d.groups[1].components.push_back (waveSource ({ waveKey (0, sine (2)) }));
    auto r = run (d);
    std::vector<float> sum (kFrameSize);
    for (int i = 0; i < kFrameSize; ++i)
        sum[static_cast<size_t> (i)] = sine (1)[static_cast<size_t> (i)] + sine (2)[static_cast<size_t> (i)];
    CHECK (maxAbsDiff (frame (r, 0), sum.data()) < 1e-5);

    // frames = last position over ALL groups + 1
    d.groups[1].components[0].keyframes.push_back (waveKey (20, sine (2)));
    CHECK_EQ (run (d).numFrames, 21);

    // remove_all_dc + full_normalize
    std::vector<float> offset (kFrameSize);
    for (int i = 0; i < kFrameSize; ++i)
        offset[static_cast<size_t> (i)] = 0.25f + 0.2f * sine (1)[static_cast<size_t> (i)];
    Description p = single (waveSource ({ waveKey (0, offset) }));
    p.removeAllDc = true;
    r = run (p);
    double mean = 0;
    for (int i = 0; i < kFrameSize; ++i)
        mean += frame (r, 0)[i];
    CHECK_NEAR (mean / kFrameSize, 0.0, 1e-6);
    p.fullNormalize = true;
    r = run (p);
    CHECK_NEAR (peakOf (frame (r, 0)), 1.0, 1e-5);
}

TEST (wt_empty_and_unknown_cases)
{
    Description d;
    auto r = run (d);
    CHECK_EQ (r.numFrames, 1);
    CHECK_EQ (peakOf (frame (r, 0)), 0.0);
    d = single (waveSource ({}));
    r = run (d);
    CHECK_EQ (peakOf (frame (r, 0)), 0.0);
    // a short wave is zero padded, an out of range position is clamped
    Keyframe k = waveKey (9999, std::vector<float> (10, 1.0f));
    r = run (single (waveSource ({ k })));
    CHECK_EQ (r.numFrames, kMaxFrames);
}

TEST (wt_cancel_and_budget)
{
    Description d = single (waveSource ({ waveKey (0, sine (1)), waveKey (256, sine (2)) }));
    Result r;
    Options o;
    o.cancel = [] { return true; };
    CHECK (render (d, o, r) == Status::cancelled);
    Options b;
    b.maxOps = 1000;
    CHECK (render (d, b, r) == Status::budgetExceeded);
    // ops are counted: the default budget lets a maximal single-component table through
    CHECK (render (d, Options {}, r) == Status::ok);
    CHECK_EQ (r.numFrames, 257);
}

TEST (wt_random_descriptions_stay_finite)
{
    sbtest::Rng rng (7);
    for (int iter = 0; iter < 60; ++iter)
    {
        Description d;
        d.removeAllDc = rng.u32() & 1;
        d.fullNormalize = rng.u32() & 1;
        d.groups.resize (1 + rng.u32() % 3);
        for (auto& g : d.groups)
            for (int n = 1 + static_cast<int> (rng.u32() % 4); n > 0; --n)
            {
                Component c;
                c.type = static_cast<ComponentType> (rng.u32() % 10);
                c.interpolationStyle = static_cast<Interpolation> (rng.u32() % 3);
                c.frequencyInterpolation = rng.u32() & 1;
                c.normalize = rng.u32() & 1;
                c.fadeStyle = static_cast<int> (rng.u32() % 4);
                c.phaseStyle = static_cast<int> (rng.u32() % 3);
                c.windowSize = static_cast<int> (rng.u32());
                c.audioSampleRate = static_cast<float> (rng.uniform() * 1e9);
                c.normalizeMult = (rng.u32() & 1) != 0;
                c.normalizeGain = (rng.u32() & 1) != 0;
                const int pcm = static_cast<int> (rng.u32() % 3000);
                for (int i = 0; i < pcm; ++i)
                    c.audio.push_back (static_cast<int16_t> (rng.u32()));
                for (int ki = static_cast<int> (rng.u32() % 4); ki > 0; --ki)
                {
                    Keyframe k;
                    k.position = static_cast<int> (rng.u32() % 400) - 50;
                    if (rng.u32() & 1)
                        k.wave.assign (rng.u32() % 3000, static_cast<float> (rng.uniform() * 4.0 - 2.0));
                    const auto rf = [&] { return static_cast<float> (rng.uniform() * 40.0 - 20.0); };
                    k.line.x = { rf(), rf(), std::nanf ("") };
                    k.line.y = { rf(), rf(), rf() };
                    k.line.power = { rf(), 1e30f };
                    k.pullPower = rf(); k.startPosition = rf() * 1e5f; k.windowFade = rf(); k.windowSize = rf() * 1e5f;
                    k.phase = rf(); k.mix = rf(); k.style = static_cast<int> (rng.u32());
                    k.cutoff = rf(); k.shape = rf(); k.upRunRise = rf(); k.downRunRise = rf();
                    k.leftPosition = rf(); k.rightPosition = rf(); k.windowShape = rf(); k.foldBoost = rf() * 1e3f;
                    k.horizontalPower = rf(); k.verticalPower = rf();
                    k.horizontalAsymmetric = rng.u32() & 1; k.verticalAsymmetric = rng.u32() & 1;
                    c.keyframes.push_back (std::move (k));
                }
                g.components.push_back (std::move (c));
            }
        Result r;
        CHECK (render (d, Options {}, r) == Status::ok);
        CHECK_EQ (r.frames.size(), static_cast<size_t> (r.numFrames) * kFrameSize);
        bool allFinite = true;
        for (float v : r.frames)
            allFinite = allFinite && std::isfinite (v);
        CHECK (allFinite);
    }
}

TEST (wt_harmonic_helpers_round_trip)
{
    double a[65] = {}, b[65] = {};
    a[3] = 0.5;
    b[1] = 1.0;
    b[7] = -0.25;
    std::vector<float> frame (kFrameSize);
    synthesizeHarmonics (a, b, 64, frame.data());
    CHECK_NEAR (harmonicMag (frame.data(), 1), 1.0, 1e-4);
    CHECK_NEAR (harmonicMag (frame.data(), 3), 0.5, 1e-4);
    double ca[65], sb[65], mag[65];
    analyzeHarmonics (frame.data(), 64, ca, sb);
    CHECK_NEAR (ca[3], 0.5, 1e-5);
    CHECK_NEAR (sb[1], 1.0, 1e-5);
    CHECK_NEAR (sb[7], -0.25, 1e-5);
    CHECK_NEAR (ca[0], 0.0, 1e-6);
    harmonicMagnitudes (frame.data(), 64, mag);
    CHECK_NEAR (mag[7], 0.25, 1e-5);
    CHECK_NEAR (mag[2], 0.0, 1e-5);
}
