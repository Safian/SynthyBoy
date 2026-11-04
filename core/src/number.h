// SynthyBoy - core: locale independent, allocation free number formatting and parsing (internal header).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>

namespace synthyboy
{
namespace number
{
/** "%.9g" of a float, exactly rounded (half to even on the exact binary value), locale independent.
    -0 prints as "0"; NaN/inf are printed as "0" (callers sanitise before). Zero-terminated, returns the length written
    (truncated to cap - 1). 9 significant digits are enough to round-trip every float. */
size_t formatG9 (float value, char* buf, size_t cap) noexcept;

/** "%.1f" of a float, exactly rounded (half to even), locale independent. A result of zero has no sign. */
size_t formatFixed1 (float value, char* buf, size_t cap) noexcept;

/** Parses a decimal number starting at begin (no leading white space skipped). strictJson selects the JSON number
    grammar; otherwise a sign, ".5" / "5." and a trailing garbage tail are accepted. On success returns true, sets
    value (+-inf on overflow, never NaN) and next (first unused character). */
bool parseDecimal (const char* begin, const char* end, bool strictJson, double& value, const char*& next) noexcept;
} // namespace number
} // namespace synthyboy
