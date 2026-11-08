// SynthyBoy - core tests: real-time rule "no allocation in process()" (docs/ARCHITECTURE.md 3.7).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The global operator new / delete of this executable are replaced by counting versions.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "alloc_hook.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/tuning.h"

using namespace synthyboy;

namespace
{
void* g_sink = nullptr;
} // namespace

TEST (alloc_hook_counts_allocations)
{
    const auto before = g_allocations.load();
    g_sink = new int (1); // escapes through a global, so the compiler cannot elide it
    CHECK (g_allocations.load() == before + 1);
    delete static_cast<int*> (g_sink);
    g_sink = nullptr;

    const auto beforeVector = g_allocations.load();
    {
        std::vector<int> v (1000, 1);
        g_sink = v.data();
        CHECK (g_allocations.load() > beforeVector);
    }
    g_sink = nullptr;
    const auto beforeAligned = g_allocations.load();
    {
        struct alignas (64) Wide { char c[64]; };
        auto* w = new Wide;
        g_sink = w;
        CHECK (g_allocations.load() == beforeAligned + 1);
        delete w;
        g_sink = nullptr;
    }
}

TEST (process_does_not_allocate)
{
    for (const sbtest::EngineCase& engineCase : sbtest::engineCases())
    for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
    {
        sbtest::Context context (engineCase.name);
        auto engine = createEngine (engineCase.kind);
        const auto beforePrepare = g_allocations.load();
        engine->prepare (48000.0, 256, qualityForTier (tier)); // allocation is allowed here
        CHECK (g_allocations.load() > beforePrepare);          // ... and the hook does see the engine's allocations

        constexpr int blockSize = 256;
        constexpr int numBlocks = 375; // 2 s at 48 kHz
        static float left[blockSize], right[blockSize];
        static Event events[64];
        sbtest::Rng rng (3);
        bool finite = true;
        int maxActive = 0;

        const auto before = g_allocations.load();
        for (int block = 0; block < numBlocks; ++block)
        {
            int n = 0;
            // a note on at the start of the block, one at 100 and note offs, bends, all-notes-off now and then
            events[n++] = sbtest::noteOn (0, 36 + static_cast<int> (rng.u32() % 48), 0.3f + 0.7f * static_cast<float> (rng.uniform()),
                                          static_cast<int> (rng.u32() % 4), block);
            events[n++] = sbtest::noteOn (100, 48 + static_cast<int> (rng.u32() % 48), 1.0f);
            if (block % 2 == 0)
                events[n++] = sbtest::noteOff (130, 36 + static_cast<int> (rng.u32() % 48), static_cast<int>(rng.u32() % 4));
            if (block % 3 == 0)
                events[n++] = sbtest::pitchBend (150, static_cast<float> (rng.uniform() * 2.0 - 1.0));
            if (block % 40 == 39)
                events[n++] = sbtest::allNotesOff (200);

            // parameter changes from the "UI thread" (here: the same thread) between blocks
            engine->setParam (kParamMasterVolume, -40.0f + static_cast<float> (rng.uniform()) * 40.0f);
            engine->setParam (kParamFilter1Cutoff, 100.0f + static_cast<float> (rng.uniform()) * 10000.0f);
            engine->setParam (kParamOsc1Wavetable, (block / 25) % 2 == 0 ? 0.0f : 1.0f);
            (void) engine->getParam (kParamMasterVolume);

            engine->process (events, n, left, right, blockSize);
            for (int i = 0; i < blockSize; ++i)
                finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
            maxActive = std::max (maxActive, engine->activeVoices());
        }
        const auto after = g_allocations.load();

        if (! CHECK_EQ (after - before, 0ul))
            std::printf ("    %s, tier %d: %lu allocation(s) during process()\n", engineCase.name, static_cast<int> (tier), after - before);
        CHECK (finite);
        CHECK (maxActive > 0);

        // reset and state-like changes during playback do not allocate either
        const auto beforeReset = g_allocations.load();
        engine->reset();
        engine->process (events, 2, left, right, blockSize);
        CHECK_EQ (g_allocations.load() - beforeReset, 0ul);
    }
}

// Whole patches that change while the engine plays: random parameters of every kind (a few per block), the modulation matrix
// wired and rewired, the quality handed over, notes and bends. Nothing may allocate in process().
TEST (process_does_not_allocate_while_patches_change)
{
    for (const sbtest::EngineCase& engineCase : sbtest::engineCases())
    for (const Tier tier : { Tier::low, Tier::high })
    {
        sbtest::Context context (engineCase.name);
        auto engine = createEngine (engineCase.kind);
        engine->setParam (kParamMasterPolyphony, 32.0f);
        engine->prepare (48000.0, 128, qualityForTier (tier));

        constexpr int blockSize = 128;
        static float left[blockSize], right[blockSize];
        sbtest::Rng rng (tier == Tier::low ? 11 : 12);
        const size_t numParams = paramCount();
        Event events[8];
        bool finite = true;
        unsigned long allocations = 0;

        for (int block = 0; block < 1500; ++block)
        {
            // parameters first: they are the "UI thread" of this test (allocation here is not counted)
            for (int k = 0; k < 6; ++k)
            {
                const auto& info = paramTable()[rng.u32() % numParams];
                engine->setParam (info.id, info.min + static_cast<float> (rng.uniform()) * (info.max - info.min));
            }
            for (int k = 0; k < 3; ++k) // the matrix: slots get a source, a destination, an amount
            {
                const auto slot = modSlotParams (static_cast<int> (rng.u32() % kNumModSlots));
                engine->setParam (paramTable()[slot.source].id, static_cast<float> (rng.u32() % (1 + modSourceCount())));
                engine->setParam (paramTable()[slot.destination].id, static_cast<float> (rng.u32() % (1 + modDestinationCount())));
                engine->setParam (paramTable()[slot.amount].id, -100.0f + 200.0f * static_cast<float> (rng.uniform()));
            }
            if (block % 100 == 50)
            {
                Quality q = qualityForTier (rng.u32() % 2 == 0 ? Tier::low : Tier::high);
                q.maxVoices = 1 + static_cast<int> (rng.u32() % 32);
                engine->setQuality (q);
            }

            int n = 0;
            if (block % 2 == 0)
                events[n++] = sbtest::noteOn (static_cast<int> (rng.u32() % 100), 30 + static_cast<int> (rng.u32() % 60), 0.3f + 0.7f * static_cast<float> (rng.uniform()));
            if (block % 3 == 0)
                events[n++] = sbtest::noteOff (120, 30 + static_cast<int> (rng.u32() % 60));
            std::stable_sort (events, events + n, [] (const Event& a, const Event& b) { return a.frame < b.frame; });

            const auto before = g_allocations.load();
            engine->process (events, n, left, right, blockSize);
            allocations += g_allocations.load() - before;
            for (int i = 0; i < blockSize; ++i)
                finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
        }

        if (! CHECK_EQ (allocations, 0ul))
            std::printf ("    %s, tier %d: %lu allocation(s) during process()\n", engineCase.name, static_cast<int> (tier), allocations);
        CHECK (finite);
    }
}

// WP1 to WP3 (docs/ui-design/13-new-keys.md): the new events (sustain pedal, note expression, release velocity), the host transport, the content
// (custom LFO shapes with point modulation, velocity / note curves, a user tuning), an MTS-ESP style tuning source and every new key at random
// values (aux sources, LFO options, beat units, scale, MPE, oscillator mapping) must not allocate in process() either. setContent () itself runs
// on the "message thread" (allocation allowed, not counted).
namespace
{
struct MovingMaster final : tuning::ITuningSource
{
    mutable float cents = 0.0f;
    bool isConnected() const noexcept override { return true; }
    double frequencyForKey (int key) const noexcept override { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0 + cents / 1200.0); }
    bool query128Keys (std::array<double, tuning::kNumKeys>& out) const noexcept override
    {
        for (int k = 0; k < tuning::kNumKeys; ++k)
            out[static_cast<size_t> (k)] = frequencyForKey (k);
        return true;
    }
};

StateContent makeContent (int variant)
{
    StateContent content;
    for (int lfo = 1; lfo <= 10; lfo += 3)
    {
        ContentLfoShape shape;
        shape.lfo = lfo;
        shape.smooth = (variant + lfo) % 2 == 0;
        const int points = 2 + (variant * 7 + lfo * 5) % 40;
        for (int i = 0; i < points; ++i)
        {
            ContentPoint p;
            p.x = static_cast<float> (i) / static_cast<float> (points - 1);
            p.y = static_cast<float> ((i * 37 + variant * 11) % 100) / 100.0f;
            p.power = static_cast<float> ((i % 5) - 2);
            if (i % 3 == 1)
                p.modY = { true, i % 2 == 0 ? "macro1" : "mod_wheel", 0.5f };
            if (i % 4 == 2)
                p.modX = { true, "pitch_wheel", 0.1f };
            shape.points.push_back (p);
        }
        content.lfoShapes.push_back (shape);
    }
    content.velocityCurve = { { 0.0f, 0.2f, 0.0f }, { 0.5f, 0.9f, -3.0f }, { 1.0f, 1.0f, 0.0f } };
    content.noteCurve = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 2.0f } };
    // WP4: a sample and a table for oscillator 1 and 2 (the store of the test holds the files)
    for (int osc = 1; osc <= 2; ++osc)
    {
        ContentSample sample;
        sample.osc = osc;
        sample.ref.pack = "rt";
        sample.ref.file = "sample.wav";
        sample.slices = { 0.2f, 0.4f, 0.6f };
        content.samples.push_back (sample);
        ContentWavetable table;
        table.osc = osc;
        table.ref.pack = "rt";
        table.ref.file = "table.wav";
        content.wavetables.push_back (table);
    }
    content.tuning.present = true;
    content.tuning.scl = "! t.scl\nt\n 7\n 200.0\n 400.0\n 500.0\n 700.0\n 900.0\n 1100.0\n 1200.0\n";
    return content;
}

std::vector<uint8_t> testWav (int frames, bool saw)
{
    std::vector<uint8_t> b;
    const auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (v >> (8 * i))); };
    const auto put16 = [&] (uint16_t v) { b.push_back (static_cast<uint8_t> (v)); b.push_back (static_cast<uint8_t> (v >> 8)); };
    const auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (t[i])); };
    tag ("RIFF"); put32 (static_cast<uint32_t> (36 + frames * 2)); tag ("WAVE");
    tag ("fmt "); put32 (16); put16 (1); put16 (1); put32 (44100); put32 (88200); put16 (2); put16 (16);
    tag ("data"); put32 (static_cast<uint32_t> (frames * 2));
    for (int i = 0; i < frames; ++i)
        put16 (static_cast<uint16_t> (static_cast<int16_t> (saw ? (i % 2048) * 12 - 12000 : static_cast<int> (10000.0 * std::sin (i * 0.05)))));
    return b;
}
} // namespace

TEST (process_does_not_allocate_with_the_new_events_content_and_keys)
{
    for (const sbtest::EngineCase& engineCase : sbtest::engineCases())
    {
        sbtest::Context context (engineCase.name);
        auto engine = createEngine (engineCase.kind);
        engine->setParam (kParamMasterPolyphony, 32.0f);
        engine->prepare (48000.0, 128, qualityForTier (Tier::high));
        MovingMaster master;
        engine->setTuningSource (&master);
        MemoryContentStore store;
        store.add ("rt", "sample.wav", testWav (30000, false));
        store.add ("rt", "table.wav", testWav (2048 * 3, true));
        engine->setContentStore (&store);

        constexpr int blockSize = 128;
        static float left[blockSize], right[blockSize];
        sbtest::Rng rng (21);
        Event events[12];
        bool finite = true;
        unsigned long allocations = 0;

        for (int block = 0; block < 1600; ++block)
        {
            if (block % 150 == 0)
                engine->setContent (makeContent (block / 150)); // message thread
            // new keys: a few random ones per block, always the extremes of the new aux / option groups too
            for (int k = 0; k < 8; ++k)
            {
                const size_t index = kNumKeysV2 + rng.u32() % (paramCount() - kNumKeysV2);
                const auto& info = paramTable()[index];
                engine->setParam (info.id, info.min + static_cast<float> (rng.uniform()) * (info.max - info.min));
            }
            for (int k = 0; k < 3; ++k)
            {
                const auto slot = modSlotParams (static_cast<int> (rng.u32() % kNumModSlots));
                engine->setParam (paramTable()[slot.source].id, static_cast<float> (rng.u32() % (1 + modSourceCount())));
                engine->setParam (paramTable()[slot.destination].id, static_cast<float> (rng.u32() % (1 + modDestinationCount())));
                engine->setParam (paramTable()[slot.auxSource].id, static_cast<float> (rng.u32() % (1 + modSourceCount())));
                engine->setParam (paramTable()[slot.auxPower].id, -10.0f + 20.0f * static_cast<float> (rng.uniform()));
            }
            master.cents = static_cast<float> (block % 200);

            int n = 0;
            if (block % 2 == 0)
                events[n++] = sbtest::noteOn (static_cast<int> (rng.u32() % 60), 30 + static_cast<int> (rng.u32() % 60), 0.3f + 0.7f * static_cast<float> (rng.uniform()),
                                              static_cast<int> (rng.u32() % 4), block);
            if (block % 3 == 0)
            {
                Event off = sbtest::noteOff (80, 30 + static_cast<int> (rng.u32() % 60), static_cast<int> (rng.u32() % 4));
                off.value = static_cast<float> (rng.uniform());
                events[n++] = off;
            }
            Event pedal = sbtest::noteOn (90, 0, rng.u32() % 2 == 0 ? 1.0f : 0.0f, static_cast<int> (rng.u32() % 4) - 1);
            pedal.type = Event::SustainPedal;
            events[n++] = pedal;
            Event expression = sbtest::noteOn (100, static_cast<int> (rng.u32() % 128) - 1, static_cast<float> (rng.uniform() * 4.0 - 2.0),
                                               static_cast<int> (rng.u32() % 16), static_cast<int> (rng.u32() % 50) - 1);
            expression.type = Event::NoteExpression;
            expression.expression = static_cast<Event::Expression> (rng.u32() % 3);
            events[n++] = expression;
            if (block % 5 == 0)
                events[n++] = sbtest::pitchBend (110, static_cast<float> (rng.uniform() * 2.0 - 1.0), static_cast<int> (rng.u32() % 4));
            std::stable_sort (events, events + n, [] (const Event& a, const Event& b) { return a.frame < b.frame; });

            TransportInfo transport;
            transport.bpm = 40.0 + static_cast<double> (rng.u32() % 200);
            transport.songPosBeats = block * 0.25;
            transport.playing = block % 7 != 0;

            const auto before = g_allocations.load();
            engine->setTransport (transport);
            engine->process (events, n, left, right, blockSize);
            allocations += g_allocations.load() - before;
            for (int i = 0; i < blockSize; ++i)
                finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
        }

        if (! CHECK_EQ (allocations, 0ul))
            std::printf ("    %s: %lu allocation(s) during process()\n", engineCase.name, allocations);
        CHECK (finite);
        engine->setTuningSource (nullptr);
    }
}
