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
//   MODULE GAIN_DB CORRELATION MODEL_DB STEADY WIDE   (the last two: the program descriptor of the meter, input power weighted)
// MODEL_DB is the power gain that the engine's makeup model (core/engine-own/fx/fx_makeup.h, FxModule::makeupPower, fed by a ProgramMeter on the same input) predicts, input power weighted
// over the measured window, 0 for a module without a model. `input=FILE` plays a raw 32 bit float stereo render (interleaved, looped) instead of a synthetic program.

#include <cmath>
#include <cstdio>
#include <iostream>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../core/fx-serum-b/fft.h"
#include "fx/fx_module.h"
#include "fx/fx_makeup.h"
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

/** spec=LO:HI:E (noise with power per Hz ~ f^E between LO and HI Hz instead of the standard program): the programs of the regression of the makeup models. */
struct SpecSpec { double lo = 30.0, hi = 12000.0, exponent = -1.5; };

/** The calibration program: noise whose power per Hz falls with f^-1.5 from 30 Hz to 12 kHz (the program of core/engine-own/fx/fx_makeup.h), made by shaping the spectrum of white
    noise with an FFT, 2^19 samples long and run as a loop. Two of them (`left`, `right`) mixed to the channel correlation of the job. */
struct Program
{
    std::vector<float> x;
    explicit Program (uint64_t seed, SpecSpec sp = SpecSpec())
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
            const double amp = (f >= sp.lo && f <= sp.hi) ? std::pow (f, sp.exponent * 0.5) : 0.0;
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
enum class Prog { noise, sawLow, sawMid, sawHigh, squareMid, sineMid, pluck, pad };
Prog progOf (const std::string& n)
{
    if (n == "sawlow") return Prog::sawLow;
    if (n == "sawmid") return Prog::sawMid;
    if (n == "sawhigh") return Prog::sawHigh;
    if (n == "squaremid") return Prog::squareMid;
    if (n == "sinemid") return Prog::sineMid;
    if (n == "pluck") return Prog::pluck;
    if (n == "pad") return Prog::pad;
    return Prog::noise;
}
/** The synthetic programs: sustained triads of saw, square and sine in three registers, a pluck (a saw note, 0.12 s decay, every 0.5 s) and a pad (a saw triad, 1.5 s attack, 2 s hold,
    1 s release, every 6 s). The right channel is detuned by 0.3 % (the width of a unison voice pair); `mono` copies the left. */
void program (Prog p, double t, float& left, float& right)
{
    double root = 130.81;
    if (p == Prog::sawLow) root = 65.41;
    if (p == Prog::sawHigh) root = 523.25;
    const double ratio[3] = { 1.0, 1.25992, 1.49831 };
    double env = 1.0;
    if (p == Prog::pluck) { const double u = std::fmod (t, 0.5); env = std::exp (-u / 0.12); }
    if (p == Prog::pad)
    {
        const double u = std::fmod (t, 6.0);
        env = u < 1.5 ? u / 1.5 : (u < 3.5 ? 1.0 : (u < 4.5 ? 1.0 - (u - 3.5) : 0.0));
    }
    double a = 0, b = 0;
    const int notes = p == Prog::pluck ? 1 : 3;
    for (int k = 0; k < notes; ++k)
        for (int c = 0; c < 2; ++c)
        {
            const double ph = t * root * ratio[k] * (c == 1 ? 1.003 : 1.0);
            const double f = ph - std::floor (ph);
            double v;
            if (p == Prog::squareMid) v = f < 0.5 ? 1.0 : -1.0;
            else if (p == Prog::sineMid) v = std::sin (6.283185307179586 * f);
            else v = 2.0 * f - 1.0;
            (c == 0 ? a : b) += v;
        }
    left = static_cast<float> (0.05 * env * a);
    right = static_cast<float> (0.05 * env * b);
}
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
        bool mono = false, haveSpec = false;
        SpecSpec spec;
        Prog prog = Prog::noise;
        std::string dump;
        float inScale = 1.0f; // `scale=G`: the input is multiplied by G (the effects are not all linear)
        std::vector<float> inputData; // a stereo render (interleaved) played instead of a synthetic program, looped
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
            if (kv.rfind ("program=", 0) == 0) { prog = progOf (kv.substr (8)); continue; }
            if (kv.rfind ("spec=", 0) == 0)
            {
                haveSpec = std::sscanf (kv.c_str() + 5, "%lf:%lf:%lf", &spec.lo, &spec.hi, &spec.exponent) == 3;
                continue;
            }
            if (kv.rfind ("input=", 0) == 0)
            {
                std::FILE* f = std::fopen (kv.c_str() + 6, "rb");
                if (f == nullptr) { std::printf ("%s bad input\n", name.c_str()); bad = true; break; }
                std::fseek (f, 0, SEEK_END);
                const long bytes = std::ftell (f);
                std::fseek (f, 0, SEEK_SET);
                inputData.resize (static_cast<size_t> (bytes) / 4);
                if (std::fread (inputData.data(), 4, inputData.size(), f) != inputData.size()) { bad = true; }
                std::fclose (f);
                continue;
            }
            if (kv.rfind ("scale=", 0) == 0) { inScale = static_cast<float> (std::atof (kv.c_str() + 6)); continue; }
            if (kv.rfind ("dump=", 0) == 0) { dump = kv.substr (5); continue; }
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
        static const Program sL (0x1234567ull), sR (0xABCDEF12345ull), sC (0x777ull);
        std::unique_ptr<Program> cL, cR, cC;
        if (haveSpec)
        {
            cL = std::make_unique<Program> (0x1234567ull, spec);
            cR = std::make_unique<Program> (0xABCDEF12345ull, spec);
            cC = std::make_unique<Program> (0x777ull, spec);
        }
        const Program& progL = haveSpec ? *cL : sL;
        const Program& progR = haveSpec ? *cR : sR;
        const Program& progC = haveSpec ? *cC : sC;
        const float rho = mono ? 1.0f : corr; // the channels are sqrt (rho) of one common program and the rest of their own
        const long periods = static_cast<long> (10.0 * kSr / kControlPeriod);
        const long skip = static_cast<long> (4.0 * kSr / kControlPeriod);
        double pIn = 0, pOut = 0, cross = 0, predNum = 0, predDen = 0, steadySum = 0, wideSum = 0;
        makeup::ProgramMeter meter;
        meter.prepare (kSr);
        std::vector<float> dIn, dOut;
        float l[kControlPeriod], r[kControlPeriod];
        for (long p = 0; p < periods; ++p)
        {
            float il[kControlPeriod], ir[kControlPeriod];
            for (int i = 0; i < kControlPeriod; ++i)
            {
                if (! inputData.empty())
                {
                    const size_t frames = inputData.size() / 2;
                    const size_t at = static_cast<size_t> (p * kControlPeriod + i) % frames;
                    il[i] = l[i] = inputData[2 * at];
                    ir[i] = r[i] = mono ? il[i] : inputData[2 * at + 1];
                    continue;
                }
                if (prog != Prog::noise)
                {
                    const double t = static_cast<double> (p * kControlPeriod + i) / kSr;
                    float a = 0.0f, b = 0.0f;
                    program (prog, t, a, b);
                    il[i] = l[i] = a;
                    ir[i] = r[i] = mono ? a : b;
                    continue;
                }
                const long at = p * kControlPeriod + i;
                const float c = std::sqrt (rho), o = std::sqrt (1.0f - rho);
                il[i] = l[i] = c * progC.at (at) + o * progL.at (at);
                ir[i] = r[i] = c * progC.at (at) + o * progR.at (at);
            }
            if (inScale != 1.0f)
                for (int i = 0; i < kControlPeriod; ++i) { il[i] *= inScale; ir[i] *= inScale; l[i] = il[i]; r[i] = ir[i]; }
            meter.accumulate (il, ir, kControlPeriod);
            meter.endPeriod (kControlPeriod);
            fx->update (frame, base.data(), delta.data());
            if (fx->hasMakeup() && p >= skip)
            {
                double e = 0;
                for (int i = 0; i < kControlPeriod; ++i) e += 0.5 * (il[i] * il[i] + ir[i] * ir[i]);
                predNum += e * fx->makeupPower (frame, base.data(), delta.data(), meter.desc());
                predDen += e;
                steadySum += e * meter.desc().steady;
                wideSum += e * meter.desc().wide;
            }
            fx->process (l, r, kControlPeriod, 0, kControlPeriod);
            if (! dump.empty())
                for (int i = 0; i < kControlPeriod; ++i) { dIn.push_back (il[i]); dIn.push_back (ir[i]); dOut.push_back (l[i]); dOut.push_back (r[i]); }
            if (p >= skip)
                for (int i = 0; i < kControlPeriod; ++i)
                {
                    pIn += 0.5 * (il[i] * il[i] + ir[i] * ir[i]);
                    pOut += 0.5 * (l[i] * l[i] + r[i] * r[i]);
                    cross += 0.5 * (il[i] * l[i] + ir[i] * r[i]);
                }
        }
        if (! dump.empty())
        {
            std::FILE* f = std::fopen ((dump + ".in").c_str(), "wb"); std::fwrite (dIn.data(), 4, dIn.size(), f); std::fclose (f);
            f = std::fopen ((dump + ".out").c_str(), "wb"); std::fwrite (dOut.data(), 4, dOut.size(), f); std::fclose (f);
        }
        std::printf ("%s %.3f %.4f %.3f %.3f %.3f\n", name.c_str(), 10.0 * std::log10 (pOut / pIn), cross / std::sqrt (pIn * pOut), predDen > 0 ? 10.0 * std::log10 (predNum / predDen + 1e-12) : 0.0, predDen > 0 ? steadySum / predDen : 0.0, predDen > 0 ? wideSum / predDen : 0.0);
    }
    return 0;
}
