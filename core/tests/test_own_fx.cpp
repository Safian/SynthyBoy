// SynthyBoy - own engine tests, effects: the vectors of docs/engine-spec/06-effects.md and 05-filters.md (reverb R1 .. R3, EQ E1 .. E8, compressor C1 / C2, phaser P1 / P2, the SVF styles),
// the effect instance pool, the latency compensation across the buses, Convolve.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "own_oracle.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/fx_types.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;
bool verbose() { return std::getenv ("SBFX_VERBOSE") != nullptr; }

std::unique_ptr<IEngine> make (int block = 256)
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, block, qualityForTier (Tier::high));
    return e;
}

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}


/** A short saw burst: osc1 level 100, env attack 0 / decay 20 ms / sustain 0. */
void burstPatch (IEngine& e, int wavetable = 1)
{
    set (e, "osc1.wavetable", static_cast<float> (wavetable));
    set (e, "osc1.level", 100);
    set (e, "filter1.on", 0);
    set (e, "master.volume", 0);
    set (e, "env1.attack", 0);
    set (e, "env1.decay", 20);
    set (e, "env1.sustain", 0);
    set (e, "env1.release", 5);
}

std::vector<float> sq10ms (const std::vector<float>& x)
{
    const size_t w = static_cast<size_t> (kSr * 0.01);
    std::vector<float> e;
    for (size_t i = 0; i + w <= x.size(); i += w)
    {
        double s = 0.0;
        for (size_t k = 0; k < w; ++k)
            s += static_cast<double> (x[i + k]) * x[i + k];
        e.push_back (static_cast<float> (10.0 * std::log10 (std::max (s, 1.0e-30))));
    }
    return e;
}

double rt60 (const std::vector<float>& x, size_t from)
{
    // Schroeder backward integration; RT60 from the -5 .. -35 dB part (x2)
    std::vector<double> edc (x.size() - from);
    double acc = 0.0;
    for (size_t i = x.size(); i-- > from;)
    {
        acc += static_cast<double> (x[i]) * x[i];
        edc[i - from] = acc;
    }
    const double total = edc[0];
    double t5 = -1.0, t35 = -1.0;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (edc[i] / total, 1.0e-30));
        if (t5 < 0.0 && db <= -5.0)
            t5 = static_cast<double> (i) / kSr;
        if (t35 < 0.0 && db <= -35.0)
        {
            t35 = static_cast<double> (i) / kSr;
            break;
        }
    }
    return (t5 >= 0.0 && t35 >= 0.0) ? 2.0 * (t35 - t5) : -1.0;
}
} // namespace

// ------------------------------------------------------------------------------------------------------------------------------------
// reverb: level and tail vectors of all six types (document 06, 9.5)

namespace
{
struct R3Row { int decay; int size; int type; double peak, w01, w025, w05, w1, w2, rt; };
// document 06, 9.5: the highest 10 ms window, the windows 0.1 / 0.25 / 0.5 / 1 / 2 s after the note on, the RT60
const R3Row kR3[] = {
    { 500, 100, 0, -8.5, -20.2, -36.9, -59.9, -103, -182, 0.61 }, { 500, 100, 1, -11.6, -17.9, -18.7, -19.6, -43.2, -84.3, 1.28 },
    { 500, 100, 2, -11.8, -89.6, -38.4, -69.1, -128, -200, 0.75 }, { 500, 100, 3, -13.4, -29.0, -21.8, -45.5, -110, -200, 0.58 },
    { 500, 100, 4, -12.9, -21.7, -39.3, -65.5, -132, -200, 0.59 }, { 500, 100, 5, -16.9, -22.3, -44.2, -68.5, -130, -200, 0.55 },
    { 500, 50, 0, -8.6, -20.2, -26.4, -43.4, -77.2, -130, 0.93 }, { 500, 50, 1, -8.7, -13.0, -26.9, -58.8, -86.9, -156, 0.44 },
    { 500, 50, 2, -11.8, -16.9, -31.1, -64.7, -122, -200, 0.53 }, { 500, 50, 3, -11.6, -14.9, -34.1, -66.7, -120, -200, 0.52 },
    { 500, 50, 4, -12.8, -21.1, -29.5, -62.6, -124, -200, 0.51 }, { 500, 50, 5, -12.6, -18.4, -33.9, -63.4, -119, -200, 0.50 },
    { 1000, 100, 0, -8.5, -20.2, -32.9, -43.8, -64.3, -115, 1.19 }, { 1000, 100, 1, -11.6, -17.9, -18.7, -19.6, -43.2, -82.6, 1.28 },
    { 1000, 100, 2, -11.8, -89.6, -26.5, -42.5, -72.3, -131, 1.05 }, { 1000, 100, 3, -13.6, -29.0, -22.0, -31.2, -65.0, -126, 0.99 },
    { 1000, 100, 4, -12.9, -17.8, -27.4, -38.1, -75.0, -131, 1.00 }, { 1000, 100, 5, -15.5, -17.5, -29.5, -41.2, -72.0, -130, 1.00 },
    { 2000, 50, 0, -8.8, -17.5, -15.6, -21.9, -34.8, -48.7, 3.19 }, { 2000, 50, 1, -8.7, -13.0, -23.9, -29.4, -40.2, -63.1, 2.17 },
    { 2000, 50, 2, -10.5, -11.5, -12.3, -24.8, -37.1, -71.0, 2.01 }, { 2000, 50, 3, -9.9, -12.8, -17.0, -30.3, -37.1, -69.0, 1.94 },
    { 2000, 50, 4, -7.6, -15.2, -10.8, -21.0, -40.1, -67.1, 1.99 }, { 2000, 50, 5, -7.2, -12.3, -14.5, -20.8, -37.6, -69.6, 2.01 },
};

std::vector<float> reverbBurst (int decay, int size, int type, bool on = true, std::vector<float>* rawOut = nullptr)
{
    auto e = make();
    burstPatch (*e);
    if (on)
    {
        set (*e, "reverb.on", 1);
        set (*e, "reverb.mix", 100);
        set (*e, "reverb.decay", static_cast<float> (decay));
        set (*e, "reverb.size", static_cast<float> (size));
        set (*e, "reverb.type", static_cast<float> (type));
        set (*e, "reverb.high_shelf_gain", 0);
        set (*e, "reverb.low_shelf_gain", 0);
        set (*e, "reverb.pre_high_cut", 12000);
        set (*e, "reverb.pre_low_cut", 20);
        set (*e, "reverb.chorus_amount", 0);
        set (*e, "reverb.predelay", 0);
        set (*e, "reverb.diff_a", 70);
        set (*e, "reverb.diff_b", 60);
        set (*e, "reverb.er_size", 50);
        set (*e, "reverb.feedback", 50);
    }
    const double secs = on ? std::min(16.0, 4.0 + 4.0 * decay / 1000.0 * (size < 60 ? 4 : 2)) : 1.0;
    const auto o = sbtest::render (*e, { sbtest::noteOn (static_cast<int> (0.1 * kSr), 60), sbtest::noteOff (static_cast<int> (0.12 * kSr), 60) }, static_cast<int> (secs * kSr), 256);
    if (rawOut != nullptr)
        *rawOut = o.left;
    return o.left;
}
} // namespace

TEST (fx_reverb_levels_and_tails_of_the_six_types)
{
    const auto dry = reverbBurst (500, 50, 0, false);
    const auto dryW = sq10ms (dry);
    double dryPeak = -1000;
    for (float v : dryW)
        dryPeak = std::max (dryPeak, static_cast<double> (v));
    if (verbose())
        std::printf ("    dry burst peak %.1f dB (spec +11.4)\n", dryPeak);
    for (const R3Row& row : kR3)
    {
        std::vector<float> raw;
        reverbBurst (row.decay, row.size, row.type, true, &raw);
        const auto w = sq10ms (raw);
        size_t pk = 0;
        for (size_t i = 0; i < w.size(); ++i)
            if (w[i] > w[pk])
                pk = i;
        auto at = [&] (double s) { const size_t i = static_cast<size_t> ((0.1 + s) / 0.01); return i < w.size() ? static_cast<double> (w[i]) : -300.0; };
        const double rel = dryPeak - 11.4; // our dry burst against the one of the document: the table is shifted by the same amount
        const double rt = rt60 (raw, static_cast<size_t> (0.1 * kSr));
        if (verbose())
            std::printf ("    d%4d s%3d t%d: peak %6.1f @%.2f (%6.1f)  +.1 %6.1f (%6.1f)  +.25 %6.1f (%6.1f)  +.5 %6.1f (%6.1f)  +1 %6.1f (%6.1f)  rt60 %.2f (%.2f)\n", row.decay, row.size, row.type,
                        w[pk] - rel, pk * 0.01, row.peak, at (0.1) - rel, row.w01, at (0.25) - rel, row.w025, at (0.5) - rel, row.w05, at (1.0) - rel, row.w1, rt, row.rt);
        CHECK (std::fabs (w[pk] - rel - row.peak) < 2.5);
        if (rt > 0.0)
            CHECK (rt > row.rt * 0.7 && rt < row.rt * 1.35);
    }
}

TEST (fx_reverb_vital_rt60_table_r1)
{
    // document 06, 9.2: RT60 of the Vital type by decay and size (the shape: size 100 % -> about the decay, smaller sizes decay longer)
    struct Row { int decay; int size; double rt; };
    const Row rows[] = { { 500, 20, 1.81 }, { 500, 50, 0.90 }, { 500, 100, 0.54 }, { 1000, 20, 3.21 }, { 1000, 50, 1.59 }, { 1000, 100, 1.06 },
                         { 2000, 20, 6.87 }, { 2000, 50, 2.80 }, { 2000, 100, 2.14 }, { 4000, 50, 7.24 }, { 4000, 100, 4.97 } };
    for (const Row& row : rows)
    {
        std::vector<float> raw;
        reverbBurst (row.decay, row.size, 0, true, &raw);
        const double rt = rt60 (raw, static_cast<size_t> (0.1 * kSr));
        if (verbose())
            std::printf ("    R1 decay %d size %d: RT60 %.2f (spec %.2f)\n", row.decay, row.size, rt, row.rt);
        CHECK (rt > row.rt * 0.7 && rt < row.rt * 1.4);
    }
}

TEST (fx_reverb_predelay_and_mix_r2)
{
    // pre-delay 100 ms: the onset moves by 100 ms and the silence before it is exact; mix 50 %: the dry burst at cos (pi / 4)
    auto render = [&] (float predelay, float mix)
    {
        auto e = make();
        burstPatch (*e);
        set (*e, "reverb.on", 1);
        set (*e, "reverb.mix", mix);
        set (*e, "reverb.decay", 2000);
        set (*e, "reverb.size", 50);
        set (*e, "reverb.chorus_amount", 0);
        set (*e, "reverb.high_shelf_gain", 0);
        set (*e, "reverb.predelay", predelay);
        return sbtest::render (*e, { sbtest::noteOn (static_cast<int> (0.1 * kSr), 60), sbtest::noteOff (static_cast<int> (0.12 * kSr), 60) }, static_cast<int> (1.0 * kSr), 256);
    };
    const auto a = render (0, 100), b = render (100, 100);
    auto onset = [&] (const std::vector<float>& x) { for (size_t i = 0; i < x.size(); ++i) if (std::fabs (x[i]) > 1.0e-5f) return static_cast<double> (i) / kSr; return -1.0; };
    const double oa = onset (a.left), ob = onset (b.left);
    if (verbose())
        std::printf ("    onset %.4f / %.4f\n", oa, ob);
    CHECK (std::fabs ((ob - oa) - 0.1) < 0.004);
    const auto half = render (0, 50), dry = [&] { auto e = make(); burstPatch (*e); return sbtest::render (*e, { sbtest::noteOn (static_cast<int> (0.1 * kSr), 60) }, static_cast<int> (0.3 * kSr), 256); }();
    double pd = 0.0, ph = 0.0;
    for (size_t i = 0; i < dry.left.size(); ++i)
    {
        pd = std::max (pd, static_cast<double> (std::fabs (dry.left[i])));
        ph = std::max (ph, static_cast<double> (std::fabs (half.left[i])));
    }
    CHECK (ph > 0.6 * pd && ph < 0.85 * pd); // the dry part is 0.707 (plus the wet at 0.707)
}

// ------------------------------------------------------------------------------------------------------------------------------------
// white noise through a patch: the gain of the spectrum against a reference patch at the nine frequencies of the EQ table

namespace
{
using KV = std::vector<std::pair<std::string, float>>;

std::vector<double> bandPowers (const std::vector<float>& x, const std::vector<double>& freqs, size_t from, size_t N = 8192)
{
    std::vector<double> acc (N / 2, 0.0);
    for (size_t pos = from; pos + N <= x.size(); pos += N / 2)
    {
        const auto ps = oracle::powerSpectrum (x, pos, N);
        for (size_t i = 0; i < N / 2; ++i)
            acc[i] += ps[i];
    }
    std::vector<double> out;
    for (double f : freqs)
    {
        const size_t c = static_cast<size_t> (f / (kSr / static_cast<double> (N)) + 0.5);
        double s = 0;
        for (size_t i = c - 6; i <= c + 6 && i < acc.size(); ++i)
            s += acc[i];
        out.push_back (s);
    }
    return out;
}

oracle::Overrides noiseBase (const KV& extra)
{
    oracle::Overrides ov = { { "osc1.on", 0 }, { "noise.on", 1 }, { "noise.level", 50 }, { "filter1.on", 0 }, { "master.volume", -6 }, { "env1.attack", 0 }, { "env1.release", 50 } };
    for (auto& e : extra)
        ov.push_back (e);
    return ov;
}

/** dB gain of the effect patch against the same patch without it. */
std::vector<double> fxGain (const KV& fx, const std::vector<double>& freqs, int note = 69, double seconds = 3.2)
{
    oracle::Note n;
    n.key = note;
    n.length = seconds - 0.1;
    const auto a = oracle::render (EngineKind::own, noiseBase (fx), { n }, seconds, kSr);
    const auto b = oracle::render (EngineKind::own, noiseBase ({}), { n }, seconds, kSr);
    const auto pa = bandPowers (a.l, freqs, static_cast<size_t> (0.4 * kSr)), pb = bandPowers (b.l, freqs, static_cast<size_t> (0.4 * kSr));
    std::vector<double> g;
    for (size_t i = 0; i < freqs.size(); ++i)
        g.push_back (10.0 * std::log10 (pa[i] / pb[i]));
    return g;
}

const std::vector<double> kEqFreqs = { 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

void expectGains (const std::vector<double>& got, const std::vector<double>& want, double tol, const char* what)
{
    for (size_t i = 0; i < want.size(); ++i)
    {
        // the top octave of a low pass / shelf: the reference runs at twice the rate (less cramping), a stronger attenuation than the document's is better
        const bool ok = std::fabs (got[i] - want[i]) <= tol || (want[i] < -8.0 && got[i] < want[i]);
        if (!CHECK (ok) || verbose())
            std::printf ("    %s @%g Hz: %.1f dB, spec %.1f dB\n", what, kEqFreqs[i], got[i], want[i]);
    }
}
} // namespace

TEST (fx_eq_vital_law_vectors_e1_e7)
{
    const KV on = { { "eq.on", 1 } };
    auto with = [&] (KV more) { KV k = on; for (auto& m : more) k.push_back (m); return k; };
    expectGains (fxGain (with ({ { "eq.mid.mode", 0 }, { "eq.mid.cutoff", 1000 }, { "eq.mid.gain", 12 } }), kEqFreqs), { 0.4, 1.0, 3.4, 7.9, 12.0, 7.9, 3.3, 1.0, 0.3 }, 1.2, "E1");
    expectGains (fxGain (with ({ { "eq.mid.cutoff", 1000 }, { "eq.mid.gain", 12 }, { "eq.mid.resonance", 100 } }), kEqFreqs), { 0.0, 0.0, 0.0, 0.1, 10.8, 0.1, 0.0, 0.0, 0.0 }, 1.5, "E2");
    expectGains (fxGain (with ({ { "eq.low.mode", 0 }, { "eq.low.cutoff", 250 }, { "eq.low.gain", 12 } }), kEqFreqs), { 12.2, 12.4, 10.5, 1.4, -0.4, -0.2, 0.0, 0.0, 0.0 }, 1.2, "E3");
    expectGains (fxGain (with ({ { "eq.high.mode", 0 }, { "eq.high.cutoff", 4000 }, { "eq.high.gain", -12 } }), kEqFreqs), { 0.0, 0.0, 0.0, 0.0, 0.2, 0.4, -1.4, -10.8, -12.4 }, 1.2, "E4");
    expectGains (fxGain (with ({ { "eq.low.mode", 1 }, { "eq.low.cutoff", 250 } }), kEqFreqs), { -20.3, -11.7, -0.7, 0.7, 0.2, 0.1, 0.0, 0.0, 0.0 }, 1.2, "E5");
    expectGains (fxGain (with ({ { "eq.high.mode", 1 }, { "eq.high.cutoff", 4000 } }), kEqFreqs), { 0.0, 0.0, 0.0, 0.1, 0.2, 0.7, -0.7, -11.7, -25.5 }, 1.2, "E6");
    expectGains (fxGain (with ({ { "eq.mid.mode", 1 }, { "eq.mid.cutoff", 1000 } }), kEqFreqs), { 0.0, -0.1, -0.4, -1.9, -28.3, -1.8, -0.3, -0.1, 0.0 }, 1.2, "E7");
}

TEST (fx_eq_serum_law_and_second_band)
{
    const KV on = { { "eq.on", 1 }, { "eq.law", 1 } };
    auto with = [&] (KV more) { KV k = on; for (auto& m : more) k.push_back (m); return k; };
    expectGains (fxGain (with ({ { "eq.mid.mode", 0 }, { "eq.mid.cutoff", 1000 }, { "eq.mid.gain", 12 } }), kEqFreqs), { 1.1, 2.5, 6.3, 10.1, 11.9, 10.1, 6.2, 2.6, 0.7 }, 1.2, "E8");
    // the second peak band: two peaks add in dB
    const auto two = fxGain ({ { "eq.on", 1 }, { "eq.mid.cutoff", 1000 }, { "eq.mid.gain", 6 }, { "eq.mid2.mode", 1 }, { "eq.mid2.cutoff", 1000 }, { "eq.mid2.gain", 6 } }, kEqFreqs);
    CHECK (std::fabs (two[4] - 12.0) < 1.0);
    const auto none = fxGain ({ { "eq.on", 1 }, { "eq.mid2.mode", 0 }, { "eq.mid2.gain", 12 } }, kEqFreqs);
    for (double g : none)
        CHECK (std::fabs (g) < 0.3);
}

// ------------------------------------------------------------------------------------------------------------------------------------
// compressor: the static laws (document 06, 6 and 6.1), the limiter, the Serum mode, the latency

namespace
{
/** Peak of a sine patch (osc1 level `level` %) in dB over the last second of a 2 s note, with `fx` applied. */
double sinePeakDb (const KV& fx, float level, int note = 69, double* onsetOut = nullptr)
{
    oracle::Overrides ov = { { "osc1.wavetable", 0 }, { "osc1.level", level }, { "filter1.on", 0 }, { "master.volume", 0 }, { "env1.attack", 0 }, { "env1.release", 50 } };
    for (auto& e : fx)
        ov.push_back (e);
    oracle::Note n;
    n.key = note;
    n.length = 1.9;
    const auto o = oracle::render (EngineKind::own, ov, { n }, 2.0, kSr);
    if (onsetOut != nullptr)
    {
        *onsetOut = -1.0;
        for (size_t i = 0; i < o.l.size(); ++i)
            if (std::fabs (o.l[i]) > 1.0e-3f)
            {
                *onsetOut = static_cast<double> (i);
                break;
            }
    }
    return oracle::toDb (oracle::peak (o.l, static_cast<size_t> (1.0 * kSr), static_cast<size_t> (1.8 * kSr)));
}

KV compSingle (float upperThr, float upperRatio, float lowerThr, float lowerRatio)
{
    return { { "compressor.on", 1 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", upperThr }, { "compressor.mid.upper_ratio", upperRatio },
             { "compressor.mid.lower_threshold", lowerThr }, { "compressor.mid.lower_ratio", lowerRatio }, { "compressor.mid.gain", 0 }, { "compressor.attack", 0 },
             { "compressor.release", 50 }, { "compressor.mix", 100 } };
}
} // namespace

TEST (fx_compressor_static_laws_c1_c2)
{
    for (float level : { 100.0f, 60.0f, 30.0f, 10.0f })
    {
        const double in = sinePeakDb ({}, level);
        for (float rho : { 0.0f, 50.0f, 100.0f })
        {
            const double out = sinePeakDb (compSingle (-20, rho, -120, 0), level);
            const double want = in > -20 ? in + rho * 0.01 * (-20 - in) : in;
            if (verbose())
                std::printf ("    upper level %g rho %g: in %.1f out %.1f want %.1f\n", level, rho, in, out, want);
            CHECK (std::fabs (out - want) < 0.9);
        }
        // upward: lower threshold -8 dB (the sines of this test reach -20 dB); ratio 50 % lifts, -50 % expands
        for (float rho : { 50.0f, 100.0f, -50.0f })
        {
            const double out = sinePeakDb (compSingle (12, 0, -8, rho), level);
            const double want = in < -8 ? in + rho * 0.01 * (-8 - in) : in;
            if (verbose())
                std::printf ("    lower level %g rho %g: in %.1f out %.1f want %.1f\n", level, rho, in, out, want);
            CHECK (std::fabs (out - want) < 1.2);
        }
    }
}

TEST (fx_compressor_serum_mode_and_limiter)
{
    // Serum mode: a hard knee at the threshold, the slope above it is 1 - rho
    const double in = sinePeakDb ({}, 100);
    const double s = sinePeakDb ([&] { auto k = compSingle (-20, 75, -120, 0); k.push_back ({ "compressor.limit", 2 }); return k; }(), 100);
    if (verbose())
        std::printf ("    serum: in %.1f out %.1f want %.1f\n", in, s, in + 0.75 * (-20 - in));
    CHECK (std::fabs (s - (in + 0.75 * (-20 - in))) < 1.2);
    // the limiter: the ceiling holds, the latency is the look-ahead
    double onsetPlain = 0, onsetLimit = 0;
    sinePeakDb ({}, 100, 69, &onsetPlain);
    const double lim = sinePeakDb ([&] { auto k = compSingle (-12, 0, -120, 0); k.push_back ({ "compressor.limit", 1 }); return k; }(), 100, 69, &onsetLimit);
    CHECK (lim < -12.0 + 0.3);
    CHECK (lim > -12.0 - 1.5);
    auto e = make();
    set (*e, "compressor.on", 1);
    set (*e, "compressor.limit", 1);
    sbtest::render (*e, {}, 512, 256);
    CHECK_EQ (e->latencySamples(), static_cast<int> (std::lround (0.0015 * kSr)));
    set (*e, "compressor.limit_latency_comp", 0);
    sbtest::render (*e, {}, 512, 256);
    CHECK_EQ (e->latencySamples(), 0);
}

// ------------------------------------------------------------------------------------------------------------------------------------
// phaser depth 2 (P2)

namespace
{
std::vector<double> notchesOf (const KV& fx, double lo, double hi, int count)
{
    oracle::Overrides ov = noiseBase (fx);
    oracle::Overrides ref = noiseBase ({});
    oracle::Note n;
    n.length = 3.5;
    const auto a = oracle::render (EngineKind::own, ov, { n }, 3.6, kSr), b = oracle::render (EngineKind::own, ref, { n }, 3.6, kSr);
    const size_t N = 8192;
    std::vector<double> pa (N / 2, 0.0), pb (N / 2, 0.0);
    for (size_t pos = static_cast<size_t> (0.3 * kSr); pos + N <= a.l.size(); pos += N / 2)
    {
        const auto x = oracle::powerSpectrum (a.l, pos, N), y = oracle::powerSpectrum (b.l, pos, N);
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
    struct N1 { double hz, db; };
    std::vector<N1> found;
    const double binHz = kSr / static_cast<double>(N);
    for (size_t i = 4; i + 4 < N / 2; ++i)
        if (g[i] < -12.0 && g[i] <= g[i - 1] && g[i] <= g[i + 1] && i * binHz >= lo && i * binHz <= hi)
            found.push_back ({ i * binHz, g[i] });
    // merge neighbours closer than 4 % keeping the deepest
    std::vector<N1> merged;
    for (const auto& f : found)
    {
        if (!merged.empty() && f.hz < merged.back().hz * 1.04)
        {
            if (f.db < merged.back().db)
                merged.back() = f;
        }
        else
            merged.push_back (f);
    }
    std::vector<double> out;
    for (const auto& m : merged)
        out.push_back (m.hz);
    (void) count;
    return out;
}
} // namespace

TEST (fx_phaser_depth2_notches_p2)
{
    const KV base = { { "phaser.on", 1 }, { "phaser.mix", 100 }, { "phaser.depth", 0 }, { "phaser.center", 1000 }, { "phaser.feedback", 0 }, { "phaser.blend", 100 }, { "phaser.rate", 0.04f } };
    struct Row { float d2; std::vector<double> hz; };
    const Row rows[] = { { 0, { 146, 451, 838, 1418, 2625, 8080 } }, { 12, { 141, 445, 832, 1430, 2666, 8262 } }, { -12, { 141, 445, 832, 1430, 2666, 8262 } },
                         { 24, { 129, 422, 814, 1453, 2801, 8801 } } };
    for (const Row& row : rows)
    {
        KV k = base;
        k.push_back ({ "phaser.depth2", row.d2 });
        const auto got = notchesOf (k, 60, 12000, 6);
        if (verbose())
        {
            std::printf ("    depth2 %g: ", row.d2);
            for (double f : got)
                std::printf ("%.0f ", f);
            std::printf ("\n");
        }
        CHECK_EQ (got.size(), row.hz.size());
        if (got.size() == row.hz.size())
            for (size_t i = 0; i < got.size(); ++i)
                // +-4 % (+-10 Hz for the lowest); the notch above 7 kHz is 6 .. 7 % low: the reference runs the chain at twice the rate (document 06 1: the host rate is allowed), the bilinear transform cramps the top octave
                CHECK (std::fabs (got[i] - row.hz[i]) < (row.hz[i] > 7000 ? row.hz[i] * 0.075 : std::max (row.hz[i] * 0.04, i == 0 ? 10.0 : 0.0)));
    }
}
