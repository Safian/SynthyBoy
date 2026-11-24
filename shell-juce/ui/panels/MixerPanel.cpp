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
            "noise.filter_blend", "noise.bus1_send", "noise.bus2_send",

            // Block B21 (13-new-keys.md 4.22): the ENV 1 button of a Main / Direct channel (Serum 2 manual p.146, p.149)
            "osc1.env1", "osc2.env1", "osc3.env1", "sub.env1", "noise.env1", "filter1.env1", "filter2.env1"
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

        // the button belongs to a channel routed to Main or Direct: for every other destination it greys out (the manual shows it only for those)
        struct Channel { const char* env; const char* destination; int firstOk, lastOk; };
        for (const Channel& c : { Channel { "osc1.env1", "osc1.destination", 3, 4 }, Channel { "osc2.env1", "osc2.destination", 3, 4 },
                                   Channel { "osc3.env1", "osc3.destination", 3, 4 }, Channel { "sub.env1", "sub.destination", 3, 4 },
                                   Channel { "noise.env1", "noise.destination", 3, 4 }, Channel { "filter1.env1", "filter1.destination", 0, 1 },
                                   Channel { "filter2.env1", "filter2.destination", 0, 1 } })
        {
            auto handle = std::make_unique<ParamHandle> (ctx, c.destination);
            handle->onChange = [this] { updateEnv1Dimming(); };
            env1Handles.push_back ({ c.env, c.firstOk, c.lastOk, std::move (handle) });
        }
        updateEnv1Dimming();
    }

    void updateEnv1Dimming()
    {
        for (const auto& channel : env1Handles)
        {
            if (! channel.destination->valid())
                continue;
            const auto& info = channel.destination->info();
            const int value = juce::roundToInt (info.min + channel.destination->normalised() * (info.max - info.min));
            for (const auto& c : controls())
                if (c->key() == channel.env)
                    c->setAlpha (c->isAvailable() && value >= channel.firstOk && value <= channel.lastOk ? 1.0f : 0.4f);
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
    struct Env1Channel
    {
        std::string env;
        int firstOk, lastOk;
        std::unique_ptr<ParamHandle> destination;
    };
    std::vector<Env1Channel> env1Handles;
    OscMixConsoleView consoleView;
};

SYNTHYBOY_REGISTER_PANEL (MixerPanel, Page::mix, 10)
} // namespace synthyboy::ui
