// SynthyBoy - the output stage of the shells (core/include/synthyboy/output_stage.h) on every factory preset: the raw own engine render of a velocity 127 four note chord
// (keys 36 48 55 60) and of a single note (key 48), held 2 s, under the device profiles of the MPC Key 61 ("low") and of the MPC XL ("high,voices=16,control=economy"), 48 kHz, the
// content packs as the store; then the stage at make-up gains kMakeupGainDb, 2, 4, 6, 9 and 15 dB, 128 frame blocks.
// Checks: no output sample above the ceiling, every sample finite, the safety clip never acts (the limiter holds the ceiling by itself), and the loudness order: the loudest 400 ms
// (momentary loudness, BS.1770 K-weighting) never falls as the make-up gain rises (no inversion by pumping; kLoudnessTolerance below).
// Prints, per profile and category, the gain reduction (deepest, mean, the share of 128 frame blocks above 3 and 6 dB) at every gain: the figures the choice of kMakeupGainDb starts from.
// Slow (about 640 renders): label `slow`.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/output_stage.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "device_profile.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 128;
constexpr double kHold = 2.0, kTail = 0.5;
const float kGains[] = { kMakeupGainDb, 2.0f, 4.0f, 6.0f, 9.0f, 15.0f };   // rising: the loudness order is checked along it
static_assert (kMakeupGainDb < 2.0f, "kGains must rise");
constexpr int kGainCount = 6;
// A deeply limited sound reaches a plateau of loudness: more gain then moves the loudest 400 ms by a few hundredths of a LU either way (the release after the attack, the window).
// 0.15 LU is far under what is heard (about 0.5 to 1 LU); the measured worst on 2026-11-19 was 0.12 LU (Bowed MPE Strings, single note, +12 -> +15 dB, at most 9.6 dB of reduction).
constexpr double kLoudnessTolerance = 0.15;

struct Preset
{
    std::string name, category;
    ParamValues values;
    StateContent content;
};

std::vector<Preset> loadPresets()
{
    std::vector<Preset> out;
    for (const PresetEntry& e : scanPresetDirectory (std::string (SYNTHYBOY_TEST_SOURCE_DIR) + "/content/presets"))
    {
        std::ifstream in (e.path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        Preset p;
        PresetMeta meta;
        p.values = ParamValues::defaults();
        const LoadResult r = loadPreset (text.data(), text.size(), meta, p.values, p.content);
        if (r.status != LoadStatus::ok && r.status != LoadStatus::newer_version)
            continue;
        p.name = meta.name;
        p.category = meta.category;
        out.push_back (std::move (p));
    }
    return out;
}

/** The raw engine output: keys at velocity 1.0 held kHold, then kTail of release. */
void renderRaw (const Preset& p, const Quality& q, const IContentStore* store, const std::vector<int>& keys, std::vector<float>& L, std::vector<float>& R, double hold = kHold, double tail = kTail)
{
    auto engine = createEngine (EngineKind::own);
    engine->prepare (kSr, kBlock, q);
    engine->setContentStore (store);
    for (size_t i = 0; i < paramCount(); ++i)
        engine->setParam (paramTable()[i].id, p.values.plain[i]);
    engine->setContent (p.content);
    {
        // 0.3 s of silence: the engine smooths master.volume from its default after a state is loaded (on the device the plugin has run before a note comes)
        std::vector<float> zl (kBlock), zr (kBlock);
        for (int b = 0; b < static_cast<int> (0.3 * kSr / kBlock); ++b)
            engine->process (nullptr, 0, zl.data(), zr.data(), kBlock);
    }
    const int blocks = static_cast<int> ((hold + tail) * kSr / kBlock), holdBlocks = static_cast<int> (hold * kSr / kBlock);
    L.assign (static_cast<size_t> (blocks * kBlock), 0.0f);
    R.assign (L.size(), 0.0f);
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<Event> ev;
        if (b == 0 || b == holdBlocks)
            for (int k : keys)
            {
                Event e;
                e.frame = 0;
                e.type = b == 0 ? Event::NoteOn : Event::NoteOff;
                e.channel = 0;
                e.key = static_cast<int16_t> (k);
                e.value = b == 0 ? 1.0f : 0.0f;
                e.noteId = b == 0 ? k : -1;
                ev.push_back (e);
            }
        engine->process (ev.empty() ? nullptr : ev.data(), static_cast<int> (ev.size()), L.data() + b * kBlock, R.data() + b * kBlock, kBlock);
    }
}

/** The loudest 400 ms (momentary loudness, BS.1770 K-weighting at 48 kHz, both channels), hop of one block. */
double momentaryMax (const std::vector<float>& L, const std::vector<float>& R)
{
    auto kw = [] (const std::vector<float>& x) {
        std::vector<double> y (x.size());
        double s1[2] = { 0, 0 }, s2[2] = { 0, 0 };
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double in = x[i];
            const double a = 1.53512485958697 * in + s1[0];
            s1[0] = -2.69169618940638 * in + 1.69065929318241 * a + s1[1];
            s1[1] = 1.19839281085285 * in - 0.73248077421585 * a;
            const double b = a + s2[0];
            s2[0] = -2.0 * a + 1.99004745483398 * b + s2[1];
            s2[1] = a - 0.99007225036621 * b;
            y[i] = b;
        }
        return y;
    };
    const auto l = kw (L), r = kw (R);
    const size_t win = static_cast<size_t> (0.4 * kSr);
    std::vector<double> prefix (l.size() + 1, 0.0);
    for (size_t i = 0; i < l.size(); ++i)
        prefix[i + 1] = prefix[i] + l[i] * l[i] + r[i] * r[i];
    double best = 0.0;
    for (size_t start = 0; start + win <= l.size(); start += kBlock)
        best = std::max (best, (prefix[start + win] - prefix[start]) / static_cast<double> (win));
    return -0.691 + 10.0 * std::log10 (std::max (best, 1e-20));
}

struct Stats
{
    double maxGr = 0, sumGr = 0;
    long blocks = 0, over3 = 0, over6 = 0;
    int renders = 0;
};
} // namespace

TEST (output_stage_on_every_factory_preset)
{
    const auto presets = loadPresets();
    CHECK (presets.size() >= 160);
    // the factory packs by their pack ids (DirectoryContentStore: <root>/<pack id>/<file>), made by CMake as for the preset gate
    DirectoryContentStore store (SYNTHYBOY_TEST_CONTENT_STORE);
    struct Prof { const char* name; const char* text; };
    const Prof profiles[] = { { "Key 61 (low)", "low" }, { "XL", "high,voices=16,control=economy" } };
    const std::vector<int> chord { 36, 48, 55, 60 }, single { 48 };
    const float C = OutputStage::kCeiling;
    long renders = 0, aboveCeiling = 0, notFinite = 0, clips = 0, inversions = 0;
    std::map<std::string, Stats> stats[2][kGainCount];   // [chord?][gain][profile + category]
    std::vector<float> L, R, l, r;
    for (const Prof& pr : profiles)
    {
        synthyboy_mpc::Profile profile;
        std::string why;
        CHECK (synthyboy_mpc::parseProfile (pr.text, profile, &why));
        Quality q = synthyboy_mpc::profileQuality (profile);
        q.realtime = false;
        for (const Preset& p : presets)
            for (int isChord = 0; isChord < 2; ++isChord)
            {
                renderRaw (p, q, &store, isChord ? chord : single, L, R);
                ++renders;
                double rawPeak = 0;
                for (size_t i = 0; i < L.size(); ++i)
                    rawPeak = std::max ({ rawPeak, static_cast<double> (std::fabs (L[i])), static_cast<double> (std::fabs (R[i])) });
                double lastLoud = -1e9;
                for (int gi = 0; gi < kGainCount; ++gi)
                {
                    OutputStage st;
                    st.setLimiter (true);   // the user's opt-in: the limiter must hold the ceiling at any gain
                    st.prepare (kSr);
                    st.setGainDb (kGains[gi]);
                    st.reset();
                    l = L;
                    r = R;
                    Stats& s = stats[isChord][gi][std::string (pr.name) + " " + p.category];
                    Stats& all = stats[isChord][gi][std::string (pr.name) + " ALL"];
                    const int firstBlocks = static_cast<int> (kHold * kSr / kBlock);
                    double deepest = 0;
                    for (size_t o = 0; o < l.size(); o += kBlock)
                    {
                        st.process (l.data() + o, r.data() + o, kBlock);
                        const double gr = st.gainReductionDb();
                        deepest = std::max (deepest, gr);
                        if (static_cast<int> (o / kBlock) < firstBlocks)
                            for (Stats* t : { &s, &all })
                            {
                                t->sumGr += gr;
                                ++t->blocks;
                                t->over3 += gr > 3.0;
                                t->over6 += gr > 6.0;
                            }
                    }
                    for (Stats* t : { &s, &all })
                    {
                        t->maxGr = std::max (t->maxGr, deepest);
                        ++t->renders;
                    }
                    for (size_t i = 0; i < l.size(); ++i)
                    {
                        notFinite += ! std::isfinite (l[i]) || ! std::isfinite (r[i]);
                        aboveCeiling += std::fabs (l[i]) > C || std::fabs (r[i]) > C;
                    }
                    clips += st.safetyClips();
                    const double loud = momentaryMax (l, r);
                    if (loud < lastLoud - kLoudnessTolerance)
                    {
                        ++inversions;
                        std::printf ("  loudness inversion: %s %s %s: %.2f LUFS at %.0f dB < %.2f at the gain before\n", pr.name, p.name.c_str(), isChord ? "chord" : "single", loud, kGains[gi], lastLoud);
                    }
                    lastLoud = std::max (lastLoud, loud);
                    if ((p.name == "Tine Glass Keys" || p.name == "808 Boom") && isChord && (kGains[gi] == kMakeupGainDb))
                    {
                        double outPeak = 0;
                        for (size_t i = 0; i < l.size(); ++i)
                            outPeak = std::max ({ outPeak, static_cast<double> (std::fabs (l[i])), static_cast<double> (std::fabs (r[i])) });
                        std::printf ("  %s, %s, chord velocity 127, %+.1f dB: raw peak %.2f dBFS, after the gain %.2f dBFS, after the stage %.2f dBFS, deepest reduction %.2f dB, loudest 400 ms %.2f LUFS\n",
                                     p.name.c_str(), pr.name, kGains[gi], 20.0 * std::log10 (rawPeak), 20.0 * std::log10 (rawPeak) + kGains[gi], 20.0 * std::log10 (outPeak), deepest, loud);
                    }
                }
            }
    }
    std::printf ("  %ld renders x %d gains: %ld samples above the ceiling, %ld not finite, %ld safety clips, %ld loudness inversions\n", renders, kGainCount, aboveCeiling, notFinite, clips, inversions);
    for (int isChord = 1; isChord >= 0; --isChord)
    {
        std::printf ("  gain reduction, %s (the first %.0f s; blocks of %d frames): category: per gain  max / mean dB, %% blocks > 3 dB, %% > 6 dB\n", isChord ? "chord 36 48 55 60 velocity 127" : "single key 48 velocity 127", kHold, kBlock);
        for (const auto& kv : stats[isChord][0])
        {
            std::printf ("    %-22s", kv.first.c_str());
            for (int gi = 0; gi < kGainCount; ++gi)
            {
                const Stats& s = stats[isChord][gi][kv.first];
                std::printf ("  +%2.0f: %5.1f / %4.1f, %3.0f%%, %3.0f%%", kGains[gi], s.maxGr, s.blocks ? s.sumGr / static_cast<double> (s.blocks) : 0.0,
                             s.blocks ? 100.0 * static_cast<double> (s.over3) / static_cast<double> (s.blocks) : 0.0, s.blocks ? 100.0 * static_cast<double> (s.over6) / static_cast<double> (s.blocks) : 0.0);
            }
            std::printf ("\n");
        }
    }
    CHECK_EQ (aboveCeiling, 0L);
    CHECK_EQ (notFinite, 0L);
    CHECK_EQ (clips, 0L);
    CHECK_EQ (inversions, 0L);
}

namespace
{
/** The number of the member `category` inside the object `key` of the flat targets file (scripts/stock-level-targets.json); NaN when absent. */
double targetOf (const std::string& text, const std::string& key, const std::string& category)
{
    const size_t k = text.find ("\"" + key + "\"");
    if (k == std::string::npos)
        return std::nan ("");
    const size_t open = text.find ('{', k), close = text.find ('}', k);
    const size_t c = text.find ("\"" + category + "\"", open);
    if (open == std::string::npos || c == std::string::npos || c > close)
        return std::nan ("");
    return std::atof (text.c_str() + text.find (':', c) + 1);
}
} // namespace

// The levelling to the stock MPC instruments (scripts/level-to-stock.py, docs/release/LEVELLING-STOCK-2026-11-19.md) with the DEFAULT stage of the shells (kMakeupGainDb, the
// limiter off: no latency, no dynamics), Key 61 and XL profiles, the gestures of the levelling (0.3 s of silence, then held 3 s, velocity 127):
// (a) the hard peak rule: the four note chord in both voicings (48 55 60 64, 36 48 55 60), the six note (36 43 48 55 60 64) and the eight note chord (36 43 48 52 55 60 64 67)
//     peak at most -1.0 dBFS at the stage output, for every preset (so a host meter never reaches its over-0 segment without a limiter);
// (b) the loudness: the mean of a preset's dB errors against its category's stock medians over both profiles (Bass, Lead, Pad, FX: note peak, note sustained, sustained of
//     the stock-voiced chord, RMS 0.3 .. 2 s, a silent one left out; Keys, Pluck: the note peak), within 3 dB for every preset but those the levelling holds back by its
//     limits (kHeldBack); the single windows are counted and printed, not asserted.
// The presets the levelling holds back by its limits (from the report of scripts/level-to-stock.py; the reason per preset is in the document).
const std::string kHeldBack[] = {   // 65 presets, all held by the hard peak rule on a chord (our chords peak 9 to 10 dB over our notes, the stock ones 2 to 8 dB)
    "808 Boom", "Acid Saw Reso", "B3 Drawbar Organ", "Bell Chime Lead", "Bell FM Ripple Lead", "Bode Barber Riser", "Bowed MPE Strings", "Bowed Pressure Strings",
    "Brass Swell Ensemble", "Breath of Machines", "Cathedral Choir", "Celesta Bells", "Comb Reso Swell", "Custom Shape Wobble", "Dark Cinema Drone", "Deep Sea Ambient",
    "Diffusor Cloud Pad", "Dirty Peak Lead", "Drawbar Organ Keys", "Dream Grand Piano", "Dual Octave Hero", "EMS Squelch Bass", "Ethereal Glass", "FM Soft Organ",
    "Fast Arp Bass", "Frozen Aurora", "Game Over Zap", "Glass Organ Tones", "Glass Zone Bells", "Golden Dawn Pad", "Harmonic Cloud", "Harpsichord Bar",
    "Hyper Dimension Pad", "Infinite Horizon", "Just Intonation Keys", "Laser Beam Lead", "Lorenz Garden", "Lush Analog Pad", "Marimba Layer Keys", "Moog Ladder Bass",
    "Muted Honky Tonk", "Odd Harmonics Pad", "Pitch Drop 808", "Plucked Lead Tone", "Pulse Width Mod Lead", "Punchy Pluck Bass", "Radiant Pulse Pad", "Shepard Ascent",
    "Slow Motion Waves", "Space Odyssey Pad", "Starlight Shimmer", "Stretched Glass Choir", "Sub Drop Boom", "Subtle Tape Drift", "Sync Cry Lead", "Throat Singer Drone",
    "Tine Glass Keys", "Twin Warp Lead", "Utility Width Keys", "Velvet Strings", "Vibraphone Bars", "Vintage Brass Pad", "Vintage Electric Tine", "Vowel Choir Pad",
    "Warm Toy Piano",
};

TEST (output_stage_presets_sit_at_the_stock_level)
{
    std::ifstream tf (std::string (SYNTHYBOY_TEST_SOURCE_DIR) + "/scripts/stock-level-targets.json");
    std::stringstream ts;
    ts << tf.rdbuf();
    const std::string targets = ts.str();
    const auto presets = loadPresets();
    DirectoryContentStore store (SYNTHYBOY_TEST_CONTENT_STORE);
    const char* profiles[] = { "low", "high,voices=16,control=economy" };
    struct Played { double peak = 0, sus = -200; };
    auto play = [&] (const Preset& p, const Quality& q, const std::vector<int>& keys) {
        std::vector<float> L, R;
        renderRaw (p, q, &store, keys, L, R, 3.0, 0.0);
        OutputStage st;   // the default: the limiter off
        st.prepare (kSr);
        st.setGainDb (kMakeupGainDb);
        st.reset();
        CHECK (! st.limiter() && st.latencySamples() == 0);
        Played r;
        double pk = 0;
        for (size_t o = 0; o + kBlock <= L.size(); o += kBlock)
        {
            st.process (L.data() + o, R.data() + o, kBlock);
            for (size_t i = o; i < o + kBlock; ++i)
                pk = std::max ({ pk, static_cast<double> (std::fabs (L[i])), static_cast<double> (std::fabs (R[i])) });
        }
        r.peak = 20.0 * std::log10 (std::max (pk, 1e-10));
        double e = 0;
        const size_t a = static_cast<size_t> (0.3 * kSr), b = std::min (L.size(), static_cast<size_t> (2.0 * kSr));
        for (size_t i = a; i < b; ++i)
            e += static_cast<double> (L[i]) * L[i] + static_cast<double> (R[i]) * R[i];
        r.sus = 10.0 * std::log10 (std::max (e / (2.0 * static_cast<double> (b - a)), 1e-20));
        return r;
    };
    long windows = 0, windowsOutside = 0, outside = 0, chords = 0, overHard = 0, overFull = 0;
    double worst = -200;
    Quality qs[2];
    for (int k = 0; k < 2; ++k)
    {
        synthyboy_mpc::Profile profile;
        std::string why;
        CHECK (synthyboy_mpc::parseProfile (profiles[k], profile, &why));
        qs[k] = synthyboy_mpc::profileQuality (profile);
        qs[k].realtime = false;
    }
    for (const Preset& p : presets)
    {
        const bool decaying = p.category == "Keys" || p.category == "Pluck";
        double sum = 0;
        int n = 0;
        for (int k = 0; k < 2; ++k)
        {
            const Played note = play (p, qs[k], { 48 }), chordS = play (p, qs[k], { 48, 55, 60, 64 });
            auto err = [&] (double v, const char* key) {
                const double t = targetOf (targets, key, p.category);
                CHECK (std::isfinite (t));
                const double e = v - t;
                sum += e;
                ++n;
                ++windows;
                windowsOutside += std::fabs (e) > 3.0;
            };
            err (note.peak, "peak_note_v127");
            if (! decaying)
            {
                if (note.sus > -80.0)
                    err (note.sus, "sustain_note_v127");
                if (chordS.sus > -80.0)
                    err (chordS.sus, "sustain_chord_v127");
            }
            const Played others[] = { play (p, qs[k], { 36, 48, 55, 60 }), play (p, qs[k], { 36, 43, 48, 55, 60, 64 }), play (p, qs[k], { 36, 43, 48, 52, 55, 60, 64, 67 }) };
            for (const Played* c : { &chordS, &others[0], &others[1], &others[2] })
            {
                ++chords;
                worst = std::max (worst, c->peak);
                overFull += c->peak > 0.0;
                if (c->peak > -1.0 + 1e-6)
                {
                    ++overHard;
                    std::printf ("  over -1 dBFS: %s (%s, %s): chord peak %.2f dBFS\n", p.name.c_str(), p.category.c_str(), profiles[k], c->peak);
                }
            }
        }
        const double mean = sum / n;
        const bool held = std::find (std::begin (kHeldBack), std::end (kHeldBack), p.name) != std::end (kHeldBack);
        if (std::fabs (mean) > 3.0 && ! held)
        {
            ++outside;
            std::printf ("  outside the window: %s (%s): mean error %+.2f dB\n", p.name.c_str(), p.category.c_str(), mean);
        }
        if (held)
            std::printf ("  held back by the levelling's limits: %s (%s): mean error %+.2f dB\n", p.name.c_str(), p.category.c_str(), mean);
    }
    std::printf ("  limiter off: %ld chord renders (4 notes in two voicings, 6, 8; both profiles): the highest peak %.2f dBFS, %ld above -1 dBFS, %ld above 0 dBFS; presets outside +-3 dB (mean error): %ld; single windows outside: %ld of %ld\n",
                 chords, worst, overHard, overFull, outside, windowsOutside, windows);
    CHECK_EQ (overHard, 0L);
    CHECK_EQ (overFull, 0L);
    CHECK_EQ (outside, 0L);
}
