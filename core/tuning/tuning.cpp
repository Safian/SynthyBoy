// SynthyBoy - core/tuning: microtonal tuning representation and table calculation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/tuning.h"

#include <cmath>
#include <iomanip>
#include <sstream>

#include "tuning_internal.h"

namespace synthyboy
{
namespace tuning
{
namespace
{
inline int floorDiv (int a, int b) noexcept
{
    int q = a / b;
    int r = a % b;
    if (r != 0 && ((r < 0) ^ (b < 0)))
        --q;
    return q;
}

inline int floorMod (int a, int b) noexcept
{
    int r = a % b;
    if (r != 0 && ((r < 0) ^ (b < 0)))
        r += b;
    return r;
}
} // namespace

KeyboardMapping KeyboardMapping::defaultMapping (int scaleSize) noexcept
{
    KeyboardMapping m;
    m.mapSize = 0;
    m.firstMidi = 0;
    m.lastMidi = 127;
    m.middleNote = 60;

    if (scaleSize == 12)
    {
        m.referenceNote = 69;
        m.referenceFrequency = kDefaultA4;
        m.scaleDegreeForReference = 9;
    }
    else
    {
        m.referenceNote = 60;
        // Frequency of Middle C in 12-TET A4=440: 440 * 2^(-9/12)
        m.referenceFrequency = kDefaultA4 * std::pow (2.0, -9.0 / 12.0);
        m.scaleDegreeForReference = 0;
    }

    return m;
}

bool KeyboardMapping::isValid() const noexcept
{
    if (firstMidi < 0 || firstMidi > 127)
        return false;
    if (lastMidi < 0 || lastMidi > 127)
        return false;
    if (firstMidi > lastMidi)
        return false;
    if (middleNote < 0 || middleNote > 127)
        return false;
    if (referenceNote < 0 || referenceNote > 127)
        return false;
    if (referenceFrequency <= 0.0 || ! std::isfinite (referenceFrequency))
        return false;
    if (mapSize < 0 || mapSize > kMaxMappingSize)
        return false;
    if (mapSize > 0 && keys.size() < static_cast<size_t> (mapSize))
        return false;

    return true;
}

Tuning::Tuning() noexcept
{
    setDefault();
}

void Tuning::setDefault() noexcept
{
    scale_.clear();
    scale_.reserve (12);
    for (int i = 1; i <= 12; ++i)
    {
        PitchEntry p;
        p.cents = i * 100.0;
        p.ratio = std::pow (2.0, i / 12.0);
        p.text = std::to_string (i * 100) + ".0";
        scale_.push_back (p);
    }

    mapping_ = KeyboardMapping::defaultMapping (12);
    name_ = "12-TET";
    description_ = "12-tone equal temperament";
    isDefault_ = true;
    isRawTun_ = false;

    for (int k = 0; k < kNumKeys; ++k)
        table_[k] = kDefaultA4 * std::pow (2.0, (k - 69) / 12.0);
}

double Tuning::frequencyForKey (int midiKey, double a4Hz) const noexcept
{
    if (midiKey < 0 || midiKey >= kNumKeys)
        return 0.0;

    const double baseFreq = table_[midiKey];
    if (baseFreq <= 0.0)
        return 0.0;

    if (std::abs (a4Hz - kDefaultA4) < 1e-12)
        return baseFreq;

    return baseFreq * (a4Hz / kDefaultA4);
}

Table Tuning::computeTable (double a4Hz) const noexcept
{
    Table res {};
    const double factor = (std::abs (a4Hz - kDefaultA4) < 1e-12) ? 1.0 : (a4Hz / kDefaultA4);
    for (int k = 0; k < kNumKeys; ++k)
    {
        const double f = table_[k];
        res[k] = (f > 0.0) ? (f * factor) : 0.0;
    }
    return res;
}

void Tuning::setMapping (const KeyboardMapping& m) noexcept
{
    if (m.isValid())
    {
        mapping_ = m;
        isDefault_ = false;
        isRawTun_ = false;
        recalculateTable();
    }
}

bool Tuning::setScale (const std::vector<PitchEntry>& pitches) noexcept
{
    if (pitches.empty() || pitches.size() > static_cast<size_t> (kMaxScaleNotes))
        return false;

    scale_ = pitches;
    isDefault_ = false;
    isRawTun_ = false;
    if (! mapping_.isValid())
        mapping_ = KeyboardMapping::defaultMapping (static_cast<int> (pitches.size()));

    recalculateTable();
    return true;
}

void Tuning::setDirectFrequencies (const Table& freqs, const std::string& n) noexcept
{
    table_ = freqs;
    name_ = n;
    description_ = n;
    isDefault_ = false;
    isRawTun_ = true;
}

void Tuning::recalculateTable() noexcept
{
    if (isRawTun_)
        return;

    const int n = static_cast<int> (scale_.size());
    if (n <= 0)
    {
        setDefault();
        return;
    }

    if (! mapping_.isValid())
        mapping_ = KeyboardMapping::defaultMapping (n);

    const double periodRatio = scale_[n - 1].ratio;
    if (periodRatio <= 0.0 || ! std::isfinite (periodRatio))
    {
        setDefault();
        return;
    }

    auto ratioForDegree = [&] (int deg) noexcept -> double
    {
        if (deg == 0)
            return 1.0;

        const int q = floorDiv (deg, n);
        const int r = floorMod (deg, n);

        const double baseRatio = (r == 0) ? 1.0 : scale_[r - 1].ratio;
        return std::pow (periodRatio, static_cast<double> (q)) * baseRatio;
    };

    const double refRatio = ratioForDegree (mapping_.scaleDegreeForReference);
    if (refRatio <= 0.0 || ! std::isfinite (refRatio))
    {
        setDefault();
        return;
    }

    for (int k = 0; k < kNumKeys; ++k)
    {
        if (k < mapping_.firstMidi || k > mapping_.lastMidi)
        {
            table_[k] = 0.0;
            continue;
        }

        int degree = 0;
        if (mapping_.mapSize == 0)
        {
            degree = k - mapping_.middleNote;
        }
        else
        {
            const int m = mapping_.mapSize;
            const int deltaKey = k - mapping_.middleNote;
            const int qM = floorDiv (deltaKey, m);
            const int rM = floorMod (deltaKey, m);

            const int degInCycle = mapping_.keys[rM];
            if (degInCycle == kUnmappedKey)
            {
                table_[k] = 0.0;
                continue;
            }

            degree = qM * n + degInCycle;
        }

        const double keyRatio = ratioForDegree (degree);
        const double freq = mapping_.referenceFrequency * (keyRatio / refRatio);
        table_[k] = (freq > 0.0 && std::isfinite (freq)) ? freq : 0.0;
    }
}

} // namespace tuning
} // namespace synthyboy
