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
/** "Choice List 0" .. : the handle the opener of slot s writes is Parameter kFirstSlotParameter + s (for the first kFirstBlockSlots), then Parameter kTailSlotParameter + (s - kFirstBlockSlots)
    after liststate (4017..4026) so existing handle numbers never shift. */
constexpr int kFirstSlotParameter = 3904;
constexpr int kFirstBlockSlots = 113;
constexpr int kTailSlotParameter = 4027;

/** The host index (the skin's "Parameter N") of the Data handle each slot opens the list of. Append only: a new opener takes the next slot. */
inline constexpr int kSlotHandles[] = {
    21, 45, 69, 86, 1240, 1241, 1242, 1244, 1376, 1377, 1380, 1381, 1564, 1576, 1577, 1595, 2057, 2119, 2772, 2773, 2774, 2775, 2776, 2777, 2778, 2779,
    2780, 2781, 2782, 2783, 2784, 2785, 2786, 2787, 2788, 2789, 2792, 2793, 2794, 2795, 2796, 2797, 2798, 2799, 2800, 2801, 2802, 2803, 2804, 2805, 2806,
    2809, 2815, 2816, 2817, 2818, 2819, 2937, 2938, 2962, 2998, 2999, 3002, 3210, 3279, 3434, 3435, 3436, 3437, 3438, 3439, 3440, 3441, 3443, 3763, 3764,
    3765, 3766, 3767, 3768, 3769, 3770, 3771, 3772, 3773, 3774, 3775, 3776, 3777, 3778, 3779, 3780, 3781, 3782, 3783, 3784, 3785, 3786, 3787, 3788, 3789,
    3790, 3791, 3792, 3793, 3794, 3795, 3796, 3797, 3798, 3799, 3800, 3801,
    3012, 3014, 3442,
    3004, 2979, 2986, 2987
};
constexpr int kSlots = static_cast<int> (sizeof (kSlotHandles) / sizeof (kSlotHandles[0]));

/** The skin's Parameter N for opener slot `slot`. */
constexpr int slotParameter (int slot) noexcept
{
    return slot < kFirstBlockSlots ? kFirstSlotParameter + slot : kTailSlotParameter + (slot - kFirstBlockSlots);
}

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

/** Bounds for optional two-level grouping on a choice list (flat lists have rootCount == 0). */
constexpr int kMaxGroups = 16;
constexpr int kMaxGroupEntries = 32;
constexpr int kMaxRootRows = 32;

/** The rows of one list: the title, every row's shown name and the normalised value that selects it (exactly what the host writes into the handle for that choice).
    When `rootCount > 0`, Level 1 shows `root[0..rootCount-1]` (each either a direct final item or a group opening Level 2 `groups[g]`). Fixed storage: no allocation. */
struct Items
{
    struct Group
    {
        char name[kNameLen] {};
        int entries[kMaxGroupEntries] {};
        int count = 0;
    };
    struct RootRow
    {
        int item = -1;
        int group = -1;
    };

    char title[kNameLen] {};
    char names[kMaxItems][kNameLen] {};
    float values[kMaxItems] {};
    int count = 0;
    Group groups[kMaxGroups] {};
    int groupCount = 0;
    RootRow root[kMaxRootRows] {};
    int rootCount = 0;

    void clear() noexcept
    {
        count = 0;
        title[0] = '\0';
        groupCount = 0;
        rootCount = 0;
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
    /** Appends a direct final item row to Level 1 (`itemIndex` in 0..count-1). */
    bool addRootItem (int itemIndex) noexcept
    {
        if (rootCount >= kMaxRootRows || itemIndex < 0 || itemIndex >= count)
            return false;
        root[rootCount++] = { itemIndex, -1 };
        return true;
    }
    /** Appends a Level 2 group and its arrow row on Level 1. */
    bool addGroup (const char* name, const int* entries, int entryCount) noexcept
    {
        if (groupCount >= kMaxGroups || rootCount >= kMaxRootRows || entries == nullptr || entryCount <= 0 || entryCount > kMaxGroupEntries)
            return false;
        auto& g = groups[groupCount];
        std::snprintf (g.name, sizeof g.name, "%s", name != nullptr ? name : "");
        g.count = 0;
        for (int i = 0; i < entryCount; ++i)
        {
            const int idx = entries[i];
            if (idx < 0 || idx >= count)
                return false;
            g.entries[g.count++] = idx;
        }
        root[rootCount++] = { -1, groupCount };
        ++groupCount;
        return true;
    }
    /** FNV-1a over the title, the names, the values and any two-level groups: the same list gives the same number (the driver checks that the control still lists the same choices before it applies a row). */
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
        h = (h ^ static_cast<uint32_t> (rootCount)) * 16777619u;
        h = (h ^ static_cast<uint32_t> (groupCount)) * 16777619u;
        for (int r = 0; r < rootCount; ++r)
        {
            h = (h ^ static_cast<uint32_t> (root[r].item + 2)) * 16777619u;
            h = (h ^ static_cast<uint32_t> (root[r].group + 2)) * 16777619u;
        }
        for (int g = 0; g < groupCount; ++g)
        {
            mix (groups[g].name);
            h = (h ^ static_cast<uint32_t> (groups[g].count)) * 16777619u;
            for (int e = 0; e < groups[g].count; ++e)
                h = (h ^ static_cast<uint32_t> (groups[g].entries[e])) * 16777619u;
        }
        return (h ^ static_cast<uint32_t> (count)) * 16777619u;
    }
};

/** One open list: the slot it was opened for, its rows, active level and page shown. The owner fills `items`, then opens; a final row tap gives the row's value and closes,
    an arrow row enters Level 2, header back arrow returns to Level 1 on the group's page, BACK closes, PREVIOUS / NEXT page. */
class Session
{
public:
    Items items;

    /** Opens the list of `slot` on the level and page that hold item `current` (out of range or unavailable: Level 1, first page). Nothing opens for an empty list (false). */
    bool open (int slot, int current) noexcept
    {
        open_ = slot >= 0 && slot < kSlots && items.count > 0;
        slot_ = open_ ? slot : -1;
        openGroup_ = -1;
        first_ = 0;
        if (open_)
        {
            if (items.rootCount == 0)
                first_ = current > 0 && current < items.count ? current - current % kRows : 0;
            else
                locateCurrent (current);
        }
        signature_ = open_ ? items.signature() : 0;
        return open_;
    }
    void close() noexcept
    {
        open_ = false;
        slot_ = -1;
        openGroup_ = -1;
        first_ = 0;
    }
    bool isOpen() const noexcept { return open_; }
    int slot() const noexcept { return slot_; }
    int openGroup() const noexcept { return open_ ? openGroup_ : -1; }
    /** Title of the level currently shown (Level 1 title, or the open group's name on Level 2). */
    const char* title() const noexcept
    {
        if (open_ && openGroup_ >= 0 && openGroup_ < items.groupCount)
            return items.groups[openGroup_].name;
        return items.title;
    }
    /** The index of the first row of the page shown. */
    int first() const noexcept { return first_; }
    /** The rows of the page shown (0 .. kRows). */
    int rowsOnPage() const noexcept { return open_ ? std::clamp (levelCount() - first_, 0, kRows) : 0; }
    const char* rowName (int row) const noexcept
    {
        if (! open_ || row < 0 || row >= rowsOnPage())
            return "";
        const int idx = first_ + row;
        if (items.rootCount == 0)
            return items.names[idx];
        if (openGroup_ >= 0 && openGroup_ < items.groupCount)
            return items.names[items.groups[openGroup_].entries[idx]];
        const auto& rr = items.root[idx];
        return rr.group >= 0 ? items.groups[rr.group].name : items.names[rr.item];
    }
    uint32_t signature() const noexcept { return signature_; }
    int lastFirst() const noexcept
    {
        const int n = levelCount();
        return n > 0 ? ((n - 1) / kRows) * kRows : 0;
    }
    bool deeperLevel() const noexcept { return open_ && openGroup_ >= 0; }
    bool rowOpensLevel (int row) const noexcept
    {
        if (! open_ || items.rootCount == 0 || openGroup_ >= 0 || row < 0 || row >= rowsOnPage())
            return false;
        return items.root[first_ + row].group >= 0;
    }
    bool hasPrevious() const noexcept { return open_ && first_ > 0; }
    bool hasNext() const noexcept { return open_ && first_ + kRows < levelCount(); }
    /** PREVIOUS / NEXT: false when there is no page that way (nothing changes). */
    bool previous() noexcept { return page (-kRows); }
    bool next() noexcept { return page (kRows); }
    /** Header back arrow: returns from Level 2 to Level 1 on the page that holds the group just left. */
    bool up() noexcept
    {
        if (! open_ || openGroup_ < 0)
            return false;
        int rootRow = 0;
        for (int r = 0; r < items.rootCount; ++r)
        {
            if (items.root[r].group == openGroup_)
            {
                rootRow = r;
                break;
            }
        }
        openGroup_ = -1;
        first_ = rootRow - rootRow % kRows;
        return true;
    }
    /** A tap on row `row` of the page shown:
        - final row: returns true with that row's value (and closes the list);
        - arrow row (Level 1 group): enters Level 2 on its first page, returns false (list stays open);
        - empty/out-of-range row: returns false (nothing changes). */
    bool choose (int row, float& value) noexcept
    {
        if (! open_ || row < 0 || row >= rowsOnPage())
            return false;
        const int idx = first_ + row;
        if (items.rootCount == 0)
        {
            value = items.values[idx];
            close();
            return true;
        }
        if (openGroup_ >= 0 && openGroup_ < items.groupCount)
        {
            value = items.values[items.groups[openGroup_].entries[idx]];
            close();
            return true;
        }
        const auto& rr = items.root[idx];
        if (rr.group >= 0)
        {
            openGroup_ = rr.group;
            first_ = 0;
            return false;
        }
        value = items.values[rr.item];
        close();
        return true;
    }

private:
    int levelCount() const noexcept
    {
        if (! open_)
            return 0;
        if (items.rootCount == 0)
            return items.count;
        if (openGroup_ >= 0 && openGroup_ < items.groupCount)
            return items.groups[openGroup_].count;
        return items.rootCount;
    }
    void locateCurrent (int current) noexcept
    {
        if (current < 0 || current >= items.count)
            return;
        for (int r = 0; r < items.rootCount; ++r)
        {
            if (items.root[r].item == current)
            {
                openGroup_ = -1;
                first_ = r - r % kRows;
                return;
            }
        }
        for (int r = 0; r < items.rootCount; ++r)
        {
            const int g = items.root[r].group;
            if (g < 0 || g >= items.groupCount)
                continue;
            const auto& grp = items.groups[g];
            for (int e = 0; e < grp.count; ++e)
            {
                if (grp.entries[e] == current)
                {
                    openGroup_ = g;
                    first_ = e - e % kRows;
                    return;
                }
            }
        }
    }
    bool page (int delta) noexcept
    {
        const int to = std::clamp (first_ + delta, 0, lastFirst());
        if (! open_ || to == first_)
            return false;
        first_ = to;
        return true;
    }

    int slot_ = -1, openGroup_ = -1, first_ = 0;
    uint32_t signature_ = 0;
    bool open_ = false;
};
}
