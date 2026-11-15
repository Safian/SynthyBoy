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

        // Destination target nodes across the top of the routing bar
        struct Target { const char* label; float x; };
        const float w = static_cast<float> (rect_.w);
        const float topY = static_cast<float> (rect_.y + 16);
        const Target targets[] = {
            { "FLT 1", rect_.x + w * 0.16f },
            { "FLT 2", rect_.x + w * 0.32f },
            { "MAIN FX", rect_.x + w * 0.52f },
            { "DIRECT", rect_.x + w * 0.72f },
            { "MASTER", rect_.x + w * 0.90f }
        };

        for (const Target& t : targets)
        {
            const Rect chip { static_cast<int> (t.x - 26.0f), rect_.y + 5, 52, 20 };
            p.rrect (chip, 3.0f, p.c (T.keylineHi, 180), 1.0f);
            p.textCentered (chip, t.label, T.fSmall, p.c (T.textDim, 230));
        }

        // Draw signal lines from each strip's bottom x to target top
        const float stripStep = w / static_cast<float> (kStrips);
        const float botY = static_cast<float> (rect_.bottom() - 4);

        for (int s = 0; s < kStrips; ++s)
        {
            const float sx = static_cast<float> (rect_.x) + (static_cast<float> (s) + 0.5f) * stripStep;
            const bool sel = (s == selectedStrip);
            const uint32_t col = sel ? p.c (T.flow[0], 255) : p.c (T.keyline, 130);
            const float stroke = sel ? 2.0f : 1.0f;

            float tx = targets[4].x; // default master
            if (s < 5) // Sub, A, B, C, Noise
            {
                const int d = destinations[s];
                if (d == 0) tx = targets[0].x;      // Filter 1
                else if (d == 1) tx = targets[1].x; // Filter 2
                else if (d == 2) tx = (targets[0].x + targets[1].x) * 0.5f; // Filters 1+2
                else if (d == 3) tx = targets[2].x; // Main FX
                else if (d == 4) tx = targets[3].x; // Direct
                else if (d == 5) tx = (targets[0].x + targets[1].x) * 0.5f; // Blend
                else continue; // None: no line
            }
            else if (s == 5 || s == 6) // F1, F2
            {
                const int d = destinations[s];
                if (d == 0) tx = targets[2].x;      // Main FX
                else if (d == 1) tx = targets[3].x; // Direct
                else continue;
            }
            else if (s == 7 || s == 8) // Bus 1, Bus 2
            {
                const int d = destinations[s];
                if (d == 0) tx = targets[2].x;      // Main
                else if (d == 1) tx = targets[3].x; // Direct
                else tx = (s == 7 ? sx + stripStep : sx - stripStep); // Other bus
            }
            else // Master
            {
                tx = targets[4].x;
            }

            // Draw clean connection
            const float midY = (topY + botY) * 0.5f;
            p.line (sx, botY, sx, midY, stroke, col);
            p.line (sx, midY, tx, midY, stroke, col);
            p.line (tx, midY, tx, topY + 9.0f, stroke, col);
            p.disc (sx, botY, sel ? 3.0f : 2.0f, col);
        }
    }
};

// ===============================================================================================================================================================
/** MixerFullScreen: Complete full screen Mixer page.
    Holds MixerRoutingBar at top, 10 channel strips side by side, and 10 QGroups.
    QGroup per strip: slot 0 fader (LEVEL), slot 1 pan, slot 2 send1, slot 3 send2: one frame around the column of the strip you touched.
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
