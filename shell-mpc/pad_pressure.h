// SynthyBoy - MPC shell: pad pressure to Filter 1 cutoff, the two controls and their place in the state chunk (pure functions, no state).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The MPC pad sends polyphonic aftertouch (0xA0, per note). The own engine turns it into a cutoff offset of Filter 1 per voice
// (synthyboy::setOwnPadPressureToCutoff, depth 0 .. 1 of 36 semitones). Two plugin parameters after the display parameters switch it and set the depth:
//   "Pad Pressure"         0 / 1 (display "Off" / "On"), default on
//   "Pad Pressure Amount"  0 .. 100 % (normalised value = percent / 100), default 35 % (about one octave of cutoff at full pressure)
// They are not part of a preset or of the engine's state: they are the player's setting for the pads. They are saved in the chunk as two members at the END of the
// chunk document ("mpcPadPressure":1,"mpcPadPressureAmount":35.00 just before the closing brace); a chunk without them (an older one) leaves the settings as they are.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace synthyboy_mpc
{
constexpr int kPadPressureParams = 2;          // "Pad Pressure", "Pad Pressure Amount"
constexpr bool kPadPressureDefaultOn = true;
constexpr float kPadPressureDefaultPercent = 35.0f;

inline float padPressureDepth (bool on, float percent) noexcept
{
    return on ? std::min (std::max (percent, 0.0f), 100.0f) * 0.01f : 0.0f;
}

/** Locale-independent JSON float parser (e.g. "35.00", "0", "100.0"). Accepts only '.' as decimal separator. */
inline bool parseJsonFloatLocaleIndependent (const char* str, size_t maxLen, float& outVal, size_t& charsRead) noexcept
{
    if (str == nullptr || maxLen == 0)
        return false;
    size_t p = 0;
    while (p < maxLen && (str[p] == ' ' || str[p] == '\t'))
        ++p;
    if (p >= maxLen)
        return false;
    bool neg = false;
    if (str[p] == '-')
    {
        neg = true;
        ++p;
    }
    else if (str[p] == '+')
    {
        ++p;
    }
    if (p >= maxLen)
        return false;
    bool hasDigits = false;
    double val = 0.0;
    while (p < maxLen && str[p] >= '0' && str[p] <= '9')
    {
        hasDigits = true;
        val = val * 10.0 + static_cast<double> (str[p] - '0');
        ++p;
    }
    if (p < maxLen && str[p] == '.')
    {
        ++p;
        double frac = 0.1;
        while (p < maxLen && str[p] >= '0' && str[p] <= '9')
        {
            hasDigits = true;
            val += static_cast<double> (str[p] - '0') * frac;
            frac *= 0.1;
            ++p;
        }
    }
    if (!hasDigits)
        return false;
    if (p < maxLen && (str[p] == 'e' || str[p] == 'E'))
    {
        size_t ep = p + 1;
        bool expNeg = false;
        if (ep < maxLen && str[ep] == '-') { expNeg = true; ++ep; }
        else if (ep < maxLen && str[ep] == '+') { ++ep; }
        int expVal = 0;
        bool expDigits = false;
        while (ep < maxLen && str[ep] >= '0' && str[ep] <= '9')
        {
            expDigits = true;
            expVal = expVal * 10 + (str[ep] - '0');
            ++ep;
        }
        if (!expDigits)
            return false;
        val *= std::pow (10.0, expNeg ? -expVal : expVal);
        p = ep;
    }
    charsRead = p;
    outVal = static_cast<float> (neg ? -val : val);
    return std::isfinite (outVal);
}

/** The chunk with the two members before the last closing brace (the program prefix is untouched).
    Uses integer division and modulo to guarantee a literal '.' decimal point regardless of the active C/C++ locale. */
inline std::string chunkWithPadPressure (std::string chunk, bool on, float percent)
{
    const size_t close = chunk.rfind ('}');
    if (close == std::string::npos)
        return chunk;
    const double clamped = std::min (std::max (static_cast<double> (percent), 0.0), 100.0);
    const int totalCent = static_cast<int> (std::round (clamped * 100.0));
    const int whole = totalCent / 100;
    const int frac = totalCent % 100;
    char buf[96];
    std::snprintf (buf, sizeof (buf), ",\"mpcPadPressure\":%d,\"mpcPadPressureAmount\":%d.%02d", on ? 1 : 0, whole, frac);
    chunk.insert (close, buf);
    return chunk;
}

/** Reads the two members from the tail of a chunk. True when both are there (then `on` and `percent` are set); false leaves them as they are. Never reads outside `size`. */
inline bool padPressureFromChunk (const void* data, size_t size, bool& on, float& percent) noexcept
{
    if (data == nullptr || size < 40)
        return false;
    const size_t tailLen = std::min<size_t> (size, 160);
    const std::string tail (static_cast<const char*> (data) + (size - tailLen), tailLen);
    static const char kOn[] = "\"mpcPadPressure\":";
    static const char kAmount[] = ",\"mpcPadPressureAmount\":";
    const size_t a = tail.rfind (kOn);
    if (a == std::string::npos)
        return false;
    size_t p = a + sizeof (kOn) - 1;
    if (p >= tail.size() || (tail[p] != '0' && tail[p] != '1'))
        return false;
    const bool newOn = tail[p] == '1';
    ++p;
    if (tail.compare (p, sizeof (kAmount) - 1, kAmount) != 0)
        return false;
    p += sizeof (kAmount) - 1;
    float v = 0.0f;
    size_t charsConsumed = 0;
    if (!parseJsonFloatLocaleIndependent (tail.data() + p, tail.size() - p, v, charsConsumed) || charsConsumed == 0)
        return false;
    on = newOn;
    percent = std::min (std::max (v, 0.0f), 100.0f);
    return true;
}
} // namespace synthyboy_mpc
