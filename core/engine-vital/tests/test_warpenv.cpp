// SynthyBoy - engine-vital tests: B21 warp-env (G34-G37 the Serum 2 warp modes, G39 the ENV 1 buttons of the Main / Direct channels)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Proves kCapWarpAlt, kCapWarpFilter, kCapWarpDist, kCapWarpFm, kCapWarpPdAm and kCapEnv1Toggle: every warp mode of the appended list is
// rendered against warp None and must make a measured difference in both slots, the neutral positions must be bit-near neutral, the two slots
// must render the same mode identically, and every ENV 1 button is rendered on and off.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
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

/** A settings list rendered on a fresh engine every time (two fresh engines with equal settings render the same samples). */
class Synth
{
public:
    void set (const char* key, float value) { settings.emplace_back (key, value); }
    void content (const StateContent& c) { stateContent = c; hasContent = true; }

    std::unique_ptr<IEngine> build() const
    {
        auto e = createEngine (EngineKind::vital);
        e->setParam (kParamMasterPolyphony, 32.0f);
        e->prepare (kSr, 256, Quality {});
        if (hasContent)
            e->setContent (stateContent);
        for (const auto& s : settings)
            setByKey (*e, s.first.c_str(), s.second);
        return e;
    }

    sbtest::Rendered playEvents (const std::vector<Event>& events, double seconds) const
    {
        auto e = build();
        return sbtest::render (*e, events, static_cast<int> (seconds * kSr), 256);
    }

    sbtest::Rendered play (double seconds = 0.6, int key = 48) const { return playEvents ({ noteOn (0, key) }, seconds); }

private:
    std::vector<std::pair<std::string, float>> settings;
    StateContent stateContent;
    bool hasContent = false;
};

ContentRemap remapCurve (int osc, int slot, std::vector<ContentPoint> points)
{
    ContentRemap r;
    r.osc = osc;
    r.slot = slot;
    r.name = "test";
    r.points = std::move (points);
    return r;
}

/** Oscillator 1 alone on the table "Sine to Saw" (frame 60 %), straight to the effects; osc 2 and osc 3 on as modulators only (destination None). */
Synth base()
{
    Synth e;
    e.set ("osc1.on", 1.0f);
    e.set ("osc1.wavetable", 6.0f);
    e.set ("osc1.frame", 60.0f);
    e.set ("osc1.level", 100.0f);
    e.set ("osc1.unison.voices", 1.0f);
    e.set ("osc1.random_phase", 0.0f);
    e.set ("osc1.destination", 3.0f);
    e.set ("osc2.on", 1.0f);
    e.set ("osc2.wavetable", 2.0f);
    e.set ("osc2.transpose", 7.0f);
    e.set ("osc2.level", 100.0f);
    e.set ("osc2.unison.voices", 1.0f);
    e.set ("osc2.random_phase", 0.0f);
    e.set ("osc2.destination", 6.0f);
    e.set ("osc3.on", 1.0f);
    e.set ("osc3.wavetable", 3.0f);
    e.set ("osc3.transpose", 12.0f);
    e.set ("osc3.level", 100.0f);
    e.set ("osc3.unison.voices", 1.0f);
    e.set ("osc3.random_phase", 0.0f);
    e.set ("osc3.destination", 6.0f);
    e.set ("noise.on", 0.0f);
    e.set ("filter1.on", 0.0f);
    e.set ("filter2.on", 0.0f);
    e.set ("master.volume", -6.0f);
    StateContent c;
    // a curve that is not the diagonal: a hump (Remap 1, 2 and 4 read it)
    c.remaps.push_back (remapCurve (1, 1, { { 0.0f, 0.0f }, { 0.3f, 0.7f }, { 1.0f, 1.0f } }));
    e.content (c);
    return e;
}

double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0 = 0.1, double t1 = 0.55)
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

double peakOf (const sbtest::Rendered& r)
{
    double p = 0.0;
    for (size_t i = 0; i < r.left.size(); ++i)
        p = std::max (p, std::fabs (static_cast<double> (r.left[i])));
    return p;
}

double rms (const sbtest::Rendered& r, double t0 = 0.1, double t1 = 0.55)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

double centroid (const sbtest::Rendered& r)
{
    return sbtest::centroidOf (r.left, static_cast<size_t> (0.1 * kSr), kFft, kSr, kFft);
}

std::vector<double> spectrumOf (const sbtest::Rendered& r)
{
    return sbtest::powerSpectrum (r.left, static_cast<size_t> (0.1 * kSr), kFft, kFft, sbtest::Window::blackmanHarris);
}

double powerAt (const std::vector<double>& p, double hz)
{
    const long centre = std::lround (hz * static_cast<double> (kFft) / kSr);
    double sum = 0.0;
    for (long k = std::max (1L, centre - 5); k <= centre + 5 && k < static_cast<long> (p.size()); ++k)
        sum += p[static_cast<size_t> (k)];
    return sum;
}

constexpr int kB21Modes = 52; // the warp modes of block B21 (the list has 24 more since GROUPB WP-06)
const char* const kModeNames[kB21Modes] = {
    "None", "Sync", "Formant", "Quantize", "Bend", "Squeeze", "Pulse", "FM A", "FM B", "RM A", "RM B", "Sync Var", "Bend +", "Bend -", "Bend +/-",
    "Asym +", "Asym -", "Asym +/-", "Flip", "Mirror", "Remap 1", "Remap 2", "Remap 3", "Remap 4", "Odd/Even", "LPF", "HPF", "Tube", "Soft Clip",
    "Hard Clip", "Diode 1", "Diode 2", "Linear Fold", "Sine Fold", "Zero-Square", "Asym", "Rectify", "Sine Shaper", "Stomp Box", "Tape Sat.",
    "Soft Sat.", "FM TZ A", "FM TZ B", "FM Exp A", "FM Exp B", "FM Lin A", "FM Lin B", "PD A", "PD B", "PD Self", "AM A", "AM B"
};

EngineCapsMask bitOfMode (int mode)
{
    if (mode <= 24)
        return kCapWarpAlt;
    if (mode <= 26)
        return kCapWarpFilter;
    if (mode <= 40)
        return kCapWarpDist;
    if (mode <= 46)
        return kCapWarpFm;
    return kCapWarpPdAm;
}

/** The amount a mode is tested at: the middle for the bipolar ones is neutral, so those use 80 %. */
float amountOf (int mode)
{
    switch (mode)
    {
        case 25: return 20.0f; // LPF: a low cutoff
        case 26: return 70.0f; // HPF
        case 47: case 48: case 49: return 70.0f;
        default: return 80.0f;
    }
}
} // namespace

TEST (warpenv_capabilities_and_lists)
{
    auto vital = createEngine (EngineKind::vital);
    auto own = createEngine (EngineKind::own);
    const EngineCapsMask bits = kCapWarpAll | kCapEnv1Toggle;
    CHECK ((vital->capabilities() & bits) == bits);
    for (size_t i = 0; i < paramCount(); ++i) // the own engine never claims more than its capability words say
        if (own->supportsParam (paramTable()[i].id))
            CHECK (capsCover (own->capabilities(), requiredCaps (i)) && capsCover2 (own->capabilities2(), requiredCaps2 (i)));
    CHECK_EQ (kNumWarpTypes, 99); // 52 after block B21, 76 with the modes from the Noise, Sub and Filters (GROUPB WP-06, test_modsources.cpp), 99 with the spectral warps (GROUPC WP-12)
    for (const char* key : { "osc1.warp.type", "osc2.warp.type", "osc3.warp.type", "osc1.warp2.type", "osc2.warp2.type", "osc3.warp2.type" })
    {
        const ParamInfo* info = findParamByKey (key);
        CHECK (info != nullptr);
        if (info == nullptr)
            continue;
        CHECK_EQ (static_cast<int> (info->numChoices), kNumWarpTypes);
        // the Vital modes keep their positions
        CHECK (std::strcmp (info->choices[0], "None") == 0);
        CHECK (std::strcmp (info->choices[1], "Sync") == 0);
        CHECK (std::strcmp (info->choices[10], "RM from Osc 3") == 0 || std::strcmp (info->choices[10], "RM from Osc 2") == 0);
        CHECK (std::strcmp (info->choices[11], "Sync Var") == 0);
        CHECK (std::strcmp (info->choices[51], "AM from Osc 3") == 0 || std::strcmp (info->choices[51], "AM from Osc 2") == 0);
    }
    // the cap bit of a value of the choice list
    const size_t index = static_cast<size_t> (findParamIndex (findParamByKey ("osc1.warp.type")->id));
    CHECK_EQ (requiredCapsForValue (index, 7.0f), requiredCaps (index));
    for (int mode = 11; mode < kB21Modes; ++mode)
        CHECK ((requiredCapsForValue (index, static_cast<float> (mode)) & bitOfMode (mode)) != 0);
    for (const char* key : { "sub.env1", "osc1.env1", "osc2.env1", "osc3.env1", "noise.env1", "filter1.env1", "filter2.env1" })
    {
        const ParamInfo* info = findParamByKey (key);
        CHECK (info != nullptr);
        if (info != nullptr)
        {
            CHECK_EQ (info->def, 1.0f);
            CHECK ((requiredCaps (static_cast<size_t> (findParamIndex (info->id))) & kCapEnv1Toggle) != 0);
        }
    }
}

TEST (warpenv_every_mode_is_audible_in_both_slots)
{
    const auto reference = base().play();
    CHECK (rms (reference) > 0.02);
    for (int slot = 1; slot <= 2; ++slot)
        for (int mode = 11; mode < kB21Modes; ++mode)
        {
            // the modulator modes use the other oscillators: the list of osc 1 has osc 2 / osc 3 in the first / second place
            Synth e = base();
            e.set (slot == 1 ? "osc1.warp.type" : "osc1.warp2.type", static_cast<float> (mode));
            e.set (slot == 1 ? "osc1.warp.amount" : "osc1.warp2.amount", amountOf (mode));
            const auto r = e.play();
            const double difference = relativeDifference (reference, r);
            const bool audible = difference > 0.05;
            if (! CHECK (audible))
                std::printf ("    slot %d mode %d (%s): relative difference %.4f\n", slot, mode, kModeNames[mode], difference);
            CHECK (sbtest::allFinite (r.left) && sbtest::allFinite (r.right));
            if (! CHECK (peakOf (r) < 8.0))
                std::printf ("    slot %d mode %d (%s): peak %.3f\n", slot, mode, kModeNames[mode], peakOf (r));
        }
}

TEST (warpenv_slot_one_and_slot_two_render_the_same_mode_identically)
{
    // (Sync Var is left out: like Sync, it raises the harmonic content that the band limit of the wavetable read allows in slot 1 only; slot 2 has
    // always read the table at the pitch of the oscillator, see 13-new-keys.md 4.22)
    for (int mode = 12; mode < kB21Modes; ++mode)
    {
        Synth one = base(), two = base();
        one.set ("osc1.warp.type", static_cast<float> (mode));
        one.set ("osc1.warp.amount", amountOf (mode));
        two.set ("osc1.warp2.type", static_cast<float> (mode));
        two.set ("osc1.warp2.amount", amountOf (mode));
        const auto a = one.play (0.4), b = two.play (0.4);
        if (! CHECK (a.left == b.left && a.right == b.right))
            std::printf ("    mode %d (%s)\n", mode, kModeNames[mode]);
    }
}

TEST (warpenv_every_mode_is_finite_with_unison_and_a_chord)
{
    // unison 5 and two notes at once (the voice pairing) and one note alone (the single voice path), the warp amount modulated by a macro
    for (int mode = 11; mode < kB21Modes; ++mode)
    {
        for (int chord = 0; chord < 2; ++chord)
        {
            Synth e = base();
            e.set ("osc1.unison.voices", 5.0f);
            e.set ("osc1.unison.detune", 40.0f);
            e.set ("osc1.warp.type", static_cast<float> (mode));
            e.set ("osc1.warp.amount", amountOf (mode));
            e.set ("osc1.warp.spread", 30.0f);
            e.set ("osc1.warp2.type", static_cast<float> (11 + (mode * 7) % 41));
            const auto r = chord == 0 ? e.play (0.35) : e.playEvents ({ noteOn (0, 48), noteOn (0, 55), noteOn (2000, 60) }, 0.35);
            if (! CHECK (sbtest::allFinite (r.left) && sbtest::allFinite (r.right) && peakOf (r) < 16.0))
                std::printf ("    mode %d (%s) chord %d\n", mode, kModeNames[mode], chord);
        }
    }
}

TEST (warpenv_neutral_positions_and_the_alt_modes)
{
    const auto reference = base().play();
    // the bipolar modes are the original sound in the middle, Flip and Remap 1 with the amount 0
    const std::pair<int, float> neutral[] = { { 14, 50.0f }, { 17, 50.0f }, { 24, 50.0f }, { 18, 0.0f }, { 20, 0.0f }, { 21, 0.0f }, { 22, 0.0f }, { 23, 0.0f },
                                               { 12, 0.0f }, { 13, 0.0f }, { 15, 0.0f }, { 16, 0.0f } };
    for (const auto& n : neutral)
    {
        Synth e = base();
        e.set ("osc1.warp.type", static_cast<float> (n.first));
        e.set ("osc1.warp.amount", n.second);
        const double difference = relativeDifference (reference, e.play());
        if (! CHECK (difference < 0.02))
            std::printf ("    mode %d (%s) at %.0f %%: difference %.4f\n", n.first, kModeNames[n.first], n.second, difference);
    }

    // Odd/Even: 0 % keeps the odd harmonics only, 100 % the even ones only
    Synth sine = base();
    sine.set ("osc1.wavetable", 1.0f); // a saw table has all the harmonics
    sine.set ("osc1.frame", 0.0f);
    const auto saw = sine.play();
    Synth odd = sine, even = sine;
    odd.set ("osc1.warp.type", 24.0f);
    odd.set ("osc1.warp.amount", 0.0f);
    even.set ("osc1.warp.type", 24.0f);
    even.set ("osc1.warp.amount", 100.0f);
    const double f0 = 130.8127827; // key 48
    const auto pSaw = spectrumOf (saw), pOdd = spectrumOf (odd.play()), pEven = spectrumOf (even.play());
    CHECK (powerAt (pSaw, 2 * f0) > 1.0e-6);
    CHECK (powerAt (pOdd, 2 * f0) < 0.02 * powerAt (pOdd, f0));
    CHECK (powerAt (pOdd, 3 * f0) > 0.02 * powerAt (pOdd, f0));
    CHECK (powerAt (pEven, f0) < 0.02 * powerAt (pEven, 2 * f0));
    CHECK (powerAt (pEven, 2 * f0) > 0.02 * powerAt (pSaw, 2 * f0));

    // the filters: a low cutoff darkens, a high cutoff of the high pass brightens
    Synth low = base(), high = base();
    low.set ("osc1.warp.type", 25.0f);
    low.set ("osc1.warp.amount", 10.0f);
    high.set ("osc1.warp.type", 26.0f);
    high.set ("osc1.warp.amount", 80.0f);
    CHECK (centroid (low.play()) < 0.6 * centroid (reference));
    CHECK (centroid (high.play()) > 1.3 * centroid (reference));

    // Sync Var: the soft edge changes the sound against the hard sync of the same ratio, and VAR 0 is the hard sync
    Synth hard = base(), soft = base(), var0 = base();
    hard.set ("osc1.warp.type", 1.0f);
    hard.set ("osc1.warp.amount", 70.0f);
    var0.set ("osc1.warp.type", 11.0f);
    var0.set ("osc1.warp.amount", 70.0f);
    var0.set ("osc1.warp.phase", 0.0f);
    soft.set ("osc1.warp.type", 11.0f);
    soft.set ("osc1.warp.amount", 70.0f);
    soft.set ("osc1.warp.phase", 360.0f);
    const auto hardRender = hard.play();
    CHECK (relativeDifference (hardRender, var0.play()) < 0.02);
    CHECK (relativeDifference (hardRender, soft.play()) > 0.1);
}

TEST (warpenv_remap_modes_use_the_curve_of_the_oscillator)
{
    // Remap 1 and 4 with the drawn curve differ from the identity curve (slot 2 of osc 1 is empty = the diagonal)
    for (int mode : { 20, 21, 23 })
    {
        Synth drawn = base(), diagonal = base();
        drawn.set ("osc1.warp.type", static_cast<float> (mode));
        drawn.set ("osc1.warp.amount", 100.0f);
        diagonal.set ("osc1.warp.type", static_cast<float> (mode));
        diagonal.set ("osc1.warp.amount", 100.0f);
        diagonal.set ("osc1.remap.slot", 1.0f); // Curve 2: nobody drew it
        if (! CHECK (relativeDifference (drawn.play(), diagonal.play()) > 0.05))
            std::printf ("    mode %d\n", mode);
    }
}

TEST (warpenv_modulator_modes_follow_the_other_oscillator)
{
    // FM / PD / AM from osc 2 stops when osc 2 is switched off, the same modes from osc 3 do not care about osc 2
    for (int mode : { 41, 43, 45, 47, 50 })
    {
        Synth withSource = base(), without = base(), other = base();
        for (Synth* e : { &withSource, &without, &other })
        {
            e->set ("osc1.warp.type", static_cast<float> (mode));
            e->set ("osc1.warp.amount", 70.0f);
        }
        without.set ("osc2.on", 0.0f);
        other.set ("osc3.on", 0.0f);
        const auto a = withSource.play(), b = without.play(), c = other.play();
        if (! CHECK (relativeDifference (a, b) > 0.02))
            std::printf ("    mode %d (%s): osc 2 off\n", mode, kModeNames[mode]);
        CHECK (relativeDifference (a, c) < 1.0e-6);
    }
    for (int mode : { 42, 44, 46, 48, 51 })
    {
        Synth withSource = base(), without = base();
        for (Synth* e : { &withSource, &without })
        {
            e->set ("osc1.warp.type", static_cast<float> (mode));
            e->set ("osc1.warp.amount", 70.0f);
        }
        without.set ("osc3.on", 0.0f);
        if (! CHECK (relativeDifference (withSource.play(), without.play()) > 0.02))
            std::printf ("    mode %d (%s): osc 3 off\n", mode, kModeNames[mode]);
    }
}

TEST (warpenv_fm_thru_zero_exp_and_linear_are_different_laws)
{
    Synth tz = base(), ex = base(), lin = base();
    tz.set ("osc1.warp.type", 41.0f);
    ex.set ("osc1.warp.type", 43.0f);
    lin.set ("osc1.warp.type", 45.0f);
    for (Synth* e : { &tz, &ex, &lin })
        e->set ("osc1.warp.amount", 90.0f);
    const auto a = tz.play(), b = ex.play(), c = lin.play();
    CHECK (relativeDifference (a, b) > 0.1);
    CHECK (relativeDifference (a, c) > 0.02); // Linear is Thru-Zero with the frequency clamped at zero
    CHECK (relativeDifference (b, c) > 0.1);
}

TEST (warpenv_default_is_the_old_sound)
{
    // an oscillator with the default warp (None) renders the same with the new ENV 1 keys at their default and written explicitly
    Synth plain = base(), explicitOn = base();
    for (const char* key : { "osc1.env1", "osc2.env1", "osc3.env1", "sub.env1", "noise.env1", "filter1.env1", "filter2.env1" })
        explicitOn.set (key, 1.0f);
    const auto a = plain.play(), b = explicitOn.play();
    CHECK (a.left == b.left && a.right == b.right);
}

namespace
{
/** A source alone, straight to the Main output, with a slow ENV 1 attack: with the ENV 1 button on the first 150 ms are quiet. */
Synth channelOnly (const char* source)
{
    Synth e;
    e.set ("osc1.on", 0.0f);
    e.set ("osc2.on", 0.0f);
    e.set ("osc3.on", 0.0f);
    e.set ("noise.on", 0.0f);
    e.set ("sub.on", 0.0f);
    e.set ("filter1.on", 0.0f);
    e.set ("filter2.on", 0.0f);
    e.set ("master.volume", -6.0f);
    e.set ("env1.attack", 400.0f);
    e.set ("env1.release", 80.0f);
    if (std::strcmp (source, "sub") == 0)
    {
        e.set ("sub.on", 1.0f);
        e.set ("sub.level", 100.0f);
        e.set ("sub.destination", 3.0f);
        e.set ("sub.octave", 0.0f);
    }
    else if (std::strcmp (source, "noise") == 0)
    {
        e.set ("noise.on", 1.0f);
        e.set ("noise.level", 50.0f);
        e.set ("noise.destination", 3.0f);
    }
    else if (std::strcmp (source, "filter1") == 0 || std::strcmp (source, "filter2") == 0)
    {
        const bool first = std::strcmp (source, "filter1") == 0;
        e.set ("osc1.on", 1.0f);
        e.set ("osc1.level", 100.0f);
        e.set ("osc1.destination", first ? 0.0f : 1.0f);
        e.set (first ? "filter1.on" : "filter2.on", 1.0f);
        e.set (first ? "filter1.destination" : "filter2.destination", 0.0f);
    }
    else
    {
        const std::string prefix = source;
        e.set ((prefix + ".on").c_str(), 1.0f);
        e.set ((prefix + ".level").c_str(), 100.0f);
        e.set ((prefix + ".destination").c_str(), 3.0f);
    }
    return e;
}
} // namespace

TEST (warpenv_env1_button_of_every_channel)
{
    for (const char* source : { "osc1", "osc2", "osc3", "sub", "noise", "filter1", "filter2" })
    {
        Synth on = channelOnly (source), off = channelOnly (source);
        const std::string key = std::string (source) + ".env1";
        off.set (key.c_str(), 0.0f);
        const auto a = on.play (0.8), b = off.play (0.8);
        // the attack is 400 ms: the enveloped channel is still quiet at 60 .. 140 ms, the free channel is at full level
        const double quiet = rms (a, 0.06, 0.14), full = rms (b, 0.06, 0.14);
        if (! (CHECK (full > 0.01) && CHECK (full > 3.0 * quiet)))
            std::printf ("    %s: enveloped %.5f, free %.5f\n", source, quiet, full);
        CHECK (sbtest::allFinite (b.left));
    }
}

TEST (warpenv_env1_off_follows_the_longest_envelope_release)
{
    // ENV 1 release 80 ms: after the note off the free channel ends with it ...
    Synth shortRelease = channelOnly ("osc1");
    shortRelease.set ("osc1.env1", 0.0f);
    shortRelease.set ("env1.attack", 5.0f);
    const auto a = shortRelease.playEvents ({ noteOn (0, 48), noteOff (12000, 48) }, 1.2);
    CHECK (rms (a, 0.15, 0.24) > 0.05);
    CHECK (rms (a, 0.6, 1.1) < 1.0e-3);

    // ... but an enabled envelope with a long release (ENV 2 on a modulation slot) keeps the voice sounding
    Synth longEnvelope = shortRelease;
    const auto destination = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc3.pan")->id)));
    longEnvelope.set ("mod01.source", 2.0f); // Env 2
    longEnvelope.set ("mod01.destination", static_cast<float> (destination));
    longEnvelope.set ("mod01.amount", 100.0f);
    longEnvelope.set ("env2.release", 1500.0f);
    longEnvelope.set ("env2.sustain", 100.0f);
    const auto b = longEnvelope.playEvents ({ noteOn (0, 48), noteOff (12000, 48) }, 1.2);
    CHECK (rms (b, 0.6, 1.1) > 0.05);
    CHECK (sbtest::allFinite (b.left));

    // with the button on, the voice ends with ENV 1 whatever ENV 2 does
    Synth enveloped = longEnvelope;
    enveloped.set ("osc1.env1", 1.0f);
    CHECK (rms (enveloped.playEvents ({ noteOn (0, 48), noteOff (12000, 48) }, 1.2), 0.6, 1.1) < 1.0e-3);
}

// ---- the sample oscillator: AM, the distortion group and the FM laws act on a sample, the rest is inert -------------------------------------------

namespace
{
std::vector<uint8_t> wav16 (const std::vector<float>& samples, int sampleRate = 48000)
{
    std::vector<uint8_t> b;
    const auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (v >> (8 * i))); };
    const auto put16 = [&] (uint16_t v) { b.push_back (static_cast<uint8_t> (v)); b.push_back (static_cast<uint8_t> (v >> 8)); };
    const auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (t[i])); };
    tag ("RIFF"); put32 (static_cast<uint32_t> (36 + samples.size() * 2)); tag ("WAVE");
    tag ("fmt "); put32 (16); put16 (1); put16 (1); put32 (static_cast<uint32_t> (sampleRate)); put32 (static_cast<uint32_t> (sampleRate * 2)); put16 (2); put16 (16);
    tag ("data"); put32 (static_cast<uint32_t> (samples.size() * 2));
    for (float s : samples)
        put16 (static_cast<uint16_t> (static_cast<int16_t> (std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767.0f))));
    return b;
}

/** Oscillator 1 as a looped 220 Hz sine sample; osc 2 is the modulator. */
struct SampleRig
{
    MemoryContentStore store;
    StateContent state;

    SampleRig()
    {
        std::vector<float> audio (48000);
        for (size_t i = 0; i < audio.size(); ++i)
            audio[i] = 0.8f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 220.0 * static_cast<double>(i) / 48000.0));
        store.add ("test", "sample.wav", wav16 (audio));
        ContentSample c;
        c.osc = 1;
        c.ref.pack = "test";
        c.ref.file = "sample.wav";
        c.ref.name = "sample";
        c.sampleRate = 48000;
        c.frames = static_cast<int> (audio.size());
        c.channels = 1;
        state.samples.push_back (c);
    }

    sbtest::Rendered play (int mode, float amount) const
    {
        auto e = createEngine (EngineKind::vital);
        e->setParam (kParamMasterPolyphony, 32.0f);
        e->prepare (kSr, 256, Quality {});
        e->setContentStore (&store);
        e->setContent (state);
        for (const auto& s : settings (mode, amount))
            setByKey (*e, s.first.c_str(), s.second);
        return sbtest::render (*e, { noteOn (0, 57) }, static_cast<int> (0.5 * kSr), 256);
    }

    std::vector<std::pair<std::string, float>> settings (int mode, float amount) const
    {
        std::vector<std::pair<std::string, float>> out;
        for (const auto& s : { std::make_pair ("osc1.type", 1.0f), std::make_pair ("osc1.sample.root", 57.0f), std::make_pair ("osc1.sample.loop_mode", 1.0f),
                               std::make_pair ("osc1.level", 100.0f), std::make_pair ("osc1.unison.voices", 1.0f), std::make_pair ("osc1.random_phase", 0.0f),
                               std::make_pair ("osc1.destination", 3.0f), std::make_pair ("osc2.on", 1.0f), std::make_pair ("osc2.transpose", 7.0f),
                               std::make_pair ("osc2.destination", 6.0f), std::make_pair ("osc3.on", 0.0f), std::make_pair ("filter1.on", 0.0f),
                               std::make_pair ("filter2.on", 0.0f), std::make_pair ("noise.on", 0.0f), std::make_pair ("master.volume", -6.0f),
                               std::make_pair ("osc1.warp.type", static_cast<float> (mode)), std::make_pair ("osc1.warp.amount", amount) })
            out.emplace_back (s.first, s.second);
        return out;
    }
};
} // namespace

TEST (warpenv_sample_oscillator_takes_am_the_distortions_and_the_fm_laws)
{
    SampleRig rig;
    const auto reference = rig.play (0, 50.0f);
    CHECK (rms (reference) > 0.05);
    for (int mode : { 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 43, 45, 50 })
    {
        const auto r = rig.play (mode, 70.0f);
        if (! CHECK (relativeDifference (reference, r) > 0.03))
            std::printf("    mode %d (%s) does nothing on a sample\n", mode, kModeNames[mode]);
        CHECK (sbtest::allFinite (r.left));
    }
    // the filters, Odd/Even and the phase domain modes have nothing to work on in a sample: the same sound
    for (int mode : { 12, 18, 20, 24, 25, 26, 47, 49 })
        CHECK (relativeDifference (reference, rig.play (mode, 70.0f)) < 1.0e-6);
}

TEST (warpenv_extreme_settings_stay_finite)
{
    // every mode in both slots at the ends of every knob, on the extreme notes, with a hot modulator and a full unison
    for (int slot = 1; slot <= 2; ++slot)
        for (int mode = 11; mode < kB21Modes; ++mode)
            for (float amount : { 0.0f, 100.0f })
                for (int key : { 12, 120 })
                {
                    Synth e = base();
                    e.set ("osc1.unison.voices", 16.0f);
                    e.set ("osc1.unison.detune", 100.0f);
                    e.set ("osc2.transpose", 48.0f);
                    e.set ("osc3.transpose", -48.0f);
                    e.set (slot == 1 ? "osc1.warp.type" : "osc1.warp2.type", static_cast<float> (mode));
                    e.set (slot == 1 ? "osc1.warp.amount" : "osc1.warp2.amount", amount);
                    e.set (slot == 1 ? "osc1.warp.phase" : "osc1.warp2.phase", amount * 3.6f);
                    e.set (slot == 1 ? "osc1.warp.spread" : "osc1.warp2.spread", amount > 0.0f ? 100.0f : -100.0f);
                    const auto r = e.play (0.25, key);
                    if (! CHECK (sbtest::allFinite (r.left) && sbtest::allFinite (r.right)))
                        std::printf ("    slot %d mode %d (%s) amount %.0f key %d\n", slot, mode, kModeNames[mode], amount, key);
                }
}
