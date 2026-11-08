// SynthyBoy - core: Kick remote-control page table and macro defaults (docs/ui-design/09-kick-integration.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/kick_pages.h"

namespace synthyboy
{
namespace
{
constexpr const char* s_more_env1[] = { "env1.release_curve" };
constexpr const char* s_more_env2[] = { "env2.release_curve" };
constexpr const char* s_more_env3[] = { "env3.release_curve" };
constexpr const char* s_more_env4[] = { "env4.release_curve" };
constexpr const char* s_more_env5[] = { "env5.release_curve" };
constexpr const char* s_more_env6[] = { "env6.release_curve" };

constexpr const char* s_more_lfo1[] = {
    "lfo1.smooth", "lfo1.smooth_time", "lfo1.stereo", "lfo1.keytrack_transpose", "lfo1.keytrack_tune"
};
constexpr const char* s_more_lfo2[] = {
    "lfo2.smooth", "lfo2.smooth_time", "lfo2.stereo", "lfo2.keytrack_transpose", "lfo2.keytrack_tune"
};
constexpr const char* s_more_lfo3[] = {
    "lfo3.smooth", "lfo3.smooth_time", "lfo3.stereo", "lfo3.keytrack_transpose", "lfo3.keytrack_tune"
};
constexpr const char* s_more_lfo4[] = {
    "lfo4.smooth", "lfo4.smooth_time", "lfo4.stereo", "lfo4.keytrack_transpose", "lfo4.keytrack_tune"
};
constexpr const char* s_more_lfo5[] = {
    "lfo5.smooth", "lfo5.smooth_time", "lfo5.stereo", "lfo5.keytrack_transpose", "lfo5.keytrack_tune"
};
constexpr const char* s_more_lfo6[] = {
    "lfo6.smooth", "lfo6.smooth_time", "lfo6.stereo", "lfo6.keytrack_transpose", "lfo6.keytrack_tune"
};
constexpr const char* s_more_lfo7[] = {
    "lfo7.smooth", "lfo7.smooth_time", "lfo7.stereo", "lfo7.keytrack_transpose", "lfo7.keytrack_tune"
};
constexpr const char* s_more_lfo8[] = {
    "lfo8.smooth", "lfo8.smooth_time", "lfo8.stereo", "lfo8.keytrack_transpose", "lfo8.keytrack_tune"
};

constexpr const char* s_more_mod_matrix[] = {
    "mod05.source", "mod05.destination", "mod05.power", "mod05.bipolar", "mod05.stereo", "mod05.bypass",
    "mod06.source", "mod06.destination", "mod06.power", "mod06.bipolar", "mod06.stereo", "mod06.bypass",
    "mod07.source", "mod07.destination", "mod07.power", "mod07.bipolar", "mod07.stereo", "mod07.bypass",
    "mod08.source", "mod08.destination", "mod08.power", "mod08.bipolar", "mod08.stereo", "mod08.bypass",
    "mod09.source", "mod09.destination", "mod09.power", "mod09.bipolar", "mod09.stereo", "mod09.bypass",
    "mod10.source", "mod10.destination", "mod10.power", "mod10.bipolar", "mod10.stereo", "mod10.bypass",
    "mod11.source", "mod11.destination", "mod11.power", "mod11.bipolar", "mod11.stereo", "mod11.bypass",
    "mod12.source", "mod12.destination", "mod12.power", "mod12.bipolar", "mod12.stereo", "mod12.bypass",
    "mod13.source", "mod13.destination", "mod13.power", "mod13.bipolar", "mod13.stereo", "mod13.bypass",
    "mod14.source", "mod14.destination", "mod14.power", "mod14.bipolar", "mod14.stereo", "mod14.bypass",
    "mod15.source", "mod15.destination", "mod15.power", "mod15.bipolar", "mod15.stereo", "mod15.bypass",
    "mod16.source", "mod16.destination", "mod16.power", "mod16.bipolar", "mod16.stereo", "mod16.bypass",
    "mod17.source", "mod17.destination", "mod17.amount", "mod17.power", "mod17.bipolar", "mod17.stereo",
    "mod17.bypass", "mod18.source", "mod18.destination", "mod18.amount", "mod18.power", "mod18.bipolar",
    "mod18.stereo", "mod18.bypass", "mod19.source", "mod19.destination", "mod19.amount", "mod19.power",
    "mod19.bipolar", "mod19.stereo", "mod19.bypass", "mod20.source", "mod20.destination", "mod20.amount",
    "mod20.power", "mod20.bipolar", "mod20.stereo", "mod20.bypass", "mod21.source", "mod21.destination",
    "mod21.amount", "mod21.power", "mod21.bipolar", "mod21.stereo", "mod21.bypass", "mod22.source",
    "mod22.destination", "mod22.amount", "mod22.power", "mod22.bipolar", "mod22.stereo", "mod22.bypass",
    "mod23.source", "mod23.destination", "mod23.amount", "mod23.power", "mod23.bipolar", "mod23.stereo",
    "mod23.bypass", "mod24.source", "mod24.destination", "mod24.amount", "mod24.power", "mod24.bipolar",
    "mod24.stereo", "mod24.bypass", "mod25.source", "mod25.destination", "mod25.amount", "mod25.power",
    "mod25.bipolar", "mod25.stereo", "mod25.bypass", "mod26.source", "mod26.destination", "mod26.amount",
    "mod26.power", "mod26.bipolar", "mod26.stereo", "mod26.bypass", "mod27.source", "mod27.destination",
    "mod27.amount", "mod27.power", "mod27.bipolar", "mod27.stereo", "mod27.bypass", "mod28.source",
    "mod28.destination", "mod28.amount", "mod28.power", "mod28.bipolar", "mod28.stereo", "mod28.bypass",
    "mod29.source", "mod29.destination", "mod29.amount", "mod29.power", "mod29.bipolar", "mod29.stereo",
    "mod29.bypass", "mod30.source", "mod30.destination", "mod30.amount", "mod30.power", "mod30.bipolar",
    "mod30.stereo", "mod30.bypass", "mod31.source", "mod31.destination", "mod31.amount", "mod31.power",
    "mod31.bipolar", "mod31.stereo", "mod31.bypass", "mod32.source", "mod32.destination", "mod32.amount",
    "mod32.power", "mod32.bipolar", "mod32.stereo", "mod32.bypass", "mod33.source", "mod33.destination",
    "mod33.amount", "mod33.power", "mod33.bipolar", "mod33.stereo", "mod33.bypass", "mod34.source",
    "mod34.destination", "mod34.amount", "mod34.power", "mod34.bipolar", "mod34.stereo", "mod34.bypass",
    "mod35.source", "mod35.destination", "mod35.amount", "mod35.power", "mod35.bipolar", "mod35.stereo",
    "mod35.bypass", "mod36.source", "mod36.destination", "mod36.amount", "mod36.power", "mod36.bipolar",
    "mod36.stereo", "mod36.bypass", "mod37.source", "mod37.destination", "mod37.amount", "mod37.power",
    "mod37.bipolar", "mod37.stereo", "mod37.bypass", "mod38.source", "mod38.destination", "mod38.amount",
    "mod38.power", "mod38.bipolar", "mod38.stereo", "mod38.bypass", "mod39.source", "mod39.destination",
    "mod39.amount", "mod39.power", "mod39.bipolar", "mod39.stereo", "mod39.bypass", "mod40.source",
    "mod40.destination", "mod40.amount", "mod40.power", "mod40.bipolar", "mod40.stereo", "mod40.bypass",
    "mod41.source", "mod41.destination", "mod41.amount", "mod41.power", "mod41.bipolar", "mod41.stereo",
    "mod41.bypass", "mod42.source", "mod42.destination", "mod42.amount", "mod42.power", "mod42.bipolar",
    "mod42.stereo", "mod42.bypass", "mod43.source", "mod43.destination", "mod43.amount", "mod43.power",
    "mod43.bipolar", "mod43.stereo", "mod43.bypass", "mod44.source", "mod44.destination", "mod44.amount",
    "mod44.power", "mod44.bipolar", "mod44.stereo", "mod44.bypass", "mod45.source", "mod45.destination",
    "mod45.amount", "mod45.power", "mod45.bipolar", "mod45.stereo", "mod45.bypass", "mod46.source",
    "mod46.destination", "mod46.amount", "mod46.power", "mod46.bipolar", "mod46.stereo", "mod46.bypass",
    "mod47.source", "mod47.destination", "mod47.amount", "mod47.power", "mod47.bipolar", "mod47.stereo",
    "mod47.bypass", "mod48.source", "mod48.destination", "mod48.amount", "mod48.power", "mod48.bipolar",
    "mod48.stereo", "mod48.bypass", "mod49.source", "mod49.destination", "mod49.amount", "mod49.power",
    "mod49.bipolar", "mod49.stereo", "mod49.bypass", "mod50.source", "mod50.destination", "mod50.amount",
    "mod50.power", "mod50.bipolar", "mod50.stereo", "mod50.bypass", "mod51.source", "mod51.destination",
    "mod51.amount", "mod51.power", "mod51.bipolar", "mod51.stereo", "mod51.bypass", "mod52.source",
    "mod52.destination", "mod52.amount", "mod52.power", "mod52.bipolar", "mod52.stereo", "mod52.bypass",
    "mod53.source", "mod53.destination", "mod53.amount", "mod53.power", "mod53.bipolar", "mod53.stereo",
    "mod53.bypass", "mod54.source", "mod54.destination", "mod54.amount", "mod54.power", "mod54.bipolar",
    "mod54.stereo", "mod54.bypass", "mod55.source", "mod55.destination", "mod55.amount", "mod55.power",
    "mod55.bipolar", "mod55.stereo", "mod55.bypass", "mod56.source", "mod56.destination", "mod56.amount",
    "mod56.power", "mod56.bipolar", "mod56.stereo", "mod56.bypass", "mod57.source", "mod57.destination",
    "mod57.amount", "mod57.power", "mod57.bipolar", "mod57.stereo", "mod57.bypass", "mod58.source",
    "mod58.destination", "mod58.amount", "mod58.power", "mod58.bipolar", "mod58.stereo", "mod58.bypass",
    "mod59.source", "mod59.destination", "mod59.amount", "mod59.power", "mod59.bipolar", "mod59.stereo",
    "mod59.bypass", "mod60.source", "mod60.destination", "mod60.amount", "mod60.power", "mod60.bipolar",
    "mod60.stereo", "mod60.bypass", "mod61.source", "mod61.destination", "mod61.amount", "mod61.power",
    "mod61.bipolar", "mod61.stereo", "mod61.bypass", "mod62.source", "mod62.destination", "mod62.amount",
    "mod62.power", "mod62.bipolar", "mod62.stereo", "mod62.bypass", "mod63.source", "mod63.destination",
    "mod63.amount", "mod63.power", "mod63.bipolar", "mod63.stereo", "mod63.bypass", "mod64.source",
    "mod64.destination", "mod64.amount", "mod64.power", "mod64.bipolar", "mod64.stereo", "mod64.bypass"
};

constexpr Page s_kickPages[kKickPageCount] = {
    // Page 1: Macros & Performance (Perf)
    { "macros", "Macros & Performance", "Perf", { "macro.1", "macro.2", "macro.3", "macro.4", "master.volume", "filter1.cutoff", "filter1.resonance", "env1.release" }, 8, nullptr, 0 },
    // Page 2: Master & Voicing (Master)
    { "master_voicing", "Master & Voicing", "Master", { "master.volume", "master.transpose", "master.tune", "master.polyphony", "master.legato", "master.bend_range", "master.velocity_track", "master.portamento.time" }, 8, nullptr, 0 },
    // Page 3: Portamento & Glide (Master)
    { "master_porta", "Portamento & Glide", "Master", { "master.portamento.time", "master.portamento.slope", "master.portamento.always", "master.portamento.scaled", "master.legato", "master.polyphony", "master.bend_range", "master.volume" }, 8, nullptr, 0 },
    // Page 4: Osc 1 Main (Osc)
    { "osc1_main", "Osc 1 Main", "Osc", { "osc1.on", "osc1.wavetable", "osc1.frame", "osc1.level", "osc1.pan", "osc1.transpose", "osc1.tune", "osc1.destination" }, 8, nullptr, 0 },
    // Page 5: Osc 1 Unison & Phase (Osc)
    { "osc1_unison", "Osc 1 Unison & Phase", "Osc", { "osc1.unison.voices", "osc1.unison.detune", "osc1.unison.spread", "osc1.unison.blend", "osc1.unison.stack", "osc1.unison.detune_curve", "osc1.phase", "osc1.random_phase" }, 8, nullptr, 0 },
    // Page 6: Osc 1 Morph & Warp (Osc)
    { "osc1_shaping", "Osc 1 Morph & Warp", "Osc", { "osc1.morph.type", "osc1.morph.amount", "osc1.morph.spread", "osc1.warp.type", "osc1.warp.amount", "osc1.warp.phase", "osc1.warp.spread", "osc1.frame_spread" }, 8, nullptr, 0 },
    // Page 7: Osc 2 Main (Osc)
    { "osc2_main", "Osc 2 Main", "Osc", { "osc2.on", "osc2.wavetable", "osc2.frame", "osc2.level", "osc2.pan", "osc2.transpose", "osc2.tune", "osc2.destination" }, 8, nullptr, 0 },
    // Page 8: Osc 2 Unison & Phase (Osc)
    { "osc2_unison", "Osc 2 Unison & Phase", "Osc", { "osc2.unison.voices", "osc2.unison.detune", "osc2.unison.spread", "osc2.unison.blend", "osc2.unison.stack", "osc2.unison.detune_curve", "osc2.phase", "osc2.random_phase" }, 8, nullptr, 0 },
    // Page 9: Osc 2 Morph & Warp (Osc)
    { "osc2_shaping", "Osc 2 Morph & Warp", "Osc", { "osc2.morph.type", "osc2.morph.amount", "osc2.morph.spread", "osc2.warp.type", "osc2.warp.amount", "osc2.warp.phase", "osc2.warp.spread", "osc2.frame_spread" }, 8, nullptr, 0 },
    // Page 10: Osc 3 Main (Osc)
    { "osc3_main", "Osc 3 Main", "Osc", { "osc3.on", "osc3.wavetable", "osc3.frame", "osc3.level", "osc3.pan", "osc3.transpose", "osc3.tune", "osc3.destination" }, 8, nullptr, 0 },
    // Page 11: Osc 3 Unison & Phase (Osc)
    { "osc3_unison", "Osc 3 Unison & Phase", "Osc", { "osc3.unison.voices", "osc3.unison.detune", "osc3.unison.spread", "osc3.unison.blend", "osc3.unison.stack", "osc3.unison.detune_curve", "osc3.phase", "osc3.random_phase" }, 8, nullptr, 0 },
    // Page 12: Osc 3 Morph & Warp (Osc)
    { "osc3_shaping", "Osc 3 Morph & Warp", "Osc", { "osc3.morph.type", "osc3.morph.amount", "osc3.morph.spread", "osc3.warp.type", "osc3.warp.amount", "osc3.warp.phase", "osc3.warp.spread", "osc3.frame_spread" }, 8, nullptr, 0 },
    // Page 13: Noise & Osc Mix (Osc)
    { "noise", "Noise & Osc Mix", "Osc", { "noise.on", "noise.level", "noise.pan", "noise.destination", "filter.routing", "osc1.level", "osc2.level", "osc3.level" }, 8, nullptr, 0 },
    // Page 14: Filter 1 Main (Filter)
    { "filter1_main", "Filter 1 Main", "Filter", { "filter1.on", "filter1.model", "filter1.style", "filter1.cutoff", "filter1.resonance", "filter1.drive", "filter1.blend", "filter1.mix" }, 8, nullptr, 0 },
    // Page 15: Filter 1 Key & Formant (Filter)
    { "filter1_model", "Filter 1 Key & Formant", "Filter", { "filter1.keytrack", "filter1.comb_offset", "filter1.formant.x", "filter1.formant.y", "filter1.formant.resonance", "filter1.formant.spread", "filter1.formant.transpose", "filter.routing" }, 8, nullptr, 0 },
    // Page 16: Filter 2 Main (Filter)
    { "filter2_main", "Filter 2 Main", "Filter", { "filter2.on", "filter2.model", "filter2.style", "filter2.cutoff", "filter2.resonance", "filter2.drive", "filter2.blend", "filter2.mix" }, 8, nullptr, 0 },
    // Page 17: Filter 2 Key & Formant (Filter)
    { "filter2_model", "Filter 2 Key & Formant", "Filter", { "filter2.keytrack", "filter2.comb_offset", "filter2.formant.x", "filter2.formant.y", "filter2.formant.resonance", "filter2.formant.spread", "filter2.formant.transpose", "filter.routing" }, 8, nullptr, 0 },
    // Page 18: Envelope 1 (Amp) (Env)
    { "env1", "Envelope 1 (Amp)", "Env", { "env1.attack", "env1.hold", "env1.decay", "env1.sustain", "env1.release", "env1.delay", "env1.attack_curve", "env1.decay_curve" }, 8, s_more_env1, 1 },
    // Page 19: Envelope 2 (Env)
    { "env2", "Envelope 2", "Env", { "env2.attack", "env2.hold", "env2.decay", "env2.sustain", "env2.release", "env2.delay", "env2.attack_curve", "env2.decay_curve" }, 8, s_more_env2, 1 },
    // Page 20: Envelope 3 (Env)
    { "env3", "Envelope 3", "Env", { "env3.attack", "env3.hold", "env3.decay", "env3.sustain", "env3.release", "env3.delay", "env3.attack_curve", "env3.decay_curve" }, 8, s_more_env3, 1 },
    // Page 21: Envelope 4 (Env)
    { "env4", "Envelope 4", "Env", { "env4.attack", "env4.hold", "env4.decay", "env4.sustain", "env4.release", "env4.delay", "env4.attack_curve", "env4.decay_curve" }, 8, s_more_env4, 1 },
    // Page 22: Envelope 5 (Env)
    { "env5", "Envelope 5", "Env", { "env5.attack", "env5.hold", "env5.decay", "env5.sustain", "env5.release", "env5.delay", "env5.attack_curve", "env5.decay_curve" }, 8, s_more_env5, 1 },
    // Page 23: Envelope 6 (Env)
    { "env6", "Envelope 6", "Env", { "env6.attack", "env6.hold", "env6.decay", "env6.sustain", "env6.release", "env6.delay", "env6.attack_curve", "env6.decay_curve" }, 8, s_more_env6, 1 },
    // Page 24: LFO 1 (LFO)
    { "lfo1", "LFO 1", "LFO", { "lfo1.shape", "lfo1.sync", "lfo1.rate", "lfo1.division", "lfo1.sync_type", "lfo1.phase", "lfo1.fade_in", "lfo1.delay" }, 8, s_more_lfo1, 5 },
    // Page 25: LFO 2 (LFO)
    { "lfo2", "LFO 2", "LFO", { "lfo2.shape", "lfo2.sync", "lfo2.rate", "lfo2.division", "lfo2.sync_type", "lfo2.phase", "lfo2.fade_in", "lfo2.delay" }, 8, s_more_lfo2, 5 },
    // Page 26: LFO 3 (LFO)
    { "lfo3", "LFO 3", "LFO", { "lfo3.shape", "lfo3.sync", "lfo3.rate", "lfo3.division", "lfo3.sync_type", "lfo3.phase", "lfo3.fade_in", "lfo3.delay" }, 8, s_more_lfo3, 5 },
    // Page 27: LFO 4 (LFO)
    { "lfo4", "LFO 4", "LFO", { "lfo4.shape", "lfo4.sync", "lfo4.rate", "lfo4.division", "lfo4.sync_type", "lfo4.phase", "lfo4.fade_in", "lfo4.delay" }, 8, s_more_lfo4, 5 },
    // Page 28: LFO 5 (LFO)
    { "lfo5", "LFO 5", "LFO", { "lfo5.shape", "lfo5.sync", "lfo5.rate", "lfo5.division", "lfo5.sync_type", "lfo5.phase", "lfo5.fade_in", "lfo5.delay" }, 8, s_more_lfo5, 5 },
    // Page 29: LFO 6 (LFO)
    { "lfo6", "LFO 6", "LFO", { "lfo6.shape", "lfo6.sync", "lfo6.rate", "lfo6.division", "lfo6.sync_type", "lfo6.phase", "lfo6.fade_in", "lfo6.delay" }, 8, s_more_lfo6, 5 },
    // Page 30: LFO 7 (LFO)
    { "lfo7", "LFO 7", "LFO", { "lfo7.shape", "lfo7.sync", "lfo7.rate", "lfo7.division", "lfo7.sync_type", "lfo7.phase", "lfo7.fade_in", "lfo7.delay" }, 8, s_more_lfo7, 5 },
    // Page 31: LFO 8 (LFO)
    { "lfo8", "LFO 8", "LFO", { "lfo8.shape", "lfo8.sync", "lfo8.rate", "lfo8.division", "lfo8.sync_type", "lfo8.phase", "lfo8.fade_in", "lfo8.delay" }, 8, s_more_lfo8, 5 },
    // Page 32: Random LFO 1 (LFO)
    { "rlfo1", "Random LFO 1", "LFO", { "rlfo1.style", "rlfo1.sync", "rlfo1.rate", "rlfo1.division", "rlfo1.sync_type", "rlfo1.stereo", "rlfo1.keytrack_transpose", "rlfo1.keytrack_tune" }, 8, nullptr, 0 },
    // Page 33: Random LFO 2 (LFO)
    { "rlfo2", "Random LFO 2", "LFO", { "rlfo2.style", "rlfo2.sync", "rlfo2.rate", "rlfo2.division", "rlfo2.sync_type", "rlfo2.stereo", "rlfo2.keytrack_transpose", "rlfo2.keytrack_tune" }, 8, nullptr, 0 },
    // Page 34: Random LFO 3 (LFO)
    { "rlfo3", "Random LFO 3", "LFO", { "rlfo3.style", "rlfo3.sync", "rlfo3.rate", "rlfo3.division", "rlfo3.sync_type", "rlfo3.stereo", "rlfo3.keytrack_transpose", "rlfo3.keytrack_tune" }, 8, nullptr, 0 },
    // Page 35: Random LFO 4 (LFO)
    { "rlfo4", "Random LFO 4", "LFO", { "rlfo4.style", "rlfo4.sync", "rlfo4.rate", "rlfo4.division", "rlfo4.sync_type", "rlfo4.stereo", "rlfo4.keytrack_transpose", "rlfo4.keytrack_tune" }, 8, nullptr, 0 },
    // Page 36: FX Chain Order 1–8 (FX)
    { "fx_order_1", "FX Chain Order 1–8", "FX", { "fx.order.1", "fx.order.2", "fx.order.3", "fx.order.4", "fx.order.5", "fx.order.6", "fx.order.7", "fx.order.8" }, 8, nullptr, 0 },
    // Page 37: FX Order 9 & Switches (FX)
    { "fx_switches", "FX Order 9 & Switches", "FX", { "fx.order.9", "fxfilter.on", "distortion.on", "compressor.on", "eq.on", "chorus.on", "delay.on", "reverb.on" }, 8, nullptr, 0 },
    // Page 38: Chorus Main (FX)
    { "fx_chorus_1", "Chorus Main", "FX", { "chorus.on", "chorus.mix", "chorus.rate", "chorus.sync", "chorus.division", "chorus.depth", "chorus.feedback", "chorus.voices" }, 8, nullptr, 0 },
    // Page 39: Chorus Delays & Filter (FX)
    { "fx_chorus_2", "Chorus Delays & Filter", "FX", { "chorus.delay1", "chorus.delay2", "chorus.filter_cutoff", "chorus.filter_spread", "chorus.mix", "chorus.depth", "chorus.feedback", "chorus.on" }, 8, nullptr, 0 },
    // Page 40: Compressor Main & Gain (FX)
    { "fx_comp_main", "Compressor Main & Gain", "FX", { "compressor.on", "compressor.mix", "compressor.bands", "compressor.attack", "compressor.release", "compressor.low.gain", "compressor.mid.gain", "compressor.high.gain" }, 8, nullptr, 0 },
    // Page 41: Compressor Low Band (FX)
    { "fx_comp_low", "Compressor Low Band", "FX", { "compressor.low.lower_threshold", "compressor.low.upper_threshold", "compressor.low.lower_ratio", "compressor.low.upper_ratio", "compressor.low.gain", "compressor.attack", "compressor.release", "compressor.mix" }, 8, nullptr, 0 },
    // Page 42: Compressor Mid Band (FX)
    { "fx_comp_mid", "Compressor Mid Band", "FX", { "compressor.mid.lower_threshold", "compressor.mid.upper_threshold", "compressor.mid.lower_ratio", "compressor.mid.upper_ratio", "compressor.mid.gain", "compressor.attack", "compressor.release", "compressor.mix" }, 8, nullptr, 0 },
    // Page 43: Compressor High Band (FX)
    { "fx_comp_high", "Compressor High Band", "FX", { "compressor.high.lower_threshold", "compressor.high.upper_threshold", "compressor.high.lower_ratio", "compressor.high.upper_ratio", "compressor.high.gain", "compressor.attack", "compressor.release", "compressor.mix" }, 8, nullptr, 0 },
    // Page 44: Delay Main (FX)
    { "fx_delay_1", "Delay Main", "FX", { "delay.on", "delay.mix", "delay.style", "delay.feedback", "delay.sync", "delay.time", "delay.time2", "delay.division" }, 8, nullptr, 0 },
    // Page 45: Delay Filter & Div 2 (FX)
    { "fx_delay_2", "Delay Filter & Div 2", "FX", { "delay.division2", "delay.filter_cutoff", "delay.filter_spread", "delay.feedback", "delay.mix", "delay.style", "delay.sync", "delay.on" }, 8, nullptr, 0 },
    // Page 46: Distortion (FX)
    { "fx_distortion", "Distortion", "FX", { "distortion.on", "distortion.type", "distortion.drive", "distortion.mix", "distortion.filter_order", "distortion.filter_cutoff", "distortion.filter_resonance", "distortion.filter_blend" }, 8, nullptr, 0 },
    // Page 47: EQ Low & Mid (FX)
    { "fx_eq_low_mid", "EQ Low & Mid", "FX", { "eq.on", "eq.low.mode", "eq.low.cutoff", "eq.low.gain", "eq.low.resonance", "eq.mid.mode", "eq.mid.cutoff", "eq.mid.gain" }, 8, nullptr, 0 },
    // Page 48: EQ Mid Q & High (FX)
    { "fx_eq_high", "EQ Mid Q & High", "FX", { "eq.mid.resonance", "eq.high.mode", "eq.high.cutoff", "eq.high.gain", "eq.high.resonance", "eq.low.gain", "eq.mid.gain", "eq.on" }, 8, nullptr, 0 },
    // Page 49: FX Filter Main (FX)
    { "fx_filter_1", "FX Filter Main", "FX", { "fxfilter.on", "fxfilter.model", "fxfilter.style", "fxfilter.cutoff", "fxfilter.resonance", "fxfilter.drive", "fxfilter.blend", "fxfilter.mix" }, 8, nullptr, 0 },
    // Page 50: FX Filter Formant/Comb (FX)
    { "fx_filter_2", "FX Filter Formant/Comb", "FX", { "fxfilter.keytrack", "fxfilter.comb_offset", "fxfilter.formant.x", "fxfilter.formant.y", "fxfilter.formant.resonance", "fxfilter.formant.spread", "fxfilter.formant.transpose", "fxfilter.mix" }, 8, nullptr, 0 },
    // Page 51: Flanger (FX)
    { "fx_flanger", "Flanger", "FX", { "flanger.on", "flanger.mix", "flanger.rate", "flanger.sync", "flanger.division", "flanger.depth", "flanger.feedback", "flanger.center" }, 8, nullptr, 0 },
    // Page 52: Phaser & Flanger Phase (FX)
    { "fx_phaser_1", "Phaser & Flanger Phase", "FX", { "phaser.on", "phaser.mix", "phaser.rate", "phaser.sync", "phaser.division", "phaser.depth", "phaser.feedback", "flanger.phase_offset" }, 8, nullptr, 0 },
    // Page 53: Phaser Tone & Stereo (FX)
    { "fx_phaser_2", "Phaser Tone & Stereo", "FX", { "phaser.center", "phaser.blend", "phaser.phase_offset", "phaser.depth", "phaser.feedback", "phaser.mix", "phaser.rate", "phaser.on" }, 8, nullptr, 0 },
    // Page 54: Reverb Space & Cuts (FX)
    { "fx_reverb_1", "Reverb Space & Cuts", "FX", { "reverb.on", "reverb.mix", "reverb.size", "reverb.decay", "reverb.predelay", "reverb.pre_low_cut", "reverb.pre_high_cut", "reverb.chorus_amount" }, 8, nullptr, 0 },
    // Page 55: Reverb Damping & Chorus (FX)
    { "fx_reverb_2", "Reverb Damping & Chorus", "FX", { "reverb.low_shelf_cutoff", "reverb.low_shelf_gain", "reverb.high_shelf_cutoff", "reverb.high_shelf_gain", "reverb.chorus_amount", "reverb.chorus_rate", "reverb.decay", "reverb.mix" }, 8, nullptr, 0 },
    // Page 56: Mod Matrix Slot 1 (Matrix)
    { "mod_slot_1", "Mod Matrix Slot 1", "Matrix", { "mod01.source", "mod01.destination", "mod01.amount", "mod01.power", "mod01.bipolar", "mod01.stereo", "mod01.bypass", nullptr }, 7, nullptr, 0 },
    // Page 57: Mod Matrix Slot 2 (Matrix)
    { "mod_slot_2", "Mod Matrix Slot 2", "Matrix", { "mod02.source", "mod02.destination", "mod02.amount", "mod02.power", "mod02.bipolar", "mod02.stereo", "mod02.bypass", nullptr }, 7, nullptr, 0 },
    // Page 58: Mod Matrix Slot 3 (Matrix)
    { "mod_slot_3", "Mod Matrix Slot 3", "Matrix", { "mod03.source", "mod03.destination", "mod03.amount", "mod03.power", "mod03.bipolar", "mod03.stereo", "mod03.bypass", nullptr }, 7, nullptr, 0 },
    // Page 59: Mod Matrix Slot 4 (Matrix)
    { "mod_slot_4", "Mod Matrix Slot 4", "Matrix", { "mod04.source", "mod04.destination", "mod04.amount", "mod04.power", "mod04.bipolar", "mod04.stereo", "mod04.bypass", nullptr }, 7, nullptr, 0 },
    // Page 60: Mod Amounts 1–8 (Matrix)
    { "mod_amounts_1_8", "Mod Amounts 1–8", "Matrix", { "mod01.amount", "mod02.amount", "mod03.amount", "mod04.amount", "mod05.amount", "mod06.amount", "mod07.amount", "mod08.amount" }, 8, nullptr, 0 },
    // Page 61: Mod Amounts 9–16 (Matrix)
    { "mod_amounts_9_16", "Mod Amounts 9–16", "Matrix", { "mod09.amount", "mod10.amount", "mod11.amount", "mod12.amount", "mod13.amount", "mod14.amount", "mod15.amount", "mod16.amount" }, 8, s_more_mod_matrix, 408 },
};

constexpr MacroTarget s_macro_targets_bass_1[] = { { "filter1.cutoff", 45.0f, false }, { "filter2.cutoff", 45.0f, false } };
constexpr MacroTarget s_macro_targets_bass_2[] = { { "filter1.drive", 50.0f, false }, { "distortion.drive", 35.0f, false }, { "osc1.warp.amount", 25.0f, false } };
constexpr MacroTarget s_macro_targets_bass_3[] = { { "env2.decay", 40.0f, false }, { "lfo1.rate", 30.0f, false } };
constexpr MacroTarget s_macro_targets_bass_4[] = { { "osc1.unison.detune", 25.0f, false }, { "chorus.mix", 35.0f, false } };

constexpr MacroTarget s_macro_targets_lead_1[] = { { "filter1.cutoff", 50.0f, false }, { "osc1.morph.amount", 30.0f, false } };
constexpr MacroTarget s_macro_targets_lead_2[] = { { "osc1.warp.amount", 45.0f, false }, { "osc1.frame", 50.0f, false } };
constexpr MacroTarget s_macro_targets_lead_3[] = { { "osc1.unison.detune", 30.0f, false }, { "osc2.unison.detune", 30.0f, false } };
constexpr MacroTarget s_macro_targets_lead_4[] = { { "delay.mix", 35.0f, false }, { "reverb.mix", 30.0f, false } };

constexpr MacroTarget s_macro_targets_pad_1[] = { { "filter1.cutoff", 55.0f, false }, { "filter1.resonance", 15.0f, false } };
constexpr MacroTarget s_macro_targets_pad_2[] = { { "osc1.frame", 60.0f, false }, { "osc2.frame", 60.0f, false } };
constexpr MacroTarget s_macro_targets_pad_3[] = { { "lfo1.rate", 25.0f, false }, { "env1.attack", -30.0f, false }, { "env1.release", 35.0f, false } };
constexpr MacroTarget s_macro_targets_pad_4[] = { { "reverb.mix", 40.0f, false }, { "reverb.decay", 30.0f, false }, { "chorus.mix", 30.0f, false } };

constexpr MacroTarget s_macro_targets_pluck_1[] = { { "filter1.cutoff", 50.0f, false }, { "env2.decay", 45.0f, false } };
constexpr MacroTarget s_macro_targets_pluck_2[] = { { "env1.decay", 50.0f, false }, { "env1.release", 40.0f, false } };
constexpr MacroTarget s_macro_targets_pluck_3[] = { { "osc1.morph.amount", 40.0f, false }, { "noise.level", 25.0f, false } };
constexpr MacroTarget s_macro_targets_pluck_4[] = { { "delay.mix", 40.0f, false }, { "reverb.mix", 30.0f, false } };

constexpr MacroTarget s_macro_targets_keys_1[] = { { "filter1.cutoff", 40.0f, false }, { "osc1.warp.amount", 30.0f, false } };
constexpr MacroTarget s_macro_targets_keys_2[] = { { "lfo1.rate", 35.0f, false }, { "phaser.mix", 40.0f, false } };
constexpr MacroTarget s_macro_targets_keys_3[] = { { "env1.release", 45.0f, false }, { "env1.decay", 35.0f, false } };
constexpr MacroTarget s_macro_targets_keys_4[] = { { "chorus.mix", 35.0f, false }, { "reverb.mix", 25.0f, false } };

constexpr MacroTarget s_macro_targets_fx_1[] = { { "filter1.cutoff", 70.0f, false }, { "fxfilter.cutoff", 70.0f, false } };
constexpr MacroTarget s_macro_targets_fx_2[] = { { "rlfo1.rate", 60.0f, false }, { "lfo1.rate", 60.0f, false } };
constexpr MacroTarget s_macro_targets_fx_3[] = { { "distortion.drive", 55.0f, false }, { "osc1.warp.amount", 50.0f, false } };
constexpr MacroTarget s_macro_targets_fx_4[] = { { "reverb.mix", 50.0f, false }, { "delay.feedback", 40.0f, false } };

constexpr MacroTarget s_macro_targets_init_1[] = { { "filter1.cutoff", 50.0f, false } };
constexpr MacroTarget s_macro_targets_init_2[] = { { "filter1.resonance", 40.0f, false } };
constexpr MacroTarget s_macro_targets_init_3[] = { { "osc1.frame", 100.0f, false } };
constexpr MacroTarget s_macro_targets_init_4[] = { { "reverb.mix", 35.0f, false } };

constexpr CategoryMacroDefault s_categoryMacroDefaults[] = {
    {
        "Bass",
        {
            { "macro.1", "Cutoff", s_macro_targets_bass_1, 2 },
            { "macro.2", "Drive & Bite", s_macro_targets_bass_2, 3 },
            { "macro.3", "Env/Wobble", s_macro_targets_bass_3, 2 },
            { "macro.4", "Width & FX", s_macro_targets_bass_4, 2 }
        }
    },
    {
        "Lead",
        {
            { "macro.1", "Brightness", s_macro_targets_lead_1, 2 },
            { "macro.2", "Timbre", s_macro_targets_lead_2, 2 },
            { "macro.3", "Detune/Glide", s_macro_targets_lead_3, 2 },
            { "macro.4", "Space", s_macro_targets_lead_4, 2 }
        }
    },
    {
        "Pad",
        {
            { "macro.1", "Warmth", s_macro_targets_pad_1, 2 },
            { "macro.2", "Morph", s_macro_targets_pad_2, 2 },
            { "macro.3", "Slow Motion", s_macro_targets_pad_3, 3 },
            { "macro.4", "Lush Space", s_macro_targets_pad_4, 3 }
        }
    },
    {
        "Pluck",
        {
            { "macro.1", "Pluck Open", s_macro_targets_pluck_1, 2 },
            { "macro.2", "Length", s_macro_targets_pluck_2, 2 },
            { "macro.3", "Harmonics", s_macro_targets_pluck_3, 2 },
            { "macro.4", "Echo & Hall", s_macro_targets_pluck_4, 2 }
        }
    },
    {
        "Keys",
        {
            { "macro.1", "Tone/Bell", s_macro_targets_keys_1, 2 },
            { "macro.2", "Tremolo", s_macro_targets_keys_2, 2 },
            { "macro.3", "Release", s_macro_targets_keys_3, 2 },
            { "macro.4", "Room/Chorus", s_macro_targets_keys_4, 2 }
        }
    },
    {
        "FX",
        {
            { "macro.1", "Sweep/Freq", s_macro_targets_fx_1, 2 },
            { "macro.2", "Chaos/Rate", s_macro_targets_fx_2, 2 },
            { "macro.3", "Crush & Fold", s_macro_targets_fx_3, 2 },
            { "macro.4", "Wash", s_macro_targets_fx_4, 2 }
        }
    },
    {
        "Init",
        {
            { "macro.1", "Cutoff", s_macro_targets_init_1, 1 },
            { "macro.2", "Resonance", s_macro_targets_init_2, 1 },
            { "macro.3", "Wavetable", s_macro_targets_init_3, 1 },
            { "macro.4", "Space", s_macro_targets_init_4, 1 }
        }
    }
};
} // namespace

const Page* kickPageTable() noexcept
{
    return s_kickPages;
}

size_t kickPageCount() noexcept
{
    return kKickPageCount;
}

const CategoryMacroDefault* categoryMacroDefaults() noexcept
{
    return s_categoryMacroDefaults;
}

size_t categoryMacroDefaultCount() noexcept
{
    return sizeof (s_categoryMacroDefaults) / sizeof (s_categoryMacroDefaults[0]);
}
} // namespace synthyboy
