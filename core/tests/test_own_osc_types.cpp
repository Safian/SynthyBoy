// SynthyBoy - own engine tests: the sample, granular, spectral and multisample oscillator types, user wavetables, the warps Remap / Odd-Even / Filter, the
// spectral morph and the memory of the wavetable bank (docs/engine-spec/07 section 8, vectors S1 .. S5, and 02 section 13).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "osc/wavetable_bank.h"
#include "own_engine.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/offline_render.h"
#include "synthyboy/spectral/start_cache.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;

struct Out
{
    std::vector<float> l, r;
};

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

std::vector<uint8_t> sineWav (double hz, double amp, int rate, double seconds)
{
    const size_t n = static_cast<size_t> (rate * seconds);
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float> (amp * std::sin (6.283185307179586 * hz * static_cast<double> (i) / rate));
    return encodeWav (x.data(), nullptr, n, rate, 16);
}

/** A sine of hz whose amplitude rises linearly from 0 to amp over the file: every position of the file has its own level and its own phase, so a loop that
    is left differs from one that is kept by far more than any threshold (docs/dsp-knowledge/testing.md: no signal whose whole periods line up with the loop). */
std::vector<uint8_t> risingSineWav (double hz, double amp, int rate, double seconds)
{
    const size_t n = static_cast<size_t> (rate * seconds);
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float> (amp * (static_cast<double> (i) / static_cast<double> (n)) * std::sin (6.283185307179586 * hz * static_cast<double> (i) / rate));
    return encodeWav (x.data(), nullptr, n, rate, 16);
}

struct Rig
{
    MemoryContentStore store;
    StateContent content;
    std::unique_ptr<IEngine> e;

    explicit Rig (bool withSample = true, const char* extraKey = nullptr, float extraValue = 0.0f)
    {
        e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->prepare (kSr, 256, qualityForTier (Tier::high));
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "osc1.random_phase", 0.0f);
        if (extraKey != nullptr)
            set (*e, extraKey, extraValue);
        if (withSample)
        {
            store.add ("test", "sine.wav", sineWav (440.0, 0.8, 44100, 2.0));
            ContentFileRef ref;
            ref.pack = "test";
            ref.file = "sine.wav";
            ref.name = "sine";
            attachSample (content, 1, ref, 44100, 88200, 1);
            e->setContentStore (&store);
            e->setContent (content);
        }
        set (*e, "osc1.sample.root", 69.0f);
        set (*e, "osc1.sample.loop_mode", 1.0f);
    }

    Out render (int key, double noteSeconds, double totalSeconds)
    {
        const size_t total = static_cast<size_t> (totalSeconds * kSr), offAt = static_cast<size_t> (noteSeconds * kSr);
        Out o;
        o.l.assign (total, 0.0f);
        o.r.assign (total, 0.0f);
        size_t pos = 0;
        while (pos < total)
        {
            const size_t n = std::min<size_t> (256, total - pos);
            Event ev[2];
            int ne = 0;
            if (pos == 0)
                ev[ne++] = sbtest::noteOn (0, key, 1.0f);
            if (offAt >= pos && offAt < pos + n)
                ev[ne++] = sbtest::noteOff (static_cast<int> (offAt - pos), key);
            e->process (ev, ne, o.l.data() + pos, o.r.data() + pos, static_cast<int> (n));
            pos += n;
        }
        return o;
    }
};

/** Amplitude (linear) of the spectral line nearest `hz` in x[a, b) (Hann window, +-10 % search). Returns the line's amplitude and its frequency in `peakHz`. */
double lineAmplitude (const std::vector<float>& x, size_t a, size_t b, double hz, double* peakHz = nullptr)
{
    double best = 0.0, bestHz = hz;
    const size_t n = b - a;
    double wsum = 0.0;
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i)
    {
        w[i] = 0.5 - 0.5 * std::cos (6.283185307179586 * static_cast<double> (i) / static_cast<double> (n));
        wsum += w[i];
    }
    for (double f = hz * 0.9; f <= hz * 1.1; f += hz * 0.0005 + 0.01)
    {
        std::complex<double> acc (0.0, 0.0);
        for (size_t i = 0; i < n; ++i)
            acc += w[i] * static_cast<double> (x[a + i]) * std::polar (1.0, -6.283185307179586 * f * static_cast<double> (i) / kSr);
        const double amp = 2.0 * std::abs (acc) / wsum;
        if (amp > best)
        {
            best = amp;
            bestHz = f;
        }
    }
    if (peakHz != nullptr)
        *peakHz = bestHz;
    return best;
}

double db (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

double peakOf (const std::vector<float>& x, size_t a, size_t b)
{
    double p = 0.0;
    for (size_t i = a; i < b; ++i)
        p = std::max (p, static_cast<double> (std::fabs (x[i])));
    return p;
}
} // namespace

// S1: the sample type plays the loaded sample at the key pitch
TEST (own_osc_sample_type_pitch_and_level)
{
    Rig r (true, "osc1.type", 1.0f);
    double hz = 0.0;
    Out o = r.render (69, 1.0, 1.0);
    const double amp = lineAmplitude (o.l, 14400, 43200, 440.0, &hz);
    std::printf ("    S1 note 69: %.2f Hz, %.2f dB, peak %.3f\n", hz, db (amp), peakOf (o.l, 14400, 43200));
    CHECK_NEAR (hz, 440.0, 1.0);
    CHECK_NEAR (db (amp), -4.93, 1.0); // the mono sample voice costs 3 dB (centre pan law)
    Rig r2 (true, "osc1.type", 1.0f);
    o = r2.render (81, 1.0, 1.0);
    lineAmplitude (o.l, 14400, 43200, 880.0, &hz);
    CHECK_NEAR (hz, 880.0, 1.5);
    Rig r3 (true, "osc1.type", 1.0f);
    set (*r3.e, "osc1.transpose", 12.0f);
    o = r3.render (69, 1.0, 1.0);
    lineAmplitude (o.l, 14400, 43200, 880.0, &hz);
    CHECK_NEAR (hz, 880.0, 1.5);
    Rig r4 (true, "osc1.type", 1.0f);
    set (*r4.e, "osc1.sample.keytrack", 0.0f);
    o = r4.render (81, 1.0, 1.0);
    lineAmplitude (o.l, 14400, 43200, 440.0, &hz);
    CHECK_NEAR (hz, 440.0, 1.0);
}

// S2: two unison voices spread hard left and right at -200 / +200 ct
TEST (own_osc_sample_type_unison_is_a_line_of_voices_panned_across)
{
    Rig r (true, "osc1.type", 1.0f);
    set (*r.e, "osc1.unison.voices", 2.0f);
    set (*r.e, "osc1.unison.detune", 100.0f);
    set (*r.e, "osc1.unison.range", 2.0f);
    set (*r.e, "osc1.unison.detune_curve", 0.0f);
    set (*r.e, "osc1.unison.spread", 100.0f);
    set (*r.e, "osc1.unison.blend", 100.0f);
    const Out o = r.render (69, 1.0, 1.0);
    double hl = 0.0, hr = 0.0;
    const double al = lineAmplitude (o.l, 14400, 43200, 392.03, &hl);
    const double ar = lineAmplitude (o.r, 14400, 43200, 493.91, &hr);
    std::printf ("    S2 left %.2f Hz %.2f dB, right %.2f Hz %.2f dB\n", hl, db (al), hr, db (ar));
    CHECK_NEAR (hl, 392.03, 1.5);
    CHECK_NEAR (hr, 493.91, 1.5);
    CHECK_NEAR (db (al), -4.94, 1.0);
    CHECK_NEAR (db (ar), -4.94, 1.0);
}

// S3b: the display's read-out: the read head of the sample type moves along the file (0 .. 1), -1 before a note and for another type
TEST (own_osc_sample_type_read_head_is_live)
{
    Rig r (true, "osc1.type", 1.0f);
    set (*r.e, "osc1.sample.loop_mode", 0.0f);
    LiveValues live;
    std::vector<float> l (256), rr (256);
    r.e->process (nullptr, 0, l.data(), rr.data(), 256);
    CHECK (r.e->copyReadout (live));
    CHECK (live.sampleHead[0] < 0.0f);
    Event on = sbtest::noteOn (0, 69, 1.0f);
    r.e->process (&on, 1, l.data(), rr.data(), 256);
    CHECK (r.e->copyReadout (live));
    const float first = live.sampleHead[0];
    for (int i = 0; i < 40; ++i)
        r.e->process (nullptr, 0, l.data(), rr.data(), 256);
    CHECK (r.e->copyReadout (live));
    std::printf ("    S3b read head after one block %.4f, after 41 blocks %.4f\n", first, live.sampleHead[0]);
    CHECK (first >= 0.0f && first < 0.05f);
    CHECK (live.sampleHead[0] > first + 0.08f && live.sampleHead[0] <= 1.0f);   // 41 blocks of 256 at 44.1 kHz on a 2 s file: about a fifth of the way
    CHECK (live.sampleHead[1] < 0.0f && live.sampleHead[2] < 0.0f);
    Rig w (false);
    Event on2 = sbtest::noteOn (0, 69, 1.0f);
    w.e->process (&on2, 1, l.data(), rr.data(), 256);
    CHECK (w.e->copyReadout (live));
    CHECK (live.sampleHead[0] < 0.0f);              // a wavetable oscillator has no read head
}

// the display's read-outs of the voices: the sample type reports the read head of every one of the newest voices (up to 8), -1 for the others; the sub oscillator reports its phase per voice
TEST (own_osc_read_outs_per_voice_and_the_sub_phase)
{
    Rig r (true, "osc1.type", 1.0f);
    set (*r.e, "osc1.sample.loop_mode", 0.0f);
    set (*r.e, "sub.on", 1.0f);
    set (*r.e, "sub.level", 100.0f);
    LiveValues live;
    std::vector<float> l (256), rr (256);
    Event on = sbtest::noteOn (0, 69, 1.0f);
    r.e->process (&on, 1, l.data(), rr.data(), 256);
    for (int i = 0; i < 10; ++i)
        r.e->process (nullptr, 0, l.data(), rr.data(), 256);
    Event on2 = sbtest::noteOn (0, 72, 1.0f);
    r.e->process (&on2, 1, l.data(), rr.data(), 256);
    CHECK (r.e->copyReadout (live));
    std::printf("    voices: newest %.4f, older %.4f, third %.4f; sub phase %.3f %.3f\n", live.voicePos[0][0], live.voicePos[0][1], live.voicePos[0][2], live.subPhase[0], live.subPhase[1]);
    CHECK (live.voicePos[0][0] >= 0.0f && live.voicePos[0][1] > live.voicePos[0][0] + 0.02f);   // the older voice is further along the file
    CHECK (live.voicePos[0][2] < 0.0f && live.voicePos[0][7] < 0.0f);                              // no third voice
    CHECK (live.voicePos[1][0] < 0.0f && live.voicePos[2][0] < 0.0f);                              // the other oscillators are not of a sample type
    CHECK (live.subPhase[0] >= 0.0f && live.subPhase[0] <= 1.0f && live.subPhase[1] >= 0.0f && live.subPhase[2] < 0.0f);
    CHECK (live.noisePos < 0.0f);
    const float ph0 = live.subPhase[0];
    r.e->process (nullptr, 0, l.data(), rr.data(), 256);
    CHECK (r.e->copyReadout (live));
    CHECK (std::fabs (live.subPhase[0] - ph0) > 1.0e-4f);   // the phase moves with the block
}

// the granular oscillator reports where its grains read and how far they have run (up to 32), and the scan position of the cloud
TEST (own_osc_granular_read_out_has_the_grains)
{
    Rig r (true, "osc1.type", 2.0f);
    set (*r.e, "osc1.gran.density", 80.0f);
    LiveValues live;
    std::vector<float> l (256), rr (256);
    CHECK (r.e->copyReadout (live));
    CHECK (live.numGrains[0] == 0);
    Event on = sbtest::noteOn (0, 69, 1.0f);
    r.e->process (&on, 1, l.data(), rr.data(), 256);
    for (int i = 0; i < 30; ++i)
        r.e->process (nullptr, 0, l.data(), rr.data(), 256);
    CHECK (r.e->copyReadout (live));
    std::printf ("    granular: %d grains, first at %.3f life %.3f, scan %.3f\n", live.numGrains[0], live.grainPos[0][0], live.grainLife[0][0], live.voicePos[0][0]);
    CHECK (live.numGrains[0] >= 2 && live.numGrains[0] <= LiveValues::kReadGrains);
    bool ok = true;
    for (int g = 0; g < live.numGrains[0]; ++g)
        ok = ok && live.grainPos[0][g] >= 0.0f && live.grainPos[0][g] <= 1.0f && live.grainLife[0][g] >= 0.0f && live.grainLife[0][g] <= 1.0f;
    CHECK (ok);
    CHECK (live.voicePos[0][0] >= 0.0f && live.voicePos[0][0] <= 1.0f);
    CHECK (live.numGrains[1] == 0 && live.numGrains[2] == 0);
}

// the same with the one-shot mode of the loop and a note held for seconds (what the device does): the grains go on being reported while the cloud plays
TEST (own_osc_granular_read_out_one_shot_held_note)
{
    Rig r (true, "osc1.type", 2.0f);
    set (*r.e, "osc1.sample.loop_mode", 0.0f);
    LiveValues live;
    std::vector<float> l (256), rr (256);
    Event on = sbtest::noteOn (0, 69, 1.0f);
    r.e->process (&on, 1, l.data(), rr.data(), 256);
    int seen = 0, blocks = 0;
    for (int i = 0; i < 400; ++i)
    {
        r.e->process (nullptr, 0, l.data(), rr.data(), 256);
        if (r.e->copyReadout (live) && live.numGrains[0] > 0)
            ++seen;
        ++blocks;
    }
    std::printf ("    granular one-shot: grains reported in %d of %d blocks, last scan %.3f, %d grains now\n", seen, blocks, live.voicePos[0][0], live.numGrains[0]);
    CHECK (seen > blocks / 2);
}

// the multisample oscillator reports where the zone's file is being read, per voice
TEST (own_osc_multisample_read_out_has_the_position)
{
    MemoryContentStore store;
    store.add ("o", "inst/p.wav", sineWav (440.0, 0.8, 44100, 2.0));
    const std::string sfz = "<region> sample=p.wav lokey=0 hikey=127 pitch_keycenter=69 loop_mode=one_shot\n";
    store.add ("o", "inst/p.sfz", std::vector<uint8_t> (sfz.begin(), sfz.end()));
    StateContent c;
    ContentFileRef i;
    i.pack = "o";
    i.file = "inst/p.sfz";
    attachInstrument (c, 1, i, 1);
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (kSr, 256, qualityForTier (Tier::high));
    set (*e, "filter1.on", 0.0f);
    set (*e, "master.volume", 0.0f);
    set (*e, "env1.attack", 0.0f);
    set (*e, "osc1.level", 100.0f);
    set (*e, "osc1.type", 4.0f);
    e->setContentStore (&store);
    e->setContent (c);
    LiveValues live;
    std::vector<float> l (256), rr (256);
    Event on = sbtest::noteOn (0, 69, 1.0f);
    e->process (&on, 1, l.data(), rr.data(), 256);
    CHECK (e->copyReadout (live));
    const float first = live.voicePos[0][0];
    for (int k = 0; k < 40; ++k)
        e->process (nullptr, 0, l.data(), rr.data(), 256);
    CHECK (e->copyReadout (live));
    std::printf ("    multisample position %.4f -> %.4f\n", first, live.voicePos[0][0]);
    CHECK (first >= 0.0f && first < 0.05f && live.voicePos[0][0] > first + 0.08f && live.voicePos[0][0] <= 1.0f);
}

// Multisample oscillator gain ramp: verifies that oscillator level changes ramp per sample across the 32-sample chunk
// instead of stepping once per control period (finding rank 24). Reuses setup from own_osc_multisample_read_out_has_the_position.
TEST (own_osc_multisample_gain_is_ramped_per_sample)
{
    const int rate = 44100;
    const double seconds = 1.0;
    const size_t totalFrames = static_cast<size_t> (rate * seconds);
    std::vector<float> constSample (totalFrames, 1.0f);

    MemoryContentStore store;
    store.add ("o", "inst/p.wav", encodeWav (constSample.data(), nullptr, constSample.size(), rate, 24));
    const std::string sfz = "<region> sample=p.wav lokey=0 hikey=127 pitch_keycenter=69 loop_mode=one_shot\n";
    store.add ("o", "inst/p.sfz", std::vector<uint8_t> (sfz.begin(), sfz.end()));
    StateContent c;
    ContentFileRef instRef;
    instRef.pack = "o";
    instRef.file = "inst/p.sfz";
    attachInstrument (c, 1, instRef, 1);

    auto setupEngine = [&] ()
    {
        auto e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->setParam (kParamMasterVolume, 0.0f);
        e->prepare (static_cast<double> (rate), 256, qualityForTier (Tier::high));
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "osc1.type", 4.0f);
        e->setContentStore (&store);
        e->setContent (c);
        return e;
    };

    // STEP 1: verify that with a FIXED gain the output is constant to 1e-6 over 0.2 s
    float fixedVal = 0.0f;
    {
        auto e = setupEngine();
        set (*e, "osc1.level", 100.0f);

        const int fixedFrames = 256 + static_cast<int> (rate * 0.2); // 256 frames onset + 8820 frames (0.2 s)
        std::vector<float> fixL (static_cast<size_t> (fixedFrames)), fixR (static_cast<size_t> (fixedFrames));
        Event on = sbtest::noteOn (0, 69, 1.0f);
        int pos = 0;
        while (pos < fixedFrames)
        {
            const int n = std::min (32, fixedFrames - pos);
            if (pos == 0)
                e->process (&on, 1, fixL.data() + pos, fixR.data() + pos, n);
            else
                e->process (nullptr, 0, fixL.data() + pos, fixR.data() + pos, n);
            pos += n;
        }
        fixedVal = fixL[256];
        float minFix = fixedVal, maxFix = fixedVal;
        for (int k = 256; k < fixedFrames; ++k)
        {
            minFix = std::min (minFix, fixL[static_cast<size_t> (k)]);
            maxFix = std::max (maxFix, fixL[static_cast<size_t> (k)]);
        }
        CHECK (maxFix - minFix < 1.0e-6f);
    }

    // STEP 2 & 3: rising gain ramp verification
    auto e = setupEngine();
    const int riseFrames = 22050; // 0.5 s at 44100 Hz
    std::vector<float> l (static_cast<size_t> (riseFrames)), r (static_cast<size_t> (riseFrames));
    Event on = sbtest::noteOn (0, 69, 1.0f);
    int pos = 0;
    while (pos < riseFrames)
    {
        const int n = std::min (32, riseFrames - pos);
        const float targetLevel = (static_cast<float> (pos) / static_cast<float> (riseFrames)) * 100.0f;
        set (*e, "osc1.level", targetLevel);
        if (pos == 0)
            e->process (&on, 1, l.data() + pos, r.data() + pos, n);
        else
            e->process (nullptr, 0, l.data() + pos, r.data() + pos, n);
        pos += n;
    }

    float globalMaxSampleStep = 0.0f;
    float globalMaxPeriodRise = 0.0f;
    const int startPeriod = 8; // skip initial onset / anti-click (sample 256)
    const int endPeriod = (riseFrames - 1) / 32;
    for (int p = startPeriod; p < endPeriod; ++p)
    {
        const int pStart = p * 32;
        float pMinStep = 1e9f, pMaxStep = 0.0f;
        float pSumStep = 0.0f;
        for (int i = 0; i < 32; ++i)
        {
            const float s = std::fabs (l[static_cast<size_t> (pStart + i + 1)] - l[static_cast<size_t> (pStart + i)]);
            pMinStep = std::min (pMinStep, s);
            pMaxStep = std::max (pMaxStep, s);
            pSumStep += (l[static_cast<size_t> (pStart + i + 1)] - l[static_cast<size_t> (pStart + i)]);
            globalMaxSampleStep = std::max (globalMaxSampleStep, s);
        }
        const float periodRise = l[static_cast<size_t> (pStart + 32)] - l[static_cast<size_t> (pStart)];
        globalMaxPeriodRise = std::max (globalMaxPeriodRise, periodRise);

        // Linear inside the period: all 32 differences equal within 2 percent of period step
        const float avgStep = pSumStep / 32.0f;
        if (avgStep > 1e-6f)
            CHECK ((pMaxStep - pMinStep) <= 0.02f * avgStep);

        // Sum of 32 differences equals change of gain over the period
        CHECK (std::fabs (pSumStep - periodRise) < 1.0e-6f);
    }

    // Largest per-sample step is at most (1 / 32) of the largest per-period rise times 1.05
    CHECK (globalMaxSampleStep <= (globalMaxPeriodRise / 32.0f) * 1.05f);

    std::printf ("    multisample gain ramp: fixed = %.7f, maxStep = %.7f, maxPeriodRise = %.7f, rampedStep = %.7f\n",
                 fixedVal, globalMaxSampleStep, globalMaxPeriodRise, globalMaxPeriodRise / 32.0f);
}

// S4: granular and spectral types play the same sample at the same pitch and at a level in the range of the Sample type
TEST (own_osc_granular_and_spectral_types_play_the_sample)
{
    const double expect[2] = { -1.49, -2.90 };
    for (int t = 2; t <= 3; ++t)
    {
        Rig r (true, "osc1.type", static_cast<float> (t));
        const Out o = r.render (69, 1.0, 1.2);
        double hz = 0.0;
        const double amp = lineAmplitude (o.l, 14400, 43200, 440.0, &hz);
        std::printf ("    S4 type %d: %.2f Hz, %.2f dB (vector %.2f), peak %.3f\n", t, hz, db (amp), expect[t - 2], peakOf (o.l, 14400, 43200));
        CHECK_NEAR (hz, 440.0, 2.0);
        CHECK_NEAR (db (amp), expect[t - 2], 1.5);
    }
}

// S5: a sample type without content is not silent: the missing file is tolerated and flagged, the oscillator plays its built-in wavetable (docs/ARCHITECTURE.md section 5, test_own_content_policy)
TEST (own_osc_sample_type_without_content_plays_the_built_in_wavetable)
{
    for (int t = 1; t <= 4; ++t)
    {
        Rig r (false, "osc1.type", static_cast<float> (t));
        const Out o = r.render (69, 0.5, 0.6);
        CHECK (peakOf (o.l, 0, o.l.size()) > 0.01);
        CHECK (peakOf (o.r, 0, o.r.size()) > 0.01);
    }
}

// S3: changing the type while a note sounds crosses over smoothly (10 ms)
TEST (own_osc_type_change_under_a_note_does_not_click)
{
    Rig r (true);
    set (*r.e, "osc1.wavetable", 0.0f); // sine
    const size_t total = static_cast<size_t> (0.6 * kSr);
    std::vector<float> l (total), rr (total);
    size_t pos = 0;
    bool switched = false;
    while (pos < total)
    {
        const size_t n = std::min<size_t> (128, total - pos);
        Event ev[1];
        int ne = 0;
        if (pos == 0)
            ev[ne++] = sbtest::noteOn (0, 69, 1.0f);
        if (!switched && pos >= static_cast<size_t> (0.3 * kSr))
        {
            set (*r.e, "osc1.type", 1.0f);
            switched = true;
        }
        r.e->process (ev, ne, l.data() + pos, rr.data() + pos, static_cast<int> (n));
        pos += n;
    }
    double worst = 0.0;
    for (size_t i = static_cast<size_t> (0.28 * kSr); i + 1 < total; ++i)
        worst = std::max (worst, static_cast<double> (std::fabs (l[i + 1] - l[i])));
    std::printf ("    S3 largest sample step across the type change: %.4f\n", worst);
    CHECK (worst < 0.2);
    CHECK (peakOf (l, static_cast<size_t> (0.4 * kSr), total) > 0.2); // the sample type sounds afterwards
}

// the content of a state: a user wavetable (Preset table) is played, a missing file is the saw
TEST (own_osc_user_wavetable_plays_and_falls_back_to_the_saw)
{
    std::vector<float> frame (2048 * 2);
    for (int i = 0; i < 2048; ++i)
    {
        frame[static_cast<size_t> (i)] = static_cast<float> (std::sin (6.283185307179586 * i / 2048.0));
        frame[static_cast<size_t> (2048 + i)] = static_cast<float> (std::sin (3.0 * 6.283185307179586 * i / 2048.0));
    }
    MemoryContentStore store;
    store.add ("test", "t.wav", encodeWav (frame.data(), nullptr, frame.size(), 44100, 16));
    StateContent c;
    ContentFileRef ref;
    ref.pack = "test";
    ref.file = "t.wav";
    attachWavetable (c, 1, ref, "me", 2);
    auto e = createOwnEngine();
    e->prepare (kSr, 256, qualityForTier (Tier::high));
    set (*e, "filter1.on", 0.0f);
    set (*e, "master.volume", 0.0f);
    set (*e, "osc1.wavetable", 13.0f);
    set (*e, "osc1.frame", 100.0f);
    set (*e, "osc1.random_phase", 0.0f);
    set (*e, "osc1.level", 100.0f);
    auto run = [&]
    {
        std::vector<float> l (24000), rr (24000);
        Event ev[1] = { sbtest::noteOn (0, 57, 1.0f) };
        e->process (ev, 1, l.data(), rr.data(), 256);
        e->process (nullptr, 0, l.data() + 256, rr.data() + 256, 24000 - 256);
        return l;
    };
    const auto saw = run(); // no content yet: the saw
    e->setContentStore (&store);
    e->setContent (c);
    e->reset();
    const auto table = run();
    const double h3 = lineAmplitude (table, 6000, 18000, 660.0), h1 = lineAmplitude (table, 6000, 18000, 220.0);
    std::printf ("    user table frame 2: harmonic 3 %.2f dB, harmonic 1 %.2f dB\n", db (h3), db (h1));
    CHECK (db (h3) > -3.0);
    CHECK (db (h1) < -40.0);
    CHECK (db (lineAmplitude (saw, 6000, 18000, 660.0)) < db (h3) - 6.0);
}

// 02 O11 / O12: Odd/Even and Filter LPF reproduce the stated harmonic levels of the saw
TEST (own_osc_warps_odd_even_and_filter)
{
    if (sbtest::skipInQuick ("own_osc_warps_odd_even_and_filter", 58, "heavy case (several seconds of 48 kHz renders), left to the SB_TEST_FULL=1 run"))
        return;
    auto run = [] (const char* type, float amount)
    {
        auto e = createOwnEngine();
        e->prepare (kSr, 256, qualityForTier (Tier::high));
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "osc1.wavetable", 1.0f);
        set (*e, "osc1.random_phase", 0.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "osc1.warp.type", [&]
        {
            const ParamInfo* p = findParamByKey ("osc1.warp.type");
            return static_cast<float> (std::string (type) == "oddeven" ? 24 : (std::string (type) == "lpf" ? 25 : (std::string (type) == "hpf" ? 26 : 0))) + (p ? 0.0f : 0.0f);
        }());
        set (*e, "osc1.warp.amount", amount);
        std::vector<float> l (48000), rr (48000);
        Event ev[1] = { sbtest::noteOn (0, 57, 1.0f) };
        e->process (ev, 1, l.data(), rr.data(), 256);
        e->process (nullptr, 0, l.data() + 256, rr.data() + 256, 48000 - 256);
        return l;
    };
    auto h = [] (const std::vector<float>& x, int k) { return db (lineAmplitude (x, 9600, 38400, 220.0 * k)); };
    const auto ref = run ("none", 50.0f);
    const auto oe25 = run ("oddeven", 25.0f), oe75 = run ("oddeven", 75.0f), oe50 = run ("oddeven", 50.0f);
    std::printf ("    O11 saw h1..h4: %.1f %.1f %.1f %.1f | odd/even 25: %.1f %.1f %.1f %.1f | 75: %.1f %.1f %.1f %.1f\n", h (ref, 1), h (ref, 2), h (ref, 3), h (ref, 4),
                 h (oe25, 1), h (oe25, 2), h (oe25, 3), h (oe25, 4), h (oe75, 1), h (oe75, 2), h (oe75, 3), h (oe75, 4));
    CHECK_NEAR (h (oe50, 1), h (ref, 1), 0.4);
    CHECK_NEAR (h (oe50, 2), h (ref, 2), 0.4);
    CHECK_NEAR (h (oe25, 1), h (ref, 1), 0.5);              // 25 %: -5.3 -17.4 -14.9 -23.4 (even harmonics 6 dB down)
    CHECK_NEAR (h (oe25, 2), h (ref, 2) - 6.0, 0.5);
    CHECK_NEAR (h (oe75, 1), h (ref, 1) - 6.0, 0.5);        // 75 %: odd harmonics 6 dB down
    CHECK_NEAR (h (oe75, 2), h (ref, 2), 0.5);
    const auto lp25 = run ("lpf", 25.0f), lp75 = run ("lpf", 75.0f);
    std::printf ("    O12 filter LPF 25: %.1f %.1f %.1f %.1f (vector -6.1 -14.1 -20.1 -25.3) | 75: %.1f %.1f %.1f %.1f (-5.3 -11.4 -15.0 -17.5)\n", h (lp25, 1), h (lp25, 2),
                 h (lp25, 3), h (lp25, 4), h (lp75, 1), h (lp75, 2), h (lp75, 3), h (lp75, 4));
    CHECK_NEAR (h (lp25, 1), -6.1, 0.8);
    CHECK_NEAR (h (lp25, 2), -14.1, 1.0);
    CHECK_NEAR (h (lp25, 3), -20.1, 1.0);
    CHECK_NEAR (h (lp75, 2), -11.4, 0.6);
}

// 02 O13 / O14 / O15b: spectral morph low pass, high pass, harmonic stretch on the saw
TEST (own_osc_spectral_morph_vectors)
{
    if (sbtest::skipInQuick ("own_osc_spectral_morph_vectors", 41, "heavy case (2 to 4 seconds of renders), left to the SB_TEST_FULL=1 run"))
        return;
    auto run = [] (float type, float amount)
    {
        auto e = createOwnEngine();
        e->prepare (kSr, 256, qualityForTier (Tier::high));
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "osc1.wavetable", 1.0f);
        set (*e, "osc1.random_phase", 0.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "osc1.morph.type", type);
        set (*e, "osc1.morph.amount", amount);
        std::vector<float> l (48000), rr (48000);
        Event ev[1] = { sbtest::noteOn (0, 57, 1.0f) };
        e->process (ev, 1, l.data(), rr.data(), 256);
        e->process (nullptr, 0, l.data() + 256, rr.data() + 256, 48000 - 256);
        return l;
    };
    auto h = [] (const std::vector<float>& x, int k) { return db (lineAmplitude (x, 9600, 38400, 220.0 * k)); };
    const auto ref = run (0.0f, 50.0f);
    const auto lp = run (7.0f, 25.0f), hp = run (8.0f, 25.0f), st = run (3.0f, 75.0f);
    std::printf ("    O13 morph low pass 25: h5 %.1f h6 %.1f h7 %.1f (vector -19.3, -24.5, < -150) | O14 high pass: h4 %.1f h5 %.1f h6 %.1f (< -150, -28.7, -20.9) | O15b stretch 75: h1 %.1f h5 %.1f h2 %.1f (-5.3 -11.4 < -120)\n",
                 h (lp, 5), h (lp, 6), h (lp, 7), h (hp, 4), h (hp, 5), h (hp, 6), h (st, 1), h (st, 5), h (st, 2));
    CHECK_NEAR (h (lp, 5), h (ref, 5), 0.5);
    CHECK_NEAR (h (lp, 6), -24.5, 1.0);
    CHECK (h (lp, 7) < -60.0);
    CHECK (h (hp, 4) < -60.0);
    CHECK_NEAR (h (hp, 5), -28.7, 1.5);
    CHECK_NEAR (h (hp, 6), h (ref, 6), 0.5);
    CHECK_NEAR (h (st, 1), -5.3, 1.0);
    CHECK_NEAR (h (st, 5), -11.4, 1.0);
    CHECK (h (st, 2) < -60.0);
}

// the memory of the built-in wavetable bank (iOS budget: the plug-in stays well below 100 MB, the bank must not take half of it)
TEST (own_wavetable_bank_memory)
{
    const size_t bytes = own::builtinBankBytes();
    std::printf ("    built-in wavetable bank: %.2f MB (%.2f MiB), all 15 tables with all mip levels and spectra\n", static_cast<double> (bytes) / 1.0e6, static_cast<double> (bytes) / 1048576.0);
    CHECK (bytes < 32u * 1024u * 1024u);
    // the exact accounting: 519 frames (eight 64 frame tables and seven single frame tables), each 13,896 time floats (18 mip levels with 4 guard samples) and 2,048 spectrum floats
    CHECK_EQ (bytes, static_cast<size_t> (519u * (13896u + 2048u) * sizeof (float)));
    CHECK_EQ (bytes, static_cast<size_t> (33099744u));
    CHECK_EQ (own::Wavetable::kBankDocumentedBytes, static_cast<size_t> (33099744u));
    CHECK_EQ (own::Wavetable::kNumLevels, 18);
    CHECK_EQ (own::Wavetable::size (17), 256);   // the floor of 256 samples per level
}

// the lazy bank (Quality::lazyTables): a table is built when it is first chosen; the audio thread reads a finished table by an atomic pointer and never a substitute
TEST (own_lazy_wavetable_bank_ready_and_same)
{
    own::resetBuiltinBankForTests();
    CHECK (own::builtinWavetableIfReady (5) == nullptr);        // nothing was built by anyone
    CHECK (! own::builtinWavetableSettled (5));
    own::ensureBuiltinWavetables ((1u << 5) | (1u << 12));
    const own::Wavetable* a = own::builtinWavetableIfReady (5);
    CHECK (a != nullptr);
    CHECK (own::builtinWavetable (5) == a);                       // the same object, not a second build
    CHECK (own::builtinWavetableSettled (5));
    CHECK (own::builtinWavetableIfReady (kWavetablePresetTable) == nullptr && own::builtinWavetableSettled (kWavetablePresetTable));   // "Preset table" has no built-in table
    CHECK (own::builtinWavetableIfReady (-1) == nullptr && own::builtinWavetableIfReady (999) == nullptr);
    own::requestBuiltinWavetable (-1, 3);                         // out of range: ignored
    own::requestBuiltinWavetable (99, 3);
    own::requestBuiltinWavetable (0, 999);
    CHECK (own::builtinBankBuildMs() >= 0.0);
}

namespace
{
struct HeldNoteRun
{
    std::unique_ptr<IEngine> eng;
    std::vector<float> out;
    HeldNoteRun (float firstTable, bool lazy)
    {
        eng = createOwnEngine();
        set ("osc1.wavetable", firstTable);
        set ("osc2.on", 0.0f);
        set ("filter1.on", 0.0f);
        Quality q;
        q.realtime = true;
        q.lazyTables = lazy;
        eng->prepare (48000.0, 256, q);
        eng->readyContent();
    }
    void set (const char* key, float v)
    {
        if (const ParamInfo* p = findParamByKey (key))
            eng->setParam (p->id, v);
    }
    void blocks (int n, bool noteOn)
    {
        std::vector<float> l (256), r (256);
        for (int b = 0; b < n; ++b)
        {
            std::vector<Event> ev;
            if (noteOn && b == 0)
                ev.push_back (sbtest::noteOn (0, 48));
            eng->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 256);
            out.insert (out.end(), l.begin(), l.end());
            out.insert (out.end(), r.begin(), r.end());
        }
    }
};
}

// A table chosen during a held note that is not built yet: the oscillator KEEPS the table it had (the output is exactly that of an engine that never changed), and once the builder has delivered
// the new table the output is the new table's: never a third waveform (no saw substitute).
TEST (own_lazy_table_change_keeps_the_old_table_until_the_swap)
{
    own::resetBuiltinBankForTests();
    own::setBuiltinBuilderPausedForTests (true);
    HeldNoteRun stay (1.0f, true), change (1.0f, true);   // saw (single cycle table: built in prepare)
    stay.blocks (8, true);
    change.blocks (8, true);
    change.set ("osc1.wavetable", 9.0f);                  // Formant Sweep: a 64 frame table nobody has built
    CHECK (change.eng->contentPending());
    stay.blocks (30, false);
    change.blocks (30, false);
    bool same = stay.out.size() == change.out.size();
    for (size_t i = 0; same && i < stay.out.size(); ++i)
        same = stay.out[i] == change.out[i];
    CHECK (same);                                         // while the new table is not ready: exactly the old table's output
    own::setBuiltinBuilderPausedForTests (false);
    bool built = false;
    for (int i = 0; i < 4000 && ! built; ++i)             // bounded wait for the builder (20 s)
    {
        built = ! change.eng->contentPending();
        if (! built)
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    CHECK (built);
    const size_t before = change.out.size();
    stay.blocks (30, false);
    change.blocks (30, false);
    bool differs = false;
    for (size_t i = before; i < change.out.size() && ! differs; ++i)
        differs = stay.out[i] != change.out[i];
    CHECK (differs);                                      // the new table sounds after the swap
    double energy = 0.0;
    for (size_t i = before; i < change.out.size(); ++i)
        energy += static_cast<double> (change.out[i]) * change.out[i];
    CHECK (energy > 1.0e-9);                              // and it is a sound, not silence
}

// A program change: the tables the incoming values name are built BEFORE the values are set (readyContentFor), so the first block after the change already has index and table together.
TEST (own_state_load_builds_the_tables_before_the_values_take_effect)
{
    own::resetBuiltinBankForTests();
    own::setBuiltinBuilderPausedForTests (true);   // the builder cannot help: only the calling thread builds
    HeldNoteRun run (1.0f, true);
    ParamValues vals = ParamValues::defaults();
    if (const ParamInfo* p = findParamByKey ("osc1.wavetable"))
        vals.plain[static_cast<size_t> (findParamIndex (p->id))] = 11.0f;
    run.eng->readyContentFor (vals.plain, paramCount());
    CHECK (own::builtinWavetableSettled (11));
    for (size_t i = 0; i < paramCount(); ++i)
        run.eng->setParam (paramTable()[i].id, vals.plain[i]);
    CHECK (! run.eng->contentPending());
    own::setBuiltinBuilderPausedForTests (false);
}

// open, close at once, repeat: the builder is stopped by the last engine (bound: one frame of a table and its 10 ms sleep), nothing is left running and nothing half built is ever read
TEST (own_lazy_engine_open_close_repeat)
{
    own::resetBuiltinBankForTests();
    for (int i = 0; i < 20; ++i)
    {
        HeldNoteRun run (static_cast<float> (1 + (i % 2)), true);
        run.set ("osc1.wavetable", static_cast<float> (5 + (i % 8)));   // a morph table: the builder starts on it
        run.blocks (2, i % 2 == 0);
        const auto t0 = std::chrono::steady_clock::now();
        run.eng.reset();                                               // the destructor stops and joins the builder when this was the last engine
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        CHECK (ms < 500.0);                                            // generous for a loaded test machine: the design bound is one frame plus 10 ms
    }
    // the tables that were finished are whole: reading them gives the same pointer and numbers on the next open
    for (int c = 5; c <= 12; ++c)
        if (const own::Wavetable* t = own::builtinWavetableIfReady (c))
            CHECK (t->numFrames() == 64);
}

// Render OSC Warp bakes the warp of the oscillator into a 256 frame table; the table played at the same warp position sounds like the warp
TEST (own_render_osc_warp_bake_matches_the_live_warp)
{
    if (sbtest::skipInQuick ("own_render_osc_warp_bake_matches_the_live_warp", 19, "heavy case (several seconds of 48 kHz renders), left to the SB_TEST_FULL=1 run"))
        return;
    ParamValues vals = ParamValues::defaults();
    auto setv = [&] (const char* key, float v)
    {
        const ParamInfo* p = findParamByKey (key);
        CHECK (p != nullptr);
        if (p != nullptr)
            vals.plain[static_cast<size_t> (findParamIndex (p->id))] = v;
    };
    setv ("osc1.warp.type", 1.0f);
    setv ("osc1.warp.amount", 40.0f);
    setv ("filter1.on", 0.0f);
    convert::OfflineRunner run = [] (const OfflineRequest& r, OfflineAudio& a)
    {
        auto twin = createOwnEngine();
        return runOfflineRequest (*twin, r, a);
    };
    wtedit::Wavetable table;
    convert::WarpRenderOptions opt;
    std::string err;
    float scale = 1.0f;
    CHECK (convert::renderOscWarp (run, &vals, opt, "bake", table, &err, &scale));
    CHECK_EQ (static_cast<int> (table.numFrames()), 256);
    // the live warp at 40 %
    auto live = createOwnEngine();
    live->prepare (kSr, 256, qualityForTier (Tier::high));
    auto render = [] (IEngine& e)
    {
        std::vector<float> l (48000), rr (48000);
        Event ev[1] = { sbtest::noteOn (0, 57, 1.0f) };
        e.process (ev, 1, l.data(), rr.data(), 256);
        e.process (nullptr, 0, l.data() + 256, rr.data() + 256, 48000 - 256);
        return l;
    };
    for (const char* k : { "filter1.on", "master.volume", "osc1.random_phase", "osc1.level" })
        set (*live, k, std::string (k) == "osc1.level" ? 100.0f : 0.0f);
    set (*live, "osc1.warp.type", 1.0f);
    set (*live, "osc1.warp.amount", 40.0f);
    const auto ref = render (*live);
    auto baked = createOwnEngine();
    baked->prepare (kSr, 256, qualityForTier (Tier::high));
    for (const char* k : { "filter1.on", "master.volume", "osc1.random_phase", "osc1.level" })
        set (*baked, k, std::string (k) == "osc1.level" ? 100.0f : 0.0f);
    std::vector<uint8_t> bytes;
    {
        std::vector<float> all;
        for (size_t f = 0; f < table.numFrames(); ++f)
            all.insert (all.end(), table.getFrame (f).data.begin(), table.getFrame (f).data.end());
        bytes = encodeWav (all.data(), nullptr, all.size(), 44100, 16);
    }
    MemoryContentStore store;
    store.add ("t", "b.wav", bytes);
    StateContent c;
    ContentFileRef ref2;
    ref2.pack = "t";
    ref2.file = "b.wav";
    attachWavetable (c, 1, ref2, "x", 256);
    baked->setContentStore (&store);
    baked->setContent (c);
    set (*baked, "osc1.wavetable", 13.0f);
    set (*baked, "osc1.frame", 40.0f);
    set (*baked, "osc1.level", 100.0f);
    const auto bk = render (*baked);
    double worst = 0.0;
    const double a1 = db (lineAmplitude (ref, 9600, 38400, 220.0)), b1 = db (lineAmplitude (bk, 9600, 38400, 220.0)); // relative to the fundamental: the bake may scale the table down
    for (int k = 1; k <= 24; ++k)
    {
        const double a = db (lineAmplitude (ref, 9600, 38400, 220.0 * k)), b = db (lineAmplitude (bk, 9600, 38400, 220.0 * k));
        if (a - a1 > -30.0)
            worst = std::max (worst, std::fabs ((a - a1) - (b - b1)));
        if (k <= 24)
            std::printf ("    h%d live %.2f baked %.2f\n", k, a, b);
    }
    {
        double rl = 0, rb = 0;
        for (size_t i = 9600; i < 38400; ++i) { rl += ref[i] * ref[i]; rb += bk[i] * bk[i]; }
        std::printf ("    live peak %.4f rms %.4f | baked peak %.4f rms %.4f\n", peakOf (ref, 9600, 38400), std::sqrt (rl / 28800), peakOf (bk, 9600, 38400), std::sqrt (rb / 28800));
    }
    std::printf ("    bake vs live warp: worst difference %.3f dB, scale %.4f\n", worst, scale);
    CHECK (worst < 0.1);
}

// 02 section 11.1: the Filter HPF / LPF warps keep an absolute inharmonic floor of -100 dB or lower (Saw, note 57, Blackman window, +-1 % around the harmonics)
namespace
{
double inharmonicFloorDb (const std::vector<float>& x, size_t from, size_t n, double f0)
{
    // direct DFT only on a grid of bins (n is a power of two): radix 2 FFT
    std::vector<std::complex<double>> a (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double ph = 6.283185307179586 * static_cast<double> (i) / static_cast<double> (n - 1);
        a[i] = (0.42 - 0.5 * std::cos (ph) + 0.08 * std::cos (2.0 * ph)) * static_cast<double> (x[from + i]);
    }
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> wl = std::polar (1.0, -6.283185307179586 / static_cast<double> (len));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    const double binHz = kSr / static_cast<double> (n);
    double total = 0.0, outside = 0.0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double p = std::norm (a[k]);
        total += p;
        const double f = static_cast<double> (k) * binHz, h = std::round (f / f0);
        if (h < 1.0 || std::fabs (f - h * f0) > std::max (0.01 * h * f0, 40.0 * binHz))
            outside += p;
    }
    return 10.0 * std::log10 (std::max (outside / std::max (total, 1.0e-30), 1.0e-30));
}
} // namespace

TEST (own_osc_filter_warps_keep_an_absolute_inharmonic_floor)
{
    for (float type : { 25.0f, 26.0f })
        for (float amount : { 25.0f, 50.0f })
        {
            auto e = createOwnEngine();
            e->prepare (kSr, 256, qualityForTier (Tier::high));
            set (*e, "filter1.on", 0.0f);
            set (*e, "master.volume", 0.0f);
            set (*e, "osc1.wavetable", 1.0f);
            set (*e, "osc1.random_phase", 0.0f);
            set (*e, "osc1.level", 100.0f);
            set (*e, "env1.attack", 0.0f);
            set (*e, "osc1.warp.type", type);
            set (*e, "osc1.warp.amount", amount);
            std::vector<float> l (96000), rr (96000);
            Event ev[1] = { sbtest::noteOn (0, 57, 1.0f) };
            e->process (ev, 1, l.data(), rr.data(), 256);
            e->process (nullptr, 0, l.data() + 256, rr.data() + 256, 96000 - 256);
            const double floorDb = inharmonicFloorDb (l, 16384, 65536, 220.0);
            std::printf ("    warp %.0f amount %.0f: energy outside the harmonics %.1f dB\n", type, amount, floorDb);
            CHECK (floorDb < -95.0); // relative to the signal power (falls with the high-pass); the oracle shows the absolute floor below -100 dB re 1, better than engine-vital
        }
}

namespace
{
std::vector<float> plainRender (const std::vector<std::pair<const char*, float>>& keys, int note, double seconds, std::function<void (IEngine&, size_t)> midBlock = {}, size_t block = 256)
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (kSr, 256, qualityForTier (Tier::high));
    set (*e, "filter1.on", 0.0f);
    set (*e, "master.volume", 0.0f);
    set (*e, "env1.attack", 0.0f);
    set (*e, "osc1.random_phase", 0.0f);
    set (*e, "osc1.level", 100.0f);
    for (const auto& k : keys)
        set (*e, k.first, k.second);
    const size_t total = static_cast<size_t> (seconds * kSr);
    std::vector<float> l (total), r (total);
    for (size_t pos = 0; pos < total; pos += block)
    {
        const size_t n = std::min (block, total - pos);
        Event ev[1];
        int ne = 0;
        if (pos == 0)
            ev[ne++] = sbtest::noteOn (0, note, 1.0f);
        if (midBlock)
            midBlock (*e, pos);
        e->process (ev, ne, l.data() + pos, r.data() + pos, static_cast<int> (n));
    }
    return l;
}

double rmsOfRange (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (b - a));
}
} // namespace

// 02 6.7: the frame position remap glides with RISE / FALL (off: the position steps at once)
TEST (own_osc_frame_remap_rise_and_fall)
{
    auto run = [] (bool remap)
    {
        return plainRender ({ { "osc1.wavetable", 5.0f }, { "osc1.frame", 0.0f }, { "osc1.remap.on", remap ? 1.0f : 0.0f }, { "osc1.remap.rise", 500.0f }, { "osc1.remap.fall", 500.0f } }, 57, 1.6,
                            [] (IEngine& e, size_t pos)
                            {
                                if (pos == static_cast<size_t> (0.5 * kSr) / 256 * 256)
                                    set (e, "osc1.frame", 100.0f);
                            });
    };
    const auto off = run (false), on = run (true);
    // second harmonic (saw to square: -6 dB at frame 0, gone at frame 100 %)
    auto h2 = [] (const std::vector<float>& x, double t) { return db (lineAmplitude (x, static_cast<size_t> (t * kSr), static_cast<size_t> (t * kSr) + 4800, 440.0)); };
    std::printf ("    frame remap, second harmonic: off %.1f %.1f | on %.1f %.1f %.1f dB at 0.55 / 1.0 / 1.5 s\n", h2 (off, 0.55), h2 (off, 1.0), h2 (on, 0.55), h2 (on, 1.0), h2 (on, 1.5));
    CHECK (h2 (off, 0.55) < -30.0);
    CHECK (h2 (on, 0.55) > -14.0);
    CHECK (h2 (on, 1.0) < h2 (on, 0.55) - 3.0);
    CHECK (h2 (on, 1.5) < h2 (on, 1.0) - 3.0);
}

// 07 7a.2: the X|Y control keys are claimed by the own engine
TEST (own_osc_xy_keys_are_claimed)
{
    auto e = createOwnEngine();
    for (const char* osc : { "osc1", "osc2", "osc3" })
    {
        for (const char* suffix : { "gran.y_axis", "gran.xy_y", "spec.y_axis", "spec.xy_y" })
        {
            const std::string key = std::string (osc) + "." + suffix;
            const ParamInfo* info = findParamByKey (key.c_str());
            CHECK (info != nullptr && e->supportsParam (info->id));
        }
    }
    // docs/ui-design/13-new-keys.md A39: loop_link is an editor action without DSP effect
    const ParamInfo* loopLink = findParamByKey ("osc1.sample.loop_link");
    CHECK (loopLink != nullptr && ! e->supportsParam (loopLink->id));
}

// 07 7a.2: the Y value of the X|Y control takes over the parameter of the axis
TEST (own_osc_xy_control_level_axis)
{
    for (float type : { 2.0f, 3.0f })
    {
        auto run = [&] (float axis, float y)
        {
            Rig r (true, "osc1.type", type);
            set (*r.e, type == 2.0f ? "osc1.gran.y_axis" : "osc1.spec.y_axis", axis);
            set (*r.e, type == 2.0f ? "osc1.gran.xy_y" : "osc1.spec.xy_y", y);
            return r.render (69, 1.0, 1.0).l;
        };
        const double full = rmsOfRange (run (0.0f, 0.0f), 14400, 43200), zero = rmsOfRange (run (1.0f, 0.0f), 14400, 43200), half = rmsOfRange (run (1.0f, 50.0f), 14400, 43200);
        std::printf ("    X|Y level axis, type %.0f: no axis %.3f, y 0 %.4f, y 50 %% %.3f\n", type, full, zero, half);
        CHECK (zero < 1.0e-4);
        CHECK_NEAR (half / full, 0.5, 0.1);
    }
}

// 07 7a.2: granular X|Y control for every axis (0 = None is ignored, 1..17 change the render)
TEST (own_osc_xy_control_every_granular_axis)
{
    if (sbtest::skipInQuick ("own_osc_xy_control_every_granular_axis", 315, "heavy case (2 to 4 seconds of renders), left to the SB_TEST_FULL=1 run"))
        return;
    // Buffer difference measure reused from own_wavetable_live_rebuild_swaps_safely (lines 614-617):
    // checks whether any rendered sample in the buffers differs (a[i] != b[i]).
    auto buffersDiffer = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return true;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return true;
        return false;
    };

    auto run = [&] (float axis, float y)
    {
        Rig r (true, "osc1.type", 2.0f);
        set (*r.e, "osc1.gran.y_axis", axis);
        set (*r.e, "osc1.gran.xy_y", y);
        return r.render (69, 1.0, 1.0).l;
    };

    // docs/engine-spec/07-sample-granular-spectral.md line 128:
    // "axis 0 = None: the knobs act" (Y value is ignored: renders with y=10 and y=90 must be identical)
    const auto none10 = run (0.0f, 10.0f), none90 = run (0.0f, 90.0f);
    CHECK (! buffersDiffer (none10, none90));

    // Reproducibility check: two renders with the same settings must be bit-identical
    const auto ref1 = run (1.0f, 10.0f), ref2 = run (1.0f, 10.0f);
    const bool reproducible = ! buffersDiffer (ref1, ref2);
    std::printf ("    granular harness reproducibility: %s\n", reproducible ? "bit-identical" : "differs");
    CHECK (reproducible);

    // docs/engine-spec/07-sample-granular-spectral.md lines 132-138:
    // 132: "1 Level | output gain of the type = y (0 mutes)"
    // 134: "4 Density | 0.1 * 10^(4 y) grains per second (0.1 .. 1000 Hz)"
    // 135: "5 Grain Length | 4000^y ms (1 ms .. 4 s)"
    // 136: "6 Window Amt / 7 Window Skew | window amount = y; skew = 2 y - 1"
    // 137: "8 Rand Offset, 9 Rand Dir, 11 Rand Length, 12 Rand Pan, 13 Rand Gain, 14 Rand Window, 15 Rand Skew | that randomisation depth = y"
    // 138: "10 Rand Pitch | 24 y semitones"
    // Axes 2, 3 (Warp 1/2) and 16, 17 (Rand Warp 1/2) do not alter the render with default harness
    // settings (warp.type is None/0, so warp amount is unused); they are covered by own_osc_xy_control_warp_axes
    // with warp 1 / warp 2 switched to a non-None type.
    for (int axis : { 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 })
    {
        const auto y10 = run (static_cast<float> (axis), 10.0f);
        const auto y90 = run (static_cast<float> (axis), 90.0f);
        const bool diff = buffersDiffer (y10, y90);
        std::printf ("    granular axis %d: differs %s\n", axis, diff ? "true" : "false");
        CHECK (diff);
    }
}

// 07 7a.2: spectral X|Y control for every axis (0 = None is ignored, 1..7 change the render)
TEST (own_osc_xy_control_every_spectral_axis)
{
    // Buffer difference measure reused from own_wavetable_live_rebuild_swaps_safely (lines 614-617):
    // checks whether any rendered sample in the buffers differs (a[i] != b[i]).
    auto buffersDiffer = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return true;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return true;
        return false;
    };

    auto run = [&] (float axis, float y)
    {
        Rig r (true, "osc1.type", 3.0f);
        set (*r.e, "osc1.spec.y_axis", axis);
        set (*r.e, "osc1.spec.xy_y", y);
        return r.render (69, 1.0, 1.0).l;
    };

    // docs/engine-spec/07-sample-granular-spectral.md line 128:
    // "axis 0 = None: the knobs act" (Y value is ignored: renders with y=10 and y=90 must be identical)
    const auto none10 = run (0.0f, 10.0f), none90 = run (0.0f, 90.0f);
    CHECK (! buffersDiffer (none10, none90));

    // Reproducibility check: two renders with the same settings must be bit-identical
    const auto ref1 = run (1.0f, 10.0f), ref2 = run (1.0f, 10.0f);
    const bool reproducible = ! buffersDiffer (ref1, ref2);
    std::printf ("    spectral harness reproducibility: %s\n", reproducible ? "bit-identical" : "differs");
    CHECK (reproducible);

    // docs/engine-spec/07-sample-granular-spectral.md lines 142-146:
    // 142: "1 Level | output gain = y (times the trim of 7a.1)"
    // 146: "6 Freq Lo / 7 Freq Hi | the low / high band edge = 20 * 1000^y Hz (20 Hz .. 20 kHz)"
    // Axes 2, 3 (Warp 1/2) and 4, 5 (Spec Flt Cutoff / Wet/Dry) do not alter the render with default
    // harness settings (warp.type is None/0, flt_use is 0); axes 2, 3 are covered by own_osc_xy_control_warp_axes
    // and axes 4, 5 are covered by own_osc_xy_control_spectral_filter_axes.
    for (int axis : { 1, 6, 7 })
    {
        const auto y10 = run (static_cast<float> (axis), 10.0f);
        const auto y90 = run (static_cast<float> (axis), 90.0f);
        const bool diff = buffersDiffer (y10, y90);
        std::printf ("    spectral axis %d: differs %s\n", axis, diff ? "true" : "false");
        CHECK (diff);
    }
}

// 07 7a.2: granular and spectral X|Y control for the warp axes (granular: 2, 16, 3, 17; spectral: 2, 3)
TEST (own_osc_xy_control_warp_axes)
{
    if (sbtest::skipInQuick ("own_osc_xy_control_warp_axes", 420, "heavy case (2 to 4 seconds of renders), left to the SB_TEST_FULL=1 run"))
        return;
    auto buffersDiffer = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return true;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return true;
        return false;
    };

    // --- Granular type (osc1.type 2.0f, param_table.cpp:396): pointwise warp (Quantize = 3, param_table.cpp:61, 248) ---
    // Warp 1 on: axis 2 (Warp 1 amount) and axis 16 (Rand Warp 1)
    {
        auto runG1 = [&] (float axis, float y, float knobWarpAmt = 50.0f, float knobRandWarp = 0.0f)
        {
            Rig r (true, "osc1.type", 2.0f);
            set (*r.e, "osc1.warp.type", 3.0f); // Quantize (kWarpTypes1[3], param_table.cpp:61, 248)
            set (*r.e, "osc1.warp.amount", knobWarpAmt); // param_table.cpp:249
            set (*r.e, "osc1.gran.rand_warp", knobRandWarp); // param_table.cpp:458
            set (*r.e, "osc1.gran.y_axis", axis); // param_table.cpp:461
            set (*r.e, "osc1.gran.xy_y", y); // param_table.cpp:462
            return r.render (69, 1.0, 1.0).l;
        };

        // R5 check: with y_axis = 0 (None), changing the normal knobs must change the render
        const auto r5_knobWarp10 = runG1 (0.0f, 0.0f, 10.0f, 0.0f);
        const auto r5_knobWarp90 = runG1 (0.0f, 0.0f, 90.0f, 0.0f);
        CHECK (buffersDiffer (r5_knobWarp10, r5_knobWarp90));

        const auto r5_knobRand10 = runG1 (0.0f, 0.0f, 50.0f, 10.0f);
        const auto r5_knobRand90 = runG1 (0.0f, 0.0f, 50.0f, 90.0f);
        CHECK (buffersDiffer (r5_knobRand10, r5_knobRand90));

        // Reproducibility check: two renders with the same settings must be bit-identical
        const auto ref1 = runG1 (2.0f, 10.0f);
        const auto ref2 = runG1 (2.0f, 10.0f);
        const bool reproducible = ! buffersDiffer (ref1, ref2);
        std::printf ("    granular harness (warp 1) reproducibility: %s\n", reproducible ? "bit-identical" : "differs");
        CHECK (reproducible);

        for (int axis : { 2, 16 })
        {
            const auto y10 = runG1 (static_cast<float> (axis), 10.0f);
            const auto y90 = runG1 (static_cast<float> (axis), 90.0f);
            const bool diff = buffersDiffer (y10, y90);
            std::printf ("    granular axis %d: differs %s\n", axis, diff ? "true" : "false");
            CHECK (diff);
        }
    }

    // Warp 2 on: axis 3 (Warp 2 amount) and axis 17 (Rand Warp 2)
    {
        auto runG2 = [&] (float axis, float y, float knobWarpAmt = 50.0f, float knobRandWarp = 0.0f)
        {
            Rig r (true, "osc1.type", 2.0f);
            set (*r.e, "osc1.warp2.type", 3.0f); // Quantize (kWarpTypes1[3], param_table.cpp:61, 425)
            set (*r.e, "osc1.warp2.amount", knobWarpAmt); // param_table.cpp:426
            set (*r.e, "osc1.gran.rand_warp2", knobRandWarp); // param_table.cpp:459
            set (*r.e, "osc1.gran.y_axis", axis); // param_table.cpp:461
            set (*r.e, "osc1.gran.xy_y", y); // param_table.cpp:462
            return r.render (69, 1.0, 1.0).l;
        };

        // R5 check: with y_axis = 0 (None), changing the normal knobs must change the render
        const auto r5_knobWarp10 = runG2 (0.0f, 0.0f, 10.0f, 0.0f);
        const auto r5_knobWarp90 = runG2 (0.0f, 0.0f, 90.0f, 0.0f);
        CHECK (buffersDiffer (r5_knobWarp10, r5_knobWarp90));

        const auto r5_knobRand10 = runG2 (0.0f, 0.0f, 50.0f, 10.0f);
        const auto r5_knobRand90 = runG2 (0.0f, 0.0f, 50.0f, 90.0f);
        CHECK (buffersDiffer (r5_knobRand10, r5_knobRand90));

        // Reproducibility check: two renders with the same settings must be bit-identical
        const auto ref1 = runG2 (3.0f, 10.0f);
        const auto ref2 = runG2 (3.0f, 10.0f);
        const bool reproducible = ! buffersDiffer (ref1, ref2);
        std::printf ("    granular harness (warp 2) reproducibility: %s\n", reproducible ? "bit-identical" : "differs");
        CHECK (reproducible);

        for (int axis : { 3, 17 })
        {
            const auto y10 = runG2 (static_cast<float> (axis), 10.0f);
            const auto y90 = runG2 (static_cast<float> (axis), 90.0f);
            const bool diff = buffersDiffer (y10, y90);
            std::printf ("    granular axis %d: differs %s\n", axis, diff ? "true" : "false");
            CHECK (diff);
        }
    }

    // --- Spectral type (osc1.type 3.0f, param_table.cpp:396): spectral warp (Spectral Comb = 76, param_table.cpp:58, 248) ---
    // Warp 1 on: axis 2 (Warp amount)
    {
        auto runS1 = [&] (float axis, float y, float knobWarpAmt = 50.0f)
        {
            Rig r (true, "osc1.type", 3.0f);
            set (*r.e, "osc1.warp.type", 76.0f); // Spectral Comb (kWarpTypes1[76], param_table.cpp:58, 248)
            set (*r.e, "osc1.warp.amount", knobWarpAmt); // param_table.cpp:249
            set (*r.e, "osc1.spec.y_axis", axis); // param_table.cpp:475
            set (*r.e, "osc1.spec.xy_y", y); // param_table.cpp:476
            return r.render (69, 1.0, 1.0).l;
        };

        // R5 check: with y_axis = 0 (None), changing the normal knob must change the render
        const auto r5_knob10 = runS1 (0.0f, 0.0f, 10.0f);
        const auto r5_knob90 = runS1 (0.0f, 0.0f, 90.0f);
        CHECK (buffersDiffer (r5_knob10, r5_knob90));

        // Reproducibility check: two renders with the same settings must be bit-identical
        const auto ref1 = runS1 (2.0f, 10.0f);
        const auto ref2 = runS1 (2.0f, 10.0f);
        const bool reproducible = ! buffersDiffer (ref1, ref2);
        std::printf ("    spectral harness (warp 1) reproducibility: %s\n", reproducible ? "bit-identical" : "differs");
        CHECK (reproducible);

        const auto y10 = runS1 (2.0f, 10.0f);
        const auto y90 = runS1 (2.0f, 90.0f);
        const bool diff = buffersDiffer (y10, y90);
        std::printf ("    spectral axis 2: differs %s\n", diff ? "true" : "false");
        CHECK (diff);
    }

    // Warp 2 on: axis 3 (Warp 2 amount)
    {
        auto runS2 = [&] (float axis, float y, float knobWarpAmt = 50.0f)
        {
            Rig r (true, "osc1.type", 3.0f);
            set (*r.e, "osc1.warp2.type", 76.0f); // Spectral Comb (kWarpTypes1[76], param_table.cpp:58, 425)
            set (*r.e, "osc1.warp2.amount", knobWarpAmt); // param_table.cpp:426
            set (*r.e, "osc1.spec.y_axis", axis); // param_table.cpp:475
            set (*r.e, "osc1.spec.xy_y", y); // param_table.cpp:476
            return r.render (69, 1.0, 1.0).l;
        };

        // R5 check: with y_axis = 0 (None), changing the normal knob must change the render
        const auto r5_knob10 = runS2 (0.0f, 0.0f, 10.0f);
        const auto r5_knob90 = runS2 (0.0f, 0.0f, 90.0f);
        CHECK (buffersDiffer (r5_knob10, r5_knob90));

        // Reproducibility check: two renders with the same settings must be bit-identical
        const auto ref1 = runS2 (3.0f, 10.0f);
        const auto ref2 = runS2 (3.0f, 10.0f);
        const bool reproducible = ! buffersDiffer (ref1, ref2);
        std::printf ("    spectral harness (warp 2) reproducibility: %s\n", reproducible ? "bit-identical" : "differs");
        CHECK (reproducible);

        const auto y10 = runS2 (3.0f, 10.0f);
        const auto y90 = runS2 (3.0f, 90.0f);
        const bool diff = buffersDiffer (y10, y90);
        std::printf ("    spectral axis 3: differs %s\n", diff ? "true" : "false");
        CHECK (diff);
    }
}

// 07 7a.2: spectral X|Y control for the spectral filter axes (4 Spec Flt Cutoff, 5 Spec Flt Wet/Dry)
TEST (own_osc_xy_control_spectral_filter_axes)
{
    auto buffersDiffer = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return true;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return true;
        return false;
    };

    auto run = [&] (float axis, float y, float cut = 0.0f, float mix = 100.0f)
    {
        Rig r (true, "osc1.type", 3.0f);
        // Spectral filter enabled from wavetable (param_table.cpp:488, 489)
        set (*r.e, "osc1.spec.flt_use", 1.0f); // onOff (param_table.cpp:488)
        set (*r.e, "osc1.spec.flt_table", 1.0f); // kWavetableSaw (param_table.cpp:489)
        r.e->readyContent();
        set (*r.e, "osc1.spec.cut", cut); // SB_PCT_RANGE (param_table.cpp:470)
        set (*r.e, "osc1.spec.mix", mix); // SB_PCT (param_table.cpp:471)
        set (*r.e, "osc1.spec.y_axis", axis); // whole (param_table.cpp:475)
        set (*r.e, "osc1.spec.xy_y", y); // SB_PCT (param_table.cpp:476)
        return r.render (69, 1.0, 1.0).l;
    };

    // R5 check: with y_axis = 0 (None), changing the normal cutoff and mix knobs must change the render
    const auto r5_cut_lo = run (0.0f, 0.0f, -80.0f, 100.0f);
    const auto r5_cut_hi = run (0.0f, 0.0f, 80.0f, 100.0f);
    CHECK (buffersDiffer (r5_cut_lo, r5_cut_hi));

    const auto r5_mix_lo = run (0.0f, 0.0f, 0.0f, 10.0f);
    const auto r5_mix_hi = run (0.0f, 0.0f, 0.0f, 90.0f);
    CHECK (buffersDiffer (r5_mix_lo, r5_mix_hi));

    // Reproducibility check: two renders with the same settings must be bit-identical
    const auto ref1 = run (4.0f, 10.0f);
    const auto ref2 = run (4.0f, 10.0f);
    const bool reproducible = ! buffersDiffer (ref1, ref2);
    std::printf ("    spectral filter harness reproducibility: %s\n", reproducible ? "bit-identical" : "differs");
    CHECK (reproducible);

    // docs/engine-spec/07-sample-granular-spectral.md lines 144-145:
    // 144: "4 Spec Flt Cutoff | the spectral filter's shift (cut) = 2 y - 1"
    // 145: "5 Spec Flt Wet/Dry | the spectral filter's mix = y"
    for (int axis : { 4, 5 })
    {
        const auto y10 = run (static_cast<float> (axis), 10.0f);
        const auto y90 = run (static_cast<float> (axis), 90.0f);
        const bool diff = buffersDiffer (y10, y90);
        std::printf ("    spectral axis %d: differs %s\n", axis, diff ? "true" : "false");
        CHECK (diff);
    }
}

// 07 7a.4: a note chokes the layers of earlier notes of the same oscillator slot (any voice)
TEST (own_osc_choke_groups_work_across_voices)
{
    auto run = [] (bool choke)
    {
        MemoryContentStore store;
        store.add ("o", "inst/p.wav", sineWav (440.0, 0.8, 44100, 2.0));
        const std::string sfz = std::string ("<region> sample=p.wav lokey=0 hikey=127 pitch_keycenter=69 loop_mode=loop_continuous group=1 ") + (choke ? "off_by=1 off_mode=fast" : "") + "\n";
        store.add ("o", "inst/p.sfz", std::vector<uint8_t> (sfz.begin(), sfz.end()));
        StateContent c;
        ContentFileRef i;
        i.pack = "o";
        i.file = "inst/p.sfz";
        attachInstrument (c, 1, i, 1);
        auto e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->prepare (kSr, 256, qualityForTier (Tier::high));
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "osc1.type", 4.0f);
        e->setContentStore (&store);
        e->setContent (c);
        const size_t total = static_cast<size_t> (1.2 * kSr);
        std::vector<float> l (total), r (total);
        for (size_t pos = 0; pos < total; pos += 256)
        {
            Event ev[1];
            int ne = 0;
            if (pos == 0)
                ev[ne++] = sbtest::noteOn (0, 69, 1.0f);
            if (pos == 14336)
                ev[ne++] = sbtest::noteOn (0, 76, 1.0f); // a fifth up: 659 Hz
            e->process (ev, ne, l.data() + pos, r.data() + pos, 256);
        }
        return l;
    };
    const auto without = run (false), with = run (true);
    const double a0 = lineAmplitude (without, 38400, 52800, 440.0), a1 = lineAmplitude (with, 38400, 52800, 440.0), b1 = lineAmplitude (with, 38400, 52800, 659.3);
    std::printf ("    choke: 440 Hz line without %.1f dB, with %.1f dB, the choking note %.1f dB\n", db (a0), db (a1), db (b1));
    CHECK (db (a0) > -12.0);
    CHECK (db (a1) < db (a0) - 30.0);
    CHECK (db (b1) > -12.0);
}

// 07 7a.3: the Mask warp from the Noise passes only the bins where the source is strong: a silent source mutes the spectral oscillator at full amount
TEST (own_osc_spectral_mask_from_the_noise)
{
    Rig plain (true, "osc1.type", 3.0f);
    Rig masked (true, "osc1.type", 3.0f);
    set (*masked.e, "osc1.warp.type", 95.0f);
    set (*masked.e, "osc1.warp.amount", 100.0f);
    const double a = rmsOfRange (plain.render (69, 1.0, 1.0).l, 14400, 43200), b = rmsOfRange (masked.render (69, 1.0, 1.0).l, 14400, 43200);
    std::printf ("    mask from a silent noise: %.4f -> %.5f\n", a, b);
    CHECK (a > 0.1);
    CHECK (b < 0.05 * a);
}

// 02 section 10: noise.filter and the generator noise pitch
TEST (own_noise_filter_and_generator_pitch)
{
    auto noise = [] (float filter, float pitch)
    {
        return plainRender ({ { "osc1.on", 0.0f }, { "noise.on", 1.0f }, { "noise.level", 100.0f }, { "noise.sample", 0.0f }, { "noise.filter", filter }, { "noise.pitch", pitch } }, 60, 1.0);
    };
    auto hf = [] (const std::vector<float>& x)
    {
        double d = 0.0, s = 0.0;
        for (size_t i = 12001; i < 40000; ++i)
        {
            d += std::pow (x[i] - x[i - 1], 2.0);
            s += static_cast<double> (x[i]) * x[i];
        }
        return d / std::max (s, 1.0e-30); // 0 for a low-pass signal, 2 for white noise
    };
    const auto white = noise (0.0f, 0.0f), lowpass = noise (-100.0f, 0.0f), highpass = noise (100.0f, 0.0f), down = noise (0.0f, -24.0f), up = noise (0.0f, 12.0f);
    std::printf ("    noise: rms white %.3f low-pass %.3f high-pass %.3f up12 %.3f | roughness white %.2f low-pass %.3f high-pass %.2f down24 %.3f\n", rmsOfRange (white, 12000, 40000),
                 rmsOfRange (lowpass, 12000, 40000), rmsOfRange (highpass, 12000, 40000), rmsOfRange (up, 12000, 40000), hf (white), hf (lowpass), hf (highpass), hf (down));
    CHECK (rmsOfRange (lowpass, 12000, 40000) < 0.12 * rmsOfRange (white, 12000, 40000));
    CHECK (hf (lowpass) < 0.05);
    CHECK (hf (highpass) > hf (white));
    CHECK (hf (down) < 0.25 * hf (white));
    CHECK_NEAR (rmsOfRange (up, 12000, 40000) / rmsOfRange (white, 12000, 40000), 0.757, 0.08);
}

// ------------------------------------------------------------------------------------------------------------------------------------
// External Mask/Vocode warp checksum reference for hop-rate FFT (rank 3)
// Reuses Rig and set from TEST (own_osc_spectral_mask_from_the_noise) at line 1398
TEST (own_osc_spectral_external_warp_checksum)
{
    auto printChecksum = [] (const char* name, const std::vector<float>& left)
    {
        CHECK (sbtest::allFinite (left));
        CHECK (left.size() >= 8);
        const double rms = rmsOfRange (left, 0, left.size());
        CHECK (rms > 0.01);
        double sumAbs = 0.0;
        for (float s : left)
            sumAbs += std::fabs (static_cast<double> (s));
        CHECK (sumAbs > 0.0);
        const size_t sz = left.size();
        std::printf ("SPECEXT-CHECKSUM %s sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     name, sumAbs,
                     static_cast<double> (left[sz - 8]),
                     static_cast<double> (left[sz - 7]),
                     static_cast<double> (left[sz - 6]),
                     static_cast<double> (left[sz - 5]),
                     static_cast<double> (left[sz - 4]),
                     static_cast<double> (left[sz - 3]),
                     static_cast<double> (left[sz - 2]),
                     static_cast<double> (left[sz - 1]));
    };

    // 1. mask_noise_u1: osc1.type = 3, osc1.unison.voices = 1, osc1.warp.type = 95, osc1.warp.amount = 100, noise.on = 1, noise.level = 100
    {
        Rig r (true, "osc1.type", 3.0f);
        set (*r.e, "osc1.unison.voices", 1.0f);
        set (*r.e, "osc1.warp.type", 95.0f);
        set (*r.e, "osc1.warp.amount", 100.0f);
        set (*r.e, "noise.on", 1.0f);
        set (*r.e, "noise.level", 100.0f);
        const auto out = r.render (69, 0.45, 0.5);
        printChecksum ("mask_noise_u1", out.l);
    }

    // 2. mask_and_vocode_noise_u4: osc1.type = 3, osc1.unison.voices = 4, osc1.warp.type = 95, osc1.warp.amount = 80,
    //                             osc1.warp2.type = 98, osc1.warp2.amount = 60, noise.on = 1, noise.level = 100
    {
        Rig r (true, "osc1.type", 3.0f);
        set (*r.e, "osc1.unison.voices", 4.0f);
        set (*r.e, "osc1.warp.type", 95.0f);
        set (*r.e, "osc1.warp.amount", 80.0f);
        set (*r.e, "osc1.warp2.type", 98.0f);
        set (*r.e, "osc1.warp2.amount", 60.0f);
        set (*r.e, "noise.on", 1.0f);
        set (*r.e, "noise.level", 100.0f);
        const auto out = r.render (69, 0.45, 0.5);
        printChecksum ("mask_and_vocode_noise_u4", out.l);
    }

    // 3. mask_osc2_u2: osc1.type = 3, osc1.unison.voices = 2, osc1.warp.type = 93, osc1.warp.amount = 100,
    //                  osc2.on = 1, osc2.wavetable = 1, osc2.level = 100
    {
        Rig r (true, "osc1.type", 3.0f);
        set (*r.e, "osc1.unison.voices", 2.0f);
        set (*r.e, "osc1.warp.type", 93.0f);
        set (*r.e, "osc1.warp.amount", 100.0f);
        set (*r.e, "osc2.on", 1.0f);
        set (*r.e, "osc2.wavetable", 1.0f);
        set (*r.e, "osc2.level", 100.0f);
        const auto out = r.render (69, 0.45, 0.5);
        printChecksum ("mask_osc2_u2", out.l);
    }
}

// docs/plans/spectral-start.md S4: the engine owns the start ledger. It runs only where the quality asks for it and has the 32 sample event grid (tier low): one tick per control period of 32
// samples whatever the host block size; tier high and a low quality without the control economy leave it disabled (no tick, nothing stored).
TEST (own_spectral_start_ledger_runs_with_the_control_grid_only_at_tier_low)
{
    auto ticksOf = [] (const Quality& q, int block, bool& enabled)
    {
        auto engine = createOwnEngine();
        engine->prepare (44100.0, 512, q);
        std::vector<float> l (512), r (512);
        for (int done = 0; done < 1280; done += block)
            engine->process (nullptr, 0, l.data(), r.data(), std::min (block, 1280 - done));
        const auto& ledger = static_cast<own::OwnEngine*> (engine.get())->startLedgerForTest();
        enabled = ledger.enabledForTest();
        return ledger.tickForTest();
    };
    for (int block : { 128, 32, 512 })
    {
        bool enabled = false;
        const uint32_t ticks = ticksOf (qualityForTier (Tier::low), block, enabled);
        CHECK (enabled);
        CHECK_EQ (ticks, 1280u / 32u);   // 40 control periods, the host block does not matter (the last block of the 512 run is 256 frames)
    }
    {
        bool enabled = true;
        const uint32_t ticks = ticksOf (qualityForTier (Tier::high), 128, enabled);
        CHECK (! enabled);
        CHECK_EQ (ticks, 0u);
    }
    {
        Quality q = qualityForTier (Tier::low);
        q.economy = 0;   // no control economy: no event grid, no stagger
        bool enabled = true;
        const uint32_t ticks = ticksOf (q, 128, enabled);
        CHECK (! enabled);
        CHECK_EQ (ticks, 0u);
    }
}

// ---- docs/plans/spectral-start.md S5: the Spectral note start is spread over the blocks at tier low --------------------------------------------------------------------------------------
// The numbers are those of the cost model at the budget of 1700 us a block (spectral::kStartBudgetUs, fixed by the device budget sweep of 2026-11-21; the plan's first figures were for 810 us): a voice's
// start work is 156 .. 245 us warm (mono), so 8 voices of a chord start at once (1604 us) and the others in the next quads; a tick = a control period = 32 samples = 0.7256 ms at 44.1 kHz,
// a quad = 4 ticks = 128 samples = 2.9025 ms.
namespace
{
constexpr double kSpecSr = 44100.0;
constexpr int kSpecKeys[16] = { 60, 64, 67, 72, 62, 65, 69, 71, 55, 59, 52, 57, 50, 53, 74, 76 };

struct SpecRig
{
    MemoryContentStore store;
    StateContent content;
    std::unique_ptr<IEngine> e;

    /** `q` is tier low with the stagger on or off; osc1 is the Spectral oscillator on a 2 s sine, no envelope attack, no filter; `liftUnisonCap`: unison 4 (the low tier's cap is 3). */
    SpecRig (const Quality& q, int unison, int polyphony = 8, bool liftUnisonCap = false, bool killFirst = false)
    {
        e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, static_cast<float> (polyphony));
        e->prepare (kSpecSr, 512, q);
        if (liftUnisonCap)
            static_cast<own::OwnEngine*> (e.get())->setLowTierUnisonCap (0);
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "env1.release", 50.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "osc1.random_phase", 0.0f);
        set (*e, "osc1.type", 3.0f);
        set (*e, "osc1.unison.voices", static_cast<float> (unison));
        if (killFirst)
        {
            set (*e, "master.voice_override", 0.0f);       // Kill mode
            set (*e, "master.voice.steal_priority", 0.0f); // the value whose case is the default of pickVictim: the earliest goes
        }
        store.add ("test", "sine.wav", sineWav (440.0, 0.8, 44100, 2.0));
        ContentFileRef ref;
        ref.pack = "test";
        ref.file = "sine.wav";
        ref.name = "sine";
        attachSample (content, 1, ref, 44100, 88200, 1);
        e->setContentStore (&store);
        e->setContent (content);
        set (*e, "osc1.sample.root", 69.0f);
        set (*e, "osc1.sample.loop_mode", 1.0f);
    }

    const spectral::StartLedger& ledger() const { return static_cast<own::OwnEngine*> (e.get())->startLedgerForTest(); }

    /** `notes` note-ons at frame 0; the last note is released at frame `offLastAt`, the others at `offAllAt` (-1: never). */
    Out render (int notes, int frames, int block, int offLastAt = -1, int offAllAt = -1)
    {
        Out o;
        o.l.assign (static_cast<size_t> (frames), 0.0f);
        o.r.assign (static_cast<size_t> (frames), 0.0f);
        for (int pos = 0; pos < frames; pos += block)
        {
            const int n = std::min (block, frames - pos);
            Event ev[20];
            int ne = 0;
            if (pos == 0)
                for (int i = 0; i < notes; ++i)
                    ev[ne++] = sbtest::noteOn (0, kSpecKeys[i], 1.0f);
            if (offLastAt >= pos && offLastAt < pos + n)
                ev[ne++] = sbtest::noteOff (offLastAt - pos, kSpecKeys[notes - 1]);
            if (offAllAt >= pos && offAllAt < pos + n)
                for (int i = 0; i < notes; ++i)
                    if (! (offLastAt >= 0 && i == notes - 1))
                        ev[ne++] = sbtest::noteOff (offAllAt - pos, kSpecKeys[i]);
            e->process (ev, ne, o.l.data() + pos, o.r.data() + pos, n);
        }
        return o;
    }
};

Quality lowStagger (bool on)
{
    Quality q = qualityForTier (Tier::low);
    q.spectralStart = on ? 1 : 0;
    return q;
}

uint64_t fnv1a (const Out& o)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h] (const std::vector<float>& x)
    {
        const unsigned char* b = reinterpret_cast<const unsigned char*> (x.data());
        for (size_t i = 0; i < x.size() * sizeof (float); ++i)
        {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    };
    mix (o.l);
    mix (o.r);
    return h;
}
} // namespace

TEST (own_spectral_chord_start_is_spread_by_the_budget)
{
    struct Row { int notes, unison; bool lift; uint32_t now, later, maxWait; };
    const Row rows[] = {
        { 3, 1, false, 3, 0, 0 },     // 3 voices: 557 .. 646 us
        { 1, 4, true, 4, 0, 0 },      // a single note of unison 4: 802 us, never delayed
        { 3, 2, false, 6, 0, 0 },     // 1203 us
        { 4, 2, false, 8, 0, 0 },     // 1604 us: the chord of the three Spectral presets starts whole (it waited 8 ticks = 5.8 ms at the budget of 810)
        { 3, 4, true, 8, 4, 4 },      // 12 voices: 8 at once, the other 4 wait one quad (4 ticks = 2.9 ms)
        { 8, 2, false, 8, 8, 8 },     // 16 voices: 8 at once (1604 us), 4 in the next quad, 4 in the one after (max wait 8 ticks = 5.8 ms)
    };
    for (const Row& row : rows)
    {
        SpecRig r (lowStagger (true), row.unison, 8, row.lift);
        r.render (row.notes, 2048, 128);
        const auto& l = r.ledger();
        CHECK (l.enabledForTest());
        CHECK_EQ (l.grantedNowForTest(), row.now);
        CHECK_EQ (l.grantedLaterForTest(), row.later);
        CHECK_EQ (l.maxWaitTicksForTest(), row.maxWait);
        CHECK (l.maxBookedForTest() <= static_cast<uint32_t> (spectral::kStartBudgetUs));
        CHECK_EQ (l.waitingForTest(), 0);
        std::printf ("    %d notes x %d: now %u later %u max wait %u ticks (%.1f ms)\n", row.notes, row.unison, l.grantedNowForTest(), l.grantedLaterForTest(), l.maxWaitTicksForTest(),
                     l.maxWaitTicksForTest() * 32.0 / kSpecSr * 1000.0);
    }
    {   // the stagger off: nothing is requested
        SpecRig r (lowStagger (false), 2);
        r.render (8, 2048, 128);
        CHECK (! r.ledger().enabledForTest());
        CHECK_EQ (r.ledger().grantedNowForTest() + r.ledger().grantedLaterForTest(), 0u);
    }
}

TEST (own_spectral_a_waiting_voice_is_silent)
{
    // One note, one voice, nothing else sounding. Before the note a foreign start (note serial 0, a pattern that fills all five quads to the budget) is booked in the engine's ledger,
    // so the voice of the note does not fit: it waits until the quads of the foreign start are over (quad 5 = tick 20 of the engine) and has to output exactly nothing until then.
    // The ledger is switched on by the first process call, which is an empty period of 32 frames (tick 0); the note starts in tick 1 and is granted in tick 20.
    constexpr int kFrames = 4096;
    constexpr int kGrantTick = 5 * 4;
    constexpr int kGrantFrame = (kGrantTick - 1) * 32;   // counted from the note
    spectral::StartPattern full;
    for (auto& us : full.us)
        us = static_cast<uint16_t> (spectral::kStartBudgetUs);

    SpecRig waiting (lowStagger (true), 1);
    {
        float pl[32], pr[32];
        waiting.e->process (nullptr, 0, pl, pr, 32);
    }
    auto& ledger = const_cast<spectral::StartLedger&> (waiting.ledger());
    CHECK (ledger.enabledForTest());
    CHECK (ledger.request (spectral::startKey (0, 0, 0), full));   // granted: the ledger is empty, and it books every quad up to the budget
    const Out a = waiting.render (1, kFrames, 128);
    CHECK_EQ (ledger.grantedLaterForTest(), 1u);                  // the voice of the note was granted after waiting ...
    CHECK_EQ (ledger.maxWaitTicksForTest(), static_cast<uint32_t> (kGrantTick - 1));   // ... exactly when the foreign booking ended
    CHECK_EQ (ledger.waitingForTest(), 0);

    int firstSound = -1;
    for (int i = 0; i < kFrames && firstSound < 0; ++i)
        if (a.l[static_cast<size_t> (i)] != 0.0f || a.r[static_cast<size_t> (i)] != 0.0f)
            firstSound = i;
    std::printf ("    waiting voice: first non-zero sample at %d (granted at frame %d)\n", firstSound, kGrantFrame);
    CHECK (firstSound >= kGrantFrame);                 // every output sample before the grant is exactly 0.0f (no tolerance)
    CHECK (firstSound >= 0);                           // after it the voice sounds
    CHECK (peakOf (a.l, kGrantFrame + 1024, a.l.size()) > 1.0e-3);
    CHECK (sbtest::allFinite (a.l) && sbtest::allFinite (a.r));

    // control: the same note without the foreign booking sounds from the first block (so the silence above is the ledger's wait, not the sound's own attack)
    SpecRig free (lowStagger (true), 1);
    const Out b = free.render (1, kFrames, 128);
    CHECK_EQ (free.ledger().grantedNowForTest(), 1u);
    CHECK (peakOf (b.l, 0, static_cast<size_t> (kGrantFrame)) > 1.0e-3);
}

TEST (own_spectral_kill_burst_keeps_the_start_work_of_the_survivors_only)
{
    // 16 notes struck together, polyphony 4, Kill mode: the notes 12 .. 15 survive. Notes 0 .. 3 got their start at the note-on (8 voices); the work booked for the given up voices of
    // the later quads is given back, so the survivors start in the next quad (4 ticks); without cancelSerial four of them would wait 8.
    SpecRig r (lowStagger (true), 2, 4, false, true);
    r.render (16, 2048, 128);
    const auto& l = r.ledger();
    CHECK_EQ (l.grantedNowForTest(), 8u);
    CHECK_EQ (l.grantedLaterForTest(), 8u);
    CHECK_EQ (l.maxWaitTicksForTest(), 4u);
    CHECK_EQ (l.waitingForTest(), 0);
}

TEST (own_spectral_start_is_block_size_independent_and_repeats)
{
    uint64_t first = 0;
    for (int block : { 32, 64, 128, 256, 512, 128 })
    {
        SpecRig r (lowStagger (true), 2);
        const Out o = r.render (8, 20000, block);
        CHECK (sbtest::allFinite (o.l));
        const uint64_t h = fnv1a (o);
        if (first == 0)
            first = h;
        else
            CHECK (h == first);
        CHECK (r.ledger().maxWaitTicksForTest() == 8u);
    }
}

TEST (own_spectral_note_off_while_waiting)
{
    // the last note (its voices wait until tick 8 = frame 256) is released at frame 40, the others at 0.4 s
    SpecRig r (lowStagger (true), 2);
    const Out o = r.render (8, 2 * 44100, 128, 40, 17640);
    CHECK_EQ (r.ledger().maxWaitTicksForTest(), 8u);
    CHECK (sbtest::allFinite (o.l) && sbtest::allFinite (o.r));
    CHECK (rmsOfRange (o.l, 2000, 8000) > 1.0e-4);
    CHECK (peakOf (o.l, 60000, o.l.size()) < 1.0e-4);
}

// Where the sound must not change (the build worker compares the lines of the base and the changed commit): a single note, a chord that fits the budget, tier high. The chord of 8 notes of
// unison 2 is the one that waits: its line with the stagger on is the only one that may differ from the stagger off after the change.
TEST (own_spectral_start_checksums_and_what_must_not_change)
{
    auto print = [] (const char* name, const Out& o) -> uint64_t
    {
        CHECK (sbtest::allFinite (o.l));
        double sumAbs = 0.0;
        for (float x : o.l)
            sumAbs += std::fabs (static_cast<double> (x));
        CHECK (sumAbs > 0.0);
        const size_t sz = o.l.size();
        std::printf ("SPECSTART-CHECKSUM %s sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", name, sumAbs,
                     static_cast<double> (o.l[sz - 8]), static_cast<double> (o.l[sz - 7]), static_cast<double> (o.l[sz - 6]), static_cast<double> (o.l[sz - 5]),
                     static_cast<double> (o.l[sz - 4]), static_cast<double> (o.l[sz - 3]), static_cast<double> (o.l[sz - 2]), static_cast<double> (o.l[sz - 1]));
        return fnv1a (o);
    };
    constexpr int kFrames = 8192;
    const uint64_t singleOn = print ("single_on", SpecRig (lowStagger (true), 2).render (1, kFrames, 128));
    const uint64_t singleOff = print ("single_off", SpecRig (lowStagger (false), 2).render (1, kFrames, 128));
    const uint64_t c3On = print ("chord3x1_on", SpecRig (lowStagger (true), 1).render (3, kFrames, 128));
    const uint64_t c3Off = print ("chord3x1_off", SpecRig (lowStagger (false), 1).render (3, kFrames, 128));
    const uint64_t c4On = print ("chord4x2_on", SpecRig (lowStagger (true), 2).render (4, kFrames, 128));
    const uint64_t c4Off = print ("chord4x2_off", SpecRig (lowStagger (false), 2).render (4, kFrames, 128));
    const uint64_t c8On = print ("chord8x2_on", SpecRig (lowStagger (true), 2).render (8, kFrames, 128));
    const uint64_t c8Off = print ("chord8x2_off", SpecRig (lowStagger (false), 2).render (8, kFrames, 128));
    Quality high = qualityForTier (Tier::high);
    const uint64_t h8 = print ("high_chord8x2", SpecRig (high, 2).render (8, kFrames, 128));
    high.spectralStart = 1;   // no control economy at tier high: the setting does nothing
    const uint64_t h8Flag = print ("high_chord8x2_flag", SpecRig (high, 2).render (8, kFrames, 128));
    CHECK (singleOn == singleOff);     // a single note is never delayed
    CHECK (c3On == c3Off);             // a chord that fits the budget is the sound of before
    CHECK (c4On == c4Off);             // so is the chord of the three Spectral presets (1604 us)
    CHECK (c8On != c8Off);             // 16 voices do not fit: eight start a quad or two late (before the change: equal)
    CHECK (h8 == h8Flag);              // nothing changes at the other tiers
}

// Quality::spectralStartBudgetUs: 0 is the default budget (the render of chord8x2_on), a budget that holds the whole chord is the render with the ledger off.
TEST (own_spectral_start_budget_quality_field)
{
    constexpr int kFrames = 8192;
    const uint64_t def = fnv1a (SpecRig (lowStagger (true), 2).render (8, kFrames, 128));
    const uint64_t off = fnv1a (SpecRig (lowStagger (false), 2).render (8, kFrames, 128));
    Quality q0 = lowStagger (true);
    q0.spectralStartBudgetUs = 0;
    CHECK (fnv1a (SpecRig (q0, 2).render (8, kFrames, 128)) == def);
    Quality big = lowStagger (true);
    big.spectralStartBudgetUs = 100000;
    CHECK (fnv1a (SpecRig (big, 2).render (8, kFrames, 128)) == off);
    CHECK (def != off);
}

// Key 61 device profile ("low,startbudget=900", shell-mpc/device_profile.h): deterministic worst-case deferred start bounds for 16-note Spectral chords.
TEST (own_spectral_key61_900us_16_note_chord_start_bound)
{
    auto key61Quality = [] () -> Quality
    {
        Quality q = qualityForTier (Tier::low);
        q.maxVoices = 8;
        q.maxUnison = 3;
        q.economy = static_cast<int> (kEconomyAll);
        q.spectralStart = 1;
        q.spectralStartBudgetUs = 900;
        return q;
    };

    // 1. Factory preset polyphony 4, U=2, 16-note strike (Kill mode, mono): 4 granted at tick 0 (killed by later notes), 8 survivors finish by tick 12 (8.71 ms @ 44.1 kHz).
    {
        SpecRig r (key61Quality(), 2, 4, false, true);
        const Out o = r.render (16, 2048, 128);
        const auto& l = r.ledger();
        CHECK (l.enabledForTest());
        CHECK_EQ (l.grantedNowForTest(), 4u);
        CHECK_EQ (l.grantedLaterForTest(), 8u);
        CHECK_EQ (l.maxWaitTicksForTest(), 12u);
        CHECK (l.maxBookedForTest() <= 900u);
        CHECK_EQ (l.waitingForTest(), 0);
        CHECK (sbtest::allFinite (o.l) && sbtest::allFinite (o.r));
        CHECK (rmsOfRange (o.l, 1024, 2048) > 1.0e-4);
    }

    // 2. Key 61 cap polyphony 8, U=3, 16-note strike (Kill mode, mono) + block-size determinism { 32, 128, 256 }.
    {
        uint64_t firstHash = 0;
        for (int block : { 32, 128, 256 })
        {
            SpecRig r (key61Quality(), 3, 8, false, true);
            const Out o = r.render (16, 4096, block);
            const auto& l = r.ledger();
            CHECK (l.enabledForTest());
            CHECK_EQ (l.grantedNowForTest(), 4u);
            CHECK_EQ (l.grantedLaterForTest(), 24u);
            CHECK_EQ (l.maxWaitTicksForTest(), 48u);
            CHECK (l.maxBookedForTest() <= 900u);
            CHECK_EQ (l.waitingForTest(), 0);
            CHECK (sbtest::allFinite (o.l) && sbtest::allFinite (o.r));
            CHECK (rmsOfRange (o.l, 2048, 4096) > 1.0e-4);
            const uint64_t h = fnv1a (o);
            if (firstHash == 0)
                firstHash = h;
            else
                CHECK (h == firstHash);
        }
    }

    // 3. Key 61 cap polyphony 8, U=3, 16-note strike (Steal mode, mono): same 48-tick worst-case delay.
    {
        SpecRig r (key61Quality(), 3, 8, false, true);
        set (*r.e, "master.voice_override", 1.0f);
        const Out o = r.render (16, 4096, 128);
        const auto& l = r.ledger();
        CHECK_EQ (l.grantedNowForTest(), 4u);
        CHECK_EQ (l.grantedLaterForTest(), 24u);
        CHECK_EQ (l.maxWaitTicksForTest(), 48u);
        CHECK (l.maxBookedForTest() <= 900u);
        CHECK_EQ (l.waitingForTest(), 0);
        CHECK (sbtest::allFinite (o.l) && sbtest::allFinite (o.r));
        CHECK (rmsOfRange (o.l, 2048, 4096) > 1.0e-4);
    }

    // 4. Uncapped 16-note chord (maxVoices=16, polyphony=16, U=3, mono): all 48 voices survive, max wait 92 ticks (66.76 ms @ 44.1 kHz).
    {
        Quality q16 = key61Quality();
        q16.maxVoices = 16;
        SpecRig r (q16, 3, 16, false, true);
        const Out o = r.render (16, 4096, 128);
        const auto& l = r.ledger();
        CHECK_EQ (l.grantedNowForTest(), 4u);
        CHECK_EQ (l.grantedLaterForTest(), 44u);
        CHECK_EQ (l.maxWaitTicksForTest(), 92u);
        CHECK (l.maxBookedForTest() <= 900u);
        CHECK_EQ (l.waitingForTest(), 0);
        CHECK (sbtest::allFinite (o.l) && sbtest::allFinite (o.r));
        CHECK (rmsOfRange (o.l, 3200, 4096) > 1.0e-4);
    }

    // 5. Key 61 cap polyphony 8, U=3, 1 osc stereo, 16-note strike (Kill mode): max wait 140 ticks (101.59 ms @ 44.1 kHz).
    {
        MemoryContentStore store;
        StateContent content;
        auto e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->prepare (kSpecSr, 512, key61Quality());
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "env1.release", 50.0f);
        set (*e, "master.voice_override", 0.0f);
        set (*e, "master.voice.steal_priority", 0.0f);
        set (*e, "osc1.on", 1.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "osc1.random_phase", 0.0f);
        set (*e, "osc1.type", 3.0f);
        set (*e, "osc1.unison.voices", 3.0f);
        constexpr size_t kWavFrames = 88200;
        std::vector<float> sl (kWavFrames), sr (kWavFrames);
        for (size_t i = 0; i < kWavFrames; ++i)
        {
            sl[i] = static_cast<float> (0.8 * std::sin (6.283185307179586 * 440.0 * static_cast<double> (i) / 44100.0));
            sr[i] = static_cast<float> (0.8 * std::sin (6.283185307179586 * 554.37 * static_cast<double> (i) / 44100.0));
        }
        store.add ("test", "stereo_sine.wav", encodeWav (sl.data(), sr.data(), kWavFrames, 44100, 16));
        ContentFileRef ref;
        ref.pack = "test";
        ref.file = "stereo_sine.wav";
        ref.name = "stereo_sine";
        attachSample (content, 1, ref, 44100, static_cast<int> (kWavFrames), 2);
        e->setContentStore (&store);
        e->setContent (content);
        set (*e, "osc1.sample.root", 69.0f);
        set (*e, "osc1.sample.loop_mode", 1.0f);

        constexpr int kFrames = 6144;
        Out o;
        o.l.assign (kFrames, 0.0f);
        o.r.assign (kFrames, 0.0f);
        for (int pos = 0; pos < kFrames; pos += 128)
        {
            Event ev[16];
            int ne = 0;
            if (pos == 0)
                for (int i = 0; i < 16; ++i)
                    ev[ne++] = sbtest::noteOn (0, kSpecKeys[i], 1.0f);
            e->process (ev, ne, o.l.data() + pos, o.r.data() + pos, 128);
        }
        const auto& l = static_cast<own::OwnEngine*> (e.get())->startLedgerForTest();
        CHECK_EQ (l.grantedNowForTest(), 2u);
        CHECK_EQ (l.grantedLaterForTest(), 24u);
        CHECK_EQ (l.maxWaitTicksForTest(), 140u);
        CHECK (l.maxBookedForTest() <= 900u);
        CHECK_EQ (l.waitingForTest(), 0);
        CHECK (sbtest::allFinite (o.l) && sbtest::allFinite (o.r));
        CHECK (rmsOfRange (o.l, 4800, 6144) > 1.0e-4);
        CHECK (rmsOfRange (o.r, 4800, 6144) > 1.0e-4);
    }

    // 6. Key 61 cap polyphony 8, U=3, all 3 oscillators Spectral (mono), 16-note strike (Kill mode): 72 surviving voices, max wait 156 ticks (113.20 ms @ 44.1 kHz).
    {
        MemoryContentStore store;
        StateContent content;
        auto e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->prepare (kSpecSr, 512, key61Quality());
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "env1.release", 50.0f);
        set (*e, "master.voice_override", 0.0f);
        set (*e, "master.voice.steal_priority", 0.0f);
        store.add ("test", "sine.wav", sineWav (440.0, 0.8, 44100, 2.0));
        ContentFileRef ref;
        ref.pack = "test";
        ref.file = "sine.wav";
        ref.name = "sine";
        for (int oscNum = 1; oscNum <= 3; ++oscNum)
            attachSample (content, oscNum, ref, 44100, 88200, 1);
        e->setContentStore (&store);
        e->setContent (content);
        for (int oscNum = 1; oscNum <= 3; ++oscNum)
        {
            const std::string P = "osc" + std::to_string (oscNum);
            set (*e, (P + ".on").c_str(), 1.0f);
            set (*e, (P + ".level").c_str(), 100.0f);
            set (*e, (P + ".random_phase").c_str(), 0.0f);
            set (*e, (P + ".type").c_str(), 3.0f);
            set (*e, (P + ".unison.voices").c_str(), 3.0f);
            set (*e, (P + ".sample.root").c_str(), 69.0f);
            set (*e, (P + ".sample.loop_mode").c_str(), 1.0f);
        }

        constexpr int kFrames = 6144;
        Out o;
        o.l.assign (kFrames, 0.0f);
        o.r.assign (kFrames, 0.0f);
        for (int pos = 0; pos < kFrames; pos += 128)
        {
            Event ev[16];
            int ne = 0;
            if (pos == 0)
                for (int i = 0; i < 16; ++i)
                    ev[ne++] = sbtest::noteOn (0, kSpecKeys[i], 1.0f);
            e->process (ev, ne, o.l.data() + pos, o.r.data() + pos, 128);
        }
        const auto& l = static_cast<own::OwnEngine*> (e.get())->startLedgerForTest();
        CHECK_EQ (l.grantedNowForTest(), 4u);
        CHECK_EQ (l.grantedLaterForTest(), 72u);
        CHECK_EQ (l.maxWaitTicksForTest(), 156u);
        CHECK (l.maxBookedForTest() <= 900u);
        CHECK_EQ (l.waitingForTest(), 0);
        CHECK (sbtest::allFinite (o.l) && sbtest::allFinite (o.r));
        CHECK (rmsOfRange (o.l, 5200, 6144) > 1.0e-4);
    }
}

TEST (own_sample_loop_exit_leaves_loop_on_release)
{
    // A sample with a loop (e.g. 10% to 30%):
    // When loop_exit = 0, on noteOff the sample continues looping.
    // When loop_exit = 1, on noteOff the sample exits the loop and plays forward past 30%.
    auto renderSample = [] (int oscNum, float loopExitVal)
    {
        MemoryContentStore store;
        StateContent content;
        auto e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->prepare (kSr, 256, qualityForTier (Tier::high));
        store.add ("test", "sine.wav", risingSineWav (437.0, 0.8, 44100, 2.0)); // 437 Hz: 0.4 s of loop is 174.8 periods, not whole ones
        ContentFileRef ref;
        ref.pack = "test";
        ref.file = "sine.wav";
        ref.name = "sine";
        attachSample (content, oscNum, ref, 44100, 88200, 1);
        e->setContentStore (&store);
        e->setContent (content);

        const std::string P = "osc" + std::to_string (oscNum);
        if (oscNum != 1)
        {
            set (*e, "osc1.on", 0.0f);
            set (*e, (P + ".on").c_str(), 1.0f);
        }
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "env1.sustain", 100.0f);
        set (*e, "env1.release", 500.0f); // 500 ms release tail
        set (*e, (P + ".level").c_str(), 100.0f);
        set (*e, (P + ".type").c_str(), 1.0f); // Sample oscillator
        set (*e, (P + ".sample.root").c_str(), 69.0f);
        set (*e, (P + ".sample.loop_mode").c_str(), 1.0f); // forward loop
        set (*e, (P + ".sample.loop_start").c_str(), 10.0f);
        set (*e, (P + ".sample.loop_end").c_str(), 30.0f);
        set (*e, (P + ".sample.loop_exit").c_str(), loopExitVal);

        // The loop is 0.2 .. 0.6 s of the 2 s file (played at 44100 / 48000 of the host rate). Note-on at 0, note-off at 0.9 s (the loop has wrapped once and the voice is
        // inside it, 0.19 s before its end), render to 1.4 s: leaving the loop reaches its end at about 1.09 s and goes on to the louder part of the file, keeping it wraps back to
        // the quiet start of the loop.
        const size_t total = static_cast<size_t> (1.4 * kSr);
        const size_t offAt = static_cast<size_t> (0.9 * kSr);
        Out o;
        o.l.assign (total, 0.0f);
        o.r.assign (total, 0.0f);
        size_t pos = 0;
        while (pos < total)
        {
            const size_t n = std::min<size_t> (256, total - pos);
            Event ev[2];
            int ne = 0;
            if (pos == 0)
                ev[ne++] = sbtest::noteOn (0, 69, 1.0f);
            if (offAt >= pos && offAt < pos + n)
                ev[ne++] = sbtest::noteOff (static_cast<int> (offAt - pos), 69);
            e->process (ev, ne, o.l.data() + pos, o.r.data() + pos, static_cast<int> (n));
            pos += n;
        }
        return o;
    };

    auto diffDb = [] (const Out& a, const Out& b, size_t from)
    {
        double d = 0.0, p = 1.0e-9;
        for (size_t i = from; i < a.l.size(); ++i)
        {
            d = std::max (d, std::fabs (static_cast<double> (a.l[i] - b.l[i])));
            p = std::max (p, std::fabs (static_cast<double> (a.l[i])));
        }
        return 20.0 * std::log10 (std::max (d / p, 1.0e-9));
    };

    const size_t offSample = static_cast<size_t> (1.15 * kSr); // after the end of the loop is reached when it is left
    // osc1: loop_exit 0 vs 1 must differ during release, by far more than the threshold (the two renders are different signals)
    const Out o1_0 = renderSample (1, 0.0f), o1_1 = renderSample (1, 1.0f);
    // osc3: the same
    const Out o3_0 = renderSample (3, 0.0f), o3_1 = renderSample (3, 1.0f);
    const double d1 = diffDb (o1_0, o1_1, offSample), d3 = diffDb (o3_0, o3_1, offSample);
    std::printf ("loop_exit 0 vs 1 after the loop end: osc1 %.1f dB, osc3 %.1f dB\n", d1, d3);
    CHECK (d1 > -20.0);
    CHECK (d3 > -20.0);
}

// docs/plans/spectral-start-cache.md: a chord struck again (the note serials move on, so the voices start on other quarters) renders the same bits with the start cache on as with it off, and its
// second strike makes no start frame: every (inputs, channel, frame) is answered by the cache (the engine's ledger books the same either way, so the timing is the one of the cache off).
TEST (own_spectral_start_cache_repeated_chord_is_bit_identical_and_second_strike_hits)
{
    spectral::StartCache& cache = spectral::StartCache::instance();
    cache.prepare();
    constexpr int kStrikeFrames = 235 * 128;   // whole 128 frame blocks: 30000 is not one, and the last block wrote 80 frames past the end of the buffer (heap overflow, ASan)
    static_assert (kStrikeFrames % 128 == 0, "the loop below renders whole blocks");
    auto run = [&] (bool cacheOn, uint64_t& missesAfter1, uint64_t& hitsAfter1, uint64_t& missesAfter2, uint64_t& hitsAfter2) -> Out
    {
        cache.clearForTest();
        Quality q = lowStagger (true);
        q.spectralStartCache = cacheOn ? 1 : 0;
        SpecRig r (q, 2);
        Out all;
        for (int strike = 0; strike < 2; ++strike)
        {
            Out o;
            o.l.assign (static_cast<size_t> (kStrikeFrames), 0.0f);
            o.r.assign (static_cast<size_t> (kStrikeFrames), 0.0f);
            for (int pos = 0; pos < kStrikeFrames; pos += 128)
            {
                Event ev[8];
                int ne = 0;
                if (pos == 0)
                    for (int i = 0; i < 4; ++i)
                        ev[ne++] = sbtest::noteOn (0, kSpecKeys[i], 1.0f);
                if (pos == 15360)
                    for (int i = 0; i < 4; ++i)
                        ev[ne++] = sbtest::noteOff (0, kSpecKeys[i]);
                r.e->process (ev, ne, o.l.data() + pos, o.r.data() + pos, 128);
            }
            all.l.insert (all.l.end(), o.l.begin(), o.l.end());
            all.r.insert (all.r.end(), o.r.begin(), o.r.end());
            const spectral::StartCache::Stats st = cache.statsForTest();
            (strike == 0 ? missesAfter1 : missesAfter2) = st.misses;
            (strike == 0 ? hitsAfter1 : hitsAfter2) = st.hits;
        }
        return all;
    };
    uint64_t m1 = 0, h1 = 0, m2 = 0, h2 = 0, om1 = 0, oh1 = 0, om2 = 0, oh2 = 0;
    const Out off = run (false, om1, oh1, om2, oh2);
    const Out on = run (true, m1, h1, m2, h2);
    CHECK (sbtest::allFinite (off.l) && sbtest::allFinite (on.l));
    CHECK (rmsOfRange (on.l, 2000, 8000) > 1.0e-4);
    CHECK_EQ (om2 + oh2, uint64_t (0));        // the cache off never asks it
    CHECK (m1 > 0);                            // the first strike computes (and stores) its frames
    CHECK_EQ (m2, m1);                         // the second strike computes none
    CHECK (h2 > h1);                           // and every frame of it is a hit
    CHECK (fnv1a (off) == fnv1a (on));
    std::printf ("SPECSTART-CHECKSUM chord4x2_twice_cache_off %016llx\nSPECSTART-CHECKSUM chord4x2_twice_cache_on %016llx\n", static_cast<unsigned long long> (fnv1a (off)),
                 static_cast<unsigned long long> (fnv1a (on)));
    std::printf ("start cache: strike 1 misses %llu hits %llu, strike 2 misses %llu hits %llu\n", static_cast<unsigned long long> (m1), static_cast<unsigned long long> (h1),
                 static_cast<unsigned long long> (m2), static_cast<unsigned long long> (h2));
}
