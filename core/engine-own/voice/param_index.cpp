// SynthyBoy - own engine: key resolution.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "param_index.h"

#include <cstdarg>
#include <cstdio>

namespace synthyboy
{
namespace own
{
namespace
{
int find (const char* fmt, ...)
{
    char key[96];
    va_list ap;
    va_start (ap, fmt);
    std::vsnprintf (key, sizeof key, fmt, ap);
    va_end (ap);
    return findParamIndex (hashKey (key));
}

ParamIndex build()
{
    ParamIndex p {};
    p.count = static_cast<int> (paramCount());
    for (int n = 1; n <= 3; ++n)
    {
        OscIdx& o = p.osc[n - 1];
#define K(field, key) o.field = find ("osc%d." key, n)
        K (on, "on"); K (level, "level"); K (pan, "pan"); K (transpose, "transpose"); K (tune, "tune"); K (wavetable, "wavetable");
        K (frame, "frame"); K (frameSpread, "frame_spread"); K (phase, "phase"); K (randomPhase, "random_phase"); K (destination, "destination");
        K (env1, "env1"); K (pitchCoarse, "pitch_coarse"); K (pitchTrack, "pitch_track"); K (bendTrack, "pitch_bend_track");
        K (uniVoices, "unison.voices"); K (uniDetune, "unison.detune"); K (uniCurve, "unison.detune_curve"); K (uniSpread, "unison.spread");
        K (uniBlend, "unison.blend"); K (uniMode, "unison.mode"); K (uniRange, "unison.range"); K (uniLaw, "unison.law"); K (uniBlendShape, "unison.blend_shape"); K (uniStack, "unison.stack"); K (uniStackRepeat, "unison.stack_repeat");
        K (warpType, "warp.type"); K (warpAmount, "warp.amount"); K (warpPhase, "warp.phase"); K (warpSpread, "warp.spread");
        K (warp2Type, "warp2.type"); K (warp2Amount, "warp2.amount"); K (warp2Phase, "warp2.phase"); K (warp2Spread, "warp2.spread");
        K (keyLow, "map.key_low"); K (keyHigh, "map.key_high"); K (velLow, "map.vel_low"); K (velHigh, "map.vel_high"); K (type, "type");
#undef K
    }
    p.sub = { find ("sub.on"), find ("sub.waveform"), find ("sub.octave"), find ("sub.tune"), find ("sub.level"), find ("sub.pan"),
              find ("sub.destination"), find ("sub.env1"), find ("sub.phase") };
    p.noise = { find ("noise.on"), find ("noise.level"), find ("noise.pan"), find ("noise.destination"), find ("noise.env1"), find ("noise.sample"),
                find ("noise.stereo"), find ("noise.keytrack"), find ("noise.pitch"), find ("noise.fine") };
    for (int n = 1; n <= 2; ++n)
    {
        FilterIdx& f = p.filter[n - 1];
#define K(field, key) f.field = find ("filter%d." key, n)
        K (on, "on"); K (model, "model"); K (style, "style"); K (cutoff, "cutoff"); K (resonance, "resonance"); K (drive, "drive"); K (blend, "blend");
        K (mix, "mix"); K (keytrack, "keytrack"); K (var, "var"); K (pan, "pan"); K (level, "level"); K (env1, "env1"); K (destination, "destination");
        K (combOffset, "comb_offset"); K (formantX, "formant.x"); K (formantY, "formant.y"); K (formantRes, "formant.resonance");
        K (formantSpread, "formant.spread"); K (formantTranspose, "formant.transpose"); K (cleanMode, "clean_mode"); K (bus1Send, "bus1_send"); K (bus2Send, "bus2_send");
#undef K
    }
    for (int n = 1; n <= 6; ++n)
    {
        EnvIdx& e = p.env[n - 1];
#define K(field, key) e.field = find ("env%d." key, n)
        K (delay, "delay"); K (attack, "attack"); K (hold, "hold"); K (decay, "decay"); K (sustain, "sustain"); K (release, "release");
        K (attackCurve, "attack_curve"); K (decayCurve, "decay_curve"); K (releaseCurve, "release_curve");
        K (units, "units"); K (legatoInverted, "legato_inverted"); K (delayBeats, "delay_beats"); K (attackBeats, "attack_beats"); K (holdBeats, "hold_beats");
        K (decayBeats, "decay_beats"); K (releaseBeats, "release_beats");
#undef K
    }
    for (int n = 1; n <= 10; ++n)
    {
        LfoIdx& l = p.lfo[n - 1];
#define K(field, key) l.field = find ("lfo%d." key, n)
        K (shape, "shape"); K (rate, "rate"); K (sync, "sync"); K (division, "division"); K (syncType, "sync_type"); K (phase, "phase");
        K (fadeIn, "fade_in"); K (delay, "delay"); K (smooth, "smooth"); K (smoothTime, "smooth_time"); K (stereo, "stereo");
        K (keyTranspose, "keytrack_transpose"); K (keyTune, "keytrack_tune"); K (mono, "mono"); K (direction, "direction"); K (host, "host"); K (swing, "swing");
#undef K
    }
    for (int n = 1; n <= 4; ++n)
    {
        RlfoIdx& l = p.rlfo[n - 1];
#define K(field, key) l.field = find ("rlfo%d." key, n)
        K (style, "style"); K (rate, "rate"); K (sync, "sync"); K (division, "division"); K (syncType, "sync_type"); K (stereo, "stereo");
        K (keyTranspose, "keytrack_transpose"); K (keyTune, "keytrack_tune");
#undef K
    }
    for (int n = 1; n <= 8; ++n)
        p.macro[n - 1] = find ("macro.%d", n);
    p.filterRouting = find ("filter.routing");
    p.master = { find ("master.volume"), find ("master.polyphony"), find ("master.bend_range"), find ("master.velocity_track"), find ("master.transpose"),
                 find ("master.tune"), find ("master.legato"), find ("master.portamento.time"), find ("master.portamento.slope"),
                 find ("master.portamento.always"), find ("master.portamento.scaled"), find ("master.bend_down_range"), find ("master.bend_down_link"),
                 find ("master.stereo_spread"), find ("master.voice.same_note"), find ("master.voice.steal_priority"), find ("master.voice_override"),
                 find ("master.poly_legato"),
                 find ("master.scale.key"), find ("master.scale.type"), find ("master.scale.mode"), find ("master.tuning.a4"), find ("master.tuning.scale"),
                 find ("master.tuning.mts_esp"), find ("master.mpe.on"), find ("master.mpe.bend_range"), find ("master.mpe.xyz_macro"),
                 find ("master.mpe.y_modwheel"), find ("master.mpe.bend_to_x"), find ("master.mpe.y_bipolar"), find ("master.stereo_routing"),
                 find ("curve.velocity.legato"), find ("curve.note.legato") };
    {
        VoiceCtlIdx& v = p.vctl;
        v.length = find ("voicectl.length");
        static const char* const kStepKeys[6] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
        for (int n = 0; n < 8; ++n)
            for (int k = 0; k < 6; ++k)
                v.step[n][k] = find ("voicectl.s%d.%s", n + 1, kStepKeys[k]);
        v.randPan = find ("voicectl.rand.pan"); v.randCutoff = find ("voicectl.rand.cutoff"); v.randEnv = find ("voicectl.rand.env");
        v.randDetune = find ("voicectl.rand.detune"); v.rand10x = find ("voicectl.rand.detune10x");
        v.scaleEnv = find ("voicectl.scale.env"); v.scaleLfo = find ("voicectl.scale.lfo"); v.scaleLfoMode = find ("voicectl.scale.lfo_mode");
        static const char* const kOscKeys[5] = { "a", "b", "c", "noise", "sub" };
        for (int k = 0; k < 5; ++k)
            v.osc[k] = find ("voicectl.osc.%s", kOscKeys[k]);
    }
    for (int s = 0; s < kNumModSlots; ++s)
        p.slot[s] = modSlotParams (s);
    return p;
}
} // namespace

const ParamIndex& paramIndex() noexcept
{
    static const ParamIndex index = build();
    return index;
}
} // namespace own
} // namespace synthyboy
