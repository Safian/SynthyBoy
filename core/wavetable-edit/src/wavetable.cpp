// SynthyBoy - wavetable-edit: Wavetable data model implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace synthyboy::wtedit
{
Wavetable::Wavetable()
{
    initDefault();
}

Wavetable::Wavetable(const std::string& name)
    : name_(name)
{
    initDefault();
}

const Frame& Wavetable::getFrame(size_t index) const
{
    if (index >= frames_.size())
        throw std::out_of_range("Wavetable frame index out of range");
    return frames_[index];
}

Frame& Wavetable::getFrame(size_t index)
{
    if (index >= frames_.size())
        throw std::out_of_range("Wavetable frame index out of range");
    return frames_[index];
}

bool Wavetable::setFrame(size_t index, const Frame& frame)
{
    if (index >= frames_.size())
        return false;
    frames_[index] = frame;
    return true;
}

bool Wavetable::insertFrame(size_t index, const Frame& frame)
{
    if (frames_.size() >= kMaxFrames || index > frames_.size())
        return false;
    frames_.insert(frames_.begin() + static_cast<std::ptrdiff_t>(index), frame);
    return true;
}

bool Wavetable::duplicateFrame(size_t index)
{
    if (frames_.size() >= kMaxFrames || index >= frames_.size())
        return false;
    Frame copy = frames_[index];
    frames_.insert(frames_.begin() + static_cast<std::ptrdiff_t>(index + 1), copy);
    return true;
}

bool Wavetable::removeFrame(size_t index)
{
    if (frames_.size() <= 1 || index >= frames_.size())
        return false;
    frames_.erase(frames_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool Wavetable::moveFrame(size_t fromIndex, size_t toIndex)
{
    if (fromIndex >= frames_.size() || toIndex >= frames_.size() || fromIndex == toIndex)
        return false;
    Frame f = frames_[fromIndex];
    frames_.erase(frames_.begin() + static_cast<std::ptrdiff_t>(fromIndex));
    frames_.insert(frames_.begin() + static_cast<std::ptrdiff_t>(toIndex), f);
    return true;
}

void Wavetable::initDefault()
{
    frames_.clear();
    Frame sawFrame;
    // Standard sawtooth waveform from 1.0 down to -1.0
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(kWaveformSize);
        sawFrame[i] = static_cast<float>(1.0 - 2.0 * t);
    }
    frames_.push_back(sawFrame);
}

void Wavetable::clear()
{
    frames_.clear();
    Frame silentFrame;
    frames_.push_back(silentFrame);
}

} // namespace synthyboy::wtedit
