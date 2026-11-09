// SynthyBoy - engine-vital tests: the modulation sources of GROUPB WP-06 (the outputs of the sound sources, Active Voices, Note-On Alt 1 / 2 / Rand 2 /
// Rand (Discrete), Release Velocity, Expression X, Fixed) and the warp modes from the Noise, Sub and Filters (13-new-keys.md 4.34).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// kCapModSources2 and kCapWarpSources are proven here: each source moves a destination, each warp mode changes the sound against None.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "alloc_hook.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;
/** process () with the allocations of the audio thread counted into `allocs`. */
void renderCounted (IEngine& engine, const std::vector<Event>& events, int totalFrames, int blockSize, unsigned long& allocs)
{
    std::vector<float> l (static_cast<size_t> (blockSize)), r (static_cast<size_t> (blockSize));
    std::vector<Event> local;
    size_t idx = 0;
    for (int start = 0; start < totalFrames; start += blockSize)
    {
        local.clear();
        while (idx < events.size() && events[idx].frame < start + blockSize)
        {
            auto e = events[idx++];
            e.frame -= start;
            local.push_back (e);
        }
        const auto before = g_allocations.load();
        engine.process (local.data(), static_cast<int> (local.size()), l.data(), r.data(), blockSize);
        allocs += g_allocations.load() - before;
    }
}


std::unique_ptr<IEngine> makeVital (const Quality& q = Quality {})
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, q);
    return e;
}

int choiceOfSource (const char* key)
{
    for (int i = 0; i < kNumModSources; ++i)
        if (std::string (modSourceTable()[i].key) == key)
            return i + 1;
    return 0;
}

int choiceOfDestination (const char* key) { return static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey (key)->id)))); }

/** Every sound source runs and is audible: three saws an octave apart, the noise, the sub, both filters (a saw each through them). */
void soundSources (IEngine& e)
{
    for (int n = 1; n <= 3; ++n)
    {
        const std::string o = "osc" + std::to_string (n);
        setByKey (e, (o + ".on").c_str(), 1.0f);
        setByKey (e, (o + ".wavetable").c_str(), 1.0f);
        setByKey (e, (o + ".level").c_str(), 60.0f);
        setByKey (e, (o + ".unison.voices").c_str(), 1.0f);
        setByKey (e, (o + ".random_phase").c_str(), 0.0f);
        setByKey (e, (o + ".destination").c_str(), n == 3 ? 0.0f : 3.0f);   // osc 3 through filter 1
        setByKey (e, (o + ".transpose").c_str(), static_cast<float> (12 * (n - 2)));
    }
    setByKey (e, "osc2.destination", 2.0f);   // osc 2 through filter 2
    setByKey (e, "noise.on", 1.0f);
    setByKey (e, "noise.level", 50.0f);
    setByKey (e, "noise.destination", 3.0f);
    setByKey (e, "sub.on", 1.0f);
    setByKey (e, "sub.level", 50.0f);
    setByKey (e, "sub.destination", 3.0f);
    setByKey (e, "filter1.on", 1.0f);
    setByKey (e, "filter1.cutoff", 2000.0f);
    setByKey (e, "filter2.on", 1.0f);
    setByKey (e, "filter2.cutoff", 2000.0f);
    setByKey (e, "master.volume", -12.0f);
}

void slot (IEngine& e, int n, int source, int destination, float amount)
{
    char key[32];
    std::snprintf (key, sizeof key, "mod%02d.source", n);
    setByKey (e, key, static_cast<float> (source));
    std::snprintf (key, sizeof key, "mod%02d.destination", n);
    setByKey (e, key, static_cast<float> (destination));
    std::snprintf (key, sizeof key, "mod%02d.amount", n);
    setByKey (e, key, amount);
}

sbtest::Rendered play (IEngine& e, double seconds = 0.5, int key = 48)
{
    return sbtest::render (e, { noteOn (0, key) }, static_cast<int> (seconds * kSr), 256);
}

double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0 = 0.1, double t1 = 0.4)
{
    double d = 0.0, n = 0.0;
    const size_t from = static_cast<size_t> (t0 * kSr), to = std::min (std::min (a.left.size(), b.left.size()), static_cast<size_t> (t1 * kSr));
    for (size_t i = from; i < to; ++i)
    {
        const double x = static_cast<double> (a.left[i]) - b.left[i];
        d += x * x;
        n += static_cast<double> (a.left[i]) * a.left[i];
    }
    return n > 0.0 ? std::sqrt (d / n) : (d > 0.0 ? 1.0e9 : 0.0);
}

double rms (const sbtest::Rendered& r, double t0, double t1, bool right = false)
{
    return sbtest::rmsOf (right ? r.right : r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}
} // namespace

TEST (modsources_capabilities_and_table)
{
    auto e = makeVital();
    CHECK ((e->capabilities() & kCapModSources2) != 0 && (e->capabilities() & kCapWarpSources) != 0);
    CHECK_EQ (kNumModSources, 57);
    CHECK_EQ (choiceOfSource ("osc_a"), 43);
    CHECK_EQ (choiceOfSource ("filter2"), 49);
    CHECK_EQ (choiceOfSource ("note_on_rand_discrete"), 54);
    CHECK_EQ (choiceOfSource ("fixed"), 57);
}

TEST (every_sound_source_moves_a_destination)
{
    // osc 1 plays through the matrix destination osc2.pan... the destination is the level of an oscillator that is not the source
    const struct { const char* key; const char* destination; } cases[] = {
        { "osc_a", "osc2.level" }, { "osc_b", "osc1.level" }, { "osc_c", "osc1.level" }, { "noise", "osc1.level" }, { "sub", "osc1.level" },
        { "filter1", "osc1.level" }, { "filter2", "osc1.level" },
    };
    for (const auto& c : cases)
    {
        auto reference = makeVital();
        soundSources (*reference);
        const auto without = play (*reference);
        auto e = makeVital();
        soundSources (*e);
        slot (*e, 1, choiceOfSource (c.key), choiceOfDestination (c.destination), 80.0f);
        const auto with = play (*e);
        const double difference = relativeDifference (without, with);
        std::printf ("    source %-8s -> %-11s difference %g\n", c.key, c.destination, difference);
        CHECK (sbtest::allFinite (with.left));
        CHECK (difference > 0.02);
    }
}

TEST (a_sound_source_that_depends_on_its_destination_is_a_stable_one_block_loop)
{
    auto e = makeVital();
    soundSources (*e);
    // osc A modulates its own level, filter 1 its own cutoff: the source is the held last sample of the block, so the loop reads the previous block (no
    // feedback node is built, nothing is allocated, the sound stays finite and bounded)
    slot (*e, 1, choiceOfSource ("osc_a"), choiceOfDestination ("osc1.level"), 80.0f);
    slot (*e, 2, choiceOfSource ("filter1"), choiceOfDestination ("filter1.cutoff"), 80.0f);
    const auto out = play (*e, 1.0);
    CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (out.right));
    CHECK (rms (out, 0.1, 0.9) > 0.001);
    CHECK (sbtest::peak (out.left) < 4.0f);
}

TEST (active_voices_alt_rand_fixed_release_velocity_and_expression_x)
{
    const auto simple = [] (IEngine& e)
    {
        setByKey (e, "osc1.on", 1.0f);
        setByKey (e, "osc1.wavetable", 1.0f);
        setByKey (e, "osc1.level", 40.0f);
        setByKey (e, "osc1.unison.voices", 1.0f);
        setByKey (e, "osc1.random_phase", 0.0f);
        setByKey (e, "osc1.destination", 3.0f);
        setByKey (e, "filter1.on", 0.0f);
        setByKey (e, "master.volume", -12.0f);
        setByKey (e, "env1.release", 20.0f);
    };
    const int level = choiceOfDestination ("osc1.level");

    // Fixed: a constant 1, the depth scales it: +50 % of the level range raises the level
    {
        auto a = makeVital(), b = makeVital();
        simple (*a);
        simple (*b);
        slot (*b, 1, choiceOfSource ("fixed"), level, 50.0f);
        CHECK (rms (play (*b), 0.1, 0.4) > 1.3 * rms (play (*a), 0.1, 0.4));
    }
    // Active Voices: the share of the polyphony in use. 2 voices of 2 = 1 raises the level, the same note alone with 8 voices allowed = 1/8 does not
    {
        auto a = makeVital(), b = makeVital();
        simple (*a);
        simple (*b);
        a->setParam (kParamMasterPolyphony, 2.0f);
        b->setParam (kParamMasterPolyphony, 2.0f);
        slot (*b, 1, choiceOfSource ("active_voices"), level, 100.0f);
        const auto chordA = sbtest::render (*a, { noteOn (0, 48), noteOn (0, 48 + 12) }, 24000, 256);
        const auto chordB = sbtest::render (*b, { noteOn (0, 48), noteOn (0, 48 + 12) }, 24000, 256);
        const double twoOfTwo = rms (chordB, 0.2, 0.45) / rms (chordA, 0.2, 0.45);
        auto c = makeVital();
        simple (*c);
        c->setParam (kParamMasterPolyphony, 16.0f);
        slot (*c, 1, choiceOfSource ("active_voices"), level, 100.0f);
        auto d = makeVital();
        simple (*d);
        d->setParam (kParamMasterPolyphony, 16.0f);
        const double oneOfSixteen = rms (play (*c), 0.1, 0.4) / rms (play (*d), 0.1, 0.4);
        std::printf ("    active voices: 2 of 2 raises the level x%g, 1 of 16 x%g\n", twoOfTwo, oneOfSixteen);
        CHECK (oneOfSixteen > 1.1 && oneOfSixteen < 1.7);
        CHECK (twoOfTwo > 2.0 * oneOfSixteen);
    }
    // Note-On Alt 1 / 2: 0 / 1 by turns at each note on, Alt 2 is the inverse: with +100 % on the level the notes alternate loud and quiet
    for (const char* source : { "note_on_alt1", "note_on_alt2" })
    {
        auto e = makeVital();
        simple (*e);
        slot (*e, 1, choiceOfSource (source), level, 100.0f);
        const auto first = play (*e, 0.35), second = play (*e, 0.35), third = play (*e, 0.35);
        const double r1 = rms (first, 0.1, 0.3), r2 = rms (second, 0.1, 0.3), r3 = rms (third, 0.1, 0.3);
        std::printf ("    %s notes %g %g %g\n", source, r1, r2, r3);
        CHECK (std::fabs (r1 - r2) > 0.3 * std::max (r1, r2));
        CHECK (std::fabs (r1 - r3) < 0.05 * std::max (r1, r3));
        CHECK ((std::string (source) == "note_on_alt1") == (r2 > r1));
    }
    // Note-On Rand 2: a number of its own per note, the same for the same note counter (deterministic)
    {
        const auto run = [&] (std::vector<double>& levels)
        {
            auto e = makeVital();
            simple (*e);
            slot (*e, 1, choiceOfSource ("note_on_rand2"), level, 100.0f);
            for (int n = 0; n < 4; ++n)
                levels.push_back (rms (play (*e, 0.35), 0.1, 0.3));
        };
        std::vector<double> first, second;
        run (first);
        run (second);
        CHECK (first == second);
        CHECK (std::fabs (first[0] - first[1]) > 0.02 * first[0] || std::fabs (first[1] - first[2]) > 0.02 * first[1]);
    }
    // Note-On Rand (Discrete): two slots with the source get two different numbers, Rand 2 gives them the same one. Osc 1 hard left, osc 2 hard right
    {
        const auto stereoPair = [&] (IEngine& e, const char* source)
        {
            setByKey (e, "osc1.on", 1.0f);
            setByKey (e, "osc2.on", 1.0f);
            for (const char* o : { "osc1", "osc2" })
            {
                setByKey (e, (std::string (o) + ".wavetable").c_str(), 1.0f);
                setByKey (e, (std::string (o) + ".level").c_str(), 40.0f);
                setByKey (e, (std::string (o) + ".unison.voices").c_str(), 1.0f);
                setByKey (e, (std::string (o) + ".random_phase").c_str(), 0.0f);
                setByKey (e, (std::string (o) + ".destination").c_str(), 3.0f);
            }
            setByKey (e, "osc1.pan", -100.0f);
            setByKey (e, "osc2.pan", 100.0f);
            setByKey (e, "filter1.on", 0.0f);
            setByKey (e, "master.volume", -12.0f);
            slot (e, 1, choiceOfSource (source), choiceOfDestination ("osc1.level"), 100.0f);
            slot (e, 2, choiceOfSource (source), choiceOfDestination ("osc2.level"), 100.0f);
        };
        auto discrete = makeVital(), shared = makeVital();
        stereoPair (*discrete, "note_on_rand_discrete");
        stereoPair (*shared, "note_on_rand2");
        const auto d = play (*discrete), s = play (*shared);
        const double ratioDiscrete = rms (d, 0.1, 0.4) / std::max (1.0e-9, rms (d, 0.1, 0.4, true));
        const double ratioShared = rms (s, 0.1, 0.4) / std::max (1.0e-9, rms (s, 0.1, 0.4, true));
        std::printf("    left / right level: discrete %g, shared %g\n", ratioDiscrete, ratioShared);
        CHECK_NEAR (ratioShared, 1.0, 0.03);
        CHECK (std::fabs (ratioDiscrete - 1.0) > 0.05);
    }
    // Release Velocity (the note-off velocity): a hard release against a soft one changes the tail
    {
        const auto tail = [&] (float velocity)
        {
            auto e = makeVital();
            simple (*e);
            setByKey (*e, "env1.release", 400.0f);
            slot (*e, 1, choiceOfSource ("release_velocity"), level, 100.0f);
            auto off = noteOff (12000, 48);
            off.value = velocity;
            return sbtest::render (*e, { noteOn (0, 48), off }, 24000, 256);
        };
        const auto soft = tail (0.0f), hard = tail (1.0f);
        CHECK (rms (hard, 0.3, 0.45) > 1.3 * rms (soft, 0.3, 0.45));
        CHECK_NEAR (rms (hard, 0.05, 0.2), rms (soft, 0.05, 0.2), 0.02 * rms (soft, 0.05, 0.2)); // before the release nothing differs
    }
    // Expression X: the per note bend over its 48 semitones; a note expression tuning of 24 st is +0.5
    {
        const auto run = [&] (float semitones)
        {
            auto e = makeVital();
            simple (*e);
            slot (*e, 1, choiceOfSource ("expression_x"), choiceOfDestination ("osc1.pan"), 100.0f);
            Event tune;
            tune.type = Event::NoteExpression;
            tune.expression = Event::Tuning;
            tune.key = 48;
            tune.value = semitones;
            tune.frame = 0;
            return sbtest::render (*e, { noteOn (0, 48), tune }, 24000, 256);
        };
        const auto centre = run (0.0f), bent = run (24.0f);
        const double balanceCentre = rms (centre, 0.1, 0.4, true) / std::max (1.0e-9, rms (centre, 0.1, 0.4));
        const double balanceBent = rms (bent, 0.1, 0.4, true) / std::max (1.0e-9, rms (bent, 0.1, 0.4));
        std::printf ("    right / left: centre %g, bent %g\n", balanceCentre, balanceBent);
        CHECK (std::fabs (balanceBent - balanceCentre) > 0.1);
    }
}

TEST (warp_modes_from_the_noise_sub_and_filters_change_the_sound)
{
    const char* const families[] = { "FM Thru-Zero", "FM Exp", "FM Linear", "PD", "AM", "RM" };
    const char* const sources[] = { "Noise", "Sub", "Filter 1", "Filter 2" };
    const auto patch = [] (IEngine& e)
    {
        soundSources (e);
        setByKey (e, "osc1.destination", 3.0f);
        setByKey (e, "osc1.level", 70.0f);
        setByKey (e, "osc1.warp.amount", 60.0f);
        setByKey (e, "filter1.on", 1.0f);
        setByKey (e, "filter2.on", 1.0f);
    };
    auto reference = makeVital();
    patch (*reference);
    const auto none = play (*reference);
    for (int family = 0; family < 6; ++family)
        for (int source = 0; source < 4; ++source)
        {
            auto e = makeVital();
            patch (*e);
            const float choice = static_cast<float> (52 + 4 * family + source);
            setByKey (*e, "osc1.warp.type", choice);
            const auto out = play (*e);
            const double difference = relativeDifference (none, out);
            std::printf ("    %-12s from %-8s (%2d) difference %g\n", families[family], sources[source], static_cast<int> (choice), difference);
            CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (out.right));
            CHECK (difference > 0.01);
        }
    // the same modes in the second slot, and on a sample oscillator (FM from the Sub moves the read speed)
    auto second = makeVital();
    patch (*second);
    setByKey (*second, "osc1.warp2.type", 53.0f);
    CHECK (relativeDifference (none, play (*second)) > 0.01);
    CHECK_EQ (static_cast<int> (choiceOfSource ("osc_a")), 43);
}

TEST (warp_modes_from_a_filter_use_the_filter_of_the_same_voice)
{
    // two notes at once: the filter source is the previous block of each voice's own filter, so a chord renders the same as the two notes added
    const auto patch = [] (IEngine& e)
    {
        soundSources (e);
        setByKey (e, "osc1.level", 70.0f);
        setByKey (e, "osc1.warp.amount", 70.0f);
        setByKey (e, "osc1.warp.type", 52.0f + 4.0f * 3.0f + 2.0f); // PD from Filter 1
    };
    auto chord = makeVital();
    patch (*chord);
    const auto both = sbtest::render (*chord, { noteOn (0, 48), noteOn (0, 55) }, 24000, 256);
    auto a = makeVital(), b = makeVital();
    patch (*a);
    patch (*b);
    const auto one = sbtest::render (*a, { noteOn (0, 48) }, 24000, 256), two = sbtest::render (*b, { noteOn (0, 55) }, 24000, 256);
    std::vector<float> sum (one.left.size());
    for (size_t i = 0; i < sum.size(); ++i)
        sum[i] = one.left[i] + two.left[i];
    double d = 0.0, n = 0.0;
    for (size_t i = 6000; i < 20000; ++i)
    {
        const double x = static_cast<double> (both.left[i]) - sum[i];
        d += x * x;
        n += static_cast<double> (sum[i]) * sum[i];
    }
    std::printf ("    chord against the sum of the notes: %g\n", std::sqrt (d / n));
    CHECK (std::sqrt (d / n) < 0.05);
}

TEST (warp_modes_from_the_sub_and_a_filter_act_on_a_sample_oscillator)
{
    // a 440 Hz sine as the sample of osc 1
    std::vector<float> audio (static_cast<size_t> (1.5 * 48000));
    for (size_t i = 0; i < audio.size(); ++i)
        audio[i] = 0.6f * static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * 440.0 * static_cast<double> (i) / 48000.0));
    std::vector<uint8_t> wav;
    const auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) wav.push_back (static_cast<uint8_t> (v >> (8 * i))); };
    const auto put16 = [&] (uint16_t v) { wav.push_back (static_cast<uint8_t> (v)); wav.push_back (static_cast<uint8_t> (v >> 8)); };
    const auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) wav.push_back (static_cast<uint8_t> (t[i])); };
    tag ("RIFF"); put32 (static_cast<uint32_t> (36 + audio.size() * 2)); tag ("WAVE");
    tag ("fmt "); put32 (16); put16 (1); put16 (1); put32 (48000); put32 (96000); put16 (2); put16 (16);
    tag ("data"); put32 (static_cast<uint32_t> (audio.size() * 2));
    for (float v : audio)
        put16 (static_cast<uint16_t> (static_cast<int16_t> (std::lround (v * 32767.0f))));
    MemoryContentStore store;
    store.add ("test", "sample.wav", wav);
    StateContent state;
    ContentSample c;
    c.osc = 1;
    c.ref.pack = "test";
    c.ref.file = "sample.wav";
    c.ref.name = "sample";
    c.sampleRate = 48000;
    c.frames = static_cast<int> (audio.size());
    c.channels = 1;
    state.samples.push_back (c);

    const auto render = [&] (float warp)
    {
        auto e = makeVital();
        soundSources (*e);
        e->setContentStore (&store);
        e->setContent (state);
        setByKey (*e, "osc1.type", 1.0f);
        setByKey (*e, "osc1.sample.root", 69.0f);
        setByKey (*e, "osc1.sample.loop_mode", 1.0f);
        setByKey (*e, "osc1.warp.amount", 60.0f);
        setByKey (*e, "osc1.warp.type", warp);
        return play (*e, 0.6, 69);
    };
    const auto none = render (0.0f);
    CHECK (rms (none, 0.2, 0.5) > 0.001);
    for (const float warp : { 53.0f, 55.0f, 57.0f, 69.0f, 73.0f })   // FM Thru-Zero from Sub / Filter 2, FM Exp from Sub, AM from Sub, RM from Sub
    {
        const auto out = render (warp);
        CHECK (sbtest::allFinite (out.left));
        CHECK (relativeDifference (none, out, 0.2, 0.5) > 0.01);
    }
}

TEST (the_new_sources_and_warp_modes_never_allocate_on_the_audio_thread)
{
    auto e = makeVital();
    soundSources (*e);
    unsigned long allocs = 0;
    renderCounted (*e, { noteOn (0, 48) }, 2048, 256, allocs);   // warm up
    allocs = 0;
    const char* const sources[] = { "osc_a", "osc_b", "noise", "sub", "filter1", "filter2", "active_voices", "note_on_alt1", "note_on_rand2", "note_on_rand_discrete",
                                    "release_velocity", "expression_x", "fixed" };
    int slotNumber = 1;
    for (const char* source : sources)
    {
        slot (*e, slotNumber, choiceOfSource (source), choiceOfDestination (slotNumber % 2 == 0 ? "osc1.pan" : "filter1.cutoff"), 30.0f);   // wired while playing
        ++slotNumber;
    }
    setByKey (*e, "osc1.warp.type", 66.0f);   // PD from Filter 1
    setByKey (*e, "osc2.warp.type", 61.0f);   // FM Linear from Sub
    renderCounted (*e, { noteOn (0, 52), noteOn (4000, 55) }, 16384, 256, allocs);
    setByKey (*e, "osc1.warp.type", 0.0f);
    for (int n = 1; n < slotNumber; ++n)
    {
        char key[32];
        std::snprintf (key, sizeof key, "mod%02d.source", n);
        setByKey (*e, key, 0.0f);                   // and unwired while playing
    }
    renderCounted (*e, { noteOn (0, 40) }, 4096, 256, allocs);
    CHECK_EQ (allocs, 0ul);
}
