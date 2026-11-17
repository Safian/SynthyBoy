// SynthyBoy - MPC UI: the page of the Macros (1280 x 628): the eight macros as cards in two rows of four, each with its knob (a name and a value beside the dial), the chips of what it drives under it, and under
// the cards the live spectrum of the output (the strip the 32 meters of the host skin were). BANK 1 (the macros 1 - 4, the top row) and BANK 2 (5 - 8, the bottom row) are the two Q groups.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "led_list.h"
#include "qgroup.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** The spectrum of the output as one line with a faint fill on a log frequency axis (the 256 levels 0 .. 1 of the owner, 20 Hz .. 20 kHz): the picture of the live curve. No touch. */
class SpectrumStrip : public Component
{
public:
    static constexpr int kPoints = 256;
    const float* levels = nullptr;   // 0 .. 1 per point, null: nothing is drawn but the frame

    Size minSize() const override { return { 400, 100 }; }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen p (sb, enabled_);
        Cell cell (sb, rect_);
        const theme::Look& T = theme::L();
        p.block (rect_);
        const Rect in { rect_.x + 10, rect_.y + 22, std::max (0, rect_.w - 20), std::max (0, rect_.h - 22 - 20) };
        if (in.w < 20 || in.h < 20)
            return;
        // the scale: the level axis is linear in dB from -60 (the base line) to -12 dB (the top), the same as the 32 meters of the skin: faint lines every 12 dB with their numbers, so that a silent strip still says what it measures
        for (int k = 1; k <= 4; ++k)
        {
            const float gy = static_cast<float> (in.bottom()) - static_cast<float> (k) * 0.25f * static_cast<float> (in.h - 4);
            p.line (static_cast<float> (in.x), gy, static_cast<float> (in.right()), gy, 1.0f, p.c (T.grid, k == 4 ? 200 : 110));
            char db[8];
            std::snprintf (db, sizeof db, "%d", -60 + 12 * k);
            p.textLeft ({ in.x + 3, static_cast<int> (gy) + 1, 30, 13 }, db, T.fSmall, p.c (T.textFaint, 255), T.fMin);
        }
        p.textRight ({ in.right() - 34, in.y, 32, 13 }, "dB", T.fSmall, p.c (T.textFaint, 255), T.fMin);
        p.textLeft ({ rect_.x + 14, rect_.y + 3, 200, 16 }, "SPECTRUM", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        static const struct { double hz; const char* name; } kDecade[3] = { { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } };
        for (const auto& d : kDecade)
        {
            const float x = static_cast<float> (in.x) + static_cast<float> (std::log10 (d.hz / 20.0) / 3.0 * in.w);
            p.line (x, static_cast<float> (in.y), x, static_cast<float> (in.bottom()), 1.0f, p.c (T.grid, 255));
            p.textCentered ({ static_cast<int> (x) - 24, in.bottom() + 2, 48, 16 }, d.name, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }
        p.textLeft ({ in.x, in.bottom() + 2, 50, 16 }, "20 Hz", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        p.textRight ({ in.right() - 50, in.bottom() + 2, 50, 16 }, "20 kHz", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        if (levels == nullptr)
            return;
        float rows[gpu::kMaxCurve];
        const int n = std::min (kPoints, static_cast<int> (gpu::kMaxCurve));
        for (int i = 0; i < n; ++i)
            rows[i] = static_cast<float> (in.bottom()) - std::clamp (levels[i], 0.0f, 1.0f) * static_cast<float> (in.h - 4);
        bool sound = false;
        for (int i = 0; i < n && ! sound; ++i)
            sound = levels[i] > 0.0f;
        sb.curve (rows, n, in.x, in.w);
        if (sound)
            sb.fill (static_cast<float> (in.bottom()), colour::rgba (T.flow[0], 40));
        sb.stroke (0.8f, colour::rgba (T.flow[0], sound ? 255 : 90));   // no sound: only the faint base line
    }

protected:
    void drawBody (Pen&) const override {}
};

// ===============================================================================================================================================================
/** Single Macro Card: the knob (name and value beside the dial) and under it the chips of the targets the macro drives (a modulation slot each). A tap on the chips asks for the targets of the macro. */
class MacroCard : public Composite
{
public:
    static constexpr int kMaxChips = 4, kChipsH = 56;
    int macroIndex = 0; // 0 .. 7
    Knob knob;

    struct Chip { char text[40] = {}; };
    Chip chips[kMaxChips];
    int numChips = 0;
    int extraDestCount = 0;

    MacroCard()
    {
        knob.state.side = true;
        addKid (knob);
    }

    Size minSize() const override { return { 280, 190 }; }

    void setMacro (int idx, const char* name)
    {
        macroIndex = idx;
        knob.setLabel (name && name[0] ? name : "MACRO");
    }
    void setValue (double val, const char* text)
    {
        knob.state.value = knob.state.modValue = std::clamp (val, 0.0, 1.0);
        ui::setText (knob.state.valueText, sizeof knob.state.valueText, text);
    }
    /** The names of the targets (the owner passes `total` and the first kMaxChips names). */
    void setDestinations (const char* const* names, int shown, int total)
    {
        numChips = std::clamp (shown, 0, kMaxChips);
        for (int i = 0; i < numChips; ++i)
            setText (chips[i].text, sizeof chips[i].text, names[i]);
        extraDestCount = std::max (0, total - numChips);
        totalDests_ = total;
    }

    Rect chipsRect() const noexcept { return { rect_.x + 6, rect_.bottom() - 6 - kChipsH, rect_.w - 12, kChipsH }; }
    /** The name beside the dial is a button when `nameId` is set: a tap on it asks for the keyboard page that renames the macro (Request nameId). The touch target is 56 pixels high around the name's line. */
    int nameId = 0;
    Rect nameRect() const noexcept
    {
        const Knob::Geom g = knob.geom();
        const int bottom = g.labelBox.y + g.labelBox.h + 6, top = std::max (rect_.y + kQLane, bottom - 56);   // 56 pixels high, ending just under the name: the value text under it stays the knob's
        return { g.labelBox.x - 4, top, std::min (g.labelBox.w + 8, rect_.right() - g.labelBox.x + 4), std::min (bottom, rect_.bottom()) - top };
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        tapCard_ = tapName_ = false;
        if (enabled_ && chipsRect().contains (x, y))
        {
            tapCard_ = true;
            return Change::none();
        }
        if (enabled_ && nameId != 0 && nameRect().contains (x, y))   // a tap is the name's; a drag that starts here is the knob's (replayed from where it began)
        {
            tapName_ = true;
            nameTap_.down (x, y, nowMs);
            nameDownMs_ = nowMs;
            return Change::none();
        }
        return Composite::touchDown (x, y, nowMs);
    }
    Change touchMove (int x, int y) override
    {
        if (tapName_)
        {
            if (! nameTap_.move (x, y))
                return Change::none();
            tapName_ = false;
            Composite::touchDown (nameTap_.startX(), nameTap_.startY(), nameDownMs_);
            return Composite::touchMove (x, y);
        }
        return tapCard_ ? Change::none() : Composite::touchMove (x, y);
    }
    Change touchUp (int x, int y) override
    {
        if (tapName_)
        {
            tapName_ = false;
            return nameRect().contains (x, y) ? Change::request (nameId) : Change::none();
        }
        if (tapCard_)
        {
            tapCard_ = false;
            return chipsRect().contains (x, y) ? Change::request (id_) : Change::none();
        }
        return Composite::touchUp (x, y);
    }

protected:
    void layout() override
    {
        // the knob lies a lane (kQLane) below the top of the card: the index chip of its Q group sits above it
        knob.place ({ rect_.x + 6, rect_.y + kQLane + 2, rect_.w - 12, std::max (80, rect_.h - kQLane - 2 - kChipsH - 12) });
    }

    void drawUnder (Pen& p) const override { p.block (rect_, false); }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (nameId != 0)   // the name is a button: a line under it says so
        {
            const Knob::Geom g = knob.geom();
            if (g.labelSize > 0.0f)
            {
                const float w = std::min (Pen::textWidth (knob.state.label, g.labelSize), static_cast<float> (g.labelBox.w));
                const float y = static_cast<float> (g.labelBox.y + g.labelBox.h) - 1.0f;
                p.line (static_cast<float> (g.labelBox.x), y, static_cast<float> (g.labelBox.x) + w, y, 1.0f, p.c (T.keylineHi, 255));
            }
        }
        const Rect cr = chipsRect();
        p.rrect (cr, T.rControl, p.c (T.keyline, 120), 1.0f);
        if (numChips == 0)
        {
            p.textCentered (cr, "NO TARGET - TAP TO ADD", T.fLabel - 2.0f, p.c (T.textFaint, 255), T.fMin);
            return;
        }
        // the targets as tags: a dot in the colour of the macros and the name (cut with an ellipsis where it does not fit); no button outline, the whole area is the one touch target
        int cx = cr.x + 12;
        const int right = cr.right() - 8 - (extraDestCount > 0 ? 48 : 0);
        for (int i = 0; i < numChips; ++i)
        {
            const float tw = sdf::textWidth (chips[i].text, T.fLabel - 3.0f);
            const int tagW = std::clamp (static_cast<int> (std::ceil (tw)) + 24, 60, 150);
            if (cx + tagW > right && i > 0)
                break;
            const int w = std::min (tagW, right - cx);
            if (w < 40)
                break;
            p.disc (static_cast<float> (cx + 4), static_cast<float> (cr.y + cr.h / 2), 3.0f, p.c (theme::sourceColour ("MAC"), 255));
            p.textLeft ({ cx + 12, cr.y, w - 12, cr.h }, chips[i].text, T.fLabel - 3.0f, p.c (T.text, 255), T.fMin);
            cx += w + 8;
        }
        if (extraDestCount > 0)
        {
            char buf[12];
            std::snprintf (buf, sizeof buf, "+%d", extraDestCount);
            p.textRight ({ cr.right() - 8 - 44, cr.y, 44, cr.h }, buf, T.fLabel - 2.0f, p.c (T.flow[0], 255), T.fMin);
        }
    }

private:
    bool tapCard_ = false, tapName_ = false;
    TapGesture nameTap_;
    double nameDownMs_ = 0.0;
    int totalDests_ = 0;
};


// ===============================================================================================================================================================
/** The page of the Macros (1280 x 628): the bar (title, BANK 1 / BANK 2), eight cards, the spectrum strip. */
class MacroView : public Composite
{
public:
    static constexpr int kBarH = 64, kNumMacros = 8, kSpectrumH = 140;

    LedList bankList;   // BANK 1: 1-4 / BANK 2: 5-8: both visible, one lit (a LED list in the bar, no boxes)

    MacroCard cards[kNumMacros];
    SpectrumStrip spectrum;
    QGroup qgroup;

    MacroView()
    {
        static const char* const kBanks[2] = { "BANK 1: 1-4", "BANK 2: 5-8" };
        bankList.setOptions (kBanks, 2);
        addKid (bankList);
        for (int i = 0; i < kNumMacros; ++i)
        {
            cards[i].setMacro (i, nullptr);
            addKid (cards[i]);
        }
        addKid (spectrum);
        setBank (0);
    }

    Size minSize() const override { return { 1280, 628 }; }

    void setBank (int b)
    {
        bank_ = (b == 1 ? 1 : 0);
        bankList.state.index = bank_;
        updateQGroup();
    }
    int bank() const noexcept { return bank_; }

    void updateQGroup()
    {
        const int offset = bank_ * 4;
        for (int k = 0; k < 4; ++k)
            qgroup.set (k, nullptr);
        for (int k = 0; k < 4; ++k)
            qgroup.set (k, &cards[offset + k].knob);
        qgroup.setActive (true);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)   // a tap on a card makes its row the Q group
            for (int i = 0; i < kNumMacros; ++i)
                if (cap_ == &cards[i])
                {
                    setBank (i / 4);
                    break;
                }
        return c;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        // one frame around the four cards of the bank (the knobs are the members; the frame never cuts through a card)
        const int o = bank_ * 4;
        Rect cards4 = cards[o].rect();
        for (int k = 1; k < 4; ++k)
            cards4 = Rect::unite (cards4, cards[o + k].rect());
        const Rect around { cards4.x - 3, cards4.y - 3, cards4.w + 6, cards4.h + 6 };
        qgroup.draw (sb, &around);
    }

    Change turnQ (int knobIndex, double delta) { return qgroup.turn (knobIndex, delta); }
    Rect cardsRect() const noexcept { return { rect_.x + 8, rect_.y + kBarH + 8, rect_.w - 16, spectrum.rect().y - 8 - (rect_.y + kBarH + 8) }; }

protected:
    void layout() override
    {
        const int m = 8;
        bankList.place ({ rect_.x + 300, rect_.y + 4, 2 * 190, 56 });

        spectrum.place ({ rect_.x + m, rect_.bottom() - m - kSpectrumH, rect_.w - 2 * m, kSpectrumH });
        // 2 rows x 4 columns
        const int gridY = rect_.y + kBarH + m, gridH = spectrum.rect().y - m - gridY, gap = 8;
        const int colW = (rect_.w - 2 * m - 3 * gap) / 4, rowH = (gridH - gap) / 2;
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 4; ++c)
                cards[r * 4 + c].place ({ rect_.x + m + c * (colW + gap), gridY + r * (rowH + gap), c == 3 ? rect_.right() - m - (rect_.x + m + 3 * (colW + gap)) : colW, rowH });
    }

    void drawUnder (Pen& p) const override
    {
        p.line (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.y + kBarH - 2), static_cast<float> (rect_.right() - 8), static_cast<float> (rect_.y + kBarH - 2), 1.5f, p.c (theme::L().keyline, 255));
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft ({ rect_.x + 14, rect_.y + 4, 260, 56 }, "MACROS", T.fTitle, p.c (T.text, 255), T.fMin);
    }

private:
    int bank_ = 0;
};

}} // namespace synthyboy_mpc::ui
