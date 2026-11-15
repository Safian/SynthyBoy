// SynthyBoy - MPC UI: Macros View
// Full-screen view (1280x628) with 8 large macro cards in two rows, destination chips, and Bank 1 / Bank 2 Q-groups.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "qgroup.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** Single Macro Card (large dial, label, value text, destination chips row).
    A tap on the card or chip row requests opening the Mod Matrix filtered to this macro. */
class MacroCard : public Composite
{
public:
    int macroIndex = 0; // 0 .. 7
    Knob knob;

    struct Chip
    {
        char text[20] = {};
    };
    std::vector<Chip> chips;
    int extraDestCount = 0;

    MacroCard()
    {
        addKid (knob);
    }

    Size minSize() const override { return { 180, 160 }; }

    void setMacro (int idx, const char* name, double val)
    {
        macroIndex = idx;
        setText (name_, sizeof name_, name && name[0] ? name : defaultName (idx));
        knob.setLabel (name_);
        setValue (val);
    }

    void setValue (double val)
    {
        knob.state.value = std::clamp (val, 0.0, 1.0);
        std::snprintf (knob.state.valueText, sizeof knob.state.valueText, "%d%%", static_cast<int> (std::lround (knob.state.value * 100.0)));
    }

    void setDestinations (const std::vector<std::string>& dests)
    {
        chips.clear();
        extraDestCount = 0;
        const size_t maxChips = 3;
        for (size_t i = 0; i < dests.size() && i < maxChips; ++i)
        {
            Chip c;
            setText (c.text, sizeof c.text, dests[i].c_str());
            chips.push_back (c);
        }
        if (dests.size() > maxChips)
            extraDestCount = static_cast<int> (dests.size() - maxChips);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Rect cr = chipsRect();
        if (cr.contains (x, y))
        {
            tapCard_ = true;
            return Change::none();
        }
        return Composite::touchDown (x, y, nowMs);
    }

    Change touchUp (int x, int y) override
    {
        if (tapCard_)
        {
            tapCard_ = false;
            if (rect_.contains (x, y))
                return Change::request (id_);
        }
        return Composite::touchUp (x, y);
    }

    Rect chipsRect() const noexcept
    {
        return { rect_.x + 8, rect_.bottom() - 44, rect_.w - 16, 40 };
    }

protected:
    void layout() override
    {
        const int knobH = std::max (80, rect_.h - 48);
        knob.place ({ rect_.x + 8, rect_.y + 8, rect_.w - 16, knobH });
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rect_, qFrame_);

        const Rect cr = chipsRect();
        int cx = cr.x;

        if (chips.empty())
        {
            p.textCentered (cr, "No Destinations", T.fMin + 1.0f, p.c (T.textDim, 150), T.fMin);
            return;
        }

        for (const Chip& chip : chips)
        {
            const float tw = sdf::textWidth (chip.text, T.fMin + 1.0f);
            const int chipW = std::clamp (static_cast<int> (std::ceil (tw)) + 14, 44, 90);
            if (cx + chipW > cr.right() - (extraDestCount > 0 ? 44 : 0))
                break;
            const Rect chipBox { cx, cr.y + 4, chipW, cr.h - 8 };
            p.rrect (chipBox, T.rChip, p.c (T.panel0, 255));
            p.rrect (chipBox, T.rChip, p.c (T.keyline, 200), 1.0f);
            p.textCentered (chipBox, chip.text, T.fMin + 1.0f, p.c (T.text, 255), T.fMin);
            cx += chipW + 6;
        }

        if (extraDestCount > 0)
        {
            char buf[12];
            std::snprintf (buf, sizeof buf, "+%d", extraDestCount);
            const Rect moreBox { cx, cr.y + 4, 38, cr.h - 8 };
            p.rrect (moreBox, T.rChip, p.c (T.flow[0], 255));
            p.textCentered (moreBox, buf, T.fMin + 1.0f, p.c (T.onBright, 255), T.fMin);
        }
    }

private:
    char name_[24] = {};
    bool tapCard_ = false;

    static const char* defaultName (int idx) noexcept
    {
        static const char* const kNames[8] = {
            "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4",
            "MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"
        };
        return (idx >= 0 && idx < 8) ? kNames[idx] : "MACRO";
    }
};


// ===============================================================================================================================================================
/** Full-screen Macros View (1280x628) with 8 large macro cards, bank selection (1-4 / 5-8), and QGroup. */
class MacroView : public Composite
{
public:
    static constexpr int kBarH = 56;
    static constexpr int kNumMacros = 8;

    MenuButton close { "CLOSE", MenuButton::Icon::None };
    MenuButton bank1Btn { "BANK 1: 1-4", MenuButton::Icon::None };
    MenuButton bank2Btn { "BANK 2: 5-8", MenuButton::Icon::None };

    MacroCard cards[kNumMacros];
    QGroup qgroup;

    MacroView()
    {
        addKid (close);
        addKid (bank1Btn);
        addKid (bank2Btn);

        for (int i = 0; i < kNumMacros; ++i)
        {
            cards[i].setMacro (i, nullptr, 0.0);
            addKid (cards[i]);
        }

        setBank (0);
    }

    Size minSize() const override { return { 800, 400 }; }

    void setBank (int b)
    {
        bank_ = (b == 1 ? 1 : 0);
        updateQGroup();
    }

    int bank() const noexcept { return bank_; }

    void updateQGroup()
    {
        const int offset = bank_ * 4;
        for (int k = 0; k < 4; ++k)
            qgroup.set (k, &cards[offset + k].knob);
        qgroup.setActive (true);
    }

    void setMacroValue (int idx, double val)
    {
        if (idx >= 0 && idx < kNumMacros)
            cards[idx].setValue (val);
    }

    void setMacroDestinations (int idx, const std::vector<std::string>& dests)
    {
        if (idx >= 0 && idx < kNumMacros)
            cards[idx].setDestinations (dests);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        return Composite::touchDown (x, y, nowMs);
    }

    Change touchUp (int x, int y) override
    {
        const bool onBank1 = (cap_ == &bank1Btn);
        const bool onBank2 = (cap_ == &bank2Btn);

        const Change c = Composite::touchUp (x, y);

        if (onBank1) { setBank (0); return Change::none(); }
        if (onBank2) { setBank (1); return Change::none(); }

        return c;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        qgroup.draw (sb);
    }

    Change turnQ (int knobIndex, double delta)
    {
        return qgroup.turn (knobIndex, delta);
    }

protected:
    void layout() override
    {
        // Top bar
        close.place ({ rect_.x + 12, rect_.y + 4, 100, kBarH - 8 });
        bank1Btn.place ({ rect_.x + 380, rect_.y + 4, 140, kBarH - 8 });
        bank2Btn.place ({ rect_.x + 530, rect_.y + 4, 140, kBarH - 8 });

        // 2 rows x 4 columns grid
        const int gridX = rect_.x + 16;
        const int gridY = rect_.y + kBarH + 8;
        const int gridW = rect_.w - 32;
        const int gridH = rect_.h - kBarH - 16;

        const int colW = (gridW - 3 * 16) / 4;
        const int rowH = (gridH - 12) / 2;

        for (int r = 0; r < 2; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                const int idx = r * 4 + c;
                const int x = gridX + c * (colW + 16);
                const int y = gridY + r * (rowH + 12);
                cards[idx].place ({ x, y, colW, rowH });
            }
        }
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        // Top bar title
        p.textLeft ({ rect_.x + 124, rect_.y + 4, 160, kBarH - 8 }, "MACROS", T.fTitle, p.c (T.text, 255), T.fMin);

        // Highlight active bank button
        const MenuButton* activeBtn = (bank_ == 0 ? &bank1Btn : &bank2Btn);
        p.rrect (activeBtn->rect(), T.rControl, p.c (T.flow[0], 255), 1.6f);

        // Header separator
        p.line (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.y + kBarH - 1),
                static_cast<float> (rect_.right() - 8), static_cast<float> (rect_.y + kBarH - 1),
                1.5f, p.c (T.keyline, 255));
    }

private:
    int bank_ = 0;
};

}} // namespace synthyboy_mpc::ui
