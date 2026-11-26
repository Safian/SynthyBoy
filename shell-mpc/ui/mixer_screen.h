// SynthyBoy - MPC UI component library: the MIXER FULL SCREEN view (MixerFullScreen).
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
/** MixerFullScreen: Complete full screen Mixer page.
    Holds the 10 channel strips side by side over the whole height of the tab (one screen: no peek tab bar), and 3 QGroups: the LEVELS layout (the Q-Links are the level faders only; PAN, the bus sends and the destination stay on the touch screen):
    [SUB, A, B, C], [NOISE, F1, F2, MAIN] and [BUS 1, BUS 2] (the XL: Q 1 .. 10; the Key 61: the banks 1 .. 3). A touch on a strip selects it and makes the group of its fader the active one.
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

    MixerStrip strips[kStrips];
    QGroup groups[kMaxGroups];

    MixerFullScreen()
    {
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
    /** The selected strip (the one last touched, or the first of the group a Q bank was made active). */
    int selectedStrip() const noexcept { return selectedStrip_; }
    void setActiveGroup (int g)
    {
        if (g < 0 || g >= kMaxGroups)
            return;
        selectStrip (groupOfStrip (selectedStrip_) == g ? selectedStrip_ : stripOfSlot (g, 0));
    }
    /** A strip is selected: the group of its fader is the active one. */
    void selectStrip (int strip)
    {
        if (strip < 0 || strip >= kStrips)
            return;
        selectedStrip_ = strip;
        activeGroup_ = groupOfStrip (strip);
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setActive (i == activeGroup_);
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

protected:
    void layout() override
    {
        const int pad = 6;
        const int topY = rect_.y + 10;
        const int stripH = std::max (100, rect_.h - (topY - rect_.y) - 12);
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
