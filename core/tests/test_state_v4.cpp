// SynthyBoy - core tests: state version 4 (REFCOMPARE round 3, question 129): the pool of effect instances stores normalised values, and the ranges of the EQ gains,
// of the compressor thresholds and gains grew, so a document of version 3 is rescaled on load: it sounds exactly as before. Also the keys that are new in version 4,
// the library rack presets of version 1, and that a document of version 4 is not touched.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdlib>
#include <string>

#include "synthyboy/fx_types.h"
#include "synthyboy/library_items.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
int indexOfKey (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? findParamIndex (p->id) : -1;
}

/** Replaces the number after `"key":` in a document text (the state text holds every key). */
void setNumber (std::string& text, const std::string& key, double value)
{
    const std::string needle = "\"" + key + "\":";
    const size_t at = text.find (needle);
    if (at == std::string::npos)
        return;
    const size_t from = at + needle.size();
    const size_t to = text.find_first_of (",}", from);
    char buf[40];
    std::snprintf (buf, sizeof buf, "%.9g", value);
    text.replace (from, to - from, buf);
}

std::string asVersion3 (std::string text)
{
    text.replace (text.find ("\"version\":4"), 11, "\"version\":3");
    return text;
}

/** The parameter number (0-based) of the key `name` in the slot of `type`. */
int numberOf (int type, const char* name)
{
    const FxTypeDescriptor& d = fxTypeDescriptor (type);
    for (int k = 0; k < d.count; ++k)
        if (std::strcmp (paramTable()[d.source[k]].key, name) == 0)
            return k;
    return -1;
}

std::string poolKey (int slot, int number)
{
    char key[24];
    std::snprintf (key, sizeof key, "fxi%02d.p%02d", slot + 1, number + 1);
    return key;
}

/** A document of version 3 with an EQ in slot 1 and a compressor in slot 2, the pool values written the way the old ranges normalised them. */
std::string oldDocument (double eqGainDb, double thresholdDb, double compGainDb, int limit)
{
    auto v = ParamValues::defaults();
    v.plain[indexOfKey ("fxi01.type")] = static_cast<float> (kFxTypeEq);
    v.plain[indexOfKey ("fxi02.type")] = static_cast<float> (kFxTypeCompressor);
    std::string text = asVersion3 (saveState (v));
    setNumber (text, poolKey (0, numberOf (kFxTypeEq, "eq.mid.gain")), (eqGainDb + 15.0) / 30.0);
    setNumber (text, poolKey (0, numberOf (kFxTypeEq, "eq.low.gain")), (-eqGainDb + 15.0) / 30.0);
    setNumber (text, poolKey (1, numberOf (kFxTypeCompressor, "compressor.mid.upper_threshold")), (thresholdDb + 80.0) / 80.0);
    setNumber (text, poolKey (1, numberOf (kFxTypeCompressor, "compressor.low.lower_threshold")), (thresholdDb - 10.0 + 80.0) / 80.0);
    setNumber (text, poolKey (1, numberOf (kFxTypeCompressor, "compressor.high.gain")), (compGainDb + 30.0) / 60.0);
    setNumber (text, poolKey (1, numberOf (kFxTypeCompressor, "compressor.limit")), limit);
    return text;
}
} // namespace

TEST (version_3_pool_values_keep_their_plain_meaning)
{
    for (const double gain : { -12.0, -3.0, 0.0, 6.0, 15.0 })
        for (const double threshold : { -70.0, -40.0, -9.0, 0.0 })
            for (const int limit : { 0, 1 })
            {
                const std::string doc = oldDocument (gain, threshold, gain * 2.0, limit);
                auto v = ParamValues::defaults();
                const auto r = loadState (doc.data(), doc.size(), v);
                CHECK (r.status == LoadStatus::ok);
                CHECK_EQ (r.version, 3);
                const auto plainOf = [&] (int slot, int type, const char* name)
                {
                    const int k = numberOf (type, name);
                    return static_cast<double> (fxInstancePlain (type, k, v.plain[static_cast<size_t> (indexOfKey (poolKey (slot, k).c_str()))]));
                };
                CHECK_NEAR (plainOf (0, kFxTypeEq, "eq.mid.gain"), gain, 1.0e-3);
                CHECK_NEAR (plainOf (0, kFxTypeEq, "eq.low.gain"), -gain, 1.0e-3);
                CHECK_NEAR (plainOf (1, kFxTypeCompressor, "compressor.mid.upper_threshold"), threshold, 1.0e-3);
                CHECK_NEAR (plainOf (1, kFxTypeCompressor, "compressor.low.lower_threshold"), threshold - 10.0, 1.0e-3);
                CHECK_NEAR (plainOf (1, kFxTypeCompressor, "compressor.high.gain"), gain * 2.0, 1.0e-3);
                CHECK_EQ (static_cast<int> (std::lround (plainOf (1, kFxTypeCompressor, "compressor.limit"))), limit); // Compress / Limit stay Compress / Limit
            }
}

TEST (version_3_convolve_ir_gain_keeps_its_plain_meaning)
{
    auto v = ParamValues::defaults();
    v.plain[indexOfKey ("fxi04.type")] = static_cast<float> (kFxTypeConvolve);
    std::string text = asVersion3 (saveState (v));
    const int k = numberOf (kFxTypeConvolve, "convolve.ir_gain");
    CHECK (k >= 0);
    setNumber (text, poolKey (3, k), (6.0 + 24.0) / 48.0);   // +6 dB in the range -24 .. 24 of version 3
    auto loaded = ParamValues::defaults();
    CHECK (loadState (text.data(), text.size(), loaded).status == LoadStatus::ok);
    CHECK_NEAR (fxInstancePlain (kFxTypeConvolve, k, loaded.plain[static_cast<size_t> (indexOfKey (poolKey (3, k).c_str()))]), 6.0f, 1.0e-3f);
    // the law of the convolution (block B35) comes with its default: the Vital law
    const int law = numberOf (kFxTypeConvolve, "convolve.law");
    CHECK (law >= 0);
    CHECK_EQ (loaded.plain[static_cast<size_t> (indexOfKey ("convolve.law"))], 0.0f);
}

TEST (version_3_document_gets_the_version_4_keys_at_their_defaults)
{
    // a previous patch with the second EQ band on and the Serum EQ law, and an EQ slot whose unused parameters hold leftovers
    auto garbage = ParamValues::defaults();
    garbage.plain[indexOfKey ("eq.mid2.mode")] = 1.0f;
    garbage.plain[indexOfKey ("eq.law")] = 1.0f;
    garbage.plain[indexOfKey ("fxi03.p20")] = 0.9f;
    const std::string doc = oldDocument (3.0, -20.0, 0.0, 0);
    const auto r = loadState (doc.data(), doc.size(), garbage);
    CHECK (r.status == LoadStatus::ok);
    CHECK_EQ (garbage.plain[static_cast<size_t> (indexOfKey ("eq.mid2.mode"))], 0.0f);
    CHECK_EQ (garbage.plain[static_cast<size_t> (indexOfKey ("eq.law"))], 0.0f);
    // the slot's own parameters of the new keys: the defaults, whatever an old document stored there
    for (const char* name : { "eq.mid2.mode", "eq.mid2.cutoff", "eq.mid2.gain", "eq.mid2.resonance", "eq.law" })
    {
        const int k = numberOf (kFxTypeEq, name);
        CHECK (k >= 0);
        if (k >= 0)
        {
            const ParamInfo* src = fxInstanceSource (kFxTypeEq, k);
            CHECK_NEAR (garbage.plain[static_cast<size_t> (indexOfKey (poolKey (0, k).c_str()))], toNormalised (*src, src->def), 1.0e-6f);
        }
    }
}

TEST (version_4_document_is_not_rescaled_and_round_trips)
{
    auto v = ParamValues::defaults();
    v.plain[indexOfKey ("fxi01.type")] = static_cast<float> (kFxTypeEq);
    v.plain[indexOfKey (poolKey (0, numberOf (kFxTypeEq, "eq.mid.gain")).c_str())] = 0.8f;
    v.plain[indexOfKey ("eq.mid2.mode")] = 1.0f;
    const std::string doc = saveState (v);
    CHECK (doc.find ("\"version\":4") != std::string::npos);
    auto loaded = ParamValues::defaults();
    CHECK (loadState (doc.data(), doc.size(), loaded).status == LoadStatus::ok);
    CHECK (saveState (loaded) == doc);
}

TEST (version_1_rack_preset_is_rescaled_and_version_2_is_not)
{
    auto base = ParamValues::defaults();
    base.plain[indexOfKey ("fxi01.type")] = static_cast<float> (kFxTypeEq);
    const int k = numberOf (kFxTypeEq, "eq.mid.gain");
    const std::string key = poolKey (0, k);
    const std::string head = "{\"format\":\"synthyboy-fx-rack\",\"version\":";
    const std::string body = ",\"values\":{\"fxi01.type\":" + std::to_string (static_cast<int> (kFxTypeEq)) + ",\"" + key + "\":0.75}}"; // +7.5 dB in the old +-15 range
    auto a = base;
    CHECK_EQ (applyFxRackPreset (head + "1" + body, a), 2);
    CHECK_NEAR (fxInstancePlain (kFxTypeEq, k, a.plain[static_cast<size_t> (indexOfKey (key.c_str()))]), 7.5f, 1.0e-3f);
    auto b = base;
    CHECK_EQ (applyFxRackPreset (head + "2" + body, b), 2);
    CHECK_NEAR (b.plain[static_cast<size_t> (indexOfKey (key.c_str()))], 0.75f, 1.0e-6f);   // version 2 is in the new ranges
    // a rack saved now is version 2 and applies as it was
    const std::string saved = serializeFxRackPreset ("x", b);
    CHECK (saved.find ("\"version\":2") != std::string::npos);
    auto c = ParamValues::defaults();
    applyFxRackPreset (saved, c);
    CHECK_NEAR (c.plain[static_cast<size_t> (indexOfKey (key.c_str()))], 0.75f, 1.0e-6f);
}
