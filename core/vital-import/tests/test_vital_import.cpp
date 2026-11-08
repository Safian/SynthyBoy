// SynthyBoy - tests of the Vital preset importer (docs/ui-design/11-vital-preset-import.md Annex C).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// All fixtures are synthesised in code (fixtures.cpp); tests/vital-presets/ (the owner's real presets) is optional.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "fixtures.h"
#include "synthyboy/modulation.h"
#include "synthyboy/preset.h"
#include "sha256.h"
#include "synthyboy/vital_import.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_import_internal.h"

using namespace synthyboy;
using sbfx::Fixture;

namespace
{
ImportResult run (const std::string& json, ImportOptions opt = {})
{
    return importVitalPreset (json.data(), json.size(), opt);
}
ImportResult run (const Fixture& f, ImportOptions opt = {}) { return run (f.json(), std::move (opt)); }

float val (const ImportResult& r, const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? r.values.plain[findParamIndex (p->id)] : -12345.0f;
}

const ReportItem* findItem (const ImportResult& r, const std::string& vitalName, ItemKind kind)
{
    for (const auto& it : r.report.items)
        if (it.vitalName == vitalName && it.kind == kind)
            return &it;
    return nullptr;
}

bool hasReason (const ImportResult& r, Reason reason)
{
    for (const auto& it : r.report.items)
        if (it.reason == reason)
            return true;
    return false;
}

bool valuesInRange (const ImportResult& r)
{
    for (size_t i = 0; i < kNumKeysV2; ++i)
    {
        const float v = r.values.plain[i];
        if (! std::isfinite (v) || constrainValue (paramTable()[i], v) != v)
            return false;
    }
    return true;
}

std::string vitalSourceName (const char* ourKey)
{
    const std::string k = ourKey;
    if (k.compare (0, 3, "env") == 0) return "env_" + k.substr (3);
    if (k.compare (0, 3, "lfo") == 0) return "lfo_" + k.substr (3);
    if (k.compare (0, 4, "rlfo") == 0) return "random_" + k.substr (4);
    if (k.compare (0, 5, "macro") == 0) return "macro_control_" + k.substr (5);
    return k;
}

std::string controlOfKey (const char* key)
{
    size_t n = 0;
    const auto* rows = vitalimport::vitalRows (n);
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp (rows[i].key, key) == 0)
            return rows[i].control;
    return {};
}

bool isSpecialKey (const std::string& k)
{
    const auto ends = [&] (const char* s) { return k.size() >= std::strlen (s) && k.compare (k.size() - std::strlen (s), std::string::npos, s) == 0; };
    return ends (".wavetable") || ends (".shape") || k == "filter.routing" || k.compare (0, 9, "fx.order.") == 0 || k == "master.bend_range"
        || (k.compare (0, 3, "mod") == 0 && (ends (".source") || ends (".destination")));
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// table
// ---------------------------------------------------------------------------------------------------------------------
TEST (vi_table_vs_params)
{
    CHECK_EQ (vitalRowCount(), static_cast<size_t> (730));
    std::set<std::string> keys, controls;
    for (size_t i = 0; i < vitalRowCount(); ++i)
    {
        const ParamInfo* p = findParamByKey (vitalRowKey (i));
        CHECK (p != nullptr);
        CHECK (keys.insert (vitalRowKey (i)).second);
        CHECK (controls.insert (vitalRowControl (i)).second);
        CHECK (! isSpecialKey (vitalRowKey (i)));
    }
    // 730 + 22 specials + 128 mod source / destination = 880: nothing missing, nothing extra
    size_t special = 0, modSrcDst = 0;
    for (size_t i = 0; i < kNumKeysV2; ++i)
    {
        const std::string k = paramTable()[i].key;
        if (keys.count (k) != 0)
            continue;
        CHECK (isSpecialKey (k));
        if (k.compare (0, 3, "mod") == 0)
            ++modSrcDst;
        else
            ++special;
        // every modulatable key has a row (so every destination can be resolved)
        CHECK ((paramTable()[i].flags & kParamModulatable) == 0);
    }
    CHECK_EQ (special, static_cast<size_t> (22));
    CHECK_EQ (modSrcDst, static_cast<size_t> (128));
    CHECK_EQ (kNumKeysV2, static_cast<size_t> (880));
    size_t modulatable = 0;
    for (size_t i = 0; i < kNumKeysV2; ++i)
        if ((paramTable()[i].flags & kParamModulatable) != 0)
        {
            ++modulatable;
            CHECK (keys.count (paramTable()[i].key) == 1);
        }
    CHECK_EQ (modulatable, static_cast<size_t> (269));
}

TEST (vi_effect_order_decoder_bijection)
{
    std::vector<uint64_t> seen;
    seen.reserve (362880);
    for (int code = 0; code < 362880; ++code)
    {
        int order[9];
        CHECK (decodeVitalEffectOrder (code, order));
        uint64_t key = 0;
        unsigned mask = 0;
        for (int i = 0; i < 9; ++i)
        {
            key = key * 9 + static_cast<uint64_t> (order[i]);
            mask |= 1u << order[i];
        }
        if (mask != 0x1FFu)
        {
            CHECK (mask == 0x1FFu);
            return;
        }
        seen.push_back (key);
    }
    std::sort (seen.begin(), seen.end());
    CHECK (std::adjacent_find (seen.begin(), seen.end()) == seen.end());
    int order[9];
    CHECK (decodeVitalEffectOrder (0, order));
    for (int i = 0; i < 9; ++i)
        CHECK_EQ (order[i], i);
    for (double bad : { -1.0, 362880.0, 1.5, 1e9, std::nan ("") })
        CHECK (! decodeVitalEffectOrder (bad, order));
}

// ---------------------------------------------------------------------------------------------------------------------
// fixtures 1 .. 10
// ---------------------------------------------------------------------------------------------------------------------
TEST (vi_fixture_default_patch_round_trip)
{
    const auto r = run (sbfx::defaultPatch());
    CHECK (r.status == ImportStatus::ok);
    const ParamValues d = ParamValues::defaults();
    int bad = 0;
    for (size_t i = 0; i < kNumKeysV2; ++i)
        if (std::fabs (r.values.plain[i] - d.plain[i]) > 1e-4 * (paramTable()[i].max - paramTable()[i].min) + 1e-5)
        {
            ++bad;
            std::printf ("    differs: %s %g vs %g\n", paramTable()[i].key, r.values.plain[i], d.plain[i]);
        }
    CHECK_EQ (bad, 0);
    CHECK (valuesInRange (r));
    CHECK_EQ (r.report.numDropped, 0);
    CHECK_EQ (r.report.numApproximated, 0);
    CHECK (r.report.fidelity == Fidelity::exact);
    CHECK_EQ (r.report.numImported, static_cast<int> (r.report.imported.size()));
    CHECK (r.report.numImported >= 730 - 64 * 5); // the unused modulation slots are ignored
    CHECK_EQ (r.report.vitalUnknownKeys, 0);
    CHECK_EQ (r.meta.name, std::string ("Fixture"));
    CHECK_EQ (r.meta.author, std::string ("Tester"));
    CHECK_EQ (r.meta.category, std::string ("Bass"));
    CHECK (! r.categoryNeedsChoice);
    CHECK_EQ (r.report.vitalVersion, std::string ("1.5.5"));
}

TEST (vi_round_trip_random_patches)
{
    sbtest::Rng rng (2026);
    int failures = 0;
    for (int iter = 0; iter < 150; ++iter)
    {
        ParamValues v = ParamValues::defaults();
        for (size_t i = 0; i < kNumKeysV2; ++i)
        {
            const ParamInfo& p = paramTable()[i];
            if (isSpecialKey (p.key))
                continue;
            float x = p.min + static_cast<float> (rng.uniform()) * (p.max - p.min);
            if ((p.flags & kParamStepped) != 0)
                x = std::floor (x + 0.5f);
            if (const int lm = legacyChoiceMax (p.key); lm >= 0 && x > static_cast<float> (lm))
                x = static_cast<float> (lm);
            x = constrainValue (p, x);
            if (std::strcmp (p.key, "master.portamento.time") == 0 && x < 2.0f)
                x = 0.0f; // below 1 ms the file's floor means "off"
            v.plain[i] = x;
        }
        Fixture f;
        sbfx::writeValues (f, v);
        // modulation connections: the first k slots
        const int k = static_cast<int> (rng.u32() % 65);
        std::vector<std::pair<int, int>> chosen; // source choice, destination choice
        for (int s = 0; s < k; ++s)
        {
            const int src = 1 + static_cast<int> (rng.u32() % kNumModSourcesV2);
            const int dst = 1 + static_cast<int> (rng.u32() % 269u);
            f.mods.emplace_back (vitalSourceName (modSourceTable()[src - 1].key), controlOfKey (paramTable()[modDestinationParamIndex (static_cast<size_t> (dst))].key));
            const ModSlotParams sp = modSlotParams (s);
            v.plain[sp.source] = static_cast<float> (src);
            v.plain[sp.destination] = static_cast<float> (dst);
        }
        for (int s = k; s < kNumModSlots; ++s) // unused slots import as defaults
        {
            const ModSlotParams sp = modSlotParams (s);
            const ParamValues d = ParamValues::defaults();
            for (int idx : { sp.amount, sp.power, sp.bipolar, sp.stereo, sp.bypass })
                v.plain[idx] = d.plain[idx];
        }
        const int code = static_cast<int> (rng.u32() % 362880);
        int order[9];
        decodeVitalEffectOrder (code, order);
        f.set ("effect_chain_order", code);
        for (int i = 0; i < 9; ++i)
            v.plain[findParamIndex (findParamByKey (("fx.order." + std::to_string (i + 1)).c_str())->id)] = static_cast<float> (order[i]);
        const int routing = static_cast<int> (rng.u32() % 3);
        f.set ("filter_1_filter_input", routing == 2 ? 1 : 0);
        f.set ("filter_2_filter_input", routing == 1 ? 1 : 0);
        v.plain[findParamIndex (findParamByKey ("filter.routing")->id)] = static_cast<float> (routing);
        const int bend = static_cast<int> (rng.u32() % 25);
        f.set ("pitch_bend_range", bend);
        v.plain[findParamIndex (findParamByKey ("master.bend_range")->id)] = static_cast<float> (bend);

        const auto r = run (f);
        CHECK (r.status == ImportStatus::ok);
        if (r.status != ImportStatus::ok)
            return;
        for (size_t i = 0; i < kNumKeysV2; ++i)
        {
            const ParamInfo& p = paramTable()[i];
            if (std::fabs (r.values.plain[i] - v.plain[i]) > 2e-4 * (p.max - p.min) + 2e-5 * std::fabs (v.plain[i]))
            {
                if (failures++ < 10)
                    std::printf ("    iter %d: %s got %g expected %g\n", iter, p.key, r.values.plain[i], v.plain[i]);
            }
        }
        CHECK (valuesInRange (r));
    }
    CHECK_EQ (failures, 0);
}

TEST (vi_fixture_min_empty)
{
    const auto r = run (std::string ("{\"settings\":{}}"));
    CHECK (r.status == ImportStatus::ok);
    CHECK (r.report.numDefaulted > 400);
    CHECK_EQ (val (r, "osc1.on"), 1.0f); // Vital's default switch values
    CHECK_EQ (val (r, "osc2.on"), 0.0f);
    CHECK_EQ (val (r, "filter1.on"), 0.0f);
    CHECK_EQ (val (r, "chorus.on"), 0.0f);
    CHECK_EQ (val (r, "osc2.destination"), 1.0f);
    CHECK_EQ (val (r, "osc3.destination"), 3.0f);
    CHECK_EQ (val (r, "osc1.destination"), 0.0f);
    CHECK_EQ (val (r, "master.volume"), ParamValues::defaults().plain[findParamIndex (findParamByKey ("master.volume")->id)]);
    CHECK_EQ (r.meta.name, std::string ("Imported"));
    CHECK (valuesInRange (r));
    // the file stem replaces an empty name
    ImportOptions o;
    o.fallbackName = "my file";
    CHECK_EQ (run (std::string ("{\"settings\":{}}"), o).meta.name, std::string ("my file"));
}

TEST (vi_fixture_osc_all)
{
    Fixture f = sbfx::defaultPatch();
    f.set ("osc_1_level", 0.5);
    f.set ("osc_1_pan", 0.4);
    f.set ("osc_1_transpose", 7);
    f.set ("osc_1_tune", 0.25);
    f.set ("osc_1_wave_frame", 128);
    f.set ("osc_1_frame_spread", -64);
    f.set ("osc_1_phase", 0.5);
    f.set ("osc_1_random_phase", 0.3);
    f.set ("osc_1_unison_voices", 7);
    f.set ("osc_1_unison_detune", 2);
    f.set ("osc_1_detune_power", -2.5);
    f.set ("osc_1_stereo_spread", 0.5);
    f.set ("osc_1_unison_blend", 0.25);
    f.set ("osc_1_stack_style", 5);
    f.set ("osc_1_spectral_morph_type", 11);
    f.set ("osc_1_spectral_morph_amount", 0.6);
    f.set ("osc_1_spectral_morph_spread", 0.25);
    f.set ("osc_1_distortion_type", 3);
    f.set ("osc_1_distortion_amount", 0.75);
    f.set ("osc_1_distortion_phase", 0.25);
    f.set ("osc_1_distortion_spread", -0.25);
    f.set ("osc_1_destination", 4);
    const auto r = run (f);
    CHECK (r.status == ImportStatus::ok);
    CHECK_NEAR (val (r, "osc1.level"), 25.0, 1e-3);
    CHECK_NEAR (val (r, "osc1.pan"), 40.0, 1e-3);
    CHECK_EQ (val (r, "osc1.transpose"), 7.0f);
    CHECK_NEAR (val (r, "osc1.tune"), 25.0, 1e-3);
    CHECK_NEAR (val (r, "osc1.frame"), 50.0, 1e-3);
    CHECK_NEAR (val (r, "osc1.frame_spread"), -25.0, 1e-3);
    CHECK_NEAR (val (r, "osc1.phase"), 180.0, 1e-3);
    CHECK_NEAR (val (r, "osc1.random_phase"), 30.0, 1e-3);
    CHECK_EQ (val (r, "osc1.unison.voices"), 7.0f);
    CHECK_NEAR (val (r, "osc1.unison.detune"), 4.0, 1e-4);
    CHECK_NEAR (val (r, "osc1.unison.detune_curve"), -2.5, 1e-5);
    CHECK_NEAR (val (r, "osc1.unison.spread"), 50.0, 1e-3);
    CHECK_NEAR (val (r, "osc1.unison.blend"), 25.0, 1e-3);
    CHECK_EQ (val (r, "osc1.unison.stack"), 5.0f);
    CHECK_EQ (val (r, "osc1.morph.type"), 11.0f);
    CHECK_NEAR (val (r, "osc1.morph.amount"), 60.0, 1e-3);
    CHECK_NEAR (val (r, "osc1.morph.spread"), 50.0, 1e-3);
    CHECK_EQ (val (r, "osc1.warp.type"), 3.0f);
    CHECK_NEAR (val (r, "osc1.warp.amount"), 75.0, 1e-3);
    CHECK_NEAR (val (r, "osc1.warp.phase"), 90.0, 1e-3);
    CHECK_NEAR (val (r, "osc1.warp.spread"), -50.0, 1e-3);
    CHECK_EQ (val (r, "osc1.destination"), 4.0f);
    CHECK_EQ (r.report.numApproximated, 0);

    // engine indices 9 and 12 (from sample) have no counterpart: None + approximated; 10 and 11 shift down by one
    for (const auto& c : { std::make_pair (9, 0), std::make_pair (12, 0), std::make_pair (10, 9), std::make_pair (11, 10) })
    {
        f.set ("osc_2_distortion_type", c.first);
        const auto q = run (f);
        CHECK_EQ (val (q, "osc2.warp.type"), static_cast<float> (c.second));
        const bool flagged = findItem (q, "osc_2_distortion_type", ItemKind::approximated) != nullptr;
        CHECK_EQ (flagged, c.second == 0);
    }
    // destinations 5 .. 14 (send to one effect) become Effects only
    for (int d = 5; d <= 14; ++d)
    {
        f.set ("osc_3_destination", d);
        const auto q = run (f);
        CHECK_EQ (val (q, "osc3.destination"), 3.0f);
        CHECK (findItem (q, "osc_3_destination", ItemKind::approximated) != nullptr);
    }
    // frame position keeps the fraction of the table
    f.set ("osc_1_wave_frame", 400);
    const auto over = run (f);
    CHECK_EQ (val (over, "osc1.frame"), 100.0f);
    CHECK (findItem (over, "osc_1_wave_frame", ItemKind::approximated) != nullptr);
}

TEST (vi_fixture_filters_routing)
{
    Fixture f = sbfx::defaultPatch();
    f.set ("filter_1_model", 7);
    f.set ("filter_1_style", 3);
    f.set ("filter_1_cutoff", 60);
    f.set ("filter_1_blend", 1.0);
    f.set ("filter_1_keytrack", 0.5);
    f.set ("filter_2_cutoff", 136);   // above our maximum
    f.set ("filter_2_style", 7);      // above our last style
    f.set ("filter_fx_cutoff", 8);    // below our minimum (8.18 Hz)
    auto r = run (f);
    CHECK_EQ (val (r, "filter1.model"), 7.0f);
    CHECK_EQ (val (r, "filter1.style"), 3.0f);
    CHECK_NEAR (val (r, "filter1.cutoff"), 8.1757989156 * std::pow (2.0, 5.0), 0.01);
    CHECK_NEAR (val (r, "filter1.blend"), 50.0, 1e-3);
    CHECK_NEAR (val (r, "filter1.keytrack"), 50.0, 1e-3);
    CHECK_EQ (val (r, "filter2.cutoff"), 20000.0f);
    CHECK (findItem (r, "filter_2_cutoff", ItemKind::approximated) != nullptr);
    CHECK_EQ (val (r, "filter2.style"), 5.0f);
    CHECK (findItem (r, "filter_2_style", ItemKind::approximated) != nullptr);
    CHECK_EQ (val (r, "fxfilter.cutoff"), 20.0f);
    CHECK (r.report.fidelity == Fidelity::close);

    const struct { double a, b; float expect; bool loop; } routing[] = { { 0, 0, 0.0f, false }, { 0, 1, 1.0f, false }, { 1, 0, 2.0f, false }, { 1, 1, 0.0f, true } };
    for (const auto& rt : routing)
    {
        f.set ("filter_1_filter_input", rt.a);
        f.set ("filter_2_filter_input", rt.b);
        r = run (f);
        CHECK_EQ (val (r, "filter.routing"), rt.expect);
        CHECK_EQ (hasReason (r, Reason::enumRemapped) && findItem (r, "filter_1_filter_input,filter_2_filter_input", ItemKind::approximated) != nullptr, rt.loop);
    }

    // a pre 0.5.0 file: no osc_N_destination and no osc_3_*, the routing comes from filter_N_oscM_input
    Fixture old;
    old.version = "0.4.2";
    old.set ("osc_1_on", 1);
    old.set ("osc_2_on", 1);
    old.set ("filter_1_osc1_input", 1);
    old.set ("filter_2_osc1_input", 0);
    old.set ("filter_1_osc2_input", 1);
    old.set ("filter_2_osc2_input", 1);
    old.set ("filter_1_sample_input", 0);
    old.set ("filter_2_sample_input", 0);
    r = run (old);
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (val (r, "osc1.destination"), 0.0f);
    CHECK_EQ (val (r, "osc2.destination"), 2.0f);   // both filters
    CHECK_EQ (val (r, "osc3.on"), 0.0f);
    CHECK_EQ (val (r, "noise.destination"), 3.0f);  // (0, 0): effects only
    CHECK (hasReason (r, Reason::legacyMigration));
}

TEST (vi_fixture_env_lfo_ranges)
{
    Fixture f = sbfx::defaultPatch();
    f.set ("env_1_attack", 1.0);
    f.set ("env_1_decay", 2.37842);   // 32 s: above our 10 s
    f.set ("env_1_hold", 1.4142);
    f.set ("env_1_sustain", 0.5);
    f.set ("env_1_attack_power", -5);
    f.set ("env_2_release", 0.0);
    f.set ("lfo_1_frequency", 0.0);
    f.set ("lfo_2_frequency", 9.0);
    f.set ("lfo_3_frequency", -7.0);
    f.set ("lfo_1_sync", 3);
    f.set ("lfo_1_tempo", 12);
    f.set ("lfo_1_sync_type", 5);
    f.set ("lfo_1_phase", 0.25);
    f.set ("lfo_1_fade_time", 2.0);
    f.set ("lfo_1_stereo", -0.5);
    f.set ("random_1_style", 3);
    f.set ("random_2_frequency", 2.0);
    f.set ("macro_control_2", 0.75);
    const auto r = run (f);
    CHECK_NEAR (val (r, "env1.attack"), 1000.0, 0.01);
    CHECK_EQ (val (r, "env1.decay"), 10000.0f);
    CHECK (findItem (r, "env_1_decay", ItemKind::approximated) != nullptr);
    CHECK_NEAR (val (r, "env1.hold"), 4000.0, 0.5);
    CHECK_NEAR (val (r, "env1.sustain"), 50.0, 1e-3);
    CHECK_NEAR (val (r, "env1.attack_curve"), -5.0, 1e-5);
    CHECK_EQ (val (r, "env2.release"), 0.0f);
    CHECK_NEAR (val (r, "lfo1.rate"), 1.0, 1e-4);
    CHECK_EQ (val (r, "lfo2.rate"), 200.0f);
    CHECK (findItem (r, "lfo_2_frequency", ItemKind::approximated) != nullptr);
    CHECK_NEAR (val (r, "lfo3.rate"), 0.01, 1e-6); // 2^-7 = 0.0078 -> 0.01: below the reporting threshold
    CHECK (findItem (r, "lfo_3_frequency", ItemKind::approximated) == nullptr);
    CHECK_EQ (val (r, "lfo1.sync"), 3.0f);
    CHECK_EQ (val (r, "lfo1.division"), 12.0f);
    CHECK_EQ (val (r, "lfo1.sync_type"), 5.0f);
    CHECK_NEAR (val (r, "lfo1.phase"), 90.0, 1e-3);
    CHECK_NEAR (val (r, "lfo1.fade_in"), 2000.0, 1e-2);
    CHECK_NEAR (val (r, "lfo1.stereo"), -180.0, 1e-3);
    CHECK_EQ (val (r, "rlfo1.style"), 3.0f);
    CHECK_NEAR (val (r, "rlfo2.rate"), 4.0, 1e-4);
    CHECK_NEAR (val (r, "macro.2"), 75.0, 1e-3);
}

TEST (vi_fixture_mod_matrix)
{
    // 64 connections: every source, bipolar / stereo / bypass / power, unsupported source and destination, a duplicate pair
    Fixture f = sbfx::defaultPatch();
    for (int s = 0; s < kNumModSourcesV2; ++s)
        f.mods.emplace_back (vitalSourceName (modSourceTable()[s].key), "filter_1_cutoff");
    f.mods.emplace_back ("bogus_source", "filter_1_cutoff");            // 29: unsupported source
    f.mods.emplace_back ("lfo_1", "sub_level");                 // 30: no such destination
    f.mods.emplace_back ("lfo_1", "osc_1_detune_range");        // 31: not a key of ours
    f.mods.emplace_back ("env_1", "osc_1_level");               // 32
    f.mods.emplace_back ("env_1", "osc_1_level");               // 33: duplicate, kept
    while (f.mods.size() < 64)
        f.mods.emplace_back ("lfo_2", "reverb_dry_wet");
    f.modExtra["31"] = "\"line_mapping\":{\"num_points\":2}";
    f.set ("modulation_1_amount", 0.5);
    f.set ("modulation_1_power", -4);
    f.set ("modulation_1_bipolar", 1);
    f.set ("modulation_1_stereo", 1);
    f.set ("modulation_1_bypass", 1);
    f.set ("modulation_64_amount", -1.0);
    const auto r = run (f);
    CHECK (r.status == ImportStatus::ok);
    for (int s = 0; s < kNumModSourcesV2; ++s)
    {
        const ModSlotParams sp = modSlotParams (s);
        CHECK_EQ (r.values.plain[sp.source], static_cast<float> (s + 1));
        CHECK_EQ (r.values.plain[sp.destination], static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("filter1.cutoff")->id)))));
    }
    CHECK_NEAR (val (r, "mod01.amount"), 50.0, 1e-3);
    CHECK_NEAR (val (r, "mod01.power"), -4.0, 1e-5);
    CHECK_EQ (val (r, "mod01.bipolar"), 1.0f);
    CHECK_EQ (val (r, "mod01.stereo"), 1.0f);
    CHECK_EQ (val (r, "mod01.bypass"), 1.0f);
    CHECK_NEAR (val (r, "mod64.amount"), -100.0, 1e-3);
    // dropped connections keep their slot empty, the following ones keep their numbers
    CHECK_EQ (val (r, "mod29.source"), 0.0f);
    CHECK_EQ (val (r, "mod30.source"), 0.0f);
    CHECK_EQ (val (r, "mod31.source"), 0.0f);
    CHECK_EQ (val (r, "mod32.source"), static_cast<float> (1));
    CHECK_EQ (val (r, "mod33.source"), static_cast<float> (1));
    CHECK_EQ (val (r, "mod32.destination"), val (r, "mod33.destination"));
    CHECK (findItem (r, "modulations[28]", ItemKind::dropped) != nullptr);
    CHECK (findItem (r, "modulations[29]", ItemKind::dropped) != nullptr);
    CHECK (findItem (r, "modulations[30]", ItemKind::dropped) != nullptr);
    CHECK (hasReason (r, Reason::modSourceUnsupported));
    CHECK (hasReason (r, Reason::modDestinationUnsupported));
    CHECK (r.report.fidelity == Fidelity::degraded);
    // area carries the slot
    CHECK_EQ (findItem (r, "modulations[28]", ItemKind::dropped)->area, std::string ("mod29"));

    // a connection with a line map curve is kept linear and flagged
    Fixture g = sbfx::defaultPatch();
    g.mods.emplace_back ("lfo_1", "filter_1_cutoff");
    g.modExtra["0"] = "\"line_mapping\":{\"num_points\":2}";
    const auto lr = run (g);
    CHECK (findItem (lr, "modulations[0].line_mapping", ItemKind::approximated) != nullptr);
    CHECK_EQ (val (lr, "mod01.source"), 7.0f);

    // more than 64 connections: the rest is ignored with a report item
    Fixture many = sbfx::defaultPatch();
    for (int i = 0; i < 80; ++i)
        many.mods.emplace_back ("lfo_1", "filter_1_cutoff");
    const auto mr = run (many);
    CHECK (mr.status == ImportStatus::ok);
    CHECK (hasReason (mr, Reason::limitExceeded));

    // every one of the 269 destinations resolves, in groups of 64 connections
    size_t resolved = 0;
    std::vector<size_t> dests;
    for (size_t c = 1; c <= static_cast<size_t> (269); ++c)
        dests.push_back (c);
    for (size_t base = 0; base < dests.size(); base += 64)
    {
        Fixture d = sbfx::defaultPatch();
        for (size_t i = base; i < std::min (dests.size(), base + 64); ++i)
            d.mods.emplace_back ("lfo_1", controlOfKey (paramTable()[modDestinationParamIndex (dests[i])].key));
        const auto q = run (d);
        for (size_t i = base; i < std::min (dests.size(), base + 64); ++i)
            if (q.values.plain[modSlotParams (static_cast<int> (i - base)).destination] == static_cast<float> (dests[i]))
                ++resolved;
    }
    CHECK_EQ (resolved, static_cast<size_t> (269));
}

TEST (vi_fixture_effects_all)
{
    Fixture f = sbfx::defaultPatch();
    for (const char* fx : { "chorus", "compressor", "delay", "distortion", "eq", "flanger", "phaser", "reverb" })
        f.set (std::string (fx) + "_on", 1);
    f.set ("filter_fx_on", 1);
    f.set ("chorus_frequency", 0.0);
    f.set ("chorus_voices", 3);
    f.set ("delay_tempo", 7);
    f.set ("delay_frequency", 2.0);
    f.set ("delay_sync", 2);
    f.set ("delay_aux_sync", 1);
    f.set ("reverb_decay_time", 0.0);
    f.set ("reverb_delay", 0.1);
    f.set ("eq_band_resonance", 0.5);
    f.set ("flanger_dry_wet", 0.25);
    f.set ("distortion_type", 4);
    f.set ("compressor_band_upper_ratio", 0.5);
    auto r = run (f);
    for (const char* k : { "chorus.on", "compressor.on", "delay.on", "distortion.on", "eq.on", "flanger.on", "phaser.on", "reverb.on", "fxfilter.on" })
        CHECK_EQ (val (r, k), 1.0f);
    CHECK_NEAR (val (r, "chorus.rate"), 1.0, 1e-4);
    CHECK_EQ (val (r, "chorus.voices"), 3.0f);
    CHECK_EQ (val (r, "delay.division"), 3.0f);
    CHECK_NEAR (val (r, "delay.time"), 250.0, 0.01);
    CHECK_NEAR (val (r, "reverb.decay"), 1000.0, 0.1);
    CHECK_NEAR (val (r, "reverb.predelay"), 100.0, 1e-3);
    CHECK_NEAR (val (r, "eq.mid.resonance"), 25.0, 1e-3);
    CHECK_NEAR (val (r, "flanger.mix"), 50.0, 1e-3);
    CHECK_EQ (val (r, "distortion.type"), 4.0f);
    CHECK_NEAR (val (r, "compressor.mid.upper_ratio"), 50.0, 1e-3);
    CHECK (findItem (r, "delay_aux_sync", ItemKind::approximated) != nullptr);

    for (int code : { 0, 362879, 12345, 1, 40320, 99999 })
    {
        f.set ("effect_chain_order", code);
        r = run (f);
        int order[9];
        decodeVitalEffectOrder (code, order);
        for (int i = 0; i < 9; ++i)
            CHECK_EQ (val (r, ("fx.order." + std::to_string (i + 1)).c_str()), static_cast<float> (order[i]));
        CHECK (! hasReason (r, Reason::effectOrderInvalid));
    }
    for (double bad : { 1.5, -1.0, 1e9, 362880.0 })
    {
        f.set ("effect_chain_order", bad);
        r = run (f);
        CHECK (hasReason (r, Reason::effectOrderInvalid));
        for (int i = 0; i < 9; ++i)
            CHECK_EQ (val (r, ("fx.order." + std::to_string (i + 1)).c_str()), static_cast<float> (i)); // identity
    }
}

TEST (vi_fixture_voice_global)
{
    Fixture f = sbfx::defaultPatch();
    f.set ("volume", 5476.0);          // (74)^2 -> -6 dB
    f.set ("polyphony", 32);
    f.set ("legato", 1);
    f.set ("portamento_time", -10);
    f.set ("portamento_slope", 3);
    f.set ("portamento_force", 1);
    f.set ("pitch_bend_range", 48);
    f.set ("velocity_track", -0.5);
    f.set ("voice_transpose", -12);
    f.set ("voice_tune", 0.5);
    f.setRaw ("tuning", "{\"tuning_name\":\"Just\",\"data\":[1,2]}");
    f.tuning = "{\"tuning_name\":\"Just\",\"data\":[1,2]}";
    f.settings.erase ("tuning");
    f.set ("oversampling", 2);
    f.set ("bypass", 0);
    auto r = run (f);
    CHECK_NEAR (val (r, "master.volume"), -6.0, 1e-3);
    CHECK_EQ (val (r, "master.polyphony"), 32.0f);
    CHECK_EQ (val (r, "master.legato"), 1.0f);
    CHECK_EQ (val (r, "master.portamento.time"), 0.0f);
    CHECK_NEAR (val (r, "master.portamento.slope"), 3.0, 1e-5);
    CHECK_EQ (val (r, "master.portamento.always"), 1.0f);
    CHECK_EQ (val (r, "master.bend_range"), 24.0f);
    CHECK (findItem (r, "pitch_bend_range", ItemKind::approximated) != nullptr);
    CHECK_NEAR (val (r, "master.velocity_track"), -50.0, 1e-3);
    CHECK_EQ (val (r, "master.transpose"), -12.0f);
    CHECK_NEAR (val (r, "master.tune"), 50.0, 1e-3);
    CHECK (findItem (r, "tuning", ItemKind::dropped) != nullptr);
    CHECK (hasReason (r, Reason::tuningUnsupported));
    CHECK (findItem (r, "oversampling", ItemKind::ignored) != nullptr);

    f.set ("portamento_time", 4);       // 16 s above our 8 s
    f.set ("voice_amplitude", 0.5);     // -6.02 dB folded into the master volume
    r = run (f);
    CHECK_EQ (val (r, "master.portamento.time"), 8000.0f);
    CHECK (findItem (r, "portamento_time", ItemKind::approximated) != nullptr);
    CHECK_NEAR (val (r, "master.volume"), -6.0 + 20.0 * std::log10 (0.5), 1e-3);
    CHECK (findItem (r, "voice_amplitude", ItemKind::approximated) != nullptr);
    f.set ("volume", 0.0);
    f.set ("voice_amplitude", 1.0);
    CHECK_EQ (val (run (f), "master.volume"), -60.0f);
}

TEST (vi_fixture_custom_wavetable)
{
    using namespace sbfx;
    Fixture f = defaultPatch();
    // osc 1: Wave Source, 3 keyframes (time, cubic), 2 groups
    const std::string osc1 = "{\"name\":\"WS\",\"author\":\"Tester\",\"remove_all_dc\":true,\"full_normalize\":true,\"groups\":["
        "{\"components\":[" + component ("Wave Source", "\"interpolation\":0,\"interpolation_style\":2",
                                        { waveKeyframe (0, sineWave (1)), waveKeyframe (5, sineWave (2)), waveKeyframe (10, sineWave (3)) }) + "]},"
        "{\"components\":[" + component ("Wave Source", "\"interpolation_style\":1", { waveKeyframe (0, sineWave (5)), waveKeyframe (10, sineWave (7)) }) + "]}]}";
    // osc 2: Line Source + Wave Folder + Frequency Filter
    const std::string line = lineObject ({ { 0, 1 }, { 0.5, 0 }, { 1, 1 } }, { 0, 0, 0 }, false);
    const std::string osc2 = wavetable ("LS", {
        component ("Line Source", "\"num_points\":3", { "{\"position\":0,\"pull_power\":0.0,\"line\":" + line + "}", "{\"position\":4,\"pull_power\":1.0,\"line\":" + line + "}" }),
        component ("Wave Folder", "", { "{\"position\":0,\"fold_boost\":0.3}", "{\"position\":4,\"fold_boost\":0.6}" }),
        component ("Frequency Filter", "\"normalize\":true", { "{\"position\":0,\"cutoff\":0.6,\"shape\":0.5,\"style\":0}" }) });
    // osc 3: Audio File Source with a tiny PCM
    std::vector<int16_t> pcm (4096);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<int16_t> (std::lround (10000.0 * std::sin (2.0 * 3.14159265358979323846 * static_cast<double> (i) / 512.0)));
    std::vector<uint8_t> pcmBytes (pcm.size() * 2);
    for (size_t i = 0; i < pcm.size(); ++i)
    {
        pcmBytes[i * 2] = static_cast<uint8_t> (static_cast<uint16_t> (pcm[i]) & 0xFF);
        pcmBytes[i * 2 + 1] = static_cast<uint8_t> (static_cast<uint16_t> (pcm[i]) >> 8);
    }
    const std::string osc3 = wavetable ("AF", { component ("Audio File Source",
        "\"normalize_gain\":true,\"normalize_mult\":1.0,\"window_size\":512,\"fade_style\":1,\"phase_style\":0,\"random_seed\":3,\"audio_sample_rate\":44100,\"audio_file\":\""
            + b64 (pcmBytes.data(), pcmBytes.size()) + "\"",
        { "{\"position\":0,\"start_position\":0,\"window_fade\":1,\"window_size\":512}", "{\"position\":6,\"start_position\":1024,\"window_fade\":1,\"window_size\":512}" }) });
    f.wavetables = "[" + osc1 + "," + osc2 + "," + osc3 + "]";
    f.set ("osc_1_wave_frame", 5);   // frame 5 of an 11 frame table: 50 %
    f.set ("osc_2_wave_frame", 2);   // 5 frames: 50 %
    ImportOptions o;
    o.caps.customWavetables = true;
    const auto r = run (f, o);
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.wavetables.size(), static_cast<size_t> (3));
    if (r.wavetables.size() == 3)
    {
        CHECK_EQ (r.wavetables[0].numFrames, 11);
        CHECK_EQ (r.wavetables[1].numFrames, 5);
        CHECK_EQ (r.wavetables[2].numFrames, 7);
        CHECK_EQ (r.wavetables[0].frames.size(), static_cast<size_t> (11 * 2048));
        CHECK_EQ (r.wavetables[0].name, std::string ("WS"));
        CHECK_EQ (r.wavetables[0].sha256.size(), static_cast<size_t> (64));
        CHECK (r.wavetables[0].sha256 != r.wavetables[1].sha256);
        // full_normalize: the peak of the first table is 1
        int peak = 0;
        for (int16_t v : r.wavetables[0].frames)
            peak = std::max (peak, std::abs (static_cast<int> (v)));
        CHECK_EQ (peak, 32767);
    }
    CHECK_NEAR (val (r, "osc1.frame"), 50.0, 1e-3);
    CHECK_NEAR (val (r, "osc2.frame"), 50.0, 1e-3);
    CHECK_EQ (val (r, "osc1.wavetable"), static_cast<float> (kWavetablePresetTable));
    CHECK_EQ (val (r, "osc2.wavetable"), static_cast<float> (kWavetablePresetTable));
    CHECK_EQ (val (r, "osc3.wavetable"), static_cast<float> (kWavetablePresetTable));
    CHECK (findItem (r, "wavetables[0]", ItemKind::approximated) == nullptr);
    CHECK (! hasReason (r, Reason::nearestWavetable));
    // determinism: same bytes, same result
    const auto r2 = run (f, o);
    CHECK (r2.wavetables.size() == r.wavetables.size() && r2.wavetables[0].sha256 == r.wavetables[0].sha256 && r2.wavetables[2].sha256 == r.wavetables[2].sha256);
    CHECK_EQ (r2.report.toJson(), r.report.toJson());
    CHECK (std::memcmp (r.values.plain, r2.values.plain, sizeof r.values.plain) == 0);
    // without the capability nothing is stored but the nearest built-in is chosen
    const auto none = run (f);
    CHECK (none.wavetables.empty());
    CHECK (hasReason (none, Reason::nearestWavetable));
    CHECK (findItem (none, "wavetables[0]", ItemKind::approximated) != nullptr);
    CHECK (val (none, "osc1.wavetable") != static_cast<float> (kWavetablePresetTable));
    CHECK (valuesInRange (none));
}

TEST (vi_fixture_each_component_type)
{
    using namespace sbfx;
    const char* types[] = { "Wave Source", "Line Source", "Audio File Source", "Shepard Tone Source", "Phase Shift", "Wave Window",
                            "Frequency Filter", "Slew Limiter", "Wave Folder", "Wave Warp" };
    Fixture f = defaultPatch();
    // one table per type in separate presets: render must succeed and report no unsupported component
    for (int t = 0; t < 10; ++t)
    {
        std::vector<std::string> comps;
        comps.push_back (component ("Wave Source", "", { waveKeyframe (0, sineWave (2)), waveKeyframe (2, sineWave (3)) }));
        if (t == 1)
            comps.push_back (component ("Line Source", "", { "{\"position\":0,\"line\":" + lineObject ({ { 0, 0 }, { 1, 1 } }, { 0, 0 }, true) + "}" }));
        else if (t == 2)
        {
            std::vector<uint8_t> pcm (2048, 0x10);
            comps.push_back (component ("Audio File Source", "\"window_size\":256,\"fade_style\":2,\"phase_style\":2,\"audio_file\":\"" + b64 (pcm.data(), pcm.size()) + "\"",
                                        { "{\"position\":0,\"start_position\":0,\"window_fade\":0.5,\"window_size\":256}" }));
        }
        else if (t == 3)
            comps.push_back (component ("Shepard Tone Source", "", { "{\"position\":0}", "{\"position\":2}" }));
        else if (t != 0)
            comps.push_back (component (types[t], "", { "{\"position\":0,\"phase\":0.2,\"mix\":1,\"style\":1,\"cutoff\":0.4,\"shape\":0.5,\"up_run_rise\":0.5,\"down_run_rise\":0.5,"
                                                        "\"left_position\":0.2,\"right_position\":0.8,\"window_shape\":1,\"fold_boost\":0.5,\"horizontal_power\":1,\"vertical_power\":1,"
                                                        "\"horizontal_asymmetric\":true,\"vertical_asymmetric\":false}" }));
        f.wavetables = "[" + wavetable (types[t], comps) + "]";
        ImportOptions o;
        o.caps.customWavetables = true;
        const auto r = run (f, o);
        CHECK (r.status == ImportStatus::ok);
        CHECK (! hasReason (r, Reason::unsupportedComponent));
        CHECK_EQ (r.wavetables.size(), static_cast<size_t> (1));
        CHECK (valuesInRange (r));
    }
    // an unknown component type is skipped and reported
    f.wavetables = "[" + wavetable ("X", { component ("Wave Source", "", { waveKeyframe (0, sineWave (1)) }), component ("Quantum Foam", "", { "{\"position\":0}" }) }) + "]";
    const auto u = run (f);
    CHECK (u.status == ImportStatus::ok);
    CHECK (hasReason (u, Reason::unsupportedComponent));
    CHECK (u.report.fidelity == Fidelity::degraded);
    // a table that is a built-in sine is not stored and selects the built-in
    f.wavetables = "[" + wavetable ("Sine", { component ("Wave Source", "", { waveKeyframe (0, sineWave (1)) }) }) + "]";
    ImportOptions o;
    o.caps.customWavetables = true;
    const auto s = run (f, o);
    CHECK (s.wavetables.empty());
    CHECK_EQ (val (s, "osc1.wavetable"), static_cast<float> (kWavetableSine));
    CHECK (findItem (s, "wavetables[0]", ItemKind::approximated) == nullptr);
    // so is a naive saw (the band limited reference differs by the Gibbs ripple only)
    std::vector<float> saw (2048);
    for (int i = 0; i < 2048; ++i)
        saw[static_cast<size_t> (i)] = static_cast<float> (2.0 * i / 2048.0 - 1.0);
    f.wavetables = "[" + wavetable ("Saw", { component ("Wave Source", "", { waveKeyframe (0, saw) }) }) + "]";
    const auto sw = run (f, o);
    CHECK (sw.wavetables.empty());
    CHECK_EQ (val (sw, "osc1.wavetable"), static_cast<float> (kWavetableSaw));
    // a bare LFO-line object is accepted as a wavetable
    f.wavetables = "[" + lineObject ({ { 0, 1 }, { 1, 0 } }, { 0, 0 }, false) + "]";
    CHECK (run (f, o).status == ImportStatus::ok);
}

TEST (vi_fixture_custom_lfo)
{
    using namespace sbfx;
    Fixture f = defaultPatch();
    f.mods.emplace_back ("lfo_2", "filter_1_cutoff");   // LFO 2 and LFO 4 are used
    f.mods.emplace_back ("lfo_4", "filter_1_cutoff");
    std::string l;
    // 1: a sine-like triangle? use the exact built-in triangle line: value 0 -> 1 -> 0, stored as y = 1 - value
    const std::string tri = lineObject ({ { 0, 1 }, { 0.5, 0 }, { 1, 1 } }, { 0, 0, 0 }, false);
    const std::string saw = lineObject ({ { 0, 1 }, { 1, 0 } }, { 0, 0 }, false);               // value 0 -> 1 (saw up)
    const std::string custom = lineObject ({ { 0, 0.9 }, { 0.2, 0.1 }, { 0.4, 0.7 }, { 0.5, 0.3 }, { 0.7, 0.8 }, { 0.9, 0.2 }, { 1, 0.5 } },
                                           { 3, -3, 0, 2, 0, 1, 0 }, true);
    std::vector<std::pair<double, double>> hundred;
    std::vector<double> pw;
    for (int i = 0; i < 100; ++i)
    {
        hundred.emplace_back (i / 99.0, (i % 7) / 7.0);
        pw.push_back(0);
    }
    const std::string h100 = lineObject (hundred, pw, false);
    const std::string invalid = "{\"name\":\"bad\",\"num_points\":1,\"points\":[0,0],\"powers\":[0],\"smooth\":false}";
    const std::string shortArrays = "{\"name\":\"bad\",\"num_points\":5,\"points\":[0,0,1,1],\"powers\":[0,0,0,0,0],\"smooth\":false}";
    f.lfos = "[" + saw + "," + tri + "," + custom + "," + h100 + "," + invalid + "," + shortArrays + ",42,{}]";
    ImportOptions o;
    o.caps.customLfoShapes = true;
    const auto r = run (f, o);
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (val (r, "lfo1.shape"), 2.0f);   // Saw Up: built-in shortcut, nothing stored
    CHECK_EQ (val (r, "lfo2.shape"), 1.0f);   // Triangle
    CHECK_EQ (val (r, "lfo3.shape"), static_cast<float> (kLfoShapeCustom)); // Custom shape selected
    CHECK_EQ (val (r, "lfo4.shape"), static_cast<float> (kLfoShapeCustom)); // Custom shape selected
    CHECK (findItem (r, "lfos[1]", ItemKind::approximated) == nullptr);
    // Custom shapes are stored in lfoShapes; with caps.customLfoShapes they are selected as Custom (not approximated)
    CHECK_EQ (r.lfoShapes.size(), static_cast<size_t> (2));
    CHECK (findItem (r, "lfos[2]", ItemKind::approximated) == nullptr);
    CHECK (findItem (r, "lfos[3]", ItemKind::approximated) == nullptr);
    if (r.lfoShapes.size() == 2)
    {
        CHECK_EQ (r.lfoShapes[0].lfo, 3);
        CHECK_EQ (r.lfoShapes[0].points.size(), static_cast<size_t> (7));
        CHECK (r.lfoShapes[0].smooth);
        CHECK_EQ (r.lfoShapes[1].points.size(), static_cast<size_t> (100));
        CHECK_NEAR (r.lfoShapes[0].points[0][1], 0.1, 1e-6);   // value = 1 - y
        CHECK_NEAR (r.lfoShapes[0].points[0][2], 3.0, 1e-6);
    }
    // without caps.customLfoShapes, LFO 4 is approximated to nearest built-in
    const auto none = run (f);
    CHECK (none.lfoShapes.empty());
    CHECK (val (none, "lfo4.shape") != static_cast<float> (kLfoShapeCustom));
    CHECK (findItem (none, "lfos[3]", ItemKind::approximated) != nullptr);
    // invalid shapes are dropped alone: lfo 5, 6 are not used (ignored), the file is fine
    CHECK (findItem (r, "lfos[4]", ItemKind::ignored) != nullptr);
    CHECK_EQ (val (r, "lfo6.shape"), 3.0f);   // num_points 5 with 4 numbers: sanitised to 2 points = a falling line value, i.e. Saw Down
    CHECK (findItem (r, "lfos[6]", ItemKind::ignored) != nullptr);
    CHECK (valuesInRange (r));
    // the stand-alone .vitallfo
    const auto s = importVitalLfoFile (tri.data(), tri.size(), o);
    CHECK (s.status == ImportStatus::ok);
    CHECK_EQ (s.lfoShapes.size(), static_cast<size_t> (1));
    const auto bad = importVitalLfoFile (invalid.data(), invalid.size(), o);
    CHECK (bad.status == ImportStatus::notVital);
}

TEST (vi_fixture_sample_sub_metadata)
{
    using namespace sbfx;
    Fixture f = defaultPatch();
    f.set ("sample_on", 1);
    f.set ("sub_on", 1);
    f.set ("sub_level", 0.8);
    f.sample = "{\"name\":\"My Sample\",\"length\":44100,\"sample_rate\":44100,\"samples\":\"AAAA\"}";
    auto r = run (f);
    CHECK_EQ (val (r, "noise.on"), 0.0f);
    CHECK (findItem (r, "sample", ItemKind::dropped) != nullptr);
    CHECK (hasReason (r, Reason::customSample));
    CHECK (findItem (r, "sub_on", ItemKind::dropped) != nullptr);
    CHECK (hasReason (r, Reason::subOscillator));
    CHECK (r.report.fidelity == Fidelity::degraded);
    // the default noise sample keeps noise.on
    f.sample = "{\"name\":\"White Noise\",\"length\":1,\"sample_rate\":44100,\"samples\":\"AAAA\"}";
    f.set ("sub_on", 0);
    r = run (f);
    CHECK_EQ (val (r, "noise.on"), 1.0f);
    CHECK (! hasReason (r, Reason::customSample));
    CHECK (! hasReason (r, Reason::subOscillator));

    // metadata
    f = defaultPatch();
    f.name = "\xF0\x9F\x8E\xB9 Keys \xC3\xA9";
    f.author = "Ann \"The\" Author";
    f.style = "Percussion";
    f.comments = std::string (1000, 'x') + "\nline two";
    r = run (f);
    CHECK_EQ (r.meta.name, f.name);
    CHECK_EQ (r.meta.author, f.author);
    CHECK_EQ (r.meta.category, std::string ("Pluck"));
    CHECK (r.meta.description.size() <= 255 && r.meta.description.size() > 240);
    CHECK (r.meta.description.find ('\n') == std::string::npos);
    CHECK (r.meta.tags.find ("vital-style:Percussion") != std::string::npos);
    CHECK (r.meta.tags.find ("src:" + r.sourceSha256.substr (0, 12)) != std::string::npos);
    CHECK (r.meta.tags.size() <= 255);
    CHECK_EQ (r.meta.license, std::string ("Imported, user-supplied; original terms apply"));
    for (const auto& st : { std::make_pair ("Bass", "Bass"), std::make_pair ("Lead", "Lead"), std::make_pair ("Pad", "Pad"), std::make_pair ("Keys", "Keys"),
                            std::make_pair ("Sequence", "Lead"), std::make_pair ("Experimental", "FX"), std::make_pair ("SFX", "FX") })
    {
        f.style = st.first;
        CHECK_EQ (run (f).meta.category, std::string (st.second));
        CHECK (! run (f).categoryNeedsChoice);
    }
    for (const char* unknown : { "Template", "", "Weird" })
    {
        f.style = unknown;
        const auto q = run (f);
        CHECK (q.categoryNeedsChoice);
        CHECK_EQ (q.meta.category, std::string ("Lead"));
    }
    // every meta string fits the .sbpreset meta parser (255 bytes), invalid UTF-8 is repaired, path-like names are kept verbatim as text
    f.name = std::string (600, 'n');
    f.author = "bad\xFF\xFE" "utf8 \xC0\xAF overlong";
    const std::string json = f.json();
    r = run (json);
    CHECK_EQ (r.meta.name.size(), static_cast<size_t> (255));
    CHECK (hasReason (r, Reason::truncated));
    CHECK (r.meta.author.find ("\xEF\xBF\xBD") != std::string::npos);
    PresetMeta back;
    const std::string text = savePreset (r.meta, r.values);
    CHECK (readPresetMeta (text.data(), text.size(), back));
    CHECK_EQ (back.name, r.meta.name);          // not dropped by the 255 byte rule of the meta parser
    CHECK_EQ (back.tags, r.meta.tags);
    CHECK_EQ (back.description, r.meta.description);
    // loadPreset of the imported file gives the same values (e)
    PresetMeta m2;
    ParamValues v2;
    const auto lr = loadPreset (text.data(), text.size(), m2, v2);
    CHECK (lr.status == LoadStatus::ok);
    for (size_t i = 0; i < kNumKeysV2; ++i)
        CHECK_NEAR (v2.plain[i], r.values.plain[i], 1e-4 * std::fabs (r.values.plain[i]) + 1e-5);
    f.name = "../../x";
    CHECK_EQ (run (f).meta.name, std::string ("../../x"));
}

// ---------------------------------------------------------------------------------------------------------------------
// parser, limits, robustness
// ---------------------------------------------------------------------------------------------------------------------
TEST (vi_tolerant_parsing_rules)
{
    // BOM and white space, last duplicate wins, unknown keys counted, numbers as strings / bools with a type mismatch
    std::string doc = "\xEF\xBB\xBF  \n{\"settings\":{\"osc_1_transpose\":3,\"osc_1_transpose\":5,\"mystery\":1,\"osc_2_transpose\":\"7\",\"osc_3_transpose\":true,"
                      "\"osc_1_tune\":null,\"osc_2_tune\":[1],\"osc_3_tune\":1e999}}  \n";
    auto r = run (doc);
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (val (r, "osc1.transpose"), 5.0f);
    CHECK_EQ (val (r, "osc2.transpose"), 7.0f);
    CHECK_EQ (val (r, "osc3.transpose"), 1.0f);
    CHECK (hasReason (r, Reason::typeMismatch));
    CHECK_EQ (r.report.vitalUnknownKeys, 1);
    CHECK_EQ (r.unknownKeys.size(), static_cast<size_t> (1));
    CHECK_EQ (val (r, "osc1.tune"), 0.0f);   // null, array, 1e999 = missing -> default
    CHECK_EQ (val (r, "osc3.tune"), 0.0f);
    // trailing data
    CHECK (run (std::string ("{\"settings\":{}} x")).status == ImportStatus::malformed);
    CHECK (run (std::string ("{\"settings\":{}}{\"settings\":{}}")).status == ImportStatus::malformed);
    // not Vital
    CHECK (run (std::string ("[1,2,3]")).status == ImportStatus::notVital);
    CHECK (run (std::string ("{\"foo\":1}")).status == ImportStatus::notVital);
    CHECK (run (std::string ("{\"name\":\"x\",\"groups\":[]}")).status == ImportStatus::notVital);
    CHECK (run (std::string ("{\"settings\":5}")).status == ImportStatus::notVital);
    // versions
    Fixture f = sbfx::defaultPatch();
    f.version = "9.9.9";
    CHECK (run (f).status == ImportStatus::ok);
    f.version = "1.5";
    r = run (f);
    CHECK (r.status == ImportStatus::ok);
    f.omitVersion = true;
    CHECK (run (f).status == ImportStatus::ok);
    f.omitVersion = false;
    f.top["synth_version"] = "12";
    CHECK (run (f).status == ImportStatus::ok);
    // the sniff
    const std::string good = sbfx::defaultPatch().json();
    CHECK (looksLikeVitalPreset (good.data(), good.size()));
    CHECK (! looksLikeVitalPreset ("[1]", 3));
    CHECK (! looksLikeVitalPreset ("{\"a\":1}", 7));
    CHECK (! looksLikeVitalPreset (nullptr, 0));
}

TEST (vi_limits_and_hostile_input)
{
    ImportOptions o;
    // file size
    {
        o.limits.maxFileBytes = 100;
        const std::string big = sbfx::defaultPatch().json();
        CHECK (run (big, o).status == ImportStatus::tooLarge);
        o = ImportOptions();
    }
    // nesting
    for (const char* open : { "[", "{\"a\":" })
    {
        std::string deep;
        for (int i = 0; i < 1000000; ++i)
            deep += open;
        const auto r = run (deep);
        CHECK (r.status == ImportStatus::malformed);
    }
    CHECK (run (std::string ("{\"settings\":{\"a\":[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]}}")).status == ImportStatus::malformed); // depth 17
    CHECK (run (std::string ("{\"settings\":{\"a\":[[[[[[[[[[[[[[1]]]]]]]]]]]]]]}}")).status == ImportStatus::ok);           // depth 16
    // working memory budget
    {
        std::string many = "{\"settings\":{\"modulations\":[";
        for (int i = 0; i < 20000; ++i)
            many += (i ? "," : "") + std::string ("{\"source\":\"lfo_1\",\"destination\":\"volume\"}");
        many += "]}}";
        o.limits.maxWorkingBytes = 100000;
        CHECK (run (many, o).status == ImportStatus::budgetExceeded);
        o = ImportOptions();
        const auto r = run (many, o);
        CHECK (r.status == ImportStatus::ok);   // 20000 entries: the first 64 are used
        CHECK (hasReason (r, Reason::limitExceeded));
    }
    // 100000 unknown settings
    {
        std::string unknown = "{\"settings\":{";
        for (int i = 0; i < 100000; ++i)
            unknown += (i ? "," : "") + std::string ("\"k") + std::to_string (i) + "\":1";
        unknown += "}}";
        const auto r = run (unknown);
        CHECK (r.status == ImportStatus::ok);
        CHECK_EQ (r.report.vitalUnknownKeys, 100000);
        CHECK_EQ (r.unknownKeys.size(), static_cast<size_t> (32));
    }
    // report items are bounded
    {
        Fixture f = sbfx::defaultPatch();
        for (int i = 0; i < 200; ++i)
            f.mods.emplace_back ("bogus_source", "volume");
        o.limits.maxReportItems = 10;
        const auto r = run (f, o);
        CHECK (r.status == ImportStatus::ok);
        CHECK (r.report.truncated);
        CHECK_EQ (r.report.items.size(), static_cast<size_t> (10));
        o = ImportOptions();
    }
    // numbers
    for (const char* n : { "1e999", "-1e999", "1e-999", "-0", "NaN", "Infinity", "0x10", "01", "+1", "1.", ".5", "--1" })
    {
        const std::string doc = std::string ("{\"settings\":{\"osc_1_transpose\":") + n + "}}";
        const auto r = run (doc);
        CHECK (r.status == ImportStatus::ok || r.status == ImportStatus::malformed);
        if (r.status == ImportStatus::ok)
            CHECK (valuesInRange (r));
    }
    {
        const auto r = run (std::string ("{\"settings\":{\"osc_1_transpose\":") + std::string (400, '9') + "}}");
        CHECK (r.status == ImportStatus::ok);
        CHECK (valuesInRange (r));
    }
    // strings: escapes, lone surrogates, NUL, 4 MB
    {
        const auto r = run (std::string ("{\"preset_name\":\"a\\u0000b\\ud800c\\udc00\\ud83d\\ude00\\n\\t\\\"\\\\\\/\",\"settings\":{}}"));
        CHECK (r.status == ImportStatus::ok);
        CHECK (r.meta.name.find ('\0') == std::string::npos);
        const auto big = run (std::string ("{\"preset_name\":\"") + std::string (4u << 20, 'z') + "\",\"settings\":{}}");
        CHECK (big.status == ImportStatus::ok);
        CHECK_EQ (big.meta.name.size(), static_cast<size_t> (255));
    }
    // base64 and content oddities
    {
        Fixture f = sbfx::defaultPatch();
        f.wavetables = "[{\"groups\":[{\"components\":[{\"type\":\"Wave Source\",\"keyframes\":[{\"position\":0,\"wave_data\":\"!!!notbase64\"},"
                       "{\"position\":-5,\"wave_data\":\"AAAA=\"},{\"position\":99999,\"wave_data\":\"AAAAAAAA\"},{\"position\":null},3]}]}]},5,\"x\",null]";
        const auto r = run (f);
        CHECK (r.status == ImportStatus::ok);
        CHECK (valuesInRange (r));
        f.wavetables = "[{\"groups\":[{\"components\":[{\"type\":\"Audio File Source\",\"window_size\":0,\"audio_sample_rate\":-1,\"audio_file\":\"\",\"keyframes\":"
                       "[{\"position\":0,\"window_size\":-1,\"start_position\":1e30}]},{\"type\":\"Audio File Source\",\"window_size\":2147483647,\"audio_sample_rate\":1e9,"
                       "\"audio_file\":\"AAA=\",\"keyframes\":[{\"position\":3}]}]}]}]";
        const auto q = run (f);
        CHECK (q.status == ImportStatus::ok);
        f.wavetables = "[{\"groups\":[{\"components\":[]}]},{\"groups\":[]},{\"groups\":5}]";
        CHECK (run (f).status == ImportStatus::ok);
        // 10 000 components: only 32 are used
        std::string comps;
        for (int i = 0; i < 10000; ++i)
            comps += (i ? "," : "") + std::string ("{\"type\":\"Wave Folder\",\"keyframes\":[{\"position\":0}]}");
        f.wavetables = "[{\"groups\":[{\"components\":[" + comps + "]}]}]";
        const auto c = run (f);
        CHECK (c.status == ImportStatus::ok);
        CHECK (hasReason (c, Reason::limitExceeded));
        // LFO oddities
        f.wavetables.clear();
        f.lfos = "[{\"num_points\":2147483647,\"points\":[0,0,1,1],\"powers\":[0,0]},{\"num_points\":-5,\"points\":[],\"powers\":[]},"
                 "{\"num_points\":3,\"points\":[1,1,0.5,0.2,0,0],\"powers\":[1e30,-1e30,0]},{\"num_points\":101,\"points\":[0,0],\"powers\":[]}]";
        const auto l = run (f);
        CHECK (l.status == ImportStatus::ok);
        CHECK (valuesInRange (l));
    }
    // wrong types everywhere
    {
        const auto r = run (std::string ("{\"synth_version\":5,\"preset_name\":[],\"author\":{},\"comments\":true,\"preset_style\":1,\"macro1\":2,"
                                         "\"settings\":{\"modulations\":{\"a\":1},\"wavetables\":\"x\",\"sample\":[],\"lfos\":7,\"tuning\":3,\"volume\":{},"
                                         "\"effect_chain_order\":\"abc\",\"pitch_bend_range\":[],\"osc_1_on\":\"1\"}}"));
        CHECK (r.status == ImportStatus::ok);
        CHECK (valuesInRange (r));
        const auto m = run (std::string ("{\"settings\":{\"modulations\":[1,\"x\",null,[],{\"source\":1,\"destination\":2},{\"source\":\"lfo_1\"},{}]}}"));
        CHECK (m.status == ImportStatus::ok);
        CHECK_EQ (m.report.numDropped, 7);
    }
    // empty and binary inputs
    for (const std::string& s : { std::string(), std::string (" \n\t "), std::string ("\x1f\x8b\x08\x00 gzip"), std::string ("\xFF\xFE{\0\"\0s\0", 9), std::string (4096, '\0') })
    {
        const auto r = run (s);
        CHECK (r.status != ImportStatus::ok);
    }
    CHECK (importVitalPreset (nullptr, 0, ImportOptions()).status != ImportStatus::ok);
}

TEST (vi_cancel_and_progress)
{
    using namespace sbfx;
    Fixture f = defaultPatch();
    f.wavetables = "[" + wavetable ("T", { component ("Wave Source", "", { waveKeyframe (0, sineWave (1)), waveKeyframe (200, sineWave (4)) }) }) + "]";
    ImportOptions o;
    int calls = 0;
    int lastPercent = -1;
    o.progress = [&] (int p, const char*) { ++calls; CHECK (p >= lastPercent); lastPercent = p; };
    CHECK (run (f, o).status == ImportStatus::ok);
    CHECK (calls >= 3);
    CHECK_EQ (lastPercent, 100);
    o = ImportOptions();
    o.cancel = [] { return true; };
    const auto r = run (f, o);
    CHECK (r.status == ImportStatus::cancelled);
    CHECK_EQ (r.report.items.size(), static_cast<size_t> (0)); // nothing is returned when cancelled
}

TEST (vi_report_json_and_determinism)
{
    using namespace sbfx;
    Fixture f = defaultPatch();
    f.set ("filter_1_cutoff", 135);
    f.mods.emplace_back ("bogus_source", "volume");
    f.name = "Quote \" and \\ and \n newline";
    const auto a = run (f), b = run (f);
    CHECK_EQ (a.report.toJson(), b.report.toJson());
    CHECK (std::memcmp (a.values.plain, b.values.plain, sizeof a.values.plain) == 0);
    CHECK_EQ (a.sourceSha256, b.sourceSha256);
    const std::string j = a.report.toJson();
    CHECK (j.find ("\"format\":\"synthyboy-import-report\"") != std::string::npos);
    CHECK (j.find ("\"fidelity\":\"degraded\"") != std::string::npos);
    // the report is valid JSON: parse it with the importer's own reader
    vitalimport::json::Doc doc;
    CHECK (vitalimport::json::parse (j.data(), j.size(), 16, 1u << 24, doc) == vitalimport::json::Status::ok);
    CHECK_EQ (a.report.items.size(), static_cast<size_t> (a.report.numApproximated + a.report.numDropped + a.report.numIgnored));
    CHECK_EQ (a.report.imported.size(), static_cast<size_t> (a.report.numImported));
}

TEST (vi_sha256_padding_boundaries)
{
    // the importer tags presets with a SHA-256: lengths 55 .. 64 mod 64 are the padding corner cases (known vectors)
    CHECK_EQ (Sha256::hashBufferHex ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56),
              std::string ("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK_EQ (Sha256::hashBufferHex ("abc", 3), std::string ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    for (size_t n = 50; n < 140; ++n) // no overflow, deterministic, different per length
    {
        const std::string s (n, 'q');
        CHECK_EQ (Sha256::hashBufferHex (s.data(), n).size(), static_cast<size_t> (64));
        CHECK (Sha256::hashBufferHex (s.data(), n) != Sha256::hashBufferHex (s.data(), n + 1));
    }
}

TEST (vi_clean_text)
{
    using vitalimport::cleanText;
    bool cut = false;
    CHECK_EQ (cleanText ("a\x01" "b\x7f" "c", 10, false), std::string ("abc"));
    CHECK_EQ (cleanText ("a\nb\tc", 10, true), std::string ("a b c"));
    CHECK_EQ (cleanText ("a\nb", 10, false), std::string ("ab"));
    CHECK_EQ (cleanText ("\xC3\xA9\xC3\xA9", 3, false, &cut), std::string ("\xC3\xA9")); // cut at a code point boundary
    CHECK (cut);
    CHECK_EQ (cleanText ("\xE2\x82", 10, false), std::string ("\xEF\xBF\xBD\xEF\xBF\xBD")); // truncated sequence
    CHECK_EQ (cleanText ("\xED\xA0\x80", 10, false), std::string ("\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD")); // encoded surrogate
    CHECK_EQ (cleanText ("\xF0\x9F\x8E\xB9", 10, false), std::string ("\xF0\x9F\x8E\xB9"));
}

// ---------------------------------------------------------------------------------------------------------------------
// fuzz
// ---------------------------------------------------------------------------------------------------------------------
TEST (vi_fuzz_random_mutations)
{
    using namespace sbfx;
    std::vector<std::string> seeds;
    seeds.push_back (defaultPatch().json());
    {
        Fixture f = defaultPatch();
        for (int i = 0; i < 64; ++i)
            f.mods.emplace_back ("lfo_1", "filter_1_cutoff");
        f.wavetables = "[" + wavetable ("T", { component ("Wave Source", "\"interpolation\":1,\"interpolation_style\":2", { waveKeyframe (0, sineWave (1)), waveKeyframe (3, sineWave (2)), waveKeyframe (6, sineWave (4)) }),
                                              component ("Wave Warp", "", { "{\"position\":0,\"horizontal_power\":2}" }) }) + "]";
        f.lfos = "[" + lineObject ({ { 0, 1 }, { 0.5, 0.2 }, { 1, 0 } }, { 1, -1, 0 }, true) + "]";
        f.sample = "{\"name\":\"White Noise\",\"length\":3,\"sample_rate\":44100,\"samples\":\"AAAA\"}";
        seeds.push_back (f.json());
    }
    sbtest::Rng rng (99);
    ImportOptions o;
    o.limits.deadlineMs = 5000;
    int accepted = 0, rejected = 0;
    for (int iter = 0; iter < 500; ++iter)
    {
        std::string s = seeds[rng.u32() % seeds.size()];
        const int ops = 1 + static_cast<int> (rng.u32() % 6);
        for (int k = 0; k < ops && ! s.empty(); ++k)
        {
            const size_t pos = rng.u32() % s.size();
            switch (rng.u32() % 7)
            {
                case 0: s[pos] = static_cast<char> (rng.u32()); break;                                  // byte
                case 1: s[pos] = static_cast<char> (s[pos] ^ (1 << (rng.u32() % 8))); break;            // bit
                case 2: s.erase (pos, 1 + rng.u32() % 20); break;                                       // delete
                case 3: s.insert (pos, 1, "{}[]\",:0-9.eE"[rng.u32() % 14]); break;                    // structure
                case 4: s.resize (pos); break;                                                          // truncate
                case 5: { const size_t n = std::min<size_t> (s.size() - pos, 1 + rng.u32() % 200); s.insert (pos, s.substr (pos, n)); break; } // duplicate
                default: s.insert (pos, rng.u32() % 2 ? "1e999" : "\"\\ud800\""); break;
            }
        }
        const auto r = run (s, o);
        if (r.status == ImportStatus::ok)
        {
            ++accepted;
            CHECK (valuesInRange (r));
            CHECK (r.meta.name.size() <= 255 && r.meta.author.size() <= 255 && r.meta.tags.size() <= 255 && r.meta.description.size() <= 255);
            CHECK (r.report.items.size() <= 2000);
        }
        else
            ++rejected;
    }
    std::printf ("    fuzz: %d accepted, %d rejected\n", accepted, rejected);
    CHECK (accepted > 20);
    CHECK (rejected > 20);
    // every prefix of a small seed (truncation at every offset)
    const std::string small = "{\"preset_name\":\"x\",\"settings\":{\"osc_1_level\":0.5,\"modulations\":[{\"source\":\"lfo_1\",\"destination\":\"volume\"}],\"lfos\":[{\"num_points\":2,\"points\":[0,0,1,1],\"powers\":[0,0]}]}}";
    for (size_t n = 0; n < small.size(); ++n)
        CHECK (run (small.substr (0, n), o).status != ImportStatus::ok);
    CHECK (run (small, o).status == ImportStatus::ok);
    // the truncated default patch (stride): never ok, never a crash
    for (size_t n = 0; n < seeds[0].size(); n += 97)
        CHECK (run (seeds[0].substr (0, n), o).status != ImportStatus::ok);
}

// ---------------------------------------------------------------------------------------------------------------------
// optional: the owner's real presets
// ---------------------------------------------------------------------------------------------------------------------
TEST (vi_real_presets_if_present)
{
#ifdef SB_VITAL_PRESETS_DIR
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir (SB_VITAL_PRESETS_DIR);
    if (! fs::is_directory (dir, ec))
    {
        std::printf ("    SKIP: %s does not exist (the owner's real presets are not in the repository)\n", SB_VITAL_PRESETS_DIR);
        return;
    }
    int files = 0, ok = 0;
    for (const auto& e : fs::recursive_directory_iterator (dir, ec))
    {
        if (! e.is_regular_file() || e.path().extension() != ".vital")
            continue;
        std::ifstream in (e.path(), std::ios::binary);
        const std::string bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        ++files;
        ImportOptions o;
        const std::string stem = e.path().stem().string();
        o.fallbackName = stem.c_str();
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = importVitalPreset (bytes.data(), bytes.size(), o);
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (s < 5.0);
        if (r.status == ImportStatus::ok)
        {
            ++ok;
            CHECK (valuesInRange (r));
        }
        else
            std::printf ("    %s: status %d (%s)\n", e.path().filename().string().c_str(), static_cast<int> (r.status), r.error.c_str());
        for (const auto& k : r.unknownKeys)
            std::printf ("    %s: unknown setting %s\n", e.path().filename().string().c_str(), k.c_str());
    }
    std::printf ("    real presets: %d files, %d imported\n", files, ok);
    CHECK_EQ (ok, files);
#else
    std::printf ("    SKIP: SB_VITAL_PRESETS_DIR not defined\n");
#endif
}

TEST (user_wavetable_store)
{
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "synthyboy_test_store";
    std::error_code ec;
    std::filesystem::remove_all (tempDir, ec);

    UserWavetableStore store (tempDir.string());
    ImportedWavetable wt;
    wt.osc = 1;
    wt.name = "My Test Table";
    wt.author = "Test Author";
    wt.numFrames = 2;
    wt.frames.resize (2 * 2048, 100);
    wt.sha256 = Sha256::hashBufferHex (wt.frames.data(), wt.frames.size() * sizeof (int16_t));

    std::string pack, file;
    CHECK (store.addWavetable (wt, pack, file));
    CHECK_EQ (pack, std::string ("user.imported-vital"));
    CHECK (! file.empty());
    CHECK (store.hasWavetable (wt.sha256));

    // Deduplication check
    std::string pack2, file2;
    CHECK (store.addWavetable (wt, pack2, file2));
    CHECK_EQ (pack2, pack);
    CHECK_EQ (file2, file);
    CHECK_EQ (store.packInfo().files.size(), static_cast<size_t> (1));

    // Verification
    std::vector<std::string> errors;
    CHECK (verifyContentPack (store.packDirectory(), store.packInfo(), errors));

    std::filesystem::remove_all (tempDir, ec);
}
