// SynthyBoy - core tests: display data of all oscillator types (OscCycle / OscDisplay).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/osc_cycle.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"
#include "test_runner.h"
#include "test_util.h"
#include "../engine-own/osc/content_set.h"

using namespace synthyboy;

namespace
{
void setv (ParamValues& v, const char* key, float x)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        v.plain[findParamIndex (p->id)] = constrainValue (*p, x);
}

std::vector<uint8_t> makeSineWav (double hz, double amp, int rate, double seconds)
{
    const size_t n = static_cast<size_t> (rate * seconds);
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float> (amp * std::sin (6.283185307179586 * hz * static_cast<double> (i) / rate));
    return encodeWav (x.data(), nullptr, n, rate, 16);
}

std::vector<uint8_t> makeRampEnvelopeWav (int totalSamples, int rate)
{
    std::vector<float> x (static_cast<size_t> (totalSamples));
    for (int i = 0; i < totalSamples; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (totalSamples);
        x[static_cast<size_t> (i)] = (i % 2 == 0) ? (0.2f + 0.8f * t) : (-0.1f - 0.4f * t);
    }
    return encodeWav (x.data(), nullptr, static_cast<size_t> (totalSamples), rate, 16);
}

std::vector<uint8_t> makeConstantWav (float level, int totalSamples, int rate)
{
    std::vector<float> x (static_cast<size_t> (totalSamples), level);
    return encodeWav (x.data(), nullptr, static_cast<size_t> (totalSamples), rate, 16);
}
} // namespace

// 1. Synthetic sample with known envelope: overview columns within tolerance, separate and interleaved
TEST (own_osc_display_sample_envelope_overview)
{
    constexpr int kSampleLen = 2000;
    constexpr int kRate = 44100;
    MemoryContentStore store;
    store.add ("pack", "ramp.wav", makeRampEnvelopeWav (kSampleLen, kRate));

    StateContent content;
    ContentFileRef ref;
    ref.pack = "pack";
    ref.file = "ramp.wav";
    ref.name = "ramp";
    attachSample (content, 1, ref, kRate, kSampleLen, 1);

    OscCycle oc;
    oc.setContent (content, &store);

    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);
    setv (v, "osc1.type", 1.0f); // Sample

    constexpr int kPoints = 50;
    std::vector<float> outMin (kPoints), outMax (kPoints);
    OscCycle::Request req;
    req.osc = 1;

    // Separate min/max arrays
    const auto res = oc.computeDisplay (v, req, outMin.data(), outMax.data(), kPoints);
    CHECK (res.kind == OscCycle::Kind::sample);

    for (int k = 0; k < kPoints; ++k)
    {
        const float t1 = static_cast<float> (k + 1) / static_cast<float> (kPoints);
        const float expMin = -0.1f - 0.4f * t1;
        const float expMax = 0.2f + 0.8f * t1;
        CHECK_NEAR (outMin[static_cast<size_t> (k)], expMin, 0.05f);
        CHECK_NEAR (outMax[static_cast<size_t> (k)], expMax, 0.05f);
    }

    // Interleaved min/max array (outMax == nullptr, outMin holds 2*kPoints)
    std::vector<float> interleaved (kPoints * 2, 0.0f);
    const auto resInterleaved = oc.computeDisplay (v, req, interleaved.data(), nullptr, kPoints);
    CHECK (resInterleaved.kind == OscCycle::Kind::sample);
    for (int k = 0; k < kPoints; ++k)
    {
        CHECK_NEAR (interleaved[static_cast<size_t> (k * 2)], outMin[static_cast<size_t> (k)], 1e-6f);
        CHECK_NEAR (interleaved[static_cast<size_t> (k * 2 + 1)], outMax[static_cast<size_t> (k)], 1e-6f);
    }
}

// 2. Start, end and loop markers at expected normalised x values
TEST (own_osc_display_sample_markers)
{
    MemoryContentStore store;
    store.add ("p", "s.wav", makeConstantWav (0.5f, 1000, 44100));
    StateContent content;
    ContentFileRef ref;
    ref.pack = "p";
    ref.file = "s.wav";
    attachSample (content, 1, ref, 44100, 1000, 1);

    OscCycle oc;
    oc.setContent (content, &store);

    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);
    setv (v, "osc1.type", 1.0f);
    setv (v, "osc1.sample.start", 15.0f);
    setv (v, "osc1.sample.end", 85.0f);
    setv (v, "osc1.sample.loop_start", 25.0f);
    setv (v, "osc1.sample.loop_end", 75.0f);
    setv (v, "osc1.sample.loop_mode", 1.0f); // continuous loop

    std::vector<float> minBuf (64), maxBuf (64);
    OscCycle::Request req;
    req.osc = 1;

    auto res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 64);
    CHECK (res.kind == OscCycle::Kind::sample);
    CHECK_NEAR (res.start, 0.15f, 1e-4f);
    CHECK_NEAR (res.end, 0.85f, 1e-4f);
    CHECK_NEAR (res.loopStart, 0.25f, 1e-4f);
    CHECK_NEAR (res.loopEnd, 0.75f, 1e-4f);
    CHECK (res.loopOn == true);

    // Relative loop mode: loop positions are relative to [start, end]
    setv (v, "osc1.sample.loop_relative", 1.0f);
    res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 64);
    const float expLoopStart = 0.15f + 0.25f * (0.85f - 0.15f); // 0.325f
    const float expLoopEnd = 0.15f + 0.75f * (0.85f - 0.15f);   // 0.675f
    CHECK_NEAR (res.loopStart, expLoopStart, 1e-4f);
    CHECK_NEAR (res.loopEnd, expLoopEnd, 1e-4f);
    CHECK (res.loopOn == true);

    // Loop off (0.0f)
    setv (v, "osc1.sample.loop_mode", 0.0f);
    res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 64);
    CHECK (res.loopOn == false);
}

// 3. Multisample zone choice by note
TEST (own_osc_display_multisample_zone_by_note)
{
    MemoryContentStore store;
    // Low zone: peak 0.35; High zone: peak 0.85
    store.add ("sfz", "low.wav", makeConstantWav (0.35f, 2000, 44100));
    store.add ("sfz", "high.wav", makeConstantWav (0.85f, 2000, 44100));

    const std::string sfzText =
        "<region> sample=low.wav lokey=0 hikey=59 pitch_keycenter=48\n"
        "<region> sample=high.wav lokey=60 hikey=127 pitch_keycenter=72\n";
    store.add ("sfz", "inst.sfz", std::vector<uint8_t> (sfzText.begin(), sfzText.end()));

    StateContent content;
    ContentFileRef ref;
    ref.pack = "sfz";
    ref.file = "inst.sfz";
    attachInstrument (content, 1, ref, 1);

    OscCycle oc;
    oc.setContent (content, &store);

    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);
    setv (v, "osc1.type", 4.0f); // Multisample

    std::vector<float> minBuf (100), maxBuf (100);

    // Note 48 -> Low zone
    OscCycle::Request reqLow;
    reqLow.osc = 1;
    reqLow.note = 48;
    auto resLow = oc.computeDisplay (v, reqLow, minBuf.data(), maxBuf.data(), 100);
    CHECK (resLow.kind == OscCycle::Kind::multisample);
    float maxValLow = 0.0f;
    for (float m : maxBuf)
        maxValLow = std::max (maxValLow, m);
    CHECK_NEAR (maxValLow, 0.35f, 0.05f);

    // Note 72 -> High zone
    OscCycle::Request reqHigh;
    reqHigh.osc = 1;
    reqHigh.note = 72;
    auto resHigh = oc.computeDisplay (v, reqHigh, minBuf.data(), maxBuf.data(), 100);
    CHECK (resHigh.kind == OscCycle::Kind::multisample);
    float maxValHigh = 0.0f;
    for (float m : maxBuf)
        maxValHigh = std::max (maxValHigh, m);
    CHECK_NEAR (maxValHigh, 0.85f, 0.05f);
}

// 4. Granular grain window position and size
TEST (own_osc_display_granular_window)
{
    constexpr int kLen = 88200; // 2.0 seconds at 44100
    MemoryContentStore store;
    store.add ("p", "gran.wav", makeSineWav (440.0, 0.7, 44100, 2.0));

    StateContent content;
    ContentFileRef ref;
    ref.pack = "p";
    ref.file = "gran.wav";
    attachSample (content, 1, ref, 44100, kLen, 1);

    OscCycle oc;
    oc.setContent (content, &store);

    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);
    setv (v, "osc1.type", 2.0f); // Granular
    setv (v, "osc1.gran.manual", 1.0f); // manual position on
    setv (v, "osc1.gran.position", 45.0f); // 45% position

    std::vector<float> minBuf (64), maxBuf (64);
    OscCycle::Request req;
    req.osc = 1;

    // Mode 0: Free (ms)
    setv (v, "osc1.gran.length_mode", 0.0f);
    setv (v, "osc1.gran.length", 400.0f); // 400 ms out of 2000 ms = 0.20
    auto res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 64);
    CHECK (res.kind == OscCycle::Kind::granular);
    CHECK_NEAR (res.grainPosition, 0.45f, 1e-3f);
    CHECK_NEAR (res.grainSize, 0.20f, 1e-3f);

    // Mode 2: Percent (density 1.0 Hz -> period 1.0 s; length_percent 60% -> lengthSec 0.6 s -> grainSize 0.6/2.0 = 0.30)
    setv (v, "osc1.gran.length_mode", 2.0f);
    setv (v, "osc1.gran.density", 1.0f);
    setv (v, "osc1.gran.length_percent", 60.0f);
    res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 64);
    CHECK_NEAR (res.grainSize, 0.30f, 1e-3f);

    // Mode 1: Beat sync (BPM 120 -> 1 beat = 0.5s; 1/4 note division = index 6 -> 1 beat -> 0.5s / 2.0s = 0.25)
    setv (v, "osc1.gran.length_mode", 1.0f);
    setv (v, "osc1.gran.length_sync", 6.0f); // 1/4 note = 1 beat
    req.bpm = 120.0;
    res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 64);
    CHECK_NEAR (res.grainSize, 0.25f, 1e-3f);
}

// 5. Spectral magnitude spectrum: peak at expected frequency
TEST (own_osc_display_spectral_peak)
{
    // 1000 Hz sine wave
    MemoryContentStore store;
    store.add ("p", "sine1k.wav", makeSineWav (1000.0, 0.8, 44100, 1.0));

    StateContent content;
    ContentFileRef ref;
    ref.pack = "p";
    ref.file = "sine1k.wav";
    attachSample (content, 1, ref, 44100, 44100, 1);

    OscCycle oc;
    oc.setContent (content, &store);

    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);
    setv (v, "osc1.type", 3.0f); // Spectral

    constexpr int kPoints = 512;
    std::vector<float> spectrum (kPoints, 0.0f);
    OscCycle::Request req;
    req.osc = 1;

    const auto res = oc.computeDisplay (v, req, spectrum.data(), nullptr, kPoints);
    CHECK (res.kind == OscCycle::Kind::spectral);

    // Frequency mapping: f = 20.0 * (20000.0 / 20.0)^x, where x in [0, 1]
    // For 1000 Hz: log10(1000 / 20) / log10(1000) = log10(50) / 3 = 1.69897 / 3 = 0.56632
    const int expPeakIndex = static_cast<int> (std::round (0.56632 * static_cast<double> (kPoints - 1))); // ~289

    int maxIdx = 0;
    float maxDb = -200.0f;
    for (int i = 0; i < kPoints; ++i)
    {
        if (spectrum[static_cast<size_t> (i)] > maxDb)
        {
            maxDb = spectrum[static_cast<size_t> (i)];
            maxIdx = i;
        }
    }

    std::printf ("    spectral peak: max %.2f dB at index %d (expected ~%d)\n", static_cast<double> (maxDb), maxIdx, expPeakIndex);
    CHECK (std::abs (maxIdx - expPeakIndex) <= 6);
    CHECK (maxDb > -12.0f);

    // Energy at 50 Hz and 10 kHz should be much lower
    CHECK (spectrum[50] < maxDb - 30.0f);
    CHECK (spectrum[480] < maxDb - 30.0f);
}

// 6. Noise: generator (flag, colour, neutral picture) vs sample
TEST (own_osc_display_noise_generator_and_sample)
{
    MemoryContentStore store;
    store.add ("p", "nsamp.wav", makeConstantWav (0.6f, 1000, 44100));

    StateContent content;
    ContentFileRef ref;
    ref.pack = "p";
    ref.file = "nsamp.wav";
    attachSample (content, 4, ref, 44100, 1000, 1); // noise sample slot is 4

    OscCycle oc;
    oc.setContent (content, &store);

    ParamValues v = ParamValues::defaults();
    setv (v, "noise.on", 1.0f);
    setv (v, "noise.type", 0.0f); // Generator
    setv (v, "noise.sample", 5.0f); // Colour 5

    std::vector<float> minBuf (64, 1.0f), maxBuf (64, 1.0f);
    OscCycle::Request req;
    req.osc = 4; // noise

    // Generator noise: kind is noise, isNoise is true, neutral zeros returned
    auto res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 64);
    CHECK (res.kind == OscCycle::Kind::noise);
    CHECK (res.isNoise == true);
    CHECK (res.noiseType == 0);
    CHECK (res.noiseColour == 5);
    for (int i = 0; i < 64; ++i)
    {
        CHECK (minBuf[static_cast<size_t> (i)] == 0.0f);
        CHECK (maxBuf[static_cast<size_t> (i)] == 0.0f);
    }

    // Sample noise: kind is sample, isNoise is true, noiseType is 1, envelope returned
    setv (v, "noise.type", 1.0f); // Sample
    res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 64);
    CHECK (res.kind == OscCycle::Kind::sample);
    CHECK (res.isNoise == true);
    CHECK (res.noiseType == 1);
    float maxVal = 0.0f;
    for (float m : maxBuf)
        maxVal = std::max (maxVal, m);
    CHECK_NEAR (maxVal, 0.6f, 0.05f);
}

// 7. No sample loaded: reports Kind::noSample, zeros written, no error
TEST (own_osc_display_no_sample_loaded)
{
    OscCycle oc;
    // No content set
    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);

    std::vector<float> minBuf (64, 99.0f), maxBuf (64, 99.0f);
    OscCycle::Request req;
    req.osc = 1;

    for (float t : { 1.0f, 2.0f, 3.0f, 4.0f }) // sample, granular, spectral, multisample
    {
        setv (v, "osc1.type", t);
        const auto res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 64);
        CHECK (res.kind == OscCycle::Kind::noSample);
        for (int i = 0; i < 64; ++i)
        {
            CHECK (minBuf[static_cast<size_t> (i)] == 0.0f);
            CHECK (maxBuf[static_cast<size_t> (i)] == 0.0f);
        }
    }
}

// 8. Sibling class OscDisplay and determinism
TEST (own_osc_display_sibling_class_and_determinism)
{
    MemoryContentStore store;
    store.add ("p", "det.wav", makeSineWav (440.0, 0.8, 44100, 1.0));
    StateContent content;
    ContentFileRef ref;
    ref.pack = "p";
    ref.file = "det.wav";
    attachSample (content, 1, ref, 44100, 44100, 1);

    OscCycle oc1, oc2;
    OscDisplay disp;
    oc1.setContent (content, &store);
    oc2.setContent (content, &store);
    disp.setContent (content, &store);

    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);
    setv (v, "osc1.type", 1.0f);

    std::vector<float> min1 (128), max1 (128);
    std::vector<float> min2 (128), max2 (128);
    std::vector<float> minD (128), maxD (128);
    OscCycle::Request req;
    req.osc = 1;

    const auto r1 = oc1.computeDisplay (v, req, min1.data(), max1.data(), 128);
    const auto r2 = oc2.computeDisplay (v, req, min2.data(), max2.data(), 128);
    const auto rD = disp.compute (v, req, minD.data(), maxD.data(), 128);

    CHECK (r1.kind == r2.kind);
    CHECK (r1.kind == rD.kind);
    CHECK (std::memcmp (min1.data(), min2.data(), 128 * sizeof (float)) == 0);
    CHECK (std::memcmp (max1.data(), max2.data(), 128 * sizeof (float)) == 0);
    CHECK (std::memcmp (min1.data(), minD.data(), 128 * sizeof (float)) == 0);
    CHECK (std::memcmp (max1.data(), maxD.data(), 128 * sizeof (float)) == 0);
}

// 9. Concurrency: running next to an engine thread
TEST (own_osc_display_running_next_to_engine_thread)
{
    MemoryContentStore store;
    store.add ("p", "sine.wav", makeSineWav (440.0, 0.8, 44100, 1.0));
    StateContent content;
    ContentFileRef ref;
    ref.pack = "p";
    ref.file = "sine.wav";
    attachSample (content, 1, ref, 44100, 44100, 1);

    auto engine = createOwnEngine();
    engine->prepare (48000.0, 256, qualityForTier (Tier::high));
    engine->setContentStore (&store);
    engine->setContent (content);

    OscCycle oc;
    oc.setContent (content, &store);

    std::atomic<bool> done { false };
    std::atomic<long> engineBlocks { 0 };

    // Thread 1: audio processing engine loop
    std::thread audioThread ([&]
    {
        std::vector<float> l (256), r (256);
        Event ev[1] = { sbtest::noteOn (0, 60, 0.8f) };
        engine->process (ev, 1, l.data(), r.data(), 256);
        long blocks = 1;
        while (! done.load (std::memory_order_acquire))
        {
            engine->process (nullptr, 0, l.data(), r.data(), 256);
            ++blocks;
            std::this_thread::yield();
        }
        engineBlocks.store (blocks);
    });

    // Thread 2: display computation hammering computeDisplay
    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);
    std::vector<float> minBuf (128), maxBuf (128);

    const int iterations = sbtest::scaled (400, 50);
    for (int i = 0; i < iterations; ++i)
    {
        setv (v, "osc1.type", static_cast<float> (1 + (i % 4))); // rotate through sample types
        OscCycle::Request req;
        req.osc = 1;
        req.note = 36 + (i % 48);
        const auto res = oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 128);
        CHECK (res.kind != OscCycle::Kind::off);
    }

    done.store (true, std::memory_order_release);
    audioThread.join();
    CHECK (engineBlocks.load() > 0);
}

// 10. Speed check: computeDisplay CPU time using sbtest::bestCpuMs
TEST (own_osc_display_speed_check)
{
    MemoryContentStore store;
    store.add ("p", "sine.wav", makeSineWav (440.0, 0.8, 44100, 1.0));
    StateContent content;
    ContentFileRef ref;
    ref.pack = "p";
    ref.file = "sine.wav";
    attachSample (content, 1, ref, 44100, 44100, 1);

    OscCycle oc;
    oc.setContent (content, &store);

    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);

    std::vector<float> minBuf (512), maxBuf (512);
    OscCycle::Request req;
    req.osc = 1;

    for (int t = 0; t <= 3; ++t)
    {
        setv (v, "osc1.type", static_cast<float> (t));
        const double bestMs = sbtest::bestCpuMs (10, [&]
        {
            oc.computeDisplay (v, req, minBuf.data(), maxBuf.data(), 512);
        });
        std::printf ("    osc type %d (512 points): best CPU time %.4f ms\n", t, bestMs);
        if (! sbtest::instrumented())
            CHECK (bestMs < 5.0); // well within interactive 60 Hz frame budget (< 16 ms)
    }
}
