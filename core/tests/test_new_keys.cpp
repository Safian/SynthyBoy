// SynthyBoy - core tests: the 604 keys appended in state version 3 (docs/ui-design/13-new-keys.md, WP0): counts, append positions,
// the unchanged first 880 keys, choice appends, destination numbers, mod source extension, v2 audio, engines ignoring the new keys.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "golden_audio.h"
#include "golden_v2.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "test_engines.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
const ParamInfo& byKey (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    static const ParamInfo none {};
    return p != nullptr ? *p : none;
}

int positionOf (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? findParamIndex (p->id) : -1;
}

int destinationOf (const char* key)
{
    const int i = positionOf (key);
    return i < 0 ? -1 : static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (i)));
}

std::vector<std::string> namesOf (const char* key)
{
    std::vector<std::string> out;
    const auto& p = byKey (key);
    for (size_t i = 0; i < choiceCount (p); ++i)
        out.push_back (choiceName (p, i));
    return out;
}
} // namespace

TEST (counts_and_constants)
{
    CHECK_EQ (paramCount(), static_cast<size_t> (2636));
    CHECK_EQ (paramCount(), kNumKeysV2 + 604 + 23 + 37 + 14 + 18 + 18 + 2 + 7 + 448 + 6 + 4 + 2 + 62 + 9 + 93 + 42 + 30 + 320 + 9 + 3 + 5); // 5: B34 (REFCOMPARE round 3: eq.mid2.mode / cutoff / gain / resonance, eq.law), 3: B33 (REFCOMPARE: oscN.unison.law), 9: B32 (REMAINDER item 2: oscN.spec.flt_use / flt_table / flt_frame), 320: B31 (REMAINDER item 1: modNN.rise / fall / delay / delay_beats / delay_sync, 5 per slot), 30: B30 (GROUPC WP-13: oscN.multi.*, 10 per oscillator), 42: B29 (GROUPC WP-12: oscN.spec.*, 14 per oscillator), 93: B28 (GROUPC WP-11: oscN.gran.*, 31 per oscillator), 9: B27 (GROUPB WP-05: oscN.ratio_src, oscN.smooth_interp, oscN.sample.slice), 62: B26 (GROUPA2 WP-07: the voice control voicectl.*), 2: B25 (GROUPA1 WP-10: delay.offset_left, reverb.feedback), 4: B24 (GROUPA1 WP-09: master.mpe.xyz_macro, y_modwheel, bend_to_x, y_bipolar), 6: B23 (GROUPA1 WP-04: sub.phase, sub.phase_contiguous, noise.stereo, noise.filter, noise.type, noise.bounce), 23: block B16 (WTREMAP), 37: B14 (PITCHUNI), 14: B17 (fx-extras), 18: B18 (osc-extras), 18: B15 (voiceglb), 2: B20 (MODAMT), 7: B21 (WARPENV), 448: B22 (FXINST: 16 slots x 28 keys)
    CHECK (paramCount() <= kMaxParams);
    CHECK_EQ (kMaxParams, static_cast<size_t> (4096)); // raised for block B22 (FX instance pool)
    CHECK_EQ (kNumModSources, 57);
    CHECK_EQ (kNumModDestinations, 1249);
    CHECK_EQ (modSourceCount(), static_cast<size_t> (57));
    CHECK_EQ (modDestinationCount(), static_cast<size_t> (1248));
    CHECK_EQ (kNumFxSlots, 16);
    CHECK_EQ (kNumWavetableChoices, 15); // + "Saw Down" (GROUPB WP-18)
    CHECK_EQ (kLfoShapeCustom, 12);
    CHECK_EQ (kNumBuiltinWavetables, 13);
    CHECK_EQ (kNumLfoShapes, 12);

    size_t modulatable = 0, newModulatable = 0;
    for (size_t i = 0; i < paramCount(); ++i)
        if ((paramTable()[i].flags & kParamModulatable) != 0)
        {
            ++modulatable;
            newModulatable += i >= kNumKeysV2 ? 1 : 0;
        }
    CHECK_EQ (modulatable, static_cast<size_t> (453 + 384 + 3 + 2 + 3 + 51 + 21 + 3 + 256 + 3 + 3)); // 3: eq.mid2.cutoff / gain / resonance of B34, 3: oscN.spec.flt_frame of B32, 256: the modulatable rise / fall / delay / delay_beats of the 64 slots of B31, 3: oscN.multi.timbre of B30, 21: the 7 modulatable keys of oscN.spec.* of B29, 51: the 17 modulatable keys of oscN.gran.* of B28 // 2: delay.offset_left, reverb.feedback of B25 // 3: sub.phase, noise.stereo, noise.filter of B23
    CHECK_EQ (newModulatable, static_cast<size_t> (184 + 384 + 3 + 2 + 3 + 51 + 21 + 3 + 256 + 3 + 3)); // 134 + the 8 FX levels of B16 + the 12 of B14 + 8 of B17 + 10 of B18 + 11 of B15 + 1 of B20 (master.stereo_routing) + the 384 pool parameters of B22
}

TEST (keys_ids_and_names_are_unique_and_automatable)
{
    std::set<uint32_t> ids;
    std::set<std::string> keys, names;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const auto& p = paramTable()[i];
        CHECK (ids.insert (hashKey (p.key)).second);
        CHECK_EQ (p.id, hashKey (p.key));
        CHECK (keys.insert (p.key).second);
        CHECK (names.insert (p.name).second);
        CHECK ((p.flags & kParamAutomatable) != 0);
        CHECK ((p.flags & kParamHidden) == 0);
        CHECK_EQ ((p.scale == Scale::stepped), ((p.flags & kParamStepped) != 0));

        // a choice never repeats a name inside its own list (text -> value must be unambiguous)
        if (p.scale == Scale::stepped && p.choiceSet == ChoiceSet::list)
        {
            std::set<std::string> own;
            for (size_t k = 0; k < choiceCount (p); ++k)
                CHECK (own.insert (choiceName (p, k)).second);
        }
    }
    CHECK_EQ (ids.size(), static_cast<size_t> (2636));
}

TEST (append_positions_golden)
{
    // R2: the new keys sit at the end of the table in the block order B01 .. B12
    struct Pos { const char* key; int position; };
    const Pos expected[] = {
        { "reverb.chorus_rate", 879 },
        { "macro.5", 880 }, { "macro.8", 883 },
        { "lfo9.shape", 884 }, { "lfo10.keytrack_tune", 909 },
        { "lfo1.mono", 910 }, { "lfo10.host", 939 },
        { "env1.units", 940 }, { "env6.release_beats", 981 },
        { "curve.velocity.legato", 982 }, { "curve.note.legato", 983 },
        { "mod01.aux_source", 984 }, { "mod01.aux_output", 987 }, { "mod02.aux_source", 988 }, { "mod64.aux_output", 1239 },
        { "master.scale.key", 1240 }, { "master.mpe.bend_range", 1248 },
        { "osc1.map.key_low", 1249 }, { "osc3.map.key_warp", 1266 },
        { "osc1.type", 1267 }, { "osc1.sample.start", 1268 }, { "osc1.warp2.type", 1294 }, { "osc1.warp2.spread", 1297 },
        { "osc2.type", 1298 }, { "osc3.type", 1329 }, { "osc3.warp2.spread", 1359 },
        { "osc1.filter_blend", 1360 }, { "main.fx_bypass", 1385 },
        { "filter1.var", 1386 }, { "fxfilter.level", 1394 },
        { "fx.order.10", 1395 }, { "fx.order.16", 1401 }, { "chorus.rack", 1402 }, { "splitter_ms.rack", 1417 },
        { "reverb.force_enable", 1418 }, { "hyperdim.on", 1419 }, { "utility.on", 1428 }, { "bode.on", 1439 },
        { "convolve.on", 1454 }, { "splitter_lh.on", 1468 }, { "splitter_ms.level", 1483 },
        // B16 (WTREMAP): wavetable remap, FX levels, Clean Mode
        { "osc1.remap.on", 1484 }, { "osc3.remap.fall", 1495 }, { "chorus.level", 1496 }, { "reverb.level", 1503 },
        { "filter1.clean_mode", 1504 }, { "fxfilter.clean_mode", 1506 },
        // B14 (PITCHUNI): pitch modes, CRS, snap, unison laws, phase legato (behind B16)
        { "osc1.pitch_mode", 1507 }, { "osc1.phase_memory", 1518 },
        { "osc2.pitch_mode", 1519 }, { "osc2.phase_memory", 1530 },
        { "osc3.pitch_mode", 1531 }, { "osc3.phase_memory", 1542 },
        { "voice.phase_legato", 1543 },
        { "compressor.limit", 1544 }, { "reverb.diff_b", 1557 }, // B17
        { "sub.on", 1558 }, { "sub.bus2_send", 1568 }, // B18 (osc-extras)
        { "noise.sample", 1569 }, { "noise.loop", 1575 },
        // B15 (voiceglb): voice steal, poly legato, bend down, stereo spread, LFO swing, quality options (behind B18)
        { "master.voice.same_note", 1576 }, { "master.bend_down_link", 1580 }, { "master.stereo_spread", 1581 },
        { "lfo1.swing", 1582 }, { "lfo10.swing", 1591 },
        { "quality.oversample_4x", 1592 }, { "quality.no_smoothing", 1593 },
        { "master.stereo_routing", 1594 }, { "master.voice_override", 1595 }, // B20 (MODAMT)
        { "sub.env1", 1596 }, { "osc1.env1", 1597 }, { "osc3.env1", 1599 }, { "noise.env1", 1600 }, { "filter1.env1", 1601 }, { "filter2.env1", 1602 }, // B21 (WARPENV)
        // B22 (FXINST): 16 slots of 28 keys (type, on, rack, order, p01 .. p24) behind B21
        { "fxi01.type", 1603 }, { "fxi01.on", 1604 }, { "fxi01.rack", 1605 }, { "fxi01.order", 1606 }, { "fxi01.p01", 1607 }, { "fxi01.p24", 1630 },
        { "fxi02.type", 1631 }, { "fxi08.p10", 1603 + 7 * 28 + 4 + 9 }, { "fxi16.type", 1603 + 15 * 28 }, { "fxi16.p24", 2050 },
        // B23 (GROUPA1 WP-04)
        { "sub.phase", 2051 }, { "sub.phase_contiguous", 2052 }, { "noise.stereo", 2053 }, { "noise.filter", 2054 }, { "noise.type", 2055 }, { "noise.bounce", 2056 },
        // B24 (GROUPA1 WP-09)
        { "master.mpe.xyz_macro", 2057 }, { "master.mpe.y_modwheel", 2058 }, { "master.mpe.bend_to_x", 2059 }, { "master.mpe.y_bipolar", 2060 },
        // B25 (GROUPA1 WP-10)
        { "delay.offset_left", 2061 }, { "reverb.feedback", 2062 },
        // B26 (GROUPA2 WP-07)
        { "voicectl.length", 2063 }, { "voicectl.s1.pan", 2064 }, { "voicectl.s1.mod2", 2069 }, { "voicectl.s2.pan", 2070 }, { "voicectl.s8.mod2", 2111 },
        { "voicectl.rand.pan", 2112 }, { "voicectl.rand.detune10x", 2116 }, { "voicectl.scale.env", 2117 }, { "voicectl.scale.lfo", 2118 },
        { "voicectl.scale.lfo_mode", 2119 }, { "voicectl.osc.a", 2120 }, { "voicectl.osc.sub", 2124 },
        // B27 (GROUPB WP-05)
        { "osc1.ratio_src", 2125 }, { "osc1.smooth_interp", 2126 }, { "osc1.sample.slice", 2127 }, { "osc2.ratio_src", 2128 }, { "osc3.sample.slice", 2133 },
        // B28 (GROUPC WP-11): the granular oscillator, 31 keys per oscillator
        { "osc1.gran.window_amount", 2134 }, { "osc1.gran.window_skew", 2135 }, { "osc1.gran.density", 2138 }, { "osc1.gran.xy_y", 2164 }, { "osc2.gran.window_amount", 2165 }, { "osc3.gran.xy_y", 2226 },
        // B29 (GROUPC WP-12): the spectral oscillator, 14 keys per oscillator
        { "osc1.spec.freq_lo", 2227 }, { "osc1.spec.freq_hi", 2228 }, { "osc1.spec.cut", 2231 }, { "osc1.spec.xy_y", 2240 }, { "osc2.spec.freq_lo", 2241 }, { "osc3.spec.xy_y", 2268 },
        // B30 (GROUPC WP-13): the multisample oscillator, 10 keys per oscillator
        { "osc1.multi.override", 2269 }, { "osc1.multi.delay", 2270 }, { "osc1.multi.timbre", 2278 }, { "osc2.multi.override", 2279 }, { "osc3.multi.timbre", 2298 },
    };
    for (const auto& e : expected)
        if (! CHECK_EQ (positionOf (e.key), e.position))
            std::printf ("    %s\n", e.key);

    // the position is the order, and the ids are the hashes
    for (size_t i = 0; i < paramCount(); ++i)
        CHECK_EQ (static_cast<size_t> (paramTable()[i].order), i);
}

TEST (first_880_keys_are_byte_identical_to_state_version_2)
{
    // computed with sbgolden::fingerprint (880) on commit 9c97a69 (golden_v2.h)
    // REFCOMPARE round 3 (B34, 13-new-keys.md 4.48), deliberate: the plain ranges of eq.low / mid / high.gain (+-15 -> +-24 dB) and of the six compressor thresholds (-80 .. 0 -> -120 .. +12 dB)
    // grew (the values of a v2 state stay valid); the fingerprint of the first 880 keys includes the ranges: 0xd0dcc938695354b2 before
    CHECK_EQ (sbgolden::fingerprint (kNumKeysV2), 0x1eac094d78181286ull);
}

TEST (choice_lists_are_append_only)
{
    const auto head = [] (const std::vector<std::string>& names, size_t n) { return std::vector<std::string> (names.begin(), names.begin() + static_cast<long> (n)); };

    const auto wavetables = namesOf ("osc2.wavetable");
    CHECK_EQ (wavetables.size(), static_cast<size_t> (15));
    CHECK (wavetables[12] == "Bell Partials" && wavetables[13] == "Preset table" && wavetables[14] == "Saw Down");

    for (const char* key : { "osc1.destination", "osc3.destination", "noise.destination" })
    {
        const auto n = namesOf (key);
        CHECK_EQ (n.size(), static_cast<size_t> (7));
        CHECK (n[4] == "Direct out" && n[5] == "Filter blend" && n[6] == "None");
    }

    for (int i = 1; i <= 10; ++i)
    {
        const auto n = namesOf (("lfo" + std::to_string (i) + ".shape").c_str());
        CHECK_EQ (n.size(), static_cast<size_t> (13));
        CHECK (n[11] == "Half Sine" && n[12] == "Custom" && static_cast<int> (n.size()) == kNumLfoShapes + 1);
    }
    for (int i = 1; i <= 4; ++i)
    {
        const auto n = namesOf (("rlfo" + std::to_string (i) + ".style").c_str());
        CHECK_EQ (n.size(), static_cast<size_t> (5));
        CHECK (n[3] == "Lorenz attractor" && n[4] == "Rossler attractor");
    }

    const char* const models[] = { "Multi SVF", "Slope", "EQ 6 dB", "Ring Mod", "Sample Hold", "Smear", "French LP", "Add Bass", "Scream",
                                   "New Wsp", "DJ Mixer", "Diffusor", "EMS Ladder", "PZ SVF",
                                   "Normal", "Multi Morph", "Flanges", "Misc", "New" };
    for (const char* key : { "filter1.model", "filter2.model", "fxfilter.model" })
    {
        const auto n = namesOf (key);
        CHECK_EQ (n.size(), static_cast<size_t> (27));
        CHECK (head (n, 8) == std::vector<std::string> ({ "Analog", "Dirty", "Ladder", "Digital", "Diode", "Formant", "Comb", "Phaser" }));
        for (size_t k = 0; k < 19; ++k)
            CHECK (n[8 + k] == models[k]);
    }
    for (const char* key : { "filter1.style", "filter2.style", "fxfilter.style" })
    {
        const auto n = namesOf (key);
        CHECK_EQ (n.size(), static_cast<size_t> (12));
        CHECK (n[5] == "Style F (shelf)" && n[6] == "Style G" && n[11] == "Style L");
    }

    const char* const fx[] = { "Chorus", "Compressor", "Delay", "Distortion", "EQ", "FX Filter", "Flanger", "Phaser", "Reverb", "Hyper/Dimension",
                               "Utility", "Bode", "Convolve", "Splitter L/H", "Splitter L/M/H", "Splitter M/S" };
    for (int i = 1; i <= 16; ++i)
    {
        const auto& p = byKey (("fx.order." + std::to_string (i)).c_str());
        const auto n = namesOf (("fx.order." + std::to_string (i)).c_str());
        CHECK_EQ (n.size(), static_cast<size_t> (kNumFxSlots));
        for (size_t k = 0; k < n.size(); ++k)
            CHECK (n[k] == fx[k]);
        CHECK_EQ (static_cast<int> (p.max), 15);
    }
    // the identity permutation is the default of the new slots
    for (int i = 10; i <= 16; ++i)
        CHECK_EQ (byKey (("fx.order." + std::to_string (i)).c_str()).def, static_cast<float> (i - 1));

    // mod source / destination choices
    for (const char* key : { "mod01.source", "mod64.source", "mod01.aux_source", "mod64.aux_source" })
    {
        CHECK_EQ (choiceCount (byKey (key)), static_cast<size_t> (58));
        CHECK_EQ (byKey (key).max, 57.0f);
    }
    CHECK_EQ (choiceCount (byKey ("mod01.destination")), static_cast<size_t> (1249));
    CHECK_EQ (byKey ("mod64.destination").max, 1248.0f);

    // MANUALCHECK: the delay mode Tap->Delay of Serum 2 (manual p.166) is appended to the four Vital styles
    {
        const auto n = namesOf ("delay.style");
        CHECK_EQ (n.size(), static_cast<size_t> (5));
        CHECK (head (n, 4) == std::vector<std::string> ({ "Mono", "Stereo", "Ping pong", "Mid ping pong" }));
        CHECK (n[4] == "Tap->Delay");
    }

    // lists that stay unchanged
    CHECK_EQ (namesOf ("lfo1.sync").size(), static_cast<size_t> (5));
    // the warp lists grew in block B21 (4.22): the eleven Vital modes keep their places, 41 modes are appended; GROUPB WP-06 (4.34) appended 24 more
    CHECK_EQ (namesOf ("osc1.warp.type").size(), static_cast<size_t> (kNumWarpTypes));
    CHECK_EQ (namesOf ("osc1.warp2.type").size(), static_cast<size_t> (kNumWarpTypes));
    CHECK_EQ (namesOf ("osc1.warp.type")[10], std::string ("RM from Osc 3"));
    CHECK_EQ (namesOf ("osc1.warp.type")[11], std::string ("Sync Var"));
    CHECK (namesOf ("osc2.warp2.type") == namesOf ("osc2.warp.type"));
    CHECK_EQ (namesOf ("filter.routing").size(), static_cast<size_t> (3));
    CHECK_EQ (namesOf ("lfo1.sync_type").size(), static_cast<size_t> (6));
    CHECK_EQ (namesOf ("osc1.morph.type").size(), static_cast<size_t> (12));
}

TEST (mod_sources_are_extended_by_appending)
{
    const auto* t = modSourceTable();
    CHECK (std::strcmp (t[0].key, "env1") == 0);
    CHECK (std::strcmp (t[27].key, "random") == 0); // choice 28 stays the last of version 2
    const char* const appended[] = { "macro5", "macro6", "macro7", "macro8", "lfo9", "lfo10", "lift", "slide", "oct_note", "poly_aftertouch" };
    const bool bipolar[] = { false, false, false, false, true, true, false, false, false, false };
    for (int k = 0; k < 10; ++k)
    {
        CHECK (std::strcmp (t[kNumModSourcesV2 + k].key, appended[k]) == 0);
        CHECK_EQ (t[kNumModSourcesV2 + k].bipolar, bipolar[k]);
    }
    CHECK (std::strcmp (choiceName (byKey ("mod05.source"), 29), "Macro 5") == 0);
    CHECK (std::strcmp (choiceName (byKey ("mod05.source"), 38), "Poly Aftertouch") == 0);
    CHECK (std::strcmp (t[38].key, "stereo") == 0);
    CHECK_EQ (t[38].bipolar, true);
    CHECK (std::strcmp (choiceName (byKey ("mod05.source"), 39), "Stereo") == 0);
    CHECK (std::strcmp (choiceName (byKey ("mod05.source"), 25), "Aftertouch") == 0); // channel pressure keeps choice 25 (the 25th name)
}

TEST (destination_numbers_of_version_2_do_not_move)
{
    // the destination choice lists every modulatable key in table order: the 269 keys of version 2 keep 1..269
    size_t next = 1;
    for (size_t i = 0; i < kNumKeysV2; ++i)
        if ((paramTable()[i].flags & kParamModulatable) != 0)
        {
            CHECK_EQ (modDestinationChoiceOf (i), next);
            CHECK_EQ (modDestinationParamIndex (next), static_cast<int> (i));
            ++next;
        }
    CHECK_EQ (next, static_cast<size_t> (270));

    // 13-new-keys.md section 2: B01 270-273, B02 274-289, B04 290-319, B09 320-337, B10 338-356, B11 357-365, B12 366-403, B16 404-411, B14 412-423, B17 424-431, B18 432-441
    struct Range { const char* first; int firstChoice; const char* last; int lastChoice; };
    const Range ranges[] = {
        { "macro.5", 270, "macro.8", 273 },
        { "lfo9.rate", 274, "lfo10.keytrack_tune", 289 },
        { "env1.delay_beats", 290, "env6.release_beats", 319 },
        { "osc1.sample.scan", 320, "osc3.warp2.spread", 337 },
        { "osc1.filter_blend", 338, "direct.level", 356 },
        { "filter1.var", 357, "fxfilter.level", 365 },
        { "hyperdim.rate", 366, "splitter_ms.level", 403 },
        { "chorus.level", 404, "reverb.level", 411 }, // B16: the FX levels
        { "osc1.pitch_coarse", 412, "osc3.unison.blend_shape", 423 }, // B14
        { "compressor.crossover_low", 424, "reverb.diff_b", 431 }, // B17
        { "sub.octave", 432, "noise.phase", 441 }, // B18
        { "master.stereo_spread", 442, "lfo10.swing", 452 }, // B15
        { "master.stereo_routing", 453, "master.stereo_routing", 453 }, // B20
        { "mod01.amount", 454, "mod64.amount", 517 },
        { "master.portamento.time", 518, "master.portamento.time", 518 },
        { "master.polyphony", 519, "master.polyphony", 519 },
    };
    for (const auto& r : ranges)
    {
        CHECK_EQ (destinationOf (r.first), r.firstChoice);
        CHECK_EQ (destinationOf (r.last), r.lastChoice);
    }
    // keys that are not destinations
    CHECK_EQ (destinationOf ("lfo9.shape"), 0);
    CHECK_EQ (destinationOf ("mod01.aux_output"), 0);
    CHECK_EQ (destinationOf ("bus1.destination"), 0);
    CHECK_EQ (destinationOf ("sub.on"), 0);
    CHECK_EQ (destinationOf ("noise.sample"), 0);

    // every choice 1..453 names a modulatable parameter, in strictly increasing table order
    int previous = -1;
    for (size_t c = 1; c <= 453; ++c)
    {
        const int p = modDestinationParamIndex (c);
        CHECK (p > previous);
        previous = p;
    }
}

TEST (mod_slot_params_include_the_aux_source)
{
    for (int slot = 0; slot < kNumModSlots; ++slot)
    {
        const auto p = modSlotParams (slot);
        const int seven[] = { p.source, p.destination, p.amount, p.power, p.bipolar, p.stereo, p.bypass };
        for (int i : seven)
            CHECK (i >= 0 && i < static_cast<int> (kNumKeysV2));
        CHECK_EQ (p.auxSource, 984 + 4 * slot);
        CHECK_EQ (p.auxInv, 985 + 4 * slot);
        CHECK_EQ (p.auxPower, 986 + 4 * slot);
        CHECK_EQ (p.auxOutput, 987 + 4 * slot);
    }
    const auto bad = modSlotParams (kNumModSlots);
    CHECK (bad.source == -1 && bad.auxSource == -1 && bad.auxOutput == -1);
    CHECK_EQ (modSlotParams (-1).auxInv, -1);
}

TEST (new_key_defaults_are_neutral)
{
    // R5: a key never changes the sound of version 2 at its default
    for (const char* key : { "lfo9.mono", "lfo1.direction", "lfo1.host", "env1.units", "env1.legato_inverted", "master.scale.type", "master.mpe.on",
                             "osc1.type", "osc1.map.key_fold", "osc1.warp2.type", "hyperdim.on", "utility.on", "bode.on", "convolve.on",
                             "splitter_lh.on", "splitter_lmh.on", "splitter_ms.on", "bus1.fx_bypass", "main.fx_bypass", "reverb.force_enable",
                             "curve.velocity.legato", "mod01.aux_source", "osc1.bus1_send", "noise.filter_blend", "chorus.rack", "splitter_ms.rack" })
        CHECK_EQ (byKey (key).def, 0.0f);
    CHECK_EQ (byKey ("filter1.level").def, 0.0f);
    CHECK_EQ (byKey ("direct.level").def, 0.0f);
    CHECK_EQ (byKey ("bus1.level").def, 0.0f);
    CHECK_EQ (byKey ("mod07.aux_output").def, 100.0f);
    CHECK_EQ (byKey ("master.tuning.a4").def, 440.0f);
    CHECK_EQ (byKey ("osc1.map.key_high").def, 127.0f);
    CHECK_EQ (byKey ("osc2.map.vel_low").def, 1.0f);
    CHECK_EQ (byKey ("filter1.destination").def, 0.0f);
    CHECK_EQ (byKey ("utility.lpf").def, 20000.0f);
    CHECK_EQ (byKey ("utility.hpf").def, 20.0f);
    CHECK_EQ (byKey ("osc3.sample.keytrack").def, 1.0f);
    CHECK_EQ (byKey ("lfo10.smooth").def, 1.0f);
    CHECK_EQ (byKey ("osc1.pitch_mode").def, 0.0f);
    CHECK_EQ (byKey ("osc1.pitch_coarse").def, 0.0f);
    CHECK_EQ (byKey ("osc1.pitch_track").def, 1.0f);
    CHECK_EQ (byKey ("osc1.pitch_bend_track").def, 1.0f);
    CHECK_EQ (byKey ("osc1.fixed_pitch").def, 0.0f); // MANUALCHECK: a wavetable oscillator without pitch tracking plays MIDI note 0 (manual p.35)
    CHECK_EQ (byKey ("osc1.transpose_snap").def, 0.0f);
    CHECK_EQ (byKey ("osc1.unison.mode").def, 0.0f);
    CHECK_EQ (byKey ("osc1.unison.range").def, 2.0f);
    CHECK_EQ (byKey ("osc1.unison.blend_shape").def, 0.0f);
    CHECK_EQ (byKey ("osc1.unison.stack_repeat").def, 0.0f);
    CHECK_EQ (byKey ("osc1.phase_legato").def, 0.0f);
    CHECK_EQ (byKey ("osc1.phase_memory").def, 0.0f);
    CHECK_EQ (byKey ("voice.phase_legato").def, 0.0f);
}

// ---------------------------------------------------------------------------------------------------------------------
// version 2 sound
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
struct AudioGolden
{
    EngineKind kind;
    bool busy;
    uint64_t hash;
    double rms[8];
};

// computed by golden_audio.h against commit 9c97a69 (before any version 3 change), Release, Apple clang arm64
const AudioGolden kAudioGolden[] = {
    { EngineKind::own, false, 0xc3e433c23a7f89a5ull, { 0.0348080222, 0.0314500257, 0.0313985968, 0.0294308046, 0.0212378432, 0.0136766413, 0.00517099979, 0 } },
    { EngineKind::own, true, 0x44f50232b180c48dull, { 0.00390323847, 0.00352709373, 0.00352535563, 0.00304898028, 0.00240944165, 0.00147217084, 0.000581839939, 0 } },
    { EngineKind::vital, false, 0x765f4bc243b189c1ull, { 0.0415010124, 0.0428797808, 0.042447056, 0.0326433135, 0.0215032742, 0.00750400726, 7.19796001e-05, 3.88780198e-38 } },
    // the busy patch of the Vital engine draws every value from its range: the wider EQ gain and compressor threshold ranges of REFCOMPARE round 3 (B34) give other values (deliberate; the default patch is unchanged)
    { EngineKind::vital, true, 0x199ba135a1fd977full, { 2.11131332e-05, 0.000167065476, 0.000484127418, 0.000793745871, 0.00110979276, 0.0013015965, 0.00129324883, 0.00126312724 } },
};
} // namespace

TEST (version_2_sound_is_unchanged_in_both_engines)
{
    for (const auto& g : kAudioGolden)
    {
        if (! isEngineAvailable (g.kind))
            continue;
        const auto r = sbgolden::audioRender (g.kind, g.busy);
        const auto rms = sbgolden::segmentRms (r);
        sbtest::state().context = engineName (g.kind);
        CHECK (rms.size() == 8);
        for (size_t i = 0; i < rms.size() && i < 8; ++i)
            if (! CHECK (std::fabs (rms[i] - g.rms[i]) <= 1.0e-6 + 1.0e-3 * g.rms[i]))
                std::printf ("    segment %zu: rms %.9g, golden %.9g\n", i, rms[i], g.rms[i]);
#if defined(__APPLE__) && defined(__aarch64__) && defined(NDEBUG)
        // bit exact on the platform the golden was computed on
        CHECK_EQ (sbgolden::audioFnv (r), g.hash);
#endif
        sbtest::state().context = "";
    }
}

TEST (engines_ignore_the_new_keys)
{
    for (const auto& c : sbtest::engineCases())
    {
        sbtest::state().context = c.name;
        auto e = sbtest::makeEngine (c);
        CHECK (e != nullptr);
        if (e == nullptr)
            continue;

        // the engine claims exactly the keys whose capability bits it reports (the own engine acts on none of them)
        const auto* t = paramTable();
        for (size_t i = kNumKeysV2; i < paramCount(); ++i)
            CHECK_EQ (e->supportsParam (t[i].id), c.kind != EngineKind::own && capsCover (e->capabilities(), requiredCaps (i)));

        // every new key at random values (also the extremes, and the new mod sources / destinations of a slot): no crash, finite audio
        sbtest::Rng rng (0x4e57ull);
        for (int round = 0; round < 3; ++round)
        {
            for (size_t i = kNumKeysV2; i < paramCount(); ++i)
            {
                const float u = round == 0 ? 1.0f : (round == 1 ? 0.0f : static_cast<float> (rng.uniform()));
                e->setParam (t[i].id, fromNormalised (t[i], u));
                CHECK_EQ (e->getParam (t[i].id), constrainValue (t[i], fromNormalised (t[i], u))); // stored and read back by every engine
            }
            // matrix slots on the appended sources and destinations
            for (int k = 0; k < kNumModSlots; ++k)
            {
                const auto s = modSlotParams (k);
                e->setParam (t[s.source].id, static_cast<float> (29 + k % 10));
                e->setParam (t[s.destination].id, static_cast<float> (270 + k % 183));
            }
            const auto out = sbtest::render (*e, { sbtest::noteOn (0, 60, 0.9f), sbtest::noteOn (100, 64, 0.9f) }, 4800, 256);
            CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (out.right));
        }
        sbtest::state().context = "";
    }
}
