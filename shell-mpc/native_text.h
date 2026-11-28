// SynthyBoy - native read-only text of the Mod page and the lists of the oscillator pages: what the plugin writes (the overlay thread) and the host reads through effGetParamDisplay.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Two plain data holders, no control and no drawing (owner decision 2026-11-27: the C++ layer has no UI elements):
//  * `Lines`: a few text lines (the Mod page title, the slot count, the title of the slot page, the two lines of the empty matrix). A line is a parameter of the host skin (nativeui::kNativeText + line) shown by a native Label of type
//    Value; its display text is the line, its value changes with every new text (the host reads the text again only when the value changed, docs/dsp-knowledge/skin-host-behaviour.md).
//  * `ListText`: the names of the rows of a list the host skin shows with native cells (the file browsers and the source of a new modulation of the oscillator pages): the title and the rows of ONE page of the list. The cells are
//    the modulation picker's (nativeui::modboard); this holds the text those cells show while the page is active.
// The writer is the overlay thread, the reader the host's UI thread: a mutex guards the characters (held for a copy of a few dozen bytes, never in the audio thread).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "native_mod_controls.h"

namespace synthyboy_mpc::nativeui::nativetext
{
enum Line { ModTitle, ModCount, ModDetail, ModEmpty1, ModEmpty2, kLines };
constexpr int kLineLen = 48;

class Lines
{
public:
    /** Set a line; the revision (and with it the value the host sees) changes only when the text does. */
    void set (int line, const char* text) noexcept
    {
        if (line < 0 || line >= kLines)
            return;
        if (text == nullptr)
            text = "";
        char next[kLineLen];
        std::strncpy (next, text, kLineLen - 1);
        next[kLineLen - 1] = '\0';
        std::lock_guard<std::mutex> guard (mutex_);
        if (std::strcmp (text_[line], next) == 0)
            return;
        std::memcpy (text_[line], next, sizeof next);
        ++revision_[line];
    }
    void copy (int line, char* out, size_t cap) const noexcept
    {
        if (out == nullptr || cap == 0)
            return;
        out[0] = '\0';
        if (line < 0 || line >= kLines)
            return;
        std::lock_guard<std::mutex> guard (mutex_);
        std::strncpy (out, text_[line], cap - 1);
        out[cap - 1] = '\0';
    }
    /** 0 while the line is empty (the skin uses that as a gate: a native cell that belongs to the line shows only while it has text); otherwise a value in (0, 1] that differs from the one before at every new text. */
    float value (int line) const noexcept
    {
        if (line < 0 || line >= kLines)
            return 0.0f;
        std::lock_guard<std::mutex> guard (mutex_);
        return text_[line][0] == '\0' ? 0.0f : static_cast<float> ((revision_[line] - 1) % 250 + 1) / 251.0f;
    }

private:
    mutable std::mutex mutex_;
    char text_[kLines][kLineLen] {};
    uint32_t revision_[kLines] {};
};

constexpr int kRowLen = 64;

/** One page of a list: the title, the rows (at most modboard::kListRows), and whether the header is a way UP (a browser) or the group list (the modulation sources). */
struct ListPage
{
    char title[kRowLen] {};
    char rows[modboard::kListRows][kRowLen] {};
    int count = 0;
    bool up = false;
};

class ListText
{
public:
    void set (const ListPage& page) noexcept
    {
        std::lock_guard<std::mutex> guard (mutex_);
        page_ = page;
        active_ = true;
    }
    void clear() noexcept
    {
        std::lock_guard<std::mutex> guard (mutex_);
        page_ = {};
        active_ = false;
    }
    bool active() const noexcept
    {
        std::lock_guard<std::mutex> guard (mutex_);
        return active_;
    }
    bool up() const noexcept
    {
        std::lock_guard<std::mutex> guard (mutex_);
        return active_ && page_.up;
    }
    /** The title (row < 0) or the text of row `row` into `out`; false while no page is active (the caller then uses the modulation picker's own names). */
    bool copy (int row, char* out, size_t cap) const noexcept
    {
        if (out == nullptr || cap == 0)
            return false;
        out[0] = '\0';
        std::lock_guard<std::mutex> guard (mutex_);
        if (! active_)
            return false;
        const char* source = row < 0 ? page_.title : row < page_.count && row < modboard::kListRows ? page_.rows[row] : "";
        std::strncpy (out, source, cap - 1);
        out[cap - 1] = '\0';
        return true;
    }

private:
    mutable std::mutex mutex_;
    ListPage page_;
    bool active_ = false;
};
}
