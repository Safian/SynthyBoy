// SynthyBoy - own engine: which keys the engine acts on and which capability bits it reports.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>

#include "synthyboy/engine_caps.h"

namespace synthyboy
{
namespace own
{
/** True if the engine acts on the key at this table position (before the capability check of IEngine::supportsParam). */
bool isImplemented (size_t paramIndex) noexcept;
EngineCapsMask ownCapabilities() noexcept;
EngineCaps2Mask ownCapabilities2() noexcept;
} // namespace own
} // namespace synthyboy
