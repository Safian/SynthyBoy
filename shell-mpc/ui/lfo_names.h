// SynthyBoy - MPC UI: the names of the shapes and of the random styles of the LFO (plain data; the pictures of the native picker are the host's).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "lfo_model.h"

namespace synthyboy_mpc { namespace ui {

namespace lfoglyph
{
/** The name of shape 0 .. 12 (the 12 built-in shapes, then the drawn one). */
inline const char* name (int shape) noexcept
{
    static const char* const n[lfo::kNumShapes] = { "SINE", "TRI", "SAW UP", "SAW DN", "SQUARE", "PULSE", "STEP UP", "STEP DN", "RANDOM", "EXP UP", "EXP DN", "HALF", "DRAWN" };
    return shape >= 0 && shape < lfo::kNumShapes ? n[shape] : "";
}
}

/** The name of a style of a random LFO (Perlin noise, sample and hold, sine interpolation, the Lorenz attractor, the Rossler attractor). */
inline const char* styleName (int style) noexcept
{
    static const char* const n[5] = { "PERLIN", "S & H", "SINE INT", "LORENZ", "ROSSLER" };
    return style >= 0 && style < 5 ? n[style] : "";
}

}} // namespace synthyboy_mpc::ui
