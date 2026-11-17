// SynthyBoy - MPC UI component library: the FX PAGE (FxFullScreen): the chain bar across the top (the effects in processing order, the cards drag to reorder, + ADD), under it the page of the selected effect
// (fx_slot_editor.h). Q groups: the rows (blocks) of four controls of the effect on screen. While the chain is empty there is no effect to edit: the gallery of the effect types (fx_gallery.h) takes the place
// of the page, one tap on a card adds that effect.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"
#include "composite.h"
#include "core.h"
#include "fx_chain_bar.h"
#include "fx_gallery.h"
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
    FxGallery gallery;   // the page of an empty chain

    FxFullScreen()
    {
        addKid (chainBar);
        addKid (slotEditor);
        addKid (gallery, false);
    }

    Size minSize() const override { return { 1280, 628 }; }

    int activeGroup() const noexcept { return slotEditor.activeRow(); }
    void setActiveGroup (int g) { slotEditor.setActiveRow (g); }
    int numGroups() const noexcept { return slotEditor.numRows(); }
    void showEditor (bool s) { show (slotEditor, s); }
    bool editorShown() const noexcept { return editorShown_; }
    bool galleryShown() const noexcept { return ! editorShown_ && gallery.hasContent(); }
    /** The name of the Q group of a row on the page: the name of its block ("LOW BAND", "MODULATION"). */
    const char* groupName (int g) const noexcept { return slotEditor.rowTitle (g); }

    void assignIds (int chainBaseId, int editorBaseId)
    {
        chainBar.assignIds (chainBaseId);
        slotEditor.assignIds (editorBaseId, editorBaseId + 100, editorBaseId + 101, editorBaseId + 102, editorBaseId + 103);
    }

    Change turnQ (int knobIndex, double delta) { return slotEditor.turnQ (knobIndex, delta); }

    /** The owner says whether an effect is on the page (an empty chain has the gallery of the types instead). */
    void setEditorShown (bool s)
    {
        editorShown_ = s;
        show (slotEditor, s);
        show (gallery, ! s && gallery.hasContent());
    }

protected:
    void layout() override
    {
        const int pad = 8;
        chainBar.place ({ rect_.x + pad, rect_.y + pad, rect_.w - 2 * pad, kBarH });
        const int topY = rect_.y + pad + kBarH + pad;
        slotEditor.place ({ rect_.x, topY, rect_.w, std::max (100, rect_.bottom() - topY) });
        gallery.place ({ rect_.x + pad, topY, rect_.w - 2 * pad, std::max (100, rect_.bottom() - topY - pad) });
    }

private:
    bool editorShown_ = false;
};

}} // namespace synthyboy_mpc::ui
