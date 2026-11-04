// SynthyBoy - engine-vital: replacement for the JuceHeader.h that the imported Vital sources include.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The imported DSP (core/engine-vital/upstream, see docs/UPSTREAM.md) only used JUCE for a leak-detector macro and
// the standard headers JuceHeader.h pulled in. This header provides exactly that, with no JUCE code at all.
#pragma once

#if defined (_MSC_VER)
 // must come before <cmath>: M_PI and friends, and no min/max macros from <windows.h>.
 #ifndef _USE_MATH_DEFINES
  #define _USE_MATH_DEFINES
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cfloat>
#include <climits>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// JUCE_LEAK_DETECTOR (x): debug-only instance counter in JUCE; nothing here.
#define JUCE_LEAK_DETECTOR(className)

// JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (x): deleted copy operations (plus the counter in JUCE).
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(className) \
    className (const className&) = delete;                      \
    className& operator= (const className&) = delete;

// Version tag written into wavetable JSON (WavetableCreator::stateToJson). Value of the Projucer project the imported
// sources came from; nothing reads it for migration any more (see docs/UPSTREAM.md).
namespace ProjectInfo
{
constexpr const char* const versionString = "1.0.6";
}

// Base64 for the embedded binary blocks of the JSON state (wave frames, samples). Standard RFC 4648 alphabet with
// padding. Not compatible with JUCE's Base64 class (different alphabet), which is intended: Vital's own files are not read.
namespace sbcompat
{
inline std::string toBase64 (const void* data, size_t size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* in = static_cast<const unsigned char*> (data);
    std::string out;
    out.reserve ((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3)
    {
        const size_t n = std::min<size_t> (3, size - i);
        const uint32_t v = (static_cast<uint32_t> (in[i]) << 16) | (n > 1 ? static_cast<uint32_t> (in[i + 1]) << 8 : 0u)
                         | (n > 2 ? static_cast<uint32_t> (in[i + 2]) : 0u);
        out.push_back (alphabet[(v >> 18) & 63]);
        out.push_back (alphabet[(v >> 12) & 63]);
        out.push_back (n > 1 ? alphabet[(v >> 6) & 63] : '=');
        out.push_back (n > 2 ? alphabet[v & 63] : '=');
    }
    return out;
}

/** Decodes standard base64 (padding optional, whitespace ignored). Returns false on an invalid character. */
inline bool fromBase64 (const std::string& text, std::vector<unsigned char>& out)
{
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : text)
    {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t> (v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back (static_cast<unsigned char> ((acc >> bits) & 0xff));
        }
    }
    return true;
}
} // namespace sbcompat
