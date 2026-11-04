// SynthyBoyVST - core tests: key hash, parameter table, normalisation, text conversion, number formatting/parsing.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "number.h"
#include "synthyboy/hash.h"
#include "synthyboy/params.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

static_assert (hashKey ("gain") == 3165055u, "hashKey must match juce::String::hashCode");
static_assert (hashKey ("") == 0u, "empty key");

TEST (hash_matches_juce_string_hashcode)
{
    CHECK_EQ (hashKey ("gain"), 3165055u);
    CHECK_EQ (hashKey ("waveform"), 604207933u);
    CHECK_EQ (hashKey ("cutoff"), 2945903597u);
    CHECK_EQ (kParamGain, 3165055u);
    CHECK_EQ (kParamWaveform, 604207933u);
    CHECK_EQ (kParamCutoff, 2945903597u);
}

TEST (table_sanity)
{
    const auto* t = paramTable();
    const size_t n = paramCount();
    CHECK (n == 3);
    CHECK (n <= kMaxParams);
    for (size_t i = 0; i < n; ++i)
    {
        CHECK_EQ (t[i].id, hashKey (t[i].key));
        CHECK (t[i].name != nullptr && t[i].name[0] != '\0');
        CHECK (t[i].group != nullptr);
        CHECK (t[i].min < t[i].max);
        CHECK (t[i].def >= t[i].min && t[i].def <= t[i].max);
        CHECK (t[i].interval >= 0.0f);
        CHECK (t[i].skew > 0.0f);
        CHECK (findParam (t[i].id) == &t[i]);
        CHECK (findParamByKey (t[i].key) == &t[i]);
        CHECK_EQ (findParamIndex (t[i].id), static_cast<int> (i));
        CHECK_EQ ((t[i].scale == Scale::stepped), ((t[i].flags & kParamStepped) != 0));
        for (size_t j = i + 1; j < n; ++j)
        {
            CHECK (t[i].id != t[j].id);
            CHECK (std::strcmp (t[i].key, t[j].key) != 0);
        }
    }
    CHECK (findParam (12345u) == nullptr);
    CHECK (findParamByKey ("nope") == nullptr);
    CHECK (findParamByKey (nullptr) == nullptr);
    CHECK_EQ (findParamIndex (0u), -1);

    // spike values
    const auto& gain = *findParam (kParamGain);
    CHECK (gain.min == -60.0f && gain.max == 6.0f && gain.def == -12.0f && gain.skew == 2.5f && gain.interval == 0.1f);
    const auto& wf = *findParam (kParamWaveform);
    CHECK (wf.min == 0.0f && wf.max == 1.0f && wf.def == 1.0f && wf.scale == Scale::stepped);
    CHECK_EQ (choiceCount (wf), 2u);
    CHECK (std::strcmp (choiceName (wf, 0), "Sine") == 0 && std::strcmp (choiceName (wf, 1), "Saw") == 0);
    CHECK (choiceName (wf, 2) == nullptr);
    CHECK_EQ (choiceCount (gain), 0u);
    const auto& cut = *findParam (kParamCutoff);
    CHECK (cut.min == 20.0f && cut.max == 20000.0f && cut.def == 8000.0f && cut.skew == 0.3f && cut.interval == 0.0f);
}

TEST (normalise_known_values)
{
    const auto& gain = *findParam (kParamGain);
    CHECK_NEAR (toNormalised (gain, -12.0f), std::pow (48.0 / 66.0, 2.5), 1e-6);
    CHECK (toNormalised (gain, -60.0f) == 0.0f);
    CHECK (toNormalised (gain, 6.0f) == 1.0f);
    CHECK (toNormalised (gain, -100.0f) == 0.0f);
    CHECK (toNormalised (gain, 100.0f) == 1.0f);
    CHECK (fromNormalised (gain, 0.0f) == -60.0f);
    CHECK (fromNormalised (gain, 1.0f) == 6.0f);
    CHECK (fromNormalised (gain, -3.0f) == -60.0f);

    const auto& cut = *findParam (kParamCutoff);
    CHECK_NEAR (fromNormalised (cut, 0.5f), 20.0 + 19980.0 * std::exp (std::log (0.5) / 0.3), 0.05);
    CHECK_NEAR (toNormalised (cut, 8000.0f), std::pow (7980.0 / 19980.0, 0.3), 1e-6);

    const auto& wf = *findParam (kParamWaveform);
    CHECK (toNormalised (wf, 0.0f) == 0.0f && toNormalised (wf, 1.0f) == 1.0f);
    CHECK (fromNormalised (wf, 0.4f) == 0.0f && fromNormalised (wf, 0.6f) == 1.0f);
}

TEST (normalise_round_trip)
{
    for (size_t pi = 0; pi < paramCount(); ++pi)
    {
        const auto& info = paramTable()[pi];
        for (int i = 0; i <= 1000; ++i)
        {
            const float n = static_cast<float> (i) / 1000.0f;
            const float plain = fromNormalised (info, n);
            CHECK (plain >= info.min && plain <= info.max);
            const float again = fromNormalised (info, toNormalised (info, plain));
            if (! CHECK_NEAR (again, plain, 1e-4 * std::max (1.0, std::fabs (static_cast<double> (plain)))))
                std::printf ("    %s: n=%g plain=%.9g again=%.9g\n", info.key, n, plain, again);
        }
        CHECK (fromNormalised (info, 0.0f) == info.min);
        CHECK (fromNormalised (info, 1.0f) == info.max);
        CHECK (constrainValue (info, info.def) == info.def);
    }
}

TEST (constrain)
{
    const auto& gain = *findParam (kParamGain);
    CHECK (constrainValue (gain, 100.0f) == 6.0f);
    CHECK (constrainValue (gain, -100.0f) == -60.0f);
    CHECK (constrainValue (gain, std::nanf ("")) == -12.0f);
    CHECK (constrainValue (gain, INFINITY) == 6.0f);
    CHECK_NEAR (constrainValue (gain, -12.04f), -12.0, 1e-5);
    CHECK_NEAR (constrainValue (gain, -12.06f), -12.1, 1e-5);
    const auto& wf = *findParam (kParamWaveform);
    CHECK (constrainValue (wf, 0.49f) == 0.0f && constrainValue (wf, 0.51f) == 1.0f && constrainValue (wf, 7.0f) == 1.0f);
}

TEST (text_known_values)
{
    char buf[64];
    const auto& gain = *findParam (kParamGain);
    const auto& wf = *findParam (kParamWaveform);
    const auto& cut = *findParam (kParamCutoff);

    CHECK_EQ (valueToText (gain, -12.0f, buf, sizeof buf), 5u);
    CHECK (std::strcmp (buf, "-12.0") == 0);
    valueToText (cut, 8000.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "8000.0") == 0);
    valueToText (cut, 20.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "20.0") == 0);
    valueToText (gain, 0.04f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "0.0") == 0);
    valueToText (gain, -0.04f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "0.0") == 0); // no "-0.0"
    valueToText (wf, 0.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "Sine") == 0);
    valueToText (wf, 1.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "Saw") == 0);

    // truncation keeps the terminator
    CHECK_EQ (valueToText (gain, -12.0f, buf, 1), 0u);
    CHECK (buf[0] == '\0');
    CHECK_EQ (valueToText (gain, -12.0f, buf, 3), 2u);
    CHECK (std::strcmp (buf, "-1") == 0);
    CHECK_EQ (valueToText (gain, -12.0f, nullptr, 0), 0u);

    float v = 0;
    CHECK (textToValue (gain, "-12.5", v) && v == -12.5f);
    CHECK (textToValue (gain, "  -12.5 dB", v) && v == -12.5f);
    CHECK (textToValue (gain, "+3", v) && v == 3.0f);
    CHECK (textToValue (gain, ".5", v) && v == 0.5f);
    CHECK (textToValue (gain, "1e1", v) && v == 6.0f); // clamped
    CHECK (textToValue (gain, "-1e999", v) && v == -60.0f);
    CHECK (textToValue (cut, "99999", v) && v == 20000.0f);
    CHECK (textToValue (cut, "0", v) && v == 20.0f);
    CHECK (! textToValue (gain, "", v));
    CHECK (! textToValue (gain, "abc", v));
    CHECK (! textToValue (gain, "-", v));
    CHECK (! textToValue (gain, ".", v));
    CHECK (! textToValue (gain, "nan", v));
    CHECK (! textToValue (gain, nullptr, v));
    CHECK (textToValue (wf, "Sine", v) && v == 0.0f);
    CHECK (textToValue (wf, "saw", v) && v == 1.0f);
    CHECK (textToValue (wf, " SAW ", v) && v == 1.0f);
    CHECK (textToValue (wf, "0", v) && v == 0.0f);
    CHECK (textToValue (wf, "1", v) && v == 1.0f);
    CHECK (textToValue (wf, "9", v) && v == 1.0f);
    CHECK (! textToValue (wf, "triangle", v));
}

// The clap-validator param-conversions criterion: value -> text -> value -> text is stable.
TEST (text_round_trip_is_stable)
{
    sbtest::Rng rng (42);
    for (size_t pi = 0; pi < paramCount(); ++pi)
    {
        const auto& info = paramTable()[pi];
        for (int i = 0; i < 1000; ++i)
        {
            // half on the normalised grid, half uniform in plain values
            const float plain = i % 2 == 0 ? fromNormalised (info, static_cast<float> (i) / 999.0f)
                                           : info.min + static_cast<float> (rng.uniform()) * (info.max - info.min);
            char a[64], b[64];
            valueToText (info, plain, a, sizeof a);
            float back = 0.0f;
            bool ok = CHECK (textToValue (info, a, back));
            ok = ok && CHECK (back >= info.min && back <= info.max);
            valueToText (info, back, b, sizeof b);
            if (! CHECK (std::strcmp (a, b) == 0))
                std::printf ("    %s: %.9g -> '%s' -> %.9g -> '%s'\n", info.key, plain, a, back, b);
        }
    }
}

TEST (text_is_locale_independent)
{
    const char* locales[] = { "de_DE.UTF-8", "de_DE", "hu_HU.UTF-8", "fr_FR.UTF-8" };
    const std::string old = std::setlocale (LC_NUMERIC, nullptr);
    for (const char* loc : locales)
    {
        if (std::setlocale (LC_NUMERIC, loc) == nullptr)
            continue;
        char buf[64];
        valueToText (*findParam (kParamGain), -12.5f, buf, sizeof buf);
        CHECK (std::strcmp (buf, "-12.5") == 0);
        float v = 0.0f;
        CHECK (textToValue (*findParam (kParamGain), "-12.5", v) && v == -12.5f);
        number::formatG9 (0.5f, buf, sizeof buf);
        CHECK (std::strcmp (buf, "0.5") == 0);
    }
    std::setlocale (LC_NUMERIC, old.c_str());
}

TEST (format_g9_known_values)
{
    struct Case { float v; const char* text; };
    const Case cases[] = {
        { 0.0f, "0" }, { -0.0f, "0" }, { 1.0f, "1" }, { -12.0f, "-12" }, { 8000.0f, "8000" }, { 0.5f, "0.5" },
        { 0.1f, "0.100000001" }, { 123456789.0f, "123456792" }, { 1.0e9f, "1e+09" }, { 20000.0f, "20000" },
        { 0.0001f, "9.99999975e-05" }, { 1.5e-5f, "1.49999996e-05" }, { 3.0e38f, "3.00000001e+38" },
        { 1.17549435e-38f, "1.17549435e-38" }, { 1.0e-45f, "1.40129846e-45" }, { 0.00012207031f, "0.000122070312" },
    };
    for (const auto& c : cases)
    {
        char buf[64];
        number::formatG9 (c.v, buf, sizeof buf);
        if (! CHECK (std::strcmp (buf, c.text) == 0))
            std::printf ("    got '%s', expected '%s'\n", buf, c.text);
    }
}

#ifndef _WIN32 // older Windows C runtimes round exact ties away from zero in printf; the reference must be exact
TEST (format_matches_printf_on_random_floats)
{
    sbtest::Rng rng (7);
    long mismatches = 0;
    for (int i = 0; i < 300000; ++i)
    {
        float v;
        uint32_t bits = rng.u32();
        if (i % 3 == 0)
        {
            std::memcpy (&v, &bits, sizeof v); // any bit pattern, incl. denormals and huge values
            if (! std::isfinite (v))
                continue;
        }
        else if (i % 3 == 1)
        {
            v = static_cast<float> ((rng.uniform() - 0.5) * 100000.0); // typical parameter values
        }
        else
        {
            v = static_cast<float> (static_cast<int> (rng.u32() % 2000001) - 1000000) / 8.0f; // many exact ties
        }

        char a[128], b[128];
        number::formatG9 (v, a, sizeof a);
        std::snprintf (b, sizeof b, "%.9g", static_cast<double> (v));
        if (v == 0.0f)
            std::snprintf (b, sizeof b, "0");
        if (std::strcmp (a, b) != 0 && mismatches++ < 5)
            std::printf ("    %%.9g mismatch: '%s' vs printf '%s'\n", a, b);

        number::formatFixed1 (v, a, sizeof a);
        std::snprintf (b, sizeof b, "%.1f", static_cast<double> (v));
        if (std::strcmp (b, "-0.0") == 0)
            std::snprintf (b, sizeof b, "0.0");
        if (std::strcmp (a, b) != 0 && mismatches++ < 5)
            std::printf ("    %%.1f mismatch for %.9g: '%s' vs printf '%s'\n", static_cast<double> (v), a, b);
    }
    CHECK_EQ (mismatches, 0L);
}
#endif

TEST (parse_matches_strtod)
{
    sbtest::Rng rng (11);
    long mismatches = 0;
    for (int i = 0; i < 100000; ++i)
    {
        char text[128];
        const double v = (rng.uniform() - 0.5) * std::pow (10.0, static_cast<double> (rng.u32() % 20) - 8.0);
        std::snprintf (text, sizeof text, "%.*g", static_cast<int> (1 + rng.u32() % 17), v);
        double got = 0.0;
        const char* next = nullptr;
        const bool ok = number::parseDecimal (text, text + std::strlen (text), true, got, next);
        const double want = std::strtod (text, nullptr);
        // within 1 ulp: the fast path is exact, long mantissas / large exponents may be off by rounding
        if (! ok || next != text + std::strlen (text) || std::fabs (got - want) > std::fabs (want) * 4.5e-16)
            if (mismatches++ < 5)
                std::printf ("    parse mismatch '%s': %.17g vs %.17g\n", text, got, want);
    }
    CHECK_EQ (mismatches, 0L);
}

TEST (parse_float_round_trip_through_text)
{
    sbtest::Rng rng (13);
    for (int i = 0; i < 200000; ++i)
    {
        float v;
        const uint32_t bits = rng.u32();
        std::memcpy (&v, &bits, sizeof v);
        if (! std::isfinite (v))
            continue;
        char text[64];
        number::formatG9 (v, text, sizeof text);
        double d = 0.0;
        const char* next = nullptr;
        if (! number::parseDecimal (text, text + std::strlen (text), true, d, next) || static_cast<float> (d) != (v == 0.0f ? 0.0f : v))
        {
            CHECK (false);
            std::printf ("    '%s' did not round trip (%.9g)\n", text, static_cast<double> (v));
            break;
        }
    }
}

TEST (parse_grammar)
{
    struct Case { const char* text; bool strictOk; bool lenientOk; };
    const Case cases[] = {
        { "0", true, true }, { "-0", true, true }, { "12", true, true }, { "12.5", true, true }, { "1e5", true, true },
        { "1E+5", true, true }, { "1e-5", true, true }, { "-1.5e3", true, true }, { "01", false, true }, { "+1", false, true },
        { ".5", false, true }, { "5.", false, true }, { "-", false, false }, { ".", false, false }, { "", false, false },
        { "e5", false, false }, { "abc", false, false }, { "1e", false, true }, { "NaN", false, false }, { "Infinity", false, false },
    };
    for (const auto& c : cases)
    {
        double v = 0.0;
        const char* next = nullptr;
        const size_t n = std::strlen (c.text);
        const bool strict = number::parseDecimal (c.text, c.text + n, true, v, next);
        const bool lenient = number::parseDecimal (c.text, c.text + n, false, v, next);
        const bool okStrict = CHECK_EQ (strict, c.strictOk);
        const bool okLenient = CHECK_EQ (lenient, c.lenientOk);
        if (! (okStrict && okLenient))
            std::printf ("    text '%s'\n", c.text);
    }

    double v = 0.0;
    const char* next = nullptr;
    auto parseStrict = [&] (const std::string& t) { return number::parseDecimal (t.c_str(), t.c_str() + t.size(), true, v, next); };
    CHECK (parseStrict ("1e999") && std::isinf (v) && v > 0);
    CHECK (parseStrict ("-1e999") && std::isinf (v) && v < 0);
    CHECK (parseStrict ("1e-999") && v == 0.0);
    const std::string longDigits = std::string (400, '9');
    CHECK (number::parseDecimal (longDigits.c_str(), longDigits.c_str() + longDigits.size(), true, v, next) && std::isinf (v));
    const std::string tinyDigits = "0." + std::string (400, '0') + "1";
    CHECK (number::parseDecimal (tinyDigits.c_str(), tinyDigits.c_str() + tinyDigits.size(), true, v, next) && v == 0.0);
    // bounded: never reads past end
    const char buf[4] = { '1', '2', '3', '4' };
    CHECK (number::parseDecimal (buf, buf + 2, true, v, next) && v == 12.0 && next == buf + 2);
}
