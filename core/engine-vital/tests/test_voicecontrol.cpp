// SynthyBoy - engine-vital tests: B26 (GROUPA2, WP-07): the voice control (13-new-keys.md 4.30): the sequence of per voice values (pan, detune, cutoff,
// envelope time, Mod 1, Mod 2), the randomisation, the envelope and LFO scaling, the oscillator mask, and the sources Voice Index, Voice Mod 1 / 2.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Proves kCapVoiceControl and kCapVoiceSources: every key is rendered off and on and must make a measured difference; the defaults (all steps zero,
// both scalings at 100 %) leave the render exactly as it was.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr size_t kFft = 16384;
constexpr double kSpacing = 0.5; // seconds between two notes
constexpr double kLength = 0.3;  // seconds a note is held

std::vector<uint8_t> wav16 (const std::vector<float>& samples, int sampleRate = 48000)
{
    std::vector<uint8_t> b;
    const auto put32 = [&b] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (v >> (8 * i))); };
    const auto put16 = [&b] (uint16_t v) { b.push_back (static_cast<uint8_t> (v)); b.push_back (static_cast<uint8_t> (v >> 8)); };
    const auto tag = [&b] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (t[i])); };
    tag ("RIFF"); put32 (static_cast<uint32_t> (36 + samples.size() * 2)); tag ("WAVE");
    tag ("fmt "); put32 (16); put16 (1); put16 (1); put32 (static_cast<uint32_t> (sampleRate)); put32 (static_cast<uint32_t> (sampleRate * 2)); put16 (2); put16 (16);
    tag ("data"); put32 (static_cast<uint32_t> (samples.size() * 2));
    for (float s : samples)
        put16 (static_cast<uint16_t> (static_cast<int16_t> (std::lround (std::clamp (s, -1.0f, 1.0f) * 32767.0f))));
    return b;
}

/** A settings list that renders on a fresh engine every time (two fresh engines with the same settings are sample identical, the note counter starts at 1). */
class Synth
{
public:
    void set (const char* key, float value) { settings.emplace_back (key, value); }
    void set (const std::string& key, float value) { settings.emplace_back (key, value); }

    void noiseSample (const std::vector<float>& audio)
    {
        store->add ("test", "noise.wav", wav16 (audio));
        ContentSample c;
        c.osc = kNoiseSampleOsc;
        c.ref.pack = "test";
        c.ref.file = "noise.wav";
        c.ref.name = "noise";
        c.sampleRate = 48000;
        c.frames = static_cast<int> (audio.size());
        c.channels = 1;
        content.samples.push_back (c);
    }

    std::unique_ptr<IEngine> build() const
    {
        auto e = createEngine (EngineKind::vital);
        e->setParam (kParamMasterPolyphony, 32.0f);
        e->prepare (kSr, 256, Quality {});
        if (! content.empty())
        {
            e->setContentStore (store.get());
            e->setContent (content);
        }
        for (const auto& s : settings)
            setByKey (*e, s.first.c_str(), s.second);
        return e;
    }

    /** `count` notes of the same key, one after the other (the note counter of the n-th is n + 1). */
    sbtest::Rendered notes (int count, int key = 60) const
    {
        std::vector<Event> events;
        for (int i = 0; i < count; ++i)
        {
            events.push_back (noteOn (static_cast<int> (i * kSpacing * kSr), key));
            events.push_back (noteOff (static_cast<int> ((i * kSpacing + kLength) * kSr), key));
        }
        auto e = build();
        return sbtest::render (*e, events, static_cast<int> ((count * kSpacing + 0.1) * kSr), 256);
    }

private:
    std::vector<std::pair<std::string, float>> settings;
    std::shared_ptr<MemoryContentStore> store = std::make_shared<MemoryContentStore>();
    StateContent content;
};

/** One oscillator, a sine, short envelopes, nothing else: the notes are windows of a clean tone. */
void tone (Synth& s)
{
    for (const char* key : { "osc2.on", "osc3.on", "noise.on", "sub.on", "filter1.on", "filter2.on" })
        s.set (key, 0.0f);
    s.set ("osc1.on", 1.0f);
    s.set ("osc1.wavetable", 0.0f);
    s.set ("osc1.level", 80.0f);
    s.set ("env1.attack", 0.0f);
    s.set ("env1.decay", 1000.0f);
    s.set ("env1.sustain", 100.0f);
    s.set ("env1.release", 5.0f);
}

void subOnly (Synth& s)
{
    for (const char* key : { "osc1.on", "osc2.on", "osc3.on", "noise.on", "filter1.on", "filter2.on" })
        s.set (key, 0.0f);
    s.set ("sub.on", 1.0f);
    s.set ("sub.level", 100.0f);
    s.set ("sub.destination", 3.0f);
    s.set ("sub.octave", 0.0f);
    s.set ("sub.waveform", 0.0f);
    s.set ("env1.attack", 0.0f);
    s.set ("env1.release", 5.0f);
}

void noiseOnly (Synth& s)
{
    for (const char* key : { "osc1.on", "osc2.on", "osc3.on", "sub.on", "filter1.on", "filter2.on" })
        s.set (key, 0.0f);
    s.set ("noise.on", 1.0f);
    s.set ("noise.level", 50.0f);
    s.set ("noise.destination", 3.0f);
    s.set ("env1.attack", 0.0f);
    s.set ("env1.release", 5.0f);
}

size_t from (int note, double offset) { return static_cast<size_t> ((note * kSpacing + offset) * kSr); }

/** (left - right) / (left + right) in the steady part of note `n`: -1 right only, +1 left only. */
double balance (const sbtest::Rendered& r, int n)
{
    const double l = sbtest::rmsOf (r.left, from (n, 0.1), from (n, 0.28));
    const double rr = sbtest::rmsOf (r.right, from (n, 0.1), from (n, 0.28));
    return l + rr > 1.0e-9 ? (l - rr) / (l + rr) : 0.0;
}

double level (const sbtest::Rendered& r, int n, double t0 = 0.1, double t1 = 0.28)
{
    return sbtest::rmsOf (r.left, from (n, t0), from (n, t1));
}

double frequency (const sbtest::Rendered& r, int n, double guess)
{
    const auto p = sbtest::powerSpectrum (r.left, from (n, 0.1), kFft, kFft, sbtest::Window::blackmanHarris);
    return sbtest::peakFrequency (p, kFft, kSr, guess);
}

double cents (double ratio) { return 1200.0 * std::log2 (ratio); }

bool identical (const sbtest::Rendered& a, const sbtest::Rendered& b) { return a.left == b.left && a.right == b.right; }

void route (Synth& s, int slot, const char* source, const char* destinationKey, float amountPercent)
{
    char key[32];
    std::snprintf (key, sizeof key, "mod%02d.source", slot);
    s.set (key, sbtest::sourceChoice (source));
    std::snprintf (key, sizeof key, "mod%02d.destination", slot);
    const ParamInfo* destination = findParamByKey (destinationKey);
    CHECK (destination != nullptr);
    s.set (key, destination != nullptr ? static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (destination->id)))) : 0.0f);
    std::snprintf (key, sizeof key, "mod%02d.amount", slot);
    s.set (key, amountPercent);
}

std::string step (int n, const char* row) { return "voicectl.s" + std::to_string (n) + "." + row; }
} // namespace

TEST (voicecontrol_capabilities_and_keys)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities() & kCapVoiceControl) != 0);
    CHECK ((e->capabilities() & kCapVoiceSources) != 0);
    auto own = createEngine (EngineKind::own);
    CHECK ((own->capabilities() & ((kCapVoiceControl | kCapVoiceSources)) & ~createEngine (EngineKind::vital)->capabilities()) == 0); // the own engine reports a bit only if its own probe proves it (test_own_*.cpp), never one that this engine lacks
    int found = 0;
    for (size_t i = 0; i < paramCount(); ++i)
        found += std::string (paramTable()[i].key).rfind ("voicectl.", 0) == 0 ? 1 : 0;
    CHECK_EQ (found, 62);
    CHECK_EQ (sbtest::sourceChoice ("voice_index"), 40.0f);
    CHECK_EQ (sbtest::sourceChoice ("voice_mod1"), 41.0f);
    CHECK_EQ (sbtest::sourceChoice ("voice_mod2"), 42.0f);
}

TEST (voicecontrol_defaults_change_nothing)
{
    // all steps zero: the sequence length, the oscillator mask and the scaling at 100 % (either mode) leave the render as it is, bit for bit
    Synth base;
    tone (base);
    base.set ("filter1.on", 1.0f);
    base.set ("lfo1.rate", 3.0f);
    const auto reference = base.notes (3);
    CHECK (level (reference, 0) > 0.05);

    Synth changed = base;
    changed.set ("voicectl.length", 3.0f);
    changed.set ("voicectl.osc.a", 0.0f);
    changed.set ("voicectl.osc.sub", 0.0f);
    changed.set ("voicectl.scale.env", 100.0f);
    changed.set ("voicectl.scale.lfo", 100.0f);
    changed.set ("voicectl.scale.lfo_mode", 1.0f);
    changed.set ("voicectl.rand.detune10x", 1.0f); // 10 x nothing is nothing
    CHECK (identical (reference, changed.notes (3)));
}

TEST (voicecontrol_pan_sequence_wraps_at_the_length_and_the_mask_selects_the_sources)
{
    Synth s;
    tone (s);
    s.set ("voicectl.length", 3.0f);
    s.set (step (1, "pan"), -100.0f);
    s.set (step (2, "pan"), 100.0f);
    s.set (step (3, "pan"), 0.0f);
    const auto r = s.notes (6);
    // pan -1 is the left side (the pan law of the oscillators), +1 the right side, 0 the middle; note 4 is step 1 again
    CHECK (balance (r, 0) > 0.9);
    CHECK (balance (r, 1) < -0.9);
    CHECK (std::fabs (balance (r, 2)) < 0.02);
    CHECK_NEAR (balance (r, 3), balance (r, 0), 1.0e-3);
    CHECK_NEAR (balance (r, 4), balance (r, 1), 1.0e-3);
    CHECK_NEAR (balance (r, 5), balance (r, 2), 1.0e-3);
    // a half step: 50 % is between the middle and the side
    {
        Synth h;
        tone (h);
        h.set (step (1, "pan"), -50.0f);
        const double b = balance (h.notes (1), 0);
        CHECK (b > 0.1 && b < 0.9);
    }
    // the mask: Osc A off - the pan no longer reaches the only oscillator that plays
    {
        Synth m = s;
        m.set ("voicectl.osc.a", 0.0f);
        const auto masked = m.notes (3);
        for (int n = 0; n < 3; ++n)
            CHECK (std::fabs (balance (masked, n)) < 0.02);
    }
    // the noise and the sub follow their own mask
    for (const bool noise : { true, false })
    {
        Synth t;
        if (noise)
            noiseOnly (t);
        else
            subOnly (t);
        t.set (step (1, "pan"), -100.0f);
        const auto on = t.notes (1);
        CHECK (balance (on, 0) > 0.9);
        t.set (noise ? "voicectl.osc.noise" : "voicectl.osc.sub", 0.0f);
        CHECK (std::fabs (balance (t.notes (1), 0)) < 0.05);
    }
}

TEST (voicecontrol_detune_moves_the_pitch_of_the_oscillators_the_sub_and_the_noise_sample)
{
    // the oscillators: +100 cents on step 1, -100 on step 2
    {
        Synth s;
        tone (s);
        s.set ("voicectl.length", 2.0f);
        s.set (step (1, "detune"), 100.0f);
        s.set (step (2, "detune"), -50.0f);
        const auto r = s.notes (3);
        const double f0 = 261.6256;
        CHECK_NEAR (cents (frequency (r, 0, f0) / f0), 100.0, 4.0);
        CHECK_NEAR (cents (frequency (r, 1, f0) / f0), -50.0, 4.0);
        CHECK_NEAR (cents (frequency (r, 2, f0) / f0), 100.0, 4.0);
        Synth masked = s;
        masked.set ("voicectl.osc.a", 0.0f);
        CHECK_NEAR (cents (frequency (masked.notes (1), 0, f0) / f0), 0.0, 4.0);
    }
    // the sub
    for (const bool on : { false, true })
    {
        Synth s;
        subOnly (s);
        s.set (step (1, "detune"), 100.0f);
        s.set ("voicectl.osc.sub", on ? 1.0f : 0.0f);
        CHECK_NEAR (cents (frequency (s.notes (1), 0, 261.6256) / 261.6256), on ? 100.0 : 0.0, 4.0);
    }
    // the noise source as a sample: a 440 Hz sine plays at the pitch of the key, the detune shifts it
    for (const bool on : { false, true })
    {
        Synth s;
        noiseOnly (s);
        std::vector<float> sine (48000);
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.8f * static_cast<float> (std::sin (2.0 * kPi * 440.0 * static_cast<double> (i) / kSr));
        s.noiseSample (sine);
        s.set ("noise.type", 1.0f);
        s.set ("noise.loop", 1.0f);
        s.set ("noise.keytrack", 0.0f);
        s.set (step (1, "detune"), 100.0f);
        s.set ("voicectl.osc.noise", on ? 1.0f : 0.0f);
        const auto r = s.notes (1, 69);
        const double f = frequency (r, 0, 440.0);
        CHECK_NEAR (cents (f / 440.0), on ? 100.0 : 0.0, 6.0);
    }
}

TEST (voicecontrol_cutoff_opens_and_closes_the_filter_per_voice)
{
    Synth s;
    tone (s);
    s.set ("osc1.wavetable", static_cast<float> (kWavetableSaw));
    s.set ("filter1.on", 1.0f);
    s.set ("filter1.cutoff", 600.0f);
    s.set ("filter1.keytrack", 0.0f);
    s.set ("voicectl.length", 3.0f);
    s.set (step (2, "cutoff"), 100.0f);
    s.set (step (3, "cutoff"), -100.0f);
    const auto r = s.notes (3);
    const auto centroidOf = [&] (int n) { return sbtest::centroidOf (r.left, from (n, 0.1), kFft, kSr, kFft); };
    CHECK (centroidOf (1) > centroidOf (0) * 1.5);
    CHECK (centroidOf (2) < centroidOf (0) * 0.8);
    // 100 % is 36 semitones: the corner moves three octaves, and the zero step is the unmoved filter
    Synth zero = s;
    zero.set (step (2, "cutoff"), 0.0f);
    zero.set (step (3, "cutoff"), 0.0f);
    const auto z = zero.notes (3);
    CHECK_NEAR (sbtest::centroidOf (z.left, from (0, 0.1), kFft, kSr, kFft), sbtest::centroidOf (z.left, from (1, 0.1), kFft, kSr, kFft), 20.0);
}

TEST (voicecontrol_envelope_time_per_voice_and_scaling_of_all_envelopes)
{
    const auto attackLevel = [] (const sbtest::Rendered& r, int n) { return level (r, n, 0.0, 0.04); };
    Synth s;
    tone (s);
    s.set ("env1.attack", 80.0f);
    s.set ("voicectl.length", 3.0f);
    s.set (step (2, "env"), 100.0f);  // times x 4
    s.set (step (3, "env"), -100.0f); // times / 4
    const auto r = s.notes (3);
    CHECK (attackLevel (r, 1) < attackLevel (r, 0) * 0.6);
    CHECK (attackLevel (r, 2) > attackLevel (r, 0) * 1.3);
    // the scaling of all envelopes (global): 400 % slow, 25 % fast, 100 % unchanged
    for (const float percent : { 25.0f, 100.0f, 400.0f })
    {
        Synth g;
        tone (g);
        g.set ("env1.attack", 80.0f);
        g.set ("voicectl.scale.env", percent);
        const double l = attackLevel (g.notes (1), 0);
        Synth plain;
        tone (plain);
        plain.set ("env1.attack", 80.0f);
        const double p = attackLevel (plain.notes (1), 0);
        if (percent < 100.0f)
            CHECK (l > p * 1.3);
        else if (percent > 100.0f)
            CHECK (l < p * 0.6);
        else
            CHECK_NEAR (l, p, 1.0e-9);
    }
    // the release is scaled as well: a long note off tail with 400 %
    {
        Synth slow;
        tone (slow);
        slow.set ("env1.release", 40.0f);
        const auto fast = slow.notes (1);
        slow.set ("voicectl.scale.env", 400.0f);
        const auto stretched = slow.notes (1);
        const double a = sbtest::rmsOf (fast.left, from (0, kLength + 0.06), from (0, kLength + 0.12));
        const double b = sbtest::rmsOf (stretched.left, from (0, kLength + 0.06), from (0, kLength + 0.12));
        CHECK (b > a * 3.0 + 1.0e-6);
    }
}

TEST (voicecontrol_voice_index_and_voice_mods_are_modulation_sources)
{
    // the level of the oscillator is the source: osc level 0 + source x 100 %
    const auto levelPatch = [] (Synth& s, const char* source, int slot = 1)
    {
        tone (s);
        s.set ("osc1.level", 0.0f);
        route (s, slot, source, "osc1.level", 100.0f);
    };
    // Voice Mod 1: step 1 = +80 %, step 2 = +30 %, step 3 = -50 % (a negative level is silence)
    {
        Synth s;
        levelPatch (s, "voice_mod1");
        s.set ("voicectl.length", 3.0f);
        s.set (step (1, "mod1"), 80.0f);
        s.set (step (2, "mod1"), 30.0f);
        s.set (step (3, "mod1"), -50.0f);
        const auto r = s.notes (4);
        CHECK (level (r, 0) > level (r, 1) * 2.0);
        CHECK (level (r, 1) > 0.01);
        CHECK (level (r, 2) < 0.002);
        CHECK_NEAR (level (r, 3), level (r, 0), 0.02 * level (r, 0));
        // Voice Mod 2 is its own row
        Synth t;
        levelPatch (t, "voice_mod2");
        t.set ("voicectl.length", 2.0f);
        t.set (step (1, "mod1"), 100.0f);  // not the row of this source
        t.set (step (2, "mod2"), 60.0f);
        const auto q = t.notes (2);
        CHECK (level (q, 0) < 0.002);
        CHECK (level (q, 1) > 0.01);
    }
    // Voice Index: 0 at the first step, 1 at the last, equal steps in between
    {
        Synth s;
        levelPatch (s, "voice_index");
        s.set ("voicectl.length", 4.0f);
        const auto r = s.notes (5);
        CHECK (level (r, 0) < 0.002);
        CHECK (level (r, 1) > 0.01);
        CHECK (level (r, 2) > level (r, 1) * 1.5);
        CHECK (level (r, 3) > level (r, 2) * 1.2);
        CHECK (level (r, 4) < 0.002); // the sequence starts again
        // a length of 1 leaves the index at 0
        s.set ("voicectl.length", 1.0f);
        const auto one = s.notes (2);
        CHECK (level (one, 1) < 0.002);
    }
    // the sources need no voice control setting to exist: with all steps at zero Voice Mod 1 is silent, and the matrix can still use it
    {
        Synth s;
        levelPatch (s, "voice_mod1");
        CHECK (level (s.notes (1), 0) < 0.002);
    }
}

TEST (voicecontrol_lfo_scaling_by_percent_and_by_rate)
{
    // a Saw Up LFO that starts with the note sets the level: the louder the note at a given time, the faster the LFO
    const auto patch = [] (Synth& s)
    {
        tone (s);
        s.set ("osc1.level", 0.0f);
        s.set ("lfo1.shape", sbtest::choiceValue ("lfo1.shape", "Saw Up"));
        s.set ("lfo1.rate", 0.8f);
        s.set ("lfo1.sync", 0.0f);
        s.set ("lfo1.sync_type", 2.0f); // envelope: one run per note
        route (s, 1, "lfo1", "osc1.level", 100.0f);
    };
    const auto at = [] (const Synth& s) { return level (s.notes (1), 0, 0.1, 0.2); };
    Synth base;
    patch (base);
    const double reference = at (base);
    CHECK (reference > 0.001);
    // Percent: a time of 50 % is twice the rate, 200 % is half of it
    Synth fast = base;
    fast.set ("voicectl.scale.lfo", 50.0f);
    Synth slow = base;
    slow.set ("voicectl.scale.lfo", 200.0f);
    CHECK (at (fast) > reference * 1.4);
    CHECK (at (slow) < reference * 0.75);
    // Rate: 200 % is twice the rate, 50 % half of it
    Synth rateFast = base;
    rateFast.set ("voicectl.scale.lfo_mode", 1.0f);
    rateFast.set ("voicectl.scale.lfo", 200.0f);
    Synth rateSlow = base;
    rateSlow.set ("voicectl.scale.lfo_mode", 1.0f);
    rateSlow.set ("voicectl.scale.lfo", 50.0f);
    CHECK_NEAR (at (rateFast), at (fast), 0.01 * reference + 1.0e-6);
    CHECK (at (rateSlow) < reference * 0.75);
    // and the random LFOs follow too: the sample and hold of the first random LFO with the same seed per note changes its rate
    {
        Synth r;
        tone (r);
        r.set ("osc1.level", 0.0f);
        route (r, 1, "rlfo1", "osc1.level", 100.0f);
        r.set ("rlfo1.rate", 2.0f);
        r.set ("rlfo1.sync", 0.0f);
        Synth q = r;
        q.set ("voicectl.scale.lfo", 25.0f);
        CHECK (! identical (r.notes (1), q.notes (1)));
    }
}

TEST (voicecontrol_randomisation_is_stable_per_note_and_differs_between_notes)
{
    // pan: every note gets its own draw, a render repeats exactly
    {
        Synth s;
        tone (s);
        s.set ("voicectl.rand.pan", 100.0f);
        const auto a = s.notes (6);
        const auto b = s.notes (6);
        CHECK (identical (a, b));
        double lo = 2.0, hi = -2.0;
        for (int n = 0; n < 6; ++n)
        {
            lo = std::min (lo, balance (a, n));
            hi = std::max (hi, balance (a, n));
        }
        CHECK (hi - lo > 0.5);
        Synth none = s;
        none.set ("voicectl.rand.pan", 0.0f);
        const auto n0 = none.notes (6);
        for (int n = 0; n < 6; ++n)
            CHECK (std::fabs (balance (n0, n)) < 0.02);
    }
    // detune: up to +-RAND cents, ten times as much with the x10 switch
    for (const bool tenfold : { false, true })
    {
        Synth s;
        tone (s);
        s.set ("voicectl.rand.detune", 50.0f);
        s.set ("voicectl.rand.detune10x", tenfold ? 1.0f : 0.0f);
        const auto r = s.notes (8);
        double widest = 0.0;
        for (int n = 0; n < 8; ++n)
            widest = std::max (widest, std::fabs (cents (frequency (r, n, 261.6256) / 261.6256)));
        const double limit = tenfold ? 500.0 : 50.0;
        CHECK (widest <= limit + 6.0);
        CHECK (widest > limit * 0.25);
    }
    // cutoff and envelope time
    {
        Synth s;
        tone (s);
        s.set ("osc1.wavetable", static_cast<float> (kWavetableSaw));
        s.set ("filter1.on", 1.0f);
        s.set ("filter1.cutoff", 600.0f);
        s.set ("filter1.keytrack", 0.0f);
        s.set ("voicectl.rand.cutoff", 100.0f);
        const auto r = s.notes (6);
        double lo = 1.0e9, hi = 0.0;
        for (int n = 0; n < 6; ++n)
        {
            const double c = sbtest::centroidOf (r.left, from (n, 0.1), kFft, kSr, kFft);
            lo = std::min (lo, c);
            hi = std::max (hi, c);
        }
        CHECK (hi > lo * 1.3);
    }
    {
        Synth s;
        tone (s);
        s.set ("env1.attack", 80.0f);
        s.set ("voicectl.rand.env", 100.0f);
        const auto r = s.notes (6);
        double lo = 1.0e9, hi = 0.0;
        for (int n = 0; n < 6; ++n)
        {
            const double l = level (r, n, 0.0, 0.04);
            lo = std::min (lo, l);
            hi = std::max (hi, l);
        }
        CHECK (hi > lo * 1.3);
    }
}

TEST (voicecontrol_chords_use_one_step_per_note_in_order)
{
    // three notes at once: the three voices get the steps 1, 2, 3 (the note counter), each its own pan
    Synth s;
    tone (s);
    s.set ("voicectl.length", 3.0f);
    s.set (step (1, "pan"), -100.0f);
    s.set (step (2, "pan"), 0.0f);
    s.set (step (3, "pan"), 100.0f);
    auto e = s.build();
    const std::vector<Event> chord { noteOn (0, 60), noteOn (0, 64), noteOn (0, 67) };
    const auto r = sbtest::render (*e, chord, static_cast<int> (0.4 * kSr), 256);
    const double l = sbtest::rmsOf (r.left, static_cast<size_t> (0.15 * kSr), static_cast<size_t> (0.35 * kSr));
    const double rr = sbtest::rmsOf (r.right, static_cast<size_t> (0.15 * kSr), static_cast<size_t> (0.35 * kSr));
    CHECK (std::fabs (l - rr) < 0.05 * (l + rr)); // left voice and right voice cancel out in the balance, the middle one is shared
    // each side holds its own note: the lowest note is only left, the highest only right
    const auto spectrum = [&] (const std::vector<float>& x, double hz)
    {
        const auto p = sbtest::powerSpectrum (x, static_cast<size_t> (0.15 * kSr), kFft, kFft, sbtest::Window::blackmanHarris);
        return p[static_cast<size_t> (hz * kFft / kSr + 0.5)];
    };
    CHECK (spectrum (r.left, 261.6256) > 100.0 * spectrum (r.right, 261.6256));
    CHECK (spectrum (r.right, 391.9954) > 100.0 * spectrum (r.left, 391.9954));
}
