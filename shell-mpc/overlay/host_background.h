// SynthyBoy - MPC overlay: the state machine of a page whose static part is a picture of the HOST's skin (docs/plans/host-skin-background.md, "sentinel").
// A view that has a background in the skin starts every time OPAQUE: our own plane draws the whole page, ground included (the host's own picture of the tab, its native widgets or the tab before the
// switch, is covered completely). Only when the page detector has read the SENTINEL (a 5 x 5 square of a colour the background has and the host's own picture does not) on the host's primary framebuffer
// does the page switch to the live pass alone (a transparent plane over the host's background). Any page change, a hidden page or a sentinel that is gone puts it back to opaque in the same step.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy_mpc { namespace overlay {

class HostBackgroundGate
{
public:
    /** One detection. `view`: the view on screen (-1: none), `capable`: that view has a background in the skin, `sentinelSeen`: the sentinel of that view is on the host's framebuffer right now (only
        meaningful with `capable`). Returns true: the view draws its live pass only (transparent plane), false: it draws everything (opaque). */
    bool step (int view, bool capable, bool sentinelSeen) noexcept
    {
        if (view != view_)
        {
            view_ = view;
            on_ = false;   // a page change resets: the new page is opaque until ITS sentinel is read
        }
        on_ = view >= 0 && capable && sentinelSeen;
        return on_;
    }
    bool on() const noexcept { return on_; }
    int view() const noexcept { return view_; }
    void reset() noexcept { view_ = -1; on_ = false; }

private:
    int view_ = -1;
    bool on_ = false;
};

}} // namespace synthyboy_mpc::overlay
