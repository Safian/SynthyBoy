// SynthyBoy - level-probe: the levels of every factory preset as a shell hands them to its host, for the levelling against the stock MPC instruments (scripts/level-to-stock.py,
// docs/release/LEVELLING-STOCK-2026-11-19.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_level_probe --presets DIR --content-dir DIR --profile TEXT [--gain DB] [--filter TEXT] [--note K] [--chord K,K,..] [--chord6 K,K,..] [--chord8 K,K,..] [--chord-stock K,K,..] [--limiter on|off] [--hold S] [--sr HZ]
//
// Per preset (own engine, a fresh engine per gesture, 0.3 s of silence before the note (the smoothing of master.volume), offline, 128 frame blocks, the quality of the device profile TEXT of shell-mpc/device_profile.h, e.g. "low" = Key 61,
// "high,voices=16,control=economy" = XL) five gestures held --hold seconds (default 3): the note (default key 48 = C3 with C4 = 60) at velocity 127 and 100, the chord (default
// 36 48 55 60) at 127 and 100, the six note chord (default 36 43 48 55 60 64) and the eight note chord (default 36 43 48 52 55 60 64 67) at 127, and the chord of the stock reference (chordS, default 48 55 60 64: the voicing the stock
// instruments were measured with) at 127 and 100, and the chord of the preset gate (chordG: C1 for Bass, else 60, plus 4, 7, 12 semitones) at 127, released after 1 s as the gate does. Each through the shells' output stage (core/include/synthyboy/output_stage.h, make-up gain
// --gain, default kMakeupGainDb; --limiter on|off, default off: the default sound of the shells). One JSON object per line and preset: for every gesture
//   raw      sample peak of the raw engine output (dBFS)            pre    raw + gain: the peak the limiter sees (dBFS)
//   post     sample peak after the stage (what the host gets)       sus    RMS of both channels after the stage, 0.3 .. 2.0 s after the strike (dBFS)
//   mom      loudest 400 ms after the stage (BS.1770 K-weighted momentary loudness, LUFS; K-weighting coefficients of 48 kHz, so --sr 48000)
//   gr_max   deepest gain reduction (dB)                            engaged  share of the 128 frame blocks in which the limiter reduced at all (gainReductionDb > 0.01 dB)
//   early    raw peak of the first 100 ms (the strike) against raw over the whole hold
// Deterministic.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
#include "../../shell-mpc/device_profile.h"
#include "../../shell-mpc/output_stage.h"

using namespace synthyboy;

namespace
{
constexpr int kBlock = 128;

struct Gesture
{
    const char* name;
    std::vector<int> keys;
    float velocity;
    double offAt = 0.0;   // seconds: the note off (0: held to the end)
};

struct Result
{
    double raw = -200, pre = -200, post = -200, sus = -200, mom = -200, grMax = 0, engaged = 0, early = -200;
};

double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-10)); }

std::vector<int> parseKeys (const std::string& s)
{
    std::vector<int> k;
    std::stringstream ss (s);
    std::string t;
    while (std::getline (ss, t, ','))
        if (! t.empty())
            k.push_back (std::atoi (t.c_str()));
    return k;
}

double momentaryMax (const std::vector<float>& L, const std::vector<float>& R, double sr)
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
    const size_t win = static_cast<size_t> (0.4 * sr);
    std::vector<double> pre (l.size() + 1, 0.0);
    for (size_t i = 0; i < l.size(); ++i)
        pre[i + 1] = pre[i] + l[i] * l[i] + r[i] * r[i];
    double best = 0.0;
    for (size_t s = 0; s + win <= l.size(); s += kBlock)
        best = std::max (best, (pre[s + win] - pre[s]) / static_cast<double> (win));
    return -0.691 + 10.0 * std::log10 (std::max (best, 1e-20));
}

Result run (const ParamValues& values, const StateContent& content, const Quality& q, const IContentStore* store, const Gesture& g, double sr, double hold, float gainDb, bool limiter, float voiceGainAlpha)
{
    auto engine = createEngine (EngineKind::own);
    setOwnVoiceCountAlpha (*engine, voiceGainAlpha);
    engine->prepare (sr, kBlock, q);
    engine->setContentStore (store);
    for (size_t i = 0; i < paramCount(); ++i)
        engine->setParam (paramTable()[i].id, values.plain[i]);
    engine->setContent (content);
    OutputStage stage;
    stage.setLimiter (limiter);
    stage.prepare (sr);
    stage.setGainDb (gainDb);
    stage.reset();
    // 0.3 s of silence first: the engine smooths master.volume from its default (-12 dB) when a state is loaded, and a note struck at once would carry that ramp (a burst in
    // a preset quieter than the default, a dip in a louder one); on the device the plugin has run before a note comes
    {
        std::vector<float> zl (kBlock), zr (kBlock);
        for (int b = 0; b < static_cast<int> (0.3 * sr / kBlock); ++b)
        {
            engine->process (nullptr, 0, zl.data(), zr.data(), kBlock);
            stage.process (zl.data(), zr.data(), kBlock);
        }
        stage.reset();
    }
    const int blocks = static_cast<int> (std::ceil (hold * sr / kBlock));
    std::vector<float> L (static_cast<size_t> (blocks) * kBlock), R (L.size());
    Result r;
    double rawPk = 0, postPk = 0, earlyPk = 0;
    const int earlyBlocks = static_cast<int> (0.1 * sr / kBlock);
    int engaged = 0;
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<Event> ev;
        const int offBlock = g.offAt > 0.0 ? static_cast<int> (std::ceil (g.offAt * sr / kBlock)) : -1;
        if (b == offBlock)
            for (int k : g.keys)
            {
                Event e;
                e.frame = 0;
                e.type = Event::NoteOff;
                e.channel = 0;
                e.key = static_cast<int16_t> (k);
                e.value = 0.0f;
                e.noteId = -1;
                ev.push_back (e);
            }
        if (b == 0)
            for (int k : g.keys)
            {
                Event e;
                e.frame = 0;
                e.type = Event::NoteOn;
                e.channel = 0;
                e.key = static_cast<int16_t> (k);
                e.value = g.velocity;
                e.noteId = k;
                ev.push_back (e);
            }
        float* l = L.data() + static_cast<size_t> (b) * kBlock;
        float* rr = R.data() + static_cast<size_t> (b) * kBlock;
        engine->process (ev.empty() ? nullptr : ev.data(), static_cast<int> (ev.size()), l, rr, kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i])) l[i] = 0.0f;
            if (! std::isfinite (rr[i])) rr[i] = 0.0f;
            rawPk = std::max ({ rawPk, static_cast<double> (std::fabs (l[i])), static_cast<double> (std::fabs (rr[i])) });
        }
        if (b < earlyBlocks)
            earlyPk = rawPk;
        stage.process (l, rr, kBlock);
        for (int i = 0; i < kBlock; ++i)
            postPk = std::max ({ postPk, static_cast<double> (std::fabs (l[i])), static_cast<double> (std::fabs (rr[i])) });
        const double gr = stage.gainReductionDb();
        r.grMax = std::max (r.grMax, gr);
        engaged += gr > 0.01;
    }
    r.raw = db (rawPk);
    r.early = db (earlyPk);
    r.pre = r.raw + gainDb;
    r.post = db (postPk);
    r.engaged = static_cast<double> (engaged) / blocks;
    const size_t a = static_cast<size_t> (0.3 * sr), e = std::min (L.size(), static_cast<size_t> (2.0 * sr));
    double s = 0;
    for (size_t i = a; i < e; ++i)
        s += static_cast<double> (L[i]) * L[i] + static_cast<double> (R[i]) * R[i];
    r.sus = e > a ? 10.0 * std::log10 (std::max (s / (2.0 * static_cast<double> (e - a)), 1e-20)) : -200.0;
    r.mom = momentaryMax (L, R, sr);
    return r;
}
} // namespace

int main (int argc, char** argv)
{
    std::string presets, contentDir, profileText = "low", filter;
    float gain = kMakeupGainDb;
    double sr = 48000.0, hold = 3.0;
    std::vector<int> note { 48 }, chord { 36, 48, 55, 60 }, chordS { 48, 55, 60, 64 }, chord6 { 36, 43, 48, 55, 60, 64 }, chord8 { 36, 43, 48, 52, 55, 60, 64, 67 };
    bool limiter = false;
    float voiceGainAlpha = kVoiceCountAlphaDefault;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--presets") presets = next();
        else if (a == "--content-dir") contentDir = next();
        else if (a == "--profile") profileText = next();
        else if (a == "--gain") gain = static_cast<float> (std::atof (next().c_str()));
        else if (a == "--filter") filter = next();
        else if (a == "--note") note = parseKeys (next());
        else if (a == "--chord") chord = parseKeys (next());
        else if (a == "--chord6") chord6 = parseKeys (next());
        else if (a == "--chord-stock") chordS = parseKeys (next());
        else if (a == "--chord8") chord8 = parseKeys (next());
        else if (a == "--limiter") limiter = next() == "on";
        else if (a == "--hold") hold = std::atof (next().c_str());
        else if (a == "--sr") sr = std::atof (next().c_str());
        else if (a == "--voice-gain-alpha") voiceGainAlpha = static_cast<float> (std::atof (next().c_str()));
        else
        {
            std::fprintf (stderr, "level-probe: unknown argument %s\n", a.c_str());
            return 2;
        }
    }
    if (presets.empty() || ! isEngineAvailable (EngineKind::own))
    {
        std::fprintf (stderr, "usage: synthyboy_level_probe --presets DIR --content-dir DIR --profile TEXT [--gain DB] [--filter TEXT] [--note K] [--chord K,..] [--chord6 K,..] [--hold S] [--sr HZ] [--voice-gain-alpha A]\n");
        return 2;
    }
    synthyboy_mpc::Profile profile;
    std::string why;
    if (! synthyboy_mpc::parseProfile (profileText, profile, &why))
    {
        std::fprintf (stderr, "level-probe: profile '%s': %s\n", profileText.c_str(), why.c_str());
        return 2;
    }
    Quality q = synthyboy_mpc::profileQuality (profile);
    q.realtime = false;
    std::unique_ptr<DirectoryContentStore> store;
    if (! contentDir.empty())
        store = std::make_unique<DirectoryContentStore> (contentDir);

    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::recursive_directory_iterator (presets))
        if (e.is_regular_file() && e.path().extension() == ".sbpreset" && e.path().string().find (filter) != std::string::npos)
            files.push_back (e.path());
    std::sort (files.begin(), files.end());
    const Gesture gestures[] = { { "note_v127", note, 1.0f }, { "note_v100", note, 100.0f / 127.0f }, { "note_v64", note, 64.0f / 127.0f }, { "chord_v127", chord, 1.0f }, { "chord_v100", chord, 100.0f / 127.0f },
                                 { "chord6_v127", chord6, 1.0f }, { "chord8_v127", chord8, 1.0f },
                                 { "chordS_v127", chordS, 1.0f }, { "chordS_v100", chordS, 100.0f / 127.0f } };
    for (const auto& f : files)
    {
        std::ifstream in (f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        PresetMeta meta;
        ParamValues values = ParamValues::defaults();
        StateContent content;
        const LoadResult lr = loadPreset (text.data(), text.size(), meta, values, content);
        if (lr.status != LoadStatus::ok && lr.status != LoadStatus::newer_version)
        {
            std::fprintf (stderr, "level-probe: %s does not load\n", f.string().c_str());
            return 1;
        }
        float trimDb = 0.0f;
        synthyboy_mpc::tierTrimDbFromPreset (text.data(), text.size(), trimDb);
        const bool isLow = profile == synthyboy_mpc::profileLow();
        const float effectiveGain = gain + (isLow ? trimDb : 0.0f);
        std::printf ("{\"name\":\"%s\",\"category\":\"%s\",\"profile\":\"%s\",\"gain\":%.2f,\"tierTrimDb\":%.2f", meta.name.c_str(), meta.category.c_str(), profileText.c_str(), gain, isLow ? trimDb : 0.0f);
        // the chord of the preset gate (synthyboy_preset_render: C1 for Bass, else C3 with C4 = 60, plus 4, 7, 12 semitones; its raw peak must stay under +3 dBFS)
        const int gk = meta.category == "Bass" ? 36 : 60;
        std::vector<Gesture> all (std::begin (gestures), std::end (gestures));
        all.push_back ({ "chordG_v127", { gk, gk + 4, gk + 7, gk + 12 }, 1.0f, 1.0 });   // held 1 s and released, as the gate plays it
        for (const Gesture& g : all)
        {
            const Result r = run (values, content, q, store.get(), g, sr, hold, effectiveGain, limiter, voiceGainAlpha);
            std::printf (",\"%s\":{\"raw\":%.3f,\"pre\":%.3f,\"post\":%.3f,\"sus\":%.3f,\"mom\":%.3f,\"gr_max\":%.3f,\"engaged\":%.4f,\"early\":%.3f}", g.name, r.raw, r.pre, r.post, r.sus, r.mom, r.grMax, r.engaged, r.early);
        }
        std::printf ("}\n");
        std::fflush (stdout);
    }
    return 0;
}
