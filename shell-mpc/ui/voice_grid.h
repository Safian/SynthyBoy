// Voice sequence: the rows of the step grid and the per-row display formatting of the host readouts (plain functions: the host skin owns the step selectors, dials and readouts).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace synthyboy_mpc { namespace ui {

namespace voicegrid
{
constexpr int kNumRows = 6, kNumSteps = 8;
inline const char* rowName (int r) noexcept
{
    static const char* const kNames[kNumRows] = { "PAN", "DETUNE", "CUTOFF", "ENV", "MOD 1", "MOD 2" };
    return r >= 0 && r < kNumRows ? kNames[r] : "";
}
/** The text of a row's value (val -1 .. +1): PAN L / R, DETUNE in cents, the others in percent. */
inline void formatValue (int row, float val, char* out, size_t maxLen) noexcept
{
    if (std::fabs (val) < 0.005f)
    {
        if (row == 0) std::snprintf (out, maxLen, "MID");
        else if (row == 1) std::snprintf (out, maxLen, "0 c");
        else std::snprintf (out, maxLen, "0%%");
        return;
    }
    if (row == 0) // PAN: %
    {
        int p = static_cast<int> (std::lround (val * 100.0f));
        if (p > 0) std::snprintf (out, maxLen, "%dR", p);
        else std::snprintf (out, maxLen, "%dL", -p);
    }
    else if (row == 1) // DETUNE: cents
    {
        int c = static_cast<int> (std::lround (val * 100.0f));
        std::snprintf (out, maxLen, "%+d c", c);
    }
    else // CUTOFF, ENV, MOD1, MOD2: %
    {
        int p = static_cast<int> (std::lround (val * 100.0f));
        std::snprintf (out, maxLen, "%+d%%", p);
    }
}
}

}} // namespace synthyboy_mpc::ui
