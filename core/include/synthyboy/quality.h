// SynthyBoy - core: quality tiers (platform scaling), see docs/ARCHITECTURE.md 3.6.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy
{
/** Hard upper bound of the voice pool; Quality::maxVoices is capped to it by Engine::prepare. */
constexpr int kMaxVoices = 32;

enum class Tier
{
    low,    // MPC-class ARMv7
    medium, // phone
    high    // desktop
};

/**
 * Per-instance quality settings (never a global). Phase 1 only uses maxVoices; the shells do not select a tier yet
 * and run with the default (8 voices, as the spike). Tier numbers are the targets of the architecture document,
 * not measured claims.
 */
struct Quality
{
    int maxVoices = 8;
};

constexpr Quality qualityForTier (Tier tier) noexcept
{
    switch (tier)
    {
        case Tier::low:    return Quality { 8 };
        case Tier::medium: return Quality { 16 };
        case Tier::high:   return Quality { kMaxVoices };
    }
    return Quality {};
}
} // namespace synthyboy
