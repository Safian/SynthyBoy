// SynthyBoy - core tests: engine behaviour for every engine in the build (silence, notes, determinism, block independence,
// pitch, stealing). The expectations that differ between the engines are in sbtest::EngineCase (test_engines.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "synthyboy/engine.h"
#include "test_runner.h"
#include "test_engines.h"
#include "test_util.h"

using namespace synthyboy;
using sbtest::EngineCase;
using sbtest::engineCases;
using sbtest::noteOff;
using sbtest::noteOn;

#define FOR_EACH_ENGINE(c) for (const EngineCase& c : engineCases()) if (sbtest::Context context_ (c.name); true)

namespace
{
constexpr double kSampleRate = 48000.0;

std::unique_ptr<IEngine> makeEngine2 (const EngineCase& c, const Quality& q = Quality {}, double sampleRate = kSampleRate)
{
    return sbtest::makeEngine (c, q, sampleRate, 512);
}

bool sameBits (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
}

/** sqrt (sum (a - b)^2 / sum a^2) */
double relativeRms (const std::vector<float>& a, const std::vector<float>& b)
{
    double error = 0.0, energy = 0.0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        const double d = static_cast<double> (a[i]) - static_cast<double> (b[i]);
        error += d * d;
        energy += static_cast<double> (a[i]) * static_cast<double> (a[i]);
    }
    return energy > 0.0 ? std::sqrt (error / energy) : 0.0;
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

TEST (engine_factory)
{
    const char* error = "unset";
    auto own = createEngine (EngineKind::own, &error);
    CHECK (own != nullptr && error == nullptr);
    CHECK (isEngineAvailable (EngineKind::own));
    CHECK (std::strcmp (engineName (EngineKind::own), "own") == 0);
    CHECK (std::strcmp (engineName (EngineKind::vital), "vital") == 0);

    error = nullptr;
    auto vital = createEngine (EngineKind::vital, &error);
    if (isEngineAvailable (EngineKind::vital))
    {
        CHECK (vital != nullptr && error == nullptr);
    }
    else
    {
        CHECK (vital == nullptr);
        CHECK (error != nullptr && std::strlen (error) > 10);
    }
    CHECK (createEngine (EngineKind::vital) == nullptr || isEngineAvailable (EngineKind::vital)); // null error pointer is fine
}

TEST (engine_silence_without_notes)
{
    FOR_EACH_ENGINE (c)
    {
        auto e = makeEngine2 (c);
        const auto out = sbtest::render (*e, {}, 96000, 256);
        CHECK_EQ (sbtest::peak (out.left), 0.0f);
        CHECK_EQ (sbtest::peak (out.right), 0.0f);
        CHECK_EQ (e->activeVoices(), 0);
    }
}

TEST (engine_silence_before_prepare_and_with_null_right)
{
    FOR_EACH_ENGINE (c)
    {
        auto e = createEngine (c.kind);
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
}

TEST (engine_note_on_off_and_tail)
{
    FOR_EACH_ENGINE (c)
    {
        for (const float waveform : { 0.0f, 1.0f })
        {
            auto e = makeEngine2 (c);
            e->setParam (kParamWaveform, waveform);
            const int total = 48000;
            const int offAt = 12000;
            const auto out = sbtest::render (*e, { noteOn (0, 69, 1.0f), noteOff (offAt, 69) }, total, 256);

            CHECK (sbtest::allFinite (out.left));
            const float pk = sbtest::peak (out.left, 0, static_cast<size_t> (offAt));
            CHECK (pk > 0.05f && pk <= 1.0f);
            CHECK (sbtest::peak (out.left) <= 1.0f);
            if (c.stereoIdentical)
                CHECK (out.left == out.right);

            // after the release the output is exactly zero and the voice is gone
            const size_t silentFrom = static_cast<size_t> (offAt + c.tailSeconds * kSampleRate);
            CHECK_EQ (sbtest::peak (out.left, silentFrom), 0.0f);
            CHECK_EQ (e->activeVoices(), 0);
            // and it was still sounding shortly after note off
            CHECK (sbtest::peak (out.left, static_cast<size_t> (offAt), static_cast<size_t> (offAt + 480)) > 0.01f);
        }
    }
}

TEST (engine_all_notes_off_releases)
{
    FOR_EACH_ENGINE (c)
    {
        auto e = makeEngine2 (c);
        const auto out = sbtest::render (*e, { noteOn (0, 60), noteOn (0, 64), noteOn (0, 67, 0.5f, 3), sbtest::allNotesOff (4800) }, 48000, 128);
        CHECK (sbtest::allFinite (out.left));
        CHECK_EQ (sbtest::peak (out.left, static_cast<size_t> (4800 + c.tailSeconds * kSampleRate)), 0.0f);
        CHECK_EQ (e->activeVoices(), 0);

        // channel specific
        auto e2 = makeEngine2 (c);
        sbtest::render (*e2, { noteOn (0, 60, 1.0f, 0), noteOn (0, 64, 1.0f, 1), sbtest::allNotesOff (100, 0) }, 48000, 256);
        CHECK_EQ (e2->activeVoices(), 1);
    }
}

TEST (engine_velocity_scales_level)
{
    FOR_EACH_ENGINE (c)
    {
        auto loud = makeEngine2 (c);
        auto soft = makeEngine2 (c);
        const auto a = sbtest::render (*loud, { noteOn (0, 60, 1.0f) }, 4800, 4800);
        const auto b = sbtest::render (*soft, { noteOn (0, 60, 0.5f) }, 4800, 4800);
        const float pa = sbtest::peak (a.left);
        const float pb = sbtest::peak (b.left);
        const double expected = std::pow (0.5, c.velocityExponent);
        if (! CHECK_NEAR (static_cast<double> (pb / pa), expected, 0.02 * expected))
            std::printf ("    velocity 0.5 gives %.4f of the full velocity peak, expected %.4f\n", static_cast<double> (pb / pa), expected);
    }
}

TEST (engine_determinism_two_instances_bit_identical)
{
    FOR_EACH_ENGINE (c)
    {
        std::vector<Event> ev = { noteOn (0, 60), noteOn (50, 64, 0.7f), sbtest::pitchBend (3000, 0.4f), noteOff (9000, 60),
                                  noteOn (12000, 72, 0.9f), noteOff (20000, 64), noteOff (30000, 72) };
        // the parameters are set before prepare(): prepare() and reset() put the engine on them (a parameter that changes
        // later is smoothed, and the first run would then start in the middle of the ramp)
        auto a = createEngine (c.kind);
        auto b = createEngine (c.kind);
        for (auto* e : { a.get(), b.get() })
        {
            e->setParam (kParamCutoff, 1234.0f);
            e->setParam (kParamGain, -3.0f);
            e->prepare (kSampleRate, 512, Quality {});
        }
        const auto ra = sbtest::render (*a, ev, 60000, 480);
        const auto rb = sbtest::render (*b, ev, 60000, 480);
        CHECK (sameBits (ra.left, rb.left));
        CHECK (sameBits (ra.right, rb.right));
        CHECK (sbtest::peak (ra.left) > 0.01f);

        // reset() returns to the same state: the second run equals the first
        a->reset();
        const auto rc = sbtest::render (*a, ev, 60000, 480);
        if (c.resetBitExact)
        {
            CHECK (sameBits (ra.left, rc.left));
        }
        else
        {
            // Vital's voices keep tiny filter residues from earlier notes (not cleared at note start), and its
            // smoothers only approach their targets: the second run is the same sound, not the same bits
            const double relative = relativeRms (ra.left, rc.left);
            if (! CHECK (relative <= c.resetTolerance))
                std::printf ("    relative RMS difference after reset(): %g\n", relative);
            else
                std::printf ("    [%s] relative RMS difference after reset(): %.3g\n", c.name, relative);
        }
    }
}

TEST (engine_block_size_independence)
{
    // events fall in the middle of blocks of every size
    const std::vector<Event> plain = {
        noteOn (0, 60), noteOn (100, 64, 0.8f), noteOn (1000, 67, 0.6f, 2), noteOff (1500, 60), noteOff (2500, 64), noteOn (3000, 72),
        sbtest::allNotesOff (3900, 2), noteOff (4000, 72)
    };
    std::vector<Event> bent = plain;
    bent.push_back (sbtest::pitchBend (777, 0.5f));
    bent.push_back (sbtest::pitchBend (1100, -0.25f, 2));
    std::stable_sort (bent.begin(), bent.end(), [] (const Event& a, const Event& b) { return a.frame < b.frame; });

    FOR_EACH_ENGINE (c)
    {
        for (const bool withBends : { false, true })
        {
            const auto& ev = withBends ? bent : plain;
            for (const float waveform : { 0.0f, 1.0f })
            {
                auto reference = makeEngine2 (c);
                reference->setParam (kParamWaveform, waveform);
                const auto ref = sbtest::render (*reference, ev, 4096, 4096);
                CHECK (sbtest::peak (ref.left) > 0.01f);

                for (const int block : { 1, 7, 64, 512 })
                {
                    auto e = makeEngine2 (c);
                    e->setParam (kParamWaveform, waveform);
                    const auto out = sbtest::render (*e, ev, 4096, block);
                    CHECK (sbtest::allFinite (out.left));
                    const double tolerance = withBends ? (waveform == 0.0f ? c.bentBlockTolerance : -1.0) : c.blockTolerance;
                    if (tolerance < 0.0)
                    {
                        // Vital moves a bend over the processed chunk (control rate), and the chunk boundaries depend on
                        // the host block size: the pitch glide differs by a few samples. The saw's edge turns any shift
                        // into a large RMS error, so for the saw only the level is compared.
                        CHECK (sbtest::peak (out.left) > 0.5f * sbtest::peak (ref.left) && sbtest::peak (out.left) < 2.0f * sbtest::peak (ref.left));
                    }
                    else if (tolerance == 0.0)
                    {
                        if (! CHECK (sameBits (ref.left, out.left) && sameBits (ref.right, out.right)))
                            std::printf ("    waveform %g block size %d differs\n", static_cast<double> (waveform), block);
                    }
                    else
                    {
                        const double relative = relativeRms (ref.left, out.left);
                        if (! CHECK (relative <= tolerance))
                            std::printf ("    %s, waveform %g block size %d: relative RMS difference %g\n", withBends ? "bent" : "plain",
                                         static_cast<double> (waveform), block, relative);
                        else if (block == 1 || block == 64)
                            std::printf ("    [%s] %s, waveform %g block size %d: relative RMS difference %.3g\n", c.name, withBends ? "bent" : "plain",
                                         static_cast<double> (waveform), block, relative);
                    }
                }
            }
        }
    }
}

TEST (engine_pitch)
{
    struct Case { float waveform; int key; float bend; double hz; };
    const Case cases[] = {
        { 1.0f, 69, 0.0f, 440.0 }, { 0.0f, 69, 0.0f, 440.0 }, { 1.0f, 57, 0.0f, 220.0 }, { 0.0f, 81, 0.0f, 880.0 },
        { 0.0f, 57, 0.0f, 220.0 }, { 1.0f, 81, 0.0f, 880.0 },
        { 1.0f, 60, 0.0f, 261.6255653 }, { 1.0f, 69, 1.0f, 440.0 * std::pow (2.0, 2.0 / 12.0) },
        { 0.0f, 69, -1.0f, 440.0 * std::pow (2.0, -2.0 / 12.0) },
    };
    FOR_EACH_ENGINE (c)
    {
        for (const auto& k : cases)
        {
            auto e = makeEngine2 (c);
            e->setParam (kParamWaveform, k.waveform);
            e->setParam (kParamCutoff, 12000.0f);
            std::vector<Event> ev = { sbtest::pitchBend (0, k.bend), noteOn (0, k.key) };
            const auto out = sbtest::render (*e, ev, 48000, 512);
            const double hz = measureFrequency (out.left, 12000, 48000, kSampleRate);
            const double tolerance = c.pitchInCents ? k.hz * (std::pow (2.0, c.pitchTolerance / 1200.0) - 1.0) : c.pitchTolerance;
            if (! CHECK_NEAR (hz, k.hz, tolerance))
                std::printf ("    waveform %g key %d bend %g: measured %.4f Hz, expected %.4f Hz (+-%.4f)\n", static_cast<double> (k.waveform), k.key,
                             static_cast<double> (k.bend), hz, k.hz, tolerance);
        }
    }
}

TEST (engine_pitch_at_other_sample_rate)
{
    FOR_EACH_ENGINE (c)
    {
        auto e = makeEngine2 (c, Quality {}, 44100.0);
        e->setParam (kParamWaveform, 0.0f);
        const auto out = sbtest::render (*e, { noteOn (0, 69) }, 44100, 441);
        CHECK_NEAR (measureFrequency (out.left, 11000, 44100, 44100.0), 440.0, 0.5);
    }
}

TEST (engine_pitch_at_many_sample_rates)
{
    FOR_EACH_ENGINE (c)
    {
        for (const double sr : { 22050.0, 32000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto e = makeEngine2 (c, Quality {}, sr);
            e->setParam (kParamWaveform, 0.0f);
            e->setParam (kParamCutoff, 10000.0f);
            const int total = static_cast<int> (sr);
            const auto out = sbtest::render (*e, { noteOn (0, 69) }, total, 480);
            CHECK (sbtest::allFinite (out.left) && sbtest::peak (out.left) > 0.02f);
            const double hz = measureFrequency (out.left, static_cast<size_t> (total / 4), static_cast<size_t> (total), sr);
            const double tolerance = c.pitchInCents ? 440.0 * (std::pow (2.0, c.pitchTolerance / 1200.0) - 1.0) : 0.5;
            if (! CHECK_NEAR (hz, 440.0, tolerance))
                std::printf ("    sample rate %g: measured %.4f Hz\n", sr, hz);
        }
    }
}

TEST (engine_cutoff_and_gain_have_effect)
{
    FOR_EACH_ENGINE (c)
    {
        auto dark = makeEngine2 (c);
        auto bright = makeEngine2 (c);
        dark->setParam (kParamCutoff, 100.0f);
        bright->setParam (kParamCutoff, 18000.0f);
        const auto d = sbtest::render (*dark, { noteOn (0, 69) }, 24000, 512);
        const auto b = sbtest::render (*bright, { noteOn (0, 69) }, 24000, 512);
        CHECK (sbtest::peak (d.left, 12000) < 0.5f * sbtest::peak (b.left, 12000));

        auto quiet = makeEngine2 (c);
        auto loud = makeEngine2 (c);
        quiet->setParam (kParamGain, -30.0f);
        loud->setParam (kParamGain, 0.0f);
        const auto q = sbtest::render (*quiet, { noteOn (0, 69) }, 24000, 512);
        const auto l = sbtest::render (*loud, { noteOn (0, 69) }, 24000, 512);
        CHECK_NEAR (sbtest::peak (l.left, 12000) / sbtest::peak (q.left, 12000), std::pow (10.0, 30.0 / 20.0), 0.05 * std::pow (10.0, 30.0 / 20.0));
    }
}

TEST (engine_gain_changes_are_smoothed)
{
    FOR_EACH_ENGINE (c)
    {
        auto e = makeEngine2 (c);
        e->setParam (kParamWaveform, 0.0f);
        e->setParam (kParamGain, -60.0f);
        sbtest::render (*e, { noteOn (0, 69) }, 4800, 512);
        e->setParam (kParamGain, 6.0f);
        const auto out = sbtest::render (*e, {}, 4800, 4800);
        // a ramp, not a jump: the first sample is far below the final level, the end of the block is at full level
        const float final = sbtest::peak (out.left, 2000, 4800);
        CHECK (final > 0.1f);
        CHECK (std::fabs (out.left[0]) < 0.05f * final);
        if (c.gainRampFrames >= 512) // the own engine's 20 ms ramp is slow enough to see in the first samples
            CHECK (sbtest::peak (out.left, 0, 20) < 0.02f);
        CHECK (sbtest::allFinite (out.left));
    }
}

TEST (engine_params)
{
    FOR_EACH_ENGINE (c)
    {
        auto e = createEngine (c.kind);
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
}

TEST (engine_voice_stealing_and_limits)
{
    FOR_EACH_ENGINE (c)
    {
        for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
        {
            const Quality q = qualityForTier (tier);
            auto e = makeEngine2 (c, q);
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
            if (c.voicePoolExact)
            {
                CHECK_EQ (maxActive, q.maxVoices); // the pool is used completely, never exceeded
                CHECK (e->activeVoices() <= q.maxVoices);
            }
            else
            {
                // Vital keeps a stolen voice alive for a few milliseconds (fade-out) next to its replacement, so more
                // voices than the limit are counted while notes arrive faster than they fade (its own pool is 32 + 2)
                CHECK (maxActive >= q.maxVoices && maxActive <= kMaxVoices + 4);
                std::printf ("    [%s] tier %d: voice limit %d, most voices counted %d\n", c.name, static_cast<int> (tier), q.maxVoices, maxActive);
            }
        }
    }
}

TEST (engine_stealing_takes_the_oldest_voice)
{
    FOR_EACH_ENGINE (c)
    {
        Quality q;
        q.maxVoices = 2;
        auto e = makeEngine2 (c, q);
        // keys 60, 64, then 67 steals 60: releasing 60 afterwards must not stop anything, releasing 64 and 67 does
        sbtest::render (*e, { noteOn (0, 60), noteOn (0, 64), noteOn (0, 67), noteOff (100, 60) }, 2000, 2000);
        CHECK_EQ (e->activeVoices(), 2);
        const auto out = sbtest::render (*e, { noteOff (0, 64), noteOff (0, 67) }, 48000, 512);
        CHECK_EQ (e->activeVoices(), 0);
        CHECK_EQ (sbtest::peak (out.left, 20000), 0.0f);
    }
}

TEST (engine_retrigger_same_key)
{
    FOR_EACH_ENGINE (c)
    {
        auto e = makeEngine2 (c);
        const auto out = sbtest::render (*e, { noteOn (0, 60), noteOn (2400, 60), noteOn (4800, 60) }, 9600, 480);
        CHECK (sbtest::allFinite (out.left));
        if (c.voicePoolExact)
            CHECK_EQ (e->activeVoices(), 3); // the earlier ones fade out (release), not cut
        else
            CHECK (e->activeVoices() >= 1 && e->activeVoices() <= 3);
        const auto tail = sbtest::render (*e, { noteOff (0, 60) }, 48000, 480);
        CHECK_EQ (e->activeVoices(), 0);
        CHECK_EQ (sbtest::peak (tail.left, 20000), 0.0f);
    }
}

TEST (engine_tolerates_odd_events)
{
    FOR_EACH_ENGINE (c)
    {
        auto e = makeEngine2 (c);
        std::vector<Event> ev = { noteOn (50, 60), noteOn (10, 64) /* unsorted */, noteOn (-5, 65), noteOn (999999, 66), noteOn (0, -1), noteOn (0, 200),
                                  noteOn (0, 60, std::nanf ("")), noteOn (0, 60, 5.0f), noteOn (0, 61, 1.0f, 99), noteOn (0, 61, 1.0f, -3),
                                  noteOff (0, 70), noteOff (0, 61, 99), sbtest::pitchBend (0, std::nanf ("")), sbtest::pitchBend (0, 100.0f, 77) };
        std::vector<float> l (256), r (256);
        for (int i = 0; i < 100; ++i)
            e->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 256);
        CHECK (sbtest::allFinite (l));
        CHECK (sbtest::allFinite (r));
    }
}

TEST (engine_quality_defaults)
{
    CHECK_EQ (Quality {}.maxVoices, 8);
    CHECK_EQ (qualityForTier (Tier::low).maxVoices, 8);
    CHECK_EQ (qualityForTier (Tier::medium).maxVoices, 16);
    CHECK_EQ (qualityForTier (Tier::high).maxVoices, kMaxVoices);

    FOR_EACH_ENGINE (c)
    {
        // out-of-range requests are capped
        Quality big;
        big.maxVoices = 1000;
        auto e = makeEngine2 (c, big);
        std::vector<Event> burst;
        for (int i = 0; i < 100; ++i)
            burst.push_back (noteOn (0, 30 + i % 60));
        sbtest::render (*e, burst, 512, 512);
        if (c.voicePoolExact)
            CHECK_EQ (e->activeVoices(), kMaxVoices);
        else
            CHECK (e->activeVoices() >= kMaxVoices && e->activeVoices() <= 2 * kMaxVoices);
        Quality zero;
        zero.maxVoices = 0;
        auto z = makeEngine2 (c, zero);
        sbtest::render (*z, burst, 512, 512);
        if (c.voicePoolExact)
            CHECK_EQ (z->activeVoices(), 1);
        else
            CHECK (z->activeVoices() >= 1 && z->activeVoices() <= 3);
    }
}
