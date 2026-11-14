// SynthyBoy - MPC overlay tests: the reference interpreter of the GPU scene is the CPU drawing of the product (scene_cpu.h); this header keeps the name the tests use.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "../scene_cpu.h"

namespace ovt {

using synthyboy_mpc::overlay::sceneColour;

inline void renderSceneReference (const synthyboy_mpc::overlay::gpu::SceneData& s, std::vector<uint32_t>& cv) { synthyboy_mpc::overlay::renderScene (s, cv); }

} // namespace ovt
