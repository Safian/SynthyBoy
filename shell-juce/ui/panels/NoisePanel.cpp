// SynthyBoy - UI panel: Noise generator (docs/ui-design/02-oscillators.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "OscWaveView.h"

namespace synthyboy::ui
{
class NoisePanel final : public Panel
{
public:
    explicit NoisePanel (UiContext& context)
        : Panel (context),
          noiseView (context),
          onHandle (context, "noise.on")
    {
        addAndMakeVisible (noiseView);
        onHandle.onChange = [this]
        {
            repaint();
            noiseView.repaint();
        };
    }

    juce::String title() const override { return "Noise"; }

    std::vector<std::string> keys() const override
    {
        return {
            // Primary (2 controls next to the visual, 02-oscillators.md §2.3)
            "noise.level", "noise.pan",

            // Behind More (5 controls)
            "noise.on", "noise.destination", "noise.filter_blend", "noise.bus1_send", "noise.bus2_send"
        };
    }

    void build() override
    {
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (i < 2)
                addControl (all[i]);
            else
                addMoreControl (all[i]);
        }
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (120.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + visualH + 3 * t.gap() + const_cast<NoisePanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (120.0f);
        auto area = contentArea();
        auto topArea = area.removeFromTop (visualH);
        noiseView.setBounds (topArea);
        area.removeFromTop (t.gap());
        layoutControls (area, false);
    }

    void paint (juce::Graphics& g) override
    {
        Panel::paint (g);
        if (onHandle.valid() && onHandle.plain() < 0.5f)
        {
            const auto& t = ctx.theme;
            g.setFont (t.labelFont());
            g.setColour (t.colours.warn);
            g.drawText ("[MUTED / OFF]", getLocalBounds().reduced (t.gap(), 0).withHeight (headerHeight() + t.gap() / 2),
                        juce::Justification::centredRight, false);
        }
    }

private:
    OscNoiseView noiseView;
    ParamHandle onHandle;
};

SYNTHYBOY_REGISTER_PANEL (NoisePanel, Page::osc, 40)
} // namespace synthyboy::ui
