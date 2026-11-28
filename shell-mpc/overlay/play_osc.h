// SynthyBoy - MPC overlay: the five oscillator instances of the Play page's "Play Osc" selector, in the order of the selector: 1 Sub, 2 A, 3 B, 4 C, 5 Noise (docs/MPC.md "Play selectors and proxies").
// Pure constants and functions, no dependencies, so that the plugin's parameter code (shell-mpc/play_params.h) and the overlay's views use ONE table.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy_mpc { namespace playosc {

constexpr int kInstances = 5;
constexpr int kSub = 1, kA = 2, kB = 3, kC = 4, kNoise = 5;
/** The key of an instance in the engine (core/engine-own/voice/param_index.cpp: voicectl.osc.<key>). */
constexpr const char* kKeys[kInstances] = { "sub", "a", "b", "c", "noise" };

/** The number N of the engine's "oscN.*" parameters of a selector instance (1 ..): 1 .. 3 for A, B, C; 0 for Sub and Noise (their parameters are "sub.*" and "noise.*") and for a value outside 1 .. 5. */
constexpr int oscNumber (int instance) noexcept { return instance >= kA && instance <= kC ? instance - 1 : 0; }
/** The selector instance of the engine's oscillator N (1 .. 3), 0 outside. */
constexpr int instanceOfOsc (int n) noexcept { return n >= 1 && n <= 3 ? n + 1 : 0; }
/** The old numbering (a chunk of the build that had three instances: 1 .. 3 = A, B, C) in the selector's numbering; 0 when outside 1 .. 3. */
constexpr int fromThreeInstanceChunk (int old) noexcept { return instanceOfOsc (old); }

}} // namespace synthyboy_mpc::playosc
