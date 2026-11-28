// SynthyBoy - MPC native choice lists: one tap on a dropdown opens the plugin's own paged list of that control's choices, one tap on a row applies it and closes the list (owner rule 2026-11-28,
// AGENTS.md "Lists: one tap chooses and closes"). The host MenuOverlay shows no names for a VST2's choices (docs/dsp-knowledge/skin-host-behaviour.md "Host list: who draws the names").
// Skin: every opener definition holds a transparent Button (handle ListOpen, id 1 of 2); each instance maps ListOpen to "ResetOnGestureEnd/Parameter <kFirstSlotParameter + slot>", where slot is the index of the
// instance's Data handle in kSlotHandles. scripts/validate_synthyboy_skin.py checks that pairing against this table, so the table and the skin cannot drift apart.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The rest of this file is the plain-data half of the lists (no host, no threads but the port's mutex; tests/choice_list_host.cpp): the debounced port of the openers (RequestPort), the rows of one list
// (Items: the shown names and, per row, the normalised value the host itself writes into the control's handle for that choice) and the paged session (Session: open on the page holding the current
// row, PREVIOUS / NEXT, one row tap gives its value and closes, BACK closes). The plugin's display thread drives it (synthyboy_mpc.cpp, PluginInstance::choiceListPoll).
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "native_text.h"

namespace synthyboy_mpc::nativeui::choicelist
{
/** "Choice List Open" (read-only: exactly 1 while a choice list is open, else 0; the skin gates the list modal with IndexedEnabling/1/2/Parameter kOpenParameter). */
constexpr int kOpenParameter = 3903;
/** "Choice List 0" .. : the handle the opener of slot s writes is Parameter kFirstSlotParameter + s. */
constexpr int kFirstSlotParameter = 3904;

/** The host index (the skin's "Parameter N") of the Data handle each slot opens the list of. Append only: a new opener takes the next slot. */
inline constexpr int kSlotHandles[] = {
    21, 45, 69, 86, 1240, 1241, 1242, 1244, 1376, 1377, 1380, 1381, 1564, 1576, 1577, 1595, 2057, 2119, 2772, 2773, 2774, 2775, 2776, 2777, 2778, 2779,
    2780, 2781, 2782, 2783, 2784, 2785, 2786, 2787, 2788, 2789, 2792, 2793, 2794, 2795, 2796, 2797, 2798, 2799, 2800, 2801, 2802, 2803, 2804, 2805, 2806,
    2809, 2815, 2816, 2817, 2818, 2819, 2937, 2938, 2962, 2998, 2999, 3002, 3210, 3279, 3434, 3435, 3436, 3437, 3438, 3439, 3440, 3441, 3443, 3763, 3764,
    3765, 3766, 3767, 3768, 3769, 3770, 3771, 3772, 3773, 3774, 3775, 3776, 3777, 3778, 3779, 3780, 3781, 3782, 3783, 3784, 3785, 3786, 3787, 3788, 3789,
    3790, 3791, 3792, 3793, 3794, 3795, 3796, 3797, 3798, 3799, 3800, 3801
};
constexpr int kSlots = static_cast<int> (sizeof (kSlotHandles) / sizeof (kSlotHandles[0]));

/** The slot of host index `handle`, -1 when no opener lists it. */
constexpr int slotOfHandle (int handle) noexcept
{
    for (int s = 0; s < kSlots; ++s)
        if (kSlotHandles[s] == handle)
            return s;
    return -1;
}

/** The host delivers one tap as two press events 30..140 ms apart with no release between (Key 61 measurement, textentry::kDebounceMs): the same slot inside this window is one request. */
constexpr double kDebounceMs = 250.0;
/** The rows of a page: the modulation picker's list cells (nativeui::modboard). */
constexpr int kRows = modboard::kListRows;
/** The longest list of the table is the FX group of the LFO's ADD TARGET (514 destinations); the cap leaves room for the matrix to grow. */
constexpr int kMaxItems = 768;
/** A name as long as a row of the list page the host reads (nativetext::ListPage), terminator included. */
constexpr int kNameLen = nativetext::kRowLen;

/** The names the host shows for the two group choices of the Mod page header ("Mod Navigation Source Group" = Parameter 2937, "... Destination Group" = Parameter 2938). The plugin's display string of
    these handles is empty: the host draws the skin's replacementStrings of the handle, and these tables are a verbatim copy of them (skin/TUI.json); index = the group (0 = every group). */
// scripts/validate_synthyboy_skin.py compares both tables with the replacementStrings of Parameter 2937 / 2938, so the list and the closed field cannot show different names.
inline constexpr const char* kModSourceGroupNames[] = { "ALL SOURCES", "ENVELOPES", "LFOS", "MACROS", "PLAYER", "SOUND" };
inline constexpr const char* kModDestinationGroupNames[] = { "ALL MODULES", "OSC A", "OSC B", "OSC C", "SUB / NOISE", "FILTER 1", "FILTER 2", "ENVELOPES", "LFOS", "FX", "MASTER" };

/** Host parameter thread -> display thread: the slots whose opener the host wrote (post), debounced per slot, the latest one taken by the driver (take). Plain data and a mutex (never the audio thread). */
class RequestPort
{
public:
    RequestPort() noexcept { lastMs_.fill (-1.0); }
    /** `nowMs`: a steady clock. A slot out of range is ignored; the same slot inside kDebounceMs of its own last accepted request is dropped (the second press event of one tap); another slot replaces
        what is pending (latest wins). */
    void post (int slot, double nowMs) noexcept
    {
        if (slot < 0 || slot >= kSlots)
            return;
        std::lock_guard<std::mutex> g (mutex_);
        const double last = lastMs_[static_cast<size_t> (slot)];
        if (last >= 0.0 && nowMs >= last && nowMs - last < kDebounceMs)
            return;
        lastMs_[static_cast<size_t> (slot)] = nowMs;
        pending_ = slot;
    }
    void post (int slot) noexcept
    {
        post (slot, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    /** The latest slot not taken yet, -1 when there is none. */
    int take() noexcept
    {
        std::lock_guard<std::mutex> g (mutex_);
        const int slot = pending_;
        pending_ = -1;
        return slot;
    }
    /** Forgets what is pending (the debounce times stay: a late second press of a tap is still one tap). */
    void clear() noexcept
    {
        std::lock_guard<std::mutex> g (mutex_);
        pending_ = -1;
    }

private:
    std::mutex mutex_;
    int pending_ = -1;
    std::array<double, kSlots> lastMs_ {};
};

/** The rows of one list: the title, every row's shown name and the normalised value that selects it (exactly what the host writes into the handle for that choice). Fixed storage: no allocation. */
struct Items
{
    char title[kNameLen] {};
    char names[kMaxItems][kNameLen] {};
    float values[kMaxItems] {};
    int count = 0;

    void clear() noexcept
    {
        count = 0;
        title[0] = '\0';
    }
    void setTitle (const char* text) noexcept { std::snprintf (title, sizeof title, "%s", text != nullptr ? text : ""); }
    /** Appends a row; false when the list is full (the row is dropped). */
    bool add (const char* name, float value) noexcept
    {
        if (count >= kMaxItems)
            return false;
        std::snprintf (names[count], sizeof names[count], "%s", name != nullptr ? name : "");
        values[count] = value;
        ++count;
        return true;
    }
    /** FNV-1a over the title, the names and the values: the same list gives the same number (the driver checks that the control still lists the same choices before it applies a row). */
    uint32_t signature() const noexcept
    {
        uint32_t h = 2166136261u;
        auto mix = [&h] (const char* text) { for (const char* c = text; *c != '\0'; ++c) h = (h ^ static_cast<unsigned char> (*c)) * 16777619u; h = (h ^ 0xffu) * 16777619u; };
        mix (title);
        for (int i = 0; i < count; ++i)
        {
            mix (names[i]);
            uint32_t bits = 0;
            static_assert (sizeof bits == sizeof values[i]);
            std::memcpy (&bits, &values[i], sizeof bits);
            h = (h ^ bits) * 16777619u;
        }
        return (h ^ static_cast<uint32_t> (count)) * 16777619u;
    }
};

/** One open list: the slot it was opened for, its rows and the page shown. The owner fills `items`, then opens; a row tap gives the row's value and closes, BACK closes, PREVIOUS / NEXT page. */
class Session
{
public:
    Items items;

    /** Opens the list of `slot` on the page that holds row `current` (out of range: the first page). Nothing opens for an empty list (false). */
    bool open (int slot, int current) noexcept
    {
        open_ = slot >= 0 && slot < kSlots && items.count > 0;
        slot_ = open_ ? slot : -1;
        first_ = open_ && current > 0 && current < items.count ? current - current % kRows : 0;
        signature_ = open_ ? items.signature() : 0;
        return open_;
    }
    void close() noexcept
    {
        open_ = false;
        slot_ = -1;
        first_ = 0;
    }
    bool isOpen() const noexcept { return open_; }
    int slot() const noexcept { return slot_; }
    /** The index of the first row of the page shown. */
    int first() const noexcept { return first_; }
    /** The rows of the page shown (0 .. kRows). */
    int rowsOnPage() const noexcept { return open_ ? std::clamp (items.count - first_, 0, kRows) : 0; }
    const char* rowName (int row) const noexcept { return row >= 0 && row < rowsOnPage() ? items.names[first_ + row] : ""; }
    uint32_t signature() const noexcept { return signature_; }
    int lastFirst() const noexcept { return items.count > 0 ? ((items.count - 1) / kRows) * kRows : 0; }
    /** PREVIOUS / NEXT: false when there is no page that way (nothing changes). */
    bool previous() noexcept { return page (-kRows); }
    bool next() noexcept { return page (kRows); }
    /** A tap on row `row` of the page shown: true with that row's value (the list closes), false for a row that shows nothing (the list stays). */
    bool choose (int row, float& value) noexcept
    {
        if (! open_ || row < 0 || row >= rowsOnPage())
            return false;
        value = items.values[first_ + row];
        close();
        return true;
    }

private:
    bool page (int delta) noexcept
    {
        const int to = std::clamp (first_ + delta, 0, lastFirst());
        if (! open_ || to == first_)
            return false;
        first_ = to;
        return true;
    }

    int slot_ = -1, first_ = 0;
    uint32_t signature_ = 0;
    bool open_ = false;
};
}
