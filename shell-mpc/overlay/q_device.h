// SynthyBoy - MPC overlay: how many Q-Link knobs the device has at once. The MPC XL has 16 knobs (4 x 4, each with its own display): the 16 Q proxies are all reachable and the idle chips of the other mapped groups
// show their knob numbers 5 .. 16. The Key 61 (and the other MPCs with one row of four knobs and a bank button) reaches four at a time: the idle chips stay 1 .. 4, relative to the bank. The panel of the two is the same size
// (1280 x 800), so it tells nothing; what the plugin has is the name the kernel gives the device (device tree, "Akai Professional MPC Key 61") and the architecture of its own build (the 64-bit module is the one the 64-bit
// MPCs load, the XL among them). The rule: a name that says XL means 16 knobs, any other name 4; when the name cannot be read the architecture decides (64-bit: 16). SYNTHYBOY_MPC_Q_KNOBS=4 or 16 overrides it (tests).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../kit/branding.h"

namespace synthyboy_mpc { namespace overlay {

/** The rule above, pure: `model` the device name ("" when it could not be read), `build64` the plugin is the 64-bit build. */
inline bool hasSixteenQLinks (const std::string& model, bool build64) noexcept
{
    if (model.empty())
        return build64;
    for (size_t at = model.find ("XL"); at != std::string::npos; at = model.find ("XL", at + 1))
    {
        const bool startOk = at == 0 || model[at - 1] == ' ' || model[at - 1] == '-';
        const bool endOk = at + 2 >= model.size() || model[at + 2] == ' ' || model[at + 2] == '\0' || model[at + 2] == '-';
        if (startOk && endOk)
            return true;
    }
    return false;
}

/** The name of the device from the device tree ("" when there is none); trailing characters that are no text are dropped. */
inline std::string readDeviceModel()
{
    std::string text;
    if (FILE* f = std::fopen ("/proc/device-tree/model", "rb"))
    {
        char buf[128];
        const size_t n = std::fread (buf, 1, sizeof buf - 1, f);
        std::fclose (f);
        for (size_t i = 0; i < n && buf[i] != '\0'; ++i)
            text += buf[i];
        while (! text.empty() && (text.back() == ' ' || text.back() == '\n'))
            text.pop_back();
    }
    return text;
}

/** The device this plugin runs on has 16 Q-Link knobs at once. */
inline bool detectSixteenQLinks()
{
    if (const char* e = std::getenv (mpckit::branding().qKnobsEnv))   // (SynthyBoy: SYNTHYBOY_MPC_Q_KNOBS; kit/branding.h)
    {
        if (std::string (e) == "16")
            return true;
        if (std::string (e) == "4")
            return false;
    }
#if defined(__aarch64__)
    constexpr bool kBuild64 = true;
#else
    constexpr bool kBuild64 = false;
#endif
    return hasSixteenQLinks (readDeviceModel(), kBuild64);
}

}} // namespace synthyboy_mpc::overlay
