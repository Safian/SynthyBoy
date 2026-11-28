// SynthyBoy - MPC overlay: a plugin-held list (header, names) shown by the native cells of the modulation picker (nativeui::modboard): kListRows rows a page, BACK, the groups button, PREVIOUS and NEXT.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// No drawing and no touch of its own (owner decision 2026-11-27: the C++ layer has no UI elements; the old popup list is deleted). The page owns the data (the names, the header); this publishes them as a ListPage (the names the host
// reads, IInstanceView::setNativeListText) and as the cells (setNativeModBoard layout 2), pages with PREVIOUS / NEXT and hands every other gesture back to the owner: a row (the index in the whole list), BACK, the groups button.
// The same cells and names as the oscillator pages' lists (osc_ui.h publishNativeLists); the skin instance is `Osc Native List` (oscNativeList) on the tab, `playNativeList` on Play (play_view.h, the Play lists).
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

#include "instance_view.h"

namespace synthyboy_mpc { namespace overlay {

class NativeListCells
{
public:
    /** What the list shows: the title (nullptr: `fallbackTitle`), the names, and whether the header is a way back to the groups. */
    struct Data
    {
        const char* header = nullptr;
        const char* fallbackTitle = "";
        const char* const* items = nullptr;
        int count = 0;
        bool headerBack = false;
    };

    /** Publishes the list and takes one gesture: onRow (index in the whole list), onBack (), onGroups () are called for it; returns true when a gesture was used (the owner may have changed its page). */
    template <class Row, class Back, class Groups>
    bool publish (IInstanceView& inst, const Data& data, Row&& onRow, Back&& onBack, Groups&& onGroups)
    {
        namespace nb = nativeui::modboard;
        const int count = data.count;
        const int lastFirst = std::max (0, ((count - 1) / nb::kListRows) * nb::kListRows);
        first_ = std::clamp (first_ - first_ % nb::kListRows, 0, lastFirst);
        nativeui::nativetext::ListPage page;
        std::snprintf (page.title, sizeof page.title, "%s", data.header != nullptr ? data.header : data.fallbackTitle);
        page.up = false;
        page.count = std::clamp (count - first_, 0, nb::kListRows);
        uint32_t signature = 2166136261u;
        auto mix = [&] (const char* text) { for (const char* c = text; *c != '\0'; ++c) signature = (signature ^ static_cast<unsigned char> (*c)) * 16777619u; signature = (signature ^ 0xffu) * 16777619u; };
        mix (page.title);
        for (int row = 0; row < page.count; ++row)
        {
            std::snprintf (page.rows[row], sizeof page.rows[row], "%s", data.items != nullptr && data.items[first_ + row] != nullptr ? data.items[first_ + row] : "");
            mix (page.rows[row]);
        }
        signature = (signature ^ static_cast<uint32_t> (first_ * 4 + count * 64 + (data.headerBack ? 2 : 0))) * 16777619u;
        if (! published_ || signature != signature_)
        {
            signature_ = signature;
            serial_ = (serial_ + 1) % 8000;
            inst.setNativeListText (&page);
            published_ = true;
        }
        const int identity = serial_ + 1;   // a new page: new cell codes, so the host reads the names again
        std::array<nativeui::FxCell, nb::kHandles> fields {};
        for (int row = 0; row < page.count; ++row)
        {
            fields[nb::ListRow + row] = { nb::kChoiceBias + first_ + row, identity, nativeui::kFxDial };
            fields[nb::ListRowGate + row] = { 1, 1, nativeui::kFxToggle };
        }
        for (int field : { nb::ListBack, nb::ListPrev, nb::ListNext, nb::ListGroups, nb::ListGate, nb::ListTitle })
            fields[field] = { 1, identity, nativeui::kFxToggle };
        inst.setNativeModBoard (2, fields.data());
        for (int field = nb::ListRow; field < nb::ListGate; ++field)
        {
            float value = 0.0f;
            if (! inst.takeNativeModBoard (field, fields[field], value) || value < 0.5f)
                continue;
            inst.setNativeModBoard (0, nullptr);   // the gesture is used: new cells at the next update (an unused gesture of the old page cannot act on the new one)
            if (field < nb::ListBack)
                onRow (first_ + field - nb::ListRow);
            else if (field == nb::ListBack)
                onBack();
            else if (field == nb::ListGroups)
                onGroups();
            else
                first_ = std::clamp (first_ + (field == nb::ListPrev ? -nb::kListRows : nb::kListRows), 0, lastFirst);
            return true;
        }
        return false;
    }

    /** The list is not on the screen: the cells and the names go away. */
    void hide (IInstanceView& inst)
    {
        if (! published_)
            return;
        inst.setNativeModBoard (0, nullptr);
        inst.setNativeListText (nullptr);
        published_ = false;
    }

    /** A new list (the first page again). */
    void restart() noexcept { first_ = 0; }
    /** Shows the page that holds `row` (index in the whole list; < 0: the first page). The next publish clamps it to the list. */
    void showRow (int row) noexcept { first_ = row > 0 ? row - row % nativeui::modboard::kListRows : 0; }

private:
    int first_ = 0, serial_ = 0;
    uint32_t signature_ = 0;
    bool published_ = false;
};

}} // namespace synthyboy_mpc::overlay
