// SynthyBoy - MPC UI component library: the BAND BOARD of a splitter (FxBandBoard), the part of the page of a splitter (L/H, L/M/H, M/S) that shows the effect racks of its bands. One LANE per band: a tile with the
// name of the band and what it is (below 500 Hz, the centre of the picture ...), then the chain of the band as cards (type picture, power, name, in processing order) and + ADD. A tap on a card, on the tile or
// on + ADD asks the owner to open that band's chain (the page of the band: fx_screen.h, fx_ui.h); the power of a card switches the effect, a drag moves it inside the lane. The board knows nothing of the engine:
// the owner hands it the names, the ranges and the cards (state), and gets the changes of the cards back (the ids: `kLaneIds` per lane).
// Same contract as every component (core.h): a state in, a rectangle, a change out.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"
#include "composite.h"
#include "core.h"
#include "fx_chain_bar.h"

namespace synthyboy_mpc { namespace ui {

/** The tile at the head of a lane: the band's name, its range and its state; one tap opens the band. */
class FxBandTile : public Component
{
public:
    enum Action : int { kOpen = 1 };
    struct State
    {
        char name[16] = {};     // LOW / MID / HIGH, SIDE
        char range[32] = {};    // "below 500 Hz", "the centre of the stereo picture"
        char info[24] = {};     // "3 effects", "empty", "SPLITTER OFF"
        int colour = 0;         // index into the flow colours of the look
        bool bypassed = false;  // the band is bypassed (it is taken out of the sum)
        bool idle = false;      // the splitter is off: the band's effects do nothing
    };
    State state;

    Size minSize() const override { return { 120, 56 }; }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (! enabled_)
            return Change::none();
        captured_ = true;
        tap_.down (x, y, nowMs);
        return Change::none();
    }
    Change touchMove (int x, int y) override
    {
        if (captured_)
            tap_.move (x, y);
        return Change::none();
    }
    Change touchUp (int, int) override
    {
        if (! captured_)
            return Change::none();
        captured_ = false;
        return tap_.up() != 0 ? Change::indexed (id_, kOpen, 0.0) : Change::none();
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const uint32_t col = T.flow[state.colour >= 0 && state.colour < 4 ? state.colour : 0];
        const bool dim = state.bypassed || state.idle;
        p.rrect (rect_, 6.0f, p.c (col, dim ? 30 : 70));
        p.rrect (rect_, 6.0f, p.c (col, dim ? 140 : 255), 1.4f);
        p.textLeft ({ rect_.x + 12, rect_.y + 4, std::max (0, rect_.w - 20), 28 }, state.name, T.fLabel + 3.0f, p.c (dim ? T.textDim : T.text, 255), T.fMin);
        p.textLeft ({ rect_.x + 12, rect_.y + 30, std::max (0, rect_.w - 20), 20 }, state.range, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        if (rect_.h >= 64)
            p.textLeft ({ rect_.x + 12, rect_.bottom() - 22, std::max (0, rect_.w - 20), 18 }, state.bypassed ? "BYPASS" : state.info, T.fLabel - 2.0f,
                        p.c (state.bypassed || state.idle ? T.text : T.textDim, 255), T.fMin);
    }

private:
    TapGesture tap_;
    bool captured_ = false;
};

class FxBandBoard : public Composite
{
public:
    static constexpr int kMaxBands = 3, kMaxCards = 13, kLaneIds = 16, kOpenId = 14, kAddId = 15, kTileW = 190;

    struct Lane
    {
        FxBandTile tile;
        FxSlotCard cards[kMaxCards];
        FxSlotCard add;
    };
    Lane lane[kMaxBands];

    FxBandBoard()
    {
        for (int b = 0; b < kMaxBands; ++b)
        {
            addKid (lane[b].tile, false);
            for (FxSlotCard& c : lane[b].cards)
            {
                c.state.selectOnPower = false;
                addKid (c, false);
            }
            lane[b].add.state.isPlus = true;
            addKid (lane[b].add, false);
        }
        assignIds (0);
    }

    Size minSize() const override { return { 600, 3 * 56 }; }

    /** The ids of the lanes: lane b has cards kLaneIds * b + 0 .. kMaxCards - 1, the tile kOpenId, + ADD kAddId (all from `base`). */
    void assignIds (int base)
    {
        base_ = base;
        for (int b = 0; b < kMaxBands; ++b)
        {
            for (int i = 0; i < kMaxCards; ++i)
                lane[b].cards[i].setId (base + b * kLaneIds + i);
            lane[b].tile.setId (base + b * kLaneIds + kOpenId);
            lane[b].add.setId (base + b * kLaneIds + kAddId);
        }
    }
    int baseId() const noexcept { return base_; }
    /** The lane and the number of a card (or kOpenId / kAddId) of a change's id; false when the id is not the board's. */
    bool decode (int id, int& band, int& what) const noexcept
    {
        const int r = id - base_;
        if (r < 0 || r >= kMaxBands * kLaneIds)
            return false;
        band = r / kLaneIds;
        what = r % kLaneIds;
        return band < numBands_;
    }

    int numBands() const noexcept { return numBands_; }
    int numCards (int b) const noexcept { return b >= 0 && b < kMaxBands ? numCards_[b] : 0; }
    /** The bands and the number of cards in each (the cards themselves are filled by the owner: lane[b].cards[i].state). */
    void setShape (int bands, const int* cards)
    {
        const int n = std::clamp (bands, 0, kMaxBands);
        bool same = n == numBands_;
        for (int b = 0; b < kMaxBands; ++b)
        {
            const int c = b < n ? std::clamp (cards[b], 0, kMaxCards) : 0;
            same = same && c == numCards_[b];
            numCards_[b] = c;
        }
        numBands_ = n;
        if (! same || ! shaped_)
        {
            shaped_ = true;
            if (rect_.w > 0)
                layout();
        }
    }

protected:
    void layout() override
    {
        const int gap = 6;
        const int n = std::max (1, numBands_);
        const int laneH = std::clamp ((rect_.h - (n - 1) * gap) / n, 56, 100);
        for (int b = 0; b < kMaxBands; ++b)
        {
            Lane& ln = lane[b];
            const bool used = b < numBands_;
            show (ln.tile, used);
            for (int i = 0; i < kMaxCards; ++i)
                show (ln.cards[i], used && i < numCards_[b]);
            const bool addShown = used && numCards_[b] < kMaxCards;
            show (ln.add, addShown);
            if (! used)
                continue;
            const int y = rect_.y + b * (laneH + gap);
            ln.tile.place ({ rect_.x, y, kTileW, laneH });
            const int k = numCards_[b] + (addShown ? 1 : 0);
            const int x0 = rect_.x + kTileW + gap;
            const int room = rect_.right() - x0;
            const int cardW = k > 0 ? std::clamp ((room - (k - 1) * gap) / k, 64, FxChainBar::kCardW) : FxChainBar::kCardW;
            int x = x0;
            for (int i = 0; i < numCards_[b]; ++i)
            {
                ln.cards[i].place ({ x, y, cardW, laneH });
                x += cardW + gap;
            }
            if (addShown)
                ln.add.place ({ x, y, cardW, laneH });
        }
    }

private:
    int base_ = 0, numBands_ = 0;
    int numCards_[kMaxBands] = {};
    bool shaped_ = false;
};

}} // namespace synthyboy_mpc::ui
