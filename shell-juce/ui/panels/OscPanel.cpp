// SynthyBoy - UI panels: Oscillators 1-3 (docs/ui-design/02-oscillators.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "OscWaveView.h"

namespace synthyboy::ui
{
template <int OscIndex>
class OscPanelBase : public Panel
{
public:
    explicit OscPanelBase (UiContext& context)
        : Panel (context),
          waveView (context, OscIndex),
          onHandle (context, "osc" + std::to_string (OscIndex) + ".on")
    {
        addAndMakeVisible (waveView);
        onHandle.onChange = [this]
        {
            repaint();
            waveView.repaint();
        };
    }

    juce::String title() const override
    {
        return "Oscillator " + juce::String (OscIndex);
    }

    /** Every key of the oscillator panel, 4 primary controls first next to the visual (brief principle 1).
        All 64 keys of oscN in docs/params.json are reachable. */
    std::vector<std::string> keys() const override
    {
        const std::string o = "osc" + std::to_string (OscIndex);
        return {
            // Primary (<= 4 next to the visual, 02-oscillators.md §2.1)
            o + ".frame", o + ".warp.amount", o + ".morph.amount", o + ".level",

            // Behind More (60 controls: unison, pitch, phase, sample, warp 2, mapping, routing)
            o + ".on", o + ".type", o + ".wavetable", o + ".pan", o + ".transpose", o + ".tune",
            o + ".phase", o + ".random_phase", o + ".destination", o + ".filter_blend",
            o + ".bus1_send", o + ".bus2_send",
            o + ".unison.voices", o + ".unison.detune", o + ".unison.detune_curve",
            o + ".unison.spread", o + ".unison.blend", o + ".unison.stack", o + ".frame_spread",
            o + ".warp.type", o + ".warp.phase", o + ".warp.spread",
            o + ".warp2.type", o + ".warp2.amount", o + ".warp2.phase", o + ".warp2.spread",
            o + ".morph.type", o + ".morph.spread",
            o + ".map.key_low", o + ".map.key_high", o + ".map.vel_low", o + ".map.vel_high",
            o + ".map.key_fold", o + ".map.key_warp",
            o + ".sample.start", o + ".sample.end", o + ".sample.root", o + ".sample.keytrack",
            o + ".sample.loop_mode", o + ".sample.loop_start", o + ".sample.loop_end",
            o + ".sample.loop_xfade", o + ".sample.loop_relative", o + ".sample.loop_link",
            o + ".sample.loop_exit", o + ".sample.fade_edges", o + ".sample.normalize",
            o + ".sample.reverse", o + ".sample.slice_mode", o + ".sample.slice_threshold",
            o + ".sample.slice_play", o + ".sample.slice_root", o + ".sample.scan",
            o + ".sample.scan_range", o + ".sample.scan_reverse", o + ".sample.scan_lock_tempo",
            o + ".sample.scan_to_bpm", o + ".sample.length_beats", o + ".sample.uni_start",
            o + ".sample.uni_span"
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
        return headerHeight() + visualH + 3 * t.gap() + const_cast<OscPanelBase*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (180.0f);
        auto area = contentArea();
        auto topArea = area.removeFromTop (visualH);
        waveView.setBounds (topArea);
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
    OscWaveView waveView;
    ParamHandle onHandle;
};

class Osc1Panel final : public OscPanelBase<1> { using OscPanelBase<1>::OscPanelBase; };
class Osc2Panel final : public OscPanelBase<2> { using OscPanelBase<2>::OscPanelBase; };
class Osc3Panel final : public OscPanelBase<3> { using OscPanelBase<3>::OscPanelBase; };

SYNTHYBOY_REGISTER_PANEL (Osc1Panel, Page::osc, 10)
SYNTHYBOY_REGISTER_PANEL (Osc2Panel, Page::osc, 20)
SYNTHYBOY_REGISTER_PANEL (Osc3Panel, Page::osc, 30)
} // namespace synthyboy::ui
