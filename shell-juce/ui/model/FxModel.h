// SynthyBoy - UI view-model: the pure parts of the FX extras of block B17 (docs/ui-design/13-new-keys.md 4.16): the tap tempo of the delay,
// the offset snap points, the edit operations of the X-Shaper transfer curves and the crossover scrubbing of the compressor.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pure C++17 on top of synthyboy_core: no JUCE, unit tested headless (shell-juce/ui/tests/test_ui_model.cpp).
#pragma once

#include <cmath>
#include <vector>

#include "synthyboy/content_state.h"

namespace synthyboy::ui
{
/** Tap->Delay (G74): the interval of the last taps is the delay time. A tap more than kResetMs after the previous one starts a new series;
    two taps are enough for an interval, up to the last four intervals are averaged (the median of the three newest and the oldest is not
    needed: a series is short). Intervals outside kMinMs .. kMaxMs are rejected (the delay time is 2 .. 4000 ms; 60 ms is a 1000 BPM tap). */
class TapTempo
{
public:
    static constexpr double kResetMs = 2000.0;
    static constexpr double kMinMs = 60.0;
    static constexpr double kMaxMs = 2000.0;
    static constexpr int kMaxIntervals = 4;

    /** A tap at `nowMs` (any monotonic clock). True once an interval is available (from the second tap of a series on). */
    bool tap (double nowMs) noexcept
    {
        if (taps > 0)
        {
            const double d = nowMs - lastMs;
            if (d > kResetMs || d < kMinMs)
            {
                restartAt (nowMs);
                return false;
            }
            intervals[next] = d;
            next = (next + 1) % kMaxIntervals;
            if (count < kMaxIntervals)
                ++count;
        }
        lastMs = nowMs;
        ++taps;
        return count > 0;
    }

    bool hasInterval() const noexcept { return count > 0; }
    /** The average of the intervals of the series, 0 before the second tap. */
    double intervalMs() const noexcept
    {
        if (count == 0)
            return 0.0;
        double sum = 0.0;
        for (int i = 0; i < count; ++i)
            sum += intervals[i];
        return sum / count;
    }
    double bpm() const noexcept { return count > 0 ? 60000.0 / intervalMs() : 0.0; }
    int tapsInSeries() const noexcept { return taps; }
    void reset() noexcept { taps = 0; count = 0; next = 0; }

private:
    void restartAt (double nowMs) noexcept
    {
        reset();
        lastMs = nowMs;
        taps = 1;
    }

    double intervals[kMaxIntervals] = {};
    double lastMs = 0.0;
    int taps = 0, count = 0, next = 0;
};

/** The three snap points of delay.offset: 1 (as left), 4/3 (triplet), 3/2 (dotted). */
constexpr float kDelayOffsetSnap[3] = { 1.0f, 4.0f / 3.0f, 1.5f };
/** The snap point within `tolerance` (relative) of `offset`, or `offset` itself. */
inline float snapDelayOffset (float offset, float tolerance = 0.03f) noexcept
{
    for (const float p : kDelayOffsetSnap)
        if (std::fabs (offset - p) <= tolerance * p)
            return p;
    return offset;
}

/** Compressor crossover scrubbing: a drag of `dx` points moves the frequency by dx / pointsPerOctave octaves, kept inside [lo, hi]. */
inline float scrubCrossover (float startHz, float dx, float pointsPerOctave, float lo, float hi) noexcept
{
    const float octaves = pointsPerOctave > 0.0f ? dx / pointsPerOctave : 0.0f;
    const float hz = startHz * std::exp2 (octaves);
    return hz < lo ? lo : (hz > hi ? hi : hz);
}

// ---- the X-Shaper curves ---------------------------------------------------------------------------------------------------------------
// A curve is a list of ContentPoint: x = input magnitude 0..1, y = output magnitude 0..1, ascending x, the first x is 0 and the last 1,
// 2 .. kMaxCurvePoints points. An empty list is the identity (the engine's default). The editor works on the list, the host stores it.
constexpr float kCurveMinGap = 0.004f; // closest two points may get in x

/** a == b without -Wfloat-equal. */
inline bool sameFloat (float a, float b) noexcept
{
    return ! (a < b) && ! (b < a);
}

/** The two end points of the identity. */
inline std::vector<ContentPoint> identityCurve()
{
    return { ContentPoint (0.0f, 0.0f), ContentPoint (1.0f, 1.0f) };
}

/** The curve the engine uses for an empty list. */
inline std::vector<ContentPoint> curveOrIdentity (const std::vector<ContentPoint>& points)
{
    return points.size() >= 2 ? points : identityCurve();
}

/** True if the points are the identity (or empty): the editor then stores an empty list. */
inline bool isIdentityCurve (const std::vector<ContentPoint>& points) noexcept
{
    if (points.empty())
        return true;
    if (points.size() != 2)
        return false;
    return sameFloat (points[0].x, 0.0f) && sameFloat (points[0].y, 0.0f) && sameFloat (points[1].x, 1.0f) && sameFloat (points[1].y, 1.0f)
        && sameFloat (points[0].power, 0.0f);
}

/** Index of the point nearest to (x, y) within `radius` (plot units, x and y scaled by the aspect), -1 if none. */
inline int curveHit (const std::vector<ContentPoint>& points, float x, float y, float radiusX, float radiusY) noexcept
{
    int best = -1;
    float bestD = 1.0f;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const float dx = (points[i].x - x) / radiusX, dy = (points[i].y - y) / radiusY;
        const float d = dx * dx + dy * dy;
        if (d <= 1.0f && d < bestD)
        {
            bestD = d;
            best = static_cast<int> (i);
        }
    }
    return best;
}

/** Adds a point at (x, y) (clamped to 0..1) between its neighbours; false when the list is full or x is too close to a point. The index of the new point
    is returned through `index`. */
inline bool curveAddPoint (std::vector<ContentPoint>& points, float x, float y, int* index = nullptr, size_t maxPoints = static_cast<size_t> (kMaxCurvePoints))
{
    if (points.size() >= maxPoints || points.size() < 2)
        return false;
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
    size_t at = 1;
    while (at < points.size() && points[at].x < x)
        ++at;
    if (at >= points.size() || x - points[at - 1].x < kCurveMinGap || points[at].x - x < kCurveMinGap)
        return false;
    points.insert (points.begin() + static_cast<long> (at), ContentPoint (x, y));
    if (index != nullptr)
        *index = static_cast<int> (at);
    return true;
}

/** Moves a point: the end points keep their x (0 and 1), an inner point stays between its neighbours; y is clamped to 0..1. False if nothing changed. */
inline bool curveMovePoint (std::vector<ContentPoint>& points, int index, float x, float y) noexcept
{
    if (index < 0 || static_cast<size_t> (index) >= points.size())
        return false;
    auto& p = points[static_cast<size_t> (index)];
    const bool end = index == 0 || static_cast<size_t> (index) + 1 == points.size();
    if (! end)
    {
        const float lo = points[static_cast<size_t> (index) - 1].x + kCurveMinGap, hi = points[static_cast<size_t> (index) + 1].x - kCurveMinGap;
        x = x < lo ? lo : (x > hi ? hi : x);
    }
    else
    {
        x = p.x;
    }
    y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
    if (sameFloat (x, p.x) && sameFloat (y, p.y))
        return false;
    p.x = x;
    p.y = y;
    return true;
}

/** Removes an inner point (the two end points and a curve of two points stay). */
inline bool curveRemovePoint (std::vector<ContentPoint>& points, int index) noexcept
{
    if (index <= 0 || static_cast<size_t> (index) + 1 >= points.size() || points.size() <= 2)
        return false;
    points.erase (points.begin() + index);
    return true;
}

/** Sets the power (bend) of the segment that starts at `index` (-20 .. 20 is the stored range, the editor uses -8 .. 8). */
inline bool curveSetPower (std::vector<ContentPoint>& points, int index, float power) noexcept
{
    if (index < 0 || static_cast<size_t> (index) + 1 >= points.size())
        return false;
    power = power < -8.0f ? -8.0f : (power > 8.0f ? 8.0f : power);
    if (sameFloat (points[static_cast<size_t> (index)].power, power))
        return false;
    points[static_cast<size_t> (index)].power = power;
    return true;
}

/** The starting shapes of the editor. */
enum class CurvePreset { identity, soft, hard, scurve, fold, steps, kNumPresets };
const char* curvePresetName (CurvePreset preset) noexcept;
std::vector<ContentPoint> curvePreset (CurvePreset preset);
} // namespace synthyboy::ui
