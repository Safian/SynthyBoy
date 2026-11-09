// SynthyBoy - engine-vital tests of GROUPD (WP-14 / WP-15 / WP-16, 13-new-keys.md 4.40): the offline render entry (isolation from the live engine, determinism, ultra
// quality, no tier caps), Switch to wavetable (sample -> table -> audible), Resample, Render OSC Warp (baked table sounds like the warped oscillator), the audio of
// the last note. The pure parts (frame <-> LFO, WAV, own engine) are in core/tests/test_wave_convert.cpp.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <atomic>
#include <thread>

#include "groupc_util.h"
#include "groupd_util.h"
#include "synthyboy/offline_render.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"

using namespace groupc;
using namespace groupd;

namespace
{
void plain (IEngine& e)
{
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.phase", 0.0f);
    setByKey (e, "osc2.on", 0.0f);
    setByKey (e, "osc3.on", 0.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
    setByKey (e, "env1.attack", 0.0f);
    setByKey (e, "env1.sustain", 100.0f);
    setByKey (e, "env1.release", 50.0f);
}

ParamValues valuesOf (const IEngine& e)
{
    ParamValues v = ParamValues::defaults();
    for (size_t i = 0; i < paramCount(); ++i)
        v.plain[i] = e.getParam (paramTable()[i].id);
    return v;
}

convert::OfflineRunner runnerOf (IEngine& e)
{
    return [&e] (const OfflineRequest& r, OfflineAudio& a) { return e.renderOffline (r, a); };
}

OfflineRequest oneNote (int key, double seconds, double tail = 0.0)
{
    OfflineRequest r;
    r.steps.resize (1);
    r.steps[0].seconds = seconds;
    OfflineNote n;
    n.key = key;
    n.velocity = 0.9f;
    n.lengthSeconds = seconds;
    r.steps[0].notes.push_back (n);
    r.tailSeconds = tail;
    return r;
}

/** The live engine plays a note for 60 blocks; `offline` runs a render at block 20 (0: none, 1: on the same thread, 2: on another thread while the blocks go on). */
std::vector<float> liveWithOffline (int offline, const Quality& quality = Quality {})
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, quality);
    plain (*e);
    setByKey (*e, "filter1.on", 1.0f);
    setByKey (*e, "filter1.cutoff", 2500.0f);
    setByKey (*e, "lfo1.rate", 3.0f);
    std::vector<float> out, l (256), r (256);
    std::thread worker;
    OfflineAudio audio;
    for (int b = 0; b < 60; ++b)
    {
        std::vector<Event> ev;
        if (b == 0)
            ev.push_back (sbtest::noteOn (0, 50, 0.8f));
        if (b == 20 && offline == 1)
            CHECK (e->renderOffline (oneNote (60, 0.4, 0.2), audio));
        if (b == 20 && offline == 2)
            worker = std::thread ([&e, &audio] { e->renderOffline (oneNote (72, 0.6, 0.3), audio); });
        e->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 256);
        out.insert (out.end(), l.begin(), l.end());
        out.insert (out.end(), r.begin(), r.end());
    }
    if (worker.joinable())
        worker.join();
    if (offline != 0)
        CHECK (audio.frames() > 10000);
    return out;
}
} // namespace

TEST (offline_render_does_not_disturb_the_live_engine)
{
    const auto reference = liveWithOffline (0);
    CHECK (sbtest::peak (reference) > 0.01f);
    CHECK (liveWithOffline (1) == reference);   // a render between two blocks: the live output is bit identical
    CHECK (liveWithOffline (2) == reference);   // a render on another thread while the audio goes on
}

TEST (offline_render_is_deterministic_and_ultra_quality_is_the_4x_oversampling)
{
    auto e = makeVital();
    plain (*e);
    setByKey (*e, "osc1.warp.type", sbtest::choiceValue ("osc1.warp.type", "Sync"));
    setByKey (*e, "osc1.warp.amount", 70.0f);
    OfflineAudio a, b, ultraA, ultraB;
    auto request = oneNote (96, 0.5, 0.1);
    CHECK (e->renderOffline (request, a));
    CHECK (e->renderOffline (request, b));
    CHECK (a.left == b.left && a.right == b.right);
    request.ultra = true;
    CHECK (e->renderOffline (request, ultraA));
    CHECK (e->renderOffline (request, ultraB));
    CHECK (ultraA.left == ultraB.left);
    CHECK (a.left.size() == ultraA.left.size());
    double d = 0.0, n = 0.0;
    for (size_t i = 0; i < a.left.size(); ++i)
    {
        d += std::pow (static_cast<double> (a.left[i]) - ultraA.left[i], 2.0);
        n += std::pow (static_cast<double> (a.left[i]), 2.0);
    }
    std::printf ("    ultra against normal quality at C7 with Sync: relative difference %.4f\n", std::sqrt (d / n));
    CHECK (std::sqrt (d / n) > 1.0e-3);           // 4x oversampling is audible (less aliasing, here a very aliased Sync at C7)
    // a plain saw at C4 is the same sound at both qualities
    setByKey (*e, "osc1.warp.type", 0.0f);
    OfflineAudio plainNormal, plainUltra;
    auto plainRequest = oneNote (60, 0.5);
    CHECK (e->renderOffline (plainRequest, plainNormal));
    plainRequest.ultra = true;
    CHECK (e->renderOffline (plainRequest, plainUltra));
    {
        const double f0 = 440.0 * std::pow (2.0, (60 - 69) / 12.0);
        const auto normal = harmonicDb (plainNormal.left, 4800, 19200, f0, kSr, 10), ultra = harmonicDb (plainUltra.left, 4800, 19200, f0, kSr, 10);
        const double worst = worstHarmonicDifference (normal, ultra, 40.0);
        std::printf ("    plain saw at C4: harmonics 1..10 normal against ultra, worst difference %.3f dB\n", worst);
        CHECK (worst < 0.5);   // the same sound ...
        CHECK (plainNormal.left != plainUltra.left);   // ... from a different (4x) render
    }
    setByKey (*e, "osc1.warp.type", sbtest::choiceValue ("osc1.warp.type", "Sync"));
    // the request values replace the live ones: the shell's parameters are newer than the engine's
    ParamValues values = valuesOf (*e);
    values.plain[findParamIndex (findParamByKey ("osc1.warp.amount")->id)] = 0.0f;
    request.ultra = false;
    request.values = &values;
    OfflineAudio fromValues;
    CHECK (e->renderOffline (request, fromValues));
    CHECK (fromValues.left != a.left);
    // and the live engine kept its own value
    CHECK_NEAR (e->getParam (findParamByKey ("osc1.warp.amount")->id), 70.0, 1.0e-3);
}

TEST (offline_render_has_no_tier_caps_and_steps_are_independent_renders)
{
    // a unison patch on the low tier (4 unison voices) renders with every unison voice offline
    auto low = createEngine (EngineKind::vital);
    low->setParam (kParamMasterPolyphony, 32.0f);
    low->prepare (kSr, 256, qualityForTier (Tier::low));
    plain (*low);
    setByKey (*low, "osc1.unison.voices", 12.0f);
    setByKey (*low, "osc1.unison.detune", 60.0f);
    auto request = oneNote (48, 0.6);
    OfflineAudio offline;
    CHECK (low->renderOffline (request, offline));
    const auto liveLow = sbtest::render (*low, { sbtest::noteOn (0, 48, 0.9f) }, static_cast<int> (0.6 * kSr), 256);
    auto high = createEngine (EngineKind::vital);
    high->setParam (kParamMasterPolyphony, 32.0f);
    Quality q = qualityForTier (Tier::high);
    q.realtime = false;
    high->prepare (kSr, 256, q);
    plain (*high);
    setByKey (*high, "osc1.unison.voices", 12.0f);
    setByKey (*high, "osc1.unison.detune", 60.0f);
    const auto liveHigh = sbtest::render (*high, { sbtest::noteOn (0, 48, 0.9f) }, static_cast<int> (0.6 * kSr), 256);
    const auto rms = [] (const std::vector<float>& v) { return sbtest::rmsOf (v, 12000, 28000); };
    std::printf ("    rms low tier live %.4f, high tier live %.4f, offline from the low tier engine %.4f\n", rms (liveLow.left), rms (liveHigh.left), rms (offline.left));
    CHECK (std::fabs (rms (offline.left) - rms (liveHigh.left)) < 0.005 * rms (liveHigh.left));  // the offline render is the high tier ...
    CHECK (std::fabs (rms (liveLow.left) - rms (liveHigh.left)) > 0.01 * rms (liveHigh.left));    // ... the low tier it was started from is not

    // a step renders as it would alone: the engine is reset and the smoothers snap, so 256 warp steps in one instance are 256 renders
    auto e = makeVital();
    plain (*e);
    setByKey (*e, "osc1.warp.type", sbtest::choiceValue ("osc1.warp.type", "Sync"));
    const uint32_t warp = findParamByKey ("osc1.warp.amount")->id;
    OfflineRequest steps;
    for (float amount : { 10.0f, 80.0f, 35.0f })
    {
        OfflineStep s;
        s.seconds = 0.15;
        s.overrides.push_back ({ warp, amount });
        OfflineNote n;
        n.key = 52;
        n.lengthSeconds = 0.15;
        s.notes.push_back (n);
        steps.steps.push_back (s);
    }
    OfflineAudio together;
    CHECK (e->renderOffline (steps, together));
    CHECK_EQ (together.stepStart.size(), static_cast<size_t> (3));
    OfflineRequest alone;
    alone.steps.push_back (steps.steps[2]);
    OfflineAudio single;
    CHECK (e->renderOffline (alone, single));
    double worst = 0.0;
    for (size_t i = 0; i < single.left.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::fabs (together.left[together.stepStart[2] + i] - single.left[i])));
    std::printf("    third step in a sequence against alone: max difference %.2e\n", worst);
    CHECK (worst < 1.0e-4);
}

TEST (switch_to_wavetable_sample_to_table_is_audibly_the_source_region)
{
    // a 220 Hz tone with 1/h partials, 1.5 s; the region 0.2 .. 0.8 becomes the table
    const double f0 = 220.0;
    const auto tone = sawTone (f0, 1.5, kSr, 16);
    wtedit::ImportOptions options; // Pitch Average
    wtedit::Wavetable table;
    std::string error;
    CHECK (convert::sampleRegionToWavetable (tone.data(), tone.size(), kSr, 0.2f, 0.8f, options, "Saw tone", table, &error));
    std::printf ("    region of %zu samples -> %zu frames\n", static_cast<size_t> (0.6 * tone.size()), table.numFrames());
    CHECK (table.numFrames() >= 10);

    Content content;
    content.store.add ("test", "saw.wav", wavetableToWav (table));
    ContentWavetable w;
    w.osc = 1;
    w.ref.pack = "test";
    w.ref.file = "saw.wav";
    w.ref.name = "Saw tone";
    w.frames = static_cast<int> (table.numFrames());
    content.state.wavetables.push_back (w);
    auto e = makeVital();
    plain (*e);
    content.apply (*e);
    setByKey (*e, "osc1.type", 0.0f);
    setByKey (*e, "osc1.wavetable", static_cast<float> (kWavetablePresetTable));
    setByKey (*e, "osc1.frame", 0.0f);
    const auto played = play (*e, 57, 1.5); // A3 = 220 Hz
    const size_t from = static_cast<size_t> (0.5 * kSr), n = static_cast<size_t> (0.8 * kSr);
    CHECK_NEAR (crossingFrequency (played, 0.5, 1.3), f0, 1.0);

    const auto source = harmonicDb (tone, static_cast<size_t> (0.3 * kSr), n, f0, kSr, 12);
    const auto audible = harmonicDb (played.left, from, n, f0, kSr, 12);
    std::vector<double> a, b;
    for (size_t i = 0; i < source.size(); ++i)
    {
        a.push_back (source[i] - source[0]);
        b.push_back (audible[i] - audible[0]);
    }
    const double worst = worstHarmonicDifference (a, b, 40.0);
    std::printf ("    harmonics 1..12 of the source region against the played table: worst difference %.3f dB\n", worst);
    CHECK (worst < 0.5);   // the stated tolerance: every harmonic within 40 dB of the loudest agrees to 0.5 dB
    // the same through every menu entry: a table that sounds (finite, not silent)
    for (const auto& choice : convert::importChoices())
    {
        wtedit::ImportOptions o;
        o.mode = choice.mode;
        wtedit::Wavetable t;
        CHECK (convert::sampleRegionToWavetable (tone.data(), tone.size(), kSr, 0.2f, 0.8f, o, "m", t));
        CHECK (t.numFrames() >= 1);
    }
}

TEST (resample_captures_the_patch_as_a_wavetable_and_is_deterministic)
{
    auto e = makeVital();
    plain (*e);
    setByKey (*e, "filter1.on", 1.0f);
    setByKey (*e, "filter1.cutoff", 1800.0f);
    setByKey (*e, "filter1.resonance", 35.0f);
    convert::ResampleOptions options;
    options.key = 60;
    options.bpm = 240.0; // one bar = 1 s
    OfflineAudio audio, again;
    CHECK (convert::renderResample (runnerOf (*e), options, audio));
    CHECK (convert::renderResample (runnerOf (*e), options, again));
    CHECK (audio.left == again.left && audio.right == again.right);   // deterministic
    CHECK (audio.frames() >= static_cast<size_t> (kSr));              // one bar plus the release

    wtedit::Wavetable table, tableAgain;
    std::string error;
    wtedit::ImportOptions import;
    CHECK (convert::audioToWavetables (audio, import, "Resampled", table, nullptr, &error));
    CHECK (convert::audioToWavetables (again, import, "Resampled", tableAgain, nullptr, &error));
    CHECK_EQ (table.numFrames(), tableAgain.numFrames());
    bool same = true;
    for (size_t f = 0; f < table.numFrames(); ++f)
        same = same && std::memcmp (table.getFrame (f).data.data(), tableAgain.getFrame (f).data.data(), sizeof (float) * wtedit::kWaveformSize) == 0;
    CHECK (same);
    CHECK (table.numFrames() >= 20);

    // the table, played on a plain oscillator, has the harmonics of the resampled note
    Content content;
    content.store.add ("test", "res.wav", wavetableToWav (table));
    ContentWavetable w;
    w.osc = 1;
    w.ref.pack = "test";
    w.ref.file = "res.wav";
    w.ref.name = "Resampled";
    w.frames = static_cast<int> (table.numFrames());
    content.state.wavetables.push_back (w);
    auto second = makeVital();
    plain (*second);
    content.apply (*second);
    setByKey (*second, "osc1.wavetable", static_cast<float> (kWavetablePresetTable));
    setByKey (*second, "osc1.frame", 30.0f);
    const double f0 = 440.0 * std::pow (2.0, (60 - 69) / 12.0);
    const auto played = play (*second, 60, 1.2);
    const size_t n = static_cast<size_t> (0.5 * kSr);
    // the middle of the table is the steady part of the capture (frame 30 % of 0.9 s ~ 0.3 s into the bar)
    const auto original = harmonicDb (audio.left, static_cast<size_t> (0.25 * kSr), n, f0, kSr, 10);
    const auto resampled = harmonicDb (played.left, static_cast<size_t> (0.5 * kSr), n, f0, kSr, 10);
    std::vector<double> a, b;
    for (size_t i = 0; i < original.size(); ++i)
    {
        a.push_back (original[i] - original[0]);
        b.push_back (resampled[i] - resampled[0]);
    }
    const double worst = worstHarmonicDifference (a, b, 30.0);
    std::printf ("    resampled filtered saw, harmonics 1..10: worst difference %.3f dB (frames %zu)\n", worst, table.numFrames());
    CHECK (worst < 1.0);

    // ultra quality is honoured by the resample as well
    options.ultra = true;
    OfflineAudio ultra;
    CHECK (convert::renderResample (runnerOf (*e), options, ultra));
    CHECK (ultra.left != audio.left);
    // A + B: the left channel goes to the first table, the right one to the second
    wtedit::Wavetable ta, tb;
    CHECK (convert::audioToWavetables (audio, import, "AB", ta, &tb, &error));
    CHECK (ta.numFrames() >= 1 && tb.numFrames() >= 1);
}

TEST (render_osc_warp_bakes_256_frames_that_sound_like_the_warped_oscillator)
{
    auto e = makeVital();
    plain (*e);
    setByKey (*e, "osc1.level", 50.0f);
    setByKey (*e, "osc1.warp.type", sbtest::choiceValue ("osc1.warp.type", "Sync"));
    setByKey (*e, "osc1.warp.amount", 40.0f);
    const ParamValues values = valuesOf (*e);

    convert::WarpRenderOptions options;
    options.osc = 1;
    wtedit::Wavetable table, again;
    std::string error;
    float levelScale = 0.0f;
    CHECK (convert::renderOscWarp (runnerOf (*e), &values, options, "Sync sweep", table, &error, &levelScale));
    CHECK (error.empty());
    std::printf ("    level scale of the baked table %.4f\n", levelScale);
    CHECK_EQ (table.numFrames(), static_cast<size_t> (256));
    CHECK (convert::renderOscWarp (runnerOf (*e), &values, options, "Sync sweep", again, &error));
    bool same = true;
    for (size_t f = 0; f < 256; ++f)
        same = same && std::memcmp (table.getFrame (f).data.data(), again.getFrame (f).data.data(), sizeof (float) * wtedit::kWaveformSize) == 0;
    CHECK (same);   // deterministic

    // WARP 0 is the plain saw (a rising ramp), WARP 100 % is another shape
    const auto& first = table.getFrame (0);
    const auto& last = table.getFrame (255);
    double d = 0.0;
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        d = std::max (d, static_cast<double> (std::fabs (first[i] - last[i])));
    CHECK (d > 0.5);
    // the baked table with WT POS = the warp amount sounds like the warped oscillator (same key, no warp, no remap)
    Content content;
    content.store.add ("test", "warp.wav", wavetableToWav (table));
    ContentWavetable w;
    w.osc = 1;
    w.ref.pack = "test";
    w.ref.file = "warp.wav";
    w.ref.name = "Sync sweep";
    w.frames = 256;
    content.state.wavetables.push_back (w);
    for (const int key : { 45, 57, 69 })
    {
        auto original = makeVital();
        plain (*original);
        setByKey (*original, "osc1.level", 50.0f);
        setByKey (*original, "osc1.warp.type", sbtest::choiceValue ("osc1.warp.type", "Sync"));
        setByKey (*original, "osc1.warp.amount", 40.0f);
        const double f0 = 440.0 * std::pow (2.0, (key - 69) / 12.0);
        const auto reference = play (*original, key, 1.0);
        auto bakedEngine = makeVital();
        plain (*bakedEngine);
        setByKey (*bakedEngine, "osc1.level", 50.0f);
        content.apply (*bakedEngine);
        setByKey (*bakedEngine, "osc1.wavetable", static_cast<float> (kWavetablePresetTable));
        for (const auto& change : convert::warpBakeParameterChanges (1, 40.0f, levelScale, 50.0f))
            setByKey (*bakedEngine, change.first.c_str(), change.second);
        const auto played = play (*bakedEngine, key, 1.0);
        const size_t from = static_cast<size_t> (0.4 * kSr), n = static_cast<size_t> (0.5 * kSr);
        const int harmonics = std::min (24, static_cast<int> (9000.0 / f0));
        const auto r = harmonicDb (reference.left, from, n, f0, kSr, harmonics);
        const auto p = harmonicDb (played.left, from, n, f0, kSr, harmonics);
        const double worst = worstHarmonicDifference (r, p, 30.0);
        std::printf ("    key %d: %d harmonics of the warped oscillator against the baked table: worst difference %.3f dB, level difference %.3f dB\n", key, harmonics, worst,
                     20.0 * std::log10 (sbtest::rmsOf (played.left, from, from + n) / sbtest::rmsOf (reference.left, from, from + n)));
        CHECK (worst < 0.1);   // the stated tolerance of the bake: 0.1 dB on every harmonic within 30 dB of the loudest
    }

    // only a wavetable oscillator is rendered
    ParamValues sampleOsc = values;
    sampleOsc.plain[findParamIndex (findParamByKey ("osc1.type")->id)] = static_cast<float> (kOscTypeSample);
    CHECK (! convert::renderOscWarp (runnerOf (*e), &sampleOsc, options, "x", table, &error));
    CHECK (! error.empty());
    CHECK (! convert::renderOscWarp (runnerOf (*e), nullptr, options, "x", table, &error));
}

TEST (render_osc_warp_without_a_warp_extracts_the_exact_cycle_of_the_wavetable)
{
    // the extraction (period measurement, phase, cubic read) is exact: with no warp every frame is the saw of the oscillator, harmonic h at 1/h
    auto e = makeVital();
    plain (*e);
    const ParamValues values = valuesOf (*e);
    convert::WarpRenderOptions options;
    options.frames = 16;
    options.normalise = false;
    wtedit::Wavetable table;
    std::string error;
    CHECK (convert::renderOscWarp (runnerOf (*e), &values, options, "Plain", table, &error));
    CHECK_EQ (table.numFrames(), static_cast<size_t> (16));
    double spread = 0.0;
    for (size_t f = 1; f < 16; ++f)
        for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
            spread = std::max (spread, static_cast<double> (std::fabs (table.getFrame (f)[i] - table.getFrame (0)[i])));
    std::printf ("    16 frames without a warp: largest difference between frames %.2e\n", spread);
    CHECK (spread < 1.0e-3);
    std::vector<float> twice (table.getFrame (0).data.begin(), table.getFrame (0).data.end());
    twice.insert (twice.end(), table.getFrame (0).data.begin(), table.getFrame (0).data.end());
    const double h1 = amplitudeAt (twice, 0, 4096, 48000.0 / 2048.0, 48000.0);
    for (int h = 2; h <= 6; ++h)
    {
        const double ratio = amplitudeAt (twice, 0, 4096, h * 48000.0 / 2048.0, 48000.0) / h1;
        std::printf ("    h%d/h1 %.4f (saw %.4f)\n", h, ratio, 1.0 / h);
        CHECK_NEAR (ratio, 1.0 / h, 0.02);
    }
}

TEST (the_last_note_renders_to_a_wav_with_its_release_tail)
{
    auto e = makeVital();
    plain (*e);
    setByKey (*e, "env1.release", 600.0f);
    convert::NoteRender note;
    note.key = 64;
    note.heldSeconds = 0.5;
    note.tailSeconds = 3.0;
    OfflineAudio audio;
    CHECK (convert::renderNote (runnerOf (*e), note, audio));
    CHECK (audio.frames() > static_cast<size_t> (0.5 * kSr));
    CHECK (audio.frames() < static_cast<size_t> (2.5 * kSr));   // the tail ends when the release is silent, it is not padded to the cap
    CHECK (sbtest::peak (audio.left) > 0.05f);
    const auto wav = convert::audioToWav (audio, 24);
    SampleFacts facts;
    CHECK (probeSampleWav (wav, "last note.wav", facts));
    CHECK_EQ (static_cast<size_t> (facts.frames), audio.frames());
    CHECK_EQ (facts.sampleRate, 48000);
    // played back through the content pipeline it is the same note: pitch of E3
    auto preview = SamplePreview::create (wav, "last note.wav");
    CHECK (preview != nullptr);
    CHECK_NEAR (crossings (preview->mono(), 4800, 20000, 48000.0), 440.0 * std::pow (2.0, (64 - 69) / 12.0), 2.0);
}
