// SynthyBoy - own engine tests: the content policy of docs/ARCHITECTURE.md section 5 (a missing file is tolerated and flagged: the oscillator plays its built-in wavetable, never silence),
// and the loop of a WAV 'smpl' chunk becoming loop settings at load time (SampleLoopSettings, used by the shells' load paths).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/user_content.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

sbtest::Rendered play (float type, float wavetable, unsigned* missing)
{
    auto e = createOwnEngine();
    e->prepare (48000.0, 256, qualityForTier (Tier::high));
    set (*e, "filter1.on", 0);
    set (*e, "master.volume", 0);
    set (*e, "osc1.level", 100);
    set (*e, "osc1.random_phase", 0);
    set (*e, "env1.attack", 1);
    set (*e, "osc1.wavetable", wavetable);
    set (*e, "osc1.type", type);
    const auto r = sbtest::render (*e, { sbtest::noteOn (0, 60) }, 24000, 256);
    if (missing != nullptr)
        *missing = e->missingContent();
    return r;
}

void put32 (std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; ++i)
        v.push_back (static_cast<uint8_t> ((x >> (8 * i)) & 0xff));
}
} // namespace

TEST (own_missing_file_plays_the_builtin_wavetable_and_is_flagged)
{
    unsigned none = 0;
    const auto reference = play (0.0f, 2.0f, &none);
    CHECK_EQ (none, 0u);
    for (float type : { 1.0f, 2.0f, 3.0f, 4.0f }) // Sample, Granular, Spectral, Multisample, with an empty store
    {
        unsigned missing = 0;
        const auto r = play (type, 2.0f, &missing);
        CHECK (sbtest::peak (r.left) > 0.05f);            // it sounds (it was -180 dBFS)
        CHECK_EQ (missing & 1u, 1u);                      // and the instance can say so
        CHECK (r.left == reference.left && r.right == reference.right); // exactly what the built-in wavetable of the oscillator plays
    }
    unsigned table = 0;
    (void) play (0.0f, 13.0f, &table); // Preset table without its content: the saw plays, flagged
    CHECK_EQ (table & 8u, 8u);
}

TEST (own_wav_smpl_loop_becomes_loop_settings)
{
    std::vector<float> x (1000, 0.25f);
    std::vector<uint8_t> wav = encodeWav (x.data(), nullptr, x.size(), 44100, 16);
    // append a 'smpl' chunk: root 62, one forward loop 100 .. 599, and fix the RIFF size
    std::vector<uint8_t> chunk;
    chunk.insert (chunk.end(), { 's', 'm', 'p', 'l' });
    put32 (chunk, 36 + 24);
    for (uint32_t w : { 0u, 0u, 22675u, 62u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 100u, 599u, 0u, 0u })
        put32 (chunk, w);
    wav.insert (wav.end(), chunk.begin(), chunk.end());
    const uint32_t riff = static_cast<uint32_t> (wav.size() - 8);
    for (int i = 0; i < 4; ++i)
        wav[4 + static_cast<size_t> (i)] = static_cast<uint8_t> ((riff >> (8 * i)) & 0xff);
    SampleFacts facts;
    CHECK (probeSampleWav (wav, "tone.wav", facts));
    CHECK_EQ (facts.rootNote, 62);
    CHECK (facts.hasLoop);
    const SampleLoopSettings s = sampleLoopSettings (facts);
    CHECK (s.valid);
    CHECK_EQ (s.mode, 1);
    CHECK_NEAR (s.startPercent, 10.0, 0.01);
    CHECK_NEAR (s.endPercent, 60.0, 0.01);
    // a file without a loop: nothing is written
    const std::vector<uint8_t> plain = encodeWav (x.data(), nullptr, x.size(), 44100, 16);
    SampleFacts f2;
    CHECK (probeSampleWav (plain, "tone.wav", f2));
    CHECK (!sampleLoopSettings (f2).valid);
}
