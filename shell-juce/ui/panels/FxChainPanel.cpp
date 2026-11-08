// SynthyBoy - UI panel: FX Chain & Racks (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxChainView.h"

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
        visual = std::make_unique<FxChainView> (ctx);
        addAndMakeVisible (*visual);

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
        const int visualH = t.ipx (140.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxChainPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (140.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxChainView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxChainPanel, Page::fx, 10)
} // namespace synthyboy::ui
