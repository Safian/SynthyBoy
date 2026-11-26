// SynthyBoy - own engine tests: the vector form of the sample oscillator block kernel (core/sample-osc/src/sample_voice.cpp: the plain read and the loop crossfade, four samples at a time
// with NEON, SampleVoice::setVectorKernels) gives, sample by sample, the bits of its scalar form, for every quality tier, loop mode, direction, pitch (up and down), crossfade length,
// mono and stereo file, block length and for the add form. Where the NEON form is not built (x86) both runs are the scalar form and the test only pins the harness.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Run on 32 bit and 64 bit ARM (qemu-user in the cross builds) with the flush-to-zero mode of the MPC shell set (the NEON of 32 bit ARM always flushes subnormals; the sample data and the
// products here stay normal numbers, so the mode only matters for the harness), and on AArch64 in the default mode as well.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "fp_flush_mode.h"
#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/sampleosc/sample_types.h"
#include "synthyboy/sampleosc/sample_voice.h"
#include "test_runner.h"

using namespace synthyboy::sampleosc;

namespace
{
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 3u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return 2.0f * uni() - 1.0f; }
};

bool sameBits (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}
} // namespace

TEST (sample_voice_vector_kernels_equal_the_scalar_kernels_for_every_tier_loop_and_pitch)
{
    const int frames = 30000;
    std::vector<float> l (static_cast<size_t> (frames)), r (l.size());
    Rng rng (7);
    for (int i = 0; i < frames; ++i)
    {
        const double t = static_cast<double> (i) / 44100.0;
        l[static_cast<size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 220.0 * t) + 0.5 * std::sin (2.0 * 3.14159265358979 * 3100.0 * t)) + 0.05f * rng.bi();
        r[static_cast<size_t> (i)] = 0.4f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 330.0 * t + 1.0)) + 0.1f * rng.bi();
    }
    SampleData mono, stereo;
    CHECK (mono.loadFromFrames (l.data(), nullptr, frames, 44100.0));
    CHECK (stereo.loadFromFrames (l.data(), r.data(), frames, 44100.0));

    long long blocks = 0, bad = 0;
    int modeNo = 0;
#if defined(__arm__) || defined(__aarch64__)
    const bool modes[2] = { false, true };
#else
    const bool modes[2] = { false, false };
#endif
    for (int mi = 0; mi < 2; ++mi)
    {
#if defined(__arm__)
        if (! modes[mi])
            continue;   // 32 bit ARM: only the mode of the shell
#endif
        sbtest::FlushMode fm (modes[mi]);
        ++modeNo;
        for (QualityTier tier : { QualityTier::low, QualityTier::medium, QualityTier::high })
            for (LoopMode loop : { LoopMode::none, LoopMode::forward, LoopMode::pingPong, LoopMode::reverse, LoopMode::tailed })
                for (float xfade : { 0.0f, 0.3f })
                    for (float semis : { 0.0f, 7.0f, -12.0f, 19.0f, -26.5f })
                        for (int variant = 0; variant < 4; ++variant)
                        {
                            const SampleView view = variant % 2 == 0 ? mono.view() : stereo.view();
                            SampleOscParams p;
                            p.quality = tier;
                            p.loopMode = loop;
                            p.loopStart = 0.15f;
                            p.loopEnd = 0.8f;
                            p.loopCrossfade = xfade;
                            p.reverse = variant == 3;
                            p.startOffset = variant == 1 ? 0.1f : 0.0f;
                            p.level = 0.9f;
                            p.pan = variant == 2 ? -0.3f : 0.2f;
                            SampleVoice vec, sca;
                            for (SampleVoice* v : { &vec, &sca })
                                v->prepare (44100.0, 512);
                            sca.setVectorKernels (false);
                            for (SampleVoice* v : { &vec, &sca })
                            {
                                v->noteOn (60, 0.8f, &view, p);
                                v->setSemitoneOffset (semis);
                            }
                            bool differ = false;
                            for (int block = 0; block < 9 && ! differ; ++block)
                            {
                                const int n = block % 3 == 0 ? 128 : (block % 3 == 1 ? 77 : 256);   // (odd lengths: the vector runs of four and the tail)
                                std::vector<float> a (static_cast<size_t> (n), 0.25f), b (a), c (a), d (a);
                                if (block % 2 == 0)
                                {
                                    vec.process (a.data(), c.data(), n);
                                    sca.process (b.data(), d.data(), n);
                                }
                                else
                                {
                                    vec.processAdd (a.data(), c.data(), n);
                                    sca.processAdd (b.data(), d.data(), n);
                                }
                                ++blocks;
                                differ = ! (sameBits (a, b) && sameBits (c, d));
                                if (block == 4)
                                {
                                    vec.noteOff();
                                    sca.noteOff();
                                }
                            }
                            bad += differ ? 1 : 0;
                        }
    }
    std::printf ("    %lld blocks in %d float mode(s) (%s): %lld voices differ between the vector and the scalar kernel\n", blocks, modeNo,
#if defined(__ARM_NEON)
                 "NEON form built",
#else
                 "no NEON form in this build",
#endif
                 bad);
    CHECK_EQ (bad, 0);
}
