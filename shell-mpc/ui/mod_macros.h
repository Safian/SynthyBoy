// SynthyBoy - MPC UI: the page of the Macros (1280 x 628): the studio spectrum across the upper area and all eight macros as compact cards in one bottom row, each with its 90 px blue dial, value,
// renameable name and native target action. Logical BANK 1 (macros 1 - 4) and BANK 2 (5 - 8) map to fixed hardware Q-Link banks 1 and 2 (Gen 1 frames the focused bank with an unnumbered Q; Gen 2 has no frame).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "macro_card_contract.h"
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

    void drawStatic (gpu::SceneBuilder& sb) const
    {
        Pen p (sb, enabled_);
        Cell cell (sb, rect_);
        const theme::Look& T = theme::L();
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
    }

    void drawLive (gpu::SceneBuilder& sb) const
    {
        if (levels == nullptr)
            return;
        const theme::Look& T = theme::L();
        const Rect in { rect_.x + 10, rect_.y + 22, std::max (0, rect_.w - 20), std::max (0, rect_.h - 22 - 20) };
        if (in.w < 20 || in.h < 20)
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

    void draw (gpu::SceneBuilder& sb) const override
    {
        drawStatic (sb);
        drawLive (sb);
    }

protected:
    void drawBody (Pen&) const override {}
};

// ===============================================================================================================================================================
/** Native Macro Card: the knob is a state/Q adapter only. The overlay owns the renameable name. */
class MacroCard : public Composite
{
public:
    int macroIndex = 0; // 0 .. 7
    Knob knob;

    /** Bounding box of the dial and value control in this card (from shared MacroCard contract). */
    Rect dialArea() const noexcept
    {
        return { rect_.x + macrocard::kDialRect.x, rect_.y + macrocard::kDialRect.y, macrocard::kDialRect.w, macrocard::kDialRect.h };
    }

    Rect nativeKnobArea() const noexcept
    {
        return { rect_.x + macrocard::kNativeKnobRect.x, rect_.y + macrocard::kNativeKnobRect.y, macrocard::kNativeKnobRect.w, macrocard::kNativeKnobRect.h };
    }

    Rect nativeTargetArea() const noexcept
    {
        return { rect_.x + macrocard::kTargetRect.x, rect_.y + macrocard::kTargetRect.y, macrocard::kTargetRect.w, macrocard::kTargetRect.h };
    }

    bool isNativeDial (int x, int y) const noexcept
    {
        if (! enabled_)
            return false;
        return nativeKnobArea().contains (x, y);
    }

    MacroCard()
    {
        knob.state.side = false;
        // Native dial: no overlay child rendering or pointer dispatch.
    }

    Size minSize() const override { return { macrocard::kCardW, macrocard::kCardH }; }
    void setEnabled (bool e) override { Composite::setEnabled (e); knob.setEnabled (e); if (! e) tapName_ = false; }
    void setInactive (bool i) override { Composite::setInactive (i); knob.setInactive (i); }

    void setMacro (int idx, const char* name)
    {
        macroIndex = idx;
        ui::setText (macroName_, sizeof macroName_, name && name[0] ? name : "MACRO");
    }
    const char* macroName() const noexcept { return macroName_; }
    void setValue (double val, const char* text)
    {
        knob.state.value = knob.state.modValue = std::clamp (val, 0.0, 1.0);
        ui::setText (knob.state.valueText, sizeof knob.state.valueText, text);
    }
    Rect nameLabelRect() const noexcept
    {
        return { rect_.x + macrocard::kNameBoxRect.x, rect_.y + macrocard::kNameBoxRect.y, macrocard::kNameBoxRect.w, macrocard::kNameBoxRect.h };
    }
    /** The name below the value is a button when `nameId` is set: a tap on it asks for the keyboard page that renames the macro (Request nameId). The touch target is 56 pixels high and non-overlapping. */
    int nameId = 0;
    Rect nameRect() const noexcept
    {
        return { rect_.x + macrocard::kNameHitRect.x, rect_.y + macrocard::kNameHitRect.y, macrocard::kNameHitRect.w, macrocard::kNameHitRect.h };
    }

    int touchTargets (Rect* out, int max) const override
    {
        if (nameId == 0 || max <= 0) return 0;
        out[0] = nameRect();
        return 1;
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        tapName_ = false;
        if (enabled_ && nameId != 0 && nameRect().contains (x, y))
        {
            tapName_ = true;
            return Change::none();
        }
        if (isNativeDial (x, y))
            return Change::none();
        return Composite::touchDown (x, y, nowMs);
    }
    Change touchMove (int x, int y) override
    {
        return tapName_ ? Change::none() : Composite::touchMove (x, y);
    }
    Change touchUp (int x, int y) override
    {
        if (tapName_)
        {
            tapName_ = false;
            return nameRect().contains (x, y) ? Change::request (nameId) : Change::none();
        }
        return Composite::touchUp (x, y);
    }

protected:
    void layout() override
    {
        knob.place (dialArea());
    }

    void drawUnder (Pen& p) const override { p.block (rect_, false); }

    void drawMacroName (Pen& p) const
    {
        const theme::Look& T = theme::L();
        const Rect nb = nameLabelRect();
        if (macroName_[0] == '\0' || nb.empty())
            return;
        p.textCentered (nb, macroName_, T.fLabel, p.c (knob.qFrame() ? T.q : T.textDim, 255), T.fMin);
    }

    /** What belongs to the INSTANCE (the name of the macro and the line under it, as wide as the name) is drawn by the live pass only: the static pass (the host-skin background) holds nothing the instance can change. */
    void drawNameLive (Pen& p) const
    {
        const theme::Look& T = theme::L();
        if (nameId != 0)   // the name is a button: a line under it says so
        {
            const Rect nb = nameLabelRect();
            const float size = Pen::fitSize (macroName_, static_cast<float> (nb.w), static_cast<float> (nb.h), T.fLabel, T.fMin);
            if (size > 0.0f)
            {
                char buf[96];
                const float w = std::min (sdf::fitText (macroName_, size, static_cast<float> (nb.w), buf, static_cast<int> (sizeof buf)), static_cast<float> (nb.w));
                const float x0 = static_cast<float> (nb.x) + (static_cast<float> (nb.w) - w) * 0.5f;
                const float y = static_cast<float> (nb.bottom()) - 1.0f;
                p.line (x0, y, x0 + w, y, 1.0f, p.c (T.keylineHi, 255));
            }
        }
    }

public:
    void drawStatic (gpu::SceneBuilder& sb, bool leaveNativeDialClear = false) const
    {
        Pen p (sb, enabled_, inactive_);
        {
            Cell cell (sb, rect_);
            if (leaveNativeDialClear)
                p.rrect (rect_, theme::L().rBlock, p.c (theme::L().keyline, 255), theme::L().frame);
            else
                drawUnder (p);
        }

    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        drawStatic (sb, false);
        drawLive (sb);
    }

    void drawLive (gpu::SceneBuilder& sb) const
    {
        Pen p (sb, enabled_, inactive_);
        Cell cell (sb, rect_);
        {
            Pen np (sb, enabled_ && ! (knob.state.led && ! knob.state.ledOn), inactive_);
            drawMacroName (np);
        }
        drawNameLive (p);
    }

private:
    char macroName_[24] = {};
    bool tapName_ = false;
};


// ===============================================================================================================================================================
/** The page of the Macros (1280 x 628): the clean header bar (title), the large upper spectrum strip, and eight compact cards in one bottom row. */
class MacroView : public Composite
{
public:
    static constexpr int kBarH = macrocard::kBarH, kNumMacros = 8, kSpectrumH = macrocard::kSpectrumRect.h;

    MacroCard cards[kNumMacros];
    SpectrumStrip spectrum;
    QGroup qgroup;

    MacroView()
    {
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
        updateQGroup();
    }
    int bank() const noexcept { return bank_; }

    Rect dialArea (int m) const noexcept
    {
        return (m >= 0 && m < kNumMacros) ? cards[m].dialArea() : Rect {};
    }

    bool isNativeDial (int x, int y, int* macroOut = nullptr) const noexcept
    {
        if (! enabled_)
            return false;
        for (int i = 0; i < kNumMacros; ++i)
        {
            if (cards[i].isNativeDial (x, y))
            {
                if (macroOut != nullptr)
                    *macroOut = i;
                return true;
            }
        }
        return false;
    }

    bool isNativeControl (int x, int y) const noexcept
    {
        if (! enabled_)
            return false;
        for (const auto& card : cards)
            if (card.nativeTargetArea().contains (x, y))
                return true;
        return isNativeDial (x, y);
    }

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
        if (isNativeControl (x, y))
            return Change::none();
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)   // a tap on a card focuses its logical 4-macro bank (Gen 1 frame focus only)
            for (int i = 0; i < kNumMacros; ++i)
                if (cap_ == &cards[i])
                {
                    setBank (i / 4);
                    break;
                }
        return c;
    }

    void drawStatic (gpu::SceneBuilder& sb, bool leaveNativeDialsClear = false) const
    {
        Pen p (sb, enabled_, inactive_);
        {
            Cell cell (sb, rect_);
            drawUnder (p);
        }
        for (int i = 0; i < kNumMacros; ++i)
            cards[i].drawStatic (sb, leaveNativeDialsClear);
        spectrum.drawStatic (sb);
    }

    void drawLive (gpu::SceneBuilder& sb) const
    {
        {
            // the title lies over the skin's marker squares (the live pass covers them): it is drawn here, never baked
            Pen p (sb, enabled_, inactive_);
            Cell cell (sb, { rect_.x + macrocard::kTitleRect.x, rect_.y + macrocard::kTitleRect.y, macrocard::kTitleRect.w, macrocard::kTitleRect.h });
            drawExtra (p);
        }
        for (int i = 0; i < kNumMacros; ++i)
            cards[i].drawLive (sb);

        #if SYNTHYBOY_MPC_QFRAME_ENABLED
        // One Gen 1 frame around the four cards of the focused bank (suppressed on XL wideDevice and compiled out on Gen 2); drawn before spectrum plain ops so cells stay in one layer.
        const int o = bank_ * 4;
        Rect cards4 = cards[o].rect();
        for (int k = 1; k < 4; ++k)
            cards4 = Rect::unite (cards4, cards[o + k].rect());
        const Rect around { cards4.x - 3, cards4.y - 3, cards4.w + 6, cards4.h + 6 };
        qgroup.draw (sb, &around);
        #endif

        spectrum.drawLive (sb);
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        drawStatic (sb);
        drawLive (sb);
    }

    Change turnQ (int knobIndex, double delta) { return qgroup.turn (knobIndex, delta); }
    Rect cardsRect() const noexcept
    {
        Rect r = cards[0].rect();
        for (int i = 1; i < kNumMacros; ++i)
            r = Rect::unite (r, cards[i].rect());
        return r;
    }

protected:
    void layout() override
    {
        // All 8 compact macro cards in one bottom row, with continuous upper SpectrumStrip from shared contract
        for (int i = 0; i < kNumMacros; ++i)
            cards[i].place ({ rect_.x + macrocard::kCardPositions[i].x, rect_.y + macrocard::kCardPositions[i].y, macrocard::kCardW, macrocard::kCardH });

        spectrum.place ({ rect_.x + macrocard::kSpectrumRect.x, rect_.y + macrocard::kSpectrumRect.y, macrocard::kSpectrumRect.w, macrocard::kSpectrumRect.h });
    }

    void drawUnder (Pen& p) const override
    {
        p.line (static_cast<float> (rect_.x + macrocard::kMargin), static_cast<float> (rect_.y + kBarH - 2), static_cast<float> (rect_.right() - macrocard::kMargin), static_cast<float> (rect_.y + kBarH - 2), 1.5f, p.c (theme::L().keyline, 255));
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft ({ rect_.x + macrocard::kTitleRect.x, rect_.y + macrocard::kTitleRect.y, macrocard::kTitleRect.w, macrocard::kTitleRect.h }, "MACROS", T.fTitle, p.c (T.text, 255), T.fMin);
    }

private:
    int bank_ = 0;
};

}} // namespace synthyboy_mpc::ui
