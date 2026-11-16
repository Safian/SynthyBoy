// SynthyBoy - MPC in-host drawing (experimental): the few things the overlay controller shares with the in-host path, in a header with no system dependencies (the controller includes it only in a build with
// SYNTHYBOY_MPC_INHOST_DRAW): what picture it is about to hand over, and where the host's Q-Link status pop-up begins in the frame on screen (found by the hook in the same frame).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>

namespace synthyboy_mpc { namespace overlay { namespace inhost {

struct FrameNote
{
    int view = -1, slot = -1, rotation = 90;
};

struct Glue
{
    /** The picture the overlay thread is about to present (written and read by the overlay thread only). */
    static FrameNote& note() noexcept { static FrameNote n; return n; }
    /** true while the in-host path presents the pictures (the controller's key follows the hook's pop-up edge then). */
    static std::atomic<bool>& active() noexcept { static std::atomic<bool> a { false }; return a; }
    /** The hook's cut x of the frame on screen. */
    static std::atomic<const std::atomic<int>*>& edgeSource() noexcept { static std::atomic<const std::atomic<int>*> p { nullptr }; return p; }
};

/** The controller: the picture of view / slot under `rotation` is complete and is presented next. */
inline void noteFrame (int view, int slot, int rotation) noexcept
{
    FrameNote& n = Glue::note();
    n.view = view;
    n.slot = slot;
    n.rotation = rotation;
}
inline bool hostEdgeActive() noexcept { return Glue::active().load (std::memory_order_relaxed); }
/** The left edge of the host's pop-up in the frame on screen (upright UI x), 1280 (PanelKey::kNoKey) when there is none. */
inline int hostEdge() noexcept
{
    const std::atomic<int>* p = Glue::edgeSource().load (std::memory_order_acquire);
    return p != nullptr ? p->load (std::memory_order_relaxed) : 1280;
}

}}} // namespace synthyboy_mpc::overlay::inhost
