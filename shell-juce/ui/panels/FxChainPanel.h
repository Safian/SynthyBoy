// SynthyBoy - UI panel: FX Chain & Racks (docs/ui-design/08-effects.md, page `fx`; docs/ui-design/18-FX-INSTANCES.md section 2.8).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The strips of the racks (FxRackView) and, below them, the panel of the selected pool module: the same panel class that edits the module of that type in
// instance 0, built with a UiContext whose key set maps its keys to the keys of the slot (FxKeySet).
#pragma once

#include "FxRackView.h"

namespace synthyboy::ui
{
class FxChainPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "FX Chain & Racks"; }

    std::vector<std::string> keys() const override
    {
        std::vector<std::string> k;
        k.reserve (16);
        for (int i = 1; i <= 16; ++i)
            k.push_back ("fx.order." + std::to_string (i));
        return k;
    }

    void build() override
    {
        visual = std::make_unique<FxRackView> (ctx);
        addAndMakeVisible (*visual);
        visual->onSelectionChanged = [this] { updateDetail(); };
        visual->onModelChanged = [this] { updateDetail(); };
        visual->onHeightChanged = [this] { notifyHeight(); };

        // At most 4 primary controls (principle 1 of brief)
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (i < 4)
                addControl (all[i]);
            else
                addMoreControl (all[i]);
        }
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visual->heightForWidth (width - 2 * t.gap()) + t.gap() + detailHeight (width)
               + const_cast<FxChainPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visual->heightForWidth (area.getWidth())));
            area.removeFromTop (ctx.theme.gap());
        }
        if (detail != nullptr)
        {
            area.removeFromTop (captionHeight());
            detail->panel->setBounds (area.removeFromTop (detail->panel->preferredHeight (area.getWidth())));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

    void paint (juce::Graphics& g) override
    {
        Panel::paint (g);
        if (detail != nullptr && visual != nullptr)
        {
            const auto& t = ctx.theme;
            g.setColour (t.colours.mod);
            g.setFont (t.font (t.px (11.0f), true));
            g.drawText ("SLOT " + juce::String (detail->slot + 1) + ":  " + juce::String (fxTypeName (detail->type)) + "  (a module of the instance pool, same controls as "
                            + juce::String (fxTypeName (detail->type)) + " in instance 0)",
                        juce::Rectangle<int> (t.gap(), visual->getBottom() + t.gap(), getWidth() - 2 * t.gap(), captionHeight()), juce::Justification::centredLeft, true);
        }
    }

    /** The panel of the selected pool module (nullptr when a module of instance 0 or nothing is selected). */
    Panel* slotPanel() noexcept { return detail != nullptr ? detail->panel.get() : nullptr; }
    int slotPanelSlot() const noexcept { return detail != nullptr ? detail->slot : -1; }
    FxRackView& rackView() noexcept { return *visual; }

private:
    struct Detail
    {
        Detail (UiContext& parent, int slotIn, int typeIn)
            : slot (slotIn), type (typeIn), context { parent.host, parent.theme, parent.assign, parent.view, std::make_shared<FxKeySet> (slotIn, typeIn) }
        {
        }
        int slot, type;
        UiContext context;
        std::unique_ptr<Panel> panel;
    };

    int captionHeight() const { return ctx.theme.ipx (22.0f); }

    int detailHeight (int width) const
    {
        return detail != nullptr ? captionHeight() + detail->panel->preferredHeight (width - 2 * ctx.theme.gap()) + ctx.theme.gap() : 0;
    }

    void notifyHeight()
    {
        if (onHeightChanged)
            onHeightChanged();
    }

    /** The panel below the strips follows the selection and the type of the selected slot. */
    void updateDetail()
    {
        int wantedSlot = -1, wantedType = kFxTypeNone;
        if (visual != nullptr && visual->selectedSlot() >= 0)
        {
            wantedSlot = visual->selectedSlot();
            wantedType = visual->typeOf ({ true, wantedSlot });
        }
        const int haveSlot = detail != nullptr ? detail->slot : -1;
        const int haveType = detail != nullptr ? detail->type : kFxTypeNone;
        if (wantedSlot == haveSlot && wantedType == haveType)
            return;

        if (detail != nullptr)
            removeChildComponent (detail->panel.get());
        detail.reset();
        if (wantedSlot >= 0 && wantedType != kFxTypeNone)
        {
            auto created = std::make_unique<Detail> (ctx, wantedSlot, wantedType);
            created->panel = PanelRegistry::instance().create (fxPanelIdOfType (wantedType), created->context);
            if (created->panel != nullptr)
            {
                created->panel->build();
                addAndMakeVisible (*created->panel);
                detail = std::move (created);
            }
        }
        notifyHeight();
        resized();
        repaint();
    }

    std::unique_ptr<FxRackView> visual;
    std::unique_ptr<Detail> detail;
};

} // namespace synthyboy::ui
