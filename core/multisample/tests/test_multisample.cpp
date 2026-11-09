// SynthyBoy - core/multisample tests: the SFZ parser, the instrument, the zone choice, the layer voice and its envelope.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/multisample/multisample.h"

#include <cmath>
#include <map>
#include <vector>

#include "test_runner.h"

using namespace synthyboy::multisample;

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kSr = 48000.0;

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

std::vector<float> sine (double hz, double seconds, float amp = 0.5f)
{
    std::vector<float> v (static_cast<size_t> (seconds * kSr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = amp * static_cast<float> (std::sin (2.0 * kPi * hz * static_cast<double> (i) / kSr));
    return v;
}

struct Files
{
    std::map<std::string, std::vector<uint8_t>> bytes;
    SampleReader reader() const
    {
        return [this] (const std::string& path, std::vector<uint8_t>& out)
        {
            const auto it = bytes.find (path);
            if (it == bytes.end())
                return false;
            out = it->second;
            return true;
        };
    }
};

double crossingHz (const std::vector<float>& x, size_t from, size_t to)
{
    int n = 0;
    for (size_t i = from + 1; i < to && i < x.size(); ++i)
        n += (x[i - 1] < 0.0f) != (x[i] < 0.0f) ? 1 : 0;
    return 0.5 * n * kSr / static_cast<double> (to - from);
}

double peakOf (const std::vector<float>& x, size_t from, size_t to)
{
    double p = 0.0;
    for (size_t i = from; i < to && i < x.size(); ++i)
        p = std::max (p, static_cast<double> (std::fabs (x[i])));
    return p;
}

std::vector<float> play (LayerVoice& v, int frames, float semitones, int releaseAt = -1, bool fast = false)
{
    std::vector<float> l (static_cast<size_t> (frames), 0.0f), r (static_cast<size_t> (frames), 0.0f), tl (256), tr (256);
    for (int done = 0; done < frames; done += 256)
    {
        if (releaseAt >= 0 && done == releaseAt)
            v.noteOff (fast);
        v.processAdd (l.data() + done, r.data() + done, tl.data(), tr.data(), std::min (256, frames - done), semitones, nullptr, 1.0f, 1.0f);
    }
    return l;
}
} // namespace

TEST (multisample_note_names_and_the_sfz_parser)
{
    CHECK_EQ (parseNoteName ("c4"), 60);
    CHECK_EQ (parseNoteName ("C#3"), 49);
    CHECK_EQ (parseNoteName ("Eb3"), 51);
    CHECK_EQ (parseNoteName ("a-1"), 9);
    CHECK_EQ (parseNoteName ("60"), -1);
    CHECK_EQ (parseNoteName ("h2"), -1);

    const std::string text =
        "// a comment\n<control>\ndefault_path=Piano Samples\\\n#define $LOW 30\n"
        "<global>\nampeg_attack=0.01 ampeg_release=0.5\namp_veltrack=40\nloop_mode=loop_continuous\n"
        "<group> lovel=1 hivel=63 volume=-3\n"
        "<region> sample=Piano C3.wav pitch_keycenter=c3 lokey=$LOW hikey=e3 tune=-14 loop_start=100 loop_end=2000\n"
        "<region>\nsample=Piano D3.wav key=d3 volume=2 /* inline */ pan=50\n"
        "<group> lovel=64 hivel=127\n<region> sample=soft sample name.wav lokey=0 hikey=127 pitch_keycenter=60 amp_velcurve_1=0.1 amp_velcurve_127=1 seq_length=2 seq_position=2 lorand=0.5 hirand=1\n"
        "<region> sw_last=c1 sample=Piano C3.wav ampeg_hold=0.2\n";
    const auto sfz = parseSfz (text);
    CHECK_EQ (sfz.regions.size(), static_cast<size_t> (4));
    const Region& a = sfz.regions[0];
    CHECK_EQ (a.sample, std::string ("Piano Samples/Piano C3.wav"));   // default_path, backslashes
    CHECK_EQ (a.center, 48);
    CHECK_EQ (a.lokey, 30);                                           // #define
    CHECK_EQ (a.hikey, 52);
    CHECK_NEAR (a.tuneCents, -14.0, 1.0e-6);
    CHECK_NEAR (a.volumeDb, -3.0, 1.0e-6);                            // <group>
    CHECK_EQ (a.hivel, 63);
    CHECK_NEAR (a.attack, 0.01, 1.0e-6);                              // <global>
    CHECK_NEAR (a.velTrack, 40.0, 1.0e-6);
    CHECK_EQ (a.loopMode, 2);
    CHECK_EQ (a.loopStart, static_cast<int64_t> (100));
    CHECK (a.hasEnvelope);
    const Region& b = sfz.regions[1];
    CHECK_EQ (b.lokey, 50);
    CHECK_EQ (b.hikey, 50);
    CHECK_EQ (b.center, 50);                                          // key= sets all three
    CHECK_NEAR (b.pan, 0.5, 1.0e-6);
    CHECK_NEAR (b.volumeDb, 2.0, 1.0e-6);                            // the region's own value wins over the group's -3
    const Region& c = sfz.regions[2];
    CHECK_EQ (c.sample, std::string ("Piano Samples/soft sample name.wav")); // spaces in the path
    CHECK_EQ (c.seqLength, 2);
    CHECK_EQ (c.seqPosition, 2);
    CHECK_NEAR (c.lorand, 0.5, 1.0e-6);
    CHECK_EQ (c.velCurve.size(), static_cast<size_t> (2));
    CHECK_EQ (sfz.regions[3].swLast, 24);
    CHECK_NEAR (sfz.regions[3].hold, 0.2, 1.0e-6);
    // a double slash inside a path is not a comment (libSFZ writes "Samples//file.flac"), a double slash after a blank is
    const auto doubled = parseSfz ("<region> sample=Tuba Long Samples//XFtuba C#2.flac lokey=3 // trailing comment\n// whole line\n<region> sample=b.wav key=5\n");
    CHECK_EQ (doubled.regions.size(), static_cast<size_t> (2));
    CHECK_EQ (doubled.regions[0].sample, std::string ("Tuba Long Samples//XFtuba C#2.flac"));
    CHECK_EQ (doubled.regions[0].lokey, 3);
    // garbage never throws and yields no regions
    CHECK (parseSfz ("<region> <<<< === \0 sample=").regions.empty());
    CHECK (parseSfz ("").regions.empty());
}

TEST (multisample_instrument_decodes_the_samples_and_chooses_zones)
{
    Files files;
    files.bytes["low.wav"] = wav16 (sine (220.0, 0.5));
    files.bytes["high.wav"] = wav16 (sine (880.0, 0.5));
    files.bytes["soft.wav"] = wav16 (sine (440.0, 0.5, 0.1f));
    files.bytes["loud.wav"] = wav16 (sine (440.0, 0.5, 0.8f));
    const auto sfz = parseSfz ("<region> sample=low.wav lokey=0 hikey=59 pitch_keycenter=57 lovel=1 hivel=127\n"
                               "<region> sample=high.wav lokey=60 hikey=127 pitch_keycenter=81\n"
                               "<region> sample=soft.wav key=69 lovel=1 hivel=63 group=1\n"
                               "<region> sample=loud.wav key=69 lovel=64 hivel=127 off_by=1\n"
                               "<region> sample=missing.wav lokey=100 hikey=101\n"
                               "<region> sample=low.wav lokey=10 hikey=10 seq_length=3 seq_position=2\n"
                               "<region> sample=high.wav lokey=11 hikey=11 lorand=0.5 hirand=1.0\n"
                               "<region> sample=high.wav sw_last=c1 lokey=12 hikey=12\n");
    Instrument inst;
    CHECK_EQ (inst.build (sfz, files.reader()), 7);
    CHECK_EQ (inst.numZones(), 8);
    CHECK_EQ (inst.numMissing(), 1);
    CHECK_EQ (inst.numPlayable(), 7);
    CHECK_EQ (inst.numSkipped(), 0);
    int out[4];
    CHECK_EQ (inst.selectZones (40, 100, 0.3f, 0, -1, out, 4), 1);
    CHECK_EQ (out[0], 0);
    CHECK_EQ (inst.selectZones (70, 100, 0.3f, 0, -1, out, 4), 1);
    CHECK_EQ (out[0], 1);
    CHECK_EQ (inst.selectZones (69, 20, 0.3f, 0, -1, out, 4), 2);   // the low zone is 0 .. 59: key 69 belongs to the high zone and the soft layer
    CHECK_EQ (out[1], 2);
    CHECK_EQ (inst.selectZones (69, 100, 0.3f, 0, -1, out, 4), 2);  // high zone + loud layer
    CHECK_EQ (out[1], 3);
    CHECK_EQ (inst.selectZones (100, 100, 0.3f, 0, -1, out, 4), 1); // the missing file is not played; the high zone is
    // round robin: the zone of the position 2 of 3 plays on every third note
    int plays = 0;
    for (uint32_t counter = 0; counter < 9; ++counter)
        plays += inst.selectZones (10, 100, 0.3f, counter, -1, out, 4);
    CHECK_EQ (plays, 3 + 9); // zone 0 (0 .. 59) always plays, the round robin zone 3 times
    // random layers
    CHECK_EQ (inst.selectZones (11, 100, 0.2f, 0, -1, out, 4), 1);
    CHECK_EQ (inst.selectZones (11, 100, 0.7f, 0, -1, out, 4), 2);
    // key switches
    CHECK (inst.hasKeySwitches());
    CHECK_EQ (inst.selectZones (12, 100, 0.3f, 0, -1, out, 4), 1);
    CHECK_EQ (inst.selectZones (12, 100, 0.3f, 0, 24, out, 4), 2);
    // the maximum of layers is respected
    CHECK_EQ (inst.selectZones (69, 100, 0.3f, 0, -1, out, 1), 1);

    // timbre
    CHECK_EQ (Instrument::timbreKey (30, 0.0f), 30);
    CHECK_EQ (Instrument::timbreKey (30, 1.0f), 97);   // inverted: 127 - key
    CHECK_EQ (Instrument::timbreKey (64, 1.0f), 63);
    CHECK_EQ (Instrument::timbreKey (100, -1.0f), 127);
    CHECK_EQ (Instrument::timbreKey (10, -1.0f), 0);
}

TEST (multisample_layer_voice_plays_a_zone_at_pitch_with_the_sfz_envelope_and_velocity)
{
    Files files;
    files.bytes["a.wav"] = wav16 (sine (440.0, 2.0));
    Instrument inst;
    inst.build (parseSfz ("<region> sample=a.wav pitch_keycenter=69 ampeg_attack=0.1 ampeg_release=0.2 amp_veltrack=100 loop_mode=loop_continuous loop_start=0 loop_end=96000"), files.reader());
    LayerVoice v;
    v.prepare (kSr);
    v.noteOn (inst, 0, 69, 127, nullptr, -1.0f, 1, synthyboy::sampleosc::QualityTier::medium);
    const auto out = play (v, 24000, 0.0f);
    CHECK_NEAR (crossingHz (out, 9600, 24000), 440.0, 8.0);
    CHECK (peakOf (out, 0, 480) < 0.1);                // the attack of 100 ms
    CHECK (peakOf (out, 6000, 9000) > 0.3);            // full level after it (0.5 x the 0.707 of the centre pan law of the sample voice)
    // an octave up
    LayerVoice w;
    w.prepare (kSr);
    w.noteOn (inst, 0, 81, 127, nullptr, -1.0f, 2, synthyboy::sampleosc::QualityTier::medium);
    CHECK_NEAR (crossingHz (play (w, 24000, 12.0f), 9600, 24000), 880.0, 15.0);
    // velocity tracking: the SFZ's 100 % makes velocity 32 about a quarter as loud; the override 0 % makes it equal
    LayerVoice soft, tracked, flat;
    for (LayerVoice* x : { &soft, &tracked, &flat }) x->prepare (kSr);
    soft.noteOn (inst, 0, 69, 127, nullptr, -1.0f, 3, synthyboy::sampleosc::QualityTier::medium);
    tracked.noteOn (inst, 0, 69, 32, nullptr, -1.0f, 4, synthyboy::sampleosc::QualityTier::medium);
    flat.noteOn (inst, 0, 69, 32, nullptr, 0.0f, 5, synthyboy::sampleosc::QualityTier::medium);
    const double loud = peakOf (play (soft, 12000, 0.0f), 6000, 12000), quiet = peakOf (play (tracked, 12000, 0.0f), 6000, 12000), same = peakOf (play (flat, 12000, 0.0f), 6000, 12000);
    CHECK_NEAR (quiet / loud, 32.0 / 127.0, 0.03);
    CHECK_NEAR (same / loud, 1.0, 0.03);
    // release: after the note off the tail dies within the release time
    LayerVoice r;
    r.prepare (kSr);
    r.noteOn (inst, 0, 69, 127, nullptr, -1.0f, 6, synthyboy::sampleosc::QualityTier::medium);
    const auto released = play (r, 48000, 0.0f, 12032);
    CHECK (peakOf (released, 12032, 12500) > 0.2);
    CHECK (peakOf (released, 12032 + 9600 + 512, 48000) < 1.0e-4);
    CHECK (! r.isActive());
    // the override envelope replaces the SFZ's
    EnvelopeSettings override;
    override.attack = 0.0f;
    override.release = 0.01f;
    LayerVoice o;
    o.prepare (kSr);
    o.noteOn (inst, 0, 69, 127, &override, -1.0f, 7, synthyboy::sampleosc::QualityTier::medium);
    const auto fast = play (o, 24000, 0.0f);
    CHECK (peakOf (fast, 480, 1500) > 0.3);            // no attack: full level at once
    // delay, hold, decay, sustain of the override
    EnvelopeSettings shape;
    shape.delay = 0.05f; shape.attack = 0.0f; shape.hold = 0.05f; shape.decay = 0.1f; shape.sustain = 0.5f; shape.release = 0.05f;
    LayerVoice s;
    s.prepare (kSr);
    s.noteOn (inst, 0, 69, 127, &shape, 0.0f, 8, synthyboy::sampleosc::QualityTier::medium);
    const auto stepped = play (s, 24000, 0.0f);
    CHECK (peakOf (stepped, 0, 2000) < 1.0e-4);       // 50 ms of delay
    CHECK (peakOf (stepped, 2600, 4700) > 0.3);       // the hold (the sample voice has its own centre pan law: 0.5 x 0.707)
    CHECK_NEAR (peakOf (stepped, 14000, 24000) / peakOf (stepped, 2600, 4700), 0.5, 0.06); // sustain 50 %
}

TEST (multisample_one_shot_loop_sustain_and_choke_semantics)
{
    Files files;
    files.bytes["a.wav"] = wav16 (sine (440.0, 0.5));
    Instrument inst;
    inst.build (parseSfz ("<region> sample=a.wav key=69 loop_mode=one_shot ampeg_release=0.01\n<region> sample=a.wav key=70 loop_mode=loop_sustain loop_start=0 loop_end=24000 ampeg_release=0.01\n"
                          "<region> sample=a.wav key=71 group=3 off_by=4 off_mode=fast ampeg_release=1\n"), files.reader());
    // one shot: the note off does not stop it
    LayerVoice a;
    a.prepare (kSr);
    a.noteOn (inst, 0, 69, 100, nullptr, -1.0f, 1, synthyboy::sampleosc::QualityTier::medium);
    const auto shot = play (a, 24000, 0.0f, 1024);
    CHECK (peakOf (shot, 12000, 14000) > 0.1);
    // loop_sustain: it loops while held (longer than the 0.5 s sample) and stops after the release
    LayerVoice b;
    b.prepare (kSr);
    b.noteOn (inst, 1, 70, 100, nullptr, -1.0f, 2, synthyboy::sampleosc::QualityTier::medium);
    const auto looped = play (b, 72192, 0.0f, 47872);
    CHECK (peakOf (looped, 30000, 40000) > 0.1);
    CHECK (peakOf (looped, 60000, 72000) < 1.0e-3);
    // choke: a fast release is 10 ms even when the SFZ release is a second
    LayerVoice c;
    c.prepare (kSr);
    c.noteOn (inst, 2, 71, 100, nullptr, -1.0f, 3, synthyboy::sampleosc::QualityTier::medium);
    CHECK_EQ (c.group(), 3);
    CHECK_EQ (c.offBy(), 4);
    const auto choked = play (c, 24000, 0.0f, 4096, true);
    CHECK (peakOf (choked, 4096 + 1024, 24000) < 1.0e-3);
}

TEST (multisample_instrument_limits_and_garbage)
{
    Files files;
    files.bytes["junk.wav"] = std::vector<uint8_t> { 1, 2, 3, 4, 5 };
    files.bytes["a.wav"] = wav16 (sine (440.0, 0.1));
    Instrument inst;
    CHECK_EQ (inst.build (parseSfz ("<region> sample=junk.wav\n<region> sample=nothing.wav\n<region> sample=a.wav lokey=5 hikey=5 end=999999999 offset=999999999"), files.reader()), 1);
    CHECK_EQ (inst.numMissing(), 2);
    int out[4];
    CHECK_EQ (inst.selectZones (5, 100, 0.5f, 0, -1, out, 4), 1);
    LayerVoice v;
    v.prepare (kSr);
    v.noteOn (inst, out[0], 5, 100, nullptr, -1.0f, 1, synthyboy::sampleosc::QualityTier::low);
    const auto audio = play (v, 4800, 0.0f);
    for (float x : audio)
        CHECK (std::isfinite (x));
    // an empty instrument plays nothing
    Instrument none;
    CHECK_EQ (none.build (parseSfz (""), files.reader()), 0);
    CHECK_EQ (none.selectZones (60, 100, 0.5f, 0, -1, out, 4), 0);
    // extreme pitch and a very high velocity curve stay finite
    LayerVoice w;
    w.prepare (kSr);
    Instrument loud;
    loud.build (parseSfz ("<region> sample=a.wav volume=40 amp_veltrack=300 amp_velcurve_127=1"), files.reader());
    w.noteOn (loud, 0, 127, 127, nullptr, -1.0f, 2, synthyboy::sampleosc::QualityTier::high);
    for (float x : play (w, 4800, 60.0f))
        CHECK (std::isfinite (x));
}

TEST (multisample_stored_sample_paths_are_safe)
{
    CHECK_EQ (storedSamplePath ("../../../Samples/Factory/Synth/SID Tarkus C0.flac", true), std::string ("Samples/Factory/Synth/SID Tarkus C0.wav"));
    CHECK_EQ (storedSamplePath ("a\\b\\c.WAV", true), std::string ("a/b/c.wav"));
    CHECK_EQ (storedSamplePath ("/etc/./passwd", false), std::string ("etc/passwd"));
    CHECK_EQ (storedSamplePath ("C:/x/y.flac", true), std::string ("C_/x/y.wav"));
    CHECK_EQ (storedSamplePath ("../..", true), std::string ("sample.wav"));
    CHECK_EQ (storedSamplePath ("dir.d/file", true), std::string ("dir.d/file.wav"));
}

TEST (multisample_sfz_writer_round_trips_every_opcode_the_parser_reads)
{
    const auto a = parseSfz ("<global> sw_default=c0\n<group> ampeg_attack=0.01 ampeg_decay=0.2 ampeg_sustain=60 ampeg_release=0.4 amp_veltrack=35 amp_keytrack=0.1 loop_crossfade=0.02\n"
                             "<region> sample=A Folder/one.wav lokey=c2 hikey=e3 pitch_keycenter=d3 lovel=10 hivel=90 tune=-7 transpose=2 volume=-4 pan=-30 offset=100 end=5000 loop_mode=loop_sustain loop_start=200 loop_end=4000 "
                             "amp_velcurve_20=0.2 amp_velcurve_100=0.9 group=3 off_by=4 off_mode=normal seq_length=4 seq_position=3 lorand=0.25 hirand=0.75 sw_last=c1 sw_lokey=c1 sw_hikey=e1\n"
                             "<region> sample=two.wav key=60\n");
    const auto b = parseSfz (writeSfz (a));
    CHECK_EQ (b.swDefault, a.swDefault);
    CHECK_EQ (b.regions.size(), a.regions.size());
    for (size_t i = 0; i < a.regions.size() && i < b.regions.size(); ++i)
    {
        const Region &x = a.regions[i], &y = b.regions[i];
        CHECK (x.sample == y.sample && x.lokey == y.lokey && x.hikey == y.hikey && x.center == y.center && x.lovel == y.lovel && x.hivel == y.hivel);
        CHECK (std::fabs (x.tuneCents - y.tuneCents) < 1.0e-5f && x.transpose == y.transpose && std::fabs (x.volumeDb - y.volumeDb) < 1.0e-5f && std::fabs (x.pan - y.pan) < 1.0e-5f);
        CHECK (x.offset == y.offset && x.end == y.end && x.loopMode == y.loopMode && x.loopStart == y.loopStart && x.loopEnd == y.loopEnd);
        CHECK (x.hasEnvelope == y.hasEnvelope && std::fabs (x.attack - y.attack) < 1.0e-6f && std::fabs (x.decay - y.decay) < 1.0e-6f && std::fabs (x.sustain - y.sustain) < 1.0e-5f
               && std::fabs (x.release - y.release) < 1.0e-6f && std::fabs (x.velTrack - y.velTrack) < 1.0e-5f && std::fabs (x.keyTrackDb - y.keyTrackDb) < 1.0e-6f);
        CHECK (x.velCurve == y.velCurve && x.group == y.group && x.offBy == y.offBy && x.offMode == y.offMode && x.seqLength == y.seqLength && x.seqPosition == y.seqPosition);
        CHECK (std::fabs (x.lorand - y.lorand) < 1.0e-6f && std::fabs (x.hirand - y.hirand) < 1.0e-6f && x.swLast == y.swLast && x.swLokey == y.swLokey && x.swHikey == y.swHikey);
        CHECK (std::fabs (x.loopCrossfadeSec - y.loopCrossfadeSec) < 1.0e-6f);
    }
}
