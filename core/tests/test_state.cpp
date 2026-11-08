// SynthyBoy - core tests: state save / load (round trip, determinism, hostile input).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
bool sameValues (const ParamValues& a, const ParamValues& b)
{
    return std::memcmp (a.plain, b.plain, sizeof a.plain) == 0;
}

LoadResult load (const std::string& s, ParamValues& v)
{
    return loadState (s.data(), s.size(), v);
}

ParamValues randomValues (sbtest::Rng& rng)
{
    ParamValues v;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const auto& info = paramTable()[i];
        v.plain[i] = constrainValue (info, info.min + static_cast<float> (rng.uniform()) * (info.max - info.min));
    }
    return v;
}

int indexOf (uint32_t id)
{
    return findParamIndex (id);
}

const size_t kVolume = static_cast<size_t> (indexOf (kParamMasterVolume));
const size_t kWavetable = static_cast<size_t> (indexOf (kParamOsc1Wavetable));
const size_t kCutoff = static_cast<size_t> (indexOf (kParamFilter1Cutoff));
const size_t kFilterOn = static_cast<size_t> (indexOf (kParamFilter1On));

/** Three parameters with values that differ from their defaults: a loader that touches nothing leaves them alone. */
ParamValues marker()
{
    ParamValues v = ParamValues::defaults();
    v.plain[kVolume] = -33.0f;
    v.plain[kWavetable] = 0.0f;
    v.plain[kCutoff] = 1234.5f;
    v.plain[kFilterOn] = 0.0f;
    return v;
}

const char* kValid = "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-20,\"osc1.wavetable\":0,\"filter1.cutoff\":500}}";
} // namespace

TEST (state_save_format_is_exact_and_deterministic)
{
    const auto v = ParamValues::defaults();
    const auto s = saveState (v);
    const std::string head = "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-12,\"master.polyphony\":16,";
    CHECK (s.compare (0, head.size(), head) == 0);
    CHECK (s.size() > 10 * 1024 && s.size() < 64 * 1024); // about 20 KiB for the whole table, far below kMaxStateBytes
    std::printf ("    default state: %zu bytes, %zu parameters\n", s.size(), paramCount());
    CHECK (s.compare (s.size() - 2, 2, "}}") == 0);
    CHECK (saveState (v) == s);
    CHECK (s.find (' ') == std::string::npos && s.find ('\n') == std::string::npos);

    // the parameters are listed in table order
    size_t pos = 0;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const std::string needle = std::string ("\"") + paramTable()[i].key + "\":";
        const size_t at = s.find (needle, pos);
        if (! CHECK (at != std::string::npos && at >= pos))
        {
            std::printf ("    %s out of order\n", paramTable()[i].key);
            break;
        }
        pos = at;
    }

    // out-of-range / NaN input is sanitised on save
    ParamValues bad = v;
    bad.plain[kVolume] = std::nanf ("");
    bad.plain[kCutoff] = 1.0e30f;
    const auto sanitised = saveState (bad);
    CHECK (sanitised.find ("\"master.volume\":-12,") != std::string::npos);
    CHECK (sanitised.find ("\"filter1.cutoff\":20000,") != std::string::npos);
}

TEST (state_round_trip)
{
    sbtest::Rng rng (1);
    for (int i = 0; i < 2000; ++i)
    {
        const auto v = randomValues (rng);
        const auto s = saveState (v);

        ParamValues loaded = marker();
        const auto r = load (s, loaded);
        CHECK (r.status == LoadStatus::ok);
        CHECK_EQ (r.applied, static_cast<int> (paramCount()));
        CHECK_EQ (r.version, 2);
        if (! CHECK (sameValues (v, loaded)))
        {
            std::printf ("    %s\n", s.c_str());
            break;
        }
        // byte for byte: save (load (save (x))) == save (x)
        CHECK (saveState (loaded) == s);
    }
}

TEST (state_empty_input)
{
    ParamValues v = marker();
    const auto before = v;
    CHECK (loadState (nullptr, 0, v).status == LoadStatus::empty);
    CHECK (loadState ("x", 0, v).status == LoadStatus::empty);
    CHECK (loadState (nullptr, 5, v).status == LoadStatus::invalid);
    CHECK (load (" ", v).status == LoadStatus::invalid);
    CHECK (sameValues (v, before));
}

TEST (state_random_bytes_are_rejected)
{
    sbtest::Rng rng (99);
    for (int round = 0; round < 3; ++round)
    {
        std::vector<unsigned char> junk (kMaxStateBytes);
        for (auto& b : junk)
            b = static_cast<unsigned char> (rng.u32());
        ParamValues v = marker();
        const auto before = v;
        CHECK (loadState (junk.data(), junk.size(), v).status == LoadStatus::invalid);
        CHECK (sameValues (v, before));
    }
}

TEST (state_truncated_documents)
{
    const std::string docs[] = {
        kValid,
        saveState (ParamValues::defaults()),
        "  {\"extra\":{\"a\":[1,2,{\"b\":null}],\"c\":\"x\\u0041\\n\"},\"format\":\"synthyboy-state\",\"version\":2,"
        "\"params\":{\"master.volume\":-20.5,\"osc1.wavetable\":0,\"filter1.cutoff\":5e2,\"unknown\":true}}\n"
    };
    for (const auto& doc : docs)
    {
        // every length of the short documents, every 11th (and the last 80) of the large one
        const size_t stride = doc.size() > 4096 ? 11 : 1;
        for (size_t len = 0; len <= doc.size(); len += (doc.size() - len <= 80 ? 1 : stride))
        {
            ParamValues v = marker();
            const auto before = v;
            const auto r = loadState (doc.data(), len, v);
            // only the complete document (or one that is complete up to trailing white space) may load
            const bool complete = len == doc.size() || doc.find_first_not_of (" \n\r\t", len) == std::string::npos;
            if (len == 0)
            {
                CHECK (r.status == LoadStatus::empty && sameValues (v, before));
            }
            else if (complete)
            {
                CHECK (r.status == LoadStatus::ok);
            }
            else if (! CHECK (r.status == LoadStatus::invalid && sameValues (v, before)))
            {
                std::printf ("    len %zu of %zu\n", len, doc.size());
                break;
            }
        }
    }
}

TEST (state_trailing_content)
{
    ParamValues v = marker();
    CHECK (load (std::string (kValid) + "  \n", v).status == LoadStatus::ok);
    v = marker();
    CHECK (load (std::string (kValid) + "x", v).status == LoadStatus::invalid);
    CHECK (load (std::string (kValid) + "{}", v).status == LoadStatus::invalid);
    CHECK (load (std::string ("\xEF\xBB\xBF") + kValid, v).status == LoadStatus::ok);
}

TEST (state_future_version)
{
    ParamValues v = marker();
    const auto r = load ("{\"format\":\"synthyboy-state\",\"version\":7,\"newThing\":{\"x\":[1,2,3]},"
                         "\"params\":{\"master.volume\":-6,\"brandNew\":3,\"filter1.cutoff\":1000,\"gain\":4}}", v);
    CHECK (r.status == LoadStatus::newer_version);
    CHECK_EQ (r.version, 7);
    CHECK_EQ (r.applied, 2);
    CHECK (v.plain[kVolume] == constrainValue (paramTable()[kVolume], -6.0f)); // applied (a legacy key is not migrated from version 7)
    CHECK (v.plain[kWavetable] == 0.0f);                                       // missing key keeps the value
    CHECK (v.plain[kCutoff] == 1000.0f);

    // version number sanity
    const char* badVersions[] = { "0", "-1", "1.5", "\"1\"", "null", "1e999", "2e9", "true", "[1]" };
    for (const char* bv : badVersions)
    {
        ParamValues w = marker();
        const auto before = w;
        const std::string doc = std::string ("{\"format\":\"synthyboy-state\",\"version\":") + bv + ",\"params\":{\"master.volume\":-6}}";
        if (! CHECK (load (doc, w).status == LoadStatus::invalid && sameValues (w, before)))
            std::printf ("    version %s\n", bv);
    }
}

TEST (state_format_and_version_are_required)
{
    ParamValues v = marker();
    const auto before = v;
    const char* docs[] = {
        "{}",
        "{\"version\":2,\"params\":{\"master.volume\":-6}}",
        "{\"format\":\"synthyboy-state\",\"params\":{\"master.volume\":-6}}",
        "{\"format\":\"other\",\"version\":2,\"params\":{\"master.volume\":-6}}",
        "{\"format\":1,\"version\":2,\"params\":{\"master.volume\":-6}}",
        "{\"format\":\"synthyboy-statex\",\"version\":2}",
        "[]", "null", "\"synthyboy-state\"", "42",
    };
    for (const char* d : docs)
        if (! CHECK (load (d, v).status == LoadStatus::invalid && sameValues (v, before)))
            std::printf ("    %s\n", d);

    // no params object at all is fine
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":2}", v).status == LoadStatus::ok);
    CHECK (sameValues (v, before));
    // key order does not matter
    CHECK (load ("{\"params\":{\"master.volume\":-6},\"version\":2,\"format\":\"synthyboy-state\"}", v).status == LoadStatus::ok);
    CHECK (v.plain[kVolume] == constrainValue (paramTable()[kVolume], -6.0f));
}

TEST (state_nan_inf_and_huge_numbers)
{
    auto doc = [] (const std::string& value) { return "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":" + value + "}}"; };
    const auto& gain = paramTable()[kVolume];

    // not JSON numbers -> the document is invalid and nothing changes
    for (const char* bad : { "NaN", "nan", "Infinity", "-Infinity", "inf", "+1", ".5", "1.", "01", "0x10", "--1", "1e", "1e+" })
    {
        ParamValues v = marker();
        const auto before = v;
        if (! CHECK (load (doc (bad), v).status == LoadStatus::invalid && sameValues (v, before)))
            std::printf ("    %s\n", bad);
    }

    // valid but out of range / overflowing numbers are clamped
    struct Case { std::string text; float expected; };
    const Case cases[] = {
        { "1e999", gain.max }, { "-1e999", gain.min }, { "1e308", gain.max }, { "-1e308", gain.min },
        { "99999999999999999999999999999999999999999999", gain.max }, { "1" + std::string (400, '0'), gain.max },
        { "-1" + std::string (400, '0'), gain.min }, { "7", gain.max }, { "-61", gain.min }, { "6", 6.0f },
        { "-60", -60.0f }, { "0.0000" + std::string (400, '0') + "1", 0.0f }, { "1e-999", 0.0f },
    };
    for (const auto& c : cases)
    {
        ParamValues v = marker();
        const auto r = load (doc (c.text), v);
        const bool ok = CHECK (r.status == LoadStatus::ok) && CHECK_NEAR (v.plain[kVolume], c.expected, 1e-4);
        if (! ok)
            std::printf ("    value %.40s -> %.9g\n", c.text.c_str(), static_cast<double> (v.plain[kVolume]));
        CHECK (std::isfinite (v.plain[kVolume]));
    }

    // stepped values snap
    ParamValues v = marker();
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"osc1.wavetable\":0.4}}", v).status == LoadStatus::ok && v.plain[kWavetable] == 0.0f);
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"osc1.wavetable\":0.6}}", v).status == LoadStatus::ok && v.plain[kWavetable] == 1.0f);
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"osc1.wavetable\":5}}", v).status == LoadStatus::ok && v.plain[kWavetable] == 5.0f);
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"osc1.wavetable\":99}}", v).status == LoadStatus::ok && v.plain[kWavetable] == 13.0f);
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"osc1.wavetable\":-5}}", v).status == LoadStatus::ok && v.plain[kWavetable] == 0.0f);
}

TEST (state_duplicate_unknown_and_wrongly_typed_keys)
{
    ParamValues v = marker();
    auto r = load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-10,\"master.volume\":-20,\"filter1.cutoff\":300}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 2);
    CHECK (v.plain[kVolume] == constrainValue (paramTable()[kVolume], -20.0f) && v.plain[kCutoff] == 300.0f);

    // duplicate top-level keys: the last wins; unknown keys of any shape are skipped
    v = marker();
    r = load ("{\"format\":\"x\",\"format\":\"synthyboy-state\",\"version\":2,\"version\":2,\"junk\":[{},[],\"s\",null,true,false,-1.5e3],"
              "\"params\":{\"master.volume\":-6},\"params\":{\"filter1.cutoff\":400,\"other\":{\"a\":{}}}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 2);
    CHECK (v.plain[kVolume] == constrainValue (paramTable()[kVolume], -6.0f) && v.plain[kCutoff] == 400.0f);

    // known key with a non-number value: skipped (not applied, not counted)
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":\"loud\",\"osc1.wavetable\":null,\"filter1.cutoff\":[1]}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 0 && sameValues (v, marker()));

    // escaped key names still match
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"\\u006daster.volume\":-6,\"filter1.cut\\u006ff\\u0066\":300}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 2 && v.plain[kCutoff] == 300.0f);

    // over-long keys do not alias a known key
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"" + std::string (500, 'a') + "master.volume\":-6}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 0);
}

TEST (state_syntax_errors_leave_values_untouched)
{
    const char* docs[] = {
        "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-6,}}",
        "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-6 \"filter1.cutoff\":3}}",
        "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{gain:-6}}",
        "{'format':'synthyboy-state','version':2}",
        "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-6},}",
        "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.vol\x01ume\":-6}}",
        "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-6,\"x\":\"\\q\"}}",
        "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-6,\"x\":\"\\u12\"}}",
        "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-6,\"x\":tru}}",
        "{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"master.volume\":-6,\"x\":[1 2]}}",
    };
    for (const char* d : docs)
    {
        ParamValues v = marker();
        const auto before = v;
        if (! CHECK (load (d, v).status == LoadStatus::invalid && sameValues (v, before)))
            std::printf ("    %s\n", d);
    }
}

TEST (state_nesting_and_size_limits)
{
    auto withExtra = [] (const std::string& extra) { return "{\"format\":\"synthyboy-state\",\"version\":2,\"x\":" + extra + "}"; };
    ParamValues v = marker();
    CHECK (load (withExtra (std::string (6, '[') + std::string (6, ']')), v).status == LoadStatus::ok);
    CHECK (load (withExtra (std::string (100, '[') + std::string (100, ']')), v).status == LoadStatus::invalid);
    CHECK (load (std::string (kMaxStateBytes, '['), v).status == LoadStatus::invalid);
    CHECK (load (std::string (kMaxStateBytes / 2, '{'), v).status == LoadStatus::invalid);

    // exactly the limit loads, one more byte does not
    std::string doc = kValid;
    doc.resize (kMaxStateBytes, ' ');
    CHECK (load (doc, v).status == LoadStatus::ok);
    doc.push_back (' ');
    v = marker();
    CHECK (load (doc, v).status == LoadStatus::invalid && sameValues (v, marker()));

    // long strings / many members are fine and bounded
    std::string many = "{\"format\":\"synthyboy-state\",\"version\":2,\"x\":[";
    for (int i = 0; i < 50000; ++i)
        many += i ? ",\"abc\"" : "\"abc\"";
    many += "]}";
    CHECK (load (many, v).status == LoadStatus::ok);
}

TEST (state_mutated_documents_never_crash)
{
    sbtest::Rng rng (2024);
    const std::string base = kValid;
    const std::string alphabet = "{}[]\",:-.e0123456789tfn\\u";
    for (int i = 0; i < 40000; ++i)
    {
        std::string doc = base;
        const int edits = 1 + static_cast<int> (rng.u32() % 4);
        for (int e = 0; e < edits; ++e)
        {
            const size_t pos = rng.u32() % doc.size();
            switch (rng.u32() % 3)
            {
                case 0: doc[pos] = static_cast<char> (rng.u32()); break;
                case 1: doc.erase (pos, 1 + rng.u32() % 4); break;
                default: doc.insert (pos, 1, alphabet[rng.u32() % alphabet.size()]); break;
            }
            if (doc.empty())
                doc = "{";
        }
        ParamValues v = marker();
        const auto before = v;
        const auto r = loadState (doc.data(), doc.size(), v);
        if (r.status == LoadStatus::ok || r.status == LoadStatus::newer_version)
        {
            for (size_t p = 0; p < paramCount(); ++p)
                CHECK (std::isfinite (v.plain[p]) && v.plain[p] >= paramTable()[p].min && v.plain[p] <= paramTable()[p].max);
        }
        else
        {
            CHECK (sameValues (v, before));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// format version 1 -> 2
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
// Literal documents of format version 1, as written by saveState() of the code before version 2 (commit b5b6826) for the
// values (default), (-3.5 / Sine / 1234.5), (5.9 / Saw / 20) and (-60 / Saw / 19999.9)
const char* kV1Documents[] = {
    "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-12,\"waveform\":1,\"cutoff\":8000}}",
    "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-3.5,\"waveform\":0,\"cutoff\":1234.5}}",
    "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":5.9000001,\"waveform\":1,\"cutoff\":20}}",
    "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-60,\"waveform\":1,\"cutoff\":19999.9004}}",
};
struct V1Expected { float gain, waveform, cutoff; };
const V1Expected kV1Expected[] = { { -12.0f, 1.0f, 8000.0f }, { -3.5f, 0.0f, 1234.5f }, { 5.9f, 1.0f, 20.0f }, { -60.0f, 1.0f, 19999.9f } };
} // namespace

TEST (state_v1_documents_are_migrated)
{
    for (size_t k = 0; k < sizeof kV1Documents / sizeof kV1Documents[0]; ++k)
    {
        ParamValues v = marker();
        const auto r = load (kV1Documents[k], v);
        const auto& want = kV1Expected[k];
        CHECK (r.status == LoadStatus::ok);
        CHECK_EQ (r.version, 1);
        CHECK_EQ (r.applied, 4); // master.volume, osc1.wavetable, filter1.cutoff and filter1.on
        // gain (dB) -> master.volume (dB), same range: unchanged
        CHECK_NEAR (v.plain[kVolume], want.gain, 1e-4);
        // waveform 0 / 1 (Sine / Saw) -> osc1.wavetable 0 / 1 (the first two built-in wavetables)
        CHECK_EQ (v.plain[kWavetable], want.waveform);
        CHECK_EQ (std::string (choiceName (paramTable()[kWavetable], static_cast<size_t> (v.plain[kWavetable]))), want.waveform == 0.0f ? "Sine" : "Saw");
        // cutoff (Hz) -> filter1.cutoff (Hz), and the filter the old engine always had is switched on
        CHECK_NEAR (v.plain[kCutoff], want.cutoff, 0.01);
        CHECK_EQ (v.plain[kFilterOn], 1.0f);

        // every other parameter is untouched
        for (size_t p = 0; p < paramCount(); ++p)
            if (p != kVolume && p != kWavetable && p != kCutoff && p != kFilterOn)
                CHECK_EQ (v.plain[p], marker().plain[p]);
    }

    // a v1 document that is saved again is a v2 document with the migrated values, and it loads to the same values
    ParamValues v = marker();
    load (kV1Documents[1], v);
    const auto saved = saveState (v);
    CHECK (saved.find ("\"version\":2") != std::string::npos);
    ParamValues again = marker();
    CHECK (load (saved, again).status == LoadStatus::ok && sameValues (v, again));
}

TEST (state_v1_partial_and_odd_documents)
{
    // partial: only the keys that are present are migrated; no cutoff -> the filter switch is not touched
    ParamValues v = marker();
    auto r = load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-20}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 1);
    CHECK_EQ (v.plain[kVolume], -20.0f);
    CHECK_EQ (v.plain[kWavetable], 0.0f);
    CHECK_EQ (v.plain[kCutoff], 1234.5f);
    CHECK_EQ (v.plain[kFilterOn], 0.0f);

    // key order inside the document does not matter, the version may follow the parameters
    v = marker();
    r = load ("{\"params\":{\"cutoff\":500,\"waveform\":1},\"version\":1,\"format\":\"synthyboy-state\"}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 3);
    CHECK_EQ (v.plain[kCutoff], 500.0f);
    CHECK_EQ (v.plain[kFilterOn], 1.0f);
    CHECK_EQ (v.plain[kWavetable], 1.0f);

    // out-of-range and fractional values are clamped / snapped like every other value
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":99,\"waveform\":7,\"cutoff\":1e9}}", v);
    CHECK (r.status == LoadStatus::ok);
    CHECK_EQ (v.plain[kVolume], 6.0f);
    CHECK_EQ (v.plain[kWavetable], 1.0f); // version 1 knew two values (a v1 file never holds 7): anything but Sine is the Saw
    CHECK_EQ (v.plain[kCutoff], 20000.0f);

    // legacy keys in a version 2 document are ignored (not migrated), and v2 keys in a v1 document win
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":2,\"params\":{\"gain\":3,\"cutoff\":100}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 0 && sameValues (v, marker()));
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":3,\"master.volume\":-8}}", v);
    CHECK (r.status == LoadStatus::ok);
    CHECK_EQ (v.plain[kVolume], 3.0f); // the migration runs after the parse: the legacy key sets master.volume last
    // legacy values that are not numbers are skipped
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":\"loud\",\"cutoff\":null}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 0 && sameValues (v, marker()));
    // a version 1 document, truncated anywhere, never loads half-way
    const std::string doc = kV1Documents[1];
    for (size_t len = 1; len < doc.size(); ++len)
    {
        ParamValues w = marker();
        CHECK (loadState (doc.data(), len, w).status == LoadStatus::invalid && sameValues (w, marker()));
    }

    // the XML blob of the spike goes through the same migration
    ParamValues x = marker();
    CHECK_EQ (migrateLegacyParam ("gain", -6.0, x), 1);
    CHECK_EQ (migrateLegacyParam ("waveform", 0.0, x), 1);
    CHECK_EQ (migrateLegacyParam ("cutoff", 1000.0, x), 2);
    CHECK_EQ (migrateLegacyParam ("master.volume", 1.0, x), 0);
    CHECK_EQ (migrateLegacyParam (nullptr, 1.0, x), 0);
    CHECK_EQ (migrateLegacyParam ("gain", std::nan (""), x), 0);
    CHECK_EQ (x.plain[kVolume], -6.0f);
    CHECK_EQ (x.plain[kWavetable], 0.0f);
    CHECK_EQ (x.plain[kCutoff], 1000.0f);
    CHECK_EQ (x.plain[kFilterOn], 1.0f);
}

TEST (state_large_documents_are_bounded)
{
    // the whole table is about 20 KiB; many times that (all keys duplicated in unknown objects) is still parsed in bounded time
    const std::string base = saveState (ParamValues::defaults());
    std::string big = base.substr (0, base.size() - 1) + ",\"extra\":[";
    while (big.size() < kMaxStateBytes - 4096)
        big += "{\"a\":[1,2,3,\"" + std::string (200, 'x') + "\"]},";
    big += "0]}";
    ParamValues v = marker();
    CHECK (big.size() <= kMaxStateBytes);
    CHECK (load (big, v).status == LoadStatus::ok);
    CHECK_EQ (v.plain[kVolume], -12.0f);
    big.resize (kMaxStateBytes + 1, ' ');
    v = marker();
    CHECK (load (big, v).status == LoadStatus::invalid && sameValues (v, marker()));
}
