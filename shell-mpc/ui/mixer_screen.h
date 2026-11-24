// SynthyBoy - MPC UI component library: the MIXER FULL SCREEN view (MixerRoutingBar, MixerFullScreen).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"
#include "composite.h"
#include "core.h"
#include "mixer_strip.h"
#include "qgroup.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** MixerRoutingBar: Displays thin signal flow lines above the channel strips. The filter and Main FX destinations open a module panel.
    Shows paths from sources (Sub, A, B, C, Noise, Filters, Buses) to destination nodes (F1, F2, Main, Direct, Master).
    The currently selected strip's path is highlighted with bright accent ink. */
class MixerRoutingBar : public Component
{
public:
    bool nativeGround = false;
    bool nativePeek = false;

    static constexpr int kStrips = 10;
    int destinations[kStrips] = {};
    int selectedStrip = 0;

    static constexpr int kPeekTargets = 5; // F1, F2, MAIN FX, BUS 1, BUS 2
    static constexpr float kTargetX[] = { 0.08f, 0.22f, 0.36f, 0.50f, 0.64f, 0.78f, 0.92f };
    Rect peekRect (int target) const noexcept
    {
        if (target < 0 || target >= kPeekTargets)
            return {};
        const int hitW = std::min (96, static_cast<int> (rect_.w * (kTargetX[1] - kTargetX[0])));
        return { rect_.x + static_cast<int> (rect_.w * kTargetX[target]) - hitW / 2, rect_.y, hitW, rect_.h };
    }
    int touchTargets (Rect* out, int max) const override
    {
        const int n = std::min (max, kPeekTargets);
        for (int i = 0; i < n; ++i)
            out[i] = peekRect (i);
        return n;
    }

    Size minSize() const override { return { 400, 56 }; }

    void setDestination (int strip, int dest) noexcept
    {
        if (strip >= 0 && strip < kStrips)
            destinations[strip] = dest;
    }

    void setSelectedStrip (int s) noexcept
    {
        if (s >= 0 && s < kStrips)
            selectedStrip = s;
    }

    void drawFrame (Pen& p) const
    {
        p.block (rect_, false);
        p.rrect (rect_, 4.0f, p.c (theme::L().keyline, 140), 1.0f);
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (! nativeGround)
            drawFrame (p);

        const char* labels[] = { "FLT 1", "FLT 2", "MAIN FX", "BUS 1", "BUS 2", "DIRECT", "MASTER" };
        const float w = static_cast<float> (rect_.w);
        const float chipB = static_cast<float> (rect_.y + 30);
        const float totalW = w;
        const float gap = 4.0f;
        const float stripW = (totalW - (static_cast<float> (kStrips) - 1.0f) * gap) / static_cast<float> (kStrips);
        const int s = selectedStrip;
        const float sx = static_cast<float> (rect_.x) + static_cast<float> (s) * (stripW + gap) + stripW * 0.5f;
        // where the selected strip goes: up to two of the targets (the blend of the two filters feeds both), or a neighbouring bus, or nothing
        int to[2] = { -1, -1 };
        float busX = -1.0f;
        const int d = destinations[s];
        if (s < 5)   // Sub, A, B, C, Noise
        {
            if (d == 0) to[0] = 0;
            else if (d == 1) to[0] = 1;
            else if (d == 2 || d == 5) { to[0] = 0; to[1] = 1; }
            else if (d == 3) to[0] = 2;
            else if (d == 4) to[0] = 5;
        }
        else if (s == 5 || s == 6)   // F1, F2
        {
            if (d == 0) to[0] = 2;
            else if (d == 1) to[0] = 5;
        }
        else if (s == 7 || s == 8)   // Bus 1, Bus 2
        {
            if (d == 0) to[0] = 2;
            else if (d == 1) to[0] = 5;
            else busX = sx + (s == 7 ? (stripW + gap) : -(stripW + gap));
        }
        else
            to[0] = 6;   // Master
        for (int i = 0; i < 7; ++i)
        {
            const float tx = rect_.x + w * kTargetX[i];
            const bool lit = i == to[0] || i == to[1];
            const int chipW = i < kPeekTargets ? std::min (68, peekRect (i).w - 4) : 68;
            const Rect chip { static_cast<int> (tx) - chipW / 2, rect_.y + 5, chipW, 24 };
            if (i >= kPeekTargets || ! nativePeek)
            {
                if (i < kPeekTargets)
                    p.rrect (chip, T.rChip, p.c (i == pressed_ ? T.flow[0] : T.keyline, 255), T.frame);
                p.textCentered (chip, labels[i], T.fLabel - 1.0f, p.c (lit ? T.flow[0] : T.textFaint, lit ? 255 : 150));
            }
        }

        // ONE line: from the selected strip up to its destination
        const float botY = static_cast<float> (rect_.bottom() - 4), midY = (chipB + botY) * 0.5f + 3.0f;
        const uint32_t col = p.c (T.flow[0], 255);
        p.disc (sx, botY, 3.0f, col);
        p.line (sx, botY, sx, midY, 2.0f, col);
        for (int k = 0; k < 2; ++k)
            if (to[k] >= 0)
            {
                const float tx = rect_.x + w * kTargetX[to[k]];
                p.line (std::min (sx, tx), midY, std::max (sx, tx), midY, 2.0f, col);
                p.line (tx, midY, tx, chipB, 2.0f, col);
            }
        if (busX >= 0.0f)   // into the other bus: along the bar to its strip
        {
            p.line (std::min (sx, busX), midY, std::max (sx, busX), midY, 2.0f, col);
            p.line (busX, midY, busX, botY, 2.0f, col);
            p.disc (busX, botY, 3.0f, col);
        }
        if (to[0] < 0 && busX < 0.0f)   // no destination: the line ends at its strip's mid line
            p.line (sx - 8.0f, midY, sx + 8.0f, midY, 2.0f, col);
    }
    Change onDown (int x, int y, double nowMs) override
    {
        tap_.down (x, y, nowMs);
        pressed_ = -1;
        for (int i = 0; i < kPeekTargets; ++i)
            if (peekRect (i).contains (x, y))
                pressed_ = i;
        return {};
    }
    Change onMove (int x, int y) override
    {
        if (tap_.move (x, y))
            pressed_ = -1;
        return {};
    }
    Change onUp (int x, int y) override
    {
        const int target = pressed_;
        pressed_ = -1;
        tap_.move (x, y);
        return tap_.up() && peekRect (target).contains (x, y) ? Change::indexed (id_, target, target) : Change{};
    }
private:
    TapGesture tap_;
    int pressed_ = -1;

};

// ===============================================================================================================================================================
/** MixerFullScreen: Complete full screen Mixer page.
    Holds MixerRoutingBar at top, 10 channel strips side by side, and 3 QGroups: the LEVELS layout (the Q-Links are the level faders only; PAN, the bus sends and the destination stay on the touch screen):
    [SUB, A, B, C], [NOISE, F1, F2, MAIN] and [BUS 1, BUS 2] (the XL: Q 1 .. 10; the Key 61: the banks 1 .. 3). A touch on a strip selects it (the routing bar shows it) and makes the group of its fader the active one.
    A strip a group has no fader for (setStripMembers) is no slot of its group: no chip. The second group is frameless (its faders lie apart: the buses are between them). Adjacency check passes with 0 offenders. */
class MixerFullScreen : public Composite
{
public:
    static constexpr int kStrips = 10;
    static constexpr int kMaxGroups = 3;
    /** The strip of every slot of every group (-1: none): the levels of SUB, A, B, C / NOISE, F1, F2, MAIN / BUS 1, BUS 2. */
    static constexpr int kStripOfSlot[kMaxGroups][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 9 }, { 7, 8, -1, -1 } };
    static int stripOfSlot (int group, int slot) noexcept
    {
        if (! QGroup::wideDevice())
            return kStripOfSlot[group][slot];
        const int strip = 4 * group + slot;
        return strip < kStrips ? strip : -1;
    }
    static int groupOfStrip (int strip) noexcept
    {
        if (QGroup::wideDevice())
            return strip / 4;
        return strip >= 7 && strip <= 8 ? 2 : (strip >= 4 ? 1 : 0);
    }

    static int slotOfStrip (int strip) noexcept
    {
        if (QGroup::wideDevice())
            return strip % 4;
        return strip == 9 ? 3 : (strip >= 7 ? strip - 7 : strip % 4);
    }

    MixerRoutingBar routingBar;
    MixerStrip strips[kStrips];
    QGroup groups[kMaxGroups];

    MixerFullScreen()
    {
        addKid (routingBar);
        const char* const titles[kStrips] = {
            "SUB", "A", "B", "C", "NOISE", "F1", "F2", "BUS1", "BUS2", "MASTER"
        };
        for (int i = 0; i < kStrips; ++i)
        {
            strips[i].setTitle (titles[i]);
            addKid (strips[i]);
        }
        setupGroups();
        setActiveGroup (0);
    }

    Size minSize() const override { return { 1280, 628 }; }

    int activeGroup() const noexcept { return activeGroup_; }
    /** The strip the routing bar shows (the one last touched, or the first of the group a Q bank was made active). */
    int selectedStrip() const noexcept { return selectedStrip_; }
    void setActiveGroup (int g)
    {
        if (g < 0 || g >= kMaxGroups)
            return;
        selectStrip (groupOfStrip (selectedStrip_) == g ? selectedStrip_ : stripOfSlot (g, 0));
    }
    /** A strip is selected: the routing bar shows it and the group of its fader is the active one. */
    void selectStrip (int strip)
    {
        if (strip < 0 || strip >= kStrips)
            return;
        selectedStrip_ = strip;
        activeGroup_ = groupOfStrip (strip);
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setActive (i == activeGroup_);
        routingBar.setSelectedStrip (strip);
    }

    void syncDeviceLayout()
    {
        const bool wide = QGroup::wideDevice();
        if (wide != wideLayout_)
        {
            wideLayout_ = wide;
            groups[1].setFrameless (! wide);
            selectStrip (selectedStrip_);
        }
    }

    int numGroups() const noexcept { return kMaxGroups; }
    const char* groupName (int g) const noexcept
    {
        static const char* const kNames[kMaxGroups] = { "LEVELS SUB A B C", "LEVELS NOISE F1 F2 MAIN", "LEVELS BUS 1 2" };
        static const char* const wideNames[kMaxGroups] = { "LEVELS SUB A B C", "LEVELS NOISE F1 F2 BUS1", "LEVELS BUS2 MAIN" };
        return g >= 0 && g < kMaxGroups ? (QGroup::wideDevice() ? wideNames[g] : kNames[g]) : "";
    }

    void assignIds (int baseId)
    {
        for (int i = 0; i < kStrips; ++i)
            strips[i].assignIds (baseId + i * 10);
    }

    /** Which of LEVEL, PAN, B1, B2 the strip has an engine parameter for. A member it has not is left out of the strip (not drawn, no touch); the strip keeps its height and the other members their places. Only the level is a Q member
        (the strip's slot in its group); a strip without a level leaves the slot empty. */
    void setStripMembers (int strip, bool level, bool pan, bool send1, bool send2)
    {
        if (strip < 0 || strip >= kStrips)
            return;
        strips[strip].setPresent (level, pan, send1, send2);
        groups[groupOfStrip (strip)].set (slotOfStrip (strip), level ? &strips[strip].fader : nullptr);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        // Touch on any strip activates its Q-Group
        for (int i = 0; i < kStrips; ++i)
        {
            if (strips[i].rect().contains (x, y))
            {
                if (selectedStrip_ != i)
                    selectStrip (i);
                break;
            }
        }
        return Composite::touchDown (x, y, nowMs);
    }

    Change turnQ (int knobIndex, double delta)
    {
        return groups[activeGroup_].turn (knobIndex, delta);
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        groups[activeGroup_].draw (sb);
    }

    void drawStaticFurniture (overlay::gpu::SceneBuilder& sb) const
    {
        Pen pen (sb, true);
        routingBar.drawFrame (pen);
        drawExtra (pen);
    }

protected:
    void layout() override
    {
        const int pad = 6;
        const int barH = 56;
        const int topY = rect_.y + 10;
        const int stripH = std::max (100, rect_.h - (topY - rect_.y) - barH - 12);
        const int gap = 4;
        const int totalW = rect_.w - 2 * pad;
        const int stripW = (totalW - (kStrips - 1) * gap) / kStrips;

        for (int i = 0; i < kStrips; ++i)
        {
            const int sx = rect_.x + pad + i * (stripW + gap);
            strips[i].place ({ sx, topY, stripW, stripH });
        }

        routingBar.place ({ rect_.x + pad, topY + stripH + 6, rect_.w - 2 * pad, barH });
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const int dividers[] = { 4, 6, 8 };
        const float divTop = static_cast<float> (strips[0].rect().y);
        const float divBot = static_cast<float> (strips[0].rect().bottom());

        for (int d : dividers)
        {
            const float dx = static_cast<float> (strips[d].rect().right() + 2);
            p.line (dx, divTop, dx, divBot, 1.5f, p.c (T.keyline, 160));
        }
    }

private:
    int activeGroup_ = 0, selectedStrip_ = 0;
    bool wideLayout_ = QGroup::wideDevice();

    void setupGroups()
    {
        for (int g = 0; g < kMaxGroups; ++g)
            for (int k = 0; k < 4; ++k)
                groups[g].set (k, stripOfSlot (g, k) >= 0 ? &strips[stripOfSlot (g, k)].fader : nullptr);
        groups[1].setFrameless (! QGroup::wideDevice());   // NOISE, F1, F2 and MAIN: the two buses lie between the faders, no frame around everything in between
    }
};

}} // namespace synthyboy_mpc::ui
