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
        setNativeControls (true);
    }

    void setHeaderInset (int w) { headerInset_ = w; layout(); }

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

    void setNativeControls (bool native) noexcept
    {
        nativeControls_ = native;
        slotEditor.setNativeControls (native);
        chainBar.nativeGround = native;
        setNativeChain (native);
    }
    bool nativeControls() const noexcept { return nativeControls_; }
    void setNativeChain (bool native)
    {
        if (native == nativeChain_)
            return;
        nativeChain_ = native;
        chainBar.setNativeWidgets (native);
        gallery.setNativeCards (native);
        slotEditor.setChainInset (native ? FxChainBar::kNativeHeight - kBarH : 0);
        layout();
    }
    void setPeekInset (int px)
    {
        slotEditor.setPeekInset (px);
        layout();
    }

    bool isNativeControl (int x, int y) const noexcept
    {
        if (! nativeControls_ || ! editorShown_)
            return false;
        if (chainBar.rect().contains (x, y))
            return false;
        return slotEditor.isNativeControl (x, y);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (isNativeControl (x, y))
            return Change::none();
        return Composite::touchDown (x, y, nowMs);
    }

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
        const int barH = nativeChain_ ? FxChainBar::kNativeHeight : kBarH;
        chainBar.place ({ rect_.x + pad + headerInset_, rect_.y + pad, rect_.w - 2 * pad - headerInset_, barH });
        const int topY = rect_.y + pad + barH + pad;
        slotEditor.place ({ rect_.x, topY, rect_.w, std::max (100, rect_.bottom() - topY) });
        gallery.place ({ rect_.x + pad, topY, rect_.w - 2 * pad, std::max (100, rect_.bottom() - topY - pad) });
    }

private:
    int headerInset_ = 0;   // a Mixer panel reserves room for its CLOSE button
    bool editorShown_ = false;
    bool nativeControls_ = true;
    bool nativeChain_ = false;
};

}} // namespace synthyboy_mpc::ui
