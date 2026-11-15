// SynthyBoy - MPC UI component library: the FX PAGE (FxFullScreen): the chain bar across the top (the effects in processing order, the cards drag to reorder, + ADD), under it the page of the selected effect
// (fx_slot_editor.h). Q groups: the rows of four knobs of the effect on screen.
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
    static constexpr int kMaxGroups = FxSlotEditor::kMaxRows;
    static constexpr int kBarH = 88;

    FxChainBar chainBar;
    FxSlotEditor slotEditor;

    FxFullScreen()
    {
        addKid (chainBar);
        addKid (slotEditor);
    }

    Size minSize() const override { return { 1280, 628 }; }

    int activeGroup() const noexcept { return slotEditor.activeRow(); }
    void setActiveGroup (int g) { slotEditor.setActiveRow (g); }
    int numGroups() const noexcept { return slotEditor.numRows(); }
    void showEditor (bool s) { show (slotEditor, s); }
    bool editorShown() const noexcept { return editorShown_; }
    static const char* groupName (int g) noexcept
    {
        static const char* const names[kMaxGroups] = { "PARAMETERS 1-4", "PARAMETERS 5-8", "PARAMETERS 9-12", "PARAMETERS 13-16", "PARAMETERS 17-20", "PARAMETERS 21-24" };
        return g >= 0 && g < kMaxGroups ? names[g] : "";
    }

    void assignIds (int chainBaseId, int editorBaseId)
    {
        chainBar.assignIds (chainBaseId);
        slotEditor.assignIds (editorBaseId, editorBaseId + 100, editorBaseId + 101, editorBaseId + 102, editorBaseId + 103);
    }

    Change turnQ (int knobIndex, double delta) { return slotEditor.turnQ (knobIndex, delta); }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        if (! editorShown_)
        {
            Pen p (sb, enabled_);
            Cell cell (sb, rect_);
            const theme::Look& T = theme::L();
            const Rect r { rect_.x + 8, rect_.y + kBarH + 16, rect_.w - 16, rect_.h - kBarH - 24 };
            p.block (r, false);
            p.textCentered ({ r.x, r.y + r.h / 2 - 44, r.w, 44 }, "NO EFFECT YET", T.fTitle, p.c (T.textDim, 255), T.fMin);
            p.textCentered ({ r.x, r.y + r.h / 2 + 4, r.w, 28 }, "TAP + ADD IN THE CHAIN ABOVE", T.fLabel, p.c (T.textFaint, 255), T.fMin);
        }
    }
    /** The owner says whether an effect is on the page (an empty chain has the hint instead). */
    void setEditorShown (bool s)
    {
        editorShown_ = s;
        show (slotEditor, s);
    }

protected:
    void layout() override
    {
        const int pad = 8;
        chainBar.place ({ rect_.x + pad, rect_.y + pad, rect_.w - 2 * pad, kBarH });
        const int topY = rect_.y + pad + kBarH + pad;
        slotEditor.place ({ rect_.x, topY, rect_.w, std::max (100, rect_.bottom() - topY) });
    }

private:
    bool editorShown_ = false;
};

}} // namespace synthyboy_mpc::ui
