// SynthyBoy - osc-oracle: the sample-type oscillators, user tables, Odd/Even, Filter and morph warps through engine-own and engine-vital (black box, IEngine):
// level, harmonic levels and the share of energy outside the harmonics (aliasing / resynthesis noise). Prints a table.
// usage: synthyboy_osc_oracle [note]
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "own_oracle.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/user_content.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;

struct Metrics
{
    double rmsDb = -200.0, peak = 0.0, outsideDb = -200.0, h[4] = { -200, -200, -200, -200 };
};

void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> wl = std::polar (1.0, -6.283185307179586 / static_cast<double> (len));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

Metrics measure (const std::vector<float>& x, double f0)
{
    Metrics m;
    const size_t n = 32768, from = static_cast<size_t> (0.25 * kSr);
    if (x.size() < from + n)
        return m;
    std::vector<std::complex<double>> a (n);
    double e = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (6.283185307179586 * static_cast<double> (i) / static_cast<double> (n));
        a[i] = w * static_cast<double>(x[from + i]);
        e += static_cast<double> (x[from + i]) * x[from + i];
        m.peak = std::max (m.peak, static_cast<double> (std::fabs (x[from + i])));
    }
    m.rmsDb = 10.0 * std::log10 (std::max (e / static_cast<double> (n), 1.0e-20));
    fft (a);
    const double binHz = kSr / static_cast<double> (n);
    double total = 0.0, outside = 0.0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double p = std::norm (a[k]);
        total += p;
        const double f = static_cast<double> (k) * binHz;
        const double nearest = std::round (f / f0);
        const bool harmonic = nearest >= 1.0 && std::fabs (f - nearest * f0) < 6.0 * binHz;
        if (!harmonic)
            outside += p;
    }
    m.outsideDb = 10.0 * std::log10 (std::max (outside / std::max (total, 1.0e-30), 1.0e-20));
    for (int h = 1; h <= 4; ++h)
    {
        double best = 0.0;
        for (size_t k = 1; k < n / 2; ++k)
            if (std::fabs (static_cast<double> (k) * binHz - h * f0) < 0.1 * f0)
                best = std::max (best, std::abs (a[k]));
        m.h[h - 1] = 20.0 * std::log10 (std::max (2.0 * best / (0.5 * static_cast<double> (n)), 1.0e-10));
    }
    return m;
}

struct Content
{
    MemoryContentStore store;
    StateContent state;
};

std::vector<uint8_t> sineWav (double hz, double amp, int rate, double seconds)
{
    const size_t n = static_cast<size_t> (rate * seconds);
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float> (amp * std::sin (6.283185307179586 * hz * static_cast<double> (i) / rate));
    return encodeWav (x.data(), nullptr, n, rate, 16);
}

oracle::Stereo run (EngineKind kind, const oracle::Overrides& ov, Content& c, int note)
{
    auto e = createEngine (kind);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, Quality {});
    oracle::applyOverrides (*e, { { "filter1.on", 0.0f }, { "master.volume", 0.0f }, { "env1.attack", 0.0f }, { "osc1.level", 100.0f }, { "osc1.random_phase", 0.0f } });
    oracle::applyOverrides (*e, ov);
    e->setContentStore (&c.store);
    e->setContent (c.state);
    e->reset();
    oracle::Stereo out;
    const size_t total = static_cast<size_t> (1.5 * kSr);
    out.l.assign (total, 0.0f);
    out.r.assign (total, 0.0f);
    size_t pos = 0;
    while (pos < total)
    {
        const size_t n = std::min<size_t> (256, total - pos);
        Event ev[1];
        int ne = 0;
        if (pos == 0)
        {
            ev[0].type = Event::NoteOn;
            ev[0].frame = 0;
            ev[0].key = static_cast<int16_t> (note);
            ev[0].value = 1.0f;
            ne = 1;
        }
        e->process (ev, ne, out.l.data() + pos, out.r.data() + pos, static_cast<int>(n));
        pos += n;
    }
    return out;
}
} // namespace

int main (int argc, char** argv)
{
    const int noteArg = argc > 1 ? std::atoi (argv[1]) : 57;
    Content c;
    c.store.add ("o", "sine.wav", sineWav (440.0, 0.8, 44100, 2.0));
    c.store.add ("o", "inst/p.wav", sineWav (440.0, 0.8, 44100, 2.0));
    const std::string sfz = "<region> sample=p.wav lokey=0 hikey=127 pitch_keycenter=69 loop_mode=loop_continuous\n";
    c.store.add ("o", "inst/p.sfz", std::vector<uint8_t> (sfz.begin(), sfz.end()));
    {
        ContentFileRef r;
        r.pack = "o";
        r.file = "sine.wav";
        attachSample (c.state, 1, r, 44100, 88200, 1);
        ContentFileRef i;
        i.pack = "o";
        i.file = "inst/p.sfz";
        attachInstrument (c.state, 1, i, 1);
    }
    struct Case
    {
        std::string name;
        oracle::Overrides ov;
        int note;
    };
    std::vector<Case> cases;
    const std::vector<std::pair<std::string, float>> types = { { "Sample", 1 }, { "Granular", 2 }, { "Spectral", 3 }, { "Multisample", 4 } };
    for (int note : { 69, 93 })
        for (const auto& t : types)
            cases.push_back ({ t.first + " note " + std::to_string (note), { { "osc1.type", t.second }, { "osc1.sample.root", 69 }, { "osc1.sample.loop_mode", 1 } }, note });
    cases.push_back ({ "Sample unison 7", { { "osc1.type", 1 }, { "osc1.sample.root", 69 }, { "osc1.sample.loop_mode", 1 }, { "osc1.unison.voices", 7 }, { "osc1.unison.detune", 40 } }, 69 });
    for (int w : { 0, 3, 2 })
        cases.push_back ({ "table " + std::to_string (w) + " (sine, triangle, square) note " + std::to_string (noteArg), { { "osc1.wavetable", static_cast<float> (w) } }, noteArg });
    for (int note : { noteArg, 93 })
    {
        cases.push_back ({ "Saw no warp note " + std::to_string (note), { { "osc1.wavetable", 1 } }, note });
        cases.push_back ({ "Saw smooth interp note " + std::to_string (note), { { "osc1.wavetable", 1 }, { "osc1.smooth_interp", 1 } }, note });
    }
    const std::vector<std::pair<std::string, float>> warps = { { "Odd/Even 25", 24 }, { "Odd/Even 75", 24 }, { "Filter LPF 25", 25 }, { "Filter LPF 75", 25 }, { "Filter HPF 25", 26 }, { "Remap 3 50", 23 } };
    const float amounts[6] = { 25, 75, 25, 75, 25, 50 };
    for (size_t i = 0; i < warps.size(); ++i)
        for (int note : { noteArg, 93 })
            cases.push_back ({ warps[i].first + " note " + std::to_string (note), { { "osc1.wavetable", 1 }, { "osc1.warp.type", warps[i].second }, { "osc1.warp.amount", amounts[i] } }, note });
    const char* morphNames[] = { "None", "Vocode", "Formant Scale", "Harmonic Stretch", "Inharmonic Stretch", "Smear", "Random Amplitudes", "Low Pass", "High Pass", "Phase Disperse", "Shepard Tone", "Time Skew" };
    for (int m = 1; m < 12; ++m)
        for (int amount : { 25, 75 })
            cases.push_back ({ std::string ("Morph ") + morphNames[m] + " " + std::to_string (amount), { { "osc1.wavetable", 9 }, { "osc1.frame", 50 }, { "osc1.morph.type", static_cast<float> (m) }, { "osc1.morph.amount", static_cast<float> (amount) } }, noteArg });
    std::printf ("%-34s | %8s %8s %6s | %8s %8s %6s | %7s\n", "case (note 57 unless stated)", "own dB", "own out", "peak", "vital dB", "vital out", "peak", "d level");
    for (const auto& cs : cases)
    {
        const int note = cs.note;
        const double f0 = 8.1757989156 * std::pow (2.0, note / 12.0);
        const Metrics own = measure (run (EngineKind::own, cs.ov, c, note).l, f0);
        const Metrics vit = measure (run (EngineKind::vital, cs.ov, c, note).l, f0);
        std::printf ("%-34s | %8.2f %8.1f %6.3f | %8.2f %8.1f %6.3f | %+7.2f\n", cs.name.c_str(), own.rmsDb, own.outsideDb, own.peak, vit.rmsDb, vit.outsideDb, vit.peak, own.rmsDb - vit.rmsDb);
    }
    return 0;
}
