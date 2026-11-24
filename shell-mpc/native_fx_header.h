// SynthyBoy - source-bound actions in the native FX editor header.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"
#include "synthyboy/fx_rack.h"

namespace synthyboy_mpc::nativeui::fxheader
{
enum Layout { Hidden, SingleOff, SingleOn, MultiOff, MultiOn, MoreOff, MoreOn };
// Legacy frames 1..6 remain reserved; current header frames identify effect
// kind and power, while the existing Page handle carries an explicit tab index.
constexpr int kLastLayout = 6 + 2 * (synthyboy::kNumFxInstanceTypes + 2);
inline int layoutFor (int kind, bool on) noexcept { return 7 + 2 * (kind - 1) + (on ? 1 : 0); }
enum BandMode { NoBand, LowAndMid, AllBands, MidAndHigh };
constexpr int kBand = kFields;
static_assert (kItems == synthyboy::kNumFxSlots * 2);

struct State
{
    int item = -1, kind = 0, layout = Hidden, rack = 0;
    int band = -1, bandMode = NoBand;
    int tab = 0, panelCount = 0;

    bool permitsBand (int choice) const noexcept
    {
        return bandMode != NoBand && choice >= 0 && choice <= 2
            && (choice != 0 || bandMode != MidAndHigh) && (choice != 2 || bandMode != LowAndMid);
    }

    bool on() const noexcept { return layout >= 7 ? ((layout - 7) & 1) != 0 : layout == SingleOn || layout == MultiOn || layout == MoreOn; }
    int tabCount() const noexcept { return panelCount > 0 ? panelCount : layout >= MultiOff ? 2 : 1; }
    int tabIndex() const noexcept { return panelCount > 0 ? tab : layout == MoreOff || layout == MoreOn ? 1 : 0; }
    bool more() const noexcept { return tabIndex() != 0; }
    int rackCount() const noexcept
    {
        return item < 13 ? synthyboy::kNumFxRacks : synthyboy::kNumPoolRacks;
    }

    FxCell cell (int field) const noexcept
    {
        if (layout == Hidden || item < 0 || item >= kItems)
            return {};
        if (field == kBand)
            return permitsBand (band) && on() ? FxCell {item, kind | (bandMode << 5) | (band << 7), kFxDial} : FxCell {};
        if (field != Power && field != Page && ! on())
            return {};
        if (field == Page && tabCount() <= 1)
            return {};
        // The bank uses the item's own identity and type as its link, rather
        // than a real parameter. The editor applies all rack mutations.
        return { item, field == Page ? kind | (tabIndex() << 5) | (tabCount() << 7) : kind, kFxDial };
    }

    float value (int field) const noexcept
    {
        if (field == Power)
            return on() ? 1.0f : 0.0f;
        if (field == Rack)
            return static_cast<float> (rack) / (rackCount() - 1);
        if (field == Page)
            return static_cast<float> (tabIndex()) / 2.0f;
        if (field == kBand)
            return static_cast<float> (band + 1) / 3.0f;
        return 0.0f;
    }
};

/** Source-bound host handles all address this one active editor. An action
    queued before a target or preset transition cannot follow its replacement. */
class Bank
{
public:
    void publish (State state, uint64_t preset) noexcept
    {
        const auto previous = this->state();
        if (state.layout == Hidden)
            setRackPopup (false);
        if (preset_.load (std::memory_order_relaxed) != preset || previous.rack != state.rack)
            actions_.publish (Hidden, nullptr);
        FxCell fields[kFields + 1];
        for (int field = 0; field <= kBand; ++field)
            fields[field] = state.cell (field);
        actions_.publish (state.layout, fields);
        state_.store (pack (state), std::memory_order_release);
        preset_.store (preset, std::memory_order_release);
    }

    State state() const noexcept
    {
        const uint32_t state = state_.load (std::memory_order_acquire);
        return { static_cast<int> (state & 63u) - 1, static_cast<int> ((state >> 6) & 31u),
                 static_cast<int> ((state >> 11) & 63u), static_cast<int> ((state >> 17) & 15u),
                 static_cast<int> ((state >> 21) & 3u) - 1, static_cast<int> ((state >> 23) & 3u),
                 static_cast<int> ((state >> 25) & 3u), static_cast<int> ((state >> 27) & 3u) };
    }

    void post (int item, int field, float value, uint64_t preset) noexcept
    {
        if (preset_.load (std::memory_order_acquire) != preset)
            return;
        const auto link = actions_.at (field);
        if (link.cell().target == item
            && (field != Rack || rackPopupLink_.load (std::memory_order_acquire) == link.link))
            actions_.post (link, value);
    }

    void setRackPopup (bool open) noexcept
    {
        if (rackPopupOpen_ == open)
            return;
        rackPopupOpen_ = open;  // overlay thread only
        const auto link = actions_.at (Rack);
        rackPopupLink_.store (open && link.cell().target >= 0 ? link.link : 0, std::memory_order_release);
    }

    bool take (int field, const State& expected, float& value) noexcept
    {
        const auto current = state();
        const bool sameEditor = pack (current) == pack (expected);
        return actions_.take (field, sameEditor ? expected.cell (field) : FxCell {}, value);
    }

    using Snapshot = NativeCellBank<kFields + 1>::Snapshot;
    Snapshot at (int field) const noexcept { return actions_.at (field); }
    bool report (Snapshot link) noexcept { return actions_.report (link); }
    bool needsReport (Snapshot link) const noexcept { return actions_.needsReport (link); }

    /** Report only the selected source. Keep the layout hidden across a
        source swap until the replacement values reach the host. */
    template <class Report>
    void poll (float lastSource, Report&& reportValue)
    {
        const auto header = state();
        const float source = static_cast<float> (header.item + 1) / kItems;
        if (source != lastSource)
            reportValue (1, 0.0f, false);
        if (header.item >= 0)
            for (int field = 0; field < kFields; ++field)
            {
                const auto link = actions_.at (field);
                const auto cell = link.cell();
                const auto expected = header.cell (field);
                if (cell.target < 0 || cell.target != expected.target || cell.source != expected.source)
                    continue;
                reportValue (2 + header.item * kFields + field, header.value (field), actions_.needsReport (link));
                actions_.report (link);
            }
        reportValue (0, source, false);
        reportValue (1, static_cast<float> (header.layout) / kLastLayout, false);
    }

private:
    static uint32_t pack (State state) noexcept
    {
        return (state.item + 1) | (state.kind << 6) | (state.layout << 11) | (state.rack << 17)
            | ((state.band + 1) << 21) | (state.bandMode << 23) | (state.tab << 25) | (state.panelCount << 27);
    }

    std::atomic<uint32_t> state_ {};
    std::atomic<uint64_t> preset_ {};
    NativeCellBank<kFields + 1> actions_;
    bool rackPopupOpen_ = false;
    std::atomic<uint64_t> rackPopupLink_ {};
};
}
