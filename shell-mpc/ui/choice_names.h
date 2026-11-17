// SynthyBoy - MPC UI component library: the names of the choices and laws on the screen. The engine's choice lists still carry the working names of the models they were measured against ("Vital", "Serum");
// the screen never shows a name of another product: every page that shows one of these parameters asks this file for the name by what the choice DOES (read from the code of the engine, core/engine-own):
//   eq.law          0  the equalizer is built from state variable sections (the Digital filter's shelves and peaks)                    -> "State variable"
//                   1  the textbook biquads (shelf, peak, notch, high / low pass) with their measured Q law                                    -> "Biquad"
//   convolve.law    0  SIZE is 0.25 .. 4 times the impulse and DECAY a share of its length (relative to the impulse)                           -> "Relative"
//                   1  SIZE is 0.1 .. 10 times and DECAY a time of up to 40 seconds, the impulse is normalised to its peak (absolute time)     -> "Seconds"
//   oscN.unison.law 0  the voices are weighted by the blend and normalised to equal power, the stereo spread is equal power                    -> "Equal power"
//                   1  a fixed gain per voice count and a table for the centre / outer voices and the stereo spread                              -> "Fixed gain"
//   reverb.type     0  the sixteen line feedback network (dense, smooth tail); 1 .. 5 are the named room models                                -> "Dense"
//   compressor.limit 2 the peak compressor (attack and release in milliseconds, the gain follows the peaks)                                   -> "Peak"
// and the label of the parameter where the engine's name says nothing to a player (EQ LAW -> BAND SHAPE, Convolve Law -> SCALING, Reverb Type -> ALGORITHM, unison law -> VOICE LAW).
// Pure functions, no state. The oscillator pages use the same functions for oscN.unison.law (shownChoice / shownLabel).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstring>

namespace synthyboy_mpc { namespace ui {

namespace choicenames {
/** Which law the key is: 0 none, 1 eq.law, 2 convolve.law, 3 oscN.unison.law, 4 reverb.type, 5 compressor.limit. */
inline int lawOf (const char* key) noexcept
{
    if (key == nullptr)
        return 0;
    if (std::strcmp (key, "eq.law") == 0)
        return 1;
    if (std::strcmp (key, "convolve.law") == 0)
        return 2;
    if (std::strstr (key, ".unison.law") != nullptr)
        return 3;
    if (std::strcmp (key, "reverb.type") == 0)
        return 4;
    if (std::strcmp (key, "compressor.limit") == 0)
        return 5;
    return 0;
}
}

/** The name of choice `index` of the parameter `key` as the screen writes it; `engineName` (the name in the engine's list) for every choice that needs no other name. */
inline const char* shownChoice (const char* key, int index, const char* engineName) noexcept
{
    switch (choicenames::lawOf (key))
    {
        case 1: return index == 0 ? "State variable" : (index == 1 ? "Biquad" : engineName);
        case 2: return index == 0 ? "Relative" : (index == 1 ? "Seconds" : engineName);
        case 3: return index == 0 ? "Equal power" : (index == 1 ? "Fixed gain" : engineName);
        case 4: return index == 0 ? "Dense" : engineName;
        case 5: return index == 2 ? "Peak" : engineName;
        default: return engineName != nullptr ? engineName : "---";
    }
}

/** The label of the parameter where it differs from the engine's name (capitals, at most 15 characters); nullptr: none, the page derives it from the engine's name. */
inline const char* shownLabel (const char* key) noexcept
{
    switch (choicenames::lawOf (key))
    {
        case 1: return "BAND SHAPE";
        case 2: return "SCALING";
        case 3: return "VOICE LAW";
        case 4: return "ALGORITHM";
        default: return nullptr;
    }
}

}} // namespace synthyboy_mpc::ui
