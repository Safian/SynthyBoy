// SynthyBoy - UI panels: Oscillators 1-3 (docs/ui-design/02-oscillators.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "OscWaveView.h"
#include "TransposeControl.h"

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
            updateDimming();
            repaint();
            waveView.repaint();
        };
        // the swipe on the view (two fingers) goes to the next / previous oscillator, like the chips
        waveView.onSwitchOscillator = [this] (int direction)
        {
            ctx.view->oscTab = juce::jlimit (0, 2, ctx.view->oscTab + direction);
            if (onHeightChanged)
                onHeightChanged();
        };
        updateDimming();
    }

    /** Only the oscillator chosen on the chip row takes part in the layout. */
    bool hiddenInLayout() const override { return ctx.view->oscTab != OscIndex - 1; }
    std::string modulePrefix() const override { return "osc" + std::to_string (OscIndex); }
    OscWaveView& view() noexcept { return waveView; }

    /** Power dot off: the whole panel dims (design 02 section 3; the controls stay editable). */
    void updateDimming()
    {
        setAlpha (onHandle.valid() && onHandle.normalised() < 0.5f ? 0.4f : 1.0f); // normalised (): the listener runs before the atomic updates
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
            o + ".sample.uni_span",
            // Block B14: Pitch & unison modes, transpose snap, phase legato (76 keys total)
            o + ".pitch_mode", o + ".pitch_coarse", o + ".pitch_track", o + ".fixed_pitch",
            o + ".pitch_bend_track", o + ".transpose_snap",
            o + ".unison.mode", o + ".unison.range", o + ".unison.blend_shape", o + ".unison.stack_repeat",
            o + ".phase_legato", o + ".phase_memory"
        };
    }

    void build() override
    {
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (i < 4)
                addControl (all[i]);
            else if (all[i] == "osc" + std::to_string (OscIndex) + ".transpose")
                addMoreControl (std::make_unique<TransposeControl> (ctx, all[i])); // OCT segments + SEM wheel
            else if (all[i] == "osc" + std::to_string (OscIndex) + ".transpose_snap")
                addMoreControl (std::make_unique<TransposeSnapControl> (ctx, all[i])); // Row 02-10 Transpose snap keyboard
            else if (auto& control = addMoreControl (all[i]); all[i] == "osc" + std::to_string (OscIndex) + ".warp.type")
            {
                if (auto* choice = dynamic_cast<ParamChoice*> (&control))
                    choice->setGroups (warpTypeGroups()); // Basic / Alt / FM / RM, the groups without a mode greyed
            }
        }

        // what the Serum 2 manual ties to other controls (p.35-36, p.47): the fixed pitch is the pitch of a wavetable oscillator with pitch tracking off,
        // the pitch bend switch is for the sample types with pitch tracking off, the stack repeat (1-3x) belongs to the Octaves / Power Chord stacks
        const std::string o = "osc" + std::to_string (OscIndex);
        for (const char* suffix : { ".pitch_track", ".type", ".unison.stack" })
        {
            auto handle = std::make_unique<ParamHandle> (ctx, o + suffix);
            handle->onChange = [this] { updateControlDimming(); };
            dimHandles.push_back (std::move (handle));
        }
        updateControlDimming();
    }

    void updateControlDimming()
    {
        const auto choiceOf = [] (ParamHandle& h)
        {
            // (read the parameter itself: the raw atomic of the APVTS is updated after the callbacks)
            return h.valid() ? juce::roundToInt (h.info().min + h.normalised() * (h.info().max - h.info().min)) : 0;
        };
        if (dimHandles.size() < 3)
            return;
        const bool tracking = choiceOf (*dimHandles[0]) != 0;
        const int type = choiceOf (*dimHandles[1]);
        const int stack = choiceOf (*dimHandles[2]);
        const std::string o = "osc" + std::to_string (OscIndex);
        for (const auto& c : controls())
        {
            const auto& key = c->key();
            bool applies = true;
            if (key == o + ".fixed_pitch")
                applies = ! tracking && type == 0;
            else if (key == o + ".pitch_bend_track")
                applies = ! tracking && type == 1;
            else if (key == o + ".unison.stack_repeat")
                applies = stack == 3 || stack == 5;
            c->setAlpha (c->isAvailable() && applies ? 1.0f : 0.4f);
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
            g.drawText ("[MUTED / OFF]",
                        getLocalBounds().reduced (t.gap(), 0).withTrimmedRight (t.touch()).withHeight (headerHeight() + t.gap() / 2),
                        juce::Justification::centredRight, false);
        }
    }

private:
    OscWaveView waveView;
    ParamHandle onHandle;
    std::vector<std::unique_ptr<ParamHandle>> dimHandles;
};

class Osc1Panel final : public OscPanelBase<1> { using OscPanelBase<1>::OscPanelBase; };
class Osc2Panel final : public OscPanelBase<2> { using OscPanelBase<2>::OscPanelBase; };
class Osc3Panel final : public OscPanelBase<3> { using OscPanelBase<3>::OscPanelBase; };

SYNTHYBOY_REGISTER_PANEL (Osc1Panel, Page::osc, 10)
SYNTHYBOY_REGISTER_PANEL (Osc2Panel, Page::osc, 20)
SYNTHYBOY_REGISTER_PANEL (Osc3Panel, Page::osc, 30)
} // namespace synthyboy::ui
