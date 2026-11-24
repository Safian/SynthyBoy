// SynthyBoy - core/tuning: internal utilities for tuning parsers and formatting.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace synthyboy
{
namespace tuning
{
namespace detail
{
inline std::string trim (const std::string& s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace (static_cast<unsigned char> (s[begin])))
        ++begin;
    while (end > begin && std::isspace (static_cast<unsigned char> (s[end - 1])))
        --end;
    return s.substr (begin, end - begin);
}

inline std::string toLower (std::string s)
{
    for (char& c : s)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}

inline std::vector<std::string> splitTokens (const std::string& line, const char* delims = " \t\r\n")
{
    std::vector<std::string> tokens;
    std::string current;
    for (char c : line)
    {
        bool isDelim = false;
        for (const char* d = delims; *d != '\0'; ++d)
        {
            if (c == *d)
            {
                isDelim = true;
                break;
            }
        }
        if (isDelim)
        {
            if (! current.empty())
            {
                tokens.push_back (current);
                current.clear();
            }
        }
        else
        {
            current.push_back (c);
        }
    }
    if (! current.empty())
        tokens.push_back (current);
    return tokens;
}

inline std::vector<std::string> splitLines (const char* text, size_t size)
{
    std::vector<std::string> lines;
    if (text == nullptr || size == 0)
        return lines;

    const char* ptr = text;
    const char* end = text + size;
    std::string current;
    current.reserve (64);

    while (ptr < end)
    {
        char c = *ptr++;
        if (c == '\r')
        {
            if (ptr < end && *ptr == '\n')
                ++ptr; // consume \n of \r\n
            lines.push_back (current);
            current.clear();
        }
        else if (c == '\n')
        {
            lines.push_back (current);
            current.clear();
        }
        else
        {
            current.push_back (c);
        }
    }
    if (! current.empty())
        lines.push_back (current);

    return lines;
}

inline bool parseDoubleLocaleClassic (const std::string& str, double& out) noexcept
{
    std::string s = trim (str);
    if (s.empty())
        return false;

    std::istringstream stream (s);
    stream.imbue (std::locale::classic());
    double val = 0.0;
    if (! (stream >> val))
        return false;

    // Check if there are unparsed non-space characters remaining
    std::string rest;
    if (stream >> rest)
        return false;

    if (! std::isfinite (val))
        return false;

    out = val;
    return true;
}

inline bool parseIntLocaleClassic (const std::string& str, int& out) noexcept
{
    std::string s = trim (str);
    if (s.empty())
        return false;

    std::istringstream stream (s);
    stream.imbue (std::locale::classic());
    int val = 0;
    if (! (stream >> val))
        return false;

    std::string rest;
    if (stream >> rest)
        return false;

    out = val;
    return true;
}

} // namespace detail
} // namespace tuning
} // namespace synthyboy
