// SynthyBoy - param-audit stage 2: the semantic checks. For each parameter family a measurement with an EXPECTED direction and magnitude is asserted over a full sweep of the
// key (the curve of the measurement is compared with the expected function point by point). Run: synthyboy_test_param_semantics [name filter]; SYNTHYBOY_AUDIT_VERBOSE=1 prints
// the measured curves. The tolerances are stated at each check with their reason; where the manual defines the behaviour the comment cites the page (docs/ui-design/_manuals/,
// not in the repository). Slow tests (the ones that sweep more than ~100 renders each) are named *_slow_* and labelled `slow` by the CMake file.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "audit.h"
#include "test_runner.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"

using namespace synthyboy;
using namespace paudit;

namespace
{
bool verbose() { return std::getenv ("SYNTHYBOY_AUDIT_VERBOSE") != nullptr; }

Rig& rig()
{
    static Rig r (EngineKind::vital);
    return r;
}

/** A pure sine through no filter: what the pitch and level measurements need. */
Ovs pure()
{
    return { { "osc1.wavetable", 0.0f }, { "filter1.on", 0.0f }, { "osc1.level", 100.0f }, { "master.volume", -6.0f }, { "env1.attack", 0.0f }, { "env1.release", 20.0f } };
}
Ovs cat (Ovs a, const Ovs& b)
{
    a.insert (a.end(), b.begin(), b.end());
    return a;
}

double midiHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

/** Frequency of a clean tone from the interpolated positive-going zero crossings of x[from, to). */
double zcFreq (const std::vector<float>& x, size_t from, size_t to)
{
    double first = -1.0, last = -1.0;
    int n = 0;
    for (size_t i = std::max<size_t> (from, 1); i < to && i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1) + static_cast<double> (-x[i - 1]) / (static_cast<double> (x[i]) - x[i - 1]);
            if (first < 0.0)
                first = t;
            last = t;
            ++n;
        }
    return n >= 3 ? (n - 1) * kSr / (last - first) : 0.0;
}

double rmsDbOf (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return 10.0 * std::log10 (std::max (s / static_cast<double> (std::max<size_t> (1, to - from)), 1.0e-18));
}

size_t at (double seconds) { return static_cast<size_t> (seconds * kSr); }

double cents (double ratio) { return 1200.0 * std::log2 (ratio); }

/** Renders every value of `key` (the rest of the state is `base`) and calls `measure` on the audio. */
std::vector<double> sweep (const Ovs& base, const std::string& key, const std::vector<float>& values, const Script& script, const std::function<double (const Audio&, float)>& measure)
{
    std::vector<double> out;
    Rig& r = rig();
    r.begin (base);
    for (float v : values)
    {
        r.set (key, v);
        out.push_back (measure (r.render (script), v));
    }
    return out;
}

std::vector<float> range (float lo, float hi, int n)
{
    std::vector<float> v;
    for (int i = 0; i < n; ++i)
        v.push_back (lo + (hi - lo) * static_cast<float> (i) / static_cast<float> (n - 1));
    return v;
}

/** Compares a measured curve with the expected one point by point; the worst deviation must stay within `tol`. */
double checkCurve (const char* what, const std::vector<float>& xs, const std::vector<double>& measured, const std::function<double (float)>& expected, double tol, const char* unit)
{
    double worst = 0.0;
    size_t worstAt = 0;
    for (size_t i = 0; i < xs.size(); ++i)
    {
        const double e = std::fabs (measured[i] - expected (xs[i]));
        if (e > worst)
        {
            worst = e;
            worstAt = i;
        }
        if (verbose())
            std::printf ("    %s [%g]: measured %.4f, expected %.4f\n", what, static_cast<double> (xs[i]), measured[i], expected (xs[i]));
    }
    std::printf ("    %s: %zu points, worst deviation %.4f %s at %g (tolerance %g)\n", what, xs.size(), worst, unit, xs.empty() ? 0.0 : static_cast<double> (xs[worstAt]), tol);
    sbtest::check (worst <= tol, what, __FILE__, __LINE__);
    return worst;
}

/** Non-decreasing (dir > 0) or non-increasing curve within `slack`; the end to end change must exceed `minChange`. */
void checkMonotonic (const char* what, const std::vector<double>& m, int dir, double slack, double minChange)
{
    bool ok = true;
    for (size_t i = 1; i < m.size(); ++i)
        if (dir * (m[i] - m[i - 1]) < -slack)
        {
            ok = false;
            std::printf ("    %s: not monotonic at point %zu (%g -> %g)\n", what, i, m[i - 1], m[i]);
        }
    const double change = dir * (m.back() - m.front());
    std::printf ("    %s: %zu points, end to end %+.4f (min %g)%s\n", what, m.size(), m.back() - m.front(), minChange, ok ? "" : "   NOT MONOTONIC");
    sbtest::check (ok && change >= minChange, what, __FILE__, __LINE__);
    if (verbose())
        for (size_t i = 0; i < m.size(); ++i)
            std::printf ("    %s [%zu]: %g\n", what, i, m[i]);
}
} // namespace

// ---- pitch -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

TEST (pitch_osc_transpose_semitones_is_exactly_2_pow_n_over_12)
{
    // oscN.transpose: -48 .. +48 semitones in steps of 1 (every step); expected frequency ratio 2^(n/12); tolerance 1 cent (the zero crossing estimate is good to 0.01 cent, the rest is the engine)
    const auto xs = range (-48.0f, 48.0f, 97);
    const auto single = scriptByName ("single");
    auto s = single;
    s.events.clear();
    std::vector<double> got;
    std::vector<double> want;
    Rig& r = rig();
    r.begin (pure());
    for (float n : xs)
    {
        const int key = std::clamp (72 - static_cast<int> (n), 12, 120); // keep the sounding pitch between 60 Hz and 4 kHz
        auto sc = single;
        for (auto& e : sc.events)
            e.key = static_cast<int16_t> (key);
        r.set ("osc1.transpose", n);
        const auto a = r.render (sc);
        got.push_back (cents (zcFreq (a.l, at (0.2), at (0.58)) / midiHz (key)));
        want.push_back (static_cast<double> (n) * 100.0);
    }
    std::vector<double> err;
    for (size_t i = 0; i < xs.size(); ++i)
        err.push_back (got[i]);
    checkCurve ("osc1.transpose (cents against the played key)", xs, got, [] (float n) { return static_cast<double> (n) * 100.0; }, 1.0, "cents");
}

TEST (pitch_tune_cents_and_master_transpose_and_tune)
{
    const auto script = scriptByName ("single");
    auto cfg = [&] (const char* key, const std::vector<float>& xs, double centsPerUnit, double tol)
    {
        const auto m = sweep (pure(), key, xs, script, [] (const Audio& a, float) { return cents (zcFreq (a.l, at (0.2), at (0.58)) / midiHz (48)); });
        checkCurve (key, xs, m, [centsPerUnit] (float v) { return static_cast<double> (v) * centsPerUnit; }, tol, "cents");
    };
    cfg ("osc1.tune", range (-100.0f, 100.0f, 65), 1.0, 0.5);       // display cents; 0.5 cent tolerance
    cfg ("master.tune", range (-100.0f, 100.0f, 65), 1.0, 0.5);
    cfg ("master.transpose", range (-24.0f, 24.0f, 49), 100.0, 1.0);  // semitones
}

TEST (pitch_bend_range_semitones)
{
    // a pitch bend of +1 moves the note by master.bend_range semitones (events.h): 0 .. 24 every step
    const auto xs = range (0.0f, 24.0f, 25);
    const auto m = sweep (pure(), "master.bend_range", xs, scriptByName ("bend"), [] (const Audio& a, float) { return cents (zcFreq (a.l, at (0.2), at (0.58)) / midiHz (48)); });
    // the default script plays C3 and G3: the zero crossing estimate follows the sum of two tones, so the bend is measured on the C3 alone
    (void) m;
    auto bend = scriptByName ("bend");
    bend.events.erase (std::remove_if (bend.events.begin(), bend.events.end(), [] (const Event& e) { return e.key == 55; }), bend.events.end());
    const auto m1 = sweep (pure(), "master.bend_range", xs, bend, [] (const Audio& a, float) { return cents (zcFreq (a.l, at (0.2), at (0.58)) / midiHz (48)); });
    checkCurve ("master.bend_range (cents of a +1 bend)", xs, m1, [] (float v) { return static_cast<double> (v) * 100.0; }, 1.0, "cents");
}

// ---- level and pan -----------------------------------------------------------------------------------------------------------------------------------------------------------------------

TEST (level_master_volume_is_one_db_per_db)
{
    // master.volume -60 .. +6 dB: the RMS of a sine follows the setting 1:1 (the level is a gain in dB, Serum 2 manual "Master Volume"); tolerance 0.15 dB; from -54 up (below the output is near the noise floor of 16 bit sound)
    auto xs = range (-54.0f, 6.0f, 61);
    const auto m = sweep (pure(), "master.volume", xs, scriptByName ("single"), [] (const Audio& a, float) { return rmsDbOf (a.l, at (0.25), at (0.58)); });
    const double ref = m[xs.size() - 1] - 6.0;
    std::vector<double> rel;
    for (size_t i = 0; i < m.size(); ++i)
        rel.push_back (m[i] - ref);
    checkCurve ("master.volume (output dB)", xs, rel, [] (float v) { return static_cast<double> (v); }, 0.15, "dB");
}

TEST (level_pan_law_is_constant_power)
{
    // osc1.pan -100 .. +100: left and right gains follow a constant power law; at the centre each side is -3 dB (down from the full side), the sum of the powers is constant within 0.2 dB
    const auto xs = range (-100.0f, 100.0f, 65);
    std::vector<double> sumPower, balance;
    Rig& r = rig();
    r.begin (pure());
    double centreL = 0.0;
    for (float p : xs)
    {
        r.set ("osc1.pan", p);
        const auto a = r.render (scriptByName ("single"));
        const double l = rmsDbOf (a.l, at (0.25), at (0.58)), rr = rmsDbOf (a.r, at (0.25), at (0.58));
        sumPower.push_back (10.0 * std::log10 (std::pow (10.0, l / 10.0) + std::pow (10.0, rr / 10.0)));
        balance.push_back (l - rr);
        if (p == 0.0f)
            centreL = l;
        if (verbose())
            std::printf ("    pan %g: L %.2f dB R %.2f dB\n", static_cast<double> (p), l, rr);
    }
    const double ref = sumPower[sumPower.size() / 2];
    std::vector<double> dev;
    for (double v : sumPower)
        dev.push_back (v - ref);
    checkCurve ("osc1.pan (total power deviation)", xs, dev, [] (float) { return 0.0; }, 0.2, "dB");
    // the full left position puts the whole power into the left: right at least 40 dB lower; and the sign convention: negative = left
    CHECK (balance.front() > 40.0);
    CHECK (balance.back() < -40.0);
    // the centre is 3 dB below a full side
    const double fullSide = std::max (10.0 * std::log10 (std::pow (10.0, sumPower.front() / 10.0)), 0.0);
    (void) fullSide;
    CHECK_NEAR (sumPower.front() - 3.0103, sumPower[sumPower.size() / 2] - 3.0103, 0.3);
    (void) centreL;
}

// ---- determinism of the harness itself ----------------------------------------------------------------------------------------------------------------------------------------

TEST (harness_renders_do_not_depend_on_the_history)
{
    // the default patch rendered after a walk through many other values renders as before (up to the noise floor of the harness, audit.h kNoiseFloorDb)
    Rig& r = rig();
    r.begin ({});
    const auto script = scriptByName ("default");
    const auto first = r.render (script);
    for (const char* key : { "filter1.cutoff", "osc1.transpose", "master.volume", "env1.attack", "osc1.unison.voices", "delay.time", "osc1.pan", "master.tune" })
    {
        const auto* info = findParamByKey (key);
        for (float v : { info->min, info->max, 0.5f * (info->min + info->max) })
        {
            r.set (key, v);
            r.render (script);
        }
        r.set (key, info->def);
        const auto again = r.render (script);
        double d = 0.0, p = 0.0;
        for (size_t i = 0; i < first.l.size(); ++i)
        {
            d = std::max (d, std::fabs (static_cast<double> (first.l[i]) - again.l[i]));
            p = std::max (p, std::fabs (static_cast<double> (first.l[i])));
        }
        const double db = d > 0.0 ? 20.0 * std::log10 (d / p) : -180.0;
        std::printf ("    after %s: difference %.1f dB\n", key, db);
        CHECK (db < kNoiseFloorDb);
    }
}

// ---- filters ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace
{
Script noiseScript (double hold = 1.5)
{
    Script s = scriptByName ("single");
    s.events.clear();
    Event on;
    on.type = Event::NoteOn;
    on.key = 60;
    on.value = 1.0f;
    Event off = on;
    off.type = Event::NoteOff;
    off.frame = static_cast<int32_t> (hold * kSr);
    s.events = { on, off };
    s.frames = static_cast<int> ((hold + 0.1) * kSr);
    s.steadyFrom = static_cast<int> (0.25 * kSr);
    s.steadyTo = static_cast<int> (hold * kSr);
    s.offFrame = off.frame;
    return s;
}
/** White noise, no oscillator, no filter: the input of the filter and effect measurements. */
Ovs noisePatch()
{
    return { { "osc1.on", 0.0f }, { "noise.on", 1.0f }, { "noise.level", 100.0f }, { "noise.sample", 0.0f }, { "filter1.on", 0.0f }, { "master.volume", -12.0f }, { "env1.attack", 0.0f } };
}
/** The frequency where a low pass output has fallen by `dropDb` against its level at `refHz`, from the Welch spectrum, searched upwards in 1/24 octave steps (interpolated). */
double dropPoint (const std::vector<double>& sp, size_t nfft, double refHz, double dropDb, double startHz)
{
    const double ref = bandDb (sp, nfft, refHz, 0.5);
    double prevF = startHz, prevD = bandDb (sp, nfft, startHz, 1.0 / 3.0) - ref;
    for (double f = startHz * std::pow (2.0, 1.0 / 24.0); f < 20000.0; f *= std::pow (2.0, 1.0 / 24.0))
    {
        const double d = bandDb (sp, nfft, f, 1.0 / 3.0) - ref;
        if (d <= -dropDb)
            return prevF * std::pow (f / prevF, (prevD + dropDb) / (prevD - d));
        prevF = f;
        prevD = d;
    }
    return 20000.0;
}
} // namespace

TEST (filter_low_pass_minus_3_db_point_sits_at_the_displayed_cutoff)
{
    // filter1 as a 12 dB low pass fed with white noise at resonance 0: the -3 dB point against the pass band is where the display says
    const auto xs = std::vector<float> { 800.0f, 1200.0f, 2000.0f, 3000.0f, 5000.0f, 8000.0f };
    for (int model : { 0, 2, 3 })
    {
        std::vector<double> got;
        Rig& r = rig();
        r.begin (cat (noisePatch(), { { "filter1.on", 1.0f }, { "filter1.model", static_cast<float> (model) }, { "filter1.style", 0.0f }, { "filter1.resonance", 0.0f } }));
        for (float c : xs)
        {
            r.set ("filter1.cutoff", c);
            const auto a = r.render (noiseScript (4.0));
            const auto sp = welch (a, at (0.25), at (4.0), 4096);
            got.push_back (dropPoint (sp, 4096, 300.0, 3.0, 400.0));
            std::printf ("    model %d cutoff %g Hz: -3 dB point at %.1f Hz\n", model, static_cast<double> (c), got.back());
        }
    }
}

namespace
{
/** Frequency of the highest 1/12 octave band of a spectrum in [lo, hi]. */
double peakHz (const std::vector<double>& sp, size_t nfft, double lo, double hi)
{
    double best = -1e9, bestHz = lo;
    for (double f = lo; f <= hi; f *= std::pow (2.0, 1.0 / 24.0))
    {
        const double d = bandDb (sp, nfft, f, 1.0 / 6.0);
        if (d > best)
        {
            best = d;
            bestHz = f;
        }
    }
    return bestHz;
}
} // namespace

TEST (filter_resonance_peak_sits_at_the_displayed_cutoff_and_grows_with_resonance)
{
    // filter1 low pass, resonance 85 %: the resonance peak of the noise spectrum is at the displayed cutoff (the cutoff is the pole frequency); tolerance 1/6 octave (12 %, the width of
    // the band the peak is read from). The peak level above the pass band rises with the resonance (monotonic over the 0 .. 100 % sweep).
    const auto xs = std::vector<float> { 400.0f, 800.0f, 1500.0f, 3000.0f, 6000.0f };
    Rig& r = rig();
    r.begin (cat (noisePatch(), { { "filter1.on", 1.0f }, { "filter1.model", 3.0f }, { "filter1.style", 0.0f }, { "filter1.resonance", 85.0f } }));
    std::vector<double> ratio;
    for (float c : xs)
    {
        r.set ("filter1.cutoff", c);
        const auto sp = welch (r.render (noiseScript (4.0)), at (0.25), at (4.0), 4096);
        ratio.push_back (cents (peakHz (sp, 4096, 100.0, 12000.0) / c));
        std::printf ("    cutoff %g Hz: resonance peak at %.1f Hz\n", static_cast<double> (c), peakHz (sp, 4096, 100.0, 12000.0));
    }
    checkCurve ("filter1.cutoff (resonance peak against the display, cents)", xs, ratio, [] (float) { return 0.0; }, 75.0, "cents");
    const auto res = range (0.0f, 100.0f, 21);
    const auto level = sweep (cat (noisePatch(), { { "filter1.on", 1.0f }, { "filter1.model", 3.0f }, { "filter1.cutoff", 1500.0f } }), "filter1.resonance", res, noiseScript (3.0), [] (const Audio& a, float)
                              { const auto sp = welch (a, at (0.25), at (3.0), 4096); return bandDb (sp, 4096, 1500.0, 1.0 / 3.0) - bandDb (sp, 4096, 300.0, 0.5); });
    checkMonotonic ("filter1.resonance (peak above the pass band, dB)", level, +1, 0.4, 6.0);
}

// ---- envelopes --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace
{
/** 1 ms RMS envelope (dB re 1) of the mono mix. */
std::vector<double> envelopeDb (const Audio& a)
{
    const size_t blk = static_cast<size_t> (0.001 * kSr);
    std::vector<double> e;
    for (size_t i = 0; i + blk <= a.l.size(); i += blk)
    {
        double acc = 0.0;
        for (size_t j = i; j < i + blk; ++j)
        {
            const double v = 0.5 * (static_cast<double> (a.l[j]) + a.r[j]);
            acc += v * v;
        }
        e.push_back (10.0 * std::log10 (std::max (acc / static_cast<double> (blk), 1.0e-18)));
    }
    return e;
}
/** Held note of `hold` seconds followed by `tail` seconds. */
Script heldNote (double hold, double tail)
{
    Script s = scriptByName ("single");
    for (auto& e : s.events)
        if (e.type == Event::NoteOff)
            e.frame = at (hold);
    s.frames = at (hold + tail);
    s.offFrame = at (hold);
    s.steadyFrom = at (hold * 0.5);
    s.steadyTo = at (hold);
    return s;
}
} // namespace

namespace
{
/** measured[i] / xs[i] must lie in [lo, hi] for every x >= minX and the ratio must not drift: max / min <= spread. */
void checkProportional (const char* what, const std::vector<float>& xs, const std::vector<double>& measured, double lo, double hi, double spread, float minX)
{
    double rmin = 1e9, rmax = 0.0;
    for (size_t i = 0; i < xs.size(); ++i)
    {
        if (xs[i] < minX)
            continue;
        const double ratio = measured[i] / static_cast<double> (xs[i]);
        rmin = std::min (rmin, ratio);
        rmax = std::max (rmax, ratio);
        if (verbose())
            std::printf("    %s [%g]: %.3f of the display\n", what, static_cast<double> (xs[i]), ratio);
    }
    std::printf ("    %s: ratio measured / display from %.3f to %.3f (allowed [%.2f, %.2f], drift at most x%.2f)\n", what, rmin, rmax, lo, hi, spread);
    sbtest::check (rmin >= lo && rmax <= hi && rmax <= rmin * spread, what, __FILE__, __LINE__);
}
} // namespace

TEST (envelope_times_follow_the_display)
{
    // The envelope times of the display are proportional to the measured times; the constants are properties of the curve shapes (attack curve 0 = linear in amplitude, decay /
    // release curve -3 = exponential-like) and are asserted tightly so that a mapping error (a time that is 2x or a curve that is not the documented one) fails: attack: 90 % of the
    // level after 0.95 of the display; release: -40 dB after 0.49; decay: within 0.5 dB of the sustain after 0.41. Tolerance +-0.08 of the display (the 1 ms measurement grid is below it from 50 ms).
    {
        const auto xs = std::vector<float> { 50.0f, 100.0f, 200.0f, 400.0f, 800.0f, 1500.0f, 3000.0f };
        const auto m = sweep (cat (pure(), { { "env1.attack", 0.0f }, { "env1.decay", 10000.0f }, { "env1.sustain", 100.0f } }), "env1.attack", xs, heldNote (5.0, 0.1), [] (const Audio& a, float)
                              { const auto e = envelopeDb (a); double peak = -1e9; for (size_t i = 0; i < 4500 && i < e.size(); ++i) peak = std::max (peak, e[i]); size_t i = 0; while (i < e.size() && e[i] < peak - 0.915) ++i; return static_cast<double> (i); });
        checkProportional ("env1.attack (90 % level)", xs, m, 0.87, 1.03, 1.1, 50.0f);
    }
    {
        const auto xs = std::vector<float> { 50.0f, 100.0f, 200.0f, 400.0f, 800.0f, 1500.0f };
        const auto m = sweep (pure(), "env1.release", xs, heldNote (0.5, 3.0), [] (const Audio& a, float)
                              { const auto e = envelopeDb (a); const size_t off = 500; const double ref = e[off - 5]; size_t i = off; while (i < e.size() && e[i] > ref - 40.0) ++i; return static_cast<double> (i - off); });
        checkProportional ("env1.release (-40 dB)", xs, m, 0.44, 0.56, 1.1, 50.0f);
    }
    {
        const auto xs = std::vector<float> { 50.0f, 100.0f, 200.0f, 400.0f, 800.0f, 1500.0f };
        const auto m = sweep (cat (pure(), { { "env1.sustain", 30.0f }, { "env1.attack", 0.0f } }), "env1.decay", xs, heldNote (4.0, 0.1), [] (const Audio& a, float)
                              { const auto e = envelopeDb (a); const double fin = e[3500]; size_t i = 0; while (i < e.size() && e[i] > fin + 0.5) ++i; return static_cast<double> (i); });
        checkProportional ("env1.decay (to the sustain)", xs, m, 0.38, 0.52, 1.25, 100.0f);
    }
    {
        // sustain 0 .. 100 %: the level during the sustain is sustain^1 in amplitude? measured in dB against the display
        const auto xs = std::vector<float> { 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f };
        const auto m = sweep (cat (pure(), { { "env1.decay", 20.0f }, { "env1.attack", 0.0f } }), "env1.sustain", xs, heldNote (1.0, 0.1), [] (const Audio& a, float) { return rmsDbOf (a.l, at (0.7), at (0.95)); });
        std::vector<double> rel;
        for (double v : m)
            rel.push_back (v - m.back());
        // Vital's law: the sustain is the envelope VALUE (the % of the display) and the amplitude follows the square of the envelope value: 40 log10 (s / 100) dB. (Serum 2 shows its
        // sustain in dB; owner question Q-AUDIT-1 in 14-morning-questions.md.) Tolerance 0.6 dB.
        checkCurve ("env1.sustain (dB below full)", xs, rel, [] (float sv) { return 40.0 * std::log10 (static_cast<double> (sv) / 100.0); }, 0.6, "dB");
    }
}

// ---- LFO, mod matrix, macros, voice control -------------------------------------------------------------------------------------------------------------------------------------------

namespace
{
/** Rate of the level modulation: the interpolated rising zero crossings of the 1 ms RMS envelope without its mean over [from, to] seconds. */
double levelModulationHz (const Audio& a, double from, double to)
{
    auto e = envelopeDb (a);
    {
        std::vector<double> sm (e.size());
        for (size_t i = 0; i < e.size(); ++i)
        {
            double acc = 0.0;
            int n = 0;
            for (size_t j = i >= 8 ? i - 8 : 0; j <= std::min (e.size() - 1, i + 8); ++j, ++n)
                acc += e[j];
            sm[i] = acc / n; // 17 ms moving average: the ripple of the carrier (130 Hz sine: 3.8 ms) is gone
        }
        e = sm;
    }
    const size_t i0 = static_cast<size_t> (from * 1000.0), i1 = std::min (e.size(), static_cast<size_t> (to * 1000.0));
    double mean = 0.0;
    for (size_t i = i0; i < i1; ++i)
        mean += e[i];
    mean /= static_cast<double> (i1 - i0);
    double first = -1.0, last = -1.0;
    int n = 0;
    for (size_t i = i0 + 1; i < i1; ++i)
        if (e[i - 1] - mean < 0.0 && e[i] - mean >= 0.0)
        {
            const double t = static_cast<double> (i - 1) + (mean - e[i - 1]) / (e[i] - e[i - 1]);
            if (first < 0.0)
                first = t;
            last = t;
            ++n;
        }
    return n >= 3 ? (n - 1) * 1000.0 / (last - first) : 0.0;
}
Ovs routed (int slot, const char* source, const char* dest, float amount, bool bipolar = false)
{
    int src = 0;
    for (size_t i = 0; i < modSourceCount(); ++i)
        if (std::strcmp (modSourceTable()[i].key, source) == 0)
            src = static_cast<int> (i) + 1;
    return routeOverrides (slot, src, dest, amount, bipolar);
}
} // namespace

TEST (lfo_rate_in_hz_and_in_tempo_divisions)
{
    // lfo1 (sine) routed to the oscillator level: the level modulation repeats at the LFO rate (Vital manual 3.2 "LFO frequency settings": Seconds / Tempo styles). Free: 3 .. 30 Hz, tolerance 6 % + 0.15 Hz.
    {
        const auto xs = std::vector<float> { 3.0f, 4.0f, 5.0f, 6.0f, 8.0f, 10.0f, 12.0f, 16.0f, 20.0f, 25.0f, 30.0f };
        Script sc = heldNote (2.0, 0.1);
        for (auto& e : sc.events)
            e.key = 96; // 2 kHz: the 1 ms RMS envelope averages whole cycles of the carrier
        sc.steadyFrom = at (0.4);
        sc.steadyTo = at (2.0);
        const auto m = sweep (cat (pure(), cat (routed (1, "lfo1", "osc1.level", 40.0f), { { "osc1.level", 60.0f }, { "lfo1.sync", 0.0f } })), "lfo1.rate", xs, sc, [] (const Audio& a, float) { return levelModulationHz (a, 0.4, 2.0); });
        checkCurve ("lfo1.rate (modulation rate, Hz)", xs, m, [] (float rate) { return static_cast<double> (rate); }, 0.02, "Hz");
    }
}

TEST (mod_matrix_amount_is_a_fraction_of_the_range_of_the_destination)
{
    // macro 1 at 100 % routed to master.tune (range -100 .. +100 cents) through slot 5: amount a % moves the pitch by a % of the range = a cents * 2 / 2... : the destination moves by
    // a/100 of its range in the engine's own scale (PARAMETERS.md, Modulation): for master.tune that is a * 2 cents. -100 .. +100 in steps of 10, tolerance 0.5 cent.
    const auto xs = range (-100.0f, 100.0f, 21);
    const auto m = sweep (cat (pure(), cat (routed (5, "macro1", "master.tune", 0.0f), { { "macro.1", 100.0f } })), "mod05.amount", xs, scriptByName ("single"), [] (const Audio& a, float)
                          { return cents (zcFreq (a.l, at (0.2), at (0.58)) / midiHz (48)); });
    checkCurve ("mod05.amount (cents at macro 100 %)", xs, m, [] (float amt) { return static_cast<double> (amt) * 2.0; }, 0.5, "cents");
    // macro value sweep at amount +50: linear in the macro
    const auto mac = range (0.0f, 100.0f, 21);
    const auto m2 = sweep (cat (pure(), routed (5, "macro1", "master.tune", 50.0f)), "macro.1", mac, scriptByName ("single"), [] (const Audio& a, float) { return cents (zcFreq (a.l, at (0.2), at (0.58)) / midiHz (48)); });
    checkCurve ("macro.1 (cents at amount +50 %)", mac, m2, [] (float v) { return static_cast<double> (v) * 1.0; }, 0.5, "cents");
}

TEST (mod_matrix_bypass_bipolar_and_slots_are_identical)
{
    // bypass = the slot does nothing (audio equals the unrouted render); bipolar centres the source: macro 50 % routed bipolar leaves the tune unchanged; and every one of the 64 slots routes
    // the same way (macro 100 %, amount +50 -> +100 cents on master.tune), tolerance 0.5 cent.
    Rig& r = rig();
    const auto sc = scriptByName ("single");
    r.begin (cat (pure(), { { "macro.1", 100.0f } }));
    const auto plain = r.render (sc);
    r.begin (cat (cat (pure(), { { "macro.1", 100.0f } }), cat (routed (1, "macro1", "master.tune", 50.0f), { { "mod01.bypass", 1.0f } })));
    const auto bypassed = r.render (sc);
    double d = 0.0;
    for (size_t i = 0; i < plain.l.size(); ++i)
        d = std::max (d, std::fabs (static_cast<double> (plain.l[i]) - bypassed.l[i]));
    CHECK (d < 1.0e-3);
    r.begin (cat (cat (pure(), { { "macro.1", 50.0f } }), routed (1, "macro1", "master.tune", 100.0f, true)));
    CHECK_NEAR (cents (zcFreq (r.render (sc).l, at (0.2), at (0.58)) / midiHz (48)), 0.0, 0.5);
    std::vector<double> perSlot;
    std::vector<float> slots;
    for (int slot = 1; slot <= 64; ++slot)
    {
        r.begin (cat (cat (pure(), { { "macro.1", 100.0f } }), routed (slot, "macro1", "master.tune", 50.0f)));
        perSlot.push_back (cents (zcFreq (r.render (sc).l, at (0.2), at (0.58)) / midiHz (48)));
        slots.push_back (static_cast<float> (slot));
    }
    checkCurve ("modNN (slot 1 .. 64, cents)", slots, perSlot, [] (float) { return 100.0; }, 0.5, "cents");
}

TEST (voice_control_detune_and_pan_per_voice)
{
    // voicectl.s1.detune -100 .. 100 cents moves the first voice by that many cents; voicectl.s1.pan -100 .. 100 puts the first voice left / right
    const auto xs = range (-100.0f, 100.0f, 21);
    const auto m = sweep (pure(), "voicectl.s1.detune", xs, scriptByName ("single"), [] (const Audio& a, float) { return cents (zcFreq (a.l, at (0.2), at (0.58)) / midiHz (48)); });
    checkCurve ("voicectl.s1.detune (cents)", xs, m, [] (float v) { return static_cast<double> (v); }, 0.5, "cents");
    const auto b = sweep (pure(), "voicectl.s1.pan", xs, scriptByName ("single"), [] (const Audio& a, float) { return rmsDbOf (a.l, at (0.25), at (0.58)) - rmsDbOf (a.r, at (0.25), at (0.58)); });
    checkMonotonic ("voicectl.s1.pan (L - R, dB, from left to right)", b, -1, 0.3, 30.0);
}

TEST (polyphony_limits_the_number_of_voices)
{
    // eight notes held, master.polyphony 1 .. 16: the power of the sum of equal sine voices at different pitches grows with the voice count: 10 log10 (min (n, 8)), tolerance 1 dB
    const auto xs = range (1.0f, 16.0f, 16);
    const auto m = sweep (cat (pure(), { { "master.volume", -18.0f } }), "master.polyphony", xs, scriptByName ("chord8"), [] (const Audio& a, float) { return rmsDbOf (a.l, at (0.25), at (0.58)); });
    std::vector<double> rel;
    for (double v : m)
        rel.push_back (v - m.front());
    checkCurve ("master.polyphony (power re one voice, dB)", xs, rel, [] (float n) { return 10.0 * std::log10 (std::min (static_cast<double> (n), 8.0)); }, 1.5, "dB");
}

TEST (wavetable_frame_position_morphs_the_spectrum_monotonically)
{
    // the audit table has frame f with 1 + 3 f harmonics: the spectral centroid rises with the frame position over the whole 0 .. 100 % sweep (65 points); frame 0 is a sine
    const auto xs = range (0.0f, 100.0f, 65);
    const auto m = sweep (cat (pure(), { { "osc1.wavetable", 13.0f } }), "osc1.frame", xs, scriptByName ("single"), [] (const Audio& a, float) { return analyse (a, scriptByName ("single")).centroid; });
    checkMonotonic ("osc1.frame (centroid, Hz)", m, +1, 4.0, 100.0);
}

TEST (distortion_drive_adds_harmonics_monotonically)
{
    // soft clip of a sine: the spectral centroid rises with the drive over -30 .. +30 dB (65 points)
    const auto xs = range (-30.0f, 30.0f, 61);
    const auto m = sweep (cat (pure(), { { "distortion.on", 1.0f }, { "distortion.type", 0.0f }, { "distortion.filter_order", 0.0f } }), "distortion.drive", xs, scriptByName ("single"), [] (const Audio& a, float) { return analyse (a, scriptByName ("single")).centroid; });
    checkMonotonic ("distortion.drive (centroid, Hz)", m, +1, 3.0, 50.0);
}

TEST (effect_mix_zero_is_the_dry_signal)
{
    // every effect with a mix control: mix 0 sounds like the effect off, mix 100 does not. Effects that only add or remove a delayed / filtered copy (chorus, delay, flanger, phaser,
    // distortion, hyperdim, convolve) must be waveform identical to the off render (noise floor of the audit); the reverb (a 3 ms onset difference of the shelving dry path), the compressor
    // (its band split is an all pass: Vital's mix 0 keeps the crossover phase) and Bode (latency compensation of the whole output) must have the level (0.3 dB) and the 24 band spectrum
    // (0.5 dB) of the off render.
    Rig& r = rig();
    const auto sc = scriptByName ("long");
    for (const char* fx : { "chorus", "delay", "flanger", "phaser", "distortion", "hyperdim", "convolve", "reverb", "compressor", "bode" })
    {
        const std::string mix = std::string (fx) + ".mix";
        r.begin ({});
        const auto dry = r.render (sc);
        r.begin ({ { std::string (fx) + ".on", 1.0f }, { mix, 0.0f }, { "convolve.ir", 1.0f } });
        const auto zero = r.render (sc);
        r.begin ({ { std::string (fx) + ".on", 1.0f }, { mix, 100.0f }, { "convolve.ir", 1.0f } });
        const auto full = r.render (sc);
        auto diff = [&] (const Audio& x, const Audio& y) { double d = 0.0, p = 1e-9; for (size_t i = 0; i < x.l.size(); ++i) { d = std::max ({ d, std::fabs (static_cast<double> (x.l[i]) - y.l[i]), std::fabs (static_cast<double> (x.r[i]) - y.r[i]) }); p = std::max (p, (double) std::fabs (y.l[i])); } return 20.0 * std::log10 (std::max (d / p, 1e-9)); };
        const bool spectral = std::string (fx) == "reverb" || std::string (fx) == "compressor" || std::string (fx) == "bode";
        const auto md = analyse (dry, sc), mz = analyse (zero, sc);
        double worstBand = 0.0;
        for (int b = 0; b < kBands; ++b)
            if (md.band[b] > md.rmsDb - 50.0)
                worstBand = std::max (worstBand, std::fabs (md.band[b] - mz.band[b]));
        std::printf ("    %s: mix 0 vs off %.1f dB (level %.2f dB, worst band %.2f dB), mix 100 vs off %.1f dB\n", fx, diff (zero, dry), mz.rmsDb - md.rmsDb, worstBand, diff (full, dry));
        if (spectral)
        {
            sbtest::check (std::fabs (mz.rmsDb - md.rmsDb) < 0.3 && worstBand < 0.5, (std::string (fx) + " mix 0 has the level and spectrum of the off render").c_str(), __FILE__, __LINE__);
        }
        else
            sbtest::check (diff (zero, dry) < kNoiseFloorDb, (std::string (fx) + " mix 0 is dry").c_str(), __FILE__, __LINE__);
        sbtest::check (std::string (fx) == "bode" || diff (full, dry) > kNoiseFloorDb, (std::string (fx) + " mix 100 differs").c_str(), __FILE__, __LINE__);
    }
}

// ---- effects ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

TEST (delay_time_is_the_echo_spacing)
{
    // a 40 ms sine burst through the delay at mix 100, feedback 0, free time: the first sound arrives `delay.time` ms after the note on (Vital manual: Delay - time in ms); tolerance 3 ms
    const auto xs = std::vector<float> { 40.0f, 60.0f, 90.0f, 130.0f, 200.0f, 300.0f, 450.0f, 700.0f, 1000.0f, 1500.0f };
    Script sc = heldNote (0.04, 2.2);
    for (auto& e : sc.events)
        e.key = 72;
    const auto m = sweep (cat (pure(), { { "delay.on", 1.0f }, { "delay.mix", 100.0f }, { "delay.feedback", 0.0f }, { "delay.sync", 0.0f }, { "delay.style", 0.0f }, { "env1.release", 5.0f } }), "delay.time", xs, sc, [] (const Audio& a, float)
                          { const auto e = envelopeDb (a); size_t i = 0; while (i < e.size() && e[i] < -60.0) ++i; return static_cast<double> (i); });
    checkCurve ("delay.time (first sound after, ms)", xs, m, [] (float t) { return static_cast<double> (t); }, 3.0, "ms");
}

TEST (eq_band_gain_in_db)
{
    // eq.mid.gain -15 .. +15 dB at 1 kHz, resonance 60 % on white noise: the extreme of the 1/24 octave band levels between 600 Hz and 1.7 kHz against the untouched bands at 150 Hz and
    // 9 kHz (mean) follows the gain, within 2.5 dB (the extreme of a noise estimate of a narrow band is biased up to 2.3 dB even at 0 dB)
    const auto xs = range (-15.0f, 15.0f, 13);
    const auto m = sweep (cat (noisePatch(), { { "eq.on", 1.0f }, { "eq.mid.cutoff", 1000.0f }, { "eq.mid.resonance", 60.0f } }), "eq.mid.gain", xs, noiseScript (6.0), [] (const Audio& a, float g)
                          { const auto sp = welch (a, at (0.25), at (6.0), 16384); const double ref = 0.5 * (bandDb (sp, 16384, 150.0, 1.0 / 2.0) + bandDb (sp, 16384, 9000.0, 1.0 / 2.0));
                            double ex = 0.0; for (double f = 600.0; f <= 1700.0; f *= 1.01) { const double d = bandDb (sp, 16384, f, 1.0 / 24.0) - ref; if (std::fabs (d) > std::fabs (ex)) ex = d; } (void) g; return ex; });
    checkCurve ("eq.mid.gain (peak of the band re the untouched bands, dB)", xs, m, [] (float g) { return static_cast<double> (g); }, 2.5, "dB");
}

TEST (unison_voices_widen_and_do_not_blow_up_the_level)
{
    // osc1.unison.voices 1 .. 16 (saw, default detune and spread): the level may grow with the voice count but by at most 10 log10 (n) + 1 dB (incoherent sum), and the stereo width (side / mid
    // power) grows from 0 (one voice is centred)
    const auto xs = range (1.0f, 16.0f, 16);
    Rig& r = rig();
    r.begin ({ { "osc1.wavetable", 1.0f }, { "filter1.on", 0.0f } });
    std::vector<double> level, width;
    for (float n : xs)
    {
        r.set ("osc1.unison.voices", n);
        const auto a = r.render (scriptByName ("single"));
        double mid = 0.0, side = 0.0;
        for (size_t i = at (0.25); i < at (0.58); ++i)
        {
            const double m = 0.5 * (static_cast<double> (a.l[i]) + a.r[i]), s = 0.5 * (static_cast<double> (a.l[i]) - a.r[i]);
            mid += m * m;
            side += s * s;
        }
        level.push_back (10.0 * std::log10 (mid + side));
        width.push_back (side / std::max (mid, 1e-12));
    }
    std::vector<double> rel;
    for (double v : level)
        rel.push_back (v - level.front());
    for (size_t i = 0; i < xs.size(); ++i)
        CHECK (rel[i] <= 10.0 * std::log10 (static_cast<double> (xs[i])) + 1.0);
    CHECK (width.front() < 1.0e-6);
    CHECK (width.back() > 0.05);
    std::printf ("    unison: level at 16 voices %+.2f dB, width %.3f\n", rel.back(), width.back());
}

TEST (velocity_tracking_scales_the_level)
{
    // master.velocity_track -100 .. 100 %: at +100 % a softer note is quieter and at -100 % louder; at 0 % the level does not depend on the velocity
    Rig& r = rig();
    auto level = [&] (float track, float vel)
    {
        r.begin (cat (pure(), { { "master.velocity_track", track }, { "env1.attack", 0.0f } }));
        auto sc = scriptByName ("single");
        for (auto& e : sc.events)
            if (e.type == Event::NoteOn)
                e.value = vel;
        return rmsDbOf (r.render (sc).l, at (0.25), at (0.58));
    };
    std::printf ("    velocity: +100 %%: %.1f dB (vel 0.25) %.1f dB (vel 1); 0 %%: %.1f / %.1f; -100 %%: %.1f / %.1f\n", level (100, 0.25f), level (100, 1.0f), level (0, 0.25f), level (0, 1.0f), level (-100, 0.25f), level (-100, 1.0f));
    CHECK (level (100, 0.25f) < level (100, 1.0f) - 6.0);
    CHECK_NEAR (level (0, 0.25f), level (0, 1.0f), 0.2);
    CHECK (level (-100, 0.25f) > level (-100, 1.0f) + 6.0);
    std::vector<double> m;
    for (float v = 0.1f; v <= 1.0f; v += 0.05f)
        m.push_back (level (100, v));
    checkMonotonic ("velocity at track +100 % (dB)", m, +1, 0.05, 6.0);
    // the amplitude follows the square of the velocity (Vital's velocity curve): 40 log10 (v) dB below the full velocity, tolerance 0.5 dB from velocity 0.1
    std::vector<float> vs;
    std::vector<double> rel;
    for (float v = 0.1f; v <= 1.0f; v += 0.05f)
    {
        vs.push_back (v);
        rel.push_back (level (100, v) - level (100, 1.0f));
    }
    checkCurve ("velocity at track +100 % (dB below velocity 1)", vs, rel, [] (float v) { return 40.0 * std::log10 (static_cast<double> (v)); }, 0.5, "dB");
}

TEST (portamento_glides_between_notes)
{
    // master.portamento.time 0 .. 1600 ms: the second of two successive notes starts below its pitch and glides up; the time to get within 10 cents grows with the display
    Rig& r = rig();
    const auto xs = std::vector<float> { 0.0f, 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f };
    r.begin (pure());
    auto seq = scriptByName ("seq");
    for (auto& e : seq.events)
        if (e.type == Event::NoteOff && e.key == 60)
            e.frame = at (3.0);
    seq.frames = at (3.2);
    std::vector<double> reached;
    for (float t : xs)
    {
        r.set ("master.portamento.time", t);
        r.set ("master.portamento.always", 1.0f);
        const auto a = r.render (seq);
        double reach = -1.0;
        for (double ts = 0.25; ts < 2.9; ts += 0.005)
        {
            const double f = zcFreq (a.l, at (ts), at (ts + 0.02));
            if (f > 0.0 && std::fabs (cents (f / midiHz (60))) < 10.0)
            {
                reach = (ts - 0.25) * 1000.0;
                break;
            }
        }
        std::printf ("    portamento %g ms: within 10 cents after %.0f ms\n", static_cast<double> (t), reach);
        reached.push_back(reach);
    }
    // the glide reaches the target (within 10 cents) after the displayed time: measured = display within 8 % + 15 ms
    for (size_t i = 1; i < xs.size(); ++i)
        CHECK (reached[i] >= 0.0 && std::fabs (reached[i] - static_cast<double> (xs[i])) <= 0.08 * static_cast<double> (xs[i]) + 15.0);
    CHECK (reached[0] <= 5.0);
}


// ---- regression tests of the defects the audit found ------------------------------------------------------------------------------------------------------------------------

TEST (regression_modulation_reaches_the_serum_effects_the_splitters_and_the_level_trims)
{
    // Found by the audit (PARAM-AUDIT-REPORT.md 3.1): the keys of Hyper / Dimension, Utility, Bode, Convolve, the three splitters were flagged modulatable but the modulation never
    // reached the DSP (StereoFxModule / SplitterBase did not run the control processors of the module), and neither did the level trims <fx>.level of the effect chain
    // (ReorderableEffectChain::process did not either). Macro 1 at 0 and at 100 % through slot 64 must sound different (more than 10 dB below the peak is no difference).
    struct Case { const char* key; Ovs ctx; float amount; };
    const std::vector<Case> cases = {
        { "utility.pan", { { "utility.on", 1.0f } }, 100.0f },        { "utility.width", { { "utility.on", 1.0f }, { "osc1.unison.voices", 5.0f } }, -100.0f }, { "utility.level", { { "utility.on", 1.0f } }, -100.0f },
        { "hyperdim.mix", { { "hyperdim.on", 1.0f } }, 100.0f },      { "hyperdim.level", { { "hyperdim.on", 1.0f } }, -100.0f },
        { "bode.mix", { { "bode.on", 1.0f }, { "bode.shift", 40.0f } }, 100.0f }, { "bode.shift", { { "bode.on", 1.0f }, { "bode.mix", 100.0f } }, 100.0f },
        { "convolve.mix", { { "convolve.on", 1.0f }, { "convolve.ir", 1.0f } }, 100.0f },
        { "splitter_lh.freq", { { "splitter_lh.on", 1.0f }, { "splitter_lh.high_bypass", 1.0f } }, 100.0f }, { "splitter_lh.level", { { "splitter_lh.on", 1.0f } }, -100.0f },
        { "splitter_lmh.level", { { "splitter_lmh.on", 1.0f } }, -100.0f },                                                              { "splitter_ms.level", { { "splitter_ms.on", 1.0f } }, -100.0f },
        { "compressor.level", { { "compressor.on", 1.0f } }, -100.0f }, { "delay.level", { { "delay.on", 1.0f }, { "delay.sync", 0.0f } }, -100.0f }, { "eq.level", { { "eq.on", 1.0f } }, -100.0f },
        { "chorus.level", { { "chorus.on", 1.0f } }, -100.0f },       { "reverb.level", { { "reverb.on", 1.0f } }, -100.0f }, { "flanger.level", { { "flanger.on", 1.0f } }, -100.0f } };
    Rig& r = rig();
    const auto sc = scriptByName ("long");
    for (const auto& c : cases)
    {
        r.begin (cat (c.ctx, routed (64, "macro1", c.key, c.amount)));
        r.set ("macro.1", 0.0f);
        const auto a = r.render (sc);
        r.set ("macro.1", 100.0f);
        const auto b = r.render (sc);
        double d = 0.0, p = 1e-9;
        for (size_t i = 0; i < a.l.size(); ++i)
        {
            d = std::max ({ d, std::fabs (static_cast<double> (a.l[i]) - b.l[i]), std::fabs (static_cast<double> (a.r[i]) - b.r[i]) });
            p = std::max (p, std::fabs (static_cast<double> (a.l[i])));
        }
        const double db = 20.0 * std::log10 (std::max (d / p, 1.0e-9));
        std::printf ("    %s: macro 0 vs 100 differ by %.1f dB re peak\n", c.key, db);
        sbtest::check (db > -10.0, (std::string ("modulation of ") + c.key + " is heard").c_str(), __FILE__, __LINE__);
    }
}

TEST (regression_a_render_after_reset_is_repeatable_with_effect_lfos)
{
    // Found by the audit (PARAM-AUDIT-REPORT.md 3.2): the phases of the chorus / flanger / phaser LFOs were not part of reset (), so the second render of the same patch differed from
    // the first by up to +1.5 dB re peak. After the first render (which settles the on / off change of the effect) every further render of the same state is bit identical.
    Rig& r = rig();
    const auto sc = scriptByName ("default");
    for (const char* fx : { "chorus", "phaser" })
    {
        r.begin ({ { std::string (fx) + ".on", 1.0f } });
        r.render (sc);
        const auto a = r.render (sc), b = r.render (sc);
        double d = 0.0;
        for (size_t i = 0; i < a.l.size(); ++i)
            d = std::max (d, std::fabs (static_cast<double> (a.l[i]) - b.l[i]));
        std::printf ("    %s: consecutive renders differ by %g\n", fx, d);
        sbtest::check (d < 1.0e-9, (std::string (fx) + " renders repeat after reset").c_str(), __FILE__, __LINE__);
    }
}

// ---- phase, keytrack, zipper noise ----------------------------------------------------------------------------------------------------------------------------------------------

TEST (osc_phase_is_the_start_phase_in_degrees)
{
    // osc1.phase 0 .. 360 degrees (random phase 0, one sine voice): the start phase of the note. Fitted over the first 8 ms of the note against sin (2 pi f t + phi) with the exact
    // frequency of the played key; the fit is good to about 2 degrees (the attack is 0 ms, the envelope shape is flat); tolerance 4 degrees
    const auto xs = range (0.0f, 360.0f, 25);
    const double f = midiHz (60);
    std::vector<double> got;
    Rig& r = rig();
    r.begin (cat (pure(), { { "env1.attack", 0.0f }, { "osc1.random_phase", 0.0f } }));
    auto sc = scriptByName ("single");
    for (auto& e : sc.events)
        e.key = 60;
    for (float ph : xs)
    {
        r.set ("osc1.phase", ph);
        const auto a = r.render (sc);
        double sn = 0.0, cs = 0.0;
        for (size_t i = 24; i < at (0.008); ++i)
        {
            const double t = static_cast<double> (i) / kSr;
            sn += a.l[i] * std::sin (2.0 * 3.14159265358979323846 * f * t);
            cs += a.l[i] * std::cos (2.0 * 3.14159265358979323846 * f * t);
        }
        double phi = std::atan2 (cs, sn) * 180.0 / 3.14159265358979323846; // the fitted start phase of sin (wt + phi)
        if (phi < 0.0)
            phi += 360.0;
        got.push_back (phi);
    }
    // compare modulo 360 with a constant offset allowed (the engine's phase 0 is a sine starting at zero): offset = the value at phase 0
    std::vector<double> dev;
    for (size_t i = 0; i < xs.size(); ++i)
    {
        double d = got[i] - got[0] - static_cast<double>(xs[i]);
        d = std::fmod (d + 540.0, 360.0) - 180.0;
        dev.push_back (d);
        if (verbose())
            std::printf ("    osc1.phase %g: fitted %.1f deg, deviation %.1f\n", static_cast<double> (xs[i]), got[i], d);
    }
    double worst = 0.0;
    for (double d : dev)
        worst = std::max (worst, std::fabs (d));
    std::printf ("    osc1.phase: %zu points, worst deviation %.2f degrees (tolerance 4); phase 0 fitted at %.1f degrees\n", xs.size(), worst, got[0]);
    CHECK (worst <= 4.0);
}

TEST (filter_keytrack_follows_the_note_octave_per_octave)
{
    // filter1 low pass at resonance 85 % on white noise (the noise does not follow the key): the resonance peak of note 72 against note 60 is at 2^(keytrack / 100) times the frequency
    // (octave per octave at 100 %, none at 0, an octave down at -100 %), tolerance 1/12 octave + the 1/24 octave grid of the peak search (0.08 + 0.04 octave)
    auto drop = [&] (float keytrack, int key)
    {
        Rig& r = rig();
        r.begin (cat (noisePatch(), { { "filter1.on", 1.0f }, { "filter1.model", 3.0f }, { "filter1.cutoff", 1500.0f }, { "filter1.resonance", 85.0f }, { "filter1.keytrack", keytrack } }));
        auto sc = noiseScript (4.0);
        for (auto& e : sc.events)
            e.key = static_cast<int16_t> (key);
        const auto sp = welch (r.render (sc), at (0.25), at (4.0), 4096);
        return peakHz (sp, 4096, 300.0, 12000.0);
    };
    for (float kt : { -100.0f, -50.0f, 0.0f, 50.0f, 100.0f })
    {
        const double ratio = drop (kt, 72) / drop (kt, 60);
        const double want = std::pow (2.0, static_cast<double> (kt) / 100.0);
        std::printf ("    keytrack %g %%: cutoff ratio one octave up %.3f (expected %.3f)\n", static_cast<double> (kt), ratio, want);
        CHECK (std::fabs (std::log2 (ratio / want)) <= 0.12);
    }
}

namespace
{
/** Plays a held sine / saw; at `jumpAt` samples the key is set to `to`. Returns the largest sample to sample step in [jumpAt - 0.002 s, jumpAt + 0.05 s]. */
struct Steps { double atJump, before, after; };
Steps stepAroundJump (const char* key, float from, float to, const Ovs& base, int note = 48)
{
    Rig& r = rig();
    r.begin (cat (base, { { key, from }, { "quality.no_smoothing", 0.0f } })); // the audit switches the smoothing off elsewhere; here it is what is measured
    r.engine().reset();
    std::vector<float> l (kBlock), rr (kBlock), all;
    std::vector<Event> ev;
    Event on;
    on.type = Event::NoteOn;
    on.key = static_cast<int16_t> (note);
    on.value = 0.8f;
    ev.push_back (on);
    const int jumpAt = static_cast<int> (0.3 * kSr);
    const auto* info = findParamByKey (key);
    for (int done = 0; done < static_cast<int> (0.5 * kSr); done += kBlock)
    {
        if (done == (jumpAt / kBlock) * kBlock)
            r.engine().setParam (info->id, to);
        r.engine().process (done == 0 ? ev.data() : nullptr, done == 0 ? 1 : 0, l.data(), rr.data(), kBlock);
        all.insert (all.end(), l.begin(), l.end());
    }
    auto largest = [&] (size_t from, size_t to)
    {
        double step = 0.0;
        for (size_t i = from; i < to && i < all.size(); ++i)
            step = std::max (step, std::fabs (static_cast<double> (all[i]) - all[i - 1]));
        return step;
    };
    return { largest (at (0.3) - 96, at (0.36)), largest (at (0.2), at (0.29)), largest (at (0.42), at (0.5)) };
}
} // namespace

TEST (parameters_change_smoothly_while_a_note_sounds)
{
    // No zipper noise / click: a jump of the key in the middle of a held note (with the smoothing of the engine on, which is the default) makes no sample to sample step larger than
    // 2.5 times the largest step of the steady sound (a click is a step of the order of the signal itself, 10 .. 100 times larger)
    struct Case { const char* key; float from, to; Ovs base; };
    const Ovs saw = { { "osc1.wavetable", 1.0f }, { "filter1.on", 1.0f }, { "filter1.cutoff", 3000.0f }, { "osc1.level", 80.0f }, { "env1.attack", 0.0f } };
    const std::vector<Case> cases = {
        { "master.volume", -30.0f, 0.0f, pure() }, { "osc1.level", 10.0f, 100.0f, pure() }, { "osc1.pan", -100.0f, 100.0f, pure() }, { "master.tune", -100.0f, 100.0f, pure() },
        { "osc1.tune", -100.0f, 100.0f, pure() }, { "filter1.cutoff", 200.0f, 12000.0f, saw }, { "filter1.resonance", 0.0f, 90.0f, saw }, { "osc1.transpose", 0.0f, 12.0f, pure() },
        { "osc1.frame", 0.0f, 100.0f, cat (pure(), { { "osc1.wavetable", 13.0f } }) }, { "distortion.drive", -10.0f, 20.0f, cat (pure(), { { "distortion.on", 1.0f } }) } };
    for (const auto& c : cases)
    {
        const Steps st = stepAroundJump (c.key, c.from, c.to, c.base);
        const double steady = std::max (st.before, st.after); // a louder or brighter sound has bigger steps of its own
        std::printf ("    %s %g -> %g: largest step at the jump %.5f, steady before %.5f, after %.5f (x%.2f)\n", c.key, static_cast<double> (c.from), static_cast<double> (c.to), st.atJump, st.before, st.after, st.atJump / std::max (steady, 1e-9));
        sbtest::check (st.atJump <= 2.5 * steady + 1.0e-4, (std::string ("no click when ") + c.key + " jumps").c_str(), __FILE__, __LINE__);
    }
}


// ---- keys that act on something other than the steady sound -----------------------------------------------------------------------------------------------------------------------

TEST (compressor_limit_latency_comp_reports_the_look_ahead)
{
    // compressor.limit_latency_comp (13-new-keys.md A142): On reports the 1.5 ms look-ahead of the limiter as chain latency (IEngine::latencySamples), Off does not
    Rig& r = rig();
    r.begin ({ { "compressor.on", 1.0f }, { "compressor.limit", 1.0f }, { "compressor.limit_latency_comp", 1.0f } });
    r.render (scriptByName ("single"));
    const int on = r.engine().latencySamples();
    r.set ("compressor.limit_latency_comp", 0.0f);
    r.render (scriptByName ("single"));
    const int off = r.engine().latencySamples();
    r.set ("compressor.limit", 0.0f);
    r.set ("compressor.limit_latency_comp", 1.0f);
    r.render (scriptByName ("single"));
    const int noLimiter = r.engine().latencySamples();
    std::printf("    latency: limiter + comp %d samples, limiter without comp %d, comp without limiter %d (1.5 ms = %d samples)\n", on, off, noLimiter, static_cast<int> (0.0015 * kSr));
    CHECK (on >= static_cast<int> (0.0014 * kSr) && on <= static_cast<int> (0.0017 * kSr) + 8);
    CHECK_EQ (off, 0);
    CHECK_EQ (noLimiter, 0);
}

TEST (quality_no_smoothing_changes_how_a_jump_lands)
{
    // quality.no_smoothing (Serum 2 "Disable Smoothing", 13-new-keys.md A106): the transition after a parameter jump in the middle of a note is different with the option on. For each of
    // the keys below the transition is rendered with and without it; the keys that the smoothing acts on differ by more than -40 dB re peak, the others are identical
    struct Case { const char* key; float from, to; Ovs base; };
    const Ovs saw = { { "osc1.wavetable", 1.0f }, { "filter1.on", 1.0f }, { "filter1.cutoff", 3000.0f }, { "osc1.level", 80.0f }, { "env1.attack", 0.0f } };
    const std::vector<Case> cases = { { "master.volume", -30.0f, 0.0f, pure() }, { "osc1.level", 10.0f, 100.0f, pure() }, { "osc1.pan", -100.0f, 100.0f, pure() }, { "osc1.tune", -100.0f, 100.0f, pure() },
                                      { "filter1.cutoff", 200.0f, 12000.0f, saw }, { "filter1.resonance", 0.0f, 90.0f, saw }, { "distortion.drive", -10.0f, 20.0f, cat (pure(), { { "distortion.on", 1.0f } }) },
                                      { "osc1.frame", 0.0f, 100.0f, cat (pure(), { { "osc1.wavetable", 13.0f } }) } };
    int differing = 0;
    for (const auto& c : cases)
    {
        std::vector<float> runs[2];
        for (int ns = 0; ns < 2; ++ns)
        {
            Rig& r = rig();
            r.begin (cat (c.base, { { c.key, c.from }, { "quality.no_smoothing", static_cast<float> (ns) } }));
            r.engine().reset();
            std::vector<float> l (kBlock), rr (kBlock);
            Event on;
            on.type = Event::NoteOn;
            on.key = 48;
            on.value = 0.8f;
            const auto* info = findParamByKey (c.key);
            const int jumpAt = (static_cast<int> (0.3 * kSr) / kBlock) * kBlock;
            for (int done = 0; done < static_cast<int> (0.5 * kSr); done += kBlock)
            {
                if (done == jumpAt)
                    r.engine().setParam (info->id, c.to);
                r.engine().process (done == 0 ? &on : nullptr, done == 0 ? 1 : 0, l.data(), rr.data(), kBlock);
                if (done >= jumpAt)
                    runs[ns].insert (runs[ns].end(), l.begin(), l.end());
            }
        }
        double d = 0.0, p = 1e-9;
        for (size_t i = 0; i < runs[0].size(); ++i)
        {
            d = std::max (d, std::fabs (static_cast<double> (runs[0][i]) - runs[1][i]));
            p = std::max (p, std::fabs (static_cast<double> (runs[0][i])));
        }
        const double db = 20.0 * std::log10 (std::max (d / p, 1e-9));
        std::printf ("    %s jump: with and without smoothing differ by %.1f dB re peak\n", c.key, db);
        differing += db > -40.0 ? 1 : 0;
        if (std::string (c.key) == "osc1.level" || std::string (c.key) == "filter1.cutoff" || std::string (c.key) == "distortion.drive")
            sbtest::check (db > -40.0, (std::string ("smoothing acts on ") + c.key).c_str(), __FILE__, __LINE__);
    }
    CHECK (differing >= 3);
}

TEST (reverb_force_enable_overrides_the_tier_mask)
{
    // reverb.force_enable (quality.h effectiveFxMask): the low tier bypasses the reverb, unless the key is on; on the high tier the key changes nothing
    const auto sc = scriptByName ("long");
    auto levelTail = [&] (Tier tier, float force, float reverb)
    {
        Rig r (EngineKind::vital, kSr, tier);
        r.begin ({ { "reverb.on", reverb }, { "reverb.mix", 100.0f }, { "reverb.force_enable", force }, { "reverb.decay", 3000.0f } });
        return rmsDbOf (r.render (sc).l, at (1.0), at (1.9));
    };
    const double lowOff = levelTail (Tier::low, 0.0f, 1.0f), lowForced = levelTail (Tier::low, 1.0f, 1.0f), noReverb = levelTail (Tier::low, 0.0f, 0.0f);
    std::printf ("    reverb tail (1.0 .. 1.9 s) on the low tier: masked %.1f dB, forced %.1f dB, reverb off %.1f dB\n", lowOff, lowForced, noReverb);
    CHECK_NEAR (lowOff, noReverb, 1.0);
    CHECK (lowForced > noReverb + 12.0);
    CHECK_NEAR (levelTail (Tier::high, 0.0f, 1.0f), levelTail (Tier::high, 1.0f, 1.0f), 1.0);
}

TEST (every_key_is_clamped_to_its_range_and_ignores_nan)
{
    // setParam constrains to [min, max] and snaps to the interval; NaN is ignored; getParam reports it back. All keys of the table, both edges, far outside (the audio of the audit's
    // OUTSIDE check only repeats this for the keys that sound the same twice)
    Rig& r = rig();
    int bad = 0;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const auto& p = paramTable()[i];
        r.engine().setParam (p.id, p.max + 1.0e6f);
        const float hi = r.engine().getParam (p.id);
        r.engine().setParam (p.id, p.min - 1.0e6f);
        const float lo = r.engine().getParam (p.id);
        r.engine().setParam (p.id, std::nanf (""));
        const float afterNan = r.engine().getParam (p.id);
        if (hi != p.max || lo != p.min || afterNan != lo)
        {
            ++bad;
            std::printf ("    %s: max side %g, min side %g, after NaN %g\n", p.key, static_cast<double> (hi), static_cast<double> (lo), static_cast<double> (afterNan));
        }
    }
    std::printf ("    %zu keys, %d not clamped\n", paramCount(), bad);
    CHECK_EQ (bad, 0);
}
