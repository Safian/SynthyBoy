// SynthyBoy - core tests: state version 3 (docs/ui-design/13-new-keys.md 5 and 10): the v2 -> v3 migration, round trips of all 1594 keys,
// the additive `content` section (round trip, rejection, fuzz), presets with content, the factory presets of version 2.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "golden_audio.h"
#include "golden_v2.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
uint64_t fnv (const std::string& s)
{
    uint64_t h = 14695981039346656037ull;
    for (const unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

/** The document with every non-integer number cut to 3 significant digits. The values of the "busy" patch come out of std::exp / std::log (skewed parameters), whose last
    bit differs between the C libraries (glibc, MSVC, Apple, ARM glibc): the shortest text of such a float differs in length and digits there (Windows and ARMv7 did). The
    exact text of the defaults (no libm involved) is fingerprinted as it is; the busy patch is fingerprinted by its structure (keys, order, integers, strings, rounded
    numbers), and its values are compared exactly, on the platform's own numbers, by the round trip below. */
std::string roundedNumbers (const std::string& text)
{
    std::string out;
    size_t i = 0;
    while (i < text.size())
    {
        const char c = text[i];
        if (c == '"')
        {
            size_t j = i + 1;
            while (j < text.size() && text[j] != '"')
                j += text[j] == '\\' ? 2 : 1;
            out.append (text, i, j + 1 - i);
            i = j + 1;
        }
        else if (c == '-' || (c >= '0' && c <= '9'))
        {
            size_t j = i + 1;
            while (j < text.size() && (std::isdigit (static_cast<unsigned char> (text[j])) || text[j] == '.' || text[j] == 'e' || text[j] == 'E' || text[j] == '+' || text[j] == '-'))
                ++j;
            const std::string token = text.substr (i, j - i);
            if (token.find_first_of (".eE") == std::string::npos)
                out += token;
            else
            {
                char buffer[32];
                std::snprintf (buffer, sizeof buffer, "%.3g", std::strtod (token.c_str(), nullptr));
                out += buffer;
            }
            i = j;
        }
        else
        {
            out += c;
            ++i;
        }
    }
    return out;
}

bool sameValues (const ParamValues& a, const ParamValues& b)
{
    return std::memcmp (a.plain, b.plain, sizeof a.plain) == 0;
}

int indexOfKey (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? findParamIndex (p->id) : -1;
}

/** What the writer of state version 2 produced for these values: the version 3 text without the 604 appended keys. */
std::string asVersion2 (const std::string& v3)
{
    std::string s = v3;
    const size_t v = s.find ("\"version\":3");
    s.replace (v, 11, "\"version\":2");
    const size_t cut = s.find (",\"macro.5\":");
    s.erase (cut);
    s += "}}";
    return s;
}

ParamValues busyValues()
{
    auto v = ParamValues::defaults();
    const auto busy = sbgolden::busyPlainValues();
    for (size_t i = 0; i < busy.size(); ++i)
        v.plain[i] = busy[i];
    return v;
}

/** Every parameter of the table at a random value of its own range (new keys included), plus the exact extremes now and then. */
ParamValues randomValues (sbtest::Rng& rng)
{
    ParamValues v;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const auto& info = paramTable()[i];
        const uint32_t pick = rng.u32() % 8;
        const float u = pick == 0 ? 0.0f : (pick == 1 ? 1.0f : static_cast<float> (rng.uniform()));
        v.plain[i] = fromNormalised (info, u);
    }
    return v;
}

ContentPoint point (float x, float y, float power = 0.0f)
{
    ContentPoint p;
    p.x = x;
    p.y = y;
    p.power = power;
    return p;
}

ContentFileRef ref (const std::string& pack, const std::string& file, const std::string& name)
{
    ContentFileRef r;
    r.pack = pack;
    r.file = file;
    r.sha256 = std::string (64, 'a');
    r.name = name;
    return r;
}

StateContent fullContent()
{
    StateContent c;
    ContentWavetable w;
    w.osc = 2;
    w.ref = ref ("factory.wavetables", "tables/Gl\xc3\xbc" "he.sbwt", "Gl\xc3\xbc" "he \"quoted\" \\ / slash");
    w.author = "Safian Szabolcs";
    w.frames = 256;
    c.wavetables.push_back (w);
    ContentSample s;
    s.osc = 3;
    s.ref = ref ("user", "samples/kick.wav", "Kick");
    s.sampleRate = 48000;
    s.frames = 24000;
    s.channels = 2;
    s.slices = { 0.0f, 0.25f, 0.5f, 0.75f };
    c.samples.push_back (s);

    ContentLfoShape l;
    l.lfo = 9;
    l.name = "Wobble";
    l.smooth = true;
    l.loopPoint = 1;
    for (int i = 0; i < 5; ++i)
        l.points.push_back (point (static_cast<float> (i) / 4.0f, (i % 2) != 0 ? 1.0f : 0.0f, 0.5f * static_cast<float> (i) - 1.0f));
    l.points[1].modX = { true, "lfo2", 0.25f };
    l.points[2].modY = { true, "mod_wheel", -0.5f };
    c.lfoShapes.push_back (l);
    ContentLfoShape l2 = l;
    l2.lfo = 1;
    l2.name = "Second";
    l2.loopPoint = -1;
    l2.points.resize (3);
    l2.points[2].x = 1.0f;
    c.lfoShapes.push_back (l2);

    c.velocityCurve = { point (0.0f, 0.0f), point (0.5f, 0.8f, 2.0f), point (1.0f, 1.0f) };
    c.noteCurve = { point (0.0f, 0.1f), point (1.0f, 0.9f) };
    c.shaperA = { point (0.0f, 0.0f), point (0.4f, 0.7f, -1.5f), point (1.0f, 0.9f) }; // B17: the X-Shaper curves
    c.shaperB = { point (0.0f, 0.0f), point (1.0f, 1.0f) };
    c.tuning.present = true;
    c.tuning.scl = "! test.scl\n12 tone\n 12\n!\n 100.0\n 200.0\n 300.0\n 400.0\n 500.0\n 600.0\n 700.0\n 800.0\n 900.0\n 1000.0\n 1100.0\n 2/1\n";
    c.tuning.hasKbm = true;
    c.tuning.kbm = "! map\n12\n0\n127\n60\n69\n440.0\n12\n";
    c.impulses.push_back (ref ("factory.impulses", "ir/hall.wav", "Hall"));
    c.filterCurves.push_back ({ "filter1", { { 0.0f, 0.0f }, { 0.5f, 6.0f }, { 1.0f, -12.0f } } });
    c.filterCurves.push_back ({ "fxfilter", { { 0.0f, 0.0f }, { 1.0f, 0.0f } } });
    ContentModCurve mc; // B20: the per-connection modulation curves
    mc.slot = 7;
    mc.points = { point (0.0f, 0.0f), point (0.3f, 0.9f, -2.5f), point (1.0f, 0.2f) };
    c.modCurves.push_back (mc);
    mc.slot = 64;
    mc.points = { point (0.0f, 1.0f), point (1.0f, 0.0f) };
    c.modCurves.push_back (mc);
    return c;
}

std::string stateText (const StateContent& c, const ParamValues& v = ParamValues::defaults())
{
    return saveState (v, c);
}
} // namespace

TEST (version_2_document_loads_into_version_3_with_the_old_values)
{
    // the transformed text is byte for byte what the writer of version 2 produced (fingerprints taken from commit 9c97a69)
    const auto defaults2 = asVersion2 (saveState (ParamValues::defaults()));
    CHECK_EQ (fnv (defaults2), 0xac26fc69f32f2e64ull);
    CHECK_EQ (defaults2.size(), static_cast<size_t> (16637));
    const auto busy2 = asVersion2 (saveState (busyValues()));
    const auto busy2Rounded = roundedNumbers (busy2);
    // REFCOMPARE round 3 (B34, deliberate): the busy values are drawn from the ranges, and the ranges of the EQ gains and of the compressor thresholds are wider: 0x625f51a76b665871 / 17976 bytes before
    CHECK_EQ (fnv (busy2Rounded), 0xa3c90049178e7f62ull);
    CHECK_EQ (busy2Rounded.size(), static_cast<size_t> (17973));

    // loaded into a ParamValues whose new keys all hold other values than their defaults (a previous version 3 patch)
    sbtest::Rng rng (77);
    for (const std::string* doc : { &defaults2, &busy2 })
    {
        const auto garbage = randomValues (rng);
        auto v = garbage;
        const auto r = loadState (doc->data(), doc->size(), v);
        CHECK (r.status == LoadStatus::ok);
        CHECK_EQ (r.version, 2);
        CHECK_EQ (r.applied, static_cast<int> (kNumKeysV2)); // the seven derived fx.order slots are not counted
        CHECK (r.content == ContentStatus::absent);

        const auto expected = doc == &defaults2 ? ParamValues::defaults() : busyValues();
        for (size_t i = 0; i < kNumKeysV2; ++i)
            CHECK_EQ (v.plain[i], constrainValue (paramTable()[i], expected.plain[i])); // same values as the version 2 loader gave
        // the new keys keep what the caller had, except the explicit fx.order.10 .. 16 (identity)
        for (size_t i = kNumKeysV2; i < paramCount(); ++i)
        {
            const std::string key = paramTable()[i].key;
            const int slot = key.compare (0, 9, "fx.order.") == 0 ? std::atoi (key.c_str() + 9) : 0;
            if (slot >= 10)
                CHECK_EQ (v.plain[i], static_cast<float> (slot - 1));
            else
                CHECK_EQ (v.plain[i], garbage.plain[i]);
        }

        // saved again it is a version 3 document whose first 880 entries read exactly like the version 2 text
        auto neutral = ParamValues::defaults();
        for (size_t i = 0; i < kNumKeysV2; ++i)
            neutral.plain[i] = v.plain[i];
        CHECK (asVersion2 (saveState (neutral)).compare (0, doc->size() - 2, *doc, 0, doc->size() - 2) == 0);
    }
}

TEST (version_1_document_goes_through_both_migrations)
{
    const char* doc = "{\"format\":\"synthyboy-state\",\"version\":1,\"params\":{\"gain\":-6,\"waveform\":1,\"cutoff\":300}}";
    auto v = ParamValues::defaults();
    v.plain[indexOfKey ("fx.order.12")] = 3.0f;
    const auto r = loadState (doc, std::strlen (doc), v);
    CHECK (r.status == LoadStatus::ok && r.version == 1 && r.applied == 4);
    CHECK_EQ (v.plain[indexOfKey ("fx.order.12")], 11.0f);
    CHECK_EQ (v.plain[indexOfKey ("filter1.cutoff")], 300.0f);
}

TEST (version_3_keys_are_never_touched_by_the_migration)
{
    // a version 3 document with fx.order.10 = 3 keeps it (the migration runs for versions below 3 only)
    auto v = ParamValues::defaults();
    v.plain[indexOfKey ("fx.order.10")] = 3.0f;
    v.plain[indexOfKey ("fx.order.1")] = 14.0f;
    const auto text = saveState (v);
    auto loaded = ParamValues::defaults();
    CHECK (loadState (text.data(), text.size(), loaded).status == LoadStatus::ok);
    CHECK_EQ (loaded.plain[indexOfKey ("fx.order.10")], 3.0f);
    CHECK_EQ (loaded.plain[indexOfKey ("fx.order.1")], 14.0f);
}

TEST (all_keys_round_trip_with_exact_floats)
{
    sbtest::Rng rng (2024);
    for (int round = 0; round < 40; ++round)
    {
        const auto v = randomValues (rng);
        const auto text = saveState (v);
        CHECK (text.size() < kMaxStateBytes);
        auto loaded = ParamValues::defaults();
        const auto r = loadState (text.data(), text.size(), loaded);
        CHECK (r.status == LoadStatus::ok && r.applied == static_cast<int> (paramCount()) && r.content == ContentStatus::absent);
        for (size_t i = 0; i < paramCount(); ++i)
            if (loaded.plain[i] != constrainValue (paramTable()[i], v.plain[i]) )
            {
                CHECK (false);
                std::printf ("    %s: %.9g -> %.9g\n", paramTable()[i].key, static_cast<double> (v.plain[i]), static_cast<double> (loaded.plain[i]));
                break;
            }
        CHECK (saveState (loaded) == text); // byte stable
    }
}

TEST (content_round_trip_is_exact_and_byte_stable)
{
    const auto content = fullContent();
    CHECK (validateContent (content));
    sbtest::Rng rng (5);
    const auto v = randomValues (rng);
    const auto text = stateText (content, v);
    CHECK (text.find ("\"content\":{\"wavetables\":[{\"osc\":2,") != std::string::npos);
    CHECK (text.find ("\"version\":3") != std::string::npos);
    CHECK (text.size() < kMaxStateBytes);

    auto loaded = ParamValues::defaults();
    StateContent got;
    got.tuning.present = true; // stale content must be replaced
    const auto r = loadStateWithContent (text.data(), text.size(), loaded, got);
    CHECK (r.status == LoadStatus::ok && r.content == ContentStatus::ok && r.version == 3);
    CHECK (got == content);
    CHECK (stateText (got, loaded) == text);

    // loadState () takes the parameters and skips the section
    auto plain = ParamValues::defaults();
    const auto r2 = loadState (text.data(), text.size(), plain);
    CHECK (r2.status == LoadStatus::ok && r2.content == ContentStatus::ignored && sameValues (plain, loaded));

    // no content: the section is absent from the text, and a loaded document replaces (clears) the old content
    const auto bare = saveState (v);
    CHECK (bare.find ("content") == std::string::npos);
    CHECK (saveState (v, StateContent {}) == bare);
    StateContent stale = fullContent();
    const auto r3 = loadStateWithContent (bare.data(), bare.size(), loaded, stale);
    CHECK (r3.status == LoadStatus::ok && r3.content == ContentStatus::absent && stale.empty());

    // an invalid document leaves everything alone
    StateContent keep = fullContent();
    auto before = loaded;
    CHECK (loadStateWithContent ("{nonsense", 9, loaded, keep).status == LoadStatus::invalid);
    CHECK (keep == fullContent() && sameValues (before, loaded));
    CHECK (loadStateWithContent (nullptr, 0, loaded, keep).status == LoadStatus::empty && keep == fullContent());

    // strings with control characters, quotes and non-ASCII text survive; \u escapes decode to UTF-8
    StateContent odd;
    odd.impulses.push_back (ref ("p", "f", "c\"\\/ \xe2\x82\xac \xf0\x9f\x8e\xb9"));
    odd.tuning.present = true;
    odd.tuning.scl = std::string ("line\n\r\ttab\"quote\\ \xe2\x82\xac");
    const auto oddText = stateText (odd);
    StateContent oddBack;
    auto tmp = ParamValues::defaults();
    CHECK (loadStateWithContent (oddText.data(), oddText.size(), tmp, oddBack).content == ContentStatus::ok && oddBack == odd);
    const std::string escaped = "{\"format\":\"synthyboy-state\",\"version\":3,\"content\":{\"impulses\":[{\"pack\":\"p\",\"file\":\"f\",\"name\":\"\\u20ac\\u00e9\\ud800x\"}]}}";
    CHECK (loadStateWithContent (escaped.data(), escaped.size(), tmp, oddBack).content == ContentStatus::ok);
    CHECK (oddBack.impulses.size() == 1 && oddBack.impulses[0].name == "\xe2\x82\xac\xc3\xa9\xef\xbf\xbd" "x");
}

TEST (content_section_belongs_to_version_3_and_later)
{
    const auto base = stateText (fullContent());
    StateContent got;
    auto v = ParamValues::defaults();

    std::string asV2 = base;
    asV2.replace (asV2.find ("\"version\":3"), 11, "\"version\":2");
    const auto r2 = loadStateWithContent (asV2.data(), asV2.size(), v, got);
    CHECK (r2.status == LoadStatus::ok && r2.content == ContentStatus::absent && got.empty());

    std::string asV4 = base;
    asV4.replace (asV4.find ("\"version\":3"), 11, "\"version\":4");
    const auto r4 = loadStateWithContent (asV4.data(), asV4.size(), v, got);
    CHECK (r4.status == LoadStatus::newer_version && r4.content == ContentStatus::ok && got == fullContent());

    // the key order of the document is free: version after content
    const std::string reordered = "{\"content\":{\"impulses\":[{\"pack\":\"p\",\"file\":\"f\"}]},\"params\":{\"master.volume\":-5},\"version\":3,\"format\":\"synthyboy-state\"}";
    const auto rr = loadStateWithContent (reordered.data(), reordered.size(), v, got);
    CHECK (rr.status == LoadStatus::ok && rr.content == ContentStatus::ok && got.impulses.size() == 1 && rr.applied == 1);
}

namespace
{
/** A state document with exactly this content text. */
LoadResult loadWithContentText (const std::string& content, StateContent& out, ParamValues& v)
{
    const std::string doc = "{\"format\":\"synthyboy-state\",\"version\":3,\"params\":{\"master.volume\":-7},\"content\":" + content + "}";
    return loadStateWithContent (doc.data(), doc.size(), v, out);
}

std::string pointsText (int n, double x0 = 0.0, double step = 0.0, double y = 0.5)
{
    std::string s = "[";
    for (int i = 0; i < n; ++i)
        s += std::string (i > 0 ? "," : "") + "{\"x\":" + std::to_string (x0 + step * i) + ",\"y\":" + std::to_string (y) + ",\"power\":0}";
    return s + "]";
}
} // namespace

TEST (content_that_breaks_a_cap_is_rejected_but_the_parameters_apply)
{
    const std::string h64 (64, 'f');
    const std::vector<std::string> bad = {
        "{\"wavetables\":[{\"osc\":4,\"pack\":\"p\",\"file\":\"f\"}]}",                                        // osc out of range
        "{\"wavetables\":[{\"osc\":1},{\"osc\":1}]}",                                                           // osc twice
        "{\"wavetables\":[{\"osc\":1},{\"osc\":2},{\"osc\":3},{\"osc\":1}]}",                                  // too many
        "{\"wavetables\":[{\"osc\":\"1\"}]}",                                                                   // wrong type
        "{\"wavetables\":{\"osc\":1}}",                                                                         // wrong container
        "{\"wavetables\":[{\"osc\":1,\"file\":\"../../etc/passwd\"}]}",                                         // path traversal
        "{\"wavetables\":[{\"osc\":1,\"file\":\"/abs/path\"}]}",
        "{\"wavetables\":[{\"osc\":1,\"file\":\"c:\\\\x\"}]}",
        "{\"wavetables\":[{\"osc\":1,\"pack\":\"a\\\\b\"}]}",
        "{\"wavetables\":[{\"osc\":1,\"sha256\":\"abc\"}]}",                                                    // hash length
        "{\"wavetables\":[{\"osc\":1,\"sha256\":\"" + std::string (63, 'f') + "G\"}]}",                           // hash digit
        "{\"wavetables\":[{\"osc\":1,\"sha256\":\"" + std::string (64, 'F') + "\"}]}",                          // upper case
        "{\"wavetables\":[{\"osc\":1,\"name\":\"" + std::string (256, 'n') + "\"}]}",                           // string too long
        "{\"wavetables\":[{\"osc\":1,\"frames\":5000}]}",
        "{\"wavetables\":[{\"osc\":1.5}]}",
        "{\"samples\":[{\"osc\":1,\"channels\":3}]}",
        "{\"samples\":[{\"osc\":1,\"sample_rate\":-1}]}",
        "{\"samples\":[{\"osc\":1,\"slices\":[0.5,0.25]}]}",                                                   // not ascending
        "{\"samples\":[{\"osc\":1,\"slices\":[1.5]}]}",
        "{\"lfo_shapes\":[{\"lfo\":11,\"points\":" + pointsText (3, 0.0, 0.5) + "}]}",
        "{\"lfo_shapes\":[{\"lfo\":1,\"points\":" + pointsText (1) + "}]}",                                     // one point
        "{\"lfo_shapes\":[{\"lfo\":1,\"points\":" + pointsText (129, 0.0, 0.0) + "}]}",                         // 129 points
        "{\"lfo_shapes\":[{\"lfo\":1,\"points\":" + pointsText (3, 0.9, -0.4) + "}]}",                          // x descending
        "{\"lfo_shapes\":[{\"lfo\":1,\"points\":" + pointsText (3, 0.0, 0.5, 2.0) + "}]}",                      // y > 1
        "{\"lfo_shapes\":[{\"lfo\":1,\"loop_point\":9,\"points\":" + pointsText (3, 0.0, 0.5) + "}]}",
        "{\"lfo_shapes\":[{\"lfo\":1,\"points\":[{\"x\":0,\"y\":0,\"mod_x\":{\"source\":\"nope\",\"amount\":0.5}},{\"x\":1,\"y\":1}]}]}",
        "{\"lfo_shapes\":[{\"lfo\":1,\"points\":[{\"x\":0,\"y\":0,\"mod_y\":{\"source\":\"lfo1\",\"amount\":7}},{\"x\":1,\"y\":1}]}]}",
        "{\"lfo_shapes\":[{\"lfo\":1,\"points\":[{\"x\":1e999,\"y\":0},{\"x\":1,\"y\":1}]}]}",                  // not finite
        "{\"curves\":{\"velocity\":{\"points\":" + pointsText (65, 0.0, 0.0) + "}}}",                            // 65 points
        "{\"curves\":{\"note\":{\"points\":" + pointsText (1) + "}}}",
        "{\"tuning\":{\"scl\":\"" + std::string (kMaxTuningSclBytes + 1, 'x') + "\"}}",                         // scl too big
        "{\"tuning\":{\"scl\":\"x\",\"kbm\":\"" + std::string (kMaxTuningKbmBytes + 1, 'x') + "\"}}",           // kbm too big
        "{\"tuning\":{\"scl\":\"a\\u0001b\"}}",                                                                // control character
        "{\"tuning\":{\"scl\":5}}",
        "{\"impulses\":[{\"file\":\"a\"},{\"file\":\"b\"}]}",                                                   // 2 impulses
        "{\"filter_curves\":[{\"filter\":\"filter3\",\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0}]}]}",
        "{\"filter_curves\":[{\"filter\":\"filter1\",\"points\":[{\"x\":0,\"y\":99},{\"x\":1,\"y\":0}]}]}",
        "{\"filter_curves\":[{\"filter\":\"filter1\",\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0}]},{\"filter\":\"filter1\",\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0}]}]}",
        "{\"mod_curves\":[{\"slot\":0,\"points\":" + pointsText (3, 0.0, 0.5) + "}]}",                           // slot out of range
        "{\"mod_curves\":[{\"slot\":65,\"points\":" + pointsText (3, 0.0, 0.5) + "}]}",
        "{\"mod_curves\":[{\"slot\":3,\"points\":" + pointsText (3, 0.0, 0.5) + "},{\"slot\":3,\"points\":" + pointsText (3, 0.0, 0.5) + "}]}", // slot twice
        "{\"mod_curves\":[{\"slot\":3,\"points\":" + pointsText (1) + "}]}",                                      // one point
        "{\"mod_curves\":[{\"slot\":3,\"points\":" + pointsText (101, 0.0, 0.0) + "}]}",                          // 101 points
        "{\"mod_curves\":[{\"slot\":3,\"points\":" + pointsText (3, 0.9, -0.4) + "}]}",                           // x descending
        "{\"mod_curves\":[{\"slot\":3,\"points\":" + pointsText (3, 0.0, 0.5, 2.0) + "}]}",                       // y > 1
        "{\"mod_curves\":{\"slot\":3}}",                                                                      // wrong container
        "{\"mod_curves\":[{\"slot\":3,\"points\":[]}]}",                                                        // neither points nor a slew (GROUPB WP-08)
        "{\"mod_curves\":[{\"slot\":3,\"points\":[],\"rise\":-1}]}",                                            // a negative time
        "{\"mod_curves\":[{\"slot\":3,\"points\":[],\"fall\":5000.5}]}",                                        // beyond kMaxModSlewMs
        "{\"mod_curves\":[{\"slot\":3,\"points\":" + pointsText (3, 0.0, 0.5) + ",\"rise\":1e999}]}",             // not finite
        "[1,2,3]",                                                                                              // the section is not an object
        "\"text\"",
        "7",
    };

    int index = 0;
    for (const auto& content : bad)
    {
        StateContent out = fullContent();
        auto v = ParamValues::defaults();
        const auto r = loadWithContentText (content, out, v);
        const bool ok = CHECK (r.status == LoadStatus::ok) && CHECK (r.content == ContentStatus::rejected) && CHECK (out.empty())
                     && CHECK_EQ (v.plain[indexOfKey ("master.volume")], -7.0f);
        if (! ok)
            std::printf ("    case %d: %.80s\n", index, content.c_str());
        ++index;
    }

    // the same shapes without the fault are accepted (the cases above fail for the one reason each)
    const std::vector<std::string> good = {
        "{}", "{\"wavetables\":[]}", "{\"unknown\":{\"deep\":[1,2,{\"x\":null}]},\"impulses\":[{\"pack\":\"p\",\"file\":\"dir/f.wav\",\"extra\":true}]}",
        "{\"lfo_shapes\":[{\"lfo\":10,\"points\":" + pointsText (128, 0.0, 0.0) + "}]}",
        "{\"curves\":{\"velocity\":{\"points\":" + pointsText (64, 0.0, 0.0) + "}}}",
        "{\"mod_curves\":[{\"slot\":64,\"points\":" + pointsText (100, 0.0, 0.0) + "}]}",
        "{\"tuning\":{\"scl\":\"" + std::string (kMaxTuningSclBytes, 'x') + "\",\"kbm\":null}}",
        "{\"tuning\":{\"scl\":\"x\",\"kbm\":\"" + std::string (kMaxTuningKbmBytes, 'x') + "\"}}",
        "{\"wavetables\":[{\"osc\":1,\"sha256\":\"" + std::string (64, '0') + "\",\"frames\":4096}]}",
    };
    for (const auto& content : good)
    {
        StateContent out;
        auto v = ParamValues::defaults();
        const auto r = loadWithContentText (content, out, v);
        if (! (CHECK (r.status == LoadStatus::ok) && CHECK (r.content == ContentStatus::ok)))
            std::printf("    good case: %.80s\n", content.c_str());
        CHECK (validateContent (out));
    }

    // malformed JSON inside the section invalidates the document (nothing applied)
    for (const char* content : { "{\"impulses\":[{\"file\":\"a\"]}", "{\"wavetables\":[{\"osc\":01}]}", "{\"tuning\":{\"scl\":\"abc}}", "{\"a\":1,}", "{\"samples\":[{\"slices\":[1,2,]}]}" })
    {
        StateContent out = fullContent();
        auto v = ParamValues::defaults();
        const auto before = v;
        CHECK (loadWithContentText (content, out, v).status == LoadStatus::invalid);
        CHECK (sameValues (v, before) && out == fullContent());
    }

    // nesting: the deepest legal point modulation (7 levels) is fine, 100 levels in an unknown member are not
    const std::string deep = std::string ("{\"x\":") + std::string (100, '[') + std::string (100, ']') + "}";
    StateContent out;
    auto v = ParamValues::defaults();
    CHECK (loadWithContentText (deep, out, v).status == LoadStatus::invalid);
}

TEST (the_largest_legal_state_stays_below_the_limit)
{
    StateContent c;
    for (int lfo = 1; lfo <= 10; ++lfo)
    {
        ContentLfoShape l;
        l.lfo = lfo;
        l.name = std::string (255, 'n');
        for (int i = 0; i < kMaxLfoShapePoints; ++i)
        {
            ContentPoint p = point (static_cast<float> (i) / 127.0f, 0.123456789f, -19.987654f);
            p.modX = { true, "poly_aftertouch", -0.987654321f };
            p.modY = { true, "poly_aftertouch", 0.987654321f };
            l.points.push_back (p);
        }
        c.lfoShapes.push_back (l);
    }
    for (int osc = 1; osc <= 3; ++osc)
    {
        ContentSample s;
        s.osc = osc;
        s.ref = ref (std::string (255, 'p').c_str(), std::string (255, 'f').c_str(), std::string (255, 'n').c_str());
        for (int i = 0; i < kMaxSampleSlices; ++i)
            s.slices.push_back (static_cast<float> (i) / 128.0f + 0.0012345f);
        c.samples.push_back (s);
    }
    for (int slot = 1; slot <= kMaxContentModCurves; ++slot) // B20: every slot with a curve of the most points
    {
        ContentModCurve mc;
        mc.slot = slot;
        for (int i = 0; i < kMaxModCurvePoints; ++i)
            mc.points.push_back (point (static_cast<float> (i) / 99.0f, 0.123456789f, -19.987654f));
        c.modCurves.push_back (mc);
    }
    for (int osc = 1; osc <= 3; ++osc) // B16: every remap slot with the most points
        for (int slot = 1; slot <= kNumRemapSlots; ++slot)
        {
            ContentRemap r;
            r.osc = osc;
            r.slot = slot;
            r.name = std::string (255, 'n');
            for (int i = 0; i < kMaxRemapPoints; ++i)
                r.points.push_back (point (static_cast<float> (i) / 63.0f, 0.123456789f, -19.987654f));
            c.remaps.push_back (r);
        }
    c.tuning.present = true;
    c.tuning.scl = std::string (kMaxTuningSclBytes, '\n');
    c.tuning.hasKbm = true;
    c.tuning.kbm = std::string (kMaxTuningKbmBytes, '\t');
    CHECK (validateContent (c));
    sbtest::Rng rng (3);
    const auto text = stateText (c, randomValues (rng));
    std::printf ("    largest legal state: %zu bytes (limit %zu)\n", text.size(), kMaxStateBytes);
    CHECK (text.size() < kMaxStateBytes * 4 / 5); // with 64 mod curves of 100 points and 12 remaps of 64 points (B20 / B16) the worst case is ~76 %
    auto v = ParamValues::defaults();
    StateContent back;
    CHECK (loadStateWithContent (text.data(), text.size(), v, back).content == ContentStatus::ok && back == c);

    // the same inside a preset file
    const auto preset = savePreset (PresetMeta::initDefault(), v, c);
    CHECK (preset.size() < (1u << 20)); // the preset file limit
}

TEST (content_fuzz_500_mutated_documents_never_crash)
{
    const std::string base = stateText (fullContent(), busyValues());
    sbtest::Rng rng (0xf022);
    int invalid = 0, rejected = 0, accepted = 0;
    for (int n = 0; n < 500; ++n)
    {
        std::string doc = base;
        const int edits = 1 + static_cast<int>(rng.u32() % 6);
        for (int e = 0; e < edits && ! doc.empty(); ++e)
        {
            // the section is the interesting part: aim most edits at it
            const size_t contentAt = doc.find ("\"content\"");
            const size_t lo = (rng.u32() % 4 != 0 && contentAt != std::string::npos) ? contentAt : 0;
            const size_t pos = lo + rng.u32() % (doc.size() - lo);
            switch (rng.u32() % 8)
            {
                case 0: doc[pos] = static_cast<char> (rng.u32() & 0xff); break;
                case 1: doc.erase (pos, 1 + rng.u32() % 16); break;
                case 2: doc.insert (pos, 1, "{}[],:\"\\0-e.tfn"[rng.u32() % 15]); break;
                case 3: doc.resize (pos); break;
                case 4: doc.insert (pos, doc.substr (lo, 1 + rng.u32() % 64)); break;
                case 5: doc[pos] = static_cast<char> ('0' + rng.u32() % 10); break;
                case 6: doc.insert (pos, "\\u00"); break;
                default: doc.insert (pos, std::string (1 + rng.u32() % 12, "[{\""[rng.u32() % 3])); break;
            }
        }

        auto v = ParamValues::defaults();
        v.plain[indexOfKey ("master.volume")] = -33.0f;
        const auto before = v;
        StateContent sentinel = fullContent();
        StateContent content = sentinel;
        const auto r = loadStateWithContent (doc.data(), doc.size(), v, content);

        // the plain loader must reach the same verdict about the same bytes
        auto v2 = before;
        const auto r2 = loadState (doc.data(), doc.size(), v2);
        const bool same = CHECK (r.status == r2.status) && CHECK_EQ (r.applied, r2.applied) && CHECK (sameValues (v, v2));
        if (! same)
        {
            std::printf ("    mutant %d disagrees\n", n);
            continue;
        }

        if (r.status == LoadStatus::invalid || r.status == LoadStatus::empty)
        {
            CHECK (sameValues (v, before) && content == sentinel);
            ++invalid;
        }
        else
        {
            CHECK (validateContent (content));
            for (size_t i = 0; i < paramCount(); ++i)
                CHECK (v.plain[i] >= paramTable()[i].min && v.plain[i] <= paramTable()[i].max);
            if (r.content == ContentStatus::rejected || r.content == ContentStatus::absent)
                CHECK (content.empty());
            rejected += r.content == ContentStatus::rejected ? 1 : 0;
            ++accepted;
        }
    }
    std::printf ("    500 mutants: %d invalid, %d accepted (%d with rejected content)\n", invalid, accepted, rejected);
    CHECK (invalid > 0 && accepted > 0);

    // truncation at every length of a small document, and a long run of random bytes
    const std::string small = stateText (fullContent());
    for (size_t len = 0; len < small.size(); len += 7)
    {
        auto v = ParamValues::defaults();
        StateContent c;
        const auto r = loadStateWithContent (small.data(), len, v, c);
        CHECK (r.status == LoadStatus::invalid || r.status == LoadStatus::empty);
    }
    for (int n = 0; n < 50; ++n)
    {
        std::string junk (1 + rng.u32() % 4000, ' ');
        for (auto& ch : junk)
            ch = static_cast<char> (rng.u32());
        auto v = ParamValues::defaults();
        StateContent c;
        CHECK (loadStateWithContent (junk.data(), junk.size(), v, c).status == LoadStatus::invalid);
    }
}

TEST (preset_with_content_round_trips_and_factory_presets_of_version_2_still_load)
{
    const auto content = fullContent();
    sbtest::Rng rng (11);
    const auto v = randomValues (rng);
    PresetMeta meta = PresetMeta::initDefault();
    meta.name = "With content";
    const auto text = savePreset (meta, v, content);
    CHECK (text.compare (0, 40, "{\"format\":\"synthyboy-preset\",\"version\":1") == 0);

    PresetMeta metaBack;
    auto back = ParamValues::defaults();
    StateContent contentBack;
    const auto r = loadPreset (text.data(), text.size(), metaBack, back, contentBack);
    CHECK (r.status == LoadStatus::ok && r.content == ContentStatus::ok && contentBack == content && metaBack.name == "With content");
    CHECK (savePreset (metaBack, back, contentBack) == text);

    // without content: the plain overloads are unchanged
    const auto plain = savePreset (meta, v);
    CHECK (plain.find ("\"content\"") == std::string::npos);
    CHECK (loadPreset (plain.data(), plain.size(), metaBack, back).status == LoadStatus::ok);
    CHECK (readPresetMeta (text.data(), text.size(), metaBack) && metaBack.name == "With content");

    // the factory presets (.sbpreset version 1 holding a state of version 3, every key explicit) load completely; the same document cut
    // back to the 880 keys of version 2 (what the first factory set was) still loads as before
    const std::filesystem::path root = std::filesystem::path (__FILE__).parent_path().parent_path().parent_path();
    int count = 0;
    std::error_code ec;
    if (std::filesystem::exists (root / "content" / "presets", ec))
        for (const auto& entry : std::filesystem::recursive_directory_iterator (root / "content" / "presets", ec))
        {
            if (entry.path().extension() != ".sbpreset")
                continue;
            std::ifstream in (entry.path(), std::ios::binary);
            const std::string doc ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
            auto garbage = randomValues (rng);
            StateContent c = fullContent();
            PresetMeta m;
            const auto fr = loadPreset (doc.data(), doc.size(), m, garbage, c);
            const bool hasContent = doc.find ("\"content\":") != std::string::npos;
            const bool ok = CHECK (fr.status == LoadStatus::ok) && CHECK_EQ (fr.version, 3) && CHECK_EQ (fr.applied, static_cast<int> (paramCount()))
                         && CHECK (hasContent ? fr.content == ContentStatus::ok : (fr.content == ContentStatus::absent && c.empty())) && CHECK (! m.name.empty());
            if (! ok)
                std::printf ("    %s\n", entry.path().string().c_str());

            // the version 2 form of the same document: the first 880 keys, no content section, state version 2
            std::string v2 = doc;
            const size_t paramsAt = v2.find ("\"params\":{");
            size_t cut = paramsAt;
            for (size_t commas = 0; cut != std::string::npos && cut < v2.size() && commas < kNumKeysV2; ++cut)
                commas += v2[cut] == ',' ? 1 : 0;
            if (CHECK (paramsAt != std::string::npos) && CHECK (cut < v2.size()))
            {
                v2.erase (cut - 1);
                v2 += "}}}";
                const size_t versionAt = v2.find ("\"version\":3");
                if (CHECK (versionAt != std::string::npos))
                    v2[versionAt + 10] = '2';
                auto old = randomValues (rng);
                StateContent oc = fullContent();
                PresetMeta om;
                const auto fo = loadPreset (v2.data(), v2.size(), om, old, oc);
                CHECK (fo.status == LoadStatus::ok);
                CHECK_EQ (fo.version, 2);
                CHECK_EQ (fo.applied, static_cast<int> (kNumKeysV2));
                CHECK (fo.content == ContentStatus::absent);
                CHECK (oc.empty());
                for (int slot = 10; slot <= 16; ++slot)
                    CHECK_EQ (old.plain[indexOfKey (("fx.order." + std::to_string (slot)).c_str())], static_cast<float> (slot - 1));
            }
            ++count;
        }
    CHECK (count >= 100);
}

namespace
{
struct Rendering
{
    sbtest::Rendered audio;
    bool exists = false;
};

/** The golden events on an engine whose parameters come from loaded state values (all 1594 of them are handed to the engine). */
Rendering renderLoaded (EngineKind kind, const ParamValues& values, bool polyphonyLast)
{
    Rendering out;
    auto engine = createEngine (kind);
    if (engine == nullptr)
        return out;
    out.exists = true;
    engine->setParam (kParamMasterPolyphony, 32.0f);
    for (size_t i = 0; i < paramCount(); ++i)
        engine->setParam (paramTable()[i].id, values.plain[i]);
    if (polyphonyLast)
        engine->setParam (kParamMasterPolyphony, 32.0f);
    engine->prepare (48000.0, 512, Quality {});
    out.audio = sbtest::render (*engine, sbgolden::audioEvents(), 48000, 480);
    return out;
}
} // namespace

TEST (version_2_document_loaded_into_version_3_renders_the_version_2_audio_in_both_engines)
{
    // golden hashes of golden_audio.h (commit 9c97a69): the default patch and the busy patch
    const struct { EngineKind kind; bool busy; uint64_t hash; } golden[] = {
        { EngineKind::own, false, 0xc3e433c23a7f89a5ull }, { EngineKind::own, true, 0x44f50232b180c48dull },
        { EngineKind::vital, false, 0x765f4bc243b189c1ull }, { EngineKind::vital, true, 0x199ba135a1fd977full },
    };
    sbtest::Rng rng (9);
    for (const auto& g : golden)
    {
        if (! isEngineAvailable (g.kind))
            continue;
        const auto doc = asVersion2 (saveState (g.busy ? busyValues() : ParamValues::defaults()));
        // a previous version 3 patch: every new key holds some other value. An engine that acts on the new keys (engine-vital since WP2/WP3)
        // would sound different with them, so it is loaded over the defaults (R5: the defaults reproduce the version 2 sound)
        auto values = g.kind == EngineKind::own ? randomValues (rng) : ParamValues::defaults();
        CHECK (loadState (doc.data(), doc.size(), values).status == LoadStatus::ok);

        const auto r = renderLoaded (g.kind, values, ! g.busy);
        sbtest::state().context = engineName (g.kind);
        CHECK (r.exists);
        const auto fresh = sbgolden::audioRender (g.kind, g.busy);
        CHECK (fresh.left == r.audio.left && fresh.right == r.audio.right); // identical to the engine fed with the old values only
#if defined(__APPLE__) && defined(__aarch64__) && defined(NDEBUG)
        CHECK_EQ (sbgolden::audioFnv (r.audio), g.hash);
#else
        (void) g.hash;
#endif
        sbtest::state().context = "";
    }
}

// REMAINDER item 1: the RISE / FALL of a slot were stored in content.mod_curves[] (GROUPB WP-08); they are the keys modNN.rise / fall now. An old document keeps its sound.
TEST (legacy_rise_and_fall_records_become_the_keys_of_the_slot)
{
    const std::string section = "{\"mod_curves\":[{\"slot\":64,\"points\":" + pointsText (2, 0.0, 1.0) + ",\"rise\":120.5,\"fall\":40},{\"slot\":12,\"points\":[],\"fall\":900},"
                                "{\"slot\":5,\"points\":[],\"rise\":250},{\"slot\":9,\"points\":" + pointsText (2, 0.0, 1.0) + "}]}";
    StateContent out;
    auto v = ParamValues::defaults();
    const auto r = loadWithContentText (section, out, v);
    CHECK (r.status == LoadStatus::ok && r.content == ContentStatus::ok);
    const auto at = [&] (const char* key) { return v.plain[indexOfKey (key)]; };
    CHECK_EQ (at ("mod64.rise"), 120.5f);
    CHECK_EQ (at ("mod64.fall"), 40.0f);
    CHECK_EQ (at ("mod12.fall"), 900.0f);
    CHECK_EQ (at ("mod12.rise"), 0.0f);
    CHECK_EQ (at ("mod05.rise"), 250.0f);
    CHECK_EQ (at ("mod09.rise"), 0.0f);
    // the records that held only a slew are gone, those with a curve keep it and carry no time any more
    CHECK_EQ (out.modCurves.size(), size_t { 2 });
    for (const auto& mc : out.modCurves)
        CHECK (mc.points.size() == 2 && mc.riseMs == 0.0f && mc.fallMs == 0.0f && (mc.slot == 64 || mc.slot == 9));
    CHECK (validateContent (out));
    // a save writes the keys, never the old fields; saving what was loaded and loading it again changes nothing
    const std::string saved = saveState (v, out);
    CHECK (saved.find ("\"rise\":") == std::string::npos && saved.find ("\"fall\":") == std::string::npos);   // no old field (the keys are "mod64.rise" ...)
    CHECK (saved.find ("\"mod64.rise\":120.5") != std::string::npos);
    auto again = ParamValues::defaults();
    StateContent againContent;
    const auto r2 = loadStateWithContent (saved.data(), saved.size(), again, againContent);
    CHECK (r2.status == LoadStatus::ok && againContent == out && sameValues (again, v));
    // the key the document states itself wins over an old record
    const std::string both = "{\"format\":\"synthyboy-state\",\"version\":3,\"params\":{\"mod05.rise\":75},\"content\":{\"mod_curves\":[{\"slot\":5,\"points\":[],\"rise\":250}]}}";
    auto v3 = ParamValues::defaults();
    StateContent c3;
    CHECK (loadStateWithContent (both.data(), both.size(), v3, c3).content == ContentStatus::ok);
    CHECK_EQ (v3.plain[indexOfKey ("mod05.rise")], 75.0f);
    CHECK (c3.modCurves.empty());
    // without the content (loadState) the old record is skipped like the rest of the section
    auto v4 = ParamValues::defaults();
    CHECK (loadState (both.data(), both.size(), v4).status == LoadStatus::ok);
    CHECK_EQ (v4.plain[indexOfKey ("mod05.rise")], 75.0f);
}
