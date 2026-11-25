// SynthyBoy - native Master tab visibility frames.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy_mpc::nativeui::master
{
enum View { Hidden = 0, Main, Tuning };
constexpr int kLastFrame = Tuning;
constexpr float panelValue (int view) noexcept { return view == Tuning ? 1.0f : 0.0f; }

enum OutputField { PeakL, PeakR, HoldL, HoldR, VoiceRatio, VoiceCount, Limiter, Clip, ClearClip, kOutputFields };
constexpr int kVoiceCountLimit = 32;

struct OutputReadout
{
    float peakL, peakR, holdL, holdR;
    int activeVoices, polyphony;
    bool clipped;
};
}
