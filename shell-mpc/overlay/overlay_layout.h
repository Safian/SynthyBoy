// Edit this file directly; overlay_layout.json holds the same numbers for the skin validator.
// SynthyBoy - MPC overlay: the views (region of a tab + two marker squares + the instance picture each) of the overlay skin, in SCREEN coordinates (landscape UI, 1280 x 800).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy_mpc { namespace overlay { namespace layout {
struct Rect { int x, y, w, h; };
struct Marker { Rect rect; unsigned rgb; unsigned cover; };   // cover: the colour of what lies under the marker (a transparent view paints it over the marker)
constexpr int kKindFilter = 0, kKindLfo = 1, kKindSpectrum = 4, kKindPlay = 5, kKindFilterUi = 6, kKindMixer = 7, kKindFx = 8, kKindMod = 9, kKindMacros = 10, kKindMaster = 11, kKindVoice = 12, kKindEnvUi = 13, kKindLfoUi = 14, kKindOscUi = 15;
constexpr bool isPageKind (int kind) { return kind == kKindPlay || kind >= kKindFilterUi; }   // a page the plugin draws entirely (its own touch handling, the host widgets hidden under it)
struct View { const char* name; int kind; int index; Rect region; bool opaque; Marker markers[2]; Rect instance; };   // instance: the film strip image bound to the display parameter "Overlay Instance" (one frame = this size)
constexpr int kScreenW = 1280;
constexpr int kScreenH = 800;
constexpr int kTabOriginY = 110;
constexpr unsigned kBackgroundRgb = 0x1e1d1e;   // the colour of the dark picture of an opaque view
constexpr int kMaxInstances = 16;   // slots 0 .. kMaxInstances - 1: the instances beyond them get none and never show the overlay
constexpr int kInstanceFrames = 33;   // frames of the film strip: 0 blank, 1 .. 16 slot 0 .. 15, 17 none, the rest blank
constexpr int kInstanceSteps = 32;   // the parameter's value of frame f is f / kInstanceSteps (exact in a float)
constexpr unsigned kInstanceSlotRgb[kMaxInstances] = { 0x1414d8, 0x3e14d8, 0x6814d8, 0x9214d8, 0x143ed8, 0x3e3ed8, 0x683ed8, 0x923ed8, 0x1468d8, 0x3e68d8, 0x6868d8, 0x9268d8, 0x1492d8, 0x3e92d8, 0x6892d8, 0x9292d8 };
constexpr unsigned kInstanceNoneRgb = 0xc814d8;    // frame 17: an instance without a slot
constexpr unsigned kInstanceBlankRgb = 0x14c8d8;   // frame 0 (nothing reported yet) and the frames after 17
constexpr int kNumViews = 37;
constexpr View kViews[kNumViews] = {
    { "play1", 5, 1, { 23, 110, 1234, 204 }, true, { { { 33, 111, 6, 6 }, 0x723a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x3a7264, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Play"
    { "macros", 10, 1, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x6b3a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x3a6b64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Macros"
    { "osca", 15, 2, { 0, 110, 1280, 628 }, true, { { { 12, 726, 6, 6 }, 0xaa3a2c, 0x1e1d1e }, { { 1250, 666, 6, 6 }, 0x3aaa64, 0x1e1d1e } }, { 24, 726, 6, 6 } },   // tab "Osc A"
    { "oscb", 15, 3, { 0, 110, 1280, 628 }, true, { { { 12, 726, 6, 6 }, 0xb13a2c, 0x1e1d1e }, { { 1250, 666, 6, 6 }, 0x3ab164, 0x1e1d1e } }, { 24, 726, 6, 6 } },   // tab "Osc B"
    { "oscc", 15, 4, { 0, 110, 1280, 628 }, true, { { { 12, 726, 6, 6 }, 0xb83a2c, 0x1e1d1e }, { { 1250, 666, 6, 6 }, 0x3ab864, 0x1e1d1e } }, { 24, 726, 6, 6 } },   // tab "Osc C"
    { "oscsub", 15, 1, { 0, 110, 1280, 628 }, true, { { { 12, 726, 6, 6 }, 0xbf3a2c, 0x1e1d1e }, { { 1250, 666, 6, 6 }, 0x3abf64, 0x1e1d1e } }, { 24, 726, 6, 6 } },   // tab "Sub"
    { "oscnoise", 15, 5, { 0, 110, 1280, 628 }, true, { { { 12, 726, 6, 6 }, 0xc63a2c, 0x1e1d1e }, { { 1250, 666, 6, 6 }, 0x3ac664, 0x1e1d1e } }, { 24, 726, 6, 6 } },   // tab "Noise"
    { "env1", 13, 1, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x4f3a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x3a4f64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Env"
    { "env2", 13, 2, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x105a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a1064, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Env 2"
    { "env3", 13, 3, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x175a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a1764, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Env 3"
    { "env4", 13, 4, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x1e5a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a1e64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Env 4"
    { "env5", 13, 5, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x255a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a2564, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Env 5"
    { "env6", 13, 6, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x2c5a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a2c64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Env 6"
    { "lfo1", 14, 1, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x1e3a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x3a1e64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO"
    { "lfo2", 14, 2, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x4f5a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a4f64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO 2"
    { "lfo3", 14, 3, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x565a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a5664, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO 3"
    { "lfo4", 14, 4, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x5d5a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a5d64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO 4"
    { "lfo5", 14, 5, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x645a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a6464, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO 5"
    { "lfo6", 14, 6, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x6b5a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a6b64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO 6"
    { "lfo7", 14, 7, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x725a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a7264, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO 7"
    { "lfo8", 14, 8, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x795a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a7964, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO 8"
    { "lfo9", 14, 9, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x805a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a8064, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO 9"
    { "lfo10", 14, 10, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x875a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a8764, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "LFO 10"
    { "rnd1", 14, 11, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x8e5a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a8e64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "RND 1"
    { "rnd2", 14, 12, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x955a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a9564, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "RND 2"
    { "rnd3", 14, 13, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x9c5a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a9c64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "RND 3"
    { "rnd4", 14, 14, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0xa35a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5aa364, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "RND 4"
    { "fx", 8, 1, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x8e3a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x3a8e64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "FX"
    { "mixer", 7, 1, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x873a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x3a8764, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Mixer"
    { "filter1", 6, 1, { 0, 110, 1280, 628 }, true, { { { 33, 114, 6, 6 }, 0x103a2c, 0x1e1d1e }, { { 1241, 114, 6, 6 }, 0x3a1064, 0x1e1d1e } }, { 45, 114, 6, 6 } },   // tab "Filter"
    { "filter2", 6, 2, { 0, 110, 1280, 628 }, true, { { { 33, 114, 6, 6 }, 0x3a5a2c, 0x1e1d1e }, { { 1241, 114, 6, 6 }, 0x5a3a64, 0x1e1d1e } }, { 45, 114, 6, 6 } },   // tab "Filter 2"
    { "filternote", 6, 3, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x415a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a4164, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Note"
    { "filtervelo", 6, 4, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x485a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a4864, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Velo"
    { "mod", 9, 1, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x953a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x3a9564, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Mod"
    { "master", 11, 1, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x9c3a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x3a9c64, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Master"
    { "tuning", 11, 2, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0x335a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x5a3364, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Tuning"
    { "voice", 12, 1, { 0, 110, 1280, 628 }, true, { { { 33, 111, 6, 6 }, 0xa33a2c, 0x1e1d1e }, { { 1241, 111, 6, 6 }, 0x3aa364, 0x1e1d1e } }, { 45, 111, 6, 6 } },   // tab "Voice"
};
}}} // namespace synthyboy_mpc::overlay::layout
