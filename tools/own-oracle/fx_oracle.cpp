// SynthyBoy - fx-oracle: the effects of engine-own against engine-vital through IEngine (black box): level, tail and RT60 of a burst, one table row per effect setting.
// usage: synthyboy_fx_oracle [reverb|modules|flanger|chorus|phaser|phasernotches|all]   (a markdown table on stdout)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <string>

#include "own_oracle.h"

using namespace oracle;
using synthyboy::EngineKind;

namespace
{
constexpr double kSr = 48000.0;

float gLevel = 100.0f;

Overrides burst()
{
    return { { "osc1.wavetable", 1 }, { "osc1.level", gLevel }, { "filter1.on", 0 }, { "master.volume", 0 }, { "env1.attack", 0 }, { "env1.decay", 20 }, { "env1.sustain", 0 }, { "env1.release", 5 } };
}

struct Metrics { double peakDb, rt60, tail1, tail2, rms; };

/** Peak of the 10 ms energy windows (dB), the windows 0.5 and 1 s after the note on, the RT60 of the backward integrated energy (-5 .. -35 dB, x2), the RMS of the first second. */
Metrics measure (EngineKind kind, const Overrides& ov, double secs, double noteLen = 0.02)
{
    Note n;
    n.start = 0.1;
    n.length = noteLen;
    n.key = 60;
    const Stereo o = render (kind, ov, { n }, secs, kSr);
    const size_t w = static_cast<size_t> (kSr * 0.01);
    std::vector<double> win;
    for (size_t i = 0; i + w <= o.l.size(); i += w)
    {
        double s = 0;
        for (size_t k = 0; k < w; ++k)
            s += static_cast<double> (o.l[i + k]) * o.l[i + k];
        win.push_back (10.0 * std::log10 (std::max (s, 1.0e-30)));
    }
    Metrics m {};
    m.peakDb = -300;
    for (double v : win)
        m.peakDb = std::max (m.peakDb, v);
    auto at = [&] (double t) { const size_t i = static_cast<size_t> (t / 0.01); return i < win.size() ? win[i] : -300.0; };
    m.tail1 = at (0.6) - m.peakDb;
    m.tail2 = at (1.1) - m.peakDb;
    m.rms = rms (o.l, static_cast<size_t> (0.1 * kSr), static_cast<size_t> (1.1 * kSr));
    std::vector<double> edc (o.l.size());
    double acc = 0;
    for (size_t i = o.l.size(); i-- > static_cast<size_t> (0.1 * kSr);)
    {
        acc += static_cast<double> (o.l[i]) * o.l[i];
        edc[i] = acc;
    }
    const double total = edc[static_cast<size_t> (0.1 * kSr)];
    double t5 = -1, t35 = -1;
    for (size_t i = static_cast<size_t> (0.1 * kSr); i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (edc[i] / total, 1.0e-30));
        if (t5 < 0 && db <= -5) t5 = static_cast<double> (i) / kSr;
        if (t35 < 0 && db <= -35) { t35 = static_cast<double> (i) / kSr; break; }
    }
    m.rt60 = (t5 >= 0 && t35 >= 0) ? 2.0 * (t35 - t5) : -1.0;
    return m;
}

void row (const std::string& name, const Overrides& ov, double secs, double noteLen = 0.02)
{
    const Metrics a = measure (EngineKind::own, ov, secs, noteLen), b = measure (EngineKind::vital, ov, secs, noteLen);
    std::printf ("| %-34s | %6.1f | %6.1f | %+5.1f | %5.2f | %5.2f | %6.1f | %6.1f |\n", name.c_str(), a.peakDb, b.peakDb, a.peakDb - b.peakDb, a.rt60, b.rt60, a.tail1, b.tail1);
}

/** The flanger vectors F1 of docs/engine-spec/06-effects.md section 7 / 8.1 on both engines: a Sine burst (dry window, the copy at 1 / center = 3.03 ms, the second one at 6.06 ms) and the RMS of a held saw (note 45) against the dry saw. */
void flangerVectors (double kStart)
{
    const Overrides fl = { { "flanger.on", 1 }, { "flanger.mix", 100 }, { "flanger.depth", 0 }, { "flanger.center", 329.63f } };
    const auto peakIn = [] (const std::vector<float>& x, double t0, double t1)
    {
        float p = 0;
        for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < x.size(); ++i)
            p = std::max (p, std::fabs (x[i]));
        return static_cast<double> (p);
    };
    std::printf ("| flanger F1 | burst dry window own / vital | copy 3.03 ms own / vital | copy 6.06 ms own / vital | saw RMS vs dry own / vital |\n|---|---|---|---|---|\n");
    for (float feedback : { 0.0f, 50.0f, -50.0f })
    {
        double bd[2], c1[2], c2[2], sr[2];
        for (int k = 0; k < 2; ++k)
        {
            const EngineKind kind = k == 0 ? EngineKind::own : EngineKind::vital;
            Overrides ov = { { "osc1.wavetable", 0 }, { "osc1.level", 100 }, { "osc1.phase", 90 }, { "filter1.on", 0 }, { "master.volume", 0 }, { "env1.attack", 0 }, { "env1.decay", 5 }, { "env1.sustain", 0 }, { "env1.release", 5 } };
            ov.insert (ov.end(), fl.begin(), fl.end());
            ov.push_back ({ "flanger.feedback", feedback });
            Note n;
            n.start = kStart;
            n.length = 0.02;
            n.key = 69;
            const Stereo b = render (kind, ov, { n }, kStart + 0.3, kSr);
            bd[k] = peakIn (b.l, kStart, kStart + 0.0029);
            c1[k] = peakIn (b.l, kStart + 0.0029, kStart + 0.0036);
            c2[k] = peakIn (b.l, kStart + 0.006, kStart + 0.0066);
            Overrides saw = { { "osc1.wavetable", 1 }, { "osc1.level", 100 }, { "filter1.on", 0 }, { "master.volume", -6 }, { "osc1.unison.voices", 1 } };
            Overrides sawFx = saw;
            sawFx.insert (sawFx.end(), fl.begin(), fl.end());
            sawFx.push_back ({ "flanger.feedback", feedback });
            Note h;
            h.start = 0.0;
            h.length = 1.0;
            h.key = 45;
            const Stereo d = render (kind, saw, { h }, 0.8, kSr), w = render (kind, sawFx, { h }, 0.8, kSr);
            sr[k] = rms (w.l, static_cast<size_t> (0.3 * kSr), static_cast<size_t> (0.7 * kSr)) / rms (d.l, static_cast<size_t> (0.3 * kSr), static_cast<size_t> (0.7 * kSr));
        }
        std::printf ("| feedback %+.0f | %.3f / %.3f | %.3f / %.3f | %.3f / %.3f | %.3f / %.3f |\n", static_cast<double> (feedback), bd[0], bd[1], c1[0], c1[1], c2[0], c2[1], sr[0], sr[1]);
    }
}
/** Gain (dB) per harmonic k = 1 .. 63 of 110 Hz of a held saw (note 45) through an effect, against the dry saw, from the window 0.4 .. 0.9 s (55 whole cycles): the steady-state response of an unmodulated effect. */
std::vector<double> harmonicGains (EngineKind kind, const Overrides& fx)
{
    Overrides saw = { { "osc1.wavetable", 1 }, { "osc1.level", 100 }, { "filter1.on", 0 }, { "master.volume", -12 }, { "osc1.unison.voices", 1 } };
    Note h;
    h.start = 0.0;
    h.length = 1.0;
    h.key = 45;
    const auto spectrum = [&] (const Overrides& extra)
    {
        Overrides ov = saw;
        ov.insert (ov.end(), extra.begin(), extra.end());
        const Stereo r = render (kind, ov, { h }, 1.0, kSr);
        const size_t i0 = static_cast<size_t> (0.4 * kSr), n = static_cast<size_t> (0.5 * kSr);
        std::vector<double> mag;
        for (int k = 1; k <= 63; ++k)
        {
            double re = 0.0, im = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                const double ph = 2.0 * 3.14159265358979323846 * 110.0 * k * static_cast<double> (i) / kSr;
                re += r.l[i0 + i] * std::cos (ph);
                im += r.l[i0 + i] * std::sin (ph);
            }
            mag.push_back (std::sqrt (re * re + im * im) * 2.0 / static_cast<double> (n));
        }
        return mag;
    };
    const auto dry = spectrum ({}), wet = spectrum (fx);
    std::vector<double> g;
    for (size_t k = 0; k < dry.size(); ++k)
        g.push_back (dry[k] > dry[0] * 3.0e-4 ? 20.0 * std::log10 (std::max (wet[k], 1.0e-12) / dry[k]) : -999.0);
    return g;
}

/** One table row: the harmonic gain profile of the own engine against engine-vital (mean and largest difference over the harmonics where both are valid and above -40 dB). */
void harmonicRow (const std::string& name, const Overrides& fx)
{
    const auto a = harmonicGains (EngineKind::own, fx), b = harmonicGains (EngineKind::vital, fx);
    double sum = 0.0, worst = 0.0;
    int n = 0, worstK = 0;
    for (size_t k = 0; k < a.size(); ++k)
        if (a[k] > -40.0 && b[k] > -40.0)
        {
            const double d = std::fabs (a[k] - b[k]);
            sum += d;
            ++n;
            if (d > worst)
            {
                worst = d;
                worstK = static_cast<int> (k) + 1;
            }
        }
    std::printf ("| %-44s | %3d | %5.2f | %5.2f (k=%d) |\n", name.c_str(), n, n > 0 ? sum / n : 0.0, worst, worstK);
}

/** The chorus vectors CH1 on both engines: the first 0.1 ms step in which a Sine burst above 0.05 appears after the note on (the delay time), then the harmonic profile of a held saw. */
void chorusVectors()
{
    const auto peakIn = [] (const std::vector<float>& x, double t0, double t1)
    {
        float p = 0;
        for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < x.size(); ++i)
            p = std::max (p, std::fabs (x[i]));
        return static_cast<double> (p);
    };
    std::printf ("| chorus CH1 (Sine burst, depth 0, feedback 0, voices 1, mix 100) | onset own (ms) | onset vital (ms) |\n|---|---|---|\n");
    for (float ms : { 5.0f, 12.0f })
    {
        double onset[2] = { -1, -1 };
        for (int k = 0; k < 2; ++k)
        {
            Overrides ov = { { "osc1.wavetable", 0 }, { "osc1.level", 100 }, { "osc1.phase", 90 }, { "filter1.on", 0 }, { "master.volume", 0 }, { "env1.attack", 0 }, { "env1.decay", 5 }, { "env1.sustain", 0 }, { "env1.release", 5 },
                             { "chorus.on", 1 }, { "chorus.mix", 100 }, { "chorus.depth", 0 }, { "chorus.feedback", 0 }, { "chorus.voices", 1 }, { "chorus.delay1", ms }, { "chorus.delay2", ms } };
            Note n;
            n.start = 0.1;
            n.length = 0.02;
            n.key = 69;
            const Stereo b = render (k == 0 ? EngineKind::own : EngineKind::vital, ov, { n }, 0.4, kSr);
            for (double t = 0.0; t < 0.03; t += 0.0001)
                if (peakIn (b.l, 0.1 + t, 0.1 + t + 0.0001) > 0.05)
                {
                    onset[k] = t * 1000.0;
                    break;
                }
        }
        std::printf ("| delay1 = delay2 = %.0f ms | %.2f | %.2f |\n", static_cast<double> (ms), onset[0], onset[1]);
    }
    std::printf ("\n| held saw, harmonic gain profile | harmonics | mean diff dB | largest diff dB |\n|---|---|---|---|\n");
    harmonicRow ("chorus 1 voice 5 ms, fb 0, mix 100", { { "chorus.on", 1 }, { "chorus.mix", 100 }, { "chorus.depth", 0 }, { "chorus.feedback", 0 }, { "chorus.voices", 1 }, { "chorus.delay1", 5 }, { "chorus.delay2", 5 }, { "chorus.sync", 0 } });
    harmonicRow ("chorus 2 voices 5 / 9 ms, fb 0, mix 100", { { "chorus.on", 1 }, { "chorus.mix", 100 }, { "chorus.depth", 0 }, { "chorus.feedback", 0 }, { "chorus.voices", 2 }, { "chorus.delay1", 5 }, { "chorus.delay2", 9 }, { "chorus.sync", 0 } });
    harmonicRow ("chorus 2 voices 5 / 9 ms, fb 40, mix 100", { { "chorus.on", 1 }, { "chorus.mix", 100 }, { "chorus.depth", 0 }, { "chorus.feedback", 40 }, { "chorus.voices", 2 }, { "chorus.delay1", 5 }, { "chorus.delay2", 9 }, { "chorus.sync", 0 } });
    harmonicRow ("chorus 4 voices 3 / 14 ms, fb 0, mix 50", { { "chorus.on", 1 }, { "chorus.mix", 50 }, { "chorus.depth", 0 }, { "chorus.feedback", 0 }, { "chorus.voices", 4 }, { "chorus.delay1", 3 }, { "chorus.delay2", 14 }, { "chorus.sync", 0 } });
}

/** The notch frequencies (Hz, 5-bin smoothed noise spectrum against the same patch without the effect, minima below -12 dB, neighbours within 4 % merged) of the phaser at centre 1 kHz, depth 0, feedback 0, mix 100: white noise, both engines. */
std::vector<double> phaserNotches (EngineKind kind, float blend, float depth2)
{
    const Overrides base = { { "osc1.on", 0 }, { "noise.on", 1 }, { "noise.level", 50 }, { "filter1.on", 0 }, { "master.volume", -6 }, { "env1.attack", 0 }, { "env1.release", 50 } };
    Overrides ov = base;
    for (auto& kv : Overrides { { "phaser.on", 1 }, { "phaser.mix", 100 }, { "phaser.depth", 0 }, { "phaser.center", 1000 }, { "phaser.feedback", 0 }, { "phaser.blend", blend }, { "phaser.rate", 0.04f }, { "phaser.depth2", depth2 } })
        ov.push_back (kv);
    Note n;
    n.length = 3.5;
    const Stereo a = render (kind, ov, { n }, 3.6, kSr), b = render (kind, base, { n }, 3.6, kSr);
    const size_t N = 8192;
    std::vector<double> pa (N / 2, 0.0), pb (N / 2, 0.0);
    for (size_t pos = static_cast<size_t> (0.3 * kSr); pos + N <= a.l.size(); pos += N / 2)
    {
        const auto x = powerSpectrum (a.l, pos, N), y = powerSpectrum (b.l, pos, N);
        for (size_t i = 0; i < N / 2; ++i)
        {
            pa[i] += x[i];
            pb[i] += y[i];
        }
    }
    std::vector<double> g (N / 2, 0.0);
    for (size_t i = 3; i + 3 < N / 2; ++i)
    {
        double u = 0, v = 0;
        for (size_t k = i - 2; k <= i + 2; ++k)
        {
            u += pa[k];
            v += pb[k];
        }
        g[i] = 10.0 * std::log10 (u / v);
    }
    const double binHz = kSr / static_cast<double> (N);
    std::vector<std::pair<double, double>> merged;
    for (size_t i = 4; i + 4 < N / 2; ++i)
        if (g[i] < -12.0 && g[i] <= g[i - 1] && g[i] <= g[i + 1] && i * binHz >= 60 && i * binHz <= 12000)
        {
            const std::pair<double, double> f { i * binHz, g[i] };
            if (! merged.empty() && f.first < merged.back().first * 1.04)
            {
                if (f.second < merged.back().second)
                    merged.back() = f;
            }
            else
                merged.push_back (f);
        }
    std::vector<double> out;
    for (const auto& m : merged)
        out.push_back (m.first);
    return out;
}

/** The notch table (P1 / P2): own engine against engine-vital. */
void phaserNotchTable()
{
    std::printf ("| blend / depth2 | notches own (Hz) | notches vital (Hz) |\n|---|---|---|\n");
    for (auto [blend, depth2] : { std::pair<float, float> { 0, 0 }, { 50, 0 }, { 100, 0 }, { 100, 12 }, { 100, -12 }, { 100, 24 } })
    {
        std::printf ("| %.0f / %+.0f |", static_cast<double> (blend), static_cast<double> (depth2));
        for (EngineKind kind : { EngineKind::own, EngineKind::vital })
        {
            for (double hz : phaserNotches (kind, blend, depth2))
                std::printf (" %.0f", hz);
            std::printf (" |");
        }
        std::printf ("\n");
    }
}

/** The phaser notch frequencies (P1): the harmonic gain profile of a held saw, own engine against engine-vital, for 4, 8 and 12 stages. */
void phaserVectors()
{
    std::printf ("| held saw, harmonic gain profile | harmonics | mean diff dB | largest diff dB |\n|---|---|---|---|\n");
    for (float blend : { 0.0f, 50.0f, 100.0f })
        for (float center : { 600.0f, 1000.0f })
        {
            char label[96];
            std::snprintf (label, sizeof label, "phaser blend %.0f, center %.0f Hz, fb 0, depth 0, mix 100", static_cast<double> (blend), static_cast<double> (center));
            harmonicRow (label, { { "phaser.on", 1 }, { "phaser.sync", 0 }, { "phaser.mix", 100 }, { "phaser.depth", 0 }, { "phaser.feedback", 0 }, { "phaser.blend", blend }, { "phaser.center", center } });
        }
    harmonicRow ("phaser blend 100, center 1000 Hz, fb 60", { { "phaser.on", 1 }, { "phaser.sync", 0 }, { "phaser.mix", 100 }, { "phaser.depth", 0 }, { "phaser.feedback", 60 }, { "phaser.blend", 100 }, { "phaser.center", 1000 } });
}
} // namespace

int main (int argc, char** argv)
{
    const std::string what = argc > 1 ? argv[1] : "all";
    if (what == "flanger")
    {
        flangerVectors (argc > 2 ? std::atof (argv[2]) : 0.1);
        return 0;
    }
    if (what == "chorus")
    {
        chorusVectors();
        return 0;
    }
    if (what == "chorusdump")
    {
        const Overrides fx = { { "chorus.on", 1 }, { "chorus.mix", 100 }, { "chorus.depth", 0 }, { "chorus.feedback", 0 }, { "chorus.voices", 1 }, { "chorus.delay1", 5 }, { "chorus.delay2", 5 }, { "chorus.sync", 0 } };
        const auto a = harmonicGains (EngineKind::own, fx), b = harmonicGains (EngineKind::vital, fx);
        for (size_t k = 0; k < 12; ++k)
            std::printf ("%7.0f Hz  own %7.2f  vital %7.2f\n", 110.0 * (k + 1), a[k], b[k]);
        return 0;
    }
    if (what == "phasernotches")
    {
        phaserNotchTable();
        return 0;
    }
    if (what == "phaser")
    {
        phaserVectors();
        return 0;
    }
    if (argc > 3)
        gLevel = static_cast<float> (std::atof (argv[3]));
    const float mix = argc > 2 ? static_cast<float> (std::atof (argv[2])) : 100.0f;
    std::printf ("| setting | peak own (dB) | peak vital (dB) | diff | RT60 own (s) | RT60 vital (s) | tail 0.5 s own (dB) | tail 0.5 s vital (dB) |\n|---|---|---|---|---|---|---|---|\n");
    row ("dry burst (no effect)", burst(), 2.0);
    if (what == "reverb" || what == "all")
    {
        static const char* names[6] = { "Vital", "Plate", "Hall", "Vintage", "Nitrous", "Basin" };
        for (int type = 0; type < 6; ++type)
            for (auto [decay, size] : { std::pair<int, int> { 500, 100 }, { 500, 50 }, { 2000, 50 } })
            {
                Overrides ov = burst();
                for (auto& kv : Overrides { { "reverb.on", 1 }, { "reverb.mix", mix }, { "reverb.decay", (float) decay }, { "reverb.size", (float) size }, { "reverb.type", (float) type },
                                            { "reverb.high_shelf_gain", 0 }, { "reverb.low_shelf_gain", 0 }, { "reverb.pre_high_cut", 12000 }, { "reverb.pre_low_cut", 20 }, { "reverb.chorus_amount", 0 } })
                    ov.push_back (kv);
                char label[64];
                std::snprintf (label, sizeof label, "reverb %s, decay %d, size %d", names[type], decay, size);
                row (label, ov, decay > 1000 ? 9.0 : 5.0, 0.02);
            }
    }
    if (what == "modules" || what == "all")
    {
        struct M { const char* name; Overrides set; };
        const std::vector<M> mods = {
            { "chorus (defaults)", { { "chorus.on", 1 } } },
            { "delay (250 ms, fb 50, mix 50)", { { "delay.on", 1 }, { "delay.sync", 0 }, { "delay.time", 250 }, { "delay.feedback", 50 }, { "delay.mix", 50 } } },
            { "flanger (defaults)", { { "flanger.on", 1 } } },
            { "phaser (defaults)", { { "phaser.on", 1 } } },
            { "phaser feedback 0", { { "phaser.on", 1 }, { "phaser.feedback", 0 } } },
            { "phaser mix 100, feedback 50", { { "phaser.on", 1 }, { "phaser.mix", 100 } } },
            { "phaser mix 100, feedback 0, depth 0", { { "phaser.on", 1 }, { "phaser.mix", 100 }, { "phaser.feedback", 0 }, { "phaser.depth", 0 } } },
            { "phaser mix 100, feedback 50, depth 0", { { "phaser.on", 1 }, { "phaser.mix", 100 }, { "phaser.depth", 0 } } },
            { "phaser mix 100, feedback 0", { { "phaser.on", 1 }, { "phaser.mix", 100 }, { "phaser.feedback", 0 } } },
            { "distortion tube, drive 12 dB", { { "distortion.on", 1 }, { "distortion.type", 6 }, { "distortion.drive", 12 } } },
            { "distortion fold, drive 12 dB", { { "distortion.on", 1 }, { "distortion.type", 2 }, { "distortion.drive", 12 } } },
            { "eq mid +12 dB at 1 kHz", { { "eq.on", 1 }, { "eq.mid.gain", 12 } } },
            { "compressor (defaults)", { { "compressor.on", 1 } } },
            { "compressor single, -20 dB, 100 %", { { "compressor.on", 1 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", -20 }, { "compressor.mid.upper_ratio", 100 } } },
            { "hyper / dimension (defaults)", { { "hyperdim.on", 1 } } },
            { "utility width 150", { { "utility.on", 1 }, { "utility.width", 150 } } },
            { "bode shift 20 %", { { "bode.on", 1 }, { "bode.shift", 20 }, { "bode.mix", 100 } } },
        };
        for (const M& m : mods)
        {
            Overrides ov = burst();
            ov.insert (ov.end(), m.set.begin(), m.set.end());
            row (m.name, ov, 3.0, 0.5);
        }
    }
    return 0;
}
