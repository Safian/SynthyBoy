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
/** MixerRoutingBar: Displays thin signal flow lines above the channel strips.
    Shows paths from sources (Sub, A, B, C, Noise, Filters, Buses) to destination nodes (F1, F2, Main, Direct, Master).
    The currently selected strip's path is highlighted with bright accent ink. */
class MixerRoutingBar : public Component
{
public:
    static constexpr int kStrips = 10;
    int destinations[kStrips] = {};
    int selectedStrip = 0;

    MixerRoutingBar() = default;

    Size minSize() const override { return { 400, 48 }; }

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

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rect_, false);
        p.rrect (rect_, 4.0f, p.c (T.keyline, 140), 1.0f);

        // the five destinations along the top: the one the selected strip feeds is lit, the others are faint
        struct Target { const char* label; float x; };
        const float w = static_cast<float> (rect_.w);
        const float chipB = static_cast<float> (rect_.y + 30);
        const Target targets[] = {
            { "FLT 1", rect_.x + w * 0.16f },
            { "FLT 2", rect_.x + w * 0.32f },
            { "MAIN FX", rect_.x + w * 0.52f },
            { "DIRECT", rect_.x + w * 0.72f },
            { "MASTER", rect_.x + w * 0.90f }
        };
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
            else if (d == 4) to[0] = 3;
        }
        else if (s == 5 || s == 6)   // F1, F2
        {
            if (d == 0) to[0] = 2;
            else if (d == 1) to[0] = 3;
        }
        else if (s == 7 || s == 8)   // Bus 1, Bus 2
        {
            if (d == 0) to[0] = 2;
            else if (d == 1) to[0] = 3;
            else busX = sx + (s == 7 ? (stripW + gap) : -(stripW + gap));
        }
        else
            to[0] = 4;   // Master
        for (int i = 0; i < 5; ++i)
        {
            const Target& t = targets[i];
            const bool lit = i == to[0] || i == to[1];
            const Rect chip { static_cast<int> (t.x - 34.0f), rect_.y + 5, 68, 24 };
            p.rrect (chip, 4.0f, p.c (lit ? T.flow[0] : T.keylineHi, lit ? 255 : 90), lit ? 1.6f : 1.0f);
            p.textCentered (chip, t.label, T.fLabel - 1.0f, p.c (lit ? T.text : T.textFaint, lit ? 255 : 150));
        }

        // ONE line: from the selected strip up to its destination
        const float botY = static_cast<float> (rect_.bottom() - 4), midY = (chipB + botY) * 0.5f + 3.0f;
        const uint32_t col = p.c (T.flow[0], 255);
        p.disc (sx, botY, 3.0f, col);
        p.line (sx, botY, sx, midY, 2.0f, col);
        for (int k = 0; k < 2; ++k)
            if (to[k] >= 0)
            {
                const float tx = targets[to[k]].x;
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
};

// ===============================================================================================================================================================
/** MixerFullScreen: Complete full screen Mixer page.
    Holds MixerRoutingBar at top, 10 channel strips side by side, and 10 QGroups.
    QGroup per strip: slot 0 fader (LEVEL), slot 1 pan, slot 2 send1, slot 3 send2: one frame around the column of the strip you touched.
    A member the strip does not have (setStripMembers: BUS 1, BUS 2 and MASTER have a level only) is no slot of its group: no chip, not in the frame.
    Adjacency check passes with 0 offenders. */
class MixerFullScreen : public Composite
{
public:
    static constexpr int kStrips = 10;
    static constexpr int kMaxGroups = kStrips;

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
    void setActiveGroup (int g)
    {
        if (g < 0 || g >= kMaxGroups)
            return;
        activeGroup_ = g;
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setActive (i == activeGroup_);
        routingBar.setSelectedStrip (activeGroup_);
    }

    int numGroups() const noexcept { return kMaxGroups; }
    const char* groupName (int g) const noexcept
    {
        if (g >= 0 && g < kStrips)
            return strips[g].title;
        return "";
    }

    void assignIds (int baseId)
    {
        for (int i = 0; i < kStrips; ++i)
            strips[i].assignIds (baseId + i * 10);
    }

    /** Which of LEVEL (Q1), PAN (Q2), B1 (Q3), B2 (Q4) the strip has an engine parameter for. A member it has not is left out of the strip (not drawn, no touch) and of the strip's Q group (no chip, not in
        the frame); the strip keeps its height and the other members their places. */
    void setStripMembers (int strip, bool level, bool pan, bool send1, bool send2)
    {
        if (strip < 0 || strip >= kStrips)
            return;
        strips[strip].setPresent (level, pan, send1, send2);
        groups[strip].set (0, level ? &strips[strip].fader : nullptr);
        groups[strip].set (1, pan ? &strips[strip].pan : nullptr);
        groups[strip].set (2, send1 ? &strips[strip].send1 : nullptr);
        groups[strip].set (3, send2 ? &strips[strip].send2 : nullptr);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        // Touch on any strip activates its Q-Group
        for (int i = 0; i < kStrips; ++i)
        {
            if (strips[i].rect().contains (x, y))
            {
                if (activeGroup_ != i)
                    setActiveGroup (i);
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
        for (int i = 0; i < kMaxGroups; ++i)
            if (i != activeGroup_)
                drawStripMapped (sb, i);
    }

    void drawStripMapped (gpu::SceneBuilder& sb, int g) const
    {
        if (g < 0 || g >= kStrips)
            return;
        const int block = groups[g].block();
        if (block < 1)
            return;
        const bool wide = QGroup::wideDevice();
        Pen pen (sb, true);
        for (int slot = 0; slot < QGroup::kSlots; ++slot)
        {
            const Component* m = groups[g].member (slot);
            if (m && ! m->rect().empty())
            {
                const Rect mr = m->rect();
                Cell cell (sb, { mr.x, mr.y - 17, mr.w, 19 });
                pen.qIndex (mr, wide ? block * QGroup::kSlots + slot + 1 : slot + 1, true);
            }
        }
    }

protected:
    void layout() override
    {
        const int pad = 6;
        const int barH = 56;
        routingBar.place ({ rect_.x + pad, rect_.y + 4, rect_.w - 2 * pad, barH });

        const int topY = rect_.y + barH + 10;
        const int stripH = std::max (100, rect_.bottom() - topY - 4);
        const int gap = 4;
        const int totalW = rect_.w - 2 * pad;
        const int stripW = (totalW - (kStrips - 1) * gap) / kStrips;

        for (int i = 0; i < kStrips; ++i)
        {
            const int sx = rect_.x + pad + i * (stripW + gap);
            strips[i].place ({ sx, topY, stripW, stripH });
        }
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
    int activeGroup_ = 0;

    void setupGroups()
    {
        for (int i = 0; i < kStrips; ++i)
        {
            // The 4 Q-Link slots: LEVEL, PAN, BUS 1, BUS 2
            groups[i].set (0, &strips[i].fader);
            groups[i].set (1, &strips[i].pan);
            groups[i].set (2, &strips[i].send1);
            groups[i].set (3, &strips[i].send2);
        }
    }
};

}} // namespace synthyboy_mpc::ui
