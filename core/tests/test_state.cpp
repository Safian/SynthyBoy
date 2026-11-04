// SynthyBoyVST - core tests: state save / load (round trip, determinism, hostile input).
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

ParamValues marker()
{
    ParamValues v = ParamValues::defaults();
    v.plain[0] = -33.0f;
    v.plain[1] = 0.0f;
    v.plain[2] = 1234.5f;
    return v;
}

const char* kValid = "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-20,\"waveform\":0,\"cutoff\":500}}";
} // namespace

TEST (state_save_format_is_exact_and_deterministic)
{
    const auto v = ParamValues::defaults();
    const auto s = saveState (v);
    CHECK (s == "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-12,\"waveform\":1,\"cutoff\":8000}}");
    CHECK (saveState (v) == s);
    CHECK (s.find (' ') == std::string::npos && s.find ('\n') == std::string::npos);

    // out-of-range / NaN input is sanitised on save
    ParamValues bad = v;
    bad.plain[0] = std::nanf ("");
    bad.plain[2] = 1.0e30f;
    CHECK (saveState (bad) == "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-12,\"waveform\":1,\"cutoff\":20000}}");
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
        CHECK_EQ (r.version, 1);
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
        "  {\"extra\":{\"a\":[1,2,{\"b\":null}],\"c\":\"x\\u0041\\n\"},\"format\":\"synthyboy-state\",\"version\":1,"
        "\"params\":{\"gain\":-20.5,\"waveform\":0,\"cutoff\":5e2,\"unknown\":true}}\n"
    };
    for (const auto& doc : docs)
    {
        for (size_t len = 0; len <= doc.size(); ++len)
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
                         "\"params\":{\"gain\":-6,\"brandNew\":3,\"cutoff\":1000}}", v);
    CHECK (r.status == LoadStatus::newer_version);
    CHECK_EQ (r.version, 7);
    CHECK_EQ (r.applied, 2);
    CHECK (v.plain[0] == constrainValue (paramTable()[0], -6.0f)); // gain applied
    CHECK (v.plain[1] == 0.0f);                                    // waveform: missing key keeps the value
    CHECK (v.plain[2] == 1000.0f);

    // version number sanity
    const char* badVersions[] = { "0", "-1", "1.5", "\"1\"", "null", "1e999", "2e9", "true", "[1]" };
    for (const char* bv : badVersions)
    {
        ParamValues w = marker();
        const auto before = w;
        const std::string doc = std::string ("{\"format\":\"synthyboy-state\",\"version\":") + bv + ",\"params\":{\"gain\":-6}}";
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
        "{\"version\":1,\"params\":{\"gain\":-6}}",
        "{\"format\":\"synthyboy-state\",\"params\":{\"gain\":-6}}",
        "{\"format\":\"other\",\"version\":1,\"params\":{\"gain\":-6}}",
        "{\"format\":1,\"version\":1,\"params\":{\"gain\":-6}}",
        "{\"format\":\"synthyboy-statex\",\"version\":1}",
        "[]", "null", "\"synthyboy-state\"", "42",
    };
    for (const char* d : docs)
        if (! CHECK (load (d, v).status == LoadStatus::invalid && sameValues (v, before)))
            std::printf ("    %s\n", d);

    // no params object at all is fine
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":1}", v).status == LoadStatus::ok);
    CHECK (sameValues (v, before));
    // key order does not matter
    CHECK (load ("{\"params\":{\"gain\":-6},\"version\":1,\"format\":\"synthyboy-state\"}", v).status == LoadStatus::ok);
    CHECK (v.plain[0] == constrainValue (paramTable()[0], -6.0f));
}

TEST (state_nan_inf_and_huge_numbers)
{
    auto doc = [] (const std::string& value) { return "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":" + value + "}}"; };
    const auto& gain = paramTable()[0];

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
        const bool ok = CHECK (r.status == LoadStatus::ok) && CHECK_NEAR (v.plain[0], c.expected, 1e-4);
        if (! ok)
            std::printf ("    value %.40s -> %.9g\n", c.text.c_str(), static_cast<double> (v.plain[0]));
        CHECK (std::isfinite (v.plain[0]));
    }

    // stepped values snap
    ParamValues v = marker();
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"waveform\":0.4}}", v).status == LoadStatus::ok && v.plain[1] == 0.0f);
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"waveform\":0.6}}", v).status == LoadStatus::ok && v.plain[1] == 1.0f);
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"waveform\":5}}", v).status == LoadStatus::ok && v.plain[1] == 1.0f);
    CHECK (load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"waveform\":-5}}", v).status == LoadStatus::ok && v.plain[1] == 0.0f);
}

TEST (state_duplicate_unknown_and_wrongly_typed_keys)
{
    ParamValues v = marker();
    auto r = load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-10,\"gain\":-20,\"cutoff\":300}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 2);
    CHECK (v.plain[0] == constrainValue (paramTable()[0], -20.0f) && v.plain[2] == 300.0f);

    // duplicate top-level keys: the last wins; unknown keys of any shape are skipped
    v = marker();
    r = load ("{\"format\":\"x\",\"format\":\"synthyboy-state\",\"version\":1,\"version\":1,\"junk\":[{},[],\"s\",null,true,false,-1.5e3],"
              "\"params\":{\"gain\":-6},\"params\":{\"cutoff\":400,\"other\":{\"a\":{}}}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 2);
    CHECK (v.plain[0] == constrainValue (paramTable()[0], -6.0f) && v.plain[2] == 400.0f);

    // known key with a non-number value: skipped (not applied, not counted)
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":\"loud\",\"waveform\":null,\"cutoff\":[1]}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 0 && sameValues (v, marker()));

    // escaped key names still match
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"\\u0067ain\":-6,\"cut\\u006ff\\u0066\":300}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 2 && v.plain[2] == 300.0f);

    // over-long keys do not alias a known key
    v = marker();
    r = load ("{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"" + std::string (500, 'a') + "gain\":-6}}", v);
    CHECK (r.status == LoadStatus::ok && r.applied == 0);
}

TEST (state_syntax_errors_leave_values_untouched)
{
    const char* docs[] = {
        "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-6,}}",
        "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-6 \"cutoff\":3}}",
        "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{gain:-6}}",
        "{'format':'synthyboy-state','version':1}",
        "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-6},}",
        "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"ga\x01in\":-6}}",
        "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-6,\"x\":\"\\q\"}}",
        "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-6,\"x\":\"\\u12\"}}",
        "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-6,\"x\":tru}}",
        "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-6,\"x\":[1 2]}}",
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
    auto withExtra = [] (const std::string& extra) { return "{\"format\":\"synthyboy-state\",\"version\":1,\"x\":" + extra + "}"; };
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
    std::string many = "{\"format\":\"synthyboy-state\",\"version\":1,\"x\":[";
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
