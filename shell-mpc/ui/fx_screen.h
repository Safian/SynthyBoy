// SynthyBoy - MPC UI component library: the FX FULL SCREEN view (FxFullScreen).
// Combines the FxChainBar across the top and FxSlotEditor below.
// Q-Groups: Group 0 = chain bar (Q1 slot select, Q2 on/off, Q3 mix, Q4 type),
// Groups 1..K = slot editor parameter rows.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"
#include "composite.h"
#include "core.h"
#include "fx_chain_bar.h"
#include "fx_slot_editor.h"
#include "qgroup.h"

namespace synthyboy_mpc { namespace ui {

class FxFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 1 + FxSlotEditor::kMaxRows; // 1 chain + 6 editor = 7 groups

    FxChainBar chainBar;
    FxSlotEditor slotEditor;
    QGroup groups[kMaxGroups];

    FxFullScreen()
    {
        addKid (chainBar);
        addKid (slotEditor);
        setupGroups();
        setActiveGroup (0);
    }

    Size minSize() const override { return { 600, 400 }; }

    int activeGroup() const noexcept { return activeGroup_; }
    void setActiveGroup (int g)
    {
        if (g < 0 || g >= kMaxGroups)
            return;
        activeGroup_ = g;
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setActive (i == activeGroup_);
        if (activeGroup_ > 0)
            slotEditor.setActiveRow (activeGroup_ - 1);
    }

    int numGroups() const noexcept { return 1 + slotEditor.numActiveRows(); }
    void showEditor (bool s) { show (slotEditor, s); }
    const char* groupName (int g) const noexcept
    {
        if (g == 0) return "Chain";
        const char* const rowNames[] = { "Params 1-4", "Params 5-8", "Params 9-12", "Params 13-16", "Params 17-20", "Params 21-24" };
        const int r = g - 1;
        return (r >= 0 && r < 6) ? rowNames[r] : "";
    }

    void assignIds (int chainBaseId, int editorBaseId)
    {
        chainBar.assignIds (chainBaseId);
        slotEditor.assignIds (editorBaseId);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (chainBar.rect().contains (x, y))
        {
            if (activeGroup_ != 0)
                setActiveGroup (0);
        }
        else if (slotEditor.rect().contains (x, y))
        {
            // Activate row based on which row was touched
            const int r = slotEditor.activeRow();
            if (activeGroup_ != r + 1)
                setActiveGroup (r + 1);
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
        if (activeGroup_ == 0)
            groups[0].draw (sb);
        // Note: when activeGroup_ > 0, slotEditor draws its active row Q-Group
    }

protected:
    void layout() override
    {
        const int pad = 6;
        const int barH = 72;
        chainBar.place ({ rect_.x + pad, rect_.y + 4, rect_.w - 2 * pad, barH });

        const int topY = rect_.y + barH + 10;
        const int editorH = std::max (100, rect_.bottom() - topY - 4);
        slotEditor.place ({ rect_.x + pad, topY, rect_.w - 2 * pad, editorH });
    }

    void drawUnder (Pen& p) const override
    {
        p.block (rect_, true);
    }

private:
    int activeGroup_ = 0;

    void setupGroups()
    {
        // Group 0: Chain bar (Q1 slot 0, Q2 slot 1, Q3 slot 2, Q4 slot 3)
        for (int i = 0; i < 4; ++i)
            groups[0].set (i, &chainBar.cards[i]);

        // Groups 1..6: delegated to slotEditor rows
        for (int r = 0; r < FxSlotEditor::kMaxRows; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                const int idx = r * 4 + c;
                groups[r + 1].set (c, &slotEditor.knobs[idx]);
            }
        }
    }
};

}} // namespace synthyboy_mpc::ui
