// SynthyBoy - MPC overlay: which plugin instance's page is on screen. The host does not tell a plugin; the skin does: every instance owns a SLOT (0 .. 15, assigned when it is
// created, freed when it closes) and reports it through the display-only parameter "Overlay Instance", which a tiny film strip image of the skin shows as a colour (the table is in
// overlay_layout.json). This header has the pieces both sides share: the slot table, the colour <-> slot mapping and the parameter value of a slot.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdlib>

#include "overlay_layout.h"

namespace synthyboy_mpc { namespace overlay {

class IInstanceView;

constexpr int kSlotNone = layout::kMaxInstances;   // what the colour says for an instance beyond the 16 slots (not an index of the table)
constexpr int kSlotUnknown = -1;                   // the colour is blank (nothing reported yet) or none of the table's: it cannot be told

/** The frame of the film strip that shows `slot` (0 .. 15; any other number is "none", an instance without a slot): frame 0 is blank, frame slot + 1, frame 17 none. */
constexpr int instanceFrameOfSlot (int slot) noexcept { return slot >= 0 && slot < layout::kMaxInstances ? slot + 1 : layout::kMaxInstances + 1; }
/** The frame the instance reports while a plugin-drawn full screen is open: the skin hides the host widgets of the Play page at it (RangedEnabling, scripts/mpc-skin-extras/82_play.py) and shows it as blank. */
constexpr int kFullScreenFrame = 24;
constexpr float fullScreenParamValue() noexcept { return static_cast<float> (kFullScreenFrame) / static_cast<float> (layout::kInstanceSteps); }
/** The value of the display parameter "Overlay Instance" (the host picks frame value * kInstanceSteps; every value is a multiple of 1/32, exact in a float). */
constexpr float instanceParamValue (int slot) noexcept { return static_cast<float> (instanceFrameOfSlot (slot)) / static_cast<float> (layout::kInstanceSteps); }
/** The inverse for a debug value: the slot of a parameter value (kSlotNone for "none", kSlotUnknown for blank / out of range). */
inline int slotOfInstanceFrame (int frame) noexcept
{
    if (frame >= 1 && frame <= layout::kMaxInstances)
        return frame - 1;
    return frame == layout::kMaxInstances + 1 ? kSlotNone : kSlotUnknown;
}
/** The colour of film strip frame `frame` (0xRRGGBB). */
inline uint32_t instanceFrameRgb (int frame) noexcept
{
    const int s = slotOfInstanceFrame (frame);
    if (s >= 0 && s < layout::kMaxInstances)
        return layout::kInstanceSlotRgb[s];
    return s == kSlotNone ? layout::kInstanceNoneRgb : layout::kInstanceBlankRgb;
}

/** What a pixel colour says: the slot 0 .. 15, kSlotNone, or kSlotUnknown (blank or any other colour). The tolerance is the page detector's (2 per channel). */
inline int classifyInstanceRgb (uint32_t rgb, int tolerance = 2) noexcept
{
    auto close = [tolerance] (uint32_t a, uint32_t b)
    {
        for (int shift = 0; shift <= 16; shift += 8)
            if (std::abs (static_cast<int> ((a >> shift) & 255u) - static_cast<int> ((b >> shift) & 255u)) > tolerance)
                return false;
        return true;
    };
    for (int s = 0; s < layout::kMaxInstances; ++s)
        if (close (rgb, layout::kInstanceSlotRgb[s]))
            return s;
    return close (rgb, layout::kInstanceNoneRgb) ? kSlotNone : kSlotUnknown;
}

/** The slots of the process: 16 places for instance objects, the lowest free one is given out and a place is reused only after its instance was released. The instances beyond the
    sixteenth are counted without a slot. Not thread safe: the controller holds its lock around every call. */
class InstanceSlots
{
public:
    /** The slot given (0 .. 15), or -1 when all 16 are taken (the instance is then registered without one). */
    int acquire (IInstanceView* v)
    {
        for (int s = 0; s < layout::kMaxInstances; ++s)
            if (! slot_[s])
            {
                slot_[s] = v;
                ++generation_[s];
                return s;
            }
        if (overflow_ < kMaxOverflow)
            overflowList_[overflow_++] = v;
        return -1;
    }
    /** true: the instance was registered (with or without a slot). */
    bool release (const IInstanceView* v) noexcept
    {
        for (int s = 0; s < layout::kMaxInstances; ++s)
            if (slot_[s] == v)
            {
                slot_[s] = nullptr;
                return true;
            }
        for (int i = 0; i < overflow_; ++i)
            if (overflowList_[i] == v)
            {
                overflowList_[i] = overflowList_[--overflow_];
                return true;
            }
        return false;
    }
    /** How many instances have been given this slot so far (0 = never): a number that changes with every new holder, so that what was cached for the earlier instance of a slot is
        recognised as stale when the slot is reused (the slot number alone is the same). */
    unsigned long generation (int slot) const noexcept { return slot >= 0 && slot < layout::kMaxInstances ? generation_[slot] : 0; }
    IInstanceView* at (int slot) const noexcept { return slot >= 0 && slot < layout::kMaxInstances ? slot_[slot] : nullptr; }
    int slotOf (const IInstanceView* v) const noexcept
    {
        for (int s = 0; s < layout::kMaxInstances; ++s)
            if (slot_[s] == v)
                return s;
        return -1;
    }
    int slotted() const noexcept { int n = 0; for (auto* p : slot_) n += p != nullptr; return n; }
    int total() const noexcept { return slotted() + overflow_; }
    /** Bit s set = slot s is in use. */
    unsigned aliveMask() const noexcept { unsigned m = 0; for (int s = 0; s < layout::kMaxInstances; ++s) if (slot_[s]) m |= 1u << s; return m; }

private:
    static constexpr int kMaxOverflow = 256;   // instances beyond the 16: only their number matters
    IInstanceView* slot_[layout::kMaxInstances] = {};
    unsigned long generation_[layout::kMaxInstances] = {};
    const IInstanceView* overflowList_[kMaxOverflow] = {};
    int overflow_ = 0;
};

}} // namespace synthyboy_mpc::overlay
