// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy_mpc { namespace ui {

/** Pure logic for hold state of a clip indicator: a hit starts/re-arms a 2000 ms hold; tap clears. */
struct ClipHold
{
    static constexpr double kHoldMs = 2000.0;
    double holdUntil_ = -1.0;

    void update (bool hit, double nowMs) noexcept
    {
        if (hit)
            holdUntil_ = nowMs + kHoldMs;
    }

    bool active (double nowMs) const noexcept
    {
        return holdUntil_ >= 0.0 && nowMs <= holdUntil_;
    }

    void clear() noexcept
    {
        holdUntil_ = -1.0;
    }
};

}} // namespace synthyboy_mpc::ui
