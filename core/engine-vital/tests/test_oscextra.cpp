// SynthyBoy - engine-vital tests: B18 osc-extras (G23 sub oscillator, G38 noise colours & extras)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Proves kCapSubOsc and kCapNoiseExtras: every key of the two bits is rendered off and on and must make a measured difference
// (a spectrum, a pitch, a level, a stereo position or a routing), and the defaults must leave the engine exactly as it was.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_bindings.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;
constexpr size_t kFft = 16384;

/** A settings list that renders on a fresh engine every time: the Vital sample oscillator keeps interpolation history per voice, so
    two notes on one engine are never sample identical, two fresh engines with the same settings are. */
class Synth
{
public:
    void set (const char* key, float value) { settings.emplace_back (key, value); }

    std::unique_ptr<IEngine> build() const
    {
        auto e = createEngine (EngineKind::vital);
        e->setParam (kParamMasterPolyphony, 32.0f);
        e->prepare (kSr, 256, Quality {});
        for (const auto& s : settings)
            setByKey (*e, s.first.c_str(), s.second);
        return e;
    }

    sbtest::Rendered play (double seconds = 0.6, int key = 48) const
    {
        return playEvents ({ noteOn (0, key) }, seconds);
    }

    sbtest::Rendered playEvents (const std::vector<Event>& events, double seconds) const
    {
        auto e = build();
        return sbtest::render (*e, events, static_cast<int> (seconds * kSr), 256);
    }

private:
    std::vector<std::pair<std::string, float>> settings;
};

/** Oscillators 1..3 and the noise off, both filters off, master volume unity: only the key under test makes sound. */
void silenceMain (Synth& e)
{
    e.set ("osc1.on", 0.0f);
    e.set ("osc2.on", 0.0f);
    e.set ("osc3.on", 0.0f);
    e.set ("noise.on", 0.0f);
    e.set ("filter1.on", 0.0f);
    e.set ("filter2.on", 0.0f);
    e.set ("master.volume", 0.0f);
}

/** Sub on, 100 %, straight to the effects (no filter). */
void subOnly (Synth& e)
{
    silenceMain (e);
    e.set ("sub.on", 1.0f);
    e.set ("sub.level", 100.0f);
    e.set ("sub.destination", 3.0f);
    e.set ("sub.octave", 0.0f);
}

/** Noise on, 50 %, straight to the effects (no filter). */
void noiseOnly (Synth& e)
{
    silenceMain (e);
    e.set ("noise.on", 1.0f);
    e.set ("noise.level", 50.0f);
    e.set ("noise.destination", 3.0f);
}

double rms (const sbtest::Rendered& r, double t0 = 0.15, double t1 = 0.55)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0 = 0.15, double t1 = 0.55)
{
    double d = 0.0, n = 0.0;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < a.left.size() && i < b.left.size(); ++i)
    {
        const double x = static_cast<double> (a.left[i]) - b.left[i];
        d += x * x;
        n += static_cast<double> (a.left[i]) * a.left[i];
    }
    return n > 0.0 ? std::sqrt (d / n) : (d > 0.0 ? 1.0e9 : 0.0);
}

bool identical (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    return a.left == b.left && a.right == b.right;
}

double centroid (const sbtest::Rendered& r)
{
    return sbtest::centroidOf (r.left, static_cast<size_t> (0.15 * kSr), kFft, kSr, kFft);
}

std::vector<double> spectrumOf (const std::vector<float>& x, double from = 0.15, size_t n = kFft)
{
    return sbtest::powerSpectrum (x, static_cast<size_t> (from * kSr), n, kFft, sbtest::Window::blackmanHarris);
}

/** Power within +-5 bins of a frequency. */
double powerAt (const std::vector<double>& p, double hz)
{
    const long centre = std::lround (hz * static_cast<double> (kFft) / kSr);
    double sum = 0.0;
    for (long k = std::max (1L, centre - 5); k <= centre + 5 && k < static_cast<long> (p.size()); ++k)
        sum += p[static_cast<size_t> (k)];
    return sum;
}

double dbBetween (const std::vector<double>& p, double hzA, double hzB)
{
    return 10.0 * std::log10 (std::max (powerAt (p, hzA), 1.0e-30) / std::max (powerAt (p, hzB), 1.0e-30));
}

/** Level of harmonic n relative to the fundamental, in dB. */
double harmonicDb (const std::vector<double>& p, double f0, int n)
{
    return dbBetween (p, f0 * n, f0);
}

/** The power that is not within +-5 bins of a harmonic of f0 (aliasing and noise), relative to the total, in dB. */
double inharmonicDb (const std::vector<double>& p, double f0)
{
    std::vector<bool> harmonic (p.size(), false);
    for (int n = 1; n * f0 < kSr * 0.5; ++n)
    {
        const long centre = std::lround (n * f0 * static_cast<double> (kFft) / kSr);
        for (long k = centre - 5; k <= centre + 5; ++k)
            if (k >= 0 && k < static_cast<long> (p.size()))
                harmonic[static_cast<size_t> (k)] = true;
    }
    double other = 0.0, total = 0.0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        total += p[k];
        if (! harmonic[k])
            other += p[k];
    }
    return 10.0 * std::log10 (std::max (other, 1.0e-30) / std::max (total, 1.0e-30));
}

double meanOf (const sbtest::Rendered& r)
{
    double sum = 0.0;
    const size_t a = static_cast<size_t> (0.15 * kSr), b = static_cast<size_t> (0.55 * kSr);
    for (size_t i = a; i < b; ++i)
        sum += r.left[i];
    return sum / static_cast<double> (b - a);
}

double midiHz (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}

/** Filter 1 low pass at 300 Hz, filter 2 at 8 kHz: two clearly different filters. */
void twoFilters (Synth& e)
{
    e.set ("filter1.on", 1.0f);
    e.set ("filter1.cutoff", 300.0f);
    e.set ("filter2.on", 1.0f);
    e.set ("filter2.cutoff", 8000.0f);
}

/** mod01: macro 1 -> destination key, at an amount in percent. */
void route (Synth& e, const char* destinationKey, float amountPercent)
{
    const ParamInfo* destination = findParamByKey (destinationKey);
    CHECK (destination != nullptr);
    e.set ("mod01.source", sbtest::sourceChoice ("macro1"));
    e.set ("mod01.destination", destination != nullptr ? static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (destination->id)))) : 0.0f);
    e.set ("mod01.amount", amountPercent);
}
} // namespace

TEST (oscextra_capabilities_are_reported)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities() & kCapSubOsc) != 0);
    CHECK ((e->capabilities() & kCapNoiseExtras) != 0);
    auto own = createEngine (EngineKind::own);
    CHECK ((own->capabilities() & ((kCapSubOsc | kCapNoiseExtras)) & ~createEngine (EngineKind::vital)->capabilities()) == 0); // the own engine reports a bit only if its own probe proves it (test_own_*.cpp), never one that this engine lacks
}

TEST (oscextra_defaults_leave_the_engine_unchanged)
{
    // The sub is off by default and the noise defaults are the stock Vital noise: the new keys at their defaults, or changed while
    // their switch is off, change not one sample.
    Synth e;
    silenceMain (e);
    e.set ("noise.on", 1.0f);
    e.set ("noise.level", 70.0f);
    e.set ("noise.destination", 3.0f);
    const auto reference = e.play();
    CHECK (rms (reference) > 0.01);

    e.set ("sub.waveform", 3.0f);
    e.set ("sub.level", 100.0f);
    e.set ("sub.octave", 2.0f);
    e.set ("sub.destination", 4.0f);
    e.set ("sub.bus1_send", 100.0f);
    CHECK (identical (reference, e.play())); // sub.on is still 0

    e.set ("noise.sample", 0.0f);
    e.set ("noise.pitch", 0.0f);
    e.set ("noise.fine", 0.0f);
    e.set ("noise.phase", 0.0f);
    e.set ("noise.random_phase", 0.0f);
    e.set ("noise.keytrack", 0.0f);
    e.set ("noise.loop", 1.0f);
    CHECK (identical (reference, e.play()));
}

TEST (sub_osc_on_off_and_level)
{
    Synth e;
    subOnly (e);

    e.set ("sub.on", 0.0f);
    CHECK (rms (e.play()) < 1.0e-4); // off: silence

    e.set ("sub.on", 1.0f);
    const auto loud = e.play();
    CHECK (rms (loud) > 0.1);

    e.set ("sub.level", 25.0f);
    const auto quieter = e.play();
    CHECK (rms (quieter) < rms (loud) * 0.6);
    CHECK (rms (quieter) > 0.01);

    e.set ("sub.level", 0.0f);
    CHECK (rms (e.play()) < 1.0e-4);
}

TEST (sub_osc_waveforms_have_their_designed_spectra)
{
    Synth e;
    subOnly (e);
    const int note = 84; // 1046.5 Hz: the aliasing of a naive saw / square / pulse would be plain
    const double f0 = midiHz (note);
    const char* const names[] = { "sine", "rounded rect", "triangle", "saw", "square", "pulse" };

    std::vector<sbtest::Rendered> renders;
    std::vector<std::vector<double>> spectra;
    for (int w = 0; w < 6; ++w)
    {
        e.set ("sub.waveform", static_cast<float> (w));
        renders.push_back (e.play (0.7, note));
        spectra.push_back (spectrumOf (renders.back().left));
        if (! CHECK (rms (renders.back()) > 0.1))
            std::printf ("    waveform %s\n", names[w]);
        // no DC, no stray energy between the harmonics (the saw, square and pulse are PolyBLEP band-limited)
        CHECK (std::fabs (meanOf (renders.back())) < 0.02 * rms (renders.back()) + 1.0e-4);
        const double stray = inharmonicDb (spectra.back(), f0);
        if (! CHECK (stray < -30.0)) // a naive saw / square sits near -16 dB
            std::printf ("    waveform %s: %.1f dB between the harmonics\n", names[w], stray);
    }

    // sine: a single line
    CHECK (harmonicDb (spectra[0], f0, 2) < -45.0);
    CHECK (harmonicDb (spectra[0], f0, 3) < -45.0);
    // rounded rect: odd harmonics only (a symmetric soft clip), the third clearly above a sine
    CHECK (harmonicDb (spectra[1], f0, 2) < -40.0);
    CHECK (std::fabs (harmonicDb (spectra[1], f0, 3) + 12.57) < 1.5); // tanh (3 sin): third at -12.6 dB, fifth at -21.9 dB
    CHECK (std::fabs (harmonicDb (spectra[1], f0, 5) + 21.86) < 1.5);
    // triangle: odd harmonics at 1 / n^2: the third at -19.1 dB
    CHECK (std::fabs (harmonicDb (spectra[2], f0, 3) + 19.1) < 1.5);
    CHECK (harmonicDb (spectra[2], f0, 2) < -35.0);
    // saw: every harmonic at 1 / n: 2nd at -6.0 dB, 3rd at -9.5 dB
    CHECK (std::fabs (harmonicDb (spectra[3], f0, 2) + 6.02) < 1.0);
    CHECK (std::fabs (harmonicDb (spectra[3], f0, 3) + 9.54) < 1.0);
    // square: odd harmonics at 1 / n: 3rd at -9.5 dB, no 2nd
    CHECK (harmonicDb (spectra[4], f0, 2) < -35.0);
    CHECK (std::fabs (harmonicDb (spectra[4], f0, 3) + 9.54) < 1.0);
    // pulse (25 %): |sin (n pi / 4)| / n, so the 4th harmonic is missing and the 2nd sits 3.0 dB under the 1st
    CHECK (harmonicDb (spectra[5], f0, 4) < -35.0);
    CHECK (std::fabs (harmonicDb (spectra[5], f0, 2) + 3.01) < 1.0);

    // all six shapes are distinct
    for (size_t i = 0; i < renders.size(); ++i)
        for (size_t j = i + 1; j < renders.size(); ++j)
            if (! CHECK (relativeDifference (renders[i], renders[j]) > 0.05))
                std::printf ("    %s vs %s\n", names[i], names[j]);
}

TEST (sub_osc_octave_and_tune_set_the_pitch)
{
    Synth e;
    subOnly (e);
    const double base = midiHz (60);
    auto pitchOf = [&] (float octave, float tune)
    {
        e.set ("sub.octave", octave);
        e.set ("sub.tune", tune);
        const auto r = e.play (0.7, 60);
        return sbtest::peakFrequency (spectrumOf (r.left), kFft, kSr, base * std::pow (2.0, octave + tune / 12.0));
    };
    CHECK (std::fabs (pitchOf (0.0f, 0.0f) / base - 1.0) < 0.003);
    CHECK (std::fabs (pitchOf (-1.0f, 0.0f) / (base * 0.5) - 1.0) < 0.003);
    CHECK (std::fabs (pitchOf (1.0f, 0.0f) / (base * 2.0) - 1.0) < 0.003);
    CHECK (std::fabs (pitchOf (-2.0f, 0.0f) / (base * 0.25) - 1.0) < 0.003);
    // sub.tune is the CRS control of the manual (p.124): continuous semitones
    CHECK (std::fabs (pitchOf (0.0f, 7.0f) / (base * std::pow (2.0, 7.0 / 12.0)) - 1.0) < 0.003);
    CHECK (std::fabs (pitchOf (0.0f, 0.5f) / (base * std::pow (2.0, 0.5 / 12.0)) - 1.0) < 0.003);
    CHECK (std::fabs (pitchOf (0.0f, -12.0f) / (base * 0.5) - 1.0) < 0.003);
}

TEST (sub_osc_pan_and_destination)
{
    Synth e;
    subOnly (e);
    e.set ("sub.destination", 4.0f); // Direct out

    auto sides = [&] (float pan)
    {
        e.set ("sub.pan", pan);
        const auto r = e.play();
        return std::make_pair (sbtest::rmsOf (r.left, static_cast<size_t> (0.15 * kSr), static_cast<size_t> (0.55 * kSr)),
                               sbtest::rmsOf (r.right, static_cast<size_t> (0.15 * kSr), static_cast<size_t> (0.55 * kSr)));
    };
    const auto left = sides (-100.0f);
    CHECK (left.first > 0.05);
    CHECK (left.second < 1.0e-3);
    const auto right = sides (100.0f);
    CHECK (right.first < 1.0e-3);
    CHECK (right.second > 0.05);
    const auto centre = sides (0.0f);
    CHECK (std::fabs (centre.first - centre.second) < 1.0e-4);
    CHECK (centre.first > 0.05);

    // the other destinations: Effects only is the unfiltered sound, None is silent, Filter 1 / 2 / both pass through the filters
    e.set ("sub.pan", 0.0f);
    e.set ("sub.waveform", 3.0f); // saw: plenty of harmonics for the filters to bite on
    e.set ("sub.destination", 3.0f);
    const auto effects = e.play (0.6, 69);
    CHECK (rms (effects) > 0.1);
    e.set ("sub.destination", 6.0f);
    CHECK (rms (e.play (0.6, 69)) < 1.0e-4);

    twoFilters (e);
    e.set ("sub.destination", 0.0f);
    const auto only1 = e.play (0.6, 69);
    e.set ("sub.destination", 1.0f);
    const auto only2 = e.play (0.6, 69);
    e.set ("sub.destination", 2.0f);
    const auto both = e.play (0.6, 69);
    CHECK (rms (only1) > 1.0e-3);
    CHECK (relativeDifference (only1, only2) > 0.5);
    CHECK (relativeDifference (only1, effects) > 0.5);
    CHECK (relativeDifference (both, only1) > 0.1);
    CHECK (relativeDifference (both, only2) > 0.1);
}

TEST (sub_osc_filter_blend_crossfades_the_two_filters)
{
    Synth e;
    subOnly (e);
    e.set ("sub.waveform", 3.0f);
    twoFilters (e);
    e.set ("sub.destination", 0.0f);
    const auto only1 = e.play (0.6, 69);
    e.set ("sub.destination", 1.0f);
    const auto only2 = e.play (0.6, 69);
    e.set ("sub.destination", 5.0f); // Filter blend
    e.set ("sub.filter_blend", 0.0f);
    const auto blend0 = e.play (0.6, 69);
    e.set ("sub.filter_blend", 100.0f);
    const auto blend100 = e.play (0.6, 69);
    e.set ("sub.filter_blend", 50.0f);
    const auto blend50 = e.play (0.6, 69);
    CHECK (relativeDifference (only1, only2) > 0.5);
    CHECK (relativeDifference (only1, blend0) < 0.02);   // 0 % = filter 1 only
    CHECK (relativeDifference (only2, blend100) < 0.02); // 100 % = filter 2 only
    CHECK (relativeDifference (only1, blend50) > 0.3);
    CHECK (relativeDifference (only2, blend50) > 0.1);
}

TEST (sub_osc_bus_sends_feed_the_buses)
{
    Synth e;
    subOnly (e);
    e.set ("sub.destination", 6.0f); // nothing to the effects: the sound can only come through a bus
    CHECK (rms (e.play()) < 1.0e-4);
    for (const char* send : { "sub.bus1_send", "sub.bus2_send" })
    {
        e.set ("sub.bus1_send", 0.0f);
        e.set ("sub.bus2_send", 0.0f);
        e.set (send, 100.0f);
        const auto viaBus = e.play();
        if (! CHECK (rms (viaBus) > 0.05))
            std::printf ("    %s\n", send);
        e.set (send, 50.0f);
        const auto half = e.play();
        CHECK (rms (half) < rms (viaBus) * 0.8);
        CHECK (rms (half) > 0.01);
    }
    // the bus level is the master of the bus
    e.set ("sub.bus1_send", 100.0f);
    e.set ("sub.bus2_send", 0.0f);
    e.set ("bus1.level", -60.0f);
    CHECK (rms (e.play()) < 1.0e-3);
}

TEST (sub_osc_mono_plays_the_lowest_note_only)
{
    Synth e;
    subOnly (e);
    const double low = midiHz (36), high = midiHz (48);
    auto both = [&] (float mono)
    {
        e.set ("sub.mono", mono);
        return e.playEvents ({ noteOn (0, 36), noteOn (0, 48) }, 0.7);
    };
    const auto monoSpec = spectrumOf (both (1.0f).left);
    CHECK (powerAt (monoSpec, low) > 1.0e-6);
    CHECK (dbBetween (monoSpec, high, low) < -30.0); // the higher note adds nothing
    const auto polySpec = spectrumOf (both (0.0f).left);
    CHECK (std::fabs (dbBetween (polySpec, high, low)) < 1.5); // both notes at the same level

    // the lowest note is released: the sub moves to the note that is still held
    e.set ("sub.mono", 1.0f);
    const auto moved = e.playEvents ({ noteOn (0, 36), noteOn (0, 48), noteOff (static_cast<int> (0.35 * kSr), 36) }, 1.0);
    const auto early = spectrumOf (moved.left, 0.1, kFft / 2);
    const auto late = spectrumOf (moved.left, 0.6, kFft / 2);
    CHECK (dbBetween (early, high, low) < -30.0);
    CHECK (dbBetween (late, low, high) < -30.0);
    CHECK (powerAt (late, high) > 1.0e-6);
    CHECK (rms (moved, 0.6, 0.95) > 0.05);
    CHECK (sbtest::allFinite (moved.left) && sbtest::allFinite (moved.right));
}

/** Power in a band in dB. */
double bandDb (const std::vector<double>& p, double lo, double hi)
{
    double sum = 0.0;
    for (size_t k = static_cast<size_t> (lo * kFft / kSr); k < static_cast<size_t> (hi * kFft / kSr) && k < p.size(); ++k)
        sum += p[k];
    return 10.0 * std::log10 (std::max (sum, 1.0e-30));
}

/** Energy tilt over two octaves: band 1 - 2 kHz minus band 250 - 500 Hz. White +6 dB, pink 0 dB, brown -6 dB. */
double tiltDb (const sbtest::Rendered& r)
{
    const auto p = spectrumOf (r.left);
    return bandDb (p, 1000.0, 2000.0) - bandDb (p, 250.0, 500.0);
}

double crestFactor (const sbtest::Rendered& r)
{
    float peak = 0.0f;
    for (size_t i = static_cast<size_t> (0.15 * kSr); i < static_cast<size_t> (0.55 * kSr); ++i)
        peak = std::max (peak, std::fabs (r.left[i]));
    return peak / rms (r);
}

TEST (noise_sample_selection_changes_the_colour)
{
    Synth e;
    noiseOnly (e);
    std::vector<sbtest::Rendered> renders;
    for (int s = 0; s < 9; ++s)
    {
        e.set ("noise.sample", static_cast<float> (s));
        renders.push_back (e.play());
        if (! CHECK (rms (renders.back()) > 0.005))
            std::printf ("    noise.sample %d\n", s);
    }
    // the colours: the energy per octave rises 3 dB / octave for white, is flat for pink and falls for brown
    CHECK (std::fabs (tiltDb (renders[0]) - 6.0) < 2.5);
    CHECK (std::fabs (tiltDb (renders[1])) < 2.5);
    CHECK (std::fabs (tiltDb (renders[2]) + 6.0) < 2.5);
    // the click sets are sparse (a high crest factor), the colours are not
    CHECK (crestFactor (renders[3]) > 10.0); // Geiger
    CHECK (crestFactor (renders[5]) > 10.0); // Vinyl clicks
    for (const int dense : { 0, 1, 2, 4, 6, 8 })
        CHECK (crestFactor (renders[static_cast<size_t> (dense)]) < 5.0);
    CHECK (tiltDb (renders[4]) < tiltDb (renders[0]) - 0.5); // tape hiss is low passed
    CHECK (tiltDb (renders[8]) < tiltDb (renders[0]) - 1.0); // atmosphere is a band-pass rumble
    // every sample is audible and distinct
    for (size_t i = 0; i < renders.size(); ++i)
        for (size_t j = i + 1; j < renders.size(); ++j)
            if (! CHECK (relativeDifference (renders[i], renders[j]) > 0.1))
                std::printf ("    noise.sample %zu vs %zu\n", i, j);
    // switching back restores the white noise exactly
    e.set ("noise.sample", 0.0f);
    CHECK (identical (renders[0], e.play()));
}

TEST (noise_pitch_fine_and_keytrack)
{
    Synth e;
    noiseOnly (e);
    e.set ("noise.sample", 4.0f); // tape hiss: a low passed noise, its corner follows the playback rate (a power law colour would not)

    auto centroidWith = [&] (const char* key, float value, int note = 60)
    {
        Synth copy = e;
        copy.set (key, value);
        return centroid (copy.play (0.6, note));
    };
    const double base = centroid (e.play());
    const double up12 = centroidWith ("noise.pitch", 12.0f);
    const double down12 = centroidWith ("noise.pitch", -12.0f);
    CHECK (up12 > base * 1.12);
    CHECK (down12 < base * 0.6);
    const double fineUp = centroidWith ("noise.fine", 100.0f);
    const double fineDown = centroidWith ("noise.fine", -100.0f);
    CHECK (fineUp > base * 1.03);
    CHECK (fineDown < base * 0.97);

    // without keytrack the note does not matter, with it the playback rate follows the key
    const double low = centroidWith ("noise.keytrack", 0.0f, 36);
    const double high = centroidWith ("noise.keytrack", 0.0f, 72);
    CHECK (std::fabs (low / high - 1.0) < 0.02);
    e.set ("noise.keytrack", 1.0f);
    const double keyLow = centroid (e.play (0.6, 48));
    const double keyHigh = centroid (e.play (0.6, 72));
    CHECK (keyHigh > keyLow * 1.8);
}

TEST (noise_phase_random_phase_and_loop)
{
    Synth e;
    noiseOnly (e);
    e.set ("noise.sample", 1.0f);

    // start phase: the same noise from another point of the sample
    const auto start0 = e.play();
    Synth shifted = e;
    shifted.set ("noise.phase", 50.0f);
    const auto start50 = shifted.play();
    CHECK (rms (start50) > 0.005);
    CHECK (relativeDifference (start0, start50) > 0.3);
    CHECK (identical (start50, shifted.play())); // deterministic
    // a quarter of the sample (0.25 s of the sample, read at 44.1 / 48) is the same noise delayed: phase 25 % at the start is the
    // unshifted noise 11025 samples later
    Synth quarter = e;
    quarter.set ("noise.phase", 25.0f);
    const auto start25 = quarter.play (1.0);
    const auto plain = e.play (1.0);
    const size_t delay = static_cast<size_t> (std::lround (11025.0 * kSr / 44100.0));
    double d = 0.0, n = 0.0;
    for (size_t i = static_cast<size_t> (0.15 * kSr); i < static_cast<size_t> (0.45 * kSr); ++i)
    {
        d += std::pow (static_cast<double> (start25.left[i]) - plain.left[i + delay], 2.0);
        n += std::pow (static_cast<double> (plain.left[i + delay]), 2.0);
    }
    CHECK (std::sqrt (d / n) < 0.05);

    // random phase: every note starts somewhere else (two notes one after the other on one engine)
    auto twoNotes = [&] (bool random)
    {
        Synth s = e;
        s.set ("noise.random_phase", random ? 1.0f : 0.0f);
        const auto r = s.playEvents ({ noteOn (0, 48), noteOff (static_cast<int> (0.4 * kSr), 48), noteOn (static_cast<int> (0.8 * kSr), 48) }, 1.3);
        double dd = 0.0, nn = 0.0;
        const size_t shift = static_cast<size_t> (0.8 * kSr);
        for (size_t i = static_cast<size_t> (0.1 * kSr); i < static_cast<size_t> (0.35 * kSr); ++i)
        {
            dd += std::pow (static_cast<double> (r.left[i]) - r.left[i + shift], 2.0);
            nn += std::pow (static_cast<double> (r.left[i]), 2.0);
        }
        return std::sqrt (dd / nn);
    };
    const double fixedDiff = twoNotes (false), randomDiff = twoNotes (true);
    CHECK (fixedDiff < 0.05);
    CHECK (randomDiff > 0.5);

    // loop: on keeps playing, off plays the sample once (a second long) and falls silent
    const auto looped = e.play (2.6);
    CHECK (rms (looped, 1.6, 2.5) > 0.01);
    Synth once = e;
    once.set ("noise.loop", 0.0f);
    const auto oneShot = once.play (2.6);
    CHECK (rms (oneShot, 0.2, 0.9) > 0.01);
    CHECK (rms (oneShot, 1.3, 2.5) < 1.0e-4);
    // the white noise, too
    once.set ("noise.sample", 0.0f);
    CHECK (rms (once.play (2.6), 1.3, 2.5) < 1.0e-4);
}

TEST (noise_extras_keep_the_existing_noise_routing)
{
    // the noise.* mixer keys of B10 (filter blend, sends) work with a non-default noise sample
    Synth e;
    noiseOnly (e);
    e.set ("noise.sample", 5.0f);
    twoFilters (e);
    e.set ("noise.destination", 5.0f);
    e.set ("noise.filter_blend", 0.0f);
    const auto blend0 = e.play();
    e.set ("noise.filter_blend", 100.0f);
    const auto blend100 = e.play();
    CHECK (relativeDifference (blend0, blend100) > 0.3);
    e.set ("noise.destination", 6.0f);
    CHECK (rms (e.play()) < 1.0e-4);
    e.set ("noise.bus2_send", 100.0f);
    CHECK (rms (e.play()) > 1.0e-3);
}

TEST (every_modulatable_key_of_the_block_follows_the_matrix)
{
    // macro 1 at 0 -> 100 %, routed to the key at 100 %: the render must change (a moved level, pan, pitch, blend, send or phase)
    struct Case { const char* key; float amount; int note; };
    const Case cases[] = {
        { "sub.octave", 25.0f, 60 },  { "sub.tune", 25.0f, 60 },   { "sub.level", 100.0f, 60 }, { "sub.pan", 100.0f, 60 },
        { "sub.filter_blend", 100.0f, 69 }, { "sub.bus1_send", 100.0f, 60 }, { "sub.bus2_send", 100.0f, 60 },
        { "noise.pitch", 25.0f, 60 }, { "noise.fine", 100.0f, 60 }, { "noise.phase", 100.0f, 60 },
    };
    for (const Case& c : cases)
    {
        const bool sub = std::strncmp (c.key, "sub.", 4) == 0;
        Synth e;
        if (sub)
        {
            subOnly (e);
            e.set ("sub.waveform", 3.0f);
            e.set ("sub.level", std::strcmp (c.key, "sub.level") == 0 ? 20.0f : 100.0f);
            if (std::strcmp (c.key, "sub.pan") == 0)
                e.set ("sub.destination", 4.0f);
            if (std::strcmp (c.key, "sub.filter_blend") == 0)
            {
                twoFilters (e);
                e.set ("sub.destination", 5.0f);
            }
            if (std::strstr (c.key, "_send") != nullptr)
                e.set ("sub.destination", 6.0f);
        }
        else
        {
            noiseOnly (e);
            e.set ("noise.sample", 4.0f);
        }
        route (e, c.key, c.amount);

        Synth low = e, high = e;
        low.set ("macro.1", 0.0f);
        high.set ("macro.1", 100.0f);
        const auto a = low.play (0.6, c.note);
        const auto b = high.play (0.6, c.note);
        const bool changed = relativeDifference (a, b) > 0.2 || std::fabs (rms (a) - rms (b)) > 0.02 || std::fabs (centroid (a) - centroid (b)) > 0.03 * centroid (a);
        if (! CHECK (changed))
            std::printf ("    %s\n", c.key);
        CHECK (sbtest::allFinite (a.left) && sbtest::allFinite (b.left));
    }
}
