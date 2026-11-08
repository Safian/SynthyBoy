// SynthyBoy - UI panel: Mixer, bus routing, and Direct output (docs/ui-design/02-oscillators.md, page `mix`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "OscWaveView.h"

namespace synthyboy::ui
{
class MixerPanel final : public Panel
{
public:
    explicit MixerPanel (UiContext& context)
        : Panel (context),
          consoleView (context)
    {
        addAndMakeVisible (consoleView);
    }

    juce::String title() const override { return "Mixer"; }

    std::vector<std::string> keys() const override
    {
        return {
            // Primary controls (4 controls, 02-oscillators.md §2.4)
            "master.volume", "direct.level", "main.fx_bypass", "filter.routing",

            // Behind More (30 controls: buses, filter destinations & sends, osc filter blends & sends)
            "bus1.level", "bus1.destination", "bus1.fx_bypass",
            "bus2.level", "bus2.destination", "bus2.fx_bypass",
            "filter1.destination", "filter1.level", "filter1.pan", "filter1.mix", "filter1.bus1_send", "filter1.bus2_send",
            "filter2.destination", "filter2.level", "filter2.pan", "filter2.mix", "filter2.bus1_send", "filter2.bus2_send",
            "osc1.filter_blend", "osc1.bus1_send", "osc1.bus2_send",
            "osc2.filter_blend", "osc2.bus1_send", "osc2.bus2_send",
            "osc3.filter_blend", "osc3.bus1_send", "osc3.bus2_send",
            "noise.filter_blend", "noise.bus1_send", "noise.bus2_send"
        };
    }

    void build() override
    {
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
        const int visualH = t.ipx (180.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + visualH + 3 * t.gap() + const_cast<MixerPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (180.0f);
        auto area = contentArea();
        auto topArea = area.removeFromTop (visualH);
        consoleView.setBounds (topArea);
        area.removeFromTop (t.gap());
        layoutControls (area, false);
    }

private:
    OscMixConsoleView consoleView;
};

SYNTHYBOY_REGISTER_PANEL (MixerPanel, Page::mix, 10)
} // namespace synthyboy::ui
