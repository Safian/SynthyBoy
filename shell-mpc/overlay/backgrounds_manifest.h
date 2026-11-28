// Edit this file directly.
// SynthyBoy - MPC overlay: the manifest of tabs that have pre-rendered static host page backgrounds in the skin.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstring>

namespace synthyboy_mpc { namespace overlay {

struct BackgroundTab
{
    const char* viewName;
    const char* tabName;
    const char* assetFile;
    int sentinelX, sentinelY;   // the 5 x 5 square of the sentinel, TAB coordinates (the host's picture must show sentinelRgb there before the plugin's plane may be transparent)
    unsigned sentinelRgb;
};

// The tabs that have a host background; the last entry is a terminator that keeps the array non-empty.
inline constexpr BackgroundTab kBackgroundTabs[] = {
    { "macros", "Macros", "", 900, 2, 0x0b1c5fu },
    { "play1", "Play", "", 900, 2, 0x2a3a50u },
    { "mixer", "Mixer", "", 900, 2, 0x0f1a4bu },
    { "fx", "FX", "", 900, 2, 0x0c1038u },
    { "master", "Master", "", 900, 2, 0x24142du },
    { "tuning", "Tuning", "", 900, 2, 0x24142du },
    { "voice", "Voice", "", 900, 2, 0x1a1010u },
    { "filter1", "Filter", "", 900, 2, 0x171c35u },
    { "filter2", "Filter 2", "", 900, 2, 0x171c35u },
    { "filternote", "Note", "", 900, 2, 0x171c35u },
    { "filtervelo", "Velo", "", 900, 2, 0x171c35u },
    { "osca", "Osc A", "", 1180, 2, 0x0f253fu },
    { "oscb", "Osc B", "", 1180, 2, 0x0f253fu },
    { "oscc", "Osc C", "", 1180, 2, 0x0f253fu },
    { "oscsub", "Sub", "", 1180, 2, 0x0f253fu },
    { "oscnoise", "Noise", "", 1180, 2, 0x0f253fu },
    { "mod", "Mod", "", 900, 2, 0x0b2017u },
    { "lfo1", "LFO", "", 900, 2, 0x0b102du },
    { "lfo2", "LFO 2", "", 900, 2, 0x0b102du },
    { "lfo3", "LFO 3", "", 900, 2, 0x0b102du },
    { "lfo4", "LFO 4", "", 900, 2, 0x0b102du },
    { "lfo5", "LFO 5", "", 900, 2, 0x0b102du },
    { "lfo6", "LFO 6", "", 900, 2, 0x0b102du },
    { "lfo7", "LFO 7", "", 900, 2, 0x0b102du },
    { "lfo8", "LFO 8", "", 900, 2, 0x0b102du },
    { "lfo9", "LFO 9", "", 900, 2, 0x0b102du },
    { "lfo10", "LFO 10", "", 900, 2, 0x0b102du },
    { "rnd1", "RND 1", "", 900, 2, 0x0b102du },
    { "rnd2", "RND 2", "", 900, 2, 0x0b102du },
    { "rnd3", "RND 3", "", 900, 2, 0x0b102du },
    { "rnd4", "RND 4", "", 900, 2, 0x0b102du },
    { "env1", "Env", "", 900, 2, 0x0b1023u },
    { "env2", "Env 2", "", 900, 2, 0x0b1023u },
    { "env3", "Env 3", "", 900, 2, 0x0b1023u },
    { "env4", "Env 4", "", 900, 2, 0x0b1023u },
    { "env5", "Env 5", "", 900, 2, 0x0b1023u },
    { "env6", "Env 6", "", 900, 2, 0x0b1023u },
    { nullptr, nullptr, nullptr, 0, 0, 0u },
};

inline constexpr int kNumBackgroundTabs = 37;

inline const BackgroundTab* backgroundOf (const char* viewName) noexcept
{
    if (viewName == nullptr)
        return nullptr;
    for (int i = 0; i < kNumBackgroundTabs; ++i)
        if (std::strcmp (viewName, kBackgroundTabs[i].viewName) == 0)
            return &kBackgroundTabs[i];
    return nullptr;
}

inline bool hasHostBackground (const char* viewName) noexcept { return backgroundOf (viewName) != nullptr; }

}} // namespace synthyboy_mpc::overlay
