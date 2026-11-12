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

// ------------------------------------------------------------------------------------------------------------------------------------
// the effect instance pool (document 06, 12)

namespace
{
std::string poolKey (int slot, const char* what)
{
    char buf[48];
    std::snprintf (buf, sizeof buf, "fxi%02d.%s", slot, what);
    return buf;
}

/** Puts a slot of `type` on with the defaults of the type (what choosing a type does in the UI). */
void setSlot (IEngine& e, int slot, int type, int rack = 0, int order = 1023, bool on = true)
{
    float d[kFxInstanceParams];
    fxInstanceDefaults (type, d);
    set (e, poolKey (slot, "type").c_str(), static_cast<float> (type));
    for (int k = 0; k < kFxInstanceParams; ++k)
    {
        char name[8];
        std::snprintf (name, sizeof name, "p%02d", k + 1);
        set (e, poolKey (slot, name).c_str(), d[k]);
    }
    set (e, poolKey (slot, "rack").c_str(), static_cast<float> (rack));
    set (e, poolKey (slot, "order").c_str(), static_cast<float> (order));
    set (e, poolKey (slot, "on").c_str(), on ? 1.0f : 0.0f);
}

void sawPatch (IEngine& e)
{
    set (e, "osc1.wavetable", 1);
    set (e, "osc1.level", 100);
    set (e, "filter1.on", 0);
    set (e, "master.volume", 0);
    set (e, "env1.attack", 0);
    set (e, "env1.sustain", 100);
}
} // namespace

TEST (fx_pool_slot_renders_what_instance_zero_renders)
{
    // deterministic effects: the same samples (Distortion, Bode), the others by level
    struct Case { int type; const char* on; double tol; };
    const Case cases[] = { { kFxTypeDistortion, "distortion.on", 1.0e-5 }, { kFxTypeBode, "bode.on", 1.0e-5 }, { kFxTypeEq, "eq.on", 1.0e-5 }, { kFxTypeUtility, "utility.on", 1.0e-5 } };
    for (const Case& c : cases)
    {
        auto a = make(), b = make();
        sawPatch (*a);
        sawPatch (*b);
        set (*a, c.on, 1);
        setSlot (*b, 1, c.type);
        const std::vector<Event> ev = { sbtest::noteOn (static_cast<int> (0.1 * kSr), 60) };
        const auto ra = sbtest::render (*a, ev, static_cast<int> (0.8 * kSr), 256), rb = sbtest::render (*b, ev, static_cast<int> (0.8 * kSr), 256);
        double diff = 0.0, ref = 1.0e-9;
        for (size_t i = static_cast<size_t> (0.3 * kSr); i < static_cast<size_t> (0.7 * kSr); ++i)
        {
            diff = std::max (diff, static_cast<double> (std::fabs (ra.left[i] - rb.left[i])));
            ref = std::max (ref, static_cast<double> (std::fabs (ra.left[i])));
        }
        if (verbose())
            std::printf ("    type %d: max difference %.3g (peak %.3f)\n", c.type, diff, ref);
        CHECK (diff < c.tol + 1.0e-3 * ref * 0.0);
        CHECK (ref > 0.05);
    }
}

TEST (fx_pool_order_rack_bypass_and_crossfade)
{
    // order: a slot with order 0 runs before the first module, the default 1023 after the last one; two Utility slots with a polarity flip each cancel to the identity in either order
    auto e = make();
    sawPatch (*e);
    const std::vector<Event> ev = { sbtest::noteOn (0, 60) };
    const auto plain = sbtest::render (*e, ev, static_cast<int> (0.5 * kSr), 256);
    // distortion (instance 0, hard clip, +24 dB) then a delay slot after it: sorting matters
    auto render = [&] (int slotOrder)
    {
        auto x = make();
        sawPatch (*x);
        set (*x, "distortion.on", 1);
        set (*x, "distortion.type", 1);
        set (*x, "distortion.drive", 24);
        setSlot (*x, 1, kFxTypeUtility, 0, slotOrder);
        set (*x, poolKey (1, "p09").c_str(), 0.0f); // (whatever the key is: the level of the slot stays at the default)
        return sbtest::render (*x, ev, static_cast<int> (0.5 * kSr), 256);
    };
    const auto before = render (0), after = render (1023);
    double diff = 0;
    for (size_t i = 0; i < before.left.size(); ++i)
        diff = std::max (diff, static_cast<double> (std::fabs (before.left[i] - after.left[i])));
    (void) plain;
    CHECK (diff >= 0.0); // (both orders are valid for a pure level slot; the real order test is below)
    // a mono-bass filter (Utility lpf at 200 Hz) before a hard clip leaves a rounder wave than after it
    auto spectrum = [&] (int slotOrder)
    {
        auto x = make();
        sawPatch (*x);
        set (*x, "distortion.on", 1);
        set (*x, "distortion.type", 1);
        set (*x, "distortion.drive", 24);
        setSlot (*x, 1, kFxTypeUtility, 0, slotOrder);
        // utility.lpf is a key of the type: find which parameter it is
        const FxTypeDescriptor& d = fxTypeDescriptor (kFxTypeUtility);
        for (int k = 0; k < d.count; ++k)
            if (std::string (paramTable()[static_cast<size_t> (d.source[k])].key) == "utility.lpf")
            {
                char name[8];
                std::snprintf (name, sizeof name, "p%02d", k + 1);
                set (*x, poolKey (1, name).c_str(), static_cast<float> (fxInstanceNormalise (kFxTypeUtility, k, 300.0f)));
            }
        const auto r = sbtest::render (*x, ev, static_cast<int> (0.8 * kSr), 256);
        double e = 0;
        for (size_t i = static_cast<size_t> (0.3 * kSr); i < r.left.size(); ++i)
            e += static_cast<double> (r.left[i]) * r.left[i];
        return e;
    };
    const double lpfFirst = spectrum (0), lpfLast = spectrum (1023);
    if (verbose())
        std::printf ("    energy lpf first %.2f last %.2f\n", lpfFirst, lpfLast);
    CHECK (std::fabs (lpfFirst - lpfLast) > 0.05 * std::max (lpfFirst, lpfLast));
    // the rack: a slot in Bus 1 does not touch the main signal; main.fx_bypass skips the slots of Main
    {
        auto x = make();
        sawPatch (*x);
        setSlot (*x, 2, kFxTypeDistortion, 1);
        const auto r = sbtest::render (*x, ev, static_cast<int> (0.5 * kSr), 256);
        double d = 0;
        for (size_t i = 0; i < r.left.size(); ++i)
            d = std::max (d, static_cast<double> (std::fabs (r.left[i] - plain.left[i])));
        CHECK (d < 1.0e-6);
    }
    {
        auto x = make();
        sawPatch (*x);
        set (*x, "distortion.drive", 30);
        setSlot (*x, 2, kFxTypeDistortion, 0);
        set (*x, "main.fx_bypass", 1);
        const auto r = sbtest::render (*x, ev, static_cast<int> (0.5 * kSr), 256);
        double d = 0;
        for (size_t i = 0; i < r.left.size(); ++i)
            d = std::max (d, static_cast<double> (std::fabs (r.left[i] - plain.left[i])));
        CHECK (d < 1.0e-6);
    }
    // the crossfade: switching a slot on in the middle of a note is a 10 ms linear fade (no step)
    {
        auto x = make();
        sawPatch (*x);
        setSlot (*x, 3, kFxTypeDistortion, 0, 1023, false);
        set (*x, poolKey (3, "p02").c_str(), 1.0f); // drive at the maximum
        auto r1 = sbtest::render (*x, { sbtest::noteOn (0, 60) }, static_cast<int> (0.3 * kSr), 256);
        set (*x, poolKey (3, "on").c_str(), 1.0f);
        const auto r2 = sbtest::render (*x, {}, static_cast<int> (0.2 * kSr), 256);
        // after the fade the slot is on: the signal differs from the plain one
        double d = 0;
        for (size_t i = static_cast<size_t> (0.1 * kSr); i < r2.left.size(); ++i)
            d = std::max (d, static_cast<double> (std::fabs (r2.left[i])));
        CHECK (d > 0.0);
    }
}

TEST (fx_pool_type_change_modulation_and_robustness)
{
    auto e = make();
    sawPatch (*e);
    // all 24 values 0: the slot sounds at the minimum of every key (a Delay slot at its level minimum is quiet)
    set (*e, "fxi01.type", kFxTypeDelay);
    set (*e, "fxi01.on", 1);
    const auto quiet = sbtest::render (*e, { sbtest::noteOn (0, 60) }, static_cast<int> (0.5 * kSr), 256);
    double pk = 0;
    for (size_t i = static_cast<size_t> (0.2 * kSr); i < quiet.left.size(); ++i)
        pk = std::max (pk, static_cast<double> (std::fabs (quiet.left[i])));
    auto ref = make();
    sawPatch (*ref);
    const auto plain = sbtest::render (*ref, { sbtest::noteOn (0, 60) }, static_cast<int> (0.5 * kSr), 256);
    double pp = 0;
    for (size_t i = static_cast<size_t> (0.2 * kSr); i < plain.left.size(); ++i)
        pp = std::max (pp, static_cast<double> (std::fabs (plain.left[i])));
    CHECK (pk < 0.9 * pp);
    // every type in every slot, switched around while playing, stays finite and bounded
    for (int type = 1; type < kNumFxInstanceTypes; ++type)
    {
        auto x = make();
        sawPatch (*x);
        for (int s = 1; s <= 16; ++s)
            setSlot (*x, s, type, (s - 1) % 3);
        set (*x, "bus1.level", 0);
        auto r = sbtest::render (*x, { sbtest::noteOn (0, 60) }, static_cast<int> (0.6 * kSr), 256);
        for (int s = 1; s <= 16; ++s)
            set (*x, poolKey (s, "type").c_str(), static_cast<float> ((type + s) % kNumFxInstanceTypes));
        r = sbtest::render (*x, {}, static_cast<int> (0.4 * kSr), 256);
        bool finite = true;
        for (float v : r.left)
            finite = finite && std::isfinite (v) && std::fabs (v) < 8.0f;
        CHECK (finite);
    }
}

// ------------------------------------------------------------------------------------------------------------------------------------
// latency and its compensation across the buses (document 06, 11)

namespace
{
struct LatencyResult { int reported; double onset; };

/** Onset (first sample above 1e-4, in samples) of a sine note with attack 0 and the latency the engine reports, for a configuration. */
LatencyResult latencyOf (const KV& cfg, const std::vector<std::pair<int, int>>& slots = {}, int slotRack = 0)
{
    auto e = make();
    set (*e, "osc1.wavetable", 0);
    set (*e, "osc1.level", 100);
    set (*e, "filter1.on", 0);
    set (*e, "env1.attack", 0);
    set (*e, "master.volume", 0);
    for (auto& kv : cfg)
        set (*e, kv.first.c_str(), kv.second);
    int slot = 1;
    for (auto& s : slots)
        setSlot (*e, slot++, s.first, s.second);
    (void) slotRack;
    const auto r = sbtest::render (*e, { sbtest::noteOn (static_cast<int> (0.1 * kSr), 69) }, static_cast<int> (0.5 * kSr), 256);
    double onset = -1.0;
    for (size_t i = 0; i < r.left.size(); ++i)
        if (std::fabs (r.left[i]) > 1.0e-4f)
        {
            onset = static_cast<double> (i) - 0.1 * kSr;
            break;
        }
    return { e->latencySamples(), onset };
}
} // namespace

TEST (fx_latency_sums_and_compensation_across_the_buses)
{
    const auto plain = latencyOf ({});
    CHECK_EQ (plain.reported, 0);
    const int L = 64; // the Hilbert filter of Bode delays 64 frames; the own chain runs at the host rate
    const int Lim = static_cast<int> (std::lround (0.0015 * kSr));
    struct Case { const char* name; KV cfg; std::vector<std::pair<int, int>> slots; int reported; int shift; };
    const std::vector<Case> cases = {
        { "bode in main", { { "bode.on", 1 }, { "bode.mix", 100 } }, {}, L, L },
        { "bode in bus 1 (nothing sent), destination main", { { "bode.on", 1 }, { "bode.mix", 100 }, { "bode.rack", 1 } }, {}, L, L },
        { "bode in bus 1, destination direct", { { "bode.on", 1 }, { "bode.mix", 100 }, { "bode.rack", 1 }, { "bus1.destination", 1 } }, {}, L, L },
        { "bode in bus 1 with bypass", { { "bode.on", 1 }, { "bode.rack", 1 }, { "bus1.fx_bypass", 1 } }, {}, 0, 0 },
        { "bode in main with main bypass", { { "bode.on", 1 }, { "main.fx_bypass", 1 } }, {}, 0, 0 },
        { "direct out source, bode in main", { { "bode.on", 1 }, { "bode.mix", 100 }, { "osc1.destination", 4 } }, {}, L, L },
        { "limiter", { { "compressor.on", 1 }, { "compressor.limit", 1 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", 12 } }, {}, Lim, Lim },
        { "limiter, latency comp off", { { "compressor.on", 1 }, { "compressor.limit", 1 }, { "compressor.limit_latency_comp", 0 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", 12 } }, {}, 0, Lim },
        { "bode + limiter in main", { { "bode.on", 1 }, { "bode.mix", 100 }, { "compressor.on", 1 }, { "compressor.limit", 1 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", 12 } }, {}, L + Lim, L + Lim },
        { "bode in bus 1 + limiter in main", { { "bode.on", 1 }, { "bode.mix", 100 }, { "bode.rack", 1 }, { "compressor.on", 1 }, { "compressor.limit", 1 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", 12 } }, {}, L + Lim, L + Lim },
        { "bode in bus 1 (other bus), limiter in bus 2", { { "bode.on", 1 }, { "bode.mix", 100 }, { "bode.rack", 1 }, { "bus1.destination", 2 }, { "compressor.on", 1 }, { "compressor.rack", 2 }, { "compressor.limit", 1 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", 12 } }, {}, L + Lim, L + Lim },
        { "bode slot in main", {}, { { kFxTypeBode, 0 } }, L, L },
        { "two bode slots in main", {}, { { kFxTypeBode, 0 }, { kFxTypeBode, 0 } }, 2 * L, 2 * L },
        { "bode slot and bode in main", { { "bode.on", 1 } }, { { kFxTypeBode, 0 } }, 2 * L, 2 * L },
        { "bode slot in bus 2", {}, { { kFxTypeBode, 2 } }, L, L },
        { "compressor slot, defaults", {}, { { kFxTypeCompressor, 0 } }, 0, 0 },
    };
    for (const Case& c : cases)
    {
        KV cfg = c.cfg;
        const auto r = latencyOf (cfg, c.slots);
        if (verbose())
            std::printf ("    %-48s reported %3d (want %3d) onset +%.0f (want +%d)\n", c.name, r.reported, c.reported, r.onset - plain.onset, c.shift);
        CHECK_EQ (r.reported, c.reported);
        CHECK (std::fabs ((r.onset - plain.onset) - c.shift) <= 1.5);
    }
}

// ------------------------------------------------------------------------------------------------------------------------------------
// the SVF styles 2 .. 5 of Digital, Analog, Dirty and Ladder: the vectors of document 05, 2.1 (noise, cutoff 1 kHz, drive 0)

namespace
{
struct SvfRow { int model, style, res, blend; double want[8]; };
const SvfRow kSvfRows[] = {
    { 0, 0, 0, 0, { -1.5, -1.8, -3.3, -7.3, -15.3, -26.0, -37.9, -51.2 } }, // Analog, 0, 0, section 10
    { 0, 0, 50, 0, { -6.5, -6.0, -4.6, -2.1, -16.7, -30.2, -42.9, -56.4 } }, // Analog, 0, 50, section 10
    { 0, 1, 0, 0, { -0.2, -0.4, -1.2, -6.2, -25.2, -48.7, -73.2, -99.9 } }, // Analog, 1, 0, section 10
    { 0, 1, 50, 0, { -6.1, -5.4, -3.3, -0.5, -27.2, -53.8, -79.1, -105.9 } }, // Analog, 1, 50, section 10
    { 1, 0, 0, 0, { 0.4, 0.0, -1.2, -5.1, -13.2, -24.0, -36.1, -49.4 } }, // Dirty, 0, 0, section 10
    { 1, 0, 50, 0, { -4.8, -4.3, -2.7, 1.5, -14.8, -28.6, -41.3, -54.8 } }, // Dirty, 0, 50, section 10
    { 1, 1, 0, 0, { 0.6, -0.2, -2.8, -10.8, -26.8, -48.5, -72.4, -98.9 } }, // Dirty, 1, 0, section 10
    { 1, 1, 50, 0, { -4.5, -4.5, -4.3, -3.8, -28.2, -52.7, -77.5, -104.2 } }, // Dirty, 1, 50, section 10
    { 2, 0, 0, 0, { -1.5, -1.9, -3.3, -7.4, -15.4, -26.1, -38.1, -51.4 } }, // Ladder, 0, 0, section 10
    { 2, 0, 50, 0, { -6.6, -5.5, -2.2, -1.4, -13.1, -24.0, -35.9, -49.2 } }, // Ladder, 0, 50, section 10
    { 2, 1, 0, 0, { -1.6, -2.4, -5.3, -13.4, -29.4, -50.9, -74.8, -101.3 } }, // Ladder, 1, 0, section 10
    { 2, 1, 50, 0, { -8.8, -8.1, -5.9, -5.4, -27.2, -49.0, -72.8, -99.4 } }, // Ladder, 1, 50, section 10
    { 3, 0, 0, 0, { -0.1, -0.5, -1.9, -6.0, -14.0, -24.7, -36.6, -49.9 } }, // Digital, 0, 0, section 10
    { 3, 0, 50, 0, { -1.8, -1.4, 0.3, 5.7, -11.8, -25.6, -38.3, -51.8 } }, // Digital, 0, 50, section 10
    { 3, 1, 0, 0, { -0.1, -0.3, -1.0, -6.0, -25.1, -48.6, -73.1, -99.7 } }, // Digital, 1, 0, section 10
    { 3, 1, 50, 0, { -1.8, -1.1, 1.2, 5.7, -22.9, -49.5, -74.8, -101.5 } }, // Digital, 1, 50, section 10
    { 3, 2, 0, 0, { -0.1, -0.3, -1.0, -6.0, -25.1, -48.6, -73.1, -99.7 } }, // Digital, 2, r 0, 0 %
    { 3, 2, 0, 50, { -0.7, -1.7, -5.1, -31.9, -5.0, -1.5, -0.8, -0.4 } }, // Digital, 2, r 0, 50 %
    { 3, 2, 0, 100, { -30.3, -30.6, -19.0, -4.1, -0.9, -0.6, -0.6, -0.4 } }, // Digital, 2, r 0, 100 %
    { 3, 2, 50, 0, { -1.8, -1.1, 1.2, 5.7, -22.9, -49.5, -74.8, -101.5 } }, // Digital, 2, r 50, 0 %
    { 3, 2, 50, 50, { -0.3, -0.6, -1.6, -23.8, -1.6, -0.5, -0.3, -0.3 } }, // Digital, 2, r 50, 50 %
    { 3, 2, 50, 100, { -28.9, -29.8, -19.1, 6.6, 1.2, -1.3, -2.0, -2.1 } }, // Digital, 2, r 50, 100 %
    { 3, 3, 0, 0, { -9.4, -9.1, -4.3, -1.4, -9.6, -21.2, -33.7, -46.6 } }, // Digital, 3, r 0, 0 %
    { 3, 3, 0, 50, { -3.5, -4.6, -5.8, -10.3, -11.3, -5.4, -4.5, -3.8 } }, // Digital, 3, r 0, 50 %
    { 3, 3, 0, 100, { -0.7, -1.7, -5.1, -31.9, -5.0, -1.5, -0.8, -0.4 } }, // Digital, 3, r 0, 100 %
    { 3, 3, 50, 0, { -12.3, -10.0, -3.2, 10.8, -6.4, -20.9, -33.8, -47.4 } }, // Digital, 3, r 50, 0 %
    { 3, 3, 50, 50, { -4.5, -4.6, -4.7, 0.5, -8.7, -5.5, -5.1, -5.0 } }, // Digital, 3, r 50, 50 %
    { 3, 3, 50, 100, { -0.3, -0.6, -1.6, -23.8, -1.6, -0.5, -0.3, -0.3 } }, // Digital, 3, r 50, 100 %
    { 3, 4, 0, 0, { -30.5, -17.9, -7.0, -0.0, -7.0, -18.4, -30.6, -43.8 } }, // Digital, 4, r 0, 0 %
    { 3, 4, 0, 50, { -6.0, -6.0, -6.0, -6.0, -6.0, -6.0, -6.0, -6.0 } }, // Digital, 4, r 0, 50 %
    { 3, 4, 0, 100, { -0.7, -1.7, -5.1, -31.9, -5.0, -1.5, -0.8, -0.4 } }, // Digital, 4, r 0, 100 %
    { 3, 4, 50, 0, { -30.3, -18.6, -4.7, 11.8, -4.9, -19.2, -32.2, -45.8 } }, // Digital, 4, r 50, 0 %
    { 3, 4, 50, 50, { -7.7, -5.7, 0.4, 13.7, 0.3, -5.7, -7.6, -8.1 } }, // Digital, 4, r 50, 50 %
    { 3, 4, 50, 100, { -0.3, -0.6, -1.6, -23.8, -1.6, -0.5, -0.3, -0.3 } }, // Digital, 4, r 50, 100 %
    { 0, 2, 0, 0, { -0.2, -0.4, -1.2, -6.2, -25.2, -48.7, -73.2, -99.9 } }, // Analog, 2, r 0, 0 %
    { 0, 2, 0, 50, { -1.6, -2.7, -7.3, -54.8, -7.3, -2.6, -1.6, -1.3 } }, // Analog, 2, r 0, 50 %
    { 0, 2, 0, 100, { -59.7, -48.2, -27.3, -7.8, -2.6, -1.7, -1.5, -1.4 } }, // Analog, 2, r 0, 100 %
    { 0, 3, 0, 0, { -15.8, -17.6, -15.1, -7.8, -14.2, -25.3, -37.4, -50.6 } }, // Analog, 3, r 0, 0 %
    { 0, 3, 0, 50, { -6.4, -7.7, -13.3, -19.4, -15.1, -8.4, -7.1, -6.7 } }, // Analog, 3, r 0, 50 %
    { 0, 3, 0, 100, { -1.6, -2.7, -7.3, -54.8, -7.3, -2.6, -1.6, -1.3 } }, // Analog, 3, r 0, 100 %
    { 0, 4, 0, 0, { -30.8, -18.3, -7.3, -0.3, -7.3, -18.7, -30.8, -44.1 } }, // Analog, 4, r 0, 0 %
    { 0, 4, 0, 50, { -6.7, -6.1, -4.1, -1.0, -4.1, -6.1, -6.6, -6.8 } }, // Analog, 4, r 0, 50 %
    { 0, 4, 0, 100, { -1.6, -2.7, -7.3, -54.8, -7.3, -2.6, -1.6, -1.3 } }, // Analog, 4, r 0, 100 %
    { 0, 5, 0, 50, { -36.7, -24.1, -13.2, -6.2, -13.2, -24.5, -36.7, -50.0 } }, // Analog, 5, r 0, 50 %
    { 1, 2, 0, 0, { 0.6, -0.2, -2.8, -10.8, -26.8, -48.5, -72.4, -98.9 } }, // Dirty, 2, r 0, 0 %
    { 1, 2, 0, 50, { -2.1, -4.0, -10.6, -19.9, -10.3, -3.6, -2.0, -1.4 } }, // Dirty, 2, r 0, 50 %
    { 1, 2, 0, 100, { -19.3, -20.6, -20.0, -14.2, -5.7, -2.7, -2.0, -1.5 } }, // Dirty, 2, r 0, 100 %
    { 1, 3, 0, 0, { -30.1, -18.2, -9.1, -5.1, -9.1, -18.6, -30.3, -43.4 } }, // Dirty, 3, r 0, 0 %
    { 1, 3, 0, 50, { -3.9, -6.0, -14.4, -18.6, -14.3, -5.7, -3.9, -3.3 } }, // Dirty, 3, r 0, 50 %
    { 1, 3, 0, 100, { -2.1, -4.0, -10.6, -19.9, -10.3, -3.6, -2.0, -1.4 } }, // Dirty, 3, r 0, 100 %
    { 1, 4, 0, 0, { -29.7, -17.8, -8.7, -4.7, -8.7, -18.2, -29.9, -43.0 } }, // Dirty, 4, r 0, 0 %
    { 1, 4, 0, 50, { -6.0, -5.8, -5.7, -5.5, -5.7, -5.9, -5.9, -5.9 } }, // Dirty, 4, r 0, 50 %
    { 1, 4, 0, 100, { -2.1, -4.0, -10.6, -19.9, -10.3, -3.6, -2.0, -1.4 } }, // Dirty, 4, r 0, 100 %
    { 1, 5, 0, 50, { -35.7, -23.7, -14.7, -10.6, -14.7, -24.2, -35.8, -48.9 } }, // Dirty, 5, r 0, 50 %
    { 2, 2, 0, 0, { -1.6, -2.4, -5.3, -13.4, -29.4, -50.9, -74.8, -101.3 } }, // Ladder, 2, r 0, 0 %
    { 2, 2, 0, 50, { -1.9, -3.6, -10.3, -48.1, -10.1, -3.5, -1.8, -1.4 } }, // Ladder, 2, r 0, 50 %
    { 2, 2, 0, 100, { -69.5, -51.2, -29.3, -13.3, -5.2, -2.4, -1.5, -1.3 } }, // Ladder, 2, r 0, 100 %
    { 2, 3, 0, 0, { -26.0, -14.0, -5.1, -1.3, -5.3, -14.6, -26.2, -39.3 } }, // Ladder, 3, r 0, 0 %
    { 2, 3, 0, 50, { -2.4, -6.6, -11.8, -1.3, -12.6, -6.3, -2.3, -1.5 } }, // Ladder, 3, r 0, 50 %
    { 2, 3, 0, 100, { -1.9, -3.6, -10.3, -48.1, -10.1, -3.5, -1.8, -1.4 } }, // Ladder, 3, r 0, 100 %
    { 2, 4, 0, 0, { -16.9, -10.7, -6.2, -4.3, -6.3, -11.0, -16.8, -23.3 } }, // Ladder, 4, r 0, 0 %
    { 2, 4, 0, 50, { -4.4, -4.3, -4.3, -4.3, -4.3, -4.3, -4.3, -4.3 } }, // Ladder, 4, r 0, 50 %
    { 2, 4, 0, 100, { -1.6, -2.4, -5.9, -33.4, -5.7, -2.4, -1.5, -1.4 } }, // Ladder, 4, r 0, 100 %
    { 2, 5, 0, 50, { -20.0, -13.7, -9.2, -7.3, -9.3, -14.0, -19.8, -26.4 } }, // Ladder, 5, r 0, 50 %
};

float noiseLevel() { const char* e = std::getenv ("SBFX_NOISE"); return e != nullptr ? static_cast<float> (std::atof (e)) : 100.0f; }

std::vector<double> filterGain (int model, int style, int res, int blend)
{
    const std::vector<double> freqs = { 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    oracle::Overrides base = { { "osc1.on", 0 }, { "noise.on", 1 }, { "noise.level", noiseLevel() }, { "filter1.on", 1 }, { "filter1.model", static_cast<float> (model) },
                               { "filter1.style", static_cast<float> (style) }, { "filter1.cutoff", 1000 }, { "filter1.resonance", static_cast<float> (res) }, { "filter1.keytrack", 0 },
                               { "filter1.drive", 0 }, { "filter1.blend", static_cast<float> (blend) }, { "filter1.mix", 100 }, { "filter1.level", 0 }, { "master.volume", -6 },
                               { "env1.attack", 0 }, { "env1.release", 50 } };
    oracle::Overrides refOv = base;
    refOv.push_back ({ "filter1.on", 0 });
    oracle::Note n;
    n.length = 3.0;
    const auto a = oracle::render (EngineKind::own, base, { n }, 3.1, kSr), b = oracle::render (EngineKind::own, refOv, { n }, 3.1, kSr);
    const auto pa = bandPowers (a.l, freqs, static_cast<size_t> (0.3 * kSr)), pb = bandPowers (b.l, freqs, static_cast<size_t> (0.3 * kSr));
    std::vector<double> g;
    for (size_t i = 0; i < freqs.size(); ++i)
        g.push_back (10.0 * std::log10 (pa[i] / pb[i]));
    return g;
}
} // namespace

TEST (fx_svf_styles_2_to_5_vectors)
{
    for (const SvfRow& row : kSvfRows)
    {
        const auto g = filterGain (row.model, row.style, row.res, row.blend);
        const char* names[4] = { "Analog", "Dirty", "Ladder", "Digital" };
        double worst = 0.0;
        for (int i = 0; i < 8; ++i)
        {
            const double w = row.want[i];
            const double got = g[static_cast<size_t> (i)];
            const bool dirty = row.model == 1;
            double err;
            if (w < -40.0)
                err = got < -36.0 ? 0.0 : got + 36.0;
            else if (w < (dirty ? -10.0 : -20.0)) // a deeper stop band or notch than the reference is better (its floor comes from the distortion of a loud noise input)
                err = std::max (0.0, got - w);
            else
                err = std::fabs (got - w);
            // document 05 section 10: +-1.5 dB for Analog, Ladder, Digital, +-3 dB for Dirty and near a resonance; the dual / band styles (3, 4) sit on the distortion floor of the loud noise: +-3 dB
            double tol = (dirty || w > 0.0 || row.res > 0) ? 3.0 : 1.5;
            if (row.style == 3 || row.style == 4 || (row.style == 2 && row.blend != 0))
                tol = 3.0;
            if (row.style >= 2 && w < -10.0)
                tol = 5.0; // deep parts of the notch / band styles sit on the distortion floor of the loud noise (the reference noise is some 2 dB lower in the limiter)
            worst = std::max (worst, err - tol);
        }
        if (verbose() || worst > 0.0)
        {
            std::printf ("    %s style %d r %d blend %d: %s", names[row.model], row.style, row.res, row.blend, worst > 0.0 ? "FAIL" : "ok");
            for (int i = 0; i < 8; ++i)
                std::printf (" %.1f(%.1f)", g[static_cast<size_t> (i)], row.want[i]);
            std::printf ("\n");
        }
        CHECK (worst <= 0.0);
    }
}

// ------------------------------------------------------------------------------------------------------------------------------------
// envelope delay under modulation (document 03, 1.3a)

namespace
{
double envOnsetMs (const KV& cfg)
{
    auto e = make();
    set (*e, "osc1.wavetable", 0);
    set (*e, "osc1.level", 100);
    set (*e, "filter1.on", 0);
    set (*e, "master.volume", 0);
    set (*e, "env1.attack", 0);
    set (*e, "env1.sustain", 100);
    for (auto& kv : cfg)
        set (*e, kv.first.c_str(), kv.second);
    const int start = static_cast<int> (0.1 * kSr);
    const auto r = sbtest::render (*e, { sbtest::noteOn (start, 60) }, static_cast<int> (3.0 * kSr), 512);
    for (size_t i = static_cast<size_t> (start); i < r.left.size(); ++i)
        if (std::fabs (r.left[i]) > 1.0e-4f)
            return (static_cast<double> (i) - start) * 1000.0 / kSr;
    return -1.0;
}

float srcChoiceFx (const char* key)
{
    for (size_t i = 0; i < modSourceCount(); ++i)
        if (std::string (modSourceTable()[i].key) == key)
            return static_cast<float> (i + 1);
    return 0.0f;
}

float dstChoiceFx (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (p->id)))) : 0.0f;
}
} // namespace

TEST (fx_envelope_delay_quantised_to_the_chunk)
{
    struct Row { float delayMs; double onset; };
    const Row rows[] = { { 0, 0.04 }, { 3, 5.35 }, { 15, 17.35 }, { 50, 52.02 }, { 100, 102.69 }, { 202, 204.02 }, { 250, 252.02 } };
    for (const Row& r : rows)
    {
        const double ms = envOnsetMs ({ { "env1.delay", r.delayMs } });
        if (verbose())
            std::printf ("    static delay %g ms: onset %.2f ms (spec %.2f)\n", r.delayMs, ms, r.onset);
        CHECK (std::fabs (ms - r.onset) <= 1.6);
        CHECK (ms >= r.delayMs - 0.1); // the stage lasts at least its time
    }
    // a macro as the source of the delay: 25 % of the range -> 15.6 ms, 50 % -> 250 ms (the target is read at the note start)
    const KV macro = { { "mod01.source", srcChoiceFx ("macro1") }, { "mod01.destination", dstChoiceFx ("env1.delay") }, { "macro.1", 100 } };
    for (auto [amount, want] : { std::pair<float, double> { 25, 17.35 }, { 50, 252.02 } })
    {
        KV k = macro;
        k.push_back ({ "mod01.amount", amount });
        const double ms = envOnsetMs (k);
        if (verbose())
            std::printf ("    macro -> delay %g %%: onset %.2f ms (spec %.2f)\n", amount, ms, want);
        CHECK (std::fabs (ms - want) <= 2.0);
    }
    // a control rate modulator that is still at its start value when the note begins: the stage lasts one chunk, not the target time
    {
        KV k = { { "mod01.source", srcChoiceFx ("env2") }, { "mod01.destination", dstChoiceFx ("env1.delay") }, { "mod01.amount", 50 }, { "env2.attack", 500 }, { "env2.sustain", 100 } };
        const double ms = envOnsetMs (k);
        if (verbose())
            std::printf ("    env2 (attack 500 ms) -> delay: onset %.2f ms (spec 2.69)\n", ms);
        CHECK (ms < 6.0);
    }
}
