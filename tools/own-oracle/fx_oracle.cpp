// SynthyBoy - fx-oracle: the effects of engine-own against engine-vital through IEngine (black box): level, tail and RT60 of a burst, one table row per effect setting.
// usage: synthyboy_fx_oracle [reverb|modules|all]   (a markdown table on stdout)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>

#include "own_oracle.h"

using namespace oracle;
using synthyboy::EngineKind;

namespace
{
constexpr double kSr = 48000.0;

Overrides burst()
{
    return { { "osc1.wavetable", 1 }, { "osc1.level", 100 }, { "filter1.on", 0 }, { "master.volume", 0 }, { "env1.attack", 0 }, { "env1.decay", 20 }, { "env1.sustain", 0 }, { "env1.release", 5 } };
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
} // namespace

int main (int argc, char** argv)
{
    const std::string what = argc > 1 ? argv[1] : "all";
    std::printf ("| setting | peak own (dB) | peak vital (dB) | diff | RT60 own (s) | RT60 vital (s) | tail 0.5 s own (dB) | tail 0.5 s vital (dB) |\n|---|---|---|---|---|---|---|---|\n");
    if (what == "reverb" || what == "all")
    {
        static const char* names[6] = { "Vital", "Plate", "Hall", "Vintage", "Nitrous", "Basin" };
        for (int type = 0; type < 6; ++type)
            for (auto [decay, size] : { std::pair<int, int> { 500, 100 }, { 500, 50 }, { 2000, 50 } })
            {
                Overrides ov = burst();
                for (auto& kv : Overrides { { "reverb.on", 1 }, { "reverb.mix", 100 }, { "reverb.decay", (float) decay }, { "reverb.size", (float) size }, { "reverb.type", (float) type },
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
