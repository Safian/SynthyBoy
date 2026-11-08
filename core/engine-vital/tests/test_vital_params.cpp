// SynthyBoy - engine-vital tests: the parameter mapping (converters, controls, tiers, built-in tables, modulation destinations).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "line_generator.h" // Vital-derived
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_bindings.h"
#include "vital_engine.h"
#include "vital_tables.h"

using namespace synthyboy;
using sbtest::noteOn;

namespace
{
constexpr double kSampleRate = 48000.0;

std::unique_ptr<IEngine> preparedEngine (const Quality& q = qualityForTier (Tier::high))
{
    auto e = createVitalEngine();
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSampleRate, 256, q);
    return e;
}

/** One block of silence: the parameters are copied into the engine at the start of a block. */
void applyParams (IEngine& e)
{
    std::vector<float> l (64), r (64);
    e.process (nullptr, 0, l.data(), r.data(), 64);
}

} // namespace

TEST (every_parameter_is_bound_or_special_exactly_once)
{
    std::vector<int> bound (paramCount(), 0);
    for (const auto& b : vitalBindings())
    {
        if (! CHECK (b.param < paramCount()))
        {
            std::printf ("    %s\n", b.control.c_str());
            continue;
        }
        ++bound[b.param];
    }
    size_t special = 0;
    // the keys of state version 3: exactly the ones whose capability bits the engine reports are bound once or handled by the adapter
    // (special); every other one is stored and ignored
    auto probe = createVitalEngine();
    size_t v3Live = 0;
    for (size_t i = kNumKeysV2; i < paramCount(); ++i)
    {
        const bool live = capsCover (probe->capabilities(), requiredCaps (i));
        v3Live += live ? 1 : 0;
        if (live)
        {
            CHECK ((bound[i] == 1) != isSpecialParam (i));
            if (bound[i] + (isSpecialParam (i) ? 1 : 0) != 1)
                std::printf ("    %s is live but neither bound nor special\n", paramTable()[i].key);
        }
        else if (! CHECK (bound[i] == 0 && ! isSpecialParam (i)))
            std::printf ("    %s is bound but its capability bit is not set\n", paramTable()[i].key);
    }
    std::printf ("    %zu keys of state version 3 are live in this engine\n", v3Live);
    for (size_t i = 0; i < kNumKeysV2; ++i)
    {
        if (isSpecialParam (i))
        {
            ++special;
            CHECK_EQ (bound[i], 0);
        }
        else if (! CHECK_EQ (bound[i], 1))
        {
            std::printf ("    %s: bound %d times\n", paramTable()[i].key, bound[i]);
        }
    }
    // 3 + 3 wavetables / shapes... : 3 oscillators, 8 LFOs, routing, 9 FX order slots, 2 x 64 slot choices, bend range
    CHECK_EQ (special, static_cast<size_t> (3 + 8 + 1 + kNumLegacyFxSlots + 2 * kNumModSlots + 1));
    std::printf ("    %zu bound, %zu special\n", vitalBindings().size(), special);

    // every control exists in the engine and no two parameters share one; except the millisecond and the beat key of an envelope time (one
    // control, gated by envN.units to opposite values)
    std::set<std::string> controls;
    std::map<std::string, std::vector<const Binding*>> shared;
    for (const auto& b : vitalBindings())
    {
        CHECK (vitalControlRange (b.control).exists);
        if (b.gateParam < 0)
            CHECK (controls.insert (b.control).second);
        else
            shared[b.control].push_back (&b);
    }
    for (const auto& entry : shared)
    {
        CHECK (controls.count (entry.first) == 0);
        CHECK_EQ (entry.second.size(), static_cast<size_t> (2));
        if (entry.second.size() == 2)
        {
            CHECK_EQ (entry.second[0]->gateParam, entry.second[1]->gateParam);
            CHECK (entry.second[0]->gateValue != entry.second[1]->gateValue);
        }
    }
    CHECK_EQ (shared.size(), static_cast<size_t> (30)); // 6 envelopes x delay, attack, hold, decay, release
}

TEST (converted_values_stay_inside_the_controls_of_the_engine)
{
    // every parameter at its minimum, default, maximum and along a sweep: the value for the engine's control is inside that
    // control's own range (read from the engine's value details). Tolerance 1e-5 of the range plus 1e-6: a few conversions end
    // exactly on a bound (4 s = 1.41421356 against 1.41421 as the engine states it).
    long checked = 0;
    for (const auto& b : vitalBindings())
    {
        const auto& info = paramTable()[b.param];
        const auto range = vitalControlRange (b.control);
        const double tol = 1.0e-5 * (range.max - range.min) + 1.0e-6;
        std::vector<float> plains = { info.min, info.def, info.max };
        for (int k = 0; k <= 200; ++k)
            plains.push_back (fromNormalised (info, static_cast<float> (k) / 200.0f));
        for (const float plain : plains)
        {
            const double raw = convertParam (b, static_cast<double> (plain), kSampleRate);
            ++checked;
            if (! CHECK (std::isfinite (raw) && raw >= range.min - tol && raw <= range.max + tol))
            {
                std::printf ("    %s = %.9g -> %s %.9g, the control accepts %.9g .. %.9g\n", info.key, static_cast<double> (plain), b.control.c_str(), raw,
                             range.min, range.max);
                break;
            }
        }
    }
    std::printf ("    %ld conversions checked\n", checked);
}

TEST (converters_known_values)
{
    const auto conv = [] (const char* key, double plain, double sr = kSampleRate)
    {
        return vitalConvert (static_cast<size_t> (findParamIndex (findParamByKey (key)->id)), plain, sr);
    };

    // volume: shown dB = sqrt (control) - 80
    CHECK_NEAR (conv ("master.volume", -12.0).raw, 68.0 * 68.0, 1e-6);
    CHECK_NEAR (conv ("master.volume", 6.0).raw, 86.0 * 86.0, 1e-6);
    CHECK_NEAR (conv ("master.volume", -60.0).raw, 0.0, 1e-9);  // silence
    CHECK_NEAR (conv ("master.volume", 0.0).raw, 6400.0, 1e-6); // 0 dB
    // level: percent of amplitude, the engine squares its control
    CHECK_NEAR (conv ("osc1.level", 50.0).raw, std::sqrt (0.5), 1e-9);
    CHECK_NEAR (conv ("osc1.level", 100.0).raw, 1.0, 1e-9);
    CHECK_NEAR (conv ("osc1.level", 0.0).raw, 0.0, 1e-9);
    // envelope times: seconds = control ^ 4
    CHECK_NEAR (conv ("env1.attack", 1000.0).raw, 1.0, 1e-9);
    CHECK_NEAR (conv ("env1.release", 81.0).raw, std::pow (0.081, 0.25), 1e-9);
    CHECK_NEAR (conv ("env1.attack", 0.0).raw, 0.0, 1e-9);
    // rates: log2 of Hz; times: log2 of seconds; delay: log2 of the frequency 1 / time
    CHECK_NEAR (conv ("lfo1.rate", 1.0).raw, 0.0, 1e-9);
    CHECK_NEAR (conv ("lfo1.rate", 8.0).raw, 3.0, 1e-9);
    CHECK_NEAR (conv ("master.portamento.time", 1000.0).raw, 0.0, 1e-9);
    CHECK_NEAR (conv ("master.portamento.time", 0.0).raw, -10.0, 1e-9); // off
    CHECK_NEAR (conv ("delay.time", 250.0).raw, 2.0, 1e-9);
    CHECK_NEAR (conv ("delay.time", 1000.0).raw, 0.0, 1e-9);
    // cutoff in Hz -> MIDI note: A4 = note 69, limited to 0.45 x the sample rate
    CHECK_NEAR (conv ("filter1.cutoff", 440.0).raw, 69.0, 1e-4);
    CHECK_NEAR (conv ("filter1.cutoff", 880.0).raw, 81.0, 1e-4);
    CHECK_NEAR (conv ("filter1.cutoff", 20000.0, 22050.0).raw, 12.0 * std::log2 (0.45 * 22050.0 / 8.1757989156), 1e-4);
    // percent -> 0..1 and the doubled / halved scales; degrees -> cycles
    CHECK_NEAR (conv ("filter1.resonance", 20.0).raw, 0.2, 1e-9);
    CHECK_NEAR (conv ("filter1.blend", 50.0).raw, 1.0, 1e-9);        // 0..100 % -> 0..2
    CHECK_NEAR (conv ("flanger.mix", 100.0).raw, 0.5, 1e-9);         // 0..100 % -> 0..0.5
    CHECK_NEAR (conv ("osc1.morph.spread", 100.0).raw, 0.5, 1e-9);   // +-100 % -> +-0.5
    CHECK_NEAR (conv ("osc1.phase", 180.0).raw, 0.5, 1e-9);
    CHECK_NEAR (conv ("lfo1.stereo", -180.0).raw, -0.5, 1e-9);
    CHECK_NEAR (conv ("master.tune", 50.0).raw, 0.5, 1e-9);
    CHECK_NEAR (conv ("osc1.unison.detune", 25.0).raw, 5.0, 1e-9);   // the engine shows control^2 as percent
    CHECK_NEAR (conv ("osc1.frame", 100.0).raw, vitaltables::kMorphFrames - 1.0, 1e-9);
    // offset choices and the warp type map (the sample variants of the engine are not offered)
    CHECK_NEAR (conv ("delay.division", 0.0).raw, 4.0, 1e-9);
    CHECK_NEAR (conv ("delay.division", 8.0).raw, 12.0, 1e-9);
    CHECK_NEAR (conv ("osc1.warp.type", 6.0).raw, 6.0, 1e-9);
    CHECK_NEAR (conv ("osc1.warp.type", 7.0).raw, 7.0, 1e-9);
    CHECK_NEAR (conv ("osc1.warp.type", 8.0).raw, 8.0, 1e-9);
    CHECK_NEAR (conv ("osc1.warp.type", 9.0).raw, 10.0, 1e-9);
    CHECK_NEAR (conv ("osc1.warp.type", 10.0).raw, 11.0, 1e-9);
    // effects: the tier may bypass them, the converter itself is the switch
    CHECK_NEAR (conv ("reverb.on", 1.0).raw, 1.0, 1e-9);
    // a special parameter has no single control
    CHECK (! conv ("osc1.wavetable", 3.0).bound);
    CHECK (! conv ("mod01.source", 3.0).bound);
}

TEST (fx_order_is_always_a_permutation)
{
    int order[kNumLegacyFxSlots];

    // a valid permutation is kept
    const float natural[kNumLegacyFxSlots] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    resolveFxOrder (natural, order);
    for (int i = 0; i < kNumLegacyFxSlots; ++i)
        CHECK_EQ (order[i], i);
    const float reversed[kNumLegacyFxSlots] = { 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    resolveFxOrder (reversed, order);
    for (int i = 0; i < kNumLegacyFxSlots; ++i)
        CHECK_EQ (order[i], 8 - i);

    // duplicates: the first position keeps the effect, later ones take the lowest free effect
    const float dup[kNumLegacyFxSlots] = { 5, 5, 5, 0, 0, 0, 8, 8, 8 };
    resolveFxOrder (dup, order);
    const int expected[kNumLegacyFxSlots] = { 5, 0, 1, 2, 3, 4, 8, 6, 7 };
    for (int i = 0; i < kNumLegacyFxSlots; ++i)
        CHECK_EQ (order[i], expected[i]);

    // anything at all gives a permutation, deterministically
    sbtest::Rng rng (3);
    for (int trial = 0; trial < 5000; ++trial)
    {
        float choices[kNumLegacyFxSlots];
        for (auto& c : choices)
            c = trial % 7 == 0 ? static_cast<float> (rng.u32() % 20) - 5.0f : static_cast<float> (rng.u32() % kNumLegacyFxSlots);
        int a[kNumLegacyFxSlots], b[kNumLegacyFxSlots];
        resolveFxOrder (choices, a);
        resolveFxOrder (choices, b);
        std::set<int> seen (a, a + kNumLegacyFxSlots);
        CHECK_EQ (seen.size(), static_cast<size_t> (kNumLegacyFxSlots));
        CHECK (*seen.begin() == 0 && *seen.rbegin() == kNumLegacyFxSlots - 1);
        CHECK (std::memcmp (a, b, sizeof a) == 0);
    }
}

TEST (parameters_reach_the_controls_of_the_engine)
{
    // every bound parameter at min / default / max / middle goes through setParam -> process -> the engine's control
    auto e = preparedEngine();
    CHECK (isVitalEngineConfigured (*e));
    long checked = 0;
    for (const auto& b : vitalBindings())
    {
        const auto& info = paramTable()[b.param];
        const auto range = vitalControlRange (b.control);
        if (b.gateParam >= 0) // the envelope time unit this binding belongs to
            e->setParam (paramTable()[b.gateParam].id, static_cast<float> (b.gateValue));
        for (const float plain : { info.min, info.def, info.max, fromNormalised (info, 0.5f) })
        {
            e->setParam (info.id, plain);
            applyParams (*e);
            double want = convertParam (b, static_cast<double> (plain), kSampleRate);
            if (b.cap == Cap::voices || b.cap == Cap::unison)
                want = std::min (want, b.cap == Cap::voices ? 32.0 : 16.0); // the quality of this engine allows all
            if (b.cap == Cap::fx)
                want = want > 0.5 ? 1.0 : 0.0;
            want = std::clamp (want, range.min, range.max);
            const double got = vitalControlValue (*e, b.control.c_str());
            ++checked;
            if (! CHECK_NEAR (got, want, 1e-5 * std::max (1.0, std::fabs (want))))
                std::printf ("    %s = %.9g: control %s is %.9g, expected %.9g\n", info.key, static_cast<double> (plain), b.control.c_str(), got, want);
        }
        e->setParam (info.id, info.def);
        if (b.gateParam >= 0)
            e->setParam (paramTable()[b.gateParam].id, paramTable()[b.gateParam].def);
    }
    std::printf ("    %ld parameter values applied and read back\n", checked);
}

TEST (modulatable_parameters_are_modulation_destinations_of_the_engine)
{
    auto e = preparedEngine();
    size_t destinations = 0, missing = 0;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const bool flagged = (paramTable()[i].flags & kParamModulatable) != 0;
        const bool exists = vitalHasModulationDestination (*e, i);
        if (flagged && i >= kNumKeysV2)
        {
            // the destinations of state version 3 exist for the keys whose capability bits the engine reports
            CHECK_EQ (exists, capsCover (e->capabilities(), requiredCaps (i)));
        }
        else if (flagged)
        {
            ++destinations;
            if (! CHECK (exists))
            {
                ++missing;
                std::printf ("    %s is flagged modulatable but the engine has no destination for it\n", paramTable()[i].key);
            }
        }
        else
        {
            CHECK (! exists);
        }
    }
    CHECK_EQ (destinations, static_cast<size_t> (269));
    CHECK_EQ (modDestinationCount(), static_cast<size_t> (kNumModDestinations - 1));
    CHECK_EQ (missing, 0u);
}

TEST (quality_tiers_cap_without_touching_the_parameters)
{
    for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
    {
        const Quality q = qualityForTier (tier);
        auto e = createVitalEngine();
        // a patch that asks for more than the tier gives: 32 voices, 16 unison voices on every oscillator, all effects on
        e->setParam (kParamMasterPolyphony, 32.0f);
        for (const char* key : { "osc1.unison.voices", "osc2.unison.voices", "osc3.unison.voices" })
            e->setParam (findParamByKey (key)->id, 16.0f);
        for (const char* key : { "chorus.on", "compressor.on", "delay.on", "distortion.on", "eq.on", "fxfilter.on", "flanger.on", "phaser.on", "reverb.on" })
            e->setParam (findParamByKey (key)->id, 1.0f);
        e->prepare (kSampleRate, 256, q);
        applyParams (*e);

        // the engine runs with the capped values ...
        CHECK_EQ (static_cast<int> (vitalControlValue (*e, "polyphony")), q.maxVoices);
        for (int i = 1; i <= 3; ++i)
            CHECK_EQ (static_cast<int> (vitalControlValue (*e, ("osc_" + std::to_string (i) + "_unison_voices").c_str())), q.maxUnison);
        CHECK_EQ (static_cast<int> (vitalControlValue (*e, "oversampling")), q.oversampling >= 2 ? 1 : 0);
        CHECK_EQ (static_cast<int> (vitalControlValue (*e, "osc_1_smooth_interpolation")), q.interpolation == Interpolation::spectral ? 1 : 0);
        const char* const fxControls[kNumLegacyFxSlots] = { "chorus_on", "compressor_on", "delay_on", "distortion_on", "eq_on", "filter_fx_on", "flanger_on",
                                                      "phaser_on", "reverb_on" };
        for (int i = 0; i < kNumLegacyFxSlots; ++i)
            CHECK_EQ (static_cast<int> (vitalControlValue (*e, fxControls[i])), static_cast<int> ((q.fxMask >> i) & 1u));

        // ... and the stored values are what the preset asked for
        CHECK_EQ (e->getParam (kParamMasterPolyphony), 32.0f);
        CHECK_EQ (e->getParam (findParamByKey ("osc2.unison.voices")->id), 16.0f);
        CHECK_EQ (e->getParam (findParamByKey ("reverb.on")->id), 1.0f);
        CHECK_EQ (e->getParam (findParamByKey ("flanger.on")->id), 1.0f);
    }

    // a quality that arrives while playing (any thread, taken over at the start of the next block)
    auto e = createVitalEngine();
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->setParam (findParamByKey ("osc1.unison.voices")->id, 12.0f);
    e->setParam (findParamByKey ("reverb.on")->id, 1.0f);
    e->prepare (kSampleRate, 256, qualityForTier (Tier::high));
    applyParams (*e);
    CHECK_EQ (static_cast<int> (vitalControlValue (*e, "polyphony")), 32);
    CHECK_EQ (static_cast<int> (vitalControlValue (*e, "reverb_on")), 1);
    e->setQuality (qualityForTier (Tier::low));
    CHECK_EQ (static_cast<int> (vitalControlValue (*e, "polyphony")), 32); // not before the next block
    applyParams (*e);
    CHECK_EQ (static_cast<int> (vitalControlValue (*e, "polyphony")), 8);
    CHECK_EQ (static_cast<int> (vitalControlValue (*e, "osc_1_unison_voices")), 4);
    CHECK_EQ (static_cast<int> (vitalControlValue (*e, "reverb_on")), 0);
    CHECK_EQ (e->getParam (findParamByKey ("osc1.unison.voices")->id), 12.0f);
    e->setQuality (qualityForTier (Tier::high));
    applyParams (*e);
    CHECK_EQ (static_cast<int> (vitalControlValue (*e, "polyphony")), 32);
    CHECK_EQ (static_cast<int> (vitalControlValue (*e, "osc_1_unison_voices")), 12);
    CHECK_EQ (static_cast<int> (vitalControlValue (*e, "reverb_on")), 1);

    // the number of notes that really sound follows the cap
    auto low = createVitalEngine();
    low->setParam (kParamMasterPolyphony, 32.0f);
    low->prepare (kSampleRate, 256, qualityForTier (Tier::low));
    std::vector<Event> burst;
    for (int i = 0; i < 20; ++i)
        burst.push_back (noteOn (0, 40 + i));
    sbtest::render (*low, burst, 512, 512);
    CHECK (low->activeVoices() >= 8 && low->activeVoices() <= 8 + 4);
}

TEST (builtin_wavetables_are_clean_single_cycles)
{
    vitaltables::WavetableSet set;
    CHECK_EQ (set.size(), kNumBuiltinWavetables);
    std::vector<float> frame (2048);
    for (int table = 0; table < kNumBuiltinWavetables; ++table)
    {
        CHECK (set.numFrames (table) == 1 || set.numFrames (table) == vitaltables::kMorphFrames);
        for (int f = 0; f < set.numFrames (table); f += std::max (1, set.numFrames (table) / 8))
        {
            vitaltables::renderFrame (table, f, frame.data());
            double peak = 0.0, mean = 0.0, energy = 0.0;
            bool finite = true;
            for (const float v : frame)
            {
                finite = finite && std::isfinite (v);
                peak = std::max (peak, static_cast<double> (std::fabs (v)));
                mean += v;
                energy += static_cast<double> (v) * v;
            }
            mean /= 2048.0;
            if (! (CHECK (finite) && CHECK_NEAR (peak, 1.0, 1e-4) && CHECK_NEAR (mean, 0.0, 1e-4) && CHECK (energy > 10.0)))
                std::printf ("    table %d frame %d: peak %g mean %g energy %g\n", table, f, peak, mean, energy);
        }
    }

    // the entries the own engine knows by name: the sine is a sine, the saw rises
    vitaltables::renderFrame (kWavetableSine, 0, frame.data());
    double worst = 0.0;
    for (int n = 0; n < 2048; ++n)
        worst = std::max (worst, std::fabs (static_cast<double> (frame[static_cast<size_t> (n)]) - std::sin (6.283185307179586 * n / 2048.0)));
    CHECK (worst < 1e-4);
    vitaltables::renderFrame (kWavetableSaw, 0, frame.data());
    CHECK (frame[1000] > frame[200] && frame[1900] > frame[1000] - 0.01f); // a rising ramp (the jump is at the cycle's end)

    // the morphing tables really change over their frames
    for (const int table : { 5, 6, 7, 8, 9, 10, 11, 12 })
    {
        std::vector<float> first (2048), last (2048);
        vitaltables::renderFrame (table, 0, first.data());
        vitaltables::renderFrame (table, vitaltables::kMorphFrames - 1, last.data());
        double diff = 0.0;
        for (size_t n = 0; n < 2048; ++n)
            diff += std::fabs (static_cast<double> (first[n]) - last[n]);
        CHECK (diff / 2048.0 > 0.02);
    }
}

TEST (builtin_lfo_shapes_render_unipolar_curves)
{
    LineGenerator line;
    std::vector<std::vector<float>> shapes;
    for (int shape = 0; shape < kNumLfoShapes; ++shape)
    {
        vitaltables::applyLfoShape (line, shape);
        const float* buffer = line.getBuffer();
        std::vector<float> copy (buffer, buffer + line.resolution());
        float lo = 1.0f, hi = 0.0f;
        for (const float v : copy)
        {
            CHECK (std::isfinite (v));
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        CHECK (lo >= -1e-4f && hi <= 1.0001f);
        CHECK (hi - lo > 0.5f); // every shape swings
        shapes.push_back (copy);
    }
    // all shapes differ from each other
    for (size_t a = 0; a < shapes.size(); ++a)
        for (size_t b = a + 1; b < shapes.size(); ++b)
        {
            double diff = 0.0;
            for (size_t n = 0; n < shapes[a].size(); ++n)
                diff += std::fabs (static_cast<double> (shapes[a][n]) - shapes[b][n]);
            if (! CHECK (diff / static_cast<double> (shapes[a].size()) > 0.005))
                std::printf ("    shapes %zu and %zu are the same\n", a, b);
        }
    // the saw up rises, the saw down falls, the exp curves are convex, the square has two levels
    CHECK (shapes[2][100] < shapes[2][1000] && shapes[2][1000] < shapes[2][1900]);
    CHECK (shapes[3][100] > shapes[3][1000] && shapes[3][1000] > shapes[3][1900]);
    CHECK (shapes[9][1024] < 0.2f && shapes[9][2000] > 0.9f);   // exp up: slow start
    CHECK (shapes[10][24] > 0.9f && shapes[10][1024] < 0.2f);   // exp down
    CHECK (shapes[4][500] > 0.99f && shapes[4][1500] < 0.01f);  // square: high, then low
    CHECK (shapes[5][200] > 0.99f && shapes[5][800] < 0.01f);   // pulse 25 %
}

TEST (modulation_slots_are_wired_and_unwired)
{
    auto e = preparedEngine();
    CHECK_EQ (vitalConnectedSlots (*e), 0);
    const auto slot = modSlotParams (0);
    const auto set = [&] (int paramIndex, float v) { e->setParam (paramTable()[paramIndex].id, v); };
    const size_t cutoffChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (kParamFilter1Cutoff)));

    set (slot.source, 7.0f);                              // LFO 1
    applyParams (*e);
    CHECK_EQ (vitalConnectedSlots (*e), 0);               // no destination: nothing happens
    set (slot.destination, static_cast<float> (cutoffChoice));
    applyParams (*e);
    CHECK_EQ (vitalConnectedSlots (*e), 1);
    set (slot.destination, 0.0f);
    applyParams (*e);
    CHECK_EQ (vitalConnectedSlots (*e), 0);

    // every source can be wired to every kind of destination and unwired again, with all 64 slots on one destination
    for (int k = 0; k < kNumModSlots; ++k)
    {
        const auto s = modSlotParams (k);
        e->setParam (paramTable()[s.source].id, static_cast<float> (1 + k % kNumModSourcesV2));
        e->setParam (paramTable()[s.destination].id, static_cast<float> (cutoffChoice));
    }
    applyParams (*e);
    CHECK_EQ (vitalConnectedSlots (*e), kNumModSlots);
    for (int k = 0; k < kNumModSlots; ++k)
        e->setParam (paramTable()[modSlotParams (k).source].id, 0.0f);
    applyParams (*e);
    CHECK_EQ (vitalConnectedSlots (*e), 0);

    // loops: whatever the engine would close with a feedback node stays unwired; the number of wired slots never exceeds the
    // number of slots that have a source and a destination, and everything can be unwired again
    const size_t lfo2Rate = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("lfo2.rate")->id)));
    const size_t lfo1Rate = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("lfo1.rate")->id)));
    set (slot.source, 7.0f);
    set (slot.destination, static_cast<float> (lfo2Rate));
    const auto slot2 = modSlotParams (1);
    e->setParam (paramTable()[slot2.source].id, 8.0f);
    e->setParam (paramTable()[slot2.destination].id, static_cast<float> (lfo1Rate));
    applyParams (*e);
    CHECK (vitalConnectedSlots (*e) >= 1 && vitalConnectedSlots (*e) <= 2);
    set (slot.source, 0.0f);
    e->setParam (paramTable()[slot2.source].id, 0.0f);
    applyParams (*e);
    CHECK_EQ (vitalConnectedSlots (*e), 0);
}

TEST (morph_never_gives_nan)
{
    // regression: "Inharmonic Stretch" (type 4) at 92 % and more returned NaN (docs/UPSTREAM.md, spectral_morph.h); every type, amount,
    // spread and register is finite now
    auto e = createVitalEngine();
    e->prepare (kSampleRate, 128, Quality {});
    std::vector<float> l (128), r (128);
    for (int type = 1; type < 12; ++type)
        for (const float amount : { 0.0f, 50.0f, 90.0f, 93.0f, 100.0f })
            for (const float spread : { -100.0f, 100.0f })
                for (const int key : { 24, 60, 108 })
                {
                    e->setParam (findParamByKey ("osc1.morph.type")->id, static_cast<float> (type));
                    e->setParam (findParamByKey ("osc1.morph.amount")->id, amount);
                    e->setParam (findParamByKey ("osc1.morph.spread")->id, spread);
                    bool finite = true;
                    const Event on = noteOn (0, key);
                    for (int block = 0; block < 4; ++block)
                    {
                        e->process (block == 0 ? &on : nullptr, block == 0 ? 1 : 0, l.data(), r.data(), 128);
                        finite = finite && sbtest::allFinite (l) && sbtest::allFinite (r);
                    }
                    const Event off = sbtest::allNotesOff (0);
                    e->process (&off, 1, l.data(), r.data(), 128);
                    if (! CHECK (finite))
                        std::printf ("    type %d amount %g spread %g key %d\n", type, static_cast<double> (amount), static_cast<double> (spread), key);
                }
}

TEST (oscillators_that_modulate_each_other_stay_finite)
{
    // regression: osc 1 as FM / RM of osc 2 and osc 2 of osc 1 left both unprocessed (docs/UPSTREAM.md, producers_module.cpp): NaN from
    // the first block on. Every combination of the four FM / RM types of the three oscillators, all three on.
    auto e = createVitalEngine();
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (kSampleRate, 128, Quality {});
    std::vector<float> l (128), r (128);
    bool finite = true;
    for (int t1 = 0; t1 <= 10; t1 += 1)
        for (int t2 = 7; t2 <= 10; ++t2)
        {
            for (int i = 1; i <= 3; ++i)
                e->setParam (findParamByKey (("osc" + std::to_string (i) + ".on").c_str())->id, 1.0f);
            e->setParam (findParamByKey ("osc1.warp.type")->id, static_cast<float> (t1));
            e->setParam (findParamByKey ("osc2.warp.type")->id, static_cast<float> (t2));
            e->setParam (findParamByKey ("osc3.warp.type")->id, static_cast<float> (7 + (t1 + t2) % 4));
            e->setParam (findParamByKey ("osc2.transpose")->id, 24.0f);
            const Event on = noteOn (0, 48);
            for (int block = 0; block < 6; ++block)
            {
                e->process (block == 0 ? &on : nullptr, block == 0 ? 1 : 0, l.data(), r.data(), 128);
                finite = finite && sbtest::allFinite (l) && sbtest::allFinite (r);
            }
            const Event off = sbtest::allNotesOff (0);
            e->process (&off, 1, l.data(), r.data(), 128);
        }
    CHECK (finite);
}
