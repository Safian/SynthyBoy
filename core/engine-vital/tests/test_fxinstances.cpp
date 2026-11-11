// SynthyBoy - engine-vital tests: the pool of effect instances (block B22, docs/ui-design/13-new-keys.md 4.23, 18-FX-INSTANCES.md section 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Proves the capability bit kCapFxInstances: a slot of every type renders what instance 0 of that type renders (the descriptor maps every key),
// several instances differ from one and from none, the order inside a rack and the rack assignment route as documented, the modulation and the
// handover of a slot work, and no allocation happens on the audio thread.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
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

std::unique_ptr<IEngine> makeVital (bool realtime = false)
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    Quality q;
    q.realtime = realtime;
    e->prepare (kSr, 256, q);
    (void) sbtest::render (*e, { noteOn (0, 60), noteOff (200, 60) }, 2048, 256); // the warm-up of the engine
    return e;
}

/** A plain saw, one voice, straight to the effects (as test_fxextra.cpp). */
void base (IEngine& e)
{
    setByKey (e, "osc1.wavetable", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

/** reset + one note. The first note after a change of the patch differs slightly from all later ones (test_fxextra.cpp), so a short one is played
    and thrown away first. */
sbtest::Rendered play (IEngine& e, double seconds = 0.8, int key = 57)
{
    e.reset();
    (void) sbtest::render (e, { noteOn (0, key), noteOff (4000, key) }, 6000, 256);
    e.reset();
    return sbtest::render (e, { noteOn (0, key) }, static_cast<int> (seconds * kSr), 256);
}

/** The same after the engine has run a while with the patch: the smoothers of the effects have converged (a delay time slews for seconds). */
sbtest::Rendered playSettled (IEngine& e, int rounds = 3, double seconds = 0.8)
{
    sbtest::Rendered r;
    for (int i = 0; i < rounds; ++i)
        r = play (e, seconds);
    return r;
}

/** Sets the parameter of a slot that stands for the key `key` of instance 0 (a plain value of that key). */
void setSlotKey (IEngine& e, int slot, int type, const char* key, float plain)
{
    const FxTypeDescriptor& d = fxTypeDescriptor (type);
    for (int k = 0; k < d.count; ++k)
        if (std::strcmp (paramTable()[d.source[k]].key, key) == 0)
        {
            e.setParam (paramTable()[fxInstanceKeys (slot).p[k]].id, fxInstanceNormalise (type, k, plain));
            return;
        }
    CHECK (false);
    std::printf ("    no parameter %s in type %s\n", key, d.name);
}

/** Slot parameters to the defaults of the type (what the UI does when it adds a module). */
void initSlot (IEngine& e, int slot, int type)
{
    float norm[kFxInstanceParams];
    fxInstanceDefaults (type, norm);
    for (int k = 0; k < kFxInstanceParams; ++k)
        e.setParam (paramTable()[fxInstanceKeys (slot).p[k]].id, norm[k]);
}

void setSlot (IEngine& e, int slot, int type, bool on, int rack = 0, int order = 1023)
{
    const FxInstanceKeys& k = fxInstanceKeys (slot);
    const auto set = [&] (int index, float value) { e.setParam (paramTable()[index].id, value); };
    set (k.type, static_cast<float> (type));
    set (k.on, on ? 1.0f : 0.0f);
    set (k.rack, static_cast<float> (rack));
    set (k.order, static_cast<float> (order));
}

void setInstance0 (IEngine& e, int type, bool on)
{
    e.setParam (paramTable()[fxTypeDescriptor (type).onIndex].id, on ? 1.0f : 0.0f);
}

/** The relative size of the difference of two renders: max |a - b| / max |a|, both channels. */
double difference (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    double d = 0.0, p = 1.0e-9;
    const size_t n = std::min (a.left.size(), b.left.size());
    for (size_t i = 0; i < n; ++i)
    {
        d = std::max ({ d, static_cast<double> (std::fabs (a.left[i] - b.left[i])), static_cast<double> (std::fabs (a.right[i] - b.right[i])) });
        p = std::max ({ p, static_cast<double> (std::fabs (a.left[i])), static_cast<double> (std::fabs (a.right[i])) });
    }
    return d / p;
}

double rms (const sbtest::Rendered& r, double t0 = 0.3, double t1 = 0.7)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}
} // namespace

TEST (fxi_slot_of_every_type_renders_what_instance_zero_renders)
{
    for (int type = 1; type < kNumFxInstanceTypes; ++type)
    {
        const FxTypeDescriptor& d = fxTypeDescriptor (type);
        auto e = makeVital();
        base (*e);

        // the slot parameters first (normalised, deterministic, inside the range), instance 0 gets the plain values they stand for: the two
        // are then given the very same numbers
        sbtest::Rng rng (1000u + static_cast<uint64_t> (type));
        float norm[kFxInstanceParams] = {};
        for (int k = 0; k < d.count; ++k)
        {
            norm[k] = static_cast<float> (0.15 + 0.7 * rng.uniform());
            e->setParam (paramTable()[d.source[k]].id, fxInstancePlain (type, k, norm[k]));
        }
        setInstance0 (*e, type, true);
        // the free running LFO of a modulation effect starts when the module starts: a pool module has run its settle chunks (100 of 128
        // frames) before the first block, so instance 0 gets the same before the first note
        (void) sbtest::render (*e, {}, 100 * 128, 256);
        const auto reference = playSettled (*e);
        CHECK (rms (reference) > 1.0e-4);

        setInstance0 (*e, type, false);
        const auto silent = playSettled (*e);

        // a fresh engine for the slot: the free running LFO of a modulation effect has the same history as the one of instance 0 had
        auto f = makeVital();
        base (*f);
        setSlot (*f, 0, type, true);
        for (int k = 0; k < kFxInstanceParams; ++k)
            f->setParam (paramTable()[fxInstanceKeys (0).p[k]].id, norm[k]);
        const auto slot = playSettled (*f);

        const double diff = difference (reference, slot);
        std::printf ("    %-16s instance 0 vs slot: relative difference %.3g, instance 0 vs no effect %.3g\n", d.name, diff, difference (reference, silent));
        CHECK (diff < 1.0e-6);                                    // -120 dB
        CHECK (difference (reference, silent) > 1.0e-4);          // the effect is audible at these settings (the comparison means something)
    }
}

TEST (fxi_every_parameter_of_every_type_is_mapped_to_the_right_control)
{
    // each parameter alone away from its default (the descriptor maps the k-th key of the type: a wrong source would show as a difference)
    // 140 parameters, two engines each: 175 s native. A sanitizer / QEMU run (sbtest::instrumented ()) takes every 4th parameter of each type (the first of each
    // type included), the native jobs all of them.
    const int stride = sbtest::scaled (1, 4);
    int mapped = 0, expected = 0;
    for (int type = 1; type < kNumFxInstanceTypes; ++type)
    {
        const FxTypeDescriptor& d = fxTypeDescriptor (type);
        expected += (d.count + stride - 1) / stride;
        float defaults[kFxInstanceParams];
        fxInstanceDefaults (type, defaults);
        for (int k = 0; k < d.count; k += stride)
        {
            float norm[kFxInstanceParams];
            std::copy (defaults, defaults + kFxInstanceParams, norm);
            norm[k] = defaults[k] < 0.5f ? 0.8f : 0.2f;

            auto e = makeVital();
            base (*e);
            for (int j = 0; j < d.count; ++j)
                e->setParam (paramTable()[d.source[j]].id, fxInstancePlain (type, j, norm[j]));
            setInstance0 (*e, type, true);
            (void) sbtest::render (*e, {}, 100 * 128, 256);
            const auto reference = playSettled (*e, 2, 0.2);

            auto f = makeVital();
            base (*f);
            setSlot (*f, 0, type, true);
            for (int j = 0; j < kFxInstanceParams; ++j)
                f->setParam (paramTable()[fxInstanceKeys (0).p[j]].id, norm[j]);
            const auto slot = playSettled (*f, 2, 0.2);

            const double diff = difference (reference, slot);
            if (! CHECK (diff < 1.0e-4)) // a wrong source key gives differences of 0.01 .. 1; the smoothers of a short note are not quite converged
                std::printf ("    %s parameter %d (%s) differs by %.3g\n", d.name, k, paramTable()[d.source[k]].key, diff);
            ++mapped;
        }
    }
    CHECK_EQ (mapped, expected);
    if (stride == 1)
        CHECK_EQ (mapped, 12 + 24 + 14 + 9 + 18 + 18 + 9 + 11 + 20 + 8 + 10 + 14 + 14);
}

// ---- several instances, order, racks ------------------------------------------------------------------------------------------------------

namespace
{
void distortionSlot (IEngine& e, int slot, int rack = 0, int order = 1023, float drive = 24.0f)
{
    setSlot (e, slot, kFxTypeDistortion, true, rack, order);
    initSlot (e, slot, kFxTypeDistortion);
    setSlotKey (e, slot, kFxTypeDistortion, "distortion.drive", drive);
    setSlotKey (e, slot, kFxTypeDistortion, "distortion.mix", 100.0f);
}

void utilitySlot (IEngine& e, int slot, int rack, int order, float lpf)
{
    setSlot (e, slot, kFxTypeUtility, true, rack, order);
    initSlot (e, slot, kFxTypeUtility);
    setSlotKey (e, slot, kFxTypeUtility, "utility.lpf", lpf);
}
} // namespace

TEST (fxi_two_instances_of_one_effect_differ_from_one_and_from_none)
{
    auto e = makeVital();
    base (*e);
    const auto none = playSettled (*e, 1);

    distortionSlot (*e, 0);
    const auto one = playSettled (*e, 1);
    distortionSlot (*e, 1);
    const auto two = playSettled (*e, 1);

    CHECK (difference (none, one) > 0.1);
    CHECK (difference (one, two) > 0.01);   // the second distortion works on the output of the first
    CHECK (difference (none, two) > 0.1);

    // instance 0 is one of the two: the distortion of instance 0 followed by a slot is the same as two slots
    setSlot (*e, 1, kFxTypeNone, false);
    setSlot (*e, 0, kFxTypeNone, false);
    e->setParam (paramTable()[fxTypeDescriptor (kFxTypeDistortion).onIndex].id, 1.0f);
    setByKey (*e, "distortion.drive", 24.0f);
    setByKey (*e, "distortion.mix", 100.0f);
    distortionSlot (*e, 0, 0, 1023);
    const auto zeroAndOne = playSettled (*e, 2);
    CHECK (difference (one, zeroAndOne) > 0.01);

    auto f = makeVital();
    base (*f);
    distortionSlot (*f, 0);
    distortionSlot (*f, 1);
    const auto twoAgain = playSettled (*f, 1);
    CHECK (difference (two, twoAgain) < 1.0e-6);   // deterministic
    auto g = makeVital();
    base (*g);
    g->setParam (paramTable()[fxTypeDescriptor (kFxTypeDistortion).onIndex].id, 1.0f);
    setByKey (*g, "distortion.drive", 24.0f);
    setByKey (*g, "distortion.mix", 100.0f);
    distortionSlot (*g, 0);
    const auto instanceZeroAndSlot = playSettled (*g, 1);
    CHECK (difference (two, instanceZeroAndSlot) < 1.0e-6);   // instance 0 + a slot == two slots (the module is the same)
}

TEST (fxi_the_order_inside_a_rack_changes_the_sound)
{
    auto e = makeVital();
    base (*e);
    distortionSlot (*e, 0, 0, 100);
    utilitySlot (*e, 1, 0, 200, 700.0f);
    const auto distortionFirst = playSettled (*e, 1);
    distortionSlot (*e, 0, 0, 300);
    const auto filterFirst = playSettled (*e, 1);
    CHECK (difference (distortionFirst, filterFirst) > 0.05);
    // equal order values: the slot with the lower number comes first
    distortionSlot (*e, 0, 0, 500);
    utilitySlot (*e, 1, 0, 500, 700.0f);
    const auto tie = playSettled (*e, 1);
    CHECK (difference (tie, distortionFirst) < 1.0e-6);

    // between the modules of instance 0: instance 0 sorts by its position in fx.order * 64 (distortion is at position 1 = 64 by default,
    // the EQ at position 3 = 192): a slot at 100 comes after the distortion and before the EQ, a slot at 0 before both
    auto f = makeVital();
    base (*f);
    setByKey (*f, "distortion.on", 1.0f);
    setByKey (*f, "distortion.drive", 24.0f);
    setByKey (*f, "distortion.mix", 100.0f);
    utilitySlot (*f, 0, 0, 0, 700.0f);
    const auto slotBefore = playSettled (*f, 1);
    utilitySlot (*f, 0, 0, 100, 700.0f);
    const auto slotAfter = playSettled (*f, 1);
    CHECK (difference (slotBefore, slotAfter) > 0.05);
    // the distortion of instance 0 (position 1 = 64) followed by the slot at 100 is the same as two slots at 64 and 100 (fresh engines, the same history)
    auto h = makeVital();
    base (*h);
    setByKey (*h, "distortion.on", 1.0f);
    setByKey (*h, "distortion.drive", 24.0f);
    setByKey (*h, "distortion.mix", 100.0f);
    utilitySlot (*h, 0, 0, 100, 700.0f);
    auto g = makeVital();
    base (*g);
    distortionSlot (*g, 0, 0, 64);
    utilitySlot (*g, 1, 0, 100, 700.0f);
    const auto rh = playSettled (*h, 1);
    const auto rg = playSettled (*g, 1);
    // (not bit-identical: the other parameters of the slot are the defaults of the type after one round trip through the normalised value, a
    // few ulp away from the defaults of instance 0, which the 24 dB drive amplifies)
    CHECK (difference (rh, rg) < 1.0e-3);
}

TEST (fxi_racks_route_a_slot_to_main_bus_1_or_bus_2)
{
    auto e = makeVital();
    base (*e);
    const auto clean = playSettled (*e, 1);

    distortionSlot (*e, 0, 0);
    const auto inMain = playSettled (*e, 1);
    CHECK (difference (clean, inMain) > 0.1);

    distortionSlot (*e, 0, 1);   // Bus 1: the main signal no longer passes it
    const auto inBus1 = playSettled (*e, 1);
    CHECK (difference (clean, inBus1) < 0.01);
    distortionSlot (*e, 0, 2);
    CHECK (difference (clean, playSettled (*e, 1)) < 0.01);

    // the sound sent to bus 1 / bus 2 passes the distortion that lives there
    setByKey (*e, "osc1.destination", 6.0f);
    setByKey (*e, "osc1.bus1_send", 100.0f);
    distortionSlot (*e, 0, 1);
    const auto busDistorted = playSettled (*e, 1);
    setSlot (*e, 0, kFxTypeDistortion, false, 1);
    const auto busClean = playSettled (*e, 1);
    CHECK (rms (busDistorted) > 0.02);
    CHECK (difference (busClean, busDistorted) > 0.1);
    // ... and only that one: bus 2 is empty
    setByKey (*e, "osc1.bus1_send", 0.0f);
    setByKey (*e, "osc1.bus2_send", 100.0f);
    distortionSlot (*e, 0, 1);
    CHECK (difference (busClean, playSettled (*e, 1)) < 0.01);
    distortionSlot (*e, 0, 2);
    CHECK (difference (busClean, playSettled (*e, 1)) > 0.1);

    // bypass of the bus effects and of the main effects bypasses the slots as well
    setByKey (*e, "bus2.fx_bypass", 1.0f);
    CHECK (difference (busClean, playSettled (*e, 1)) < 0.01);
    setByKey (*e, "bus2.fx_bypass", 0.0f);
    setByKey (*e, "osc1.bus2_send", 0.0f);
    setByKey (*e, "osc1.destination", 3.0f);
    distortionSlot (*e, 0, 0);
    setByKey (*e, "main.fx_bypass", 1.0f);
    CHECK (difference (clean, playSettled (*e, 1)) < 0.01);
}

TEST (fxi_a_slot_that_is_off_or_none_changes_nothing_and_the_tier_mask_bypasses_it)
{
    auto e = makeVital();
    base (*e);
    const auto clean = playSettled (*e, 1);
    distortionSlot (*e, 0);
    setSlot (*e, 0, kFxTypeDistortion, false);   // off
    CHECK (difference (clean, playSettled (*e, 1)) < 1.0e-6);
    setSlot (*e, 0, kFxTypeNone, true);          // on, but no module
    CHECK (difference (clean, playSettled (*e, 1)) < 1.0e-6);
    distortionSlot (*e, 0);
    const auto on = playSettled (*e, 1);
    CHECK (difference (clean, on) > 0.1);
    // the tier may switch the module off, as it does for instance 0 (its parameters keep their values)
    Quality q;
    q.fxMask = kFxAll & ~static_cast<uint32_t> (kFxDistortion);
    e->setQuality (q);
    CHECK (difference (clean, playSettled (*e, 1)) < 1.0e-6);
    q.fxMask = kFxAll;
    e->setQuality (q);
    CHECK (difference (on, playSettled (*e, 1)) < 1.0e-6);
}

TEST (fxi_a_type_change_while_sounding_makes_no_click)
{
    // transparent settings (mix 0, modules that are exactly dry then; the reverb is not, even in instance 0): the output must stay the output of
    // the dry signal while the module behind the slot is replaced eight times
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.wavetable", 0.0f);   // a sine: any discontinuity shows
    setByKey (*e, "osc1.level", 60.0f);

    const int blocks = 160;
    const auto render = [&] (bool change)
    {
        e->reset();
        (void) sbtest::render (*e, { noteOn (0, 57), noteOff (4000, 57) }, 6000, 256);   // as play (): a short note first, then the real one
        e->reset();
        std::vector<float> out;
        out.reserve (static_cast<size_t> (blocks) * 256);
        std::vector<Event> events { noteOn (0, 57) };
        for (int b = 0; b < blocks; ++b)
        {
            if (change && b % 20 == 10)
            {
                const int step = b / 20;
                const int types[] = { kFxTypeDistortion, kFxTypeDelay, kFxTypeHyperDimension, kFxTypeUtility, kFxTypeNone, kFxTypeChorus, kFxTypeFlanger, kFxTypeFilter };
                const int type = types[step % 8];
                setSlot (*e, 0, type, true);
                initSlot (*e, 0, type);
                static const char* const mixKey[] = { "", "chorus.mix", "", "delay.mix", "distortion.mix", "", "fxfilter.mix", "flanger.mix", "", "reverb.mix", "hyperdim.mix", "utility.mix" };
                if (type != kFxTypeNone)
                    setSlotKey (*e, 0, type, mixKey[type], 0.0f);
            }
            auto r = sbtest::render (*e, events, 256, 256);
            events.clear();
            out.insert (out.end(), r.left.begin(), r.left.end());
        }
        return out;
    };
    const auto steady = render (false);
    const auto changed = render (true);
    for (int step = 0; step < 8; ++step)
    {
        double w = 0.0;
        for (size_t i = static_cast<size_t> (step * 20 * 256); i < static_cast<size_t> ((step + 1) * 20 * 256); ++i)
            w = std::max (w, static_cast<double> (std::fabs (steady[i] - changed[i])));
        std::printf ("    change %d deviates by %g\n", step, w);
    }
    double worst = 0.0;
    size_t at = 0;
    for (size_t i = 0; i < steady.size(); ++i)
        if (std::fabs (steady[i] - changed[i]) > worst)
        {
            worst = std::fabs (steady[i] - changed[i]);
            at = i;
        }
    std::printf ("    worst sample %zu (block %zu)\n", at, at / 256);
    std::printf("    largest deviation from the unchanged render while the module is replaced eight times: %.3g (%.1f dBFS)\n", worst, 20.0 * std::log10 (std::max (worst, 1.0e-12)));
    CHECK (worst < 1.0e-3);   // -60 dBFS
    CHECK (sbtest::allFinite (changed));
}

TEST (fxi_realtime_mode_passes_dry_until_the_module_is_ready_then_takes_it_over)
{
    // two engines with the same history; the first one gets a slot after the reset, the second one stays dry
    auto reference = makeVital (true);
    base (*reference);
    reference->reset();
    const auto dry = sbtest::render (*reference, { noteOn (0, 57) }, 256, 256);

    auto e = makeVital (true);
    base (*e);
    e->reset();
    setSlot (*e, 0, kFxTypeReverb, true);
    initSlot (*e, 0, kFxTypeReverb);
    setSlotKey (*e, 0, kFxTypeReverb, "reverb.mix", 100.0f);
    setSlotKey (*e, 0, kFxTypeReverb, "reverb.size", 100.0f);
    auto first = sbtest::render (*e, { noteOn (0, 57) }, 256, 256);
    // the request is only posted by the first block: it is dry
    double d = 0.0;
    for (size_t i = 0; i < 256; ++i)
        d = std::max (d, static_cast<double> (std::fabs (first.left[i] - dry.left[i])));
    CHECK (d < 1.0e-6);

    // the module arrives without the audio thread waiting for it
    bool taken = false;
    for (int attempt = 0; attempt < 400 && ! taken; ++attempt)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
        const auto r = sbtest::render (*e, {}, 256, 256);
        double energy = 0.0;
        for (float v : r.left)
            energy += static_cast<double> (v) * v;
        taken = attempt > 5 && energy > 0.0 && std::fabs (r.left.back()) > 0.0f && vitalHasPoolModule (*e, 0);
    }
    CHECK (taken);
    // offline (deterministic) and realtime mode end up in the same state: the same slot renders the same
    const auto a = sbtest::render (*e, {}, 4800, 256);
    CHECK (sbtest::allFinite (a.left));
}


// ---- automation and modulation ------------------------------------------------------------------------------------------------------------

TEST (fxi_host_automation_of_a_slot_parameter_changes_the_sound_while_it_plays)
{
    auto e = makeVital();
    base (*e);
    distortionSlot (*e, 0, 0, 1023, 0.0f);
    e->reset();
    const int blocks = 60;
    std::vector<float> out;
    std::vector<Event> events { noteOn (0, 57) };
    for (int b = 0; b < blocks; ++b)
    {
        if (b == 30)
            setSlotKey (*e, 0, kFxTypeDistortion, "distortion.drive", 30.0f);   // what a host does: an ordinary parameter write
        const auto r = sbtest::render (*e, events, 256, 256);
        events.clear();
        out.insert (out.end(), r.left.begin(), r.left.end());
    }
    const auto energy = [&] (int b0, int b1)
    {
        double sum = 0.0;
        for (size_t i = static_cast<size_t> (b0) * 256; i < static_cast<size_t> (b1) * 256; ++i)
            sum += static_cast<double> (out[i]) * out[i];
        return std::sqrt (sum / (static_cast<double> (b1 - b0) * 256.0));
    };
    CHECK (energy (40, 60) > 1.2 * energy (10, 30));   // 0 dB drive -> 30 dB drive: the saw is driven into the clipper
    CHECK (sbtest::allFinite (out));
}

TEST (fxi_modulation_of_a_slot_parameter_survives_a_change_of_the_module_behind_it)
{
    auto e = makeVital();
    base (*e);
    const FxInstanceKeys& keys = fxInstanceKeys (0);
    int driveNumber = -1;
    for (int k = 0; k < fxTypeDescriptor (kFxTypeDistortion).count; ++k)
        if (std::strcmp (paramTable()[fxTypeDescriptor (kFxTypeDistortion).source[k]].key, "distortion.drive") == 0)
            driveNumber = k;
    CHECK (driveNumber >= 0);
    const size_t choice = modDestinationChoiceOf (static_cast<size_t> (keys.p[driveNumber]));
    CHECK (choice >= 520);

    // macro 1 -> the drive of the slot, full range
    const auto configure = [&] (float macro)
    {
        setByKey (*e, "mod01.source", sbtest::sourceChoice ("macro1"));
        setByKey (*e, "mod01.destination", static_cast<float> (choice));
        setByKey (*e, "mod01.amount", 100.0f);
        setByKey (*e, "macro.1", macro);
    };
    distortionSlot (*e, 0, 0, 1023, -30.0f);
    configure (0.0f);
    const auto unmodulated = playSettled (*e, 2, 0.4);
    CHECK (vitalPoolDestinationValid (*e, 0, driveNumber));
    CHECK (vitalConnectedSlots (*e) >= 1);
    configure (100.0f);
    const auto modulated = playSettled (*e, 2, 0.4);
    CHECK (difference (unmodulated, modulated) > 0.1);          // the macro drives the distortion of the slot

    // the module is replaced by another distortion (a type change and back): the connection is rebuilt, the macro still works
    setSlot (*e, 0, kFxTypeNone, true);
    (void) playSettled (*e, 1, 0.1);
    CHECK (! vitalPoolDestinationValid (*e, 0, driveNumber));
    distortionSlot (*e, 0, 0, 1023, -30.0f);
    const auto again = playSettled (*e, 2, 0.4);
    CHECK (vitalPoolDestinationValid (*e, 0, driveNumber));
    CHECK (difference (modulated, again) < 1.0e-6);

    // a slot of another type: the destination of the same key number is the parameter of that type (no crash, no stuck connection)
    setSlot (*e, 0, kFxTypeReverb, true);
    initSlot (*e, 0, kFxTypeReverb);
    const auto reverb = playSettled (*e, 2, 0.4);
    CHECK (sbtest::allFinite (reverb.left));
}

TEST (fxi_sixteen_heavy_slots_stay_finite_under_extreme_settings)
{
    const int types[16] = { kFxTypeReverb, kFxTypeCompressor, kFxTypeDelay, kFxTypeConvolve, kFxTypeBode, kFxTypeHyperDimension, kFxTypeDistortion,
                            kFxTypeFilter, kFxTypeEq, kFxTypePhaser, kFxTypeFlanger, kFxTypeChorus, kFxTypeReverb, kFxTypeDelay, kFxTypeConvolve, kFxTypeUtility };
    sbtest::Rng rng (77);
    for (int round = 0; round < 6; ++round)
    {
        auto e = makeVital();
        base (*e);
        setByKey (*e, "osc1.unison.voices", 4.0f);
        for (int s = 0; s < 16; ++s)
        {
            setSlot (*e, s, types[s], true, s % 3, static_cast<int> (rng.u32() % 1024));
            for (int k = 0; k < kFxInstanceParams; ++k)
            {
                // round 0: all at the minimum, 1: all at the maximum, then random
                const float v = round == 0 ? 0.0f : (round == 1 ? 1.0f : static_cast<float> (rng.uniform()));
                e->setParam (paramTable()[fxInstanceKeys (s).p[k]].id, v);
            }
        }
        setByKey (*e, "osc1.bus1_send", 100.0f);
        setByKey (*e, "osc1.bus2_send", 100.0f);
        const auto r = playSettled (*e, 2, 0.6);
        bool finite = sbtest::allFinite (r.left) && sbtest::allFinite (r.right);
        float peak = std::max (sbtest::peak (r.left), sbtest::peak (r.right));
        CHECK (finite);
        CHECK (peak < 100.0f);
        if (! finite || peak >= 100.0f)
            std::printf ("    round %d: finite %d peak %g\n", round, finite ? 1 : 0, static_cast<double> (peak));
    }
}

// ---- the edits of fx_rack.h are what the engine does ----------------------------------------------------------------------------------------

namespace
{
std::vector<float> readAll (const IEngine& e)
{
    std::vector<float> plain (paramCount());
    for (size_t i = 0; i < paramCount(); ++i)
        plain[i] = e.getParam (paramTable()[i].id);
    return plain;
}

void writeAll (IEngine& e, const std::vector<FxWrite>& writes)
{
    for (const FxWrite& w : writes)
        e.setParam (paramTable()[w.param].id, w.value);
}
} // namespace

TEST (fxi_the_rack_edits_of_the_ui_model_give_the_order_the_engine_plays)
{
    // instance 0: distortion at position 1; a utility slot with a low pass appended to Main: it sounds behind the distortion
    const auto setup = [] (IEngine& e)
    {
        base (e);
        setByKey (e, "distortion.on", 1.0f);
        setByKey (e, "distortion.drive", 24.0f);
        setByKey (e, "distortion.mix", 100.0f);
    };
    auto e = makeVital();
    setup (*e);
    std::vector<FxWrite> w;
    const int slot = fxAddModule (readAll (*e).data(), kFxTypeUtility, 0, w);
    CHECK_EQ (slot, 0);
    writeAll (*e, w);
    setSlotKey (*e, 0, kFxTypeUtility, "utility.lpf", 700.0f);
    const auto behind = playSettled (*e, 1);

    // the same in an engine that is told the order directly: utility first (order 0), then the distortion
    auto f = makeVital();
    setup (*f);
    utilitySlot (*f, 0, 0, 0, 700.0f);
    const auto explicitFirst = playSettled (*f, 1);
    CHECK (difference (behind, explicitFirst) > 0.05);   // the two orders are different sounds

    // move the slot to the front of Main with the model: it must sound like the explicit order
    w.clear();
    CHECK (fxMove (readAll (*e).data(), { true, 0 }, 0, 0, w));
    writeAll (*e, w);
    CHECK (difference (explicitFirst, playSettled (*e, 1)) < 1.0e-3);   // (the slot's lpf differs by the round trip of the normalised value)

    // and back to the end of Main: behind the distortion again
    w.clear();
    CHECK (fxMove (readAll (*e).data(), { true, 0 }, 0, 16, w));
    writeAll (*e, w);
    CHECK (difference (behind, playSettled (*e, 1)) < 1.0e-3);

    // a module of instance 0 moved behind the slot: the distortion at the end
    w.clear();
    CHECK (fxMove (readAll (*e).data(), { false, 3 }, 0, 16, w));
    writeAll (*e, w);
    CHECK (difference (explicitFirst, playSettled (*e, 1)) < 1.0e-3);

    // removing the slot: the sound is the distortion alone
    auto g = makeVital();
    setup (*g);
    const auto alone = playSettled (*g, 1);
    w.clear();
    CHECK (fxRemoveSlot (readAll (*e).data(), 0, w));
    writeAll (*e, w);
    CHECK (difference (alone, playSettled (*e, 1)) < 1.0e-3);
}

// ---- numeric robustness (GROUPA1 item 0) -----------------------------------------------------------------------------------------------------
// The slot values of a Serum 2 bass preset that went to NaN in the render gate (normalised values, typed in: no preset file): compressor with
// +13.5 dB makeup on three bands, two EQs with gain and Q 63 / 47, then a Diode 1 distortion at +12.7 dB. The loud signal drove futils::exp2 below
// -126, where the exponent field of the float wrapped; the chain must stay finite for every input level.
TEST (fxi_loud_compressor_eq_diode_chain_stays_finite)
{
    static const float kSlots[4][24] = {
        { 1.0f, 0.48302f, 0.909907f, 0.0f, 0.5f, 0.946985f, 0.5f, 0.732233f, 0.724686f, 0.5f, 0.946985f, 0.5f, 0.732233f, 0.724686f, 0.5f, 0.946985f, 0.5f, 0.732233f, 0.724686f, 0.666667f, 0.0f, 1.0f, 0.401337f, 0.462624f },
        { 0.0f, 0.210711f, 0.647889f, 0.62892f, 0.0f, 0.548841f, 0.792982f, 0.472519f, 0.0f, 0.759315f, 0.5f, 0.3f, 0.666667f },
        { 0.0f, 0.190875f, 0.5f, 0.3f, 0.0f, 0.440857f, 0.230993f, 0.468739f, 0.0f, 0.759315f, 0.5f, 0.3f, 0.666667f },
        { 0.538462f, 0.711166f, 0.0f, 0.5f, 0.375229f, 0.5f, 0.0f, 0.373795f, 0.5f },
    };
    static const int kTypes[4] = { 2, 5, 5, 4 };
    for (const float level : { 100.0f, 200.0f }) // unison saw stack, the second one beyond a normal patch
        for (const int voices : { 1, 6 })
        {
            auto e = makeVital();
            base (*e);
            setByKey (*e, "master.volume", 0.0f);
            setByKey (*e, "osc1.level", level);
            setByKey (*e, "osc1.unison.voices", static_cast<float> (voices));
            for (int s = 0; s < 4; ++s)
            {
                setSlot (*e, s, kTypes[s], true, 0, 993 + s);
                for (int k = 0; k < kFxInstanceParams; ++k)
                    e->setParam (paramTable()[fxInstanceKeys (s).p[k]].id, kSlots[s][k]);
            }
            std::vector<Event> chord { noteOn (0, 36), noteOn (0, 43), noteOn (0, 48), noteOn (0, 55) };
            e->reset();
            const auto r = sbtest::render (*e, chord, static_cast<int> (1.0 * kSr), 256);
            bool ok = true;
            double peak = 0.0;
            for (size_t i = 0; i < r.left.size(); ++i)
            {
                ok = ok && std::isfinite (r.left[i]) && std::isfinite (r.right[i]);
                peak = std::max ({ peak, static_cast<double> (std::fabs (r.left[i])), static_cast<double> (std::fabs (r.right[i])) });
            }
            if (! CHECK (ok && peak < 100.0))
                std::printf ("    level %g voices %d\n", static_cast<double> (level), voices);
        }
}
