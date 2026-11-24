// SynthyBoy - core/tuning: Scala .kbm keyboard mapping parser and serializer.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <iomanip>
#include <sstream>

#include "synthyboy/tuning.h"
#include "tuning_internal.h"

namespace synthyboy
{
namespace tuning
{
namespace
{
constexpr char kKbmComment = '!';
constexpr int kKbmHeaderCount = 7;
} // namespace

ParseResult parseKbm (const char* text, size_t size, Tuning& out) noexcept
{
    if (text == nullptr || size == 0)
        return { ParseError::emptyInput, 0, "Input is empty" };

    if (size > kMaxTuningFileSize)
        return { ParseError::tooLarge, 0, "Input exceeds maximum size limit" };

    const auto lines = detail::splitLines (text, size);
    if (lines.empty())
        return { ParseError::emptyInput, 0, "Input contains no lines" };

    KeyboardMapping mapping;
    int headerIndex = 0;
    size_t lineNum = 0;

    for (const auto& rawLine : lines)
    {
        ++lineNum;
        const std::string line = detail::trim (rawLine);
        if (line.empty() || line[0] == kKbmComment)
            continue;

        const auto tokens = detail::splitTokens (line);
        if (tokens.empty())
            continue;

        const std::string& token = tokens[0];

        if (headerIndex < kKbmHeaderCount)
        {
            switch (headerIndex)
            {
                case 0: // map size
                {
                    if (! detail::parseIntLocaleClassic (token, mapping.mapSize))
                        return { ParseError::invalidHeader, lineNum, "Invalid map size" };
                    if (mapping.mapSize < 0 || mapping.mapSize > kMaxMappingSize)
                        return { ParseError::invalidMapping, lineNum, "Map size out of range" };
                    break;
                }
                case 1: // first midi
                {
                    if (! detail::parseIntLocaleClassic (token, mapping.firstMidi))
                        return { ParseError::invalidHeader, lineNum, "Invalid first MIDI note" };
                    if (mapping.firstMidi < 0 || mapping.firstMidi > 127)
                        return { ParseError::invalidMapping, lineNum, "First MIDI note out of range" };
                    break;
                }
                case 2: // last midi
                {
                    if (! detail::parseIntLocaleClassic (token, mapping.lastMidi))
                        return { ParseError::invalidHeader, lineNum, "Invalid last MIDI note" };
                    if (mapping.lastMidi < 0 || mapping.lastMidi > 127)
                        return { ParseError::invalidMapping, lineNum, "Last MIDI note out of range" };
                    break;
                }
                case 3: // middle note
                {
                    if (! detail::parseIntLocaleClassic (token, mapping.middleNote))
                        return { ParseError::invalidHeader, lineNum, "Invalid middle note" };
                    if (mapping.middleNote < 0 || mapping.middleNote > 127)
                        return { ParseError::invalidMapping, lineNum, "Middle note out of range" };
                    break;
                }
                case 4: // reference note
                {
                    if (! detail::parseIntLocaleClassic (token, mapping.referenceNote))
                        return { ParseError::invalidHeader, lineNum, "Invalid reference note" };
                    if (mapping.referenceNote < 0 || mapping.referenceNote > 127)
                        return { ParseError::invalidMapping, lineNum, "Reference note out of range" };
                    break;
                }
                case 5: // reference frequency
                {
                    if (! detail::parseDoubleLocaleClassic (token, mapping.referenceFrequency))
                        return { ParseError::invalidHeader, lineNum, "Invalid reference frequency" };
                    if (mapping.referenceFrequency <= 0.0 || ! std::isfinite (mapping.referenceFrequency))
                        return { ParseError::invalidMapping, lineNum, "Reference frequency must be positive and finite" };
                    break;
                }
                case 6: // scale degree for reference note
                {
                    if (! detail::parseIntLocaleClassic (token, mapping.scaleDegreeForReference))
                        return { ParseError::invalidHeader, lineNum, "Invalid scale degree for reference note" };
                    break;
                }
            }
            ++headerIndex;
            if (headerIndex == kKbmHeaderCount && mapping.mapSize > 0)
                mapping.keys.reserve (static_cast<size_t> (mapping.mapSize));
        }
        else
        {
            // Reading mapping keys
            if (mapping.keys.size() >= static_cast<size_t> (mapping.mapSize))
                break;

            if (token == "x" || token == "X")
            {
                mapping.keys.push_back (kUnmappedKey);
            }
            else
            {
                int degree = 0;
                if (! detail::parseIntLocaleClassic (token, degree))
                    return { ParseError::invalidMapping, lineNum, "Invalid scale degree in mapping list" };
                mapping.keys.push_back (degree);
            }
        }
    }

    if (headerIndex < kKbmHeaderCount)
        return { ParseError::invalidHeader, lineNum, "Incomplete .kbm header (less than 7 lines)" };

    if (mapping.firstMidi > mapping.lastMidi)
        return { ParseError::invalidMapping, lineNum, "firstMidi > lastMidi" };

    if (mapping.mapSize > 0 && mapping.keys.size() < static_cast<size_t> (mapping.mapSize))
        return { ParseError::invalidMapping, lineNum, "Fewer mapping lines than map size" };

    Tuning temp = out;
    temp.setMapping (mapping);
    out = std::move (temp);
    return { ParseError::ok, 0, "Success" };
}

ParseResult Tuning::loadKbm (const char* text, size_t size) noexcept
{
    return parseKbm (text, size, *this);
}

std::string Tuning::toKbm() const
{
    std::ostringstream ss;
    ss.imbue (std::locale::classic());

    ss << "! Generated by SynthyBoy\n";
    ss << "!\n";
    ss << mapping_.mapSize << "\n";
    ss << mapping_.firstMidi << "\n";
    ss << mapping_.lastMidi << "\n";
    ss << mapping_.middleNote << "\n";
    ss << mapping_.referenceNote << "\n";
    ss << std::fixed << std::setprecision (6) << mapping_.referenceFrequency << "\n";
    ss << mapping_.scaleDegreeForReference << "\n";

    if (mapping_.mapSize > 0)
    {
        ss << "! Key mapping entries\n";
        for (int deg : mapping_.keys)
        {
            if (deg == kUnmappedKey)
                ss << "x\n";
            else
                ss << deg << "\n";
        }
    }

    return ss.str();
}

} // namespace tuning
} // namespace synthyboy
