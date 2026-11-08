// SynthyBoy - core/tuning: AnaMark .tun tuning parser and serializer.
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
constexpr char kTunComment = ';';

enum class TunSection
{
    none,
    tuning,
    exactTuning,
    functionalTuning,
    other
};

} // namespace

ParseResult parseTun (const char* text, size_t size, Tuning& out) noexcept
{
    if (text == nullptr || size == 0)
        return { ParseError::emptyInput, 0, "Input is empty" };

    if (size > kMaxTuningFileSize)
        return { ParseError::tooLarge, 0, "Input exceeds maximum size limit" };

    const auto lines = detail::splitLines (text, size);
    if (lines.empty())
        return { ParseError::emptyInput, 0, "Input contains no lines" };

    TunSection section = TunSection::none;
    double baseFreq = kDefaultMidi0Freq;
    bool hasBaseFreq = false;

    std::array<double, kNumKeys> tuningCents {};
    std::array<bool, kNumKeys> hasTuningCents {};

    std::array<double, kNumKeys> exactCents {};
    std::array<bool, kNumKeys> hasExactCents {};

    std::string tuningName = "AnaMark Tuning";
    size_t lineNum = 0;
    bool hasSeenValidSection = false;

    for (const auto& rawLine : lines)
    {
        ++lineNum;
        std::string line = detail::trim (rawLine);
        if (line.empty() || line[0] == kTunComment)
            continue;

        // Strip inline comments starting with ';'
        const size_t commentPos = line.find (kTunComment);
        if (commentPos != std::string::npos)
            line = detail::trim (line.substr (0, commentPos));

        if (line.empty())
            continue;

        if (line.front() == '[' && line.back() == ']')
        {
            const std::string secName = detail::toLower (detail::trim (line.substr (1, line.size() - 2)));
            if (secName == "tuning")
            {
                section = TunSection::tuning;
                hasSeenValidSection = true;
            }
            else if (secName == "exact tuning")
            {
                section = TunSection::exactTuning;
                hasSeenValidSection = true;
            }
            else if (secName == "functional tuning" || secName == "scale tuning")
            {
                section = TunSection::functionalTuning;
                hasSeenValidSection = true;
            }
            else
            {
                section = TunSection::other;
            }
            continue;
        }

        if (section == TunSection::tuning || section == TunSection::exactTuning)
        {
            const size_t eqPos = line.find ('=');
            if (eqPos == std::string::npos)
                return { ParseError::invalidTunEntry, lineNum, "Missing '=' in assignment" };

            const std::string key = detail::toLower (detail::trim (line.substr (0, eqPos)));
            const std::string valStr = detail::trim (line.substr (eqPos + 1));

            if (key == "basefreq")
            {
                double bf = 0.0;
                if (! detail::parseDoubleLocaleClassic (valStr, bf) || bf <= 0.0 || ! std::isfinite (bf))
                    return { ParseError::invalidTunEntry, lineNum, "Invalid BaseFreq value" };
                baseFreq = bf;
                hasBaseFreq = true;
            }
            else if (key.rfind ("note", 0) == 0) // starts with "note"
            {
                const auto tokens = detail::splitTokens (key);
                if (tokens.size() < 2)
                    return { ParseError::invalidTunEntry, lineNum, "Malformed note key" };

                int noteIdx = -1;
                if (! detail::parseIntLocaleClassic (tokens[1], noteIdx) || noteIdx < 0 || noteIdx >= kNumKeys)
                    return { ParseError::invalidTunEntry, lineNum, "Note index out of 0..127 range" };

                double cents = 0.0;
                if (! detail::parseDoubleLocaleClassic (valStr, cents) || ! std::isfinite (cents))
                    return { ParseError::invalidTunEntry, lineNum, "Invalid cents value in note assignment" };

                if (section == TunSection::tuning)
                {
                    tuningCents[noteIdx] = cents;
                    hasTuningCents[noteIdx] = true;
                }
                else
                {
                    exactCents[noteIdx] = cents;
                    hasExactCents[noteIdx] = true;
                }
            }
        }
    }

    if (! hasSeenValidSection)
        return { ParseError::invalidTunSection, lineNum, "No valid [Tuning] or [Exact Tuning] section found" };

    // Check that at least some notes were defined
    int notesCount = 0;
    for (int k = 0; k < kNumKeys; ++k)
    {
        if (hasExactCents[k] || hasTuningCents[k])
            ++notesCount;
    }

    if (notesCount == 0 && ! hasBaseFreq)
        return { ParseError::invalidTunEntry, lineNum, "No tuning data or notes parsed" };

    Table table {};
    for (int k = 0; k < kNumKeys; ++k)
    {
        double cents = static_cast<double> (k * 100);
        if (hasExactCents[k])
            cents = exactCents[k];
        else if (hasTuningCents[k])
            cents = tuningCents[k];

        const double freq = baseFreq * std::pow (2.0, cents / 1200.0);
        table[k] = (freq > 0.0 && std::isfinite (freq)) ? freq : 0.0;
    }

    Tuning temp = out;
    temp.setDirectFrequencies (table, tuningName);
    out = std::move (temp);
    return { ParseError::ok, 0, "Success" };
}

ParseResult Tuning::loadTun (const char* text, size_t size) noexcept
{
    return parseTun (text, size, *this);
}

std::string Tuning::toTun() const
{
    std::ostringstream ss;
    ss.imbue (std::locale::classic());

    ss << "; AnaMark TUN file generated by SynthyBoy\n";
    ss << "[Exact Tuning]\n";

    // Use frequency of key 0 as baseFreq, fallback to kDefaultMidi0Freq if 0
    const double baseFreq = (table_[0] > 0.0) ? table_[0] : kDefaultMidi0Freq;
    ss << "BaseFreq = " << std::fixed << std::setprecision (16) << baseFreq << "\n";

    for (int k = 0; k < kNumKeys; ++k)
    {
        double cents = 0.0;
        if (table_[k] > 0.0 && baseFreq > 0.0)
            cents = 1200.0 * std::log2 (table_[k] / baseFreq);
        else
            cents = k * 100.0;

        ss << "note " << k << " = " << std::fixed << std::setprecision (12) << cents << "\n";
    }

    return ss.str();
}

} // namespace tuning
} // namespace synthyboy
