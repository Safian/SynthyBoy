// SynthyBoy - MPC UI component library: Carousel, the selector of the three thirds of the Play pages (the oscillator source, the envelope, the LFO). The current entry stands in the middle at full size and brightness, the previous entry at
// its left and the next entry at its right, smaller and dimmer and faded towards the edges, as in a carousel. A tap on a neighbour steps to it (the neighbours ARE the arrows: there is no room for chevrons), a tap on the name opens the
// list of all entries (a Request: the owner opens a ListPopup), a horizontal drag across it steps once per kDragStepPx pixels (the labels snap: no animation, so that the pictures are deterministic and cost nothing between touches).
// It is a drop-in for the Stepper in the places that use it as such: the same state fields (`count`, `index`, `wrap`, `text`), the same changes (Index with the new index and the id; Request with the id for the middle), the same turn.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

#include "base.h"

namespace synthyboy_mpc { namespace ui {

class Carousel : public Component
{
public:
    struct State
    {
        int count = 0, index = 0;
        char text[48] = {};                 // the name of the current entry (used when `names` is null)
        const char* const* names = nullptr; // the owner's array of the names of all entries (`count` of them, not copied): the neighbours are named from it; null: only the current entry is shown
        bool wrap = false;                  // the list goes round at the ends: the neighbour of the last entry is the first (and the other way round); without it the missing neighbour is empty and the end does not step
        bool dim = false;                   // the name drawn dim
        unsigned dotMask = 0;               // bit i: a small dot beside the name of entry i (the envelopes that have targets); the dot of a neighbour is smaller
        const uint32_t* dotColours = nullptr; // the colour of the dot of entry i (rgb, one entry per entry); null: the accent colour
    };
    State state;
    bool listable = true;                   // a tap on the middle is a Request (the owner opens the list); false: the middle does nothing
    static constexpr int kDragStepPx = 60, kMinW = 240;

    void setItem (const char* text) { setText (state.text, sizeof state.text, text); }
    Size minSize() const override { return { kMinW, 56 }; }

    /** The rectangles of the three zones (the touch targets): the left neighbour, the name, the right neighbour. The neighbours are a quarter of the width each, at least 56 px. */
    Rect zone (int which) const noexcept   // 0 left, 1 middle, 2 right
    {
        const int side = std::max (56, rect_.w / 4);
        if (which == 0) return { rect_.x, rect_.y, side, rect_.h };
        if (which == 2) return { rect_.right() - side, rect_.y, side, rect_.h };
        return { rect_.x + side, rect_.y, std::max (0, rect_.w - 2 * side), rect_.h };
    }
    int touchTargets (Rect* out, int max) const override
    {
        int n = 0;
        for (int z = 0; z < 3 && n < max; ++z)
            out[n++] = zone (z);
        return n;
    }
    /** The entry `step` places from the current one (-1 the previous, +1 the next); -1 when there is none (an end without `wrap`, or no entries). */
    int neighbour (int step) const noexcept
    {
        if (state.count <= 0)
            return -1;
        int n = state.index + step;
        if (state.wrap)
            return ((n % state.count) + state.count) % state.count;
        return n >= 0 && n < state.count ? n : -1;
    }
    /** The name of entry i as the carousel writes it ("" when there is none). */
    const char* nameOf (int i) const noexcept
    {
        if (i == state.index && state.text[0] != '\0')
            return state.text;
        return state.names != nullptr && i >= 0 && i < state.count && state.names[i] != nullptr ? state.names[i] : (i == state.index ? state.text : "");
    }
    double wheelUnit() const noexcept override { return 1.0; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = rect_.inset (2);
        p.control (b, qFrame_);
        const Rect zl = zone (0).inset (2), zm = zone (1), zr = zone (2).inset (2);
        const uint32_t accent = state.dotColours != nullptr ? 0 : T.flow[0];
        auto dotColour = [&] (int i) { return state.dotColours != nullptr ? state.dotColours[i] : accent; };
        const float sideSize = T.fLabel + 2.0f;
        // the neighbours: smaller and dim
        for (int s = -1; s <= 1; s += 2)
        {
            const int n = neighbour (s);
            if (n < 0 || n == state.index)
                continue;
            const Rect z = s < 0 ? zl : zr;
            const bool dot = n < 32 && (state.dotMask & (1u << n)) != 0;
            const Rect box { z.x + 6, z.y + 2, std::max (0, z.w - 12 - (dot ? 12 : 0)), std::max (0, z.h - 4) };
            p.textCentered (box, nameOf (n), sideSize, p.c (T.textDim, 130), T.fMin);
            if (dot)
                p.disc (static_cast<float> (s < 0 ? z.right() - 10 : z.x + 10), static_cast<float> (z.y + z.h / 2), 2.5f, p.c (dotColour (n), 130));
        }
        // the current entry: full size and brightness (a dot of its own beside the name when it has one)
        const bool dot = state.index >= 0 && state.index < 32 && (state.dotMask & (1u << state.index)) != 0;
        const Rect box { zm.x + 4 + (dot ? 14 : 0), zm.y + 2, std::max (0, zm.w - 8 - (dot ? 14 : 0)), std::max (0, zm.h - 4) };
        const char* cur = nameOf (state.index);
        const float size = p.textCentered (box, cur, T.fValue, p.c (T.text, state.dim ? 90 : 255), T.fMin);
        if (dot)
        {
            const float tw = size > 0.0f ? Pen::textWidth (cur, size) : 0.0f;
            p.disc (static_cast<float> (box.x + box.w / 2) - tw * 0.5f - 10.0f, static_cast<float> (box.y + box.h / 2), 4.0f, p.c (dotColour (state.index), 255));
        }
        // the fade towards the edges: three strips of the control's own colour over the outer end of each neighbour
        for (int k = 0; k < 3; ++k)
        {
            const int a = 150 - 50 * k;
            p.rect (b.x + 2 + 7 * k, b.y + 3, b.x + 2 + 7 * (k + 1), b.bottom() - 3, p.c (T.panel1, a));
            p.rect (b.right() - 2 - 7 * (k + 1), b.y + 3, b.right() - 2 - 7 * k, b.bottom() - 3, p.c (T.panel1, a));
        }
    }
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        x0_ = x;
        startIndex_ = state.index;
        stepsDone_ = 0;
        dragged_ = false;
        zone_ = x < zone (1).x ? 0 : (x >= zone (2).x ? 2 : 1);
        return Change::none();
    }
    Change onMove (int x, int) override
    {
        if (state.count <= 1)
            return Change::none();
        if (std::abs (x - x0_) < TapGesture::kSlop && ! dragged_)
            return Change::none();
        dragged_ = true;
        // dragging to the right brings the previous entries (the row slides with the finger); one step per kDragStepPx
        const int want = (x - x0_) / kDragStepPx;
        if (want == stepsDone_)
            return Change::none();
        stepsDone_ = want;
        return goTo (startIndex_ - want);
    }
    Change onUp (int, int) override
    {
        const bool tap = tap_.up() != 0 && ! dragged_;
        dragged_ = false;
        if (! tap || state.count <= 1)
            return tap && zone_ == 1 && listable ? Change::request (id_) : Change::none();
        if (zone_ == 1)
            return listable ? Change::request (id_) : Change::none();
        return goTo (state.index + (zone_ == 0 ? -1 : 1));
    }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        return d == 0 ? Change::none() : goTo (state.index + static_cast<int> (std::clamp<long> (d, -100000, 100000)));
    }

private:
    Change goTo (int n) const
    {
        if (state.count <= 1)
            return Change::none();
        if (state.wrap)
            n = ((n % state.count) + state.count) % state.count;
        else
            n = std::clamp (n, 0, state.count - 1);
        return n == state.index ? Change::none() : Change::indexed (id_, n, n);
    }
    TapGesture tap_;
    int x0_ = 0, startIndex_ = 0, stepsDone_ = 0, zone_ = 1;
    bool dragged_ = false;
};

}} // namespace synthyboy_mpc::ui
