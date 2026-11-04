// SynthyBoyVST - core tests: own engine behaviour (silence, notes, determinism, block independence, pitch, stealing).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "synthyboy/engine.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;

namespace
{
constexpr double kSampleRate = 48000.0;

std::unique_ptr<IEngine> makeEngine (const Quality& q = Quality {}, double sampleRate = kSampleRate)
{
    auto e = createOwnEngine();
    e->prepare (sampleRate, 512, q);
    return e;
}

bool sameBits (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
}

/** Frequency from rising zero crossings (linear interpolation) in [from, to). */
double measureFrequency (const std::vector<float>& x, size_t from, size_t to, double sampleRate)
{
    int count = 0;
    double first = 0.0, last = 0.0;
    for (size_t i = from + 1; i < to && i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1) + static_cast<double> (-x[i - 1]) / static_cast<double> (x[i] - x[i - 1]);
            if (count++ == 0)
                first = t;
            last = t;
        }
    return count < 2 ? 0.0 : (count - 1) * sampleRate / (last - first);
}
} // namespace

TEST (engine_silence_without_notes)
{
    auto e = makeEngine();
    const auto out = sbtest::render (*e, {}, 96000, 256);
    CHECK_EQ (sbtest::peak (out.left), 0.0f);
    CHECK_EQ (sbtest::peak (out.right), 0.0f);
    CHECK_EQ (e->activeVoices(), 0);
}

TEST (engine_silence_before_prepare_and_with_null_right)
{
    auto e = createOwnEngine();
    std::vector<float> l (64, 1.0f), r (64, 1.0f);
    const Event on = noteOn (0, 60);
    e->process (&on, 1, l.data(), r.data(), 64);
    CHECK_EQ (sbtest::peak (l), 0.0f);
    CHECK_EQ (sbtest::peak (r), 0.0f);
    CHECK_EQ (e->activeVoices(), 0);

    e->prepare (kSampleRate, 512, Quality {});
    std::vector<float> mono (512, 5.0f);
    e->process (&on, 1, mono.data(), nullptr, 512);
    CHECK (sbtest::peak (mono) > 0.0f && sbtest::allFinite (mono));
    e->process (nullptr, 0, nullptr, nullptr, 512); // must not crash
    e->process (nullptr, 0, mono.data(), nullptr, 0);
    e->process (nullptr, 0, mono.data(), nullptr, -5);
}

TEST (engine_note_on_off_and_tail)
{
    for (const float waveform : { 0.0f, 1.0f })
    {
        auto e = makeEngine();
        e->setParam (kParamWaveform, waveform);
        const int total = 48000;
        const int offAt = 12000;
        const auto out = sbtest::render (*e, { noteOn (0, 69, 1.0f), noteOff (offAt, 69) }, total, 256);

        CHECK (sbtest::allFinite (out.left));
        const float pk = sbtest::peak (out.left, 0, static_cast<size_t> (offAt));
        CHECK (pk > 0.05f && pk <= 1.0f);
        CHECK (sbtest::peak (out.left) <= 1.0f);
        CHECK (out.left == out.right);

        // release is 250 ms: after that the output is exactly zero and the voice is gone
        const size_t silentFrom = static_cast<size_t> (offAt + 0.26 * kSampleRate);
        CHECK_EQ (sbtest::peak (out.left, silentFrom), 0.0f);
        CHECK_EQ (e->activeVoices(), 0);
        // and it was still sounding shortly after note off
        CHECK (sbtest::peak (out.left, static_cast<size_t> (offAt), static_cast<size_t> (offAt + 480)) > 0.01f);
    }
}

TEST (engine_all_notes_off_releases)
{
    auto e = makeEngine();
    const auto out = sbtest::render (*e, { noteOn (0, 60), noteOn (0, 64), noteOn (0, 67, 0.5f, 3), sbtest::allNotesOff (4800) }, 48000, 128);
    CHECK (sbtest::allFinite (out.left));
    CHECK_EQ (sbtest::peak (out.left, static_cast<size_t> (4800 + 0.26 * kSampleRate)), 0.0f);
    CHECK_EQ (e->activeVoices(), 0);

    // channel specific
    auto e2 = makeEngine();
    sbtest::render (*e2, { noteOn (0, 60, 1.0f, 0), noteOn (0, 64, 1.0f, 1), sbtest::allNotesOff (100, 0) }, 48000, 256);
    CHECK_EQ (e2->activeVoices(), 1);
}

TEST (engine_velocity_scales_level)
{
    auto loud = makeEngine();
    auto soft = makeEngine();
    const auto a = sbtest::render (*loud, { noteOn (0, 60, 1.0f) }, 4800, 4800);
    const auto b = sbtest::render (*soft, { noteOn (0, 60, 0.5f) }, 4800, 4800);
    CHECK_NEAR (sbtest::peak (b.left) * 2.0f, sbtest::peak (a.left), 1e-6);
}

TEST (engine_determinism_two_instances_bit_identical)
{
    std::vector<Event> ev = { noteOn (0, 60), noteOn (50, 64, 0.7f), sbtest::pitchBend (3000, 0.4f), noteOff (9000, 60),
                              noteOn (12000, 72, 0.9f), noteOff (20000, 64), noteOff (30000, 72) };
    auto a = makeEngine();
    auto b = makeEngine();
    a->setParam (kParamCutoff, 1234.0f);
    b->setParam (kParamCutoff, 1234.0f);
    const auto ra = sbtest::render (*a, ev, 60000, 480);
    const auto rb = sbtest::render (*b, ev, 60000, 480);
    CHECK (sameBits (ra.left, rb.left));
    CHECK (sameBits (ra.right, rb.right));
    CHECK (sbtest::peak (ra.left) > 0.01f);

    // reset() returns to the same state: the second run equals the first
    a->reset();
    const auto rc = sbtest::render (*a, ev, 60000, 480);
    CHECK (sameBits (ra.left, rc.left));
}

TEST (engine_block_size_independence)
{
    // events fall in the middle of blocks of every size
    const std::vector<Event> ev = {
        noteOn (0, 60), noteOn (100, 64, 0.8f), sbtest::pitchBend (777, 0.5f), noteOn (1000, 67, 0.6f, 2),
        sbtest::pitchBend (1100, -0.25f, 2), noteOff (1500, 60), noteOff (2500, 64), noteOn (3000, 72), sbtest::allNotesOff (3900, 2),
        noteOff (4000, 72)
    };
    for (const float waveform : { 0.0f, 1.0f })
    {
        auto reference = makeEngine();
        reference->setParam (kParamWaveform, waveform);
        const auto ref = sbtest::render (*reference, ev, 4096, 4096);
        CHECK (sbtest::peak (ref.left) > 0.01f);

        for (const int block : { 1, 7, 64, 512 })
        {
            auto e = makeEngine();
            e->setParam (kParamWaveform, waveform);
            const auto out = sbtest::render (*e, ev, 4096, block);
            if (! CHECK (sameBits (ref.left, out.left) && sameBits (ref.right, out.right)))
                std::printf ("    waveform %g block size %d differs\n", static_cast<double> (waveform), block);
        }
    }
}

TEST (engine_pitch)
{
    struct Case { float waveform; int key; float bend; double hz; };
    const Case cases[] = {
        { 1.0f, 69, 0.0f, 440.0 }, { 0.0f, 69, 0.0f, 440.0 }, { 1.0f, 57, 0.0f, 220.0 }, { 0.0f, 81, 0.0f, 880.0 },
        { 1.0f, 60, 0.0f, 261.6255653 }, { 1.0f, 69, 1.0f, 440.0 * std::pow (2.0, 2.0 / 12.0) },
        { 0.0f, 69, -1.0f, 440.0 * std::pow (2.0, -2.0 / 12.0) },
    };
    for (const auto& c : cases)
    {
        auto e = makeEngine();
        e->setParam (kParamWaveform, c.waveform);
        e->setParam (kParamCutoff, 12000.0f);
        std::vector<Event> ev = { sbtest::pitchBend (0, c.bend), noteOn (0, c.key) };
        const auto out = sbtest::render (*e, ev, 48000, 512);
        const double hz = measureFrequency (out.left, 12000, 48000, kSampleRate);
        if (! CHECK_NEAR (hz, c.hz, 0.5))
            std::printf ("    waveform %g key %d bend %g: measured %.4f Hz, expected %.4f Hz\n", static_cast<double> (c.waveform), c.key,
                         static_cast<double> (c.bend), hz, c.hz);
    }
}

TEST (engine_pitch_at_other_sample_rate)
{
    auto e = makeEngine (Quality {}, 44100.0);
    e->setParam (kParamWaveform, 0.0f);
    const auto out = sbtest::render (*e, { noteOn (0, 69) }, 44100, 441);
    CHECK_NEAR (measureFrequency (out.left, 11000, 44100, 44100.0), 440.0, 0.5);
}

TEST (engine_cutoff_and_gain_have_effect)
{
    auto dark = makeEngine();
    auto bright = makeEngine();
    dark->setParam (kParamCutoff, 100.0f);
    bright->setParam (kParamCutoff, 18000.0f);
    const auto d = sbtest::render (*dark, { noteOn (0, 69) }, 24000, 512);
    const auto b = sbtest::render (*bright, { noteOn (0, 69) }, 24000, 512);
    CHECK (sbtest::peak (d.left, 12000) < 0.5f * sbtest::peak (b.left, 12000));

    auto quiet = makeEngine();
    auto loud = makeEngine();
    quiet->setParam (kParamGain, -30.0f);
    loud->setParam (kParamGain, 0.0f);
    const auto q = sbtest::render (*quiet, { noteOn (0, 69) }, 24000, 512);
    const auto l = sbtest::render (*loud, { noteOn (0, 69) }, 24000, 512);
    CHECK_NEAR (sbtest::peak (l.left, 12000) / sbtest::peak (q.left, 12000), std::pow (10.0, 30.0 / 20.0), 0.05 * std::pow (10.0, 30.0 / 20.0));
}

TEST (engine_gain_changes_are_smoothed)
{
    auto e = makeEngine();
    e->setParam (kParamWaveform, 0.0f);
    e->setParam (kParamGain, -60.0f);
    sbtest::render (*e, { noteOn (0, 69) }, 4800, 512);
    e->setParam (kParamGain, 6.0f);
    const auto out = sbtest::render (*e, {}, 4800, 4800);
    // the ramp takes 20 ms (960 samples): the first samples are still quiet, the end of the block is at full level
    CHECK (sbtest::peak (out.left, 0, 20) < 0.02f);
    CHECK (sbtest::peak (out.left, 2000, 4800) > 0.1f);
    CHECK (sbtest::allFinite (out.left));
}

TEST (engine_params)
{
    auto e = createOwnEngine();
    CHECK_EQ (e->getParam (kParamGain), -12.0f);
    CHECK_EQ (e->getParam (kParamWaveform), 1.0f);
    CHECK_EQ (e->getParam (kParamCutoff), 8000.0f);
    e->setParam (kParamCutoff, 1.0e9f);
    CHECK_EQ (e->getParam (kParamCutoff), 20000.0f);
    e->setParam (kParamWaveform, 0.2f);
    CHECK_EQ (e->getParam (kParamWaveform), 0.0f);
    e->setParam (kParamGain, std::nanf (""));
    CHECK (std::isfinite (e->getParam (kParamGain)));
    e->setParam (kParamGain, -3.0f);
    e->setParam (kParamGain, std::nanf (""));
    CHECK_EQ (e->getParam (kParamGain), -3.0f); // NaN ignored
    e->setParam (12345u, 1.0f);                 // unknown id ignored
    CHECK_EQ (e->getParam (12345u), 0.0f);

    // parameters survive prepare()
    e->prepare (kSampleRate, 512, Quality {});
    CHECK_EQ (e->getParam (kParamGain), -3.0f);
}

TEST (engine_voice_stealing_and_limits)
{
    for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
    {
        const Quality q = qualityForTier (tier);
        auto e = makeEngine (q);
        CHECK (q.maxVoices >= 1 && q.maxVoices <= kMaxVoices);

        sbtest::Rng rng (5);
        std::vector<Event> ev;
        for (int i = 0; i < 400; ++i)
        {
            const int frame = i * 40;
            ev.push_back (noteOn (frame, 24 + static_cast<int> (rng.u32() % 80), 0.2f + 0.8f * static_cast<float> (rng.uniform()),
                                  static_cast<int> (rng.u32() % 16)));
            if (i % 5 == 0)
                ev.push_back (sbtest::pitchBend (frame + 1, static_cast<float> (rng.uniform() * 2.0 - 1.0), static_cast<int> (rng.u32() % 16)));
            if (i % 7 == 0)
                ev.push_back (noteOff (frame + 2, 24 + static_cast<int> (rng.u32() % 80), static_cast<int> (rng.u32() % 16)));
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b) { return a.frame < b.frame; });

        // all voices at once, then the stream
        std::vector<Event> burst;
        for (int i = 0; i < 3 * q.maxVoices; ++i)
            burst.push_back (noteOn (0, 40 + i % 40));
        burst.insert (burst.end(), ev.begin(), ev.end());

        std::vector<float> l (512), r (512);
        int maxActive = 0;
        size_t idx = 0;
        bool finite = true;
        for (int start = 0; start < 16000 + 512; start += 512)
        {
            std::vector<Event> local;
            while (idx < burst.size() && burst[idx].frame < start + 512)
            {
                auto x = burst[idx++];
                x.frame -= start;
                local.push_back (x);
            }
            e->process (local.data(), static_cast<int> (local.size()), l.data(), r.data(), 512);
            for (float v : l)
                finite = finite && std::isfinite (v) && std::fabs (v) < 20.0f;
            maxActive = std::max (maxActive, e->activeVoices());
        }
        CHECK (finite);
        CHECK_EQ (maxActive, q.maxVoices); // the pool is used completely, never exceeded
        CHECK (e->activeVoices() <= q.maxVoices);
    }
}

TEST (engine_stealing_takes_the_oldest_voice)
{
    Quality q;
    q.maxVoices = 2;
    auto e = makeEngine (q);
    // keys 60, 64, then 67 steals 60: releasing 60 afterwards must not stop anything, releasing 64 and 67 does
    sbtest::render (*e, { noteOn (0, 60), noteOn (0, 64), noteOn (0, 67), noteOff (100, 60) }, 2000, 2000);
    CHECK_EQ (e->activeVoices(), 2);
    const auto out = sbtest::render (*e, { noteOff (0, 64), noteOff (0, 67) }, 48000, 512);
    CHECK_EQ (e->activeVoices(), 0);
    CHECK_EQ (sbtest::peak (out.left, 20000), 0.0f);
}

TEST (engine_retrigger_same_key)
{
    auto e = makeEngine();
    const auto out = sbtest::render (*e, { noteOn (0, 60), noteOn (2400, 60), noteOn (4800, 60) }, 9600, 480);
    CHECK (sbtest::allFinite (out.left));
    CHECK_EQ (e->activeVoices(), 3); // the earlier ones fade out (release 250 ms), not cut
    const auto tail = sbtest::render (*e, { noteOff (0, 60) }, 48000, 480);
    CHECK_EQ (e->activeVoices(), 0);
    CHECK_EQ (sbtest::peak (tail.left, 20000), 0.0f);
}

TEST (engine_tolerates_odd_events)
{
    auto e = makeEngine();
    std::vector<Event> ev = { noteOn (50, 60), noteOn (10, 64) /* unsorted */, noteOn (-5, 65), noteOn (999999, 66), noteOn (0, -1), noteOn (0, 200),
                              noteOn (0, 60, std::nanf ("")), noteOn (0, 60, 5.0f), noteOn (0, 61, 1.0f, 99), noteOn (0, 61, 1.0f, -3),
                              noteOff (0, 70), noteOff (0, 61, 99), sbtest::pitchBend (0, std::nanf ("")), sbtest::pitchBend (0, 100.0f, 77) };
    std::vector<float> l (256), r (256);
    for (int i = 0; i < 100; ++i)
        e->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 256);
    CHECK (sbtest::allFinite (l));
}

TEST (engine_quality_defaults)
{
    CHECK_EQ (Quality {}.maxVoices, 8);
    CHECK_EQ (qualityForTier (Tier::low).maxVoices, 8);
    CHECK_EQ (qualityForTier (Tier::medium).maxVoices, 16);
    CHECK_EQ (qualityForTier (Tier::high).maxVoices, kMaxVoices);

    // out-of-range requests are capped
    Quality big;
    big.maxVoices = 1000;
    auto e = makeEngine (big);
    std::vector<Event> burst;
    for (int i = 0; i < 100; ++i)
        burst.push_back (noteOn (0, 30 + i % 60));
    sbtest::render (*e, burst, 512, 512);
    CHECK_EQ (e->activeVoices(), kMaxVoices);
    Quality zero;
    zero.maxVoices = 0;
    auto z = makeEngine (zero);
    sbtest::render (*z, burst, 512, 512);
    CHECK_EQ (z->activeVoices(), 1);
}
