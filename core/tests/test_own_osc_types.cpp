// SynthyBoy - own engine tests: the sample, granular, spectral and multisample oscillator types, user wavetables, the warps Remap / Odd-Even / Filter, the
// spectral morph and the memory of the wavetable bank (docs/engine-spec/07 section 8, vectors S1 .. S5, and 02 section 13).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

#include "osc/wavetable_bank.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/offline_render.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;

struct Out
{
    std::vector<float> l, r;
};

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

std::vector<uint8_t> sineWav (double hz, double amp, int rate, double seconds)
{
    const size_t n = static_cast<size_t> (rate * seconds);
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float> (amp * std::sin (6.283185307179586 * hz * static_cast<double> (i) / rate));
    return encodeWav (x.data(), nullptr, n, rate, 16);
}

struct Rig
{
    MemoryContentStore store;
    StateContent content;
    std::unique_ptr<IEngine> e;

    explicit Rig (bool withSample = true, const char* extraKey = nullptr, float extraValue = 0.0f)
    {
        e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->prepare (kSr, 256, qualityForTier (Tier::high));
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "osc1.random_phase", 0.0f);
        if (extraKey != nullptr)
            set (*e, extraKey, extraValue);
        if (withSample)
        {
            store.add ("test", "sine.wav", sineWav (440.0, 0.8, 44100, 2.0));
            ContentFileRef ref;
            ref.pack = "test";
            ref.file = "sine.wav";
            ref.name = "sine";
            attachSample (content, 1, ref, 44100, 88200, 1);
            e->setContentStore (&store);
            e->setContent (content);
        }
        set (*e, "osc1.sample.root", 69.0f);
        set (*e, "osc1.sample.loop_mode", 1.0f);
    }

    Out render (int key, double noteSeconds, double totalSeconds)
    {
        const size_t total = static_cast<size_t> (totalSeconds * kSr), offAt = static_cast<size_t> (noteSeconds * kSr);
        Out o;
        o.l.assign (total, 0.0f);
        o.r.assign (total, 0.0f);
        size_t pos = 0;
        while (pos < total)
        {
            const size_t n = std::min<size_t> (256, total - pos);
            Event ev[2];
            int ne = 0;
            if (pos == 0)
                ev[ne++] = sbtest::noteOn (0, key, 1.0f);
            if (offAt >= pos && offAt < pos + n)
                ev[ne++] = sbtest::noteOff (static_cast<int> (offAt - pos), key);
            e->process (ev, ne, o.l.data() + pos, o.r.data() + pos, static_cast<int> (n));
            pos += n;
        }
        return o;
    }
};

/** Amplitude (linear) of the spectral line nearest `hz` in x[a, b) (Hann window, +-10 % search). Returns the line's amplitude and its frequency in `peakHz`. */
double lineAmplitude (const std::vector<float>& x, size_t a, size_t b, double hz, double* peakHz = nullptr)
{
    double best = 0.0, bestHz = hz;
    const size_t n = b - a;
    double wsum = 0.0;
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i)
    {
        w[i] = 0.5 - 0.5 * std::cos (6.283185307179586 * static_cast<double> (i) / static_cast<double> (n));
        wsum += w[i];
    }
    for (double f = hz * 0.9; f <= hz * 1.1; f += hz * 0.0005 + 0.01)
    {
        std::complex<double> acc (0.0, 0.0);
        for (size_t i = 0; i < n; ++i)
            acc += w[i] * static_cast<double> (x[a + i]) * std::polar (1.0, -6.283185307179586 * f * static_cast<double> (i) / kSr);
        const double amp = 2.0 * std::abs (acc) / wsum;
        if (amp > best)
        {
            best = amp;
            bestHz = f;
        }
    }
    if (peakHz != nullptr)
        *peakHz = bestHz;
    return best;
}

double db (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

double peakOf (const std::vector<float>& x, size_t a, size_t b)
{
    double p = 0.0;
    for (size_t i = a; i < b; ++i)
        p = std::max (p, static_cast<double> (std::fabs (x[i])));
    return p;
}
} // namespace

// S1: the sample type plays the loaded sample at the key pitch
TEST (own_osc_sample_type_pitch_and_level)
{
    Rig r (true, "osc1.type", 1.0f);
    double hz = 0.0;
    Out o = r.render (69, 1.0, 1.0);
    const double amp = lineAmplitude (o.l, 14400, 43200, 440.0, &hz);
    std::printf ("    S1 note 69: %.2f Hz, %.2f dB, peak %.3f\n", hz, db (amp), peakOf (o.l, 14400, 43200));
    CHECK_NEAR (hz, 440.0, 1.0);
    CHECK_NEAR (db (amp), -4.93, 1.0); // the mono sample voice costs 3 dB (centre pan law)
    Rig r2 (true, "osc1.type", 1.0f);
    o = r2.render (81, 1.0, 1.0);
    lineAmplitude (o.l, 14400, 43200, 880.0, &hz);
    CHECK_NEAR (hz, 880.0, 1.5);
    Rig r3 (true, "osc1.type", 1.0f);
    set (*r3.e, "osc1.transpose", 12.0f);
    o = r3.render (69, 1.0, 1.0);
    lineAmplitude (o.l, 14400, 43200, 880.0, &hz);
    CHECK_NEAR (hz, 880.0, 1.5);
    Rig r4 (true, "osc1.type", 1.0f);
    set (*r4.e, "osc1.sample.keytrack", 0.0f);
    o = r4.render (81, 1.0, 1.0);
    lineAmplitude (o.l, 14400, 43200, 440.0, &hz);
    CHECK_NEAR (hz, 440.0, 1.0);
}

// S2: two unison voices spread hard left and right at -200 / +200 ct
TEST (own_osc_sample_type_unison_is_a_line_of_voices_panned_across)
{
    Rig r (true, "osc1.type", 1.0f);
    set (*r.e, "osc1.unison.voices", 2.0f);
    set (*r.e, "osc1.unison.detune", 100.0f);
    set (*r.e, "osc1.unison.range", 2.0f);
    set (*r.e, "osc1.unison.detune_curve", 0.0f);
    set (*r.e, "osc1.unison.spread", 100.0f);
    set (*r.e, "osc1.unison.blend", 100.0f);
    const Out o = r.render (69, 1.0, 1.0);
    double hl = 0.0, hr = 0.0;
    const double al = lineAmplitude (o.l, 14400, 43200, 392.03, &hl);
    const double ar = lineAmplitude (o.r, 14400, 43200, 493.91, &hr);
    std::printf ("    S2 left %.2f Hz %.2f dB, right %.2f Hz %.2f dB\n", hl, db (al), hr, db (ar));
    CHECK_NEAR (hl, 392.03, 1.5);
    CHECK_NEAR (hr, 493.91, 1.5);
    CHECK_NEAR (db (al), -4.94, 1.0);
    CHECK_NEAR (db (ar), -4.94, 1.0);
}

// S4: granular and spectral types play the same sample at the same pitch and at a level in the range of the Sample type
TEST (own_osc_granular_and_spectral_types_play_the_sample)
{
    const double expect[2] = { -1.49, -2.90 };
    for (int t = 2; t <= 3; ++t)
    {
        Rig r (true, "osc1.type", static_cast<float> (t));
        const Out o = r.render (69, 1.0, 1.2);
        double hz = 0.0;
        const double amp = lineAmplitude (o.l, 14400, 43200, 440.0, &hz);
        std::printf ("    S4 type %d: %.2f Hz, %.2f dB (vector %.2f), peak %.3f\n", t, hz, db (amp), expect[t - 2], peakOf (o.l, 14400, 43200));
        CHECK_NEAR (hz, 440.0, 2.0);
        CHECK_NEAR (db (amp), expect[t - 2], 1.5);
    }
}

// S5: a sample type without content is silent
TEST (own_osc_sample_type_without_content_is_silent)
{
    for (int t = 1; t <= 4; ++t)
    {
        Rig r (false, "osc1.type", static_cast<float> (t));
        const Out o = r.render (69, 0.5, 0.6);
        CHECK (peakOf (o.l, 0, o.l.size()) == 0.0);
        CHECK (peakOf (o.r, 0, o.r.size()) == 0.0);
    }
}

// S3: changing the type while a note sounds crosses over smoothly (10 ms)
TEST (own_osc_type_change_under_a_note_does_not_click)
{
    Rig r (true);
    set (*r.e, "osc1.wavetable", 0.0f); // sine
    const size_t total = static_cast<size_t> (0.6 * kSr);
    std::vector<float> l (total), rr (total);
    size_t pos = 0;
    bool switched = false;
    while (pos < total)
    {
        const size_t n = std::min<size_t> (128, total - pos);
        Event ev[1];
        int ne = 0;
        if (pos == 0)
            ev[ne++] = sbtest::noteOn (0, 69, 1.0f);
        if (!switched && pos >= static_cast<size_t> (0.3 * kSr))
        {
            set (*r.e, "osc1.type", 1.0f);
            switched = true;
        }
        r.e->process (ev, ne, l.data() + pos, rr.data() + pos, static_cast<int> (n));
        pos += n;
    }
    double worst = 0.0;
    for (size_t i = static_cast<size_t> (0.28 * kSr); i + 1 < total; ++i)
        worst = std::max (worst, static_cast<double> (std::fabs (l[i + 1] - l[i])));
    std::printf ("    S3 largest sample step across the type change: %.4f\n", worst);
    CHECK (worst < 0.2);
    CHECK (peakOf (l, static_cast<size_t> (0.4 * kSr), total) > 0.2); // the sample type sounds afterwards
}

// the content of a state: a user wavetable (Preset table) is played, a missing file is the saw
TEST (own_osc_user_wavetable_plays_and_falls_back_to_the_saw)
{
    std::vector<float> frame (2048 * 2);
    for (int i = 0; i < 2048; ++i)
    {
        frame[static_cast<size_t> (i)] = static_cast<float> (std::sin (6.283185307179586 * i / 2048.0));
        frame[static_cast<size_t> (2048 + i)] = static_cast<float> (std::sin (3.0 * 6.283185307179586 * i / 2048.0));
    }
    MemoryContentStore store;
    store.add ("test", "t.wav", encodeWav (frame.data(), nullptr, frame.size(), 44100, 16));
    StateContent c;
    ContentFileRef ref;
    ref.pack = "test";
    ref.file = "t.wav";
    attachWavetable (c, 1, ref, "me", 2);
    auto e = createOwnEngine();
    e->prepare (kSr, 256, qualityForTier (Tier::high));
    set (*e, "filter1.on", 0.0f);
    set (*e, "master.volume", 0.0f);
    set (*e, "osc1.wavetable", 13.0f);
    set (*e, "osc1.frame", 100.0f);
    set (*e, "osc1.random_phase", 0.0f);
    set (*e, "osc1.level", 100.0f);
    auto run = [&]
    {
        std::vector<float> l (24000), rr (24000);
        Event ev[1] = { sbtest::noteOn (0, 57, 1.0f) };
        e->process (ev, 1, l.data(), rr.data(), 256);
        e->process (nullptr, 0, l.data() + 256, rr.data() + 256, 24000 - 256);
        return l;
    };
    const auto saw = run(); // no content yet: the saw
    e->setContentStore (&store);
    e->setContent (c);
    e->reset();
    const auto table = run();
    const double h3 = lineAmplitude (table, 6000, 18000, 660.0), h1 = lineAmplitude (table, 6000, 18000, 220.0);
    std::printf ("    user table frame 2: harmonic 3 %.2f dB, harmonic 1 %.2f dB\n", db (h3), db (h1));
    CHECK (db (h3) > -3.0);
    CHECK (db (h1) < -40.0);
    CHECK (db (lineAmplitude (saw, 6000, 18000, 660.0)) < db (h3) - 6.0);
}

// 02 O11 / O12: Odd/Even and Filter LPF reproduce the stated harmonic levels of the saw
TEST (own_osc_warps_odd_even_and_filter)
{
    auto run = [] (const char* type, float amount)
    {
        auto e = createOwnEngine();
        e->prepare (kSr, 256, qualityForTier (Tier::high));
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "osc1.wavetable", 1.0f);
        set (*e, "osc1.random_phase", 0.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "osc1.warp.type", [&]
        {
            const ParamInfo* p = findParamByKey ("osc1.warp.type");
            return static_cast<float> (std::string (type) == "oddeven" ? 24 : (std::string (type) == "lpf" ? 25 : (std::string (type) == "hpf" ? 26 : 0))) + (p ? 0.0f : 0.0f);
        }());
        set (*e, "osc1.warp.amount", amount);
        std::vector<float> l (48000), rr (48000);
        Event ev[1] = { sbtest::noteOn (0, 57, 1.0f) };
        e->process (ev, 1, l.data(), rr.data(), 256);
        e->process (nullptr, 0, l.data() + 256, rr.data() + 256, 48000 - 256);
        return l;
    };
    auto h = [] (const std::vector<float>& x, int k) { return db (lineAmplitude (x, 9600, 38400, 220.0 * k)); };
    const auto ref = run ("none", 50.0f);
    const auto oe25 = run ("oddeven", 25.0f), oe75 = run ("oddeven", 75.0f), oe50 = run ("oddeven", 50.0f);
    std::printf ("    O11 saw h1..h4: %.1f %.1f %.1f %.1f | odd/even 25: %.1f %.1f %.1f %.1f | 75: %.1f %.1f %.1f %.1f\n", h (ref, 1), h (ref, 2), h (ref, 3), h (ref, 4),
                 h (oe25, 1), h (oe25, 2), h (oe25, 3), h (oe25, 4), h (oe75, 1), h (oe75, 2), h (oe75, 3), h (oe75, 4));
    CHECK_NEAR (h (oe50, 1), h (ref, 1), 0.4);
    CHECK_NEAR (h (oe50, 2), h (ref, 2), 0.4);
    CHECK_NEAR (h (oe25, 1), h (ref, 1), 0.5);              // 25 %: -5.3 -17.4 -14.9 -23.4 (even harmonics 6 dB down)
    CHECK_NEAR (h (oe25, 2), h (ref, 2) - 6.0, 0.5);
    CHECK_NEAR (h (oe75, 1), h (ref, 1) - 6.0, 0.5);        // 75 %: odd harmonics 6 dB down
    CHECK_NEAR (h (oe75, 2), h (ref, 2), 0.5);
    const auto lp25 = run ("lpf", 25.0f), lp75 = run ("lpf", 75.0f);
    std::printf ("    O12 filter LPF 25: %.1f %.1f %.1f %.1f (vector -6.1 -14.1 -20.1 -25.3) | 75: %.1f %.1f %.1f %.1f (-5.3 -11.4 -15.0 -17.5)\n", h (lp25, 1), h (lp25, 2),
                 h (lp25, 3), h (lp25, 4), h (lp75, 1), h (lp75, 2), h (lp75, 3), h (lp75, 4));
    CHECK_NEAR (h (lp25, 1), -6.1, 0.8);
    CHECK_NEAR (h (lp25, 2), -14.1, 1.0);
    CHECK_NEAR (h (lp25, 3), -20.1, 1.0);
    CHECK_NEAR (h (lp75, 2), -11.4, 0.6);
}

// 02 O13 / O14 / O15b: spectral morph low pass, high pass, harmonic stretch on the saw
TEST (own_osc_spectral_morph_vectors)
{
    auto run = [] (float type, float amount)
    {
        auto e = createOwnEngine();
        e->prepare (kSr, 256, qualityForTier (Tier::high));
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "osc1.wavetable", 1.0f);
        set (*e, "osc1.random_phase", 0.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "osc1.morph.type", type);
        set (*e, "osc1.morph.amount", amount);
        std::vector<float> l (48000), rr (48000);
        Event ev[1] = { sbtest::noteOn (0, 57, 1.0f) };
        e->process (ev, 1, l.data(), rr.data(), 256);
        e->process (nullptr, 0, l.data() + 256, rr.data() + 256, 48000 - 256);
        return l;
    };
    auto h = [] (const std::vector<float>& x, int k) { return db (lineAmplitude (x, 9600, 38400, 220.0 * k)); };
    const auto ref = run (0.0f, 50.0f);
    const auto lp = run (7.0f, 25.0f), hp = run (8.0f, 25.0f), st = run (3.0f, 75.0f);
    std::printf ("    O13 morph low pass 25: h5 %.1f h6 %.1f h7 %.1f (vector -19.3, -24.5, < -150) | O14 high pass: h4 %.1f h5 %.1f h6 %.1f (< -150, -28.7, -20.9) | O15b stretch 75: h1 %.1f h5 %.1f h2 %.1f (-5.3 -11.4 < -120)\n",
                 h (lp, 5), h (lp, 6), h (lp, 7), h (hp, 4), h (hp, 5), h (hp, 6), h (st, 1), h (st, 5), h (st, 2));
    CHECK_NEAR (h (lp, 5), h (ref, 5), 0.5);
    CHECK_NEAR (h (lp, 6), -24.5, 1.0);
    CHECK (h (lp, 7) < -60.0);
    CHECK (h (hp, 4) < -60.0);
    CHECK_NEAR (h (hp, 5), -28.7, 1.5);
    CHECK_NEAR (h (hp, 6), h (ref, 6), 0.5);
    CHECK_NEAR (h (st, 1), -5.3, 1.0);
    CHECK_NEAR (h (st, 5), -11.4, 1.0);
    CHECK (h (st, 2) < -60.0);
}

// the memory of the built-in wavetable bank (iOS budget: the plug-in stays well below 100 MB, the bank must not take half of it)
TEST (own_wavetable_bank_memory)
{
    const size_t bytes = own::builtinBankBytes();
    std::printf ("    built-in wavetable bank: %.1f MB (the eight 64 frame tables with all mip levels and spectra)\n", static_cast<double>(bytes) / 1048576.0);
    CHECK (bytes < 32u * 1024u * 1024u);
}

// Render OSC Warp bakes the warp of the oscillator into a 256 frame table; the table played at the same warp position sounds like the warp
TEST (own_render_osc_warp_bake_matches_the_live_warp)
{
    ParamValues vals = ParamValues::defaults();
    auto setv = [&] (const char* key, float v)
    {
        const ParamInfo* p = findParamByKey (key);
        CHECK (p != nullptr);
        if (p != nullptr)
            vals.plain[static_cast<size_t> (findParamIndex (p->id))] = v;
    };
    setv ("osc1.warp.type", 1.0f);
    setv ("osc1.warp.amount", 40.0f);
    setv ("filter1.on", 0.0f);
    convert::OfflineRunner run = [] (const OfflineRequest& r, OfflineAudio& a)
    {
        auto twin = createOwnEngine();
        return runOfflineRequest (*twin, r, a);
    };
    wtedit::Wavetable table;
    convert::WarpRenderOptions opt;
    std::string err;
    float scale = 1.0f;
    CHECK (convert::renderOscWarp (run, &vals, opt, "bake", table, &err, &scale));
    CHECK_EQ (static_cast<int> (table.numFrames()), 256);
    // the live warp at 40 %
    auto live = createOwnEngine();
    live->prepare (kSr, 256, qualityForTier (Tier::high));
    auto render = [] (IEngine& e)
    {
        std::vector<float> l (48000), rr (48000);
        Event ev[1] = { sbtest::noteOn (0, 57, 1.0f) };
        e.process (ev, 1, l.data(), rr.data(), 256);
        e.process (nullptr, 0, l.data() + 256, rr.data() + 256, 48000 - 256);
        return l;
    };
    for (const char* k : { "filter1.on", "master.volume", "osc1.random_phase", "osc1.level" })
        set (*live, k, std::string (k) == "osc1.level" ? 100.0f : 0.0f);
    set (*live, "osc1.warp.type", 1.0f);
    set (*live, "osc1.warp.amount", 40.0f);
    const auto ref = render (*live);
    auto baked = createOwnEngine();
    baked->prepare (kSr, 256, qualityForTier (Tier::high));
    for (const char* k : { "filter1.on", "master.volume", "osc1.random_phase", "osc1.level" })
        set (*baked, k, std::string (k) == "osc1.level" ? 100.0f : 0.0f);
    std::vector<uint8_t> bytes;
    {
        std::vector<float> all;
        for (size_t f = 0; f < table.numFrames(); ++f)
            all.insert (all.end(), table.getFrame (f).data.begin(), table.getFrame (f).data.end());
        bytes = encodeWav (all.data(), nullptr, all.size(), 44100, 16);
    }
    MemoryContentStore store;
    store.add ("t", "b.wav", bytes);
    StateContent c;
    ContentFileRef ref2;
    ref2.pack = "t";
    ref2.file = "b.wav";
    attachWavetable (c, 1, ref2, "x", 256);
    baked->setContentStore (&store);
    baked->setContent (c);
    set (*baked, "osc1.wavetable", 13.0f);
    set (*baked, "osc1.frame", 40.0f);
    set (*baked, "osc1.level", 100.0f);
    const auto bk = render (*baked);
    double worst = 0.0;
    for (int k = 1; k <= 24; ++k)
    {
        const double a = db (lineAmplitude (ref, 9600, 38400, 220.0 * k)), b = db (lineAmplitude (bk, 9600, 38400, 220.0 * k));
        if (a > -30.0)
            worst = std::max (worst, std::fabs (a - b));
        if (k <= 24)
            std::printf ("    h%d live %.2f baked %.2f\n", k, a, b);
    }
    {
        double rl = 0, rb = 0;
        for (size_t i = 9600; i < 38400; ++i) { rl += ref[i] * ref[i]; rb += bk[i] * bk[i]; }
        std::printf ("    live peak %.4f rms %.4f | baked peak %.4f rms %.4f\n", peakOf (ref, 9600, 38400), std::sqrt (rl / 28800), peakOf (bk, 9600, 38400), std::sqrt (rb / 28800));
    }
    std::printf ("    bake vs live warp: worst difference %.3f dB, scale %.4f\n", worst, scale);
    CHECK (worst < 0.1);
}
