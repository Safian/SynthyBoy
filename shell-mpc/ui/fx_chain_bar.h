// SynthyBoy - MPC UI component library: the FX CHAIN BAR components (FxSlotCard, FxChainBar).
// Displays the effect modules in processing order with type icon, name, power toggle, rack tag,
// drag-and-drop reordering, and a "+" button to add new effects.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"
#include "composite.h"
#include "core.h"
#include "synthyboy/fx_types.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** FxSlotCard: One effect slot card in the chain bar.
    Shows type pictogram, type name, power toggle, rack badge ("M", "B1", "B2"),
    or "+" for adding a new effect. Supports tap to select, and drag-and-drop to reorder. */
class FxSlotCard : public Component
{
public:
    enum Action : int
    {
        kSelect = 1,
        kTogglePower = 2,
        kMove = 3,
        kAdd = 4
    };

    struct State
    {
        int slot = 0;              // 0 .. 15
        int type = 0;              // synthyboy::FxInstanceType
        char name[24] = {};
        bool on = true;
        int rack = 0;              // 0 Main, 1 Bus 1, 2 Bus 2
        bool selected = false;
        bool isPlus = false;
        bool empty = false;
    };
    State state;

    FxSlotCard() = default;

    Size minSize() const override { return { 64, 56 }; }

    Rect powerRect() const noexcept
    {
        if (state.isPlus || state.empty)
            return {};
        return { rect_.right() - 56, rect_.y, 56, 44 };   // a touch target of its own
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (! enabled_)
            return Change::none();
        captured_ = true;
        dragCandidate_ = false;
        downX_ = x;
        downY_ = y;
        tap_.down (x, y, nowMs);

        // Check if power button was tapped
        const Rect pr = powerRect();
        if (! pr.empty() && pr.contains (x, y))
        {
            state.on = ! state.on;
            return Change::indexed (id_, kTogglePower, state.on ? 1.0 : 0.0);
        }
        return Change::none();
    }

    Change touchMove (int x, int y) override
    {
        if (! captured_ || ! enabled_ || state.isPlus || state.empty)
            return Change::none();
        tap_.move (x, y);
        if (std::abs (x - downX_) > 12)
            dragCandidate_ = true;
        return Change::none();
    }

    Change touchUp (int x, int y) override
    {
        (void) y;
        if (! captured_)
            return Change::none();
        captured_ = false;
        const int tapCode = tap_.up();

        if (state.isPlus)
        {
            return Change::indexed (id_, kAdd, 0.0);
        }

        if (dragCandidate_ && std::abs (x - downX_) > 24)
        {
            // Dragged to reorder: x determines target
            dragCandidate_ = false;
            return Change::indexed (id_, kMove, static_cast<double> (x));
        }

        if (tapCode != 0) // Tap on card selects slot
        {
            return Change::indexed (id_, kSelect, 0.0);
        }
        return Change::none();
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rect_, state.selected);

        // Selection / focus outline
        if (state.selected)
            p.rrect (rect_, T.rControl, p.c (qFrame_ ? T.q : T.flow[0], 255), qFrame_ ? 2.0f : 1.6f);
        else
            p.rrect (rect_, T.rControl, p.c (T.keyline, 160), 1.0f);

        // 1. "+" Add Card
        if (state.isPlus)
        {
            const float cx = static_cast<float> (rect_.x + rect_.w / 2);
            const float cy = static_cast<float> (rect_.y + rect_.h / 2);
            p.line (cx - 10.0f, cy, cx + 10.0f, cy, 2.0f, p.c (T.textDim, 220));
            p.line (cx, cy - 10.0f, cx, cy + 10.0f, 2.0f, p.c (T.textDim, 220));
            p.textCentered ({ rect_.x + 2, rect_.bottom() - 18, rect_.w - 4, 16 }, "ADD", T.fSmall, p.c (T.textDim, 180));
            return;
        }

        // 2. Active Slot Card
        // Rack badge on top left: "M", "B1", "B2"
        const char* const rackBadges[] = { "MAIN", "BUS 1", "BUS 2" };
        const char* badge = (state.rack >= 0 && state.rack < 3) ? rackBadges[state.rack] : "SPLIT";
        p.textLeft ({ rect_.x + 6, rect_.y + 4, std::max (0, rect_.w - 64), 16 }, badge, T.fSmall + 1.0f, p.c (T.flow[state.rack == 1 ? 1 : (state.rack == 2 ? 3 : 0)], 255), T.fMin);

        // Power toggle on top right
        const Rect pr = powerRect();
        if (! pr.empty())
        {
            const float pcx = static_cast<float> (pr.x + pr.w / 2);
            const float pcy = static_cast<float> (pr.y + pr.h / 2);
            const uint32_t pCol = state.on ? p.c (T.flow[0], 255) : p.c (T.textDim, 120);
            const float a0 = 1.5f * kPi + 0.6f;
            p.arc (pcx, pcy, 8.0f, a0, a0 + 2.0f * kPi - 1.2f, 2.0f, pCol);
            p.line (pcx, pcy - 10.0f, pcx, pcy - 1.0f, 2.0f, pCol);
        }

        // Type Pictogram in middle
        drawTypePictogram (p, rect_.x + rect_.w / 2, rect_.y + rect_.h / 2);

        // Type Name at bottom
        p.textCentered ({ rect_.x + 2, rect_.bottom() - 17, rect_.w - 4, 15 },
                        state.name[0] != '\0' ? state.name : "EMPTY",
                        T.fSmall, p.c (state.on ? T.text : T.textDim, 240));
    }

private:
    TapGesture tap_;
    bool captured_ = false, dragCandidate_ = false;
    int downX_ = 0, downY_ = 0;

    void drawTypePictogram (Pen& p, int cx, int cy) const
    {
        const theme::Look& T = theme::L();
        const float fcx = static_cast<float> (cx), fcy = static_cast<float> (cy - 1);
        const uint32_t col = state.on ? p.c (T.flow[0], 220) : p.c (T.textDim, 140);

        switch (state.type)
        {
            case synthyboy::kFxTypeEq: // Bell curve
                p.line (fcx - 12.0f, fcy + 4.0f, fcx - 5.0f, fcy + 4.0f, 1.5f, col);
                p.line (fcx - 5.0f, fcy + 4.0f, fcx, fcy - 6.0f, 1.5f, col);
                p.line (fcx, fcy - 6.0f, fcx + 5.0f, fcy + 4.0f, 1.5f, col);
                p.line (fcx + 5.0f, fcy + 4.0f, fcx + 12.0f, fcy + 4.0f, 1.5f, col);
                break;
            case synthyboy::kFxTypeCompressor: // Knee curve
                p.line (fcx - 10.0f, fcy + 6.0f, fcx, fcy - 1.0f, 1.5f, col);
                p.line (fcx, fcy - 1.0f, fcx + 10.0f, fcy - 5.0f, 1.5f, col);
                break;
            case synthyboy::kFxTypeDelay: // Clock / echoes
                p.arc (fcx, fcy, 7.0f, 0.0f, 2.0f * kPi, 1.2f, col);
                p.line (fcx, fcy, fcx, fcy - 4.0f, 1.2f, col);
                p.line (fcx, fcy, fcx + 3.0f, fcy, 1.2f, col);
                break;
            case synthyboy::kFxTypeDistortion: // Saturating S-curve
                p.line (fcx - 10.0f, fcy + 6.0f, fcx - 3.0f, fcy + 4.0f, 1.5f, col);
                p.line (fcx - 3.0f, fcy + 4.0f, fcx + 3.0f, fcy - 4.0f, 1.5f, col);
                p.line (fcx + 3.0f, fcy - 4.0f, fcx + 10.0f, fcy - 6.0f, 1.5f, col);
                break;
            case synthyboy::kFxTypeFilter: // Low-pass filter curve
                p.line (fcx - 10.0f, fcy - 5.0f, fcx, fcy - 5.0f, 1.5f, col);
                p.line (fcx, fcy - 5.0f, fcx + 10.0f, fcy + 6.0f, 1.5f, col);
                break;
            case synthyboy::kFxTypeChorus: // Dual waves
                p.line (fcx - 10.0f, fcy - 2.0f, fcx, fcy - 6.0f, 1.2f, col);
                p.line (fcx, fcy - 6.0f, fcx + 10.0f, fcy - 2.0f, 1.2f, col);
                p.line (fcx - 10.0f, fcy + 4.0f, fcx, fcy, 1.2f, col);
                p.line (fcx, fcy, fcx + 10.0f, fcy + 4.0f, 1.2f, col);
                break;
            case synthyboy::kFxTypeReverb: // Diffusion decay
                for (int i = 0; i < 4; ++i)
                {
                    const float h = 10.0f - static_cast<float> (i) * 2.5f;
                    p.line (fcx - 8.0f + static_cast<float> (i) * 5.0f, fcy + 5.0f,
                            fcx - 8.0f + static_cast<float> (i) * 5.0f, fcy + 5.0f - h, 1.5f, col);
                }
                break;
            default: // Generic diamond
                p.line (fcx - 6.0f, fcy, fcx, fcy - 6.0f, 1.2f, col);
                p.line (fcx, fcy - 6.0f, fcx + 6.0f, fcy, 1.2f, col);
                p.line (fcx + 6.0f, fcy, fcx, fcy + 6.0f, 1.2f, col);
                p.line (fcx, fcy + 6.0f, fcx - 6.0f, fcy, 1.2f, col);
                break;
        }
    }
};

// ===============================================================================================================================================================
/** FxChainBar: The top chain bar containing up to 16 FxSlotCards and a "+" card.
    Handles slot selection, reordering, and adding new modules. */
class FxChainBar : public Composite
{
public:
    static constexpr int kMaxSlots = 16;
    FxSlotCard cards[kMaxSlots];
    FxSlotCard plusCard;

    FxChainBar()
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            cards[i].state.slot = i;
            addKid (cards[i]);
        }
        plusCard.state.isPlus = true;
        addKid (plusCard);
    }

    Size minSize() const override { return { 400, 68 }; }

    int numActiveSlots() const noexcept { return numSlots_; }
    void setNumActiveSlots (int n) noexcept
    {
        const int m = std::clamp (n, 0, kMaxSlots);
        if (m == numSlots_ && laidOut_)
            return;
        numSlots_ = m;
        for (int i = 0; i < kMaxSlots; ++i)
            show (cards[i], i < numSlots_);
        show (plusCard, numSlots_ < kMaxSlots);
        if (rect_.w > 0)
            layout();
    }

    void assignIds (int baseId)
    {
        for (int i = 0; i < kMaxSlots; ++i)
            cards[i].setId (baseId + i);
        plusCard.setId (baseId + kMaxSlots);
    }

protected:
    void layout() override
    {
        const int pad = 4;
        const int gap = 6;
        const int cardH = rect_.h - 2 * pad;
        const int totalItems = numSlots_ + (numSlots_ < kMaxSlots ? 1 : 0);
        const int cardW = totalItems > 0 ? std::clamp ((rect_.w - 2 * pad - (totalItems - 1) * gap) / totalItems, 64, 130) : 80;
        laidOut_ = true;

        int x = rect_.x + pad;
        for (int i = 0; i < numSlots_; ++i)
        {
            cards[i].place ({ x, rect_.y + pad, cardW, cardH });
            x += cardW + gap;
        }
        if (numSlots_ < kMaxSlots)
        {
            plusCard.place ({ x, rect_.y + pad, cardW, cardH });
        }
    }

    void drawUnder (Pen& p) const override
    {
        p.block (rect_, false);
        p.rrect (rect_, 6.0f, p.c (theme::L().keyline, 140), 1.0f);
    }

private:
    int numSlots_ = 6;
    bool laidOut_ = false;
};

}} // namespace synthyboy_mpc::ui
