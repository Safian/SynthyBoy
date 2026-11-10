// SynthyBoy - engine-vital tests: the multisample oscillator (GROUPC WP-13, block B30): kCap2Multisample is proven here, every key is rendered off and on.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "groupc_util.h"
#include "synthyboy/state.h"

using namespace groupc;
using sbtest::noteOn;

namespace
{
const char* const kTwoZones =
    "<control> default_path=\n<global> amp_veltrack=0 ampeg_release=0.05\n"
    "<region> sample=low.wav lokey=0 hikey=72 pitch_keycenter=69\n"
    "<region> sample=high.wav lokey=73 hikey=127 pitch_keycenter=81\n";

void multiPatch (IEngine& e, Content& c)
{
    basePatch (e, c, kOscTypeMultisample);
    setByKey (e, "osc1.multi.vel_override", 0.0f);
}

Content twoZones()
{
    Content c;
    c.instrument (kTwoZones, { { "low.wav", sine (440.0, 2.0) }, { "high.wav", sine (1760.0, 2.0) } });
    return c;
}
} // namespace

TEST (multisample_capability_type_list_and_keys)
{
    auto e = makeVital();
    CHECK ((e->capabilities2() & kCap2Multisample) != 0);
    const auto* type = findParamByKey ("osc1.type");
    CHECK_EQ (type->max, 4.0f);
    CHECK_EQ (std::string (choiceName (*type, 4)), "Multisample");
    CHECK ((requiredCapsForValue2 (static_cast<size_t> (findParamIndex (type->id)), 4.0f) & kCap2Multisample) != 0);
    for (const char* key : { "osc1.multi.override", "osc2.multi.timbre", "osc3.multi.release" })
        CHECK ((requiredCaps2 (static_cast<size_t> (findParamIndex (findParamByKey (key)->id))) & kCap2Multisample) != 0);
    CHECK_EQ (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc3.multi.timbre")->id))), 986);
}

TEST (multisample_type_plays_the_zone_of_the_key_at_the_key_pitch)
{
    auto content = twoZones();
    auto e = makeVital();
    multiPatch (*e, content);
    // key 69: the low zone at its recorded pitch; key 72: three semitones up in the same zone; key 81: the high zone at its recorded pitch
    const auto a = play (*e, 69, 1.0);
    CHECK (sbtest::allFinite (a.left));
    CHECK (rmsBetween (a, 0.2, 0.8) > 0.03);
    CHECK_NEAR (crossingFrequency (a, 0.2, 0.8), 440.0, 15.0);
    e->reset();
    CHECK_NEAR (crossingFrequency (play (*e, 72, 1.0), 0.2, 0.8), 440.0 * std::pow (2.0, 3.0 / 12.0), 20.0);
    e->reset();
    CHECK_NEAR (crossingFrequency (play (*e, 81, 1.0), 0.2, 0.8), 1760.0, 60.0);
    // the transposition and the tune of the oscillator move it
    e = makeVital();
    multiPatch (*e, content);
    setByKey (*e, "osc1.transpose", 12.0f);
    CHECK_NEAR (crossingFrequency (play (*e, 69, 1.0), 0.2, 0.8), 880.0, 30.0);
    // a different type is a different sound: the wavetable oscillator
    e = makeVital();
    multiPatch (*e, content);
    setByKey (*e, "osc1.type", 0.0f);
    CHECK (relativeDifference (a, play (*e, 69, 1.0), 0.2, 0.8) > 0.3);
    // without an instrument (nothing loaded, or the SFZ names files that are not there): silence
    Content none;
    e = makeVital();
    multiPatch (*e, none);
    CHECK (sbtest::peak (play (*e, 69, 0.4).left) < 1.0e-6f);
    Content missing;
    missing.instrument (kTwoZones, {});
    e = makeVital();
    multiPatch (*e, missing);
    CHECK (sbtest::peak (play (*e, 69, 0.4).left) < 1.0e-6f);
    // the pitch tracking switch plays the recorded pitch whatever the key
    e = makeVital();
    multiPatch (*e, content);
    setByKey (*e, "osc1.pitch_track", 0.0f);
    CHECK_NEAR (crossingFrequency (play (*e, 90, 1.0), 0.2, 0.8), 1760.0, 60.0); // zone 2 (73 .. 127) at its recorded pitch
}

TEST (multisample_velocity_layers_envelope_override_velocity_tracking_and_timbre)
{
    Content layers;
    layers.instrument ("<global> ampeg_attack=0.25 ampeg_release=0.05 amp_veltrack=100\n"
                       "<region> sample=soft.wav key=69 lovel=1 hivel=70\n<region> sample=hard.wav key=69 lovel=71 hivel=127\n"
                       "<region> sample=up.wav lokey=100 hikey=100 pitch_keycenter=100\n",
                       { { "soft.wav", sine (300.0, 2.0) }, { "hard.wav", sine (900.0, 2.0) }, { "up.wav", sine (2000.0, 2.0) } });
    const auto render = [&] (const std::vector<std::pair<const char*, float>>& keys, int key, float velocity)
    {
        auto e = makeVital();
        multiPatch (*e, layers);
        for (const auto& k : keys)
            setByKey (*e, k.first, k.second);
        return play (*e, key, 1.0, velocity);
    };
    // the velocity picks the layer
    CHECK_NEAR (crossingFrequency (render ({}, 69, 0.4f), 0.4, 0.9), 300.0, 15.0);
    CHECK_NEAR (crossingFrequency (render ({}, 69, 1.0f), 0.4, 0.9), 900.0, 30.0);
    // the SFZ envelope has a 250 ms attack; OVERRIDE with no attack starts at once
    const auto sfzEnvelope = render ({}, 69, 1.0f);
    const auto overridden = render ({ { "osc1.multi.override", 1.0f }, { "osc1.multi.attack", 0.0f }, { "osc1.multi.decay", 0.0f }, { "osc1.multi.sustain", 100.0f } }, 69, 1.0f);
    CHECK (rmsBetween (sfzEnvelope, 0.0, 0.1) < 0.35 * rmsBetween (sfzEnvelope, 0.5, 0.9));
    CHECK (rmsBetween (overridden, 0.02, 0.1) > 0.7 * rmsBetween (overridden, 0.5, 0.9));
    // the override envelope: the delay, the hold / decay / sustain, the release
    const auto delayed = render ({ { "osc1.multi.override", 1.0f }, { "osc1.multi.delay", 300.0f }, { "osc1.multi.attack", 0.0f } }, 69, 1.0f);
    CHECK (rmsBetween (delayed, 0.0, 0.25) < 1.0e-3 && rmsBetween (delayed, 0.4, 0.8) > 0.05);
    const auto sustained = render ({ { "osc1.multi.override", 1.0f }, { "osc1.multi.attack", 0.0f }, { "osc1.multi.decay", 200.0f }, { "osc1.multi.sustain", 25.0f } }, 69, 1.0f);
    const auto full = render ({ { "osc1.multi.override", 1.0f }, { "osc1.multi.attack", 0.0f }, { "osc1.multi.decay", 200.0f }, { "osc1.multi.sustain", 100.0f } }, 69, 1.0f);
    CHECK_NEAR (rmsBetween (sustained, 0.6, 0.9) / rmsBetween (full, 0.6, 0.9), 0.25, 0.04);   // the sustain level
    CHECK (rmsBetween (sustained, 0.02, 0.06) > 1.5 * rmsBetween (sustained, 0.6, 0.9));         // the decay before it
    // VEL TRACK: the SFZ's 100 % makes a soft note quiet; the override with 0 % makes it as loud as a hard one
    const auto quietSfz = render ({ { "osc1.multi.override", 1.0f }, { "osc1.multi.attack", 0.0f } }, 69, 0.3f);
    const auto flat = render ({ { "osc1.multi.override", 1.0f }, { "osc1.multi.attack", 0.0f }, { "osc1.multi.vel_override", 1.0f }, { "osc1.multi.vel_track", 0.0f } }, 69, 0.3f);
    CHECK (rmsBetween (flat, 0.3, 0.9) > 1.8 * rmsBetween (quietSfz, 0.3, 0.9));
    // TIMBRE inverts the zone against the key: key 27 looks up 127 - 27 = 100 (the up.wav zone is mapped to key 100 only)
    CHECK (sbtest::peak (render ({}, 27, 1.0f).left) < 1.0e-6f);
    const auto inverted = render ({ { "osc1.multi.timbre", 100.0f } }, 27, 1.0f);
    CHECK (rmsBetween (inverted, 0.4, 0.9) > 0.03);
    // timbre as a modulation destination
    const auto destination = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc1.multi.timbre")->id)));
    const auto withMacro = [&] (float macro)
    {
        auto e = makeVital();
        multiPatch (*e, layers);
        setByKey (*e, "mod01.source", 19.0f);
        setByKey (*e, "mod01.destination", static_cast<float> (destination));
        setByKey (*e, "mod01.amount", 100.0f);
        setByKey (*e, "macro.1", macro);
        return rmsBetween (play (*e, 27, 1.0), 0.4, 0.9);
    };
    CHECK (withMacro (0.0f) < 1.0e-4 && withMacro (100.0f) > 0.03);
}

TEST (multisample_round_robin_key_switch_choke_group_and_loops)
{
    // round robin: three notes in a row are three different samples
    Content rr;
    rr.instrument ("<global> amp_veltrack=0 ampeg_release=0.02\n"
                   "<region> sample=a.wav key=60 seq_length=3 seq_position=1\n<region> sample=b.wav key=60 seq_length=3 seq_position=2\n<region> sample=c.wav key=60 seq_length=3 seq_position=3\n",
                   { { "a.wav", sine (300.0, 1.0) }, { "b.wav", sine (600.0, 1.0) }, { "c.wav", sine (1200.0, 1.0) } });
    auto e = makeVital();
    multiPatch (*e, rr);
    setByKey (*e, "osc1.sample.root", 60.0f);
    std::vector<double> pitches;
    for (int i = 0; i < 3; ++i)
    {
        e->reset();
        pitches.push_back (crossingFrequency (play (*e, 60, 0.6), 0.15, 0.5));
    }
    CHECK (std::fabs (pitches[0] - 300.0) < 25.0 && std::fabs (pitches[1] - 600.0) < 40.0 && std::fabs (pitches[2] - 1200.0) < 80.0);

    // key switch: the note c1 (24) selects the second articulation
    Content ks;
    ks.instrument ("<global> amp_veltrack=0 sw_default=c0 sw_lokey=c1 sw_hikey=d1\n"
                   "<region> sample=a.wav sw_last=c1 lokey=60 hikey=60\n<region> sample=b.wav sw_last=d1 lokey=60 hikey=60\n",
                   { { "a.wav", sine (300.0, 1.0) }, { "b.wav", sine (600.0, 1.0) } });
    e = makeVital();
    multiPatch (*e, ks);
    e->process (nullptr, 0, nullptr, nullptr, 0);
    const auto keyed = sbtest::render (*e, { noteOn (0, 26), noteOn (4800, 60) }, static_cast<int> (0.8 * kSr), 256); // d1 (26), then middle C
    CHECK_NEAR (crossingFrequency (keyed, 0.3, 0.7), 600.0, 40.0);

    // loops: loop_continuous keeps a short sample sounding for longer than the file
    Content loop;
    loop.instrument ("<global> amp_veltrack=0\n<region> sample=a.wav key=69 loop_mode=loop_continuous loop_start=0 loop_end=4800\n", { { "a.wav", sine (440.0, 0.1) } });
    e = makeVital();
    multiPatch (*e, loop);
    CHECK (rmsBetween (play (*e, 69, 1.0), 0.6, 0.9) > 0.05);
    Content once;
    once.instrument ("<global> amp_veltrack=0\n<region> sample=a.wav key=69 loop_mode=no_loop\n", { { "a.wav", sine (440.0, 0.1) } });
    e = makeVital();
    multiPatch (*e, once);
    CHECK (rmsBetween (play (*e, 69, 1.0), 0.6, 0.9) < 1.0e-3);

    // choke group: a note in group 2 silences the running group 1 note (off_by)
    Content choke;
    choke.instrument ("<global> amp_veltrack=0 ampeg_release=2\n<region> sample=a.wav key=60 group=1 off_by=2 off_mode=fast loop_mode=loop_continuous loop_start=0 loop_end=48000\n"
                      "<region> sample=b.wav key=62 group=2 volume=-60\n",
                      { { "a.wav", sine (440.0, 1.0) }, { "b.wav", sine (440.0, 0.05, 0.001f) } });
    e = makeVital();
    multiPatch (*e, choke);
    const auto chokeRender = sbtest::render (*e, { noteOn (0, 60), noteOn (24000, 62) }, static_cast<int> (1.5 * kSr), 256);
    CHECK (rmsBetween (chokeRender, 0.3, 0.45) > 0.03);
    CHECK (rmsBetween (chokeRender, 1.1, 1.4) < 0.1 * rmsBetween (chokeRender, 0.3, 0.45));
}

TEST (multisample_last_zone_readout_unison_and_determinism_at_extreme_settings)
{
    auto content = twoZones();
    auto e = makeVital();
    multiPatch (*e, content);
    play (*e, 80, 0.3);
    LiveValues live;
    CHECK (e->copyReadout (live));
    CHECK_EQ (live.multisampleZone[0], 1);   // the high zone
    CHECK_EQ (live.multisampleKey[0], 80);
    CHECK_EQ (live.multisampleZone[1], -1);

    const auto extreme = [&]
    {
        auto x = makeVital();
        multiPatch (*x, content);
        setByKey (*x, "osc1.unison.voices", 16.0f);
        setByKey (*x, "osc1.unison.detune", 90.0f);
        setByKey (*x, "osc1.multi.override", 1.0f);
        setByKey (*x, "osc1.multi.attack", 3.0f);
        setByKey (*x, "osc1.multi.delay", 20.0f);
        setByKey (*x, "osc1.multi.timbre", -100.0f);
        setByKey (*x, "osc1.random_phase", 100.0f);
        setByKey (*x, "osc1.warp.type", 27.0f);
        setByKey (*x, "osc1.warp.amount", 100.0f);
        return sbtest::render (*x, { noteOn (0, 12), noteOn (1000, 69), noteOn (2000, 127), noteOn (3000, 100) }, static_cast<int> (1.2 * kSr), 333);
    };
    const auto a = extreme(), b = extreme();
    CHECK (sbtest::allFinite (a.left) && sbtest::allFinite (a.right));
    CHECK (sbtest::peak (a.left) < 16.0f);
    CHECK (identical (a, b));
    // a unison of four voices: wider and louder than one
    const auto unison = [&] (float voices)
    {
        auto x = makeVital();
        multiPatch (*x, content);
        setByKey (*x, "osc1.unison.voices", voices);
        setByKey (*x, "osc1.unison.detune", 40.0f);
        return play (*x, 69, 0.8);
    };
    CHECK (relativeDifference (unison (1.0f), unison (4.0f), 0.2, 0.7) > 0.05);
}

TEST (multisample_instrument_content_survives_a_state_round_trip)
{
    auto content = twoZones();
    ParamValues values = ParamValues::defaults();
    values.plain[static_cast<size_t> (findParamIndex (findParamByKey ("osc2.type")->id))] = 4.0f;
    values.plain[static_cast<size_t> (findParamIndex (findParamByKey ("osc2.multi.release")->id))] = 1234.0f;
    content.state.instruments[0].osc = 2;
    content.state.instruments[0].zones = 2;
    const std::string text = saveState (values, content.state);
    ParamValues loaded = ParamValues::defaults();
    StateContent loadedContent;
    CHECK (loadStateWithContent (text.data(), text.size(), loaded, loadedContent).status == LoadStatus::ok);
    CHECK (loadedContent == content.state);
    CHECK_EQ (loadedContent.instruments.size(), static_cast<size_t> (1));
    CHECK_EQ (loadedContent.instruments[0].osc, 2);
    CHECK_EQ (loadedContent.instruments[0].ref.file, std::string ("inst/instrument.sfz"));
    CHECK_EQ (loaded.plain[static_cast<size_t> (findParamIndex (findParamByKey ("osc2.multi.release")->id))], 1234.0f);
    // the instrument of oscillator 2 plays after the reload
    auto e = makeVital();
    content.state = loadedContent;
    content.apply (*e);
    setByKey (*e, "osc2.type", 4.0f);
    setByKey (*e, "osc2.on", 1.0f);
    setByKey (*e, "osc1.on", 0.0f);
    setByKey (*e, "osc2.level", 100.0f);
    setByKey (*e, "filter1.on", 0.0f);
    setByKey (*e, "env1.attack", 0.0f);
    const auto r = play (*e, 69, 0.6);
    CHECK (rmsBetween (r, 0.2, 0.5) > 0.03);
    // an invalid instrument (a path that climbs out of the pack) is rejected by the validator
    StateContent bad = content.state;
    bad.instruments[0].ref.file = "../x.sfz";
    CHECK (! validateContent (bad));
}

// ---- REMAINDER item 4 (S12, 13-new-keys.md 4.43): the SFZ opcodes that were read and ignored: trigger=release, xfin_* / xfout_*, fil_* ------------------------------------------

TEST (multisample_trigger_release_plays_a_zone_when_the_key_is_released)
{
    // a 440 Hz zone while the key is held, a 1760 Hz zone (trigger=release, one shot) after it
    const char* sfz = "<control>\n<global> amp_veltrack=0 ampeg_release=0.05\n"
                      "<region> sample=body.wav lokey=0 hikey=127 pitch_keycenter=69\n"
                      "<region> sample=noise.wav lokey=0 hikey=127 pitch_keycenter=69 trigger=release loop_mode=one_shot\n";
    Content c;
    c.instrument (sfz, { { "body.wav", sine (440.0, 3.0) }, { "noise.wav", sine (1760.0, 2.0) } });
    auto e = makeVital();
    multiPatch (*e, c);
    setByKey (*e, "env1.release", 1500.0f);   // the voice (ENV 1) lives on after the key: the release zone sounds as long as the voice does
    const auto r = sbtest::render (*e, { noteOn (0, 69, 1.0f), sbtest::noteOff (static_cast<int> (0.6 * kSr), 69) }, static_cast<int> (1.6 * kSr), 256);
    CHECK (sbtest::allFinite (r.left));
    CHECK_NEAR (crossingFrequency (r, 0.2, 0.55), 440.0, 15.0);      // held: the body zone only
    CHECK (rmsBetween (r, 0.2, 0.55) > 0.03);
    CHECK_NEAR (crossingFrequency (r, 0.8, 1.4), 1760.0, 60.0);       // released: the release zone (the body has faded in 50 ms)
    CHECK (rmsBetween (r, 0.8, 1.4) > 0.03);
    // without trigger=release the same file is silent after the release
    const char* plain = "<control>\n<global> amp_veltrack=0 ampeg_release=0.05\n<region> sample=body.wav lokey=0 hikey=127 pitch_keycenter=69\n";
    Content p;
    p.instrument (plain, { { "body.wav", sine (440.0, 3.0) } });
    auto e2 = makeVital();
    multiPatch (*e2, p);
    setByKey (*e2, "env1.release", 1500.0f);
    const auto s = sbtest::render (*e2, { noteOn (0, 69, 1.0f), sbtest::noteOff (static_cast<int> (0.6 * kSr), 69) }, static_cast<int> (1.6 * kSr), 256);
    CHECK (rmsBetween (s, 0.8, 1.4) < 0.002);
}

TEST (multisample_crossfade_and_filter_opcodes_change_the_sound)
{
    // xfin / xfout by key: between key 60 and 72 the low zone fades out and the high zone fades in
    const char* xf = "<control>\n<global> amp_veltrack=0\n"
                     "<region> sample=low.wav lokey=0 hikey=127 pitch_keycenter=69 xfout_lokey=60 xfout_hikey=72\n"
                     "<region> sample=high.wav lokey=0 hikey=127 pitch_keycenter=69 xfin_lokey=60 xfin_hikey=72\n";
    const auto levelOf = [&] (const char* text, int key)
    {
        Content c;
        c.instrument (text, { { "low.wav", sine (440.0, 2.0) }, { "high.wav", sine (440.0, 2.0) } });
        auto e = makeVital();
        multiPatch (*e, c);
        return rmsBetween (play (*e, key, 0.8), 0.2, 0.7);
    };
    const double at60 = levelOf (xf, 60), at66 = levelOf (xf, 66), at72 = levelOf (xf, 72);
    CHECK (at60 > 0.02 && at72 > 0.02);
    // equal power: the two zones sum to the same level at the middle of the fade as at its ends (the zones are identical here, coherent sum, so the middle is above the ends by sin + cos)
    CHECK (at66 > 0.9 * at60);
    // a zone with fil_type=lpf_2p at a low cutoff removes the high partials of a bright sample
    const auto bright = [] { std::vector<float> v (96000); for (size_t i = 0; i < v.size(); ++i) { const double t = static_cast<double> (i) / kSr; v[i] = 0.2f * static_cast<float> (std::sin (2.0 * kPi * 440.0 * t) + std::sin (2.0 * kPi * 4400.0 * t)); } return v; }();
    const auto filtered = [&] (const char* fil)
    {
        const std::string text = std::string ("<control>\n<global> amp_veltrack=0\n<region> sample=b.wav lokey=0 hikey=127 pitch_keycenter=69 ") + fil + "\n";
        Content c;
        c.instrument (text, { { "b.wav", bright } });
        auto e = makeVital();
        multiPatch (*e, c);
        return play (*e, 69, 0.8);
    };
    const auto open = filtered(""), low = filtered("fil_type=lpf_2p cutoff=700"), high = filtered("fil_type=hpf_2p cutoff=3000"), keytracked = filtered("fil_type=lpf_2p cutoff=700 fil_keytrack=1200 fil_keycenter=57");
    CHECK (rmsBetween (open, 0.2, 0.7) > 0.05);
    CHECK (rmsBetween (low, 0.2, 0.7) < 0.85 * rmsBetween (open, 0.2, 0.7));           // the 4.4 kHz partial is gone, the 440 Hz one stays
    CHECK (rmsBetween (low, 0.2, 0.7) > 0.4 * rmsBetween (open, 0.2, 0.7));
    CHECK (rmsBetween (high, 0.2, 0.7) < 0.85 * rmsBetween (open, 0.2, 0.7));
    CHECK (relativeDifference (open, low, 0.2, 0.7) > 0.3);
    CHECK (relativeDifference (low, keytracked, 0.2, 0.7) > 0.05);                      // 12 keys above the centre: the cutoff an octave up
    CHECK (sbtest::allFinite (low.left) && sbtest::allFinite (high.left) && sbtest::allFinite (keytracked.left));
}
