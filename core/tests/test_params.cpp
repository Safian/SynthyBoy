// SynthyBoy - core tests: key hash, parameter table, normalisation, text conversion, number formatting/parsing.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "number.h"
#include "synthyboy/hash.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

static_assert (hashKey ("gain") == 3165055u, "hashKey must match juce::String::hashCode");
static_assert (hashKey ("") == 0u, "empty key");

namespace
{
const ParamInfo& byKey (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    static const ParamInfo none {};
    return p != nullptr ? *p : none;
}

bool isLowerKeyChar (char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/** key grammar: lowercase dotted, `[a-z][a-z0-9_]*(.[a-z0-9_]+)+` (at least two segments) */
bool keyFollowsGrammar (const char* key)
{
    const size_t n = std::strlen (key);
    if (n == 0 || key[0] < 'a' || key[0] > 'z')
        return false;
    int dots = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (key[i] == '.')
        {
            if (i == 0 || i + 1 == n || key[i + 1] == '.')
                return false;
            ++dots;
        }
        else if (! isLowerKeyChar (key[i]))
        {
            return false;
        }
    }
    return dots >= 1;
}

bool groupFollowsGrammar (const char* group)
{
    // levels separated by " / ", no empty level, no leading or trailing blank
    const std::string g = group;
    if (g.empty() || g.front() == ' ' || g.back() == ' ')
        return false;
    size_t pos = 0;
    while (true)
    {
        const size_t next = g.find (" / ", pos);
        const std::string level = g.substr (pos, next == std::string::npos ? std::string::npos : next - pos);
        if (level.empty() || level.front() == '/' || level.back() == '/')
            return false;
        if (next == std::string::npos)
            return true;
        pos = next + 3;
    }
}
} // namespace

TEST (hash_matches_juce_string_hashcode)
{
    CHECK_EQ (hashKey ("gain"), 3165055u);
    CHECK_EQ (hashKey ("waveform"), 604207933u);
    CHECK_EQ (hashKey ("cutoff"), 2945903597u);
    CHECK_EQ (kParamMasterVolume, hashKey ("master.volume"));
    CHECK_EQ (kParamFilter1Cutoff, hashKey ("filter1.cutoff"));
    CHECK_EQ (kParamOsc1Wavetable, hashKey ("osc1.wavetable"));
}

TEST (table_sanity)
{
    const auto* t = paramTable();
    const size_t n = paramCount();
    CHECK (n >= 800);
    CHECK (n <= kMaxParams);
    std::printf ("    %zu parameters\n", n);

    std::set<uint32_t> ids;
    std::set<std::string> keys, names;
    for (size_t i = 0; i < n; ++i)
    {
        const auto& p = t[i];
        const bool ok = CHECK_EQ (p.id, hashKey (p.key)) && CHECK (keyFollowsGrammar (p.key)) && CHECK (groupFollowsGrammar (p.group));
        if (! ok)
            std::printf ("    bad entry %zu: key '%s' group '%s'\n", i, p.key, p.group);

        // no hash collision, no duplicated key or display name (the destination choice lists the names)
        CHECK (ids.insert (p.id).second);
        CHECK (keys.insert (p.key).second);
        CHECK (names.insert (p.name).second);

        CHECK (p.name != nullptr && p.name[0] != '\0');
        CHECK (p.group != nullptr && p.group[0] != '\0');
        CHECK (p.min < p.max);
        CHECK (p.def >= p.min && p.def <= p.max);
        CHECK (p.interval >= 0.0f);
        CHECK (p.skew > 0.0f);
        CHECK_EQ (static_cast<size_t> (p.order), i);
        CHECK (findParam (p.id) == &p);
        CHECK (findParamByKey (p.key) == &p);
        CHECK_EQ (findParamIndex (p.id), static_cast<int> (i));
        CHECK_EQ ((p.scale == Scale::stepped), ((p.flags & kParamStepped) != 0));
        CHECK_EQ ((p.scale == Scale::stepped), (p.choiceSet != ChoiceSet::none));
        CHECK ((p.flags & kParamAutomatable) != 0);
        CHECK ((p.flags & kParamModulatable) == 0 || p.scale != Scale::stepped); // only continuous / integer parameters are destinations
        CHECK (p.decimals <= 4);
    }
    CHECK (findParam (12345u) == nullptr);
    CHECK (findParamByKey ("nope") == nullptr);
    CHECK (findParamByKey (nullptr) == nullptr);
    CHECK_EQ (findParamIndex (0u), -1);
    CHECK (findParamByKey ("gain") == nullptr); // the keys of format version 1 are gone (migration: core/src/state.cpp)
}

TEST (choice_names_match_the_range)
{
    size_t stepped = 0;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const auto& p = paramTable()[i];
        if (p.scale != Scale::stepped)
        {
            CHECK_EQ (choiceCount (p), 0u);
            CHECK (choiceName (p, 0) == nullptr);
            continue;
        }
        ++stepped;
        const size_t count = choiceCount (p);
        // the range is exactly 0 .. count - 1, step 1, and every name exists, is not empty and is unique within the parameter
        const bool ok = CHECK (p.min == 0.0f) && CHECK (p.interval == 1.0f) && CHECK (count >= 2)
                        && CHECK_EQ (static_cast<size_t> (p.max), count - 1) && CHECK (p.def >= 0.0f && p.def <= p.max);
        if (! ok)
            std::printf ("    %s: range 0..%g, %zu choices\n", p.key, static_cast<double> (p.max), count);
        std::set<std::string> seen;
        for (size_t k = 0; k < count; ++k)
        {
            const char* name = choiceName (p, k);
            CHECK (name != nullptr && name[0] != '\0');
            if (name != nullptr)
                CHECK (seen.insert (name).second);
        }
        CHECK (choiceName (p, count) == nullptr);
    }
    CHECK (stepped > 300); // switches, models, the 128 source / destination choices of the matrix, ...

    // the built-in lists have the sizes the engines build
    CHECK_EQ (choiceCount (byKey ("osc1.wavetable")), static_cast<size_t> (kNumWavetableChoices));
    CHECK_EQ (choiceCount (byKey ("lfo8.shape")), static_cast<size_t> (kNumLfoShapes + 1));
    CHECK_EQ (choiceCount (byKey ("fx.order.9")), static_cast<size_t> (kNumFxSlots));
    CHECK (std::strcmp (choiceName (byKey ("osc1.wavetable"), kWavetableSine), "Sine") == 0);
    CHECK (std::strcmp (choiceName (byKey ("osc3.wavetable"), kWavetableSaw), "Saw") == 0);
    CHECK_EQ (choiceCount (byKey ("mod01.source")), 1 + modSourceCount());
    CHECK_EQ (choiceCount (byKey ("mod64.destination")), 1 + modDestinationCount());
}

TEST (modulation_destinations_are_listed)
{
    size_t modulatable = 0;
    std::set<std::string> labels;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const auto& p = paramTable()[i];
        const size_t choice = modDestinationChoiceOf (i);
        if ((p.flags & kParamModulatable) == 0)
        {
            if (choice != 0u)
            {
                CHECK (choice >= 454 && choice <= 519);
            }
            continue;
        }
        ++modulatable;
        // listed once, in table order: choice k is the k-th modulatable parameter and carries its name; the 66 appended destinations of block B20
        // (64 mod amounts, portamento time, polyphony) sit after key 1595, so the modulatable keys of later blocks (B22) continue at 520
        CHECK_EQ (choice, modulatable + (i > 1595 ? 66u : 0u));
        CHECK_EQ (modDestinationParamIndex (choice), static_cast<int> (i));
        const auto& dest = byKey ("mod01.destination");
        CHECK (std::strcmp (choiceName (dest, choice), p.name) == 0);
        CHECK (labels.insert (p.name).second);
    }
    CHECK_EQ (modulatable, static_cast<size_t> (453 + 384 + 3 + 2 + 3 + 51 + 21 + 3 + 256)); // + 3 of B23, 2 of B25 // 384: the 16 x 24 parameters fxiNN.pKK of block B22
    CHECK_EQ (modDestinationCount(), static_cast<size_t> (519 + 384 + 3 + 2 + 3 + 51 + 21 + 3 + 256));
    CHECK_EQ (byKey ("mod17.destination").max, static_cast<float> (519 + 384 + 3 + 2 + 3 + 51 + 21 + 3 + 256));
    CHECK (modulatable > 200);
    CHECK_EQ (modDestinationParamIndex (0), -1);
    CHECK_EQ (modDestinationParamIndex (modDestinationCount() + 1), -1);
    CHECK (std::strcmp (choiceName (byKey ("mod01.destination"), 0), "None") == 0);

    // a destination text parses back to its choice
    float v = 0.0f;
    CHECK (textToValue (byKey ("mod02.destination"), "Filter 1 Cutoff", v));
    CHECK_EQ (static_cast<int> (v), static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (kParamFilter1Cutoff)))));

    // the sources
    CHECK_EQ (modSourceCount(), static_cast<size_t> (kNumModSources));
    std::set<std::string> keys;
    for (size_t k = 0; k < modSourceCount(); ++k)
    {
        CHECK (keys.insert (modSourceTable()[k].key).second);
        CHECK (std::strcmp (choiceName (byKey ("mod05.source"), k + 1), modSourceTable()[k].name) == 0);
    }
    CHECK (std::strcmp (choiceName (byKey ("mod05.source"), 0), "None") == 0);

    // every slot has its seven parameters
    for (int slot = 0; slot < kNumModSlots; ++slot)
    {
        const auto s = modSlotParams (slot);
        for (const int idx : { s.source, s.destination, s.amount, s.power, s.bipolar, s.stereo, s.bypass })
            CHECK (idx >= 0);
    }
    CHECK_EQ (modSlotParams (-1).source, -1);
    CHECK_EQ (modSlotParams (kNumModSlots).source, -1);
}

TEST (groups_are_a_hierarchy_of_non_empty_levels)
{
    std::set<std::string> topLevels;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const std::string g = paramTable()[i].group;
        topLevels.insert (g.substr (0, g.find (" / ")));
    }
    for (const char* expected : { "Master", "Osc 1", "Osc 2", "Osc 3", "Noise", "Filter 1", "Filter 2", "FX Filter", "Envelope 6", "LFO 8",
                                  "Random LFO 4", "Macros", "Mod Matrix", "FX" })
        CHECK (topLevels.count (expected) == 1);
}

TEST (default_patch_is_the_clean_basic_patch)
{
    // the defaults (docs/PARAMETERS.md "default patch"): one saw oscillator through an open filter at -12 dB, nothing else on
    CHECK_EQ (byKey ("master.volume").def, -12.0f);
    CHECK_EQ (byKey ("osc1.on").def, 1.0f);
    CHECK_EQ (byKey ("osc2.on").def, 0.0f);
    CHECK_EQ (byKey ("osc3.on").def, 0.0f);
    CHECK_EQ (byKey ("noise.on").def, 0.0f);
    CHECK_EQ (byKey ("osc1.wavetable").def, static_cast<float> (kWavetableSaw));
    CHECK_EQ (byKey ("osc1.unison.voices").def, 1.0f);
    CHECK_EQ (byKey ("filter1.on").def, 1.0f);
    CHECK (byKey ("filter1.cutoff").def >= 10000.0f);
    CHECK_EQ (byKey ("filter2.on").def, 0.0f);
    CHECK_EQ (byKey ("fxfilter.on").def, 0.0f);
    CHECK (byKey ("env1.attack").def > 0.0f && byKey ("env1.attack").def <= 10.0f);
    CHECK_EQ (byKey ("env1.sustain").def, 100.0f);
    for (const char* fx : { "chorus", "compressor", "delay", "distortion", "eq", "flanger", "phaser", "reverb" })
        CHECK_EQ (byKey ((std::string (fx) + ".on").c_str()).def, 0.0f);
    for (int slot = 1; slot <= kNumModSlots; ++slot)
    {
        char key[32];
        std::snprintf (key, sizeof key, "mod%02d.source", slot);
        CHECK_EQ (byKey (key).def, 0.0f);
        std::snprintf (key, sizeof key, "mod%02d.destination", slot);
        CHECK_EQ (byKey (key).def, 0.0f);
    }
    // the default FX order is a permutation of the nine effects
    std::set<int> order;
    for (int slot = 1; slot <= kNumFxSlots; ++slot)
        order.insert (static_cast<int> (byKey (("fx.order." + std::to_string (slot)).c_str()).def));
    CHECK_EQ (order.size(), static_cast<size_t> (kNumFxSlots));
}

TEST (normalise_known_values)
{
    const auto& vol = byKey ("master.volume");
    CHECK_NEAR (toNormalised (vol, -12.0f), std::pow (48.0 / 66.0, 2.5), 1e-6);
    CHECK (toNormalised (vol, -60.0f) == 0.0f);
    CHECK (toNormalised (vol, 6.0f) == 1.0f);
    CHECK (toNormalised (vol, -100.0f) == 0.0f);
    CHECK (toNormalised (vol, 100.0f) == 1.0f);
    CHECK (fromNormalised (vol, 0.0f) == -60.0f);
    CHECK (fromNormalised (vol, 1.0f) == 6.0f);
    CHECK (fromNormalised (vol, -3.0f) == -60.0f);

    const auto& cut = byKey ("filter1.cutoff");
    CHECK_NEAR (fromNormalised (cut, 0.5f), 20.0 + 19980.0 * std::exp (std::log (0.5) / 0.3), 0.05);
    CHECK_NEAR (toNormalised (cut, 8000.0f), std::pow (7980.0 / 19980.0, 0.3), 1e-6);

    const auto& wt = byKey ("osc1.wavetable");
    CHECK (toNormalised (wt, 0.0f) == 0.0f && toNormalised (wt, wt.max) == 1.0f);
    CHECK (fromNormalised (wt, 0.0f) == 0.0f && fromNormalised (wt, 1.0f) == wt.max);
    CHECK_EQ (fromNormalised (wt, 0.5f), 7.0f); // 14 choices (13 built-in tables and Preset table): 6.5 rounds up
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
    const auto& vol = byKey ("master.volume");
    CHECK (constrainValue (vol, 100.0f) == 6.0f);
    CHECK (constrainValue (vol, -100.0f) == -60.0f);
    CHECK (constrainValue (vol, std::nanf ("")) == -12.0f);
    CHECK (constrainValue (vol, INFINITY) == 6.0f);
    CHECK_NEAR (constrainValue (vol, -12.04f), -12.04, 1e-5); // continuous: no grid
    CHECK_NEAR (constrainValue (byKey ("master.bend_range"), 2.4f), 2.0, 1e-5); // integer parameters snap
    const auto& wt = byKey ("osc1.wavetable");
    CHECK (constrainValue (wt, 0.49f) == 0.0f && constrainValue (wt, 0.51f) == 1.0f && constrainValue (wt, 70.0f) == wt.max);
    const auto& voices = byKey ("osc1.unison.voices");
    CHECK (constrainValue (voices, 7.4f) == 7.0f && constrainValue (voices, 99.0f) == 16.0f && constrainValue (voices, 0.0f) == 1.0f);
}

TEST (text_known_values)
{
    char buf[64];
    const auto& vol = byKey ("master.volume");
    const auto& wt = byKey ("osc1.wavetable");
    const auto& cut = byKey ("filter1.cutoff");

    CHECK_EQ (valueToText (vol, -12.0f, buf, sizeof buf), 5u);
    CHECK (std::strcmp (buf, "-12.0") == 0);
    valueToText (cut, 8000.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "8000.0") == 0);
    valueToText (cut, 20.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "20.0") == 0);
    valueToText (vol, 0.04f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "0.0") == 0);
    valueToText (vol, -0.04f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "0.0") == 0); // no "-0.0"
    valueToText (wt, 0.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "Sine") == 0);
    valueToText (wt, 1.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "Saw") == 0);
    valueToText (byKey ("lfo1.rate"), 0.05f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "0.05") == 0);   // slow rates keep their decimals
    valueToText (byKey ("osc1.phase"), 179.6f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "180") == 0);
    valueToText (byKey ("osc1.unison.voices"), 7.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "7") == 0);
    valueToText (byKey ("mod01.source"), 7.0f, buf, sizeof buf);
    CHECK (std::strcmp (buf, "LFO 1") == 0);

    // truncation keeps the terminator
    CHECK_EQ (valueToText (vol, -12.0f, buf, 1), 0u);
    CHECK (buf[0] == '\0');
    CHECK_EQ (valueToText (vol, -12.0f, buf, 3), 2u);
    CHECK (std::strcmp (buf, "-1") == 0);
    CHECK_EQ (valueToText (vol, -12.0f, nullptr, 0), 0u);

    float v = 0;
    CHECK (textToValue (vol, "-12.5", v) && v == -12.5f);
    CHECK (textToValue (vol, "  -12.5 dB", v) && v == -12.5f);
    CHECK (textToValue (vol, "+3", v) && v == 3.0f);
    CHECK (textToValue (vol, ".5", v) && v == 0.5f);
    CHECK (textToValue (vol, "1e1", v) && v == 6.0f); // clamped
    CHECK (textToValue (vol, "-1e999", v) && v == -60.0f);
    CHECK (textToValue (cut, "99999", v) && v == 20000.0f);
    CHECK (textToValue (cut, "0", v) && v == 20.0f);
    CHECK (! textToValue (vol, "", v));
    CHECK (! textToValue (vol, "abc", v));
    CHECK (! textToValue (vol, "-", v));
    CHECK (! textToValue (vol, ".", v));
    CHECK (! textToValue (vol, "nan", v));
    CHECK (! textToValue (vol, nullptr, v));
    CHECK (textToValue (wt, "Sine", v) && v == 0.0f);
    CHECK (textToValue (wt, "saw", v) && v == 1.0f);
    CHECK (textToValue (wt, " SAW ", v) && v == 1.0f);
    CHECK (textToValue (wt, "0", v) && v == 0.0f);
    CHECK (textToValue (wt, "1", v) && v == 1.0f);
    CHECK (textToValue (wt, "99", v) && v == wt.max);
    CHECK (textToValue (wt, "Bell Partials", v) && v == 12.0f);
    CHECK (! textToValue (wt, "didgeridoo", v));
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
        valueToText (byKey ("master.volume"), -12.5f, buf, sizeof buf);
        CHECK (std::strcmp (buf, "-12.5") == 0);
        float v = 0.0f;
        CHECK (textToValue (byKey ("master.volume"), "-12.5", v) && v == -12.5f);
        valueToText (byKey ("lfo1.rate"), 0.25f, buf, sizeof buf);
        CHECK (std::strcmp (buf, "0.25") == 0);
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

TEST (format_fixed_matches_printf)
{
    sbtest::Rng rng (21);
    long mismatches = 0;
    for (int decimals = 0; decimals <= 4; ++decimals)
        for (int i = 0; i < 100000; ++i)
        {
            float v;
            if (i % 2 == 0)
                v = static_cast<float> ((rng.uniform() - 0.5) * 20000.0);
            else
                v = static_cast<float> (static_cast<int> (rng.u32() % 400001) - 200000) / 1000.0f; // many decimal ties
            char a[128], b[128];
            number::formatFixed (v, decimals, a, sizeof a);
            std::snprintf (b, sizeof b, "%.*f", decimals, static_cast<double> (v));
            if (std::strcmp (b, "-0") == 0 || std::strcmp (b, "-0.0") == 0 || std::strcmp (b, "-0.00") == 0 || std::strcmp (b, "-0.000") == 0
                || std::strcmp (b, "-0.0000") == 0)
                std::memmove (b, b + 1, std::strlen (b)); // a result of zero has no sign
            if (std::strcmp (a, b) != 0 && mismatches++ < 5)
                std::printf ("    %%.%df mismatch for %.9g: '%s' vs printf '%s'\n", decimals, static_cast<double> (v), a, b);
        }
    CHECK_EQ (mismatches, 0L);
}
