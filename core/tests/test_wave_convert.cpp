// SynthyBoy - core tests of GROUPD (WP-14 / WP-15 / WP-16, 13-new-keys.md 4.40): sample region -> wavetable, wavetable frame <-> LFO shape, the audio of a render
// as WAV, and the offline render driver on the own engine (the engine of the mobile shell). The Vital engine's parts are in engine-vital/tests/test_groupd.cpp.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "groupd_util.h"
#include "synthyboy/engine.h"
#include "synthyboy/offline_render.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
using namespace groupd;

namespace
{
double frameDifference (const wtedit::Frame& a, const wtedit::Frame& b)
{
    double worst = 0.0;
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        worst = std::max (worst, static_cast<double> (std::fabs (a[i] - b[i])));
    return worst;
}

wtedit::Frame sineFrame (int harmonic = 1, float amp = 0.9f)
{
    wtedit::Frame f;
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        f[i] = amp * static_cast<float> (std::sin (2.0 * kPi * harmonic * static_cast<double> (i) / wtedit::kWaveformSize));
    return f;
}
} // namespace

TEST (switch_to_wavetable_converts_the_start_end_region_of_the_sample)
{
    const double sr = 44100.0;
    const auto tone = sawTone (220.0, 1.0, sr, 12);
    wtedit::ImportOptions options; // Pitch Average
    wtedit::Wavetable table;
    std::string error;
    CHECK (convert::sampleRegionToWavetable (tone.data(), tone.size(), sr, 0.25f, 0.75f, options, "Saw region", table, &error));
    CHECK (table.numFrames() >= 1);
    CHECK_EQ (table.name(), std::string ("Saw region"));

    // the first frame is one cycle of the sample: its spectrum has the harmonic series of the source (1/h) within 1 dB
    const auto& frame = table.getFrame (0);
    std::vector<double> amp;
    for (int h = 1; h <= 8; ++h) // a whole cycle: the plain DFT bin h is exact
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        {
            re += frame[i] * std::cos (2.0 * kPi * h * static_cast<double> (i) / wtedit::kWaveformSize);
            im += frame[i] * std::sin (2.0 * kPi * h * static_cast<double> (i) / wtedit::kWaveformSize);
        }
        amp.push_back (20.0 * std::log10 (std::max (std::sqrt (re * re + im * im), 1.0e-9)));
    }
    for (int h = 2; h <= 8; ++h)
        CHECK_NEAR (amp[static_cast<size_t> (h - 1)] - amp[0], -20.0 * std::log10 (static_cast<double> (h)), 1.0);

    // START .. END too short, empty, outside: refused with a reason
    CHECK (! convert::sampleRegionToWavetable (tone.data(), tone.size(), sr, 0.5f, 0.5001f, options, "x", table, &error));
    CHECK (! error.empty());
    CHECK (! convert::sampleRegionToWavetable (nullptr, 0, sr, 0.0f, 1.0f, options, "x", table, &error));
    CHECK (! convert::sampleRegionToWavetable (tone.data(), tone.size(), sr, 0.9f, 0.1f, options, "x", table, &error));

    // every entry of the menu gives a table from a long enough region
    const auto& choices = convert::importChoices();
    CHECK_EQ (choices.size(), static_cast<size_t> (9));
    for (const auto& choice : choices)
    {
        wtedit::ImportOptions o;
        o.mode = choice.mode;
        wtedit::Wavetable t;
        CHECK (convert::sampleRegionToWavetable (tone.data(), tone.size(), sr, 0.0f, 1.0f, o, "m", t, &error));
        CHECK (t.numFrames() >= 1);
    }
}

TEST (wavetable_frame_to_lfo_shape_and_back_stays_within_the_error_bound)
{
    struct Case { const char* name; wtedit::Frame frame; float tolerance; };
    std::vector<Case> cases;
    cases.push_back ({ "sine", sineFrame(), 0.002f });
    cases.push_back ({ "third harmonic", sineFrame (3), 0.002f });
    wtedit::Frame saw, square, tri;
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
    {
        const float x = static_cast<float> (i) / static_cast<float> (wtedit::kWaveformSize);
        saw[i] = 2.0f * x - 1.0f;
        square[i] = x < 0.5f ? 0.8f : -0.8f;
        tri[i] = x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;
    }
    cases.push_back ({ "saw", saw, 0.002f });
    cases.push_back ({ "square", square, 0.002f });
    cases.push_back ({ "triangle", tri, 0.002f });
    for (const auto& c : cases)
    {
        ContentLfoShape shape;
        float reached = 1.0f;
        CHECK (convert::frameToLfoShape (c.frame, 3, shape, c.tolerance, kMaxLfoShapePoints, &reached));
        CHECK_EQ (shape.lfo, 3);
        CHECK (shape.points.size() >= 2 && static_cast<int> (shape.points.size()) <= kMaxLfoShapePoints);
        CHECK_EQ (shape.points.front().x, 0.0f);
        CHECK_EQ (shape.points.back().x, 1.0f);
        CHECK (reached <= c.tolerance);
        wtedit::Frame back;
        convert::lfoShapeToFrame (shape, back);
        // the LFO range 0..1 is the frame range -1..+1: an LFO error e is a frame error of 2 e
        const double err = frameDifference (c.frame, back);
        std::printf ("    %-15s points %3zu  frame -> LFO -> frame max error %.5f\n", c.name, shape.points.size(), err);
        CHECK (err <= 2.0 * c.tolerance + 1.0e-4);
    }

    // a frame that no 128 straight segments follow (noise): the points are capped and the reported error is the real one
    wtedit::Frame noise;
    sbtest::Rng rng (7);
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        noise[i] = static_cast<float> (rng.uniform() * 1.8 - 0.9);
    ContentLfoShape shape;
    float reached = 0.0f;
    CHECK (convert::frameToLfoShape (noise, 1, shape, 0.002f, 64, &reached));
    CHECK_EQ (static_cast<int> (shape.points.size()), 64);
    wtedit::Frame back;
    convert::lfoShapeToFrame (shape, back);
    CHECK_NEAR (frameDifference (noise, back), 2.0 * static_cast<double> (reached), 1.0e-3);
    CHECK (! convert::frameToLfoShape (noise, 1, shape, 0.002f, 1));

    // a frame beyond +-1 is scaled to fit instead of clipped
    wtedit::Frame loud = sineFrame (1, 3.0f);
    CHECK (convert::frameToLfoShape (loud, 1, shape, 0.002f));
    convert::lfoShapeToFrame (shape, back);
    CHECK_NEAR (back[512], 1.0, 0.01);
}

TEST (lfo_shape_to_frame_to_lfo_keeps_the_curve_powers_and_the_loop_of_a_closed_shape)
{
    // a shape with curved segments, smoothing off: shape -> frame -> shape -> frame, the second frame equals the first within the point budget
    ContentLfoShape shape;
    shape.lfo = 2;
    shape.points = { ContentPoint (0.0f, 0.0f, 3.0f), ContentPoint (0.3f, 1.0f, -2.0f), ContentPoint (0.6f, 0.2f, 0.0f), ContentPoint (1.0f, 0.7f, 0.0f) };
    wtedit::Frame first, second;
    convert::lfoShapeToFrame (shape, first);
    CHECK (first[0] < -0.99f);          // y 0 -> -1
    CHECK (first[614] > 0.9f);          // the peak at x 0.3
    ContentLfoShape reduced;
    float reached = 0.0f;
    CHECK (convert::frameToLfoShape (first, 2, reduced, 0.002f, kMaxLfoShapePoints, &reached));
    convert::lfoShapeToFrame (reduced, second);
    CHECK (frameDifference (first, second) <= 2.0 * 0.002 + 1.0e-4);

    // smoothing is part of the sampled frame
    ContentLfoShape smooth = shape;
    smooth.smooth = true;
    wtedit::Frame smoothFrame;
    convert::lfoShapeToFrame (smooth, smoothFrame);
    CHECK (frameDifference (first, smoothFrame) > 0.01);
}

TEST (interpolated_frame_blends_neighbours_linearly)
{
    wtedit::Wavetable table;
    table.setFrame (0, sineFrame (1, 1.0f));
    table.insertFrame (1, sineFrame (2, 1.0f));
    table.insertFrame (2, sineFrame (3, 1.0f));
    CHECK_EQ (frameDifference (convert::interpolatedFrame (table, 0.0f), table.getFrame (0)), 0.0);
    CHECK_EQ (frameDifference (convert::interpolatedFrame (table, 1.0f), table.getFrame (2)), 0.0);
    CHECK_EQ (frameDifference (convert::interpolatedFrame (table, 0.5f), table.getFrame (1)), 0.0);
    const auto quarter = convert::interpolatedFrame (table, 0.25f);
    for (size_t i = 0; i < wtedit::kWaveformSize; i += 97)
        CHECK_NEAR (quarter[i], 0.5f * (table.getFrame (0)[i] + table.getFrame (1)[i]), 1.0e-6);
}

TEST (audio_of_a_render_becomes_a_24_bit_wav_without_clipping)
{
    OfflineAudio audio;
    audio.sampleRate = 48000.0;
    for (int i = 0; i < 4800; ++i)
    {
        audio.left.push_back (2.0f * static_cast<float> (std::sin (i * 0.05)));  // above full scale
        audio.right.push_back (0.25f * static_cast<float> (std::sin (i * 0.03)));
    }
    const auto wav = convert::audioToWav (audio, 24);
    CHECK (wav.size() == 44 + 4800 * 6);
    CHECK (std::memcmp (wav.data(), "RIFF", 4) == 0 && std::memcmp (wav.data() + 8, "WAVE", 4) == 0);
    SampleFacts facts;
    CHECK (probeSampleWav (wav, "note.wav", facts));
    CHECK_EQ (facts.sampleRate, 48000);
    CHECK_EQ (facts.frames, 4800);
    auto preview = SamplePreview::create (wav, "note.wav");
    CHECK (preview != nullptr);
    float p = 0.0f;
    for (float v : preview->mono())
        p = std::max (p, std::fabs (v));
    CHECK (p <= 1.0f && p > 0.5f);   // the mono mix of the scaled channels
    OfflineAudio empty;
    CHECK (convert::audioToWav (empty).empty());
}

TEST (offline_render_on_the_own_engine_is_deterministic_and_leaves_the_live_engine_alone)
{
    const auto holdAndRender = [] (bool withOffline) -> std::vector<float>
    {
        auto live = createOwnEngine();
        live->prepare (48000.0, 256, qualityForTier (Tier::medium));
        std::vector<float> out;
        std::vector<float> l (256), r (256);
        for (int b = 0; b < 40; ++b)
        {
            std::vector<Event> ev;
            if (b == 0)
                ev.push_back (sbtest::noteOn (0, 60, 0.9f));
            if (b == 10 && withOffline)
            {
                OfflineRequest request;
                request.steps.resize (1);
                request.steps[0].seconds = 0.5;
                OfflineNote n;
                n.key = 72;
                n.lengthSeconds = 0.4;
                request.steps[0].notes.push_back (n);
                request.tailSeconds = 0.2;
                OfflineAudio a, c;
                CHECK (renderOfflineAny (*live, EngineKind::own, nullptr, nullptr, request, a));
                CHECK (renderOfflineAny (*live, EngineKind::own, nullptr, nullptr, request, c));
                CHECK (a.left == c.left && a.right == c.right);   // repeatable
                CHECK (a.left.size() >= 24000);
                CHECK (sbtest::peak (a.left) > 0.01f);
                CHECK_EQ (a.stepStart.size(), static_cast<size_t> (1));
            }
            live->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 256);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    // the live engine plays on exactly as if nothing had been rendered: the render ran on its own instance
    CHECK (holdAndRender (false) == holdAndRender (true));
}

TEST (offline_request_validation_and_limits)
{
    auto e = createOwnEngine();
    e->prepare (48000.0, 256, Quality {});
    OfflineAudio a;
    OfflineRequest empty;
    CHECK (! renderOfflineAny (*e, EngineKind::own, nullptr, nullptr, empty, a));
    OfflineRequest tooLong;
    tooLong.steps.resize (1);
    tooLong.steps[0].seconds = kMaxOfflineSeconds + 1.0;
    CHECK (! renderOfflineAny (*e, EngineKind::own, nullptr, nullptr, tooLong, a));
    OfflineRequest badRate;
    badRate.sampleRate = 100.0;
    badRate.steps.resize (1);
    CHECK (! renderOfflineAny (*e, EngineKind::own, nullptr, nullptr, badRate, a));
    OfflineRequest ok;
    ok.steps.resize (2);
    ok.steps[0].seconds = 0.1;
    ok.steps[1].seconds = 0.2;
    CHECK (renderOfflineAny (*e, EngineKind::own, nullptr, nullptr, ok, a));
    CHECK_EQ (a.stepStart.size(), static_cast<size_t> (2));
    CHECK_EQ (a.stepStart[0], static_cast<size_t> (0));
    CHECK_EQ (a.stepStart[1], static_cast<size_t> (4800));
    CHECK_EQ (a.frames(), static_cast<size_t> (14400));
}
