// SynthyBoy - own-oracle: render and measurement helpers over IEngine (shared by the tool and the oracle tests).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/params.h"

namespace oracle
{
using Overrides = std::vector<std::pair<std::string, float>>;

struct Note
{
    int key = 69;
    float vel = 1.0f;
    double start = 0.0, length = 1.0; // seconds
    int channel = 0;
};

struct Stereo
{
    std::vector<float> l, r;
};

inline void applyOverrides (synthyboy::IEngine& e, const Overrides& ov)
{
    for (const auto& kv : ov)
    {
        const auto* p = synthyboy::findParamByKey (kv.first.c_str());
        if (p == nullptr)
            std::fprintf (stderr, "unknown key %s\n", kv.first.c_str());
        else
            e.setParam (p->id, kv.second);
    }
}

/** Renders the notes (events at the right sample) for `seconds` with `ov` applied on a fresh engine. */
inline Stereo render (synthyboy::EngineKind kind, const Overrides& ov, const std::vector<Note>& notes, double seconds, double sr = 48000.0)
{
    auto e = synthyboy::createEngine (kind);
    Stereo out;
    if (!e)
        return out;
    e->setParam (synthyboy::kParamMasterPolyphony, 32.0f);
    e->prepare (sr, 256, synthyboy::Quality {});
    applyOverrides (*e, ov);
    e->reset();
    const size_t total = static_cast<size_t> (seconds * sr);
    out.l.assign (total, 0.0f);
    out.r.assign (total, 0.0f);
    struct Ev { size_t frame; synthyboy::Event ev; };
    std::vector<Ev> evs;
    for (const auto& n : notes)
    {
        synthyboy::Event on;
        on.type = synthyboy::Event::NoteOn;
        on.key = static_cast<int16_t> (n.key);
        on.value = n.vel;
        on.channel = static_cast<int16_t> (n.channel);
        evs.push_back ({ static_cast<size_t> (n.start * sr), on });
        synthyboy::Event off = on;
        off.type = synthyboy::Event::NoteOff;
        off.value = 0.0f;
        evs.push_back ({ static_cast<size_t> ((n.start + n.length) * sr), off });
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.frame < b.frame; });
    constexpr size_t kBlock = 256;
    size_t pos = 0, ei = 0;
    std::vector<synthyboy::Event> block;
    while (pos < total)
    {
        const size_t n = std::min (kBlock, total - pos);
        block.clear();
        while (ei < evs.size() && evs[ei].frame < pos + n)
        {
            synthyboy::Event ev = evs[ei].ev;
            ev.frame = static_cast<int32_t> (evs[ei].frame - pos);
            block.push_back (ev);
            ++ei;
        }
        e->process (block.data(), static_cast<int> (block.size()), out.l.data() + pos, out.r.data() + pos, static_cast<int> (n));
        pos += n;
    }
    return out;
}

inline double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0;
    b = std::min (b, x.size());
    for (size_t i = a; i < b; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return b > a ? std::sqrt (s / static_cast<double> (b - a)) : 0.0;
}

inline double peak (const std::vector<float>& x, size_t a, size_t b)
{
    double p = 0;
    b = std::min (b, x.size());
    for (size_t i = a; i < b; ++i)
        p = std::max (p, static_cast<double> (std::fabs (x[i])));
    return p;
}

inline double toDb (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

/** Power spectrum (Hann window, size n power of two) of x starting at `a`. Returns n/2 bins of magnitude squared. */
inline std::vector<double> powerSpectrum (const std::vector<float>& x, size_t a, size_t n)
{
    std::vector<std::complex<double>> v (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * static_cast<double> (i) / static_cast<double> (n));
        v[i] = (a + i < x.size() ? x[a + i] : 0.0f) * w;
    }
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (v[i], v[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = v[i + k], t = v[i + k + len / 2] * w;
                v[i + k] = u + t;
                v[i + k + len / 2] = u - t;
                w *= wl;
            }
        }
    }
    std::vector<double> p (n / 2);
    for (size_t i = 0; i < n / 2; ++i)
        p[i] = std::norm (v[i]) / (static_cast<double> (n) * static_cast<double> (n));
    return p;
}

struct Peak
{
    double hz, db;
};

inline std::vector<Peak> topPeaks (const std::vector<double>& ps, double sr, size_t n, int count, double minSepHz = 3.0)
{
    std::vector<Peak> all;
    for (size_t i = 2; i + 2 < ps.size(); ++i)
        if (ps[i] > ps[i - 1] && ps[i] >= ps[i + 1] && ps[i] > ps[i - 2] && ps[i] >= ps[i + 2])
        {
            // parabolic interpolation on the log spectrum
            const double a = std::log (ps[i - 1] + 1e-30), b = std::log (ps[i] + 1e-30), c = std::log (ps[i + 1] + 1e-30);
            const double d = 0.5 * (a - c) / (a - 2 * b + c);
            all.push_back ({ (static_cast<double> (i) + d) * sr / static_cast<double> (n), 10.0 * std::log10 (ps[i] + 1e-30) });
        }
    std::sort (all.begin(), all.end(), [] (const Peak& x, const Peak& y) { return x.db > y.db; });
    std::vector<Peak> out;
    for (const auto& p : all)
    {
        bool near = false;
        for (const auto& q : out)
            near = near || std::fabs (q.hz - p.hz) < minSepHz;
        if (!near)
            out.push_back (p);
        if (static_cast<int> (out.size()) >= count)
            break;
    }
    return out;
}

/** Power outside the harmonics of f0 (within +-`tol` of the spacing) relative to the power at the harmonics, in dB (aliasing and noise floor). */
inline double inharmonicDb (const std::vector<double>& ps, double sr, size_t n, double f0, double tol = 0.12)
{
    double harm = 0, other = 0;
    const double binHz = sr / static_cast<double>(n);
    for (size_t i = 4; i < ps.size(); ++i)
    {
        const double h = static_cast<double> (i) * binHz / f0;
        const double frac = std::fabs (h - std::round (h));
        (frac < tol ? harm : other) += ps[i];
    }
    return 10.0 * std::log10 ((other + 1e-30) / (harm + 1e-30));
}
} // namespace oracle
