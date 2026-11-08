// SynthyBoy - UI view-model: the maths behind the gestures of the audit gap closure (docs/ui-design/UI-GAPS-REPORT.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pure C++17, no JUCE: zoom / pan windows, slice lists, loop pairs, transpose segments, grouped choice lists, the two-finger tracker
// (tap, swipe, pinch, pan) and small text parsers. The Components only draw and forward events to these.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace synthyboy::ui
{
// ---- zoom / pan window over a 0..1 range --------------------------------------------------------------------------------------------

/** The visible window [start, start + span] of a 0..1 range (sample view, wave canvas). span 1 = fit. */
struct Viewport
{
    static constexpr double kMinSpan = 1.0 / 256.0;

    double start = 0.0;
    double span = 1.0;

    bool isFit() const noexcept { return span >= 1.0; }
    double zoom() const noexcept { return 1.0 / span; }
    void fit() noexcept { start = 0.0; span = 1.0; }
    /** factor > 1 zooms in. The point of the range at `anchor` (0..1 of the range) stays where it is on screen. */
    void zoomAbout (double anchor, double factor) noexcept;
    /** Moves the window by `deltaNorm` of the range (positive = later). */
    void pan (double deltaNorm) noexcept;
    /** Screen fraction 0..1 of the window <-> position 0..1 of the range. */
    double toRange (double screenFraction) const noexcept { return start + screenFraction * span; }
    double toScreen (double rangePos) const noexcept { return (rangePos - start) / span; }

private:
    void clampWindow() noexcept;
};

// ---- slices -------------------------------------------------------------------------------------------------------------------------

inline constexpr int kMaxSlices = 128;
inline constexpr float kMinSliceGap = 0.002f;

/** Slice starts 0..1 ascending. All edits keep the list ascending, within 0..1, at least kMinSliceGap apart and at most kMaxSlices long. */
int nearestSlice (const std::vector<float>& slices, float pos, float tolerance) noexcept; // -1 if none within tolerance
bool addSlice (std::vector<float>& slices, float pos);
bool removeSlice (std::vector<float>& slices, int index);
/** Moves slice `index` to `pos`, clamped between its neighbours; returns the index (unchanged). */
bool moveSlice (std::vector<float>& slices, int index, float pos);
/** The threshold line: an onset becomes a slice when its strength is >= 1 - threshold (SampleData::detectOnsets), so the line height
    above the bottom of the waveform area is 1 - threshold: 100 % = line at the bottom (all onsets), 0 % = at the top. */
float thresholdToLine (float percent) noexcept;
float lineToThreshold (float lineFraction) noexcept;

// ---- loop pair ----------------------------------------------------------------------------------------------------------------------

/** Drag between the two loop markers moves both by `delta` (0..1 of the sample), keeping the length; stops at the ends. */
void moveLoopPair (float& loopStart, float& loopEnd, float delta) noexcept;
/** True if x lies strictly between the two markers and farther than `tolerance` from both. */
bool betweenLoopMarkers (float x, float loopStart, float loopEnd, float tolerance) noexcept;

// ---- transpose ----------------------------------------------------------------------------------------------------------------------

/** oscN.transpose (-48..48 semitones) as OCT segment + SEM wheel: oct = floor ((t + 6) / 12), sem = t - 12 * oct (-6..+5). */
struct TransposeSplit
{
    int oct = 0;
    int sem = 0;
};
TransposeSplit splitTranspose (int semitones) noexcept;
int joinTranspose (int oct, int sem) noexcept; // clamped to -48..48

// ---- grouped choice lists -----------------------------------------------------------------------------------------------------------

struct ChoiceGroup
{
    std::string name;
    std::vector<int> items; // choice indices of the key
    bool gap = false;       // the design lists the group but the engine has no mode in it: shown greyed, no items
};
/** WARP+ list of docs/ui-design/02 (Basic / Alt / FM / RM, with the Filter / Distortion / PD / AM groups the engine does not have as gaps).
    Every choice of `oscN.warp.type` is in exactly one group. */
std::vector<ChoiceGroup> warpTypeGroups();

// ---- two-finger tracker -------------------------------------------------------------------------------------------------------------

struct TouchPoint
{
    float x = 0.0f, y = 0.0f;
};

/** Feeds on down / drag / up of at most two touches; reports tap, swipe, pinch and pan (a drag gesture every second drag event, because
    touch events arrive one finger at a time). */
class TouchTracker
{
public:
    enum class Phase { down, drag, up };
    enum class Kind { none, twoFingerTap, twoFingerSwipe, pinch, pan };
    struct Gesture
    {
        Kind kind = Kind::none;
        float dx = 0.0f, dy = 0.0f; // swipe: total movement of the pair; pan: movement since the last event
        float scale = 1.0f;         // pinch: distance ratio since the last event
        TouchPoint centre;          // pinch / pan: the centre of the two touches
    };

    static constexpr int kTapMaxMs = 350;
    static constexpr float kTapSlopPx = 14.0f;
    static constexpr float kSwipeMinPx = 48.0f;

    Gesture feed (Phase phase, int touchId, TouchPoint pos, int64_t timeMs);
    int activeTouches() const noexcept { return count; }
    void reset() noexcept;

private:
    struct Touch
    {
        int id = -1;
        TouchPoint down, now;
        bool active = false;
    };
    Touch touches[2];
    int count = 0;
    int64_t pairDownMs = 0;
    bool pairMoved = false;     // beyond the tap slop
    bool pairWasPinch = false;
    TouchPoint pairStartCentre, pairLastCentre;
    float pairLastDistance = 0.0f, pairStartDistance = 0.0f;
    bool pairTapPending = false;
    int pendingUpdates = 0;
};

// ---- small parsers ------------------------------------------------------------------------------------------------------------------

/** FFT bin entry: "37" or "37%" = 37 percent. Returns the amplitude 0..1; false for text that is not a number in 0..100. */
bool parseBinAmplitude (std::string_view text, float& amplitude01);
/** Bin phase entry in degrees (any real, wrapped to 0..360). */
bool parseBinPhaseDegrees (std::string_view text, float& degrees);
/** Number field with clamp: "12", "12.5"; false if not a finite number. */
bool parseNumber (std::string_view text, double& value);
} // namespace synthyboy::ui
