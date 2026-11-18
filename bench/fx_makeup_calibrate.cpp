// SynthyBoy - bench: the power gain of one effect module on pink noise, for the calibration of the tier level makeup (docs/release/TIER-LEVEL-COMPENSATION.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_fx_makeup_calibrate < JOBS.txt
//
// One job per line: MODULE [mono|stereo] [chord] [key=value]...   MODULE is the name of the effect (chorus compressor delay distortion eq filter flanger phaser reverb hyperdim utility bode;
// convolve needs content and is not run here). The keys are the plain values of the engine's parameters (e.g. "reverb.mix=100 reverb.decay=2000"); everything else is the default.
// The module runs on 10 s of the calibration program (noise with the power per Hz falling with f^-1.5 from 30 Hz to 12 kHz, 0.1 RMS, the same for every job; `stereo` (default): the
// channels share 70 % of their power, `indep`: independent channels, `mono`: the same in both; `chord`: a triad of sawtooth waves instead) in control periods; the output is
// the power gain of the last 6 s in dB, and the correlation of the output with the input (the mean of both channels), one line per job:
//   MODULE GAIN_DB CORRELATION
// The figure of an effect as the engine's makeup models it (core/engine-own/fx/fx_makeup.h) is printed too, "MODEL_DB", when the models are compiled in.

#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../core/fx-serum-b/fft.h"
#include "fx/fx_module.h"
#include "synthyboy/params.h"

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
const char* const kNames[13] = { "chorus", "compressor", "delay", "distortion", "eq", "filter", "flanger", "phaser", "reverb", "hyperdim", "utility", "bode", "convolve" };

struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    float white() noexcept
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return static_cast<float> (static_cast<double> (s >> 11) / 9007199254740992.0 * 2.0 - 1.0);
    }
};

/** The calibration program: noise whose power per Hz falls with f^-1.5 from 30 Hz to 12 kHz (the program of core/engine-own/fx/fx_makeup.h), made by shaping the spectrum of white
    noise with an FFT, 2^19 samples long and run as a loop. Two of them (`left`, `right`) mixed to the channel correlation of the job. */
struct Program
{
    std::vector<float> x;
    explicit Program (uint64_t seed)
    {
        constexpr int n = 1 << 19;
        constexpr double sr = 44100.0;
        Rng rng;
        rng.s ^= seed;
        fx::Radix2FFT fft (n);
        std::vector<fx::Complex> spec (static_cast<size_t> (n));
        for (int k = 0; k < n; ++k)
        {
            const double f = k <= n / 2 ? k * sr / n : (n - k) * sr / n;
            const double amp = (f >= 30.0 && f <= 12000.0) ? std::pow (f, -0.75) : 0.0;
            // random phase, Hermitian symmetric
            if (k == 0 || k == n / 2) { spec[static_cast<size_t> (k)] = fx::Complex (0.0f, 0.0f); continue; }
            if (k < n / 2)
            {
                const double ph = 3.141592653589793 * rng.white();
                spec[static_cast<size_t> (k)] = fx::Complex (static_cast<float> (amp * std::cos (ph)), static_cast<float> (amp * std::sin (ph)));
            }
            else
                spec[static_cast<size_t> (k)] = spec[static_cast<size_t> (n - k)].conj();
        }
        fft.ifft (spec.data());
        x.resize (static_cast<size_t> (n));
        double e = 0;
        for (int i = 0; i < n; ++i) { x[static_cast<size_t> (i)] = spec[static_cast<size_t> (i)].r; e += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)]; }
        const float g = static_cast<float> (0.1 / std::sqrt (e / n));
        for (auto& v : x) v *= g;
    }
    float at (long i) const noexcept { return x[static_cast<size_t> (i % static_cast<long> (x.size()))]; }
};
} // namespace

int main()
{
    std::string line;
    while (std::getline (std::cin, line))
    {
        std::istringstream is (line);
        std::string name;
        if (! (is >> name) || name[0] == '#')
            continue;
        int module = -1;
        for (int m = 0; m < 13; ++m)
            if (name == kNames[m])
                module = m;
        if (module < 0 || module == 12)
        {
            std::printf ("%s unknown\n", name.c_str());
            continue;
        }
        bool mono = false, chord = false;
        float corr = 0.7f;
        std::vector<float> base (paramCount()), delta (paramCount(), 0.0f);
        for (size_t i = 0; i < paramCount(); ++i)
            base[i] = paramTable()[i].def;
        std::string kv;
        bool bad = false;
        while (is >> kv)
        {
            if (kv == "mono") { mono = true; continue; }
            if (kv == "stereo") { mono = false; corr = 0.7f; continue; }
            if (kv == "indep") { mono = false; corr = 0.0f; continue; }
            if (kv == "chord") { chord = true; continue; }
            const size_t eq = kv.find ('=');
            const ParamInfo* info = eq == std::string::npos ? nullptr : findParamByKey (kv.substr (0, eq).c_str());
            if (info == nullptr) { std::printf ("%s bad key %s\n", name.c_str(), kv.c_str()); bad = true; break; }
            base[static_cast<size_t> (findParamIndex (info->id))] = static_cast<float> (std::atof (kv.c_str() + eq + 1));
        }
        if (bad)
            continue;
        constexpr double kSr = 44100.0;
        auto fx = makeFxModule (module);
        fx->prepare (kSr, kControlPeriod);
        FxFrame frame;
        frame.noSmoothing = true;
        static const Program progL (0x1234567ull), progR (0xABCDEF12345ull), progC (0x777ull);
        const float rho = mono ? 1.0f : corr; // the channels are sqrt (rho) of one common program and the rest of their own
        const long periods = static_cast<long> (10.0 * kSr / kControlPeriod);
        const long skip = static_cast<long> (4.0 * kSr / kControlPeriod);
        double pIn = 0, pOut = 0, cross = 0;
        float l[kControlPeriod], r[kControlPeriod];
        for (long p = 0; p < periods; ++p)
        {
            float il[kControlPeriod], ir[kControlPeriod];
            for (int i = 0; i < kControlPeriod; ++i)
            {
                if (chord)
                {
                    // a C major triad of sawtooth waves (the right channel with a slight detune), the same level as the noise
                    float a = 0.0f, b = 0.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        static const double kF[3] = { 130.81, 164.81, 196.00 };
                        const double t = static_cast<double> (p * kControlPeriod + i) / kSr;
                        const double ph = t * kF[k], phr = t * kF[k] * 1.003;
                        a += static_cast<float> (2.0 * (ph - std::floor (ph)) - 1.0);
                        b += static_cast<float> (2.0 * (phr - std::floor (phr)) - 1.0);
                    }
                    il[i] = l[i] = 0.05f * a;
                    ir[i] = r[i] = mono ? il[i] : 0.05f * b;
                    continue;
                }
                const long at = p * kControlPeriod + i;
                const float c = std::sqrt (rho), o = std::sqrt (1.0f - rho);
                il[i] = l[i] = c * progC.at (at) + o * progL.at (at);
                ir[i] = r[i] = c * progC.at (at) + o * progR.at (at);
            }
            fx->update (frame, base.data(), delta.data());
            fx->process (l, r, kControlPeriod, 0, kControlPeriod);
            if (p >= skip)
                for (int i = 0; i < kControlPeriod; ++i)
                {
                    pIn += 0.5 * (il[i] * il[i] + ir[i] * ir[i]);
                    pOut += 0.5 * (l[i] * l[i] + r[i] * r[i]);
                    cross += 0.5 * (il[i] * l[i] + ir[i] * r[i]);
                }
        }
        std::printf ("%s %.3f %.4f\n", name.c_str(), 10.0 * std::log10 (pOut / pIn), cross / std::sqrt (pIn * pOut));
    }
    return 0;
}
