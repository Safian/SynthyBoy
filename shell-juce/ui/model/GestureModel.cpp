// SynthyBoy - UI view-model: gesture maths (see GestureModel.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "GestureModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace synthyboy::ui
{
// ---- Viewport -----------------------------------------------------------------------------------------------------------------------

void Viewport::clampWindow() noexcept
{
    span = std::min (1.0, std::max (kMinSpan, span));
    start = std::min (1.0 - span, std::max (0.0, start));
}

void Viewport::zoomAbout (double anchor, double factor) noexcept
{
    if (! (factor > 0.0))
        return;
    anchor = std::min (1.0, std::max (0.0, anchor));
    const double screen = toScreen (anchor);
    span /= factor;
    span = std::min (1.0, std::max (kMinSpan, span));
    start = anchor - screen * span;
    clampWindow();
}

void Viewport::pan (double deltaNorm) noexcept
{
    start += deltaNorm;
    clampWindow();
}

// ---- slices -------------------------------------------------------------------------------------------------------------------------

int nearestSlice (const std::vector<float>& slices, float pos, float tolerance) noexcept
{
    int best = -1;
    float bestDist = tolerance;
    for (size_t i = 0; i < slices.size(); ++i)
    {
        const float d = std::fabs (slices[i] - pos);
        if (d <= bestDist)
        {
            bestDist = d;
            best = static_cast<int> (i);
        }
    }
    return best;
}

bool addSlice (std::vector<float>& slices, float pos)
{
    if (! (pos >= 0.0f && pos <= 1.0f) || static_cast<int> (slices.size()) >= kMaxSlices)
        return false;
    for (const float s : slices)
        if (std::fabs (s - pos) < kMinSliceGap)
            return false;
    slices.insert (std::upper_bound (slices.begin(), slices.end(), pos), pos);
    return true;
}

bool removeSlice (std::vector<float>& slices, int index)
{
    if (index < 0 || index >= static_cast<int> (slices.size()))
        return false;
    slices.erase (slices.begin() + index);
    return true;
}

bool moveSlice (std::vector<float>& slices, int index, float pos)
{
    if (index < 0 || index >= static_cast<int> (slices.size()))
        return false;
    const size_t i = static_cast<size_t> (index);
    const float lo = i == 0 ? 0.0f : slices[i - 1] + kMinSliceGap;
    const float hi = i + 1 >= slices.size() ? 1.0f : slices[i + 1] - kMinSliceGap;
    slices[i] = std::min (hi, std::max (lo, pos));
    return true;
}

float thresholdToLine (float percent) noexcept
{
    return 1.0f - std::min (100.0f, std::max (0.0f, percent)) / 100.0f;
}

float lineToThreshold (float lineFraction) noexcept
{
    return (1.0f - std::min (1.0f, std::max (0.0f, lineFraction))) * 100.0f;
}

// ---- loop pair ----------------------------------------------------------------------------------------------------------------------

void moveLoopPair (float& loopStart, float& loopEnd, float delta) noexcept
{
    const float lo = std::min (loopStart, loopEnd), hi = std::max (loopStart, loopEnd);
    delta = std::min (1.0f - hi, std::max (-lo, delta));
    loopStart += delta;
    loopEnd += delta;
}

bool betweenLoopMarkers (float x, float loopStart, float loopEnd, float tolerance) noexcept
{
    const float lo = std::min (loopStart, loopEnd), hi = std::max (loopStart, loopEnd);
    return x > lo + tolerance && x < hi - tolerance;
}

// ---- transpose ----------------------------------------------------------------------------------------------------------------------

TransposeSplit splitTranspose (int semitones) noexcept
{
    semitones = std::min (48, std::max (-48, semitones));
    const int oct = static_cast<int> (std::floor (static_cast<double> (semitones + 6) / 12.0));
    return { oct, semitones - 12 * oct };
}

int joinTranspose (int oct, int sem) noexcept
{
    return std::min (48, std::max (-48, 12 * oct + sem));
}

// ---- grouped choice lists -----------------------------------------------------------------------------------------------------------

std::vector<ChoiceGroup> warpTypeGroups()
{
    // oscN.warp.type: None Sync Formant Quantize Bend Squeeze Pulse "FM from Osc 2" "FM from Osc 3" "RM from Osc 2" "RM from Osc 3"
    return {
        { "Basic", { 0, 1, 2, 3, 5, 6 }, false },
        { "Alt", { 4 }, false },
        { "Filter", {}, true },
        { "Distortion", {}, true },
        { "FM", { 7, 8 }, false },
        { "PD", {}, true },
        { "AM", {}, true },
        { "RM", { 9, 10 }, false },
    };
}

// ---- TouchTracker -------------------------------------------------------------------------------------------------------------------

namespace
{
float dist (TouchPoint a, TouchPoint b) noexcept
{
    return std::hypot (a.x - b.x, a.y - b.y);
}
TouchPoint mid (TouchPoint a, TouchPoint b) noexcept
{
    return { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
}
} // namespace

void TouchTracker::reset() noexcept
{
    touches[0] = {};
    touches[1] = {};
    count = 0;
    pairMoved = pairWasPinch = pairTapPending = false;
}

TouchTracker::Gesture TouchTracker::feed (Phase phase, int touchId, TouchPoint pos, int64_t timeMs)
{
    Gesture out;
    int slot = -1;
    for (int i = 0; i < 2; ++i)
        if (touches[i].active && touches[i].id == touchId)
            slot = i;

    if (phase == Phase::down)
    {
        if (slot >= 0)
            return out;
        for (int i = 0; i < 2 && slot < 0; ++i)
            if (! touches[i].active)
                slot = i;
        if (slot < 0)
            return out; // a third finger is ignored
        touches[slot] = { touchId, pos, pos, true };
        ++count;
        if (count == 2)
        {
            pairDownMs = timeMs;
            pairMoved = false;
            pairWasPinch = false;
            pairTapPending = true;
            pendingUpdates = 0;
            pairStartCentre = pairLastCentre = mid (touches[0].now, touches[1].now);
            pairStartDistance = pairLastDistance = dist (touches[0].now, touches[1].now);
        }
        return out;
    }

    if (slot < 0)
        return out;

    if (phase == Phase::drag)
    {
        touches[slot].now = pos;
        if (count != 2)
            return out;
        // touch events arrive one finger at a time: judge the pair only after two updates, or the distance jitters between them
        if (++pendingUpdates < 2)
            return out;
        pendingUpdates = 0;
        const TouchPoint c = mid (touches[0].now, touches[1].now);
        const float d = dist (touches[0].now, touches[1].now);
        if (dist (touches[0].down, touches[0].now) > kTapSlopPx || dist (touches[1].down, touches[1].now) > kTapSlopPx)
            pairMoved = true;
        const float dCentre = std::hypot (c.x - pairLastCentre.x, c.y - pairLastCentre.y);
        const float dDist = std::fabs (d - pairLastDistance);
        if (dDist > 0.5f && dDist >= dCentre && pairLastDistance > 1.0f)
        {
            out.kind = Kind::pinch;
            out.scale = d / pairLastDistance;
            out.centre = c;
            pairWasPinch = true;
        }
        else if (dCentre > 0.5f)
        {
            out.kind = Kind::pan;
            out.dx = c.x - pairLastCentre.x;
            out.dy = c.y - pairLastCentre.y;
            out.centre = c;
        }
        pairLastCentre = c;
        pairLastDistance = d;
        return out;
    }

    // up
    if (count == 2 && pairTapPending)
    {
        pairTapPending = false;
        const TouchPoint c = mid (touches[0].now, touches[1].now);
        if (! pairMoved && timeMs - pairDownMs <= kTapMaxMs)
        {
            out.kind = Kind::twoFingerTap;
            out.centre = c;
        }
        else if (! pairWasPinch && std::fabs (c.x - pairStartCentre.x) >= kSwipeMinPx
                 && std::fabs (c.x - pairStartCentre.x) > 2.0f * std::fabs (c.y - pairStartCentre.y)
                 && std::fabs (dist (touches[0].now, touches[1].now) - pairStartDistance) < kSwipeMinPx)
        {
            out.kind = Kind::twoFingerSwipe;
            out.dx = c.x - pairStartCentre.x;
            out.dy = c.y - pairStartCentre.y;
            out.centre = c;
        }
    }
    touches[slot] = {};
    --count;
    return out;
}

// ---- parsers ------------------------------------------------------------------------------------------------------------------------

bool parseNumber (std::string_view text, double& value)
{
    std::string s (text);
    s.erase (std::remove_if (s.begin(), s.end(), [] (unsigned char ch) { return std::isspace (ch) != 0; }), s.end());
    if (s.empty())
        return false;
    char* end = nullptr;
    const double v = std::strtod (s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || ! std::isfinite (v))
        return false;
    value = v;
    return true;
}

bool parseBinAmplitude (std::string_view text, float& amplitude01)
{
    std::string s (text);
    while (! s.empty() && (s.back() == '%' || s.back() == ' '))
        s.pop_back();
    double v = 0.0;
    if (! parseNumber (s, v) || v < 0.0 || v > 100.0)
        return false;
    amplitude01 = static_cast<float> (v / 100.0);
    return true;
}

bool parseBinPhaseDegrees (std::string_view text, float& degrees)
{
    std::string s (text);
    while (! s.empty() && (s.back() == ' ' || s.back() == 'd'))
        s.pop_back();
    if (s.size() >= 2 && static_cast<unsigned char> (s[s.size() - 2]) == 0xC2 && static_cast<unsigned char> (s.back()) == 0xB0)
        s.resize (s.size() - 2); // UTF-8 degree sign
    double v = 0.0;
    if (! parseNumber (s, v))
        return false;
    v = std::fmod (v, 360.0);
    if (v < 0.0)
        v += 360.0;
    degrees = static_cast<float> (v);
    return true;
}
} // namespace synthyboy::ui
