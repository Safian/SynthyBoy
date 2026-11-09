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

// ---- warp picture -------------------------------------------------------------------------------------------------------------------

namespace
{
float curveOf (const std::function<float (float)>& remapCurve, float x)
{
    return remapCurve ? remapCurve (std::min (1.0f, std::max (0.0f, x))) : x;
}

float asymPicture (float u, float s)
{
    return s >= 0.0f ? std::pow (u, 1.0f + 3.0f * s) : 1.0f - std::pow (1.0f - u, 1.0f + 3.0f * -s);
}

float bendPicture (float u, float s)
{
    const float h = u < 0.5f ? 2.0f * u : 2.0f * (1.0f - u);
    const float power = s >= 0.0f ? 1.0f + 3.0f * s : 1.0f / (1.0f + 3.0f * -s);
    const float r = 0.5f * std::pow (h, power);
    return u < 0.5f ? r : 1.0f - r;
}

// the shaped value of a distortion mode of block B21 at the drive of the amount (the shapes of distortion.type, simplified)
float shapePicture (int type, float v, float amount)
{
    const float x = v * (1.0f + 7.0f * amount);
    switch (type)
    {
        case 27: return std::tanh (x + 0.3f) - std::tanh (0.3f);                      // Tube
        case 28: return std::tanh (x);                                              // Soft Clip
        case 29: return std::min (1.0f, std::max (-1.0f, x));                      // Hard Clip
        case 30: return x >= 0.0f ? 1.0f - std::exp (-x) : -0.5f * (1.0f - std::exp (2.0f * x)); // Diode 1
        case 31: return x >= 0.0f ? 1.0f - std::exp (-x) : std::exp (x) - 1.0f;   // Diode 2
        case 32: // Linear Fold: a triangle fold
        {
            float r = std::fmod (x * 0.25f + 0.75f, 1.0f);
            r -= std::floor (r);
            return std::fabs (-4.0f * r + 2.0f) - 1.0f;
        }
        case 33: return std::sin (x * 1.5707963f);                                  // Sine Fold
        case 34: return std::tanh (x * 32.0f);                                      // Zero-Square
        case 35: return std::tanh (std::max (x, 0.0f)) + std::max (std::min (x, 0.0f), -0.5f); // Asym
        case 36: return std::fabs (std::min (1.0f, std::max (-1.0f, x)));          // Rectify
        case 37: return std::sin (x * 1.5707963f);                                  // Sine Shaper
        case 38: return 0.7f * std::tanh (2.0f * x) + 0.3f * std::min (1.0f, std::max (-1.0f, x)); // Stomp Box
        case 39: return 0.63661977f * std::atan (x * 1.4137167f);                   // Tape Sat.
        default: return x / (1.0f + std::fabs (x));                                 // Soft Sat.
    }
}
} // namespace

float warpedValue (int type, float amount, float phase, const std::function<float (float)>& wave, const std::function<float (float)>& remapCurve)
{
    constexpr float twoPi = 6.28318530718f;
    phase -= std::floor (phase);
    amount = std::min (1.0f, std::max (0.0f, amount));
    if (type <= 0 || amount <= 0.0005f)
        return wave (phase);
    switch (type)
    {
        case 1: // Sync: the cycle restarts faster
        {
            const float p = phase * (1.0f + amount * 3.0f);
            return wave (p - std::floor (p));
        }
        case 2: // Formant: the cycle is squeezed into the first part and faded with a Hann window, the rest is silent
        {
            const float f = 1.0f + amount * 4.0f;
            const float p = phase * f;
            if (p >= 1.0f)
                return 0.0f;
            return wave (p) * (0.5f - 0.5f * std::cos (twoPi * p));
        }
        case 3: // Quantize: the phase moves in steps
        {
            const float steps = std::max (2.0f, 64.0f - amount * 62.0f);
            return wave (std::floor (phase * steps) / steps);
        }
        case 4: // Bend: the middle of the cycle is pushed to one side (< 0.5 early, > 0.5 late)
        {
            const float p = phase - 0.5f;
            const float s = amount > 0.5f ? 1.0f + (amount - 0.5f) * 2.0f : 1.0f - (0.5f - amount) * 1.5f;
            return wave (std::min (1.0f, std::max (0.0f, 0.5f + p * s)));
        }
        case 5: // Squeeze: the phase is bent towards the end of the cycle
            return wave (std::pow (phase, 1.0f + amount * 3.0f));
        case 6: // Pulse: the pulse width moves away from 50 %
        {
            const float pw = 0.5f + (amount - 0.5f) * 0.8f;
            return wave (phase < pw ? phase / (2.0f * pw) : 0.5f + (phase - pw) / (2.0f * (1.0f - pw)));
        }
        case 7:
        case 8: // FM from Osc 2 / Osc 3
        {
            const float ratio = type == 7 ? 2.0f : 3.0f;
            const float p = phase + amount * 0.25f * std::sin (twoPi * ratio * phase);
            return wave (p - std::floor (p));
        }
        case 9:
        case 10: // RM from Osc 2 / Osc 3: the cycle times the other oscillator, mixed in with the amount
        {
            const float ratio = type == 9 ? 2.0f : 3.0f;
            return wave (phase) * (1.0f - amount + amount * std::sin (twoPi * ratio * phase));
        }
        // ---- block B21: the Serum 2 modes ------------------------------------------------------------------------------------------
        case 11: // Sync Var: sync with a soft edge (the VAR fader is the phase knob; the picture uses a mid value)
        {
            const float p = phase * (1.0f + amount * 3.0f);
            const float u = p - std::floor (p);
            const float edge = std::min (u, 1.0f - u);
            return wave (u) * (edge < 0.25f ? 0.5f - 0.5f * std::cos (3.14159265f * edge / 0.25f) : 1.0f);
        }
        case 12: return wave (bendPicture (phase, amount));
        case 13: return wave (bendPicture (phase, -amount));
        case 14: return wave (bendPicture (phase, 2.0f * amount - 1.0f));
        case 15: return wave (asymPicture (phase, amount));
        case 16: return wave (asymPicture (phase, -amount));
        case 17: return wave (asymPicture (phase, 2.0f * amount - 1.0f));
        case 18: return phase < 1.0f - amount ? wave (phase) : -wave (phase); // Flip
        case 19: // Mirror: the second half plays the first half backwards
            return wave (0.5f * asymPicture (phase < 0.5f ? 2.0f * phase : 2.0f * (1.0f - phase), 2.0f * amount - 1.0f));
        case 20: return wave (phase + amount * (curveOf (remapCurve, phase) - phase));
        case 21:
        {
            const float half = phase < 0.5f ? 0.0f : 1.0f;
            const float h = 2.0f * phase - half;
            return wave ((h + amount * (curveOf (remapCurve, h) - h) + half) * 0.5f);
        }
        case 22: return wave (phase - amount * std::sin (twoPi * phase) / twoPi);
        case 23:
        {
            const float x = 4.0f * phase;
            const float k = std::floor (x);
            const float h = x - k;
            return wave ((k + h + amount * (curveOf (remapCurve, h) - h)) * 0.25f);
        }
        case 24: // Odd/Even
        {
            const float a = wave (phase), b = wave (phase + 0.5f - std::floor (phase + 0.5f));
            const float even = amount > 0.5f ? 1.0f : 2.0f * amount, odd = amount < 0.5f ? 1.0f : 2.0f * (1.0f - amount);
            return even * 0.5f * (a + b) + odd * 0.5f * (a - b);
        }
        case 25:
        case 26: // LPF / HPF: a moving average of the cycle (the cutoff opens with the amount for the LPF, closes for the HPF)
        {
            const float width = type == 25 ? 0.4f * (1.0f - amount) : 0.4f * amount;
            float sum = 0.0f;
            constexpr int taps = 9;
            for (int i = 0; i < taps; ++i)
                sum += wave (phase + width * (static_cast<float> (i) / (taps - 1) - 0.5f) - std::floor (phase + width * (static_cast<float> (i) / (taps - 1) - 0.5f)));
            sum /= static_cast<float> (taps);
            return type == 25 ? sum : wave (phase) - sum;
        }
        case 41: case 42: case 43: case 44: case 45: case 46: // FM Thru-Zero / Exp / Linear
        {
            const float ratio = (type % 2) == 1 ? 2.0f : 3.0f;
            const float p = phase + amount * amount * 0.3f * std::sin (twoPi * ratio * phase);
            return wave (p - std::floor (p));
        }
        case 47: case 48: // PD from X / Y
        {
            const float ratio = type == 47 ? 2.0f : 3.0f;
            const float p = phase + 0.45f * amount * std::sin (twoPi * ratio * phase);
            return wave (p - std::floor (p));
        }
        case 49: // PD Self
        {
            const float p = phase + 0.45f * amount * wave (phase);
            return wave (p - std::floor (p));
        }
        case 50: case 51: // AM from X / Y
        {
            const float ratio = type == 50 ? 2.0f : 3.0f;
            return wave (phase) * (1.0f - amount + amount * (0.5f + 0.5f * std::sin (twoPi * ratio * phase)));
        }
        default: break;
    }
    if (type >= 27 && type <= 40)
        return shapePicture (type, wave (phase), amount);
    return wave (phase);
}

// ---- CPU colour ---------------------------------------------------------------------------------------------------------------------

int cpuLevel (float load) noexcept
{
    return load < 0.5f ? 0 : load < 0.8f ? 1 : 2;
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
    // oscN.warp.type: the eleven Vital modes (None Sync Formant Quantize Bend Squeeze Pulse FM from X / Y, RM from X / Y) and the 41 Serum 2 modes
    // appended in block B21 (13-new-keys.md 4.22): 11 Sync Var, 12 .. 14 Bend +, -, +/-, 15 .. 17 Asym +, -, +/-, 18 Flip, 19 Mirror, 20 .. 23 Remap 1-4,
    // 24 Odd/Even, 25 / 26 LPF / HPF, 27 .. 40 the distortions, 41 .. 46 FM Thru-Zero / Exp / Linear from X / Y, 47 / 48 / 49 PD from X / Y / Self,
    // 50 / 51 AM from X / Y; GROUPB WP-06 (4.34) 52 .. 75: FM Thru-Zero (52 .. 55), FM Exp (56 .. 59), FM Linear (60 .. 63), PD (64 .. 67), AM (68 .. 71) and
    // RM (72 .. 75) from the Noise, Sub, Filter 1 and Filter 2
    return {
        { "Basic", { 0, 1, 11, 2, 3, 5, 6 }, false },
        { "Alt", { 4, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 }, false },
        { "Filter", { 25, 26 }, false },
        { "Distortion", { 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40 }, false },
        { "FM", { 7, 8, 41, 42, 43, 44, 45, 46, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63 }, false },
        { "PD", { 47, 48, 49, 64, 65, 66, 67 }, false },
        { "AM", { 50, 51, 68, 69, 70, 71 }, false },
        { "RM", { 9, 10, 72, 73, 74, 75 }, false },
        // GROUPC WP-12 (4.38): the spectral warps of the spectral oscillator (76 .. 98)
        { "Spectral", { 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92 }, false },
        { "Spectral Mask", { 93, 94, 95, 96, 97, 98 }, false },
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
