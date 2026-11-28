// SynthyBoy - MPC native Play lists: the codes of the "Play List" handle (nativeui::kPlayList) and the bridge from the host thread that receives them to the overlay thread that owns the Play page.
// The Play page's choices with more than eight options are not host MenuOverlays (the host cannot show the names of a VST2's choices in one): a skin radio Button writes a code into "Play List", the Play page
// (overlay/play_view.h) opens its own paged native list (overlay/native_list_cells.h: the modulation picker's cells, nativeui::modboard) with the names and applies the row tapped.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace synthyboy_mpc::nativeui::playlist
{
/** The codes of the handle (stepped integer 0 .. kLastCode; the host value is code / kLastCode). */
enum Code
{
    None = 0,
    Source = 1,        // the source of the shown oscillator A / B / C: wavetable categories and tables, or the roots, folders and files of the content (two levels, like the Osc pages)
    LfoChoose = 2,     // LFO 1 .. 10, RND 1 .. 4 (uibind::chooseLfo (row + 1))
    NoiseSound = 3,    // the nine choices of noise.sample (through the compact Noise "First" item)
    LfoShape = 4,      // the shape of the shown standard LFO (playenvlfo::Shape)
    LfoDivision = 5,   // the division of the shown LFO (playenvlfo::Division)
    LfoStandard = 6,   // LFO 1 .. 10 (uibind::chooseLfo (row + 1))
    kLastCode = LfoStandard
};

/** The host delivers one tap as two press events 30..140 ms apart with no release between (Key 61 measurement, textentry::kDebounceMs): the same code inside this window is one request. */
constexpr double kDebounceMs = 250.0;

/** The code of the host value of the handle: lround (value * kLastCode); None for anything out of range or not finite. */
inline int codeOfValue (float value) noexcept
{
    if (! (value > 0.0f) || value > 1.0f)
        return None;
    return std::clamp (static_cast<int> (std::lround (value * static_cast<float> (kLastCode))), 0, static_cast<int> (kLastCode));
}

/** The title of the list of `code` where the list has no own header (the Source list's header is the browser's). */
inline const char* title (int code) noexcept
{
    switch (code)
    {
        case Source: return "SOURCE";
        case LfoChoose: case LfoStandard: return "SELECT LFO";
        case NoiseSound: return "NOISE SOUND";
        case LfoShape: return "LFO SHAPE";
        case LfoDivision: return "LFO DIVISION";
        default: return "";
    }
}

/** The rows of the LfoChoose list (LFO 1 .. 10, RND 1 .. 4: uibind::kSelLfos + uibind::kSelRnds); LfoStandard shows the first ten. */
constexpr int kLfoChooseRows = 14, kLfoStandardRows = 10;
inline const char* const* lfoNames() noexcept
{
    static const char* const names[kLfoChooseRows] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "LFO 9", "LFO 10", "RND 1", "RND 2", "RND 3", "RND 4" };
    return names;
}

/** Host thread -> overlay thread: the codes written by the host (post), debounced, the latest one taken by the Play page (take). Plain data, a mutex (the host's parameter thread, never the audio thread). */
class RequestPort
{
public:
    /** `nowMs`: a steady clock; None is ignored (the Button's release), the same code inside kDebounceMs of its own last accepted request is dropped. */
    void post (int code, double nowMs) noexcept
    {
        if (code <= None || code > kLastCode)
            return;
        std::lock_guard<std::mutex> g (mutex_);
        if (code == lastCode_ && nowMs >= lastMs_ && nowMs - lastMs_ < kDebounceMs)
            return;
        lastCode_ = code;
        lastMs_ = nowMs;
        pending_ = code;
    }
    void post (int code) noexcept
    {
        post (code, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    /** The latest code not taken yet, None when there is none. */
    int take() noexcept
    {
        std::lock_guard<std::mutex> g (mutex_);
        const int code = pending_;
        pending_ = None;
        return code;
    }
    /** Forgets what is pending and the debounce time. */
    void clear() noexcept
    {
        std::lock_guard<std::mutex> g (mutex_);
        pending_ = lastCode_ = None;
        lastMs_ = 0.0;
    }

private:
    std::mutex mutex_;
    int pending_ = None, lastCode_ = None;
    double lastMs_ = 0.0;
};
}
