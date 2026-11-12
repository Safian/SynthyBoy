// SynthyBoy - own-oracle: command line probe. Renders the same patch through engine-own and engine-vital (black box, public IEngine) and prints metrics.
// usage: synthyboy_own_oracle <report> [--note N] [--vel V] [--secs S] [--len L] [--only own|vital] [key=value ...]
//   reports: summary | peaks | env | alias | spectrum
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "own_oracle.h"

#include "synthyboy/modulation.h"

using namespace oracle;
using synthyboy::EngineKind;

namespace
{
/** A value on the command line: a number, "src:<modulation source key>" or "dst:<parameter key>" (the choice value of the matrix lists). */
float parseValue (const std::string& v)
{
    if (v.rfind ("src:", 0) == 0)
    {
        for (size_t i = 0; i < synthyboy::modSourceCount(); ++i)
            if (v.substr (4) == synthyboy::modSourceTable()[i].key)
                return static_cast<float> (i + 1);
        std::fprintf (stderr, "unknown source %s\n", v.c_str());
        return 0.0f;
    }
    if (v.rfind ("dst:", 0) == 0)
    {
        const int idx = synthyboy::findParamIndex (synthyboy::findParamByKey (v.substr (4).c_str()) ? synthyboy::findParamByKey (v.substr (4).c_str())->id : 0);
        if (idx < 0)
            std::fprintf (stderr, "unknown destination %s\n", v.c_str());
        return idx < 0 ? 0.0f : static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (idx)));
    }
    return static_cast<float> (std::atof (v.c_str()));
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: %s summary|peaks|env|alias|spectrum [--note N] [--vel V] [--secs S] [--len L] [--only own|vital] key=value...\n", argv[0]);
        return 2;
    }
    const std::string report = argv[1];
    int note = 69;
    float vel = 1.0f;
    double secs = 1.5, len = 1.0, step = 0.05;
    std::string only;
    Overrides ov;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--note" && i + 1 < argc) note = std::atoi (argv[++i]);
        else if (a == "--vel" && i + 1 < argc) vel = static_cast<float> (std::atof (argv[++i]));
        else if (a == "--secs" && i + 1 < argc) secs = std::atof (argv[++i]);
        else if (a == "--len" && i + 1 < argc) len = std::atof (argv[++i]);
        else if (a == "--step" && i + 1 < argc) step = std::atof (argv[++i]);
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a.find ('=') != std::string::npos)
            ov.push_back ({ a.substr (0, a.find ('=')), parseValue (a.substr (a.find ('=') + 1)) });
    }
    const double sr = 48000.0;
    for (const auto kind : { EngineKind::vital, EngineKind::own })
    {
        const char* name = synthyboy::engineName (kind);
        if (!only.empty() && only != name)
            continue;
        if (!synthyboy::isEngineAvailable (kind))
            continue;
        Note n;
        n.key = note;
        n.vel = vel;
        n.length = len;
        const Stereo s = render (kind, ov, { n }, secs, sr);
        const size_t a = static_cast<size_t> (0.3 * sr), b = static_cast<size_t> (len * sr);
        if (report == "summary")
            std::printf ("%-5s rmsL %.2f dB rmsR %.2f dB peak %.2f dB\n", name, toDb (rms (s.l, a, b)), toDb (rms (s.r, a, b)), toDb (peak (s.l, 0, s.l.size())));
        else if (report == "peaks" || report == "spectrum")
        {
            const size_t N = 32768;
            const auto ps = powerSpectrum (s.l, std::min (a, s.l.size() - N), N);
            std::printf ("%-5s rms %.2f dB:", name, toDb (rms (s.l, a, b)));
            for (const auto& p : topPeaks (ps, sr, N, report == "peaks" ? 8 : 24))
                std::printf (" %.2fHz/%.1f", p.hz, p.db);
            std::printf ("\n");
        }
        else if (report == "alias")
        {
            const size_t N = 32768;
            const auto ps = powerSpectrum (s.l, std::min (a, s.l.size() - N), N);
            const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            std::printf ("%-5s rms %.2f dB inharmonic %.1f dB\n", name, toDb (rms (s.l, a, b)), inharmonicDb (ps, sr, N, f0));
        }
        else if (report == "series")
        {
            // RMS in dB (absolute) every `step` seconds over a 5 ms window
            std::printf ("%-5s", name);
            for (double t = 0.0; t < secs; t += step)
                std::printf (" %.0f", toDb (rms (s.l, static_cast<size_t> (t * sr), static_cast<size_t> ((t + 0.005) * sr))));
            std::printf ("\n");
        }
        else if (report == "env")
        {
            // time (ms) at which the 1 ms RMS envelope first reaches 10 / 50 / 90 % of its maximum, and time after the note off to fall below -20 / -40 / -60 dB of it
            const size_t w = static_cast<size_t> (0.001 * sr);
            std::vector<double> e;
            for (size_t i = 0; i + w < s.l.size(); i += w / 2)
                e.push_back (rms (s.l, i, i + w));
            double mx = 0;
            size_t imax = 0;
            for (size_t i = 0; i < e.size(); ++i)
                if (e[i] > mx) { mx = e[i]; imax = i; }
            auto first = [&] (double f) { for (size_t i = 0; i < e.size(); ++i) if (e[i] >= f * mx) return 0.5 * static_cast<double> (i); return -1.0; };
            const size_t off = static_cast<size_t> (len * 2000.0);
            auto after = [&] (double db) { for (size_t i = std::max (off, imax); i < e.size(); ++i) if (e[i] <= mx * std::pow (10.0, db / 20.0)) return 0.5 * static_cast<double> (i) - len * 1000.0; return -1.0; };
            std::printf ("%-5s max %.2f dB  rise10 %.1f 50 %.1f 90 %.1f ms  fall(after off) -20dB %.1f -40dB %.1f -60dB %.1f ms\n", name, toDb (mx),
                         first (0.1), first (0.5), first (0.9), after (-20), after (-40), after (-60));
        }
    }
    return 0;
}
