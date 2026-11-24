// SynthyBoy - standalone ENV editor handles.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy_mpc::nativeui::env
{
enum Field
{
    Attack, Decay, Sustain, Release, AttackCurve, DecayCurve, ReleaseCurve, Delay, Hold,
    Beats, Retrig, Selection, Targets, kFields
};
constexpr int kInstances = 6;
constexpr const char* kNames[kFields] {
    "Env Native Attack", "Env Native Decay", "Env Native Sustain", "Env Native Release",
    "Env Native Attack Curve", "Env Native Decay Curve", "Env Native Release Curve", "Env Native Delay", "Env Native Hold",
    "Env Native Beats", "Env Native Retrig", "Env Native Selection", "Env Native Targets"
};
}
