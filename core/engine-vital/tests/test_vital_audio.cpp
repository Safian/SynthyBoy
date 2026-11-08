// SynthyBoy - engine-vital tests: audio quality gates of the Vital-derived engine through our parameter model (docs/QUALITY.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every tolerance carries its justification: either a measurement (the number measured on the development machine is written
// next to the assertion, the threshold is that number plus a margin) or an analytic expectation. The executable replaces the
// global operator new with a counter, so that every render also asserts "no allocation in process()".

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "alloc_hook.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/state.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

unsigned long g_allocationsInProcess = 0;

/** A prepared Vital engine at the given tier with the polyphony parameter wide open. */
std::unique_ptr<IEngine> makeEngine()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    return e;
}

void prepare (IEngine& e, Tier tier = Tier::high)
{
    e.prepare (kSr, kBlock, qualityForTier (tier));
}

/** Like sbtest::render, but counts allocations made inside process(). */
sbtest::Rendered render (IEngine& engine, const std::vector<Event>& events, int totalFrames, int blockSize = kBlock)
{
    sbtest::Rendered out;
    out.left.assign (static_cast<size_t> (totalFrames), 0.0f);
    out.right.assign (static_cast<size_t> (totalFrames), 0.0f);
    std::vector<Event> local;
    size_t idx = 0;
    for (int start = 0; start < totalFrames; start += blockSize)
    {
        const int n = std::min (blockSize, totalFrames - start);
        local.clear();
        while (idx < events.size() && events[idx].frame < start + n)
        {
            auto e = events[idx++];
            e.frame -= start;
            local.push_back (e);
        }
        const auto before = g_allocations.load();
        engine.process (local.data(), static_cast<int> (local.size()), out.left.data() + start, out.right.data() + start, n);
        g_allocationsInProcess += g_allocations.load() - before;
    }
    return out;
}

double hzOfKey (int key)
{
    return 440.0 * std::pow (2.0, (key - 69) / 12.0);
}

/** Pure oscillator: only oscillator `osc` (1..3) on, filter 1 off (nothing between the oscillator and the output but the effects, all off). */
void pureOscillator (IEngine& e, int osc, float wavetable)
{
    for (int i = 1; i <= 3; ++i)
        setByKey (e, ("osc" + std::to_string (i) + ".on").c_str(), i == osc ? 1.0f : 0.0f);
    setByKey (e, ("osc" + std::to_string (osc) + ".wavetable").c_str(), wavetable);
    setByKey (e, ("osc" + std::to_string (osc) + ".level").c_str(), 50.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}
} // namespace

namespace
{
void* g_sink = nullptr;
}

TEST (alloc_hook_works_in_this_executable)
{
    const auto before = g_allocations.load();
    g_sink = new int (3); // escapes through a global, so the compiler cannot elide it
    CHECK (g_allocations.load() > before);
    delete static_cast<int*> (g_sink);
    g_sink = nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// 8a. parameter sweep
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
/** Renders a few notes on the current patch and checks the output: finite, below the ceiling. Returns the peak. */
double playAndCheck (IEngine& e, const char* label, double ceiling, int blocks = 5)
{
    std::vector<float> l (kBlock), r (kBlock);
    const Event events[] = { noteOn (0, 41, 1.0f), noteOn (3, 60, 0.9f), noteOn (7, 67, 0.7f), noteOn (11, 88, 0.8f) };
    double peak = 0.0;
    bool finite = true;
    for (int b = 0; b < blocks; ++b)
    {
        const auto before = g_allocations.load();
        e.process (b == 0 ? events : nullptr, b == 0 ? 4 : 0, l.data(), r.data(), kBlock);
        g_allocationsInProcess += g_allocations.load() - before;
        for (int i = 0; i < kBlock; ++i)
        {
            finite = finite && std::isfinite (l[static_cast<size_t> (i)]) && std::isfinite (r[static_cast<size_t> (i)]);
            peak = std::max ({ peak, static_cast<double> (std::fabs (l[static_cast<size_t> (i)])), static_cast<double> (std::fabs (r[static_cast<size_t> (i)])) });
        }
    }
    const Event off = sbtest::allNotesOff (0);
    e.process (&off, 1, l.data(), r.data(), kBlock);
    if (! (CHECK (finite) && CHECK (peak <= ceiling)))
        std::printf ("    %s: finite %d peak %g\n", label, static_cast<int> (finite), peak);
    return peak;
}
} // namespace

TEST (parameter_sweep_every_parameter_min_default_max)
{
    // Output ceiling: the engine ends in a hard clamp at +-2.1 (SoundEngine, Clamp (-2.1, 2.1)); we assert 4.0, so that a missing clamp
    // would be seen. A NaN or an inf anywhere fails. High tier: every parameter alone at min, default and max while four notes play
    // (the low tier gets the random patches below: 20 whole patches, which keeps the run time of this executable at about 20 s).
    constexpr double kCeiling = 4.0;
    for (const Tier tier : { Tier::high })
    {
        auto e = makeEngine();
        prepare (*e, tier);
        g_allocationsInProcess = 0;
        double worst = 0.0;
        const char* worstKey = "";
        for (size_t i = 0; i < paramCount(); ++i)
        {
            const auto& info = paramTable()[i];
            for (const float value : { info.min, info.def, info.max })
            {
                e->setParam (info.id, value);
                const double peak = playAndCheck (*e, info.key, kCeiling);
                if (peak > worst)
                {
                    worst = peak;
                    worstKey = info.key;
                }
            }
            e->setParam (info.id, info.def);
        }
        std::printf ("    tier %s: %zu parameters x 3 values, highest peak %.3f (%s), allocations in process(): %lu\n", tier == Tier::high ? "high" : "low",
                     paramCount(), worst, worstKey, g_allocationsInProcess);
        CHECK_EQ (g_allocationsInProcess, 0ul);
    }
}

TEST (parameter_sweep_random_patches)
{
    // whole patches: every parameter random at once (the validator's param-fuzz does this), 20 per tier
    constexpr double kCeiling = 4.0;
    for (const Tier tier : { Tier::low, Tier::high })
    {
        auto e = makeEngine();
        prepare (*e, tier);
        g_allocationsInProcess = 0;
        sbtest::Rng rng (tier == Tier::low ? 101 : 202);
        double worst = 0.0;
        for (int patch = 0; patch < 20; ++patch)
        {
            for (size_t i = 0; i < paramCount(); ++i)
            {
                const auto& info = paramTable()[i];
                e->setParam (info.id, info.min + static_cast<float> (rng.uniform()) * (info.max - info.min));
            }
            worst = std::max (worst, playAndCheck (*e, "random patch", kCeiling, 12));
        }
        std::printf ("    tier %s: 20 random patches, highest peak %.3f, allocations in process(): %lu\n", tier == Tier::high ? "high" : "low", worst,
                     g_allocationsInProcess);
        CHECK_EQ (g_allocationsInProcess, 0ul);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// 8b. tuning
// ---------------------------------------------------------------------------------------------------------------------
TEST (tuning_each_oscillator_four_wavetables_four_keys)
{
    // fundamental by FFT peak (0.8 s, 2^17 points, parabolic interpolation: resolution far below a cent) against
    // 440 * 2^((key - 69) / 12). Limit 2 cents (the brief); measured worst case is printed.
    double worstCents = 0.0;
    std::string worst;
    for (int osc = 1; osc <= 3; ++osc)
    {
        auto e = makeEngine();
        for (const float wavetable : { 0.0f, 1.0f, 2.0f, 3.0f })
            for (const int key : { 33, 57, 69, 93 })
            {
                pureOscillator (*e, osc, wavetable);
                setByKey (*e, ("osc" + std::to_string (osc) + ".unison.voices").c_str(), 1.0f);
                prepare (*e);
                const auto out = render (*e, { noteOn (0, key) }, 72000);
                const auto spectrum = sbtest::powerSpectrum (out.left, 14400, 38400, 131072);
                const double f = sbtest::peakFrequency (spectrum, 131072, kSr, hzOfKey (key));
                const double cents = 1200.0 * std::log2 (f / hzOfKey (key));
                if (std::fabs (cents) > std::fabs (worstCents))
                {
                    worstCents = cents;
                    worst = "osc " + std::to_string (osc) + " table " + std::to_string (static_cast<int> (wavetable)) + " key " + std::to_string (key);
                }
                if (! CHECK (std::fabs (cents) <= 2.0))
                    std::printf ("    osc %d wavetable %g key %d: %.4f Hz, expected %.4f Hz (%.3f cents)\n", osc, static_cast<double> (wavetable), key, f,
                                 hzOfKey (key), cents);
            }
    }
    std::printf ("    worst error %.4f cents (%s)\n", worstCents, worst.c_str());
}

// ---------------------------------------------------------------------------------------------------------------------
// 8c. alias budget (docs/ARCHITECTURE.md 3.2)
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
/** Energy that is not in a harmonic of f0, relative to the total, in dB: 4-term Blackman-Harris spectrum of 0.83 s (2^16 points), a
    harmonic is every bin within +-8 bins (5.9 Hz) of k * f0, k = 1 .. Nyquist / f0. */
double nonHarmonicDb (const std::vector<float>& x, double f0)
{
    constexpr size_t kFft = 65536;
    const auto p = sbtest::powerSpectrum (x, 12000, 40000, kFft, sbtest::Window::blackmanHarris);
    const double binHz = kSr / static_cast<double> (kFft);
    std::vector<char> harmonic (p.size(), 0);
    for (int k = 1; k * f0 < kSr / 2.0; ++k)
    {
        const long centre = std::lround (k * f0 / binHz);
        for (long b = centre - 8; b <= centre + 8; ++b)
            if (b >= 0 && b < static_cast<long> (p.size()))
                harmonic[static_cast<size_t> (b)] = 1;
    }
    double total = 0.0, other = 0.0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        total += p[k];
        if (! harmonic[k])
            other += p[k];
    }
    return 10.0 * std::log10 (std::max (other / total, 1e-30));
}
} // namespace

TEST (alias_budget_guarded)
{
    // The alias budget of docs/ARCHITECTURE.md 3.2, measured on the unmodified engine (docs/QUALITY.md) and guarded here: the worst
    // non-harmonic energy over the keys, per mode and tier, must not exceed the measured number by more than 3 dB. The numbers are
    // the engine's own (they are NOT below -60 dB: see docs/QUALITY.md, findings). keys: every third key 21 .. 120 for the plain
    // shapes, ten keys for the spectral morphs.
    struct Mode { const char* name; float wavetable; int morphType; bool plain; double highDb, lowDb; };
    const Mode modes[] = {
        // name                    table morph plain   high    low   (the worst case measured on the unmodified engine, dB)
        { "saw",                   1, 0,  true,  -35.5, -10.9 },
        { "square",                2, 0,  true,  -34.3, -10.0 },
        { "pulse25",               4, 0,  true,  -36.0, -11.6 },
        { "saw+vocode",            1, 1,  false, -59.9, -50.6 },
        { "saw+formant scale",     1, 2,  false, -59.4, -47.9 },
        { "saw+harmonic stretch",  1, 3,  false, -64.8, -48.3 },
        { "saw+smear",             1, 5,  false, -40.5,  -2.7 },
        { "saw+random amplitudes", 1, 6,  false, -52.0, -34.0 },
        { "saw+low pass",          1, 7,  false, -67.4, -10.9 },
        { "saw+high pass",         1, 8,  false, -30.7, -16.2 },
        { "saw+phase disperse",    1, 9,  false, -51.6, -10.9 },
        { "saw+time skew",         1, 11, false, -51.6, -10.9 },
    };
    std::vector<int> plainKeys, morphKeys = { 21, 33, 45, 57, 69, 81, 93, 105, 117, 120 };
    for (int key = 21; key <= 120; key += 3)
        plainKeys.push_back (key);

    for (const Tier tier : { Tier::high, Tier::low })
    {
        std::printf ("    tier %s (oversampling %dx), 48 kHz, worst non-harmonic energy:\n", tier == Tier::high ? "high" : "low", qualityForTier (tier).oversampling);
        for (const auto& mode : modes)
        {
            auto e = makeEngine();
            pureOscillator (*e, 1, mode.wavetable);
            setByKey (*e, "osc1.morph.type", static_cast<float> (mode.morphType));
            setByKey (*e, "osc1.morph.amount", 50.0f);
            prepare (*e, tier);
            double worst = -300.0;
            int worstKey = 0;
            for (const int key : mode.plain ? plainKeys : morphKeys)
            {
                e->reset();
                const auto out = render (*e, { noteOn (0, key) }, 56000);
                const double db = nonHarmonicDb (out.left, hzOfKey (key));
                if (db > worst)
                {
                    worst = db;
                    worstKey = key;
                }
            }
            const double recorded = tier == Tier::high ? mode.highDb : mode.lowDb;
            std::printf ("      %-24s %7.1f dB at key %3d   (guard %.1f dB)\n", mode.name, worst, worstKey, recorded + 3.0);
            CHECK (worst <= recorded + 3.0);
        }
    }
}

TEST (alias_budget_of_the_inharmonic_modes_is_not_a_budget)
{
    // Inharmonic Stretch moves the partials off the harmonic series on purpose and Shepard Tone adds octave copies: "energy that is
    // not in a harmonic" is their sound, not aliasing. They are not part of the guard above; they must stay finite (morph_never_gives_nan).
    auto e = makeEngine();
    pureOscillator (*e, 1, 1.0f);
    setByKey (*e, "osc1.morph.type", 4.0f);
    setByKey (*e, "osc1.morph.amount", 50.0f);
    prepare (*e);
    const auto out = render (*e, { noteOn (0, 57) }, 24000);
    CHECK (sbtest::allFinite (out.left));
    CHECK_EQ (g_allocationsInProcess, 0ul);
}

// ---------------------------------------------------------------------------------------------------------------------
// 8d. filters
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
const char* const kModelNames[] = { "Analog", "Dirty", "Ladder", "Digital", "Diode", "Formant", "Comb", "Phaser" };

/** White noise (the noise oscillator at 100 %) through filter 1 only. */
void noiseThroughFilter (IEngine& e, int model, float cutoff, float resonance)
{
    setByKey (e, "osc1.on", 0.0f);
    setByKey (e, "noise.on", 1.0f);
    setByKey (e, "noise.level", 100.0f);
    setByKey (e, "filter1.on", 1.0f);
    setByKey (e, "filter1.model", static_cast<float> (model));
    setByKey (e, "filter1.cutoff", cutoff);
    setByKey (e, "filter1.resonance", resonance);
    setByKey (e, "master.volume", -12.0f);
}

sbtest::Rendered playNoise (IEngine& e, double seconds = 1.0)
{
    e.reset();
    return render (e, { noteOn (0, 60) }, static_cast<int> (seconds * kSr));
}

/** Power of the spectrum of x[from, from + n) between lo and hi Hz. */
double bandPower (const std::vector<float>& x, size_t from, size_t n, double lo, double hi)
{
    constexpr size_t kFft = 16384;
    const auto p = sbtest::powerSpectrum (x, from, n, kFft);
    double sum = 0.0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        const double f = static_cast<double> (k) * kSr / kFft;
        if (f >= lo && f <= hi)
            sum += p[k];
    }
    return sum;
}
} // namespace

TEST (filters_cutoff_sweep_attenuates_monotonically)
{
    // low-pass models (Analog, Dirty, Ladder, Digital, Diode): the noise that passes grows with the cutoff. 3 % tolerance for the noise estimate.
    const float cutoffs[] = { 150, 300, 600, 1200, 2400, 4800, 9600 };
    for (int model = 0; model < 5; ++model)
    {
        auto e = makeEngine();
        noiseThroughFilter (*e, model, 1000.0f, 20.0f);
        prepare (*e);
        double previous = 0.0;
        std::string line;
        for (const float cutoff : cutoffs)
        {
            setByKey (*e, "filter1.cutoff", cutoff);
            const auto out = playNoise (*e);
            const double rms = sbtest::rmsOf (out.left, 14400, 48000);
            line += " " + std::to_string (static_cast<int> (cutoff)) + ":" + std::to_string (rms).substr (0, 6);
            CHECK (std::isfinite (rms) && rms > 0.0);
            if (! CHECK (rms >= previous * 0.97))
                std::printf ("    %s: rms %g at cutoff %g after %g\n", kModelNames[model], rms, static_cast<double> (cutoff), previous);
            previous = rms;
        }
        std::printf ("    %-7s rms by cutoff:%s\n", kModelNames[model], line.c_str());
    }
}

TEST (filters_other_models_respond_to_the_cutoff_and_stay_bounded)
{
    // Formant, Comb and Phaser are not low-pass filters: the cutoff must still change the sound (centroid) and nothing may blow up
    for (int model = 5; model < 8; ++model)
    {
        auto e = makeEngine();
        noiseThroughFilter (*e, model, 500.0f, 50.0f);
        prepare (*e);
        double lowest = 1e9, highest = 0.0, peak = 0.0;
        for (const float cutoff : { 200.0f, 800.0f, 3200.0f })
        {
            setByKey (*e, "filter1.cutoff", cutoff);
            const auto out = playNoise (*e, 0.8);
            const double c = sbtest::centroidOf (out.left, 14400, 16384, kSr);
            lowest = std::min (lowest, c);
            highest = std::max (highest, c);
            peak = std::max (peak, static_cast<double> (sbtest::peak (out.left)));
            CHECK (sbtest::allFinite (out.left));
        }
        std::printf ("    %-7s centroid %.0f .. %.0f Hz over cutoff 200 .. 3200 Hz, peak %.2f\n", kModelNames[model], lowest, highest, peak);
        CHECK (peak <= 4.0);
        if (model != 5) // the formant filter is steered by X / Y, its cutoff does not move the spectrum
            CHECK (highest > 1.05 * lowest);
    }
}

TEST (filters_resonance_raises_the_peak_and_stays_bounded)
{
    for (int model = 0; model < 5; ++model)
    {
        auto e = makeEngine();
        noiseThroughFilter (*e, model, 1000.0f, 20.0f);
        prepare (*e);
        setByKey (*e, "filter1.resonance", 20.0f);
        const auto low = playNoise (*e);
        setByKey (*e, "filter1.resonance", 80.0f);
        const auto high = playNoise (*e);
        // power in the band around the cutoff, 20 % against 80 % resonance
        const double ratioDb = 10.0 * std::log10 (bandPower (high.left, 14400, 16384, 800, 1250) / bandPower (low.left, 14400, 16384, 800, 1250));
        std::printf ("    %-7s band power around the cutoff, 80 %% / 20 %% resonance: %+.1f dB\n", kModelNames[model], ratioDb);
        CHECK (ratioDb > 3.0);

        // maximum resonance: bounded, finite, no growth over time (no self-oscillation blow-up)
        setByKey (*e, "filter1.resonance", 100.0f);
        const auto max = playNoise (*e, 2.0);
        CHECK (sbtest::allFinite (max.left));
        CHECK (sbtest::peak (max.left) <= 4.0f);
        const double early = sbtest::rmsOf (max.left, 9600, 48000), late = sbtest::rmsOf (max.left, 48000, 96000);
        CHECK (late <= 2.0 * early);
    }
}

TEST (filters_cutoff_modulation_by_an_lfo_moves_the_spectral_centroid)
{
    auto e = makeEngine();
    noiseThroughFilter (*e, 3, 600.0f, 20.0f);
    setByKey (*e, "lfo1.rate", 2.0f);
    setByKey (*e, "lfo1.sync", 0.0f);
    prepare (*e);

    const auto spread = [&] (const sbtest::Rendered& out)
    {
        double lo = 1e9, hi = 0.0;
        for (size_t start = 12000; start + 8192 <= 96000; start += 4800)
        {
            const double c = sbtest::centroidOf (out.left, start, 8192, kSr);
            lo = std::min (lo, c);
            hi = std::max (hi, c);
        }
        return hi / lo;
    };

    const double steady = spread (playNoise (*e, 2.0));
    const auto slot = modSlotParams (0);
    e->setParam (paramTable()[slot.source].id, sbtest::sourceChoice ("lfo1"));
    e->setParam (paramTable()[slot.destination].id, static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (kParamFilter1Cutoff)))));
    e->setParam (paramTable()[slot.amount].id, 40.0f);
    const double moving = spread (playNoise (*e, 2.0));
    std::printf ("    centroid max / min: %.2f without, %.2f with LFO 1 on the cutoff\n", steady, moving);
    CHECK (steady < 1.15);
    CHECK (moving > 1.5);
}

// ---------------------------------------------------------------------------------------------------------------------
// 8e. modulation matrix
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
/** A steady sine (filter off): the base of the modulation tests. */
void steadySine (IEngine& e, float level = 80.0f)
{
    pureOscillator (e, 1, 0.0f);
    setByKey (e, "osc1.level", level);
    setByKey (e, "master.volume", -6.0f);
}

void wire (IEngine& e, int slotIndex, const char* source, const char* destinationKey, float amount, bool bipolar = false, bool stereo = false, bool bypass = false)
{
    const auto slot = modSlotParams (slotIndex);
    const auto set = [&] (int index, float v) { e.setParam (paramTable()[index].id, v); };
    set (slot.source, sbtest::sourceChoice (source));
    set (slot.destination, static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey (destinationKey)->id)))));
    set (slot.amount, amount);
    set (slot.bipolar, bipolar ? 1.0f : 0.0f);
    set (slot.stereo, stereo ? 1.0f : 0.0f);
    set (slot.bypass, bypass ? 1.0f : 0.0f);
}

/** RMS envelope in windows of 20 ms. */
std::vector<double> envelopeOf (const std::vector<float>& x, size_t from, size_t to)
{
    std::vector<double> env;
    for (size_t s = from; s + 960 <= to; s += 960)
        env.push_back (sbtest::rmsOf (x, s, s + 960));
    return env;
}

double correlation (const std::vector<double>& a, const std::vector<double>& b)
{
    double ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double> (a.size());
    mb /= static_cast<double> (b.size());
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        ab += (a[i] - ma) * (b[i] - mb);
        aa += (a[i] - ma) * (a[i] - ma);
        bb += (b[i] - mb) * (b[i] - mb);
    }
    return ab / std::sqrt (aa * bb + 1e-30);
}

/** Largest sample difference of two renders after the first 6000 frames. The first note of a render differs in its attack from the
    one after reset() (measured up to 0.2 for a 0.4 peak sine, the engine's first-note behaviour, docs/UPSTREAM.md), the steady part
    does not (measured below 1e-5). */
double steadyDifference (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    double m = 0.0;
    for (size_t i = 6000; i < a.left.size() && i < b.left.size(); ++i)
        m = std::max ({ m, static_cast<double> (std::fabs (a.left[i] - b.left[i])), static_cast<double> (std::fabs (a.right[i] - b.right[i])) });
    return m;
}
} // namespace

TEST (matrix_lfo_on_the_oscillator_level_is_a_tremolo)
{
    auto e = makeEngine();
    steadySine (*e);
    setByKey (*e, "lfo1.rate", 4.0f);
    setByKey (*e, "lfo1.sync", 0.0f);
    prepare (*e);
    const auto steady = envelopeOf (render (*e, { noteOn (0, 69) }, 96000).left, 24000, 96000);
    e->reset();
    wire (*e, 0, "lfo1", "osc1.level", 40.0f, true);
    const auto moving = envelopeOf (render (*e, { noteOn (0, 69) }, 96000).left, 24000, 96000);
    const auto range = [] (const std::vector<double>& v) { return *std::max_element (v.begin(), v.end()) / *std::min_element (v.begin(), v.end()); };
    std::printf ("    envelope max / min: %.3f steady, %.2f with LFO 1 on the level (%.1f dB)\n", range (steady), range (moving), sbtest::toDb (range (moving)));
    CHECK (range (steady) < 1.05); // measured 1.0x: the sine is steady
    CHECK (range (moving) > 2.0);  // a depth of at least 6 dB
}

TEST (matrix_envelope_on_the_cutoff_opens_the_filter)
{
    auto e = makeEngine();
    setByKey (*e, "filter1.cutoff", 200.0f);
    setByKey (*e, "env2.attack", 400.0f);
    setByKey (*e, "env2.decay", 10.0f);
    setByKey (*e, "env2.sustain", 100.0f);
    setByKey (*e, "env1.sustain", 100.0f);
    setByKey (*e, "osc1.wavetable", 1.0f);
    prepare (*e);
    const auto before = render (*e, { noteOn (0, 45) }, 48000);
    const double dark = sbtest::centroidOf (before.left, 24000, 8192, kSr);
    e->reset();
    wire (*e, 0, "env2", "filter1.cutoff", 60.0f);
    const auto out = render (*e, { noteOn (0, 45) }, 48000);
    const double early = sbtest::centroidOf (out.left, 1000, 4096, kSr);
    const double late = sbtest::centroidOf (out.left, 30000, 8192, kSr);
    std::printf ("    centroid: %.0f Hz without the slot (late), %.0f Hz early and %.0f Hz late with env 2 on the cutoff\n", dark, early, late);
    CHECK (late > 1.5 * early);
    CHECK (late > 1.5 * dark);
}

TEST (matrix_macro_velocity_and_mod_wheel_are_sources)
{
    // macro: level 10 % + macro 1 at 0 / 100 % through a slot of +80 %
    {
        auto e = makeEngine();
        steadySine (*e, 10.0f);
        prepare (*e);
        wire (*e, 0, "macro1", "osc1.level", 80.0f);
        setByKey (*e, "macro.1", 0.0f);
        const double zero = sbtest::rmsOf (render (*e, { noteOn (0, 69) }, 24000).left, 12000, 24000);
        setByKey (*e, "macro.1", 100.0f);
        e->reset();
        const double full = sbtest::rmsOf (render (*e, { noteOn (0, 69) }, 24000).left, 12000, 24000);
        std::printf ("    macro 1: level rms %.4f at 0 %%, %.4f at 100 %% (%.1f dB)\n", zero, full, sbtest::toDb (full / zero));
        CHECK (full > 3.0 * zero);
    }
    // velocity: the engine's own velocity tracking off, the slot does the work
    {
        auto e = makeEngine();
        steadySine (*e, 30.0f);
        setByKey (*e, "master.velocity_track", 0.0f);
        prepare (*e);
        const double plainLoud = sbtest::rmsOf (render (*e, { noteOn (0, 69, 1.0f) }, 24000).left, 12000, 24000);
        e->reset();
        const double plainSoft = sbtest::rmsOf (render (*e, { noteOn (0, 69, 0.25f) }, 24000).left, 12000, 24000);
        wire (*e, 0, "velocity", "osc1.level", 60.0f);
        e->reset();
        const double loud = sbtest::rmsOf (render (*e, { noteOn (0, 69, 1.0f) }, 24000).left, 12000, 24000);
        e->reset();
        const double soft = sbtest::rmsOf (render (*e, { noteOn (0, 69, 0.25f) }, 24000).left, 12000, 24000);
        std::printf ("    velocity: without the slot loud/soft %.3f, with it %.2f\n", plainLoud / plainSoft, loud / soft);
        CHECK_NEAR (plainLoud / plainSoft, 1.0, 0.01); // measured: tracking off, the level does not depend on the velocity
        CHECK (loud / soft > 1.5);
    }
    // mod wheel event
    {
        auto e = makeEngine();
        steadySine (*e, 10.0f);
        prepare (*e);
        wire (*e, 0, "mod_wheel", "osc1.level", 80.0f);
        const auto wheel = [] (int frame, float v) { auto ev = noteOn (frame, 0, v); ev.type = Event::ModWheel; return ev; };
        const double down = sbtest::rmsOf (render (*e, { wheel (0, 0.0f), noteOn (0, 69) }, 24000).left, 12000, 24000);
        e->reset();
        const double up = sbtest::rmsOf (render (*e, { wheel (0, 1.0f), noteOn (0, 69) }, 24000).left, 12000, 24000);
        std::printf ("    mod wheel: level rms %.4f at 0, %.4f at 1\n", down, up);
        CHECK (up > 2.0 * down);
    }
}

TEST (matrix_slot_bypass_amount_zero_and_no_destination_do_nothing)
{
    auto e = makeEngine();
    steadySine (*e);
    setByKey (*e, "lfo1.rate", 4.0f);
    setByKey (*e, "lfo1.sync", 0.0f);
    prepare (*e);
    const auto play = [&]
    {
        e->reset();
        return render (*e, { noteOn (0, 69) }, 24000);
    };
    const auto reference = play();
    const auto cleanSlot = [&]
    {
        const auto slot = modSlotParams (0);
        for (const int index : { slot.source, slot.destination })
            e->setParam (paramTable()[index].id, 0.0f);
    };
    const size_t levelChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc1.level")->id)));

    // destination none: the source is wired to nothing
    cleanSlot();
    e->setParam (paramTable()[modSlotParams (0).source].id, sbtest::sourceChoice ("lfo1"));
    e->setParam (paramTable()[modSlotParams (0).amount].id, 100.0f);
    const double noDestination = steadyDifference (reference, play());
    // source none
    cleanSlot();
    e->setParam (paramTable()[modSlotParams (0).destination].id, static_cast<float> (levelChoice));
    const double noSource = steadyDifference (reference, play());
    // bypassed slot, wired
    wire (*e, 0, "lfo1", "osc1.level", 100.0f, true, false, true);
    const double bypassed = steadyDifference (reference, play());
    // amount 0, wired
    wire (*e, 0, "lfo1", "osc1.level", 0.0f);
    const double zero = steadyDifference (reference, play());
    std::printf ("    largest steady sample difference to the unwired patch: no destination %.3g, no source %.3g, bypassed %.3g, amount 0 %.3g\n",
                 noDestination, noSource, bypassed, zero);
    // measured: all exactly 0 after the attack (a sine of peak 0.4); 1e-6 leaves room for another compiler's float contraction
    for (const double d : { noDestination, noSource, bypassed, zero })
        CHECK (d < 1.0e-6);

    // and the same slot not bypassed and with an amount does something
    wire (*e, 0, "lfo1", "osc1.level", 60.0f, true);
    CHECK (steadyDifference (reference, play()) > 0.05);
}

TEST (matrix_stereo_slot_modulates_the_channels_in_opposite_directions)
{
    auto e = makeEngine();
    steadySine (*e);
    setByKey (*e, "lfo1.rate", 4.0f);
    setByKey (*e, "lfo1.sync", 0.0f);
    prepare (*e);
    for (const bool stereo : { false, true })
    {
        wire (*e, 0, "lfo1", "osc1.level", 40.0f, true, stereo);
        e->reset();
        const auto out = render (*e, { noteOn (0, 69) }, 96000);
        const double c = correlation (envelopeOf (out.left, 24000, 96000), envelopeOf (out.right, 24000, 96000));
        std::printf ("    %s slot: correlation of the left and right envelopes %+.3f\n", stereo ? "stereo" : "mono", c);
        if (stereo)
            CHECK (c < -0.5);
        else
            CHECK (c > 0.95);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// 8f. unison
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
void unisonSaw (IEngine& e, int voices, float detune = 25.0f, float spread = 100.0f)
{
    pureOscillator (e, 1, 1.0f);
    setByKey (e, "osc1.unison.voices", static_cast<float> (voices));
    setByKey (e, "osc1.unison.detune", detune);
    setByKey (e, "osc1.unison.spread", spread);
    setByKey (e, "osc1.unison.blend", 80.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
}

double correlationOf (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
{
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < to; ++i)
    {
        ab += static_cast<double> (a[i]) * b[i];
        aa += static_cast<double> (a[i]) * a[i];
        bb += static_cast<double> (b[i]) * b[i];
    }
    return ab / std::sqrt (aa * bb + 1e-30);
}

/** Share of the power within +-2 % of the fundamental in the power between 0.5 and 1.5 x the fundamental (the width of the main line;
    the window of 1.2 s has a main lobe of +-2.7 Hz, +-1.2 % at 220 Hz). */
double lineShare (const std::vector<float>& x, double f0)
{
    constexpr size_t kFft = 131072;
    const auto p = sbtest::powerSpectrum (x, 14400, 57600, kFft, sbtest::Window::blackmanHarris);
    const double binHz = kSr / kFft;
    double narrow = 0.0, wide = 0.0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        const double f = static_cast<double> (k) * binHz;
        if (f > 0.5 * f0 && f < 1.5 * f0)
            wide += p[k];
        if (std::fabs (f - f0) < 0.02 * f0)
            narrow += p[k];
    }
    return narrow / wide;
}
} // namespace

TEST (unison_seven_voices_against_one)
{
    constexpr int kKey = 57;
    const double f0 = hzOfKey (kKey);
    auto one = makeEngine();
    unisonSaw (*one, 1);
    prepare (*one);
    const auto a = render (*one, { noteOn (0, kKey) }, 72000);
    auto seven = makeEngine();
    unisonSaw (*seven, 7);
    prepare (*seven);
    const auto b = render (*seven, { noteOn (0, kKey) }, 72000);

    // spectral spread: the detuned voices move energy out of the main line
    const double shareOne = lineShare (a.left, f0), shareSeven = lineShare (b.left, f0);
    // stereo decorrelation: the voices are spread over the stereo field
    const double corrOne = correlationOf (a.left, a.right, 24000, 72000), corrSeven = correlationOf (b.left, b.right, 24000, 72000);
    // level compensation
    const double levelDb = sbtest::toDb (sbtest::rmsOf (b.left, 24000, 72000) / sbtest::rmsOf (a.left, 24000, 72000));
    std::printf ("    line share (+-2 %% of the fundamental): %.3f for 1 voice, %.3f for 7; L/R correlation %.4f and %.4f; level of 7 against 1: %+.2f dB\n",
                 shareOne, shareSeven, corrOne, corrSeven, levelDb);
    CHECK (shareOne > 0.98);
    CHECK (shareSeven < 0.9 * shareOne); // measured 0.794 of 1.000: a fifth of the power is spread beyond +-2 %
    CHECK (corrOne > 0.9999);   // one voice, centred: L == R
    CHECK (corrSeven < 0.9);    // measured value printed above
    CHECK (std::fabs (levelDb) <= 3.0);
}

TEST (unison_detune_zero_and_spread_zero_collapse)
{
    constexpr int kKey = 57;
    auto one = makeEngine();
    unisonSaw (*one, 1);
    prepare (*one);
    const auto single = render (*one, { noteOn (0, kKey) }, 48000);

    // detune 0: seven voices on the same pitch and phase: the sound of one voice, louder by the engine's level compensation
    auto coherent = makeEngine();
    unisonSaw (*coherent, 7, 0.0f, 0.0f);
    prepare (*coherent);
    const auto c = render (*coherent, { noteOn (0, kKey) }, 48000);
    double dot = 0.0, norm = 0.0, cc = 0.0;
    for (size_t i = 12000; i < 48000; ++i)
    {
        dot += static_cast<double> (single.left[i]) * c.left[i];
        norm += static_cast<double> (single.left[i]) * single.left[i];
        cc += static_cast<double> (c.left[i]) * c.left[i];
    }
    const double scale = dot / norm; // the gain that best maps the single voice on the coherent unison
    double err = 0.0;
    for (size_t i = 12000; i < 48000; ++i)
        err += std::pow (c.left[i] - scale * single.left[i], 2.0);
    const double residual = std::sqrt (err / cc);
    std::printf ("    detune 0 / spread 0: seven voices = %.3f x one voice (%+.2f dB), residual %.2e\n", scale, sbtest::toDb (scale), residual);
    CHECK (residual < 1e-3); // a copy of the single voice up to a gain
    // spread 0: left and right are the same signal
    double lr = 0.0;
    for (size_t i = 12000; i < 48000; ++i)
        lr = std::max (lr, static_cast<double> (std::fabs (c.left[i] - c.right[i])));
    CHECK (lr < 1e-6);

    // spread 0 with detune: still mono
    auto detuned = makeEngine();
    unisonSaw (*detuned, 7, 40.0f, 0.0f);
    prepare (*detuned);
    const auto d = render (*detuned, { noteOn (0, kKey) }, 48000);
    double lr2 = 0.0;
    for (size_t i = 12000; i < 48000; ++i)
        lr2 = std::max (lr2, static_cast<double> (std::fabs (d.left[i] - d.right[i])));
    std::printf ("    spread 0 with detune: largest L - R difference %.3g\n", lr2);
    CHECK (lr2 < 1e-6);
}

// ---------------------------------------------------------------------------------------------------------------------
// 8g. effects
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
/** A held saw note (key 48) with the filter wide open: the probe of the effect tests. */
void sawProbe (IEngine& e, int key = 48)
{
    (void) key;
    pureOscillator (e, 1, 1.0f);
    setByKey (e, "filter1.on", 1.0f);
    setByKey (e, "filter1.cutoff", 12000.0f);
    setByKey (e, "master.volume", -12.0f);
}

/** Mean absolute difference in dB of two spectra over the bins where the first is within 70 dB of its peak. */
double spectralDistanceDb (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t n)
{
    constexpr size_t kFft = 16384;
    const auto pa = sbtest::powerSpectrum (a, from, n, kFft), pb = sbtest::powerSpectrum (b, from, n, kFft);
    double peak = 0.0;
    for (const double v : pa)
        peak = std::max (peak, v);
    double sum = 0.0;
    size_t count = 0;
    for (size_t k = 8; k < pa.size() / 2; ++k)
        if (pa[k] > peak * 1e-7)
        {
            sum += std::fabs (10.0 * std::log10 ((pb[k] + 1e-30) / pa[k]));
            ++count;
        }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}
} // namespace

TEST (fx_reverb_has_a_tail_after_the_note_off)
{
    auto e = makeEngine();
    sawProbe (*e);
    prepare (*e);
    const std::vector<Event> notes = { noteOn (0, 57), noteOff (14400, 57) };
    const auto dry = render (*e, notes, 72000);
    setByKey (*e, "reverb.on", 1.0f);
    setByKey (*e, "reverb.mix", 50.0f);
    setByKey (*e, "reverb.decay", 2000.0f);
    e->reset();
    const auto wet = render (*e, notes, 72000);
    // 0.3 .. 0.8 s after the note off (the release is 200 ms): silence without the reverb, a tail with it
    const double dryTail = sbtest::rmsOf (dry.left, 14400 + 14400, 14400 + 38400), wetTail = sbtest::rmsOf (wet.left, 14400 + 14400, 14400 + 38400);
    std::printf ("    rms 0.3 .. 0.8 s after the note off: %.2g without, %.4f with the reverb\n", dryTail, wetTail);
    CHECK_EQ (dryTail, 0.0);
    CHECK (wetTail > 1e-3);
    CHECK (sbtest::allFinite (wet.left) && sbtest::allFinite (wet.right));
}

TEST (fx_delay_echo_arrives_at_the_set_time)
{
    // the probe is a burst of noise (a periodic signal would correlate at every period): 30 ms, release 5 ms
    auto e = makeEngine();
    noiseThroughFilter (*e, 3, 20000.0f, 0.0f);
    setByKey (*e, "env1.release", 5.0f);
    setByKey (*e, "delay.on", 1.0f);
    setByKey (*e, "delay.sync", 0.0f);
    setByKey (*e, "delay.style", 0.0f); // mono
    setByKey (*e, "delay.mix", 50.0f);
    setByKey (*e, "delay.feedback", 0.0f);
    setByKey (*e, "delay.filter_spread", 100.0f);
    setByKey (*e, "delay.filter_cutoff", 20000.0f);
    prepare (*e);
    for (const float timeMs : { 120.0f, 250.0f, 400.0f })
    {
        setByKey (*e, "delay.time", timeMs);
        setByKey (*e, "delay.time2", timeMs);
        e->reset();
        // the echo is a second copy of the burst; the lag with the largest correlation to the burst is the delay time
        const auto out = render (*e, { noteOn (0, 60), noteOff (1440, 60) }, 48000);
        const size_t burst = 1440;
        double best = -1.0;
        size_t bestLag = 0;
        for (size_t lag = static_cast<size_t> (0.5 * timeMs * 48); lag < static_cast<size_t> (1.5 * timeMs * 48); ++lag)
        {
            double c = 0.0;
            for (size_t i = 0; i < burst; ++i)
                c += static_cast<double> (out.left[i]) * out.left[i + lag];
            if (c > best)
            {
                best = c;
                bestLag = lag;
            }
        }
        const double ms = static_cast<double> (bestLag) / 48.0;
        std::printf ("    delay time %.0f ms: echo found at %.3f ms\n", static_cast<double> (timeMs), ms);
        CHECK_NEAR (ms, static_cast<double> (timeMs), 1.0);
    }
}

TEST (fx_compressor_equalizer_and_modulation_effects_change_the_sound)
{
    // compressor: a loud saw loses peak level
    {
        auto e = makeEngine();
        sawProbe (*e);
        setByKey (*e, "master.volume", 0.0f);
        setByKey (*e, "osc1.level", 100.0f);
        prepare (*e);
        const auto plain = render (*e, { noteOn (0, 48), noteOn (0, 55) }, 48000);
        setByKey (*e, "compressor.on", 1.0f);
        setByKey (*e, "compressor.bands", 3.0f); // single band
        for (const char* band : { "low", "mid", "high" })
        {
            setByKey (*e, (std::string ("compressor.") + band + ".upper_threshold").c_str(), -30.0f);
            setByKey (*e, (std::string ("compressor.") + band + ".upper_ratio").c_str(), 100.0f);
        }
        e->reset();
        const auto squashed = render (*e, { noteOn (0, 48), noteOn (0, 55) }, 48000);
        const double a = sbtest::peak (plain.left, 24000), b = sbtest::peak (squashed.left, 24000);
        std::printf ("    compressor: peak %.3f without, %.3f with (%+.1f dB)\n", a, b, sbtest::toDb (b / a));
        CHECK (b < a * 0.9);
    }
    // equalizer: +12 dB around 1 kHz on noise
    {
        auto e = makeEngine();
        noiseThroughFilter (*e, 3, 20000.0f, 0.0f);
        prepare (*e);
        const auto flat = playNoise (*e);
        setByKey (*e, "eq.on", 1.0f);
        setByKey (*e, "eq.mid.mode", 0.0f);
        setByKey (*e, "eq.mid.cutoff", 1000.0f);
        setByKey (*e, "eq.mid.gain", 12.0f);
        setByKey (*e, "eq.mid.resonance", 60.0f);
        const auto boosted = playNoise (*e);
        const double gain = 10.0 * std::log10 (bandPower (boosted.left, 14400, 16384, 800, 1250) / bandPower (flat.left, 14400, 16384, 800, 1250));
        const double away = 10.0 * std::log10 (bandPower (boosted.left, 14400, 16384, 6000, 9000) / bandPower (flat.left, 14400, 16384, 6000, 9000));
        std::printf ("    equalizer, mid band +12 dB at 1 kHz: %+.1f dB around 1 kHz, %+.1f dB at 6 .. 9 kHz\n", gain, away);
        CHECK (gain > 6.0);
        CHECK (std::fabs (away) < 3.0);
    }
    // chorus, flanger, phaser: each changes the spectrum of a held saw
    for (const char* fx : { "chorus", "flanger", "phaser", "distortion" })
    {
        auto e = makeEngine();
        sawProbe (*e);
        prepare (*e);
        const auto off = render (*e, { noteOn (0, 48) }, 48000);
        setByKey (*e, (std::string (fx) + ".on").c_str(), 1.0f);
        if (std::string (fx) == "distortion")
            setByKey (*e, "distortion.drive", 20.0f);
        e->reset();
        const auto on = render (*e, { noteOn (0, 48) }, 48000);
        const double distance = spectralDistanceDb (off.left, on.left, 14400, 16384);
        std::printf ("    %-10s mean spectral change %.2f dB\n", fx, distance);
        CHECK (distance > 0.5);
        CHECK (sbtest::allFinite (on.left) && sbtest::allFinite (on.right));
    }
}

TEST (fx_order_changes_the_output)
{
    // distortion then delay against delay then distortion: the echo of a distorted signal is not the distorted echo of a clean one
    const auto play = [] (float firstSlot, float secondSlot)
    {
        auto e = makeEngine();
        sawProbe (*e);
        setByKey (*e, "master.volume", -18.0f);
        setByKey (*e, "distortion.on", 1.0f);
        setByKey (*e, "distortion.drive", 24.0f);
        setByKey (*e, "delay.on", 1.0f);
        setByKey (*e, "delay.sync", 0.0f);
        setByKey (*e, "delay.time", 100.0f);
        setByKey (*e, "delay.feedback", 60.0f);
        setByKey (*e, "delay.mix", 50.0f);
        // slots 1 and 2 hold the two effects; the other seven keep the rest in the natural order
        setByKey (*e, "fx.order.1", firstSlot);
        setByKey (*e, "fx.order.2", secondSlot);
        int slot = 3;
        for (int fx = 0; fx < kNumLegacyFxSlots; ++fx)
            if (fx != static_cast<int> (firstSlot) && fx != static_cast<int> (secondSlot))
                setByKey (*e, ("fx.order." + std::to_string (slot++)).c_str(), static_cast<float> (fx));
        prepare (*e);
        return render (*e, { noteOn (0, 48), noteOff (24000, 48) }, 72000);
    };
    const auto a = play (3.0f, 2.0f); // distortion, delay
    const auto b = play (2.0f, 3.0f); // delay, distortion
    double diff = 0.0, energy = 0.0;
    for (size_t i = 0; i < a.left.size(); ++i)
    {
        diff += std::pow (static_cast<double> (a.left[i]) - b.left[i], 2.0);
        energy += static_cast<double> (a.left[i]) * a.left[i];
    }
    const double relative = std::sqrt (diff / energy);
    std::printf ("    distortion >> delay against delay >> distortion: relative RMS difference %.3f\n", relative);
    CHECK (sbtest::allFinite (a.left) && sbtest::allFinite (b.left));
    CHECK (relative > 0.05);

    // an invalid order (duplicates) still gives a valid permutation: all effects run, output finite
    auto e = makeEngine();
    sawProbe (*e);
    for (int i = 1; i <= kNumLegacyFxSlots; ++i)
        setByKey (*e, ("fx.order." + std::to_string (i)).c_str(), 2.0f);
    setByKey (*e, "delay.on", 1.0f);
    setByKey (*e, "reverb.on", 1.0f);
    prepare (*e);
    CHECK (sbtest::allFinite (render (*e, { noteOn (0, 48) }, 24000).left));
}

TEST (all_effect_tests_were_allocation_free)
{
    CHECK_EQ (g_allocationsInProcess, 0ul); // g_allocationsInProcess counts every render of this executable that ran before this test
}

// ---------------------------------------------------------------------------------------------------------------------
// 8h. polyphony, stealing, legato, portamento, pitch bend, all notes off
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
/** Pitch of x around frame `at` in semitones against A4, from the rising zero crossings in a window of about three periods of f. */
double semitonesAt (const std::vector<float>& x, size_t at, double f)
{
    const size_t window = static_cast<size_t> (3.0 * kSr / f);
    int count = 0;
    double first = 0.0, last = 0.0;
    for (size_t i = at + 1; i < at + window && i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1) + static_cast<double> (-x[i - 1]) / static_cast<double> (x[i] - x[i - 1]);
            if (count++ == 0)
                first = t;
            last = t;
        }
    return count < 2 ? 0.0 : 12.0 * std::log2 ((count - 1) * kSr / (last - first) / 440.0);
}

void steadySineNote (IEngine& e)
{
    pureOscillator (e, 1, 0.0f);
    setByKey (e, "osc1.level", 60.0f);
    setByKey (e, "env1.attack", 2.0f);
}
} // namespace

TEST (voices_polyphony_parameter_and_stealing)
{
    auto e = makeEngine();
    steadySineNote (*e);
    setByKey (*e, "master.polyphony", 4.0f);
    prepare (*e);
    std::vector<Event> notes;
    for (int i = 0; i < 8; ++i)
        notes.push_back (noteOn (i, 48 + 3 * i));
    render (*e, notes, 24000);
    std::printf ("    polyphony 4, eight notes held: %d voices sound\n", e->activeVoices());
    CHECK_EQ (e->activeVoices(), 4);
    CHECK_EQ (e->getParam (findParamByKey ("master.polyphony")->id), 4.0f); // the stored value

    // stealing takes the oldest note: with two voices and the keys 60, 64, 67 the last two remain
    auto two = makeEngine();
    steadySineNote (*two);
    setByKey (*two, "master.polyphony", 2.0f);
    prepare (*two);
    const auto out = render (*two, { noteOn (0, 60), noteOn (200, 64), noteOn (400, 67) }, 48000);
    const auto p = sbtest::powerSpectrum (out.left, 24000, 24000, 65536);
    const auto level = [&] (int key) { return std::sqrt (p[static_cast<size_t> (hzOfKey (key) / (kSr / 65536.0))]); };
    std::printf ("    stealing: level of key 60 / 64 / 67 = %.3g / %.3g / %.3g\n", level (60), level (64), level (67));
    CHECK (level (60) < 0.02 * level (64));
    CHECK (level (64) > 0.0 && level (67) > 0.0);
    CHECK_EQ (two->activeVoices(), 2);

    // all notes off: every voice releases and ends in silence
    const auto events = std::vector<Event> { noteOn (0, 60), noteOn (0, 64), sbtest::allNotesOff (4800) };
    two->reset();
    const auto released = render (*two, events, 48000);
    CHECK_EQ (sbtest::peak (released.left, 4800 + 24000), 0.0f);
    CHECK_EQ (two->activeVoices(), 0);
}

TEST (voices_legato_does_not_restart_the_envelope)
{
    // one voice and a slow attack: a second overlapping note restarts the attack unless the legato mode is on
    const auto levelAfterSecondNote = [] (bool legato)
    {
        auto e = makeEngine();
        steadySineNote (*e);
        setByKey (*e, "master.polyphony", 1.0f); // legato only matters when the voices run out
        setByKey (*e, "env1.attack", 600.0f);
        setByKey (*e, "master.legato", legato ? 1.0f : 0.0f);
        prepare (*e);
        const auto out = render (*e, { noteOn (0, 60), noteOn (48000, 64) }, 72000);
        return sbtest::rmsOf (out.left, 48000 + 2400, 48000 + 4800) / sbtest::rmsOf (out.left, 48000 - 2400, 48000);
    };
    const double with = levelAfterSecondNote (true), without = levelAfterSecondNote (false);
    std::printf ("    level right after the second note / before it: %.3f with legato, %.3f without\n", with, without);
    CHECK (with > 0.8);
    CHECK (without < 0.5 * with); // the new voice starts its attack from silence
}

TEST (voices_portamento_reaches_ninety_percent_in_ninety_percent_of_the_time)
{
    // the time to cross 90 % of the interval (60 -> 72, an octave) with portamento 300 ms, linear slope, "always" glide
    for (const float timeMs : { 150.0f, 300.0f, 600.0f })
    {
        auto e = makeEngine();
        steadySineNote (*e);
        setByKey (*e, "master.portamento.time", timeMs);
        setByKey (*e, "master.portamento.slope", 0.0f);
        setByKey (*e, "master.portamento.always", 1.0f);
        setByKey (*e, "master.polyphony", 1.0f); // one voice: the glide is the pitch of that voice
        setByKey (*e, "master.legato", 1.0f);
        prepare (*e);
        const int start = 24000;
        const auto out = render (*e, { noteOn (0, 60), noteOn (start, 72) }, start + 48000);
        const double from = -9.0, to = 3.0; // semitones against A4
        double reached = -1.0;
        for (size_t at = start; at < start + 40000; at += 96)
        {
            const double guess = 261.6 * std::pow (2.0, std::min (1.0, static_cast<double> (at - start) / (timeMs * 48.0)));
            const double st = semitonesAt (out.left, at, guess);
            if (st > from + 0.9 * (to - from))
            {
                reached = static_cast<double> (at - start + 184) / 48.0; // the window is centred 1.5 periods (about 3.8 ms) after `at`
                break;
            }
        }
        std::printf ("    portamento %.0f ms: 90 %% of the octave reached after %.1f ms (%.1f %% of the set time; measured 90 %% .. 92 %%)\n",
                     static_cast<double> (timeMs), reached, 100.0 * reached / static_cast<double> (timeMs));
        CHECK (reached > 0.0);
        CHECK_NEAR (reached, 0.9 * static_cast<double> (timeMs), 0.1 * static_cast<double> (timeMs));
    }
}

TEST (voices_pitch_bend_range)
{
    for (const float range : { 2.0f, 7.0f, 12.0f, 24.0f })
        for (const float bend : { 1.0f, -0.5f })
        {
            auto e = makeEngine();
            steadySineNote (*e);
            setByKey (*e, "master.bend_range", range);
            prepare (*e);
            const auto out = render (*e, { noteOn (0, 69), sbtest::pitchBend (4800, bend) }, 60000);
            const auto p = sbtest::powerSpectrum (out.left, 24000, 32768, 131072);
            const double expected = 440.0 * std::pow (2.0, static_cast<double> (bend * range) / 12.0);
            const double f = sbtest::peakFrequency (p, 131072, kSr, expected);
            const double cents = 1200.0 * std::log2 (f / expected);
            if (! CHECK (std::fabs (cents) <= 2.0))
                std::printf ("    range %g, bend %g: %.3f Hz, expected %.3f Hz (%.2f cents)\n", static_cast<double> (range), static_cast<double> (bend), f, expected, cents);
        }
}

// ---------------------------------------------------------------------------------------------------------------------
// 8i. tiers
// ---------------------------------------------------------------------------------------------------------------------
TEST (tiers_low_caps_voices_and_unison_without_changing_the_patch)
{
    // a patch that asks for the sky: 32 voices, 16 unison voices on all three oscillators
    const auto patch = [] (IEngine& e)
    {
        setByKey (e, "master.polyphony", 32.0f);
        for (int i = 1; i <= 3; ++i)
        {
            setByKey (e, ("osc" + std::to_string (i) + ".on").c_str(), 1.0f);
            setByKey (e, ("osc" + std::to_string (i) + ".wavetable").c_str(), 1.0f);
            setByKey (e, ("osc" + std::to_string (i) + ".unison.voices").c_str(), 16.0f);
            setByKey (e, ("osc" + std::to_string (i) + ".transpose").c_str(), static_cast<float> ((i - 2) * 12));
        }
        setByKey (e, "filter1.cutoff", 3000.0f);
    };
    auto low = makeEngine(), high = makeEngine();
    patch (*low);
    patch (*high);
    prepare (*low, Tier::low);
    prepare (*high, Tier::high);

    std::vector<Event> notes;
    for (int i = 0; i < 20; ++i)
        notes.push_back (noteOn (i, 40 + i));
    const auto a = render (*low, notes, 48000);
    const auto b = render (*high, notes, 48000);
    std::printf ("    20 notes held: %d voices on the low tier, %d on the high tier\n", low->activeVoices(), high->activeVoices());
    CHECK_EQ (low->activeVoices(), 8);
    CHECK_EQ (high->activeVoices(), 20);
    // the stored patch is what was asked for
    CHECK_EQ (low->getParam (findParamByKey ("master.polyphony")->id), 32.0f);
    CHECK_EQ (low->getParam (findParamByKey ("osc3.unison.voices")->id), 16.0f);
    CHECK (sbtest::allFinite (a.left) && sbtest::allFinite (b.left));
}

TEST (tiers_the_same_patch_sounds_alike_on_low_and_high)
{
    // a patch inside the low tier's limits (4 unison voices, 8 notes): spectral centroid within 15 % between the tiers
    for (const float wavetable : { 1.0f, 2.0f, 9.0f })
    {
        const auto play = [&] (Tier tier)
        {
            auto e = makeEngine();
            setByKey (*e, "osc1.wavetable", wavetable);
            setByKey (*e, "osc1.unison.voices", 4.0f);
            setByKey (*e, "osc1.unison.detune", 20.0f);
            setByKey (*e, "filter1.cutoff", 4000.0f);
            setByKey (*e, "filter1.resonance", 30.0f);
            prepare (*e, tier);
            const auto out = render (*e, { noteOn (0, 45), noteOn (0, 52), noteOn (0, 57), noteOn (0, 64) }, 72000);
            return sbtest::centroidOf (out.left, 24000, 16384, kSr);
        };
        const double lowCentroid = play (Tier::low), highCentroid = play (Tier::high);
        const double ratio = lowCentroid / highCentroid;
        std::printf ("    wavetable %g: centroid %.0f Hz on the low tier, %.0f Hz on the high tier (ratio %.3f)\n", static_cast<double> (wavetable), lowCentroid,
                     highCentroid, ratio);
        CHECK (ratio > 0.85 && ratio < 1.15);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// 8j. state
// ---------------------------------------------------------------------------------------------------------------------
TEST (state_random_walk_saves_loads_and_renders_the_same)
{
    // A random walk over the whole table (300 steps of 12 parameters each, a few of them in the matrix), the end point saved with
    // saveState, loaded into a fresh engine: both render the same notes bit for bit. The engines are prepared with the same
    // quality and their random generators are seeded from a counter that prepare() resets (docs/UPSTREAM.md), so random LFOs, the noise
    // and the random phase are reproducible too.
    sbtest::Rng rng (2025); // (a seed that leaves the random walk audible with the 1558-key table; 2024 drew a silent patch after B17)
    ParamValues walk = ParamValues::defaults();
    for (int step = 0; step < 300; ++step)
        for (int k = 0; k < 12; ++k)
        {
            const size_t i = rng.u32() % paramCount();
            const auto& info = paramTable()[i];
            walk.plain[i] = constrainValue (info, info.min + static_cast<float> (rng.uniform()) * (info.max - info.min));
        }
    // make the random elements audible: a sound making core (envelope, level, routing past the filters, volume) and the random sources on
    const struct { const char* key; float value; } core[] = {
        { "osc1.on", 1.0f }, { "osc1.level", 60.0f }, { "osc1.destination", 3.0f }, { "osc1.random_phase", 100.0f }, { "noise.on", 1.0f },
        { "noise.level", 30.0f }, { "noise.destination", 3.0f }, { "env1.delay", 0.0f }, { "env1.hold", 0.0f }, { "env1.attack", 5.0f },
        { "env1.sustain", 100.0f }, { "env1.release", 100.0f }, { "master.volume", -12.0f }, { "master.polyphony", 16.0f },
        { "filter.routing", 0.0f }, { "osc1.warp.type", 0.0f }, { "reverb.mix", 20.0f }
    };
    for (const auto& c : core)
        walk.plain[static_cast<size_t> (findParamIndex (findParamByKey (c.key)->id))] = c.value;

    const auto saved = saveState (walk);
    ParamValues loaded = ParamValues::defaults();
    CHECK (loadState (saved.data(), saved.size(), loaded).status == LoadStatus::ok);
    CHECK (std::memcmp (walk.plain, loaded.plain, sizeof walk.plain) == 0);

    const auto play = [] (const ParamValues& values, Tier tier)
    {
        auto e = createEngine (EngineKind::vital);
        for (size_t i = 0; i < paramCount(); ++i)
            e->setParam (paramTable()[i].id, values.plain[i]);
        e->prepare (kSr, kBlock, qualityForTier (tier));
        std::vector<Event> notes = { noteOn (0, 48), noteOn (2000, 60, 0.7f), noteOn (5000, 72), noteOff (30000, 60), noteOff (40000, 48), noteOff (50000, 72) };
        return render (*e, notes, 72000);
    };
    for (const Tier tier : { Tier::high, Tier::low })
    {
        const auto original = play (walk, tier);
        // another engine in between: the counter of one must not leak into the next
        play (ParamValues::defaults(), tier);
        const auto restored = play (loaded, tier);
        const bool same = std::memcmp (original.left.data(), restored.left.data(), original.left.size() * sizeof (float)) == 0
                          && std::memcmp (original.right.data(), restored.right.data(), original.right.size() * sizeof (float)) == 0;
        std::printf ("    tier %s: output after save / load is %s (peak %.3f, rms %.4f)\n", tier == Tier::high ? "high" : "low",
                     same ? "bit identical" : "DIFFERENT", static_cast<double> (sbtest::peak (original.left)), sbtest::rmsOf (original.left, 0, 72000));
        CHECK (same);
        CHECK (sbtest::allFinite (original.left) && sbtest::peak (original.left) > 0.0f);
    }
    CHECK_EQ (g_allocationsInProcess, 0ul);
}
