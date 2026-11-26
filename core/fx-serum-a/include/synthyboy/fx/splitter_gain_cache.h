// SynthyBoy - exact-bits memo of dbToGain () for the splitters' gain / level parameters.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstring>

#include "synthyboy/fx/dsp_utils.h"

namespace synthyboy::fx
{
/** One memo slot of dbToGain (). The result is a pure function of the float, so a hit on the exact
    bit pattern returns the very same float that dbToGain () would compute (NaN payloads and -0
    included, which compare by bits, not by value). Exactly 0 dB, either sign, is answered without
    the pow: C99 Annex F makes pow (x, +-0) exactly 1 for every x, and the test compares it with
    dbToGain () on this libm. */
class DbGainCache
{
public:
    float get (float db) noexcept
    {
        std::uint32_t bits;
        std::memcpy (&bits, &db, sizeof bits);
        if (valid_ && bits == bits_)
            return gain_;
        if (db == 0.0f)
            return 1.0f; // not memoised: the compare above is already cheaper than a miss path
        bits_ = bits;
        gain_ = dbToGain (db);
        valid_ = true;
        return gain_;
    }

private:
    std::uint32_t bits_ = 0;
    float gain_ = 0.0f;
    bool valid_ = false;
};

} // namespace synthyboy::fx
