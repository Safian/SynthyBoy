// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "clip_hold.h"
#include "core.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

/** Small rounded CLIP indicator label.
    Dim with subtle outline when inactive; bright alert red fill with light text when active.
    Touch target is at least 56x56 px (touch minimum); a tap clears the hold. */
class ClipLabel : public Component
{
public:
    struct State
    {
        bool active = false;
    };
    State state;
    ClipHold hold;

    ClipLabel() = default;
    Size minSize() const override { return { 56, 56 }; }

    int touchTargets (Rect* out, int max) const override
    {
        if (max > 0)
        {
            out[0] = rect_;
            return 1;
        }
        return 0;
    }

    void update (bool hit, double nowMs) noexcept
    {
        hold.update (hit, nowMs);
        state.active = hold.active (nowMs);
    }

    void clear() noexcept
    {
        hold.clear();
        state.active = false;
    }

    Rect badgeRect() const noexcept
    {
        constexpr int badgeW = 50, badgeH = 20;
        const int bx = rect_.x + (rect_.w - badgeW) / 2;
        const int by = rect_.y + (rect_.h - badgeH) / 2;
        return { bx, by, badgeW, badgeH };
    }

protected:
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        return Change::none();
    }

    Change onMove (int x, int y) override
    {
        tap_.move (x, y);
        return Change::none();
    }

    Change onUp (int, int) override
    {
        if (tap_.up() != 0)
        {
            clear();
            return Change::indexed (id_, 0, 0.0);
        }
        return Change::none();
    }

    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = badgeRect();
        const float r = T.rChip;
        const uint32_t red = T.negative;

        if (state.active)
        {
            p.rrect (b, r, p.c (red, 255));
            p.textCentered (b, "CLIP", T.fSmall, p.c (T.text, 255), T.fMin);
        }
        else
        {
            p.rrect (b, r, p.c (T.panel1, 200));
            p.rrect (b, r, p.c (T.keyline, 160), 1.0f);
            p.textCentered (b, "CLIP", T.fSmall, p.c (T.textDim, 160), T.fMin);
        }
    }

private:
    TapGesture tap_;
};

}} // namespace synthyboy_mpc::ui
