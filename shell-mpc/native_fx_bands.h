// SynthyBoy - native band lanes of an FX splitter (components/native/band_lane): the data behind the tile, the cards and the ADD card of every lane.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The skin draws the lanes (one fxNativeBandLane definition, one entry per lane); the plugin publishes plain data here and takes the presses back as
// commands (FxUi: openBand, bandAdd, bandSelect, bandPower, bandMove). Per lane `kLaneHandles` handles (native_ui_params.h, fxbands::Field):
// Tile (0 hidden, else 1 + colour * 2 + dim: the tile face; its display is the band name), Range and Info (the other two lines of the tile),
// Count (the cards of the lane), Open (a tap on the tile), Add (a tap on the ADD card), AddAvail (a free module exists), then Select and Power per card
// (the display of Select is the name of the effect). A press is posted with the revision of the handle it was reported under: a press on a lane that
// changed since (the structure signature) is dropped. The drag that reorders a lane is the plugin's own gesture on the MOVE grip (no handle).
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "native_fx_cells.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "ui/fx_layout.h"

namespace synthyboy_mpc::nativeui::fxbands
{
constexpr int kStates = 9;   // State values 0 .. 8 (components/sizes.json band_lane.states)
// The place of the lanes, tab coordinates (the tab starts at y 110 of the screen): mirrors components/sizes.json band_lane.* (checked by scripts/tests/test_band_lane.py).
namespace geometry
{
constexpr int kX = 8, kY = 208, kPitch = 70, kHeight = 64, kTileW = 190, kCardX = 196, kCardPitch = 76, kCardW = 72, kPowerH = 32;
constexpr int kGripX = 32, kGripW = 40, kGripH = 32, kAddX = 1184, kAddW = 72;
}

struct Card
{
    int item = -1;   // the module of the lane (FxRackItem index + pool offset): the identity of a press
    int kind = 0;    // synthyboy::FxInstanceType
    bool on = false;
};

struct Lane
{
    int kind = 0, band = 0, colour = 0, count = 0;   // the splitter kind (fxl::kKindSplitter*), the band, the flow colour (0 .. 3), the cards
    float low = 0.0f, high = 0.0f;                   // the crossover frequencies in Hz (L/H: low only; L/M/H: both)
    bool bypassed = false, idle = false, addAvail = false;
    std::array<Card, kCards> cards {};

    bool dim() const noexcept { return bypassed || idle; }
    int state() const noexcept { return 1 + colour * 2 + (dim() ? 1 : 0); }
};

struct State
{
    bool shown = false;
    int bands = 0;
    uint32_t structure = 0;
    std::array<Lane, kLanes> lane {};

    FxCell cell (int handle) const noexcept
    {
        const int l = handle / kLaneHandles, field = handle % kLaneHandles;
        if (! shown || l >= bands)
            return {};
        const Lane& ln = lane[static_cast<size_t> (l)];
        if (field == Open || (field == Add && ln.addAvail))
            return { l, field, kFxDial };
        if (field >= Select && field < Select + kCards && field - Select < ln.count)
            return { ln.cards[static_cast<size_t> (field - Select)].item, ln.cards[static_cast<size_t> (field - Select)].kind, kFxDial };
        if (field >= Power && field < Power + kCards && field - Power < ln.count)
            return { ln.cards[static_cast<size_t> (field - Power)].item, ln.cards[static_cast<size_t> (field - Power)].kind, kFxDial };
        return {};
    }
};

// ---- the texts of the tile and the cards (shared by the host's display strings and the tests) ---------------------------------------------------------
inline void hzText (float hz, char* out, size_t cap) noexcept
{
    if (hz >= 1000.0f)
        std::snprintf (out, cap, "%.1f kHz", static_cast<double> (hz) / 1000.0);
    else
        std::snprintf (out, cap, "%.0f Hz", static_cast<double> (hz));
}
inline void nameText (const Lane& lane, char* out, size_t cap) noexcept { std::snprintf (out, cap, "%s", ui::fxl::bandName (lane.kind, lane.band)); }
inline void rangeText (const Lane& lane, char* out, size_t cap) noexcept
{
    char a[16] = {}, b[16] = {};
    hzText (lane.low, a, sizeof a);
    hzText (lane.high, b, sizeof b);
    const int bands = ui::fxl::bandsOfSplitter (lane.kind);
    if (lane.kind == ui::fxl::kKindSplitterMs)
        std::snprintf (out, cap, "%s", lane.band == 0 ? "the centre of the picture" : "what makes it wide");
    else if (lane.band == 0)
        std::snprintf (out, cap, "below %s", a);
    else if (lane.band == bands - 1)
        std::snprintf (out, cap, "above %s", bands == 3 ? b : a);
    else
        std::snprintf (out, cap, "%s to %s", a, b);
}
inline void infoText (const Lane& lane, char* out, size_t cap) noexcept
{
    if (lane.bypassed)
        std::snprintf (out, cap, "BYPASS");
    else if (lane.idle)
        std::snprintf (out, cap, "SPLITTER OFF");
    else if (lane.count == 0)
        std::snprintf (out, cap, "no effect");
    else
        std::snprintf (out, cap, "%d effect%s", lane.count, lane.count == 1 ? "" : "s");
}
inline void cardText (int kind, char* out, size_t cap) noexcept
{
    std::snprintf (out, cap, "%s", kind > 0 && kind < synthyboy::kNumFxInstanceTypes ? synthyboy::fxTypeDescriptor (kind).name : "");
}

/** The maximum of a handle as the host reads it (effGetParameterProperties): the steps of the value. */
constexpr int maxOf (int field) noexcept { return field == Tile ? kStates - 1 : field == Count ? kCards : 1; }
constexpr bool isAction (int field) noexcept { return field == Open || field == Add || (field >= Select && field < Select + 2 * kCards); }
constexpr bool hasText (int field) noexcept { return field <= Info || (field >= Select && field < Select + kCards); }

class Bank
{
public:
    void publish (const State& s, uint64_t preset) noexcept
    {
        if (structure_ != s.structure || preset_.load (std::memory_order_relaxed) != preset || shown_ != s.shown)
            actions_.invalidate();
        structure_ = s.structure;
        shown_ = s.shown;
        for (int l = 0; l < kLanes; ++l)
        {
            const Lane& ln = s.lane[static_cast<size_t> (l)];
            const bool live = s.shown && l < s.bands;
            laneBits_[l].store (live ? static_cast<uint32_t> (1 | (ln.kind << 1) | (ln.band << 5) | (ln.colour << 7) | (ln.count << 9)
                                                           | (ln.bypassed ? 1 << 13 : 0) | (ln.idle ? 1 << 14 : 0) | (ln.addAvail ? 1 << 15 : 0)) : 0u,
                                std::memory_order_release);
            low_[l].store (ln.low, std::memory_order_relaxed);
            high_[l].store (ln.high, std::memory_order_relaxed);
            for (int i = 0; i < kCards; ++i)
                cards_[l][i].store (i < ln.count ? ln.cards[static_cast<size_t> (i)].kind | (ln.cards[static_cast<size_t> (i)].on ? 1 << 5 : 0) : 0, std::memory_order_release);
        }
        FxCell links[kHandles];
        for (int h = 0; h < kHandles; ++h)
            links[h] = s.cell (h);
        actions_.publish (s.shown ? 1 : 0, links);
        preset_.store (preset, std::memory_order_release);
    }

    /** The lane as published (the fields the texts need). */
    Lane lane (int l) const noexcept
    {
        Lane ln;
        const uint32_t bits = laneBits_[l].load (std::memory_order_acquire);
        if ((bits & 1u) == 0)
            return ln;
        ln.kind = static_cast<int> ((bits >> 1) & 15u);
        ln.band = static_cast<int> ((bits >> 5) & 3u);
        ln.colour = static_cast<int> ((bits >> 7) & 3u);
        ln.count = static_cast<int> ((bits >> 9) & 15u);
        ln.bypassed = (bits >> 13 & 1u) != 0;
        ln.idle = (bits >> 14 & 1u) != 0;
        ln.addAvail = (bits >> 15 & 1u) != 0;
        ln.low = low_[l].load (std::memory_order_relaxed);
        ln.high = high_[l].load (std::memory_order_relaxed);
        return ln;
    }
    bool shown (int l) const noexcept { return (laneBits_[l].load (std::memory_order_acquire) & 1u) != 0; }
    Card card (int l, int i) const noexcept
    {
        const int bits = i >= 0 && i < kCards ? cards_[l][i].load (std::memory_order_acquire) : 0;
        return { -1, bits & 31, (bits & 32) != 0 };
    }

    float value (int handle) const noexcept
    {
        const int l = handle / kLaneHandles, field = handle % kLaneHandles;
        if (handle < 0 || l >= kLanes || ! shown (l))
            return 0.0f;
        const Lane ln = lane (l);
        if (field == Tile)
            return static_cast<float> (ln.state()) / (kStates - 1);
        if (field == Range || field == Info)
            return 1.0f;
        if (field == Count)
            return static_cast<float> (ln.count) / kCards;
        if (field == AddAvail)
            return ln.addAvail ? 1.0f : 0.0f;
        if (field >= Power && field < Power + kCards)
            return field - Power < ln.count && card (l, field - Power).on ? 1.0f : 0.0f;
        return 0.0f;
    }

    /** The display string of a handle (Tile: the band name, Range, Info, Select: the effect name); empty for the others. */
    void text (int handle, char* out, size_t cap) const noexcept
    {
        out[0] = '\0';
        const int l = handle / kLaneHandles, field = handle % kLaneHandles;
        if (handle < 0 || l >= kLanes || ! shown (l))
            return;
        const Lane ln = lane (l);
        if (field == Tile)
            nameText (ln, out, cap);
        else if (field == Range)
            rangeText (ln, out, cap);
        else if (field == Info)
            infoText (ln, out, cap);
        else if (field >= Select && field < Select + kCards && field - Select < ln.count)
            cardText (card (l, field - Select).kind, out, cap);
    }

    void post (int handle, float value, uint64_t preset) noexcept
    {
        if (handle < 0 || handle >= kHandles || preset_.load (std::memory_order_acquire) != preset)
            return;
        actions_.post (actions_.at (handle), value);
    }

    bool take (int handle, const State& expected, float& value) noexcept
    {
        const bool same = actions_.layout() == (expected.shown ? 1 : 0) && structure_ == expected.structure;
        return actions_.take (handle, same ? expected.cell (handle) : FxCell {}, value);
    }

    /** Reports every live handle (display thread). `report (handle, value, force)`; returns true when a text changed (the host must read the names again). */
    template <class Report>
    bool poll (Report&& report)
    {
        bool labels = false;
        for (int h = 0; h < kHandles; ++h)
        {
            const int field = h % kLaneHandles;
            const auto link = actions_.at (h);
            const bool action = isAction (field), live = action && link.cell().target >= 0;
            const bool lit = shown (h / kLaneHandles);
            if (action && ! live)
            {
                reportedKey_[h] = 0;
                continue;
            }
            if (! action && ! lit && reportedKey_[h] == 0)
                continue;
            uint32_t key = 0;
            if (hasText (field))
            {
                char buf[40];
                text (h, buf, sizeof buf);
                key = 2166136261u;
                for (const char* c = buf; *c != '\0'; ++c)
                    key = (key ^ static_cast<uint8_t> (*c)) * 16777619u;
                key |= 1u;
            }
            else
                key = lit ? 1u : 0u;
            const bool textMoved = hasText (field) && key != reportedKey_[h];
            const bool moved = (live && actions_.needsReport (link)) || textMoved;
            report (h, value (h), moved);
            reportedKey_[h] = lit || live ? key : 0;
            labels |= moved && hasText (field);
            if (live)
                actions_.report (link);
        }
        return labels;
    }

private:
    NativeCellBank<kHandles> actions_;
    std::atomic<uint32_t> laneBits_[kLanes] {};
    std::atomic<float> low_[kLanes] {}, high_[kLanes] {};
    std::atomic<int> cards_[kLanes][kCards] {};
    std::atomic<uint64_t> preset_ {};
    uint32_t structure_ = 0;   // the overlay thread only
    bool shown_ = false;
    uint32_t reportedKey_[kHandles] {};   // the display thread only
};
} // namespace synthyboy_mpc::nativeui::fxbands
